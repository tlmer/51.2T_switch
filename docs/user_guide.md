# 51.2T 交换机 TLM 模型 —— 客户用户指南  [2026-10-09 建 / v1]

> 本册面向**集成方**:交付清单、依赖、构建、例化、集成步骤、边界与 FAQ ✓
> 模型 = SystemC/TLM-2.0 行为级参考模型(64×800G 交换机);⛔ 不做逐拍 RTL 级细节 ✓

## §0 交付形态一览

| 件 | 形态 |
|---|---|
| 模型本体 | `model/{port,core,top}`(C++/SystemC 源) |
| 自测 | `model/tb/`(16 台)+ `model/regress.sh`(收集器,全绿 ⇒ 退出码 0) |
| 构建 | `model/Makefile`(`SYSTEMC_HOME` 可覆盖) |
| 文档 | 本册;仓库 `README.md` |

## §1 Filelist(交付清单)

```text
model/port/mac_tlm.{h,cpp}      # 端口 PHY = 800G MAC TLM(线侧 1024b 双 lane)
model/core/sw_core.h            # 交换核:L2 查表转发 / 8 类队列 / PFC / ECN(头文件)
model/top/switch_top.h          # 顶层:SwitchTop(N)× 端口 + 核,信号全自持
model/tb/sw_l2_common.h         # TB 公共设施(线帧构建/驱动/重组;端口 TB 皆用)
model/tb/tb_sw_*.cpp            # 自测台(见 §8)
model/tb/tb_mac_loopback.cpp    # 端口 PHY 回归(与 RDMA 线同一台,逐字节平移)
model/tb/tb_mac_lane800.cpp     # 端口 PHY 档位/契约回归(同上)
model/Makefile                  # 构建(16 台)
model/regress.sh                # 回归收集器
```

## §2 依赖性

| 项 | 说明 |
|---|---|
| SystemC 2.3.4 | Accellera 标准件(含 TLM-2.0);**唯一必需第三方** |
| g++ ≥ 9 | C++14;`-O1` 即可(自测全部构建于 `-std=c++14 -O1`) |
| OS | Linux x86_64(模型本身无平台依赖,任何能跑 SystemC 的环境均可) |

## §3 构建命令

```sh
cd model
make                              # 全部 15 台自测
sh regress.sh                     # 回归;期望末行:全部 PASS ✓(16/16)

# 覆盖依赖路径(客户环境):
make SYSTEMC_HOME=/your/systemc
```

## §4 例化模板与绑定要点

### 4.1 快启

```cpp
#include "switch_top.h"

sc_clock clk("clk", 2.0, SC_NS);       // 行为级单时钟(2ns 仅为自测节奏;可自定)
sc_signal<bool> rst;
SwitchTop top("top", 64);              // 端口数 N:64 = 51.2T 盘面;功能台可用 2/3/4/8
top.clk_in(clk); top.rst_in(rst);
```

### 4.2 线侧接口(每端口)

| 方向 | 信号 | 位宽 | 语义 |
|---|---|---|---|
| 入 | `line_rxd0/1` | 512 | 线侧收数据(双 lane,各 64B/拍) |
| 入 | `line_rxc0/1` | 64 | 线侧收控制位(1=控制字符) |
| 入 | `line_ena_rx0/1` | 1 | 收侧时钟使能(行为级节流;两 lane 须同值) |
| 出 | `line_txd0/1` | 512 | 线侧发数据 |
| 出 | `line_txc0/1` | 64 | 线侧发控制位 |
| 出 | `line_dval0/1` | 1 | 线侧发有效(使能期内持续) |
| 入 | `line_ena_tx0/1` | 1 | 发侧时钟使能(可做**慢排节流**,自测即用此造拥塞) |

⚠ 线帧与空闲拍口径:帧 = `START(ctrl) + 6×PRE + SFD + 载荷 + FCS(以太网 CRC32) + TERM(ctrl)`;
空闲 = 字节 0x07 且控制位全 1;`LINK` 128 流字节/拍 = lane0(0..63)+ lane1(64..127),
字内 **REV 序** —— 细节见 `model/port/mac_tlm.cpp` 头注(与 RDMA 线**同一件**,契约已冻结)✓

### 4.3 交换核参数(运行前设;默认 = 全关,行为等价于纯 L2 转发)

```cpp
top.core.set_pfc(true);                 // 无损:PFC 生成(XOFF/XON)+ 响应(闸门)
top.core.set_wm(xoff, xon, max_bytes);  // 每口每类水位(字节);过 max ⇒ 丢弃(计数非静默)
top.core.set_ecn(true, min_bytes);      // 拥塞管理:队列水位过 min ⇒ 出口帧置 CE(11)
top.core.set_vlan(true);                // VLAN 域隔离;成员表:set_vlan_member(port, vid, true)
```

### 4.4 计数面(每口;监视/判读用)

`sw_rx_frames` / `sw_tx_frames` / `sw_drop` / `sw_pfc_tx` / `sw_pfc_rx` /
`sw_ecn_marked` / `sw_cnp_rx` / `sw_qmax`(排队高水位,字节)✓

## §5 集成步骤(建议顺序)

1. 克隆 ⇒ `cd model && make && sh regress.sh` ⇒ **`全部 PASS ✓(16/16)`**(先确认基线);
2. 选 N(功能台 2/3/4/8;全配置 64),例化 `SwitchTop`;
3. 按 §4.2 把每端口的线侧接到你的流量源/宿(或本方 800G MAC/PCS 模型/真 PCS 的 TLM 侧);
4. 运行前设 §4.3 参数(无损/ECN 按需);
5. 用 §4.4 计数面与(可选)线侧抓帧做判读 —— 判据口径照 `model/tb/` 各台头注(预登记)✓

## §6 接口契约与版本

- **线侧(端口 PHY)= 本方 800G MAC TLM 契约**(1024b 双 lane;`dval/ena` 双 lane 同值;
  帧格式/空闲/REV 字序),与 RDMA 线**同一份文件**⇒ 既有线侧判据全组平移 ✓
- 核侧=内部件(帧级);计数面 §4.4 = 监视口径(**累加器**,复位清零)✓
- 版本:本册随发布版走(1.0)✓

## §7 已知差异与边界(集成前必读)

| # | 边界(明写,不藏) |
|---|---|
| 1 | **行为级**:帧级流水(收全帧→查表→入队→调度),⛔ 非逐拍内部仲裁/背板细节 |
| 2 | 组播/广播 = **泛洪**(无组成员表);**VLAN 域隔离 ✓ 已做**(带签帧仅成员口出、非成员丢+计数;未带签旁路) |
| 3 | ECN = **阈值档**(入队 `occ > min` ⇒ 置 CE(11));⛔ WRED 概率档(maxp)未做 |
| 4 | 缓存 = **每口每类独立水位**;共享缓存池/每口配额(池化)未做 |
| 5 | PFC 位级字段 = **简版口径**:class-enable-vector 位 = "本帧就该类表态"(time>0 关 / t=0 开);⛔ 严格 802.1Qbb 的多类联合编码未做 |
| 6 | DCQCN **端点闭环不在本模型**(只做交换机半边:CE 标记 + CNP 计数) |
| 7 | 调度 = 类 7..0 **严格优先**(控制帧最高且绕过闸门);⛔ WRR 权重档未做 |
| 8 | 帧上限 4096B(缓冲上限;超 ⇒ 丢弃计数);⛔ 巨帧/Jumbo 超限行为 = 丢 |

## §8 自检与回归(交付验收自带)

```sh
cd model && sh regress.sh        # ⇒ 全部 PASS ✓(16/16)
```

| 台 | 覆盖 |
|---|---|
| `tb_sw_bringup2` / `tb_sw_bringup64` | 顶层 elaboration(N=2/64)+ 空闲计数零 |
| `tb_sw_l2_uni` | 学表/单播命中/泛洪/同口过滤 + 计数面(逐字节) |
| `tb_sw_l2_flood` / `tb_sw_l2_mcast` | 未知单播泛洪 / 广播·组播(负控:入口不发) |
| `tb_sw_pfc xoff/gate/lossless/off` | PFC 生成(逐字段)/闸门/无损闭环(**100 帧 0 丢**)/负控(关 PFC ⇒ 溢出丢可见) |
| `tb_sw_ecn mark/count` | CE 阈值标记(逐帧位级)/ CNP 计数(负控) |
| `tb_sw_scale64` / `tb_sw_stress_q` | 64 口端到端一笔 + 高水位 / 七口并发灌同口 + 自洽 |
| `tb_sw_vlan` | VLAN 域隔离(同域单播/跨域丢+计数/广播成员域/未带签旁路) |
| `tb_mac_loopback` / `tb_mac_lane800` | ★ 端口 PHY 护城河(与 RDMA 线同名台、逐字节平移) |

## §9 常见问题(FAQ)

- **Q: 为什么端口线侧是 512b×2?** A: 800G 档 = 1024b/拍,按双 lane(各 512b/64B)呈现;
  400G 档同模型支持(仅 lane0 有效,见 TB 用法)✓
- **Q: 跑 N=64 很慢?** A: 64 口每拍全激活;功能验证建议用 N=2..8,`tb_sw_scale64` 已覆盖全配置冒烟 ✓
- **Q: PFC/ECN 默认为什么是关的?** A: 保持"纯 L2 转发"最小行为;按需 `set_pfc/set_ecn` 打开 ✓
- **Q: 计数怎么清零?** A: 拉低 `rst_in` 一拍即清零全部累加器 ✓
- **Q: 想换自己的端口模型?** A: `top/switch_top.h` 里端口是独立件(仅依赖线侧信号 + 核侧帧接口),
  可替换后保留 `SwCore` 接线 ✓
