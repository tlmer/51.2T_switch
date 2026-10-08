//============================================================================
// tb_sw_bringup.cpp — M0 冒烟:两档 N 起得来 + 全口计数零
// 判据(预登记):① N 顶层 elaboration 通过 ② 空闲跑 20ns:各口 o_rx_frames/o_drop_frames == 0
//   ③ 终判行 TB_SW_BRINGUP<N> PASS ✓(N 由 -DSW_N_PORTS 给;本台同时覆盖 2/64 两档)
//============================================================================
#include <systemc.h>
#include "switch_top.h"
#include <cstdio>
#ifndef SW_N_PORTS
#define SW_N_PORTS 2
#endif

int sc_main(int, char **) {
    sc_core::sc_clock clk("clk", 2.0, sc_core::SC_NS);
    sc_core::sc_signal<bool> rst;
    SwitchTop top("top", SW_N_PORTS);
    top.clk_in(clk); top.rst_in(rst);
    for (int i = 0; i < SW_N_PORTS; i++) {                 // 线侧 ena 全关(冒烟:空闲)
        top.line_ena_tx0[i]->write(false); top.line_ena_tx1[i]->write(false);
        top.line_ena_rx0[i]->write(false); top.line_ena_rx1[i]->write(false);
    }
    rst.write(false);
    sc_core::sc_start(4, sc_core::SC_NS);
    rst.write(true);
    sc_core::sc_start(20, sc_core::SC_NS);

    int fails = 0;
    for (int i = 0; i < SW_N_PORTS; i++) {
        if (top.sw_rx_frames[i]->read().to_uint() != 0) fails++;
        if (top.sw_drop[i]->read().to_uint()      != 0) fails++;
    }
    printf("   v N=%d 顶层 elaboration + 空闲 20ns: 异常口 = %d\n", SW_N_PORTS, fails);
    printf("TB_SW_BRINGUP%d %s\n", SW_N_PORTS, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
