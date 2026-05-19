# 0. 定义资源路径
# 定义工具链根目录 
TOOLCHAIN_PATH = $(CURDIR)/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
# 定义 libiio 路径 
IIO_BASE = $(CURDIR)/../toolchain/libiio

# 2. 拼接出具体的编译器和工具
CC      = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-gcc
CXX     = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-g++
STRIP   = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-strip

# 3. 编译选项
# -I 指定头文件搜索路径
# -L 指定库文件搜索路径
# -liio 链接 libiio 库
# -Wl,--allow-shlib-undefined [偷懒做法] 并告诉链接器只链接 libiio，“不要管那些找不到的子符号”
CFLAGS  = -I$(IIO_BASE)/usr/include
LDFLAGS = -L$(IIO_BASE)/usr/lib/arm-linux-gnueabihf \
          -L$(IIO_BASE)/lib/arm-linux-gnueabihf \
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

