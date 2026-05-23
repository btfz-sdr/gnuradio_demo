### 1. 开场

上一集我们搞定了交叉编译系统。很多同学肯定在想，既然 Pluto 带有 ARM 芯片，为什么不用它直接解调？本集我们强度更进一步：**抛开 GNU Radio，用纯 C 语言编写一个完整的广播级 FM 接收机 DSP 内核，直接烧录进 PlutoSDR 脱机运行！**

板子将独立完成 **2.4M 采样率拉取 $\rightarrow$ IIR 滤波 $\rightarrow$ 降采样 $\rightarrow$ 反正切鉴频 $\rightarrow$ 去加重隔直** 全套解调算法，最终通过 UDP 将 48kHz 音频流送回电脑播放。你的电脑不参与任何解调，只当喇叭。大脑清空，直接开干！

![][p2]

</br>

### 2. 启动环境

学完第一第二课，大家应该有如下启动开发环境的命令：

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
│   ├── hello_plutosdr.c
│   └── makefile
├── 02-ad9361-iiostream
│   ├── ad9361-iiostream.c
│   └── makefile
├── 03-pluto_fm_radio   <-- 这里
│   ├── makefile
│   ├── pluto_fm_radio.c
└── toolchain           <-- 上一课已经介绍如何安装
    ├── gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
    └── libiio
```

b.编译与运行：

注意：首先需要用 `ifconfig` 查看当前电脑的 ip，替换 `pluto_fm_radio.c` 中的 `#define DEST_IP "192.168.1.119"`     

```
# 清空编译过程和目标文件
➜  03-pluto_fm_radio make clean
rm -rf pluto_fm_radio

# 编译（调用交叉编译工具链）
➜  03-pluto_fm_radio make build
/home/btfz/Desktop/PLUTOSDR/iio/03-pluto_fm_radio/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc pluto_fm_radio.c -o pluto_fm_radio -I/home/btfz/Desktop/PLUTOSDR/iio/03-pluto_fm_radio/../toolchain/libiio/usr/include -O3 -L/home/btfz/Desktop/PLUTOSDR/iio/03-pluto_fm_radio/../toolchain/libiio/usr/lib/arm-linux-gnueabihf -L/home/btfz/Desktop/PLUTOSDR/iio/03-pluto_fm_radio/../toolchain/libiio/lib/arm-linux-gnueabihf -liio -lm -Wl,--allow-shlib-undefined
/home/btfz/Desktop/PLUTOSDR/iio/03-pluto_fm_radio/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-strip pluto_fm_radio

# scp 将生成的产物复制到 pluto sdr linux 的 home 目录下
# ssh 登陆到 pluto sdr linux 环境，赋予 pluto_fm_radio 可执行权限，并执行
# 最终可以看到我们交叉编译的产物正确执行
➜  03-pluto_fm_radio make run
scp -O pluto_fm_radio root@pluto.local:~/
root@pluto.local's password: 
pluto_fm_radio                                                                                                                                              100% 9840     6.5MB/s   00:00    
ssh -t root@pluto.local "chmod +x ~/pluto_fm_radio; ./pluto_fm_radio"
root@pluto.local's password: 
* Acquiring IIO context
* Acquiring AD9361 streaming devices
* Configuring AD9361 for streaming
* Acquiring AD9361 phy channel 0
* Acquiring AD9361 RX lo channel
* Initializing AD9361 IIO streaming channels
* Enabling IIO streaming channels
* Creating non-cyclic IIO buffers with 256 MiS

>>> FM Demodulator Started (Radio Grade) <<<
Target Freq: 93.0 MHz | Out Audio: 48 kHz S16LE (Bit-Perfect)
```

然后再开另一个终端，在其中运行（`192.168.1.119` 要换成你电脑的 ip）：

```
ffplay -nodisp -f s16le -ar 48000 -ch_layout mono -framedrop -async 1 -fflags nobuffer udp://192.168.1.119:1234
ffplay -f s16le -ar 48000 -ch_layout mono -framedrop -async 1 -fflags nobuffer -vf "showspectrum=s=800x400:mode=combined:color=rainbow" udp://192.168.1.119:1234
```

然后你就能在自己电脑上听到 plutosdr 收到的 FM93 的广播了：

![][p1]

</br>

### 4. 代码介绍

这个代码是在上一课 libiio 收发框架基础上改造来的，为了方便学习对比，我将主要差别的逻辑都放在 `// FM 相关的逻辑` 后面，其中 main 中前部分和收发框架也类似，后面收到数据后的处理逻辑不一样：

#### 1）在之前 libiio 收发框架上微调

```
int main(int argc, char **argv) {
    struct iio_device *rx;
    struct stream_cfg rxcfg;
    struct sockaddr_in servaddr;

    // Listen to ctrl+c and IIO_ENSURE
    signal(SIGINT, handle_sig);

    /* ---- 1. 网络 UDP 初始化 ---- */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    IIO_ENSURE(sockfd >= 0 && "Failed to create socket");
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(DEST_PORT);
    servaddr.sin_addr.s_addr = inet_addr(DEST_IP);

    /* ---- 2. 配置流参数 ---- */
    rxcfg.lo_hz   = FM_LO_FREQ;
    rxcfg.fs_hz   = RX_SAMPLING_RATE;
    rxcfg.bw_hz   = RX_BANDWIDTH;
    rxcfg.rfport  = "A_BALANCED"; 

    printf("* Acquiring IIO context\n");
    if (argc == 1) {
        IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
    }
    else if (argc == 2) {
        IIO_ENSURE((ctx = iio_create_context_from_uri(argv[1])) && "No context");
    }
    IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

    printf("* Acquiring AD9361 streaming devices\n");
    IIO_ENSURE(get_ad9361_stream_dev(RX, &rx) && "No RX dev found");
    
    printf("* Configuring AD9361 for streaming\n");
    IIO_ENSURE(cfg_ad9361_streaming_ch(&rxcfg, RX, 0) && "RX port configuration failed");
    
    printf("* Initializing AD9361 IIO streaming channels\n");
    IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 0, &rx0_i) && "RX chan I not found");
    IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 1, &rx0_q) && "RX chan Q not found");

    printf("* Enabling IIO streaming channels\n");
    iio_channel_enable(rx0_i);
    iio_channel_enable(rx0_q);

    printf("* Creating non-cyclic IIO buffers with 256 MiS\n");
    rxbuf = iio_device_create_buffer(rx, 256 * 1024, false);
    if (!rxbuf) {
        perror("Could not create RX buffer");
        _shutdown();
    }
```

**注意：** 在上面初始化逻辑中，新增了网络 UDP 初始化逻辑，用于将 plutosdr 中处理好的音频数据通过 UDP 传给上位机来播放（因为我们这个 plutosdr 板子上没有音频输出能力，否则就可以一把梭哈了）。

</br>

然后在 while 的前半部分的数据读取，也是可 libiio 收发框架类似：

```
    /* ---- 4. 主循环处理 ---- */
    while (!stop) {
        // Refill RX buffer
        ssize_t nbytes_rx = iio_buffer_refill(rxbuf);
        if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); _shutdown(); }

        // READ: Get pointers to RX buf and read IQ from RX buf port 0
        ptrdiff_t p_inc = iio_buffer_step(rxbuf);
        char *p_end     = iio_buffer_end(rxbuf);
        char *p_dat;

        int out_idx = 0;

        for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {
            
            // 提取12位ADC数据并移位完成符号位对齐
            float i_val = (float)(((int16_t*)p_dat)[0] << 4);
            float q_val = (float)(((int16_t*)p_dat)[1] << 4);
```

</br>

#### 2）抗混叠基带低通滤波器

在上面获取到 `i_val` 和 `q_val` 之后，立马使用两行代码对数据进行抗混叠基带低通滤波：

```
            // 【DSP升级】：抗混叠基带低通滤波器 (IIR 低通，为降采样护航)
            demod_state.lpf_i = LPF_ALPHA * i_val + (1.0f - LPF_ALPHA) * demod_state.lpf_i;
            demod_state.lpf_q = LPF_ALPHA * q_val + (1.0f - LPF_ALPHA) * demod_state.lpf_q;
```

这里用到的低通滤波器，在数字信号处理（DSP）中被称为 一阶 **IIR**（无限冲激响应）低通滤波器，也常常被叫做 指数滑动平均滤波器（**Exponential Moving Average, EMA**）。

它的核心物理原理非常优雅，可以用两句话概括：用当前这一时刻的采样值，去平滑（折中）过去累积的历史值，从而滤掉高频突变（噪声），保留低频趋势。

我们将代码中的符号还原为标准的数学递推公式。设 $x[n]$ 是当前输入的原始 IQ 采样，$y[n]$ 是滤波后的输出，$y[n-1]$ 是上一个时刻留存的历史输出，$\alpha$（即代码中的 `LPF_ALPHA`）是一个介于 $0$ 到 $1$ 之间的系数。公式如下：

$$y[n] = \alpha \cdot x[n] + (1 - \alpha) \cdot y[n-1]$$

我们可以把这个公式拆成两个权重的“拔河”：

* **`LPF_ALPHA * i_val`**：这是**新数据**的权重。$\alpha$ 越大，说明你越信任当前天线刚收到的新采样。
* **`(1.0f - LPF_ALPHA) * demod_state.lpf_i`**：这是**历史老数据**的权重。$(1-\alpha)$ 越大，说明滤波器越依赖过去的趋势，记忆越深刻。

最终效果：

* **时域平滑**：高频噪声表现为信号的“急剧突变（毛刺）”。因为历史数据占了 $75\%$ 的权重（$1 - 0.25$），上一次平缓的趋势会像海绵一样，强行把突发的毛刺拉回、拖平，从而滤掉高频。
* **频域抗混叠**：我们的代码接下来要做 `5:1` 降采样（$2.4\text{ MHz} \rightarrow 480\text{ kHz}$）。根据奈奎斯特定律，降采样后只能承载 $\pm240\text{ kHz}$ 内的信号。这几行代码的任务就是在抽样前，提前把 $\pm240\text{ kHz}$ 以外的带外噪声干掉，防止它们“折叠”混叠进有用信号带内。

</br>

#### 3）5:1 抽取

接下来是 5:1 抽取数据：

```
            // 第一级抽取：5:1 (计数器替代取模)
            if (++decim1_cnt >= DECIM_1) {
                decim1_cnt = 0; // 清零计数器

                // 调用抽象解调内核
                float audio_raw = iio_fm_demod_kernel(demod_state.lpf_i, demod_state.lpf_q, &demod_state);
```

其中调用了一个封装函数 `iio_fm_demod_kernel`，**这个函数是 FM（调频）接收机中最核心的 DSP（数字信号处理）解调内核，它的任务是把 PlutoSDR 采集到的 IQ 复信号，还原成我们耳朵能听到的音频信号。**

整个函数分为两个标准阶段：**反正切差分鉴频（提取频率变化）** 和 **去加重滤波（恢复音质）**。

<u>a. 第一阶段：FM 鉴频器（反正切差分）</u>

FM 广播的原理是用音频信号的幅度去控制高频载波的频率。因此，解调的本质就是测量信号瞬时频率的变化。

```
float phase = atan2f(q, i);  # 计算当前采样点的绝对相位
float diff  = phase - state->last_phase; # 瞬时频率是相位对时间的导数（由于采样间隔固定，因此等于其差分）
```

相位解卷绕：`atan2f` 的输出范围在 $[-\pi, \pi]$ 之间。当相位在 $\pi$ 和 $-\pi$ 的边界处来回跳变时（比如从 $179^\circ$ 转到 $-179^\circ$），直接做减法会得到一个接近 $2\pi$ 的巨大错误差分值，听起来就是极尖锐的爆音。这两行判断的作用是把跳变的相位“拉回”到正常的 $[- \pi, \pi]$ 差分范围内，保证音频连续：

```
if (diff >  M_PI) diff -= 2.0f * M_PI;
if (diff < -M_PI) diff += 2.0f * M_PI;
state->last_phase = phase;
```

<u>b. 第二阶段：去加重低通滤波（De-emphasis）</u>

这是广播级 FM 解调特有的步骤，用来恢复声音的正常频响。


```c
state->deemph_state = DEEMPH_ALPHA * diff + (1.0f - DEEMPH_ALPHA) * state->deemph_state;
return state->deemph_state;
```

* **背景**：FM 调制在传输过程中，高频部分极易受到空气中的噪声干扰。为了抗干扰，电台发射端会提前把音频的高频部分人为放大（**预加重 Pre-emphasis**）。
* **原理**：接收端在解调出音频（`diff`）后，必须使用一阶 IIR 低通滤波器进行大小相等的反向衰减（**去加重**），把高频声音压回去，同时也把高频噪声一起抹掉。
* **参数**：`DEEMPH_ALPHA`（0.0408f）就是基于中国标准的 $50\,\mu\text{s}$ 去加重时间常数、在 $480\text{ kHz}$ 速率下精确计算出的低通截止频率系数，它能让广播听起来不刺耳、更浑厚。

</br>

#### 4）10:1 抽取

再接下来是 10:1 抽取：

```
                // 第二级抽取：10:1
                if (++decim2_cnt >= DECIM_2) {
                    decim2_cnt = 0;

                    // 调用抽象隔直内核并存入发送缓冲区
                    if (out_idx < AUDIO_BUF_MAX) {
                        audio_out[out_idx++] = iio_audio_dc_block(audio_raw, &demod_state);
                    }
                }
```

其中调用了一个封装函数 `iio_audio_dc_block`，**这是音频处理的最后一道关卡，它的核心功能是滤除直流分量（隔直）并进行音频放大与安全截断，确保输出的 PCM 数据能安全地送入声卡播放。**

整个函数分为两个部分：**一阶 IIR 高通滤波器** 和 **幅值映射与限幅控制**。

<u>a. 第一阶段：一阶 IIR 高通滤波器（隔直）</u>

在解调输出的音频信号中，往往裹挟着由于硬件不平衡或算法残余导致的固定直流偏置（DC Offset）。如果不滤除它，喇叭的纸盆会被持续推向一侧，导致声音极其沉闷，甚至引起严重失真。

```c
float dc_out = input - state->dc_x_prev + DC_R * state->dc_y_prev;
state->dc_x_prev = input;
state->dc_y_prev = dc_out;
```

* **数学本质**：这是标准的一阶 IIR 高通差分方程，其时域表达式为：
    $$y[n] = x[n] - x[n-1] + R \cdot y[n-1]$$
    * `input - state->dc_x_prev` ($x[n] - x[n-1]$) 是一个**微分器**。如果输入是死板的直流电平（前后两点完全相同），相减后直接归零，从而将直流生硬地卡死。
    * `+ DC_R * state->dc_y_prev` ($R \cdot y[n-1]$) 是**反馈项**。`DC_R`（代码中为 `0.9948f`）是极点半径。它的作用是让滤波器具备“微调能力”，允许有用的交流音频信号（如低音、人声）平滑地通过。在 $48\text{ kHz}$ 采样率下，这个系数刚好把切断频率定在约 $40\text{ Hz}$，既除尽了直流，又保护了低音。


<u>a. 第二阶段：音频增益放大与严格截断（Saturating Limiter）</u>

经过前面算法的处理，`dc_out` 此时是一个非常微小的浮点数（通常在 $\pm0.01$ 范围内徘徊）。我们要把它转换成声卡能直接播放的 $16\text{位}$ 有符号整数（`int16_t`，范围 $-32768$ 到 $32767$）。


```c
# 音量放大：乘以一个巨大的系数 `800000.0f`，把微弱的解调信号拉升到正常的音量频带。
float am_val = dc_out * 800000.0f; 
```

接下来是防爆音硬限幅：这是经典的饱和截断（Clamping）。如果遇到强电台，放大后的 am_val 可能会冲过 $32767$（比如变成 $32800$）。
 - 如果直接用 C 语言的 `(int16_t)` 强转，数据会发生**数据溢出翻转（Wrap-around）**，$32800$ 会诡异地变成 $-32736$。这在听觉上表现为瞬间把极强的正电压跳变成负电压，产生极其刺耳、像金属碎裂一样的**数字爆音**。
- 这两行 `if` 相当于给音频修了两道铁栅栏，一旦过界就死死卡在最大值，从而确保偶尔过载时也只是轻微的模拟削波（Saturate），绝不产生数字硬溢出的恶性毛刺音。

```c
if (am_val >  32767.0f) am_val =  32767.0f;
if (am_val < -32768.0f) am_val = -32768.0f;
return (int16_t)am_val;
```

</br>

### 5. 结语

伴随清晰的广播声，嵌入式脱机实战圆满完成！Pluto 独自在板载 Linux 中完成了 **2.4MSps 射频吞吐、IIR 抗混叠、5:1 抽取、反正切鉴频、去加重降噪、10:1 抽取与饱和防爆音隔直** 的全套 DSP 运算。

你成功将算法下沉到了边缘端硬件。这也是你第一个 PlutoSDR 嵌入式工程实战的胜利！恭喜！


[#0]:https://gemini.google.com/app/4182c5b121b72e94?utm_source=app_launcher&utm_medium=owned&utm_campaign=base_all    

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/pluto_fm_make_show.gif   
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/plutosdr_fm_in_arm.png    

