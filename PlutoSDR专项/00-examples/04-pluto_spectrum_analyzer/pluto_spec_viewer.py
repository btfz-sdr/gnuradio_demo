#!/usr/bin/env python
# coding=utf-8
import socket
import numpy as np
import matplotlib.pyplot as plt
import sys
import argparse  # ---- 引入参数解析模块 ----

# ---- 命令行参数解析逻辑 ----
# python pluto_spec_viewer.py -s 10 -i "192.168.1.119"
parser = argparse.ArgumentParser(
    description="PlutoSDR Real-Time Spectrum Viewer (Edge-FFT Client)",
    formatter_class=argparse.RawDescriptionHelpFormatter
)
parser.add_argument('-i', '--ip', type=str, default="192.168.1.119",
                    help="接收端 PC 的真实 IP 地址 (默认: 192.168.1.119)")
parser.add_argument('-s', '--span', type=float, default=4.0,
                    help="频谱总带宽/扫宽 Span，单位 MHz (默认: 4.0)")
parser.add_argument('-p', '--port', type=int, default=1234,
                    help="UDP 监听端口 (默认: 1234)")

args = parser.parse_args()

# ---- 将解析后的参数赋值给配置变量 ----
UDP_IP = args.ip
UDP_PORT = args.port
SPAN_MHZ = args.span

# ---- 初始化 UDP Socket ----
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
    sock.bind((UDP_IP, UDP_PORT))
except OSError as e:
    print(f"[ERROR] 绑定 {UDP_IP}:{UDP_PORT} 失败: {e}")
    print("[TIPS] 请检查 IP 是否为主机当前真实 IP，或端口是否被占用。")
    sys.exit(1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)

# ---- 初始化 Matplotlib 绘图 ----
plt.ion()  

# 禁止刷新时把窗口置顶并抢占焦点
plt.rcParams['figure.raise_window'] = False

fig, ax = plt.subplots(figsize=(10, 6))

# ---- 利用传入的 span 动态计算 X 轴坐标 ----
# 例如传入 10，则生成 -5.0 到 5.0 MHz；传入 4，则生成 -2.0 到 2.0 MHz
half_span = SPAN_MHZ / 2.0
x = np.linspace(-half_span, half_span, 1024)  
line, = ax.plot(x, np.zeros(1024), color='#ff4757', lw=1.5)

# 视窗配置
ax.set_xlim(-half_span, half_span) # 显式同步锁定 X 轴边界
ax.set_ylim(0, 70)
ax.grid(True, linestyle='--', alpha=0.5, color='#ccc')
ax.set_xlabel("Bandwidth (MHz)", fontsize=12)
ax.set_ylabel("Power (dB)", fontsize=12)

# 在标题中动态展示当前的配置参数
ax.set_title(f"PlutoSDR Real-Time Spectrum (Edge-FFT)\nIP: {UDP_IP} | Span: {SPAN_MHZ} MHz", 
             fontsize=13, fontweight='bold')

running = True
def on_close(event):
    global running
    print("\n[INFO] 检测到绘图窗口关闭，正在安全退出...")
    running = False
    sock.close()  
    sys.exit(0)

fig.canvas.mpl_connect('close_event', on_close)

print(f"正在精准监听 {UDP_IP}:{UDP_PORT} 上的 PlutoSDR 频谱流 (配置带宽: {SPAN_MHZ} MHz)...")

try:
    while running:
        try:
            data, addr = sock.recvfrom(4096)
        except OSError:
            break
            
        if len(data) == 4096:
            spectrum = np.frombuffer(data, dtype=np.float32)
            
            # 更新折线数据
            line.set_ydata(spectrum)
            
            # 三管齐下，强行激活不同系统下的显卡图层刷新
            fig.canvas.draw_idle()     # 1. 告诉 GUI 换缓冲区（空闲时重绘）
            fig.canvas.flush_events()  # 2. 强行冲刷当前的 GUI 事件队列
            plt.pause(0.001)           # 3. 让出 CPU 控制权给 Matplotlib 引擎

except KeyboardInterrupt:
    print("\n[INFO] 用户终止程序。")
finally:
    print("[INFO] 释放网络资源。")

# sudo tcpdump -i any udp port 1234 -XX
