# 获取当前 toolchain.mk 文件所在的目录路径
TOOL_PATH := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))


# 1. 定义资源路径
# 定义工具链根目录
SYSROOT_PATH    = $(TOOL_PATH)/sysroot
IIO_BASE        = $(TOOL_PATH)/libiio
LIQUID_BASE     = $(TOOL_PATH)/liquid-dsp
LIQUID_LIB_FILE = $(LIQUID_BASE)/lib/libliquid.a

# 2. 定义 gcc
HOST_ARCH ?= $(shell uname -m)
ifeq ($(HOST_ARCH),x86_64)
    TOOLCHAIN_PATH = $(TOOL_PATH)/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
    CC             = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-gcc
    CXX            = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-g++
    STRIP          = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-strip
else ifeq ($(HOST_ARCH),aarch64)
    TOOLCHAIN_PATH = $(TOOL_PATH)/armv7-rpi2-linux-gnueabihf
    CC             = $(TOOLCHAIN_PATH)/bin/armv7-rpi2-linux-gnueabihf-gcc
    CXX            = $(TOOLCHAIN_PATH)/bin/armv7-rpi2-linux-gnueabihf-g++
    STRIP          = $(TOOLCHAIN_PATH)/bin/armv7-rpi2-linux-gnueabihf-strip
else
    $(error ❌ 错误: 暂不支持的宿主机架构: $(HOST_ARCH))
endif

# 3. 编译选项
# -I 指定头文件搜索路径
# -L 指定库文件搜索路径
# -liio 链接 libiio 库
# -march/-mcpu/-mfpu 激活 cortex-a9 NEON 加速，保证 symsync 实时性
# --sysroot 强制约束编译器和链接器到目标板的 sysroot 环境中，彻底解决 GLIBC 版本不匹配问题
# -Wl,--allow-shlib-undefined [偷懒做法] 并告诉链接器只链接 libiio，“不要管那些找不到的子符号”
# 添加 -lm 链接数学库
# 添加 -O3 优化以确保解调实时性
CFLAGS  = -I$(IIO_BASE)/usr/include \
          -I$(LIQUID_BASE)/include \
          --sysroot=$(SYSROOT_PATH) \
 		  -Wall -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
LDFLAGS = --sysroot=$(SYSROOT_PATH) \
          -L$(SYSROOT_PATH)/lib \
          -L$(SYSROOT_PATH)/usr/lib \
          -L$(IIO_BASE)/usr/lib/arm-linux-gnueabihf \
          -L$(IIO_BASE)/lib/arm-linux-gnueabihf \
          -lm \
          $(LIQUID_LIB_FILE) \
          -liio -Wl,--allow-shlib-undefined

# 4. 带密钥自动进入
# 定义 SSH/SCP 公共选项，避免重复编写
SSH_OPTS = -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
# 如果密码固定，可以定义密码变量
SSH_PASS = analog
