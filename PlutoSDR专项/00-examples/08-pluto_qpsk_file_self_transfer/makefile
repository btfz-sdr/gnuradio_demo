# ============================================================
# QPSK 文件自收发（PlutoSDR 真机）
# 工程根目录：本 makefile 所在目录（10-pluto_qpsk_file_self_transfer）
# 所有路径均相对本目录，迁移整个工程目录即可。
# ============================================================

# 0. 资源路径（相对工程根目录向上找 toolchain）
TOOLCHAIN_PATH = $(CURDIR)/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
IIO_BASE       = $(CURDIR)/../toolchain/libiio
LIQUID_BASE    = $(CURDIR)/../toolchain/liquid-dsp
LIQUID_LIB_FILE = $(LIQUID_BASE)/lib/libliquid.a

# 1. 编译器/工具
CC      = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-gcc
STRIP   = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-strip

# 2. 编译选项
#    -march/-mcpu/-mfpu 激活 cortex-a9 NEON 加速，保证 symsync 实时性
#    -Wl,--allow-shlib-undefined 只链接 libiio，忽略其子依赖缺失（部署时由板子补齐）
CFLAGS  = -I$(IIO_BASE)/usr/include \
          -I$(LIQUID_BASE)/include \
          -Wall -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
LDFLAGS = -L$(IIO_BASE)/usr/lib/arm-linux-gnueabihf \
          -L$(IIO_BASE)/lib/arm-linux-gnueabihf \
          $(LIQUID_LIB_FILE) \
          -liio -lm -Wl,--allow-shlib-undefined

TARGET  = pluto_qpsk_file_self_transfer
PLUTO  ?= root@pluto.local

# 真机运行参数（可用命令行覆盖）
IN_FILE  ?= test.png
OUT_FILE ?= recovered.png
LO_MHZ   ?= 2500
SAMP_MHZ ?= 2.5
BW_MHZ   ?= 2.0
RX_GAIN  ?= 40
TX_ATTN  ?= 10

RUN_ARGS = -i $(IN_FILE) -o $(OUT_FILE) -l $(LO_MHZ) -s $(SAMP_MHZ) -b $(BW_MHZ) -g $(RX_GAIN) -a $(TX_ATTN)

# 3. 编译规则
build:
	$(CC) $(TARGET).c qpsk_phy.c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

# 4. 上机（先编译，scp 二进制 + 测试文件到板子，再 ssh 运行）
run: clean build
	scp -O $(TARGET) $(PLUTO):~/
	scp -O doc/01-simulation/$(IN_FILE) $(PLUTO):~/$(IN_FILE)
	@echo "------------------------------------------------"
	ssh -t $(PLUTO) "chmod +x ~/$(TARGET); ./$(TARGET) $(RUN_ARGS)"

# 5. 宿主机仿真（用 doc/01-simulation 里的单文件版验证算法）
sim:
	gcc -Wall -O2 -o doc/01-simulation/industrial_pipeline4 \
	    doc/01-simulation/industrial_pipeline4.c -lm -lc -lliquid

# 6. 宿主机仿真回归测试（编译 + 运行 + 比对 recovered.png）
simtest:
	bash tools/sim_test.sh

.PHONY: build clean run sim simtest
