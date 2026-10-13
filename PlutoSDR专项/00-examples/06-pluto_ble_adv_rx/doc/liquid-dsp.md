# ADALM-PLUTO 纯 C++ 接收机集成 liquid-dsp 实现 GFSK/蓝牙广播接收指南

在 ADALM-PLUTO 上实现低功耗蓝牙（BLE）广播包的高性能纯 C++ 接收机时，由于 10M 采样率下的原始 IQ 过采样信号会产生时钟漂移和相位抖动，直接滑动相关会导致相关系数卡在 `0.25` 左右。

通过引入开源轻量级数字信号处理库 **`liquid-dsp`**，利用其内置的数字锁相环（PLL）与符号时钟同步机制（Symbol Clock Recovery），可以完美复现 GNU Radio 流程图中 `GFSK Demod` 的高性能解调效果。

---

## 一、 `liquid-dsp` 交叉编译指南

采用标准 Autotools 机制，将源码针对 Linaro 交叉编译工具链（`arm-linux-gnueabihf`）进行编译并安装：

```bash
# 1. 生成配置脚本（若源码目录无 configure）
./bootstrap.sh

# 2. 运行交叉编译配置
CC=/home/btfz/Desktop/PLUTOSDR/iio/toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc \
CFLAGS="-O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fPIC" \
./configure \
    --host=arm-linux-gnueabihf \
    --prefix=/home/btfz/Desktop/PLUTOSDR/iio/toolchain/liquid-dsp

# 3. 编译并安装
make -j4
make install

```

---

## 二、 核心 Makefile 配置（静态链接 `libliquid`）

在嵌入式开发中，为了避免在 Pluto 开发板上频繁拷贝动态库 `.so` 文件，推荐将 `liquid-dsp` 采用**静态链接方式**（直接链接 `libliquid.a`）打包进二进制程序中。

修改后的项目 `Makefile` 配置片段：

```makefile
# 1. 路径定义
TOOLCHAIN_PATH = $(CURDIR)/../toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
IIO_BASE = $(CURDIR)/../toolchain/libiio
LIQUID_BASE = $(CURDIR)/../toolchain/liquid-dsp
LIQUID_LIB_FILE = $(LIQUID_BASE)/lib/libliquid.a

# 2. 编译器
CC      = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-gcc
CXX     = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-g++
STRIP   = $(TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-strip

# 3. 编译与链接选项（显式静态引用 libliquid.a，开启 NEON 硬件加速）
CFLAGS  = -I$(IIO_BASE)/usr/include \
          -I$(LIQUID_BASE)/include \
          -O3 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard

LDFLAGS = -L$(IIO_BASE)/usr/lib/arm-linux-gnueabihf \
          -L$(IIO_BASE)/lib/arm-linux-gnueabihf \
          $(LIQUID_LIB_FILE) \
          -liio -lm -Wl,--allow-shlib-undefined

TARGET = pluto_ble_adv_rx
FREQ   ?= 2402
SAMPLE ?= 10

build:
	$(CC) $(TARGET).c ble_phy.c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	scp -O $(TARGET) root@pluto.local:~/
	ssh -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET) -f $(FREQ) -s $(SAMPLE)"

```

---

## 三、 在 C++ 中调用 `liquid-dsp` 进行 GFSK 解调

在 `ble_phy.c` 中，引入 `liquid/liquid.h` 并初始化 GMSK/GFSK 解调器，替代手写的原始差分逻辑：

```c
#include <liquid/liquid.h>
#include "ble_phy.h"

// 实例化解调器：sps = 10 (10M采样率下每符号10个点), bt = 0.5 (BLE规范)
static gmskmdemod q = NULL;

void init_demodulator() {
    if (!q) {
        q = gmskdemod_create(10, 0.5f, 2);
    }
}

void demodulate_ble_packets(const int16_t *iq_samples, int num_samples, int channel) {
    init_demodulator();

    for (int i = 0; i < num_samples; i++) {
        // 将 Pluto 的 int16_t IQ 转换为 liquid 所需的 float complex
        float complex z = (float)iq_samples[2 * i] + _Complex_I * (float)iq_samples[2 * i + 1];
        
        // 内部自动完成时钟同步、鉴频与比特判决
        // int bit = gmskdemod_demodulate(q, z);
        // ... 后续同步头匹配与包解析逻辑 ...
    }
}

```
