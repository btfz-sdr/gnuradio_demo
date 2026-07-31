### 1、前言

在 GNU Radio 中，除了按采样率传输的连续数据流（Stream）和异步控制消息（Message Passing），还有一种非常关键的技术——**标签（Stream Tags）**。

标签可以**精准绑定到某个具体的采样点**上（随着数据流同步流动），常用于标注信号突发（Burst）起始、通道估计参数、帧头同步位置等信息。

今天通过编写两个自定义 Python 模块——一个负责检测阈值并打上 Tag（`Threshold Detector`），另一个负责读取 Tag 并重置计数器（`Detector Counter`），演示如何在 Embedded Python Block 中实现 Stream Tags 的写入与读取。

</br>

### 2、编写 Tag 写入与读取模块

#### 模块 A：Tag 写入端（`Threshold Detector`）

双击新建第一个 Embedded Python Block，编写阈值检测与打 Tag 代码：

```python
import numpy as np
from gnuradio import gr
import pmt

class blk(gr.sync_block):
    """Threshold Detector - 当信号幅值超过设定阈值时，在对应采样点打上 'detect' 标签"""

    def __init__(self, threshold=1.0, report_period=128):
        gr.sync_block.__init__(
            self,
            name='Threshold Detector',
            in_sig=[np.float32],
            out_sig=[np.float32]
        )
        self.threshold = threshold
        self.report_period = report_period
        self.timer = 0
        self.readyForTag = True

    def work(self, input_items, output_items):
        for index in range(len(input_items[0])):
            # 1. 满足阈值条件且处于可打 Tag 状态时写入 Tag
            if input_items[0][index] >= self.threshold and self.readyForTag:
                key = pmt.intern('detect')
                value = pmt.from_float(np.round(float(input_items[0][index]), 2))
                # 计算当前采样点在全局数据流中的绝对索引 (nitems_written + 当前块偏移)
                writeIndex = self.nitems_written(0) + index
                
                # 给指定绝对索引位置的采样点添加 Tag
                self.add_item_tag(0, writeIndex, key, value)
                self.readyForTag = False

            # 2. 定时防重打机制（防挂连打 Tag）
            if not self.readyForTag:
                self.timer += 1

            if self.timer >= self.report_period:
                self.timer = 0
                self.readyForTag = True

        output_items[0][:] = input_items[0]
        return len(output_items[0])

```

</br>

#### 模块 B：Tag 读取端（`Detector Counter`）

双击新建第二个 Embedded Python Block，编写读取 Tag 并重置输出计数器的代码：

```python
import numpy as np
from gnuradio import gr
import pmt

class blk(gr.sync_block):
    """Detector Counter - 读取输入流中的 'detect' 标签，计算自上次检测点以来的采样数"""

    def __init__(self):
        gr.sync_block.__init__(
            self,
            name='Detector Counter',
            in_sig=[np.float32],
            out_sig=[np.float32]
        )
        self.samplesSinceDetection = 0

    def work(self, input_items, output_items):
        # 1. 获取当前 work 窗口范围内的所有 Tags
        tagTuple = self.get_tags_in_window(0, 0, len(input_items[0]))
        
        # 2. 解析 Key 为 'detect' 的 Tag，并将全局绝对 offset 转换为当前 work 内的相对 index
        relativeOffsetList = []
        for tag in tagTuple:
            if pmt.to_python(tag.key) == 'detect':
                relativeOffsetList.append(tag.offset - self.nitems_read(0))
        
        relativeOffsetList.sort()

        # 3. 逐采样点输出计数，遇到 Tag 位置则清零重置
        for index in range(len(output_items[0])):
            output_items[0][index] = self.samplesSinceDetection

            if len(relativeOffsetList) > 0 and index >= relativeOffsetList[0]:
                relativeOffsetList.pop(0)
                self.samplesSinceDetection = 0  # 遇到检测 Tag，计数器清零
            else:
                self.samplesSinceDetection += 1  # 正常累加采样点数

        return len(output_items[0])

```

</br>

**核心 API 解析**：

1. **绝对索引计算**：
    * 全局已写入数：`self.nitems_written(0)`
    * 全局已读取数：`self.nitems_read(0)`
    * 全局绝对位置 = `self.nitems_written(port) + index`
2. **添加 Tag**：`self.add_item_tag(port, writeIndex, key_pmt, value_pmt)`
3. **获取 Tag**：`self.get_tags_in_window(port, start_offset, end_offset)`，返回当前 buffer 范围内的所有 Tag 元组。
4. **相对位置转换**：`tag.offset - self.nitems_read(port)` 将全局逻辑位置转为当前数组索引。

</br>

### 3、在流程图中使用 Tag 模块

搭建流程图验证基于 Tag 的检测与响应逻辑：

1. **测试信号生成**：
    * `GLFSR Source` 生成伪随机序列，依次经过 `Repeat`（插值扩大）、`Multiply Const`、`Add Const` 以及 `Single Pole IIR Filter`（平滑边缘），构造出带有上升沿的方波脉冲信号。
2. **Tag 写入与处理**：
    * 信号送入 **`Threshold Detector`**（设置 `Threshold = 750m`），在上升沿越过阈值瞬间生成 `detect: 0.76` 等 Tag。
    * 数据流继续送入 **`Detector Counter`**，提取该 Tag 并将输出转为锯齿状的采样点计数波形。
    * 经过 **Tag Gate**（设置 `Propagate Tags: No`）断开 Tag 传播，防止后级 Sink 重复绘制。
3. **波形可视化**：
    * **图 1 (QT GUI Time Sink 1)**：展示滤波前后的原始方波与 Tag 在信号上升沿处的精确锚定位置。
    * **图 3 (QT GUI Time Sink 3)**：展示计数器输出波形，每次触发 Tag 时计数器即归零并重新开始线性递增。

![][p1]  

</br>

### 4、运行效果

运行流程图后：

![][p2]   

* **上半图（信号波形）**：当平滑后的信号（黑色波形 Signal 4）超过 $0.75$ 阈值时，GRC 界面会自动在对应位置标注出三角形下尖头 `detect: 0.76`。
* **下半图（计数器）**：原本平滑递增的斜坡波形在每个 `detect` 标签出现的精确时间点瞬间归零，随后重新向上爬升，直观验证了基于 Stream Tag 的样本级精准控制。

</br>

### 参考链接

* [GNU Radio Wiki - Stream Tags](https://wiki.gnuradio.org/index.php/Stream_Tags)
* [GNU Radio Wiki - Python Block Tags Tutorial](https://www.google.com/search?q=https://wiki.gnuradio.org/index.php/Python_Block_Tags)    

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_python_block_tag_grc.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_python_block_tag_grc_show.gif

