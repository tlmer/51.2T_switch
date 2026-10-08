//============================================================================
// sw_l2_common.h — L2 转发面 TB 公共设施(线侧端到端:LINK 注防/观测)  [2026-10-08 建/M1]
//
// ★ 线侧口径**照 mac_tlm.cpp 实读**,不发明:
//   · 线帧 = START(ctrl) + 6×PRE + SFD + payload + FCS(4B,以太网 CRC32 反射) + TERM(ctrl);
//     常量 IDLE=0x07 / START=0xFB / TERM=0xFD / SFD=0xD5 / PRE=0x55 ✓
//   · LINK 128 流字节/拍 = lane0(0..63)+ lane1(64..127);字内**REV 序**(posn,与 mac_tlm 默认同档)✓
//   · ⚠ **驱动铁律**:MAC RX 在 ena=1 时**每拍重处理线上现值** ⇒ TB **每拍必须换新值**;
//     帧毕**驻 idle 拍**(idle 各状态跳过 ⇒ 重复处理无害)✓
//   · 观测:MAC TX 出线(link_txd/txc/dval)按同样映射重组;idle 跳过;TERM 收帧;剥 FCS ✓
//============================================================================
#ifndef SW_L2_COMMON_H
#define SW_L2_COMMON_H

#include <systemc.h>
#include "switch_top.h"
#include <vector>
#include <cstdio>

// ---- 线帧常量(mac_tlm.cpp:15-19 实读)----
static const unsigned char C_IDLE  = 0x07;
static const unsigned char C_START = 0xFB;
static const unsigned char C_TERM  = 0xFD;
static const unsigned char C_SFD   = 0xD5;
static const unsigned char C_PRE   = 0x55;

// ---- 字内字节映射(REV 序;mac_tlm.cpp:24 同档)----
inline int sw_posn(int i) { return (i/8)*8 + (7-(i%8)); }

// ---- 以太网 FCS(mac_tlm.cpp:34 同式)----
inline unsigned int sw_crc32(const unsigned char* d, int n) {
    unsigned int c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) {
        c ^= (unsigned int)d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

// ---- 组线帧(START + 6×PRE + SFD + payload + FCS + TERM)----
inline void sw_build_wire(const std::vector<unsigned char>& pay, std::vector<unsigned char>& wire) {
    wire.clear();
    wire.push_back(C_START);
    for (int i = 0; i < 6; i++) wire.push_back(C_PRE);
    wire.push_back(C_SFD);
    for (size_t i = 0; i < pay.size(); i++) wire.push_back(pay[i]);
    unsigned int c = sw_crc32(pay.empty() ? 0 : &pay[0], (int)pay.size());
    wire.push_back((unsigned char)(c & 0xff));        wire.push_back((unsigned char)((c >> 8) & 0xff));
    wire.push_back((unsigned char)((c >> 16) & 0xff)); wire.push_back((unsigned char)((c >> 24) & 0xff));
    wire.push_back(C_TERM);
}

// ---- 驱动一拍(128 流字节 ⇒ 双 lane;REV 序)----
inline void sw_drive_beat(SwitchTop& top, int p, const unsigned char* b, const bool* c) {
    sc_biguint<512> d0 = 0, d1 = 0; sc_biguint<64> c0 = 0, c1 = 0;
    for (int i = 0; i < 128; i++) {
        int pos = sw_posn((i < 64) ? i : (i - 64));
        if (i < 64) { d0.range(pos*8+7, pos*8) = b[i]; c0[pos] = c[i]; }
        else        { d1.range(pos*8+7, pos*8) = b[i]; c1[pos] = c[i]; }
    }
    top.line_rxd0[p]->write(d0); top.line_rxd1[p]->write(d1);
    top.line_rxc0[p]->write(c0); top.line_rxc1[p]->write(c1);
}
inline void sw_drive_idle(SwitchTop& top, int p) {
    unsigned char b[128]; bool c[128];
    for (int i = 0; i < 128; i++) { b[i] = C_IDLE; c[i] = true; }
    sw_drive_beat(top, p, b, c);
}

// ---- 出线重组(每口一台;剥 FCS;逐字节存)----
struct SwLineRx {
    int st;                                   // 0=IDLE 1=PRE 2=PAY
    int pre_n;
    unsigned int frames;
    std::vector<unsigned char> pay;
    std::vector<std::vector<unsigned char> > got;   // 完成帧(已剥 FCS)
    SwLineRx() : st(0), pre_n(0), frames(0) {}
    void feed(const sc_biguint<512>& d0, const sc_biguint<512>& d1,
              const sc_biguint<64>& c0, const sc_biguint<64>& c1) {
        for (int i = 0; i < 128; i++) {
            unsigned char b; bool c;
            if (i < 64) { int pos = sw_posn(i);      b = (unsigned char)d0.range(pos*8+7, pos*8).to_uint(); c = c0[pos].to_bool(); }
            else        { int pos = sw_posn(i - 64); b = (unsigned char)d1.range(pos*8+7, pos*8).to_uint(); c = c1[pos].to_bool(); }
            if (b == C_IDLE && c) continue;                       // idle 字节:各态跳过
            switch (st) {
            case 0: if (b == C_START && c) { st = 1; pre_n = 0; } break;
            case 1: if (pre_n < 6 && b == C_PRE && !c) pre_n++;
                    else if (pre_n == 6 && b == C_SFD && !c) { st = 2; pay.clear(); }
                    else st = 0;
                    break;
            case 2: if (c) {
                        if (b == C_TERM) {
                            if (pay.size() >= 4) got.push_back(std::vector<unsigned char>(pay.begin(), pay.end()-4));
                            frames++; st = 0;
                        }
                    } else pay.push_back(b);
                    break;
            }
        }
    }
};

// ---- PFC 帧(802.1Qbb 味;★F3 简版口径:vector 位 = 本帧就该类表态;time>0 关 / t=0 开)----
inline void sw_build_pfc(int port, int cls, unsigned int time, std::vector<unsigned char>& out) {
    out.assign(34, 0);
    const unsigned char DST[6] = {0x01,0x80,0xC2,0x00,0x00,0x01};
    for (int i = 0; i < 6; i++) out[i] = DST[i];
    out[6]=0x02; out[7]=0x00; out[8]=0x00; out[9]=0x00; out[10]=0x00; out[11]=(unsigned char)(0x10+port);
    out[12]=0x88; out[13]=0x08; out[14]=0x01; out[15]=0x01;
    unsigned int vec = (1u << cls);
    out[16]=(unsigned char)((vec >> 8) & 0xff); out[17]=(unsigned char)(vec & 0xff);
    for (int k = 0; k < 8; k++) {
        unsigned int t = (k == cls) ? time : 0u;
        out[18 + k*2] = (unsigned char)((t >> 8) & 0xff); out[19 + k*2] = (unsigned char)(t & 0xff);
    }
}
struct SwPfcInfo { bool is_pfc; unsigned int vec; unsigned int t[8]; };
inline SwPfcInfo sw_parse_pfc(const std::vector<unsigned char>& fr) {
    SwPfcInfo r; r.is_pfc = false; r.vec = 0;
    for (int k = 0; k < 8; k++) r.t[k] = 0;
    if (fr.size() >= 34 && fr[12]==0x88 && fr[13]==0x08 && fr[14]==0x01 && fr[15]==0x01) {
        r.is_pfc = true;
        r.vec = ((unsigned int)fr[16] << 8) | fr[17];
        for (int k = 0; k < 8; k++) r.t[k] = ((unsigned int)fr[18+k*2] << 8) | fr[19+k*2];
    }
    return r;
}

// ---- 测试台基类:复位/发帧/采样/等待/判据 ---(子类只写 tests())----
struct SwL2Bench : public sc_core::sc_module {
    SwitchTop&            top;
    sc_core::sc_clock&    clk;
    sc_core::sc_out<bool> rst_o;
    std::vector<SwLineRx> lrx;                 // [N] 每口出线重组
    int                   fails;

    // ★ M2:出线节流(ena 占空;灌队列场景用;on/off 拍)
    struct Thr { int port, on, off; bool active; };
    std::vector<Thr>      thr;
    long                  pump_cyc;

    SC_HAS_PROCESS(SwL2Bench);                 // ⚠ 基类里 SC_THREAD 需要它定义 SC_CURRENT_USER_MODULE ✓
    SwL2Bench(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c)
      : sc_module(nm), top(t), clk(c), rst_o("rst_o"), lrx(t.N), fails(0), pump_cyc(0) {
        SC_THREAD(run);
    }

    void set_throttle(int p, int on, int off, bool active = true) {
        for (size_t i = 0; i < thr.size(); i++) if (thr[i].port == p) { thr[i].on=on; thr[i].off=off; thr[i].active=active; return; }
        Thr t; t.port = p; t.on = on; t.off = off; t.active = active; thr.push_back(t);
    }

    // 采样所有口一次(边后调用)
    void sample() {
        for (int p = 0; p < top.N; p++)
            if (top.line_dval0[p]->read() && top.line_dval1[p]->read())
                lrx[p].feed(top.line_txd0[p]->read(), top.line_txd1[p]->read(),
                            top.line_txc0[p]->read(), top.line_txc1[p]->read());
    }

    // 每拍泵:节流驱动 + 采样(替代裸 sample)✓
    void pump() {
        pump_cyc++;
        for (size_t i = 0; i < thr.size(); i++) if (thr[i].active) {
            bool en = ((pump_cyc % (thr[i].on + thr[i].off)) < thr[i].on);
            top.line_ena_tx0[thr[i].port]->write(en);
            top.line_ena_tx1[thr[i].port]->write(en);
        }
        sample();
    }

    // 线侧发一帧(SC_THREAD 语境;★ 每拍换新值,帧毕驻 idle)
    void send(int p, const std::vector<unsigned char>& pay) {
        std::vector<unsigned char> wire; sw_build_wire(pay, wire);
        for (size_t off = 0; off < wire.size(); off += 128) {
            unsigned char b[128]; bool c[128];
            for (int i = 0; i < 128; i++) {
                size_t k = off + i;
                if (k < wire.size()) { b[i] = wire[k]; c[i] = (k == 0 || k == wire.size() - 1); }
                else                 { b[i] = C_IDLE;  c[i] = true; }
            }
            sw_drive_beat(top, p, b, c);
            wait(clk.posedge_event());
            pump();
        }
        sw_drive_idle(top, p);
        wait(clk.posedge_event());
        pump();
    }

    // 等某口收满 want 帧(或预算尽)
    bool wait_frames(int p, unsigned int want, int budget) {
        for (int k = 0; k < budget; k++) {
            if (lrx[p].frames >= want) return true;
            wait(clk.posedge_event());
            pump();
        }
        return lrx[p].frames >= want;
    }
    // 空转(泵)budget 拍
    void idle_cycles(int budget) {
        for (int k = 0; k < budget; k++) { wait(clk.posedge_event()); pump(); }
    }

    // 扫 lrx[p].got[from..) 中的 PFC 帧:返回条数;info[] 依序存
    int scan_pfc(int p, size_t from, SwPfcInfo* info, int maxn) {
        int n = 0;
        for (size_t i = from; i < lrx[p].got.size(); i++) {
            SwPfcInfo r = sw_parse_pfc(lrx[p].got[i]);
            if (r.is_pfc) { if (info && n < maxn) info[n] = r; n++; }
        }
        return n;
    }
    // 取 lrx[p].got[from..) 中非 PFC 帧的条数
    int scan_data(int p, size_t from) {
        int n = 0;
        for (size_t i = from; i < lrx[p].got.size(); i++) if (!sw_parse_pfc(lrx[p].got[i]).is_pfc) n++;
        return n;
    }

    void chk(bool ok, const char* what) {
        printf("   [%s] %s\n", ok ? "ok" : "XX", what);
        if (!ok) fails++;
    }
    unsigned int cnt(const std::vector<sc_signal<sc_uint<32> >*>& v, int p) {
        return v[p]->read().to_uint();
    }
    static bool pay_eq(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) return false;
        return true;
    }

    // 造帧:dst6 + src6 + [可插 802.1Q 标签(可带 PCP/VID)] + type 0x0800 + 数据(pattern)
    // ⚠ vid 默认 100(0x64)= 与早期版本 TCI 低字节逐位相同 ⇒ 既有台零变化 ✓
    static std::vector<unsigned char> mk_frame(const unsigned char* dst, const unsigned char* src,
                                               int total, unsigned char seed, bool vlan = false,
                                               int pcp = 0, int vid = 100) {
        std::vector<unsigned char> f;
        for (int i = 0; i < 6; i++) f.push_back(dst[i]);
        for (int i = 0; i < 6; i++) f.push_back(src[i]);
        if (vlan) {
            f.push_back(0x81); f.push_back(0x00);
            f.push_back((unsigned char)(((pcp & 0x7) << 5) | ((vid >> 8) & 0x0f)));  // PCP+VID 高 4b
            f.push_back((unsigned char)(vid & 0xff));
        }
        f.push_back(0x08); f.push_back(0x00);
        while ((int)f.size() < total) f.push_back((unsigned char)(seed + f.size()*3));
        return f;
    }

    void run() {
        // ---- 复位 + 初相:全口 ena=1,RX 驻 idle ----
        for (int p = 0; p < top.N; p++) {
            top.line_ena_tx0[p]->write(true); top.line_ena_tx1[p]->write(true);
            top.line_ena_rx0[p]->write(true); top.line_ena_rx1[p]->write(true);
            sw_drive_idle(top, p);
        }
        rst_o.write(false);
        for (int i = 0; i < 4; i++) wait(clk.posedge_event());
        rst_o.write(true);
        wait(clk.posedge_event());
        pump();
        tests();
        sc_core::sc_stop();
    }
    virtual void tests() = 0;
};

#endif // SW_L2_COMMON_H
