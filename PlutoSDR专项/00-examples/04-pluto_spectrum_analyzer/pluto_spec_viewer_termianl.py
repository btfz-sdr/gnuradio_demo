#!/usr/bin/env python
# coding=utf-8
import socket
import numpy as np
import sys
import argparse
import curses
import locale
import select
import time

# ---- 确保 curses 能够正确渲染 UTF-8 字符 ----
locale.setlocale(locale.LC_ALL, '')

# ---- 命令行参数解析逻辑 ----
parser = argparse.ArgumentParser(
    description="PlutoSDR Real-Time Spectrum Viewer (Industrial Curses Client)",
    formatter_class=argparse.RawDescriptionHelpFormatter
)
parser.add_argument('-i', '--ip', type=str, default="192.168.1.119",
                    help="接收端 PC 的真实 IP 地址 (默认: 192.168.1.119)")
parser.add_argument('-s', '--span', type=float, default=10.0,
                    help="频谱总带宽/扫宽/采样率 Span，单位 MHz (默认: 10.0)")
parser.add_argument('-p', '--port', type=int, default=1234,
                    help="UDP 监听端口 (默认: 1234)")
parser.add_argument('--shift', action='store_true', default=False,
                    help="是否在客户端进行 fftshift 变换 (默认: False)")
parser.add_argument('--alpha', type=float, default=0.4,
                    help="IIR 滤波平滑系数 (0.1 ~ 1.0，值越小越平滑，默认: 0.4)")

args = parser.parse_args()

UDP_IP = args.ip
UDP_PORT = args.port
SPAN_MHZ = args.span
NEED_FFTSHIFT = args.shift
SMOOTH_ALPHA = max(0.01, min(1.0, args.alpha))  # 限制在 0 ~ 1 之间

# ---- 初始化 UDP Socket ----
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
    sock.bind((UDP_IP, UDP_PORT))
except OSError as e:
    print(f"[ERROR] 绑定 {UDP_IP}:{UDP_PORT} 失败: {e}")
    sys.exit(1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
sock.setblocking(False)

# ---- Unicode 渐变高度字符 ----
BLOCK_CHARS = [" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"]

def curses_main(stdscr):
    curses.curs_set(0)
    stdscr.nodelay(True)
    
    # 初始化终端色彩
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_CYAN, -1)   # 顶部状态
    curses.init_pair(2, curses.COLOR_GREEN, -1)  # 实时频谱（绿色）
    curses.init_pair(3, curses.COLOR_RED, -1)    # 报警/峰值保持线（红色）

    # 算法缓冲区
    smooth_spectrum = np.zeros(1024)
    max_hold_spectrum = np.zeros(1024)
    
    packet_count = 0
    last_error_msg = "None"
    
    # 帧率控制
    last_render_time = 0
    TARGET_FPS = 30  # 限制终端刷新率，大幅降低 GPU/CPU 绘图负载
    frame_interval = 1.0 / TARGET_FPS

    while True:
        ch = stdscr.getch()
        if ch == ord('q') or ch == ord('Q'):
            break
        elif ch == ord('r') or ch == ord('R'):
            # 按 R 键重置最大值保持线
            max_hold_spectrum.fill(0)

        # 1. ---- 尽可能快地排空 UDP 缓冲区（防止丢包堆积导致时延） ----
        has_new_data = False
        raw_data = None
        
        while True:
            ready = select.select([sock], [], [], 0)
            if ready[0]:
                try:
                    data, addr = sock.recvfrom(4096)
                    if len(data) == 4096:
                        raw_data = np.frombuffer(data, dtype=np.float32)
                        packet_count += 1
                        has_new_data = True
                except BlockingIOError:
                    break
                except Exception as e:
                    last_error_msg = str(e)
                    break
            else:
                break

        # 2. ---- 信号 DSP 平滑处理 ----
        if has_new_data and raw_data is not None:
            if NEED_FFTSHIFT:
                raw_data = np.fft.fftshift(raw_data)
                
            # 一阶 IIR 滤波平滑
            smooth_spectrum = SMOOTH_ALPHA * raw_data + (1.0 - SMOOTH_ALPHA) * smooth_spectrum
            
            # 更新最大值保持线 (Max Hold)
            max_hold_spectrum = np.maximum(max_hold_spectrum, raw_data)
        else:
            # 如果没有新包，峰值线缓慢衰减（衰减率 0.1 dB/帧）
            max_hold_spectrum = np.maximum(0, max_hold_spectrum - 0.1)
            # 释放 CPU 时间片，防止空转死循环把单核跑满
            time.sleep(0.001)

        # 3. ---- 限制终端渲染帧率（主绘图逻辑） ----
        current_time = time.time()
        if current_time - last_render_time < frame_interval:
            continue
        last_render_time = current_time

        # 获取窗口大小
        lines, columns = stdscr.getmaxyx()
        y_axis_width = 8   # 轴宽度
        plot_width = columns - y_axis_width - 2
        plot_height = lines - 11  # 给底部的操作提示多留一行
        
        if plot_width < 20 or plot_height < 4:
            stdscr.erase()
            stdscr.addstr(0, 0, "终端窗口太小，请拉大窗口！", curses.color_pair(3))
            stdscr.refresh()
            continue

        stdscr.erase()

        # 4. ---- 顶部状态面板 ----
        stdscr.addstr(0, 0, "=== PlutoSDR Real-Time Spectrum (Custom DSP Engine) ===", curses.color_pair(1) | curses.A_BOLD)
        stdscr.addstr(1, 0, f"IP: {UDP_IP}  |  Port: {UDP_PORT}  |  Span: {SPAN_MHZ} MHz  |  Alpha: {SMOOTH_ALPHA:.2f}")
        
        flow_color = curses.color_pair(2) if packet_count > 0 else curses.color_pair(3)
        flow_status = "ACTIVE" if has_new_data else "WAITING"
        stdscr.addstr(2, 0, f"Packets: {packet_count} [{flow_status}]", flow_color)
        stdscr.addstr(2, 40, f"Last Err: {last_error_msg}", curses.color_pair(3) if last_error_msg != "None" else curses.A_NORMAL)
        stdscr.addstr(3, 0, "操作提示: 按 [q] 退出 | 按 [r] 重置 Max-Hold 峰值线", curses.color_pair(1))
        stdscr.addstr(4, 0, "-" * (columns - 1))

        start_row = 5

        # 5. ---- 插值重采样 ----
        xp = np.linspace(0, 1023, plot_width)
        fp = np.arange(1024)
        disp_smooth = np.interp(xp, fp, smooth_spectrum)
        disp_max_hold = np.interp(xp, fp, max_hold_spectrum)

        # 6. ---- 逐行渲染 Y 轴、波形与峰值线 ----
        db_min, db_max = 0.0, 70.0
        
        for r in range(plot_height):
            curr_row = start_row + r
            curr_db = db_max - (r / (plot_height - 1)) * (db_max - db_min)
            
            # 打印 Y 轴刻度
            if r % 3 == 0 or r == plot_height - 1:
                y_label = f"{curr_db:5.1f} |"
            else:
                y_label = "      |"
            stdscr.addstr(curr_row, 0, y_label)

            # 打印列像素
            cell_min_db = curr_db - (db_max - db_min) / (plot_height - 1)
            for c in range(plot_width):
                val = disp_smooth[c]
                max_val = disp_max_hold[c]
                
                # A. 渲染 Max-Hold 峰值线（在对应的分贝行画一个单独的红色横线 '-'）
                if max_val >= cell_min_db and max_val < curr_db:
                    stdscr.addch(curr_row, y_axis_width + c, "-", curses.color_pair(3))
                    continue

                # B. 渲染实时绿色频谱波形
                if val >= curr_db:
                    char = BLOCK_CHARS[8]
                elif val < cell_min_db:
                    char = BLOCK_CHARS[0]
                else:
                    ratio = (val - cell_min_db) / (curr_db - cell_min_db)
                    idx = int(ratio * 8)
                    char = BLOCK_CHARS[max(0, min(8, idx))]
                
                if char != " ":
                    stdscr.addch(curr_row, y_axis_width + c, char, curses.color_pair(2))

        # 7. ---- 渲染 X 轴刻度标尺 ----
        axis_row = start_row + plot_height
        stdscr.addstr(axis_row, 0, " " * (y_axis_width - 1) + "+")
        
        tick_indices = [int(i * (plot_width - 1) / 10) for i in range(11)]
        for c in range(plot_width):
            if c in tick_indices:
                stdscr.addch(axis_row, y_axis_width + c, "+")
            else:
                stdscr.addch(axis_row, y_axis_width + c, "-")

        # 8. ---- 渲染 X 轴数字标签 ----
        label_row = axis_row + 1
        half_span = SPAN_MHZ / 2.0
        x_ticks = np.linspace(-half_span, half_span, 11)

        for i, val in enumerate(x_ticks):
            col_pos = y_axis_width + tick_indices[i]
            label_str = f"{val:.2f}"
            draw_col = col_pos - len(label_str) // 2
            if draw_col + len(label_str) < columns:
                stdscr.addstr(label_row, draw_col, label_str)

        stdscr.refresh()


try:
    curses.wrapper(curses_main)
except KeyboardInterrupt:
    pass
finally:
    print("[INFO] 释放网络资源并退出。")
    sock.close()
