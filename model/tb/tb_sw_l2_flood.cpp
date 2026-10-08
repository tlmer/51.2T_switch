//============================================================================
// tb_sw_l2_flood.cpp — 未知单播泛洪(N=4,线侧端到端)  [2026-10-08 建/M1]
//
// 判据(预登记;照 验证方案.md §2-L1 tb_sw_l2_flood):
//   ① 口1 发 X(dst=UNK 从未学)⇒ **除入口外各一口** 1 份、逐字节一致;
//     负控:**入口(口1)不发** ✓
//   ② 换入口:口0 发 Y(同 dst=UNK)⇒ 口1/口2/口3 各恰 1 份(口0 不发)——
//     泛洪集合随入口走、**不重复不遗漏** ✓
//   ③ 计数面:rx=[1,1,0,0] / tx=[1,1,2,2] / drop 全 0 ✓
//============================================================================
#include "sw_l2_common.h"

struct Drv : SwL2Bench {
    Drv(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        const unsigned char UNK[6] = {0x02,0,0,0,0,0x99};   // 从不作 src ⇒ 表永不命中
        const unsigned char M0[6]  = {0x02,0,0,0,0,0x01};
        const unsigned char M1[6]  = {0x02,0,0,0,0,0x02};
        std::vector<unsigned char> X = mk_frame(UNK, M1, 96, 0x44);
        std::vector<unsigned char> Y = mk_frame(UNK, M0, 96, 0x55);
        printf("== TB_SW_L2_FLOOD(4 口;线侧 LINK 端到端)==\n");

        // ---- 相位 ①:入口=口1 ----
        send(1, X);
        bool g0 = wait_frames(0, 1, 3000), g2 = wait_frames(2, 1, 3000), g3 = wait_frames(3, 1, 3000);
        idle_cycles(50);
        chk(g0 && g2 && g3 && lrx[0].frames == 1 && lrx[2].frames == 1 && lrx[3].frames == 1,
            "① 泛洪:口0/口2/口3 各恰 1 份");
        chk(lrx[1].frames == 0,               "① 负控:入口(口1)不发");
        chk(g0 && pay_eq(lrx[0].got[0], X),   "① 逐字节:口0 == X");
        chk(g2 && pay_eq(lrx[2].got[0], X),   "① 逐字节:口2 == X");
        chk(g3 && pay_eq(lrx[3].got[0], X),   "① 逐字节:口3 == X");

        // ---- 相位 ②:换入口=口0(泛洪集合随入口走)----
        send(0, Y);
        bool h1 = wait_frames(1, 1, 3000), h2 = wait_frames(2, 2, 3000), h3 = wait_frames(3, 2, 3000);
        idle_cycles(50);
        chk(h1 && h2 && h3 && lrx[1].frames == 1 && lrx[2].frames == 2 && lrx[3].frames == 2,
            "② 泛洪集合随入口:口1 新增 1 / 口2 口3 各增至 2");
        chk(lrx[0].frames == 1,               "② 负控:入口(口0)无新增(仍 1 份 X)");
        chk(h1 && pay_eq(lrx[1].got[0], Y),   "② 逐字节:口1 == Y");
        chk(h2 && pay_eq(lrx[2].got[1], Y),   "② 逐字节:口2 第 2 份 == Y");
        chk(h3 && pay_eq(lrx[3].got[1], Y),   "② 逐字节:口3 第 2 份 == Y");

        // ---- 相位 ③:计数面(F5)----
        printf("   计数 rx=[%u,%u,%u,%u] tx=[%u,%u,%u,%u] drop=[%u,%u,%u,%u]\n",
               cnt(top.sw_rx_frames,0), cnt(top.sw_rx_frames,1), cnt(top.sw_rx_frames,2), cnt(top.sw_rx_frames,3),
               cnt(top.sw_tx_frames,0), cnt(top.sw_tx_frames,1), cnt(top.sw_tx_frames,2), cnt(top.sw_tx_frames,3),
               cnt(top.sw_drop,0),      cnt(top.sw_drop,1),      cnt(top.sw_drop,2),      cnt(top.sw_drop,3));
        chk(cnt(top.sw_rx_frames,0)==1 && cnt(top.sw_rx_frames,1)==1 &&
            cnt(top.sw_rx_frames,2)==0 && cnt(top.sw_rx_frames,3)==0, "计数 rx=[1,1,0,0]");
        chk(cnt(top.sw_tx_frames,0)==1 && cnt(top.sw_tx_frames,1)==1 &&
            cnt(top.sw_tx_frames,2)==2 && cnt(top.sw_tx_frames,3)==2, "计数 tx=[1,1,2,2]");
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
    printf("TB_SW_L2_FLOOD %s\n", drv.fails ? "FAIL" : "PASS");
    return drv.fails ? 1 : 0;
}
