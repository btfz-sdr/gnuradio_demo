### 引言

在无线电的世界里，1 + 1 并不总是等于 2。如果你能控制两束电磁波的**相位**，你就能在空间中制造“静默区”，甚至能像灯塔一样，利用“消失的信号”为无人机或终端进行精准导航。

本文将基于 **Ettus USRP B210** 和 **GNU Radio**，展示如何从底层的相干性实验演进到实用的 **AoD（Angle of Departure，离去角）** 测量系统。

</br>

### 一、通过实验了解核心概念：相干性（Coherence）

一切实验的基础在于两路信号的**空间相干性**。
使用 B210 的双通道发射功能，两路 RF 链共享同一个本振（LO）。当我们在 GNU Radio 中手动引入  相位偏移时，接收端会观察到信号功率骤降——这就是**相消干涉**。

![][p3]

**关键结论**：如果我们在发射端补偿  才能实现抵消，说明信号在到达接收天线时原本是“同相”的。

</br>

#### 1.1 流程图介绍
 
上面实验使用的流程图比较简单：采用 B210 双通道进行发送，使用 hackrf 单通道进行接收。在发送端可以通过调节第二路信号相对于第一路信号的相位差，来在接收端观察由于相干导致对信号的影响。

**注意：** 设备初始摆放最好让发送的两个天线与接收天线距离相等。

![][p1]

</br>

#### 1.2 在线模拟相干小工具

用下面的小工具可以在线模拟无线电相干干涉：

<div style="width: 800px; height: 550px; overflow: hidden; border: 1px solid #ddd; border-radius: 8px; position: relative; background: #000;">
    <iframe 
      src="https://tuchuang.beautifulzzzz.com:3000/?path=radio_simulation_html_tools/simulation/radio_coherent_interferometry_simulation.html" 
        style="
            width: 1067px; 
            height: 733px; 
            border: none;
            position: absolute;
            top: 0;
            left: 0;
            -webkit-transform: scale(0.75);
            transform-origin: 0 0;
            -webkit-transform-origin: 0 0;
        "
        scrolling="no">
    </iframe>
</div>

</br>

### 二、进阶：AoD 扫描定位原理

既然手动调节相位可以移动“静默点”，如果我们让相位从 $0^\circ \to 360^\circ$ 快速循环旋转，空间中就会出现一个周期性扫描的“虚幻波束”。

可以通过下面的小工具体验：

<div style="width: 800px; height: 600px; overflow: hidden; border: 1px solid #ddd; border-radius: 8px; position: relative; background: #000;">
    <iframe 
      src="https://tuchuang.beautifulzzzz.com:3000/?path=radio_simulation_html_tools/simulation/AoD_scanning_and_direction_finding_simulation.html" 
        style="
            width: 1333px; 
            height: 1000px; 
            border: none;
            position: absolute;
            top: 0;
            left: 0;
            -webkit-transform: scale(0.60);
            transform-origin: 0 0;
            -webkit-transform-origin: 0 0;
        "
        scrolling="no">
    </iframe>
</div>

#### 2.1 信号建模与同步

为了让接收机（HackRF）知道扫描的起点，我们设计了一个**带同步脉冲的扫描协议**：

* **TX1 (参考路)**：发送基准正弦波，但在每圈扫描开始时，幅度瞬间加倍。
* **TX2 (扫描路)**：发送相位不断旋转的正弦波。

依托上述原理的发送流程图为：

![][p5]

</br>

#### 2.2 接收端的“时间-空间”转换

接收机观察到的信号功率包络包含两个特征点：

1. **同步峰值（Sync Peak）**：代表扫描周期的 $t_0$ 时刻。
2. **干涉谷底（Null Point）**：代表扫描波束正好“扫过”接收机的时刻。

**AoD 计算公式**：

$$\theta_{AoD} = \frac{T_{null} - T_{sync}}{T_{period}} \times 360^\circ$$

</br>

#### 2.3 算法优化：基于差分检测的精准寻峰

在实际环境中，正弦包络非常平缓，直接寻找最大值容易受噪声干扰。我们引入了**滑动有限差分算法**：
`diff_buf = buf_np[delay:] - buf_np[:-delay]`

通过计算信号在 1000 个采样点内的增量，我们能将平缓的背景完全滤除，只留下陡峭的同步脉冲上升沿。

Python Block 核心逻辑实现：

```python
# 核心差分定位逻辑
diff_buf = buf_np[self.diff_delay:] - buf_np[:-self.diff_delay]
max_diff_rel_idx = np.argmax(diff_buf) # 锁定同步脉冲起始
min_rel_idx = np.argmin(buf_np)        # 锁定干涉静默点

# 计算相对偏移并映射至 360 度
idx_diff = min_rel_idx - max_diff_rel_idx
self.aod = (idx_diff / self.scan_period) * 360 % 360
```

最终接收流程图为：

![][p4]

</br>

#### 2.4 实验结果与分析

通过 GNU Radio 实时处理，我们可以看到：**当 B210 从中间向左平移时，在 mag² 图中能够明显看出其由于周期性相干导致的波浪形的接收信号的包络（avg 信号），以及做差分检测形成的同步峰值（diff 信号）。通过计算同步峰值与干涉谷底（波浪的底部）之间的距离，来计算 AoD**：

![][p6]

此外，我们也能深刻理解到这个算法的🐮B 之处：

* **稳定性**：由于采用了全周期扫描寻峰，系统对短时的多径衰落不敏感。
* **动态响应**：当移动接收机位置时，`idx_diff` 随之线性变化，在控制台可以实时打印出当前的 AoD 角度。

---

**工程避坑指南**

1. **增益控制（AGC）**：做功率包络实验时，务必**关闭接收机的 AGC**。否则，当信号进入静默点时，AGC 会疯狂抬高增益，导致你观察不到真实的波谷（实际测试发现有 AGC 好像会更有利于肉眼可见效果）。
2. **硬件饱和**：同步脉冲幅度不要设得太高。如果发生 Clipping（削峰），差分算法会因找不到斜率而失效。
3. **多径效应**：在室内演示时，墙壁反射会使“静默点”变浅。建议在开阔地带测试以获得最佳的  以上的抑径比。

---

**结语**

AoD 扫描技术是 5G 毫米波波束赋形和定位协议的简化原型。通过 GNU Radio 的 Python 编程能力，我们可以将深奥的麦克斯韦方程组简化为几行数组运算。这不仅是学术上的演示，更是对软件定义无线电（SDR）极致灵活性的最佳实践。



[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202512/coherence_tx_rx_grc.png    
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202512/coherence_tx_rx_grc_result.png    
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202512/coherence_test_mv.gif      
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202512/aod_rx_grc.png     
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202512/aod_tx_grc.png     
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202512/aod_test_mv.gif     


