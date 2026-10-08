//============================================================================
// tb_sw_ecn.cpp — M3 拥塞管理(ECN/WRED 味)两模式台(N=2,线侧 LINK 端到端)  [2026-10-09 建]
//
// 用法:`./tb_sw_ecn mark|count`(判据 = 验证方案.md §2-L3 预登记)✓
//
// 口径(明写,不发明):
//   · ECN = IPv4 TOS 低 2 位(直连 0x0800 ⇒ TOS@[15];802.1Q ⇒ TOS@[19]);标记 = 置 **CE(11)** ✓
//   · 标记时机 = **入队时** `occ > ecn_min_bytes`(单阈值档;⛔ 概率档 maxp 未做)✓
//   · CNP 味判据 = IPv4 ∧ proto=17 ∧ UDP dport=**4791** ∧ BTH opcode=**0x81**(照常转发,只计数)✓
//
// 模式判据(预登记):
//   mark :① 阈值下 ⇒ **不置**(负控:阈值调到极大 ⇒ 全帧 TOS.ECN == 00)
//         ② 过阈 ⇒ **置 CE(11)**(灌爆过阈,出口帧逐帧位级可检:有 CE 且无假 CE)
//   count:① `o_ecn_marked[1]` == 出线上实测 CE 帧数(计数对表)
//         ② `o_cnp_rx[0]` == 1(注入 1 笔 CNP 味;负控:TCP 帧/UDP 非 4791 ⇒ 不计数)✓
//============================================================================
#include "sw_l2_common.h"
#include <cstring>

static const unsigned char MA0[6] = {0x02,0,0,0,0,0x01};
static const unsigned char MA1[6] = {0x02,0,0,0,0,0x02};

// IPv4 帧(dst+src+[vlan]+0800+20B IP 头+数据);ecn = TOS 低 2 位;proto:6=TCP / 17=UDP
static std::vector<unsigned char> mk_ip(const unsigned char* dst, const unsigned char* src,
                                        int total, unsigned char seed,
                                        unsigned char ecn = 0, unsigned char proto = 6) {
    std::vector<unsigned char> f;
    for (int i = 0; i < 6; i++) f.push_back(dst[i]);
    for (int i = 0; i < 6; i++) f.push_back(src[i]);
    f.push_back(0x08); f.push_back(0x00);
    f.push_back(0x45);                       // version 4 / IHL 5
    f.push_back((unsigned char)(ecn & 0x03));
    f.push_back(0x00); f.push_back(0x40);    // len(不校)
    f.push_back(0x00); f.push_back(0x01);    // id
    f.push_back(0x40); f.push_back(0x00);    // flags/frag
    f.push_back(0x40); f.push_back(proto);   // ttl / proto
    f.push_back(0x00); f.push_back(0x00);    // csum(不校)
    f.push_back(10);  f.push_back(0); f.push_back(0); f.push_back(1);
    f.push_back(10);  f.push_back(0); f.push_back(0); f.push_back(2);
    while ((int)f.size() < total) f.push_back((unsigned char)(seed + f.size()*3));
    return f;
}
// CNP 味帧:IPv4/UDP(dport 4791)/BTH opcode=0x81
static std::vector<unsigned char> mk_cnp(const unsigned char* dst, const unsigned char* src) {
    std::vector<unsigned char> f = mk_ip(dst, src, 64, 0x90, 0, 17);
    int udp0 = 34;                                            // 14 + IHL(20)
    f[udp0+0]=0xB1; f[udp0+1]=0xC1;                           // sport(不校)
    f[udp0+2]=0x12; f[udp0+3]=0xB7;                           // dport = 4791 ✓
    f[udp0+4]=0x00; f[udp0+5]=0x20;                           // len
    f[udp0+6]=0; f[udp0+7]=0;                                 // csum
    f[udp0+8]=0x81;                                           // BTH opcode = CNP ✓
    return f;
}
static unsigned char ecn_of(const std::vector<unsigned char>& f) { return (unsigned char)(f[15] & 0x03); }

//----------------------------------------------------------------------------
struct DrvMark : SwL2Bench {
    DrvMark(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        top.core.set_pfc(false);
        top.core.set_wm(1u<<30, 1u<<29, 1u<<30);              // 关 PFC/不丢(本台只看 ECN)
        printf("== TB_SW_ECN_MARK(阈值/置位/负控)==\n");
        send(1, mk_frame(MA0, MA1, 64, 0x10));                // 学表:MA1 ⇒ 口1
        idle_cycles(200);

        // ① 负控:阈值极大 ⇒ 一个都不置
        top.core.set_ecn(true, 1000000);
        const int A = 20;
        std::vector<std::vector<unsigned char> > expA;
        for (int i = 0; i < A; i++) { expA.push_back(mk_ip(MA1, MA0, 200, (unsigned char)(0x20+i))); send(0, expA[i]); }
        for (int k = 0; k < 4000 && scan_data(1, 0) < A; k++) { wait(clk.posedge_event()); pump(); }
        int nA = 0, ceA = 0;
        for (size_t i = 0; i < lrx[1].got.size(); i++) {
            if (sw_parse_pfc(lrx[1].got[i]).is_pfc) continue;
            nA++;
            if (ecn_of(lrx[1].got[i]) == 0x3) ceA++;
        }
        printf("   读数A(阈值下):到 %d 帧,其中 CE=%d\n", nA, ceA);
        chk(nA == A && ceA == 0, "① 负控:阈值下 ⇒ 不置 CE(全 00)");
        chk(cnt(top.sw_ecn_marked, 1) == 0, "① 计数 o_ecn_marked[1] == 0");

        // ② 过阈 ⇒ 置 CE(11)
        top.core.set_ecn(true, 1024);
        size_t base1 = lrx[1].got.size();
        set_throttle(1, 1, 15);
        const int B = 40;
        for (int i = 0; i < B; i++) send(0, mk_ip(MA1, MA0, 200, (unsigned char)(0x60+i)));
        idle_cycles(1500);
        set_throttle(1, 1, 0);
        for (int k = 0; k < 8000 && scan_data(1, 0) < A + B; k++) { wait(clk.posedge_event()); pump(); }
        int nB = 0, ceB = 0, bad_ce = 0;
        for (size_t i = base1; i < lrx[1].got.size(); i++) {
            if (sw_parse_pfc(lrx[1].got[i]).is_pfc) continue;
            nB++;
            unsigned char e = ecn_of(lrx[1].got[i]);
            if (e == 0x3) ceB++;
            else if (e != 0x0) bad_ce++;                      // 非 00 非 11 ⇒ 形状坏
        }
        printf("   读数B(过阈):到 %d 帧,其中 CE=%d,形状坏=%d;计数 o_ecn_marked[1]=%u\n",
               nB, ceB, bad_ce, cnt(top.sw_ecn_marked, 1));
        chk(nB == B,       "② 40 帧全到");
        chk(ceB >= 1,      "② 过阈 ⇒ 有 CE(11) 置位");
        chk(bad_ce == 0,   "② 无形状坏(逐帧位级:非 00 即 11)");
        chk(cnt(top.sw_ecn_marked, 1) == (unsigned)ceB, "② 计数 == 出线上实测 CE 帧数");
        chk(cnt(top.sw_drop, 0) == 0 && cnt(top.sw_drop, 1) == 0, "② drop == 0");
    }
};

//----------------------------------------------------------------------------
struct DrvCount : SwL2Bench {
    DrvCount(sc_core::sc_module_name nm, SwitchTop& t, sc_core::sc_clock& c) : SwL2Bench(nm, t, c) {}
    void tests() {
        top.core.set_pfc(false);
        top.core.set_wm(1u<<30, 1u<<29, 1u<<30);
        top.core.set_ecn(true, 1024);
        printf("== TB_SW_ECN_COUNT(计数对表 + CNP 味)==\n");
        send(1, mk_frame(MA0, MA1, 64, 0x10));
        idle_cycles(200);
        // 灌爆(过阈 ⇒ 有标记)
        set_throttle(1, 1, 15);
        const int K = 30;
        for (int i = 0; i < K; i++) send(0, mk_ip(MA1, MA0, 200, (unsigned char)(0x60+i)));
        idle_cycles(1500);
        set_throttle(1, 1, 0);
        for (int k = 0; k < 8000 && scan_data(1, 0) < K; k++) { wait(clk.posedge_event()); pump(); }
        int n = 0, ce = 0;
        for (size_t i = 0; i < lrx[1].got.size(); i++) {
            if (sw_parse_pfc(lrx[1].got[i]).is_pfc) continue;
            n++; if (ecn_of(lrx[1].got[i]) == 0x3) ce++;
        }
        printf("   读数:到 %d 帧,CE=%d;o_ecn_marked[1]=%u\n", n, ce, cnt(top.sw_ecn_marked, 1));
        chk(n == K && ce >= 1, "① 过阈流量到齐且有 CE");
        chk(cnt(top.sw_ecn_marked, 1) == (unsigned)ce, "① o_ecn_marked[1] == 实测 CE 帧数");
        // ② CNP 味:注入 1 笔 ⇒ 计数 1;负控:TCP 帧 / UDP 非 4791 ⇒ 不加
        send(0, mk_cnp(MA1, MA0));                            // CNP(应计数 + 照常转发)
        idle_cycles(400);
        unsigned int cnp_after1 = cnt(top.sw_cnp_rx, 0);
        send(0, mk_ip(MA1, MA0, 64, 0xA0, 0, 6));             // 负控:TCP
        std::vector<unsigned char> bad = mk_cnp(MA1, MA0);
        bad[37] = 0x00; bad[36] = 0x50;                       // 负控:dport 改成 80(非 4791)
        send(0, bad);
        idle_cycles(400);
        printf("   读数:o_cnp_rx[0]=%u(注入 1 笔 CNP + 2 笔负控后仍应 ==1)\n", cnt(top.sw_cnp_rx, 0));
        chk(cnp_after1 == 1,                 "② CNP 味计数 == 1");
        chk(cnt(top.sw_cnp_rx, 0) == 1,      "② 负控:TCP / UDP 非 4791 ⇒ 不计数");
        chk(scan_data(1, 0) >= K + 3,        "② CNP 帧照常转发(未被吞)");
    }
};

int sc_main(int argc, char* argv[]) {
    const char* mode = (argc > 1) ? argv[1] : "mark";
    sc_core::sc_clock clk("clk", 2.0, sc_core::SC_NS);
    sc_core::sc_signal<bool> rst;
    SwitchTop top("top", 2);
    top.clk_in(clk); top.rst_in(rst);
    SwL2Bench* drv;
    const char* up;
    if (!strcmp(mode, "mark")) { drv = new DrvMark ("drv", top, clk); up = "MARK"; }
    else                       { drv = new DrvCount("drv", top, clk); up = "COUNT"; }
    drv->rst_o(rst);
    sc_core::sc_start();
    printf("TB_SW_ECN_%s %s\n", up, drv->fails ? "FAIL" : "PASS");
    return drv->fails ? 1 : 0;
}
