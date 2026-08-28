### 1、前言

在 GNU Radio 中，除了展示基础的波形与频谱图外，我们还可以通过组合各种 **QT GUI 控件（控件交互、矢量绘图、指示灯警报、提示图片）** 以及 **自定义 Python 逻辑模块（如阈值判定与消息传递）**，构建功能完善、交互丰富的高级仪表盘系统。

今天通过一个功率阈值侦测（Power Threshold Detection）的实验流程图，演示如何将信号进行 FFT 矢量化频域转换、计算对数功率谱、动态绘制门限红线，并通过自定义 Python 块触发 LED 指示灯与消息调试输出。

</br>

### 2、系统架构与数据流设计

整个系统包含 **信号流处理（Stream/Vector Processing）**、**阈值比较与状态触发（Python Block / Message Passing）** 以及 **QT GUI 多维度交互呈现** 三大部分。

![][p1]

</br>

### 3、核心处理链路详解

#### 1. 信号源与频谱功率计算 (DSP 链路)

* **信号源**：`Noise Source`（高斯白噪声）或硬件射频源（`osmocom Source` / `Soapy HackRF Source`），采样率设为 $10\text{MSps}$。
* **矢量化分帧**：**`Stream to Vector`** 将连续采样流按照向量长度（`vec_len = 1024`）打包为点阵向量。
* **频域变换**：**`FFT`**（黑曼窗 Blackman-Harris，向前变换），将时域信号转换为 1024 点频域向量。
* **功率谱能量计算**：
* **`Fast Multiply Const`**：进行矢量标量乘法，提升内核计算效率。
* **`Complex to Mag^2`**：计算模的平方（即各个频点的功率强度）。
* **`Log10`**：乘以 $10 \log_{10}(\cdot)$ 换算为对数功率谱（$\text{dB}$ 刻度）。



#### 2. 交互式门限绘制与最大值提取

* **动态门限绘制**：**`Constant Source`**（结合 `QT GUI Range` 动态调节阈值如 $-30\text{dB}$）生成标量，转换为 1024 维向量后送入 **`QT GUI Vector Sink`** 的端口 1，与端口 0 的实际信号功率谱重叠画在同一坐标系中，形成直观的“门限基准红线”。
* **峰值提取**：**`Max`** 模块把 1024 维频域向量规约为单个最大功率点（寻找频带内最高峰值）。

#### 3. Python 逻辑判断与消息通知 (Interactive & Message)

* **绝对值转换**：**`Abs`** 模块将小于 0 的负分贝数值转换为正绝对值，方便阈值判定逻辑。
* **自定义 Python 模块（`Judge Threshold`）**：
* 读取当前频带最大值与用户设定阈值。
* 当信号强度超越设定门限时，通过 **Message Passing（PMT 消息）** 触发异步事件：
* 向 **`QT GUI LED Indicator`** 端口投递布尔状态，实现“超过阈值时 LED 灯亮起警报”。
* 向 **`Message Debug`** 端口投递日志，在控制台打印告警 PDU 信息。
* 将数值输出至 **`QT GUI Number Sink`** 进行实时数字指示。


</br>

### 4、QT GUI 丰富交互页面配置技巧

为了打造专业的 SDR 控制仪表盘，本例综合运用了多种 QT 交互组件：

| 控件类别 | 控件名称 | 功能描述 |
| --- | --- | --- |
| **参数调节** | `QT GUI Range` | 滑块控制接收增益（`if_gain`/`vga_gain`）、中心频率与触发门限（`threshold`）。 |
| **状态开关** | `QT GUI Check Box` | 复选框控制硬件 LNA 放大器开/关（`amp_on_+14dB`）。 |
| **下拉选择** | `QT GUI Chooser` | 下拉菜单切换射频带宽（`10M` / `20M` 等）。 |
| **矢量图像显示** | `QT GUI Vector Sink` | 同时输入功率谱向量与门限平线向量，实时比对频谱超标情况。 |
| **警报与指示** | `QT GUI LED Indicator` / `Number Sink` | 接收 PMT 控制消息，呈现灯光亮灭与实时数字。 |
| **品牌与说明** | `QT GUI Graphic Item` | 在界面上载入外部图片（如硬件示意图 `hackrfusage.jpeg`），丰富界面视觉。 |

</br>

### 5、最终效果

![][p2]

</br>

### 参考链接

* [GNU Radio Wiki - Vector Blocks](https://www.google.com/search?q=https://wiki.gnuradio.org/index.php/Vector_Blox)
* [GNU Radio Wiki - Message Passing](https://wiki.gnuradio.org/index.php/Message_Passing)
* [GNU Radio Wiki - QT GUI Blocks](https://www.google.com/search?q=https://wiki.gnuradio.org/index.php/QT_GUI_Category)

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202608/grc_tui_qtgui_grc.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202608/grc_tui_qtgui_grc_show.gif

