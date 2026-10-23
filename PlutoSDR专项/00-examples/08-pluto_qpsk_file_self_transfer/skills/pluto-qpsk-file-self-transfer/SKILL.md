---
name: pluto-qpsk-file-self-transfer
description: PlutoSDR(AD9361) 单板 QPSK 文件自收发 —— 前导检测、DQPSK、逐包重同步、交织/重复/文件 CRC、真机 libiio 收发框架、仿真回归流程。改 qpsk_phy.c/h、pluto_qpsk_file_self_transfer.c、makefile，或排查"锁不上头/漏包/恢复文件 CRC 不过/真机一个包都解不出"时用本技能。
---

# PlutoSDR QPSK 文件自收发

工程根目录：本文件所在目录往上三级（即 `pluto_qpsk_file_self_transfer/`）。
编译验证：`make build`（交叉编译）；仿真回归：`make simtest`。

## 触发条件

- 改 `qpsk_phy.c` / `qpsk_phy.h`（前导、DQPSK、交织、symsync、检测/解码）
- 改 `pluto_qpsk_file_self_transfer.c`（libiio 配置、TX/RX 流、参数解析）
- 真机出现：header 解不出、chunk 大量缺失、`recovered.png` CRC 不过、RX 溢出/欠载
- 想确认"锅在基带还是射频"：先用 `make simtest` 跑仿真，再用本技能的真机检查点
- 移植到其他路径后 makefile 找不到 toolchain / 库

## 工程文件地图

| 路径（相对工程根目录） | 作用 |
|---|---|
| `pluto_qpsk_file_self_transfer.c` | 真机主程序：getopt、读文件、AD9361 配置、循环 TX 推流、RX 采集 |
| `qpsk_phy.c` / `qpsk_phy.h` | 基带算法：前导、DQPSK、组包/解包、symsync、检测、重组、CRC |
| `makefile` | `build` / `run` / `sim` / `simtest` |
| `doc/design.md` | 架构与帧结构 |
| `doc/01-simulation/` | 单文件仿真版与测试图 |
| `tools/sim_test.sh` | 仿真回归脚本 |
| `tools/README.md` | 工具说明 |

## 处理链

```
TX: 文件 → CRC32 → 分片 → 组包(CRC32+Hamming74) → 字节交织
    → [前导64 + DQPSK数据 + 保护16] → RRC插值(k=2) → int16 IQ → 循环TX buffer

RX: int16 IQ → float → 直流抑制 → 软件AGC(fast attack/slow decay)
    → symsync(RRC,32滤波器) → 前导相关峰检测 → 逐包DQPSK解码
    → 解交织 → packetizer解码 → 按chunk index重组 → 文件CRC → 写文件
```

## 硬规则（按踩坑代价排序）

1. **DQPSK 必须用显式相位增量，不能用 modem 符号索引相加**。
   liquid 的 QPSK 星座索引顺序是 Gray 码（0→45°, 1→135°, 2→-45°, 3→-135°），
   索引 +1 不等于相位 +90°。用索引做差分会产生系统性 50% 符号错。正确做法见
   `qpsk_phy.c` 的 `build_packet_symbols()`：`delta[] = {1, I, -1, -I}`，
   相位参考为前导末符号；接收端 `cargf(cur*conj(prev))` 后量化到 90° 整数倍。
2. **前导必须非周期**。周期前导（两半相同）在做 64 符号相关时每 32 符号一个峰，
   候选起点会差 32 的整数倍 → 整包解不出来。用 LFSR 生成 64 个互不相同 QPSK 符号。
3. **每包独立前导，失败包只丢自己**。解码失败后不要固定步进，让检测器在下一次
   相关峰重新同步。这是中途强干扰（仿真 2000 样点清零）仍能 908/908 的原因。
4. **起始训练段与包前导必须用不同序列**。训练段（1024 符号）只给 AGC/symsync
   收敛；若与包前导相同，检测器会在训练段产生一堆假候选，还可能因 min_gap 跳过
   真正的 header 包。
5. **RX/TX 采样率在 Pluto 上联动**（BLE 工程硬规则 5）：本工程 TX/RX 共用一个
   `param_samp_mhz`，不要两头写不同值。
6. **TX 用硬件循环缓冲推流一次**（BLE TX 硬规则 1 的思路）：软件循环 push 在板子上
   周期不稳、可能欠载。整段 `tx_n` 样点填进 `iio_device_create_buffer(tx, tx_n, true)`，
   `push` 一次后只做 RX refill。代价是缓冲区大（`tx_n*4` 字节），
   但 Pluto 内存够用；进程退出即停发。
7. **TX 循环缓冲意味着信号会重复**：RX 多收的 margin 会收到下一轮开头，
   那是 header + 前几个 chunk 的重复。重组靠 chunk index 去重，不会写错，
   但别把 margin 里的重复 header 当成异常。
8. **文件级 CRC 是最终判据**：chunk 收齐 + `crc_generate_key` 一致才写文件。
   不要只看 "Decoded N/N chunks" 就认为成功。
9. **交叉编译用 `-Wl,--allow-shlib-undefined`**：本机 toolchain 只有 libiio，
   其依赖（libusb/libserialport/avahi/xml2）在 Pluto rootfs 上才齐全；链接阶段
   忽略未定义子符号是 BLE 工程的既有做法。

## 排查方法论

1. **先跑仿真排除基带问题**：`make simtest`。仿真通过而真机失败 → 锅在射频/I/O。
2. **仿真旋钮对号入座**：`SIM_LEAD`（RX 早启动）、`SIM_BURST`（瞬时干扰）、
   `SIM_CFO`/`SIM_PHASE`/`SIM_NOISE`/`SIM_GAIN`（射频损伤）。改哪个能复现真机
   现象，就往哪个方向查真机配置。
3. **真机先查增益**：自环信号很强，RX 手动增益 40 dB 容易削顶。削顶不是加性噪声，
   前导相关峰也会塌。先降到 0~20 dB 看 header 是否出来。
4. **确认 RX 真的采到信号**：临时把 `rx_collected` 的前若干样本 dump 出来，
   看功率包络是否有一段明显高于底噪；没有就是环路没通（端口/衰减/LO）。
5. **编译不过先看路径**：makefile 用 `$(CURDIR)/../toolchain/...`，工程必须放在
   `iio/` 同级目录下，迁移时整体搬目录，不要只搬某个文件。

## 实测基线（改动后可对比）

| 场景 | 结果 |
|---|---|
| 仿真默认参数（随机相偏、CFO=0.0002、noise=0.05、gain=0.5） | 908/908 chunks，`recovered.png == test.png` |
| 仿真随机相偏 ×20 | 20/20 |
| 仿真 CFO | ≤0.02 rad/sample 通过 |
| 仿真起始零样本 | 100000 样点通过 |
| 仿真中途清零 | 2000 样点通过；5000 样点 907/908 |
| 真机交叉编译 | `make build` 通过（本阶段验收） |

## 已知局限

- **未上真机验证**：当前只验证交叉编译通过；真机射频参数（增益/衰减/端口）是
  按 BLE 工程经验给的默认值，上机第一件事是降 RX 增益防削顶。
- **单板自环**：TX/RX 共用 LO，频偏天然小；双机场景若频偏 >0.02 rad/sample
  （仿真界限）需在前导后加决策导向 PLL 做粗频偏补偿。
- **离线批量处理**：RX 先整段采集再解码，不做实时流式输出；文件越大内存越大
  （tx float + rx float 各 `tx_n*8` 字节）。
- **无 ARQ**：丢包靠重复 + 交织 + Hamming74 兜底，不重传。
