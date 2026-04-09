## 前言

在深入研究本项目之前，强烈建议你先按顺序攻克以下**四个前置课程**，建立扎实的基础：

![][p1]

- [OFDM 正交频分复用收发 DEMO 演示][#1]
- [OFDM TX 详解][#2]    
- [OFDM RX 详解][#3]     
- [OFDM 文件收发][#5]       

</br>

此外，若想实现完整的应用层交互，推荐同步学习《[从 Bit 到 App：GNURadio 极简跨层传输框架（告别导师的“微信功能”焦虑）][#4]》，彻底告别“导师问你微信聊天功能怎么实现”的焦虑。

</br>

## 流程图介绍
### 1. 从繁至简：模块的集成

通过之前的 TX/RX 详解，我们已经构建出一个逻辑完整但规模庞大的 OFDM 收发流程图：

![][p8]

在 GNU Radio 中，为了提升开发效率，我们可以利用**层次化模块（Hierarchical Blocks）**：

- **OFDM Receiver** 是接收端所有核心逻辑的合并版
- **OFDM Transmitter** 则是发送端的对应集成版本

利用这些集成块，我们可以将复杂的流程图瞬间简化。

</br>

### 2. 跨层集成：文件与消息收发

结合《[从 Bit 到 App：GNURadio 极简跨层传输框架（告别导师的“微信功能”焦虑）][#4]》中的文件传输逻辑，我们最终得到了一个既能传消息、又能传文件的 **OFDM 极简跨层流程图**：

![][p2]

</br>

### 3. 硬件落地：USRP B210 实战

基于上述逻辑，只需针对硬件参数稍加微调，即可实现基于 B210 的 OFDM 文件收发实验 `ofdm_loopback_b210_min.grc`：

![][p4]

- **开发技巧：关于“集成”与“展开”的混用**

    实际开发中，这些集成块可以与展开式的 OFDM 实现**灵活混用**。例如：采用“发送端展开 + 接收端集成”的模式。
    - **为何保留展开式？** 在调试阶段，展开式架构能让我们直观地观察数据流、异步消息及星座图的动态。
    - **面向未来：** 展开式架构也是后续扩展 **MIMO-OFDM** 实验的必经之路。

> 全部展开的基于 B210 收发的流程图在 `ofdm_loopback_b210_complex.grc`

</br>

## 效果演示

该系统支持**实时消息**与**文件**的传输。传输文件时，UI 会同步显示传输进度百分比。

- **⚠️ 注意事项：**
    - 建议测试文件大小控制在 **100KB** 以内。
    - 请根据个人电脑的 CPU 性能，合理调整采样率，避免溢出（Overrun）或滞后（Underrun）。

![][p3]


</br>

[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/47     
[#2]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/48
[#3]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/88
[#5]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/89
[#4]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/87     

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/ofdm_series_show2.png    
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/ofdm_trx_sch.png      
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/ofdm_trx_sch_show.png     
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/ofdm_trx_b210_min_sch.png     


[p8]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/tx_ofdm.png       


