## 前言

学习这个流程图时建议先将**其前置的三个课程学会**！！！

![][p1]

- [OFDM 正交频分复用收发 DEMO 演示][#1]
- [OFDM TX 详解][#2]    
- [OFDM RX 详解][#3]     

</br>

同时，最好也将《[从 Bit 到 App：GNURadio 极简跨层传输框架（告别导师的“微信功能”焦虑）][#4]》学会。

</br>

## 流程图介绍

我们经过 TX/RX 详解，已经得到了一个**巨大的 OFDM 收发流程图**：

![][p8]

同时，我们在 TX/RX 详解两个教程中也介绍过： 其中 OFDM Receiver 就是我们整个接收流程图的合并版，同时发送流程图也有对应的合并版本，因此我们能将上述流程图变为极简版本。

此外，我们再加上《[从 Bit 到 App：GNURadio 极简跨层传输框架（告别导师的“微信功能”焦虑）][#4]》中的文件传输框架流程图，最终得到这样一个具备文件和消息收发的 OFDM 流程图：

![][p2]

</br>

## 效果演示

支持传输消息和文件，传输文件时能显示百分比（建议文件大小 100K 以内，建议根据自己电脑性能，适当调高或调低采样率）：

![][p3]


</br>

[#1]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/47     
[#2]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/48
[#3]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/88
[#4]:https://beautifulzzzz.com/gnuradio/tutorial/lesson/87     

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/ofdm_series_show.png    
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/ofdm_trx_sch.png      
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/ofdm_trx_sch_show.png     


[p8]:https://tuchuang.beautifulzzzz.com:3000/?path=202603/tx_ofdm.png       


