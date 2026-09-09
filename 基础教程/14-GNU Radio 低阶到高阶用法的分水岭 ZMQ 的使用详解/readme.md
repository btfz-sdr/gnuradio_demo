### 1、前言

学会使用 GNU Radio 中的 ZMQ，是从低阶使用者向高阶迈进的第一步！

因为学会了 ZMQ，就可以将 GNU Radio 中的实时数据流通过 socket 引到外面的 python/c 等大型应用程序中，做数据分析、展示、人工智能等。

来自 ZeroMQ 官方介绍：ZeroMQ (0MQ, ZMQ)，看起来像是一个可嵌入的网络库，同时起到了并发框架的作用。它为您提供了在进程内、进程间、TCP和多播等各种传输中承载原子消息的 socket 。

</br>

### 2、ZMQ 块的类型

SINK | SOURCE | 特征
--- | --- | ---
PUB | SUB | 广播，可一对多
PUSH | PULL | 点播，点对点对等网络
REQ | REP | 点对点链路，一个请求一个回复，类似客户端服务器

</br>

**Data Blocks：**
ZMQ data blocks 传输原始流数据；没有格式化。数据类型和采样率由馈送 ZMQSink 的流程图确定。因此，接收数据的流程图或程序必须知道这些参数，以便正确地解释数据。

</br>

**Message Blocks**
不像普通的 ZeroMQ 字符串，GNU Radio ZMQ Message Blocks 使用 PMT 对数据进行编码和解码。

</br>

### 3、ZMQ 块的使用

- ZMQ 块的用户应该对ZeroMQ有一些熟悉。特别是，应该认识到ZMQ套接字和BSD套接字之间的区别。有关概述，请参阅 ZMQ Socket API。
- ZMQ 块使用 endpoints 来描述 ZMQ 应该如何传递数据。常见 endpoints 使用 TCP 传输数据，当然也可以采用其他协议。想要了解不同协议的 endpoints，可以参阅 `zmq_tcp` 和 `zmq_ipc`。
- 可以在49152–65535范围内分配专用端口。
- 不建议在单个流程图中使用ZMQ块，因为 `Virtual_Source` 和 `Virtual_Sink` 块的效率要高得多。

</br>

**TCP Bind vs Connect**

一些用户可能会想直接连接到GNU Radio ZMQ Blocks。虽然这是可能的，但需要谨慎。

首先要注意，在任何拓扑中，必须有一个到给定端点的绑定，而可能有多个到同一端点的连接。（A-B 之间绑定一次，可能会出现多个连接）

在 GNU Radio 中，stream sinks bind and stream sources connect。Message blocks 取决于参数设置。

==还要注意==：TCP端点的语义在绑定和连接之间有所不同。


</br>

**TCP Bind**

当绑定一个 TCP 端点时，您可以指定要侦听的连接点。

如果您指定了一个IP地址，则说明 socket 只接受与该地址相关联的网络上的连接（例如：`127.0.0.1` or `192.168.1.123`）

在某些情况下，您可能希望在连接到节点的所有网络上进行侦听。对于 GNU  Radio，您应该使用 `0.0.0.0` 作为通配符地址；尽管 ZMQ 接受 * 作为通配符，但它并不是在所有情况下都能很好地工作。因此，您可以选择绑定到 `tcp://0.0.0.0:54321`

请注意，如果您没有输入IP地址，bind 会将该值视为网络适配器名称（例如 eth0）。详细信息参阅：`zmq_tcp`

</br>

**TCP Connect**

连接 TCP 端点时，您可以指定要连接的远程端点。您可以指定 IP 地址或 DNS 可解析名称。

</br>

**Wire Format**

ZMQ stream blocks 具有传递标记的选项。此外，PUB/SUB块支持过滤。这两个选项都会影响 ZMQ-wire 协议。

当过滤器字符串被提供给 PUB/SUB 块时，GNU Radio 使用多部分消息来发送过滤器字符串，然后是有效载荷。尝试与 GNU Radio ZMQ 块接口的非 GNU 无线电代码必须为此部分准备好，并将其丢弃。请注意，只有在指定了非空筛选器的情况下，发送方才会发送此消息部分。

接下来，如果启用了发送标签，则要发送的数据窗口内的任何标签都将以特殊格式编码，并在有效载荷数据之前进行预处理。如果未启用标记，则会忽略此标头。

这两个特征使得发送器配置与接收器配置的匹配变得至关重要。否则将导致流程图中出现运行时错误。

</br>

### 4、DEMO

#### 4.1 同一台电脑上的两个流程图

在同一个电脑上时，localhost 的 IP 地址应为 `127.0.0.1`。它的开销比完整的IP要小。

下面使用 PUB/SUB 的流程图来自 [Simulation_example:_AM_transmitter_and_receiver：](https://wiki.gnuradio.org/index.php?title=Simulation_example:_AM_transmitter_and_receiver)

![][p1]

==这个流程图我也在 B 站上面有详细的视频介绍：==[GNU Radio 系列教程（十二）－－ 窄带 FM 收发系统（基于ZMQ模拟射频发送）](https://www.bilibili.com/video/BV1ZW4y177AN/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0)

</br>

#### 4.2 不同电脑上的两个流程图

在不同电脑上时，则必须在该连接的每一端指定接收块（sink block) 的 IP 和端口号。例如，如果 Sink 位于 IP `192.168.1.194:50241`，Source 位于 IP `192.168.1.85`，则 Source 和 Sink 块都必须指定 Sink IP 和端口 `192.168.1.194:00241`。

![][p2]

</br>

#### 4.3 作为 REQ/REP 服务器的 Python 程序

下面的 Python 程序在其 REQ socket 上接收字符串消息，将文字变成大写，然后在其 REP socket 上发送出去。术语在这里变得混乱，因为传入的 REQ 来自 [`GR ZMQ_REP_Message_Sink`](https://wiki.gnuradio.org/index.php?title=ZMQ_REP_Message_Sink)，并返回到 [`ZMQ_REQ_Message_Source`](https://wiki.gnuradio.org/index.php?title=ZMQ_REQ_Message_Source)。

==只需记住：== sink 是流程图的终点，source 是流程图的起点。

```Python
#!/usr/bin/python3
# -*- coding: utf-8 -*-

import pmt
import zmq

context = zmq.Context()

# 1. REP 套接字：监听来自 zmq_req.py (REQ 客户端) 的请求
rep_sock = context.socket(zmq.REP)
rep_sock.bind("tcp://127.0.0.1:50247")
print(f"REP listening on tcp://127.0.0.1:50247 (for zmq_req.py)")

# 2. REQ 套接字：连接到 zmq_rep.py (REP 服务器)
req_sock = context.socket(zmq.REQ)
req_sock.connect("tcp://127.0.0.1:50246")
print(f"REQ connected to tcp://127.0.0.1:50246 (zmq_rep.py)")
print("=" * 60)

# 注册 req_sock 以等待 zmq_rep.py 的回复
poller = zmq.Poller()
poller.register(req_sock, zmq.POLLIN)

while True:
    try:
        # 1. 阻塞等待 zmq_req.py 发送请求
        request_data = rep_sock.recv()
        
        # 2. 将请求透传发送给 zmq_rep.py
        req_sock.send(request_data)
        
        # 3. 等待 zmq_rep.py 的响应（带 5 秒超时）
        socks = dict(poller.poll(5000))
        
        if req_sock in socks and socks[req_sock] == zmq.POLLIN:
            resp_bytes = req_sock.recv()
            
            # 解析 zmq_rep.py 返回的数据 (PMT 格式)
            try:
                p_val = pmt.deserialize_str(resp_bytes)
                if pmt.is_symbol(p_val):
                    val_str = pmt.symbol_to_string(p_val)
                elif pmt.is_string(p_val):
                    val_str = pmt.string_to_python_string(p_val)
                else:
                    val_str = str(pmt.to_python(p_val))
                
                # 将 "TEST" 转换为小写 "test"
                converted_str = val_str.lower()
                print(f"[Server] Converted: {val_str} -> {converted_str}")
                
                # 重新打包为 PMT Symbol 并序列化
                reply_bytes = pmt.serialize_str(pmt.intern(converted_str))
            except Exception as e:
                print(f"[Error] Failed to parse/convert PMT: {e}")
                reply_bytes = resp_bytes
            
            # 4. 回复给 zmq_req.py
            rep_sock.send(reply_bytes)
        else:
            print("[WARNING] Timeout waiting for zmq_rep.py")
            # 超时重置 REQ socket 以恢复状态
            req_sock.close()
            req_sock = context.socket(zmq.REQ)
            req_sock.connect("tcp://127.0.0.1:50246")
            poller.register(req_sock, zmq.POLLIN)
            
            err_pmt = pmt.serialize_str(pmt.intern("error"))
            rep_sock.send(err_pmt)

    except KeyboardInterrupt:
        break
    except Exception as e:
        print(f"[Error] {e}")
        import traceback
        traceback.print_exc()

print("Shutting down...")
```

</br>

安装 NetCat：方便我们测试 TCP 
-《[NetCat使用指南](https://www.jianshu.com/p/cb26a0f6c622)》
-《[Sending TCP/UDP packets using Netcat](https://help.ubidots.com/en/articles/937233-sending-tcp-udp-packets-using-netcat)》
-《[Simple client / server with nc not working](https://serverfault.com/questions/960798/simple-client-server-with-nc-not-working)》
==注意==，这鬼软件有好[几个不同的软件](https://wiki.archlinux.org/title/Network_tools)，我用的是 openbsd-netcat



```
sudo pacman -S openbsd-netcat
```
</br>

上面代码：

kind | port | method | func | C/S
---|---|---|---|---
**REQ** | 50246 | connect | recv() | server
**REP** | 50247 | bind | send() | client

</br>

while 循环中用 REQ 等待接收，然后转为大写，用 REP 发送出去:（比较坑的是，我用 netcat 建立 tcp 服务器和客户端，无法与上面 python 脚本通信，似乎一启动，建立连接，server 就异常退出了，最终还是得用 GNN Radio 开启两个 ZMQ 工程，然后与这个 python 脚本通信，整体信息流如下：）

![][p3]

</br>

#### 4.4 作为 PUSH/PULL 服务器的 Python 程序

与上面 demo 类似，是基于 PUSH/PULL 传递消息。

```Python
#!/usr/bin/python3
# -*- coding: utf-8 -*-

# zmq_PUSH_PULL_server.py

import sys
import pmt
import zmq

_debug = 0          # set to zero to turn off diagnostics

# create a PUSH socket
_PROTOCOL = "tcp://"
_SERVER = "127.0.0.1"          # localhost
_PUSH_PORT = ":50252"
_PUSH_ADDR = _PROTOCOL + _SERVER + _PUSH_PORT
if (_debug):
    print ("'zmq_PUSH_PULL_server' version 20068.1 binding to:", _PUSH_ADDR)
push_context = zmq.Context()
if (_debug):
    assert (push_context)
push_sock = push_context.socket (zmq.PUSH)
if (_debug):
    assert (push_sock)
rc = push_sock.bind (_PUSH_ADDR)
if (_debug):
    assert (rc == None)

# create a PULL socket
_PROTOCOL = "tcp://"
_SERVER = "127.0.0.1"          # localhost
_PULL_PORT = ":50251"
_PULL_ADDR = _PROTOCOL + _SERVER + _PULL_PORT
if (_debug):
    print ("'zmq_PUSH_PULL_server' connecting to:", _PULL_ADDR)
pull_context = zmq.Context()
if (_debug):
    assert (pull_context)
pull_sock = pull_context.socket (zmq.PULL)
if (_debug):
    assert (pull_sock)
rc = pull_sock.connect (_PULL_ADDR)
if (_debug):
    assert (rc == None)

while True:
    #  Wait for next request from client
    data = pull_sock.recv()
    message = pmt.to_python(pmt.deserialize_str(data))
    # print("Received request: %s" % message)

    output = message.upper()    # capitalize message

    #  Send reply back to client
    push_sock.send (pmt.serialize_str(pmt.to_pmt(output)))
```

</br>

#### 4.5 处理流程图数据的 Python 程序

==个 demo 是几乎贯穿后面 GNU Radio 高阶用法的最重要的 DEMO。== 因为，通常情况下我们会使用 GNU Radio 进行信号处理，但希望数据流流入普通 python 程序，然后做丰富的数据分析等逻辑。这里，PUB 和 PUSH 可以让应用程序获得这些数据流。(这里我们将 `127.0.0.1` 换成了 `*`，这样能够让同一局域网内的设备都能访问)

一般的，流程图中采用 PUB/PUSH Sink，将数据送出：

![][p4]

然后，普通 python 脚本就可以对其进行 recv：

```Python
#!/usr/bin/python3
# -*- coding: utf-8 -*-

# zmq_SUB_proc.py
# Author: Marc Lichtman

import zmq
import numpy as np
import time
import matplotlib.pyplot as plt

context = zmq.Context()
socket = context.socket(zmq.SUB)
socket.connect("tcp://127.0.0.1:55555") # connect, not bind, the PUB will bind, only 1 can bind
socket.setsockopt(zmq.SUBSCRIBE, b'') # subscribe to topic of all (needed or else it won't work)

while True:
    if socket.poll(10) != 0: # check if there is a message on the socket
        msg = socket.recv() # grab the message
        print(len(msg)) # size of msg
        data = np.frombuffer(msg, dtype=np.complex64, count=-1) # make sure to use correct data type (complex64 or float32); '-1' means read all data in the buffer
        print(data[0:10])
        # plt.plot(np.real(data))
        # plt.plot(np.imag(data))
        # plt.show()
    else:
        time.sleep(0.1) # wait 100ms and try again
```

</br>

### 参考链接

[[1]. GNU Radio 系列教程（一） —— 什么是 GNU Radio][#1]    
[[2]. GNU Radio 系列教程（二） —— 绘制第一个信号分析流程图][#2]    
[[3]. GNU Radio 系列教程（三） —— 变量的使用][#3]     
[[4]. GNU Radio 系列教程（四） —— 比特的打包与解包][#4]    
[[5]. GNU Radio 系列教程（五） —— 流和向量][#5]    
[[6]. GNU Radio 系列教程（六） —— 基于层创建自己的块][#6]    
[[7]. GNU Radio 系列教程（七）—— 创建第一个块][#7]     
[[8]. GNU Radio 系列教程（八）—— 创建能处理向量的 Python 块][#8]    
[[9]. GNU Radio 系列教程（九）—— Python 块的消息传递][#9]     
[[10]. GNU Radio 系列教程（十）—— Python 块的 Tags][#10]     
[[11]. GNU Radio 系列教程（十一）—— 低通滤波器][#11]     
[[12]. GNU Radio 系列教程（十二）—— 窄带 FM 收发系统（基于ZMQ模拟射频发送）][#12]     
[[13]. GNU Radio 系列教程（十三）—— 用两个 HackRF 实现 FM 收发][#13]    
[[14]. SDR 教程实战 —— 利用 GNU Radio + HackRF 做 FM 收音机][#X1]     
[[15]. SDR 教程实战 —— 利用 GNU Radio + HackRF 做蓝牙定频测试工具（超低成本）][#X2]     
</br>


[#1]:https://www.cnblogs.com/zjutlitao/p/16648432.html
[#2]:https://www.cnblogs.com/zjutlitao/p/16655824.html#top
[#3]:https://www.bilibili.com/video/BV1o14y1s7Km/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#4]:https://www.bilibili.com/video/BV1NG4y1z7mt/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#5]:https://www.bilibili.com/video/BV1me411u7jm/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#6]:https://www.bilibili.com/video/BV1814y1e7ZU/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#7]:https://www.bilibili.com/video/BV18V4y1g7i9/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#8]:https://www.bilibili.com/video/BV1MB4y1n7od/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#9]:https://www.bilibili.com/video/BV1DN4y1N7n1/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#10]:https://www.bilibili.com/video/BV1uW4y1v77Y/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#11]:https://www.bilibili.com/video/BV1L14y187iU/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#12]:https://www.bilibili.com/video/BV1ZW4y177AN/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#13]:https://www.bilibili.com/video/BV1TM41177Bj/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#X1]:https://www.bilibili.com/video/BV1eP4y1f7rc/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0
[#X2]:https://www.bilibili.com/video/BV1ft4y1L7Ve/?spm_id_from=333.788&vd_source=84f94348691c2906fc1038d54989b7e0

[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=/b7/c0e8dfd4bd35d1bb81c7a36391a9a4.png
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=/6b/11cb813d94f67b31ac4b830261ed7b.png
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=/7c/466632e883ebdc411d84197666e727.png
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=/a7/bab6b0b9ce12576700587d509f904c.png
[px]:https://tuchuang.beautifulzzzz.com:3000/?path=/7b/24abbb1cf6f0bee204045d1f3bdb34.png

