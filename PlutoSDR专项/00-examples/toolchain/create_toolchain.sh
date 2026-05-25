#!/bin/bash

# ==========================================
# 1. 自动检测宿主机硬件架构
# ==========================================
HOST_ARCH=$(uname -m)

echo "=> 当前检测到宿主机架构为: ${HOST_ARCH}"

# ==========================================
# 2. 根据架构差异化准备 gcc-linaro 工具链
# ==========================================
if [ "${HOST_ARCH}" = "x86_64" ]; then
    echo "=> [x86_64 平台] 开始从 ARM 官网下载标准交叉工具链..."
    wget https://developer.arm.com/-/cdn-downloads/permalink/legacy-linaro-gnu-toolchains/7.3-2018.05/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
    
    echo "=> 正在解压..."
    tar -xJf gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
    rm gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
    echo "=> x86_64 工具链准备就绪。"

elif [ "${HOST_ARCH}" = "aarch64" ]; then
    echo "=> [aarch64 平台] 检测到本地备份，开始解压 Docker 伪装工具链..."
    if [ ! -f "software/gcc-linaro-7.3.1-2018.05-aarch64_arm-linux-gnueabihf.zip" ]; then
        echo "❌ 错误: 未在 software/ 目录下找到预包好的 zip 文件！"
        exit 1
    fi
    
    # 解压 zip（它内部包含名为 gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf 的目录）
    unzip -q software/gcc-linaro-7.3.1-2018.05-aarch64_arm-linux-gnueabihf.zip
    
    # 强制赋予解压出来的替身脚本和管理脚本执行权限
    chmod +x gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/*
    echo "=> aarch64 伪装工具链部署完成。"

else
    echo "❌ 错误: 暂不支持的架构类型 ${HOST_ARCH}"
    exit 1
fi

# ==========================================
# 3. 公共部分：下载并准备 libiio 库 (两端一致)
# ==========================================
echo "=> 开始准备 libiio 依赖库..."
# 如果目录已存在，先清理，防止 wget 覆盖叠加
rm -rf libiio

mkdir libiio
wget https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz
tar -xzf libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz -C libiio
rm -rf libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz

echo "OvO 所有环境组件初始化成功！"
