//============================================================================
// tb_sw_stress_q.cpp — M4 压力台(N=8:多口并发灌同一出口 + 阈值/高水位/自洽)  [2026-10-09 建]
//
// 判据(预登记;照 验证方案.md §2-L4):
//   ① 并发灌入:口0..6 轮转各发 6 帧 ⇒ 口7(节流慢排 + `max=1000B` 小帽)
//   ② 计数自洽:tx[7] + Σdrop == 42;Σrx == 43(42 灌入 + 1 学表);Σtx == Σrx + 6(学表泛洪复制 7 份)
//   ③ 阈值/高水位:**0 < o_qmax[7] ≤ 1000**(小帽生效);**drop > 0**(满溢非静默)
//   ④ 送达子集逐字节:口7 收到的每帧都能在期望集里一一对上(不重不漏)✓
//============================================================================
#include "sw_l2_common.h"

struct Drv : SwL2Bench {
    Drv(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        const unsigned char M7[6] = {0x02,0,0,0,0,0x08};
        const unsigned char MS[7][6] = {
            {0x02,0,0,0,0,0x10}, {0x02,0,0,0,0,0x11}, {0x02,0,0,0,0,0x12}, {0x02,0,0,0,0,0x13},
            {0x02,0,0,0,0,0x14}, {0x02,0,0,0,0,0x15}, {0x02,0,0,0,0,0x16} };
        printf("== TB_SW_STRESS_Q(8 口并发 + 小帽/高水位/自洽)==\n");
        top.core.set_pfc(false);
        top.core.set_wm(1u<<30, 1u<<29, 1000);         // ★ 小帽 1000B:压力可见

        std::vector<unsigned char> lf = mk_frame(MS[0], M7, 96, 0x30);
        send(7, lf);                                   // 学表 M7 ⇒ 口7(泛洪到 0..6)
        for (int p = 0; p < 7; p++) wait_frames(p, 1, 4000);
        idle_cycles(200);
        chk(lrx[7].frames == 0, "基准:口7 尚无帧(学表帧自口7 出)");

        // ① 轮转并发:r 轮 × 7 口(每帧 seed 唯一)
        set_throttle(7, 1, 15);                        // 口7 慢排 ⇒ 队列积累
        const int K = 6;
        std::vector<std::vector<unsigned char> > exp;
        for (int r = 0; r < K; r++)
            for (int p = 0; p < 7; p++) {
                std::vector<unsigned char> f = mk_frame(M7, MS[p], 200, (unsigned char)(0x40 + p*8 + r), true, 1);
                exp.push_back(f);
                send(p, f);                            // 轮转:各源交替进同一出口队列 ✓
            }
        idle_cycles(800);
        set_throttle(7, 1, 0);                         // 放开排空
        for (int k = 0; k < 30000; k++) {
            unsigned int d = 0;
            for (int p = 0; p < 8; p++) d += cnt(top.sw_drop, p);
            if (cnt(top.sw_tx_frames, 7) + d >= 42) break;
            wait(clk.posedge_event()); pump();
        }
        // ④ 送达子集:一一对上
        int got7 = scan_data(7, 0);
        std::vector<char> used(exp.size(), 0);
        int matched = 0;
        for (size_t i = 0; i < lrx[7].got.size(); i++) {
            if (sw_parse_pfc(lrx[7].got[i]).is_pfc) continue;
            for (size_t j = 0; j < exp.size(); j++)
                if (!used[j] && pay_eq(lrx[7].got[i], exp[j])) { used[j] = 1; matched++; break; }
        }
        // ② 自洽
        unsigned int sumrx = 0, sumtx = 0, sumdp = 0;
        for (int p = 0; p < 8; p++) { sumrx += cnt(top.sw_rx_frames, p); sumtx += cnt(top.sw_tx_frames, p); sumdp += cnt(top.sw_drop, p); }
        printf("   读数:tx[7]=%u Σdrop=%u o_qmax[7]=%u;Σrx=%u Σtx=%u\n",
               cnt(top.sw_tx_frames, 7), sumdp, cnt(top.sw_qmax, 7), sumrx, sumtx);
        chk(cnt(top.sw_tx_frames, 7) + sumdp == 42, "② 自洽:tx[7] + Σdrop == 42");
        chk(sumrx == 43 && sumtx + sumdp == sumrx + 6, "② Σrx==43 ∧ Σtx+Σdrop==Σrx+6(学表泛洪复制 7 份)");
        chk(cnt(top.sw_qmax, 7) > 0 && cnt(top.sw_qmax, 7) <= 1000, "③ 高水位:0 < o_qmax[7] ≤ 1000(小帽生效)");
        chk(sumdp > 0,                              "③ 满溢丢弃 >0(非静默)");
        chk(got7 == matched,                        "④ 送达子集逐字节一一对上(不重不漏)");
        chk(got7 >= 1,                              "④ 至少有帧送达(非全丢)");
    }
};

int sc_main(int, char**) {
    sc_core::sc_clock clk("clk", 2.0, sc_core::SC_NS);
    sc_core::sc_signal<bool> rst;
    SwitchTop top("top", 8);
    top.clk_in(clk); top.rst_in(rst);
    Drv drv("drv", top, clk);
    drv.rst_o(rst);
    sc_core::sc_start();
    printf("TB_SW_STRESS_Q %s\n", drv.fails ? "FAIL" : "PASS");
    return drv.fails ? 1 : 0;
}
