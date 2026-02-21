#!/usr/bin/env python
# coding=utf-8
import torch
import matplotlib.pyplot as plt
import numpy as np
import train_1d as train  
import factory           
import os
import sys

def plot_samples(data_dir, result_dir, item_size, num_samples=5):
    train.ITEM_SIZE = item_size
    dataset = train.BinDataset(data_dir)
    mods = dataset.mods
    
    fig, axes = plt.subplots(len(mods), num_samples, figsize=(num_samples*3, len(mods)*3))
    
    for i, mod in enumerate(mods):
        # 找到属于该调制类型的索引
        indices = np.where(dataset.all_labels == i)[0]
        selected_indices = np.random.choice(indices, num_samples, replace=False)
        
        for j, idx in enumerate(selected_indices):
            # 这里会自动调用你在 BinDataset 里写的 __getitem__，包含旋转增强
            iq_tensor, _ = dataset[idx]
            iq = iq_tensor.numpy()

            ax = axes[i, j]
            ax.scatter(iq[0, :], iq[1, :], s=1, alpha=0.5) # x, y, 点大小, 点透明度
            ax.set_xlim([-1.2, 1.2])
            ax.set_ylim([-1.2, 1.2])
            if j == 0:
                ax.set_ylabel(mod, fontsize=12, fontweight='bold')
            ax.set_xticks([]); ax.set_yticks([])

    plt.suptitle(f"Augmented Constellation Samples (ITEM_SIZE={item_size})", fontsize=16)
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    
    save_path = os.path.join(result_dir, "result_constellation_samples.png")
    plt.savefig(save_path)
    print(f"增强样本预览图已保存至: {save_path}")

if __name__ == "__main__":
    DATA_DIR, _, ITEM_SIZE, RESULT_DIR = factory.parse_script_params(sys.argv)
    plot_samples(DATA_DIR, RESULT_DIR, ITEM_SIZE)
