### 1、前言

在 GNU Radio 流程图的调试和控制过程中，除了流数据（Stream）传输外，我们经常需要定时触发某个控制指令或发送特定的测试数据。**Message Strobe（消息选通器）** 就是专门为此而生的调试利器。

它可以按照设定的时间间隔（周期），定时向网络或其它模块的控制端口发送指定的 PMT 消息，常用于**定时修改模块参数（如动态改频）**或**模拟数据包源（如测试 ZMQ 消息传输）**。

</br>

### 2、应用场景一：定时控制模块参数（动态改频）

![][p2]

`demo2_change_frequence.grc` 演示了如何使用 Message Strobe 动态控制 `Signal Source` 的输出频率：

- 1）**核心配置**：
    * **`Message Strobe`**：
        * `Message PMT`: `pmt.cons(pmt.intern("freq"), pmt.from_double(1000))`（在 GRC 简写为 `((freq . 1000))` 对）。
        * `Period (ms)`: `5k`（即每隔 $5$ 秒触发一次）。
- 2）**控制链路（灰色虚线）**：
    * 将 `Message Strobe` 的 `strobe` 消息输出端口连接到 `Signal Source` 的 `cmd` 控制输入端口。
- 3）**实验效果**：
    * 流程图运行后，每隔 5 秒，`Message Strobe` 就会向 `Signal Source` 发送一条修改频率为 $1\text{kHz}$ 的命令，在 **QT GUI Frequency Sink** 上可以实时观察到谱线峰值的跳变。

</br>

### 3、应用场景二：模拟异步消息源与 ZMQ 通信调试

![][p2]

`demo1_hello_world.grc` 演示了如何结合 Message Strobe、ZMQ 消息模块与 Message Debug 进行纯异步消息链路的调试：

- 1）**消息生成与发送**：
    * **`Message Strobe`**：配置为每隔 $2000\text{ms}$（$2$ 秒）自动生成并投递一条字符串消息 `"hello world"`。
    * **`ZMQ PUB Message Sink`**：绑定 `tcp://127.0.0.1:50245` 端口，将异步 PMT 消息以发布者模式广播出去。
- 2）**消息接收与打印分析**：
    * **`ZMQ SUB Message Source`**：连接同一端口 `tcp://127.0.0.1:50245`，订阅接收网络消息。
    * **`Message Debug`**：连接至 `print` 端口，在 GNU Radio 控制台终端实时打印接收到的 PMT 消息内容及时间戳。
- 3）**流控与运行维持**：
    * 引入 **`Null Source` $\rightarrow$ `Throttle` $\rightarrow$ `Null Sink`** 链路，为纯消息驱动的流程图提供必要的系统主时钟基础，防止 CPU 满载跑死。

</br>

### 4、常用 PMT 表达式速查

在使用 Message Strobe 发送自定义命令或数据时，常用以下 PMT 表达式写法：

| 命令用途 | PMT 表达式写法 | 说明 |
| --- | --- | --- |
| **字符串消息** | `pmt.string_to_symbol("hello world")` | 发送简单文本消息 |
| **字典/键值对命令** | ` pmt.dict_add(pmt.make_dict(), pmt.intern("freq"), pmt.from_float(1000))` | 修改参数（如改变频率为 $1\text{kHz}$） |

</br>

### 参考链接

* [GNU Radio Wiki - Message Strobe](https://wiki.gnuradio.org/index.php/Message_Strobe)
* [GNU Radio Wiki - Polymorphic Types (PMTs)](https://www.google.com/search?q=https://wiki.gnuradio.org/index.php/Polymorphic_Types)
* [GNU Radio Wiki - ZMQ PUB Message Sink](https://wiki.gnuradio.org/index.php/ZMQ_PUB_Message_Sink)


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202608/grc_tui_msg_grc1.png       
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202608/grc_tui_msg_grc2.png    

