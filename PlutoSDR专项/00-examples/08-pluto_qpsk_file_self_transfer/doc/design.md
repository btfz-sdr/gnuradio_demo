# 真机架构与链路设计

工程根目录：`10-pluto_qpsk_file_self_transfer/`。所有路径均相对该根目录。

## 1. 总架构

```
test.png
   │
   ▼
[主程序] 读文件 → file_size / file_crc / num_chunks
   │
   ▼
qpsk_phy_build_tx()                      ← qpsk_phy.c
   组包(CRC32+Hamming74) → 字节交织 → 前导 + DQPSK → RRC 插值(k=2)
   │
   ▼
TX cyclic buffer（整段 IQ，int16，MSB 对齐）
   │ iio_buffer_push() 一次
   ▼
AD9361 TX ──环路/线缆──▶ AD9361 RX
   │
   ▼
RX non-cyclic buffer 连续 refill 采集 tx_n + margin 个样本
   │ int16 → float complex
   ▼
qpsk_phy_rx_process()                   ← qpsk_phy.c
   直流抑制 → 软件 AGC → symsync 定时 → 前导相关检测
   → 逐包 DQPSK 解码 → 解交织 → packetizer 解码 → 重组 → 文件 CRC
   │
   ▼
recovered.png
```

## 2. 帧结构

每个包（header 与 data 同一物理格式）：

```
┌────────────┬────────────────────────────┬──────────┐
│ 前导 64 QPSK│ 编码数据 enc_len*4 个 DQPSK  │ 保护 16 零│
└────────────┴────────────────────────────┴──────────┘
```

- 前导：16-bit LFSR 生成的非周期 QPSK 序列，自相关尖锐，用于突发检测。
- 数据：DQPSK 显式相位增量 `{0°, +90°, 180°, -90°}`，相位参考为前导末符号。
- `enc_len = packetizer_get_enc_msg_len(144, CRC32, Hamming74)`，当前 = 259 字节
  → 1036 个 DQPSK 符号；每包共 `64 + 1036 + 16 = 1116` 符号 = 2232 样点。

raw 包（144 字节）：

```
[0..3]   SYNC_WORD 0xAA55AA55
[4]      type: 1=header, 2=data
[5]      reserved
[6..7]   seq/低 16 位 chunk index
[8..11]  header: total_chunks ; data: chunk_index
[12..15] header: file_crc    ; data: 0
[16..]   header: 8B file_size + 8B reserved ; data: 128B 文件净荷
```

发送顺序：

```
STARTUP_SYMBOLS(1024, 随机 QPSK，无前导相关峰)
→ HEADER_REPEATS(4) 个 header 包
→ 每个 chunk 连续 REPEAT_FACTOR(2) 个 data 包
→ TAIL_SYMBOLS(64) 零符号 flush RRC
```

## 3. 关键设计点（为什么这样设计）

1. **每包独立前导 → 逐包重同步**：任何一包被噪声/瞬时干扰打坏，只丢那一包，
   后续包靠自己的前导重新定位，不会像固定步进方案那样整段连锁失锁。
2. **DQPSK 而非绝对 QPSK**：消除固定相偏与 90° 相位模糊；对自环小 CFO 免疫。
3. **前导相关检测而非 `0..199` 盲扫**：能处理 RX/TX 启动时刻差、AGC/symsync
   收敛延迟、滤波器群延迟。仿真验证可容忍 100000 样点的起始零样本。
4. **起始训练段与包前导使用不同序列**：训练段只负责 AGC/symsync 收敛，
   不产生前导相关峰，避免污染候选检测。
5. **字节交织 + Hamming74 + CRC32 + 包重复 + 文件 CRC**：
   交织把突发错误打散到多个 Hamming 码字；重复包降低单包丢失概率；
   文件级 CRC 是最终正确性判据。
6. **TX 用硬件循环缓冲整段推流一次**：复用 BLE TX 的"硬件定时"思路，
   避免软件循环推流在板子上产生周期抖动/欠载。RX 非循环缓冲连续 refill。

## 4. AD9361 配置

| 项 | 默认值 | 说明 |
|---|---|---|
| LO | 2500 MHz | `-l` 覆盖 |
| 采样率 | 2.5 MSPS | `-s` 覆盖；RX/TX 同源联动 |
| 模拟带宽 | 2.0 MHz | `-b` 覆盖 |
| RX 增益 | 40 dB 手动 | `-g` 覆盖；自环信号强时可降到 0~20 dB 防削顶 |
| TX 衰减 | 10 dB | `-a` 覆盖 |
| RX 端口 | A_BALANCED | |
| TX 端口 | A | |

> 注意：单板自环时 RX/TX 采样率联动（BLE 工程硬规则 5）；若 RX 程序把采样率
> 改掉，TX 会一起变。本工程 TX/RX 使用同一个 `param_samp_mhz`，天然一致。

## 5. 仿真与真机的对应关系

| 仿真（`doc/01-simulation/industrial_pipeline4.c`） | 真机（本工程） |
|---|---|
| `apply_channel_impairments()` | 真实环路：增益/相偏/噪声/DC 由 AD9361 决定 |
| `sim_main()` | `pluto_qpsk_file_self_transfer.c` 的 IIO 收发主流程 |
| 内置 `PLUTOSDR_HW` 分支 | 拆到主程序 + `qpsk_phy.c/h` |
| `build_tx_signal()` | `qpsk_phy_build_tx()` |
| `rx_process_and_reassemble()` | `qpsk_phy_rx_process()` |

仿真版保留单文件，便于快速回归；真机版按 BLE 工程习惯拆成
`main + phy` 两个编译单元。
