#!/usr/bin/env python
# coding=utf-8
"""
航班信息查询模块
提供航班信息的离线查询功能
"""

import threading
import re
from datetime import datetime

# 航班信息缓存
flight_cache = {}
cache_lock = threading.Lock()

# 本地航空公司数据库（完整版）
AIRLINE_DB = {
    # 中国主要航空公司
    "CES": "东方航空", "CCA": "中国国际航空", "CSN": "南方航空",
    "CSC": "四川航空", "CHH": "海南航空", "CBJ": "首都航空",
    "CQH": "春秋航空", "CXA": "厦门航空", "CDG": "山东航空",
    "CSH": "上海航空", "CBF": "河北航空", "CJX": "江西航空",
    "CYZ": "长龙航空", "CUA": "中国联航", "CNS": "中国邮政航空",
    "OKA": "奥凯航空", "HXA": "华夏航空", "CXN": "幸福航空",
    "CSZ": "深圳航空", "DKH": "吉祥航空", "CDC": "长龙航空",
    "JYH": "九元航空", "GCR": "天津航空", "CJG": "桂林航空",
    "CQN": "重庆航空", "CSC": "成都航空", "CGH": "北部湾航空",
    "CJN": "瑞丽航空", "CSS": "顺丰航空", "CYZ": "圆通航空",
    
    # 国际及地区航空公司
    "SIA": "新加坡航空", "UAE": "阿联酋航空", "QTR": "卡塔尔航空",
    "THA": "泰国航空", "AAR": "韩亚航空", "KAL": "大韩航空",
    "JAL": "日本航空", "ANA": "全日空", "CPA": "国泰航空",
    "EVA": "长荣航空", "CAL": "中华航空", "MAS": "马来西亚航空",
    "QFA": "澳洲航空", "BAW": "英国航空", "KLM": "荷兰皇家航空",
    "AFR": "法国航空", "DLH": "汉莎航空", "UAL": "美联航",
    "AAL": "美国航空", "DAL": "达美航空", "ACA": "加拿大航空",
    "UPS": "联合包裹", "FDX": "联邦快递", "DHL": "敦豪航空",
    "NCA": "日本货运航空", "KZR": "哈萨克斯坦航空",
    
    # 快递/货运航空公司
    "UPS": "UPS航空", "FDX": "FedEx", "CNS": "中国邮政航空",
    "CSS": "顺丰航空", "CYZ": "圆通航空", "CSN": "南方航空货运",
}

def extract_flight_prefix(flight_number):
    """提取航班号前缀（航空公司代码）"""
    if not flight_number or flight_number == "-------":
        return None
    match = re.match(r'^([A-Z]{2,3})', flight_number)
    return match.group(1) if match else None

def get_flight_info_offline(flight_number):
    """
    离线查询航班信息（基于本地规则库）
    返回格式: 航空公司
    """
    if not flight_number or flight_number == "-------":
        return None
    
    prefix = extract_flight_prefix(flight_number)
    if prefix:
        airline = AIRLINE_DB.get(prefix, prefix)
        return f"{airline}"
    
    return None

def get_flight_info(flight_number):
    """
    统一航班信息查询接口
    参数:
        flight_number: 航班号（如 CES5339）
    返回:
        航空公司名称
    """
    if not flight_number or flight_number == "-------":
        return None
    
    # 先检查缓存
    with cache_lock:
        if flight_number in flight_cache:
            cache_time, cache_data = flight_cache[flight_number]
            if (datetime.now().timestamp() - cache_time) < 600:
                return cache_data
    
    result = get_flight_info_offline(flight_number)
    
    # 缓存结果
    if result:
        with cache_lock:
            flight_cache[flight_number] = (datetime.now().timestamp(), result)
    
    return result
