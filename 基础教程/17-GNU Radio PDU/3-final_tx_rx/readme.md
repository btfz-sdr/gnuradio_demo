
本教程详细介绍如何在 GNU Radio Companion (GRC) 中利用分层块（Hier Block）机制，将复杂的 PDU 发送与接收子图封装为自定义模块，并在主流程图中完成文件的无线/有线模拟信道传输与环回测试（Loopback Test）。

</br>

## 1. 架构设计与模块简介

在基于数据包（Packet-based / PDU）的数字通信系统设计中，为了保持工程结构的清晰性与模块复用度，通常会把**物理层与数据链路层的组帧/解帧流程**单独抽离出来，封装为 Hier Block（分层块）。

本次环回测试系统由以下三个核心 GRC 流程图文件组成：

### 1.1 发送子图 (`packet_tx.grc`)

负责将上层输入的 PDU 或数据流加工成基带调制信号。主要处理环节包含：

* **CRC32 校验码生成**：追加数据包末尾校验。
* **Protocol Formatter**：添加包头（Header），标记数据包长度与帧类型。
* **FEC 异步编码**：进行信道编码以提升抗干扰能力。
* **Bits to Symbols & 脉冲成形**：将比特映射为星座图符号（如 BPSK/QPSK），并通过多相重采样器与脉冲成形滤波器生成基带 IQ 信号。

![][p2]

</br>

### 1.2 接收子图 (`packet_rx.grc`)

负责从连续或突发的基带 IQ 信号中恢复出原始数据包。主要处理环节包含：

* **能量/相关性检测与时钟同步**：利用前导码（Preamble）进行帧同步与 Timing Offset 估计。
* **Costas 锁相环（Costas Loop）**：纠正载波频率与相位偏移。
* **Header/Payload Demux**：解析 Header 后动态提取 Payload 内容。
* **软判决解调与 FEC 解码**：提高微弱信号下的译码成功率。
* **CRC Check & PDU 还原**：校验无误后剥离包头，输出解包后的数据。

![][p1]

</br>

### 1.3 主测试流程图 (`packet_loopback_hier.grc`)

作为顶层系统（Top Block），主要负责：

* **数据源管理**：从本地文件读取字节流并转换为 PDU 突发。
* **子图模块调用**：例化并连接 `PacketTx` 与 `PacketRx` 模块。
* **信道模拟（Channel Model）**：引入加性高斯白噪声（AWGN）、频率偏移（Frequency Offset）与多径衰落，模拟真实信道环境。
* **实时数据观察与落盘**：通过星座图、时域图与 Message Debug 观察传输质量，并将接收到的数据流写入本地磁盘文件。

![][p3]

> **说明**：关于收发子图内部的数字信号处理原理与具体参数配置，已在前期课程中详细剖析，本篇重点讲解**模块编译生成、环境安装与联合调试**流程。

</br>

## 2. 详细操作步骤

### 步骤 1：编译并生成子图模块 (Generate Hier Blocks)

在 GNU Radio 中，Hier Block 必须先生成对应的 Python 代码与 YAML 配置描述文件，系统才能将其识别为新的 Block 节点。

1. **生成发送模块 (`PacketTx`)**：
* 在 GRC 中打开发送子图文件 `packet_tx.grc`。
* 点击菜单栏的 **Generate the flow graph** 按钮。
* 观察控制台输出，确认没有语法与变量缺失错误。


2. **生成接收模块 (`PacketRx`)**：
* 在 GRC 中打开接收子图文件 `packet_rx.grc`。
* 同样点击 **Generate the flow graph** 按钮。

#### 路径与文件生成说明

编译成功后，GNU Radio 会自动在用户本地配置目录 `~/.local/state/gnuradio` 中生成以下四份核心文件：

* `packet_tx.py`（发送端 Python 逻辑实现）
* `packet_tx.block.yml`（发送端 GRC 模块界面描述文件）
* `packet_rx.py`（接收端 Python 逻辑实现）
* `packet_rx.block.yml`（接收端 GRC 模块界面描述文件）

</br>

### 步骤 2：刷新模块库与解决报红错误 (Reload Block Library)

1. 打开发送与接收的测试主流程图文件 `packet_loopback_hier.grc`。
2. **现象说明**：首次打开该流程图时，由于 GRC 尚未加载刚刚编译生成的自定义 YML 文件，图中的 `PacketTx` 和 `PacketRx` 模块可能呈**灰色虚线框或红色报错状态**。
3. **刷新加载**：
* 点击 GRC 工具栏中的 **Reload Blocks** 按钮。
* 刷新后，右侧模块列表中会出现 `(no modules specified)/Packet Operators`，主流程图中的 `PacketTx` 与 `PacketRx` 模块变亮并恢复正常的连接端口。

</br>

### 步骤 3：准备测试文件与环境配置

在运行测试前，需确保文件路径与数据源匹配：

1. 将测试用的视频文件 `a.mkv` 放置在当前 GRC 流程图运行的同级目录下（已经默认放好）。
2. 确认主流程图中的 **File Source** 节点路径正确指向了 `./a.mkv`（或绝对路径）。
3. 确认 **File Sink** 节点输出文件名设置为 `output_a.mkv`。

</br>

### 步骤 4：运行环回测试与效果验证

1. **启动测试**：点击 GRC 顶部工具栏的 **Execute the flow graph** 运行按钮。
2. **过程监控**：
* 运行窗口将弹出一个或多个 QT GUI 界面，展示**解调前的星座图**、**信道时域波形**以及**解调后的眼图/相位误差曲线**。
* 调整信道模型（Channel Model）中的 Noise Voltage（噪声电压）与 Frequency Offset（频偏），观察接收端锁相环与译码器的鲁棒性。

    ![][p4]

3. **实时播放与数据比对**：
* 随着数据的传输，系统会在本地生成 `output_a.mkv` 文件。
* 打开本地播放器（如 VLC、MPV 或 PotPlayer），直接读取并**实时播放 `output_a.mkv`**。
* 如果音视频画面流畅且无花屏/卡顿，说明 PDU 分包、校验、信道传输及重组恢复全流程运行正常！


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202609/packet_rx_hier_grc.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202609/packet_tx_hier_grc.png
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202609/packet_loopback_grc.png
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202609/packet_loopback_grc_show.png     


