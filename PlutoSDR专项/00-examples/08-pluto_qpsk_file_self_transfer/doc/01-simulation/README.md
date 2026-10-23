# doc/01-simulation —— 主机端仿真

工程根目录：`10-pluto_qpsk_file_self_transfer/`。本目录相对根目录为
`doc/01-simulation/`。

这里的 `industrial_pipeline4.c` 是单文件仿真版，用于在宿主机验证基带算法
（前导、DQPSK、交织、逐包重同步、文件 CRC）。真机版把同一套算法拆到了
根目录的 `qpsk_phy.c/h`。

## 文件

| 文件 | 作用 |
|---|---|
| `industrial_pipeline4.c` | 单文件仿真主程序（算法与真机 `qpsk_phy.c` 同源） |
| `test.png` | 测试输入文件（116108 字节） |
| `industrial_pipeline.c` / `2` / `3` / `robust_pipeline4.c` | 历史版本，仅作对照参考 |

## 编译运行

在工程根目录：

```bash
make sim        # 编译 doc/01-simulation/industrial_pipeline4
make simtest    # 编译 + 运行 + 比对 recovered.png 与 test.png
```

或直接进入本目录：

```bash
cd doc/01-simulation
gcc -Wall -O2 -o industrial_pipeline4 industrial_pipeline4.c -lm -lc -lliquid
./industrial_pipeline4
cmp test.png recovered.png && echo PASS
```

## 仿真信道旋钮（环境变量）

| 变量 | 默认 | 含义 |
|---|---|---|
| `SIM_CFO` | 0.0002 | 频偏 rad/sample |
| `SIM_PHASE` | 随机 | 固定相偏 rad |
| `SIM_NOISE` | 0.05 | 高斯噪声 sigma |
| `SIM_GAIN` | 0.50 | 信道增益 |
| `SIM_LEAD` | 0 | 起始零样本数（模拟 RX 早启动） |
| `SIM_BURST` | 0 | 中途清零样本数（模拟掉电/强干扰） |
| `SIM_BURST_POS` | 0.5 | 突发位置（占整段比例） |

示例：

```bash
SIM_NOISE=0.1 SIM_LEAD=100000 ./industrial_pipeline4
SIM_BURST=2000 SIM_BURST_POS=0.5 ./industrial_pipeline4
```

## 已知仿真基线

- 默认参数：908/908 chunk，`recovered.png == test.png`。
- 随机相偏 20/20 通过。
- CFO ≤ 0.02 rad/sample 通过。
- 起始零样本 100000 通过。
- 中途清零 2000 样点通过；5000 样点丢 1 chunk。
- 低增益（无噪声）0.001 通过。

真机参数对不上时，先用本目录把算法复现，再回真机查射频/增益问题。
