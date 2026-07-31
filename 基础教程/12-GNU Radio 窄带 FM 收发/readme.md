### 1、前言

在没有硬件 SDR（如 HackRF、USRP 或 PlutoSDR）的情况下，我们可以利用 GNU Radio 提供的 **ZMQ（ZeroMQ）** 跨进程通信模块，在单机上模拟射频信号的发送与接收。

今天通过搭建**窄带 FM（NBFM）发射端**与**接收端**流程图，演示经典的 NBFM 调制解调全流程，包括语音采集、亚音（PL Tone）叠加、预加重、IQ 上采样、静噪（Squelch）控制以及 ZMQ 虚拟射频信道传输。

</br>

### 2、NBFM 发射端（Transmitter）

发射端负责将音频信号调制为 NBFM IQ 信号，并插值提升采样率后通过 ZMQ 发布。

![][p2]

#### 流程图结构与核心配置：

- 1）**音频采集与带通滤波**：
    * **`Audio Source`**：采样率 `samp_rate = 48k`。
    * **`Band Pass Filter`**：通带 `[300Hz, 5kHz]`，滤除直流分量及人声频带之外的高频噪声。
    * **`Multiply Const`**：音量增益调节（`Audio gain`）。
- 2）**CTCSS/PL 亚音叠加**：
    * **`Signal Source`** + **`QT GUI Chooser`**：产生亚音频率（如 $67.0\text{Hz}$），与音频信号相加（`Add`）后送入调制器。
- 3）**FM 调制**：
    * **`NBFM Transmit`**：
        * `Audio Rate`: $48\text{kHz}$
        * `Quadrature Rate`: $192\text{kHz}$（完成 4 倍内部上采样）
        * `Tau`: $75\mu\text{s}$（标准预加重常数）
        * `Max Deviation`: $5\text{kHz}$（最大频偏）
- 4）**低通滤波与 ZMQ 射频输出**：
    * **`Low Pass Filter`**：截止频率 $5\text{kHz}$，过滤调制后的带外杂散。
    * **`Repeat`**：插值系数为 $3$（将采样率从 $192\text{kHz}$ 进一步提升至 $192\text{k} \times 3 = 576\text{kHz}$），模拟高采样率的射频前端。
    * **`ZMQ PUB Sink`**：绑定端口 `tcp://127.0.0.1:49203`，将二进制 IQ 流广播发布。

</br>

### 3、NBFM 接收端（Receiver）

接收端通过 ZMQ 订阅信号，经过抽取滤波、静噪门限和 NBFM 解调后恢复原始音频。

![][p1]

#### 流程图结构与核心配置：

- 1）**ZMQ 信号接收与频谱观察**：
    * **`ZMQ SUB Source`**：连接 `tcp://127.0.0.1:49203` 接收模拟射频 IQ 流（采样率 $576\text{kHz}$）。
    * **`QT GUI Waterfall Sink`**：实时观察接收信号的时频瀑布图。
- 2）**信道选择与抽取滤波**：
    * **`Band-pass Filter Taps`** + **`FFT Filter`**：
        * 通带范围：`[-3kHz, 3kHz]`
        * `Decimation`: $3$（3 抽取，将采样率从 $576\text{kHz}$ 降回 $192\text{kHz}$）。
- 3）**静噪控制（Squelch）**：
    * **`Simple Squelch`**：设置静噪门限（如 `-50dB`），当无载波或信号过弱时自动无声，避免收到刺耳的无信号底噪。
- 4）**NBFM 解调与音频播放**：
    * **`NBFM Receive`**：
        * `Quadrature Rate`: $192\text{kHz}$
        * `Audio Rate`: $48\text{kHz}$
        * `Tau`: $75\mu\text{s}$（去加重）
    * **`Multiply Const`** + **`Audio Sink`**：音量控制与声卡播放（采样率 $48\text{kHz}$）。

</br>

### 4、关键系统参数映射

| 阶段 | 采样率 / 频宽 | 对应模块 / 作用 |
| --- | --- | --- |
| **基带音频** | $48\text{kHz}$ | `Audio Source` / `Audio Sink` |
| **正交调制速率** | $192\text{kHz}$ | `NBFM Transmit` / `NBFM Receive`（含 4 倍内插/抽取） |
| **模拟射频速率** | $576\text{kHz}$ | `Repeat` ($\times 3$) / `FFT Filter Decimation` ($\div 3$) |
| **网络传输层** | ZMQ TCP Socket | `ZMQ PUB Sink` (Bind) $\rightarrow$ `ZMQ SUB Source` (Connect) |

</br>

### 5、最终效果

![][p3]

</br>

### 参考链接

* [GNU Radio Wiki - NBFM Transmit](https://wiki.gnuradio.org/index.php/NBFM_Transmit)
* [GNU Radio Wiki - NBFM Receive](https://wiki.gnuradio.org/index.php/NBFM_Receive)
* [GNU Radio Wiki - ZeroMQ Blocks](https://wiki.gnuradio.org/index.php/ZMQ_PUB_Sink)    



[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_fm_receiver_grc.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_fm_transmitter_grc.png
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_fm_trx_grc_show.png

