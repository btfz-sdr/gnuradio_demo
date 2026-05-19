### 1. 开场

在前三集的探索中，我们已经成功搭建了 GNU Radio 的开发环境，并实现了 PlutoSDR 与 PC 端的联动。然而，在真正的嵌入式 SDR 应用场景中，PlutoSDR 往往需要扮演“独立战士”的角色——脱离笨重的 PC 宿主机，直接在内部的 ARM 处理芯片上运行信号处理逻辑。

这就好比为初生的婴儿“断开脐带”。

本集我们将深入 PlutoSDR 内部那颗 Zynq 7010 SoC，探索其轻量级的 Linux 系统。由于 Pluto 硬件资源有限，无法像 PC 一样直接在本地编译大型程序，因此我们将通过交叉编译（Cross-Compilation）技术，在强大的 Docker 容器中构建出适配 ARM 架构的可执行文件。从最简单的 "Hello Pluto" 到基于 `libiio` 的底层射频驱动实战，本篇将带你跨越 GUI 的束缚，真正掌握嵌入式无线电开发的硬核力量。

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

### 3. 连接设备

之后可以通过 `ssh root@pluto.local` (密码：analog) 命令连接进入 PlutoSDR 中的 Linux 系统：

```   
➜  ~ ssh root@pluto.local
The authenticity of host 'pluto.local (192.168.1.109)' can't be established.
ED25519 key fingerprint is SHA256:8202KpNbfuNwLU2i9ArQSBz5+/jvrjWrcLGVNZJ3YvA.
This key is not known by any other names.
Are you sure you want to continue connecting (yes/no/[fingerprint])? yes
Warning: Permanently added 'pluto.local' (ED25519) to the list of known hosts.
root@pluto.local's password: 
Welcome to:
______ _       _        _________________
| ___ \ |     | |      /  ___|  _  \ ___ \
| |_/ / |_   _| |_ ___ \ `--.| | | | |_/ /
|  __/| | | | | __/ _ \ `--. \ | | |    /
| |   | | |_| | || (_) /\__/ / |/ /| |\ \
\_|   |_|\__,_|\__\___/\____/|___/ \_| \_|

a17fe-dirty
https://wiki.analog.com/university/tools/pluto
# 
``` 

并能通过常见的 linux 命令查看资源和网络：`free`、`df -h`、`ifconfig`。

此时我们发现，此 linux 系统中并没有 gcc、python 等基础的开发环境，更别说 GNU Radio 这种重量级应用了。

</br>

### 4. 交叉编译并运行

要让 PlutoSDR 实现“脱机运行”，需要从依赖 PC 端 GNU Radio 的图形化逻辑，转向基于 libiio 的 C/C++ 编程。Pluto 内部运行的是一个轻量级的 Buildroot Linux 系统，资源有限，因此不能在 Pluto 内部直接编译，必须在我们的 PC（Docker 环境）中进行**交叉编译**。

#### 1）理解核心：libiio 与 交叉编译

在 GNU Radio 中，我们使用的是 `PlutoSDR Source/Sink` 模块，它们底层调用的是 `libiio`。在脱机模式下，你需要直接编写 C 或 C++ 代码来调用 `libiio` 操控射频前端（AD936x），然后将代码编译为适用于 **ARM Cortex-A9 (Zynq)** 架构的可执行文件。

> 备注：在《[第一集：从 USRP 到 PlutoSDR，开启嵌入式无线电新篇章][#1]》有详细介绍调用链。

</br>

#### 2）获取交叉编译工具链

PlutoSDR 的官方 SDK 推荐使用基于 `arm-linux-gnueabihf-` 的工具链。

在你的 Docker/Ubuntu24.04 环境中，不能直接使用 apt 安装，这会导致版本过新，我们需要去官网下载对应版本的工具链，在 pluto sdr linux 系统中运行下面命令查看版本：

```
# /lib/libc.so.6
GNU C Library (GNU libc) stable release version 2.25, by Roland McGrath et al.
Copyright (C) 2017 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.
There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
Compiled by GNU CC version 7.3.1 20180425 [linaro-7.3-2018.05 revision d29120a424ecfbc167ef90065c0eeb7f91977701].
Available extensions:
	crypt add-on version 2.1 by Michael Glad and others
	GNU Libidn by Simon Josefsson
	Native POSIX Threads Library by Ulrich Drepper et al
	BIND-8.2.3-T5B
libc ABIs: UNIQUE
For bug reporting instructions, please see:
<http://www.gnu.org/software/libc/bugs.html>.
```

因此去 [arm-gnu-toolchain-downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 官网，找到最老版本系列 [GNU Toolchain releases from Linaro (discontinued) for versions 4.9-2016.02 to 7.5-2019.12](https://developer.arm.com/Downloads/-/Legacy%20Linaro%20GNU%20Toolchains)，点击展开 Downloads:7.3-2018.05，找到 `gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz` 下载：

```
wget https://developer.arm.com/-/cdn-downloads/permalink/legacy-linaro-gnu-toolchains/7.3-2018.05/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
mkdir toolchain
tar -xJf gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz -C ./toolchain
rm gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
```

</br>

#### 3）交叉编译第一个 hello pluto 并运行

<div style="display: flex; align-items: flex-start; gap: 40px;">
<div>

a.首先注意我们的目录结构：

```
➜  plutosdr_iio_gcc_demo tree -L 2
.
├── 01-hello_plutosdr
│   ├── hello_plutosdr.c
│   └── makefile
└── toolchain # 注意上一步的 toolchain 在这里
    └── gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
```

</br>
</br>

b.编写一个最简单的 `hello_plutosdr.c` 程序如下：

```
#include <stdio.h>

int main(void) {
    printf("hello pluto sdr\n");
    return 0;
}
```

</div>
<div>

c.再编写一个用于编译、清理、运行的 makefile 文件：

```
# 1. 定义工具链根目录 (使用绝对路径)
TOOLCHAIN_PATH = $(CURDIR)/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf

# 2. 拼接出具体的编译器和工具
CC      = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-gcc
CXX     = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-g++
STRIP   = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-strip

# 3. 编译规则
build:
	$(CC) hello_plutosdr.c -o hello_plutosdr
	# 可选：减小二进制体积，方便传输到 PlutoSDR
	$(STRIP) hello_plutosdr

clean:
	rm -rf hello_plutosdr

run:
	scp -O hello_plutosdr root@pluto.local:~/
	ssh root@pluto.local "chmod +x ~/hello_plutosdr && ~/hello_plutosdr"
```

</div>
</div>

</br>

最后，我们只要在 Docker/Ubuntu24.04 环境中运行下面命令，即可体验交叉编译与代码运行：

```
# 清空编译过程和目标文件
➜  01-hello_plutosdr make clean
rm -rf hello_plutosdr

# 编译（调用交叉编译工具链）
➜  01-hello_plutosdr make build
/home/gnuradio/PLUTOSDR/iio/01-hello_plutosdr/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc hello_plutosdr.c -o hello_plutosdr
# 可选：减小二进制体积，方便传输到 PlutoSDR
/home/gnuradio/PLUTOSDR/iio/01-hello_plutosdr/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-strip hello_plutosdr

# scp 将生成的产物复制到 pluto sdr linux 的 home 目录下
# ssh 登陆到 pluto sdr linux 环境，赋予 hello_plutosdr 可执行权限，并执行
# 最终可以看到我们交叉编译的产物正确执行，打印：hello pluto sdr
➜  01-hello_plutosdr make run
scp -O hello_plutosdr root@pluto.local:~/
root@pluto.local's password: 
hello_plutosdr                                                                                                                                              100% 5624     3.2MB/s   00:00    
ssh root@pluto.local "chmod +x ~/hello_plutosdr && ~/hello_plutosdr"
root@pluto.local's password: 
hello pluto sdr
```

</br>

#### 4）交叉编译基于 libiio 的可控制 SDR 的程序

a.交叉编译能直接与 IIO 通信的程序**需要依赖 libiio 支持**，由于 plutosdr 使用 Zynq7010 芯片，ARM32 (armv7l)，并且通过查看 pluto 系统上 `/lib/libiio` 的版本发现使用的是 **0.25 版本**，因此我们从 [Github 拉取对应的库][#2]，放到 toolchain 中，用来给复杂交叉编译程序提供 iio 库支持：

```
cd toolchain
mkdir libiio
wget https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz
tar -xzf libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz -C libiio
rm -rf libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz
```

备注：通过 `uname -m` 或 `cat /proc/cpuinfo` 也能看出 CPU

</br>

<div style="display: flex; align-items: flex-start; gap: 40px;">
<div>

b.接着直接从 Github 下载官方 v0.25 版本 demo：[`ad9361-iiostream.c`][#3]，然后编写如下的 makefile：

```
# 1. 定义资源路径
# 定义工具链根目录 
TOOLCHAIN_PATH = $(CURDIR)/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
# 定义 libiio 路径 
IIO_BASE = $(CURDIR)/../toolchain/libiio

# 2. 拼接出具体的编译器和工具
CC      = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-gcc
CXX     = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-g++
STRIP   = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-strip

# 3. 编译选项
# -I 指定头文件搜索路径
# -L 指定库文件搜索路径
# -liio 链接 libiio 库
# -Wl,--allow-shlib-undefined [偷懒做法] 并告诉链接器只链接 libiio，“不要管那些找不到的子符号”
CFLAGS  = -I$(IIO_BASE)/usr/include
LDFLAGS = -L$(IIO_BASE)/usr/lib/arm-linux-gnueabihf \
          -L$(IIO_BASE)/lib/arm-linux-gnueabihf \
          -liio -Wl,--allow-shlib-undefined

# 4. 编译规则
build:
	$(CC) ad9361-iiostream.c -o ad9361-iiostream $(CFLAGS) $(LDFLAGS)
	# 可选：减小二进制体积，方便传输到 PlutoSDR
	$(STRIP) ad9361-iiostream

clean:
	rm -rf ad9361-iiostream

run:
	scp -O ad9361-iiostream root@pluto.local:~/
	ssh -t root@pluto.local "chmod +x ~/ad9361-iiostream; /bin/sh"
```

</div>
<div>

此时的目录结构为：

```
├── 01-hello_plutosdr
│   ├── hello_plutosdr.c
│   └── makefile
├── 02-ad9361-iiostream
│   ├── ad9361-iiostream.c
│   └── makefile
└── toolchain
    ├── gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
    └── libiio
```

</br>

接下来就可以 `make build` 然后 `make run`。

**注意 1：** `makefile` 中 `run` 命令最后不是直接运行，而是停在 plutosdr 内部，让用户自己输入 `./ad9361-iiostream` 运行。因为，该程序是阻塞式，在 plutosdr 内部运行能看到实时的 log。     
**注意 2：** 如果运行报 `Could not create RX buffer: Device or resource busy` 错误，是由于**硬件资源冲突**，通常是因为另一个进程（比如系统的 IIO 守护进程或其它的 SDR 软件）正在占用 AD9361 的 DMA 通道。

---

我们使用 `ps | grep iio` 发现是 `/usr/sbin/iiod` 占着，这个进程是 **IIO Daemon (`iiod`)**，它是 PlutoSDR 的核心服务之一：
- **它干什么的？**
    `iiod` 相当于一个“中介”：
    * **远程连接**：当你用电脑上的 **MATLAB、GNU Radio、Python (PyADI-IIO)** 或 **IIO Oscilloscope** 通过网络或 USB 连接 Pluto 时，都是在和这个 `iiod` 进程通信。
    * **资源管理**：它负责协调外部请求并与内核中的 IIO 驱动交互。
- **能杀吗？**
    可以杀，但有讲究：
    * **如果你杀掉它：** 所有的上位机（MATLAB/GNU Radio 等）将**无法**通过网络连接到这台 Pluto。
    * **对你当前任务的好处：** 杀掉它可以完全释放硬件资源。如果之前的上位机连接没有正常断开，导致 `Device or resource busy`，杀掉 `iiod` 是最彻底的解决办法。

不要直接用 `kill -9`，建议使用 `killall iiod` 优雅地停止它。

---

</div>
</div>

c.最后，我们在 pluto 中运行我们的 `ad9361-iiostream` 效果如下：

```
~ # ./ad9361-iiostream 
* Acquiring IIO context
* Acquiring AD9361 streaming devices
* Configuring AD9361 for streaming
* Acquiring AD9361 phy channel 0
* Acquiring AD9361 RX lo channel
* Acquiring AD9361 phy channel 0
* Acquiring AD9361 TX lo channel
* Initializing AD9361 IIO streaming channels
* Enabling IIO streaming channels
* Creating non-cyclic IIO buffers with 1 MiS
* Starting IO streaming (press CTRL+C to cancel)
	RX     1.05 MSmp, TX     1.05 MSmp
	RX     2.10 MSmp, TX     2.10 MSmp
	RX     3.15 MSmp, TX     3.15 MSmp
	RX     4.19 MSmp, TX     4.19 MSmp
	RX     5.24 MSmp, TX     5.24 MSmp
...
# Ctrl+c 停止运行
```

</br>

### 5. AD9361 基于 libiio 收发框架详解

4.4 中介绍的《交叉编译基于 libiio 的可控制 SDR 的程序》使用的代码是一个使用 **libiio** 库编写的 C 语言程序，主要用于演示如何与 **AD9361**（一款高性能、高度集成的射频收发器，常用于软件定义无线电 SDR，如 ADALM-Pluto）进行实时信号流传输。

简单来说，它的功能是：**通过计算机控制开发板，同时进行无线电信号的接收（RX）和发送（TX）。**

#### 1）初始化与配置 (Setup)

代码首先连接到 AD9361 设备，并设置了射频（RF）的关键参数：

* **中心频率 (LO Frequency)：** 设置为 2.5 GHz。
* **采样率 (Sample Rate)：** 设置为 2.5 MS/s（每秒 250 万次采样）。
* **带宽 (Bandwidth)：** 接收端 2 MHz，发送端 1.5 MHz。
* **端口选择：** 选择具体的物理天线接口（如 `A_BALANCED`）。

```
// Streaming devices
struct iio_device *tx;
struct iio_device *rx;

// RX and TX sample counters
size_t nrx = 0;
size_t ntx = 0;

// Stream configurations
struct stream_cfg rxcfg;
struct stream_cfg txcfg;

// Listen to ctrl+c and IIO_ENSURE
signal(SIGINT, handle_sig);

// RX stream config
rxcfg.bw_hz = MHZ(2);   // 2 MHz rf bandwidth
rxcfg.fs_hz = MHZ(2.5);   // 2.5 MS/s rx sample rate
rxcfg.lo_hz = GHZ(2.5); // 2.5 GHz rf frequency
rxcfg.rfport = "A_BALANCED"; // port A (select for rf freq.)

// TX stream config
txcfg.bw_hz = MHZ(1.5); // 1.5 MHz rf bandwidth
txcfg.fs_hz = MHZ(2.5);   // 2.5 MS/s tx sample rate
txcfg.lo_hz = GHZ(2.5); // 2.5 GHz rf frequency
txcfg.rfport = "A"; // port A (select for rf freq.)

printf("* Acquiring IIO context\n");
if (argc == 1) {
	IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
}
else if (argc == 2) {
	IIO_ENSURE((ctx = iio_create_context_from_uri(argv[1])) && "No context");
}
IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

printf("* Acquiring AD9361 streaming devices\n");
IIO_ENSURE(get_ad9361_stream_dev(TX, &tx) && "No tx dev found");
IIO_ENSURE(get_ad9361_stream_dev(RX, &rx) && "No rx dev found");

printf("* Configuring AD9361 for streaming\n");
IIO_ENSURE(cfg_ad9361_streaming_ch(&rxcfg, RX, 0) && "RX port 0 not found");
IIO_ENSURE(cfg_ad9361_streaming_ch(&txcfg, TX, 0) && "TX port 0 not found");

printf("* Initializing AD9361 IIO streaming channels\n");
IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 0, &rx0_i) && "RX chan i not found");
IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 1, &rx0_q) && "RX chan q not found");
IIO_ENSURE(get_ad9361_stream_ch(TX, tx, 0, &tx0_i) && "TX chan i not found");
IIO_ENSURE(get_ad9361_stream_ch(TX, tx, 1, &tx0_q) && "TX chan q not found");

printf("* Enabling IIO streaming channels\n");
iio_channel_enable(rx0_i);
iio_channel_enable(rx0_q);
iio_channel_enable(tx0_i);
iio_channel_enable(tx0_q);
```

</br>

#### 2）数据缓冲管理 (Buffering)

程序在内存中创建了两个“大桶”（Buffer），每个大小为 1 MiS（1,048,576 个采样点）：

* **RX Buffer：** 用于存放从天线接收并数字化后的原始信号数据。
* **TX Buffer：** 用于存放准备发送给天线转换成电磁波的数据。

```
printf("* Creating non-cyclic IIO buffers with 1 MiS\n");
rxbuf = iio_device_create_buffer(rx, 1024*1024, false);
if (!rxbuf) {
	perror("Could not create RX buffer");
	shutdown();
}
txbuf = iio_device_create_buffer(tx, 1024*1024, false);
if (!txbuf) {
	perror("Could not create TX buffer");
	shutdown();
}

printf("* Starting IO streaming (press CTRL+C to cancel)\n");	
```

</br>

#### 3）实时信号处理循环 (The Main Loop)

这是代码最核心的部分，它在一个 `while` 循环中不断执行以下动作：
* **推送到发送端 (`iio_buffer_push`)：** 将 TX 缓冲区的数据发往硬件。
* **从接收端抓取 (`iio_buffer_refill`)：** 从硬件读取最新的接收数据填充到 RX 缓冲区。
* **处理接收到的信号：**
    * 代码演示了如何遍历接收缓冲区。
    * **示例操作：** 它把接收到的 I（实部）和 Q（虚部）信号进行了交换（Swap I and Q）。
* **生成发送信号：**
    * 代码演示了如何填充发送缓冲区。
    * **示例操作：** 它将所有发送数据设为 0（发送静默信号）。

```
while (!stop)
{
	ssize_t nbytes_rx, nbytes_tx;
	char *p_dat, *p_end;
	ptrdiff_t p_inc;

	// Schedule TX buffer
	nbytes_tx = iio_buffer_push(txbuf);
	if (nbytes_tx < 0) { printf("Error pushing buf %d\n", (int) nbytes_tx); shutdown(); }

	// Refill RX buffer
	nbytes_rx = iio_buffer_refill(rxbuf);
	if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); shutdown(); }

	// READ: Get pointers to RX buf and read IQ from RX buf port 0
	p_inc = iio_buffer_step(rxbuf);
	p_end = iio_buffer_end(rxbuf);
	for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {
		// Example: swap I and Q
		const int16_t i = ((int16_t*)p_dat)[0]; // Real (I)
		const int16_t q = ((int16_t*)p_dat)[1]; // Imag (Q)
		((int16_t*)p_dat)[0] = q;
		((int16_t*)p_dat)[1] = i;
	}

	// WRITE: Get pointers to TX buf and write IQ to TX buf port 0
	p_inc = iio_buffer_step(txbuf);
	p_end = iio_buffer_end(txbuf);
	for (p_dat = (char *)iio_buffer_first(txbuf, tx0_i); p_dat < p_end; p_dat += p_inc) {
		// Example: fill with zeros
		// 12-bit sample needs to be MSB aligned so shift by 4
		// https://wiki.analog.com/resources/eval/user-guides/ad-fmcomms2-ebz/software/basic_iq_datafiles#binary_format
		((int16_t*)p_dat)[0] = 0 << 4; // Real (I)
		((int16_t*)p_dat)[1] = 0 << 4; // Imag (Q)
	}

	// Sample counter increment and status output
	nrx += nbytes_rx / iio_device_get_sample_size(rx);
	ntx += nbytes_tx / iio_device_get_sample_size(tx);
	printf("\tRX %8.2f MSmp, TX %8.2f MSmp\n", nrx/1e6, ntx/1e6);
}
```

</br>

#### 4）资源释放 (Cleanup)

当按下 `Ctrl+C` 时，程序会安全地关闭流通道、销毁缓冲区并断开与设备的连接，防止硬件进入挂起状态。

```
shutdown();
```

</br>

#### 总结

**a. 代码中的关键技术点**

* **IQ 数据格式：** AD9361 处理的是复数信号。代码中使用了 `int16_t` 来处理 12 位精度的采样数据。
* **位对齐 (Bit Alignment)：** 在发送数据时，代码提到了 `<< 4`（左移 4 位）。这是因为 AD9361 的 12 位数据在 16 位存储空间中通常是高位对齐的。
* **libiio 抽象：** 代码展示了 libiio 的典型工作流：`Context (上下文)` -> `Device (设备)` -> `Channel (通道)` -> `Buffer (缓冲区)`。

</br>

**b. 它有什么用？**

如果你有一个支持 libiio 的 SDR 设备（比如 PlutoSDR），编译并运行这个程序，你就能看到你的电脑正在以 2.5 GHz 的频率实时“监听”并“尝试发送”信号。**它是开发更复杂的 SDR 应用（如 FM 收音机、WiFi 扫描器、基站模拟器等）的基础骨架代码。**


</br>

### 6. 结语

至此，我们不仅完成了从 PC 开发者到嵌入式开发者的身份转变，更亲手打通了 PlutoSDR 脱机运行的全链路：从环境搭建、工具链选择，到 `libiio` 核心框架的深度解析。

**“断开脐带”并不意味着失去支持，而是意味着更广阔的自由。**

当我们能够直接通过 C 语言操控 AD9361 的 DMA 缓冲区时，PlutoSDR 就不再仅仅是一个外设，而是一个具备实时信号处理能力的自持系统。你可以将其嵌入到无人机、远程监测站或是微型基站中，在没有 PC 的地方，依然能够精准地捕获和发射电磁波。

在接下来的课程中，我们将更进一步，探索如何将这些基础框架转化为具体的应用逻辑，比如实现一个真正的窄带通信协议或自动化频谱监测。无线电的浪漫，正是在于这种从底层代码到高空波段的极致掌控。

</br>

### 参考链接

[[1]. Github —— libiio][#2]      
[[2]. ADI Wiki —— Analog Devices Linux][#4]     
[[3]. Github —— Analog Devices, Inc.][#5]    



[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/107    
[#2]:https://github.com/analogdevicesinc/libiio/tree/v0.25     
[#3]:https://github.com/analogdevicesinc/libiio/blob/v0.25/examples/ad9361-iiostream.c      
[#4]:https://analogdevicesinc.github.io/linux/    
[#5]:https://github.com/analogdevicesinc          
 

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202605/plutosdr_ssh_linux_connect.png      


