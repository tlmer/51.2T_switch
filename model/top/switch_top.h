//============================================================================
// switch_top.h — 51.2T 交换机 TLM 顶层  [2026-10-08 建 / M0 骨架]
//
// 规格 = ../设计方案.md §1/§2.3;结构:N × SwPortPhy(= MacTlm@800G 档) + SwCore
//   线侧(每口)= LINK 1024b 双 lane(**800G 线 F1 契约原样**;dval/ena 由本顶层引出给 TB/上游)
//   核侧(每口)= 帧级(MAC 的 roverce_cmac_s_axis ⇄ 核 rx;核 tx ⇄ MAC 的 cmac_m_axis)
// ★ N 运行时参数化(默认 64 = 51.2T 盘面;功能台用小 N)✓
//============================================================================
#ifndef SWITCH_TOP_H
#define SWITCH_TOP_H

#include <systemc.h>
#include <vector>
#include "mac_tlm.h"
#include "sw_core.h"

class SwitchTop : public sc_module {
public:
    const int N;
    SwitchTop(sc_core::sc_module_name nm, int n)
    : sc_module(nm), N(n), clk_in("clk_in"), rst_in("rst_in"), core("core", n) {
        for (int i = 0; i < N; i++) {
            macs.push_back(new MacTlm(sc_gen_unique_name("mac")));
            macs[i]->set_lane_mode(MacTlm::LM_800G);                 // ★ 800G 档(契约 F1)✓
            macs[i]->clk(clk_in); macs[i]->rst_n(rst_in);

            // 线侧信号(LINK 1024b 双 lane;方向照 mac_tlm 端口表)
            auto* ldv0 = new sc_signal<bool>; auto* ldv1 = new sc_signal<bool>;
            auto* ltd0 = new sc_signal<sc_biguint<512>>; auto* ltd1 = new sc_signal<sc_biguint<512>>;
            auto* ltc0 = new sc_signal<sc_biguint<64>>;  auto* ltc1 = new sc_signal<sc_biguint<64>>;
            auto* ldr0 = new sc_signal<sc_biguint<512>>; auto* ldr1 = new sc_signal<sc_biguint<512>>;
            auto* lcr0 = new sc_signal<sc_biguint<64>>;  auto* lcr1 = new sc_signal<sc_biguint<64>>;
            auto* ltn0 = new sc_signal<bool>; auto* ltn1 = new sc_signal<bool>;
            auto* lrn0 = new sc_signal<bool>; auto* lrn1 = new sc_signal<bool>;
            line_dval0.push_back(ldv0); line_dval1.push_back(ldv1);
            line_txd0.push_back(ltd0);  line_txd1.push_back(ltd1);
            line_txc0.push_back(ltc0);  line_txc1.push_back(ltc1);
            line_rxd0.push_back(ldr0);  line_rxd1.push_back(ldr1);
            line_rxc0.push_back(lcr0);  line_rxc1.push_back(lcr1);
            line_ena_tx0.push_back(ltn0); line_ena_tx1.push_back(ltn1);
            line_ena_rx0.push_back(lrn0); line_ena_rx1.push_back(lrn1);

            macs[i]->link_txdval_0(*ldv0); macs[i]->link_txdval_1(*ldv1);
            macs[i]->link_txd_0(*ltd0);    macs[i]->link_txd_1(*ltd1);
            macs[i]->link_txc_0(*ltc0);    macs[i]->link_txc_1(*ltc1);
            macs[i]->link_rxd_0(*ldr0);    macs[i]->link_rxd_1(*ldr1);
            macs[i]->link_rxc_0(*lcr0);    macs[i]->link_rxc_1(*lcr1);
            macs[i]->link_txclk_ena_0(*ltn0); macs[i]->link_txclk_ena_1(*ltn1);
            macs[i]->link_rxclk_ena_0(*lrn0); macs[i]->link_rxclk_ena_1(*lrn1);

            // 核侧信号:线→核(MAC 的 roce_cmac_s_axis)与核→线(MAC 的 cmac_m_axis)
            auto* rv = new sc_signal<bool>; auto* rd = new sc_signal<sc_biguint<2048>>;
            auto* rk = new sc_signal<sc_biguint<256>>; auto* rl = new sc_signal<bool>;
            auto* tu = new sc_signal<bool>;
            auto* cv = new sc_signal<bool>; auto* cd = new sc_signal<sc_biguint<2048>>;
            auto* ck = new sc_signal<sc_biguint<256>>; auto* cl = new sc_signal<bool>;
            auto* cr = new sc_signal<bool>;
            r_v.push_back(rv); r_d.push_back(rd); r_k.push_back(rk); r_l.push_back(rl);
            c_v.push_back(cv); c_d.push_back(cd); c_k.push_back(ck); c_l.push_back(cl); c_r.push_back(cr);

            macs[i]->roce_cmac_s_axis_tvalid(*rv); macs[i]->roce_cmac_s_axis_tdata(*rd);
            macs[i]->roce_cmac_s_axis_tkeep(*rk);  macs[i]->roce_cmac_s_axis_tlast(*rl);
            macs[i]->roce_cmac_s_axis_tuser(*tu);
            macs[i]->cmac_m_axis_tvalid(*cv); macs[i]->cmac_m_axis_tdata(*cd);
            macs[i]->cmac_m_axis_tkeep(*ck);  macs[i]->cmac_m_axis_tlast(*cl);
            macs[i]->cmac_m_axis_tready(*cr);

            // ★ MacTlm 的 5 个观测输出口(⚠ 新增 sc_out ⇒ 全宿主必绑;E109 血课)✓
            auto* o1 = new sc_signal<sc_uint<32>>;   auto* o2 = new sc_signal<sc_uint<32>>;
            auto* o3 = new sc_signal<sc_uint<32>>;   auto* o4 = new sc_signal<sc_uint<32>>;
            auto* o5 = new sc_signal<sc_uint<32>>;
            o_ftx.push_back(o1); o_frx.push_back(o2); o_idle.push_back(o3);
            o_fcs.push_back(o4); o_lm.push_back(o5);
            macs[i]->o_frames_tx(*o1); macs[i]->o_frames_rx(*o2);
            macs[i]->o_idle_beats(*o3); macs[i]->o_fcs_err(*o4);
            macs[i]->o_lane_mismatch_cnt(*o5);

            // 核端口绑定(*指针端口)
            (*core.rx_tvalid[i])(*rv); (*core.rx_tdata[i])(*rd);
            (*core.rx_tkeep[i])(*rk);  (*core.rx_tlast[i])(*rl);
            (*core.tx_tvalid[i])(*cv); (*core.tx_tdata[i])(*cd);
            (*core.tx_tkeep[i])(*ck);  (*core.tx_tlast[i])(*cl);
            (*core.tx_ready[i])(*cr);

            // 核计数输出(face F5)⇒ 本顶层引出为信号(供 TB 判读)✓
            auto* s1 = new sc_signal<sc_uint<32>>;   auto* s2 = new sc_signal<sc_uint<32>>;
            auto* s3 = new sc_signal<sc_uint<32>>;
            auto* s4 = new sc_signal<sc_uint<32>>;   auto* s5 = new sc_signal<sc_uint<32>>;
            auto* s6 = new sc_signal<sc_uint<32>>;   auto* s7 = new sc_signal<sc_uint<32>>;
            auto* s8 = new sc_signal<sc_uint<32>>;   auto* s9 = new sc_signal<sc_uint<32>>;
            auto* s10 = new sc_signal<sc_uint<32>>;
            sw_rx_frames.push_back(s1); sw_rx_bytes.push_back(s2); sw_drop.push_back(s3);
            sw_tx_frames.push_back(s4); sw_tx_bytes.push_back(s5);      // ★ M1 新增(E109:必绑)✓
            sw_pfc_tx.push_back(s6);    sw_pfc_rx.push_back(s7);        // ★ M2 新增(E109:必绑)✓
            sw_ecn_marked.push_back(s8); sw_cnp_rx.push_back(s9);       // ★ M3 新增(E109:必绑)✓
            sw_qmax.push_back(s10);                                     // ★ M4 新增(E109:必绑)✓
            (*core.o_rx_frames[i])(*s1); (*core.o_rx_bytes[i])(*s2); (*core.o_drop_frames[i])(*s3);
            (*core.o_tx_frames[i])(*s4); (*core.o_tx_bytes[i])(*s5);
            (*core.o_pfc_tx[i])(*s6); (*core.o_pfc_rx[i])(*s7);
            (*core.o_ecn_marked[i])(*s8); (*core.o_cnp_rx[i])(*s9); (*core.o_qmax[i])(*s10);
        }
        core.clk(clk_in); core.rst_n(rst_in);       // ⚠ 只绑一次(在循环外)✓
    }

    sc_in<bool> clk_in, rst_in;

    // 线侧(暴露给 TB / 上游;每组 8 位/位宽照 mac_tlm 端口表)
    std::vector<sc_signal<bool>*>              line_dval0, line_dval1;
    std::vector<sc_signal<sc_biguint<512>>*>   line_txd0, line_txd1;
    std::vector<sc_signal<sc_biguint<64>>*>    line_txc0, line_txc1;
    std::vector<sc_signal<sc_biguint<512>>*>   line_rxd0, line_rxd1;
    std::vector<sc_signal<sc_biguint<64>>*>    line_rxc0, line_rxc1;
    std::vector<sc_signal<bool>*>              line_ena_tx0, line_ena_tx1, line_ena_rx0, line_ena_rx1;

    // 核侧观测(线→核 的既有信号,供 TB 判读)
    std::vector<sc_signal<bool>*>              r_v, r_l;
    std::vector<sc_signal<sc_biguint<2048>>*>  r_d;
    std::vector<sc_signal<sc_biguint<256>>*>   r_k;
    // 核→线 的既有信号(供 TB 驱动/判读;M1 起由核驱动,当前仅暴露)
    std::vector<sc_signal<bool>*>              c_v, c_l, c_r;
    std::vector<sc_signal<sc_biguint<2048>>*>  c_d;
    std::vector<sc_signal<sc_biguint<256>>*>   c_k;

    // 核计数(每口;face F5)
    std::vector<sc_signal<sc_uint<32>>*> sw_rx_frames, sw_rx_bytes, sw_drop;
    std::vector<sc_signal<sc_uint<32>>*> sw_tx_frames, sw_tx_bytes;   // ★ M1 新增
    std::vector<sc_signal<sc_uint<32>>*> sw_pfc_tx, sw_pfc_rx;         // ★ M2 新增
    std::vector<sc_signal<sc_uint<32>>*> sw_ecn_marked, sw_cnp_rx;     // ★ M3 新增
    std::vector<sc_signal<sc_uint<32>>*> sw_qmax;                      // ★ M4 新增
    // MAC 观测口(每口一组;供 TB 判读)
    std::vector<sc_signal<sc_uint<32>>*> o_ftx, o_frx, o_idle, o_fcs, o_lm;

    std::vector<MacTlm*> macs;
    SwCore core;
};

#endif // SWITCH_TOP_H
