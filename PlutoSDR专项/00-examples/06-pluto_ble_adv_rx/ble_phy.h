// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BLE_PHY_H
#define BLE_PHY_H

#include <stdint.h>

#define SAMPLE_PER_SYMBOL 10

// ================== 发射端通用函数 ==================
uint8_t bt_swap_bits(uint8_t value);
void bt_crc(const uint8_t *data, int length, uint8_t *dst);
void bt_dewhitening(const uint8_t *data, int length, int channel, uint8_t *dst);

// ================== 【接收端独占】数字解调算法 ==================
/**
 * @brief 设置接收端采样率（MHz）。BLE LE 1M 符号率为 1 Msym/s，
 *        每符号样点数 = fs / 1 MHz。默认 10 MHz（10 样点/符号）。
 *        必须在首次调用 demodulate_ble_packets 之前调用。
 */
void ble_phy_set_sample_rate(double fs_mhz);

/**
 * @brief 从原始 IQ 采样中解调出比特流，并通过状态机搜索、解调、校验 BLE 广播包
 * @param iq_samples 输入的交织 int16_t IQ 采样缓冲区 (I, Q, I, Q ...)
 * @param num_samples 传入的采样点对数
 * @param channel 蓝牙信道号（37, 38, 39，用作解白化种子）
 */
void demodulate_ble_packets(const int16_t *iq_samples, int num_samples, int channel);

#endif // BLE_PHY_H
