#!/usr/bin/env python
# coding=utf-8
import os
import re
import sys
import torch
import torch.nn as nn # 导入 nn 确保模型类能正常引用
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.metrics import confusion_matrix
from torch.utils.data import DataLoader

import factory

def plot_confusion_matrix(data_dir, result_dir, item_size):
    # 1. 使用工厂一键加载模型、数据集、设备和模式
    try:
        model, dataset, device, mode = factory.load_amc_environment(data_dir, result_dir, item_size)
    except Exception as e:
        print(f"\n[错误] 环境加载失败！请检查 ITEM_SIZE 或模型文件。")
        print(f"报错信息: {e}")
        return

    mods = dataset.mods
    # 建议只在测试集上评估（此处使用全量数据进行最终验证）
    test_loader = DataLoader(dataset, batch_size=1024, shuffle=False, num_workers=4)

    model.eval()

    y_true = []
    y_pred = []

    print(f"正在以 {mode} 模式 (ITEM_SIZE={item_size}) 进行预测...")
    with torch.no_grad():
        for images, labels in test_loader:
            images = images.to(device)
            outputs = model(images)
            _, predicted = torch.max(outputs, 1)
            
            y_true.extend(labels.numpy())
            y_pred.extend(predicted.cpu().numpy())

    # 3. 计算混淆矩阵
    cm = confusion_matrix(y_true, y_pred)
    # 归一化：显示百分比
    cm_norm = cm.astype('float') / (cm.sum(axis=1)[:, np.newaxis] + 1e-12)

    # 4. 绘图
    plt.figure(figsize=(10, 8))
    sns.heatmap(cm_norm, annot=True, fmt='.2f', cmap='Blues',
                xticklabels=mods, yticklabels=mods)
    
    plt.title(f'Modulation Classification Confusion Matrix ({mode})')
    plt.xlabel('Predicted Label')
    plt.ylabel('True Label')
    plt.tight_layout()
    
    result_path = os.path.join(result_dir,'result_confusion_matrix.png')
    plt.savefig(result_path)
    print(f"混淆矩阵已保存为: {result_path}")
    plt.show()

if __name__ == "__main__":
    DATA_DIR, TOTAL_SAMPLES, ITEM_SIZE, RESULT_DIR = factory.parse_script_params(sys.argv)
    
    MODEL_PATH = os.path.join(RESULT_DIR, "amc_custom.pth")
    if not os.path.exists(MODEL_PATH):
        print(f"错误: 在 {DATA_DIR} 中未找到模型文件 {MODEL_PATH}")
        sys.exit(1)

    print(f"正在通过 Factory 加载模型，数据目录: {DATA_DIR}, ITEM_SIZE={ITEM_SIZE}")
    # 注意：函数签名微调，移除了不再需要的单独 model_path 参数（factory 内部已处理）
    plot_confusion_matrix(DATA_DIR, RESULT_DIR, ITEM_SIZE)
