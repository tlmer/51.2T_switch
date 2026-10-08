//============================================================================
// tb_sw_vlan.cpp — VLAN 域隔离(N=3,线侧 LINK 端到端)  [2026-10-09 建]
//
// 覆盖 = 验证方案.md §2-L1 表第 4 行「tb_sw_vlan:VLAN 域隔离(负控:跨域帧不转发 + 计数)」✓
// 口径(明写,不发明):带签帧(0x8100)**仅在 VID 成员口出**;非成员 ⇒ **丢 + 计数(不静默)**;
//   **未带签帧旁路过滤**(照 M1 行为)✓
//
// 判据(预登记):
//   基准:先学表(关 VLAN):口1 发 tag100、口2 发 tag200(各自泛洪到另两口的 0 份/1 份照 M1)✓
//   ① 同域单播:口0 发 tag100 dst=M1 ⇒ **仅口1** 逐字节;口2 无 ✓
//   ② 跨域负控:口0 发 tag200 dst=M1(口1 非 200 成员)⇒ **不转发** + `o_drop[1]` 增 1 ✓
//   ③ 广播成员域:口0 发 tag100 广播 ⇒ 仅口1(成员);口2 非成员 ⇒ 丢 + `o_drop[2]` 增 1 ✓
//   ④ 未带签旁路:口0 发未带签 dst=M1 ⇒ 照常到口1(逐字节)✓
//   ⑤ 计数面:rx=[4,1,1] tx=[2,4,1] drop=[0,1,1] ✓
//============================================================================
#include "sw_l2_common.h"

struct Drv : SwL2Bench {
    Drv(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        const unsigned char M0[6] = {0x02,0,0,0,0,0x01};
        const unsigned char M1[6] = {0x02,0,0,0,0,0x02};
        const unsigned char M2[6] = {0x02,0,0,0,0,0x03};
        const unsigned char BC[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
        printf("== TB_SW_VLAN(3 口;VLAN 域隔离)==\n");

        // ---- 基准:学表(VLAN 关)----
        send(1, mk_frame(M0, M1, 100, 0x11, true, 0, 100));      // tag100,src=M1 ⇒ 泛洪到 0/2
        wait_frames(0, 1, 3000); wait_frames(2, 1, 3000);
        send(2, mk_frame(M0, M2, 100, 0x12, true, 0, 200));      // tag200,src=M2 ⇒ 泛洪到 0/1
        wait_frames(0, 2, 3000); wait_frames(1, 1, 3000);
        idle_cycles(200);
        chk(lrx[0].frames == 2 && lrx[1].frames == 1 && lrx[2].frames == 1, "基准:学表泛洪照 M1 各就位");
        size_t b1 = lrx[1].got.size(), b2 = lrx[2].got.size();

        // ---- ★ 开 VLAN + 成员表:口1∈{100},口2∈{200} ----
        top.core.set_vlan_member(1, 100, true);
        top.core.set_vlan_member(2, 200, true);
        top.core.set_vlan(true);

        // ① 同域单播
        std::vector<unsigned char> f100 = mk_frame(M1, M0, 200, 0x21, true, 0, 100);
        send(0, f100);
        bool g1 = wait_frames(1, 2, 3000);
        idle_cycles(300);
        chk(g1 && lrx[1].frames == 2 && pay_eq(lrx[1].got[b1], f100), "① 同域单播:tag100 ⇒ 仅口1、逐字节");
        chk(lrx[2].frames == 1,                                    "① 负控:口2 无新增");

        // ② 跨域负控(tag200 去口1 ⇒ 非成员)
        unsigned int d1 = cnt(top.sw_drop, 1);
        send(0, mk_frame(M1, M0, 200, 0x22, true, 0, 200));
        idle_cycles(600);
        chk(lrx[1].frames == 2,                                    "② 跨域:口1 无投递");
        chk(cnt(top.sw_drop, 1) == d1 + 1,                         "② 跨域:o_drop[1] 增 1(不静默)");

        // ③ 广播成员域
        std::vector<unsigned char> bc = mk_frame(BC, M0, 150, 0x23, true, 0, 100);
        unsigned int d2 = cnt(top.sw_drop, 2);
        send(0, bc);
        bool g3 = wait_frames(1, 3, 3000);
        idle_cycles(300);
        chk(g3 && lrx[1].frames == 3 && pay_eq(lrx[1].got[b1 + 1], bc), "③ 广播 tag100:仅口1(成员)收到");
        chk(lrx[2].frames == 1 && cnt(top.sw_drop, 2) == d2 + 1,        "③ 负控:口2 非成员 ⇒ 无投递 + 丢计数");

        // ④ 未带签旁路
        std::vector<unsigned char> unt = mk_frame(M1, M0, 180, 0x24);
        send(0, unt);
        bool g4 = wait_frames(1, 4, 3000);
        idle_cycles(200);
        chk(g4 && lrx[1].frames == 4 && pay_eq(lrx[1].got[b1 + 2], unt), "④ 未带签:旁路过滤,照常投递逐字节");

        // ⑤ 计数面
        printf("   计数 rx=[%u,%u,%u] tx=[%u,%u,%u] drop=[%u,%u,%u]\n",
               cnt(top.sw_rx_frames,0), cnt(top.sw_rx_frames,1), cnt(top.sw_rx_frames,2),
               cnt(top.sw_tx_frames,0), cnt(top.sw_tx_frames,1), cnt(top.sw_tx_frames,2),
               cnt(top.sw_drop,0),      cnt(top.sw_drop,1),      cnt(top.sw_drop,2));
        chk(cnt(top.sw_rx_frames,0)==4 && cnt(top.sw_rx_frames,1)==1 && cnt(top.sw_rx_frames,2)==1, "计数 rx=[4,1,1]");
        chk(cnt(top.sw_tx_frames,0)==2 && cnt(top.sw_tx_frames,1)==4 && cnt(top.sw_tx_frames,2)==1, "计数 tx=[2,4,1]");
        chk(cnt(top.sw_drop,0)==0 && cnt(top.sw_drop,1)==1 && cnt(top.sw_drop,2)==1,                "计数 drop=[0,1,1]");
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
    printf("TB_SW_VLAN %s\n", drv.fails ? "FAIL" : "PASS");
    return drv.fails ? 1 : 0;
}
