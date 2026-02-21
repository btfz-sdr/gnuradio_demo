#!/usr/bin/env python
# coding=utf-8
# pip install scikit-learn
import torch
import numpy as np
import matplotlib.pyplot as plt
from sklearn.manifold import TSNE
import factory
import os
import sys
from torch.utils.data import DataLoader

def plot_tsne(data_dir, result_dir, item_size):
    # 1. 使用 factory 加载模型、数据集和模式
    model, dataset, device, mode = factory.load_amc_environment(data_dir, result_dir, item_size)
    
    # 取一小部分数据进行可视化，否则 t-SNE 算不动
    subset_indices = np.random.choice(len(dataset), 2000, replace=False)
    subset = torch.utils.data.Subset(dataset, subset_indices)
    loader = DataLoader(subset, batch_size=128, shuffle=False)

    # 定义钩子获取特征提取层的输出
    features = []
    labels_list = []

    def hook_fn(module, input, output):
        # 将输出拉平，确保 1D (Batch, Features) 和 2D (Batch, C, H, W) 都能处理
        feat = output.cpu().detach().numpy()
        features.append(feat.reshape(feat.shape[0], -1))

    # 2. 根据模型模式动态选择挂载点
    if mode == "2D":
        # 2D 模型挂在全局池化层之后，全连接层之前
        target_layer = model.pool 
    else:
        # 1D 模型挂在第一个线性层（fc[1] 是 ReLU）
        target_layer = model.fc[1]

    handle = target_layer.register_forward_hook(hook_fn) 

    with torch.no_grad():
        for iq, labels in loader:
            _ = model(iq.to(device))
            labels_list.extend(labels.numpy())

    handle.remove()
    features = np.concatenate(features, axis=0)

    # 3. t-SNE 降维
    print(f"正在进行 {mode} 模型的 t-SNE 降维计算，请稍候...")
    tsne = TSNE(
        n_components=2,
        perplexity=30,
        init='pca',          # 建议加上，有助于降维结果更稳定
        learning_rate='auto' # 建议加上，适配新版 sklearn
    )
    low_dim_embs = tsne.fit_transform(features)

    # 4. 绘图
    plt.figure(figsize=(12, 10))
    for i, mod in enumerate(dataset.mods):
        idx = np.where(np.array(labels_list) == i)
        plt.scatter(low_dim_embs[idx, 0], low_dim_embs[idx, 1], label=mod, alpha=0.6)
    
    plt.legend()
    plt.title(f"t-SNE Feature Visualization ({mode} Model, ITEM_SIZE={item_size})")
    plt.savefig(os.path.join(result_dir, "result_tsne_analysis.png"))
    print(f"t-SNE 分析图已保存至: {os.path.join(result_dir, 'result_tsne_analysis.png')}")

if __name__ == "__main__":
    DATA_DIR, _, ITEM_SIZE, RESULT_DIR = factory.parse_script_params(sys.argv)
    # 直接调用，不再需要手动拼接 MODEL_PATH，由 factory 处理
    plot_tsne(DATA_DIR, RESULT_DIR, ITEM_SIZE)
