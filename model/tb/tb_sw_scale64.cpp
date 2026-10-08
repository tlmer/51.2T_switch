//============================================================================
// tb_sw_scale64.cpp — M4 规模台(N=64 全配置;线侧 LINK 端到端)  [2026-10-09 建]
//
// 判据(预登记;照 验证方案.md §2-L4):
//   ① 学表帧:口63 发 ⇒ 其余 **63 口各恰 1 份**、逐字节;负控:入口(口63)不发 ✓
//   ② 端到端一笔:口0 发(dst=M63 已学)⇒ **仅口63** 逐字节 300B ✓;负控:其余口无新增 ✓
//   ③ 高水位读数在册:o_qmax[63] ≥ 300(帧在队里待过)✓
//   ④ 计数面:rx=[1@0,1@63];tx[63]==1,tx[0..62]==1;drop 全 0 ✓
//============================================================================
#include "sw_l2_common.h"

struct Drv : SwL2Bench {
    Drv(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        const unsigned char M63[6] = {0x02,0,0,0,0,0x40};
        const unsigned char M0 [6] = {0x02,0,0,0,0,0x01};
        printf("== TB_SW_SCALE64(N=64 端到端一笔 + 高水位)==\n");

        // ① 学表帧(口63 ⇒ 泛洪到其余 63 口)
        std::vector<unsigned char> lf = mk_frame(M0, M63, 96, 0x30);
        send(63, lf);
        int fl = 0;
        for (int p = 0; p < 64; p++) if (p != 63 && wait_frames(p, 1, 30000)) fl++;
        idle_cycles(300);
        chk(fl == 63,              "① 学表帧:其余 63 口各恰 1 份");
        chk(lrx[63].frames == 0,   "① 负控:入口(口63)不发");
        bool lb = true;
        for (int p = 0; p < 63 && lb; p++) lb = pay_eq(lrx[p].got[0], lf);
        chk(lb,                    "① 逐字节:63 份副本 == 学表帧");

        // ② 端到端一笔:口0 ⇒ 口63(单播命中)
        std::vector<unsigned char> u = mk_frame(M63, M0, 300, 0x50, true, 3);
        send(0, u);
        bool g = wait_frames(63, 1, 30000);
        idle_cycles(200);
        chk(g && lrx[63].frames == 1 && pay_eq(lrx[63].got[0], u), "② 口0→口63 端到端逐字节(仅目标口)");
        int extra = 0;
        for (int p = 0; p < 63; p++) if (lrx[p].frames != 1) extra++;
        chk(extra == 0,            "② 负控:其余口无新增(仍各 1)");

        // ③ 高水位 + ④ 计数
        unsigned int sumtx = 0, sumdp = 0;
        for (int p = 0; p < 64; p++) { sumtx += cnt(top.sw_tx_frames, p); sumdp += cnt(top.sw_drop, p); }
        printf("   读数:o_qmax[63]=%u;rx=[%u@0,%u@63];tx[63]=%u;Σtx=%u\n",
               cnt(top.sw_qmax, 63), cnt(top.sw_rx_frames, 0), cnt(top.sw_rx_frames, 63),
               cnt(top.sw_tx_frames, 63), sumtx);
        chk(cnt(top.sw_qmax, 63) >= 300, "③ 高水位 o_qmax[63] ≥ 300(读数在册)");
        chk(cnt(top.sw_rx_frames, 0) == 1 && cnt(top.sw_rx_frames, 63) == 1, "④ rx=[1@0,1@63]");
        chk(cnt(top.sw_tx_frames, 63) == 1, "④ tx[63] == 1(仅端到端那笔)");
        chk(sumtx == 64 && sumdp == 0, "④ Σtx == 64(63 份学表泛洪 + 1 笔端到端)、drop 全 0");
    }
};

int sc_main(int, char**) {
    sc_core::sc_clock clk("clk", 2.0, sc_core::SC_NS);
    sc_core::sc_signal<bool> rst;
    SwitchTop top("top", 64);
    top.clk_in(clk); top.rst_in(rst);
    Drv drv("drv", top, clk);
    drv.rst_o(rst);
    sc_core::sc_start();
    printf("TB_SW_SCALE64 %s\n", drv.fails ? "FAIL" : "PASS");
    return drv.fails ? 1 : 0;
}
