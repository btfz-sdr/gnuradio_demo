#!/usr/bin/env python
# coding=utf-8
import os
import sys
import torch
import torch.nn as nn
import torch.optim as optim
import numpy as np
from torch.utils.data import Dataset, DataLoader, random_split
from tqdm import tqdm  # 用于打印进度条

# ==========================================
# 1. 配置与参数 (自动适配)
# ==========================================
from factory import parse_script_params
DATA_DIR, TOTAL_SAMPLES, ITEM_SIZE, RESULT_DIR = parse_script_params(sys.argv)

# 超参数配置
BATCH_SIZE = 2048 
EPOCHS = 50
LEARNING_RATE = 0.001
DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# ==========================================
# 2. 自定义数据集加载类 (字节对齐)
# 设计理由：内存效率。在大数据训练时，直接把所有 .bin 文件读入内存会导致内存溢出。
# memmap 允许程序像操作内存一样操作磁盘文件，只在真正需要数据（__getitem__）时才加载到 RAM。
# ==========================================
class BinDataset(Dataset):
    def __init__(self, data_dir):
        self.samples = []
        self.labels = []
        # 按字母顺序排列调制类型，确保索引一致
        self.mods = sorted([f.replace('.bin', '') for f in os.listdir(data_dir) if f.endswith('.bin')])
        
        print(f"检测到调制类型: {self.mods}")
        print(f"当前 ITEM_SIZE 配置: {ITEM_SIZE}")
        
        for idx, mod in enumerate(self.mods):
            path = os.path.join(data_dir, f"{mod}.bin")
            file_size = os.path.getsize(path)
            # 自动根据 ITEM_SIZE 计算样本字节
            bytes_per_sample = 2 * ITEM_SIZE * 4  # 2 (I/Q) × ITEM_SIZE × 4 bytes per float32
            num_samples = file_size // bytes_per_sample 
            
            # 使用内存映射实现大数据量加载
            data_mmap = np.memmap(path, dtype='float32', mode='r', shape=(num_samples, 2, ITEM_SIZE))
            self.samples.append(data_mmap)
            self.labels.append(np.full(num_samples, idx))
            print(f" - {mod}: 载入 {num_samples} 个样本")

        self.all_labels = np.concatenate(self.labels)
        self.total_len = len(self.all_labels)
        self.cumulative_sizes = np.cumsum([len(s) for s in self.samples])

    def __len__(self):
        return self.total_len

    # 在训练端加入“随机相位增强”
    # 让模型**“见过各种角度的信号”**。这样即使接收端偏了 30° 或 45°，模型依然能准确识别出形状。
    # 只对每个样本做一次随机旋转（方案 A），而不是把每个样本旋转 360 度后的所有角度都生成出来喂给模型（方案 B）
    # - 这是一个非常深刻的问题，触及了深度学习中**数据增强（Data Augmentation）与数据集扩充（Dataset Expansion）**的核心区别。
    #    - 1. 训练效率与内存瓶颈
    #    - 2. 泛化能力：连续 vs 离散
    #    - 3. 概率上的等效性（每一代都做了一定的概率旋转，模型实际上已经见过样本 A 在空间中各个方向的投影了）
    # 在训练时使用随机旋转（Data Augmentation）虽然能增强泛化能力，但如果数据本身的幅度太小，或者网络还没有收敛，这种 “动态变化”
    # 的输入会让模型感到困惑，导致 Loss 无法下降，准确率卡死在随机水平（16.5%）。
    #
    # 1. 峰值归一化 (Peak Normalization / Max Scaling)
    # |- 它将信号中的最大模值（Peak）强制缩放到 1。   
    # |- 物理意义：将信号的“外包络”约束在单位圆内。 
    # |- 优点：星座图点会分布在 1.0 附近，非常直观，模型梯度大。  
    # |- 缺点：对异常脉冲（Spike）非常敏感。如果信号中有一个极大的噪声点，整个星座图会缩得很小。   
    # 2. 能量归一化 (L2 / Power Normalization)
    # |- 物理意义：保证每个信号块（Item）的总能量恒定为 1。
    # |- 优点：符合通信系统中“等能量发射”的假设。
    # |- 缺点：幅值受符号长度影响严重。正如之前计算的，256 点时幅值会缩到 0.04 左右。
    # 3. 标准化 (Z-Score Normalization)
    # |- 物理意义：将数据调整为均值为 0，方差为 1 的分布。
    # |- 优点：能把不同动态范围的信号拉回到模型最喜欢的“激活区”。
    # |- 适用场景：作为模型的第一层（BatchNorm1d）效果极佳。
    # 4. 符号归一化 (Symbol-wise Normalization)
    # |- 这是一种“通信特有”的归一化。它不看整个 256 点的块，而是根据某种算法（如 CMA 恒模算法）估计出符号的平均幅度并归一。
    # |- 特点：它能消除信道衰落，但实现复杂，通常用于解调前的同步环节。  
    def __getitem__(self, idx):
        # 每次处理 256 个数据，相当于 1 item
        file_idx = np.searchsorted(self.cumulative_sizes, idx, side='right')
        local_idx = idx if file_idx == 0 else idx - self.cumulative_sizes[file_idx - 1]
        
        # 1. 提取原始 IQ 数据 [2, ITEM_SIZE]
        iq = self.samples[file_idx][local_idx].copy()

        # 2. 峰值归一化
        peak = np.max(np.abs(iq)) + 1e-12
        iq /= peak

        # 3. 核心：加入随机相位旋转增强 (Data Augmentation)
        # 产生一个 0 到 2*pi 之间的随机角度
        random_phase = np.random.uniform(0, 2 * np.pi)
        cos_phi = np.cos(random_phase)
        sin_phi = np.sin(random_phase)
        
        # 构建旋转矩阵
        # [I_new] = [cos -sin] [I_old]
        # [Q_new] = [sin  cos] [Q_old]
        i_old = iq[0, :]
        q_old = iq[1, :]
        
        i_new = i_old * cos_phi - q_old * sin_phi
        q_new = i_old * sin_phi + q_old * cos_phi
        
        iq_rotated = np.stack([i_new, q_new], axis=0).astype(np.float32)

        # 3. 转换为 Tensor
        x = torch.from_numpy(iq_rotated).float()
        y = torch.tensor(self.all_labels[idx]).long()
        return x, y

# ==========================================
# 3. 模型定义 (ResNet)
# ==========================================
class ResidualBlock(nn.Module):
    # 包含：Conv1d -> BatchNorm -> ReLU -> Conv1d -> BatchNorm
    # forward 中：return torch.relu(x + self.conv(x))
    # 信号识别需要较深的网络来提取高阶特征。x + self.conv(x) 的残差连接（Skip Connection）允许梯度直接回传，让网络更容易训练。
    def __init__(self, channels):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv1d(channels, channels, kernel_size=5, padding=2),
            nn.BatchNorm1d(channels),
            nn.ReLU(),
            nn.Conv1d(channels, channels, kernel_size=5, padding=2),
            nn.BatchNorm1d(channels)
        )
        # 增加注意力算子
        self.attention = SEBlock(channels)

    def forward(self, x):
        # 残差计算公式：out = ReLU( Attention(F(x)) + x )
        residual = self.conv(x)
        residual = self.attention(residual)
        return torch.relu(x + residual)

class SEBlock(nn.Module):
    def __init__(self, channels, reduction=4):
        super().__init__()
        self.avg_pool = nn.AdaptiveAvgPool1d(1) # 算子：全局平均池化
        self.fc = nn.Sequential(
            nn.Linear(channels, channels // reduction, bias=False),
            nn.ReLU(),
            nn.Linear(channels // reduction, channels, bias=False),
            nn.Sigmoid() # 算子：Sigmoid 激活，输出 0~1 的权重
        )

    def forward(self, x):
        b, c, _ = x.size()
        # 1. Squeeze: [B, C, L] -> [B, C, 1] -> [B, C]
        y = self.avg_pool(x).view(b, c)
        # 2. Excitation: [B, C] -> [B, C]
        y = self.fc(y).view(b, c, 1)
        # 3. Scale: 逐通道加权
        return x * y.expand_as(x)

class AMCNet(nn.Module):
    def __init__(self, num_classes):
        super().__init__()
        self.prep = nn.Sequential(
            nn.Conv1d(2, 64, kernel_size=7, padding=3),
            nn.BatchNorm1d(64),
            nn.ReLU()
        )
        self.layer1 = ResidualBlock(64)
        self.layer2 = ResidualBlock(64)
        self.flatten = nn.Flatten()
        self.fc = nn.Sequential(
            nn.Linear(64 * ITEM_SIZE, 1024),
            nn.ReLU(),
            nn.Dropout(0.5),
            nn.Linear(1024, num_classes)
        )

    def forward(self, x):
        # x 维度: [Batch, 2, 256]
        # 1. 动态峰值归一化 (针对每一个样本进行缩放)
        # 找到每个样本中 I 和 Q 绝对值的最大值
        with torch.no_grad():
            # 将 [B, 2, 256] 展平为 [B, 512] 求最大值，再 view 回 [B, 1, 1]
            max_val, _ = torch.max(torch.abs(x).view(x.size(0), -1), dim=1, keepdim=True)
            max_val = max_val.view(-1, 1, 1) + 1e-12
        x = x / max_val

        # 2. 进入原有网络
        x = self.prep(x)
        x = self.layer1(x)
        x = self.layer2(x)
        x = self.flatten(x)
        return self.fc(x)

# ==========================================
# 4. 训练主程序
# ==========================================
if __name__ == "__main__":
    full_dataset = BinDataset(DATA_DIR)
    mods = full_dataset.mods
    
    train_size = int(0.8 * len(full_dataset))
    test_size = len(full_dataset) - train_size
    train_set, test_set = random_split(full_dataset, [train_size, test_size])

    train_loader = DataLoader(train_set, batch_size=BATCH_SIZE, shuffle=True, num_workers=8, pin_memory=True)
    test_loader = DataLoader(test_set, batch_size=BATCH_SIZE, shuffle=False, num_workers=4)

    model = AMCNet(len(mods)).to(DEVICE)
    optimizer = optim.Adam(model.parameters(), lr=LEARNING_RATE)
    criterion = nn.CrossEntropyLoss()
    
    # 检查是否存在已有训练基础
    model_path = os.path.join(RESULT_DIR, "amc_custom.pth")
    log_path = os.path.join(RESULT_DIR, "train_log.csv")
    start_epoch = 0

    if os.path.exists(model_path):
        print(f"检测到已有模型权重: {model_path}，正在加载并继续训练...")
        model.load_state_dict(torch.load(model_path, map_location=DEVICE))
        if os.path.exists(log_path):
            with open(log_path, 'r') as f:
                lines = f.readlines()
                if lines:
                    try:
                        last_line = lines[-1].strip().split(',')
                        start_epoch = int(last_line[0]) + 1
                        print(f"检测到历史日志，将从第 {start_epoch + 1} 个 Epoch 开始...")
                    except:
                        print("日志解析失败，将从第 1 个 Epoch 开始...")
    else:
        print("未检测到训练基础，开始全新训练。")

    # --- 解决警告：使用新版 Amp 接口 ---
    scaler = torch.amp.GradScaler('cuda') 
    
    print(f"\n开始训练，设备: {DEVICE}")
    for epoch in range(start_epoch, EPOCHS):
        model.train()
        total_loss = 0
        # 增加进度条
        train_pbar = tqdm(train_loader, desc=f"Epoch {epoch+1}/{EPOCHS} [Train]")
        
        for images, labels in train_pbar:
            images, labels = images.to(DEVICE), labels.to(DEVICE)
            
            optimizer.zero_grad()
            # --- 解决警告：使用新版 Autocast 接口 ---
            with torch.amp.autocast('cuda'):
                outputs = model(images)
                loss = criterion(outputs, labels)
            
            scaler.scale(loss).backward()
            scaler.step(optimizer)
            scaler.update()
            
            total_loss += loss.item()
            train_pbar.set_postfix(loss=f"{loss.item():.4f}")

        # 验证阶段
        model.eval()
        correct = 0
        test_pbar = tqdm(test_loader, desc=f"Epoch {epoch+1}/{EPOCHS} [Test]")
        with torch.no_grad():
            for images, labels in test_pbar:
                images, labels = images.to(DEVICE), labels.to(DEVICE)
                outputs = model(images)
                _, predicted = torch.max(outputs.data, 1)
                correct += (predicted == labels).sum().item()
                test_pbar.set_postfix(acc=f"{100 * correct / len(test_set):.2f}%")
        
        final_acc = 100 * correct / len(test_set)
        avg_loss = total_loss / len(train_loader)
        print(f"Summary -> Epoch {epoch+1}: Loss = {avg_loss:.4f}, Accuracy = {final_acc:.2f}%\n")
        
        # 写入 log (保持 a 模式，断点续传时追加)
        with open(log_path, "a") as f:
            f.write(f"{epoch},{avg_loss},{final_acc}\n")

        # 每次 Epoch 结束保存一次权重，防止中断
        torch.save(model.state_dict(), model_path)

    # 导出 ONNX
    onnx_path = os.path.join(RESULT_DIR, "amc_model.onnx")
    dummy_input = torch.randn(1, 2, ITEM_SIZE).to(DEVICE)
    torch.onnx.export(model, dummy_input, onnx_path, 
                      input_names=['input'], 
                      dynamic_axes={'input': {0: 'batch_size'}})
    
    print(f"模型训练完成。")
    print(f"权重文件已保存至: {model_path}")
    print(f"ONNX文件已导出至: {onnx_path}")
