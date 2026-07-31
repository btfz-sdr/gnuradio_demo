### 1、前言

在前文了解了简单的单流 Python Block 后，本篇进一步探讨如何在 **Embedded Python Block** 中处理 **向量数据（Vector）**。

在实际无线电信号处理中（如 FFT 变换、OFDM 帧处理等），数据往往是以固定长度的向量打包传输的。通过自定义 Python 块，我们可以对每个向量内部的数据进行灵活的批处理操作（例如寻找向量内的最大值、平均值或滤波处理）。

今天通过编写一个 **最大值保持模块（Max Hold Block）**，演示如何处理多路向量流。

</br>

### 2、编写支持向量的 Python Block 代码

在 GRC 中拖入 **Embedded Python Block**，双击打开编辑器并修改代码如下：

```python
import numpy as np
from gnuradio import gr

class blk(gr.sync_block):
    """Embedded Python Block example - Max Hold for Vectors"""

    def __init__(self, vectorSize=16):  # 参数 vectorSize 会暴露在 GRC 配置界面
        gr.sync_block.__init__(
            self,
            name='Max Hold Block',
            # 端口类型使用元组 (dtype, vector_len) 指定输入/输出为向量形式
            in_sig=[(np.float32, vectorSize), (np.float32, vectorSize)],
            out_sig=[(np.float32, vectorSize), (np.float32, vectorSize)]
        )
        self.vectorSize = vectorSize

    def work(self, input_items, output_items):
        """对每个输入的向量求最大值，并将该向量内的所有采样点赋值为该最大值"""
        for portIndex in range(len(input_items)):
            for vectorIndex in range(len(input_items[portIndex])):
                # 寻找当前向量中的最大值
                maxValue = np.max(input_items[portIndex][vectorIndex])
                # 将该最大值填充到输出向量的所有采样点位置
                for sampleIndex in range(len(input_items[portIndex][vectorIndex])):
                    output_items[portIndex][vectorIndex][sampleIndex] = maxValue

        return len(output_items[0])

```

**关键点解析**：

1. **向量端口声明**：`in_sig` 和 `out_sig` 中使用元组 `(np.float32, vectorSize)`，显式告知 GNU Radio 端口数据类型为长度为 `vectorSize` 的 Float 向量。
2. **多维数组索引**：在 `work` 函数中，数据结构为三维数组：`input_items[端口索引][向量索引][采样点索引]`。
3. **算法逻辑**：针对输入流中的每个向量（长度为 16），找出最大值 `maxValue`，并将输出向量全部赋值为此最大值，形成阶梯状的“最大值包络”效果。

</br>

### 3、在流程图中使用向量 Python 块

搭建流程图验证多通道向量的处理：

- 1）**信号源与向量转换**：
    * **通道 0**：`Signal Source`（$100\text{Hz}$ 余弦波，Float 类型） $\rightarrow$ `Throttle` $\rightarrow$ `Stream to Vector`（Num Items = 16）。
    * **通道 1**：`Noise Source`（高斯白噪声，Float 类型） $\rightarrow$ `Stream to Vector`（Num Items = 16）。
- 2）**连接至自定义模块**：
    * 两路打包好的向量流接入自定义的 **`Max Hold Block`**（设置 `Vectorsize = 16`）。
- 3）**向量还原与对比显示**：
    * 输出的两路向量分别通过 **Vector to Stream** 展平为普通单点流。
    * 分别接入两个 **QT GUI Time Sink**，并通过 **Virtual Source** 引入未处理的原始信号（`cos` 和 `noise`）进行叠加对比（Signal 1 为最大值保持后的阶梯波形，Signal 2 为原始波形）。

![][p1]

</br>

### 4、运行与波形分析

运行流程图后可以看到：

![][p2]

* **下半图（余弦波）**：蓝线（Signal 1）在每 16 个采样点（即一个 Vector 长度）内均保持该段内的最大值，形成紧贴余弦波顶部的阶梯状包络。
* **上半图（高斯噪声）**：蓝线（Signal 1）同样对高斯白噪声进行了分段阶梯状的最大值提取，准确反映了每一个向量块中的峰值变化。

</br>

### 参考链接

* [GNU Radio Wiki - Embedded Python Block](https://wiki.gnuradio.org/index.php/Embedded_Python_Block)
* [GNU Radio Wiki - Vector Operations Guide](https://wiki.gnuradio.org/index.php/Streams_and_Vectors)


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_second_python_block_grc.png    
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_second_python_block_grc_show.png    

