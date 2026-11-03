# doc/01-simulation —— 主机端 OFDM 仿真

工程根目录：`11-pluto_ofdm_file_self_transfer/`。本目录相对根目录为
`doc/01-simulation/`。

这里的 `ofdm_pipeline.c` 是单文件仿真版，用于在宿主机验证 OFDM 基带算法
（flexframe 组帧、S0/S1 同步、文件头/数据帧、重复策略、文件 CRC）。
真机版把同一套算法拆到了根目录的 `ofdm_phy.c/h`。

## 文件

| 文件 | 作用 |
|---|---|
| `ofdm_pipeline.c` | 单文件仿真主程序（算法与真机 `ofdm_phy.c` 同源） |
| `test.png` | 测试输入文件（116108 字节） |

## 编译运行

在工程根目录：

```bash
make sim        # 编译 doc/01-simulation/ofdm_pipeline
make simtest    # 编译 + 运行 + 比对 recovered.png 与 test.png
```

或直接进入本目录：

```bash
cd doc/01-simulation
gcc -Wall -O2 -o ofdm_pipeline ofdm_pipeline.c -lm -lc -lliquid
./ofdm_pipeline
cmp test.png recovered.png && echo PASS
```

## 仿真信道旋钮（环境变量）

| 变量 | 默认 | 含义 |
|---|---|---|
| `SIM_CFO` | 0.02 | 频偏 rad/sample |
| `SIM_PHASE` | 随机 | 固定相偏 rad（负值表示随机） |
| `SIM_NOISE` | 0.05 | 高斯噪声 sigma |
| `SIM_GAIN` | 1.0 | 信道增益 |
| `SIM_LEAD` | 0 | 起始零样本数（模拟 RX 早启动） |
| `SIM_BURST` | 0 | 中途清零样本数（模拟掉电/强干扰） |
| `SIM_BURST_POS` | 0.5 | 突发位置（占整段比例） |

示例：

```bash
SIM_NOISE=0.2 SIM_LEAD=100000 ./ofdm_pipeline
SIM_BURST=5000 SIM_BURST_POS=0.5 ./ofdm_pipeline
```

## 已知仿真基线

- 默认参数：908/908 chunk，`recovered.png == test.png`。
- 随机相偏 20/20 通过。
- `SIM_CFO=0.02`、`SIM_NOISE=0.2` 通过。
- 起始零样本 100000 通过。
- 中途清零 5000 样点通过（重复分片错开，短突发不丢文件）。
- `SIM_CFO` 约到 0.04 rad/sample 仍可通过；更高 CFO 需提高采样率或校准 LO。

> 说明：仿真默认 `SIM_CFO=0.02 rad/sample`。真机 10 MSPS 下对应约 31.8 kHz 频偏，
> 远小于 64 子载波间隔 156.25 kHz，处于 OFDM 同步器舒适区。
