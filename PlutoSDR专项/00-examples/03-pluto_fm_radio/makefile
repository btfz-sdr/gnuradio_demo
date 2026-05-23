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
CFLAGS  = -I$(IIO_BASE)/usr/include -O3
LDFLAGS = -L$(IIO_BASE)/usr/lib/arm-linux-gnueabihf \
          -L$(IIO_BASE)/lib/arm-linux-gnueabihf \
          -liio -lm -Wl,--allow-shlib-undefined

TARGET = pluto_fm_radio

# 4. 编译规则
build:
	$(CC) $(TARGET).c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	scp -O $(TARGET) root@pluto.local:~/
	@echo "------------------------------------------------"
	@echo "Tips: On your PC, run this to hear sound:"
	@echo "ffplay -f s16le -ar 48000  udp://@:1234"
	@echo "ffplay -nodisp -f s16le -ar 48000 -ch_layout mono -fflags nobuffer -flags low_delay udp://192.168.1.119:1234"
	@echo "ffplay -nodisp -f s16le -ar 48000 -ch_layout mono -framedrop -async 1 -fflags nobuffer udp://192.168.1.119:1234"
	@echo "------------------------------------------------"
	ssh -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET)"
