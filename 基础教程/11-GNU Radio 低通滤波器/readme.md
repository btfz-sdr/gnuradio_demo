### 1、前言

低通滤波器（LPF）是数字信号处理中最基础也最常用的模块之一，用于滤除高于截止频率的高频信号，仅保留低频分量。

今天通过三张流程图，由浅入深了解 GNU Radio 中低通滤波器的基本使用、内置低通模块以及使用自定义抽头（Taps）的 FIR 滤波实现。

</br>

### 2、未加滤波器的直通测试

`test.grc` 是最基础的信号传输与频域观察链路：

![][p1]

1. **信号源配置**：`Signal Source` 产生 $0\text{Hz}$（或通过 `QT GUI Range` 动态调节）的余弦波。
2. **频域观察**：信号直接送入 `QT GUI Frequency Sink` 观察频谱。

</br>

### 3、使用 GNU Radio 内置 Low Pass Filter 模块

`test0.grc` 加入了 GNU Radio 自带的 **Low Pass Filter** 模块：

- 1）**滤波器参数配置**：
    * **Sample Rate**: $32\text{k}$（与系统采样率一致）
    * **Cutoff Freq**: $8\text{kHz}$（截止频率设为 $8\text{kHz}$）
    * **Transition Width**: $4\text{kHz}$（过渡带宽度）
    * **Window**: Hamming（汉明窗）
- 2）**性能与流控**：
    * 滤波后的信号经过 **Throttle** 限速后再送入 **QT GUI Frequency Sink** 显示。
    * 当滑动 `frequency` 调节信号频率超过截止频率（$8\text{kHz}$）时，频谱上的信号幅度将被大幅衰减。

![][p2]

</br>

### 4、使用 Frequency Xlating FIR Filter 与自定义 Taps

`untitled.grc` 演示了更进阶的滤波方式——使用频率搬移 FIR 滤波器（**Frequency Xlating FIR Filter**）配合自定义抽头（Taps）：

- 1）**自定义 Taps 配置**：
    * 使用 **Import** 模块引入 `numpy`（`Import: np`）。
    * 通过 **Variable** 模块定义矩形窗抽头 `boxcarFilter`（值为 `125m, 125m...` 数组）或使用 **Low-pass Filter Taps** 模块生成系统的滤波器系数。
- 2）**频率搬移滤波（Frequency Xlating FIR Filter）**：
    * 将自定义的 Taps 填入滤波器的 `Taps` 参数中。
    * 该模块能在滤波的同时完成下变频/频率搬移（`Center Frequency` 参数控制），非常适用于数字接收机前端的信道选择与滤波。

![][p3]

</br>

### 参考链接

* [GNU Radio Wiki - Low Pass Filter](https://wiki.gnuradio.org/index.php/Low_Pass_Filter)
* [GNU Radio Wiki - Frequency Xlating FIR Filter](https://wiki.gnuradio.org/index.php/Frequency_Xlating_FIR_Filter)
* [GNU Radio Wiki - Designing Filter Taps](https://wiki.gnuradio.org/index.php/Designing_Filter_Taps)



[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_lpf_grc1.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_lpf_grc2.png    
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_lpf_grc3.png
  

