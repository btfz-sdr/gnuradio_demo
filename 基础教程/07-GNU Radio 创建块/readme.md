### 1、前言

除了直接使用 GNU Radio 自带的 C++/Python 模块，我们还可以使用 **Embedded Python Block** 在 GRC 中用原生 Python 代码快速编写自定义算法模块。

今天通过编写一个根据参数决定“加法（Add）还是乘法（Multiply）”的自定义模块，来看下 Embedded Python Block 的基本开发与调用流程。

</br>

### 2、编写嵌入式 Python 模块代码

在 GRC 中添加 **Embedded Python Block** 并双击打开编辑器，替换代码如下：

```python
import numpy as np
from gnuradio import gr

class blk(gr.sync_block):
    """Embedded Python Block example - a simple add or multiply block"""

    def __init__(self, additionFlag=True):  # 构造函数参数会变成 GRC 的配置项，必须带默认值
        gr.sync_block.__init__(
            self,
            name='Add or Multiply Block',        # 在 GRC 模块上显示的名称
            in_sig=[np.complex64, np.complex64], # 定义 2 个复数输入端口
            out_sig=[np.complex64]              # 定义 1 个复数输出端口
        )
        self.additionFlag = additionFlag

    def work(self, input_items, output_items):
        """核心处理函数：根据标志位决定对两路输入做相加还是相乘"""
        if self.additionFlag:
            output_items[0][:] = input_items[0][:] + input_items[1][:]
        else:
            output_items[0][:] = input_items[0][:] * input_items[1][:]
        return len(output_items[0])

```

**关键点解析**：

1. **继承基类**：继承自 `gr.sync_block`（同步块），表示输入和输出的采样点数量是一一对应的。
2. **定义端口 (`in_sig` / `out_sig`)**：使用 NumPy 类型定义输入/输出端口数量及数据类型，这里定义了 2 入 1 出的 `complex64` 端口。
3. **参数绑定 (`__init__`)**：构造函数的参数（如 `additionFlag=True`）会自动暴露为 GRC 模块的属性参数供用户修改。
4. **数据处理 (`work`)**：通过 `input_items[x]` 拿到输入数据切片，运算后填入 `output_items[0]`。

</br>

### 3、在流程图中使用自定义模块

搭建流程图验证自定义模块逻辑：

- 1）**信号源**：
    * **Signal Source 1**：$1\text{kHz}$ 余弦波（复数类型）。
    * **Signal Source 2**：$3\text{kHz}$ 余弦波（复数类型）。
- 2）**连接方式**：
    * 两路信号源同时接入自定义模块 **`Add or Multiply Block`** 的输入端口。
    * 另将两路信号源接入 **QT GUI Time Sink** 作原始对比。
    * 自定义模块输出接入 **Throttle**，随后接入 **QT GUI Time Sink** 和 **QT GUI Frequency Sink**。
- 3）**参数控制**：
    * 双击自定义模块，修改 **Additionflag** 为 `True`（执行加法，频域将看到 $1\text{kHz}$ 和 $3\text{kHz}$ 两条谱线）或 `False`（执行相乘，混频得到频率差与频率和）。

![][p1]

</br>

### 4、跑起来看效果

运行流程图后：

![][p2]

* 当 `Additionflag` 设为 `False`（相乘）时，在 **QT GUI Frequency Sink** 中可以清晰看到混频后的频点，验证了 Python 代码逻辑在 GRC 数据流中的实时运行。

</br>

### 参考链接

* [GNU Radio Wiki - Embedded Python Block](https://wiki.gnuradio.org/index.php/Embedded_Python_Block)
* [GNU Radio Wiki - Python Block in GRC Guide](https://www.google.com/search?q=https://wiki.gnuradio.org/index.php/Python_Block_in_GRC)    

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_first_python_block_grc31135234.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_first_python_block_grc_show.png      

