#!/usr/bin/env python
# coding=utf-8
# pip install torchview torchviz
import torch
from torchview import draw_graph
from torchviz import make_dot
import factory
import os
import sys

def visualize_with_viz(data_dir, result_dir, item_size):
    # 使用 factory 获取模型和模式
    model, dataset, _, mode = factory.load_amc_environment(data_dir, result_dir, item_size)
    model = model.cpu()
    
    # 根据模式准备模拟输入
    if mode == "2D":
        x = torch.randn(1, 1, 64, 64)
    else:
        x = torch.randn(1, 2, item_size)
        
    # 跟踪前向传播
    y = model(x)

    # 生成可视化图，params 传入 model.named_parameters() 才能看到层名称
    dot = make_dot(y, params=dict(model.named_parameters()), show_attrs=True, show_saved=True)
    dot.format = 'png'
    output_path = os.path.join(result_dir, "result_amcnet_flow")
    dot.render(output_path, cleanup=True) # cleanup=True 会自动删除中间的 .gv 文件

    print(f"更形象的结构流向图已保存至: {output_path}.png")

def visualize_structure(data_dir, result_dir, item_size):
    # 使用 factory 获取模型和模式
    model, dataset, _, mode = factory.load_amc_environment(data_dir, result_dir, item_size)
    model = model.cpu()
    
    # 根据模式定义输入维度
    if mode == "2D":
        input_size = (1, 1, 64, 64)
        stat_input = (1, 64, 64)
    else:
        input_size = (1, 2, item_size)

    # 使用 torchview 绘制拓扑结构图
    draw_graph(
        model, 
        input_size=input_size, 
        expand_nested=True, 
        save_graph=True, 
        directory=result_dir,
        filename="result_network_structure",
        device='cpu'
    )
    
    print(f"[{mode}] 模型拓扑结构图已生成。")

    # 手动打印参数量替代 torchstat，避免 numpy 报错
    total_params = sum(p.numel() for p in model.parameters())
    trainable_params = sum(p.numel() for p in model.parameters() if p.requires_grad)
    print(f"--- 模型统计 ---")
    print(f"总参数量: {total_params:,}")
    print(f"可训练参数量: {trainable_params:,}")
    print(f"----------------")

if __name__ == "__main__":
    # 解析器获取基础路径参数
    DATA_DIR, _, ITEM_SIZE, RESULT_DIR = factory.parse_script_params(sys.argv)
    
    # 先画拓扑图，再画流向图
    visualize_structure(DATA_DIR, RESULT_DIR, ITEM_SIZE)
    visualize_with_viz(DATA_DIR, RESULT_DIR, ITEM_SIZE)
