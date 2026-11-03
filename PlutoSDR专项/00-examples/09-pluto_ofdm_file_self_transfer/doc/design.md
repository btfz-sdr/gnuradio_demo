# 真机架构与 OFDM 链路设计

工程根目录：`11-pluto_ofdm_file_self_transfer/`。所有路径均相对该根目录。

## 1. 总架构

```
test.png
   │
   ▼
[主程序] 读文件 → file_size / file_crc / num_chunks
   │
   ▼
ofdm_phy_build_tx()                       ← ofdm_phy.c
   ofdmflexframegen：S0/S1 前导 + 文件头帧 + 数据帧 + 帧间隙
   │
   ▼
TX cyclic buffer（整段 IQ，int16，MSB 对齐）
   │ iio_buffer_push() 一次
   ▼
AD9361 TX ──环路/线缆/空口──▶ AD9361 RX
   │
   ▼
RX non-cyclic buffer 连续 refill 采集 tx_n + margin 个样本
   │ int16 → float complex
   ▼
ofdm_phy_rx_process()                    ← ofdm_phy.c
   直流抑制 → 峰值归一化 → 第一遍同步找文件头
   → 分配重组缓冲 → 第二遍同步收数据帧 → 文件级 CRC → 写文件
   │
   ▼
recovered.png
```

## 2. OFDM 波形与帧结构

波形参数（`ofdm_phy.h`）：

| 参数 | 值 | 说明 |
|---|---|---|
| 子载波数 `OFDM_M` | 64 | liquid 默认示例值，偶数 |
| 循环前缀 `OFDM_CP` | 16 | 1/4 符号，抗多径/定时误差 |
| taper | 4 | OFDM 符号重叠，liquid 默认 |
| 子载波分配 | `ofdmframe_init_default_sctype(64)` | 44 data + 6 pilot + 14 null |
| 帧间隙 | 2 个零符号 | 帮助帧同步复位，吸收短突发尾部 |

每个 OFDM 帧（flexframe）内部：

```
S0a + S0b   突发检测 / 定时 / 粗频偏估计
S1          信道估计（pilot）
header      BPSK + Golay(24,12) + CRC32（liquid 默认，非常稳）
payload     QPSK + Hamming74 + CRC32
tail        1 个零符号 flush
```

`header` 的用户 8 字节：

```
[0]      frame type：1=文件头帧，2=数据帧
[1]      repeat index
[2..5]   data 帧：chunk_index；header 帧保留
[6..7]   reserved
```

文件头帧净荷 32 字节：

```
[0..7]   file_size
[8..11]  file_crc
[12..15] num_chunks
[16..31] reserved（零）
```

数据帧净荷固定 `PAYLOAD_CHUNK_SIZE = 128` 字节；最后一包不足 128 字节时补零，
真实文件长度由文件头中的 `file_size` 控制，接收端按 `file_size` 截断。

## 3. 发送顺序与重复策略

```
4 个文件头帧
→ 第 1 轮：chunk 0..num_chunks-1 各一帧
→ 第 2 轮：chunk 0..num_chunks-1 各一帧
```

关键点：**同一 chunk 的两次重复不放在相邻位置**，而是先完整发一轮、再完整发一轮。
这样短突发/短衰落（远小于整文件时长）最多打掉同一 chunk 的一份拷贝，另一份大概率存活。
QPSK 参考工程把重复包放在相邻位置；本工程在此处做了增强，仿真验证
`SIM_BURST=5000` 仍能 908/908 通过。

## 4. 接收端两遍同步

1. **第一遍**：整段样本过 `ofdmflexframesync`，只找文件头帧，拿到
   `file_size` / `file_crc` / `num_chunks`。
2. 防御性检查头帧数据，按解码出的 `file_size` / `num_chunks` 分配重组缓冲。
3. **第二遍**：`ofdmflexframesync_reset()` 后重放同一段样本，这次收集数据帧，
   按 `chunk_index` 去重并写入对应文件偏移。
4. 收齐所有 chunk 后做文件级 `crc_generate_key(LIQUID_CRC_32)` 校验，通过才写文件。

两遍重放会多花一点 CPU，但换来两个好处：
- 数据帧不必在文件头之前到达也能被正确处理（第二遍对整段样本重新同步）；
- 分配重组缓冲时已经知道真实文件大小，支持 RX-only 两机模式。

## 5. 为什么 OFDM 比单载波更适合两机

- liquid 的 `ofdmflexframesync` 自带 S0/S1 突发检测、定时、CFO 估计、信道估计与均衡，
  不像 QPSK 参考工程那样依赖单载波 symsync 的固定定时环路。
- 每帧独立同步，某一帧被瞬时干扰打掉只丢那一帧，不会整段连锁失锁。
- 频偏容忍：在 10 MSPS 采样率下，64 子载波间隔 = 156.25 kHz。liquid 的 S0 频偏估计
  通常可覆盖 ±0.5 子载波间隔 ≈ ±78 kHz（约 ±31 ppm @ 2.5 GHz）。两机 LO 若有更大频偏，
  应提高采样率或先校准 LO。仿真基线见 `doc/01-simulation/README.md`。

## 6. AD9361 配置

| 项 | 默认值 | 说明 |
|---|---|---|
| LO | 2500 MHz | `-l` 覆盖 |
| 采样率 | 10 MSPS | `-s` 覆盖；RX/TX 同源联动 |
| 模拟带宽 | 8 MHz | `-b` 覆盖；保持约 0.8×fs |
| RX 增益 | 40 dB 手动 | `-g` 覆盖；自环信号强时可降到 0~20 dB 防削顶 |
| TX 衰减 | 10 dB | `-a` 覆盖 |
| RX 端口 | A_BALANCED | |
| TX 端口 | A | |

> 注意：单板自环时 RX/TX 采样率联动（BLE 工程硬规则 5）；本工程 TX/RX 使用同一个
> `param_samp_mhz`，天然一致。两机模式也强烈建议两端用相同采样率。

## 7. 仿真与真机的对应关系

| 仿真（`doc/01-simulation/ofdm_pipeline.c`） | 真机（本工程） |
|---|---|
| `build_tx()` 中 `ofdmflexframegen_write()` | `ofdm_phy_build_tx()` |
| 信道：`SIM_CFO` / `SIM_PHASE` / `SIM_NOISE` / `SIM_GAIN` / `SIM_LEAD` / `SIM_BURST` | 真实空口/环路：增益/相偏/噪声/DC 由 AD9361 决定 |
| `run_sync()` + 两遍 `rx_callback` | `ofdm_phy_rx_process()` |
| 文件 CRC 后写 `recovered.png` | 真机主程序写 `recovered.png` |

仿真版保留单文件，便于快速回归；真机版按 BLE/QPSK 工程习惯拆成
`main + ofdm_phy` 两个编译单元。
