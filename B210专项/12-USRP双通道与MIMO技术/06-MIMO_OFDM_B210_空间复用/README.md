在数字通信领域，**MIMO**（Multiple-Input Multiple-Output，多输入多输出）是一项革命性的技术。简单来说，它通过在发送端和接收端同时使用多个天线，利用空间维度来大幅提升通信系统的容量和可靠性。


## 一、背景知识
### 1.0 前置课程

**注意，学习本课程前务必弄懂其前置 7 个课程，否则根本扛不住！！！**

<div style="display: flex; align-items: flex-start; gap: 20px;">
<div style="font-family: sans-serif; line-height: 1.6;">

![][p10]

</div>
<div style="flex: 0 0 36%; max-width: 36%;">

这些前置课程包括：

- 4 个 [GNU Radio 基础教程](https://beautifulzzzz.com/gnuradio/tutorial/topic/1) 中的 OFDM 教程：    
 
    - 《[OFDM 正交频分复用收发 DEMO 演示](https://beautifulzzzz.com/gnuradio/tutorial/lesson/47)》     
    - 《[OFDM TX 详解](https://beautifulzzzz.com/gnuradio/tutorial/lesson/48)》    
    - 《[OFDM RX 详解](https://beautifulzzzz.com/gnuradio/tutorial/lesson/88)》     
    - 《[OFDM 文件收发](https://beautifulzzzz.com/gnuradio/tutorial/lesson/89)》 

- 3 个 [B210 专项教程](https://beautifulzzzz.com/gnuradio/tutorial/topic/2) 中的 OFDM+MIMO 教程：      

    - 《[第三节：基于 B210 的 OFDM 文件传输](https://beautifulzzzz.com/gnuradio/tutorial/lesson/90)》     
    - 《[第四节：MIMO OFDM B210 空间分集数据收发](https://beautifulzzzz.com/gnuradio/tutorial/lesson/91)》    
    - 《[第五节：MIMO OFDM B210 时间分集数据收发](https://beautifulzzzz.com/gnuradio/tutorial/lesson/92)》     

- 1 个 《[使用 gr-modtool 创建 C++ OOT](https://beautifulzzzz.com/gnuradio/tutorial/lesson/54)》教程

**备注：** 对于有些演示课程，没有文档说明，只有视频和相关流程图。

</div>
</div>


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

</br>

### 1.3 MIMO 的常见分类

根据应用场景的不同，MIMO 主要分为以下几种形式：

| 类型 | 描述 | 应用场景 |
| :--- | :--- | :--- |
| **SU-MIMO** | 单用户 MIMO。所有天线资源同时只服务于一个用户。 | 早期 4G 终端 |
| **MU-MIMO** | 多用户 MIMO。基站可以同时与多个不同的移动终端进行通信。 | Wi-Fi 6, 5G |
| **Massive MIMO** | 大规模 MIMO。在基站端部署数十甚至上百根天线，是 5G 的核心技术。 | 5G 宏基站 |

</br>

### 1.4 数学表达（简述）

在物理层，MIMO 的输入输出关系通常表示为：$\mathbf{y} = \mathbf{H}\mathbf{x} + \mathbf{n}$

其中：
* $\mathbf{y}$ 是接收信号向量。
* $\mathbf{H}$ 是信道矩阵（描述了每一对收发天线之间的增益）。
* $\mathbf{x}$ 是发送信号向量。
* $\mathbf{n}$ 是噪声。

通过对信道矩阵 $\mathbf{H}$ 进行处理（如奇异值分解 SVD），系统可以开辟出多个并行的虚拟通道。

</br>

## 二、 MIMO 信道矩阵 $\mathbf{H}$ 是如何“捞”出来的？

在 OFDM 系统中，获取 $\mathbf{H}$ 的核心科技叫：**正交导频（Orthogonal Training Sequences）**。

### 2.1 核心矛盾

如果两根发射天线（Tx1, Tx2）在同一时间、同一频率发送完全一样的同步字（Sync Word），接收天线（Rx1）收到的将是两个信号的叠加：$y_1 = (h_{11} + h_{12}) \cdot \text{sync\_word}$。此时，数学上你是**无法拆分**出 $h_{11}$ 和 $h_{12}$ 的。

</br>

### 2.2 解决方法：时/频/码域正交

<div style="display: flex; align-items: flex-start; gap: 20px;">
<div style="font-family: sans-serif; line-height: 1.6;">
为了得到矩阵的四个元素，我们必须让两个天线的“名片”（导频）互不干扰：

* **频率正交（梳状导频）**：这是最常用的。
    * **Tx1**：在偶数子载波（0, 2, 4...）发送导频，奇数子载波填 0。
    * **Tx2**：在奇数子载波（1, 3, 5...）发送导频，偶数子载波填 0。
    * **接收端逻辑**：Rx1 在偶数位置收到的就是 $h_{11}$，奇数位置收到的就是 $h_{12}$。通过插值，就能还原出全频段的 $H$。
* **时间正交**：
    * 第一个符号由 Tx1 发送，Tx2 闭嘴；第二个符号由 Tx2 发送，Tx1 闭嘴。
    * **缺点**：浪费时间，且如果信道变化快（如高速移动），第二个符号时的信道可能已经变了。
* **码域正交（Orthogonal Codes）**：
    * 两根天线同时发，但 Tx1 发的是 $[+1, +1]$，Tx2 发的是 $[+1, -1]$。
    * 接收端通过相关运算（加法和减法）就能把两路信号剥离出来。
</div>
<div style="flex: 0 0 36%; max-width: 36%;">

![][p4]

</div>
</div>

### 2.3 MIMO 导频正交方案深度对比

各维度对比表如下：

| 特性维度 | **频率正交 (FDM / Comb-type)** | **时间正交 (TDM / Block-type)** | **码域正交 (CDM / Orthogonal Cover)** |
| :--- | :--- | :--- | :--- |
| **核心机制** | 不同天线占用不同的**子载波** | 不同天线占用不同的**符号时间** | 不同天线在同频同波下使用**正交编码** |
| **推荐等级** | ⭐⭐⭐⭐⭐ (工业界主流) | ⭐ (仅限静态/实验) | ⭐⭐⭐ (5G 高级特性) |
| **抗移动性** | **极强**。两路信道同时测量，无时间差。 | **极差**。测量 Tx2 时，Tx1 的信道可能已变。 | **中等**。受限于相干时间内的正交性保持。 |
| **抗频选衰落** | **一般**。填 0 的位置需要靠插值还原。 | **极强**。每根天线都能探测全频段。 | **极强**。每根天线都能探测全频段。 |
| **功率效率** | **较低**。单天线瞬时功率波动大 (PAPR)。 | **高**。天线可以全功率发送导频。 | **高**。能量分布均匀，且有处理增益。 |
| **同步要求** | 正常。与常规 OFDM 一致。 | 正常。 | **极高**。频率偏移 (CFO) 会破坏码间正交。 |
| **典型应用** | **4G LTE / Wi-Fi / 5G (部分)** | 早期实验系统、低速传感器 | **5G NR (DMRS 信号)** |
| **B210 实现难度** | **低**。只需修改 `sync_word` 的向量索引。 | **低**。控制发射开关时间即可。 | **中**。需要额外的矩阵解扩运算。 |


</br>

## 三、基于时间正交导频 H 矩阵求解

我们本实验使用时间正交导频来求解 H 矩阵：

### 3.1 MIMO 2 路 TX 设计

**发送序列：**

- 天线1: [`sync_word1`, `sync_word2`, `zero_word`, `header`, `payload`]
- 天线2: [`zero_word`, `zero_word`, `sync_word2`, `zero_word`, `payload`]

**接收端观测：**

- 符号 0：`sync_word1`（仅天线1发送）
- 符号 1：`sync_word2`（仅天线1发送）
- 符号 2：`sync_word2`（仅天线2发送）
- 符号 3 及以后：header+payload数据

</br>

![][p5]

**备注：** 因此，此时发送流程图比较简单，只要巧妙利用 `OFDM_Carrier_Allocator` 字段，便可设计出导频时间正交属性的数据流。

</br>

### 3.2 MIMO 2 路 RX 设计

在 2x2 MIMO 系统中，H 矩阵为：

```
H = [h11  h12]
    [h21  h22]
```

其中：

- `h11`: 天线1→接收天线1的信道系数
- `h12`: 天线2→接收天线1的信道系数
- `h21`: 天线1→接收天线2的信道系数
- `h22`: 天线2→接收天线2的信道系数

</br>

#### 1）创建自己的 MIMO 信道估计 C++ OOT

由于官方封装的 [`OFDM_Channel_Estimation`][#1] 比较死板只能处理 SISO 模式数据，并且只允许两个（或一个）同步字的情况，我们想要用在 MIMO 2x2 中需要改造其支持：

- 三个同步字情况
- 计算 hx1, hx2

**注：** 原来这个模块是基于两个同步字来进行信道估计的，现在也是进行信道估计，因此改动不大，我们使用 C++ OOT 来对官方块进行微调：

参考《[[36]. GNU Radio 系列教程（三六）—— 使用 gr-modtool 创建 C++ OOT][#5]》 创建一个自己的 `ofdm_chanest_vcvc` 块：

```
gr_modtool newmod mimo  
cd gr-mimo 
➜  gr-mimo git:(main) ✗ gr_modtool add ofdm_chanest_vcvc
GNU Radio module name identified: mimo
{'sink': 'Source block with outputs, but no stream inputs', 'source': 'Sink block with inputs, but no stream outputs', 'sync': 'Block with synchronous 1:1 input-to-output', 'decimator': 'Block with synchronous N:1 input-to-output', 'interpolator': 'Block with synchronous 1:N input-to-output', 'general': 'General-purpose block type', 'tagged_stream': 'Block with input-to-output flow controlled by input stream tags (e.g. packetized streams)', 'hier': 'Hierarchical container block for other blocks; usually can be described by a flowgraph', 'noblock': 'C++ or Python class'}
Enter block type: general
Language (python/cpp): cpp
Language: C++
Block/code identifier: ofdm_chanest_vcvc
Please specify the copyright holder: btfz
Enter valid argument list, including default arguments: 

Add Python QA code? [Y/n] n
Add C++ QA code? [Y/n] n
Adding file 'lib/ofdm_chanest_vcvc_impl.h'...
Adding file 'lib/ofdm_chanest_vcvc_impl.cc'...
Adding file 'include/gnuradio/mimo/ofdm_chanest_vcvc.h'...
Adding file 'python/mimo/bindings/docstrings/ofdm_chanest_vcvc_pydoc_template.h'...
Adding file 'python/mimo/bindings/ofdm_chanest_vcvc_python.cc'...
Adding file 'grc/mimo_ofdm_chanest_vcvc.block.yml'...
Editing grc/CMakeLists.txt...
```

然后参考：[`ofdm_chanest_vcvc_impl.h`][#3] [`ofdm_chanest_vcvc_python.cc`][#2] 和 [`digital_ofdm_chanest_vcvc.block.yml`][#4] 修改我们自己模块中的相应代码（基本上都一样，只是官方的是基于 digital 命令空间的，我们是 mimo 命名空间，稍微需要改几处（最新源码都在 github 和 gitee 上，这里就不贴代码了，太多）。

```
cd gr-mimo
rm -rf build
mkdir build
cd build
cmake ..
make
sudo make install
sudo ldconfig 

# 之后就能在 gnuradio 中看到一个叫 mimo 的组，里面有我们创建的 ofdm_chanest_vcvc 模块
# 删除用 sudo make uninstall 
```

</br>

#### 2）MIMO 信道估计 C++ OOT 核心逻辑介绍

该 OOT 实现的是 **2x2 MIMO 接收端的一个天线分支（RX1 或 RX2）** 的处理逻辑。在 MIMO 系统中，每一个接收天线都会同时“听到”来自两个发送天线（TX1, TX2）的信号。

</br>

● <u>为了从叠加的信号中分别解耦出 $h_{x1}$ 和 $h_{x2}$，我采用了 **分时同步符号（Time-Multiplexed Training Symbols）** 的策略</u>：

```
int carr_offset = get_carr_offset(in, in + d_fft_len); 
get_chan_taps(in + d_fft_len, d_ref_sym, carr_offset, h_a_taps);      
get_chan_taps(in + 2 * d_fft_len, d_sync_symbol3, carr_offset, h_b_taps); 

add_item_tag(0, nitems_written(0), 
             pmt::string_to_symbol("ofdm_sync_carr_offset"), 
             pmt::from_long(carr_offset));

add_item_tag(0, nitems_written(0), 
             pmt::string_to_symbol("ofdm_sync_chan_taps"), 
             pmt_h_a);
```

在 `general_work` 中可以清晰看到同步序列布局（对于当前接收天线 $x$，接收到的频域信号 $Y_x[k]$ 在同步阶段满足）：

1. **Symbol 1 (`sync_symbol1`)**: 主要是为了 `get_carr_offset`（频率同步）（这个是保留原来的逻辑）    
2. **Symbol 2 (`sync_symbol2`)**: TX1 发送特定导频，TX2 静默。此时计算出的 `h_a_taps` 即为 $h_{x1}$
    * $Y_x^{(2)}[k] = h_{x1}[k] \cdot X_{sync1}[k] + Noise$，代码对应：`taps[i] = rx_sym[i] / ref_sym[i]` (即执行 $h_{x1} = Y/X$)
3. **Symbol 3 (`sync_symbol3`)**: TX2 发送特定导频，TX1 静默。此时计算出的 `h_b_taps` 即为 $h_{x2}$
    *  $Y_x^{(3)}[k] = h_{x2}[k] \cdot X_{sync3}[k] + Noise$，代码对应：第二次调用 `get_chan_taps` 得到 `h_b_taps`

**备注1：** 其中 `h_a_taps` 就是原来这个块的 `ofdm_sync_chan_taps` 标签数据，因此我没有重复调用，而是直接复用它打在了 `ofdm_sync_chan_taps` 标签上。

</br>

● <u>我们计算出的 h 是通过消息向下游传递的，为了保证后续收集两路 h 信息合并成 H 信息的块，这里需要对 `h_a`, `h_b` 的消息打上时间戳</u>：

```
pmt::pmt_t curr_rx_time = get_current_rx_time(nitems_read(0));
if (curr_rx_time != pmt::PMT_NIL) {
    pmt::pmt_t h_dict = pmt::make_dict();
    h_dict = pmt::dict_add(h_dict, pmt::mp("h_a"), pmt::init_c32vector(d_fft_len, h_a_taps));
    h_dict = pmt::dict_add(h_dict, pmt::mp("h_b"), pmt::init_c32vector(d_fft_len, h_b_taps));
    message_port_pub(pmt::mp("h_out"), pmt::cons(curr_rx_time, h_dict));
}
```

* **为什么要这样做？**     
    - 1）严格的时间戳对齐 (Timestamp-based Synchronization)    

      在 GNU Radio 这种异步流处理框架中，由于缓存和调度原因，不同模块的处理速度可能不同。通过将 **业务数据**（流）和 **估计出的信道参数**（消息端口）绑定同一个时间戳，下游模块（如 MIMO 均衡器）可以在收到消息后，精准地找到该信道系数对应的采样点，从而避免由于处理延迟导致的信道补偿错位。    

    - 2）H 矩阵的原子性封装 (Atomic Delivery)

    - 3）支撑后续的“两路对齐”
        * **RX1 节点**：发布 $(h_{11}, h_{12})$ + 时间戳 $T_1$
        * **RX2 节点**：发布 $(h_{21}, h_{22})$ + 时间戳 $T_1$      

        下游的 **MIMO 合并/均衡模块** 会根据这个 $T_1$ 时间戳，将来自两个不同 Block 的消息“配对”，构建出最终的矩阵：

$$H = \begin{bmatrix} h_{11} & h_{12} \\\\ h_{21} & h_{22} \end{bmatrix}$$

</br>

#### 3）MIMO 2-RX 信道估计流程图

![][p6]


</br>

## 三、 核心算法演进：如何从混合信号中“提取”数据？

在上面，我们利用时间正交导频成功求得了信道矩阵 $\mathbf{H}$。然而，在 **空间复用（Spatial Multiplexing）** 模式下，接收端面临的最大挑战是：$N$ 根天线收到的信号是互相干扰、混合在一起的。

我们的目标是求解线性方程组 $\mathbf{y} = \mathbf{H}\mathbf{x} + \mathbf{n}$，其中 $\mathbf{y}$ 是接收信号，$\mathbf{x}$ 是我们想要恢复的发送数据，$\mathbf{n}$ 是噪声。如何设计一个均衡矩阵 $\mathbf{W}$ 来从 $\mathbf{y}$ 中准确恢复 $\mathbf{x}$，决定了系统的最终性能。

### 3.1 常见的 MIMO 信号检测方案

在工程实践中，MIMO 检测算法主要分为**线性检测**和**非线性检测**两大类。

**1. 线性检测算法 (Linear Detection)**
这是目前工程实现中最主流的方案，因为它在计算复杂度和性能之间取得了较好的平衡。
*   **迫零算法 (Zero-Forcing, ZF)**：
    *   **原理**：试图通过信道矩阵的伪逆 $\mathbf{W} = (\mathbf{H}^H\mathbf{H})^{-1}\mathbf{H}^H$ 来完全消除流间干扰。
    *   **代价**：它在消除干扰的同时，往往会极大地**放大噪声**。这导致在低信噪比（SNR）环境下，误码率（BER）表现较差。
*   **最小均方误差 (MMSE)**：
    *   **原理**：在求逆时引入了噪声功率 $\sigma^2$ 的考量。公式为 $\mathbf{W} = (\mathbf{H}^H\mathbf{H} + \sigma^2\mathbf{I})^{-1}\mathbf{H}^H$。
    *   **优势**：它在“抑制干扰”和“避免噪声放大”之间取得了最佳折中，是目前 4G/5G 接收机的首选方案。

**2. 非线性检测算法 (Non-linear Detection)**
*   **最大似然检测 (MLD)**：遍历所有可能的发送组合，寻找欧氏距离最近的一个。虽然性能最好，但计算量随天线数量呈**指数级增长**，通常仅作为理论性能上限参考。
*   **串行干扰抵消 (SIC/V-BLAST)**：像“剥洋葱”一样，先解调信号最强的一路，从总信号中减去它，再解调下一路。

</br>

为了更直观地对比上述方案，下表总结了它们的核心特性：

| 方案 | 核心思想 | 优势 | 劣势 | 适用场景 |
| :--- | :--- | :--- | :--- | :--- |
| **ZF (迫零)** | 矩阵求逆，强制消除干扰 | **算法简单**，计算量小，易于硬件实现 | **噪声增强效应**明显，低信噪比下性能差 | 信道条件好、信噪比高的环境 |
| **MMSE** | 考虑噪声统计特性的加权求逆 | 综合性能最优，抗噪能力强 | 需要估计噪声功率，计算量略高于 ZF | 4G/5G 主流商用标准 |
| **MLD** | 暴力遍历所有可能性 | 误码率性能理论最优 | **计算复杂度极高**，实时性差 | 理论仿真、极低阶 MIMO |
| **SIC** | 逐级解调并减去干扰 | 性能接近 MLD，复杂度适中 | 存在误差传播风险（第一步解错，后面全错） | V-BLAST 系统 |

</br>

### 3.2 本实验采用的 ZF 方法

尽管 MMSE 在理论上具有更好的抗噪性能，但在本实验（或本项目）的实际场景中，我们最终选择了 **迫零算法 (Zero-Forcing, ZF)** 作为核心解调方案。做出这一选择主要基于以下几点考量：

1.  **实现复杂度与实时性**：
    基于 GNU Radio 的软件无线电平台对实时计算能力有较高要求。ZF 算法仅需进行矩阵求逆运算，无需像 MMSE 那样额外估计噪声方差 $\sigma^2$，也避免了 MLD 的指数级运算量。这使得 ZF 在通用处理器（CPU）上更容易实现低延迟的实时解调。

2.  **信道环境假设**：
    在我们的实验环境中（如视距传输或高信噪比场景），噪声的影响相对较小，而流间干扰是主要矛盾。ZF 算法能够最直接、彻底地消除天线间的互相关干扰，满足系统解调的基本需求。

3.  **算法验证的基准**：
    作为最基础的线性检测算法，ZF 是验证 MIMO 链路连通性的最佳起点。

</br>

#### 1）2x2 MIMO ZF (或近似 MMSE) 算法实现逻辑

在本系统中，接收端通过计算信道矩阵 $\mathbf{H}$ 的伪逆矩阵 $\mathbf{H}^{\dagger}$ 来构建均衡器：

$$ \mathbf{W}_{ZF} = (\mathbf{H}^H\mathbf{H})^{-1}\mathbf{H}^H $$

最终的信号估计值 $\hat{\mathbf{x}}$ 通过下式获得：

$$ \hat{\mathbf{x}} = \mathbf{W}_{ZF} \mathbf{y} $$

通过这种方式，我们成功将混合的接收信号 $\mathbf{y}$ 分离还原为独立的数据流，完成了 MIMO 空间复用的解调过程，对于我们 2x2 系统推导如下：

![][p7]

![][p8]

#### 2）创建自己的 2x2 MIMO ZF (或近似 MMSE) 解码器 C++ OOT

该 OOT 实现了一个 **2x2 MIMO ZF (或近似 MMSE) 解码器**，它需要两个实例（如两个节点）协同工作，通过交换接收信号和信道信息来解码各自的数据流：

1.  **启动**: 两个 `mimo_precision_decoder` 实例启动，分别设置 `d_path_id` 为 1 和 2。
2.  **数据流入 `work`**: 本地接收到 OFDM 符号 `y_local`。
3.  **广播**: `work` 将 `y_local` 发送给对方。
4.  **接收**: `handle_y_msg` 在本节点接收到来自对方的 `y_remote`。
5.  **信道更新**: `handle_h_local/remote_msg` 和 `update_h_smooth` 持续更新本地存储的完整 2x2 信道矩阵 $\mathbf{H}$。
6.  **延迟对齐**: `work` 中的 FIFO 确保了 `y_local` (延迟后) 和 `y_remote` (刚接收) 在时间上对齐。
7.  **检测**: `work` 使用对齐的 `y_local`, `y_remote` 和最新的 $\mathbf{H}$ 执行 ZF/MMSE 运算，输出解码后的数据流。

</br>

这个 OOT 增加到我们的 mimo 分类中（有全部源码，创建过程可以不用研究）：

```
➜  gr-mimo git:(master) ✗ gr_modtool add mimo_precision_decoder
GNU Radio module name identified: mimo
('sink', 'source', 'sync', 'decimator', 'interpolator', 'general', 'tagged_stream', 'hier', 'noblock')
Enter block type: general
Language (python/cpp): cpp
Language: C++
Block/code identifier: mimo_precision_decoder
Please specify the copyright holder: btfz
Enter valid argument list, including default arguments: 
 
Add Python QA code? [Y/n] n
Add C++ QA code? [Y/n] n
Adding file 'lib/mimo_precision_decoder_impl.h'...

Adding file 'lib/mimo_precision_decoder_impl.cc'...
Adding file 'include/gnuradio/mimo/mimo_precision_decoder.h'...
Adding file 'python/mimo/bindings/docstrings/mimo_precision_decoder_pydoc_template.h'...
Adding file 'python/mimo/bindings/mimo_precision_decoder_python.cc'...
Adding file 'grc/mimo_mimo_precision_decoder.block.yml'...
Editing grc/CMakeLists.txt...
```

核心代码如下：

<u>**`handle_y_msg()` - 处理远程接收信号**</u>
*   接收来自另一个协同节点的接收信号 `y`。
*   将接收到的信号与其时间戳 `key` 一起存储到 `d_remote_y_buffer` 地图中，以便在 `work` 函数中按 `key` 查找。

```
void mimo_precision_decoder_impl::handle_y_msg(pmt::pmt_t msg) {
    if (pmt::is_pair(msg)) {
        std::string ts_str = pmt::write_string(pmt::car(msg));
        pmt::pmt_t data_pmt = pmt::cdr(msg);
        
        if(pmt::is_c32vector(data_pmt)) {
            std::lock_guard<std::mutex> lock(d_buffer_mutex);
            d_remote_y_buffer[ts_str] = pmt::c32vector_elements(data_pmt);
            
            if (d_remote_y_buffer.size() > (size_t)d_max_cache) {
                d_remote_y_buffer.erase(d_remote_y_buffer.begin());
            }
        }
    }
}
```

<u>**`handle_h_local_msg()` / `handle_h_remote_msg()` - 处理信道估计**</u>
*   分别接收来自**本地**和**远程**节点的信道估计结果 (`h_a`, `h_b`)。
*   这些 `h_a`, `h_b` 被认为是信道矩阵 $\mathbf{H}$ 的不同行或列的部分信息。

```
void mimo_precision_decoder_impl::handle_h_local_msg(pmt::pmt_t msg) {
    try {
        pmt::pmt_t dict = pmt::cdr(msg);
        pmt::pmt_t ha_pmt = pmt::dict_ref(dict, pmt::intern("h_a"), pmt::PMT_NIL);
        pmt::pmt_t hb_pmt = pmt::dict_ref(dict, pmt::intern("h_b"), pmt::PMT_NIL);
        if(pmt::is_c32vector(ha_pmt) && pmt::is_c32vector(hb_pmt)) {
            update_h_smooth(pmt::c32vector_elements(ha_pmt), pmt::c32vector_elements(hb_pmt), true);
        }
    } catch (...) {}
}

void mimo_precision_decoder_impl::handle_h_remote_msg(pmt::pmt_t msg) {
    try {
        pmt::pmt_t dict = pmt::cdr(msg);
        pmt::pmt_t ha_pmt = pmt::dict_ref(dict, pmt::intern("h_a"), pmt::PMT_NIL);
        pmt::pmt_t hb_pmt = pmt::dict_ref(dict, pmt::intern("h_b"), pmt::PMT_NIL);
        if(pmt::is_c32vector(ha_pmt) && pmt::is_c32vector(hb_pmt)) {
            update_h_smooth(pmt::c32vector_elements(ha_pmt), pmt::c32vector_elements(hb_pmt), false);
        }
    } catch (...) {}
}
```

<u>**`update_h_smooth()` - 更新信道矩阵**</u>
*   这是**信道融合与平滑**的核心。
*   根据 `d_path_id` 和 `is_local` 标志，决定将接收到的 `h_a`, `h_b` 信息更新到 `d_h11`, `d_h12`, `d_h21`, `d_h22` 中的哪个位置。
*   使用**指数平滑** `(1-d_alpha) * old_value + d_alpha * new_value` 来更新信道系数，使其随时间缓慢变化，适应信道的慢衰落。

```
void mimo_precision_decoder_impl::update_h_smooth(
    const std::vector<std::complex<float>>& h_a, 
    const std::vector<std::complex<float>>& h_b, bool is_local) 
{
    std::lock_guard<std::mutex> lock(d_buffer_mutex);
    auto update = [this](std::vector<std::complex<float>>& target, const std::vector<std::complex<float>>& src) {
        if(src.size() != (size_t)d_fft_len) return;
        for(int i=0; i<d_fft_len; ++i) 
            target[i] = (1.0f - d_alpha) * target[i] + d_alpha * src[i];
    };

    if (d_path_id == 1) {
        if (is_local) { update(d_h11, h_a); update(d_h12, h_b); }
        else { update(d_h21, h_a); update(d_h22, h_b); }
    } else {
        if (is_local) { update(d_h21, h_a); update(d_h22, h_b); }
        else { update(d_h11, h_a); update(d_h12, h_b); }
    }
}
```

<u>**`work()` - 核心处理函数**</u>
这是代码的**心脏**，负责实际的数据流处理。

*   **获取输入**: 从 GNU Radio 输入端口获取当前 OFDM 符号块。
*   **处理标签**: 读取 `rx_time` 标签，用于建立时间同步和帧对齐，生成一个唯一的 `key` (例如 `timestamp_frame_index`)。
*   **广播本地信号**: 将当前接收到的 OFDM 符号 `y` 通过 `y_out` 消息端口发送给另一个协同节点，附带 `key` 以便对方识别。
*   **延迟线 (FIFO)**: 将当前 `key` 和信号放入一个延迟队列 `d_local_fifo`，然后取出队列前端（已延迟了 `d_delay_syms` 个符号）的旧信号 `old_item`。这个 `old_item` 是本地稍早前接收到的信号。
*   **MIMO 检测**:
    *   查看是否有来自远程节点的、与 `old_item.key` 相匹配的接收信号 `y_remote`。
    *   **如果有匹配信号**:
        *   遍历 `d_fft_len` 个子载波。
        *   对于每个子载波 `k`，使用当前存储的 2x2 信道矩阵 $\mathbf{H}$ (`d_h11[k]`, `d_h12[k]`, `d_h21[k]`, `d_h22[k]`)。
        *   计算行列式 `det = H11*H22 - H12*H21`，并计算带正则化项的倒数 `inv_det = conj(det) / (|det|^2 + lambda_reg)`。**这就是 ZF/MMSE 的核心**。
        *   根据 `d_path_id` (1 或 2) 决定解码哪个数据流，应用相应的 ZF 公式：
            *   `path_id==1`: $\hat{x}\_1 = (H_{22} y_1 - H_{12} y_2) / \det(\mathbf{H})$
            *   `path_id==2`: $\hat{x}\_2 = (H_{11} y_2 - H_{21} y_1) / \det(\mathbf{H})$
        *   将解码结果写入输出缓冲区 `out`。
    *   **如果没有匹配信号**: 执行降级模式，对本地信号做简单的信道逆滤波，性能大幅下降。

```
int mimo_precision_decoder_impl::work(int noutput_items,
                                      gr_vector_const_void_star &input_items,
                                      gr_vector_void_star &output_items)
{
    const auto *in = (const std::complex<float> *)input_items[0];
    auto *out = (std::complex<float> *)output_items[0];

    std::vector<tag_t> tags;
    get_tags_in_window(tags, 0, 0, noutput_items);

    for (int i = 0; i < noutput_items; i++) {
        // 1) 处理标签: 读取 rx_time 标签，用于建立时间同步和帧对齐，生成一个唯一的 key (例如 timestamp_frame_index)
        // 一个帧的 payload = 10 个 item, 我们每个 item 通过 msg 发出，并打上 时间戳+item_offset 级别的标签（方便对齐）
        for (const auto& tag : tags) {
            if (tag.offset == nitems_read(0) + i && tag.key == pmt::intern("rx_time")) {
                // 关键修复 2：使用 write_string 处理 rx_time (Pair 类型)
                d_active_base_ts = pmt::write_string(tag.value);
            }
        }

        std::string curr_key = d_active_base_ts + "_" + std::to_string((nitems_read(0) + i) % d_items_per_frame);

        if (d_active_base_ts != "None") {
            std::vector<std::complex<float>> current_vec(&in[i * d_fft_len], &in[(i + 1) * d_fft_len]);
            // 发送 y_out 给另一端，key 使用 intern 过的符号以提高效率
            message_port_pub(pmt::intern("y_out"), pmt::cons(pmt::intern(curr_key), pmt::init_c32vector(d_fft_len, current_vec)));
        }

        // 2) 延迟线 (FIFO): 将当前 key 和信号放入一个延迟队列 d_local_fifo，然后取出队列前端（已延迟了 d_delay_syms 个符号）的旧信号 old_item。
        // 这个 old_item 是本地稍早前接收到的信号。
        d_local_fifo.push_back({curr_key, std::vector<std::complex<float>>(&in[i * d_fft_len], &in[(i + 1) * d_fft_len])});
        fifo_item old_item = d_local_fifo.front();
        d_local_fifo.pop_front();

        // 3) MIMO 检测
        std::lock_guard<std::mutex> lock(d_buffer_mutex);
        if (d_remote_y_buffer.count(old_item.key)) {
            const auto& y_remote = d_remote_y_buffer[old_item.key];
            for (int k = 0; k < d_fft_len; k++) {
                std::complex<float> det = d_h11[k] * d_h22[k] - d_h12[k] * d_h21[k];
                std::complex<float> inv_det = std::conj(det) / (std::norm(det) + d_lambda_reg);

                if (d_path_id == 1) {
                    out[i * d_fft_len + k] = (d_h22[k] * old_item.data[k] - d_h12[k] * y_remote[k]) * inv_det;
                } else {
                    out[i * d_fft_len + k] = (-d_h21[k] * y_remote[k] + d_h11[k] * old_item.data[k]) * inv_det;
                }
            }
        } else {
            // 回退模式：若无匹配数据，则进行简单的单通道增益补偿
            for (int k = 0; k < d_fft_len; k++) {
                std::complex<float> h_eff = (d_path_id == 1) ? d_h11[k] : d_h22[k];
                out[i * d_fft_len + k] = old_item.data[k] * (1.0f / (std::norm(h_eff) + 1e-3f)) * std::conj(h_eff) * 0.1f;
            }
        }
    }

    return noutput_items;
}
```

#### 3）2x2 MIMO ZF (或近似 MMSE) 解码流程图

![][p9]

</br>

## 四、编译、安装与运行

### 4.1 启动环境

这里使用 docker，其内部环境为 Ubuntu 24.04 + GNU Radio 3.10.9.2 (Python 3.12.3)：

```
systemctl start  docker
xhost -          
xhost +local:docker 
docker run -it --rm \
    --net=host \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v /dev/bus/usb/:/dev/bus/usb/ \
    -v /home/btfz/Desktop/B210:/home/gnuradio/B210 \
    --privileged \
    --group-add=audio \
    ubuntu:gnuradio-310 bash

zsh
export UHD_IMAGES_DIR=/home/gnuradio/B210/B210_images/  
uhd_find_devices
```

**注意：** 你有相同的环境就行，不强制 docker，但是同时强烈不建议 windows！

</br>

### 4.2 编译安装 OOT

```
git clone git@github.com:btfz-sdr/gr-mimo.git
cd gr-mimo
git checkout v1.0
mkdir build
cd build 
cmake ..
make
sudo make install
sudo ldconfig
```

之后就能在 gnuradio 中看到一个叫 mimo 的分组，里面有几个我们自己创建的 OOT 模块（这里一定要用 C++，python 速度太慢，大概是 1/50 的速度）。

</br>

### 4.3 生成与运行

```
git clone git@github.com:oldprogram/gnuradio_demo.git --recurse-submodules
cd ./gnuradio_demo/B210专项/12-USRP双通道与MIMO技术/06-MIMO_OFDM_B210_空间复用
gnuradio-companion trx_mimo_space_multiplexing.grc
```

之后打开流程图，并点击 `RUN->Generate` 产生可执行 python 文件，然后在命令行中运行可执行 python 文件，然后实验介绍就在《[MIMO 空间复用：基于 B210 的 OFDM 时间复用实战演示][#6]》中：

<iframe src="//player.bilibili.com/player.html?isOutside=true&aid=116469748734204&bvid=BV1tXoZBHEzT&cid=37841929530&p=1" scrolling="no" border="0" frameborder="no" framespacing="0" allowfullscreen="true" width="800" height="450"></iframe>


</br>

<!--https://gemini.google.com/app/99d47a422294df0c?utm_source=app_launcher&utm_medium=owned&utm_campaign=base_all-->     
<!--https://gemini.google.com/app/571d9c111d6d38a6?utm_source=app_launcher&utm_medium=owned&utm_campaign=base_all-->             


[#1]:https://wiki.gnuradio.org/index.php/OFDM_Channel_Estimation     
[#2]:https://github.com/gnuradio/gnuradio/blob/master/gr-digital/python/digital/bindings/ofdm_chanest_vcvc_python.cc     
[#3]:https://github.com/gnuradio/gnuradio/blob/master/gr-digital/lib/ofdm_chanest_vcvc_impl.h
[#4]:https://github.com/gnuradio/gnuradio/blob/master/gr-digital/grc/digital_ofdm_chanest_vcvc.block.yml
[#5]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/54     
[#6]:https://www.bilibili.com/video/BV1tXoZBHEzT/    


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/mimo_spatial_multiplexing.png     
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/mimo_space_diversity.png    
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/mimo_beamforming.png        
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/mimo_how_to_get_H.png      
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_space_multiplexing_2TX.png     
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_space_multiplexing_2RX1.png     
[p7]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_space_multiplexing_zf.png
[p8]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_space_multiplexing_zf2.png     
[p9]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_space_multiplexing_2RX2.png      
[p10]:https://tuchuang.beautifulzzzz.com:3000/?path=202604/mimo_space_multiplexing_prepare.png    

