# 51.2T_switch

**51.2T 数据中心交换机(64×800G)系统级 TLM 参考模型** —— SystemC/TLM-2.0,面向 800G 交换机的
早期软件开发、无损网络(ROCEv2 无损以太)验证与回归测试。

- **不绑平台**:模型为标准 SystemC/TLM-2.0,端口线侧为 **1024b 双 lane(800G 档)接口**,
  可直接接入 Synopsys VDK / ARM Fast Models 等虚拟平台,或与本方既有 800G MAC/PCS 模型对接;
- **端口 PHY 复用本方已上线件**:每端口 = 800G MAC TLM(与 400G/800G RDMA 线**同一份**、契约逐字节一致)
  ⇒ 线侧判据全组平移,零回退;
- **自带回归**:模型自带 **16 个自测用例**,克隆即可复现 `全部 PASS (16/16)`;
- **架构对标**:64×800G / **8 组 × 8 口**(Tomahawk 级 pipes 组织);共享缓存 + 8 流量类 +
  无损(PFC)+ 拥塞管理(ECN/DCQCN 味)。

## Features

- **64 × 800G ports, 51.2 Tbps switching capacity** (organized as 8 groups × 8 ports, Tomahawk-class pipes);
- **L2 forwarding**: MAC learning, unicast hit forwarding, unknown-unicast / broadcast / multicast flooding, byte-exact pass-through;
- **VLAN domain isolation**: tagged frames egress only on member ports (non-members dropped and counted); untagged frames bypass;
- **Lossless (PFC)**: per-port per-class watermarks, PFC generation (XOFF/XON, 802.1Qbb-style fields)
  and PFC response (egress gating with timer) — closed-loop verified with zero drop;
- **Congestion management (ECN / DCQCN flavor)**: threshold-based CE marking (per-frame bit-level checkable)
  + CNP counting — the switch-side half of a DCQCN loop;
- **8 traffic classes** (VLAN PCP mapping) with strict-priority scheduling, control frames bypassing gates (deadlock-free);
- **Proven 800G port PHY**: the same 800G MAC TLM as the RDMA lines (1024b dual-lane line interface,
  contract-frozen, identical file);
- **Platform-agnostic**: plain SystemC/TLM-2.0, no vendor simulator required; **no QEMU-based simulation approximation required**;
- **Self-test suite**: 16 testcases, `全部 PASS (16/16)` reproducible locally.

## 目录结构

```text
.
├── model/                 # 模型本体
│   ├── port/              # 端口 PHY = 800G MAC TLM(线侧 1024b 双 lane;与 RDMA 线同一件)
│   ├── core/              # 交换核:L2 查表转发 + 8 类队列/水位 + PFC 生成/响应 + ECN 标记
│   ├── top/               # switch_top(N_PORTS):N × 端口 + 交换核,信号全自持
│   ├── tb/                # 自测 TB(16 台)+ 公共设施
│   ├── Makefile
│   └── regress.sh         # 回归收集器:全部 PASS ⇒ 退出码 0
└── docs/
    └── user_guide.md      # 用户指南(filelist / 依赖 / 构建 / 例化 / 集成 / FAQ)
```

## 环境要求

| 项 | 要求 |
|---|---|
| SystemC | 2.3.4(Accellera),含 TLM 2.0 |
| 编译器 | g++ ≥ 9(C++14) |
| OS | Linux(x86_64) |

## 构建与自测

```sh
cd model
make            # 编译全部 16 台自测
sh regress.sh   # 回归收集器;期望末行:全部 PASS ✓(16/16),退出码 0
```

单台运行(例):

```sh
./tb_sw_l2_uni          # L2 单播/学表/泛洪/同口过滤
./tb_sw_pfc lossless    # PFC 无损闭环(100 帧、0 丢)
./tb_sw_ecn mark        # ECN 阈值标记(逐帧位级)
./tb_sw_scale64         # 64 口全配置端到端一笔
./tb_sw_vlan            # VLAN 域隔离(成员口/跨域丢)
```

如 SystemC 不在默认路径,`make SYSTEMC_HOME=/path/to/systemc` 覆盖。

## 例化(最小骨架)

```cpp
#include "switch_top.h"

sc_clock clk("clk", 2.0, SC_NS);      // 行为级:DUT 单时钟
sc_signal<bool> rst;
SwitchTop top("top", 64);             // 64 × 800G(51.2T 盘面);功能台可用 2/3/4/8
top.clk_in(clk); top.rst_in(rst);

// 端口 i 的线侧(1024b 双 lane):
//   输入:line_rxd0/1[512]、line_rxc0/1[64]、line_ena_rx0/1
//   输出:line_txd0/1[512]、line_txc0/1[64]、line_dval0/1、line_ena_tx0/1
// 另:核计数面(每口)sw_rx_frames/sw_tx_frames/sw_drop/sw_pfc_tx/sw_pfc_rx/
//     sw_ecn_marked/sw_cnp_rx/sw_qmax,供监视/判读。

top.core.set_pfc(true);               // 使能无损(PFC 生成/响应)
top.core.set_wm(4096, 2048, 16384);   // 每口每类:XOFF/XON/上限(字节)
top.core.set_ecn(true, 1024);         // 使能 ECN 标记(阈值档,字节)
top.core.set_vlan(true);              // 使能 VLAN 域隔离(另 set_vlan_member(port,vid,true))
```

集成步骤与边界详见 [`docs/user_guide.md`](docs/user_guide.md)。
