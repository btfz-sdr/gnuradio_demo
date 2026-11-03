/*
 * ofdm_phy.c —— OFDM 文件自收发基带算法实现
 *
 * 本文件只包含基带算法，不包含 libiio/AD9361 硬件层。
 * 真机主程序 pluto_ofdm_file_self_transfer.c 与仿真版
 * doc/01-simulation/ofdm_pipeline.c 都基于同一套算法思路。
 *
 * 工程根目录：本文件所在目录（11-pluto_ofdm_file_self_transfer）。
 * 编译由根目录 makefile 完成。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>

#include "ofdm_phy.h"

/* ============================== 内部工具 ============================== */

/* 简单一阶直流抑制：AD9361 直流附近通常有 LO 泄漏/DC 偏置，OFDM 的 DC
 * 子载波本为 NULL，这里再做一次时域 DC 抑制更稳。 */
static void dc_block(float complex *x, unsigned int n)
{
    float complex dc = 0.0f;
    const float alpha = 1e-4f;
    for (unsigned int i = 0; i < n; i++) {
        dc = dc * (1.0f - alpha) + x[i] * alpha;
        x[i] -= dc;
    }
}

/* 峰值归一化：把整段 IQ 归一化到目标峰值，既防削顶，又给弱信号足够增益。
 * OFDM 同步器内部会做频域增益估计，这里只需把信号拉到合理动态范围。 */
static void normalize_peak(float complex *x, unsigned int n)
{
    float peak = 1e-6f;
    for (unsigned int i = 0; i < n; i++) {
        float m = cabsf(x[i]);
        if (m > peak)
            peak = m;
    }
    if (peak < 1e-9f)
        return;

    float scale = 0.5f / peak;
    for (unsigned int i = 0; i < n; i++)
        x[i] *= scale;
}

/* 构造 OFDM flexframe 生成器（发送端） */
static ofdmflexframegen create_flexframegen(void)
{
    unsigned char p[OFDM_M];
    ofdmframe_init_default_sctype(OFDM_M, p);

    ofdmflexframegenprops_s props;
    ofdmflexframegenprops_init_default(&props);
    props.check      = LIQUID_CRC_32;
    props.fec0       = LIQUID_FEC_HAMMING74;
    props.fec1       = LIQUID_FEC_NONE;
    props.mod_scheme = LIQUID_MODEM_QPSK;

    ofdmflexframegen fg = ofdmflexframegen_create(OFDM_M, OFDM_CP, OFDM_TAPER,
                                                  p, &props);
    if (fg)
        ofdmflexframegen_set_header_len(fg, HDR_USER_LEN);
    return fg;
}

/* 构造 OFDM flexframe 同步器（接收端） */
static ofdmflexframesync create_flexframesync(framesync_callback cb, void *userdata)
{
    unsigned char p[OFDM_M];
    ofdmframe_init_default_sctype(OFDM_M, p);

    ofdmflexframesync fs = ofdmflexframesync_create(OFDM_M, OFDM_CP, OFDM_TAPER,
                                                    p, cb, userdata);
    if (fs)
        ofdmflexframesync_set_header_len(fs, HDR_USER_LEN);
    return fs;
}

/* 把一个 OFDM 帧写入发送缓冲。payload_len 决定该帧的净荷长度。
 * 写入内容包括：完整帧 + 1 个 tail 零符号 + OFDM_GAP_SYMBOLS 个帧间隙。 */
static unsigned int write_one_frame(ofdmflexframegen fg,
                                    const unsigned char *hdr,
                                    const unsigned char *payload,
                                    unsigned int payload_len,
                                    float complex *out)
{
    if (ofdmflexframegen_assemble(fg, hdr, payload, payload_len) != LIQUID_OK)
        return 0;

    unsigned int frame_symbols = ofdmflexframegen_getframelen(fg);
    unsigned int frame_len = OFDM_M + OFDM_CP;
    unsigned int total = (frame_symbols + 1 + OFDM_GAP_SYMBOLS) * frame_len;

    /* write 返回帧完成标志；即使不检查返回值，样本流中也会先输出帧再输出零。 */
    (void)ofdmflexframegen_write(fg, out, total);
    return total;
}

/* 打包文件头帧的用户头（8 字节） */
static void build_header_user_hdr(unsigned char *hdr, uint16_t repeat_idx)
{
    memset(hdr, 0, HDR_USER_LEN);
    hdr[0] = FRAME_TYPE_HEADER;
    hdr[1] = (unsigned char)(repeat_idx & 0xffu);
}

/* 打包数据帧的用户头（8 字节） */
static void build_data_user_hdr(unsigned char *hdr, uint32_t chunk_idx,
                                uint16_t repeat_idx)
{
    memset(hdr, 0, HDR_USER_LEN);
    hdr[0] = FRAME_TYPE_DATA;
    hdr[1] = (unsigned char)(repeat_idx & 0xffu);
    memcpy(hdr + 2, &chunk_idx, 4);
}

/* ============================== 发送端 ============================== */

unsigned int ofdm_phy_calc_tx_capacity(uint32_t num_chunks)
{
    ofdmflexframegen fg = create_flexframegen();
    if (!fg)
        return 0;

    unsigned char hdr[HDR_USER_LEN] = {0};
    unsigned char payload[PAYLOAD_CHUNK_SIZE];
    memset(payload, 0, sizeof(payload));

    unsigned int frame_len = OFDM_M + OFDM_CP;

    /* 文件头帧：净荷 32 字节 */
    if (ofdmflexframegen_assemble(fg, hdr, payload, HEADER_PAYLOAD_LEN) != LIQUID_OK) {
        ofdmflexframegen_destroy(fg);
        return 0;
    }
    unsigned int header_samples =
        (ofdmflexframegen_getframelen(fg) + 1 + OFDM_GAP_SYMBOLS) * frame_len;

    /* 数据帧：净荷 128 字节 */
    if (ofdmflexframegen_assemble(fg, hdr, payload, PAYLOAD_CHUNK_SIZE) != LIQUID_OK) {
        ofdmflexframegen_destroy(fg);
        return 0;
    }
    unsigned int data_samples =
        (ofdmflexframegen_getframelen(fg) + 1 + OFDM_GAP_SYMBOLS) * frame_len;

    ofdmflexframegen_destroy(fg);

    return HEADER_REPEATS * header_samples +
           num_chunks * REPEAT_FACTOR * data_samples +
           1024u; /* 安全余量 */
}

unsigned int ofdm_phy_build_tx(float complex *tx, unsigned int tx_capacity,
                               const unsigned char *file_data, uint64_t file_size,
                               uint32_t file_crc, uint32_t num_chunks)
{
    ofdmflexframegen fg = create_flexframegen();
    if (!fg)
        return 0;

    unsigned int tx_cursor = 0;

    /* ---------- 文件头帧 ---------- */
    unsigned char hdr[HDR_USER_LEN];
    unsigned char header_payload[HEADER_PAYLOAD_LEN];
    memset(header_payload, 0, sizeof(header_payload));
    memcpy(header_payload + 0,  &file_size, 8);
    memcpy(header_payload + 8,  &file_crc,  4);
    memcpy(header_payload + 12, &num_chunks, 4);

    for (unsigned int r = 0; r < HEADER_REPEATS; r++) {
        build_header_user_hdr(hdr, (uint16_t)r);
        unsigned int total = write_one_frame(fg, hdr, header_payload,
                                             HEADER_PAYLOAD_LEN,
                                             &tx[tx_cursor]);
        if (total == 0 || tx_cursor + total > tx_capacity) {
            fprintf(stderr, "[TX] header frame overflow!\n");
            ofdmflexframegen_destroy(fg);
            return 0;
        }
        tx_cursor += total;
    }

    /* ---------- 数据帧 ----------
     * 同一 chunk 的两次重复不放在相邻位置，而是先完整发一轮、再完整发第二轮；
     * 这样短突发/短衰落最多打掉同一 chunk 的一份拷贝，另一份大概率存活。 */
    unsigned char data_payload[PAYLOAD_CHUNK_SIZE];
    for (unsigned int r = 0; r < REPEAT_FACTOR; r++) {
        for (uint32_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
            uint64_t off = (uint64_t)chunk_idx * PAYLOAD_CHUNK_SIZE;
            uint64_t remaining = file_size > off ? file_size - off : 0;
            unsigned int cur = remaining < PAYLOAD_CHUNK_SIZE
                               ? (unsigned int)remaining : PAYLOAD_CHUNK_SIZE;

            /* 最后一包不足 128 字节时补零；真实长度由文件头中的 file_size 控制 */
            memset(data_payload, 0, sizeof(data_payload));
            memcpy(data_payload, file_data + off, cur);

            build_data_user_hdr(hdr, chunk_idx, (uint16_t)r);
            unsigned int total = write_one_frame(fg, hdr, data_payload,
                                                 PAYLOAD_CHUNK_SIZE,
                                                 &tx[tx_cursor]);
            if (total == 0 || tx_cursor + total > tx_capacity) {
                fprintf(stderr, "[TX] data frame overflow at chunk %u!\n", chunk_idx);
                ofdmflexframegen_destroy(fg);
                return 0;
            }
            tx_cursor += total;
        }
    }

    ofdmflexframegen_destroy(fg);
    return tx_cursor;
}

/* ============================== 接收端 ============================== */

typedef struct {
    int mode;                  /* 0=找文件头，1=收数据 */
    int got_header;            /* 是否已解出文件头 */
    uint64_t file_size;        /* 从文件头帧解出 */
    uint32_t file_crc;
    uint32_t num_chunks;

    unsigned char *file_buf;   /* 重组缓冲，在文件头找到后分配 */
    unsigned char *chunk_flags;/* 去重标志 */
    unsigned int decoded_chunks;
    unsigned int total_frames; /* 回调触发次数（含无效头） */
} ofdm_rx_ctx;

static int ofdm_rx_callback(unsigned char *header, int header_valid,
                            unsigned char *payload, unsigned int payload_len,
                            int payload_valid, framesyncstats_s stats,
                            void *userdata)
{
    ofdm_rx_ctx *c = (ofdm_rx_ctx *)userdata;
    (void)stats;

    c->total_frames++;
    if (!header_valid)
        return 0;

    unsigned char type = header[0];

    if (c->mode == 0) {
        /* 第一遍：找文件头帧 */
        if (type == FRAME_TYPE_HEADER && payload_valid &&
            payload_len >= HEADER_PAYLOAD_LEN && !c->got_header) {
            memcpy(&c->file_size, payload + 0, 8);
            memcpy(&c->file_crc,  payload + 8, 4);
            memcpy(&c->num_chunks, payload + 12, 4);
            c->got_header = 1;
            printf("[RX] OFDM header decoded: file_size=%llu, crc=0x%08X, chunks=%u\n",
                   (unsigned long long)c->file_size, c->file_crc, c->num_chunks);
        }
    } else {
        /* 第二遍：收数据帧。逐帧重同步 + chunk 去重。 */
        if (type == FRAME_TYPE_DATA && payload_valid &&
            payload_len == PAYLOAD_CHUNK_SIZE) {
            uint32_t chunk_idx = 0;
            memcpy(&chunk_idx, header + 2, 4);

            if (chunk_idx < c->num_chunks && c->file_buf && c->chunk_flags &&
                !c->chunk_flags[chunk_idx]) {
                uint64_t off = (uint64_t)chunk_idx * PAYLOAD_CHUNK_SIZE;
                uint64_t remaining = c->file_size > off ? c->file_size - off : 0;
                unsigned int cur = remaining < PAYLOAD_CHUNK_SIZE
                                   ? (unsigned int)remaining : PAYLOAD_CHUNK_SIZE;
                memcpy(c->file_buf + off, payload, cur);
                c->chunk_flags[chunk_idx] = 1;
                c->decoded_chunks++;
            }
        }
    }

    return 0;
}

/* 把整段样本喂给同步器 */
static void run_flexframesync(ofdmflexframesync fs,
                              float complex *rx, unsigned int rx_n)
{
    for (unsigned int i = 0; i < rx_n; i += OFDM_RX_BLOCK_SAMPLES) {
        unsigned int n = rx_n - i < OFDM_RX_BLOCK_SAMPLES
                         ? rx_n - i : OFDM_RX_BLOCK_SAMPLES;
        ofdmflexframesync_execute(fs, &rx[i], n);
    }
}

int ofdm_phy_rx_process(float complex *rx_samples, unsigned int rx_n,
                        uint64_t expected_file_size, uint32_t expected_file_crc,
                        uint32_t expected_num_chunks, const char *out_filename)
{
    if (rx_n == 0)
        return 0;

    /* 1. 时域预处理（原地） */
    dc_block(rx_samples, rx_n);
    normalize_peak(rx_samples, rx_n);

    ofdm_rx_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    ofdmflexframesync fs = create_flexframesync(ofdm_rx_callback, &ctx);
    if (!fs) {
        fprintf(stderr, "[RX] failed to create OFDM frame synchronizer\n");
        return 0;
    }

    /* 2. 第一遍：发现文件头 */
    ctx.mode = 0;
    run_flexframesync(fs, rx_samples, rx_n);
    printf("[RX] Pass 1: %u frames detected, header %s.\n",
           ctx.total_frames, ctx.got_header ? "OK" : "MISSING");

    if (!ctx.got_header) {
        fprintf(stderr, "[RX] ERROR: no valid OFDM file header found.\n");
        ofdmflexframesync_destroy(fs);
        return 0;
    }

    /* 头帧损坏可能解出离谱长度，做防御性检查 */
    if (ctx.file_size == 0 || ctx.file_size > OFDM_MAX_FILE_SIZE ||
        ctx.num_chunks == 0 || ctx.num_chunks > OFDM_MAX_CHUNKS) {
        fprintf(stderr, "[RX] ERROR: invalid file header (size=%llu, chunks=%u).\n",
                (unsigned long long)ctx.file_size, ctx.num_chunks);
        ofdmflexframesync_destroy(fs);
        return 0;
    }

    if (expected_file_size != 0 &&
        (ctx.file_size != expected_file_size || ctx.num_chunks != expected_num_chunks)) {
        printf("[RX] WARNING: header mismatch (expected size=%llu chunks=%u).\n",
               (unsigned long long)expected_file_size, expected_num_chunks);
    }

    /* 3. 分配重组缓冲 */
    ctx.file_buf = (unsigned char *)calloc(ctx.file_size > 0 ? ctx.file_size : 1, 1);
    ctx.chunk_flags = (unsigned char *)calloc(ctx.num_chunks > 0 ? ctx.num_chunks : 1, 1);
    if (!ctx.file_buf || !ctx.chunk_flags) {
        fprintf(stderr, "[RX] ERROR: out of memory.\n");
        free(ctx.file_buf);
        free(ctx.chunk_flags);
        ofdmflexframesync_destroy(fs);
        return 0;
    }

    /* 4. 第二遍：收数据帧 */
    ofdmflexframesync_reset(fs);
    ctx.mode = 1;
    ctx.total_frames = 0;
    run_flexframesync(fs, rx_samples, rx_n);
    printf("[RX] Pass 2: %u frames detected, decoded %u / %u chunks (unique).\n",
           ctx.total_frames, ctx.decoded_chunks, ctx.num_chunks);

    ofdmflexframesync_destroy(fs);

    /* 5. 文件级 CRC 校验 + 写出 */
    int success = 0;
    if (ctx.decoded_chunks == ctx.num_chunks) {
        uint32_t crc_rx = crc_generate_key(LIQUID_CRC_32, ctx.file_buf,
                                           (unsigned int)ctx.file_size);
        if (crc_rx == ctx.file_crc) {
            printf("[RX] File CRC OK (0x%08X). Writing %s\n", crc_rx, out_filename);
            FILE *fout = fopen(out_filename, "wb");
            if (fout) {
                fwrite(ctx.file_buf, 1, ctx.file_size, fout);
                fclose(fout);
                success = 1;
            } else {
                fprintf(stderr, "[RX] ERROR: cannot write %s\n", out_filename);
            }
        } else {
            printf("[RX] ERROR: file CRC mismatch (rx=0x%08X, tx=0x%08X).\n",
                   crc_rx, ctx.file_crc);
        }
    } else {
        printf("[RX] ERROR: %u chunks missing.\n",
               ctx.num_chunks - ctx.decoded_chunks);
    }

    free(ctx.file_buf);
    free(ctx.chunk_flags);
    return success;
}
