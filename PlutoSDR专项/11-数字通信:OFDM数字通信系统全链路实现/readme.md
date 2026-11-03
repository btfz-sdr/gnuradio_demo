### 1. 开场

上一集我们用 **DQPSK + 单载波** 让 Pluto 把 `test.png` 实现工业级自收发。这一集把调制从单载波换到 **OFDM**——不是为了炫技，而是因为单载波在真实空口里最棘手的两个问题，恰好是 OFDM 天生擅长处理的：**多径引起的码间干扰（ISI）**，以及**频谱效率**。

OFDM 的思路其实很朴素：与其用一条很宽的"高速公路"跑一串连续的车，不如把它切成几十条窄的"平行车道"，每辆车（一个子载波）慢慢开，车与车之间互不干扰。窄车道对多径不敏感（每个子载波的符号周期变长，复制过来的回波来不及干扰下一个符号），代价是要加一段循环前缀（CP）当"缓冲垫"。

![][p11]

这一集我们就用 liquid-dsp 的 `ofdmflexframegen` / `ofdmflexframesync`，在 Pluto 板载 ARM 上把这条链路真正跑通：

**读文件 → CRC32 → 908 个 128B 分片 → 打包用户头与元数据 → 加 Hamming(7,4) 编码与字节级交织 → 组帧 (ofdmflexframegen) → 符号映射 (QPSK/BPSK) → 64点子载波映射（44数据 + 6导频 + 14空子载波） → IFFT 调制与插入 16点循环前缀 (CP) → 附加 S0/S1 前导、tail 与帧间隙 → 按“轮次错开”策略编排序列 → int16 IQ 缩放 → 硬件循环缓冲区推流 → AD9361 TX →（板内泄漏 / 环路 / 空口）→ AD9361 RX 采集 → float IQ 还原 → 直流抑制 + 峰值归一化 → 第一遍离线同步（利用 S0 突发检测与粗频偏估计）捕获文件头帧（获取大小、CRC、分片数） → 分配重组缓冲 → 第二遍离线同步（利用 S1 符号进行信道估计、64点 FFT 解调与子载波均衡）收齐所有数据帧 → 按 chunk index + 轮次去重并拼回缓存 → 文件级 CRC32 校验 → recovered.png**

![][p12]

这条链路有三个题眼，也正好是本集要展开讲的内容：

1. **为什么把"同步"做成一帧一次，而不是一整段锁一次**：OFDM 的每一帧都自带 S0/S1 前导，接收端对每个帧重新做突发检测、定时、频偏估计和信道估计。丢一帧只丢那一帧，不会连锁失锁——这是"块状传输"和上一集单载波"连续载波跟踪"最本质的区别。
2. **为什么一帧只能装 128 字节**：64 个子载波里只有 44 个用来传数据，QPSK 每个子载波带 2 bit，一个 OFDM 符号就是 88 bit；再算上 Hamming(7,4) 的 7/4 展开，一个符号实际只送 ~6.3 字节原始数据，128 字节净荷要 21 个数据符号。把账算清楚，才能理解后面所有的耗时和码率。(OFDM 符号，就是某一时刻并行同时发出的信息总量）
3. **为什么重复帧要"错开一整轮"发**：上一集把同一 chunk 的两个副本相邻着发，这一集改成"第一轮发完 908 个 chunk，第二轮再发一遍"。因为每一帧都独立同步，错开发能把一次持续几百毫秒的短衰落/短突发摊薄——最多打掉同一 chunk 的一份拷贝。

**补充：** 本集代码在 `00-examples/09-pluto_ofdm_file_self_transfer/`。算法先在宿主机单文件仿真版 `doc/01-simulation/ofdm_pipeline.c` 上验证（可注入频偏/相偏/噪声/突发），再拆成真机版 `pluto_ofdm_file_self_transfer.c + ofdm_phy.c/h`。

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

</br>

### 3. 交叉编译并运行

a. 先看目录结构：

```
➜  00-examples tree -L 1
.
├── 01-hello_plutosdr
├── 02-ad9361-iiostream
├── 03-pluto_fm_radio
├── 04-pluto_spectrum_analyzer
├── 05-pluto_adsb_receiver
├── 06-pluto_ble_adv_rx
├── 07-pluto_ble_adv_tx
├── 08-pluto_qpsk_file_self_transfer   <-- 上一集：单载波 DQPSK 文件自收发
├── 09-pluto_ofdm_file_self_transfer   <-- 本集：OFDM 文件自收发
└── toolchain                          <-- 交叉编译工具链 + libiio + liquid-dsp

➜  09-pluto_ofdm_file_self_transfer tree -L 2
.
├── pluto_ofdm_file_self_transfer.c    # 主程序：getopt + 读文件 + AD9361 配置 + 循环 TX 推流 + RX 采集
├── ofdm_phy.c                         # 【核心】OFDM 基带：flexframe 组帧、两遍同步解码、重组
├── ofdm_phy.h                         # OFDM 波形参数 + 帧类型 + 基带对外接口
├── makefile                           # build / run / sim / simtest
├── README.md                          # 本工程快速使用说明
├── doc/
│   ├── README.md                      # 工程总览
│   ├── design.md                      # 架构与帧结构设计
│   └── 01-simulation/                 # 单文件仿真版（ofdm_pipeline.c + test.png）
├── skills/                            # 工程笔记：硬规则、实测基线、避坑清单
└── tools/                             # sim_test.sh 仿真回归脚本
```

</br>

b. 交叉编译：

```
➜  09-pluto_ofdm_file_self_transfer make build
arm-linux-gnueabihf-gcc pluto_ofdm_file_self_transfer.c ofdm_phy.c -o pluto_ofdm_file_self_transfer \
    -I.../toolchain/libiio/usr/include -I.../toolchain/liquid-dsp/include \
    -Wall -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
    -L.../toolchain/libiio/usr/lib/arm-linux-gnueabihf \
    -L.../toolchain/libiio/lib/arm-linux-gnueabihf \
    .../toolchain/liquid-dsp/lib/libliquid.a -liio -lm -Wl,--allow-shlib-undefined
arm-linux-gnueabihf-strip pluto_ofdm_file_self_transfer

➜  09-pluto_ofdm_file_self_transfer file pluto_ofdm_file_self_transfer
pluto_ofdm_file_self_transfer: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
    dynamically linked, interpreter /lib/ld-linux-armhf.so.3, for GNU/Linux 3.2.0,
    BuildID[sha1]=455ec524c65b11ba80a2ca8c2ccbfffe63428817, stripped
```

- `-march=armv7-a -mfpu=neon` 不是装饰：OFDM 每帧都要做 64 点 IFFT/FFT，NEON 打开与否在 Cortex-A9 上是倍数级的差别。
- `-Wl,--allow-shlib-undefined` 沿用前几集的做法——本机 toolchain 里只有 libiio 本体，它的子依赖在 Pluto rootfs 上才齐全。

</br>

c. 先在宿主机上验证基带（**强烈建议**，这一步能排除 90% 的"到底是算法错还是射频没通"）：

```
➜  09-pluto_ofdm_file_self_transfer make simtest
==> 编译 .../doc/01-simulation/ofdm_pipeline.c
==> 运行: 默认参数
== OFDM pipeline simulation ==
cfo=0.02000 phase=5.279 noise=0.050 gain=1.000 lead=0 burst=0 pos=0.50
file=test.png size=116108 crc=0x516CAD26 chunks=908
tx_n=4945600 samples (4.95 M)
  [sim] OFDM header: size=116108, crc=0x516CAD26, chunks=908
pass1: frames=1820 header=OK
pass2: frames=1820 decoded=908/908 chunks
PASS: file CRC OK, wrote recovered.png
==> PASS: 默认参数
```

`bash tools/sim_test.sh stress` 还会再跑一组：随机相偏 ×5、`SIM_LEAD=100000`（模拟 RX 早启动、前面几千个样点全是零）、`SIM_BURST=5000`（中途挖掉 5000 个样点模拟强干扰/掉电）、`SIM_NOISE=0.2 SIM_CFO=0.02`（更强噪声 + 频偏），本机全部通过。仿真信道旋钮都是环境变量，改一个就能复现一类真机现象，对照表在 `doc/01-simulation/README.md`。

</br>

d. 上板单板自收发。`makefile` 的默认参数全都可以用命令行覆盖：

```
IN_FILE  ?= test.png     # 待发送文件
OUT_FILE ?= recovered.png# 接收重组文件
LO_MHZ   ?= 2500         # 中心频率 MHz
SAMP_MHZ ?= 10           # 采样率 MHz（OFDM 建议 ≥10，子载波间隔更宽，抗双机频偏）
BW_MHZ   ?= 8            # 模拟带宽 MHz
RX_GAIN  ?= 40           # RX 手动增益 dB
TX_ATTN  ?= 10           # TX 衰减 dB
```

```
➜  09-pluto_ofdm_file_self_transfer make run
scp -O pluto_ofdm_file_self_transfer root@pluto.local:~/
scp -O doc/01-simulation/test.png root@pluto.local:~/test.png
ssh -t root@pluto.local "chmod +x ~/pluto_ofdm_file_self_transfer; \
    ./pluto_ofdm_file_self_transfer -i test.png -o recovered.png -l 2500 -s 10 -b 8 -g 40 -a 10"

[FILE] Loading test.png ...
[FILE] size=116108, crc=0x516CAD26, chunks=908
[TX] Generated 4945600 OFDM samples (4.95 M).
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
* Enabling TX streaming channels
[TX] Cyclic buffer filled (4945600 samples), pushing once...
* Enabling RX streaming channels
[RX] Collecting 6945600 samples...
[RX] 4194304 / 6945600 samples
[RX] Collected 6945600 samples.
[RX] OFDM header decoded: file_size=116108, crc=0x516CAD26, chunks=908
[RX] Pass 1: 2549 frames detected, header OK.
[RX] Pass 2: 2549 frames detected, decoded 908 / 908 chunks (unique).
[RX] File CRC OK (0x516CAD26). Writing recovered.png
real    0m 18.76s
user    0m 18.25s
sys     0m 0.40s
```

> `make run` 里的 `scp/ssh` 是裸命令，没有内置密码。容器里装了 `sshpass` 的话直接可用；宿主机上则要么先配好免密，要么照上面三条命令手动执行一遍（`scp -O` 的 `-O` 不能省，Pluto 的 busybox 不支持 SFTP）。

把板上的文件拉回来比对：

```
➜  md5sum test.png recovered.png
055453f5baf71482da04b02f9b1aed03  test.png
055453f5baf71482da04b02f9b1aed03  recovered.png
```

逐字节一致。几条打印值得先解释清楚，它们就是整条链路的账本：

| 打印行 | 是什么 | 怎么算出来的 |
|---|---|---|
| `chunks=908` | 文件分片数 | `ceil(116108 / 128) = 908`，最后一片只有 12 字节 |
| `Generated 4945600 OFDM samples` | 整段基带样点数 | `4×1520（头帧） + 1816×2720（数据帧）`，见第 4 节 6） |
| `Collecting 6945600 samples` | RX 采集样点数 | `tx_n + 0.2 s × 10 MSPS = 4,945,600 + 2,000,000` |
| `Pass 1: 2549 frames detected` | 第一遍检出的帧数 | 6,945,600 样点覆盖 1.404 个发送周期 → `1820 × 1.404 ≈ 2555`，实测 2549 |
| `decoded 908 / 908 chunks` | 去重后收齐的分片 | 1816 个数据帧里每个 chunk 各有 2 个副本，只要一份活着就算数 |
| `real 18.76s` | 板载总耗时 | 两次 pass 各遍历 694 万样点，`user 18.25s` 说明是**单核算力**瓶颈，不是射频 |

</br>

e. **一个必须实测的参数：RX 手动增益**。OFDM 的峰均比（PAPR）比单载波高得多，增益给猛了削顶、给少了信噪比不够，而这条"甜区"只能试出来。同一块板、同一组参数，只改 `-g`：

| RX 增益 | Pass 1 检出帧数 | 结果 | 板载耗时 |
|---|---|---|---|
| 20 dB | 1198 | `header MISSING`，头帧一个都没解出来 | 10.3 s |
| 30 dB | 2548 | 头帧 OK，但只解出 **492 / 908** chunk | 19.1 s |
| **40 dB** | 2549 | **908 / 908，File CRC OK** | 18.8 s |
| 50 dB | 2552 | 908 / 908，File CRC OK | 18.7 s |

20 dB 时能检出 1198 个"疑似帧"却没有一个头帧通过校验：S0 的能量相关在低信噪比下还会偶尔冒峰，但头部要用 Golay(24,12) + CRC32 真正解出来，信噪比不够就是过不去。30 dB 更有意思——**头帧活了，数据帧只活了一半**。原因在编码强度：头部是 BPSK + Golay(24,12)（编码率 1/2），数据帧是 QPSK + Hamming(7,4)（编码率 4/7），同样误码率下 BPSK 每符号的门限比 QPSK 低约 3 dB，再加上头帧重复 4 次、数据帧只重复 2 次；两头叠加，就出现了"头解出来了、chunk 却缺 416 个"的中间态。这条现象正好说明"头帧 OK"不等于"链路 OK"，最终判据只能看文件 CRC。

</br>

f. 两机收发（`-T` / `-R`）。本集把收发做成了可分离的两个模式，两机场景就是一台只发、一台只收：

```
# TX 端：循环推流，Ctrl-C 结束
./pluto_ofdm_file_self_transfer -T -i test.png -l 2500 -s 10 -b 8 -a 10

# RX 端：先采固定长度样本，采集完再离线解码
./pluto_ofdm_file_self_transfer -R -o recovered.png -l 2500 -s 10 -b 8 -g 40 -S 10000000
```

`-T` 模式推完一次后不做任何事（循环缓冲区在硬件里自己转），`-R` 模式用 `-S` 指定采集样点数，10 MSPS 下 1000 万点 = 1 秒，足够覆盖一个完整发送周期（0.495 s）还留了余量。RX 端不需要预先知道文件大小——文件头帧会把 `file_size / file_crc / num_chunks` 带过来。

> 两机空口需要两台 Pluto 同时连着，本集只用一台做了自环实测，两机部分是按 `doc/design.md` 的设计给出的命令，**尚未实测**。频偏账先算一下：10 MSPS、64 子载波时子载波间隔 156.25 kHz，liquid 的 S0 频偏估计一般能覆盖 ±0.5 个子载波间隔（约 ±78 kHz），对应 2.5 GHz 下约 ±31 ppm；两端 TCXO 若偏差更大，优先把采样率提到 20 MSPS 或先校准 LO，而不是去改 OFDM 内部参数。

</br>

### 4. 代码介绍

先把整件事用通俗话讲一遍，再逐段看代码。

**发送方向：文件是怎么变成 OFDM 电磁波的？**

1. **切块**：文件按 128 字节切 908 块，最后一块不足就补零（真实长度由文件头里的 `file_size` 控制）。
2. **贴快递单**：每块前面挂 8 字节用户头（帧类型、第几次重复、chunk 编号），接收端靠编号把碎片放回原位。
3. **打成 OFDM 帧**：每块变成一个独立的 OFDM 帧。帧最前面是 S0/S1 前导（给接收端找突发、对时、估频偏、估信道用），中间是 BPSK 头部，后面才是 QPSK 净荷。相当于每一块都自带"信封 + 邮票 + 收件地址"，不依赖上一块。
4. **数字变射频**：整段样点一次性塞进 AD9361 的硬件循环缓冲区，由采样时钟按 10 MSPS 的节奏往 DAC 送，软件只负责把它填满一次。
5. **发射**：上变频到 2.5 GHz 从 TX 口出去。

**接收方向：电磁波又是怎么变回文件的？**

1. **射频变数字**：RX 下变频、ADC 采样，按 10 MSPS 落在内存里。
2. **清理信号**：去直流（AD9361 有 LO 泄漏引起的 DC 偏置，OFDM 的 DC 子载波本来也是 NULL），再按峰值归一化，把信号拉到合理动态范围。
3. **第一遍同步**：整段样点过一遍同步器，只关心一件事——**有没有文件头帧**。有，就从它里面拿到文件大小、CRC、分片数。
4. **第二遍同步**：按文件大小分配好重组缓冲，reset 同步器，把同一段样点再过一遍，这次专心收数据帧，按 chunk 编号去重、填到对应偏移。
5. **证明没错**：908 块收齐只是必要条件，最后对整份文件算一次 CRC32，与文件头里的一致才写盘。

一句话总结：**发送端把文件切成"自带前导的小块"，每块独立调制到一组正交子载波上；接收端对每一块重新同步、解调、校验，最后按编号拼回文件，用文件级 CRC 证明一个字节都没错。**

![][p3]

下面进入代码细节。

</br>

#### 1）链路总览与文件分工

主程序只做"文件 + 射频 + 数据搬运"，信号处理全部集中在 `ofdm_phy.c`：

```c
/* ---- 主程序 pluto_ofdm_file_self_transfer.c ---- */
uint32_t file_crc = crc_generate_key(LIQUID_CRC_32, file_data, (unsigned int)file_size);
uint32_t num_chunks = (uint32_t)((file_size + PAYLOAD_CHUNK_SIZE - 1) / PAYLOAD_CHUNK_SIZE);

unsigned int tx_capacity = ofdm_phy_calc_tx_capacity(num_chunks);
tx = malloc(tx_capacity * sizeof(float complex));
tx_n = ofdm_phy_build_tx(tx, tx_capacity, file_data, file_size, file_crc, num_chunks);

txbuf = iio_device_create_buffer(tx_dev, (size_t)tx_n, true);  /* true = 硬件循环 */
/* ... 填 int16 IQ，push 一次 ... */
/* ... RX 非循环 buffer 连续 refill，采集 tx_n + margin 个样点 ... */

int ok = ofdm_phy_rx_process(rx, rx_collected, file_size, file_crc, num_chunks, param_out);
```

`ofdm_phy.h` 只暴露三个入口（`calc_tx_capacity` / `build_tx` / `rx_process`）和一组波形常量，`ofdm_phy.c` 内部全是 `static` 的算法函数。这种拆法和上一集一致，好处是**算法的权威版本只有一份**（`ofdm_phy.c`），主程序里看不到任何 FFT、星座、同步的细节。宿主机仿真版为了保持单文件便于回归，把这些逻辑复制了一份到 `doc/01-simulation/ofdm_pipeline.c`——这是本工程已知的一处重复，**改波形参数（`OFDM_M` / `OFDM_CP` / 分片大小 / 重复次数）时两边必须同步改**，否则会出现"仿真过、真机不过"的假象。

![][p4]

</br>

#### 2）OFDM 参数：64 个子载波、16 点循环前缀

```c
#define OFDM_M                 64      /* 子载波数（偶数，>=8） */
#define OFDM_CP                16      /* 循环前缀长度（样点） */
#define OFDM_TAPER             4       /* OFDM 符号重叠 taper 长度 */
#define OFDM_GAP_SYMBOLS       2       /* 每帧之后额外补的零符号数 */
```

子载波分配用 liquid 的默认表 `ofdmframe_init_default_sctype(64)`：**44 个数据子载波 + 6 个导频 + 14 个空子载波**（空子载波留作保护带和 DC，导频均匀插在数据子载波之间，S1 符号正是靠它估出每个子载波的幅度与相位）。这个比例决定了所有码率，值得记下来：

$$\Delta f = \frac{f_s}{N} = \frac{10\times10^{6}}{64} = 156.25\ \text{kHz}$$

$$T_{\text{sym}} = \frac{N + N_{cp}}{f_s} = \frac{64+16}{10\times10^{6}} = 8\ \mu\text{s}$$

也就是说：**子载波间隔 156.25 kHz，一个 OFDM 符号 8 µs（其中有用部分 6.4 µs，循环前缀 1.6 µs，CP 开销 20%）**。10 MSPS 采样下，一个符号正好 80 个样点。

这里有两个设计上的取舍：

- **CP 取 1/4 而不是 1/16**：CP 越长越抗多径（能容忍的时延扩展越大），但开销也越大。1/4 是"室内/桌面短距离多径 + 定时误差"这个场景里比较稳的选择，代价是 20% 的速率。真正做长距离宏蜂窝才会压到 1/8、1/16。
- **采样率取 10 MSPS 而不是 2.5 MSPS**：同样的 64 子载波，采样率越高子载波间隔越宽（2.5 MSPS 时只有 39 kHz），而**子载波间隔越宽、对收发两端的载波频偏越宽容**。这是上一集单载波不需要考虑、OFDM 必须考虑的问题，第 3 节 f 小节的频偏账就是算这个。

![][p5]

</br>

#### 3）一帧到底装了多少东西：31 个 OFDM 符号的账（微观层面）

```
========================================================================================
 1. 净荷数据流膨胀过程 (Data Pipeline Expansion)
========================================================================================
 [ test.png 文件 ] (116,108 B)
        │
        ▼ 按 128 B 切片 (最后一片补零)
 ┌───────────────┬───────────────┬─────  ───┬───────────────┐
 │ Chunk 0 (128B)│ Chunk 1 (128B)│   ...    │ Chunk N (12B) │
 └───────────────┴───────────────┴──────────┴───────────────┘
        │
        ├─────────────────────────────────────────────────┐
        ▼ (+4 B CRC32)                                    │
 [ 132 字节净荷 ]                                          │
        │                                                 ▼ (单帧总账概览)
        ▼ (× 7/4 Hamming)                                 • 净荷校验: 128 + 4 = 132 B
 [ 231 字节 = 1848 比特 ]                                  • Hamming(7,4): 231 B = 1848 bit
        │                                                 • QPSK 调制: 1848 / 2 = 924 符号
        ▼ (÷ 2 比特/符号，QPSK)                            • 数据子载波: 924 / 44 = 21 个符号
 [ 924 个 QPSK 调制符号 ]                                  • 头部符号: 7 个 (Golay 编码 + BPSK)
        │                                                 • 前导符号: 3 个 (S0a, S0b, S1)
        ▼ (÷ 44 数据子载波/符号)                            • 尾部与间隔: 1 个 tail + 2 个 gap
 ┌────────────────────────────────────────────────────────┐
 │ 21 个 OFDM 数据符号                                     │
 └────────────────────────────────────────────────────────┘

========================================================================================
 2. 时间轴排布与物理帧结构 (Time-Domain Frame Structure: 34 Symbols = 2720 Samples)
========================================================================================
 一个 Chunk 的传输总时长：34 符号 × 8.0 µs/符号 = 272 µs (@ 10 MSPS)

 ┌─────┬─────┬─────┬───────────┬──────────────────────────────────┬──────┬─────┬─────┐
 │ S0a │ S0b │ S1  │ 头部 ×7   │        QPSK 数据符号 ×21           │ tail │ gap │ gap │
 └─────┴─────┴─────┴───────────┴──────────────────────────────────┴──────┴─────┴─────> 时间 (t)
   ↑     ↑     ↑         ↑                       ↑                    ↑      ↑     ↑
   │     │     │         │                       │                    │      └─────┴─ 两个零符号 gap
   │     │     │         │                       │                    │               (防止帧间粘连、重置同步器)
   │     │     │         │                       │                    └───── 1 个零符号 tail
   │     │     │         │                       │                           (吐干净成形滤波器拖尾)
   │     │     │         │                       └───────── 21 个 OFDM 数据符号
   │     │     │         │                                  (搬运 128 B 净荷数据)
   │     │     │         └───────── 7 个头部符号 (装载 8 B 用户头 + 内部 6 B + CRC)
   │     │     └─────────────────── S1 符号 (已知符号，用于信道估计、幅相校准)
   └─────┴───────────────────────── S0a + S0b 突发检测、定时与粗频偏估计

========================================================================================
 3. 单个 OFDM 符号的微观剖面 (Single OFDM Symbol: 80 Samples @ 10 MSPS = 8.0 µs)
========================================================================================
 ┌──────────────────────────┬────────────────────────────────────────────────────────┐
 │ 循环前缀 CP (16 样点)      │                  有用符号周期 (64 样点)                  │
 │      1.6 µs              │                       6.4 µs                           │
 └──────────────────────────┴────────────────────────────────────────────────────────┘
  * 复制有用符号末尾 16 点      * 64 点 IFFT 变换
    作为多径保护缓冲垫          * 子载波间隔 Δf = 10 MHz / 64 = 156.25 kHz

========================================================================================
 4. 频域 64 子载波分配矩阵 (Frequency Domain: 64 Subcarriers)
========================================================================================
 频域索引: [ 0 … 6 ] ──► [ 7 … 31 ] ──► [ 32 ] ──► [ 33 … 57 ] ──► [ 58 … 63 ]
 角色:     14 个空子载波   数据与导频      DC空载      数据与导频       14个空子载波
          (Nyquist边缘) (含 25 个D/P)   (0 Hz)    (含 25 个D/P)    (保护带/滤波边缘)

 图例对应:
 N = 空子载波 (14 个：包含 Nyquist 边缘、±1~±6 保护带及 DC 载波，躲避本振泄漏)
 D = 数据子载波 (44 个：承载 924 个 QPSK 调制符号)
 P = 导频子载波 (6 个：均匀穿插在 ±12 / ±20 / ±28 等固定位置，供接收端动态跟踪信道)
```

</br>

#### 4）发送端：8 字节用户头 + 两种帧

从宏观来看，整个通信过程分为文件头阶段和数据分片传输阶段，通过协同发送两种帧来完成任务：

- **文件头帧（File Header Frame）—— “总指挥部”**：
    - 全局只此一份，包含文件的总大小（`file_size`）、整文件校验（`file_crc`）和总分片数（`num_chunks`）。
    - 重复发送 4 次：为了对抗无线空口的冷启动和突发干扰，确保接收端“随便抓到其中一个就行”。
- **数据帧（Data Frames）—— “运输部队”**：
    - 每一个 Chunk 对应一个数据帧，携带对应的 `chunk_idx`（分片索引）和 128 字节的实际内容。

##### 1. 文件头帧（Header）的结构与设计

文件头帧组装可以参考下面 `build_header_user_hdr` 函数：

```c
/* 打包文件头帧的用户头（8 字节） */
static void build_header_user_hdr(unsigned char *hdr, uint16_t repeat_idx)
{
    memset(hdr, 0, HDR_USER_LEN);
    hdr[0] = FRAME_TYPE_HEADER;                /* 1：表示文件头帧 */
    hdr[1] = (unsigned char)(repeat_idx & 0xffu); /* 重复序号 (0-3) */
    /* 剩余字节保持为 0（Reserved） */
}

```

文件头帧的用户头只填 `type = 1` 和重复序号，**真正的元数据放在净荷里**（32 字节）：

```c
unsigned char header_payload[HEADER_PAYLOAD_LEN];
memset(header_payload, 0, sizeof(header_payload));
memcpy(header_payload + 0,  &file_size, 8);    /* 文件字节数 */
memcpy(header_payload + 8,  &file_crc,  4);    /* 整文件 CRC32 */
memcpy(header_payload + 12, &num_chunks, 4);   /* 分片数 */

```

为什么元数据不塞在 8 字节用户头里？因为 liquid 的用户头只有 8 字节，`file_size(8) + file_crc(4) + num_chunks(4)` 根本放不下。放进净荷还有个附带好处：**头帧净荷也走一遍 Hamming + CRC32**，比裸的 8 字节头更抗错。头帧重复 4 次，就是为了让接收端“随便抓到其中一个就行”。

##### 2. 数据帧（Data）的结构与补零机制

数据帧则使用配套的 `build_data_user_hdr` 函数来填充 8 字节用户头，通过 `type` 字段与文件头帧区分：

```c
/* 打包数据帧的用户头（8 字节） */
static void build_data_user_hdr(unsigned char *hdr, uint32_t chunk_idx,
                                uint16_t repeat_idx)
{
    memset(hdr, 0, HDR_USER_LEN);
    hdr[0] = FRAME_TYPE_DATA;                  /* 2：表示数据帧 */
    hdr[1] = (unsigned char)(repeat_idx & 0xffu);
    memcpy(hdr + 2, &chunk_idx, 4);
}

```

数据帧净荷固定 128 字节，最后一片不够就补零：

```c
uint64_t off = (uint64_t)chunk_idx * PAYLOAD_CHUNK_SIZE;
uint64_t remaining = file_size > off ? file_size - off : 0;
unsigned int cur = remaining < PAYLOAD_CHUNK_SIZE ? (unsigned int)remaining : PAYLOAD_CHUNK_SIZE;

memset(data_payload, 0, sizeof(data_payload));
memcpy(data_payload, file_data + off, cur);    /* 不足部分留零 */

```

接收端按 `file_size` 截断写盘，所以补的零不会污染文件。

![][p6]   

##### 3. 如何将 8 字节用户头映射为 7 个 OFDM 符号？

在微观层面，无论发送的是文件头帧还是数据帧，它们在物理层（空口射频）上都共享同一个严格的时域和频域结构：

```text
========================================================================================
 逻辑结构与物理层时域符号的映射关系插图 (Mapping Diagram)
========================================================================================

【宏观逻辑层：单帧组成】
┌───────────────────────────┬────────────────────────────────────────────────────────┐
│   User Header (绿色 8B)    │           Payload (蓝色，128B 数据或 32B 元数据)        │
└───────────────────────────┴────────────────────────────────────────────────────────┘
                   │                                      │
                   │ (1. 加强纠错编码: Rate 1/2 FEC)        │ (1. 加 CRC32 校验 + Hamming)
                   │ (2. BPSK 调制映射到 44 个子载波)        │ (2. QPSK 调制映射到 44 个子载波)
                   ▼                                      ▼
【微观物理层：时域 OFDM 符号排布】
┌─────┬─────┬─────┬───────────┬──────────────────────────────────┬──────┬─────┬─────┐
│ S0a │ S0b │ S1  │  头部 ×7   │        QPSK 数据符号 ×21          │ tail │ gap │ gap │
└─────┴─────┴─────┴───────────┴──────────────────────────────────┴──────┴─────┴─────>
                      ▲                                              ▲
                      │                                              │
                      └─ 对应绿色部分：                                └─ 对应蓝色部分：
                         经过 BPSK 编码与子载波分配                        经过 Hamming + QPSK 编码
                         在时域上精准占据 7 个符号的位置                    在时域上展开为 21 个数据符号
```

在底层物理层（如 liquid-dsp 的 `flexframegen`）中，这 8 字节的用户头并不是直接变成射频信号的，而是经历了一套标准的**信道编码与资源映射流程**：

1. **组装原始比特**：8 字节用户头（64 bits）+ 内部控制开销（6 字节 / 48 bits）+ 头部 CRC 校验（2 字节 / 16 bits），总共约有 **128 bits（16 字节）** 的原始控制信息。
2. **强前向纠错编码 (FEC)**：为了保证控制头部具有极高的抗干扰、抗误码能力，系统会对这 16 字节施加 **Rate 1/2 的前向纠错编码（如 Hamming 汉明码或卷积码）**，编码后数据量膨胀一倍，得到大约 **336 bits** 的编码比特流。
3. **子载波与调制映射**：
    * 头部为了追求极端鲁棒性，通常采用 **BPSK 调制**（每个子载波承载 1 bit）。
    * 在 64-FFT 体系中，扣除直流、保护带和导频后，每个 OFDM 符号实际可用的有效子载波数量为 **44 个**。
    * 将总编码比特除以单个符号的容量：$\frac{336 \text{ bits}}{44 \text{ subcarriers} \times 1 \text{ bit/symbol}} \approx 7.63$ 个符号。
4. **符号对齐**：物理层向上取整并对齐，最终在时域上精准占据 **7 个 OFDM 符号**的位置（即图中的 `头部 ×7`），优先于后面的 128 字节净荷发射，让接收端能够提前“秒懂”当前帧的身份与控制参数。

</br>

#### 5）发送端：重复帧"错开一整轮"

这是本集相对上一集 QPSK 工程最重要的一处改进：

```c
/* 同一 chunk 的两次重复不放在相邻位置，而是先完整发一轮、再完整发第二轮；
 * 这样短突发/短衰落最多打掉同一 chunk 的一份拷贝，另一份大概率存活。 */
for (unsigned int r = 0; r < REPEAT_FACTOR; r++) {
    for (uint32_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
        ...
        build_data_user_hdr(hdr, chunk_idx, (uint16_t)r);
        total = write_one_frame(fg, hdr, data_payload, PAYLOAD_CHUNK_SIZE, &tx[tx_cursor]);
        tx_cursor += total;
    }
}
```

上一集是"chunk0、chunk0、chunk1、chunk1……"，两个副本在时间上只隔几毫秒；这一集是"chunk0…chunk907、chunk0…chunk907"，两个副本隔了整整 **0.247 秒**。

区别在于**它能防什么类型的干扰**：相邻重复只能防"随机的单帧误码"，一旦来一段持续几十毫秒的干扰（对端设备突发、衰落谷、ADC 饱和），相邻的两个副本会一起死；错开 0.247 s 之后，同样的干扰最多打死一份。仿真里 `SIM_BURST=5000`（在 10 MSPS 下挖掉 5000 个样点 = 0.5 ms 的整段清零）能稳定通过，靠的就是这个设计——它清掉的是一小段连续的帧，而每个 chunk 的另一份在 0.247 s 之后。

代价也很直接：接收端必须等**整轮发完**才可能凑齐文件，最坏情况下延迟从"毫秒级"变成"两轮时长"。文件传输场景不介意这个，实时语音就不能这么干。

</br>

#### 6）发送端：整段 IQ 交给硬件循环缓冲区

`ofdm_phy_build_tx()` 返回的是 float 复数样点，主程序把它缩放到 12-bit DAC 满量程的 0.8 倍，填进长度恰好等于 `tx_n` 的**循环缓冲区**，然后只 push 一次：

```c
float peak = 1e-6f;
for (unsigned int i = 0; i < tx_n; i++) {
    float m = cabsf(tx[i]);
    if (m > peak) peak = m;
}
float tx_scale = 0.8f * 32767.0f / peak;      /* 留 20% 余量防削顶 */

txbuf = iio_device_create_buffer(tx_dev, (size_t)tx_n, true);  /* cyclic */
for (char *p = (char *)iio_buffer_first(txbuf, tx0_i); p < p_end; p += p_inc) {
    ((int16_t *)p)[0] = (int16_t)lrintf(crealf(tx[cnt]) * tx_scale);
    ((int16_t *)p)[1] = (int16_t)lrintf(cimagf(tx[cnt]) * tx_scale);
    cnt++;
}
[TX] Cyclic buffer filled (4945600 samples), pushing once...
iio_buffer_push(txbuf);
```

为什么按**整段峰值**缩放、而不是像接收端那样做 AGC？因为发送端只需要一件事：整段信号都别削顶。OFDM 是多载波叠加，峰均比能到 10 dB 以上，如果按平均功率归一化，偶发的峰会直接撞上 DAC 满量程，削顶产生的带外杂散会污染邻道；按整段峰值缩放到 0.8，把削顶概率压到零，代价是平均功率低了几个 dB——自环和短距离空口都完全够用。

那么 `tx_n = 4,945,600` 是怎么来的？把第 3 节的账乘开：

| 项 | 计算 | 样点 |
|---|---|---|
| 文件头帧 × 4 | `4 × (16 + 1 + 2) × 80` | 6,080 |
| 数据帧 × 1816 | `1816 × (31 + 1 + 2) × 80` | 4,939,520 |
| 合计 | `908 chunk × 2 轮 + 4 头帧 = 1820 帧` | **4,945,600** |

`+1` 是 tail 零符号（把帧尾滤波器的拖尾吐干净），`+2` 是帧间隙（让相邻帧的同步状态复位）。这两个看起来"多余"的符号少了任何一个，都会出现"最后一帧解不出"或"相邻帧粘连"这类难查的怪问题——这是 `skills/` 里排前三的硬规则。

按 10 MSPS 折算，整段空口时长 = 4,945,600 / 10e6 = **0.4946 s**。

</br>

#### 7）接收端：int16 → float、去直流、峰值归一化

RX 非循环 buffer 连续 refill，把 12-bit MSB 对齐的 int16 还原成 float 复数：

```c
while (!stop && rx_collected < rx_need) {
    ssize_t nb = iio_buffer_refill(rxbuf);
    for (char *p = (char *)iio_buffer_first(rxbuf, rx0_i);
         p < p_end && rx_collected + cnt < rx_need; p += p_inc, cnt++) {
        int16_t i = ((int16_t *)p)[0];
        int16_t q = ((int16_t *)p)[1];
        rx[rx_collected + cnt] = ((float)i / 32768.0f) + _Complex_I * ((float)q / 32768.0f);
    }
    rx_collected += cnt;
}
```

采满后进入 `ofdm_phy_rx_process()`，第一步是两段原地预处理：

```c
/* 简单一阶直流抑制：AD9361 直流附近通常有 LO 泄漏/DC 偏置 */
static void dc_block(float complex *x, unsigned int n)
{
    float complex dc = 0.0f;
    const float alpha = 1e-4f;
    for (unsigned int i = 0; i < n; i++) {
        dc = dc * (1.0f - alpha) + x[i] * alpha;
        x[i] -= dc;
    }
}

/* 峰值归一化：整段拉到目标峰值，既防削顶，又给弱信号足够增益 */
static void normalize_peak(float complex *x, unsigned int n)
{
    float peak = 1e-6f;
    for (unsigned int i = 0; i < n; i++)
        if (cabsf(x[i]) > peak) peak = cabsf(x[i]);
    if (peak < 1e-9f) return;

    float scale = 0.5f / peak;
    for (unsigned int i = 0; i < n; i++)
        x[i] *= scale;
}
```

和上一集单载波不同，这里**没有做 AGC**。原因有两个：一是同步器内部会对每个子载波做信道增益估计（S1 导频），输入幅度只要不溢出、不进量化噪声底就够；二是 OFDM 的包络起伏本来就大，一个"快攻慢放"的时变 AGC 会在帧内改变幅度，反而破坏子载波间的幅度关系。**整段一次性的峰值归一化**对块状传输是更安全的选择。

</br>

#### 8）接收端：为什么要过两遍

`ofdm_phy_rx_process()` 对同一段样点跑两遍同步：

```c
/* 2. 第一遍：发现文件头 */
ctx.mode = 0;
run_flexframesync(fs, rx_samples, rx_n);
printf("[RX] Pass 1: %u frames detected, header %s.\n",
       ctx.total_frames, ctx.got_header ? "OK" : "MISSING");

/* 3. 分配重组缓冲 */
ctx.file_buf    = calloc(ctx.file_size, 1);
ctx.chunk_flags = calloc(ctx.num_chunks, 1);

/* 4. 第二遍：收数据帧 */
ofdmflexframesync_reset(fs);
ctx.mode = 1;
run_flexframesync(fs, rx_samples, rx_n);
```

看起来是"多花一倍 CPU"，但换来两个结构性好处：

1. **不用预知文件大小**。重组缓冲必须按 `file_size` 分配，而 `file_size` 只能从文件头帧里解出来。一遍边收边重组的话，数据帧可能比头帧先到（多径、丢包、或 RX 启动晚），到时候没有缓冲区可用。两遍彻底解耦了这个依赖，这也是 `-R` 两机模式能工作的前提（RX 端完全不知道对面发的是什么文件）。
2. **数据帧早于头帧也能救回来**。第二遍是拿整段样点重新同步，头帧在哪个位置已经不重要了。

代价是 CPU：同一段 694 万样点要走两遍同步器，这也是 `real 18.76s` 里最主要的开销。真要做实时流式，得改成单遍 + 动态扩容，或者让 TX 在文件头帧之后插一段足够长的保护间隔。

</br>

#### 9）接收端：逐帧回调、chunk 去重、文件级 CRC

同步器每解出一帧就回调一次，回调里只做两件事——按 `type` 分流、按 `chunk_idx` 去重：

```c
static int ofdm_rx_callback(unsigned char *header, int header_valid,
                            unsigned char *payload, unsigned int payload_len,
                            int payload_valid, framesyncstats_s stats, void *userdata)
{
    ofdm_rx_ctx *c = (ofdm_rx_ctx *)userdata;
    c->total_frames++;
    if (!header_valid)
        return 0;                                 /* 头都没过 CRC，整帧丢掉 */

    unsigned char type = header[0];

    if (c->mode == 0) {
        /* 第一遍：找文件头帧 */
        if (type == FRAME_TYPE_HEADER && payload_valid &&
            payload_len >= HEADER_PAYLOAD_LEN && !c->got_header) {
            memcpy(&c->file_size,  payload + 0, 8);
            memcpy(&c->file_crc,   payload + 8, 4);
            memcpy(&c->num_chunks, payload + 12, 4);
            c->got_header = 1;
        }
    } else {
        /* 第二遍：收数据帧。逐帧重同步 + chunk 去重。 */
        if (type == FRAME_TYPE_DATA && payload_valid &&
            payload_len == PAYLOAD_CHUNK_SIZE) {
            uint32_t chunk_idx = 0;
            memcpy(&chunk_idx, header + 2, 4);

            if (chunk_idx < c->num_chunks && !c->chunk_flags[chunk_idx]) {
                uint64_t off = (uint64_t)chunk_idx * PAYLOAD_CHUNK_SIZE;
                uint64_t remaining = c->file_size > off ? c->file_size - off : 0;
                unsigned int cur = remaining < PAYLOAD_CHUNK_SIZE
                                   ? (unsigned int)remaining : PAYLOAD_CHUNK_SIZE;
                memcpy(c->file_buf + off, payload, cur);
                c->chunk_flags[chunk_idx] = 1;
                c->decoded_chunks++;
            }
        }
    }
    return 0;
}
```

两个细节：`header_valid` 是 liquid 对**用户头**的 CRC 校验结果，`payload_valid` 是**净荷**的（Hamming 纠错 + CRC32）校验结果，两者都过才用；`chunk_flags[]` 让重复副本自动变成"第二份不写"。这也解释了为什么 RX 会看到 1816 个数据帧却只写 908 次。

最后一道关是文件级 CRC：

```c
if (ctx.decoded_chunks == ctx.num_chunks) {
    uint32_t crc_rx = crc_generate_key(LIQUID_CRC_32, ctx.file_buf, (unsigned int)ctx.file_size);
    if (crc_rx == ctx.file_crc) {
        FILE *fout = fopen(out_filename, "wb");
        fwrite(ctx.file_buf, 1, ctx.file_size, fout);
        fclose(fout);
        success = 1;
    }
}
```

**"chunk 收齐"只是必要条件，"文件 CRC 一致"才是最终判据。** 每一帧自带 CRC32，理论上收齐就不会错，但如果出现"chunk_idx 对、内容错"的极端情况（比如 CRC 碰撞，或者头帧里的 `num_chunks` 本身就解错了），只有文件级 CRC 能兜住。

![][p10]

</br>

#### 10）性能账：射频 0.5 秒，CPU 18.8 秒

把实测数字摆在一起看，这条链路的瓶颈在哪儿一目了然：

| 项 | 数值 | 说明 |
|---|---|---|
| 空口时长 | 0.4946 s | `4,945,600 / 10 MSPS` |
| 单副本净荷速率 | 128 B / 272 µs = **470 kB/s** | 已含前导、头部、tail、间隙开销 |
| 重复 2 次后净荷速率 | **235 kB/s** ≈ 1.88 Mbit/s | 实测 `116,108 B / 0.4946 s = 234.8 kB/s`，吻合 |
| 频谱效率 | 1.88 Mbit / 8 MHz ≈ **0.24 bit/s/Hz** | 模拟带宽 8 MHz；数据子载波实际占用约 ±3.4 MHz |
| 板载解码耗时 | **18.8 s** | 两遍同步 × 694 万样点，`user 18.25s` |
| 解码/空口时间比 | **≈ 38×** | 远慢于实时，只能"先采后算" |

**射频不是瓶颈，CPU 才是。** 18.8 秒里绝大部分时间花在两次 `ofdmflexframesync_execute()` 上：694 万样点、每 80 个样点一次 64 点 FFT、每个符号都要做 S0 相关和信道估计，Cortex-A9 @666 MHz 跑两遍就是这个量级。这也决定了本集的工作方式是"采集 → 离线解码"，而不是实时流式。

如果想提速，按性价比排序有三条路：**(1)** 只跑一遍同步（用 TX 端保证头帧在前 + 足够长的保护间隔）；**(2)** 加大每帧净荷（512 B/帧时，前导+头部开销从 32% 降到约 11%，帧数减少 4 倍）；**(3)** 用 NEON 手写相关与 FFT。三条都属于下一步的事，本集的目标是先把"能无损传完文件"这件事钉死。

</br>

### 5. 结语

随着终端里滚出 `Pass 2: 2549 frames detected, decoded 908 / 908 chunks` 和 `File CRC OK`，把板上的 `recovered.png` 拉回来 `md5sum` 一比对——一个字节都没变。116 KB 的文件完整走完了"分片 → OFDM 组帧 → 射频 → 同步 → 解调 → 重组"的闭环，而且是在 Pluto 的 ARM 上现算的。

回看这一集真正学到的东西，不是"调通了 liquid 的 API"，而是三件可以迁移的判断：

- **块状同步 vs 连续跟踪**：把同步做进每一帧，换来"丢一帧只丢一帧"的鲁棒性，代价是每帧 32% 的前导/头部开销。这是 OFDM 这类块传输系统的核心工程取舍。
- **冗余要放对位置**：同样的 2 倍冗余，"相邻重复"只能抗随机误码，"错开一轮"能抗持续几百毫秒的突发。冗余的排布方式比冗余的量更重要。
- **参数是试出来的，不是抄来的**：RX 增益这一栏的实测（20 dB 无头帧 / 30 dB 缺 416 个 chunk / 40 dB 与 50 dB 全通过）说明，OFDM 高 PAPR 下的"甜区"只能靠扫。任何"默认 40 dB 应该没问题"的说法都得拿数据说话。

和上一集放在一起对比，两条链路的性格差别很清楚：

| 维度 | 上一集：单载波 DQPSK | 本集：OFDM |
|---|---|---|
| 同步方式 | 前导相关 + symsync 定时环，连续跟踪 | 每帧 S0/S1 独立估计定时/频偏/信道 |
| 频偏容忍 | 靠差分抵消，容忍度低 | 子载波间隔 156.25 kHz，S0 估计覆盖 ±78 kHz |
| 多径/ISI | RRC 成形缓解，无显式均衡 | CP 吸收时延扩展 + 导频信道均衡 |
| 每包开销 | 64 个 QPSK 符号前导 + 16 保护零（共 1116 符号） | 3 个前导 + 7 个头部符号，占 31 个 OFDM 符号的 32% |
| 单帧净荷 | 128 B（DQPSK，1036 个调制符号） | 128 B（21 个 OFDM 符号） |
| 实测结果 | 908/908，File CRC OK | 908/908，File CRC OK |

**当前验证边界（写清楚，免得误导）**：交叉编译通过、宿主机仿真（含随机相偏/早启动/中途突发/噪声+频偏）通过、单板真机自环 908/908 且文件 md5 一致；**两机空口、大于 100 MB 的文件、实时流式解码都还没有实测**。自环成功某种意义上占了"TX/RX 共用一颗 LO、频偏≈0"的便宜，真实空口的频偏、多径、AGC 动态范围都会更难，两机实测是下一步必做的事。

从第一行 `printf("hello pluto sdr")`，到 FM 收音机、频谱分析仪、ADS-B 解码、蓝牙广播收发、QPSK 文件自收发，再到这一集的 **OFDM 文件传输**——一条现代数字通信链路的每一层（信源分片、信道编码、交织、多载波调制、同步、均衡、校验重组）都已经在 Pluto 上亲手走了一遍。

底盘既定，视野向外。下一课，我们将从单天线通信迈向空间阵列，正式开启：

《基于双通道相位差的 TDOA/AOA 空间无线电测向》

</br>

### 参考链接

[[1]. 上一集：PlutoSDR 嵌入式脱机 QPSK 文件自收发全链路构建][#1]    
[[2]. Analog Devices —— libiio `ad9361-iiostream.c`（本集主程序的底层框架来源）][#2]    
[[3]. liquid-dsp —— `ofdmflexframegen`（组帧：S0/S1 前导、头部、净荷、帧间隙）][#3]    
[[4]. liquid-dsp —— `ofdmflexframesync`（同步：突发检测、定时、频偏、信道估计、FEC 解码）][#4]    
[[5]. 本工程 `doc/design.md`（帧结构、重复策略、两机频偏分析与仿真/真机映射表）][#5]    

[#1]:../10-PlutoSDR嵌入式脱机QPSK文件自收发全链路构建/readme.md
[#2]:https://github.com/analogdevicesinc/libiio/blob/v0.25/examples/ad9361-iiostream.c
[#3]:https://liquidsdr.org/doc/ofdmflexframegen/
[#4]:https://liquidsdr.org/doc/ofdmflexframesync/
[#5]:../00-examples/09-pluto_ofdm_file_self_transfer/doc/design.md

[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/pluto_ofdm_data_flowgraphy.png                          
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ofdm_file_code_flowgraph.png   
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ofdm_file_subcarriers.png    
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/plutosdr_ofdm_user_header.png    
[p11]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/pluto_ofdm_as_car.jpeg      
[p12]:https://tuchuang.beautifulzzzz.com:3000/?path=202610/pluto_ofdm_arch.jpeg

