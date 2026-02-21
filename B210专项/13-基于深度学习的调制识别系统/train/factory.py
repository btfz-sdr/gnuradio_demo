#!/usr/bin/env python
# coding=utf-8
import os
import sys
import re
import torch

def get_model_type(result_dir):
    """通过权重文件探测是 1D 还是 2D 模型"""
    model_path = os.path.join(result_dir, "amc_custom.pth")
    if not os.path.exists(model_path):
        raise FileNotFoundError(f"未找到权重文件: {model_path}")
    
    state_dict = torch.load(model_path, map_location='cpu')
    # 获取第一个参数的维度，1D卷积是3维 [O, I, K]，2D卷积是4维 [O, I, K, K]
    first_layer_shape = next(iter(state_dict.values())).shape
    return "2D" if len(first_layer_shape) == 4 else "1D", state_dict

def load_amc_environment(data_dir, result_dir, item_size):
    """
    一键加载环境
    返回: model, dataset, device, mode_str
    """
    import train_1d
    import train_2d

    mode, state_dict = get_model_type(result_dir)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    
    if mode == "2D":
        train_2d.ITEM_SIZE = item_size
        dataset = train_2d.ConstellationDataset(data_dir, img_size=train_2d.IMG_SIZE)
        model = train_2d.AMCNet2D(len(dataset.mods))
    else:
        train_1d.ITEM_SIZE = item_size
        dataset = train_1d.BinDataset(data_dir)
        model = train_1d.AMCNet(len(dataset.mods))
    
    model.load_state_dict(state_dict)
    model.to(device)
    model.eval()
    
    return model, dataset, device, mode

def parse_script_params(argv):
    if len(argv) < 1:
        print("用法: python train.py /path/to/collected_data_{num1}k_item{num2}")
        sys.exit(1)

    DATA_DIR = argv[1]
    m = re.fullmatch(r'collected_data_(\d+)k_item(\d+)', os.path.basename(DATA_DIR))
    TOTAL_SAMPLES = int(m.group(1)) if m else 0
    ITEM_SIZE = int(m.group(2)) if m else 0
    if TOTAL_SAMPLES == 0:
        print("用法: python script.py /path/to/collected_data_{num1}k_item{num2}")
        sys.exit(1)

    RESULT_DIR = os.path.join(DATA_DIR, "result")
    if not os.path.exists(RESULT_DIR):
        os.makedirs(RESULT_DIR)
    return DATA_DIR, TOTAL_SAMPLES*1000, ITEM_SIZE, RESULT_DIR 

