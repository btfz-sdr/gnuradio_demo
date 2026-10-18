// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BLE_PHY_H
#define BLE_PHY_H

#include <stdint.h>

#define SAMPLE_PER_SYMBOL 4
#define LEN_GAUSS_FILTER  4
#define IFS_BLANK_SAMPLES 4000 // 1毫秒的空中静寂留白点数，防止手机 AGC 锁死

// 字节比特反转
uint8_t bt_swap_bits(uint8_t value);

// 24-bit CRC 计算
void bt_crc(const uint8_t *data, int length, uint8_t *dst);

// 加白/解白
void bt_dewhitening(const uint8_t *data, int length, int channel, uint8_t *dst);

// LSB 串行位展开
int bytes_to_bits_lsb(const uint8_t *bytes, int byte_len, uint8_t *bits_dst);

// GFSK 积分查表调制器
int gen_sample_from_phy_bit(const uint8_t *bits, int num_bit, int16_t *iq_samples_dst);

#endif // BLE_PHY_H
