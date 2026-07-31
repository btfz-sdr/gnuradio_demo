
### 1、前言

在 GNU Radio 中，除了按采样率连续传输的同步数据流（Stream），模块之间还可以通过 **消息传递（Message Passing）** 进行异步的事件控制与参数传递。GNU Radio 内部采用 **PMT（Polymorphic Types，多态类型）** 来打包和解析这些控制消息。

今天通过编写两个自定义 Python 模块——一个负责发控制消息（`Selector Control`），一个负责收消息并切换数据通道（`Multiplexer`），演示在 Embedded Python Block 中如何实现消息的订阅与发布。

</br>

### 2、编写消息发送与接收模块

#### 模块 A：消息接收端（`Multiplexer`）

双击新建第一个 Embedded Python Block，编写多路选择器代码：

```python
import numpy as np
from gnuradio import gr
import pmt

class blk(gr.sync_block):
    """Multiplexer - 根据接收到的控制消息在两路输入数据流之间进行切换"""

    def __init__(self):
        gr.sync_block.__init__(
            self,
            name='Multiplexer',
            in_sig=[np.complex64, np.complex64], # 两路复数输入
            out_sig=[np.complex64]               # 单路复数输出
        )
        self.selectPortName = 'selectPort'
        # 1. 注册消息输入端口
        self.message_port_register_in(pmt.intern(self.selectPortName))
        # 2. 绑定消息处理回调函数 handle_msg
        self.set_msg_handler(pmt.intern(self.selectPortName), self.handle_msg)
        self.selector = True  # 切换标志位

    def handle_msg(self, msg):
        """消息回调函数：将 PMT 消息解析为 Python bool 值"""
        self.selector = pmt.to_bool(msg)

    def work(self, input_items, output_items):
        # 根据消息解析出来的 selector 标志，选择转发端口 0 或端口 1 的数据
        if self.selector:
            output_items[0][:] = input_items[0]
        else:
            output_items[0][:] = input_items[1]
        return len(output_items[0])

```

</br>

#### 模块 B：消息发送端（`Selector Control`）

双击新建第二个 Embedded Python Block，编写计数发布控制消息的代码：

```python
import numpy as np
from gnuradio import gr
import pmt

class blk(gr.sync_block):
    """Selector Control - 统计处理的采样点数，定时向外发布切换消息"""

    def __init__(self, Num_Samples_To_Count=128):
        gr.sync_block.__init__(
            self,
            name='Selector Control',
            in_sig=[np.complex64],
            out_sig=[np.complex64]
        )
        self.Num_Samples_To_Count = Num_Samples_To_Count
        self.portName = 'messageOutput'
        # 注册消息输出端口
        self.message_port_register_out(pmt.intern(self.portName))
        self.state = True
        self.counter = 0

    def work(self, input_items, output_items):
        # 累加处理过的采样点数量
        self.counter += len(output_items[0])

        # 当采样点数达到设定的阈值时，翻转状态并发布 PMT 消息
        if self.counter > self.Num_Samples_To_Count:
            PMT_msg = pmt.from_bool(self.state)
            self.message_port_pub(pmt.intern(self.portName), PMT_msg)
            self.state = not self.state
            self.counter = 0  # 重置计数器

        output_items[0][:] = input_items[0]
        return len(output_items[0])

```

</br>

**核心 API 解析**：

1. **端口注册**：
    * 接收端：`self.message_port_register_in(pmt.intern("port_name"))`
    * 发送端：`self.message_port_register_out(pmt.intern("port_name"))`
2. **处理回调**：`self.set_msg_handler(pmt_port, callback_func)` 绑定异步接收逻辑，无需在 `work()` 里阻塞等待。
3. **消息类型转换（PMT）**：
    * Python 转 PMT：`pmt.from_bool(val)`
    * PMT 转 Python：`pmt.to_bool(msg)`
4. **消息发布**：`self.message_port_pub(pmt_port, pmt_msg)`

</br>

### 3、在流程图中使用消息端口

搭建流程图验证消息驱动的信号切换：

1. **输入源**：
    * 输入通道 0：`Noise Source`（高斯白噪声）。
    * 输入通道 1：`Signal Source`（$1\text{kHz}$ 余弦波）。
2. **数据与消息流向**：
    * **`Multiplexer`** 接收两路源信号，输出经由 **`Selector Control`** 进行采样点计数。
    * **`Selector Control`** 的 `messageOutput` 灰色消息端口，一方面通过 **Virtual Sink/Source** 虚线异步连接回 `Multiplexer` 的 `selectPort` 控制端口，另一方面接入 **Message Debug** 进行终端打印日志分析。
3. **图形输出**：
    * 主数据流接入 **Throttle** 后送入 **QT GUI Time Sink**。

![][p1]

</br>

### 4、运行效果

运行流程图后：

![][p2]

* 在 **QT GUI Time Sink** 中可以看到：波形在“高斯白噪声”与“$1\text{kHz}$ 正弦波”之间按固定周期交替切换。
* 在控制台或 **Message Debug** 中能观察到布尔型 PMT 消息（`#t` / `#f`）的实时异步投递日志。

</br>

### 参考链接

* [GNU Radio Wiki - Message Passing](https://wiki.gnuradio.org/index.php/Message_Passing)
* [GNU Radio Wiki - Polymorphic Types (PMTs)](https://www.google.com/search?q=https://wiki.gnuradio.org/index.php/Polymorphic_Types)




[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_python_block_msg_grc.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_python_block_msg_grc_show.gif

