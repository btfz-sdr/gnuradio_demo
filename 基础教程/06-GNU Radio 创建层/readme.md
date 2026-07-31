### 1、前言

当流程图越来越复杂时，把常用功能封装成“子层/子模块”能大大提升复用性。在 GNU Radio 中，这种自定义模块被称为 **层级块（Hier Block）**。

今天通过创建和调用一个简单的移频器子模块（`Frequency Shifter`），来看下 Hier Block 的完整创建与调用流程。

</br>

### 2、创建子层（Hier Block）

首先编写子层流程图（`Frequency Shifter`）：

- 1）**Options 属性设置**：
    * **Generate Options**: 改为 `Hier Block`（关键点，这代表它是一个可被调用的子模块）。
    * **Category**: 设置在模块树中的分类，如 `[GRC Hier Blocks]`。
- 2）**暴露参数（Parameter）**：
    * 添加两个 **Parameter** 模块，分别命名为 `samp_rate` 和 `frequency`，用于接收父层传进来的采样率和移频频率。
- 3）**暴露接口（Pad Source / Sink）**：
    * **Pad Source (Label: in)**：作为子模块的输入端口。
    * **Pad Sink (Label: out)**：作为子模块的输出端口。
- 4）**内部逻辑**：
    * **Signal Source** 产生正弦信号，与输入的信号通过 **Multiply** 进行相乘，实现频率搬移（Frequency Shift）。

![][p1]

> **注意**：配置完成后，点击工具栏的 **Generate**（或按下 `F5`）生成 Python 代码，这样该模块才会注册进本地的 GRC 模块库中。

</br>

### 3、在主流程图中使用子层

子层生成后，即可在主流程图中直接调用：

1. **搜索并添加自定义模块**：
* 首先点击上面图标栏右侧放大镜图标右边的刷新图标（`reload blocks`）
* 在右侧模块树（或 `Ctrl + F`）中搜索 `Frequency Shifter` 并拖入工作区。
* 在未生成或找不到该模块时，GRC 会暂时显示为红色虚线的 `Missing Block`。



2. **参数传递与连接**：
* 将主流程图的 `samp_rate` 和 `frequency`（通过 `QT GUI Range` 控制）传递给该子模块。
* **数据流路径**：`Noise Source` $\rightarrow$ `Low Pass Filter` $\rightarrow$ **`FrequencyShifter (子层)`** $\rightarrow$ `Throttle` $\rightarrow$ `QT GUI Frequency Sink`。

![][p3]

</br>

### 4、跑起来看效果

生成并运行主流程图：

* 拖动主界面的 `frequency` 滑块，可以观察到经过低通滤波后的高斯白噪声频谱在频域上随滑动条实时左右平移，验证了自定义 `Frequency Shifter` 子模块的移频功能。

![][p4] 

</br>

### 参考链接

* [GNU Radio Wiki - Creating Hier Blocks](https://www.google.com/search?q=https://wiki.gnuradio.org/index.php/Creating_Hier_Blocks)



[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_hier_grc1.png         
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_hier_grc2.png       
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_hier_grc3.png    
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_hier_grc3_show.png

