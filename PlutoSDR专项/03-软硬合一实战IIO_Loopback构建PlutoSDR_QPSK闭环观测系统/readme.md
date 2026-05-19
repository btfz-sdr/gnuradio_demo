### 1. 引子

为什么要用 IIO Loopback？

*   **痛点分析**：传统的 PlutoSDR 收发需要两台设备，且空中信号不可见，调试困难。
*   **解决方案**：利用 IIO 的内部 Loopback 模式，将发射数据直接回环到接收端，实现单设备闭环调试。
*   **核心价值**：无需射频环境，即可在时域和频域上观测 QPSK 信号在 PlutoSDR 内部的完整流转。

</br>

### 2. 启动环境

学完前两课，大家应该有如下启动开发环境的命令：

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

### 3. 体验实验

<u>1）下载并打开流程图</u>

```
git clone git@github.com:btfz-sdr/gnuradio_demo.git --recurse-submodules
cd gnuradio_demo/PlutoSDR专项/03-软硬合一实战IIO_Loopback构建PlutoSDR_QPSK闭环观测系统
gnuradio-companion qpsk_tx_rx_with_plutosdr.grc 
```

打开后点击菜单栏的 `RUN -> Generate`，会看到在 03 下生成了一个 `qpsk_tx_rx_with_plutosdr.py` 脚本。

![][p1]

备注：这个流程图是我们《[QPSK+B210 搞定大文件视频传输](https://beautifulzzzz.com/gnuradio/tutorial/lesson/9)》课程中介绍的一样，都是基于 SDR + QPSK 实现文件自收发传输的 GNU Radio 案例（只是这里我将设备从 B210 换成了 PlutoSDR）。

</br>

<u>2）运行流程图与 IIO</u>

```
# 在终端 1 中运行 osc，参考上一课介绍的：
# - 进行连接设备
# - AD936x TAB 下的 FPGA Settings 分区中将 TX 1/TX 2 的 DDS Mode 设置为 DAC Buffer Output
# - AD936x Advanced TAB 下的 BIST 子块，将 Loopback 依次设置为：
#     - Disable
#     - Digital TX -> Digital RX
#     - RF RX->RF TX
osc

# 在终端 2 中运行 gnuradio 流程图：
# 注意：先运行 osc 并配置好，再启动下面的脚本；osc Loopback 模式每次切换，下面脚本都要重新运行
python3 qpsk_tx_rx_with_plutosdr.py
```

至此我们可以借助 IIO OSC 工具做诸多观测：
        
*   **时域观测**：QT GUI Time Raster Sink (观察波形畸变)。
*   **频域观测**：QT GUI Frequency Sink (观察频谱泄漏)。
*   **星座图**：QT GUI Constellation Sink (核心！观察 QPSK 星座点的扩散情况，这是衡量链路质量的金标准)。

</br>

### 4. 实验分析
<u>1）Loopback 实验分析</u>

Loopback 模式 | 描述 | 结果分析 | 实验截图
---|---|---|---
**Disable**                  | **信号路径**：经过外部馈线/天线<br>**目的**：模拟真实通信场景<br>**星座图观感**：算法纠偏后可清晰识别 | ![][p5] | <div style="max-width: 640px">![][p2]</div>
**Digital TX -> Digital RX** | **信号路径**：芯片数字域折返<br>**目的**：验证调制解调算法逻辑<br>**星座图观感**：完美、无噪声、点极细 | ![][p6] | <div style="max-width: 640px">![][p3]</div>
**RF RX->RF TX**             | **信号路径**：内部射频模拟回环<br>**目的**：验证芯片模拟前端性能<br>**星座图观感**：旋转严重、噪声大、测试极限 | ![][p7] | <div style="max-width: 640px">![][p4]</div>


</br>

<u>2）IQ 轨迹图分析</u>

在 `Disable Loopback`（即实际信号通过天线或线缆传输）模式下，这张绿色的 **IQ 轨迹图（Constellation Trajectory）** 具有非常重要的参考意义。

它不仅仅是几个“点”，那些连接点的**轨迹线**揭示了基带信号在经过 PlutoSDR 的模拟前端、增益放大器以及物理信道时发生的**物理形变**。

![][p8]

以下是具体的参考价值：

- a. 验证成型滤波器（Root Raised Cosine）的效果

    如果你发送的是理想的方波，IQ 图应该是四个孤立的点，线会直接穿过原点。

    * **参考意义：** 看到图中这种圆滑过渡的“蛛网状”连线，说明你的 **RRC（根升余弦滤波）** 正在起作用。这些轨迹展示了信号在切换符号时的带限特性。如果线条变得非常生硬或出现尖锐折点，通常意味着发射端的插值滤波或带宽设置不匹配。

- b. 观察功率放大器（PA）的非线性失真

    观察轨迹图的最外圈：

    * **正常：** 轨迹圆弧应该是饱满且对称的。
    * **异常：** 如果四个顶角的轨迹向中心收缩，或者线条变得“扁平”，说明 **PA 已经进入了饱和区**。这在测试外部天线时很有用，可以帮你判断当前的发射功率（TX Gain）是否过大，导致了严重的线性度恶化。

- c. 判断信道中的噪声与干扰

    * **星座点块（点的粗细）：** 星座点聚拢的程度反映了信噪比（SNR）。
    * **轨迹抖动：** 如果连接两个符号之间的线条不是平滑的曲线，而是带有明显的毛刺或细微的锯齿震荡，说明信道中存在明显的**高频干扰**或接收端的 **ADC 量化噪声** 较大。

- d. 识别频率偏移（CFO）的动态过程

    虽然右侧蓝色的“科斯塔斯环路”已经帮你把点锁住了，但左侧这个原始 IQ 轨迹图能看到“算法处理前”的状态。

    * **参考意义：** 如果这个绿色的方框在不停地旋转，说明发射机和接收机之间存在**频率偏差**。旋转得越快，说明频偏越大。通过观察轨迹旋转的方向，甚至可以直观判断出是发高了还是发低了。


**总结：**

当你测试外部天线时，如果发现蓝色的点很散，你可以通过看绿色的轨迹图来定位原因：

* 如果轨迹图**非常规整但整体偏小**，说明信号太弱（天线增益不够）。
* 如果轨迹图**顶端被削平（变成方框）**，说明信号太强导致失真。
* 如果轨迹图**在旋转**，说明 Costas 环还没跟上频偏。

这就好比是在看信号的“原片”，对于排除模拟电路层面的故障非常有价值。


</br>

### 5. 总结与预告

#### **■ 本课总结：从“调通算法”到“看透物理”**

通过本课的 IIO Loopback 闭环观测系统，我们完成了一次从**数字逻辑**到**模拟射频**的深度巡检。

* **算法验证**：在 `Digital TX -> RX` 模式下，我们确认了 QPSK 调制解调逻辑在纯数字域的完美无瑕。
* **物理透视**：借助 **IQ 轨迹图**，我们学会了像“老中医”号脉一样，通过线条的圆滑度、外圈的收缩以及旋转速度，直接诊断 RRC 滤波效果、PA 饱和度以及 CFO 频偏。

这种 **“信号质量 - 硬件参数”** 的映射观测方法，是你在处理复杂无线通信问题时的核心竞争力。你不再是盲目调整增益，而是能通过现象直击本质。

#### **■ 下课预告：断开脐带，开启“自由行走”模式**

目前为止，我们的 PlutoSDR 依然像个“巨婴”，时刻需要依赖 PC 端的 GNU Radio 来维持生命。

但在真正的工程落地中，Pluto 往往需要被部署在无人机、信号塔或偏远的野外。**下一集，我们将挑战“脱机运行（Standalone Mode）”** —— 我们将学习如何将开发好的算法交叉编译并直接烧录进 Pluto 内部的 ARM 处理器中，实现真正的**嵌入式无线电独立运行**。

脱离电脑后的 PlutoSDR 还是那个玩具吗？不，它将是你手中的**独立作战单元**。

</br>

### 参考链接

[[1]. GNU Radio Wiki —— PlutoSDR Source][#1]     
[[2]. GNU Radio Wiki —— PlutoSDR Sink][#2]     
[[3]. ADI Wiki —— GNU Radio 和 IIO 设备：gr-iio][#3]      
[[4]. GNU Radio Wiki —— Constellation Modulator][#4]          


[#0]:https://gemini.google.com/app/b9403f9e5b262c92?utm_source=app_launcher&utm_medium=owned&utm_campaign=base_all
[#1]:https://wiki.gnuradio.org/index.php/PlutoSDR_Source    
[#2]:https://wiki.gnuradio.org/index.php/PlutoSDR_Sink    
[#3]:https://wiki.analog.com/resources/tools-software/linux-software/gnuradio      
[#4]:https://wiki.gnuradio.org/index.php/Constellation_Modulator     

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/qpsk_tx_rx_with_plutosdr_grc.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/qpsk_tx_rx_with_plutosdr_test1.png
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/qpsk_tx_rx_with_plutosdr_test2.png
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/qpsk_tx_rx_with_plutosdr_test3.png
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/qpsk_tx_rx_with_plutosdr_test1_2.png
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/qpsk_tx_rx_with_plutosdr_test2_2.png
[p7]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/qpsk_tx_rx_with_plutosdr_test3_2.png
[p8]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/qpsk_tx_rx_with_plutosdr_test4.png



