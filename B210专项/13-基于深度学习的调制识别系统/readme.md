### 一、神经网络架构

神经网络架构的演进本质上是 **“如何更有效地提取特征”** 和 **“如何让深层网络更容易训练”** 的过程。

为了让你看清全貌，我将神经网络划分为**五大核心家族**。它们有的擅长“看”（空间），有的擅长“听”（时间），有的擅长“思考”（全局）。

#### 1. 神经网络架构全家族对比表

| 家族系列 | 代表模型 | 核心创新点 | 现实世界应用 | 在 IQ 信号识别中的隐喻 |
| --- | --- | --- | --- | --- |
| **卷积家族 (CNN)** | LeNet, VGG, **ResNet**, EfficientNet | **局部感知**。利用卷积核平移捕捉局部特征（边缘、形状）。 | 刷脸解锁、自动驾驶、物体检测。 | 像放大镜，观察星座图上点的分布距离和边缘。 |
| **序列家族 (RNN)** | **LSTM**, GRU, Bi-RNN | **记忆门控**。允许信息在时间轴上传递，处理变长序列。 | 语音输入法、股票预测、机器翻译。 | 像磁带，记录相位随时间旋转的连续轨迹。 |
| **注意力家族 (Transformer)** | **BERT, GPT**, ViT, Swin | **全局关联**。通过 Self-Attention 算出数据中各元素间的相关性。 | 聊天机器人 (ChatGPT)、长文分析。 | 像雷达，直接锁定整个信号帧中最关键的特征点。 |
| **生成家族 (GAN/Diffusion)** | CycleGAN, **Stable Diffusion** | **对抗/去噪迭代**。从无到有生成高保真数据。 | AI 绘画、照片修复、虚拟人。 | 像画师，根据规律生成完美的 IQ 信号样本。 |
| **轻量化家族 (Mobile)** | **MobileNet**, ShuffleNet | **结构拆分**。将复杂计算拆解为低成本的小计算。 | 手机端美颜、无人机避障。 | 像缩微模型，让算法能跑在低功耗的单片机上。 |

</br>

#### 2. 深入了解各家族的“独门绝技”

**1. 卷积家族：从简单到深邃**

* **VGG/AlexNet**：早期的“砖块”堆叠。
* **ResNet**：引入了“捷径”（Skip Connection）。正如你代码中所用，它解决了深层网络“越练越废”的问题。
* **DenseNet**：把每一层都和后面连起来。如果说 ResNet 是“接力跑”，DenseNet 就是“全员大合唱”。

**2. 序列家族：为了不遗忘**

* **LSTM (长短期记忆)**：通过“遗忘门”决定哪些信息该丢。在处理像 WBFM 这种频率随时间缓慢变化的信号时，它比 CNN 更有优势。

**3. 注意力家族：目前的“统治者”**

* **Transformer**：它彻底抛弃了卷积和循环。它认为：如果你想知道第 1 个采样点的意义，你应该直接去看它和第 256 个点之间的关系，而不是中间一个一个传过去。
* **Vision Transformer (ViT)**：证明了把图片切成块当成“单词”看，效果竟然比卷积还好。

**4. 特殊架构家族**

* **GNN (图神经网络)**：专门处理非欧几里得数据（如社交网络、化学分子）。如果 IQ 信号采样点之间有复杂的逻辑拓扑，GNN 就能派上用场。
* **Autoencoder (自编码器)**：先压缩再还原。常用于信号降噪——输入带噪 IQ，输出纯净 IQ。

</br>

#### 3. 无线电领域常用的深度学习模型

| 架构名称 | 核心机制 | 原始设计领域 | 引入无线电信号（IQ）的逻辑 | 适用场景 / 优缺点 |
| --- | --- | --- | --- | --- |
| **ResNet (残差网络)** | 残差连接 (Skip Connection) | 计算机视觉 (CV) | 解决深层网络训练时 IQ 特征消失的问题，保留原始相位细节。 | **当前首选**。通用性强，性能稳定，适合大多数调制识别任务。 |
| **CLDNN** | CNN + LSTM + DNN 串联 | 语音识别 (ASR) | CNN 降噪提纯，LSTM 处理 IQ 随时间演变的相位轨迹，DNN 分类。 | **性能巅峰**。尤其适合低信噪比 (Low SNR) 和复杂时变信道。 |
| **Transformer** | 自注意力 (Self-Attention) | 自然语言处理 (NLP) | 将 IQ 序列看作“句子”，捕捉长距离码元之间的全局依赖关系。 | **大模型趋势**。适合极长序列（如协议帧分析），但计算开销巨大。 |
| **LSTM / GRU** | 门控状态 (Gating) | 翻译、预测 | 专门针对 IQ 信号的**时间连续性**建模，模拟解调器的状态机。 | **鲁棒型**。对频率偏移、时间偏移有较强的容忍度，但训练慢。 |
| **DenseNet** | 密集连接 (Concatenation) | 医疗影像、CV | 每一层都与后面所有层连接，极致利用每一条 IQ 原始采样点。 | **参数高效型**。比 ResNet 更小巧，特征重用率高，适合精细分类。 |
| **MobileNet** | 深度可分离卷积 | 移动端 CV | 极度压缩参数量，让 AMC 模型能跑在嵌入式 SDR 终端或手机上。 | **边缘计算**。牺牲 1-2% 的精度来换取极高的推理速度。 |
| **U-Net** | 编码-解码 + 跳跃连接 | 医学图像分割 | 将 IQ 变换为谱图（Spectrogram）后，进行信号提取或降噪。 | **信号增强**。适用于从嘈杂频谱中“抠出”有用信号，做信号清洗。 |
| **GAN (生成对抗网络)** | 判别器与生成器博弈 | 图像生成、换脸 | 模拟生成虚假的、带各种干扰的 IQ 信号，用于扩充训练数据集。 | **数据增强**。当你某种调制类型的样本很少时，可以用它造数据。 |

</br>

### 二、Arch Linux 下 Miniconda 配置流程

#### 1. 安装与基础初始化

首先通过 AUR 安装，并直接将初始化脚本挂载到 Shell：

```bash
yay -S miniconda3

# 自动挂载 Conda 函数到 zsh (如果你用的是 bash 需要到 ~/.bashrc)
echo "[ -f /opt/miniconda3/etc/profile.d/conda.sh ] && source /opt/miniconda3/etc/profile.d/conda.sh" >> ~/.zshrc
```

#### 2. 彻底解决 `encodings` 报错 (优化项)

报错的核心是系统环境变量 `PYTHONHOME` 干扰。与其每次手动 `unset`，不如直接在 `.zshrc` 中**确保**它在进入 Conda 时是干净的：

```bash
# 在 ~/.zshrc 中追加这一行，确保启动 Conda 前清理干扰变量
alias conda='unset PYTHONPATH; unset PYTHONHOME; conda'
```

*这样你以后直接输入 `conda` 命令时，它会自动帮你清理掉那两个“罪魁祸首”环境变量。*

#### 3. 授权与频道优化

跳过繁琐的多次 `tos accept`，建议直接配置一次并切换到更友好的频道：

```bash
# 1. 接受条款
conda tos accept --channel https://repo.anaconda.com/pkgs/main
conda tos accept --channel https://repo.anaconda.com/pkgs/r

# 2. (进阶) 强烈建议添加社区源，包更新更快且无商业争议
conda config --add channels conda-forge
conda config --set channel_priority strict

```

#### 4. 消除 OpenSSL 警告（深度优化）

这个警告是因为 Arch Linux 使用的是 OpenSSL 3.x，而 Miniconda 内部还在找旧版的 Provider。 解决方法：在你的 `(base)` 环境下安装 `conda-forge` 提供的最新 `openssl` 包，这通常能解决版本不匹配的问题：

```bash
sudo conda install -n base conda-forge::openssl -y
```

#### 5. 配置镜像（加速下载）

```
# 添加清华镜像源
# conda config --add channels https://mirrors.tuna.tsinghua.edu.cn/anaconda/pkgs/main/
# conda config --add channels https://mirrors.tuna.tsinghua.edu.cn/anaconda/cloud/conda-forge/
conda config --set show_channel_urls yes

# 国内清华、阿里镜像都不行，采用下面命令切回原始的
conda config --remove-key channels
```

#### 6. 验证创建环境

现在你可以丝滑地创建环境了：

```bash
conda create -n amc_v100 python=3.10 -y

...

Downloading and Extracting Packages:
                                                                                
Preparing transaction: done                                                     
Verifying transaction: done                                                     
Executing transaction: done                                                     
#                                                                               
# To activate this environment, use                                             
#                                                                               
#     $ conda activate amc_v100
#
# To deactivate an active environment, use
#
#     $ conda deactivate
```

**小贴士：** 创建成功后，记得用 `conda activate amc_v100` 进入环境。如果在环境里运行程序还报 OpenSSL 的错，在该环境下再执行一次 `conda install openssl` 即可。    

**备注：**    
- `conda env list`：查看所有已安装环境的列表
- `conda activate amc_v100`：激活某环境
- `conda deactivate`：退出当前环境
- `conda remove -n amc_v100 --all`：彻底删除某环境
- `conda clean --all`：清理冗余缓存

</br>

### 三、RadioML 2016.10a 数据集训练

这里我们利用 V100 强大的算力，针对经典的 RadioML 2016.10a 数据集进行训练：

#### 1. 环境准备 (V100 专用)

在终端中，建议创建一个独立的 Conda 环境，并安装支持 CUDA 12.4（或向下兼容）的组件：

```bash
# 创建环境
conda create -n amc_v100 python=3.10 -y
conda activate amc_v100


# 安装 PyTorch (根据你的 CUDA 13.0 情况，通常安装最新的稳定版即可)
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu124

# 安装辅助库
pip install numpy matplotlib h5py pandas
pip install onnx onnxruntime-gpu
```

#### 2. 获取与加载数据

**RML2016.10a.tar.bz2** 从 DeepSig <sup>[1][#1]</sup> 官网比较难下载（总是服务器报错），发现可以从 kaggle <sup>[2][#2]</sup> 模型网站下载。

- **数据集结构：** 它是一个 Python 字典，存放在 .pkl 文件中（大约为 600M）。
- **格式：** {(调制类型, SNR): [N, 2, 128] 的样本数据}。

#### 3. 编写训练脚本

```python
import pickle
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset, random_split
from torch.cuda.amp import GradScaler, autocast
import matplotlib.pyplot as plt

# ==========================================
# 1. 配置与参数
# ==========================================
DATA_PATH = 'RML2016.10a_dict.pkl'
BATCH_SIZE = 1024  # V100 16GB 显存很大，可以设高一些提高吞吐量
EPOCHS = 70
LEARNING_RATE = 0.001
DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# ==========================================
# 2. 数据加载与预处理
# ==========================================
def load_and_process_data(path):
    print("正在加载数据...")
    with open(path, 'rb') as f:
        # RML2016.10a 包含 11 种调制类型
        d = pickle.load(f, encoding='latin1')
    
    mods, snrs = [list(set(k[0] for k in d.keys())), 
                  sorted(list(set(k[1] for k in d.keys())))]
    X = []
    lbl = []
    for mod in mods:
        for snr in snrs:
            X.append(d[(mod, snr)])
            for i in range(d[(mod, snr)].shape[0]):
                lbl.append(mods.index(mod))
    
    X = np.vstack(X)
    X = torch.from_numpy(X).float() # (220000, 2, 128)
    lbl = torch.from_numpy(np.array(lbl)).long()
    
    dataset = TensorDataset(X, lbl)
    train_size = int(0.8 * len(dataset))
    test_size = len(dataset) - train_size
    return random_split(dataset, [train_size, test_size]), mods

# ==========================================
# 3. 定义模型 (ResNet 变体)
# ==========================================
class ResidualBlock(nn.Module):
    def __init__(self, channels):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv1d(channels, channels, kernel_size=3, padding=1),
            nn.BatchNorm1d(channels),
            nn.ReLU(),
            nn.Conv1d(channels, channels, kernel_size=3, padding=1),
            nn.BatchNorm1d(channels)
        )
    def forward(self, x):
        return torch.relu(x + self.conv(x))

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
            nn.Linear(64 * 128, 256),
            nn.ReLU(),
            nn.Dropout(0.5),
            nn.Linear(256, num_classes)
        )

    def forward(self, x):
        x = self.prep(x)
        x = self.layer1(x)
        x = self.layer2(x)
        x = self.flatten(x)
        return self.fc(x)

# ==========================================
# 4. 训练主程序
# ==========================================
if __name__ == "__main__":
    (train_set, test_set), mods = load_and_process_data(DATA_PATH)
    train_loader = DataLoader(train_set, batch_size=BATCH_SIZE, shuffle=True, num_workers=4)
    test_loader = DataLoader(test_set, batch_size=BATCH_SIZE, shuffle=False)

    model = AMCNet(len(mods)).to(DEVICE)
    optimizer = optim.Adam(model.parameters(), lr=LEARNING_RATE)
    criterion = nn.CrossEntropyLoss()
    scheduler = optim.lr_scheduler.ReduceLROnPlateau(optimizer, 'min', patience=5, factor=0.5)
    scaler = GradScaler() # 混合精度

    history = {'train_loss': [], 'test_acc': []}

    print(f"开始在 {DEVICE} 上训练...")
    for epoch in range(EPOCHS):
        model.train()
        total_loss = 0
        for i, (images, labels) in enumerate(train_loader):
            images, labels = images.to(DEVICE), labels.to(DEVICE)
            
            optimizer.zero_grad()
            with autocast(): # V100 混合精度加速
                outputs = model(images)
                loss = criterion(outputs, labels)
            
            scaler.scale(loss).backward()
            scaler.step(optimizer)
            scaler.update()
            
            total_loss += loss.item()

        # 验证
        model.eval()
        correct = 0
        with torch.no_grad():
            for images, labels in test_loader:
                images, labels = images.to(DEVICE), labels.to(DEVICE)
                outputs = model(images)
                _, predicted = torch.max(outputs.data, 1)
                correct += (predicted == labels).sum().item()
        
        acc = 100 * correct / len(test_set)
        avg_loss = total_loss / len(train_loader)
        history['train_loss'].append(avg_loss)
        history['test_acc'].append(acc)
        scheduler.step(avg_loss)
        
        print(f'Epoch [{epoch+1}/{EPOCHS}], Loss: {avg_loss:.4f}, Test Acc: {acc:.2f}%')

    # 保存模型
    torch.save(model.state_dict(), "amc_resnet_v100.pth")
    print("模型已保存为 amc_resnet_v100.pth")

    # 导出为 ONNX (供 GNU Radio 使用)
    dummy_input = torch.randn(1, 2, 128).to(DEVICE)
    torch.onnx.export(model, dummy_input, "amc_model.onnx")
    print("ONNX 模型已导出。")
```

**🔍 代码核心说明**

- **1D Convolution (Conv1d)**：这是处理无线电 I/Q 信号（时间序列）的标准方式。
- **Mixed Precision (autocast)**：这是针对 V100 的“超能力”。它能将大部分运算转化为 FP16 精度，速度飞快且不损失准确率。
- **Batch Size = 1024**：V100 有 16GB 显存，对于这种小型信号模型，大的 Batch Size 能大幅缩短训练时间。

</br>

#### 4. 执行训练与观察

**a）运行脚本：**

```bash
python train_amc.py
```

**b）监控显卡：**  再开一个终端窗口，运行：

```
watch -n 0.5 nvidia-smi
```

你会看到 **Pwr:Usage** 显著上升（V100 满载可达 300W），这说明 Tensor Core 正在全力工作。

一旦训练完成，我们将得到 `amc_model.onnx`。

</br>

### 四、在 Conda 中安装 GNU Radio

在 Conda 环境中安装 GNU Radio 可以彻底解决库路径冲突问题，让我们在一个统一的环境下调用 V100 的 GPU 加速和 B210 的硬件驱动。

以下是在 amc_v100 环境中安装 GNU Radio 的步骤：

#### 1. 在 Conda 中安装 GNU Radio

由于 GNU Radio 依赖较多，建议使用 conda-forge 频道，它提供的二进制包最全且更新最快：

```bash
# 1. 激活环境
conda activate amc_v100

# 2. 安装 GNU Radio 及相关组件
# 包含常用的 uhd (支持 B210) 和 gr-osmosdr
conda install -c conda-forge gnuradio gnuradio-build-deps uhd gnuradio-osmosdr -y

# 3. 图形界面
conda install -c conda-forge qt pyqt
```

**备注：** 之后运行 `gnuradio-config-info --version` 能看到我们安装的 gnuradio 的版本，然后运行 `gnuradio-companion` 能出现 GUI 则说明安装 OK。

#### 2. 配置 UHD (B210 驱动)

在 Conda 中安装的 UHD 需要下载对应的 FPGA 镜像文件，否则 B210 无法工作：

```
# 下载 FPGA 镜像（如果使用非官方的 B210，需要参考我们的《[USRP B210 专项教程/第三集：环境搭建与驱动安装][#8]》，需要进行换固件并最终用：`export UHD_IMAGES_DIR=/home/btfz/Desktop/B210/B210_images` 指定自己电脑上的固件）
uhd_images_downloader

# 验证 Conda 环境下的 UHD 是否能找到 B210
uhd_find_devices
```

**备注：** 如果报错找不到 `libudev.so.0` 需要找到自己电脑上已经有的对应的版本并创建一个软链接：`ln -s /home/btfz/.conda/envs/amc_v100/lib/libudev.so.1 /home/btfz/.conda/envs/amc_v100/lib/libudev.so.0`   

</br>

### 五、编写数据采集与推理二合一 GRC 流程图

先使用 RadioML 2016.10a 数据集训练的模型，再通过 gnuradio 实现一个带多种调制和推理能力的流程图，实验发现效果没那么好，只能识别出一些模拟信号。因此我们干脆一步到位，**实现一个集数据采集、推理于一体的流程图，我们自己采集、自己训练、自己推理！**

#### 1 流程图解析

```
cd grc
gnuradio-companion sig_ai_analy.grc 
```

![][p1]

- 流程图的下半部分负责：'BPSK', 'QPSK', '8PSK', 'QAM16', 'QAM64', 'PAM4' 星座调制信号生成并发送（发送的是 0~256 的随机数据，并增加 RRC 滤波，最终通过 USRP B210 发送）
- 流程图的上半部分负责：通过 USRP B210 接收信号，并通过多项时钟同步恢复基本星座，然后通过 AGC 让信号保持均衡，然后通过选择器，将数据流分为两路（上路：推理 python 块；下路：采集 python 块）

</br>

**1）Universal Modulator**

其中 `Universal Modulator` python 块负责根据 mod_selection 来选择不同的调制，并将输入的随机数据流生成对应调制数据（代码比较简单，就是定义一堆星座映射，然后根据这个映射将输入的随机字节流转换为对应调制）

**2）AMC Universal Collector**

其中 `AMC Universal Collector` python 块负责数据采集：

- 将 AGC 之后的数据按照 `num_items(256)` 个组成一个样本，然后采集 `sample_per_mod(50000)` 次，将每一种调制的数据保存在 `collected_data_dir` 对应的目录下的 bin 文件中。 
- 用户通过 `mode_selection` 切换不同调制时，会触发 Variable to Message，然后通知到该模块的 `handle_mod_msg`，然后启动新的采集（这里为了采集稳定的数据，每次切换前会 delay 一些时间 `wait_time`）   
- 由于需要大量的磁盘操作，这里每隔 `self.buffer_size` 大小调用 `self.flush_buffer()` 保存一次。
- 我们这里在存入数据前增加了峰值归一化，方便后续处理

备注：采集保存路径 `collected_data_dir` 是自动计算拼接的，格式固定，后续一些脚本会根据文件夹的名字识别 `num_items` 和 `sample_per_mod` 信息

**3）AMC Universal Predictor**

其中 `AMC Universal Predictor` 块负责数据推理：

- 通过训练的 `amc_model.onnx` 的模型，针对来的数据流进行分析判定是哪种调制
- 由于处理速度很快，一秒会给出超多的判定，这里用个平滑窗口 `window_size` 对结果实施众数投票逻辑，统计频率最高的类别作为本次的输出
- 推理我直接强行让其用 CPU
- 由于我使用了两种不同的训练模型，因此这里也根据模型的情况，让推理块自动选择使用 1D 模式还是 2D 模式

</br>

#### 2 训练

采样上一节的流程图，分别采集 6 种调制的数据，最终会在 train 中出现 `collected_data_52k_item256` 类似的文件夹，其中包含 `8PSK.bin BPSK.bin PAM4.bin QAM16.bin QAM64.bin QPSK.bin` 采集的样本。

然后在 train 文件夹内执行：

- `python train_1d.py collected_data_52k_item256` 使用 1d 模型进行训练
- `python train_2d.py collected_data_52k_item256` 使用 2d 模型进行训练

**注意：** 训练模型只能二选一，当想要从一种模型切换到第二种模型时需要删除 `collected_data_52k_item256` 下的 `result` 的文件夹。

</br>

这两种训练算法代表了自动调制识别（AMC）领域的两种主流深度学习路径：**时域序列特征流（1D）**和**统计分布图像流（2D）**。

**1）技术架构对比**

| 维度 | **1D 训练算法 (ResNet + SE)** | **2D 训练算法 (Constellation CNN)** |
| --- | --- | --- |
| **数据形态** |  的原始 IQ 时间序列 |  的星座图密度图像 |
| **核心算子** | 一维卷积 (Conv1d) + 通道注意力 (SE) | 二维卷积 (Conv2d) + 残差块 (BasicBlock) |
| **归一化策略** | 逐样本动态峰值归一化 | 密度直方图 + Log 增强 |
| **主要特征** | 捕捉信号的**相位偏移**和**时间关联** | 捕捉信号的**欧氏距离**和**分布几何轮廓** |

</br>

**2）核心逻辑总结**

1D 算法：时域深度提取

* **注意力机制：** 引入了 `SEBlock`。它通过“挤压-激励”操作，自动学习 I 路和 Q 路以及不同特征通道的重要性，能有效抑制噪声干扰。
* **时间敏感性：** 卷积核沿着时间轴滑动，擅长捕捉像 PSK 这种由于相位连续变化产生的波形特征。
* **模型规模：** 相对较轻量，适合处理长序列，推理延迟对序列长度呈线性增长。

2D 算法：空间分布映射

* **密度转化：** 核心在于 `histogram2d`。它抛弃了时间顺序，只关注点在复平面上的落点频率。通过 `log1p` 处理，解决了 QAM 高阶调制中中心点过亮、边缘点过暗的显示问题。
* **空间鲁棒性：** 对采样偏置不敏感，因为只要“星座形状”在，分类就能成功。
* **图像优势：** 利用了成熟的 CV（计算机视觉）架构，识别高阶 QAM（如 16QAM vs 64QAM）的能力通常强于 1D 算法。

</br>

**3）算法优劣及适用场景**

什么时候选 1D (train_1d.py)？

* **低信噪比环境：** 当噪声很大，星座图已经糊成一团时，1D 算法能通过时间相关性挽救一部分准确率。
* **计算资源受限：** 1D 卷积计算量远小于图像卷积。
* **关键特征：** 针对连续相位调制（如 GFSK, MSK），1D 是唯一选择。

什么时候选 2D (train_2d.py)？

* **高阶 QAM 识别：** 当你需要精准区分 QAM16/64/128 时，2D 算法对“格点”几何结构的敏感度极高。
* **频偏/相偏较大：** 如果前端同步做得不好，1D 算法会因为相位旋转导致序列错乱，而 2D 算法只需要在图像上识别“旋转后的圆环或方阵”，容错力更强。

---

这两个脚本都包含了非常出色的 **断点续训** 逻辑和 **ONNX 自动导出** 功能：

1. **内存映射 (memmap)：** 解决了海量 `.bin` 文件（GB 级别）无法一次性塞进内存的痛点。
2. **数据增强：** 统一使用了“随机相位旋转”，这让模型在推理时对天线角度或初始相位完全免疫。
3. **AMP 混合精度：** 紧跟 PyTorch 最新接口（`torch.amp`），在不损失精度的情况下极大加快了显卡训练速度。

</br>

#### 3 模型可视化分析

混淆矩阵 | 样本星座图 | 学习曲线 | 模型流图 | t-SNE 
---|---|---|---|---
visualize_confusion_matrix.py | visualize_constellation.py | visualize_learning_curves.py | visualize_model.py | visualize_tsne.py
![][p2] | ![][p3] | ![][p4] | ![][p5] | ![][p6]

</br>

### 参考链接

[[1]. DeepSig 网站][#1]    
[[2]. kaggle 模型网站][#2]    
[[3]. Blog —— More on DeepSig’s RML Datasets][#3]    
[[4]. Blog —— Using Machine Learning to Classify Radio Signals][#4]    
[[5]. Github —— ModulationRecognition][#5]     
[[6]. Github —— deepsig_datasets][#6]     
[[7]. Github —— Complex_Convolutions][#7]     

[#1]:https://www.deepsig.ai/datasets/      
[#2]:https://www.kaggle.com/datasets/nolasthitnotomorrow/radioml2016-deepsigcom/data         
[#3]:https://cyclostationary.blog/2020/08/17/more-on-deepsigs-rml-data-sets/     
[#4]:https://erichizdepski.wordpress.com/2019/05/      
[#5]:https://github.com/OmerElshrief/ModulationRecognition     
[#6]:https://github.com/sofwerx/deepsig_datasets?tab=readme-ov-file      
[#7]:https://github.com/JakobKrzyston/Complex_Convolutions      
[#8]:https://beautifulzzzz.com/gnuradio/      


[p1]:https://tuchuang.beautifulzzzz.com:3000/?path=202602/sig_ai_analy_grc.png           
[p2]:https://tuchuang.beautifulzzzz.com:3000/?path=202602/result_confusion_matrix.png      
[p3]:https://tuchuang.beautifulzzzz.com:3000/?path=202602/result_constellation_samples.png      
[p4]:https://tuchuang.beautifulzzzz.com:3000/?path=202602/result_learning_curves.png     
[p5]:https://tuchuang.beautifulzzzz.com:3000/?path=202602/result_amcnet_flow2.png      
[p6]:https://tuchuang.beautifulzzzz.com:3000/?path=202602/result_tsne_analysis.png     

 
