# 11-pluto_ofdm_file_self_transfer 工程总览

工程根目录：`11-pluto_ofdm_file_self_transfer/`。本文所有路径均相对该根目录。

目标：在 ADALM-PlutoSDR（AD9361）上完成 OFDM 文件自收发（TX → 环路 → RX），
并重组出与原文件逐字节一致的文件；同时支持两机收发（`-T` / `-R`）。
当前阶段只验证 **交叉编译通过** 与 **宿主机仿真通过**，暂不上真机。

## 目录结构

| 路径 | 作用 |
|---|---|
| `pluto_ofdm_file_self_transfer.c` | 真机主程序：getopt 参数、读文件、AD9361 配置、循环 TX 推流、RX 采集、调用基带处理 |
| `ofdm_phy.h` / `ofdm_phy.c` | 与硬件无关的 OFDM 基带算法：flexframe 组帧、头/数据帧、两遍同步解码、重组、文件 CRC |
| `makefile` | `build` / `run` / `sim` / `simtest` |
| `doc/README.md` | 本文件 |
| `doc/design.md` | 真机架构与 OFDM 链路设计 |
| `doc/01-simulation/` | 主机端仿真版（`ofdm_pipeline.c`）及测试图片 |
| `skills/pluto-ofdm-file-self-transfer/SKILL.md` | 本工程技能文档（触发条件、文件地图、链路、踩坑、基线） |
| `tools/sim_test.sh` | 仿真回归测试脚本 |
| `tools/README.md` | 工具说明 |

## 快速开始

### 1. 真机交叉编译（本阶段验收目标）

```bash
make build
```

产物：`pluto_ofdm_file_self_transfer`（ARM 可执行文件，已 strip）。

### 2. 宿主机仿真

仿真版在 `doc/01-simulation/`：

```bash
make sim       # 编译 doc/01-simulation/ofdm_pipeline
make simtest   # 编译 + 运行 + 比对 recovered.png 与 test.png
```

或手动：

```bash
cd doc/01-simulation
gcc -Wall -O2 -o ofdm_pipeline ofdm_pipeline.c -lm -lc -lliquid
./ofdm_pipeline
cmp test.png recovered.png && echo PASS
```

### 3. 真机运行（暂未执行）

单板自收发：

```bash
make run LO_MHZ=2500 SAMP_MHZ=10 BW_MHZ=8 RX_GAIN=20 TX_ATTN=10
```

两机收发：

```bash
# TX 端
./pluto_ofdm_file_self_transfer -T -i test.png -l 2500 -s 10 -b 8 -a 10
# RX 端
./pluto_ofdm_file_self_transfer -R -o recovered.png -l 2500 -s 10 -b 8 -g 20 -S 10000000
```

`run` 会 scp 二进制与 `doc/01-simulation/test.png` 到 `root@pluto.local` 后 ssh 运行。
非交互 scp 需要环境里已有 `PLUTO_PW`/askpass 配置（参考 BLE 工程）。

## 与 QPSK 参考工程的关系

真机 I/O 层（libiio 设备查找、通道配置、属性写入、getopt 参数风格）沿用
`10-pluto_qpsk_file_self_transfer` 的框架；基带算法来自 liquid-dsp 的
`ofdmflexframegen` / `ofdmflexframesync`，并沿用“文件头帧 + 数据分片 +
重复发送 + 文件级 CRC”的可靠性设计。与 QPSK 的映射见 `doc/design.md`。
