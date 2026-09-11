
本教程介绍如何在 GNU Radio Companion (GRC) 中利用 **ZeroMQ (ZMQ)** 进程间通信机制，将发送端、模拟信道与接收端拆分为独立的流程图，完成图片文件（`test1.png` $\rightarrow$ `output1.png`）的无线传输模拟。

</br>

## 1. 系统整体架构

不同于单一流程图（Top Block）的直连设计，本次实验采用 **解耦的三进程架构**。各模块间通过 ZMQ 套接字进行 IQ 数据流传输：

```
[ tx_ofdm.grc ]  -- (ZMQ PUB :49203) -->  [ chan_loopback.grc ]  -- (ZMQ PUB :49201) -->  [ rx_ofdm.grc ]
  (发送端)                                     (信道模拟器)                                 (接收端)

```

### 1.1 发送端流程图 (`tx_ofdm.grc`)

* **数据源**：`File Source` 读取本地 `test1.png` 图像文件（解构为字节流）。
* **帧结构与编码**：通过 `Stream CRC32` 增加校验，`Packet Header Generator` 构建包头，并将 Header（BPSK 调制）与 Payload（QPSK 调制）进行符号映射。
* **OFDM 调制**：通过 `OFDM Carrier Allocator` 插入子载波与导频（Pilot），经 `FFT` 变换及 `OFDM Cyclic Prefixer` 添循环前缀生成时域 IQ 信号。
* **数据出口**：末端连接 **ZMQ PUB Sink**，绑定地址为 `tcp://127.0.0.1:49203`。

![][p2]

</br>

### 1.2 信道模拟器 (`chan_loopback.grc`)

* **数据入口**：**ZMQ SUB Source** 监听并连接 `tcp://127.0.0.1:49203`，接收发送端发出的基带信号。
* **信道损伤模拟**：内置 `Channel Model`，支持动态调节高斯白噪声（Noise Voltage）、频偏（Frequency Offset）及 Timing Offset（时钟偏移）。
* **数据出口**：末端连接 **ZMQ PUB Sink**，绑定地址为 `tcp://127.0.0.1:49201`。

![][p3]

</br>

### 1.3 接收端流程图 (`rx_ofdm.grc`)

* **数据入口**：**ZMQ SUB Source** 连接 `tcp://127.0.0.1:49201` 获取信道传输后的信号。
* **OFDM 接收机**：采用 `OFDM Receiver` 封装块，自动完成同步、信道估计、相位纠正、解调及 CRC 校验。
* **数据落盘**：通过 `Tag Gate` 屏蔽标签传递后，经由 `File Sink` 将解压恢复出的字节流写入 `output1.png`。

![][p1]

</br>

## 2. 实验操作与启动顺序

由于 ZMQ 采用了 **PUB/SUB（发布/订阅）** 模式，为了确保数据流建立时订阅端已就位、防止首包数据丢失，**必须严格按照以下顺序启动三个流程图**：

### 步骤 1：启动接收端 (`rx_ofdm.grc`)

1. 在 GRC 中打开 `rx_ofdm.grc`。
2. 点击运行按钮。
3. 此时接收端的 ZMQ SUB Source 会开始监听 `49201` 端口，等待信道数据的到达。

### 步骤 2：启动信道模拟器 (`chan_loopback.grc`)

1. 在 GRC 中打开 `chan_loopback.grc`。
2. 点击运行按钮。
3. 信道端连接至发送端端口 `49203`，同时开启 `49201` 发布端口，与步骤 1 的接收端建立数据通道。

### 步骤 3：启动发送端 (`tx_ofdm.grc`)

1. 确认当前运行目录下存在待传输的图像文件 `test1.png`。
2. 在 GRC 中打开 `tx_ofdm.grc` 并点击运行。
3. 发送端会瞬间将 `test1.png` 的数据读取、打包、OFDM 调制并推送至 ZMQ 网络。

</br>

## 3. 结果验证与预览

1. **传输完成**：由于文件尺寸较小（如百 KB 级别），在启动 `tx_ofdm.grc` 的一瞬间，数据便已通过网络套接字完成闭环传输。
2. **文件检查**：查看当前运行目录下生成的 `output1.png` 文件。
3. **图像预览**：在 Linux 终端中可使用轻量级图片查看工具（如 `feh`）查看接收到的图片：
```bash
feh output1.png

```


若图片正常显示且无花屏/损坏，即代表整个跨进程 OFDM 传输系统部署成功。

</br>

## 4. 常见问题与提示

* **图像文件损坏/打不开**：
* 检查 `chan_loopback.grc` GUI 中的 `Noise Voltage` 是否设得过高。如果信道干扰太大导致数据包 CRC 校验失败，`OFDM Receiver` 会直接丢弃损伤的 Payload，导致文件写入不完整。


* **端口冲突报错 (`Address already in use`)**：
* 若重复运行流程图，请确保上一次运行的 Python 进程已彻底关闭，防止 ZMQ 端口（`49201` / `49203`）被占用。


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202609/rx_ofdm_grc.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202609/tx_ofdm_grc.png
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202609/chan_loopback_grc.png

