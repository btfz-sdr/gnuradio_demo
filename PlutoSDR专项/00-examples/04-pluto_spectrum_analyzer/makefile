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

TARGET = pluto_spectrum_analyzer

# 使用 ?= 允许命令行传参覆盖。若命令行未指定，则使用默认值
# make run FREQ=100.0 SAMPLE=4
IP     ?= 192.168.2.10
PORT   ?= 1234
FREQ   ?= 93
SAMPLE ?= 10

# 4. 编译规则
build:
	# 联合编译当前目录下的 kiss_fft 基础组件
	$(CC) $(TARGET).c kiss_fft.c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	scp -O $(TARGET) root@pluto.local:~/
	@echo "------------------------------------------------"
	@echo " |  __ \| |     | |       / ___|                            | |"
	@echo " | |__) | |_   _| |_ ___  \___ \  _ __   ___  ___ |_   _ __ | |"
	@echo " |  ___/| | | | | __/ _ \  ___) || '_ \ / _ \/ __| __| '__| | |"
	@echo " | |    | | |_| | || (_) ||____/ | |_) |  __/ (__ | |_| |   |_|"
	@echo " |_|    |_|\__,_|\__\___/        | .__/ \___|\___| \__|_|   (_)"
	@echo "                                 | |"
	@echo "                                 |_|"
	@echo "Tips: On your PC, use Python or GNU Radio to receive"
	@echo "1024 float points via UDP port 1234 to plot waterfall."
	@echo "python pluto_spec_viewer.py -s $(SAMPLE) -i \"$(IP)\" -p $(PORT)"
	@echo "------------------------------------------------"
	ssh -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET) -f $(FREQ) -s $(SAMPLE) -i $(IP) -p $(PORT)"
