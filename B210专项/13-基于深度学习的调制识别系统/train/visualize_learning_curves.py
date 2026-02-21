#!/usr/bin/env python
# coding=utf-8
import pandas as pd
import matplotlib.pyplot as plt
import os
import sys

import factory

def plot_curves(result_dir):
    log_path = os.path.join(result_dir, "train_log.csv")
    if not os.path.exists(log_path):
        print("未找到日志文件 train_log.csv")
        return

    # 自动探测模式以丰富标题信息
    try:
        mode, _ = factory.get_model_type(result_dir)
    except:
        mode = "Unknown"

    df = pd.read_csv(log_path, names=['epoch', 'loss', 'accuracy'])

    fig, ax1 = plt.subplots(figsize=(10, 6))

    # 绘制 Loss
    ax1.set_xlabel('Epochs')
    ax1.set_ylabel('Loss', color='tab:red')
    ax1.plot(df['epoch'], df['loss'], color='tab:red', label='Train Loss')
    ax1.tick_params(axis='y', labelcolor='tab:red')

    # 绘制 Accuracy
    ax2 = ax1.twinx()
    ax2.set_ylabel('Accuracy (%)', color='tab:blue')
    ax2.plot(df['epoch'], df['accuracy'], color='tab:blue', label='Test Acc')
    ax2.tick_params(axis='y', labelcolor='tab:blue')

    plt.title(f'Training Metrics Over Time ({mode} Model)')
    plt.savefig(os.path.join(result_dir, "result_learning_curves.png"))
    print("曲线图已保存。")

if __name__ == "__main__":
    DATA_DIR, TOTAL_SAMPLES, ITEM_SIZE, RESULT_DIR = factory.parse_script_params(sys.argv)
    plot_curves(RESULT_DIR)
