### 1. 开场

上一集我们把 Pluto 变成了一台**板载蓝牙广播嗅探器**：不接任何蓝牙芯片，直接在 Zynq 的 ARM 上把 2.4GHz 的 BLE 广播包解出来，AdvA、设备名、厂商数据一条条打在终端里。这一集做反过来的事——**让 Pluto 自己变成一个蓝牙设备**：用纯 C 现场造一个 LE 1M 的广播包，从 AD9361 的发射口喊出去，让手机上的 nRF Connect 看得见，让上一集自己的接收机解得开。

BLE 广播包很短（本集的包 40 字节 = 320 bit，空中只占 1.324 ms），但"短"不等于"简单"，它把 GFSK 调制、白化、CRC、定时这几件事全压在了一毫秒里：

- LE 1M PHY：**GFSK 调制、符号率 1 Msym/s**（1µs 一个符号）、标准频偏 ±250kHz、调制指数 h 应在 0.45~0.55；
- 空中数据流 = **8 bit 前导码 `10101010` + 32 bit access address `0x8E89BED6` + PDU + 3 字节 CRC-24**，其中 PDU 与 CRC **全程白化**（种子 = 实际发射信道），这样哪怕载荷是长串 0 也不会出现长时间无跳变的波形；
- 广播是**周期性的突发**：发完一个包要留一段静默（本集留 1ms），再等下一个广播间隔；
- 除了波形本身，发射端还有四个"看不见但决定成败"的参数：**中心频率**、**白化种子**、**发射电平（衰减）**、**广播间隔**。这四个参数只要有一个和接收端的假设对不上，现象就是"手机上什么都看不到"，而且**极难靠肉眼区分是哪一层的锅**。

本集死磕的链路（全部跑在 Pluto 板载 ARM 上）：

**PDU 组装 → CRC-24 → 白化 → LSB 比特展开 → 高斯成形（BT≈0.5） → 相位累加查表 → int16 IQ → 硬件循环缓冲区（长度 = 采样率 × 广播间隔）→ DMA 按采样时钟自主循环发射**

一路上有三件事值得先说清楚，它们是全文的暗线：`-s` 采样率一旦离开 4MHz，发出去的就**不再是蓝牙**；手机上看到的 7.5 秒通、5 秒断，其实是**手机自己在骗你**；而本集真正的主角是第三件——**广播周期不能交给软件 `usleep`，必须交给硬件循环缓冲区**。前两件我只用两个小节讲清结论，第三件会讲透：把一个周期的样点数交给 DMA，广播间隔的误差就只剩板上晶振的 ppm 级偏差。

![][p1]

</br>

---

**补充：** 之前我已经出过详细的[蓝牙专项][#8] ([B站系列视频][#7])，详细解析了 BLE 广播包的[接收][#6]与[发送][#5]：

![][p5]

![][p6]

---

</br>

### 2. 启动环境

学完前几课，大家应该有如下启动开发环境的命令：

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

</br>

### 3. 交叉编译并运行

a.先看目录结构：

```
➜  iio tree -L 1
.
├── 01-hello_plutosdr
├── 02-ad9361-iiostream
├── 03-pluto_fm_radio
├── 04-pluto_spectrum_analyzer
├── 05-pluto_adsb_receiver
├── 06-pluto_ble_adv_rx           <-- 上一集：板载蓝牙广播接收机
├── 07-pluto_ble_adv_tx           <-- 本集
└── toolchain                     <-- 前面课程已经装好的交叉编译工具链

➜  07-pluto_ble_adv_tx tree
.
├── pluto_ble_adv_tx.c   # 主程序：参数解析 + AD9361 配置 + PDU/CRC/白化组装 + 循环发射
├── ble_phy.c            # 【核心】纯 C 调制器：比特 -> 高斯成形 -> 相位查表 -> int16 IQ
├── ble_phy.h            # 对外接口 + 收发共用函数（CRC-24 / 白化 / LSB 位展开）
├── makefile             # build / run（CH / INTERVAL / ATTN / SAMPLE 四个旋钮）
└── skills/              # 工程笔记：硬规则、实测基线、避坑清单（想直接抄结论看这里）
```

b.编译与运行：

和上一集一样，`makefile` **不需要改 IP** —— 设备地址统一用主机名 `pluto.local`（IIO over IP 和 SSH 都用它）。四个旋钮都有默认值，`make run` 直接可用：

```
# makefile 里的默认值（可用命令行覆盖）
CH       ?= 37      # 信道号：同时决定中心频率和白化种子
INTERVAL ?= 200     # 广播间隔 ms
ATTN     ?= 30      # 发射衰减 dB（0 = 最大输出）
SAMPLE   ?= 4       # 采样率 MHz（只有 4.0 才是标准 BLE 符号率）
```

```
# 编译（调用交叉编译工具链）
➜  09-pluto_ble_adv_tx make build
/home/gnuradio/PLUTOSDR/iio/09-pluto_ble_adv_tx/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc \
    pluto_ble_adv_tx.c ble_phy.c -o pluto_ble_adv_tx \
    -I.../toolchain/libiio/usr/include -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
    -L.../toolchain/libiio/usr/lib/arm-linux-gnueabihf -liio -lm -Wl,--allow-shlib-undefined
.../arm-linux-gnueabihf-strip pluto_ble_adv_tx

# scp 到 Pluto，ssh 上板运行（需要输入 Pluto 的密码）
➜  09-pluto_ble_adv_tx make run
scp -O pluto_ble_adv_tx root@pluto.local:~/
root@pluto.local's password: 
pluto_ble_adv_tx                                   100%   18KB
------------------------------------------------
 ____  _          _____ _           _ 
| __ )| | ___    | ____| |_   _____| |
|  _ \| |/ _ \   |  _| | \ \ / / _ \ |
| |_) | |  __/   | |___| |\ V /  __/ |
|____/|_|\___|   |_____|_| \_/ \___|_|
                                        
------------------------------------------------
ssh -t root@pluto.local "chmod +x ~/pluto_ble_adv_tx; ./pluto_ble_adv_tx -c 37 -i 200 -a 30 -s 4"
root@pluto.local's password: 
* Acquiring IIO context
* Acquiring AD9361 streaming devices
* Configuring AD9361 for streaming
* Acquiring AD9361 phy channel 0
* Acquiring AD9361 TX lo channel
* Initializing AD9361 IIO streaming channels
* Enabling IIO streaming channels
[RF INFO] AD9361 发射衰减已设为 30.00 dB (0 = 最大输出)

==================================================================
> pre_wt_adv_pdu:
HEX: 42 1E 06 05 04 03 23 DC 17 09 68 65 6C 6C 6F 20 62 74 66 7A 27 73 20 67 6E 75 2D 72 61 64 69 6F 
> Calculated CRC-24 (空中字节序): F4 0F D3
------------------------------------------------------------------
> ll data final:
HEX: AA D6 BE 89 8E CF CC 51 A4 39 A4 45 6C 62 38 79 2D FA 1B 97 C3 24 9D CD AA B9 20 13 BF D4 ED 25 56 AA 5F 95 1E 57 FB 86 
==================================================================

[SUCCESS] 基带转换完成。IQ 发射采样总数 (含留白): 5296

>>> Edge-Computing BLE ADV Transmitter Started <<<
Target Dev  : default
BLE Channel : 37 (2402.00 MHz, 白化种子 37)
Sample Rate : 4.00 MHz
TX Atten    : 30.00 dB
ADV Interval: 200 ms

[TX] 硬件循环周期样点数 = 800000 (对应设定间隔 200 ms)
```

程序启动时会把**要发射的那串字节**完整打出来，这三行是理解整条链路的钥匙：

| 打印行 | 是什么 | 怎么读 |
|---|---|---|
| `pre_wt_adv_pdu` | 白化**之前**的 PDU（32 字节） | `42` = 广播头（低 4 bit `0x2` = ADV_NONCONN_IND，bit6 的 `1` = TxAdd，表示随机地址）；`1E` = 长度 30 = AdvA 6 + 广播数据 24；接下来 6 字节是 **AdvA 的小端序**，所以 `06 05 04 03 23 DC` 在手机上显示为 `DC:23:03:04:05:06`；`17 09` 是"设备名"这条 AD 结构（长度 23 = 22 字节名字 + 1 字节类型），后面 22 字节就是 `hello btfz's gnu-radio` |
| `Calculated CRC-24` | 对上面 32 字节算出的 CRC-24（空中字节序） | 校验方向是 `swap(高), swap(中), swap(低)`，不是常见的 LSB-first，这是上一集踩过的坑 2；**这个值随 MAC/设备名变化**，所以提示里不要写死任何数字 |
| `ll data final` | 真正展开成比特去调制的 40 字节 | 头部 5 字节 `AA D6 BE 89 8E` = 8 bit 前导码 `0xAA` + 32 bit access address **`0x8E89BED6` 的 LSB 先发**（`D6 BE 89 8E`，上一集坑 1 说的就是这个顺序）；后面 35 字节是"PDU + CRC"整体白化后的结果 |

最后两行是发射端的时间账：`5296` 是**一个包**占的样点数（1296 样点的调制输出 + 4000 样点留白，也就是那个 1ms 静默），而 `800000` 是**硬件循环缓冲区的总长度** = 4MHz × 200ms —— DMA 会按照这个长度无限循环，广播周期就是这 800000 个样点。这是本集最关键的一处设计，第 4.5 节展开讲。

![][p2] 

c.空口验证：**用上一集的接收机当校验仪**

发射端"自己说自己发出去了"没有意义，本集的验收标准是：**上一集的 08 工程（同一块板子的接收链路）能把本集发出去的包解出来，且 CRC 全过**。做法是先把接收 LO 对到发射信道（37 → 2402MHz），再抓一段原始 IQ：

```
# 1) 把接收 LO 对到发射信道（这一步最容易被忽略：LO 停在 2400 时，2402 会落在 4MHz 带宽的奈奎斯特边缘，一个包都解不出来）
➜  iio_attr -u ip:pluto.local -c ad9361-phy altvoltage0 frequency 2402000000
2401999998

# 2) 抓 15 秒空口（只读，不动发射配置；4MHz 采样 = 16MB/s）
➜  iio_readdev -u ip:pluto.local -b 262144 -s 60000000 cf-ad9361-lpc voltage0 voltage1 > cap.bin
➜  ls -l cap.bin
-rw-r--r-- 1 btfz users 240000000   cap.bin      # 60M 样点 = 15.0 s

# 3) 用 08 工程的解调器解这一段（自己写个几十行的小驱动，链接 08 工程同一份 ble_phy.c；信道 37 = 解白化种子）
➜  ./decode_file cap.bin 37 0 15
[PKT #6] BLE 广播包 (CH 37, 位置 409680, AA 误差 0/40, PDU 32 bytes, CRC OK)
[PDU] 42 1E 06 05 04 03 23 DC 17 09 68 65 6C 6C 6F 20 62 74 66 7A 27 73 20 67 6E 75 2D 72 61 64 69 6F F4 0F D3 
[ADV] type=0x2  AdvA=DC:23:03:04:05:06
```

解调 15 秒，能看到两件事：

```
共解出 495 个 CRC 通过包，来自 9 个广播地址（周围的手机/家电都在广播）
其中我们自己的 DC:23:03:04:05:06：73 个，全部 CRC OK
  时间范围 0.169 ~ 14.909 s（连续 14.74 秒）
  间隔 ms: 最小 200.6 | 中位 201.2 | 平均 204.7 | 最大 404.7
  间隔 >500ms 的空洞: 0 个 | >1s: 0 个
```

也就是说：**包本身完全正确**（PDU / CRC / MAC 逐字节对上），**空口上确实一直在发**（14.74 秒无 5 秒级空洞），手机上的 nRF Connect 也能看到 `DC:23:03:04:05:06` 和 `hello btfz's gnu-radio`。

不过这一轮的数据里已经埋着下一节的引子：这版的广播周期还是软件"睡"出来的，**宿主推流下中位 201.2ms 看着没问题，可同一份代码搬到板子上跑，Ellisys 量到的却是另一个数（10/20/100ms 指令 → 35/75/395ms）**。同一份代码换个运行方式，周期就变了——这才引出了第 4.5 节的设计。

</br>

### 4. 代码介绍

#### 1）链路总览与文件分工

整个调制链集中在 `ble_phy.c`（纯 C，不依赖任何第三方 DSP 库），主程序只负责"把字节凑齐 + 配好射频 + 把周期交给硬件"：

```c
/* ---- 1. 组包：PDU -> CRC-24 -> 白化 -> LL 数据（AA + 白化后的 PDU/CRC） ---- */
uint8_t mac[6] = {0xDC, 0x23, 3, 4, 5, 6};
int  adv_data_len = adv_name_len + 2;
pdu_adv[0] = 0x42;                          /* ADV_NONCONN_IND + TxAdd=1 */
pdu_adv[1] = (adv_data_len + 6) & 0xFF;     /* PDU 长度 = AdvA 6 + 数据 */
for (int i = 0; i < 6; i++) pdu_adv[2 + i] = mac[5 - i];   /* AdvA 小端序 */
bt_crc(pdu_adv, pdu_len, crc_bytes);                       /* CRC-24（空中字节序） */
bt_dewhitening(pdu_adv_crc, pdu_crc_len, param_ch, pdu_adv_crc_wt);  /* 种子 = 信道号 */
ll_payload[0..4] = {0xAA, 0xD6, 0xBE, 0x89, 0x8E};          /* 前导码 + AA(LSB 先发) */

/* ---- 2. 调制：LSB 展开 -> 高斯成形 -> 相位查表 ---- */
int total_bits   = bytes_to_bits_lsb(ll_payload, ll_payload_len, ll_datas_bit);
int gen_samples  = gen_sample_from_phy_bit(ll_datas_bit, total_bits, mod_iq_buf);

/* ---- 3. 发射：一个周期一个缓冲区，硬件循环播放 ---- */
txbuf = iio_device_create_buffer(tx, target_samples, true);  /* true = 硬件循环 */
```

`pluto_ble_adv_tx.c` 负责外围：`getopt` 解析 `-c/-i/-a/-s/-d`、AD9361 配置（4 Msps、模拟带宽 2MHz、`A` 端口、显式写发射衰减）、清零 DDS、组装 PDU、创建循环缓冲区并发射。`ble_phy.c/h` 是**收发共用**的基带层（上一集的接收机也用同一份文件里的 CRC-24/白化函数，这也是"发出去的包能被上一集解出来"的前提）。

![][p3]

#### 2）GFSK 调制器：高斯成形核 + 相位累加查表

BLE 是 GFSK：**1 = 正向频偏，0 = 负向频偏**，符号之间平滑过渡（不是 FSK 那种瞬间跳频）。`gen_sample_from_phy_bit()` 用最省算力的方式实现了它：

```c
/* 1. 把每个符号摆进过采样缓冲（1 个比特 -> 4 个样点，但只在符号起点放一个 ±1 冲激） */
for (int i = 0; i < num_bit * SAMPLE_PER_SYMBOL; i++)
    if (i % SAMPLE_PER_SYMBOL == 0)
        tmp_phy_bit_over_sampling[i + LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL - 1] =
            (float)(bits[i / SAMPLE_PER_SYMBOL]) * 2.0f - 1.0f;

/* 2. 高斯成形核（BT≈0.5）：16 项里下标 4~12 这 9 个会被用到，抽头和 = 256 */
int gauss_coef_int8[16] = {0, 0, 0, 0, 2, 11, 32, 53, 60, 53, 32, 11, 2, 0, 0, 0};

/* 3. 相位累加 + 1024 点查表：每一步的相位增量就是这一点的瞬时频偏 */
for (int i = 0; i < active_samples - 1; i++) {
    int32_t acc = 0;
    for (int j = 3; j < (LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL - 4); j++)
        acc += gauss_coef_int8[LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL - j - 1]
             * (int)tmp_phy_bit_over_sampling[i + j];
    tmp_phase = (tmp_phase + acc) % 1024;      /* 负值再 +1024，防相位撕裂 */
    if (tmp_phase < 0) tmp_phase += 1024;
    iq_samples_dst[(i + 1) * 2]     = cos_table[tmp_phase];
    iq_samples_dst[(i + 1) * 2 + 1] = sin_table[tmp_phase];
}
```

代码里那三个数字不是随手写的，都能验算：**抽头和 256 ÷ 相位表 1024 = 每符号前进 1/4 圈**，在 1 Msym/s 下就是 **±250kHz** 频偏，由此 **h = 2 × 250kHz / 1Msym/s = 0.50**，正好落在规范要求的 0.45~0.55 中间；而相位表幅度取 24000（int16 满量程的 **−2.7 dBFS**），是给高斯成形的过冲留的余量，避免削顶。

宿主机上把 `gen_sample_from_phy_bit()` 的输出直接做 FFT 与瞬时频率统计，量化结果如下：

| 指标 | 实测 | 判据 |
|---|---|---|
| 每符号频偏 | ±250 kHz | 规范 ±250kHz（h 0.45~0.55） |
| 调制指数 h | 0.50 | 0.45 ~ 0.55 ✅ |
| −20 dB 占用带宽 | 1098 kHz | ≤ 1 MHz 信道附近 ✅ |
| ±1 MHz 处杂散 | −30.9 dB | ≤ −20 dB ✅ |
| ±2 MHz 处杂散 | −71.6 dB | ≤ −30 dB ✅ |
| 位重建 | 320/320 bit 正确 | 含 48 bit 前导+AA 逐位一致 ✅ |

![][p4]

</br>

#### 3）关键设计 1：信道号是"频率 + 白化种子"的唯一来源

早期版本的坑很典型：**中心频率写在参数里、白化种子却是代码里写死的 `37`**。结果只要把频率改到 38 信道，发出去的包波形没问题、频谱没问题、自己写的接收端也能解（因为它知道用 37 解），但**任何标准接收机（手机、nRF Connect）一个包都解不出来**——因为 BLE 的白化种子就是信道号，接收端永远按"我当前在哪个信道"去解白。

所以现在只留一个事实来源：信道号。它同时决定中心频率与白化种子，两个动作永远同步：

```c
static double ble_ch_freq_mhz(int ch) {
    if (ch == 37) return 2402.0;
    if (ch == 38) return 2426.0;
    if (ch == 39) return 2480.0;
    if (ch >= 0  && ch <= 10) return 2404.0 + 2.0 * ch;       /* 数据信道 */
    if (ch >= 11 && ch <= 36) return 2402.0 + 2.0 * (ch + 2);
    return -1.0;
}
...
bt_dewhitening(pdu_adv_crc, pdu_crc_len, param_ch, pdu_adv_crc_wt);   /* 种子 = param_ch */
```

`-c 38` 就是"2426MHz + 种子 38"一起换。`-f`（手动覆盖频率）保留下来做实验，但**一旦和信道号不一致就告警**，明确告诉你"这样只有知道你在干什么的接收端能解"。

#### 4）关键设计 2：发射衰减必须显式写一次

老版本的注释写着"0dB 最大输出"，代码里却**从来没写过 `hardwaregain`** —— 实际发射电平是驱动的上电默认值。这一集把它读了出来（板子刚上电、程序还没跑的状态）：

```
voltage0 (output) 采样率 4000000 . rf_bandwidth 2000000 . hardwaregain -30.00 dB
```

也就是说**以前所有实验其实是在 −30dB 衰减（比最大输出低 30dB）下跑的**，"电平不可复现"在联调时极其致命：昨天和今天测的结果可能差 30dB，而你并不知道。现在启动时显式写一次，并**只告警、不致命**：

```c
if (get_phy_chan(TX, 0, &tx_phy) && iio_channel_find_attr(tx_phy, "hardwaregain")) {
    int e = iio_channel_attr_write_double(tx_phy, "hardwaregain", -param_attn_db);
    if (e < 0)
        fprintf(stderr, "[WARN] 写 TX hardwaregain 失败 (%d), 发射衰减仍是硬件默认值\n", e);
    else
        printf("[RF INFO] AD9361 发射衰减已设为 %.2f dB (0 = 最大输出)\n", param_attn_db);
}
```

之所以是"告警"而不是"报错退出"：部分 Pluto 固件对 `voltage0 (output)` 的 `hardwaregain` 写操作会返回 `EINVAL`，这时候程序继续跑，并在屏幕上明确写出"衰减仍是硬件默认值"——**宁可显式地不确定，也不要假装设置成功了**。

#### 5）关键设计 3：广播周期交给硬件循环缓冲区

这是本集的题眼：**要让广播间隔极其精准，就不能让软件决定"下一个包什么时候发"。**

BLE 广播天然是"周期性的短突发"：发一个 1.3ms 的包，静默一段，再发下一个，如此往复。这个周期（advInterval）在规范里是 **0.625ms 的整数倍**（20ms~10.24s）——它从头到尾就是一个离散的、可以被精确表达的量，没有理由做不准。

但最早那版代码是用软件"睡"出来的：发一次（阻塞式 `iio_buffer_push()`，等 DMA 把这一段搬完），然后 `usleep(间隔 − 包长)`。看着没毛病，实测却站不住。把间隔指令改成 10 / 20 / 100ms，用 Ellisys 分析仪在空口上量实际周期：

| 指令间隔 | 空口实测 | 偏差 |
|---|---|---|
| 10 ms | 35 ms | ~3.5 倍 |
| 20 ms | 75 ms | ~3.75 倍 |
| 100 ms | 395 ms | ~3.95 倍 |

三个点严格落在同一条直线上（**实测 ≈ 4 × 指令 − 5ms**）——这不是随机抖动，而是链路上存在一个比例性的偏差；换成从宿主机推流跑同一份代码，实测又变成 201.2ms，几乎就是指令值。**同一个程序，换个运行方式，周期就变了**：`iio_buffer_push()` 的返回时刻取决于 DMA 什么时候搬完，`usleep()` 的返回时刻取决于内核调度和定时器实现，两者都不在你手里。

真正可控的量是**样点**。在 4MHz 采样率下，一个样点是 250ns，而规范要求的最小粒度 0.625ms 正好等于 **2500 个样点**——也就是说任何合规的广播间隔在 4MHz 下都是整数个样点。既然如此，就别去"睡"，而是让 DMA 按采样时钟去循环播放一个长度恰好等于周期的缓冲区：

```c
/* ---- 4. 计算目标广播周期的总样点数并创建硬件循环缓冲区 ---- */
long target_samples = (long)(param_samp_mhz * 1000000.0 * ((double)param_interval_ms / 1000.0));
if (target_samples < generated_samples) {
    target_samples = generated_samples; // 确保总样点数大于实际包长度
    fprintf(stderr, "[WARN] 设定的广播间隔太短，已自动调整为包的最小物理时长\n");
}

// 创建硬件循环缓冲区 (最后一个参数设为 true，代表硬件自动循环播放)
txbuf = iio_device_create_buffer(tx, target_samples, true);

/* 缓冲区填充：前半部分填入实际的突发包，剩余空间全部填零（静默等待） */
ptrdiff_t p_inc = iio_buffer_step(txbuf);
char     *p_end = iio_buffer_end(txbuf);
int       iq_ptr = 0;
for (char *p_dat = (char *)iio_buffer_first(txbuf, tx0_i); p_dat < p_end; p_dat += p_inc) {
    if (iq_ptr < generated_samples) {
        ((int16_t*)p_dat)[0] = mod_iq_buf[2 * iq_ptr];     // I
        ((int16_t*)p_dat)[1] = mod_iq_buf[2 * iq_ptr + 1]; // Q
    } else {
        ((int16_t*)p_dat)[0] = 0; // 填零作为静默期
        ((int16_t*)p_dat)[1] = 0;
    }
    iq_ptr++;
}

printf("[TX] 硬件循环周期样点数 = %ld (对应设定间隔 %d ms)\n", target_samples, param_interval_ms);

/* 硬件循环模式下，只需将缓冲区推送一次，硬件即会按周期自主无限循环发送 */
iio_buffer_push(txbuf);

/* 保持程序存活，避免进程退出导致发射停止 */
while (!stop) sleep(1);
```

缓冲区的内容就是一个完整的广播周期：**开头 5296 个样点是包**（1296 样点的调制输出 + 4000 样点的 1ms 留白，留给收端 AGC 回落的机会），**剩下 794704 个样点全是 0**（周期里其余的静默），合起来 800000 个样点。DMA 读到末尾就绕回开头，一秒绕 5 次，从此与 CPU、调度、`usleep` 的实现都无关。

于是广播周期的精度变成了"数样点"的精度：分辨率就是 1 个样点 = **250ns**，准确度只取决于板上采样时钟本身（ppm 级）。想换周期只要换 `target_samples`，而且一眼可验算——`-i 100` → 400000 样点，`-i 200` → 800000 样点。

代价也要说清楚。间隔在进程启动时就被缓冲区长度定死了，改间隔要重启（换来的是运行期的绝对稳定）；缓冲区大小 = 采样率 × 间隔（200ms @4MHz 是 800000 个复数样点 ≈ 3.2MB），间隔越长占的 DMA 内存越多；发射靠"进程活着 + DMA 循环"两个条件同时成立，进程退出或被杀就停发——所以上面那句 `while (!stop) sleep(1);` 不是占位，它是这套方案的组成部分。

从原理上说，改完之后周期的误差源就只剩板上采样时钟的 ppm 级偏差了——那才是这个问题本该有的、也是唯一剩下的误差源。

#### 6）踩过的坑 1：把 `-s` 从 4 改成 10

"Pluto 采样率能到 61.44MHz，那把采样率提高一点，波形是不是更精细、更好收？"——早期版本里我自己就是这么想的，结果是**发出去的信号彻底不再是蓝牙**。原因很简单：`SAMPLE_PER_SYMBOL` 是编译期常量 4，`-s 10` 只是把同一串样点播快 2.5 倍。

| | `-s 4`（默认） | 只改 `-s 10` |
|---|---|---|
| 符号率 | 4 Msps ÷ 4 = **1.00 Msym/s** ✅ | 10 Msps ÷ 4 = **2.5 Msym/s** ❌ |
| 每符号频偏 | ±250 kHz ✅ | ±625 kHz（随播放速率等比放大） |
| 占用带宽 | ≈1 MHz ✅ | ≈2.8 MHz ❌ |
| 标准接收机 | 正常解码 | **一个包都收不到**（不是"断续"，是完全没有） |

两个补充事实：一是"往低设"这条路也不存在——板子自己报的可用范围是 `sampling_frequency_available: [2083333 1 61440000]`，下限 2.083MHz，固件根本不接受更低的值，4MHz 还有 2 倍余量；二是 Pluto 上 **RX/TX 采样率是联动的**，只写 TX 通道的 `sampling_frequency`，读回来接收通道也是同一个值，所以别在同一块板上一边发射、一边用 10MHz 跑上一集的接收程序（那会把发射的采样率一起带成 10MHz）。真要同时收发，用两块板，或者让接收也降到 4MHz。

一句话收尾：**符号率 = 采样率 ÷ 过采样倍数，它才是硬约束**，改采样率本质上就是改符号率。

#### 7）踩过的坑 2：手机上看到的"断续"，其实是手机

现象：nRF Connect 里能看见 `hello btfz's gnu-radio`，但**7.5 秒有、5 秒没有，周而复始**；把广播间隔改成 20 / 100 / 200ms，现象一模一样。

"断续"最容易让人怀疑发射端丢包，但在下结论之前，我换了三个观察者去看同一个空口：手机（断续）、上一集的 Pluto 接收机（20 秒能量包络每秒活跃 3.2%~4.6% 一条平线；15 秒逐包解调、按 MAC 挑出我们的 73 个包，**>500ms 空洞 0 个**）、以及后来用 Ellisys 蓝牙协议分析仪复核（连续）。**两个独立接收机都证明包在按周期发**，那这个节奏就只能产生在"空口之后"——手机/App 的扫描窗口、系统扫描节流、与 WiFi 分时等等。

顺带还牵出一个真实的规范缺口：BLE 广播事件应该在 37/38/39 三个信道上轮换发送（[Vol 6, Part B 4.4.2][#2]），而本集为了把"信道 = 频率 = 种子"这条链路讲透，**只发单信道**。手机的扫描器是三个信道跳着听的，一旦它的扫描占空比不高（息屏、省电、被别的 BLE 连接或 WiFi 分时），单信道广播就只在轮到该信道的窗口里被听到——表现正是"断续"，而且**和广播间隔无关**，与实测完全吻合。

还有一句规范细节值得记下：advInterval 的范围是 20ms~10.24s，其中**可连接广播（ADV_IND）下限 20ms，而本集的不可连接广播（ADV_NONCONN_IND）下限是 100ms**。所以自测时 `-i 20` 只是"发得比规范更快"，能被本项目自己的接收机解出来，要让标准设备按规范认，请保持在 **≥100ms**。

这条经验我当成本集最值钱的一条留下来：**排查"收不到 / 断续"时，先把手机（以及一切会做扫描策略、限流、UI 平滑的显示端）从证据链里拿掉。**

#### 8）跑起来之后看什么

除了上面那三行组包日志，运行时还有这些信息可以判断发射端状态：

| 输出 | 含义 | 不正常时怎么办 |
|---|---|---|
| `[RF INFO] AD9361 发射衰减已设为 xx dB` | `hardwaregain` 写成功 | 变成 `[WARN] 写 TX hardwaregain 失败`：该固件不支持，衰减仍是硬件默认值（实测默认 −30dB） |
| `[SUCCESS] ... 5296` | 一个包的 IQ 样点数 | 和"包 320bit × 4 + 16 拖尾 + 4000 留白"对不上就是调制器/参数异常 |
| `[TX] 硬件循环周期样点数 = N` | 硬件循环缓冲区总长 = 采样率(MHz) × 间隔(ms) × 1000 | 对不上就是参数有问题；出现 `[WARN] 设定的广播间隔太短` → 间隔比包长还短，已自动夹到包的最小物理时长 |
| `[WARN] 广播间隔 xx ms 低于 BLE 规范最短 20 ms` | `-i` 太小 | 本工程是不可连接广播，建议 ≥100ms |
| `Error pushing TX buf` | IIO 推流失败 | 查设备是否掉线（`ping pluto.local` / `lsusb`），或间隔太长导致缓冲区超出可用内存 |

真出问题时，我一般按这个顺序走：先看程序启动时打出的三行组包日志，确认"发出去的东西是对的"——字节序（AdvA 小端、AA 的 LSB 先发）、CRC 方向、白化种子，任何一个错都会表现成"手机完全看不到"；再用独立观察者看空口（同一块板的接收链路或第二块板），不要用手机当第一裁判；最后，改任何射频参数前后都把 `sampling_frequency` / `hardwaregain` 读回来记档，否则"昨天能收到今天收不到"永远查不清。

本集每一条坑的完整清单、实测基线和验证脚本，都整理在工程目录的 `skills/pluto-ble-adv-tx/` 里，想直接抄结论可以看那里。

</br>

### 5. 结语

伴随着 nRF Connect 里跳出的 `hello btfz's gnu-radio`，以及上一集接收机终端里滚出来的 `[ADV] type=0x2 AdvA=DC:23:03:04:05:06 ... CRC OK`，我们的**板载边缘端蓝牙广播发射机**实战圆满成功！15 秒实测 73 个包、CRC 全过、周围另外 8 个广播源一起在跑的情况下也没丢包。

回看这套架构：从"人话"（设备名、MAC）到"空口"（前导码、AA、GFSK 波形）的每一层转换，都在 Pluto 的 ARM 上现算：**PDU 组装 → CRC-24 → 白化 → LSB 位展开 → 高斯成形 → 相位累加查表 → int16 IQ → 硬件循环缓冲区**，只有 617 行纯 C（另 54 行 makefile），不依赖任何蓝牙协议栈、不依赖任何 DSP 库。

和前面几集的纯"收"实验比，这一集最大的不同是它带大家了解如何"发"数据。从第一行 `printf("hello pluto sdr")`，到 FM 收音机、频谱分析仪、ADS-B 飞机解码，再到**蓝牙广播的接收与发射**——到这一集，你已经能自己用一块 SDR **造出一条完整的空口链路**：会听，也会喊。接下来我们强度会更进一步，纯 C 实现基于 QPSK 的文件收发，让你从简单的 GFSK 调制过度到现代的 QPSK 调制技术实现，敬请期待！

</br>

### 参考链接

[[1]. 第四集：断开脐带 —— 交叉编译与 PlutoSDR 嵌入式脱机运行实战][#1]    
[[2]. Bluetooth SIG —— Core Specification（BLE 空口：access code / 白化 / CRC-24 / advInterval 的权威定义，见 Vol 6 Part B 4.4.2）][#2]    
[[3]. Wikipedia —— Bluetooth Low Energy][#3]    
[[4]. Analog Devices —— libiio `ad9361-iiostream.c`（本集主程序的底层框架来源）][#4]    

[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/110        
[#2]:https://www.bluetooth.com/specifications/specs/        
[#3]:https://en.wikipedia.org/wiki/Bluetooth_Low_Energy        
[#4]:https://github.com/analogdevicesinc/libiio/blob/v0.25/examples/ad9361-iiostream.c        
[#5]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/80     
[#6]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/77          
[#7]:https://space.bilibili.com/397747073/lists/4964593?type=season     
[#8]:https://beautifulzzzz.com/gnuradio/tutorial/topic/1     

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ble_tx_arch.jpeg       
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/pluto_ble_tx_command_show_small.gif    
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ble_tx_code_flowgraph.png     
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ble_tx_gfsk.jpeg     
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/gnuradio_sdr_ble_stack.png    
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/bilibili_sdr_ble_stack.png    

