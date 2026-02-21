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
BATCH_SIZE = 128      # 2D卷积计算量较大，建议调小 Batch
EPOCHS = 19
LEARNING_RATE = 0.001
IMG_SIZE = 64         # 星座图分辨率 (64x64)
IQ_RANGE = 1.2        # IQ 坐标映射范围 [-1.2, 1.2]
DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# ==========================================
# 2. 自定义数据集 (IQ序列转2D密度图)
# ==========================================
class ConstellationDataset(Dataset):
    def __init__(self, data_dir, img_size=64, iq_range=1.2):
        self.img_size = img_size
        self.iq_range = iq_range
        self.bins = np.linspace(-iq_range, iq_range, img_size + 1)
        
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

    def __getitem__(self, idx):
        # 每次处理 256 个数据，相当于 1 item
        file_idx = np.searchsorted(self.cumulative_sizes, idx, side='right')
        local_idx = idx if file_idx == 0 else idx - self.cumulative_sizes[file_idx - 1]
        
        # 1. 提取原始 IQ 数据 [2, ITEM_SIZE]
        iq = self.samples[file_idx][local_idx].copy()

        # 2. 峰值归一化
        peak = np.max(np.abs(iq)) + 1e-12
        iq /= peak

        # 3. 随机相位旋转增强
        random_phase = np.random.uniform(0, 2 * np.pi)
        cos_p, sin_p = np.cos(random_phase), np.sin(random_phase)
        i_new = iq[0, :] * cos_p - iq[1, :] * sin_p
        q_new = iq[0, :] * sin_p + iq[1, :] * cos_p

        # 4. 生成 2D 密度图 (核心算子)
        # 将点投射到网格中
        hist, _, _ = np.histogram2d(i_new, q_new, bins=self.bins, range=[[-self.iq_range, self.iq_range], [-self.iq_range, self.iq_range]])
        
        # 使用 log 增强并归一化至 [0, 1]
        hist = np.log1p(hist)
        max_h = np.max(hist)
        if max_h > 0: hist /= max_h

        x = torch.from_numpy(hist).float().unsqueeze(0) # [1, H, W]
        y = torch.tensor(self.all_labels[idx]).long()
        return x, y

# ==========================================
# 3. 2D ResNet 模型定义
# ==========================================
class BasicBlock2D(nn.Module):
    def __init__(self, in_planes, planes, stride=1):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(in_planes, planes, kernel_size=3, stride=stride, padding=1, bias=False),
            nn.BatchNorm2d(planes),
            nn.ReLU(),
            nn.Conv2d(planes, planes, kernel_size=3, stride=1, padding=1, bias=False),
            nn.BatchNorm2d(planes)
        )
        self.shortcut = nn.Sequential()
        if stride != 1 or in_planes != planes:
            self.shortcut = nn.Sequential(
                nn.Conv2d(in_planes, planes, kernel_size=1, stride=stride, bias=False),
                nn.BatchNorm2d(planes)
            )

    def forward(self, x):
        return torch.relu(self.conv(x) + self.shortcut(x))

class AMCNet2D(nn.Module):
    def __init__(self, num_classes):
        super().__init__()
        self.prep = nn.Sequential(
            nn.Conv2d(1, 32, kernel_size=3, stride=1, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU()
        )
        self.layer1 = BasicBlock2D(32, 64, stride=2)  # 32x32
        self.layer2 = BasicBlock2D(64, 128, stride=2) # 16x16
        self.layer3 = BasicBlock2D(128, 256, stride=2)# 8x8
        
        self.pool = nn.AdaptiveAvgPool2d(1)
        self.fc = nn.Sequential(
            nn.Linear(256, 128),
            nn.ReLU(),
            nn.Dropout(0.3),
            nn.Linear(128, num_classes)
        )

    def forward(self, x):
        x = self.prep(x)
        x = self.layer1(x)
        x = self.layer2(x)
        x = self.layer3(x)
        x = self.pool(x).view(x.size(0), -1)
        return self.fc(x)

# ==========================================
# 4. 训练主程序
# ==========================================
if __name__ == "__main__":
    full_dataset = ConstellationDataset(DATA_DIR, img_size=IMG_SIZE, iq_range=IQ_RANGE)
    mods = full_dataset.mods
    
    train_size = int(0.8 * len(full_dataset))
    test_size = len(full_dataset) - train_size
    train_set, test_set = random_split(full_dataset, [train_size, test_size])

    train_loader = DataLoader(train_set, batch_size=BATCH_SIZE, shuffle=True, num_workers=4, pin_memory=True)
    test_loader = DataLoader(test_set, batch_size=BATCH_SIZE, shuffle=False, num_workers=2)

    model = AMCNet2D(len(mods)).to(DEVICE)
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
    dummy_input = torch.randn(1, 1, IMG_SIZE, IMG_SIZE).to(DEVICE)
    torch.onnx.export(model, dummy_input, onnx_path, 
                      input_names=['input'], 
                      dynamic_axes={'input': {0: 'batch_size'}})
    
    print(f"模型训练完成。")
    print(f"权重文件已保存至: {model_path}")
    print(f"ONNX文件已导出至: {onnx_path}")
