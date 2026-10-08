//============================================================================
// tb_sw_l2_mcast.cpp — 广播/组播泛洪(N=4,线侧端到端)  [2026-10-08 建/M1]
//
// 判据(预登记;照 验证方案.md §2-L1 tb_sw_l2_mcast):
//   ① 广播(dst=ff×6):口2 发 B ⇒ 口0/口1/口3 各恰 1 份、逐字节一致;负控:口2(入口)不发 ✓
//   ② 组播(dst=01:00:5e:…,I/G 位=1):口1 发 Mc ⇒ 口0/口2/口3 各恰 1 份;
//     负控:口1(入口)不发 ✓(⚠ 简化口径:组成员表不建 ⇒ 组播=泛洪,设计册 §2.2 已记)
//   ③ 计数面:rx=[0,1,1,0] / tx=[2,1,1,2] / drop 全 0 ✓
//============================================================================
#include "sw_l2_common.h"

struct Drv : SwL2Bench {
    Drv(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        const unsigned char BC[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
        const unsigned char MC[6] = {0x01,0x00,0x5e,0x00,0x00,0x01};   // 组播(I/G=1)
        const unsigned char M1[6] = {0x02,0,0,0,0,0x02};
        const unsigned char M2[6] = {0x02,0,0,0,0,0x03};
        std::vector<unsigned char> B  = mk_frame(BC, M2, 128, 0x66);
        std::vector<unsigned char> Mc = mk_frame(MC, M1, 200, 0x77);
        printf("== TB_SW_L2_MCAST(4 口;线侧 LINK 端到端)==\n");

        // ---- 相位 ①:广播 ----
        send(2, B);
        bool g0 = wait_frames(0, 1, 3000), g1 = wait_frames(1, 1, 3000), g3 = wait_frames(3, 1, 3000);
        idle_cycles(50);
        chk(g0 && g1 && g3 && lrx[0].frames == 1 && lrx[1].frames == 1 && lrx[3].frames == 1,
            "① 广播:口0/口1/口3 各恰 1 份");
        chk(lrx[2].frames == 0,               "① 负控:入口(口2)不发");
        chk(g0 && pay_eq(lrx[0].got[0], B),   "① 逐字节:口0 == B");
        chk(g1 && pay_eq(lrx[1].got[0], B),   "① 逐字节:口1 == B");
        chk(g3 && pay_eq(lrx[3].got[0], B),   "① 逐字节:口3 == B");

        // ---- 相位 ②:组播(I/G 位)----
        send(1, Mc);
        bool h0 = wait_frames(0, 2, 3000), h2 = wait_frames(2, 1, 3000), h3 = wait_frames(3, 2, 3000);
        idle_cycles(50);
        chk(h0 && h2 && h3 && lrx[0].frames == 2 && lrx[2].frames == 1 && lrx[3].frames == 2,
            "② 组播:口0/口3 各增至 2;口2 收 1");
        chk(lrx[1].frames == 1,                "② 负控:入口(口1)不发");
        chk(h0 && pay_eq(lrx[0].got[1], Mc),   "② 逐字节:口0 第 2 份 == Mc");
        chk(h2 && pay_eq(lrx[2].got[0], Mc),   "② 逐字节:口2 == Mc");
        chk(h3 && pay_eq(lrx[3].got[1], Mc),   "② 逐字节:口3 第 2 份 == Mc");

        // ---- 相位 ③:计数面(F5)----
        printf("   计数 rx=[%u,%u,%u,%u] tx=[%u,%u,%u,%u] drop=[%u,%u,%u,%u]\n",
               cnt(top.sw_rx_frames,0), cnt(top.sw_rx_frames,1), cnt(top.sw_rx_frames,2), cnt(top.sw_rx_frames,3),
               cnt(top.sw_tx_frames,0), cnt(top.sw_tx_frames,1), cnt(top.sw_tx_frames,2), cnt(top.sw_tx_frames,3),
               cnt(top.sw_drop,0),      cnt(top.sw_drop,1),      cnt(top.sw_drop,2),      cnt(top.sw_drop,3));
        chk(cnt(top.sw_rx_frames,0)==0 && cnt(top.sw_rx_frames,1)==1 &&
            cnt(top.sw_rx_frames,2)==1 && cnt(top.sw_rx_frames,3)==0, "计数 rx=[0,1,1,0]");
        chk(cnt(top.sw_tx_frames,0)==2 && cnt(top.sw_tx_frames,1)==1 &&
            cnt(top.sw_tx_frames,2)==1 && cnt(top.sw_tx_frames,3)==2, "计数 tx=[2,1,1,2]");
        chk(cnt(top.sw_drop,0)==0 && cnt(top.sw_drop,1)==0 &&
            cnt(top.sw_drop,2)==0 && cnt(top.sw_drop,3)==0,          "计数 drop 全 0");
    }
};

int sc_main(int, char**) {
    sc_core::sc_clock clk("clk", 2.0, sc_core::SC_NS);
    sc_core::sc_signal<bool> rst;
    SwitchTop top("top", 4);
    top.clk_in(clk); top.rst_in(rst);
    Drv drv("drv", top, clk);
    drv.rst_o(rst);
    sc_core::sc_start();
    printf("TB_SW_L2_MCAST %s\n", drv.fails ? "FAIL" : "PASS");
    return drv.fails ? 1 : 0;
}
