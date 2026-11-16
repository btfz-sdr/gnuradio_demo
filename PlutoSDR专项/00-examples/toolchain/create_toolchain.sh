#!/bin/bash

# ==========================================
# 1. 自动检测宿主机硬件架构
# ==========================================
HOST_ARCH=$(uname -m)

echo "=> 当前检测到宿主机架构为: ${HOST_ARCH}"

# 幂等检查：目录存在且非空即视为已就绪
is_ready() {
    [ -d "$1" ] && [ -n "$(ls -A "$1" 2>/dev/null)" ]
}

# ==========================================
# 1.5 检查并自动安装 sshpass 工具
# ==========================================
if ! command -v sshpass &> /dev/null; then
    echo "=> 检测到系统未安装 sshpass，开始自动识别系统并安装..."
    
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS_NAME=$ID
    else
        OS_NAME=$(uname -s)
    fi

    if [[ "$OS_NAME" == "ubuntu" || "$OS_NAME" == "debian" || "$OS_NAME" == "linuxmint" ]]; then
        echo "=> 检测到 Ubuntu/Debian 系列系统，使用 apt 安装 sshpass..."
        sudo apt-get update && sudo apt-get install -y sshpass
    elif [[ "$OS_NAME" == "arch" || "$OS_NAME" == "manjaro" ]]; then
        echo "=> 检测到 Arch Linux 系列系统，使用 pacman 安装 sshpass..."
        sudo pacman -S --noconfirm sshpass
    else
        echo "❌ 错误: 无法自动识别您的发行版 ($OS_NAME)，请手动安装 sshpass 后重试："
        echo "   Ubuntu/Debian: sudo apt-get install sshpass"
        echo "   Arch Linux:    sudo pacman -S sshpass"
        exit 1
    fi
    
    # 再次验证是否安装成功
    if ! command -v sshpass &> /dev/null; then
        echo "❌ 错误: sshpass 安装失败，请检查网络或手动安装。"
        exit 1
    fi
    echo "=> sshpass 安装成功！"
else
    echo "=> sshpass 已就绪。"
fi

# ==========================================
# 2. 根据架构差异化准备 gcc-linaro 工具链
# ==========================================
if [ "${HOST_ARCH}" = "x86_64" ]; then
    TOOLCHAIN_DIR="gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf"
    
    if is_ready "${TOOLCHAIN_DIR}"; then
        echo "=> [x86_64] 工具链已就绪，跳过下载解压。"
    else
        echo "=> [x86_64 平台] 开始从 ARM 官网下载标准交叉工具链..."
        wget https://developer.arm.com/-/cdn-downloads/permalink/legacy-linaro-gnu-toolchains/7.3-2018.05/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz

        echo "=> 正在解压..."
        tar -xJf gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
        rm gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
        echo "=> x86_64 工具链准备就绪。"
    fi

elif [ "${HOST_ARCH}" = "aarch64" ]; then
    TOOLCHAIN_DIR="armv7-rpi2-linux-gnueabihf"
    
    if is_ready "${TOOLCHAIN_DIR}"; then
        echo "=> [aarch64] 工具链已就绪，跳过克隆与解压部署。"
    else
        echo "=> [aarch64 平台] 开始从 Gitee 克隆工具链仓库..."
        
        # 如果本地残留了同名目录，先安全清理
        rm -rf arm-linux-gnueabihf-aarch64-host
        
        git clone https://gitee.com/btfz-sdr/arm-linux-gnueabihf-aarch64-host.git
        
        if [ ! -d "arm-linux-gnueabihf-aarch64-host" ]; then
            echo "❌ 错误: 克隆 arm-linux-gnueabihf-aarch64-host 仓库失败！"
            exit 1
        fi

        echo "=> 正在解压工具链归档..."
        cd arm-linux-gnueabihf-aarch64-host
        if [ ! -f "arm-linux-gnueabihf-aarch64-host.tar.xz" ]; then
            echo "❌ 错误: 未在仓库中找到 arm-linux-gnueabihf-aarch64-host.tar.xz 文件！"
            exit 1
        fi
        
        tar -xJf arm-linux-gnueabihf-aarch64-host.tar.xz
        cd ..

        # 检查解压后的目标目录是否存在
        if [ ! -d "arm-linux-gnueabihf-aarch64-host/armv7-rpi2-linux-gnueabihf" ]; then
            echo "❌ 错误: 未找到解压后的 armv7-rpi2-linux-gnueabihf 目录！"
            exit 1
        fi

        # 赋予解压出来的目录写权限，防止 mv 报 Permission denied
        chmod +w arm-linux-gnueabihf-aarch64-host/armv7-rpi2-linux-gnueabihf

        # 直接把解压出来的目录移动到当前根目录下（保留原名 armv7-rpi2-linux-gnueabihf）
        mv arm-linux-gnueabihf-aarch64-host/armv7-rpi2-linux-gnueabihf ./

        # 清理克隆下来的整个临时仓库目录
        rm -rf arm-linux-gnueabihf-aarch64-host

        # 赋予执行权限
        chmod +x "${TOOLCHAIN_DIR}/bin/"*
        echo "=> aarch64 伪装工具链部署完成。"
    fi

else
    echo "❌ 错误: 暂不支持的架构类型 ${HOST_ARCH}"
    exit 1
fi

# ==========================================
# 3. 公共部分：下载并准备 libiio 库 (两端一致)
# ==========================================
if is_ready "libiio"; then
    echo "=> libiio 已就绪，跳过下载解压。"
else
    echo "=> 开始准备 libiio 依赖库..."
    # 目录不存在或为空，安全清理后重建
    rm -rf libiio

    mkdir libiio
    wget https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz
    tar -xzf libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz -C libiio
    rm -rf libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz
fi

# ==========================================
# 4. 公共部分：下载并准备 sysroot v0.38 库
# ==========================================
if is_ready "sysroot"; then
    echo "=> sysroot 已就绪，跳过下载解压。"
else
    echo "=> 开始准备 sysroot 依赖库..."
    # 目录不存在或为空，安全清理后重建
    rm -rf sysroot

    wget https://github.com/analogdevicesinc/plutosdr-fw/releases/download/v0.38/sysroot-v0.38.tar.gz
    tar -xzf sysroot-v0.38.tar.gz
    mv staging sysroot
    rm -rf sysroot-v0.38.tar.gz
fi

echo "OvO 所有环境组件初始化成功！"
