# 10-pluto_qpsk_file_self_transfer

基于 ADALM-PlutoSDR（AD9361）单板自收发的 QPSK 文件传输程序：
把 `test.png` 通过 QPSK 调制后从 TX 发出，RX 接收、解调、重组，输出 `recovered.png`。

> 工程根目录：本文件所在目录。所有命令都在根目录下执行，迁移整个目录即可。

## 快速使用

### 1. 交叉编译真机程序

```bash
make build
```

产物：`pluto_qpsk_file_self_transfer`（ARM 可执行文件）。

### 2. 上机自收发

```bash
make run
```

`run` 会做三件事：编译 → scp 程序与 `doc/01-simulation/test.png` 到 `root@pluto.local` → ssh 上板运行。
默认参数：LO 2500 MHz、采样率 2.5 MSPS、带宽 2 MHz、RX 增益 40 dB、TX 衰减 10 dB。

覆盖默认参数：

```bash
make run LO_MHZ=2500 SAMP_MHZ=2.5 BW_MHZ=2.0 RX_GAIN=20 TX_ATTN=20
```

成功标志：屏幕出现 `Decoded 908 / 908 chunks` 与 `File CRC OK`，
板子上生成 `recovered.png`，内容与 `test.png` 一致。

### 3. 宿主机仿真（不上板验证算法）

```bash
make sim        # 编译 doc/01-simulation/industrial_pipeline4
make simtest    # 编译 + 运行 + 自动比对 recovered.png 与 test.png
```

仿真信道可用环境变量调节，见 `doc/01-simulation/README.md`。

## 目录

| 路径 | 作用 |
|---|---|
| `pluto_qpsk_file_self_transfer.c` | 真机主程序（libiio / AD9361 收发） |
| `qpsk_phy.c` / `qpsk_phy.h` | 基带算法（前导、DQPSK、交织、解码） |
| `makefile` | `build` / `run` / `sim` / `simtest` |
| `doc/` | 设计文档与仿真资料 |
| `skills/` | 本工程技能文档 |
| `tools/` | 测试脚本 |

详细设计见 `doc/design.md`，完整说明见 `doc/README.md`。

## 真机实测记录

- `make run`（默认参数，无天线/无线缆）实测：`908/908` chunks，文件 CRC 通过。
- 无外部天线/线缆也能成功，是因为单板上 TX→RX 存在泄漏/近场耦合，构成一个隐藏闭环；
  这验证了基带算法与 libiio 收发链路，但不是真实空口信道。
