/*
 * qpsk_phy.h —— QPSK 文件自收发基带算法（真机/仿真共用）
 *
 * 工程根目录：本文件所在目录（10-pluto_qpsk_file_self_transfer）。
 * 所有路径说明均相对该根目录，迁移整个目录即可，不要写死绝对路径。
 */
#ifndef QPSK_PHY_H
#define QPSK_PHY_H

#include <stdint.h>
#include <complex.h>
#include <liquid/liquid.h>

/* ============================== 帧结构常量 ============================== */

#define PAYLOAD_CHUNK_SIZE   128          /* 数据包净荷字节数 */
#define PKT_HDR_LEN          16           /* 包头固定字段长度 */
#define PKT_RAW_LEN          (PKT_HDR_LEN + PAYLOAD_CHUNK_SIZE) /* 144 */

#define SYNC_WORD            0xAA55AA55u
#define PKT_TYPE_HEADER      1
#define PKT_TYPE_DATA        2

#define PREAMBLE_LEN         64           /* 每包前导 QPSK 符号数 */
#define GUARD_SYMBOLS        16           /* 包间保护零符号 */
#define TAIL_SYMBOLS         64           /* 发送尾部 flush 符号 */
#define STARTUP_SYMBOLS      1024         /* 起始训练符号，让 AGC/symsync 收敛 */

#define REPEAT_FACTOR        2            /* 每个数据包重复发送次数 */
#define HEADER_REPEATS       4            /* 文件头包重复次数 */

#define TX_K                 2            /* 每符号采样数 (sps) */
#define TX_M                 7            /* RRC 滤波器符号延迟 */
#define TX_BETA              0.30f        /* RRC rolloff */
#define TX_FILTERS           32           /* symsync 滤波器组数 */

#define PREAMBLE_THRESH      0.55f        /* 归一化前导相关峰门限 */

/* ============================== 公共 API ============================== */

/*
 * 生成完整基带发送采样。
 *   tx           : 输出 float complex 缓冲，由调用者分配
 *   tx_capacity  : tx 缓冲容量（样点数）
 *   file_data    : 待传输文件内容
 *   file_size    : 文件字节数
 *   file_crc     : 文件 CRC-32（crc_generate_key 结果）
 *   num_chunks   : 文件分片数
 * 返回实际生成的样点数；失败返回 0。
 */
unsigned int qpsk_phy_build_tx(float complex *tx, unsigned int tx_capacity,
                               const unsigned char *file_data, uint64_t file_size,
                               uint32_t file_crc, uint32_t num_chunks);

/*
 * 对接收样本执行：直流抑制 -> 软件 AGC -> symsync 符号定时 ->
 * 前导相关检测 -> 逐包 DQPSK 解码 -> 重组 -> 文件级 CRC 校验 -> 写出文件。
 *   rx_samples   : 接收 IQ 样本（原地处理，会被修改）
 *   rx_n         : 样本数
 *   file_size    : 发送端文件大小（用于分配与校验）
 *   file_crc     : 发送端文件 CRC（与包头里的一致才通过）
 *   num_chunks   : 发送端分片数
 *   out_filename : 重组成功后的输出文件路径
 * 返回 1 表示成功，0 表示失败。
 */
int qpsk_phy_rx_process(float complex *rx_samples, unsigned int rx_n,
                        uint64_t file_size, uint32_t file_crc,
                        uint32_t num_chunks, const char *out_filename);

#endif /* QPSK_PHY_H */
