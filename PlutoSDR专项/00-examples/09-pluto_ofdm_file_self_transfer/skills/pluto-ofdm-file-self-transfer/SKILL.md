---
name: pluto-ofdm-file-self-transfer
description: PlutoSDR(AD9361) OFDM 文件自收发/两机收发 —— liquid-dsp ofdmflexframegen/ofdmflexframesync、S0/S1 同步、文件头帧+数据帧、重复分片错开、两遍同步解码、文件 CRC、真机 libiio 收发框架、仿真回归流程。改 ofdm_phy.c/h、pluto_ofdm_file_self_transfer.c、makefile，或排查"OFDM 锁不上帧/header 解不出/漏 chunk/恢复文件 CRC 不过/两机频偏太大"时用本技能。
---

# PlutoSDR OFDM 文件自收发

工程根目录：本文件所在目录往上三级（即 `pluto_ofdm_file_self_transfer/`）。
编译验证：`make build`（交叉编译）；仿真回归：`make simtest`。

## 触发条件

- 改 `ofdm_phy.c` / `ofdm_phy.h`（OFDM 参数、flexframe 组帧、同步/解码）
- 改 `pluto_ofdm_file_self_transfer.c`（libiio 配置、TX/RX 流、`-T`/`-R` 模式）
- 真机出现：OFDM 帧一个都检测不到、header 解不出、chunk 大量缺失、
  `recovered.png` CRC 不过、RX 溢出/欠载、两机频偏太大锁不上
- 想确认"锅在基带还是射频"：先用 `make simtest` 跑仿真，再用本技能的真机检查点
- 移植到其他路径后 makefile 找不到 toolchain / 库

## 工程文件地图

| 路径（相对工程根目录） | 作用 |
|---|---|
| `pluto_ofdm_file_self_transfer.c` | 真机主程序：getopt、读文件、AD9361 配置、循环 TX 推流、RX 采集、`-T`/`-R` 模式 |
| `ofdm_phy.c` / `ofdm_phy.h` | OFDM 基带算法：flexframe 组帧、头/数据帧、两遍同步解码、重组、文件 CRC |
| `makefile` | `build` / `run` / `sim` / `simtest` |
| `doc/design.md` | 架构与帧结构 |
| `doc/01-simulation/` | 单文件仿真版与测试图 |
| `tools/sim_test.sh` | 仿真回归脚本 |
| `tools/README.md` | 工具说明 |

## 处理链

```
TX: 文件 → CRC32 → 分片 → 文件头帧(32B 元数据) + 数据帧(128B chunk)
    → 每帧经 ofdmflexframegen：S0/S1 + BPSK header + QPSK payload
    → 帧间隙 → int16 IQ → 循环 TX buffer

RX: int16 IQ → float → 直流抑制 → 峰值归一化
    → ofdmflexframesync 第一遍找文件头 → 分配重组缓冲
    → reset 后第二遍收数据帧（按 chunk_index 去重）
    → 文件级 CRC → 写文件
```

## 硬规则（按踩坑代价排序）

1. **TX 端和 RX 端的 `header_len` 必须一致，且都是 8**。
   `ofdmflexframegen_set_header_len(fg, 8)` 和
   `ofdmflexframesync_set_header_len(fs, 8)` 缺一不可。否则头帧/数据帧都解不出。
2. **OFDM flexframe 一帧写完后必须补 tail + 间隙**。
   本工程每帧样本数 = `(getframelen() + 1 + OFDM_GAP_SYMBOLS) * (M + CP)`：
   `+1` 是 tail 零符号，`OFDM_GAP_SYMBOLS` 是帧间隙。少写 tail 会导致最后一帧
   flush 不干净，少写间隙会让相邻帧同步状态粘连。
3. **`ofdmflexframegen_write()` 一次写多少样本要按帧长计算**。
   不要在 `assemble` 前调用 `getframelen()`，必须 `assemble` 之后再取；
   否则净荷长度变化后帧长是旧的。
4. **同一 chunk 的重复帧必须错开**。
   发送顺序是「第 1 轮全部 chunk → 第 2 轮全部 chunk」，不要像 QPSK 参考那样
   把同一 chunk 的两个重复包相邻发。错开后短突发/短衰落最多打掉一份拷贝。
   仿真里 `SIM_BURST=5000` 依赖这个设计。
5. **接收端用两遍同步，不要一遍边收边重组**。
   第一遍只找文件头（拿到 `file_size/crc/num_chunks`），分配好缓冲后再 reset
   第二遍收数据帧。这样 RX-only 两机模式不需要预先知道文件大小，且数据帧早于
   头帧到达也能被第二遍重新同步到。
6. **文件级 CRC 是最终判据**：chunk 收齐 + `crc_generate_key` 一致才写文件。
   不要只看 "decoded N/N chunks" 就认为成功。
7. **RX/TX 采样率在 Pluto 上联动**（BLE 工程硬规则 5）：本工程 TX/RX 共用一个
   `param_samp_mhz`，不要两头写不同值。两机模式两端采样率也要一致。
8. **TX 用硬件循环缓冲推流一次**（BLE TX 硬规则 1 的思路）：整段 `tx_n` 样点填进
   `iio_device_create_buffer(tx, tx_n, true)`，`push` 一次后只做 RX refill。
   TX-only 模式推流后循环等待 Ctrl-C，不要反复软件 push。
9. **交叉编译用 `-Wl,--allow-shlib-undefined`**：本机 toolchain 只有 libiio，
   其依赖在 Pluto rootfs 上才齐全；链接阶段忽略未定义子符号是既有做法。
10. **两机频偏先算归一化值**：`SIM_CFO` 的单位是 rad/sample。真机 10 MSPS、
    64 子载波时子载波间隔 156.25 kHz，S0 频偏估计通常覆盖 ±0.5 子载波间隔
    （约 ±78 kHz）。若两机 LO 偏差超过这个范围，优先提高采样率（`-s 20`）或
    先校准 LO，不要硬调 OFDM 内部参数。

## 排查方法论

1. **先跑仿真排除基带问题**：`make simtest`。仿真通过而真机失败 → 锅在射频/I/O。
2. **仿真旋钮对号入座**：`SIM_CFO`（频偏）、`SIM_LEAD`（RX 早启动）、
   `SIM_BURST`（瞬时干扰）、`SIM_NOISE`/`SIM_GAIN`/`SIM_PHASE`（射频损伤）。
   改哪个能复现真机现象，就往哪个方向查真机配置。
3. **真机先查增益**：自环信号很强，RX 手动增益 40 dB 容易削顶。OFDM 峰均比高，
   削顶会让 S0 相关峰和导频均衡一起变差。先降到 0~20 dB 看 header 是否出来。
4. **确认 RX 真的采到信号**：临时把 `rx_collected` 的前若干样本 dump 出来，
   看功率包络是否有一段明显高于底噪；没有就是环路没通（端口/衰减/LO）。
5. **编译不过先看路径**：makefile 用 `$(CURDIR)/../toolchain/...`，工程必须放在
   `iio/` 同级目录下，迁移时整体搬目录，不要只搬某个文件。

## 实测基线（改动后可对比）

| 场景 | 结果 |
|---|---|
| 仿真默认参数（CFO=0.02、noise=0.05、随机相偏） | 908/908 chunks，`recovered.png == test.png` |
| 仿真随机相偏 ×5 | 5/5 |
| 仿真 `SIM_LEAD=100000` | 通过 |
| 仿真 `SIM_BURST=5000` | 通过 |
| 仿真 `SIM_NOISE=0.2 SIM_CFO=0.02` | 通过 |
| 真机交叉编译 | `make build` 通过（本阶段验收） |

## 已知局限

- **未上真机验证**：当前只验证交叉编译 + 宿主机仿真通过；真机射频参数
  （增益/衰减/端口/采样率）是按 QPSK/BLE 工程经验给的默认值。
- **RX-only 采集长度需人工匹配文件大小**：默认 `-S 10000000`（10 MSPS 下 1 秒）
  足够覆盖 `test.png` 的完整发送周期；更大文件需要按 `tx_n/fs` 估算后调大 `-S`。
- **离线批量处理**：RX 先整段采集再两遍解码，不做实时流式输出；文件越大内存越大。
- **无 ARQ**：丢包靠重复 + FEC + 文件 CRC 兜底，不重传。
- **两机大频偏**：OFDM S0 频偏估计有范围，超过 ±0.5 子载波间隔时需提高采样率
  或校准 LO；未做额外的整数频偏扫描。
