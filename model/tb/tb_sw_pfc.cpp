//============================================================================
// tb_sw_pfc.cpp — M2 无损(PFC)四模式台(N=2,线侧 LINK 端到端)  [2026-10-08 建]
//
// 用法:`./tb_sw_pfc xoff|gate|lossless|off`(判据 = 验证方案.md §2-L2 预登记)✓
//
// ★ 拓扑口径(压缩多跳):源口 0 灌帧 ⇒ 目的口 1;口 1 出线**节流**(ena 占空)造拥堵;
//   本 TB 兼当"上游":见到口 1 发出的 PFC(XOFF)即**停注**,见到 XON 即**恢复**(把多跳链路压缩进一台)✓
//
// 模式判据(预登记):
//  xoff     :① 过 XOFF ⇒ 口1 发 PFC:**逐字段对表**(dst=01:80:C2:00:00:01 / 0x8808 / 0x0101 /
//             vec bit2=1 / t[2]=0xFFFF);**刷新节奏** ⇒ XOFF 出现 ≥2 次
//             ② 落 XON ⇒ 发 XON(vec bit2=1 / t[2]=0);其后数据全恢复
//             ③ 计数:o_pfc_tx[1] == XOFF数+XON数;drop==0;40 帧逐字节全到
//  gate     :① 负控:类 5 的 PFC **不挡**类 2(照常出)
//             ② XOFF(类2)⇒ 后续类 2 帧**暂停出队**(2000 拍无新增)
//             ③ XON(类2)⇒ 恢复,2 帧逐字节到
//             ④ PFC **被消费不转发**(两向 got 无 PFC);o_pfc_rx[1]==3
//  lossless :① XOFF≥1 且 XON≥1(开关量闭环)② 100 帧逐字节全到 ③ **drop==0**(无损)
//             ④ o_pfc_tx[1]==XOFF数+XON数
//  off      :① 负控:**PFC 关 ⇒ o_pfc_tx[1]==0**(零动作)② 满溢丢弃 **>0**(不静默)
//             ③ 自洽:Σtx + Σdrop == 100
//============================================================================
#include "sw_l2_common.h"
#include <cstring>

static const unsigned char M0[6] = {0x02,0,0,0,0,0x01};
static const unsigned char M1[6] = {0x02,0,0,0,0,0x02};

//----------------------------------------------------------------------------
// 模式 xoff:PFC 生成 / 刷新 / XON
//----------------------------------------------------------------------------
struct DrvXoff : SwL2Bench {
    DrvXoff(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        top.core.set_pfc(true);
        top.core.set_wm(4096, 2048, 262144);
        top.core.pfc_refresh = 64;                     // 刷新节奏(测得)
        top.core.pfc_quantum = 0xFFFF;
        printf("== TB_SW_PFC_XOFF(PFC 生成/刷新/XON)==\n");

        send(1, mk_frame(M0, M1, 64, 0x10));           // 学表:M1 ⇒ 口1
        idle_cycles(200);
        set_throttle(1, 1, 15);                        // 口1 出线 1/16 占空 ⇒ 慢排
        const int K = 40;
        std::vector<std::vector<unsigned char> > expect;
        for (int i = 0; i < K; i++) {
            expect.push_back(mk_frame(M1, M0, 200, (unsigned char)(0x40 + i), true, 2));  // 类2
            send(0, expect[i]);
        }
        idle_cycles(1500);                             // XOFF 发出 + 刷新期

        SwPfcInfo infos[256];
        int n1 = scan_pfc(1, 0, infos, 256);
        int xoff_cnt = 0, xon_cnt = 0; int first_xoff = -1;
        for (int i = 0; i < n1; i++) {
            if ((infos[i].vec & 0x4) && infos[i].t[2] > 0) { xoff_cnt++; if (first_xoff < 0) first_xoff = i; }
            if ((infos[i].vec & 0x4) && infos[i].t[2] == 0) xon_cnt++;
        }
        // ① 逐字段对表(取口1 上第一个 PFC 帧的原文)
        bool fields = false;
        if (first_xoff >= 0) {
            for (size_t i = 0; i < lrx[1].got.size(); i++) {
                if (!sw_parse_pfc(lrx[1].got[i]).is_pfc) continue;
                const std::vector<unsigned char>& f = lrx[1].got[i];
                fields = (f[0]==0x01 && f[1]==0x80 && f[2]==0xC2 && f[3]==0x00 && f[4]==0x00 && f[5]==0x01)
                      && (f[12]==0x88 && f[13]==0x08 && f[14]==0x01 && f[15]==0x01)
                      && (f[17] == 0x04)                       // vec bit2(低字节)
                      && (f[18+2*2]==0xFF && f[19+2*2]==0xFF); // t[2] = 0xFFFF
                break;
            }
        }
        printf("   读数:口1 上 PFC 共 %d(XOFF=%d 含刷新 / XON=%d);首帧逐字段=%d\n",
               n1, xoff_cnt, xon_cnt, (int)fields);
        chk(fields,                    "① PFC-XOFF 帧逐字段对表(dst/etype/opcode/vec/t)");
        chk(xoff_cnt >= 2,             "① 刷新节奏:XOFF 出现 ≥2 次");

        // ② 放开出线 ⇒ 队列回落 ⇒ XON;数据全恢复
        set_throttle(1, 1, 0);                         // 出线全速
        for (int k = 0; k < 4000 && xon_cnt == 0; k++) {
            wait(clk.posedge_event()); pump();
            int n2 = scan_pfc(1, 0, infos, 256);
            xon_cnt = 0;
            for (int i = 0; i < n2; i++) if ((infos[i].vec & 0x4) && infos[i].t[2] == 0) xon_cnt++;
        }
        chk(xon_cnt >= 1,              "② 落 XON ⇒ 发出 XON 帧(t=0)");
        for (int k = 0; k < 4000 && scan_data(1, 0) < K; k++) { wait(clk.posedge_event()); pump(); }
        // 数据帧逐字节(过滤 PFC 后按序对)
        std::vector<std::vector<unsigned char> > got;
        for (size_t i = 0; i < lrx[1].got.size(); i++)
            if (!sw_parse_pfc(lrx[1].got[i]).is_pfc) got.push_back(lrx[1].got[i]);
        bool bytes_ok = (got.size() >= (size_t)K);
        for (int i = 0; i < K && bytes_ok; i++) bytes_ok = pay_eq(got[i], expect[i]);
        chk(bytes_ok,                  "② XON 后数据恢复:40 帧逐字节");
        chk(cnt(top.sw_pfc_tx, 1) == (unsigned)(xoff_cnt + xon_cnt), "③ 计数 o_pfc_tx[1] == XOFF数+XON数");
        chk(cnt(top.sw_tx_frames, 1) == (unsigned)K,   "③ 计数 tx[1] == 40");
        chk(cnt(top.sw_drop, 0) == 0 && cnt(top.sw_drop, 1) == 0, "③ drop == 0");
    }
};

//----------------------------------------------------------------------------
// 模式 gate:收 PFC ⇒ 出队闸门 / XON 恢复 / 错类负控
//----------------------------------------------------------------------------
struct DrvGate : SwL2Bench {
    DrvGate(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        top.core.set_pfc(true);
        top.core.set_wm(4096, 2048, 262144);
        printf("== TB_SW_PFC_GATE(闸门:XOFF/XON/错类负控)==\n");

        send(1, mk_frame(M0, M1, 64, 0x10));           // 学表:M1 ⇒ 口1
        idle_cycles(200);
        chk(lrx[1].frames == 0, "基准:口1 出线尚无帧");
        // ① 负控:先注入类 5 的 XOFF ⇒ 类 2 不受影响
        std::vector<unsigned char> f5; sw_build_pfc(1, 5, 0xFFFF, f5);
        send(1, f5);
        idle_cycles(200);
        send(0, mk_frame(M1, M0, 200, 0x21, true, 2));
        bool nc = wait_frames(1, 1, 3000);
        chk(nc, "① 负控:类5 XOFF 不挡类 2(照常出)");
        // ② 注入类 2 XOFF ⇒ 闸门关
        std::vector<unsigned char> f2x; sw_build_pfc(1, 2, 0xFFFF, f2x);
        send(1, f2x);
        idle_cycles(200);
        std::vector<unsigned char> q1 = mk_frame(M1, M0, 200, 0x22, true, 2);
        std::vector<unsigned char> q2 = mk_frame(M1, M0, 200, 0x23, true, 2);
        send(0, q1); send(0, q2);
        idle_cycles(2000);
        chk(lrx[1].frames == 1,        "② XOFF(类2)⇒ 后续 2 帧暂停出队(无新增)");
        // ③ 注入 XON ⇒ 恢复
        std::vector<unsigned char> f2o; sw_build_pfc(1, 2, 0, f2o);
        send(1, f2o);
        bool rec = wait_frames(1, 3, 3000);
        idle_cycles(100);
        chk(rec && lrx[1].frames == 3, "③ XON(类2)⇒ 恢复,2 帧到齐");
        chk(lrx[1].got.size() >= 3 && pay_eq(lrx[1].got[1], q1) && pay_eq(lrx[1].got[2], q2),
                                       "③ 逐字节:恢复的 2 帧");
        // ④ PFC 消费不转发 + 计数
        chk(scan_pfc(0, 0, 0, 0) == 0 && scan_pfc(1, 0, 0, 0) == 0, "④ PFC 被消费:两向出线均无 PFC");
        chk(cnt(top.sw_pfc_rx, 1) == 3, "④ 计数 o_pfc_rx[1] == 3");
        chk(cnt(top.sw_drop, 0) == 0 && cnt(top.sw_drop, 1) == 0, "④ drop == 0");
        chk(lrx[0].frames == 1,         "④ 负控:口0 仅学表帧 1 份(注入 PFC 未被倒灌)");
    }
};

//----------------------------------------------------------------------------
// 模式 lossless:PFC 闭环 ⇒ 无损(0 丢)
//----------------------------------------------------------------------------
struct DrvLossless : SwL2Bench {
    DrvLossless(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        top.core.set_pfc(true);
        top.core.set_wm(4096, 2048, 16384);            // max=16384 < 总注入 20000B ⇒ 无 PFC 必丢
        top.core.pfc_refresh = 1000000;                // 本模式只看开关量(关刷新)
        printf("== TB_SW_PFC_LOSSLESS(闭环:0 丢)==\n");

        send(1, mk_frame(M0, M1, 64, 0x10));
        idle_cycles(200);
        set_throttle(1, 1, 15);
        const int K = 100;
        std::vector<std::vector<unsigned char> > expect;
        for (int i = 0; i < K; i++) expect.push_back(mk_frame(M1, M0, 200, (unsigned char)(0x40 + i), true, 2));

        int next = 0, delivered = 0, xoff_seen = 0, xon_seen = 0;
        bool paused = false;
        size_t cur = 0;
        for (int cyc = 0; cyc < 300000 && (next < K || delivered < K); cyc++) {
            for (size_t i = cur; i < lrx[1].got.size(); i++) {          // 扫新帧(先反应)
                SwPfcInfo r = sw_parse_pfc(lrx[1].got[i]);
                if (r.is_pfc) {
                    if ((r.vec & 0x4) && r.t[2] > 0) { paused = true;  xoff_seen++; }
                    else if ((r.vec & 0x4) && r.t[2] == 0) { paused = false; xon_seen++; }
                } else delivered++;
            }
            cur = lrx[1].got.size();
            if (!paused && next < K) { send(0, expect[next]); next++; } // 上游:不禁则注
            else { wait(clk.posedge_event()); pump(); }
        }
        set_throttle(1, 1, 0);                                       // 放开出线排空
        for (int k = 0; k < 20000; k++) {
            if (delivered >= K) break;
            wait(clk.posedge_event()); pump();
            for (size_t i = cur; i < lrx[1].got.size(); i++)
                if (!sw_parse_pfc(lrx[1].got[i]).is_pfc) delivered++;
            cur = lrx[1].got.size();
        }
        std::vector<std::vector<unsigned char> > got;
        for (size_t i = 0; i < lrx[1].got.size(); i++)
            if (!sw_parse_pfc(lrx[1].got[i]).is_pfc) got.push_back(lrx[1].got[i]);
        bool bytes_ok = (got.size() >= (size_t)K);
        for (int i = 0; i < K && bytes_ok; i++) bytes_ok = pay_eq(got[i], expect[i]);
        printf("   读数:XOFF=%d XON=%d 送达=%d/%d Σdrop=%u\n", xoff_seen, xon_seen, delivered, K,
               cnt(top.sw_drop,0) + cnt(top.sw_drop,1));
        chk(xoff_seen >= 1 && xon_seen >= 1, "① 开关量闭环:XOFF≥1 且 XON≥1");
        chk(bytes_ok && delivered == K,      "② 100 帧逐字节全到");
        chk(cnt(top.sw_drop,0) == 0 && cnt(top.sw_drop,1) == 0, "③ **drop == 0**(无损)");
        chk(cnt(top.sw_pfc_tx, 1) == (unsigned)(xoff_seen + xon_seen), "④ 计数 o_pfc_tx[1] == XOFF+XON");
        chk(cnt(top.sw_rx_frames, 0) == (unsigned)K, "④ 计数 rx[0] == 100");
    }
};

//----------------------------------------------------------------------------
// 模式 off:负控(PFC 关 ⇒ 满溢丢弃可见)
//----------------------------------------------------------------------------
struct DrvOff : SwL2Bench {
    DrvOff(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        top.core.set_pfc(false);                       // ★ 负控:PFC 关
        top.core.set_wm(4096, 2048, 16384);
        printf("== TB_SW_PFC_OFF(负控:PFC 关 ⇒ 溢出丢)==\n");

        send(1, mk_frame(M0, M1, 64, 0x10));
        idle_cycles(200);
        unsigned int base = 0;
        for (int p = 0; p < 2; p++) base += cnt(top.sw_tx_frames, p) + cnt(top.sw_drop, p);
        set_throttle(1, 1, 15);
        const int K = 100;
        for (int i = 0; i < K; i++) send(0, mk_frame(M1, M0, 200, (unsigned char)(0x40 + i), true, 2));
        idle_cycles(500);
        set_throttle(1, 1, 0);                         // 放开排空
        unsigned int sum = base;
        for (int k = 0; k < 40000; k++) {
            sum = 0;
            for (int p = 0; p < 2; p++) sum += cnt(top.sw_tx_frames, p) + cnt(top.sw_drop, p);
            if (sum - base >= (unsigned int)K) break;
            wait(clk.posedge_event()); pump();
        }
        unsigned int drop = cnt(top.sw_drop, 0) + cnt(top.sw_drop, 1);
        printf("   读数:tx=[%u,%u] drop=[%u,%u] pfc_tx=[%u,%u]\n",
               cnt(top.sw_tx_frames,0), cnt(top.sw_tx_frames,1),
               cnt(top.sw_drop,0), cnt(top.sw_drop,1),
               cnt(top.sw_pfc_tx,0), cnt(top.sw_pfc_tx,1));
        chk(cnt(top.sw_pfc_tx,0) == 0 && cnt(top.sw_pfc_tx,1) == 0, "① 负控:PFC 关 ⇒ 零 PFC 动作");
        chk(drop > 0,                                  "② 满溢丢弃 >0(不静默)");
        chk(sum - base == (unsigned int)K,             "③ 自洽:Σtx + Σdrop(增量)== 100");
    }
};

int sc_main(int argc, char* argv[]) {
    const char* mode = (argc > 1) ? argv[1] : "xoff";
    sc_core::sc_clock clk("clk", 2.0, sc_core::SC_NS);
    sc_core::sc_signal<bool> rst;
    SwitchTop top("top", 2);
    top.clk_in(clk); top.rst_in(rst);
    SwL2Bench* drv = 0;
    const char* up = "XOFF";
    if      (!strcmp(mode, "xoff"))     { drv = new DrvXoff    ("drv", top, clk); up = "XOFF"; }
    else if (!strcmp(mode, "gate"))     { drv = new DrvGate    ("drv", top, clk); up = "GATE"; }
    else if (!strcmp(mode, "lossless")) { drv = new DrvLossless("drv", top, clk); up = "LOSSLESS"; }
    else                                { drv = new DrvOff     ("drv", top, clk); up = "OFF"; }
    drv->rst_o(rst);
    sc_core::sc_start();
    printf("TB_SW_PFC_%s %s\n", up, drv->fails ? "FAIL" : "PASS");
    return drv->fails ? 1 : 0;
}
