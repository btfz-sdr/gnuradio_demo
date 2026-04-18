在数字通信领域，**MIMO**（Multiple-Input Multiple-Output，多输入多输出）是一项革命性的技术。简单来说，它通过在发送端和接收端同时使用多个天线，利用空间维度来大幅提升通信系统的容量和可靠性。


## 一、背景知识
### 1.1 MIMO 的核心原理

传统通信（如 SISO，单输入单输出）就像在一条窄路上跑一辆车。而 MIMO 则像是将道路拓宽成多车道的高速公路，并允许汽车在不同车道上并行行驶。



其核心思想是利用**多径效应**。在城市等复杂环境中，信号会因建筑物反射产生多个路径。MIMO 并不视这些干扰为敌人，反而利用它们来区分不同的数据流。

</br>

### 1.2 主要技术手段

主要有如下技术手段实现 MIMO，他们也有各自的优劣点：

分类 | 手段 | 优劣 | 图示
---|---|---|---
**空间复用 </br>Spatial Multiplexing**</br>(同时搬三箱不同的货) | <div style="max-width: 600px; overflow-wrap: break-word;">将待传输的数据切分成多个独立的子流，并在同一频率上通过不同天线同时发送。这直接倍增了数据传输速率。</br></br>**备注：** 接收端有多个天线，每个天线收到的都是 $N$ 个发射信号的混合体。只要这些信号经过的路径略有不同（由于环境反射），接收端就能通过复杂的矩阵运算（如零迫迫算法 Zero-Forcing 或最小均方误差 MMSE）把混合信号“拆解”回原来的独立子流。</br>**前提条件：** 需要信道具有丰富的**多径效应**。如果收发之间完全空旷（视距传输），信号路径太像，矩阵就会退化，复用效果大打折扣。</div> | **特点：** 提高**速度** </br> **优势：** 吞吐量（Mbps）直接翻倍，不需要额外带宽。</br>**劣势：** 对信道环境要求高；信道质量差时，误码率会飙升。 | ![][p1]
**空间分集 </br>Space Diversity**</br>(三个人同时搬一箱货) | <div style="max-width: 600px; overflow-wrap: break-word;">多个天线发送相同的信息。如果某一条路径受阻或衰减，接收端仍能从其他路径获得清晰信号，从而极大增强了链路的稳定性。</br></br>例如：两个天线交替或以特定编码发送**相同**的数据，接收端多个天线同时听。如果天线 A 因为建筑遮挡信号衰减了，天线 B 可能正处于信号波峰。通过“合并策略”（如最大比合并 MRC），系统能合成一个比单天线强得多的信号。</div> | **特点：** 提高**质量** </br> **优势：** 极大降低掉线率，扩大信号覆盖边缘，信号非常“硬”。</br>**劣势：** 浪费了天线资源，速率没有提升（还是只传一份数据）。 | ![][p2]
**波束赋形 </br>Beamforming**</br>(用扩音喇叭对着某人喊)  | <div style="max-width: 600px; overflow-wrap: break-word;">通过调整每个天线发射信号的相位和幅度，使电磁波定向指向用户，减少干扰并延伸覆盖范围。</br></br>例如：基站并不盲目地向四周撒网。通过微调每个天线阵元发射信号的**相位（时间差）**，让电磁波在特定方向上发生**相长干涉**（叠加增强），在其他方向上发生**相消干涉**（相互抵消）。结果就是信号形成了一个窄窄的“手电筒光束”，精准打在目标设备上。</div> | **特点：** 提高**增益** </br> **优势：** 能量集中，干扰更小，能传得更远；在 5G 毫米波这种易损信号中是救命稻草。</br>**劣势：** 需要实时追踪用户位置，算法复杂度高；对于高速移动的用户（如高铁）对齐难度大。  | ![][p3]

**注：** “时间分集”通常作为辅助手段。比如在 5G 中，我们会结合空时块编码 (STBC)，既在空间上分集，也在时间上分集，双保险。

</br>

## 二、从简单时间分集到 STBC（空时分组码）

在 MIMO（多输入多输出）系统的语境下，**时间分集**通常不再是孤立存在的，而是演变为一种结合了空间维度的技术，最典型的代表就是 **空时分组码（Space-Time Block Coding, STBC）**。

MIMO 时间分集的核心逻辑在于：利用**多个发射天线在不同的时间点**发送同一信息的不同变形，从而在接收端获得分集增益。

本课程将会从最简单的时间分集介绍到复杂的空时分组码（阿拉莫蒂码 (Alamouti Code)）。

### 2.1 课程前言

本系列实验是在《[基于 B210 的 OFDM 文件传输][#1]》基础上的升级。OFDM 解决了频域的可靠性，而 MIMO 将带我们进入**空间域**。我们将通过三个实验课程将 MIMO 彻底讲透：

- **[MIMO_OFDM_B210_空间分集][#2]**：通过天线协同与相位干预，亲手“握住”电磁波，理解信号如何在空间叠加 [已经发布] 
- **MIMO_OFDM_B210_时间分集**：学习通过“错峰出行”规避干涉，掌握导频正交化的雏形 [本课]
- **MIMO_OFDM_B210_空间复用**：挑战 B210 的性能极限，实现在同一频率、同一时间传输两倍的数据量

**注意：** 看到本教程前务必彻底搞懂 OFDM 相关课程，这是所有 MIMO 实验的底层基石！

</br>

### 2.2 简单的时间分集实验

这个简单的时间分集流程图 `trx_ofdm_mimo_time_diversity.grc` 比较简单，它是在上一课《[MIMO_OFDM_B210_空间分集][#2]》的流程图基础上微调得到的：

![][p8]

</br>

如上图，仅仅将发送部分的空间分集策略，改为通过一个自定义 python block 来实现时间分集：

```
import numpy as np
from gnuradio import gr
import time

class time_diversity_switch(gr.sync_block):
    """
    时间分集开关：
    - 输入：1 路复数 IQ 流
    - 输出：2 路复数 IQ 流
    - 参数 period_sec：切换周期（秒），默认 1 秒
    - 行为：每 period_sec 秒切换一次输出通道，另一路输出 0
    """

    def __init__(self, period_sec=1.0):
        gr.sync_block.__init__(
            self,
            name='Time Diversity Switch',
            in_sig=[np.complex64],
            out_sig=[np.complex64, np.complex64]
        )
        self.period_sec = float(period_sec)
        if self.period_sec <= 0:
            raise ValueError("Period must be positive.")
        self.last_switch_time = None
        self.current_output = 0  # 0 -> output 0 active; 1 -> output 1 active

    def work(self, input_items, output_items):
        in0 = input_items[0]
        out0 = output_items[0]
        out1 = output_items[1]
        noutput_items = len(out0)

        # 获取当前时间（秒）
        now = time.time()
        current_slot = int(now // self.period_sec) % 2  # 0 or 1

        # 切换逻辑（可选：只在跨周期时更新，但这里直接用当前 slot 更简单）
        if current_slot == 0:
            # 输出到端口0，端口1为0
            out0[:] = in0[:noutput_items]
            out1[:] = 0
        else:
            # 输出到端口1，端口0为0
            out0[:] = 0
            out1[:] = in0[:noutput_items]

        return noutput_items
```

</br>

运行后可以在 **2路发送数据** 窗口看出 TX1/TX2 两路在交替发送数据，我们可以选择 RX1 进行接收，观察到可以接收到数据，但是**相对于上一课的空间分集，会有误码率（这是因为数据在两路中不断切换带来的）**。

![][p9]

**备注：** 在数字通信领域，**时间分集（Time Diversity）是一种通过在不同的时间间隔内多次发送同一信息信号，以对抗无线信道中衰落（Fading）** 影响的技术。

其核心思想是：如果两次发送的时间间隔足够大（超过信道的相干时间），那么这两次信号经历的衰落将是相互独立的。即使其中一个信号在传输过程中遭遇了严重的深衰落，另一个信号仍有较大概率能被成功接收。

我们上述实验仅仅演示了对数据的时序控制能力，并没有严格按照时间分集的定义来设计，具体原因看到后面大家就能理解：导频时间正交技术会用到类似的操作。


</br>

[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/90
[#2]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/91    


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/mimo_spatial_multiplexing.png     
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/mimo_space_diversity.png    
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/mimo_beamforming.png          
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_space_diversity_grc.png    
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_show_siso.png     
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_show_mimo.png       
[p7]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_show_beamforming2.png    
[p8]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_time_diversity_grc.png      
[p9]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_time_diversity_grc_show2.png
[p10]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/mimo_how_to_get_H.png
