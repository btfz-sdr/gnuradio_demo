### 一、基础知识

相位调制（Phase Modulation, PM）是指利用调制信号的振幅变化来直接控制载波相位的调制方式。在复数基带表达中，调相信号的输出表达式为：

$$\text{output} = e^{j \cdot \text{sensitivity} \cdot m(t)} = \cos\big(m(t) \cdot \text{sensitivity}\big) + j \cdot \sin\big(m(t) \cdot \text{sensitivity}\big)$$

* **调制波形**：$m(t)$，假设输入信号在 $[-1, 1]$ 范围内变化。
* **灵敏度参数（Sensitivity）**：决定了输入幅度峰值时产生的相位偏移量（单位：弧度 rad）。
* **IQ 变化特点**：由于输出信号为模长等于 1 的复数 $\text{complex}(\cos\theta, \sin\theta)$，其在 IQ 平面（星座图）中的轨迹始终保持在单位圆上，弧度的摆动范围由 $\pm (m(t)_{\text{max}} \cdot \text{sensitivity})$ 决定。

</br>

### 二、流程图详解

根据调相公式与 GNU Radio 模块的功能特性，绘制如下对比流程图：

![][p1] 

流程图包含两套并行的相位调制实现路径，用于对比验证：

1. **自定义逻辑实现链路（上路：PM-SELF）**：
* **Signal Source**：产生幅度为 $1$、频率为 $1\text{ kHz}$ 的正弦/余弦信号 $m(t)$，取值范围范围为 $[-1, 1]$。
* **Multiply Const**：将输入信号乘以调制灵敏度 $\text{sens}$（其中 $\text{sens} = \frac{\pi \cdot n}{8}$）。
* **Transcendental**：分别计算 $\cos(m(t) \cdot \text{sens})$（实部 $I$）与 $\sin(m(t) \cdot \text{sens})$（虚部 $Q$）。
* **Float To Complex**：将 $I$、$Q$ 两路浮点信号合成为复数 IQ 信号 $\text{complex}(\text{cos}, \text{sin})$。
* **Throttle**：限速块，控制采样率为 $48\text{ kHz}$。
* **QT GUI Sink (PM-SELF)**：显示自研调相逻辑的 IQ 星座图及频谱。


2. **官方模块对比链路（下路：PM-BLOCK）**：
* 直接将信号送入 GNU Radio 官方封装的 **Phase Mod** 模块（`Sensitivity` 设置为 $3.14159$）。
* 输出至 **QT GUI Sink (PM-BLOCK)** 进行对比显示。



**关键参数与变量：**

* `samp_rate` = $48\text{ kHz}$
* `sens` = $3.14159$
* `n` = QT GUI Range 动态调节范围（$1 \sim 8$，步长 $2$，默认值为 $8$）

</br>

### 三、实验观察

通过调节 `QT GUI Range` 中的变量 $n$，可以在 IQ 星座图中实时观察到相位偏移弧度的变化：

* **当 $n = 8$ 时**（$\text{sens} = \pi$）：
* 输入幅度范畴为 $[-1, 1]$，产生的相位偏移范围为 $[-\pi, \pi]$。
* **星座图观察**：IQ 轨迹在单位圆上正好旋转一周，呈现为一个**完整的圆**。


* **当 $n = 4$ 时**（$\text{sens} = \pi/2$）：
* 产生的相位偏移范围为 $[-\pi/2, \pi/2]$。
* **星座图观察**：IQ 轨迹覆盖单位圆的**右半圆**（角度摆幅共 $180^\circ$）。


* **当 $n = 2$ 时**（$\text{sens} = \pi/4$）：
* 产生的相位偏移范围为 $[-\pi/4, \pi/4]$。
* **星座图观察**：IQ 轨迹仅在**右侧 $1/4$ 圆弧**之间摆动（角度摆幅共 $90^\circ$）。



**对比结论**：
上路使用三角函数与复数合成（`Transcendental` + `Float To Complex`）的输出结果，与下路官方 `Phase Mod` 模块的显示完全一致，验证了调相模块在基带处理中的数学本质。



[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202609/pm_grc.png


