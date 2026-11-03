/*
 * ofdm_phy.h —— OFDM 文件自收发基带算法（真机/仿真共用）
 *
 * 工程根目录：本文件所在目录（11-pluto_ofdm_file_self_transfer）。
 * 所有路径说明均相对该根目录，迁移整个目录即可，不要写死绝对路径。
 *
 * 算法基于 liquid-dsp 的 ofdmflexframegen / ofdmflexframesync：
 *   - 每个文件头包、数据包都是独立的 OFDM 帧
 *   - S0/S1 前导负责突发检测、定时、频偏估计与信道估计
 *   - 每帧自带 CRC + FEC，物理层按帧自动重同步
 */
#ifndef OFDM_PHY_H
#define OFDM_PHY_H

#include <stdint.h>
#include <complex.h>
#include <liquid/liquid.h>

/* ============================== OFDM 波形参数 ============================== */

#define OFDM_M                 64      /* 子载波数（偶数，>=8） */
#define OFDM_CP                16      /* 循环前缀长度（样点） */
#define OFDM_TAPER             4       /* OFDM 符号重叠 taper 长度 */
#define OFDM_GAP_SYMBOLS       2       /* 每帧之后额外补的零符号数，帮助同步复位 */

/* ============================== 文件分包参数 ============================== */

#define PAYLOAD_CHUNK_SIZE     128     /* 数据帧净荷字节数 */
#define HEADER_PAYLOAD_LEN     32      /* 文件头帧净荷字节数 */
#define HDR_USER_LEN           8       /* OFDM flexframe 用户头长度（协议固定 8） */

#define FRAME_TYPE_HEADER      1       /* 文件头帧：净荷为 file_size/crc/chunks */
#define FRAME_TYPE_DATA        2       /* 数据帧：净荷为文件分片 */

#define HEADER_REPEATS         4       /* 文件头帧重复次数 */
#define REPEAT_FACTOR          2       /* 每个数据帧重复次数（与 QPSK 参考一致） */

/* RX 同步处理块大小：太小调用次数多，太大仍可工作；2048 兼顾效率与稳健 */
#define OFDM_RX_BLOCK_SAMPLES  2048

/* 防止损坏头导致的天量分配（相对保守，够本工程验证用） */
#define OFDM_MAX_FILE_SIZE     (256u * 1024u * 1024u)
#define OFDM_MAX_CHUNKS        (OFDM_MAX_FILE_SIZE / PAYLOAD_CHUNK_SIZE)

/* ============================== 公共 API ============================== */

/*
 * 计算完整发送序列所需的最大 IQ 样点数（含尾部零符号与帧间隙）。
 *   num_chunks : 文件分片数
 * 返回样点容量；供主程序在调用 ofdm_phy_build_tx() 前分配 tx 缓冲。
 */
unsigned int ofdm_phy_calc_tx_capacity(uint32_t num_chunks);

/*
 * 生成完整 OFDM 基带发送采样。
 *   tx           : 输出 float complex 缓冲，由调用者按 calc_tx_capacity 分配
 *   tx_capacity  : tx 缓冲容量（样点数）
 *   file_data    : 待传输文件内容
 *   file_size    : 文件字节数
 *   file_crc     : 文件 CRC-32（crc_generate_key 结果）
 *   num_chunks   : 文件分片数
 * 返回实际生成的样点数；失败返回 0。
 */
unsigned int ofdm_phy_build_tx(float complex *tx, unsigned int tx_capacity,
                               const unsigned char *file_data, uint64_t file_size,
                               uint32_t file_crc, uint32_t num_chunks);

/*
 * 对接收样本执行：直流抑制 -> 峰值归一化 -> OFDM 帧同步 -> 逐帧解码 ->
 * 文件头发现 -> 数据帧重组 -> 文件级 CRC 校验 -> 写出文件。
 *
 *   rx_samples        : 接收 IQ 样本（原地处理，会被修改）
 *   rx_n              : 样本数
 *   expected_file_size : 发送端文件大小；0 表示未知（RX-only 两机模式）
 *   expected_file_crc  : 发送端文件 CRC；0 表示未知
 *   expected_num_chunks: 发送端分片数；0 表示未知
 *   out_filename      : 重组成功后的输出文件路径
 *
 * 实际用于分配与校验的是从文件头帧解码出的 file_size/file_crc/num_chunks，
 * expected_* 仅在自收发模式下用于交叉核对与日志提示。
 * 返回 1 表示成功，0 表示失败。
 */
int ofdm_phy_rx_process(float complex *rx_samples, unsigned int rx_n,
                        uint64_t expected_file_size, uint32_t expected_file_crc,
                        uint32_t expected_num_chunks, const char *out_filename);

#endif /* OFDM_PHY_H */
