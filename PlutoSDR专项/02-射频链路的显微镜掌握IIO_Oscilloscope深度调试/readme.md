我们第一课已经完成了从 USRP 到 PlutoSDR 的“心路历程”和基础握手，第二课我们就需要深入到 ADI 官方最核心的调试工具 —— **IIO Oscilloscope**（俗称 IIO-Osc）。

这个工具不仅是示波器，更是配置 AD936x 系列芯片寄存器的“大管家”。

![][p1]

</br>

### 1. 为什么它是 PlutoSDR 的“本命”工具？

在第一集中，我们实现了“握手”。但如果你想深入了解 AD9363 的内部增益控制、星座图质量或手动校准，普通的接收软件（如 SDR++）就显得太“表面”了。

同时，在玩 B210 时，我们习惯了直接跑 GNU Radio 或 UHD。但在玩嵌入式 SDR（尤其是 Zynq 架构）时，直接操作底层寄存器往往能帮我们定位 90% 的射频问题。

* **原生支持**：ADI 官方开发，基于 Linux IIO 子系统，与 Pluto 内部驱动完美契合。
* **实时交互**：无需重新编译代码，点点鼠标就能改变增益控制模式（AGC/Manual）、滤波器带宽和采样率。
* **全能监控**：时域波形、频域频谱、星座图、控制变量（如温度、RSSI）一站式搞定。

![][p2]

</br>

### 2. 软件环境：如何优雅地开启“示波器”？

安装直接参考 ADI 官方的介绍指南 <sup>[1][#1]</sup>：

* **windows 用户**：使用 ADI 提供的 releases 整合安装包。
* **arch linux 用户**：使用 `yay -S iio-oscilloscope-git`。
* **ubuntu24.04 用户**：稍微有点麻烦，下面详细介绍。

```
# 准备
sudo apt-get update
sudo apt-get -y install libglib2.0-dev libgtk-3-dev libgtkdatabox-dev libmatio-dev libfftw3-dev libxml2 libxml2-dev bison flex libavahi-common-dev libavahi-client-dev libcurl4-openssl-dev libjansson-dev cmake libaio-dev libserialport-dev
sudo apt-get -y install libiio-dev
sudo apt-get -y install libad9361-dev

# 编译安装
git clone https://github.com/analogdevicesinc/iio-oscilloscope.git 
cd iio-oscilloscope 
git checkout origin/libiio-v0  # 截至2025年10月1日，IIO-Oscilloscope 主分支迁移到 libiio v1
mkdir build &&  cd build
cmake ../ && make -j $(nproc)   
sudo make install
sudo ldconfig 
```

之后运行 `osc` 即可打开 GUI 页面程序。

此外如果出现 `osc: error while loading shared libraries: libosc.so.0: cannot open shared object file: No such file or directory`，需要 `sudo find / -name "libosc.so.0"` 查找到对应目录（`/usr/local/lib/`），然后：

```
export LD_LIBRARY_PATH=/usr/local/lib/
osc                                   
```

</br>

**PS：** 结合第一课的操作，上面操作完之后，如果退出 docker 容器之后，下次启动还要重复操作，可以用下面命令将当前容器状态保存为新镜像（增量存储）：

```
➜  ~ docker ps   
CONTAINER ID   IMAGE                             COMMAND                  CREATED          STATUS                    PORTS      NAMES
5878e3fd1734   ubuntu:gnuradio-310               "bash"                   26 minutes ago   Up 26 minutes                   
➜  ~ docker commit 5878e3fd1734 ubuntu:gnuradio-plutosdr
sha256:23d28734fecabb7a69a61eb3a93ba016b07b0efeed5c0c2eeb22c6c27b4eb25c
➜  ~ docker image list 
IMAGE                             ID             DISK USAGE   CONTENT SIZE   EXTRA
ubuntu:gnuradio-310               ef1f08a0b394       3.41GB             0B      
ubuntu:gnuradio-38                69c3f019986b       3.01GB             0B        
ubuntu:gnuradio-plutosdr          23d28734feca        3.8GB             0B        
ubuntu:gnuradio-radar             dc19e62de441       3.29GB             0B        
ubuntu:gnuradio-releases-3.7      d4628a38d878       1.62GB             0B        
```

之后就可以用下面方法直接启动了：

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
```

</br>

### 3. ADI IIO Osilloscope 四大插件参数详解

启动后连接设备：（我们使用 USB 连接到 OTG USB 口，在电脑上会看到两个信息）

<img src="https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_connect.png" style="max-width:730px;height:auto"></img>

</br>

#### 3.1 DMM Tab/Plugin（数字万用表插件）

启动按钮激活后，数字万用表将持续显示设备特定数据：

![][p4]

</br>

#### 3.2 Debug Tab/Plugin（调试插件）

- 设备选择：设置活动设备。选择设备后，插件中显示的所有其他信息都将与该设备相关。
- IIO 设备属性：允许对设备的属性进行读/写操作。
- 寄存器：提供对设备寄存器的底层访问。

<img src="https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_debug.png" style="max-width:730px;height:auto"></img>

备注：这是高级玩家玩的，能够直接读取/写入 SPI 寄存器。虽然难，但这是理解 AD9363 芯片手册的捷径。

</br>

#### 3.3 AD936X Tab/Plugin

首先是一个 AD936x 的功能块图（还有一个比较经典的 AD936x 的电路板模块图）：

<img src="https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_diagram.png" style="max-width:730px;height:auto"></img>

然后就是各个参数设置：

**1）Global Settings**

![][p7]

- <u>ENSM (Enable State Machine) 状态机控制</u>     
    ENSM 是 AD9361 内部的状态管理器，负责控制芯片在休眠、接收 (RX)、发送 (TX) 或全双工 (FDD) 模式之间切换。
    * **Active ENSM**: 当前芯片正处于的状态（图中为 `fdd`）。
    * **ENSM Modes**: 模式选择下拉框。
    * **fdd (Frequency Division Duplex)**: 频率双工模式，收发同时工作（最常用）。
    * **tdd (Time Division Duplex)**: 时分双工模式，收发分时进行。
    * **wait, sleep, alert**: 各种低功耗或待机状态。
- <u>Calibration Mode (校准模式)</u>        
    AD9361 内部有大量的模拟电路，受温度和电压影响很大，因此需要实时校准。
    * **auto**: 软件/固件会自动根据状态切换和时间间隔触发内部校准（包括正交校准、DC 偏移校准等）。
    * **manual**: 手动控制校准时机，通常用于实验环境下排除自动校准带来的干扰。
- <u>Path Rates (路径时钟速率)</u>       
    这一排数字展示了射频信号从天线到数字基带转换过程中，每一级滤波和抽样/插值的频率（单位通常为 **MHz**）。
    - **RX Path Rates (接收路径)**
        * **BBPLL**: 基带锁相环频率（图中为 983.040 MHz），它是所有数字时钟的源头。
        * **ADC**: 模数转换器采样率（245.760 MHz）。
        * **R2, R1, RF**: 接收端的各级 FIR/HB 滤波器抽样后的频率。
        * **RXSAMP**: **最终输出到电脑/FPGA 的采样率**（30.720 MSPS）。
    - **TX Path Rates (发送路径)**
        * 与接收类似，**TXSAMP**（30.720 MSPS）是电脑发给芯片的原始数据速率，经过 T1、T2、DAC 等级联插值放大，最后由 **BBPLL** 调制。
- <u>TRX Rate Governor (速率管理器)</u>          
    * **nominal**: 标称模式。
    * 这主要影响 BBPLL 的分频策略，确保各级路径速率在硬件允许的范围内，通常保持默认即可。
- <u>XO Correction (晶振校准)</u>   
    * **40000000 (40 MHz)**: 这是板载温补晶振 (TCXO) 的参考频率。
    * **作用**: 如果你发现接收到的中心频率有细微偏移（例如应该在 100MHz 但偏移了 500Hz），可以通过微调这个数值来校正频率误差。
- <u>Filter FIR Configuration (FIR 滤波器配置)</u>       
    * **Filter FIR configuration**: 点击文件夹图标可以加载自定义的 FIR 滤波器系数文件（`.ftr`）。
    * **Auto Filter**: 勾选后，驱动程序会根据你设置的采样率自动计算并加载最合适的滤波器参数，以保证信号通带和抗混叠效果。

</br>

**[总结]**：如果我们进行常规的 SDR 开发（如 GNU Radio 实验）：
- **ENSM** 保持 `fdd`。
- **Calibration** 保持 `auto`。
- 最核心的关注点应是 **RXSAMP / TXSAMP**，它们决定了你能观察到的瞬时频谱带宽。
- 如果发现频率不准，调整 **XO Correction**。

</br>

**2）Receive Chain**

![][p8]

* **RF Bandwidth**: 模拟带宽。决定硬件前端让多宽的信号通过。
* **Sampling Rate**: 采样率。决定你在软件上能看到的频谱宽度。
* **RX LO**: 中心频率。你想接收的频率点。
* **Gain Control**: 增益模式。
    * `manual`: 自己调 dB 值。
    * `slow/fast attack`: **AGC（自动增益控制）**，防止信号太强导致波形溢出变成方波。
* **Tracking**: 校准追踪。全勾选，用于消除频谱中心的直流尖峰和镜像干扰。

</br>

**3）Transmit Chain**

这是 **Transmit Chain（发射链路）** 设置，逻辑与接收端对称，但重点在于控制发射功率：

![][p9]     

* **RF Bandwidth**: 发射信号的模拟带宽。
* **Sampling Rate**: 采样率。决定你发给芯片的数据速率。
* **TX LO Frequency**: 发射中心频率。
* **RF Port Select**: 发射端口选择（如 A、B 端口）。
* **Attenuation (dB)**: **发射衰减**。
    * **关键点**：数值越大，发射功率越**小**。
    * 如果你需要增加发射距离，就减小这个 dB 值；如果担心功率太大烧坏对端设备，就增大这个值。
* **RSSI**: 这里显示的是发射端的信号强度指示（通常用于监控）。

</br>

**4）FPGA Setting**

这是 **FPGA Settings** 界面，主要负责 FPGA 内部的数字信号处理和测试源配置：

![][p10]

* <u>a. 如果你不发送电脑里的文件，FPGA 可以自己生成信号进行自检。</u>
    * **DDS Mode**: 直接数字频率合成模式。
        * `One CW Tone`: 发送一个单频点信号（用来测试载波、泄露等非常管用）。
        * `Two Tone`: 发送两个频点（测试三阶交调失真）。
        * `Independent I/Q control (I/Q 独立控制)`: 允许分别手动设置 I 路（实部）和 Q 路（虚部）的参数。主要用于调试硬件不平衡。比如你可以只发 I 路信号，观察频谱上的载波泄露；或者微调两路相位，测试正交调制器的镜像抑制能力。
        * `DAC Buffer Output (DAC 缓存输出)`: 这是最常用的模式。它让 FPGA 停止产生内部测试单音，转而通过 DMA 发送你电脑软件（如 GNU Radio、MATLAB 或文件）传输过来的实际数据。当你想要发射真正的通信协议信号（如 OFDM、单边带调制等）时，必须切换到这个模式。
    * **Frequency / Scale / Phase**:
        * 调节这个测试单音的**频率偏移**、**幅度**（dBFS）和**初始相位**。
    * **注意**：如果 `Scale` 设为 `-Inf dB`，意味着关闭输出。        
* <u>b. DMA Buffer (直接存储器访问)</u>      
    * **Sampling Rate**: 这里是 FPGA 侧看到的采样率，必须与前面 AD9361 设置的采样率保持一致，否则数据会由于“喂”得太快或太慢导致断流。      
* <u>c. Receive (接收端处理)</u>    
    * **Phase Rotation**: 相位旋转。在数字域微调接收信号的相位，通常用于多通道（MIMO）同步时的相位对齐。

</br>

**简而言之**：这一页主要是让你在没有外接信号源的情况下，通过 FPGA 产生一个**内部模拟信号**来测试链路通不通。

</br>

#### 3.4 AD936X Advanced Tab/Plugin

**1）ENSM/MODE/Clocks 选项卡**

这是 **AD936X Advanced**（高级设置）中的 **ENSM/MODE/Clocks** 选项卡。这里主要涉及芯片底层的硬件时序、端口物理连接以及时钟源配置：

![][p11]

* <u>a. ENSM Mode (状态机底层控制)</u>
    * **FDD / TDD**: 设置双工模式。
    * **Pin Pulse Mode / TXNRX Pin Control**:
        * 正常情况下通过软件指令切换收发。
        * 勾选后，可以使用 FPGA 的物理引脚（ENABLE 和 TXNRX 引脚）进行**硬件级极速切换**，适用于对时延要求极高的跳频通信。
    * **TDD（时分双工）Mode 选项**: 仅在 TDD 下有效，用于优化合成器（VCO）的启动速度或跳过校准。
        * **Use Dual Synth**: **开启双合成器**。让接收和发送各用一个独立的频率锁相环，切换时不需要重新调频，实现“零延迟”收发转换。
        * **Use FDD VCO tables**: **借用 FDD 查表**。使用全双工模式下的频率校准表，省去 TDD 模式下每次切换都要进行的耗时计算。
        * **Skip VCO cal**: **跳过 VCO 校准**。强制不进行频率校准，切换速度最快，但频率准确度和信号质量可能随温度变化而变差。
        * **Update TX Gain in ALERT**: **在 Alert 状态更新增益**。允许在芯片还没进入发射状态（Alert 预备状态）时就提前设好发射功率，确保信号一出来就是对的分贝数。
                  这四个选项主要为了解决 **TDD（时分双工）** 模式下，收发频率切换太慢的问题。
* <u>b. Mode (端口物理映射)</u>
    * **RX port input**: 选择具体的差分信号输入引脚（如 RX1A, RX2A 等）以及是否为 **Balanced**（平衡/差分）模式。这必须与你 PCB 板上的走线匹配。
    * **TX port output**: 同上，选择信号从哪个物理引脚发出去。
    * **RX2 Phase Inversion**: **RX2 相位反转**。如果 PCB 走线时 I/Q 或差分线画反了，可以在这里通过数字方式“翻转”相位，不用改板子。
* <u>c. Clocks (时钟高级配置)</u>
    * **XO Disable use EXT RefCLK**: **禁用内置晶振，使用外部参考时钟**。如果你给板子外接了高精度时钟源，必须勾选此项。
    * **Ext RX/TX LO**: 允许收发链路直接使用外部本地振荡器信号，绕过内部 PLL。
    * **CLOCKOUT**: 设置芯片的 `CLK_OUT` 引脚输出频率（可分频输出给 FPGA 或其他芯片作为主钟）。
    * **Fastlock Pin Control**: 使用硬件引脚触发频率锁定配置文件的切换。

</br>

**[总结]**：这一页是**硬件底层对接**用的。如果你不是在调试**自制 PCB 板**、**外接参考时钟**或**超高性能硬件切换**，建议大部分保持默认，重点只看 **RX/TX port** 是否选对了对应的天线引脚。

</br>

**2）eLNA（外部低噪声放大器） 设置页**

这是 **eLNA（外部低噪声放大器）** 设置页。当你的硬件板子在 AD9361 芯片外面又加了一级放大器时，需要在这里配置，以便让系统的增益计算更准确。

![][p12]

* **LNA Gain (mdB)**：**外部 LNA 的增益**。单位是毫分贝（mdB），例如填 `15000` 代表 15dB。设置后，系统的 RSSI 强度显示会把这部分增益算进去。
* **LNA Bypass Loss (mdB)**：**旁路损耗**。如果你的外部 LNA 支持 Bypass（关掉放大直接通过），这里填关掉时的信号损耗。
* **Settling Delay (ns)**：**建立延迟**。外部 LNA 开关切换后，需要等多少纳秒信号才能稳定。
* **RX1 GPO0 / RX2 GPO1**：**控制引脚**。勾选后，芯片会自动通过 GPO 引脚给外部 LNA 发控制信号（告诉它什么时候该放大，什么时候该旁路）。
* **External LNA enabled for all gain indexes**：**全增益区间开启**。强制外部 LNA 在所有增益设置下都工作，不随 AGC 自动关闭。

</br>

**[总结]**：这页是用来**告诉芯片你外面还有一个放大器**，并让芯片通过 GPO 引脚去**自动控制**它。如果你用的是 PlutoSDR 这种没有外置 LNA 的板子，这页不用管。

</br>

**3）RSSI（接收信号强度指示）设置页**

这是 **RSSI（接收信号强度指示）** 的高级配置页，主要控制信号强度是如何“算”出来的，直接影响自动增益控制（AGC）的稳定性。

![][p13]

* **Duration (us)**：**统计时长**。算法计算平均能量的时间窗口。时间越长，数值越稳，但响应变慢。
* **Delay (us)**：**延迟时间**。在状态切换（如从发射转接收）后，等待信号稳定多久再开始测量能量。
* **Wait (us)**：**等待时间**。连续测量之间的间隔时间。
* **Restart Mode**：**重启触发模式**。
    * `Gain Change Occurs`：每当增益改变时，重新开始计算 RSSI。
    * `ENSM Enable High`：当接收状态开启时重新触发测量。

</br>

**[总结]**：这页是用来调节**信号强度检测的灵敏度和平滑度**。如果你的信号强度跳变太厉害导致自动增益乱闪，就调大 `Duration`。

</br>

**4）GAIN 设置页**

这是最复杂的 **GAIN（增益控制）** 高级设置页，主要用于精细调优 **AGC（自动增益控制）** 算法。它决定了芯片在面对强干扰或微弱信号时，动作快不快、稳不稳。

![][p17]

* <u>a. 模式与基础配置 (Mode & MGC)</u>
    * **Table Mode**: 建议选 `Full Gain Table`。这是芯片最全的增益查找表，能覆盖最大动态范围。
    * **MGC (Manual Gain Control)**: 手动增益设置。如果你选了手动模式，可以配置通过软件指令还是 FPGA 引脚来加减 dB 值。
* <u>b. AGC 核心阈值 (AGC Thresholds)</u>
    这是自动增益控制的“决策线”，决定了增益何时调整：
    * **Inner/Outer High**: 信号超过此线，判定为“太强”，AGC 立即**减小**增益。
    * **Inner/Outer Low**: 信号低于此线，判定为“太弱”，AGC 缓慢**增加**增益。
    * **Gain Update Interval**: 调整频率。设置每隔多少微秒评估一次并修改增益。
* <u>c. 过载检测 (Overload Detectors)</u>
    专门防止硬件“爆表”的保险丝：
    * **ADC Overload**: 监测数字后端。防止 A/D 转换器出现削顶失真（波形变方波）。
    * **LMT Overload**: 监测射频前端。防止 LNA 或混频器饱和导致的非线性失真。
    * **Threshold/Exceed Cntr**: 设置信号强度和持续时间，达到标准即触发强制降增益。
* <u>d. 快速响应设置 (Fast Attack AGC)</u>
    用于捕捉 Wi-Fi 等突发脉冲信号：
    * **State Wait Time**: 反应时间。发现新信号后，AGC 等待信号稳定并调整到合适增益的速度。
    * **Peak Detectors**: 快速检测信号峰值，确保在数据包开始的几十纳秒内就完成压限。
* <u>e. 增益解锁逻辑 (Gain Unlock)</u>
    决定一帧信号结束后，AGC 怎么“归位”：
    * **Unlock Logic**: 监测功率掉落（信号消失）或突增（新信号）。一旦满足，AGC 解锁当前状态。
    * **Restart Mode**: 解锁后，增益通常会跳回 `MAX Gain`（最大增益），重新待命寻找下一个微弱信号。

</br>

**[总结]**：

* **常规连续信号（如广播）**：调优 **Thresholds**，追求稳定。
* **突发脉冲信号（如通信数据包）**：调优 **Fast Attack** 和 **Unlock**，追求速度。
* **信号总是失真**：调低 **Overload** 的阈值，让降噪更灵敏。

</br>

**5）TX Monitor（发射监控）设置页**

这是 **TX Monitor（发射监控）** 设置。AD9361 内部有一条特殊的反馈链路，可以把发射出去的信号“采样”回来，用于实时监控发射功率或进行自校准。

![][p14]

* **Frontend Gain**: 监控路径的前端增益。
* **LO Common Mode**: 本振共模电平调节，用于优化监控链路的线性度。
* **Low/High Gain Threshold**: 增益切换阈值。当发射功率变化时，监控链路会自动在低增益和高增益模式间切换。
* **Delay / Duration**: 测量发射功率时的延迟和统计时长（以接收样本数为单位）。
* **DC Tracking**: 发射监控路径的直流偏移追踪，保持测量准确。
* **One Shot Mode**: 单次测量模式。

</br>

**[总结]**：这页是用来**监控自己发了多强信号**的。它利用内部反馈链路实现闭环控制，确保发射功率稳定。普通通信实验一般不需要动。

</br>

**6）Aux ADC/DAC/IO 设置页**

这是 AD9361/AD9364 的 **Aux ADC/DAC/IO（辅助功能与通用 IO）** 设置页。除了处理射频信号，芯片还自带了一些辅助硬件，用于监控环境或控制外部设备（如放大器、开关）。

![][p18]

* <u>a. Temp Sensor (温度传感器)</u>
    * **Measurement Interval**: 测量周期（多长时间测一次温度）。
    * **Offset**: 温度偏移校准。
    * **Periodic Measurement**: 勾选后，芯片会持续监控自身温度。由于 AD9361 发热量大，温度会显著影响时钟频率和增益稳定性。
* <u>b. Aux ADC & DAC (辅助数据转换器)</u>
    这两部分是芯片内部自带的通用 ADC 和 DAC，不是用来跑射频数据的。
    * **Aux ADC**: 可以用来读取外部传感器的模拟电压（例如检测电池电压或功率计输出）。
    * **Aux DAC (DAC1 & DAC2)**: 输出一个稳定的直流电压。
        * **用途**：常用于控制外部压控振荡器 (VCO) 或给外部功率放大器 (PA) 提供偏置电压。
        * **Enable in RX/TX/ALERT**: 允许你在不同的工作状态下（收/发/预备）自动改变这个输出电压。
* <u>c. GPO Control (通用输出控制)</u>
    GPO 是芯片上的四个物理引脚（GPO 0-3），用于输出数字高低电平。
    * **GPO Manual Mode**: 手动控制这四个引脚的开关。
    * **自动逻辑 (GPO 0-3 各个板块)**:
        * **Enable RX/TX State**: **这是最实用的功能**。你可以设置：当芯片进入接收状态时，某个 GPO 自动变高电平。
        * **用途**：用于控制板载的**外部 T/R 开关**（收发切换开关）或外部放大器的使能引脚。
        * **RX/TX Delay**: 设置一个延时。比如先让外部开关切换好，再启动芯片内部的射频链路，防止信号被截断或损坏设备。
* <u>d. Control OUTS</u>
    * 通过索引 (Index) 监控芯片内部状态机的具体状态输出，通常用于高级调试。

</br>

**[总结]**：这一页就是芯片的“管家功能”。

* **想看芯片热不热？** 配置 `Temp Sensor`。
* **想控制外接的射频开关或放大器？** 使用 `GPO Control`。
* **想给外部电路提供一个可调电压？** 使用 `Aux DAC`。

</br>

**7）MISC（杂项-校准与补偿设置）设置页**

这是 **MISC（杂项）** 选项卡，主要负责接收端的实时校准优化，解决“中心尖峰”和“镜像干扰”。

![][p15]

* <u>a. DC Offset Tracking (直流偏移追踪)</u>
         这是为了消除频谱中心那个讨厌的**直流尖峰**。
    * **RX Freq > 4 GHz / < 4 GHz**: 针对不同频段，由于电路噪声特性不同，需要分别设置参数。
    * **Attenuation / Count**: 内部校准环路的步进和统计次数。数值决定了系统消除中心尖峰的速度和深度。
    * **Update Event Mask**: 决定在什么事件发生（如增益改变）时触发直流偏移更新。

* <u>b. QEC Tracking (正交误差补偿)</u>
    * **Slow QEC**: **慢速正交校准**。
        * 开启后，系统会持续在后台缓慢修正 I 通道和 Q 通道之间的增益和相位偏差。
    * **作用**：大幅抑制频谱上的**镜像干扰**（即中心点对称位置的虚假信号）。

</br>

**[总结]**：这一页主要是**给信号“去杂质”**的。`DC Offset` 负责去掉中心的**尖点**，`QEC` 负责去掉对称位置的**虚假镜像**。通常保持默认，并勾选 `Slow QEC` 即可。

</br>

**8）BIST (Built-In Self-Test，内置自检) 页**

这是 **BIST (Built-In Self-Test，内置自检)** 页面。它的核心作用是**排除故障**，帮你确认问题是出在射频天线、模拟芯片内部，还是 FPGA 接口。

![][p16]

* **Bist TONE (单音自检)**
    * 在数字域生成一个干净的单频信号（Sine wave）。
    * **用途**：如果你能看到这个单音，说明数字接口（LVDS/CMOS）和 FPGA 接收是通的。
* **Bist PRBS (伪随机序列自检)**
    * 生成随机码流进行压力测试。
    * **用途**：用来测试数字接口的数据传输是否有误码（BER测试）。
* **Loopback (回环模式)**
    * 将发射端的数据直接在芯片内部“导流”回接收端。
    * **用途**：**最常用的排障手段**。如果你开启回环后信号正常，但关掉后收不到天线信号，说明问题出在外部射频电路或天线上。
* **Level / Frequency / Channel Mask**
    * 设置自检信号的幅度、频率偏移（以采样率为基准分频）以及开启哪一个通道（I/Q、通道1/2）进行测试。

</br>

**[总结]**：这页就是“内部自连”测试。怀疑硬件坏了？先跑个 `Loopback` 或 `Bist TONE`，能通就代表芯片核心和接口没问题。

</br>

### 4. 四大核心模块详解（实战演示）
#### 4.1 Time Domain 实验

要实现 **Loopback（回环）** 模式，让 PlutoSDR 发射的信号不经过天线，直接在芯片内部“导流”回接收端，请按照以下步骤操作：

![][p19]

设置好之后，点击 `File -> New Plot`：

* **Plot Type**: 选择 `Time Domain`。
* **Samples**：改为 `30us`。
* **Channels**: 勾选 `voltage0` (I路) 和 `voltage1` (Q路)。
* 点击 **Run**。

![][p20]

我们可以在时域看到两个相位差 90° 的正弦波，且频率刚好等于我们在 FPGA Settings 里设定的单音频率，说明回环成功（30us 有 3 个周期数据）。

这种模式是排查问题的神器：如果 Loopback 模式波形完美，但关掉后接天线收不到信号，那就说明我们的天线、馈线或者外部电磁环境出了问题。

</br>

#### 4.2 Frequency Domain 实验

体验 **Frequency Domain（频域/频谱）** 模式可以让你直观地看到信号的中心频率、带宽以及谐波。在 **Loopback（回环）** 模式下，由于信号不经过空间衰减和天线干扰，频谱会非常干净，是观察数字调制效果的最佳时机。

在上面实验基础上：

- 在**FPGA Settings**中，将 `Frequency` 从 0.1MHz 改为 10M
- 打开 **Plot** 窗口，进行以下关键设置：
    - **Select Plot Type**: 在下拉菜单中选择 **Frequency Domain**。
    - **Choose Channels**：勾选 `voltage0`（此时软件通常会自动识别 IQ 对，显示为 `RX1` 或 `Combined` 信号）。
    - **Window**: 选 `Blackman-Harris` 或 `Hamming`，这会让频谱的“毛刺”更少，主峰更清晰。
    - **Average**: 如果波形跳动太快，可以将 Average 设为 `5` 或 `10`，让频谱显示更平滑。
- **Run**: 点击 **Play**

![][p21]

我们会看到一个在 `+10.0 MHz` 位置的强信号尖峰。

</br>

#### 4.3 Constellation(X vs Y) 实验

在 **Loopback（回环）** 模式下观察 **Constellation（星座图）**，是验证射频链路 **I/Q 正交性** 和 **信号质量** 最直观的方法。

星座图本质上是把 I 路数据作为 X 轴，Q 路数据作为 Y 轴。在 2x2 PlutoSDR 上，这就是观察 `voltage0` vs `voltage1`。

在上面实验基础上：

- 在**FPGA Settings**中，将 `Frequency` 从 10MHz 改为 1M
- 打开 **Plot** 窗口，进行以下关键设置：
    - **Select Plot Type**: 在下拉菜单中选择 **Constellation (X vs Y)**。
    - **Choose Channels**:
        * **X轴 (Horizontal)**: 选择 `voltage0` (RX1 I)。
        * **Y轴 (Vertical)**: 选择 `voltage1` (RX1 Q)。
- **Run**: 点击 **Play**

![][p23]      

**你会看到什么？（体验重点）**

- **情况 A：看到一个完美的圆圈**
    当你发射的是 **单音信号 (CW)** 时，I 和 Q 是相位差 90 度的正弦波。
    * **物理意义**：$I = \cos(\omega t), Q = \sin(\omega t)$。在坐标系中，这就构成了一个**圆**。
    * **观察点**：如果圆很圆，说明 I 和 Q 两路增益平衡，相位严格正交。
- **情况 B：看到一个椭圆**
    * **原因**：这说明存在 **I/Q 不平衡**（Gain Imbalance 或 Phase Error）。
    * **动手试试**：去 **AD936X Advanced -> MISC** 页面，取消勾选 `Slow QEC`。你会发现圆圈可能会变成扁平的椭圆，这代表此时镜像抑制变差。重新勾选后，校准算法会自动把椭圆“修”回正圆。
- **情况 C：看到一个“甜甜圈”或粗线条的圆**
    * **原因**：这代表信号中存在 **噪声**。
    * **动手试试**：在 **AD936X** 主页面调大 **Hardware Gain**，你会发现圆圈的线条变细（信噪比提高）；如果增益调得太大导致过载，圆圈会变成一个**正方形**（波形被削顶了）。
- **情况 D：圆圈不在中心（偏移）**
    * **原因**：存在 **DC Offset（直流偏移）**。
    * **动手试试**：在 **MISC** 页面反复开关 `DC Offset Tracking`。你会看到圆圈的中心点在坐标轴原点附近跳动或移动。

</br>

**进阶玩法：体验数字通信**

如果你在电脑上运行 GNU Radio 并发送 **QPSK** 或 **16QAM** 信号给 PlutoSDR，并在 **DDS Mode** 选为 `DAC Buffer Output`（见我们第一次对话的设置）：

* **QPSK**：你会看到星座图上有 **4 个点**。
* **16QAM**：你会看到分布整齐的 **16 个点点阵**。
* **同步演示**：如果点在不停地旋转，那是由于发射端和接收端的频率偏移（Carrier Frequency Offset）造成的。

</br>

**[总结建议]**：

在 Loopback 模式下看星座图，主要是看它**圆不圆**（代表正交性好坏）以及**线条细不细**（代表噪声高低）。对于 2x2 设备，你可以分别对比 RX1 和 RX2 的圆圈质量，检查两个通道的硬件一致性。

</br>

#### 4.4 Cross Correlation 实验

体验 **Cross Correlation（互相关）** 模式可以让你直观地衡量两个信号之间的“相似度”以及它们在时间轴上的相对偏移。在 **2x2 PlutoSDR** 的 **Loopback（回环）** 模式下，这是观察 RX1 和 RX2 通道同步性和一致性的最佳手段。

在上面实验基础上：

* **FPGA Settings**：确保 **DDS Mode** 为 `One CW Tone`，并将 `Frequency` 设为 `1.0 MHz`（方便观察包络）。
* **打开 Plot 窗口**，进行以下关键设置：
    * **Select Plot Type**: 在下拉菜单中选择 **Cross Correlation**。
    * **Choose Channels**: 必须**同时勾选 4 路通道**（`voltage0`、`voltage1`、`voltage2`、`voltage3`）。因为软件需要完整的复数（IQ）对来计算 RX1 ($I_1+jQ_1$) 与 RX2 ($I_2+jQ_2$) 的相关性。
    * **Average**: 设为 `5` 左右以获得平稳的波形。
* **Run**: 点击 **Play**。

![][p24]

我们会看到一个像“纺锤体”的波形，是典型的**两个有限带宽信号（或带窗的正弦波）的互相关函数**。

* **中心峰值（Peak）**：波形最中间、幅度最大的那个点，代表了 RX1 和 RX2 **最契合**的时刻。
* **对称性**：波形关于中心左右对称，说明两路信号的频率成分非常一致，只是在相位或时间上有一个偏移。
* **包络形状**：这种菱形/三角形状的包络，反映了 IIO-Oscilloscope 内部计算互相关时所取的数据块大小（Window Size）。

**核心观察点：时间偏移（Time Lag）**：请仔细看图片顶部的横轴（单位通常是 **$\mu s$** 或 **Samples**）：

* **如果峰值在 0 刻度**：说明你的 PlutoSDR 内部 RX1 和 RX2 路径是完美同步的。
* **如果峰值偏离 0**：偏移的数值就是信号从 TX 发出到两路 RX 接收之间的时间差。
* 在 **Loopback** 模式下，这个偏移通常极小（接近 0），因为所有信号都在芯片内部跑。

这里我们会看到一个完美的“纺锤形”复相关包络：

* **中心峰值**：位于横轴 `0` 刻度附近，代表两路信号完全同步对齐。
* **物理意义**：该实验证明了 2x2 硬件链路在当前频率（如 2400MHz）下具有极佳的通道一致性。

</br>

**额外补充**：

我们也可以通过开启芯片内置的 TONE 或伪随机序列（PRBS）发生器来产生发送信号：

* **FPGA Settings**：确保 **DDS Mode** 为 disable（因为我们要用 BIST 硬件生成的信号，而不是 FPGA 生成的单音）。
* **BIST 页面配置**：
    - （如果想要也产生单音信号）Bist TONE: 点击下拉菜单，选择 Injection Point TX
    - （如果想要产生伪随机信号）Bist PRBS: 点击下拉菜单，选择 Injection Point TX

如果选择使用伪随机信号，此时，你会发现原来的“纺锤形”长包络消失了，取而代之的是一个位于中心位置、**底座极宽但尖端极其细锐的脉冲峰**：

![][p25]

* **实验结论**：
    * **单音（Tone）**：是窄带信号，自相关性在时间上是发散的。
    * **PRBS（伪随机码）**：是宽带信号，其功率分布在很宽的频段内。在时域上，只有当序列完全对齐时，相关值才会瞬间爆表。
    * **应用场景**：这正是 **CDMA 通信（码分多址）** 和 **扩频通信** 的物理基础。通过这种尖锐的脉冲，接收机可以从嘈杂的底噪中精准锁定信号的到达时间（Time of Arrival）。

</br>

### 5. 结语

掌握 **IIO Oscilloscope** 并不只是为了学会点按几个按钮，而是为了建立一种“硬件直觉”。当你能通过寄存器读写准确判断出一个信号是由于 LMT 过载还是 I/Q 不平衡导致的畸变时，你就已经从“应用层玩家”跨越到了“底层定义者”的行列。

PlutoSDR 的魅力在于它完全透明的硬件链路，而 IIO-Osc 正是开启这扇大门的钥匙。在下一集中，我们将把视野从底层寄存器拉回到系统级开发——**我们将学习如何将 GNU Radio 的强大处理能力与 IIO Oscilloscope 的深度调试性能强强联手**，实现真正意义上的“软硬合一”，敬请期待！

</br>

### 参考链接：

[[1]. ADI 百科 —— IIO示波器详细介绍][#1]     
[[2]. Github —— iio-oscilloscope][#2]        
[[3]. ADI 百科 —— What is libiio?][#3]         

[#1]:https://wiki.analog.com/resources/tools-software/linux-software/iio_oscilloscope
[#2]:https://github.com/analogdevicesinc/iio-oscilloscope     
[#3]:https://wiki.analog.com/resources/tools-software/linux-software/libiio#how_to_build_it     



[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_about.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_show1.png        
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_connect.png     
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_dmm.png        
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_debug.png    
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_diagram.png     
[p7]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_control_global_settings.png     
[p8]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_control_receive_chain.png    
[p9]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_control_transmit_chain.png     
[p10]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_control_fpga_setting.png      
[p11]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_advanced_ensm.png     
[p12]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_advanced_eLNA.png
[p13]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_advanced_rssi.png    
[p14]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_advanced_tx_monitor.png
[p15]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_advanced_misc.png    
[p16]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_advanced_bist.png     
[p17]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_advanced_gain.png     
[p18]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_ad936x_advanced_aux.png
[p19]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_plot_time.png     
[p20]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_plot_time_show.png    
[p21]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_plot_frequency_show.png    
[p22]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_plot_iq_show.png              
[p23]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_plot_iq_show2.png       
[p24]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_plot_cross_correlation_show.png       
[p25]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/iio_oscilloscope_plot_cross_correlation_show2.png    


