### 1. 开场

上一集我们把 Pluto 变成了一台**板载蓝牙广播发射机**：不接任何蓝牙芯片，直接在 Zynq 的 ARM 上用纯 C 造 LE 1M 广播包，从 AD9361 的发射口喊出去。这一集我们把调制方式从 GFSK 升级到 **QPSK**，做一件更"文件级"的事——**让 Pluto 自己把一张图片通过 QPSK 从 TX 发出去，再由自己的 RX 收回来、解调、重组，得到一张和原图逐字节一致的图片**。

和上一集的短突发不同，文件传输是**一段连续的长流**：`test.png` 有 116108 字节，按 128 字节切片就是 908 个数据包，每个包都要经过"组包 → 编码 → 交织 → 前导 + DQPSK 调制 → RRC 成形"，在接收端再反过来走一遍"定时恢复 → 前导检测 → 差分解调 → 解交织 → 解码 → 重组"。链路全程跑在 Pluto 板载 ARM 上：

**读文件 → CRC32/分片 → 组包(CRC32+Hamming74) → 字节交织 → [前导 64 + DQPSK 数据 + 保护 16] → RRC 插值(k=2) → int16 IQ → 硬件循环缓冲区 → AD9361 TX →（板上泄漏/线缆环路）→ AD9361 RX → float IQ → 直流抑制 → 软件 AGC → symsync 定时 → 前导相关检测 → 逐包 DQPSK 解码 → 解交织 → packetizer 解码 → 按 chunk index 重组 → 文件 CRC32 → recovered.png**

这条链路的题眼在三个地方，也正好对应本集要展开讲的内容：**为什么数据要用 DQPSK 而不是绝对 QPSK**（消除固定相偏和 90° 相位模糊）；**为什么每包都要带自己的前导**（逐包重同步，中间坏几包不会连锁崩掉）；**为什么 TX 要整段交给硬件循环缓冲区推流一次**（和上一集一样，把"节奏"交给采样时钟，不交给软件）。

![][p1]

</br>

**补充：** 本集代码在 `08-pluto_qpsk_file_self_transfer`，算法先在宿主机单文件仿真版 `doc/01-simulation/industrial_pipeline4.c` 验证，再拆成真机版 `pluto_qpsk_file_self_transfer.c + qpsk_phy.c/h`。

</br>

### 2. 启动环境

和上一集完全一致，先启动 gnuradio-plutosdr 容器：

```
systemctl start  docker
xhost -
xhost +local:docker
docker run -it --rm \
    --net=host \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v /dev/bus/usb/:/dev/bus/usb/ \
    -v /home/btfz/Desktop/PLUTOSDR:/home/gnuradio/PLUTOSDR \
    --privileged \
    --group-add=audio \
    ubuntu:gnuradio-plutosdr bash
sudo chmod +666 PLUTOSDR
zsh
sudo rm -rf /run/dbus
sudo rm -rf /run/avahi-daemon//pid
sudo mkdir -p /run/dbus   # 确保 dbus 运行所需的目录存在
sudo dbus-daemon --system --fork # 以后台模式启动 dbus-daemon
sudo avahi-daemon -D      # 后台模式运行
SoapySDRUtil --find
```

宿主机 `/home/btfz/Desktop/PLUTOSDR` 被挂载到容器 `/home/gnuradio/PLUTOSDR`，所以本集工程在容器内的路径是：

```
/home/gnuradio/PLUTOSDR/iio/08-pluto_qpsk_file_self_transfer
```

</br>

### 3. 交叉编译并运行

a. 先看目录结构：

```
➜  iio tree -L 1
.
├── 06-pluto_ble_adv_rx         
├── 07-pluto_ble_adv_tx           <-- 上一集：板载蓝牙广播发射机
├── 08-pluto_qpsk_file_self_transfer   <-- 本集：QPSK 文件自收发
└── toolchain                     <-- 前面课程已经装好的交叉编译工具链

➜  08-pluto_qpsk_file_self_transfer tree -L 2
.
├── pluto_qpsk_file_self_transfer.c   # 主程序：getopt + 读文件 + AD9361 配置 + 循环 TX 推流 + RX 采集
├── qpsk_phy.c                        # 【核心】基带算法：前导/DQPSK/交织/组包/检测/解码/重组
├── qpsk_phy.h                        # 帧结构常量 + 基带对外接口
├── makefile                          # build / run / sim / simtest
├── README.md                         # 本工程快速使用说明
├── doc/
│   ├── README.md                     # 工程总览
│   ├── design.md                     # 架构与帧结构
│   └── 01-simulation/                # 单文件仿真版（industrial_pipeline4.c + test.png）
├── skills/                           # 工程笔记：硬规则、实测基线、避坑清单
└── tools/                            # sim_test.sh 仿真回归脚本
```

b. 编译与运行：

`makefile` 和上一集一样**不需要改 IP**，设备地址统一用主机名 `pluto.local`。所有参数都有默认值，`make run` 直接可用：

```
# makefile 里的默认值（可用命令行覆盖）
IN_FILE  ?= test.png     # 待发送文件
OUT_FILE ?= recovered.png# 接收重组文件
LO_MHZ   ?= 2500         # 中心频率 MHz
SAMP_MHZ ?= 2.5          # 采样率 MHz
BW_MHZ   ?= 2.0          # 模拟带宽 MHz
RX_GAIN  ?= 40           # RX 手动增益 dB
TX_ATTN  ?= 10           # TX 衰减 dB
```

```
# 编译（调用交叉编译工具链，链接 liquid-dsp 静态库 + libiio）
➜  08-pluto_qpsk_file_self_transfer make build
arm-linux-gnueabihf-gcc pluto_qpsk_file_self_transfer.c qpsk_phy.c -o pluto_qpsk_file_self_transfer \
    -I.../toolchain/libiio/usr/include -I.../toolchain/liquid-dsp/include \
    -Wall -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
    -L.../toolchain/libiio/usr/lib/arm-linux-gnueabihf \
    .../toolchain/liquid-dsp/lib/libliquid.a -liio -lm -Wl,--allow-shlib-undefined
arm-linux-gnueabihf-strip pluto_qpsk_file_self_transfer

# scp 程序与测试图到 Pluto，ssh 上板运行
➜  08-pluto_qpsk_file_self_transfer make run
scp -O pluto_qpsk_file_self_transfer root@pluto.local:~/
scp -O doc/01-simulation/test.png root@pluto.local:~/test.png
ssh -t root@pluto.local "chmod +x ~/pluto_qpsk_file_self_transfer; ./pluto_qpsk_file_self_transfer -i test.png -o recovered.png -l 2500 -s 2.5 -b 2.0 -g 40 -a 10"

[FILE] Loading test.png ...
[FILE] size=116108, crc=0x516CAD26, chunks=908
[TX] enc_len=259, enc_symbols=1036, packet_symbols=1116, interleave_step=129
[TX] Generated 4064416 RF samples (4.06 M).
* Acquiring IIO context
* Acquiring AD9361 streaming devices
* Configuring AD9361 for streaming
* Acquiring AD9361 phy channel 0
* Acquiring AD9361 RX lo channel
* Acquiring AD9361 phy channel 0
* Acquiring AD9361 TX lo channel
[RF] RX gain = 40.0 dB (manual)
[RF] TX atten = 10.0 dB
* Initializing AD9361 IIO streaming channels
* Enabling IIO streaming channels
[TX] Cyclic buffer filled (4064416 samples), pushing once...
[RX] Collecting 4264416 samples...
[RX] Collected 4264416 samples.
[RX] Symbol timing recovered 2132208 symbols.
[RX] Detected 1884 preamble candidates.
[RX] Header decoded: file_size=116108, crc=0x516CAD26, chunks=908
[RX] Decoded 908 / 908 chunks (unique).
[RX] File CRC OK (0x516CAD26). Writing recovered.png
```

程序启动时打出的三行数字就是整条链路的账本，值得先解释清楚：

| 打印行 | 是什么 | 怎么读 |
|---|---|---|
| `enc_len=259, enc_symbols=1036` | 144 字节 raw 包经 `CRC32 + Hamming(7,4)` 后变 259 字节，再乘 4 = 1036 个 QPSK 符号 | 259 = (144+4) × 7 ÷ 4，说明 packetizer 的编码展开完全可验算 |
| `packet_symbols=1116` | 一个包的物理符号数 | 64 前导 + 1036 数据 + 16 保护零 |
| `interleave_step=129` | 字节交织器的步长 | 与 259 互素，保证每个下标都能被访问且打散距离足够远 |
| `Generated 4064416 RF samples` | 整段基带样点数 | (1024 起始训练 + 1820 包 × 1116 + 64 尾) × 2 样点/符号 |
| `Detected 1884 preamble candidates` | 前导相关峰个数 | 1820 个真包 + 因循环缓冲区多收的一轮重复头 |

最后 `Decoded 908 / 908 chunks` + `File CRC OK` 说明 908 个数据包全部解出、重组文件的 CRC 与发送端一致——把板上的 `recovered.png` 拉回来看，就是原图。

c. 一个反直觉但重要的现象：**不接天线、不接线缆，自收发也能成功**。

这不是玄学，而是单板上 TX→RX 的泄漏/近场耦合构成了一个隐藏闭环：AD9361 的 TX 输出通过 PCB 走线、电源/地平面、芯片封装耦合串进 RX 的 LNA 输入。TX 只衰减 10 dB、RX 手动增益 40 dB，2 MHz 带宽下底噪很低，漏过去的信号 SNR 足够高；再加上 TX/RX 共用同一颗 LO，频偏几乎为 0，对 DQPSK 极其友好，所以直接 908/908。这验证了**基带算法 + libiio 收发链路**，但它不是真实空口信道；要测空口，下一步接天线/线缆并把 RX 增益降下来（0~20 dB）防削顶。

![][p2]

</br>

### 4. 代码介绍

先把整件事用通俗话讲一遍，再逐段看代码。

**发送方向：文件是怎么变成电磁波的？**

一个文件在磁盘上归根到底就是一长串 `0/1`。直接把这串 `0/1` 送进天线是不行的，需要一层层"打包 + 翻译"：

1. **切块**：把文件按 128 字节切成 908 个小块（最后一块不足就补零）。
2. **贴快递单**：每块前面加上同步字、块编号、类型，再算一个 CRC 校验码，接收端拿到后能知道"这是第几块、内容对不对"。
3. **加纠错**：把每块的字节再过一遍 Hamming(7,4) 编码，并像洗牌一样交织，这样就算路上有几个 bit 被噪声打坏，接收端也能纠回来或分散到不同码字里。
4. **翻译成符号**：把 `0/1` 每 2 个分成一组——`00 / 01 / 10 / 11` 四种状态，对应 4 个不同的相位，这就是 QPSK。本集用的是 DQPSK：信息藏在"相邻符号的相位差"里，而不是绝对相位里，所以收发双方不需要事先对齐一个绝对角度。
5. **波形整形**：符号之间如果相位瞬间跳变，会占很宽的频带。RRC 脉冲成形把跳变抹平，让信号既好收又不超出带宽。
6. **数字变射频**：把整形后的数字样点交给 AD9361 的 DAC 变成模拟电压，再上变频到 2.5 GHz，从 TX 端口发射出去——这一步之后，它才真正变成了空间里的电磁波。

**接收方向：电磁波又是怎么变回文件的？**

接收端做的事几乎是发送的逆过程，但多了"找回节奏"这一步：

1. **射频变数字**：RX 端口感应到信号，下变频回基带，ADC 把模拟波形变回数字样点。
2. **清理信号**：去掉直流偏置，再用 AGC 把强弱不定的信号自动调到合适的幅度。
3. **找采样点**：symsync 符号定时恢复负责在波形上找到"眼睛睁得最大"的时刻，每个符号采一个点。
4. **找包起点**：用已知的前导序列做相关，峰出现在哪里，哪里就是一个包的开始。
5. **符号变比特**：对相邻符号求相位差，反推出 `00/01/10/11`，再拼回字节。
6. **解码 + 查错**：解交织、Hamming 纠错、CRC 校验，确认这一块内容完整无误。
7. **拼回文件**：按块编号把 908 块放回正确位置，最后对整份文件再算一次 CRC，一致了才写出 `recovered.png`。

一句话总结：**发送端把文件切成带编号和校验的小包，再把比特翻译成相位变化去调制电磁波；接收端先把电磁波还原成符号和比特，再按编号把小包拼回文件，最后用 CRC 证明"一个字节都没错"。** 

![][p3]

下面进入代码细节：

</br>

#### 1）链路总览与文件分工

和上一集一样，主程序只做"文件 + 射频 + 数据搬运"，真正的信号处理全部集中在 `qpsk_phy.c`（依赖 liquid-dsp）：

```c
/* ---- 主程序 pluto_qpsk_file_self_transfer.c ---- */
uint32_t file_crc = crc_generate_key(LIQUID_CRC_32, file_data, (unsigned int)file_size);
uint32_t num_chunks = (file_size + PAYLOAD_CHUNK_SIZE - 1) / PAYLOAD_CHUNK_SIZE;

unsigned int tx_n = qpsk_phy_build_tx(tx, tx_capacity, file_data, file_size, file_crc, num_chunks);

txbuf = iio_device_create_buffer(tx_dev, (size_t)tx_n, true);  /* true = 硬件循环 */
/* ... 填充 int16 IQ，push 一次 ... */
/* ... RX 非循环 buffer 连续 refill 采集 rx_need 个样本 ... */

int ok = qpsk_phy_rx_process(rx, rx_collected, file_size, file_crc, num_chunks, param_out);
```

`qpsk_phy.h` 把帧结构常量和两个入口函数暴露出来，`qpsk_phy.c` 内部则是一串 `static` 的算法函数。下面按"发送端 → 接收端"的顺序逐个拆。

![][p4]

</br>

#### 2）发送端：组包与编码（CRC32 + Hamming74 + 字节交织）

每个包在进调制器前是一段固定 144 字节的 `raw` 数据，前 16 字节是包头，后 128 字节是净荷：

```c
#define PAYLOAD_CHUNK_SIZE   128
#define PKT_HDR_LEN          16
#define PKT_RAW_LEN          (PKT_HDR_LEN + PAYLOAD_CHUNK_SIZE)  /* 144 */

static void build_header_raw(unsigned char *raw, uint64_t file_size,
                             uint32_t file_crc, uint32_t total_chunks,
                             uint16_t repeat_idx)
{
    memset(raw, 0, PKT_RAW_LEN);
    uint32_t sync = SYNC_WORD;          /* 0xAA55AA55 */
    memcpy(raw + 0, &sync, 4);
    raw[4] = PKT_TYPE_HEADER;           /* 1 */
    uint16_t rep = repeat_idx;
    memcpy(raw + 6, &rep, 2);
    memcpy(raw + 8, &total_chunks, 4);
    memcpy(raw + 12, &file_crc, 4);
    memcpy(raw + 16, &file_size, 8);    /* 文件头净荷：8 字节文件大小 */
}
```

数据包同构，只是把 `type` 改成 `PKT_TYPE_DATA`，把 `raw[8..11]` 写成 chunk index，把 `raw[16..]` 写成该分片的 128 字节文件内容：

```c
static void build_data_raw(unsigned char *raw, uint32_t chunk_idx,
                           const unsigned char *file_data, uint64_t file_size)
{
    memset(raw, 0, PKT_RAW_LEN);
    uint32_t sync = SYNC_WORD;
    memcpy(raw + 0, &sync, 4);
    raw[4] = PKT_TYPE_DATA;             /* 2 */
    uint16_t lo = (uint16_t)(chunk_idx & 0xffffu);
    memcpy(raw + 6, &lo, 2);
    memcpy(raw + 8, &chunk_idx, 4);

    uint64_t off = (uint64_t)chunk_idx * PAYLOAD_CHUNK_SIZE;
    uint64_t remaining = file_size > off ? file_size - off : 0;
    unsigned int cur = remaining < PAYLOAD_CHUNK_SIZE ? (unsigned int)remaining
                                                      : PAYLOAD_CHUNK_SIZE;
    memcpy(raw + PKT_HDR_LEN, file_data + off, cur);
}
```

![][p5]

`raw` 交给 liquid-dsp 的 `packetizer` 做 `CRC-32 + Hamming(7,4)` 编码，得到一个 259 字节的 `enc`。随后做一层**字节交织**，把突发错误在编码块内打散：

```c
static void encode_packet(packetizer pk, const unsigned char *raw,
                          unsigned char *enc, unsigned char *enc_int,
                          unsigned int enc_len, unsigned int step)
{
    packetizer_encode(pk, raw, enc);
    interleave_bytes(enc, enc_int, enc_len, step);
}

/* 交织：out[i] = in[(i*step) % len]；step 与 len 互素 */
static void interleave_bytes(const unsigned char *in, unsigned char *out,
                             unsigned int len, unsigned int step)
{
    for (unsigned int i = 0; i < len; i++)
        out[i] = in[(i * step) % len];
}
```

`step` 由 `interleave_step_for(enc_len)` 选出来：从 `enc_len/2` 往下找第一个与 `enc_len` 互素的数，259 时得到 129。交织后相邻字节在解交织时会被摊到相隔很远的 Hamming 码字里，这样一段连续误码不会集中打穿同一个 7 bit 码字。

![][p6]

</br>

#### 3）发送端：非周期前导生成

每个包最前面是 64 个已知 QPSK 符号，接收端靠它做相关检测。前导用 16-bit LFSR 现算，**刻意做成非周期**：

```c
static void gen_preamble(unsigned char *sym_idx, float complex *sym_cplx,
                         unsigned int total, modem mod)
{
    uint16_t lfsr = 0xACE1u;
    for (unsigned int i = 0; i < total; i++) {
        /* taps: x^16 + x^14 + x^13 + x^11 + 1 */
        unsigned int bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
        lfsr = (uint16_t)((lfsr >> 1) | (bit << 15));
        unsigned int s = lfsr & 0x3u;

        sym_idx[i] = (unsigned char)s;
        modem_modulate(mod, s, &sym_cplx[i]);
    }
}
```

如果前导是周期序列（比如两半相同），64 符号相关会在"错开 32 符号"的位置也出现等高峰，候选起点就会差 32 的整数倍，整包解不出来。非周期 LFSR 序列的自相关只在 0 处尖锐，检测器拿到的就是唯一峰。

![][p7]

</br>

#### 4）发送端：DQPSK 调制（显式相位增量）

数据段没有直接用绝对 QPSK，而是 DQPSK。这里有一个必须绕开的坑：liquid-dsp 的 QPSK 星座索引顺序是 Gray 码（`0→45°, 1→135°, 2→-45°, 3→-135°`），**索引 +1 不等于相位 +90°**，所以不能拿符号索引做差分。正确做法是显式维护相位：

```c
static void build_packet_symbols(const unsigned char *enc_int,
                                 unsigned int enc_len, unsigned int enc_symbols,
                                 const float complex *pre_cplx,
                                 float complex *pkt_syms)
{
    const float complex delta[4] = {1.0f, _Complex_I, -1.0f, -_Complex_I};
    unsigned int sym_idx = 0;

    /* 前导：绝对 QPSK，已知序列 */
    for (unsigned int i = 0; i < PREAMBLE_LEN; i++)
        pkt_syms[sym_idx++] = pre_cplx[i];

    /* 数据：DQPSK，相位参考从前导末符号延续 */
    float complex carrier = pre_cplx[PREAMBLE_LEN - 1];
    for (unsigned int i = 0; i < enc_len; i++) {
        unsigned char b = enc_int[i];
        for (int sh = 6; sh >= 0; sh -= 2) {
            unsigned int d = (b >> sh) & 0x3u;
            carrier *= delta[d];
            pkt_syms[sym_idx++] = carrier;
        }
    }

    /* 包间保护零符号 */
    for (unsigned int i = 0; i < GUARD_SYMBOLS; i++)
        pkt_syms[sym_idx++] = 0.0f;
}
```

`delta[] = {1, I, -1, -I}` 就是四个相位增量 `{0°, +90°, 180°, -90°}`。`carrier` 从**前导末符号**开始，每 2 bit 乘一次对应的 `delta`，于是当前符号的绝对相位 = 上一符号相位 + 数据相位增量。接收端只要做相邻符号相位差，就能还原 2 bit，固定相偏和 90° 相位模糊在差分里自动抵消。

![][p8]

</br>

#### 5）发送端：RRC 脉冲成形 + 起始训练段

符号流先经过 RRC（根升余弦）插值，`k=2` 表示每个符号 2 个采样点：

```c
unsigned int h_len = 2 * TX_K * TX_M + 1;   /* 29 阶 */
float h[h_len];
liquid_firdes_prototype(LIQUID_FIRFILT_RRC, TX_K, TX_M, TX_BETA, 0.0f, h);
firinterp_crcf interp = firinterp_crcf_create(TX_K, h, h_len);
```

`TX_M=7, TX_BETA=0.30` 控制滤波器阶数和滚降。正式发包前先发 1024 个**随机 QPSK 训练符号**，序列与包前导不同，作用是让接收端 AGC / symsync 先收敛，又不产生前导相关峰：

```c
{
    uint16_t lfsr = 0x5A5Au;   /* 与前导不同种子 */
    for (unsigned int i = 0; i < STARTUP_SYMBOLS; i++) {
        unsigned int bit = ((lfsr >> 0) ^ (lfsr >> 1) ^ (lfsr >> 4) ^ (lfsr >> 6)) & 1u;
        lfsr = (uint16_t)((lfsr >> 1) | (bit << 15));
        float complex s;
        modem_modulate(mod, lfsr & 0x3u, &s);
        firinterp_crcf_execute(interp, s, &tx[tx_cursor]);
        tx_cursor += TX_K;
    }
}
```

之后按 `HEADER_REPEATS(4) + num_chunks × REPEAT_FACTOR(2)` 的顺序把每个包符号逐一过 `firinterp_crcf_execute()` 写入 `tx`，最后再 flush 64 个零符号把滤波器尾巴吐干净。头包重复 4 次、数据包重复 2 次，是给"丢包"留的冗余：同一 chunk 两副本只要收到一个就算数。

![][p9]  

</br>

#### 6）发送端：硬件循环缓冲区推流一次

`qpsk_phy_build_tx()` 返回的是 float 复数样点，主程序把它缩放到 12-bit DAC 满量程的 0.8 倍，填进一个**长度恰好等于 tx_n 的硬件循环缓冲区**，然后只 push 一次：

```c
float peak = 1e-6f;
for (unsigned int i = 0; i < tx_n; i++) {
    float m = cabsf(tx[i]);
    if (m > peak) peak = m;
}
float tx_scale = 0.8f * 32767.0f / peak;

txbuf = iio_device_create_buffer(tx_dev, (size_t)tx_n, true); /* cyclic */
...
for (char *p = (char *)iio_buffer_first(txbuf, tx0_i); p < p_end; p += p_inc) {
    if (cnt < tx_n) {
        ((int16_t *)p)[0] = (int16_t)lrintf(crealf(tx[cnt]) * tx_scale);
        ((int16_t *)p)[1] = (int16_t)lrintf(cimagf(tx[cnt]) * tx_scale);
    } else {
        ((int16_t *)p)[0] = 0;
        ((int16_t *)p)[1] = 0;
    }
    cnt++;
}
iio_buffer_push(txbuf);
```

沿用上一集"硬件定时"的思路：整段交给 DMA 循环播放，节奏由采样时钟决定，程序只负责活着和收数。循环播放意味着 RX 多收的 margin 会收到下一轮开头（重复的 header 和前面几个 chunk），但这无害——重组靠 chunk index 去重。

</br>

#### 7）接收端：int16 → float，直流抑制，软件 AGC

RX 非循环 buffer 连续 refill，把 AD9361 的 12-bit MSB 对齐 int16 还原成 float 复数：

```c
while (!stop && rx_collected < rx_need) {
    ssize_t nb = iio_buffer_refill(rxbuf);
    ...
    for (char *p = (char *)iio_buffer_first(rxbuf, rx0_i);
         p < p_end && rx_collected + cnt < rx_need;
         p += p_inc, cnt++) {
        int16_t i = ((int16_t *)p)[0];
        int16_t q = ((int16_t *)p)[1];
        rx[rx_collected + cnt] = ((float)i / 32768.0f) + _Complex_I * ((float)q / 32768.0f);
    }
    rx_collected += cnt;
}
```

采满后进入 `qpsk_phy_rx_process()`。第一步是直流抑制 + 软件 AGC：

```c
static void dc_block(float complex *x, unsigned int n)
{
    float complex dc = 0.0f;
    const float alpha = 1e-4f;
    for (unsigned int i = 0; i < n; i++) {
        dc = dc * (1.0f - alpha) + x[i] * alpha;
        x[i] -= dc;
    }
}

static float complex agc_execute(simple_agc *a, float complex x)
{
    float mag2 = crealf(x) * crealf(x) + cimagf(x) * cimagf(x);
    float alpha = (mag2 > a->power) ? a->attack : a->decay;   /* 快攻慢放 */
    a->power = (1.0f - alpha) * a->power + alpha * mag2;
    if (a->power < 1e-8f) a->power = 1e-8f;
    float target_gain = 1.0f / sqrtf(a->power);
    a->gain = 0.9f * a->gain + 0.1f * target_gain;
    ...
    return x * a->gain;
}
```

`dc_block` 是一阶高通，去掉 AD9361 残余直流；AGC 用 `attack=0.05` 快速压低强信号、`decay=0.0005` 缓慢抬高弱信号，把样点功率归一化到 1 附近，给后续 symsync 一个稳定的输入幅度。

![][p10]

</br>

#### 8）接收端：符号定时恢复 symsync

定时恢复用 liquid-dsp 的 `symsync_crcf`，内部是"RRC 匹配滤波 + 32 滤波器组 + 定时误差反馈环"：

```c
symsync_crcf symsync = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC,
                                                    TX_K, TX_M, TX_BETA, TX_FILTERS);
symsync_crcf_set_lf_bw(symsync, 0.02f);

unsigned int max_symbols = rx_n / TX_K + 4096;
float complex *rx_symbols = (float complex *)malloc(max_symbols * sizeof(float complex));
unsigned int rx_sym_count = 0;
symsync_crcf_execute(symsync, rx_samples, rx_n, rx_symbols, &rx_sym_count);
```

输入是 2.5 Msps 的样点流，输出是 1.25 Msym/s 的符号流（`k=2` 抽 1）。`TX_FILTERS=32` 让滤波器组覆盖 ±0.5 符号的定时相位，环能自动跟踪采样钟偏差，把每个符号采到眼图最开的位置。

</br>

#### 9）接收端：前导相关检测（逐包重同步的核心）

对符号流做 64 长滑动相关，用**能量归一化**让门限不受增益波动影响：

```c
static float preamble_corr(const float complex *rx, unsigned int s,
                           const float complex *pre, unsigned int P)
{
    float complex acc = 0.0f;
    float energy = 0.0f;
    for (unsigned int n = 0; n < P; n++) {
        float complex x = rx[s + n];
        acc += x * conjf(pre[n]);
        energy += crealf(x) * crealf(x) + cimagf(x) * cimagf(x);
    }
    return cabsf(acc) / sqrtf(energy * (float)P + 1e-12f);
}
```

检测器扫整条符号流，超过 `PREAMBLE_THRESH(0.55)` 就做 ±2 局部峰值细化，再用 `min_gap`（一个前导长度）抑制同一前导的重复计数：

```c
for (unsigned int s = 0; s + P <= n; s++) {
    float c = preamble_corr(rx, s, pre, P);
    if (c < PREAMBLE_THRESH)
        continue;

    int best = (int)s;
    float best_c = c;
    for (int d = -2; d <= 2; d++) { ... 找局部最大 ... }

    if (best > last_peak + min_gap) {
        candidates[count++] = (unsigned int)best;
        last_peak = best;
        s = best + min_gap - 1;
    }
}
```

因为**每个包都有前导**，检测器拿到的是整条流里所有包的起点，而不是"锁一次就死跟"。任何一包坏了，下一包用自己的前导重新定位，这就是"逐包重同步"。

![][p11]

</br>

#### 10）接收端：DQPSK 差分解码

对每个候选起点，跳过 64 个前导符号，对后面 1036 个数据符号做相邻相位差：

```c
static int decode_packet_at(const float complex *rx, unsigned int s,
                            unsigned int P, packetizer pk,
                            unsigned int enc_len, unsigned int enc_symbols,
                            unsigned int deint_step, unsigned char *raw_out)
{
    unsigned char *enc_int = (unsigned char *)malloc(enc_len);

    float complex prev = rx[s + P - 1];   /* 前导末符号作为相位参考 */
    unsigned int bitbuf = 0, nbits = 0, byteidx = 0;

    for (unsigned int i = 0; i < enc_symbols && byteidx < enc_len; i++) {
        float complex cur = rx[s + P + i];
        float diff = cargf(cur * conjf(prev));
        prev = cur;

        /* 归一化到 [-pi, pi) 后量化到 90° 整数倍 */
        float a = diff;
        while (a >=  M_PI) a -= 2.0f * M_PI;
        while (a <  -M_PI) a += 2.0f * M_PI;
        int k = (int)roundf(a / (M_PI / 2.0f));
        unsigned int d = ((k % 4) + 4) % 4;

        bitbuf = (bitbuf << 2) | d;
        nbits += 2;
        if (nbits == 8) {
            enc_int[byteidx++] = (unsigned char)bitbuf;
            bitbuf = 0;
            nbits = 0;
        }
    }

    unsigned char *enc = (unsigned char *)malloc(enc_len);
    deinterleave_bytes(enc_int, enc, enc_len, deint_step);
    int crc_pass = packetizer_decode(pk, enc, raw_out);
    ...
}
```

`cargf(cur * conjf(prev))` 就是两个相邻符号的相位差。因为 DQPSK 编码时 `cur = prev × delta[d]`，所以这个相位差只取决于数据 dibit，与载波绝对相位无关；固定相偏在 `cur * conjf(prev)` 里被抵消，小频偏只是给每个相位差加一个很小的常数，量化到 90° 整数倍后仍落在正确区间。量化后每 4 个 dibit 拼回一个字节，解交织，再交给 `packetizer_decode()` 做 Hamming 纠错 + CRC 校验。

</br>

#### 11）接收端：重组与文件级 CRC

`qpsk_phy_rx_process()` 对候选点做**两遍解码**：第一遍只找 header，第二遍解数据包：

```c
/* 第一遍：找文件头 */
for (unsigned int i = 0; i < num_candidates; i++) {
    if (!decode_packet_at(..., raw)) continue;
    uint32_t sync; memcpy(&sync, raw, 4);
    if (sync != SYNC_WORD || raw[4] != PKT_TYPE_HEADER) continue;
    memcpy(&file_size_rx, raw + 16, 8);
    memcpy(&file_crc_rx, raw + 12, 4);
    memcpy(&total_chunks_rx, raw + 8, 4);
    header_ok = 1;
    break;
}

/* 第二遍：解数据包，按 chunk index 去重重组 */
for (unsigned int i = 0; i < num_candidates; i++) {
    if (!decode_packet_at(..., raw)) continue;
    if (sync != SYNC_WORD || raw[4] != PKT_TYPE_DATA) continue;
    uint32_t idx; memcpy(&idx, raw + 8, 4);
    if (idx >= num_chunks || chunk_flags[idx]) continue;   /* 重复包自动去重 */
    memcpy(file_rx + off, raw + PKT_HDR_LEN, cur);
    chunk_flags[idx] = 1;
    decoded_chunks++;
}

/* 文件级 CRC 是最终判据 */
if (decoded_chunks == num_chunks) {
    unsigned int crc_rx = crc_generate_key(LIQUID_CRC_32, file_rx, (unsigned int)file_size);
    if (crc_rx == file_crc_rx) {
        FILE *fout = fopen(out_filename, "wb");
        fwrite(file_rx, 1, file_size, fout);
        ...
    }
}
```

每个数据包的 chunk index 写在 `raw[8..11]`，重复包（`REPEAT_FACTOR=2` 发了两遍）只取第一个 CRC 通过的。全部 908 个 chunk 收齐后，对重组文件整体算一次 CRC-32，和 header 里的 `file_crc` 一致才写文件——**"chunk 收齐"只是必要条件，"文件 CRC 一致"才是最终成功**。

</br>

### 5. 结语

随着 `make run` 终端里滚出 `Decoded 908 / 908 chunks` 和 `File CRC OK`，再把板上的 `recovered.png` 拉回来打开，一张 116KB 的图片已经完整走完了"文件 → 分片 → QPSK 调制 → 射频 → 接收解调 → 重组"的闭环。

回看这条链路：从字节流到空口符号的每一层转换都在 Pluto 的 ARM 上现算，核心基带集中在 `qpsk_phy.c`（约 500 行纯 C + liquid-dsp），主程序只做文件与 AD9361 I/O。相比上一集 GFSK 的"短突发广播"，这一集第一次把**前导检测、差分调制、符号定时恢复、逐包重同步、交织纠错、文件级校验**这些现代数字通信的要素串成了一条真正能传文件的链路。

从第一行 `printf("hello pluto sdr")`，到 **FM 收音机**、**频谱分析仪**、**ADS-B 解码**、**蓝牙广播收发**，再到前一阶段的 **QPSK 文件自收发**——你已经能在一块 Pluto 上驾驭一条完整的无线数字通信链路。

但单载波在复杂的现实无线环境中，常常要面对**多径衰落**和**码间干扰（ISI）**的严峻考验。如何把无线通信带向更高的**频谱效率**和**对抗多径**的终极形态？

下一集，我们将正式迈入 OFDM（正交频分复用）的世界，挑战在 PlutoSDR 板载（Zynq 嵌入式环境）实现真正的 **OFDM 文件自收发**！

</br>

### 参考链接

[[1]. 第四集：断开脐带 —— 交叉编译与 PlutoSDR 嵌入式脱机运行实战][#1]    
[[2]. 上一集：PlutoSDR 嵌入式脱机 BLE 广播发射机全链路构建][#2]     
[[3]. Analog Devices —— libiio `ad9361-iiostream.c`（本集主程序的底层框架来源）][#3]     
[[4]. liquid-dsp —— 本集使用的 packetizer / symsync / firinterp 算法库][#4]      

[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/110     
[#2]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/80     
[#3]:https://github.com/analogdevicesinc/libiio/blob/v0.25/examples/ad9361-iiostream.c     
[#4]:https://github.com/jgaeddert/liquid-dsp      

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_arch.png       
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/pluto_qpsk_file_run_show.gif           
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_data_flowgraph.png          
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_code_flowgraph.png           
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_pack_frame.png       
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_pack_hamming_interleaving.jpeg       
[p7]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_pack_preamble.jpeg       
[p8]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_pack_qpsk.jpeg       
[p9]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_pack_rrc.jpeg       
[p10]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_pack_dc_agc.jpeg      
[p11]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_qpsk_file_pack_preamble_corr.jpg     


