# 1. 定义工具链根目录 (使用绝对路径)
TOOLCHAIN_PATH = $(CURDIR)/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf

# 2. 拼接出具体的编译器和工具
CC      = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-gcc
CXX     = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-g++
STRIP   = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-strip

# 3. 编译规则
build:
	$(CC) hello_plutosdr.c -o hello_plutosdr
	# 可选：减小二进制体积，方便传输到 PlutoSDR
	$(STRIP) hello_plutosdr

clean:
	rm -rf hello_plutosdr

run:
	scp -O hello_plutosdr root@pluto.local:~/
	ssh root@pluto.local "chmod +x ~/hello_plutosdr && ~/hello_plutosdr"

