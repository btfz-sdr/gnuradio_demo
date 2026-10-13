// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ble_replay.c - 离线回放工具：把录到的原始 IQ 文件喂给 ble_phy.c 的解调器。
 *
 * 用途：不接 Pluto 也能验证/调试解调算法，或者复现空口抓到的疑难包。
 *
 * 录制（在 Pluto 上或通过网络，manual 增益避免削顶）：
 *   iio_attr -u ip:pluto.local -c ad9361-phy voltage0 gain_control_mode manual
 *   iio_attr -u ip:pluto.local -c ad9361-phy voltage0 hardwaregain 60
 *   iio_attr -u ip:pluto.local -c ad9361-phy altvoltage0 frequency 2402000000
 *   iio_readdev -u ip:pluto.local -b 262144 -s 20971520 \
 *               cf-ad9361-lpc voltage0 voltage1 > ch37.bin
 *
 * 回放：
 *   make replay FILE=ch37.bin CH=37
 * 或直接：
 *   gcc -O3 -o ble_replay ble_replay.c ble_phy.c -lm && ./ble_replay ch37.bin 37
 *
 * 文件格式：交织 int16 小端 IQ (I,Q,I,Q,...)，与 iio_readdev 输出一致。
 */
#include "ble_phy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
                "用法: %s <iq.bin> <channel 37|38|39> [块大小(默认262144)]\n"
                "  iq.bin  : 交织 int16 小端 IQ 文件 (iio_readdev 输出)\n"
                "  channel : BLE 广播信道号，决定解白化种子\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    int channel = atoi(argv[2]);
    int block   = argc > 3 ? atoi(argv[3]) : 262144;

    if (block <= 0) block = 262144;

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }

    int16_t *buf = malloc((size_t)block * 2 * sizeof(int16_t));
    if (!buf) { fprintf(stderr, "out of memory\n"); fclose(f); return 1; }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    long long total = 0;
    size_t got;
    while ((got = fread(buf, sizeof(int16_t) * 2, (size_t)block, f)) > 0) {
        demodulate_ble_packets(buf, (int)got, channel);
        total += (long long)got;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    double sec = (double)total / 10e6;          /* 默认 10 Msps 录制 */

    printf("[replay] %lld 样点 (%.3f s 空口数据) 用时 %.3f s (%.1fx 实时)\n",
           total, sec, dt, dt > 0 ? sec / dt : 0.0);

    free(buf);
    fclose(f);
    return 0;
}
