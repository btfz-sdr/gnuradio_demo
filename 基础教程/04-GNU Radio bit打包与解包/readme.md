### 1、前言

在数字通信中，数据打包（Pack）与解包（Unpack）是极其常见的操作。今天用 `Fundamentals.grc` 来看下如何在 GRC 里实现比特流的组包与拆包，并通过直方图和时域图验证数据的还原过程。

</br>

### 2、流程图构建与关键模块说明

流程图核心由随机比特生成、打包/解包处理以及可视化统计三部分组成：

1. **随机比特源（Random Source）**：生成取值在 $[0, 2)$ 范围内的随机数据（即 `0` 和 `1` 的单比特数据）。
2. **比特打包（Pack K Bits）**：设置 $K=4$，将输入的 4 个 1-bit 数据拼成 1 个范围在 $[0, 15]$ 的 4-bit 字节数据。
3. **比特解包（Unpack K Bits）**：同样设置 $K=4$，将打包好的字节重新还原为独立的 1-bit 数据。
4. **数据转换与显示（Char To Float & Sinks）**：
    * 使用 **Char To Float** 将 byte 类型转换为 float 类型供 GUI 模块接收。
    * **QT GUI Histogram Sink**：分别观察打包后（4bits）和解包还原后（1bit）的数据分布。
    * **QT GUI Time Sink**：将原始比特与解包后的比特进行时域对齐波形对比。

![][p1]

</br>

### 3、跑起来看效果

运行流程图后，观察弹出的可视化窗口：

1. **直方图（Histogram）**：
* 4bits 直方图分布在 $0\sim15$ 之间，证明 4 个 bit 已成功打包为新的字节。
* 1bit 直方图重新集中在 $0$ 和 $1$，说明解包后恢复为了单比特分布。


2. **时域波形（Time Sink）**：对比原始数据与解包后的信号波形，两条曲线完全重合，验证了比特打包与解包过程的无损还原。

![][p2]

</br>

### 参考链接

* [GNU Radio Wiki - Pack K Bits](https://wiki.gnuradio.org/index.php/Pack_K_Bits)
* [GNU Radio Wiki - Unpack K Bits](https://wiki.gnuradio.org/index.php/Unpack_K_Bits)

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_pack.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_pack_show.png

