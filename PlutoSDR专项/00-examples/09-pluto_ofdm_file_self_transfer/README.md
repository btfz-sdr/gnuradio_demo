# 11-pluto_ofdm_file_self_transfer

基于 ADALM-PlutoSDR（AD9361）的 OFDM 文件传输程序：
把 `test.png` 通过 OFDM（liquid-dsp `ofdmflexframegen`/`ofdmflexframesync`）调制后从 TX 发出，
RX 接收、同步、解调、重组，输出 `recovered.png`。

> 工程根目录：本文件所在目录。所有命令都在根目录下执行，迁移整个目录即可。

## 快速使用

### 1. 交叉编译真机程序

```bash
make build
```

产物：`pluto_ofdm_file_self_transfer`（ARM 可执行文件，已 strip）。

### 2. 单板自收发

```bash
make run
```

`run` 会做三件事：编译 → scp 程序与 `doc/01-simulation/test.png` 到 `root@pluto.local` → ssh 上板运行。
默认参数：LO 2500 MHz、采样率 10 MSPS、带宽 8 MHz、RX 增益 40 dB、TX 衰减 10 dB。

覆盖默认参数：

```bash
make run LO_MHZ=2500 SAMP_MHZ=10 BW_MHZ=8 RX_GAIN=20 TX_ATTN=20
```

成功标志：屏幕出现 `decoded 908 / 908 chunks` 与 `File CRC OK`，
板子上生成 `recovered.png`，内容与 `test.png` 一致。

### 3. 两机收发（TX 端 / RX 端）

OFDM 程序原生支持收发分离，适合两台 Pluto 之间传输：

TX 端（循环发送，直到 Ctrl-C）：

```bash
./pluto_ofdm_file_self_transfer -T -i test.png -l 2500 -s 10 -b 8 -a 10
```

RX 端（采集后离线解码）：

```bash
./pluto_ofdm_file_self_transfer -R -o recovered.png -l 2500 -s 10 -b 8 -g 20 -S 10000000
```

> 建议两机 LO 先校准，或把采样率提高到 10 MSPS 以上：OFDM 子载波间隔随采样率增大，
> 抗两机 LO 频偏能力更强。详细分析见 `doc/design.md`。

### 4. 宿主机仿真（不上板验证算法）

```bash
make sim        # 编译 doc/01-simulation/ofdm_pipeline
make simtest    # 编译 + 运行 + 自动比对 recovered.png 与 test.png
```

仿真信道可用环境变量调节，见 `doc/01-simulation/README.md`。

## 目录

| 路径 | 作用 |
|---|---|
| `pluto_ofdm_file_self_transfer.c` | 真机主程序（libiio / AD9361 收发，支持自收发 / TX-only / RX-only） |
| `ofdm_phy.c` / `ofdm_phy.h` | OFDM 基带算法（flexframe 组帧、头/数据帧、两遍同步解码、文件 CRC） |
| `makefile` | `build` / `run` / `sim` / `simtest` |
| `doc/` | 设计文档与仿真资料 |
| `skills/` | 本工程技能文档 |
| `tools/` | 测试脚本 |

详细设计见 `doc/design.md`，完整说明见 `doc/README.md`。

## 与 QPSK 参考工程的关系

真机 I/O 层（libiio 设备查找、通道配置、属性写入、getopt 参数风格、循环 TX 推流）沿用
`10-pluto_qpsk_file_self_transfer` 的框架；基带算法从单载波 DQPSK 换成
liquid-dsp 的 OFDM flexframe，但保留了“文件头帧 + 数据分片 + 重复发送 + 文件级 CRC”的可靠性设计。
