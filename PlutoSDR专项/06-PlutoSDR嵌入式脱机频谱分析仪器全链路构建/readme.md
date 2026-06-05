### 1. 开场

上一集我们用纯 C 语言在 PlutoSDR 的板载 ARM 芯片上跑通了广播级 FM 接收机。看到音频流通过 UDP 丝滑送回电脑的那一刻，相信大家已经感受到了“算法下沉边缘端”的巨大威力。

既然解调能做，那射频领域最核心的“天眼”——**频谱分析仪**，我们自然也不能放过！

本集我们的硬核强度再度升级：**我们不仅要继续抛弃 GNU Radio，还要首次在 Pluto 上引入动态命令行参数解析，并直接在板载单片机/ARM 侧死磕数字信号处理（DSP）的圣杯——快速傅里叶变换（FFT）！**

板子将独立吞吐 **10MHz 超高采样率 $\rightarrow$ 动态自适应硬件带宽 $\rightarrow$ 汉宁窗（Hanning）平滑时域信号 $\rightarrow$ 片上 1024 点高级 kissFFT 变换 $\rightarrow$ 功率谱密度（dB）换算与 FFT-Shift 移频 $\rightarrow$ 多帧片上平滑过滤** 全套边缘计算流程。

最终，Pluto 吐出的不再是庞大的原始 IQ 字节流，而是处理得干干净净的、只有 1024 个浮点数的频域功率数据。你的电脑只负责画图，核心计算全在芯片上！大脑清空，直接开干！

![][p4]

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
├── 02-ad9361-iiostream
├── 03-pluto_fm_radio   
├── 04-pluto_spectrum_analyzer   <-- 这里
│   ├── kiss_fft.c
│   ├── kiss_fft.h
│   ├── makefile
│   ├── pluto_spectrum_analyzer.c
│   └── pluto_spec_viewer.py
└── toolchain           <-- 上一课已经介绍如何安装
    ├── gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
    └── libiio
```

b.编译与运行：

注意：首先需要用 `ifconfig` 查看当前电脑的 ip，替换 `makefile` 中的 `IP     ?= 192.168.1.119`     

```
# 清空编译过程和目标文件
➜  04-pluto_spectrum_analyzer make clean
rm -rf pluto_spectrum_analyzer

# 编译（调用交叉编译工具链）
➜  04-pluto_spectrum_analyzer make build
# 联合编译当前目录下的 kiss_fft 基础组件
/home/gnuradio/PLUTOSDR/iio/04-pluto_spectrum_analyzer/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc pluto_spectrum_analyzer.c kiss_fft.c -o pluto_spectrum_analyzer -I/home/gnuradio/PLUTOSDR/iio/04-pluto_spectrum_analyzer/../toolchain/libiio/usr/include -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -L/home/gnuradio/PLUTOSDR/iio/04-pluto_spectrum_analyzer/../toolchain/libiio/usr/lib/arm-linux-gnueabihf -L/home/gnuradio/PLUTOSDR/iio/04-pluto_spectrum_analyzer/../toolchain/libiio/lib/arm-linux-gnueabihf -liio -lm -Wl,--allow-shlib-undefined
/home/gnuradio/PLUTOSDR/iio/04-pluto_spectrum_analyzer/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-strip pluto_spectrum_analyzer

# scp 将生成的产物复制到 pluto sdr linux 的 home 目录下
# ssh 登陆到 pluto sdr linux 环境，赋予 pluto_spectrum_analyzer 可执行权限，并执行
# 最终可以看到我们交叉编译的产物正确执行
➜  04-pluto_spectrum_analyzer make run
scp -O pluto_spectrum_analyzer root@pluto.local:~/
root@pluto.local's password: 
pluto_spectrum_analyzer                                                                                                                                     100%   14KB   3.9MB/s   00:00    
------------------------------------------------
Tips: On your PC, use Python or GNU Radio to receive
1024 float points via UDP port 1234 to plot waterfall.
python pluto_spec_viewer.py -s 10 -i "192.168.1.119" -p 1234
------------------------------------------------
ssh -t root@pluto.local "chmod +x ~/pluto_spectrum_analyzer; ./pluto_spectrum_analyzer -f 93 -s 10 -i 192.168.1.119 -p 1234"
root@pluto.local's password: 
* Acquiring IIO context
* Acquiring AD9361 streaming devices
* Configuring AD9361 for streaming
* Acquiring AD9361 phy channel 0
* Acquiring AD9361 RX lo channel
* Initializing AD9361 IIO streaming channels
* Enabling IIO streaming channels
* Creating non-cyclic IIO buffers with 256 MiS

>>> Edge-Computing Spectrum Analyzer Started <<<
Target Dev  : default
Target PC IP: 192.168.1.119:1234
Center Freq : 93.00 MHz
Sample Rate : 10.00 MHz (Hardware BW: 8.00 MHz)
Resolution  : 1024 points
```

然后再开另一个终端，在其中运行（`192.168.1.119` 要换成你电脑的 ip）（注意下面命令是自动生成的，参考上面 log 中 `Tips: On your PC` 的说明），：

```
python pluto_spec_viewer.py -s 10 -i "192.168.1.119" -p 1234
```

然后你就能在自己电脑上看到频谱显示了：

<div style="display: flex; align-items: flex-start; gap: 40px;">
<div>

<img src="https://tuchuang.beautifulzzzz.com:3000/?path=202605/pluto_spec_make_show.gif" alt="" style="max-height:518px;width:auto">   

</div>
<div>

![][p1]    

</div>
</div>

</br>

### 4. 代码介绍

这个代码是在《[第四集：断开脐带 —— 交叉编译与 PlutoSDR 嵌入式脱机运行实战][#1]》 libiio 收发框架基础上改造来的，为了方便学习对比，我将主要差别的逻辑都放在 `// FM 相关的逻辑` 后面，其中 main 中前部分和收发框架也类似，后面收到数据后的处理逻辑不一样：

#### 1）首次支持动态参数传入

为了达到：`pluto_spectrum_analyzer -f $(FREQ) -s $(SAMPLE) -i $(IP) -p $(PORT)` 运行时带上不同的参数，我们这里引入了动态参数支持能力：

```
#include <getopt.h>  /* ---- 【全新引入】Linux 标准命令行参数解析库 ---- */

/* 打印帮助菜单 */
void print_help(char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d <device>  plutosdr 设备 url (默认: default)\n");
    printf("  -f <freq>    中心频率 Center Frequency in MHz (默认: 93.0)\n");
    printf("  -s <rate>    采样率 Sampling Rate in MHz (默认: 10.0)\n");
    printf("  -i <ip>      目标 PC IP 地址 (默认: 192.168.1.119)\n");
    printf("  -p <port>    目标 PC PORT 地址 (默认: 1234)\n");
    printf("  -h           显示当前帮助菜单\n");
}

int main(int argc, char **argv) {
    struct iio_device *rx;
    struct stream_cfg rxcfg;
    struct sockaddr_in servaddr;

    // 默认变量初始化
    char param_dev[32] = "default";
    double param_lo_mhz = DEFAULT_LO_FREQ_MHZ;
    double param_samp_mhz = DEFAULT_SAMP_RATE_MHZ;
    char param_ip[32] = DEFAULT_DEST_IP;
    int param_port = DEFAULT_DEST_PORT;

    /* ---- 【核心升级】：使用 getopt 动态解析传入参数 ---- */
    int opt;
    while ((opt = getopt(argc, argv, "d:f:s:i:p:h")) != -1) {
        switch (opt) {
            case 'd':
                strncpy(param_dev, optarg, sizeof(param_dev) - 1);
                break;
            case 'f':
                param_lo_mhz = atof(optarg);
                break;
            case 's':
                param_samp_mhz = atof(optarg);
                break;
            case 'i':
                strncpy(param_ip, optarg, sizeof(param_ip) - 1);
                break;
            case 'p':
                param_port = atoi(optarg);
                break;
            case 'h':
            default:
                print_help(argv[0]);
                return 0;
        }
    }
```

这样就能像专业工具一样，支持 `pluto_spectrum_analyzer -h` 打印帮助菜单，以及支持多种不同参数输入了。

</br>

#### 2）while 循环读取前的微调（各种初始化）

```
    // Listen to ctrl+c and IIO_ENSURE
    signal(SIGINT, handle_sig);

    /* ---- 1. 网络 UDP 初始化 ---- */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    IIO_ENSURE(sockfd >= 0 && "Failed to create socket");
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(param_port);
    servaddr.sin_addr.s_addr = inet_addr(param_ip);

    /* ---- 2. 配置动态流参数 ---- */
    rxcfg.lo_hz   = (long long)(param_lo_mhz * 1000000.0 + .5);
    rxcfg.fs_hz   = (long long)(param_samp_mhz * 1000000.0 + .5);
    // 【硬核自适应】：模拟硬件低通滤波器带宽设为采样率的 80%，完美解决抛物线“出界”和混叠问题
    rxcfg.bw_hz   = (long long)(param_samp_mhz * 0.8 * 1000000.0 + .5);
    rxcfg.rfport  = "A_BALANCED"; 

    // 注意：如果有 URI 环境参数传入，getopt 之后的剩余参数可以通过 optind 获取
    printf("* Acquiring IIO context\n");
    if (memcmp(param_dev,"default", 8) == 0) {
        IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
    } else {
        IIO_ENSURE((ctx = iio_create_context_from_uri(param_dev)) && "No context");
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

    /* ---- 3. 初始化频谱仪算法组件与内存 ---- */
    init_hanning_window();
    
    // 分配 kissFFT 句柄（0表示不执行反FFT）
    kiss_fft_cfg fft_plan = kiss_fft_alloc(FFT_POINTS, 0, NULL, NULL);
    IIO_ENSURE(fft_plan && "FFT allocation failed");
    
    kiss_fft_cpx *fft_in  = malloc(FFT_POINTS * sizeof(kiss_fft_cpx));
    kiss_fft_cpx *fft_out = malloc(FFT_POINTS * sizeof(kiss_fft_cpx));
    float *spectrum_avg_buf = calloc(FFT_POINTS, sizeof(float));
    float *spectrum_send_payload = malloc(FFT_POINTS * sizeof(float));
    
    IIO_ENSURE(fft_in && fft_out && spectrum_avg_buf && spectrum_send_payload);

    printf("\n>>> Edge-Computing Spectrum Analyzer Started <<<\n");
    printf("Target Dev  : %s\n", param_dev);
    printf("Target PC IP: %s:%d\n", param_ip, param_port);
    printf("Center Freq : %.2f MHz\n", param_lo_mhz);
    printf("Sample Rate : %.2f MHz (Hardware BW: %.2f MHz)\n", param_samp_mhz, param_samp_mhz * 0.8);
    printf("Resolution  : %d points\n\n", FFT_POINTS);

    int fft_sample_idx = 0;
    int frame_counter = 0;
```

**注意：** 

- a. 在上面初始化逻辑中，新增了网络 UDP 初始化逻辑，用于将 plutosdr 中处理好的频谱数据通过 UDP 传给上位机来显示（因为我们这个 plutosdr 板子上没有显示能力，否则就可以一把梭哈了）。
- b. 配置动态流参数和 IIO 收发框架代码类似，只是我将其改为了根据函数传参动态修改之前写死的参数了（今后我们会用这种方式来做，能让程序更灵活）。
- c. 第 3 部分是本课特有的逻辑，主要用于初始化频谱仪算法组件与内存。


</br>

#### 3）收到 IQ 之后的处理

然后在 while 的前半部分的数据读取，也是可 libiio 收发框架类似，直到 `提取12位ADC数据，对齐符号位并转换为浮点数` 部分之后才是本课的专有逻辑：

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

        for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {
            
            // 提取12位ADC数据，对齐符号位并转换为浮点数
            float i_val = (float)(((int16_t*)p_dat)[0] << 4);
            float q_val = (float)(((int16_t*)p_dat)[1] << 4);

            // 【DSP升级】：加窗处理，平滑灌入 FFT 输入缓冲区
            fft_in[fft_sample_idx].r = i_val * hanning_window[fft_sample_idx];
            fft_in[fft_sample_idx].i = q_val * hanning_window[fft_sample_idx];

            // 当缓冲区攒够 1024 个点，立刻就地解决，在板载端执行 FFT 变换！
            if (++fft_sample_idx >= FFT_POINTS) {
                fft_sample_idx = 0; // 滚回复位
                frame_counter++;

                // 核心计算：执行快速傅里叶变换
                kiss_fft(fft_plan, fft_in, fft_out);

                // 将复数输出转化为 dB 刻度值，并完成移动和片上帧平滑
                process_fft_output(fft_out, spectrum_send_payload, spectrum_avg_buf, frame_counter);

                // 积攒到设定帧数后，打包通过 UDP 发回给 PC 上位机绘图
                if (frame_counter >= FRAME_AERAGE_NUM) {
                    frame_counter = 0; // 平滑帧清零
                    
                    // 发送 1024 帧 float 数组，数据量极小，完全不卡网络
                    sendto(sockfd, spectrum_send_payload, FFT_POINTS * sizeof(float), 0,
                           (struct sockaddr *)&servaddr, sizeof(servaddr));
                }
            }
        }
    }
```

- 首先是加窗处理，然后平滑灌入 FFT 输入缓冲区
- 然后当缓冲区攒够 1024 个点，立刻就地解决，在板载端执行 FFT 变换！
    - 调用 `kiss_fft(fft_plan, fft_in, fft_out)` 执行快速傅里叶变换
    - 调用 `process_fft_output(fft_out, spectrum_send_payload, spectrum_avg_buf, frame_counter);` 将复数输出转化为 dB 刻度值，并完成移动和片上帧平滑
    - 最后当积攒到设定帧数后，打包通过 UDP 发回给 PC 上位机绘图

</br>

<u>**a. 快速傅里叶变换 kiss_fft**</u>

这部分代码比较成熟，直接封装在 `kiss_fft.c/.h` 中，可以类似 gnuradio 中的快速傅里叶模块，具备 FFT 和 IFFT 双重功能：

```
/* * 初始化 FFT 配置
 * nfft: FFT 点数 (例如 1024)
 * inverse_fft: 0 为正变换 (FFT)，1 为反变换 (IFFT)
 */
kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void * mem, size_t * lenmem);

/*
 * 执行 FFT 变换
 * cfg: 经过 alloc 初始化的配置结构体
 * fin: 输入时域 I/O 数组 (大小为 nfft)
 * fout: 输出频域 I/O 数组 (大小为 nfft)
 */
void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_cpx *fin, kiss_fft_cpx *fout);

/* 释放内存 */
void kiss_fft_free(void* cfg);
```

</br>

<u>**b. process_fft_output**</u>

这个函数能够将复数输出转化为 dB 刻度值，并完成移动和片上帧平滑：

```
/* ---- 【功能抽象】动态范围转换（将幅值平方转为标准 dB 功率，并完成 FFT 移频） ---- */
// 提示：SDR 读取到的 IQ 经 FFT 后，前半部分是正频率，后半部分是负频率。
// 转换时需要执行 FFT-Shift 逻辑，把中心频点（直流分量）挪到屏幕正中间。
static inline void process_fft_output(kiss_fft_cpx *out, float *output_db, float *avg_buffer, int frame_cnt) {
    float norm_factor = 1.0f / (FFT_POINTS * 2048.0f); // 归一化因子
    
    for (int i = 0; i < FFT_POINTS; i++) {
        // 优雅实现 FFT-Shift：后半部分移到左边，前半部分移到右边
        int shift_idx = (i < FFT_POINTS / 2) ? (i + FFT_POINTS / 2) : (i - FFT_POINTS / 2);
        
        // 计算幅值平方（功率）
        float power = (out[i].r * out[i].r + out[i].i * out[i].i) * norm_factor;
        
        // 防止 log10(0) 导致死机
        if (power < 1e-10f) power = 1e-10f;
        
        // 换算成相对 dB 值
        float db_val = 10.0f * log10f(power);
        
        // 在板载端对多帧数据进行无缝累加
        avg_buffer[shift_idx] += db_val;
        
        // 当达到平滑帧数时，求出平均值并输出
        if (frame_cnt == FRAME_AERAGE_NUM) {
            output_db[shift_idx] = avg_buffer[shift_idx] / FRAME_AERAGE_NUM;
            avg_buffer[shift_idx] = 0.0f; // 清空以便下一轮统计
        }
    }
}
```

</br>

**备注**：上面两个函数实现的逻辑我之前在 GnuRadio 中也实现过，可以参考下（GnuRadio 图块化设计更清晰一点）：《[[28]. GNU Radio 系列教程（二八）—— 用功率阈值侦测的例子介绍 GNU Radio 复杂交互页面设计][#2]》     

![][p2]

</br>

#### 4）pluto_spec_viewer.py

这个 python 脚本用于监听 UDP 上的 PlutoSDR 频谱流并绘制频谱图，代码比较简单，这里就不单独展开（主要是我们 plutosdr 没有屏幕，不然我就给他做屏幕上了）。

</br>

### 5. 结语

伴随着电脑屏幕上丝滑刷新的瀑布流，我们的**边缘计算级嵌入式频谱分析仪**实战圆满成功！

回看整套架构，Pluto 在板载 Linux 中顶住了 **10MSps 的超高射频吞吐**，在片上完成了**动态流控、汉宁窗加窗、1024 点 kissFFT 变换、FFT-Shift 直流对中以及多帧片上平滑**。通过将全套 DSP 算力下沉到边缘端，网络传输的数据量暴跌了几个数量级，彻底解放了上位机的 CPU。

从动态参数解析到硬核的频域变换，你已经打通了从底层射频驱动到高层算法调优的全部闭环。这也是你迈向高级嵌入式 SDR 专家的又一次重大胜利！恭喜道友，我们下一期更精彩！

[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/110      
[#2]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/46      
[#3]:https://gemini.google.com/app/c2ddc1c30df902a9?utm_source=app_launcher&utm_medium=owned&utm_campaign=base_all     

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/pluto_spec_viewer_show.gif      
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/pluto_spectrum_analyzer_gnuradio_grc.png       
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/pluto_spec_make_show.gif     
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/plutosdr_spectru_in_arm.png     


[bgm]:舟——万妮达
