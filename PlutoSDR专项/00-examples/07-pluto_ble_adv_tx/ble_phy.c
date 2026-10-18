// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "ble_phy.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

uint8_t bt_swap_bits(uint8_t value) {
    uint64_t val = (uint64_t)value;
    uint64_t m = val * 0x0202020202ULL;
    m = m & 0x010884422010ULL;
    return (uint8_t)(m % 1023);
}

void bt_crc(const uint8_t *data, int length, uint8_t *dst) {
    uint32_t init = 0x555555;
    uint8_t ret[3];
    ret[0] = (uint8_t)((init >> 16) & 0xFF);
    ret[1] = (uint8_t)((init >> 8) & 0xFF);
    ret[2] = (uint8_t)(init & 0xFF);

    for (int i = 0; i < length; i++) {
        uint8_t d = data[i];
        for (int v = 0; v < 8; v++) {
            uint8_t t = (ret[0] >> 7) & 1;

            ret[0] <<= 1;
            if (ret[1] & 0x80) ret[0] |= 1;

            ret[1] <<= 1;
            if (ret[2] & 0x80) ret[1] |= 1;

            ret[2] <<= 1;

            if ((d & 1) != t) {
                ret[2] ^= 0x5B;
                ret[1] ^= 0x06;
            }
            d >>= 1;
        }
    }

    dst[0] = bt_swap_bits(ret[0]);
    dst[1] = bt_swap_bits(ret[1]);
    dst[2] = bt_swap_bits(ret[2]);
}

void bt_dewhitening(const uint8_t *data, int length, int channel, uint8_t *dst) {
    uint8_t lfsr = bt_swap_bits((uint8_t)channel) | 2;
    uint8_t masks[8] = {128, 64, 32, 16, 8, 4, 2, 1};

    for (int i = 0; i < length; i++) {
        uint8_t d = bt_swap_bits(data[i]);
        for (int b = 0; b < 8; b++) {
            if (lfsr & 0x80) {
                lfsr ^= 0x11;
                d ^= masks[b];
            }
            lfsr <<= 1;
        }
        dst[i] = bt_swap_bits(d);
    }
}

int bytes_to_bits_lsb(const uint8_t *bytes, int byte_len, uint8_t *bits_dst) {
    int ptr = 0;
    for (int i = 0; i < byte_len; i++) {
        for (int b = 0; b < 8; b++) {
            bits_dst[ptr++] = (bytes[i] >> b) & 1;
        }
    }
    return ptr;
}

int gen_sample_from_phy_bit(const uint8_t *bits, int num_bit, int16_t *iq_samples_dst) {
    int active_samples = (num_bit * SAMPLE_PER_SYMBOL) + (LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL);
    int oversampling_len = (num_bit * SAMPLE_PER_SYMBOL) + 2 * LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL;
    float *tmp_phy_bit_over_sampling = calloc(oversampling_len, sizeof(float));
    if (!tmp_phy_bit_over_sampling) {
        fprintf(stderr, "gen_sample_from_phy_bit: 分配过采样缓冲失败\n");
        return 0;
    }

    for (int i = 0; i < num_bit * SAMPLE_PER_SYMBOL; i++) {
        if (i % SAMPLE_PER_SYMBOL == 0) {
            int idx = i + LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL - 1;
            tmp_phy_bit_over_sampling[idx] = (float)(bits[i / SAMPLE_PER_SYMBOL]) * 2.0f - 1.0f;
        }
    }

    /* 相位查找表 1024 点：只算一次（以前每次调用都要重跑 2048 次三角函数） */
    static int16_t cos_table[1024];
    static int16_t sin_table[1024];
    static int tables_ready = 0;
    if (!tables_ready) {
        float gain = 24000.0f; // 数字基带幅度 (int16 满量程的 -2.7 dBFS)
        for (int i = 0; i < 1024; i++) {
            cos_table[i] = (int16_t)(gain * cosf(2.0f * M_PI * i / 1024.0f));
            sin_table[i] = (int16_t)(gain * sinf(2.0f * M_PI * i / 1024.0f));
        }
        tables_ready = 1;
    }

    /* 高斯成形核 (BT≈0.5)：16 项里只有下标 4~12 这 9 个会被下面的循环用到，
     * 两端的 0 是给 LEN_GAUSS_FILTER*SAMPLE_PER_SYMBOL 的窗口边界留的。
     * 抽头和 = 256 -> 每符号相位前进 256/1024 = 1/4 圈 = ±250 kHz (h = 0.50)。 */
    int gauss_coef_int8[16] = {0, 0, 0, 0, 2, 11, 32, 53, 60, 53, 32, 11, 2, 0, 0, 0};

    int tmp_phase = 0;
    iq_samples_dst[0] = cos_table[tmp_phase];
    iq_samples_dst[1] = sin_table[tmp_phase];

    for (int i = 0; i < active_samples - 1; i++) {
        int32_t acc = 0;
        for (int j = 3; j < (LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL - 4); j++) {
            int coef_idx = LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL - j - 1;
            acc += gauss_coef_int8[coef_idx] * (int)tmp_phy_bit_over_sampling[i + j];
        }

        // 🔴 安全边界处理：防止 C 语言负数模运算引起的相位撕裂
        tmp_phase = (tmp_phase + acc) % 1024;
        if (tmp_phase < 0) tmp_phase += 1024;

        iq_samples_dst[(i + 1) * 2]     = cos_table[tmp_phase];
        iq_samples_dst[(i + 1) * 2 + 1] = sin_table[tmp_phase];
    }

    // 🔴 帧隔离技术：在有效突发信号末尾，人为拼接全零留白
    for (int i = 0; i < IFS_BLANK_SAMPLES; i++) {
        iq_samples_dst[(active_samples + i) * 2]     = 0;
        iq_samples_dst[(active_samples + i) * 2 + 1] = 0;
    }

    free(tmp_phy_bit_over_sampling);
    return active_samples + IFS_BLANK_SAMPLES;
}
