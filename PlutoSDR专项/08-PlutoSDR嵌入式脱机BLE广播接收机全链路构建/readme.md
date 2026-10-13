### 1. 开场

上一集我们用 Pluto 的板载 ARM 直接解码了天上飞机的 ADS-B 报文，把一块巴掌大的 SDR 变成了一套微型航空监视站。这一次，我们把天线转向**离你最近的那个 2.4GHz 世界 —— 蓝牙低功耗（BLE）广播包**。

每一秒，你身边的手机、耳机、手环、智能家居设备都在向外"喊号码"：BLE 设备通过 3 个广播信道（**37 = 2402MHz、38 = 2426MHz、39 = 2480MHz**）周期性发出广播包，包里带着自己的 MAC 地址（AdvA）、设备名、厂商自定义数据。**抓下来、解出来，你就等于拥有了一台"蓝牙嗅探器"**。

BLE 广播包虽然短（最短 10 字节 ≈ 80µs，常见的带名字包 40 多字节 ≈ 400µs），但对接收端的要求一点都不低：

- LE 1M PHY：**GFSK 调制、符号率 1 Msym/s**（1µs 一个符号），标准频偏 ±250kHz；
- 每个包以 **40 bit 的 access code** 开头（8 bit 前导码 `0xAA` + 32 bit 地址 `0x8E89BED6`），接收端必须把它匹配上，后面的 PDU 才有意义；
- PDU 与 CRC 全程**白化**（whitening，种子就是信道号），末尾 3 字节是 **CRC-24 校验**（初值 `0x555555`，生成多项式 `0x00065B`）；
- 广播包是**突发**：没有连接、没有握手，错过了就过去了，重来要等下一个广播周期。

本集我们死磕的 DSP 链路：**Pluto 板载 ARM 捕获 2.4GHz 射频信号 $\rightarrow$ 正交鉴频（quadrature demod）$\rightarrow$ 1 符号匹配滤波（Integrate & Dump）$\rightarrow$ 硬判决取比特 $\rightarrow$ 10 个定时相位并行 40bit 滑窗 $\rightarrow$ access code 汉明距离匹配 $\rightarrow$ 每个命中相位各自独立收包 $\rightarrow$ 解白化 $\rightarrow$ CRC-24 校验 $\rightarrow$ AdvA / PDU 解析**

所有计算都在 Pluto 芯片上就地完成，PC 一根线都不用接（想看结果，SSH 终端里的报文就是）。同时本集踩平了 4 个"能让你怀疑人生"的坑：从 **"access code 永远到不了 40/40"**、到 **"AA 一个 bit 都没错，PDU 还是错 1 个 bit"**、再到 **"好心加的一级 DC 跟踪，反而让收包少了一半"**。这些坑对所有 GFSK/FSK 小包接收项目（蓝牙、ZigBee、自定义 2.4G 私有协议）都通用。

![][p7]

</br>

---
**补充：**
之前我已经出过详细的[蓝牙专项][#8] ([B站系列视频][#7])，详细解析了 BLE 广播包的[接收][#6]与[发送][#5]：

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
├── 06-pluto_ble_adv_rx           <-- 本集
└── toolchain                     <-- 前面课程已经装好的交叉编译工具链

➜  06-pluto_ble_adv_rx tree
.
├── pluto_ble_adv_rx.c   # 主程序：IIO 采集 + AD9361 配置 + 分块喂给解调器
├── ble_phy.c            # 【核心】纯 C 解调器：IQ -> 比特 -> AA -> PDU -> CRC
├── ble_phy.h            # 对外接口 + 收发共用函数（CRC-24 / 白化）
├── ble_replay.c         # 离线回放：把录到的 IQ 喂给同一个 ble_phy.c
├── makefile             # build / run / capture / replay
├── doc/                 # 解调链设计、GNU Radio gfsk_demod 对照、证据与调参表
├── skills/              # 踩坑手册（本集所有坑的完整版都在这里）
└── tools/               # 独立 Python 参考实现（合成包 / AA 扫描 / 候选解码）
```

b.编译与运行：

和上一集不同，本集的 `makefile` **不需要改 IP** —— 设备地址统一用主机名 `pluto.local`（IIO over IP 和 SSH 都用它）。

```
# 编译（调用交叉编译工具链）
➜  06-pluto_ble_adv_rx make build
/home/gnuradio/PLUTOSDR/iio/06-pluto_ble_adv_rx/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc \
    pluto_ble_adv_rx.c ble_phy.c -o pluto_ble_adv_rx \
    -I.../toolchain/libiio/usr/include -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
    -L.../toolchain/libiio/usr/lib/arm-linux-gnueabihf -liio -lm -Wl,--allow-shlib-undefined
.../arm-linux-gnueabihf-strip pluto_ble_adv_rx

# scp 到 Pluto，ssh 上板运行（需要输入 Pluto 的密码）
➜  06-pluto_ble_adv_rx make run CH=37 SAMPLE=10
scp -O pluto_ble_adv_rx root@pluto.local:~/
root@pluto.local's password: 
pluto_ble_adv_rx                                                      100%   14KB   3.9MB/s   00:00    
------------------------------------------------
 ____  _          _____ _           _ 
| __ )| | ___    | ____| |_   _____| |
|  _ \| |/ _ \   |  _| | \ \ / / _ \ |
| |_) | |  __/   | |___| |\ V /  __/ |
|____/|_|\___|   |_____|_| \_/ \___|_|
                                         
------------------------------------------------
ssh -t root@pluto.local "chmod +x ~/pluto_ble_adv_rx; ./pluto_ble_adv_rx -c 37 -s 10"
root@pluto.local's password: 
* Acquiring IIO context
* Acquiring AD9361 streaming devices
* Configuring AD9361 for streaming
* Initializing AD9361 IIO streaming channels
* Enabling IIO streaming channels
[RF INFO] AD9361 增益模式已设为 fast_attack (AGC)

>>> Edge-Computing BLE ADV Receiver Started <<<
Target Dev  : default
Center Freq : 2402.00 MHz
Sample Rate : 10.00 MHz
BLE Channel : 37 (De-whitening Seed)

[BLE] 解调链: 正交鉴频 -> 10点匹配滤波(1符号积分) -> 全 10 个定时相位并行搜 AA + 每相位各一路收包
[BLE] AA 门限 37/40 bit, 白化种子 = 信道 37
[AA ] 定时相位 7, 误差 0/40
[AA ] 定时相位 8, 误差 0/40
[AA ] 定时相位 9, 误差 0/40

==================================================================
[PKT #61] BLE 广播包 (CH 37, 位置 27875164, AA 误差 0/40, PDU 37 bytes, CRC OK)
[PDU] 20 23 3D 35 A8 3A 5E 50 02 01 06 06 09 6D 69 64 65 61 12 FF A8 06 01 32 32 33 39 36 36 37 39 41 43 31 31 36 39 A1 5B A5 
[ADV] type=0x0  AdvA=50:5E:3A:A8:35:3D
==================================================================
```

上面这条报文是**真机跑出来的**（16 秒实测共 **61 个包**，`refill` 零错误，来自多个不同设备）：`20` 是 PDU 头（`0x0` = ADV_IND，长度 0x23 = 35 字节），`23 3D 35 A8 3A 5E 50` 是 `AdvA = 50:5E:3A:A8:35:3D`，后面 `09 6D 69 64 65 61` 是「设备名 = midea」（`6D 69 64 65 61` 正是 ASCII 的 `midea`），最后的 `A1 5B A5` 就是 CRC-24。

c.离线回放：**不接板子也能调算法**

调试解调器最痛苦的是"每次都要上板、肉眼盯终端"。本集专门做了一套离线闭环：把原始 IQ 录到文件，之后所有算法迭代都在 PC 上跑，跑通了再上板。

```
# 抓 2 秒原始 IQ 到文件（走网络 IIO，不需要 SSH 密码；顺手把增益切成手动 60dB）
➜  06-pluto_ble_adv_rx make capture CH=38 SEC=2 FILE=ch38.bin
  当前增益: 60.000000
iio_readdev -u ip:pluto.local -b 262144 -s 20000000 cf-ad9361-lpc voltage0 voltage1 > ch38.bin
-rw-r--r-- 1 btfz users 80000000  6月 10 12:00 ch38.bin
回放: make replay FILE=ch38.bin CH=38

# 用宿主机 gcc 编译【同一个 ble_phy.c】，离线回放
➜  06-pluto_ble_adv_rx make replay FILE=ch38.bin CH=38
gcc -O3 -Wall -o ble_replay ble_replay.c ble_phy.c -lm
./ble_replay ch38.bin 38
[harness] total samples: 20000000 (2.000 s)
...
```

因为回放和设备端共用同一个 `ble_phy.c`（同一份算法、只换数据来源），**离线结论可以直接代表设备端**，回放速度约为实时的 1.9 倍。

![][p8]

</br>

### 4. 代码介绍

#### 1）链路总览与文件分工

整个解调链集中在 `ble_phy.c`（纯 C，不依赖任何第三方 DSP 库），每进入一个样点做五件事，**每样点只做一次 O(1) 的移位运算**：

```c
/* 1. 正交鉴频：与 GNU Radio 的 quadrature_demod_cf 完全一致 */
float ci = ii * s_prev_i + qq * s_prev_q;   /* Re{x[n] conj(x[n-1])} */
float cq = qq * s_prev_i - ii * s_prev_q;   /* Im{x[n] conj(x[n-1])} */
float d  = atan2f(cq, ci);

/* 2. 1 符号匹配滤波（Integrate & Dump）：这一步 GNU Radio 没有，白捡约 10dB */
if (++s_w >= s_sps) s_w = 0;
s_mf += d - s_ring[s_w];
s_ring[s_w] = d;

/* 3. 硬判决 -> 送进"当前样点所属定时相位"的 40bit 滑窗 */
int ph = s_phase;
uint8_t bit = (s_mf > 0.0f) ? 1u : 0u;
uint32_t lo = (s_lo[ph] << 1) | bit;
uint32_t hi = ((s_hi[ph] << 1) | (s_lo[ph] >> 31)) & 0xFFu;

/* 4. 每样点一次 40bit 汉明比较（查表 popcount），命中就起一路收包 */

/* 5. 属于本相位的收包路线，追加 1 个 bit，到字节边界就试解白化 + CRC */
```

`pluto_ble_adv_rx.c` 负责外围：IIO 上下文/AD9361 配置（10 Msps、硬件带宽 3MHz、`A_BALANCED` 端口、`fast_attack` AGC）、256K 样点一块地 `iio_buffer_refill`、把交织的 int16 IQ 拷进连续缓冲喂给 `demodulate_ble_packets()`。采样率通过 `ble_phy_set_sample_rate(fs_mhz)` 传进解调器（`样点/符号 = fs / 1MHz`，10MHz 就是 10 个样点一个符号），换采样率不用改算法。

![][p9]

#### 2）为什么不能照搬 GNU Radio 的 `gfsk_demod`

一开始我是按 GNU Radio 的链路来的（`quadrature_demod_cf` $\rightarrow$ M&M 时钟恢复 $\rightarrow$ 硬判决），结果**判决出来的比特几乎是纯噪声**——用匹配滤波在原始录波上量 access code，真实信号是精确 40/40、软幅度约 ±1.5；而当时 C 端输出 257801 个判决比特里，AA 最高只到 **34/40**、≥32/40 的只有 16 个（随机比特的期望值就是 23 个）——**等于完全没解调出来**。对照试验后确认了两个不能照搬的地方：

| GNU Radio 的做法 | 在 BLE 上为什么不行 | 本工程的做法 |
|---|---|---|
| `quadrature_demod_cf` 后直接判决 | 10 Msps 下单样点鉴频的相位步进只有 ±0.157rad，噪声按 5MHz 级带宽进来，判决裕量几乎为零 | 判决前加一级 **1 符号积分（boxcar）**，等效带内噪声大降，约 +10dB |
| M&M（Mueller & Müller）反馈环恢复定时 | BLE 前导码只有 8 bit，环根本来不及收敛；更致命的是 M&M 有两个稳定平衡点，实测**锁在了眼图交叉点**（软判决幅度 0.16，而正确采样点是 1.5） | 不用反馈环，改**全相位并行前馈搜索**：10 个定时相位各一个滑窗，每样点只更新 1 个窗、只做 1 次比较，O(1)/样点覆盖全部相位空间 |

详细的参数对照和推导放在工程内 `doc/gnuradio-gfsk-demod.md`（逐行核对了本机 GNU Radio 的 `gfsk.py`／C++ 版本），解调链设计与全部实测证据在 `doc/demodulator.md`。

![][p10]   

</br>

#### 3）坑 1：access code 比特序 —— "32/40 天花板"的元凶

这是最"反直觉"的一个坑，也是"**AA 匹配几乎没有一次能做到 100%**"的根本原因之一：**BLE 的 access address 是 LSB 先发的**。对于广播地址 `0x8E89BED6`，空中实际先出现的是最低字节 `0xD6`，然后 `0xBE`、`0x89`、`0x8E`。

如果按书写顺序把它拼成 `8E 89 BE D6` 当模板，就相当于拿**时间反序的地址**去做相关 —— 真实的包永远匹配不上，相关系数只能停在随机水平（约 20/40），**解调器再完美也到不了 40/40**，这就是长期卡在 32/40 的原因。

正确做法是**从 32 位整数值按 LSB 展开**（`ble_phy.c` 里连注释一起留在了原地，避免以后有人再改回去）：

```c
static void build_aa_pattern(void)
{
    const uint32_t aa = 0x8E89BED6u;   /* BLE advertising access address */
    uint64_t v = 0;
    int k = 0;
    for (int b = 0; b < 8; b++)
        v |= (uint64_t)((0xAAu >> b) & 1u) << (AA_BITS - 1 - k++);   /* 前导码 0xAA */
    for (int b = 0; b < 32; b++)
        v |= (uint64_t)((aa >> b) & 1u) << (AA_BITS - 1 - k++);      /* AA，LSB 先发 */
    s_pat_lo = (uint32_t)(v & 0xFFFFFFFFu);
    s_pat_hi = (uint32_t)((v >> 32) & 0xFFu);
}
```

改完这一条，真机上的 AA 误差立刻从"最高 34/40"变成**大量 0/40**。

</br>

#### 4）坑 2：CRC-24 的空中字节序

第二个坑是**校验方向**：BLE 的 CRC-24 在空口上的 3 个字节是 `swap(高), swap(中), swap(低)`（`swap` = 字节内比特反转），**不是**常见的 LSB-first 顺序。这个坑会同时毒害"写生成器"和"写解析器"两边，特征是"AA 明明 0 错、PDU 看着也像那么回事，但 CRC 永远不过"。

验证方法很土但很有效：**拿一个真实包做基准**。比如美的设备的这条 PDU，最后 3 字节 `A1 5B A5` 就是正确答案；生成器和解析器必须都能复现它：

```c
dst[0] = bt_swap_bits(ret[0]);   /* 高字节 */
dst[1] = bt_swap_bits(ret[1]);   /* 中字节 */
dst[2] = bt_swap_bits(ret[2]);   /* 低字节 */
```

顺便说一句：我们自己的合成包生成器 `tools/gen_ble.py` 一开始也栽在这儿（生成器写错了顺序，结果"解调器解不出自己发的包"）——**只要有一侧的口径是错的，双方都会浪费时间互相怀疑**。

</br>

#### 5）坑 3：AA 命中的相位 ≠ 能把 PDU 解出来的相位（漏包主因）

这是本集含金量最高的一个坑：**收包明明解出了 AA（误差 0/40），但 CRC 就是不过，而且换一个包又能过**。

真相是：**40 bit 的 access code 对采样时刻不敏感**（它只有 40 个比特、而且前导码是 `1010...` 的规整图案，判断裕量大），**但后面 ~300 bit 的 PDU 不是**。我们把每个真实包的 10 个定时相位**逐个试了一遍**，结果是：

- 同一个真实包，**平均只有 4.9 / 10 个相位能通过 CRC**；
- 其余相位的 AA 误差同样是 0，但 PDU 里错 1~2 个 bit → CRC 必挂；
- 更坑的是：**先命中的那个相位，并不代表它是能解出来的那个**。实测某个包最先命中的相位在 PDU 头就错了 1 bit（长度字节 `0x23` 读成 `0x25`），而完美的相位**在 1 个样点之后就出现在窗口里**。

而最初的实现是"AA 一命中就 `s_collect = 1`，并在收包期间**跳过 AA 搜索**（`continue`）"——等于无条件采纳最先命中的那个相位的判决结果，**按 5/10 的概率抽签，抽错就丢包**。

修法：**每个定时相位各起一路收包**。AA 在哪个相位命中，就在哪个相位上独立累计 PDU 位；谁先通过 CRC 谁算数。同一个包会有多路同时成功，报告端按样点位置去重（100 样点内只报一次；BLE 最短的包也有 80 个符号 = 800 样点，不可能吃掉隔壁的包）。

```c
/* 第 4 步：AA 命中，给【本相位】起一路收包（收包期间这一级继续跑，不停） */
int err = pop_hamming(lo ^ s_pat_lo) + pop_hamming(hi ^ s_pat_hi);
int started = 0;
if (err <= AA_ERR_MAX) {
    coll_t *c = &s_coll[ph];
    if (!c->active || err < c->err) {       /* 同一路里出现更优的窗口，可以接管 */
        started = 1;                        /* 当前样点的 bit 已经含在 40bit 窗里 */
        for (int k = 0; k < AA_BITS; k++) { /* 用窗口里的 40 bit 给这路收包打头 */
            int j = AA_BITS - 1 - k;
            c->bits[k] = (uint8_t)((j >= 32) ? ((hi >> (j - 32)) & 1u)
                                             : ((lo >> j) & 1u));
        }
        c->nbits = AA_BITS; c->err = err; c->nsamp = 0; c->active = 1;
    }
}

/* 第 5 步：本样点的 bit 只属于相位 ph，所以只有 ph 这一路在推进（1 符号 1 bit） */
coll_t *c = &s_coll[ph];
if (c->active && !started) {
    if (c->nbits < AA_SLOT_BITS) c->bits[c->nbits++] = bit;
    if (((c->nbits - AA_BITS) % 8) == 0 && ble_try_decode(c, channel))
        c->active = 0;                      /* 解出来了，这一路收工 */
    else if (++c->nsamp > AA_SLOT_BITS * s_sps)
        c->active = 0;                      /* 什么也没收到，放弃 */
}
```

代价只有 10 路 × (40bit 窗 + 384bit 缓冲) ≈ 4KB，每样点仍然只有一次移位。效果（同一批 2.1 秒真实录波）：

| 录波 | 改前 | 改后 | 独立 Python 参考实现 |
|---|---|---|---|
| CH37 | 16 包 | **18 包** | 18 包 |
| CH38 | 7 包 | **23 包** | 18 包 |
| CH39 | 16 包 | **47 包** | 36 包 |

⚠️ 注意这里 C 端反超了 Python 参考实现——因为**参考实现每个候选位置只试一个相位，它自己也在漏包**。所以做交叉验证时要清楚：**"C 明显少于参考"才是 bug 信号，"C 多于参考"是正常的**。另外提醒一句，实现这个"每相位一路"时我踩过一个 1 行的坑：**某一路刚起头时，当前样点的 bit 已经含在 40bit 窗口里了，不要再往这一路追加一次**，否则整包错位 1 bit、一个包都解不出来（现象是"改完 0 包"，见上面代码里的 `!started`）。

</br>

#### 6）坑 4：不要盲目扣 DC / 频偏 —— 试过，删了

坑 3 修完，剩下的漏包就很有限了。但中间我还"想当然"地加过一级**门控慢速 DC 跟踪**（理由：AD9361 有残余 LO 偏置，实测某次录波鉴频输出带 0.086 rad/样点 ≈ 137kHz 的偏置，和 ±250kHz 的 BLE 频偏同量级，不消掉判决门限就偏了）。

它在那一段 0.26 秒的录波上**确实**把收包从 0 变成了 2 —— 但在 2.1 秒的长录波上量化后，结论是**整体有害**：

- 那个跟踪器收敛到的是**噪声的不对称性**（−0.061 rad/样点），而不是包所在处的真实偏置（≈0）；
- 扣掉它等于把整个眼图平移 0.6（信号幅度只有 ±1.5），弱符号成片翻错：同一段录波 **门控 DC 6 包 vs 不扣 16 包**；6 段录波合计 **35 vs 45 包**。

所以最后**把这一级删掉了**，回到和 GNU Radio/参考实现一致的"鉴频 $\rightarrow$ 匹配滤波 $\rightarrow$ 判决"。经验：**看到眼图整体偏在一侧，先查增益（削顶会产生真实的鉴频偏置），不要用盲目估计器去"补"**——尤其是当这个估计器的统计口径（噪声）和你要修的对象（信号处的偏置）不是一回事的时候。

</br>

#### 7）工具链：录波 + 回放 + 独立参考 + 合成包

本集所有结论都靠这套工具量化出来，都在 `tools/`（详细用法见 `tools/README.md`）：

| 工具 | 用途 |
|---|---|
| `gen_ble.py <SNR dB>` | 生成**合成 BLE 广播包**（含 CRC-24/白化/10Msps 交织 int16 IQ），用于解调器回归：当前基线 **≥10dB 出包、≤5dB 不出** |
| `aa_scan.py <阈值> <iq.bin>...` | 匹配滤波 + 全 10 相位扫 access code，输出归一化相关系数 r。用来**判"锅在谁身上"**：真实信号应能到 r≈0.98（等价 40/40）；若 C 输出的比特落在随机统计上，问题就在解调器、不在射频/增益 |
| `decode_cand.py <iq.bin> <信道> <阈值>` | 对候选点按**候选自己的定时相位**完整解码（切片 $\rightarrow$ 解白化 $\rightarrow$ CRC-24），把真包和误报分开，用来核对 C 端是否漏包 |
| `ble_replay.c` | 离线回放：**同一个 `ble_phy.c`**，宿主机 gcc 编译 |

排查问题时的顺序（这套方法论比任何单条经验都值钱）：

1. **先证明锅在谁身上**（`aa_scan.py`）：真实录波的 AA 应该 40/40、软幅度 ±1.5；如果 C 输出的判决比特落在随机统计（汉明距离均值 ≈ 20/40）上，就是解调器的问题，别去怀疑射频和增益。
2. **拿独立实现当基准**（`decode_cand.py`）：同一段录波它解出多少 CRC-OK 的包，C 就该接近多少。**注意参考实现也会漏包（它每位置只试一个相位）**，详见坑 3。
3. **录波 + 回放**（`make capture` / `make replay`）：不接设备也能迭代，一次抓、反复调。
4. **合成包回归**（`gen_ble.py`）：不依赖空口，改完解调器先跑一遍确认没有回退。
5. **要定位"某个包为什么丢了"**，把 C 内部候选表和参考的候选表**按全局样点号**对齐比较。⚠️ 这里有个诊断陷阱：`demodulate_ble_packets()` 是**按块调用**的（每次 262144 样点），函数内的循环下标 `i` 从 0 开始，**它是块内下标、不是全局样点号**。拿它去和录波文件里的位置比，会得出"这里根本没有候选"的错误结论——我自己就被这个假结论带偏过一次（当时误判成 DC 跟踪器的问题）。要比较就自己加一个 `static long long g_n` 数全局序号。

</br>

#### 8）输出与诊断

每解出一个包，打印"三行一框"的报文块（上面第 3 节里就是真实输出）：`[PKT]` 给出信道、全局样点位置、AA 误差、PDU 长度；`[PDU]` 是完整十六进制；`[ADV]` 是解析后的 PDU 类型和 AdvA。另外每 50 个 buffer（约 1.3 秒）打印一行统计：

```
[STAT] 已处理 26.2 M样点 | AA 候选 139 | 成功解包 38
```

| 字段 | 含义 | 正常范围 |
|---|---|---|
| 已处理样点 | 累计处理样点数 | 每 50 个 buffer 涨 13.1M |
| AA 候选 | access code 汉明距离 ≤3 的候选个数（**不是包数**） | 与包数同量级（一个包会在多个相位命中） |
| 成功解包 | 累计 CRC-OK 的包数 | 稳定增长 |

诊断经验：**AA 候选多、成功解包少** → 解调裕量不足（先查坑 3/坑 1）；**AA 候选长期为 0** → 增益/频点/信道参数不对（先用 `iio_attr` 查中心频率和增益）；**`refill` 报错** → CPU 不够或缓冲区太小。

</br>

### 5. 结语

伴随着 SSH 终端里源源不断滚出的 **`[PKT] ... [ADV] type=0x0 AdvA=50:5E:3A:A8:35:3D`**，我们的**板载边缘端蓝牙广播嗅探器**实战圆满成功！

回看这套架构，Pluto 在板载 Linux 里稳稳吞吐 **10MSps 的射频数据**（256K 样点一块），在片上完成了**正交鉴频 $\rightarrow$ 1 符号匹配滤波 $\rightarrow$ 全相位并行定时搜索 $\rightarrow$ access code 匹配 $\rightarrow$ 每相位独立收包 $\rightarrow$ 解白化 $\rightarrow$ CRC-24 校验**的完整链路。16 秒实测 61 个包、来自多个不同设备，`iio_buffer_refill` 零溢出——Zynq-7010 的算力还有充足余量。

更重要的是，这一集留下了一张"**GFSK 小包接收通用避坑清单**"：

| 坑 | 现象 | 结论 |
|---|---|---|
| AA 比特序反了 | 匹配率永远卡在 32/40 上下 | access code 是 **LSB 先发**，模板必须从整数值位展开，不能按书写字节顺序拼 |
| CRC 空中字节序 | AA 全对、PDU 像样，CRC 永不过 | BLE CRC-24 空中是 `swap(高/中/低)`，必须拿真实包做基准 |
| 只认最先命中的相位 | 有 AA（0/40）却收不到包 | **40bit 的 AA 对采样时刻不敏感、300bit 的 PDU 敏感**，实测每包只有 ~4.9/10 个相位能过 CRC → **每个相位各起一路收包** |
| 盲目扣 DC/频偏 | 短录波看着有效、长录波更差 | 估计器统计的是"噪声的不对称性"，不是眼图偏置；**先查增益，别乱补** |
| 反馈环做定时 | 锁定在眼图交叉点 | BLE 前导码只有 8bit，**M&M 收不敛**；用前馈全相位并行搜索，O(1)/样点 |

从第一行 `printf("hello pluto sdr")`，到 FM 收音机、频谱分析仪、ADS-B 飞机解码，再到今天的**蓝牙广播嗅探**——你已经把"算法下沉边缘端"这套打法练了三遍。下一集更精彩：我们让 Pluto 从"听得见"变成"喊得出"，自己造一个 BLE 广播发射端（`ble_phy.h` 里的 CRC-24/白化函数就是收发共用的伏笔）。恭喜道友，我们下一期更精彩！

</br>

### 参考链接

[[1]. 第四集：断开脐带 —— 交叉编译与 PlutoSDR 嵌入式脱机运行实战][#1]    
[[2]. Bluetooth SIG —— Core Specification（BLE 空口：access code / 白化 / CRC-24 的权威定义）][#2]    
[[3]. Wikipedia —— Bluetooth Low Energy][#3]    
[[4]. GNU Radio —— gr-digital `gfsk.py`（本集对照的参考实现，本机源码已逐行核对）][#4]    

[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/110        
[#2]:https://www.bluetooth.com/specifications/specs/        
[#3]:https://en.wikipedia.org/wiki/Bluetooth_Low_Energy        
[#4]:https://github.com/gnuradio/gnuradio/blob/main/gr-digital/python/digital/gfsk.py       
[#5]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/80     
[#6]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/77          
[#7]:https://space.bilibili.com/397747073/lists/4964593?type=season     
[#8]:https://beautifulzzzz.com/gnuradio/tutorial/topic/1     

[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/gnuradio_sdr_ble_stack.png    
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/bilibili_sdr_ble_stack.png        
[p7]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ble_rx_arch2.jpeg         
[p8]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ble_rx_show.gif       
[p9]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ble_rx_code_flowgraph.png       
[p10]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/hackrf_ble_rx_gnuradio_flowgraph.png     



