//============================================================================
// tb_sw_l2_uni.cpp — L2 单播/学表/泛洪/同口过滤(N=3,线侧端到端)  [2026-10-08 建/M1]
//
// 判据(预登记;照 验证方案.md §2-L1 tb_sw_l2_uni):
//   ① 学表+泛洪:口0 发 S0(src=M0,dst=M1 未学)⇒ 口1/口2 **各恰 1 份、逐字节一致**;
//     负控:**入口(口0)不发** ✓
//   ② 单播命中:口1 发 U1(1500B,VLAN 标签;src=M1,dst=M0 已学⇒命中口0)⇒ **仅口0** 1 份、
//     逐字节(含 VLAN 标签未改);负控:口2 无新增 ✓
//   ③ 同口过滤:口0 发 F0(dst=M0 命中=入口)⇒ 全口无新增 + **丢弃计数 p0=1**(丢因非静默)✓
//   ④ 计数面(F5):rx=[2,1,0] / tx=[1,1,1] / drop=[1,0,0];字节 rx=[164,1500,0] tx=[1500,100,100] ✓
//============================================================================
#include "sw_l2_common.h"

struct Drv : SwL2Bench {
    Drv(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        const unsigned char M0[6] = {0x02,0,0,0,0,0x01};
        const unsigned char M1[6] = {0x02,0,0,0,0,0x02};
        std::vector<unsigned char> S0 = mk_frame(M1, M0,  100, 0x11);        // 口0 发(学 M0→0;dst M1 未知)
        std::vector<unsigned char> U1 = mk_frame(M0, M1, 1500, 0x22, true);  // 口1 发(VLAN 标签;dst M0 命中)
        std::vector<unsigned char> F0 = mk_frame(M0, M0,   64, 0x33);        // 口0 发(dst=自己⇒同口过滤)
        printf("== TB_SW_L2_UNI(3 口;线侧 LINK 端到端)==\n");

        // ---- 相位 ①:泛洪 + 逐字节 + 负控(入口不发)----
        send(0, S0);
        bool g1 = wait_frames(1, 1, 3000), g2 = wait_frames(2, 1, 3000);
        idle_cycles(50);
        chk(g1 && g2 && lrx[1].frames == 1 && lrx[2].frames == 1, "① 泛洪:口1/口2 各恰 1 份");
        chk(lrx[0].frames == 0,                                   "① 负控:入口(口0)不发");
        chk(g1 && pay_eq(lrx[1].got[0], S0),                      "① 逐字节:口1 副本 == S0");
        chk(g2 && pay_eq(lrx[2].got[0], S0),                      "① 逐字节:口2 副本 == S0");

        // ---- 相位 ②:单播命中 + 负控(第三口零)----
        send(1, U1);
        bool g0 = wait_frames(0, 1, 6000);
        idle_cycles(200);
        chk(g0 && lrx[0].frames == 1,          "② 单播命中:仅口0 收 1 份");
        chk(g0 && pay_eq(lrx[0].got[0], U1),   "② 逐字节B 含 VLAN 标签未改");
        chk(lrx[2].frames == 1,                "② 负控:口2 无新增(仍 1 份)");
        chk(lrx[1].frames == 1,                "② 负控:入口(口1)不发");

        // ---- 相位 ③:同口过滤 ----
        send(0, F0);
        idle_cycles(500);
        chk(lrx[0].frames == 1 && lrx[1].frames == 1 && lrx[2].frames == 1, "③ 同口命中:全口无新增");
        chk(cnt(top.sw_drop, 0) == 1, "③ 丢弃计数:p0 丢 1");

        // ---- 相位 ④:计数面(F5;读数照抄)----
        printf("   计数 rx=[%u,%u,%u] tx=[%u,%u,%u] drop=[%u,%u,%u] rxB=[%u,%u,%u] txB=[%u,%u,%u]\n",
               cnt(top.sw_rx_frames,0), cnt(top.sw_rx_frames,1), cnt(top.sw_rx_frames,2),
               cnt(top.sw_tx_frames,0), cnt(top.sw_tx_frames,1), cnt(top.sw_tx_frames,2),
               cnt(top.sw_drop,0),      cnt(top.sw_drop,1),      cnt(top.sw_drop,2),
               cnt(top.sw_rx_bytes,0),  cnt(top.sw_rx_bytes,1),  cnt(top.sw_rx_bytes,2),
               cnt(top.sw_tx_bytes,0),  cnt(top.sw_tx_bytes,1),  cnt(top.sw_tx_bytes,2));
        chk(cnt(top.sw_rx_frames,0)==2 && cnt(top.sw_rx_frames,1)==1 && cnt(top.sw_rx_frames,2)==0, "计数 rx=[2,1,0]");
        chk(cnt(top.sw_tx_frames,0)==1 && cnt(top.sw_tx_frames,1)==1 && cnt(top.sw_tx_frames,2)==1, "计数 tx=[1,1,1]");
        chk(cnt(top.sw_drop,1)==0 && cnt(top.sw_drop,2)==0,            "计数 drop 其余口 0");
        chk(cnt(top.sw_rx_bytes,0)==164 && cnt(top.sw_rx_bytes,1)==1500,
                                                                       "计数 rxB=[164,1500,0]");
        chk(cnt(top.sw_tx_bytes,0)==1500 && cnt(top.sw_tx_bytes,1)==100 && cnt(top.sw_tx_bytes,2)==100,
                                                                       "计数 txB=[1500,100,100]");
    }
};

int sc_main(int, char**) {
    sc_core::sc_clock clk("clk", 2.0, sc_core::SC_NS);
    sc_core::sc_signal<bool> rst;
    SwitchTop top("top", 3);
    top.clk_in(clk); top.rst_in(rst);
    Drv drv("drv", top, clk);
    drv.rst_o(rst);
    sc_core::sc_start();
    printf("TB_SW_L2_UNI %s\n", drv.fails ? "FAIL" : "PASS");
    return drv.fails ? 1 : 0;
}
