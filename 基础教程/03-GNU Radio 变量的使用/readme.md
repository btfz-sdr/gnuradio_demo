### 1、前言

玩转 GNU Radio，变量（Variables）是必不可少的。今天用 `base.grc` 来看下如何在 GRC 里搞定变量调参（滑动条、下拉框）。

</br>

### 2、三种变量玩法（Variable）

`base.grc` 演示了 3 种控制频率 `frequency` 的方式（同一个 ID 同一时间只能 Enable 一个）：

1. **静态变量（Variable）**：固定死一个值（比如 `4000`）。
2. **滑动条（QT GUI Range）**：运行时用拉条调频，范围 `-samp_rate/2` 到 `samp_rate/2`，步长 `100`。
3. **下拉框（QT GUI Chooser）**：做成选择题，只能选预设好的几个频点（如 `0`、`1000`、`-2000`）。

![][p1]


</br>

### 3、跑起来看效果

通过使能 ID=frequency 的三个模块中的一个，来分别体验三种变量的实际效果：

![][p2]    

</br>

### 参考链接

* [GNU Radio Wiki - Variables in Flowgraphs](https://wiki.gnuradio.org/index.php/Variables_in_Flowgraphs)





[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_variable.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202607/grc_tui_variable_show.png

