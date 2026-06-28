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
# 添加 -lm 链接数学库
# 添加 -O3 优化以确保解调实时性
# 【关键优化】：注入硬件架构参数，激活 NEON 向量加速引擎
CFLAGS  = -I$(IIO_BASE)/usr/include -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
LDFLAGS = -L$(IIO_BASE)/usr/lib/arm-linux-gnueabihf \
          -L$(IIO_BASE)/lib/arm-linux-gnueabihf \
          -liio -lm -Wl,--allow-shlib-undefined

TARGET = pluto_adsb_receiver

# 使用 ?= 允许命令行传参覆盖。若命令行未指定，则使用默认值
# make run FREQ=1090 SAMPLE=10
IP     ?= 192.168.2.10
PORT   ?= 1234
FREQ   ?= 1090
SAMPLE ?= 4

# 4. 编译规则
build:
	$(CC) $(TARGET).c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	scp -O $(TARGET) root@pluto.local:~/
	@echo "------------------------------------------------"
	@echo "   ____  _   _ ____  _____ ____     _           _   "
	@echo "  |  _ \\| | | |  _ \\| ____|  _ \\   / \\   _ __ | |_ "
	@echo "  | |_) | | | | | | |  _| | |_) | / _ \\ | '_ \\| __|"
	@echo "  |  __/| |_| | |_| | |___|  _ < / ___ \\| | | | |_ "
	@echo "  |_|    \\___/|____/|_____|_| \\_/_/   \\_\\_| |_|\\__|"
	@echo "                                 by beautifulzzzz"
	@echo "Tips: On your PC, run Python to receive decoded"
	@echo "ADS-B messages via UDP port $(PORT):"
	@echo "python pluto_adsb_viewer.py --ip $(IP) --port $(PORT)"
	@echo "------------------------------------------------"
	ssh -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET) -f $(FREQ) -s $(SAMPLE) -i $(IP) -p $(PORT)"
