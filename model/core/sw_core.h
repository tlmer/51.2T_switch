//============================================================================
// sw_core.h — 51.2T 交换机 TLM 的交换核(行为级)  [2026-10-08 建 / M2:无损 PFC]
//
// 规格 = ../设计方案.md §2.2;判据 = ../验证方案.md §2-L2
// 演进:
//   M0(骨架):每口收帧/字节计数,不转发 ✓
//   M1:L2 学表/查表转发(单播命中/未知单播泛洪/广播·组播)+ 出队发射 + TX 计数 ✓
//   M2(本版):★ **无损(PFC)** ——(a)**8 类队列**(按 VLAN PCP 映射;未带标签 ⇒ 类 0)
//     (b)**水位/阈值**:`xoff_bytes/xon_bytes/max_bytes`(每口每类)
//     (c)**pfc_gen**:过 XOFF ⇒ 生成 **PFC(0x8808/0x0101)** 帧从**该口**发向上游;落 XON ⇒ 发 XON(t=0);
//        连续 XOFF 按 `pfc_refresh` 节奏重发 ✓
//     (d)**pfc_rsp**:收到 PFC ⇒ **关该口对应类的出队闸门**(time×quantum_cycles 计时期);收 XON ⇒ 开;
//        控制帧**绕过闸门、最高优先**出队(防死锁)✓
//     (e)**丢满非静默**:入队过 `max_bytes` ⇒ 丢弃计数 ✓
//   M3:ECN/WRED 标记
// ⚠ 默认 **pfc_enable=false + max_bytes 极大** ⇒ M1 各台行为**零变化**(回归护栏)✓
//
// ★★ 核→MAC(cmac_m_axis)握手铁律(2026-10-08 自 mac_tlm.cpp ②/④ 段实读推出,**勿凭感觉改**):
//   · MAC 在 T_IDLE 内 **每拍吃一次 tvalid=1 的呈现**("不撤=每拍重吃同一拍")⇒ 本核**每拍换新值或撤**;
//   · tlast 被吃后 MAC 转 **T_EMIT**(tready=0;期间忽略 tvalid ⇒ 呈现的拍丢失)⇒ 规矩 = **tlast 后 1 拍气泡,
//     再按「本拍读到的 tready」起下一帧** ✓
// ★ 核收 MAC(roce_cmac_s_axis)= **无 ready 恒收**:每拍采样 ✓
// ★ 计数口径:o_rx_frames/o_tx_frames = **数据帧**;控制帧(PFC)另计 o_pfc_tx/o_pfc_rx ✓
// ★ 队列可达性:出队 = **控制队列(最高优先)> 类 7..0 严格优先**;⛔ 单口无类间 HOL 阻塞 ✓
//============================================================================
#ifndef SW_CORE_H
#define SW_CORE_H

#include <systemc.h>
#include <vector>
#include <deque>

class SwCore : public sc_module {
public:
    const int N;
    static const int NC = 8;                                // 类数(8,照设计册;PCP ⇒ 类)✓

    // ---- 线→核(MAC roce_cmac_s_axis;无 ready,恒收) ----
    std::vector<sc_in<bool>*>               rx_tvalid;
    std::vector<sc_in<sc_biguint<2048>>*>   rx_tdata;
    std::vector<sc_in<sc_biguint<256>>*>    rx_tkeep;
    std::vector<sc_in<bool>*>               rx_tlast;
    // ---- 核→线(MAC cmac_m_axis;有 ready) ----
    std::vector<sc_out<bool>*>              tx_tvalid;
    std::vector<sc_out<sc_biguint<2048>>*>  tx_tdata;
    std::vector<sc_out<sc_biguint<256>>*>   tx_tkeep;
    std::vector<sc_out<bool>*>              tx_tlast;
    std::vector<sc_in<bool>*>               tx_ready;
    // ---- 计数(face F5;累加器) ----
    std::vector<sc_out<sc_uint<32>>*>       o_rx_frames;    // 每口收帧数(数据;入核)
    std::vector<sc_out<sc_uint<32>>*>       o_rx_bytes;
    std::vector<sc_out<sc_uint<32>>*>       o_tx_frames;    // 每口发帧数(数据;核→MAC 交完)
    std::vector<sc_out<sc_uint<32>>*>       o_tx_bytes;
    std::vector<sc_out<sc_uint<32>>*>       o_drop_frames;  // 丢弃(分因:不成帧/同口过滤/满溢)
    std::vector<sc_out<sc_uint<32>>*>       o_pfc_tx;       // ★ M2:PFC 帧发出数
    std::vector<sc_out<sc_uint<32>>*>       o_pfc_rx;       // ★ M2:PFC 帧收到数
    std::vector<sc_out<sc_uint<32>>*>       o_ecn_marked;   // ★ M3:出口 CE 标记帧数(逐帧位级可检)
    std::vector<sc_out<sc_uint<32>>*>       o_cnp_rx;       // ★ M3:CNP 味收帧数(IP/UDP4791/BTH 0x81)
    std::vector<sc_out<sc_uint<32>>*>       o_qmax;         // ★ M4:每口排队**高水位**(字节,含各类合计)

    // ---- ★ M2 参数(公开;TB 按需设;默认 = M1 行为)----
    bool         pfc_enable      = false;                   // 默认关(护栏)
    unsigned int xoff_bytes      = 4096;                    // 每口每类:过 ⇒ 发 XOFF
    unsigned int xon_bytes       = 2048;                    // 落 ⇒ 发 XON(须曾发过 XOFF)
    unsigned int max_bytes       = (1u << 20);              // 每口每类上限;过 ⇒ 丢(非静默)
    unsigned int pfc_quantum     = 0xFFFF;                  // XOFF 帧 time 字段(quanta)
    unsigned int pfc_refresh     = 256;                     // XOFF 持续期重发节奏(拍)
    unsigned int pfc_quantum_cycles = 1;                    // 1 quantum = N 拍(计时期)
    void set_pfc(bool en) { pfc_enable = en; }
    void set_wm(unsigned int xoff, unsigned int xon, unsigned int mx) { xoff_bytes=xoff; xon_bytes=xon; max_bytes=mx; }
    // ---- ★ M3 参数(默认关;阈值化 WRED 味:入队时 occ > ecn_min ⇒ 置 CE(11))----
    bool         ecn_enable = false;
    unsigned int ecn_min_bytes = 1024;                      // 单阈值档(⛔ 概率档 maxp 未做;行为级口径)
    void set_ecn(bool en, unsigned int mn) { ecn_enable = en; ecn_min_bytes = mn; }
    // ---- ★ VLAN 域隔离(默认关 ⇒ M1~M4 行为零变化;开 ⇒ 带签帧仅在**成员口**出,非成员丢+计数)----
    //   口径:未带签帧**旁路**过滤(照 M1);VID = 802.1Q TCI 低 12 位 ✓
    bool vlan_enable = false;
    void set_vlan(bool en) { vlan_enable = en; }
    void set_vlan_member(int port, int vid, bool m) {
        if (port >= 0 && port < N && vid >= 0 && vid < 4096) vlan_mem[port][vid] = m ? 1 : 0;
    }

    SC_HAS_PROCESS(SwCore);
    SwCore(sc_core::sc_module_name nm, int n) : sc_module(nm), N(n) {
        for (int i = 0; i < N; i++) {
            rx_tvalid.push_back(new sc_in<bool>(sc_gen_unique_name("rx_tvalid")));
            rx_tdata .push_back(new sc_in<sc_biguint<2048>>(sc_gen_unique_name("rx_tdata")));
            rx_tkeep .push_back(new sc_in<sc_biguint<256>>(sc_gen_unique_name("rx_tkeep")));
            rx_tlast .push_back(new sc_in<bool>(sc_gen_unique_name("rx_tlast")));
            tx_tvalid.push_back(new sc_out<bool>(sc_gen_unique_name("tx_tvalid")));
            tx_tdata .push_back(new sc_out<sc_biguint<2048>>(sc_gen_unique_name("tx_tdata")));
            tx_tkeep .push_back(new sc_out<sc_biguint<256>>(sc_gen_unique_name("tx_tkeep")));
            tx_tlast .push_back(new sc_out<bool>(sc_gen_unique_name("tx_tlast")));
            tx_ready .push_back(new sc_in<bool>(sc_gen_unique_name("tx_ready")));
            o_rx_frames.push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_rx_frames")));
            o_rx_bytes .push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_rx_bytes")));
            o_tx_frames.push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_tx_frames")));
            o_tx_bytes .push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_tx_bytes")));
            o_drop_frames.push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_drop_frames")));
            o_pfc_tx.push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_pfc_tx")));
            o_pfc_rx.push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_pfc_rx")));
            o_ecn_marked.push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_ecn_marked")));
            o_cnp_rx.push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_cnp_rx")));
            o_qmax.push_back(new sc_out<sc_uint<32>>(sc_gen_unique_name("o_qmax")));
            f_rx[i] = f_by[i] = f_tx[i] = f_ty[i] = f_dp[i] = f_ptx[i] = f_prx[i] = 0;
            f_ecn[i] = f_cnp[i] = 0; f_qmax[i] = 0;
        }
        rx_buf.resize(N); rx_ovf.assign(N, false);
        tx_q.resize(N); ctrl_q.resize(N); txst.resize(N);
        vlan_mem.assign(N, std::vector<unsigned char>(4096, 0));   // ★ VLAN 成员表(默认全非成员;按需 set)
        for (int i = 0; i < N; i++) tx_q[i].resize(NC);
        occ.assign(N, std::vector<unsigned int>(NC, 0));
        gated.assign(N, std::vector<char>(NC, 0));
        gate_until.assign(N, std::vector<unsigned long>(NC, 0));
        pfc_st.assign(N, std::vector<char>(NC, 0));
        pfc_last.assign(N, std::vector<unsigned long>(NC, 0));
        SC_METHOD(core_step);
        sensitive << clk.pos();
        dont_initialize();
    }
    sc_in<bool> clk;
    sc_in<bool> rst_n;

private:
    static const int AXIS_BYTES = 256;
    static const int MAX_FRAME  = 4096;

    struct L2Ent { unsigned char mac[6]; int port; };
    struct TxFsm { bool active; int off; int bubble; std::vector<unsigned char> cur; bool is_ctrl; };

    std::vector<std::vector<unsigned char> > rx_buf;
    std::vector<char>                        rx_ovf;
    std::vector<std::vector<std::deque<std::vector<unsigned char> > > > tx_q;   // [N][NC]
    std::vector<std::deque<std::vector<unsigned char> > > ctrl_q;               // [N] 控制帧(PFC)
    std::vector<TxFsm>                       txst;
    std::vector<L2Ent>                       l2;
    std::vector<std::vector<unsigned char> > vlan_mem;     // [N][4096] VLAN 成员
    std::vector<std::vector<unsigned int> >  occ;          // [N][NC] 排队字节
    std::vector<std::vector<char> >          gated;        // [N][NC] 闸门关?
    std::vector<std::vector<unsigned long> > gate_until;   // [N][NC] 到期拍
    std::vector<std::vector<char> >          pfc_st;       // [N][NC] 0=idle 1=xoff 已发
    std::vector<std::vector<unsigned long> > pfc_last;     // [N][NC] 上次发拍
    unsigned long cycle_cnt = 0;
    unsigned int f_rx[64], f_by[64], f_tx[64], f_ty[64], f_dp[64], f_ptx[64], f_prx[64];
    unsigned int f_ecn[64], f_cnp[64], f_qmax[64];

    // ---- 工具 ----
    static bool mac_eq(const unsigned char* a, const unsigned char* b) {
        for (int i = 0; i < 6; i++) if (a[i] != b[i]) return false;
        return true;
    }
    // 帧类别:带 802.1Q 标签(0x8100)⇒ PCP(3b);否则类 0 ✓
    static int frame_class(const std::vector<unsigned char>& fr) {
        if (fr.size() >= 15 && fr[12] == 0x81 && fr[13] == 0x00) return (fr[14] >> 5) & 0x7;
        return 0;
    }
    // ★ VLAN:VID(802.1Q TCI 低 12 位);未带签 ⇒ -1(旁路过滤)✓
    static int frame_vid(const std::vector<unsigned char>& fr) {
        if (fr.size() >= 16 && fr[12] == 0x81 && fr[13] == 0x00)
            return ((fr[14] & 0x0f) << 8) | fr[15];
        return -1;
    }
    // PFC?以太网 MAC 控制:0x8808 + opcode 0x0101(802.1Qbb F3 简版:开关量 + 标准位级字段)✓
    static bool is_pfc(const std::vector<unsigned char>& fr) {
        return fr.size() >= 34 && fr[12] == 0x88 && fr[13] == 0x08 && fr[14] == 0x01 && fr[15] == 0x01;
    }
    static void pfc_build(int port, int cls, bool xoff, unsigned int quantum,
                          std::vector<unsigned char>& out) {
        out.assign(34, 0);
        const unsigned char DST[6] = {0x01,0x80,0xC2,0x00,0x00,0x01};   // 标准 PFC 组播 MAC
        for (int i = 0; i < 6; i++) out[i] = DST[i];
        out[6]=0x02; out[7]=0x00; out[8]=0x00; out[9]=0x00; out[10]=0x00; out[11]=(unsigned char)(0x10+port);
        out[12]=0x88; out[13]=0x08;                                      // EtherType = MAC Control
        out[14]=0x01; out[15]=0x01;                                      // opcode = PFC
        unsigned int vec = (1u << cls);                                  // class-enable-vector(bit0=类0);★F3 简版:位=本帧就该类表态
        out[16]=(unsigned char)((vec >> 8) & 0xff); out[17]=(unsigned char)(vec & 0xff);
        for (int k = 0; k < 8; k++) {                                    // 8 × time(quanta,大端)
            unsigned int t = (xoff && k == cls) ? quantum : 0u;
            out[18 + k*2] = (unsigned char)((t >> 8) & 0xff);
            out[19 + k*2] = (unsigned char)(t & 0xff);
        }
    }

    // ---- L2 表 ----
    int  l2_lookup(const unsigned char* m) {
        for (size_t i = 0; i < l2.size(); i++) if (mac_eq(l2[i].mac, m)) return l2[i].port;
        return -1;
    }
    void l2_learn(const unsigned char* m, int port) {
        for (size_t i = 0; i < l2.size(); i++) if (mac_eq(l2[i].mac, m)) { l2[i].port = port; return; }
        L2Ent e; for (int i = 0; i < 6; i++) e.mac[i] = m[i]; e.port = port; l2.push_back(e);
    }

    // ---- ★ M3:IPv4 TOS 字节偏移(-1 = 非 IPv4 或形状不识别)----
    //   直连以太网(0x0800)⇒ TOS 在 [15];802.1Q 标签(0x8100+0x0800)⇒ TOS 在 [19] ✓
    static int ip_tos_off(const std::vector<unsigned char>& fr) {
        if (fr.size() < 20) return -1;
        if (fr[12] == 0x08 && fr[13] == 0x00) return 15;
        if (fr[12] == 0x81 && fr[13] == 0x00 && fr.size() >= 24 && fr[16] == 0x08 && fr[17] == 0x00) return 19;
        return -1;
    }
    // ★ M3:CNP 味判据(明写口径,不发明):IPv4 ∧ proto=17 ∧ UDP dport=4791 ∧ BTH opcode=0x81 ✓
    static bool is_ipv4_cnp(const std::vector<unsigned char>& fr) {
        int t = ip_tos_off(fr); if (t < 0) return false;
        int ip0 = t - 1;
        if ((fr[ip0] >> 4) != 4) return false;
        int ihl = (fr[ip0] & 0x0f) * 4;
        if (ihl < 20 || fr.size() < (size_t)(ip0 + ihl + 9)) return false;
        if (fr[ip0 + 9] != 17) return false;                       // IP proto = UDP
        int udp0 = ip0 + ihl;
        if ((((unsigned int)fr[udp0+2] << 8) | fr[udp0+3]) != 4791) return false;   // dport = RoCEv2
        if (fr[udp0 + 8] != 0x81) return false;                    // BTH opcode = CNP
        return true;
    }

    // ---- 入队(类队列;满 ⇒ 丢 + 计数;★M3:occ > ecn_min ⇒ 入队时置 CE(11);★VLAN:非成员 ⇒ 丢)----
    void enqueue(int p, int c, const std::vector<unsigned char>& fr) {
        if (occ[p][c] + fr.size() > max_bytes) { f_dp[p]++; return; }   // 满溢丢弃(非静默)✓
        if (vlan_enable) {                                              // ★ VLAN 域隔离(带签帧)
            int vid = frame_vid(fr);
            if (vid >= 0 && !vlan_mem[p][vid]) { f_dp[p]++; return; }   // 非成员 ⇒ 丢 + 计数(不静默)✓
        }
        if (ecn_enable && occ[p][c] > ecn_min_bytes) {
            int t = ip_tos_off(fr);
            if (t >= 0) {                                              // 有 IP 头才可标(否则原样过)
                std::vector<unsigned char> mf = fr;
                mf[t] = (unsigned char)((mf[t] & 0xFC) | 0x03);        // ECN 两位 = CE(11) ✓
                tx_q[p][c].push_back(mf);
                f_ecn[p]++;
                occ[p][c] += (unsigned)fr.size();
                return;
            }
        }
        tx_q[p][c].push_back(fr);
        occ[p][c] += (unsigned)fr.size();
    }

    // ---- 收全一帧(PFC ⇒ 闸门;数据 ⇒ L2)----
    void ingest(int ing) {
        std::vector<unsigned char>& fr = rx_buf[ing];
        if (rx_ovf[ing]) { f_dp[ing]++; rx_ovf[ing] = false; fr.clear(); return; }
        if (is_pfc(fr)) {                                              // ★ PFC:消费(不转发/不学表)
            f_prx[ing]++;
            unsigned int vec = ((unsigned int)fr[16] << 8) | fr[17];
            for (int k = 0; k < 8; k++) {
                if (!(vec & (1u << k))) continue;                      // ★F3 简版:位=0 ⇒ 本类不动
                unsigned int t = ((unsigned int)fr[18 + k*2] << 8) | fr[19 + k*2];
                if (t > 0) {                                           // 表态 XOFF ⇒ 关闸门(计时期)
                    gated[ing][k] = 1;
                    gate_until[ing][k] = cycle_cnt + (unsigned long)t * pfc_quantum_cycles;
                } else {                                               // 表态 XON(t=0)⇒ 开闸门
                    gated[ing][k] = 0;
                }
            }
            fr.clear(); return;
        }
        if (is_ipv4_cnp(fr)) f_cnp[ing]++;                         // ★ M3:CNP 味计数(照常转发)✓
        if (fr.size() >= 14) {
            l2_learn(&fr[6], ing);
            int cls = frame_class(fr);
            bool bc = true;
            for (int i = 0; i < 6; i++) if (fr[i] != 0xff) { bc = false; break; }
            bool mc = (fr[0] & 0x01) != 0;
            if (bc || mc) {
                for (int p = 0; p < N; p++) if (p != ing) enqueue(p, cls, fr);
            } else {
                int hit = l2_lookup(&fr[0]);
                if (hit == ing) f_dp[ing]++;
                else if (hit >= 0) enqueue(hit, cls, fr);
                else for (int p = 0; p < N; p++) if (p != ing) enqueue(p, cls, fr);
            }
        } else f_dp[ing]++;
        fr.clear();
    }

    // ---- 水位评估:PFC 生成(XOFF 跨阈 / 落 XON / XOFF 期刷新)----
    void pfc_eval(int p) {
        for (int c = 0; c < NC; c++) {
            unsigned int o = occ[p][c];
            if (pfc_st[p][c] == 0) {
                if (o > xoff_bytes) {                                  // 跨 XOFF ⇒ 发一次
                    std::vector<unsigned char> f; pfc_build(p, c, true, pfc_quantum, f);
                    ctrl_q[p].push_back(f);
                    pfc_st[p][c] = 1; pfc_last[p][c] = cycle_cnt;
                }
            } else {
                if (o <= xon_bytes) {                                  // 落 XON ⇒ 发 XON(t=0)
                    std::vector<unsigned char> f; pfc_build(p, c, false, 0, f);
                    ctrl_q[p].push_back(f);
                    pfc_st[p][c] = 0;
                } else if (cycle_cnt - pfc_last[p][c] >= pfc_refresh) { // 持续过阈 ⇒ 按节奏重发
                    std::vector<unsigned char> f; pfc_build(p, c, true, pfc_quantum, f);
                    ctrl_q[p].push_back(f);
                    pfc_last[p][c] = cycle_cnt;
                }
            }
        }
    }

    // ---- 出队源选择:控制队列 > 类 7..0(严格优先;闸门关 ⇒ 跳过)----
    bool pop_next(int p, std::vector<unsigned char>& out, bool& is_ctrl) {
        if (!ctrl_q[p].empty()) { out.swap(ctrl_q[p].front()); ctrl_q[p].pop_front(); is_ctrl = true; return true; }
        for (int c = NC - 1; c >= 0; c--) {
            bool open = (!gated[p][c]) || (cycle_cnt >= gate_until[p][c]);
            if (gated[p][c] && open) gated[p][c] = 0;                  // 计时期过 ⇒ 自开
            if (!open) continue;
            if (!tx_q[p][c].empty()) {
                out.swap(tx_q[p][c].front()); tx_q[p][c].pop_front(); is_ctrl = false; return true;
            }
        }
        return false;
    }

    // ---- 每拍一步 ----
    void core_step() {
        if (!rst_n.read()) {
            for (int i = 0; i < N; i++) {
                rx_buf[i].clear(); rx_ovf[i] = false;
                ctrl_q[i].clear();
                for (int c = 0; c < NC; c++) { tx_q[i][c].clear(); occ[i][c] = 0; gated[i][c] = 0; gate_until[i][c] = 0; pfc_st[i][c] = 0; pfc_last[i][c] = 0; }
                txst[i].active = false; txst[i].off = 0; txst[i].bubble = 0; txst[i].cur.clear(); txst[i].is_ctrl = false;
                f_rx[i] = f_by[i] = f_tx[i] = f_ty[i] = f_dp[i] = f_ptx[i] = f_prx[i] = 0;
                f_ecn[i] = f_cnp[i] = 0; f_qmax[i] = 0;
                o_rx_frames[i]->write(0); o_rx_bytes[i]->write(0);
                o_tx_frames[i]->write(0); o_tx_bytes[i]->write(0); o_drop_frames[i]->write(0);
                o_pfc_tx[i]->write(0); o_pfc_rx[i]->write(0);
                o_ecn_marked[i]->write(0); o_cnp_rx[i]->write(0); o_qmax[i]->write(0);
                tx_tvalid[i]->write(false); tx_tlast[i]->write(false);
                tx_tdata[i]->write(0); tx_tkeep[i]->write(0);
            }
            l2.clear(); cycle_cnt = 0;
            return;
        }
        cycle_cnt++;

        // ① 收:每拍采样(MAC→核 无 ready);tlast ⇒ 帧齐 ⇒ ingest
        for (int i = 0; i < N; i++) {
            if (rx_tvalid[i]->read()) {
                sc_biguint<2048> d = rx_tdata[i]->read();
                sc_biguint<256>  k = rx_tkeep[i]->read();
                std::vector<unsigned char>& fr = rx_buf[i];
                for (int j = 0; j < AXIS_BYTES; j++) {
                    if (!k[j].to_bool()) continue;
                    f_by[i]++;
                    if ((int)fr.size() < MAX_FRAME) fr.push_back((unsigned char)d.range(j*8+7, j*8).to_uint());
                    else rx_ovf[i] = true;
                }
                if (rx_tlast[i]->read()) { f_rx[i]++; ingest(i); }
            }
        }

        // ② 水位 ⇒ PFC 生成(M2;默认关)
        if (pfc_enable) for (int i = 0; i < N; i++) pfc_eval(i);

        // ③ 发:出队发射(★ 握手铁律:tlast 后 1 拍气泡 + 按本拍 tready 门)
        for (int i = 0; i < N; i++) {
            TxFsm& t = txst[i];
            bool rdy = tx_ready[i]->read();
            if (!t.active) {
                if (t.bubble > 0) {
                    t.bubble--;
                    tx_tvalid[i]->write(false); tx_tlast[i]->write(false);
                } else if (rdy && pop_next(i, t.cur, t.is_ctrl)) {
                    t.off = 0; t.active = true;
                }
            }
            if (t.active) {
                if (!rdy) {
                    tx_tvalid[i]->write(false); tx_tlast[i]->write(false);
                } else {
                    int len = (int)t.cur.size();
                    int nb  = (len - t.off > AXIS_BYTES) ? AXIS_BYTES : (len - t.off);
                    sc_biguint<2048> d = 0; sc_biguint<256> k = 0;
                    for (int j = 0; j < nb; j++) {
                        d.range(j*8+7, j*8) = t.cur[t.off + j];
                        k[j] = true;
                    }
                    bool last = (t.off + nb >= len);
                    tx_tdata[i]->write(d); tx_tkeep[i]->write(k);
                    tx_tvalid[i]->write(true); tx_tlast[i]->write(last);
                    t.off += nb;
                    if (last) {
                        t.active = false; t.bubble = 1;
                        if (t.is_ctrl) { f_ptx[i]++; }                     // 控制帧:发完才计(同数据口口径)
                        else { f_tx[i]++; f_ty[i] += (unsigned)len;
                               int c = frame_class(t.cur);
                               if (occ[i][c] >= (unsigned)len) occ[i][c] -= (unsigned)len; }
                        t.cur.clear();
                    }
                }
            }
        }

        // ④ 计数出线 + 高水位(每口各类合计;★M4)
        for (int i = 0; i < N; i++) {
            unsigned int tot = 0;
            for (int c = 0; c < NC; c++) tot += occ[i][c];
            if (tot > f_qmax[i]) f_qmax[i] = tot;
            o_rx_frames[i]->write(f_rx[i]);
            o_rx_bytes[i]->write(f_by[i]);
            o_tx_frames[i]->write(f_tx[i]);
            o_tx_bytes[i]->write(f_ty[i]);
            o_drop_frames[i]->write(f_dp[i]);
            o_pfc_tx[i]->write(f_ptx[i]);
            o_pfc_rx[i]->write(f_prx[i]);
            o_ecn_marked[i]->write(f_ecn[i]);
            o_cnp_rx[i]->write(f_cnp[i]);
            o_qmax[i]->write(f_qmax[i]);
        }
    }
};

#endif // SW_CORE_H
