### 1. 为什么在玩转 USRP 后，你一定要尝试 PlutoSDR？

如果你跟着我的教程走过了 USRP 的“入门、基础、进阶”阶段，你已经掌握了 SDR 的核心逻辑。但随着实验的深入，你可能会发现 USRP B210 虽然强大，但在某些场景下略显“沉重”：USRP B210 虽然是工业级的性能怪兽，但它高度依赖 Host 电脑的 CPU 算力，且体积和功耗限制了它在户外、无人机或独立监测场景下的发挥。

**PlutoSDR (ADALM-PLUTO)** 的出现填补了这一空白。尤其是国内优秀的开源硬件方案（如**唐朝击剑鱼丸**版：ZYNQ7010 + AD9363），让我们可以直接步入**嵌入式 SDR** 的殿堂。它不仅是一个射频前端，更是一个带 Linux 系统的独立处理平台，是无线电发烧友进阶“全栈工程师”的必经之路。

</br>

### 2. 硬件硬核对比：大块头 vs 小钢炮

通过下面这两张硬件实拍图，我们可以直观地感受两者的设计哲学差异：

条目 | **Ettus USRP B210：FPGA 性能怪兽** | **PlutoSDR (DIY Z7010 版)：嵌入式 SDR 的“小钢炮”**
---|---|---
**核心架构** | 基于 **Xilinx XC7K325T**（Kintex-7 系列，拥有强大的逻辑资源） | 基于 **Xilinx Zynq-7010**，这不只是 FPGA，它内部集成了一个 **双核 ARM Cortex-A9 处理器**，支持运行独立的 Linux 系统
**射频芯片** | **AD9361**，支持原生双通道收发（2x2 MIMO），拥有极高的动态范围  | **AD9363**（可以通过软件“白嫖”升级，解锁接近 AD9361 的性能）。厂家出厂已做好全频带移植，频率覆盖 **70MHz - 6GHz**
**接口**    | USB 3.0                                                  | 支持 **千兆以太网** 与 **USB OTG**
**工作模式** | **Host-Based**，通过 USB 3.0 将原始 I/Q 数据传给电脑，编解码全靠电脑 CPU | **独立运行**，你可以通过 SSH 登录到 Pluto 内部的 Linux 系统，直接在板子上运行 Python 脚本或 C 程序
**优势**    | 带宽大（高达 56MHz 实时带宽），适合处理高吞吐量的协议（如 4G/5G 基站模拟） | **高集成度**，自带板载存储（SD 卡）存放固件，真正实现脱离电脑工作
**价格**    | 1500+                                                    | 500+ （比 hackrf 便宜）
**图片**    | ![][p3]  | ![][p2]

</br>

### 3. 快速上手：让你的 PlutoSDR 动起来

本教程参考《闲鱼店铺“唐朝击剑鱼丸”DIY 出品的 ZYNQ7010+AD9363 软件无线电资料》，基于移植好的固件，省去了复杂的底层编译过程，真正做到“开箱即用”：

```
➜  plutoSDR tree -L 2
.
├── Fish_Ball_SDR开箱必看.pdf
└── Pluto固件相关
    ├── 出厂所带SD卡为固件pluto-fw-v0.38.txt
    ├── 基于pluto-fw-v0.38移植固件（支持网口和USB）
    └── 基于ZC706-FMCOMMS2-3移植固件（支持网口）
```

#### 第一步：准备 SD 卡启动盘

将 SD 卡格式化为 FAT32 格式，将 SD 卡固件下的文件复制进 SD 卡：     

```
sudo mkfs.vfat -F 32 /dev/sdb
sudo fdisk /dev/sdb  # d 删除所有分区，o 创建一个 DOS 分区表，n 创建一个主要分区，a 使分区成为启动分区，w 保存
sudo mkfs.vfat -F 32 /dev/sdb1
sudo mount /dev/sdb1 /mnt/sd1
cd /mnt/sd1
sudo cp -r ~/Desktop/XXX/Pluto固件相关/基于pluto-fw-v0.38移植固件（支持网口和USB）/SD卡固件/* ./
cd ..
sudo umount -f /dev/sdb1
```

#### 第二步：硬件连接与启动

1）将 SD 卡插入 SD 卡槽，拨码开关设置为 SD 卡启动模式，插入网线（或 USB 口），通电    
2）默认固件登录账号：root 密码:analog    
3）若使用 USB 口，需提前安装 USB 虚拟网卡驱动，正常状态即可使用官方上位机    
4）若使用网口，需要设置板卡 IP，登陆后串口输入 `ifconfig eth0 192.168.1.xx`，正常状态即可使用官方上位机    

**注意**：由于 AD9363 + Zynq 的发热量较大，长时间工作建议使用 CNC 散热壳体或风扇。

</br>

### 4. 上位机如何发现你的 SDR？

在 Host 电脑（以 Linux 环境为例）下，我们需要配置好工具链才能顺利“握手”。

**1）安装依赖**：安装 `soapysdr`、`soapyplutosdr` 插件：

```
# [1]-ubuntu24.04 上工具安装命令（基于 docker 的 ubuntu 环境，如果你直接有 ubuntu24.04 环境可以跳过 zsh 及之前的命令）
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
    ubuntu:gnuradio-310 bash
sudo chmod +666 PLUTOSDR
zsh

sudo apt update
sudo apt install usbutils # lsusb 查看设备

sudo apt install -y avahi-daemon dbus   
sudo mkdir -p /run/dbus   # 确保 dbus 运行所需的目录存在
sudo dbus-daemon --system --fork # 以后台模式启动 dbus-daemon
sudo avahi-daemon -D      # 后台模式运行

sudo apt install -y libiio-utils libiio-dev  # 底层驱动（iio_info -s）


-># 直接在 ubuntu 上用下面两会导致找不到设备，需要用手动编译安装方式：
-># sudo apt-get install soapysdr-module-all 
-># sudo apt-get install soapysdr-tools 
sudo apt install git cmake build-essential libsoapysdr-dev libiio-dev libad9361-dev
git clone https://github.com/pothosware/SoapyPlutoSDR.git
cd SoapyPlutoSDR
mkdir build && cd build
cmake ..
make
sudo make install
sudo ldconfig

# [2]-arch linux 上工具安装命令
sudo pacman -S libiio   
sudo pacman -S soapyplutosdr # 这个一定要，不然找不到（soapyplutosdr 是 soapysdr 的底层模块）
sudo pacman -S soapysdr
sudo pacman -S soapy_power
```

</br>

| 工具/组件 | Arch Linux 包名 | Ubuntu 24.04 安装方式 | 说明 |
| :--- | :--- | :--- | :--- |
| **`iio_info`** | `libiio` | `sudo apt install libiio-utils` | 底层硬件通信工具，通过 `libiio` 直接访问 PlutoSDR |
| **SoapySDR 的 Pluto 插件** | `soapyplutosdr` | **源码编译** `SoapyPlutoSDR` | 依赖 `libiio` |
| **`SoapySDRUtil`** | `soapysdr` | **源码编译 `SoapyPlutoSDR` 后自动获得**<br>（`apt` 方式无效，`soapysdr-tools` 无法识别 Pluto） | SoapySDR 核心诊断工具，需要 Pluto 插件才能看到设备 |
| **`soapy_power`** | `soapy_power` | `sudo apt install python3-soapy-power`<br>（或源码编译） | 基于 SoapySDR API 的频率扫描应用 |


> ⚠️ **特别强调**：在 Ubuntu 24.04 中，**不要**使用 `sudo apt install soapysdr-module-all` 或 `sudo apt install soapysdr-tools`，这会导致找不到 Pluto 设备。唯一有效的方式是**源码编译 `SoapyPlutoSDR`**。

它们之间的层次结构为：

```
┌─────────────────────────────────────────────────┐
│  应用层                                          │
│  soapy_power  │ 其他 SoapySDR 应用               │
└───────────────────────┬─────────────────────────┘
                        │ 调用 SoapySDR API
┌───────────────────────▼─────────────────────────┐
│  核心层（SoapySDR 框架）                          │
│  SoapySDRUtil  （由插件提供设备发现能力）           │
│  Ubuntu：通过编译 SoapyPlutoSDR 获得              │
│  Arch：  soapysdr 包                             │
└───────────────────────┬─────────────────────────┘
                        │ 通过插件接口
┌───────────────────────▼─────────────────────────┐
│  硬件适配层（SoapySDR 插件）                       │
│  SoapyPlutoSDR  （即 soapyplutosdr）             │
│  Ubuntu：必须源码编译                             │
│  Arch：  soapyplutosdr 包                        │
└───────────────────────┬─────────────────────────┘
                        │ 调用 libiio API
┌───────────────────────▼─────────────────────────┐
│  设备通信层                                      │
│  libiio  （提供 iio_info 等工具）                 │
│  Ubuntu：libiio-utils                           │
│  Arch：  libiio                                 │
└───────────────────────┬─────────────────────────┘
                        │ USB / 网络
┌───────────────────────▼─────────────────────────┐
│  硬件层                                          │
│  ADALM-PLUTO (PlutoSDR)                         │
└─────────────────────────────────────────────────┘
```


<u>备注：`ADI IIO Oscilloscope` 由 ADI 开发的图形化界面工具，专门用于实时展示和配置基于 Linux IIO（Industrial I/O）子系统的设备数据。对于我们正在使用的 PlutoSDR 来说，它是官方推荐的可视化调试工具。接下来单独一个课程介绍。</u>

</br>

**2）探测命令**：

在网口和 USB 都连接的情况下，在 linux 上位机上通过 `sudo SoapySDRUtil --find` 会报找不到设备：

```bash
➜  ~ sudo SoapySDRUtil --find
######################################################
##     Soapy SDR -- the SDR abstraction library     ##
######################################################

ERROR: Unable to create Avahi DNS-SD client :Daemon not running
[WARNING] Unable to scan ip: -26

No devices found! 
```

这个错误表明 SoapySDR 在尝试查找设备时遇到了问题，特别是与 Avahi DNS-SD 客户端相关的错误。Avahi 是一个用于局域网内设备发现的服务，SoapySDR 可能依赖它来扫描和发现可用的 SDR（软件定义无线电）设备。

```bash
sudo systemctl start avahi-daemon

SoapySDRUtil --find
SoapySDRUtil --probe # 可以查看详细信息（发现只有 1收，1发！！！实际上有俩，用 iio 可以测试出来）
SoapySDRUtil --probe=ip:pluto.local
SoapySDRUtil --device=ip:pluto.local --rate=1e6 --direction=RX
SoapySDRUtil --device=ip:pluto.local --rate=1e6 --direction=TX
SoapySDRUtil --info

iio_info -s

Available contexts:
	0: 192.168.1.100 (FISH Ball PlutoSDR Rev.A (Z7010-AD9361)), serial= [ip:pluto.local]
	1: (coretemp,acpitz,asus on ASUSTeK COMPUTER INC.) [local:]

# 下面的比较特殊，在 ubuntu 上属于 python 工具
# soapy_power 是一个基于 SoapySDR 的频谱扫描工具（类似于 rtl_power）
# soapy_power -f 2.4G:2.5G  --even --fft-window boxcar --remove-dc -g 40 -B 2M -T 1 -e 10
```

</br>

### 5. “小钢炮”能干哪些特殊的实验？（预告篇）

PlutoSDR 的魅力在于它的“独立性”，这让我们能玩出 USRP 玩不出的花样：

* **【实验 A】脱机信号监测站**：将 Python 脚本部署在 Pluto 内部，连接移动电源丢在楼顶，定时监测周围 Wi-Fi 或航空信号，并将结果通过网口发送回服务器。
* **【实验 B】SATSAGEN 频谱分析仪**：配合 Windows 下的 SATSAGEN，将 Pluto 变成一台带扫描源的**标量网络分析仪**，测量天线驻波和滤波器特性。
* **【实验 C】嵌入式数字图像传输**：直接在板载 ARM 处理器上完成视频压缩与 QPSK 调制，实现超低延迟的图传。
* **【实验 D】反无人机频率干扰**：利用其便携性，开发手持式的全频带干扰监测设备。

</br>

### 6. 总结：该如何选择？

* 如果你追求**极高的实时带宽**和**多天线 MIMO**，**B210** 是你的不二之选。
* 如果你追求**便携、嵌入式开发、高性价比**，或者想学习 **Zynq SoC 架构**，那么 **PlutoSDR** 将打开你新世界的大门。

</br>

### 7. 软件工具链推荐

1. **SDR++**：目前最流畅的 GUI 接收软件，原生支持 libiio。
2. **IIO-Oscilloscope**：AD 官方“示波器”，观测星座图、调试射频寄存器的神器。
3. **MATLAB/Simulink**：Pluto 是 MATLAB 的官方教学板，支持点击式系统建模。

</br>

### 8. 结语

PlutoSDR 绝非 USRP 的“低配替代品”，它是你通向**嵌入式无线电架构师**的阶梯。在下一集中，我们将学习**射频链路的“显微镜”**—— 掌握 IIO Oscilloscope 深度调试。


[#1]:https://github.com/pothosware/SoapyPlutoSDR    
[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202601/B210_TCJJYW.png        
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202501/zync7010_ad9363.png      
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202601/B210_TCJJYW_arch.png
