# 0. 定义资源路径
# 定义工具链根目录 
TOOLCHAIN_PATH  = $(CURDIR)/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
SYSROOT_PATH    = ../toolchain/sysroot
IIO_BASE        = ../toolchain/libiio

# 2. 拼接出具体的编译器和工具
CC      = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-gcc
CXX     = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-g++
STRIP   = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-strip

# 3. 编译选项
# -I 指定头文件搜索路径
# -L 指定库文件搜索路径
# -liio 链接 libiio 库
# --sysroot 强制约束编译器和链接器到目标板的 sysroot 环境中，彻底解决 GLIBC 版本不匹配问题
# -Wl,--allow-shlib-undefined [偷懒做法] 并告诉链接器只链接 libiio，“不要管那些找不到的子符号”
# 添加 -lm 链接数学库
# 添加 -O3 优化以确保解调实时性
# 【关键优化】：注入硬件架构参数，激活 NEON 向量加速引擎
CFLAGS  = -I$(IIO_BASE)/usr/include \
          --sysroot=$(SYSROOT_PATH) \
          -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
LDFLAGS = --sysroot=$(SYSROOT_PATH) \
          -L$(SYSROOT_PATH)/lib \
          -L$(SYSROOT_PATH)/usr/lib \
          -L$(IIO_BASE)/usr/lib/arm-linux-gnueabihf \
          -L$(IIO_BASE)/lib/arm-linux-gnueabihf \
          -lm \
          -liio -Wl,--allow-shlib-undefined

TARGET = ad9361-iiostream

# 4. 编译规则
build:
	$(CC) ad9361-iiostream.c -o ad9361-iiostream $(CFLAGS) $(LDFLAGS)
	# 可选：减小二进制体积，方便传输到 PlutoSDR
	$(STRIP) ad9361-iiostream

clean:
	rm -rf $(TARGET)

run:
	scp -O $(TARGET) root@pluto.local:~/
	ssh -t root@pluto.local "chmod +x ~/$(TARGET); /bin/sh"

