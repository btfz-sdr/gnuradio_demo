### 1、前言

在 GNU Radio 中，数据传输主要分为 **流（Stream）** 和 **向量（Vector）** 两种形式。

* **流（Stream）**：数据按单个采样点依次传输（比如单个 Float、Complex 等）。
* **向量（Vector）**：把多个采样点打包成一个固定长度的数组打包传输（常用于 FFT、OFDM 等批处理场景）。

今天通过三个流程图（`Fundamentals2.grc` ~ `4.grc`），一次性搞懂 Stream 与 Vector 之间的转换与拆解逻辑。

</br>

### 2、Stream 转 Vector（Stream to Vector）

`Fundamentals2.grc` 演示了如何将普通的采样点流转换为指定长度的向量：

1. **单点流转向量**：通过 **Stream to Vector** 模块，可以将单采样点流打包成长度为 128（`Num Items = 128`）的向量流。
2. **数据类型对应**：不管是 Complex（蓝色端口）、Float（橙色端口）还是 Byte（黄绿色端口），都可以通过设置对应的 Type 进行向量化打包。

![][p1]

</br>

### 3、多路流合成向量与解压（Streams to Vector & Vector to Stream）

`Fundamentals3.grc` 演示了如何把多路独立的采样流合成一个向量，以及如何重新解开：

1. **多路流合成（Streams to Vector）**：
* 两个 **Signal Source** 分别产生 $1\text{kHz}$（幅度 1）和 $100\text{Hz}$（幅度 0.1）的信号。
* 输入到 **Streams to Vector**（`Num Inputs = 2`），交替/交织打包为一个长度为 2 的向量（`[A, B]`）。


2. **向量展开为单流（Vector to Stream）**：
* 通过 **Vector to Stream**（`Num Items = 2`）将 `[A, B]` 向量重新展平为单流。
* 展平后的数据在 **QT GUI Time Sink (A+B)** 中表现为 A 和 B 采样点交替出现的波形。

![][p2]

</br>

### 4、向量拆解为多路流（Vector to Streams）

`Fundamentals4.grc` 在图 2 的基础上，增加了 **Vector to Streams** 模块，演示如何将打包好的向量精准拆回原始的多路独立流：

1. **向量分流拆解**：
* **Streams to Vector** 打包出的 `[A, B]` 向量直接送入 **Vector to Streams**（`Num Outputs = 2`）。


2. **通道还原**：
* `out0` 重新输出 A 信号（$1\text{kHz}$），接入 **QT GUI Time Sink (A)**。
* `out1` 重新输出 B 信号（$100\text{Hz}$），接入 **QT GUI Time Sink (B)**。
* 波形与源头的两个 Signal Source 完全一致，实现了多路信号的向量复用与完美还原。

![][p3]

</br>

### 参考链接

* [GNU Radio Wiki - Streams and Vectors](https://wiki.gnuradio.org/index.php/Streams_and_Vectors)
* [GNU Radio Wiki - Stream to Vector](https://wiki.gnuradio.org/index.php/Stream_to_Vector)


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_stream_vector_grc2.png    
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_stream_vector_grc3.png        
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_stream_vector_grc431105033.png         

