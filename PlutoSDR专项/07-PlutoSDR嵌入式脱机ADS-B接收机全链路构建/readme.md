### 1. 开场

上一集我们在 PlutoSDR 的板载 ARM 芯片上跑通了频谱分析仪，见识到 10MHz 采样的 FFT 变换能力。这次，我们的目标再度升级——**我们要用 Pluto 的板载 ARM 直接解码天上飞机的 ADS-B 信号！**

ADS-B（Automatic Dependent Surveillance–Broadcast）是民航飞机定期广播的航班信息报文，工作在 1090 MHz 频率。每一架飞机大约每秒发送 6~10 条 112 位的短报文，包含了 ICAO 地址、航班号、高度、航向速度等关键信息。如果能用我们的 tiny Pluto 来实时捕获并解码这些报文，那意味着——**你已经用一块巴掌大的 SDR 板卡，搭建了一套货真价实的微型航空交通监视系统！**

本集我们死磕的 DSP 链路：**Pluto 板载 ARM 捕获 1090MHz 射频信号 $\\rightarrow$ IQ 幅度包络检测 $\\rightarrow$ per-buffer 最小噪声追踪（跨 buffer EMA）$\\rightarrow$ 双阶段前导码匹配 $\\rightarrow$ PPM 位同步解码 $\\rightarrow$ CRC-24 校验（poly=0xFFF409）$\\rightarrow$ 字段解析（航班号/高度/速度/航向）$\\rightarrow$ UDP 输出**

所有核心计算在 Pluto 芯片上就地解决，PC 只负责用 Python 图形化展示解码结果！同时本集还解决了一个**关键工程问题：ADS-B 长时运行死机的根因与修复**——为后续所有项目提供了重要的性能参考。

![][p3]

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

a.首先注意我们的目录结构：

```
➜  plutosdr_iio_gcc_demo tree -L 2
.
├── 01-hello_plutosdr
├── 02-ad9361-iiostream
├── 03-pluto_fm_radio   
├── 04-pluto_spectrum_analyzer
├── 05-pluto_adsb_receiver       <-- 这里
│   ├── makefile
│   ├── pluto_adsb_receiver.c
│   └── pluto_adsb_viewer.py
└── toolchain           <-- 前面课程已经安装好的工具链
    ├── gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
    └── libiio
```

b.编译与运行：

注意：首先需要用 `ifconfig` 查看当前电脑的 ip，替换 `makefile` 中的 `IP     ?= 192.168.1.119`     

```
# 清空编译过程和目标文件
➜  05-pluto_adsb_receiver make clean
rm -rf pluto_adsb_receiver

# 编译（调用交叉编译工具链）
➜  05-pluto_adsb_receiver make build
/home/gnuradio/PLUTOSDR/iio/05-pluto_adsb_receiver/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc pluto_adsb_receiver.c -o pluto_adsb_receiver -I/home/gnuradio/PLUTOSDR/iio/05-pluto_adsb_receiver/../toolchain/libiio/usr/include -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -L/home/gnuradio/PLUTOSDR/iio/05-pluto_adsb_receiver/../toolchain/libiio/usr/lib/arm-linux-gnueabihf -L/home/gnuradio/PLUTOSDR/iio/05-pluto_adsb_receiver/../toolchain/libiio/lib/arm-linux-gnueabihf -liio -lm -Wl,--allow-shlib-undefined
/home/gnuradio/PLUTOSDR/iio/05-pluto_adsb_receiver/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-strip pluto_adsb_receiver

# scp 将生成的产物复制到 pluto sdr linux 的 home 目录下
# ssh 登陆到 pluto sdr linux 环境，赋予 pluto_adsb_receiver 可执行权限，并执行
➜  05-pluto_adsb_receiver make run
scp -O pluto_adsb_receiver root@pluto.local:~/
root@pluto.local's password: 
pluto_adsb_receiver                                                                   100%   14KB   3.9MB/s   00:00    
------------------------------------------------
   ____  _   _ ____  _____ ____     _           _   
  |  _ \| | | |  _ \| ____|  _ \   / \   _ __ | |_ 
  | |_) | | | | | | |  _| | |_) | / _ \ | '_ \| __|
  |  __/| |_| | |_| | |___|  _ < / ___ \| | | | |_ 
  |_|    \___/|____/|_____|_| \_/_/   \_\_| |_|\__|
                                 by beautifulzzzz
Tips: On your PC, run Python to receive decoded
ADS-B messages via UDP port 1234:
python pluto_adsb_viewer.py --ip 192.168.1.119 --port 1234
------------------------------------------------
ssh -t root@pluto.local "chmod +x ~/pluto_adsb_receiver; ./pluto_adsb_receiver -f 1090 -s 4 -i 192.168.1.119 -p 1234"
root@pluto.local's password: 
* Acquiring IIO context
* Acquiring AD9361 streaming devices
* Configuring AD9361 for streaming
* Acquiring AD9361 phy channel 0
* Acquiring AD9361 RX lo channel
* Configuring RX gain
* Initializing AD9361 IIO streaming channels
* Enabling IIO streaming channels
* Creating non-cyclic IIO buffers with 128 MiS

>>> Edge-Computing ADS-B Receiver Started <<<
Target Dev  : default
Target PC IP: 192.168.1.119:1234
Center Freq : 1090.00 MHz
Sample Rate : 4.00 MHz (Hardware BW: 3.20 MHz)
[DIAG] buf#20 | NF=36 | Avg=144640 | Peak=15295744 | PreCand=205 | Msg=4041 | Valid=0
>>> DF=17 ICAO=78174F TC=31 | 8D78174FF8210002004AB882664B
>>> DF=17 ICAO=7813AC CALL=CSN6624 | 8D7813AC230D33B6DB2D201DE2B8
>>> DF=17 ICAO=7813AC ALT=8850ft | 8D7813AC583123EEE09AB4D47966
>>> DF=17 ICAO=780EBE HDG=77° SPD=0? VR=-32640fpm | 8D780EBE9908DC2F98083DCE4250
>>> DF=17 ICAO=780DAC ALT=27600ft | 8D780DAC588F800667F485A1DBF5
>>> DF=17 ICAO=78174F HDG=84° SPD=0? VR=-32064fpm | 8D78174F9910F00D782C04B432DE
```

接着启动 PC 端查看器（`192.168.1.119` 要换成你电脑的 ip）：

```
python pluto_adsb_viewer.py
```

然后你就能在电脑上实时看到带字段解析的 ADS-B 报文了，航班号、高度、航向/速度一目了然：

```
>>> DF=17 ICAO=780C01 CALL=CES5339          ← 航班号 CES5339
>>> DF=17 ICAO=7805D6 ALT=25600ft            ← 高度 25600 英尺
>>> DF=17 ICAO=780C01 ALT=30125ft            ← 高度 30125 英尺
>>> DF=17 ICAO=780C01 HDG=3° SPD=0? VR=64fpm  ← 航向 3°, 垂直速率
>>> DF=17 ICAO=7813AC ALT=8850ft            
>>> DF=17 ICAO=780DAC HDG=123° SPD=0? VR=-32704fpm
```

![][p2] 

</br>

### 4. 代码介绍

这个代码是在《[第四集：交叉编译与 PlutoSDR 嵌入式脱机运行实战][#1]》libiio 收发框架基础上改装的。前半部分与之前的频谱分析仪和 FM 接收机完全一致（IIO 初始化、UDP 初始化、getopt 参数解析），后半部分则完全替换为 ADS-B 1090ES 专用解码逻辑：

#### 1）动态参数传入（与之前保持完全一致）

```
#include <getopt.h>  /* ---- 【核心】Linux 标准命令行参数解析库 ---- */

/* 打印帮助菜单 */
void print_help(char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d <device>  plutosdr 设备 url (默认: default)\n");
    printf("  -f <freq>    中心频率 Center Frequency in MHz (默认: 1090.0)\n");
    printf("  -s <rate>    采样率 Sampling Rate in MHz (默认: 4.0)\n");
    printf("  -i <ip>      目标 PC IP 地址 (默认: 192.168.1.119)\n");
    printf("  -p <port>    目标 PC PORT 地址 (默认: 1234)\n");
    printf("  -h           显示当前帮助菜单\n");
}
```

ADS-B 默认参数：频率 1090 MHz，采样率 **4 MSPS**（⚠️ 不是 10 MSPS！详见下文分析）。

#### 2）while 循环读取前的微调（手动增益 + 手动增益选择）

ADS-B 是脉冲 AM 信号，AGC 自动增益会不断推高增益导致噪声基底饱和。我们改为手动增益 60 dB 以充分利用 ADC 动态范围：

```
/* ---- 2.5 设置手动增益（防止 ADC 削波，保留脉冲动态范围） ---- */
struct iio_device *phy_dev = get_ad9361_phy();
struct iio_channel *rx_gain = iio_device_find_channel(phy_dev, "voltage0", false);
if (rx_gain) {
    iio_channel_attr_write(rx_gain, "gain_control_mode", "manual");
    iio_channel_attr_write_longlong(rx_gain, "hardwaregain", 60);  // 60 dB！
}
```

**为什么是 60 dB 而不是 15 dB？** ADS-B（1090 MHz）信号是高空脉冲，路径衰减极大。15 dB 只有弱信号（Avg≈6700, Peak≈150K），无法可靠 PPM 解码。实机测试表明 55-65 dB 最佳，此时 Avg~30K-150K，Peak~1M-24M，前导码和 PPM 解码都能正常判决。

另外 **Buffer 大小从 256K 降为 128K** — Cortex-A9 的 L2 cache 只有 512KB，256K 的 magnitude 数组（1MB）频繁触发 cache miss，长时间运行会导致死机。128K 是内存带宽和捕获延迟的最佳平衡点。

#### 3）前导码检测：双阶段脉冲匹配（阈值相对化）

ADS-B 报文以 8 µs 的前导码开头，包含 4 个 0.5 µs 宽度的脉冲，位于 0µs、1.0µs、3.5µs、4.5µs 处：

```
Preamble @ 4 MSPS (HALF_BIT_SAMPS=2):
  ┌──┐       ┌──┐                    ┌──┐  ┌──┐
  │  │       │  │                    │  │  │  │
  └──┘       └──┘                    └──┘  └──┘
  0  0.5     1  1.5                      3.5 4  4.5  (µs)
```

第一阶段：快速预检——检查第一个脉冲位置（0-0.5µs，2 个采样点）的平均幅度。**阈值不再是写死的绝对值，而是基于当前噪声基底动态计算**：

```
float first_pulse_check = 0;
for (int s = 0; s < HALF_BIT_SAMPS; s++)
    first_pulse_check += magnitude[i + s];
first_pulse_check /= HALF_BIT_SAMPS;

if (first_pulse_check < pulse_thresh)
    continue;
```

其中 `pulse_thresh = noise_floor × 1.5`，并设有安全钳：未收敛时 `min_thresh = avg_mag × 0.3`，避免首个 buffer 误触发。

第二阶段：完整匹配——检查 4 个脉冲和间隙的能量比大于 3.0，且脉冲能量超过噪声基底 3 倍。

```
static int detect_preamble(const float *mag, int idx, int buf_len) {
    // 计算4个脉冲位置的平均能量
    float avg_pulse = ...;
    // 计算间隙位置的平均能量  
    float avg_gap = ...;
    float ratio = avg_pulse / avg_gap;
    return (ratio > 3.0f && avg_pulse > 3.0f * g_noise_floor) ? 1 : 0;
}
```

搜索步进为 HALF_BIT_SAMPS=2（4 MSPS 下每 0.5 µs 一次），搜索效率相比逐点提升了 2 倍。

#### 4）PPM 位同步解码（阈值相对化）

ADS-B 的每个数据位持续 1 µs（4 个采样点），采用脉冲位置调制（数据总共 112 位，包含 CRC）：

$$ \\text{Bit} = \\begin{cases} 1, & E_{\\text{前2}} > E_{\\text{后2}} \\\\ 0, & E_{\\text{前2}} < E_{\\text{后2}} \\end{cases} $$

```
Bit 1: ┌──────┐          Bit 0:          ┌──────┐
       │pulse │                          │pulse │
       └──────┘                          └──────┘
       0  0.5 1 (µs)                     0  0.5 1 (µs)
```

**判决阈值从写死的 `30.0f` 改为相对于噪声基底**：`threshold = g_noise_floor × 0.3`。这确保了在不同增益/信号强度下都能可靠解码，增益从 15 dB 到 60 dB 无需改代码。

```
static int decode_ppm_bit(const float *mag, int idx) {
    float first_half  = 0;
    float second_half = 0;
    for (int s = 0; s < HALF_BIT_SAMPS; s++) {
        first_half  += mag[idx + s];
        second_half += mag[idx + HALF_BIT_SAMPS + s];
    }
    float diff = first_half - second_half;
    float threshold = g_noise_floor * 0.3f;
    if (diff > threshold)  return 1;   // 前半段有脉冲 → Bit 1
    if (diff < -threshold) return 0;   // 后半段有脉冲 → Bit 0
    return -1;  // 无法判决
}
```

#### 5）CRC-24 校验（⚠️ 关键）

ADS-B 报文的最后 24 位是 CRC 校验和，使用 **gr-air-modes 源码确认的多项式 `0xFFF409`**（不是常见文档误传的 `0xFFFA51`！）：

正确做法是**字节查表法**：对前 11 字节计算 CRC，再与最后 3 字节 CRC 字段 XOR，结果为 0 表示报文有效：

```
static uint32_t crc24_modes(const uint8_t *msg, int len_bytes) {
    if (crc_table[1] != 0xFFF409) generate_crc_table();
    uint32_t crc = 0;
    for (int i = 0; i < len_bytes - 3; i++) {
        crc = (crc_table[((crc >> 16) ^ msg[i]) & 0xFF] ^ (crc << 8)) & 0xFFFFFF;
    }
    uint32_t ap = (uint32_t)msg[len_bytes-3] << 16
                | (uint32_t)msg[len_bytes-2] << 8
                | (uint32_t)msg[len_bytes-1];
    return (crc ^ ap) & 0xFFFFFF;
}
```

❌ 常见错误：
- 使用 `0xFFFA51` 多项式 → 通通 CRC 失败，Valid=0
- 逐位计算 112 位后补 24 个零 → 那是生成 CRC 的方法，不是验证方法
- 字节查表后来不及验证就换用逐位法 → 验证方法错了

验证方式：对 112 位报文的**前 11 字节**计算 CRC，与**最后 3 字节** XOR → 结果为 0 表示报文有效。

#### 6）自适应噪声基底（❗长期运行死机的根因修复）

ADS-B 信号的信噪比随飞机距离和天线质量变化很大。我们使用**per-buffer 最小值 + 跨 buffer EMA** 追踪噪声基底：

```c
// 遍历：计算幅度 + 统计 + per-buffer 最小值
float buf_min = 1e30f;
for (each sample) {
    float mag_sq = i_val * i_val + q_val * q_val;
    magnitude[sample_count] = mag_sq;
    if (mag_sq > peak_mag) peak_mag = mag_sq;
    sum_mag += mag_sq;
    if (mag_sq < buf_min) buf_min = mag_sq;  // 脉冲不会成为 min
    sample_count++;
}
// 跨 buffer NF 追踪：per-buffer 最小值做 EMA
noise_floor += 0.05f * (buf_min - noise_floor);
if (noise_floor < 10.0f) noise_floor = 10.0f;
```

**为什么不用 per-sample EMA？** 在 60 dB 增益下，脉冲能量（mag_sq≈10^9+）比噪声（≈10^3）大 6 个数量级。per-sample EMA 的更新公式 `NF += 0.01 × (mag_sq - NF)` 会被脉冲瞬间拉到数千万，precheck 永远无法通过，程序空转耗尽资源 — 这就是 Pluto 运行 10-30 秒后死机的根本原因！

**为什么 per-buffer min 有效？** 脉冲能量 ≈10^7+，噪声 ≈10^3。最小值永远是对应噪声的采样点，完美隔离脉冲。再通过跨 buffer EMA（α=0.05）平滑，NF 稳定在 10-36，永不崩溃。

#### 7）字段解析：航班号/高度/速度/航向

CRC 通过后，还会根据 Type Code 解码报文中的字段：

**Type Code 1-4 → 航班号：** 8 个 6-bit 字符编码（1-26=A-Z，48-57=0-9），从 bits 41-88 提取：

```
static int decode_callsign(const uint8_t *msg, char *cs, int cs_len)
>>> DF=17 ICAO=780C01 CALL=CES5339
```

**Type Code 9-18 → 气压高度：** 12-bit 编码（bits 41-52），Q-bit 为 1 时以 25 英尺精度解码：

```
static int decode_altitude(const uint8_t *msg) 
>>> DF=17 ICAO=7805D6 ALT=25600ft
```

**Type Code 19 → 速度/航向/垂直速度：** sub_type 1-3 对应地速/指示空速/真空速，10-bit 航向（0-360°），10-bit 垂直速率（64 ft/min 精度，符号扩展）：

```
static void decode_velocity(const uint8_t *msg, char *out, int out_len) 
>>> DF=17 ICAO=780C01 HDG=3° VR=64fpm
```

#### 8）输出与诊断

每 20 个 buffer 打印一次诊断摘要：

```
[DIAG] buf#20 | NF=36 | Avg=144640 | Peak=15295744 | PreCand=205 | Msg=4041 | Valid=0
```

| 字段 | 含义 | 正常范围 |
|------|------|---------|
| NF | 噪声基底（per-buffer min EMA） | 10-200 |
| Avg | 当前 buffer 平均幅度 | 10K-200K |
| Peak | 当前 buffer 峰值幅度 | 1M-25M |
| PreCand | 前导码候选数 | 150-250 |
| Msg | 累计检测报文数 | - |
| Valid | 累计有效报文数（CRC 通过） | 稳定增长 |

诊断标志：**NF 持续下降或突然飚高 + PreCand=0** = NF 算法有问题；**PreCand>300 且 Valid=0** = 阈值过松或 CRC 多项式错误。

</br>

### 5. 结语

伴随着 Pluto 串口中源源不断飘出的 **`>>> DF=17 ICAO=780C01 CALL=CES5339 ALT=30125ft`** 报文，我们的**板载边缘端微型 ADS-B 航空监视系统**实战圆满成功！

回看这套架构，Pluto 在板载 Linux 中顶住了 **4MSps 的射频吞吐**（10 MSps 反而效果更差——高频噪声淹没了脉冲信号），在片上完成了**幅度包络检测 $\\rightarrow$ per-buffer 最小噪声追踪 $\\rightarrow$ 双阶段前导码匹配 $\\rightarrow$ PPM 位同步 $\\rightarrow$ CRC-24 校验（0xFFF409）$\\rightarrow$ 字段解析**的全套 DSP 链路。

更重要的收获是找到了 **pluto 长时间运行死机的真正原因**——被脉冲污染的噪声基底使 precheck 跳过所有位置，程序空转耗尽资源。以下工程经验对所有 Pluto 项目通用：

| 优化 | 效果 |
|------|------|
| NF 用 per-buffer min + 跨 buffer EMA | 永不漂移，稳定追踪 |
| 阈值相对于 NF（不要写死） | 增益从 15→60 dB 无需改代码 |
| Buffer 128K（不要用 256K）| 减少 cache miss，避免死机 |
| 搜索步进 2（不要逐点） | CPU 减半，不漏检 |
| 采样率 4 MSPS（不要 10 MSPS） | 信号质量反而更好 |

从最初的第一行 `printf("hello pluto sdr")`，到 FM 收音机，到频谱分析仪，到 ADS-B 飞机解码——你已经完成了从嵌入式 SDR 入门到航空无线电监视的跃迁！并且学会了宝贵的**嵌入式实时系统性能调优**经验。恭喜道友，我们下一期更精彩！

</br>

### 参考链接


[[1]. ADS-B Exchange 全球航班追踪，精准可靠][#2]            
[[2]. WiKi —— 自动相关监视广播（ADS-B ）][#3]    
[[3]. ORG —— 美国交通部][#4]     
[[4]. Web —— 自动相关监视 - 广播 (ADS-B)（有个视频比较好）][#5]      
[[5]. Web —— ADS-B 输出和 ADS-B 输入详解][#6]     

[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/110        
[#2]:https://www.adsbexchange.com/     
[#3]:https://en.wikipedia.org/wiki/Automatic_Dependent_Surveillance%E2%80%93Broadcast         
[#4]:https://www.faa.gov/about/office_org/headquarters_offices/avs/offices/afx/afs/afs400/afs410/ads-b        
[#5]:https://www.flightradar24.com/how-it-works/ads-b     
[#6]:https://spire.com/wiki/explaining-the-difference-between-ads-b-out-and-ads-b-in/      


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/pluto_adsb_terminal_show.png     
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202606/ads-b-hero-260402.gif      
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202606/ads_b_command_show.gif
