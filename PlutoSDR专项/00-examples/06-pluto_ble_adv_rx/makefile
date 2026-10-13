# 0. 定义资源路径
# 定义工具链根目录 
TOOLCHAIN_PATH = $(CURDIR)/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
# 定义 libiio 路径 
IIO_BASE = ../toolchain/libiio

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

TARGET  = pluto_ble_adv_rx
PLUTO   = root@pluto.local
# 网络 IIO 地址：抓 IQ 用，不需要登录 Pluto
URI    ?= ip:pluto.local

# 使用 ?= 允许命令行传参覆盖。若命令行未指定，则使用默认值
# make run CH=38 SAMPLE=10
CH     ?= 37
SAMPLE ?= 10
# FREQ 一般不填：-c 已经决定了中心频率；只有想偏离标准信道时才显式给
FREQ   ?=

RUN_ARGS = -c $(CH) -s $(SAMPLE)
ifneq ($(FREQ),)
RUN_ARGS += -f $(FREQ)
endif

# 4. 编译规则
build:
	$(CC) $(TARGET).c ble_phy.c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET) ble_replay

run: clean build 
	scp -O $(TARGET) $(PLUTO):~/
	@echo "------------------------------------------------"
	@echo " ____  _          _____ _           _ "
	@echo "| __ )| | ___    | ____| |_   _____| |"
	@echo "|  _ \\| |/ _ \\   |  _| | \\ \\ / / _ \\ |"
	@echo "| |_) | |  __/   | |___| |\\ V /  __/ |"
	@echo "|____/|_|\\___|   |_____|_| \\_/ \\___|_|"
	@echo "                                         "
	@echo "------------------------------------------------"
	ssh -t $(PLUTO) "chmod +x ~/$(TARGET); ./$(TARGET) $(RUN_ARGS)"

# ---------------------------------------------------------------- 离线工具
# 5. 离线回放：用宿主机 gcc 编译，把录到的 IQ 文件喂给同一个 ble_phy.c
#    make replay FILE=ch37.bin CH=37
ble_replay: ble_replay.c ble_phy.c ble_phy.h
	gcc -O3 -Wall -o ble_replay ble_replay.c ble_phy.c -lm

replay: ble_replay
	./ble_replay $(FILE) $(CH)

# 6. 抓 IQ 到文件（走网络 IIO，无需 SSH 密码；manual 增益避免削顶）
#    make capture CH=37 SEC=2 FILE=ch37.bin
SEC  ?= 2
GAIN ?= 60
FILE ?= ch$(CH).bin
# 采样点数 = SEC * 10 Msps
NSAMP = $(shell echo $$(( $(SEC) * 10000000 )))
# 信道 -> 中心频率 (Hz)
FREQ_HZ = $(if $(filter 37,$(CH)),2402000000,$(if $(filter 38,$(CH)),2426000000,2480000000))

capture:
	-@iio_attr -u $(URI) -c ad9361-phy voltage0 gain_control_mode manual 2>/dev/null
	-@iio_attr -u $(URI) -c ad9361-phy voltage0 hardwaregain $(GAIN) 2>/dev/null
	@echo -n "  当前增益: "; iio_attr -u $(URI) -c ad9361-phy voltage0 hardwaregain 2>/dev/null | head -1
	@iio_attr -u $(URI) -c ad9361-phy altvoltage0 frequency $(FREQ_HZ) 2>/dev/null
	iio_readdev -u $(URI) -b 262144 -s $(NSAMP) cf-ad9361-lpc voltage0 voltage1 > $(FILE)
	@ls -l $(FILE)
	@echo "回放: make replay FILE=$(FILE) CH=$(CH)"

.PHONY: build clean run replay capture
