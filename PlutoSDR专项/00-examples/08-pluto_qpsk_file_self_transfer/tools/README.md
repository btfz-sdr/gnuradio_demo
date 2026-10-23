# tools/ —— 工程辅助工具

工程根目录：`10-pluto_qpsk_file_self_transfer/`。本目录相对根目录为 `tools/`。

| 脚本 | 用途 |
|---|---|
| `sim_test.sh` | 宿主机仿真回归：编译 `doc/01-simulation/industrial_pipeline4.c`、运行、`cmp` 比对 `recovered.png` 与 `test.png`；加 `stress` 参数再跑随机相偏/早启动/中途突发三组压力测试 |

## 用法

在工程根目录：

```bash
bash tools/sim_test.sh          # 默认仿真 + 比对
bash tools/sim_test.sh stress   # 额外跑压力测试
```

也可从工程根目录直接：

```bash
make simtest
```

仿真信道环境变量（`SIM_CFO` / `SIM_PHASE` / `SIM_NOISE` / `SIM_GAIN` /
`SIM_LEAD` / `SIM_BURST` / `SIM_BURST_POS`）会透传，例如：

```bash
SIM_NOISE=0.1 SIM_LEAD=100000 bash tools/sim_test.sh
```

## 判据

- 默认仿真：`908/908 chunks` 且 `recovered.png == test.png`。
- `stress`：随机相偏 5 次、`SIM_LEAD=100000`、`SIM_BURST=2000` 全部通过。
- 任一步 `cmp` 不一致脚本以非 0 退出，直接失败。
