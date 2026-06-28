#!/usr/bin/env python
# coding=utf-8
"""
ADS-B 动态航迹追踪查看器
解析 PlutoSDR 文本，聚合飞机状态，并在终端绘制动态看板（含彩色同步雷达图）
"""
import argparse
import socket
import signal
import os
import time
import math  # 用于经纬度解算的三角函数运算
import urllib.request  # 用于无感获取本地公网基准地理位置
import json  # 解析地理位置 API 返回的 JSON 数据
from datetime import datetime
import threading  # 用于异步获取航班信息
import flight_info  # 航班信息查询模块

# 存储当前空域所有飞机的字典 {icao: {航班号, 高度, 速度, 垂直速率, 航向, 最后更新时间}}
aircraft_database = {}

running = True

# ---- 全局本地基准坐标（默认杭州，启动时通过 API 动态覆盖） ----
BASE_LAT = 30.25000
BASE_LON = 120.16000

# ---- 【新增强调】：ANSI 终端着色配置控制序列 ----
COLOR_RESET = "\033[0m"
COLOR_BOLD  = "\033[1m"
COLOR_CENTER = "\033[1;31m"  # 雷达站中心圆心用高亮红

# 颜色循环调色板（剔除黑色和深灰色，保证终端背底下的高可见度）
COLOR_PALETTE = [
    "\033[32m",    # 绿色
    "\033[33m",    # 黄色
    "\033[34m",    # 蓝色
    "\033[35m",    # 品红
    "\033[36m",    # 青色
    "\033[92m",    # 高亮绿
    "\033[93m",    # 高亮黄
    "\033[94m",    # 高亮蓝
    "\033[95m",    # 高亮品红
    "\033[96m",    # 高亮青
]

def get_base_location():
    """ 启动时单次调用开放地理 API 动态锁死接收机基准经纬度 """
    global BASE_LAT, BASE_LON
    try:
        # 使用高可用且免 Key 的 ip-api 获取当前公网位置
        with urllib.request.urlopen("http://ip-api.com/json/?fields=status,lat,lon", timeout=3.0) as response:
            res_data = json.loads(response.read().decode())
            if res_data.get("status") == "success":
                BASE_LAT = float(res_data.get("lat"))
                BASE_LON = float(res_data.get("lon"))
    except Exception:
        # 如果网络断开或 API 超时，静默降级为默认的杭州坐标，不破坏程序启动流程
        pass

def signal_handler(sig, frame):
    global running
    running = False

# ---- 【微调修改】：更换为 100% 免疫横跳的 Local CPR 局部解算工具函数 ----
def decode_local_cpr(ref_lat, ref_lon, cpr_lat, cpr_lon, is_odd):
    """ 带有距离最小化校验锁的 Local CPR 解算器 """
    Nb = 524288.0 if cpr_lat > 131072 or cpr_lon > 131072 else 131072.0

    def calc_by_mode(odd_mode):
        d_lat = 360.0 / 59.0 if odd_mode else 360.0 / 60.0
        j = math.floor(ref_lat / d_lat) + math.floor(0.5 + ((ref_lat % d_lat) / d_lat) - (cpr_lat / Nb))
        lat = d_lat * (j + (cpr_lat / Nb))

        def get_nl(lat_val):
            if abs(lat_val) >= 87.0: return 1
            try:
                tmp = 1 - math.cos(math.pi / 15.0)
                num = tmp / (math.cos(math.pi / 180.0 * lat_val) ** 2)
                cos_nl = 1 - num
                if cos_nl < -1 or cos_nl > 1: return 1
                return max(1, min(59, int(2 * math.pi / math.acos(cos_nl))))
            except: return 1

        nl = get_nl(lat) - (1 if odd_mode else 0)
        d_lon = 360.0 / nl if nl > 0 else 360.0
        m = math.floor(ref_lon / d_lon) + math.floor(0.5 + ((ref_lon % d_lon) / d_lon) - (cpr_lon / Nb))
        lon = d_lon * (m + (cpr_lon / Nb))
        if lon >= 180: lon -= 360
        return lat, lon

    # 【核心锁】：强行用 Even 和 Odd 各算一次
    lat1, lon1 = calc_by_mode(True)
    lat2, lon2 = calc_by_mode(False)

    # 计算两个解距离杭州参考点的欧氏距离
    dist1 = (lat1 - ref_lat)**2 + (lon1 - ref_lon)**2
    dist2 = (lat2 - ref_lat)**2 + (lon2 - ref_lon)**2

    # 谁离杭州近，就用谁的解！彻底免疫奇偶帧跳格干扰
    final_lat, final_lon = (lat1, lon1) if dist1 < dist2 else (lat2, lon2)
    return round(final_lat, 5), round(final_lon, 5)

def parse_pluto_line(line):
    """
    解析 Pluto 发来的自定义文本格式
    例如: DF=17 ICAO=780C01 HDG=3° SPD=340G VR=64fpm | 8D780C01...
    或者: DF=17 ICAO=780C01 CALL=CES5339 | 8D780C01...
    """
    try:
        if "|" not in line:
            return None
        payload = line.split("|")[0].strip()
        parts = payload.split()
        
        data = {}
        for part in parts:
            if "=" in part:
                k, v = part.split("=", 1)
                data[k] = v
        return data
    except Exception:
        return None

# 用于给不同飞机动态分配颜色索引的计数器
color_assignment_counter = 0

def update_database(data):
    """ 更新飞机数据库，融合不同报文的字段 """
    global color_assignment_counter
    if not data or "ICAO" not in data:
        return
    
    icao = data["ICAO"]
    now = time.time()
    
    # 强行过滤由于无线电高强度噪声错位产生的异常数据包，维护系统稳定性
    if "CALL" in data and (len(data["CALL"]) > 8 or data["CALL"].strip() == ""):
        return

    # 如果是新飞机，初始化基础结构并动态绑定唯一的专属颜色
    if icao not in aircraft_database:
        assigned_color = COLOR_PALETTE[color_assignment_counter % len(COLOR_PALETTE)]
        color_assignment_counter += 1
        
        aircraft_database[icao] = {
            "CALL": "-------",
            "ALT": "-----",
            "SPD": "-----",
            "HDG": "---",
            "VR": "-----",
            "LAT": "-------",  # 【微调添加】：初始化经纬度空状态
            "LON": "-------",
            "EVEN": None,      # 【微调添加】：缓存 Even 位置帧
            "ODD": None,       # 【微调添加】：缓存 Odd 位置帧
            "COUNT": 0,
            "FIRST_SEEN": datetime.now().strftime("%H:%M:%S"),
            "FLIGHT_INFO": None,      # 【微调添加】：航班信息
            "FLIGHT_INFO_LOADING": False, # 【微调添加】：加载状态
            "LAST_SEEN": now,
            "COLOR": assigned_color  # 【微调添加】：绑定静态颜色标记
        }
    
    # 融合新字段
    info = aircraft_database[icao]
    
    # 【缺陷修正三】：由系统算法脑补出来的推演帧不应该去延长飞机的整体失联下线倒计时
    # 只有收到完全合法的真实硬件底层报文时，才更新 LAST_SEEN 寿命锚点
    if any(k in data for k in ["ALT", "SPD", "HDG", "VR", "CALL", "LATBIN"]):
        info["LAST_SEEN"] = now
        
    info["COUNT"] += 1
    
    # 【缺陷修正二】：航班号和信息强锁历史状态，防止被未知 Type 产生的空字段洗掉
    if "CALL" in data and data["CALL"].strip() not in ["", "-------"]:
        old_call = info.get("CALL", "-------")
        info["CALL"] = data["CALL"]
        # 航班号变化时异步获取航班信息
        if data["CALL"] != old_call and not info.get("FLIGHT_INFO_LOADING", False):
            info["FLIGHT_INFO_LOADING"] = True
            
            def fetch_flight_info_async(call_sign, icao_addr):
                try:
                    flight_info_str = flight_info.get_flight_info(call_sign)
                    if flight_info_str and icao_addr in aircraft_database:
                        aircraft_database[icao_addr]["FLIGHT_INFO"] = flight_info_str
                finally:
                    if icao_addr in aircraft_database:
                        aircraft_database[icao_addr]["FLIGHT_INFO_LOADING"] = False
            
            thread = threading.Thread(target=fetch_flight_info_async, args=(data["CALL"], icao))
            thread.daemon = True
            thread.start()

    # 1. 收到位置报文：更新真实高度，并记录物理基准
    if "ALT" in data and data["ALT"] != "-----":
        info["ALT"] = data["ALT"]
        try:
            # 剥离 "ft" 后转换为整数缓存，例如 "17325ft" -> 17325
            info["ALT_NUM"] = int(data["ALT"].replace("ft", "").strip())
            info["LAST_ALT_TIME"] = now
        except ValueError:
            pass

    # 2. 收到速度报文（包含垂直速率）：若高度丢包，利用物理速率进行航迹推演
    if "VR" in data:
        info["VR"] = data["VR"]
        # 如果当前有历史高度数字，且并非刚刚更新（防止和真实报文打架）
        if "ALT_NUM" in info and "LAST_ALT_TIME" in info:
            delta_t = now - info["LAST_ALT_TIME"]
            # 仅在断帧 2 秒以上、30 秒以内的区间进行平滑推演
            if 2.0 <= delta_t <= 30.0 and "fpm" in data["VR"]:
                try:
                    vr_val = int(data["VR"].replace("fpm", "").strip())
                    if vr_val != 0:
                        # 物理推算：新高度 = 历史高度 + 垂直速率(fpm) * 时间差(秒) / 60
                        predicted_alt = info["ALT_NUM"] + int(vr_val * (delta_t / 60.0))

                        # 限制一下合理性，防止推演滑稽（高度不可能为负数）
                        if predicted_alt < 0: predicted_alt = 0

                        info["ALT_NUM"] = predicted_alt
                        info["ALT"] = f"{predicted_alt}ft*"  # 加上 * 标记
                        info["LAST_ALT_TIME"] = now          # 推进推演时间轴（但不更新 LAST_SEEN）
                except ValueError:
                    pass
   
    if "HDG" in data:  info["HDG"] = data["HDG"]
    if "SPD" in data:  info["SPD"] = data["SPD"]

    # ---- 采用基于动态 API 基准坐标的 Local CPR 单帧解算逻辑 ----
    if "F" in data and "LATBIN" in data and "LONBIN" in data:
        f_flag = int(data["F"])
        
        # 【核心修正】：由于传输序列化标签颠倒，在此处进行适配器拦截，物理扶正输入源
        lat_bin = int(data["LONBIN"])  # 拿 LONBIN 的数值作为纬度二进制输入
        lon_bin = int(data["LATBIN"])  # 拿 LATBIN 的数值作为经度二进制输入
        
        if f_flag == 0:
            info["EVEN"] = {"lat": lat_bin, "lon": lon_bin, "time": now}
        else:
            info["ODD"] = {"lat": lat_bin, "lon": lon_bin, "time": now}
            
        # 直接代入启动时动态获取到的本地公网经纬度基准进行秒解
        res = decode_local_cpr(BASE_LAT, BASE_LON, lat_bin, lon_bin, is_odd=(f_flag == 1))
        if res:
            info["LAT"], info["LON"] = res

def clean_expired_aircraft(timeout_sec=60):
    """ 剔除失联超过指定时间的飞机 """
    now = time.time()
    expired = [icao for icao, info in aircraft_database.items() if now - info["LAST_SEEN"] > timeout_sec]
    for icao in expired:
        del aircraft_database[icao]

# ---- 【全新添加】：实时生成字符二维雷达动态星空图 ----
def generate_radar_map(rows=17, cols=51, max_range_deg=3.0):
    """ 根据活跃飞机的经纬度，动态生成带有 ANSI 彩色控制符的二维投影画布 """
    # 注意：因为加入了 ANSI 变色控制符，数组里存储的不能单单是普通字符，需要存入拼接后的彩色字符串
    canvas = [[" " for _ in range(cols)] for _ in range(rows)]
    
    center_r = rows // 2
    center_c = cols // 2
    
    # 用亮红色绘制雷达原点
    canvas[center_r][center_c] = f"{COLOR_CENTER}☩{COLOR_RESET}" 
    
    # 用轻量字符标注一下东南西北方位刻度线
    if canvas[0][center_c] == " ": canvas[0][center_c] = "N"
    if canvas[rows-1][center_c] == " ": canvas[rows-1][center_c] = "S"
    if canvas[center_r][0] == " ": canvas[center_r][0] = "W"
    if canvas[center_r][cols-1] == " ": canvas[center_r][cols-1] = "E"

    for icao, info in aircraft_database.items():
        if isinstance(info["LAT"], float) and isinstance(info["LON"], float):
            # 计算目标与中心基准点的经纬度相对偏差
            d_lat = info["LAT"] - BASE_LAT
            d_lon = info["LON"] - BASE_LON
            
            # 经度方向由于高纬度收缩，乘以余弦进行几何步长补偿，防止雷达图横向拉伸畸变
            d_lon_compensated = d_lon * math.cos(math.pi / 180.0 * BASE_LAT)
            
            # 归一化映射到字符矩阵坐标上 (Y轴方向相反，北在上，所以减去 d_lat)
            r = center_r - int((d_lat / max_range_deg) * center_r)
            c = center_c + int((d_lon_compensated / max_range_deg) * center_c)
            
            # 确保投影点没有溢出我们设定的终端显示画布边界
            if 0 <= r < rows and 0 <= c < cols:
                # 动态分配识别标识：有呼号用呼号首字母，没呼号用飞机特征 ✈️ 或数字表示
                symbol = "✈"
                if info["CALL"] != "-------" and len(info["CALL"]) > 0:
                    symbol = info["CALL"][0]
                
                # 【核心修改】：从该飞机的字典里取出绑定的特定颜色，对当前网格点进行染色封装
                canvas[r][c] = f"{info['COLOR']}{COLOR_BOLD}{symbol}{COLOR_RESET}"

    # 拼接渲染成终端可识别的连续字符串
    map_lines = []
    map_lines.append("┌" + "─" * (cols) + "┐")
    for row in canvas:
        # 拼接时由于存在不可见控制字符，终端渲染会自动计算真实的物理对齐宽度
        map_lines.append("│" + "".join(row) + "│")
    map_lines.append("└" + "─" * (cols) + "┘")
    return "\n".join(map_lines)

def draw_radar_screen(total_raw):
    """ 清屏并绘制色彩深度联动、支持一机一色的空管级监控面板 """
    os.system('cls' if os.name == 'nt' else 'clear')
    
    # 1. 打印雷达头部基本面态势
    print("=" * 140)
    print(f"  ADS-B 实时航迹监控看板 | 运行中... | 累计捕获原始报文: {total_raw}")
    print(f"  当前空域活跃目标数: {len(aircraft_database)} 个 | 刷新时间: {datetime.now().strftime('%H:%M:%S')}")
    print(f"  基准中心（PlutoSDR 站台位置）：纬度={BASE_LAT:.5f}，经度={BASE_LON:.5f}")
    print("=" * 140)
    
    # 2. 注入动态生成的字符识别动画（点分布图）
    print("【 空 域 活 跃 目 标 方 位 散 点 投 影 】 (中心 ☩ 为本地接收机，英文字母为对应航班首字母)")
    print(generate_radar_map(rows=13, cols=65, max_range_deg=3.5))
    print("=" * 140)
    
    # 3. 打印传统的表格详细状态流
    print(f" {'ICAO 地址':<8} | {'航班号':<6} | {'高度 (ALT)':<8} | {'地速 (SPD)':<8} | {'航向 (HDG)':<7} | {'垂直速率':<5} | {'纬度 (LAT)':<8} | {'经度 (LON)':<8} | {'报文数':<2} | {'航班信息':<30}")
    print("-" * 140)
    
    for icao, info in sorted(aircraft_database.items(), key=lambda x: x[1]['LAST_SEEN'], reverse=True):
        lat_str = f"{info['LAT']:.5f}" if isinstance(info['LAT'], float) else str(info['LAT'])
        lon_str = f"{info['LON']:.5f}" if isinstance(info['LON'], float) else str(info['LON'])
        
        # 【微调添加】：获取航班信息显示
        flight_info_str = info.get("FLIGHT_INFO", "")
        if not flight_info_str and info["CALL"] != "-------" and info.get("FLIGHT_INFO_LOADING", False):
            flight_info_str = "查询中..."
        elif not flight_info_str and info["CALL"] != "-------":
            flight_info_str = ""
        elif info["CALL"] == "-------":
            flight_info_str = ""

        # 生成该行文本的基础内容结构
        row_content = f" {icao:<10} | {info['CALL']:<9} | {info['ALT']:<10} | {info['SPD']:<10} | {info['HDG']:<10} | {info['VR']:<9} | {lat_str:<10} | {lon_str:<10} | {info['COUNT']:<6} | {flight_info_str:<30}"
        
        # 【核心修改】：利用当前飞机独有的 `info['COLOR']` 渲染整行表格，实现与雷达点色彩的绝对联动
        print(f"{info['COLOR']}{row_content}{COLOR_RESET}")
        
    print("=" * 140)
    print(" 提示: 按 Ctrl+C 退出监控。超时 60s 未收到报文的目标将自动移出屏幕。")

def main():
    global running
    parser = argparse.ArgumentParser(description="Advanced ADS-B Track Tracker")
    parser.add_argument("--ip", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=1234)
    args = parser.parse_args()

    # 【微调添加】：启动时单次无感加载基准位置 API
    get_base_location()

    signal.signal(signal.SIGINT, signal_handler)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.ip, args.port))
    sock.settimeout(0.5)

    total_raw_msgs = 0
    last_ui_update = 0

    while running:
        try:
            data, addr = sock.recvfrom(4096)
            line = data.decode("utf-8", errors="replace").strip()
            if line and line.startswith("DF="):
                total_raw_msgs += 1
                parsed_data = parse_pluto_line(line)
                update_database(parsed_data)
                
            # 每 0.5 秒强制刷新一次 UI，并检查超时剔除
            now = time.time()
            if now - last_ui_update > 0.5:
                clean_expired_aircraft(timeout_sec=60)
                draw_radar_screen(total_raw_msgs)
                last_ui_update = now
                
        except socket.timeout:
            # 即使没收到网络包，UI 也要保持刷新（处理倒计时剔除）
            now = time.time()
            if now - last_ui_update > 0.5:
                clean_expired_aircraft(timeout_sec=60)
                draw_radar_screen(total_raw_msgs)
                last_ui_update = now
            continue
        except Exception as e:
            pass

    print("\n[Viewer] 退出航迹追踪。")

if __name__ == "__main__":
    main()
