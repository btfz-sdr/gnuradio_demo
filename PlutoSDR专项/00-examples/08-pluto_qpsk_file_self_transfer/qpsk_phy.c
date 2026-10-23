/*
 * qpsk_phy.c —— QPSK 文件自收发基带算法实现
 *
 * 本文件只包含基带算法，不包含 libiio/AD9361 硬件层与仿真信道，
 * 真机主程序 pluto_qpsk_file_self_transfer.c 与仿真版
 * doc/01-simulation/industrial_pipeline4.c 都基于同一套算法思路。
 *
 * 工程根目录：本文件所在目录（10-pluto_qpsk_file_self_transfer）。
 * 编译由根目录 makefile 完成。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>

#include "qpsk_phy.h"

/* ============================== 工具函数 ============================== */

static unsigned int gcd_u(unsigned int a, unsigned int b)
{
    while (b) {
        unsigned int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* 为交织器选一个与块长互素的步长 */
static unsigned int interleave_step_for(unsigned int len)
{
    unsigned int step = len / 2;
    if (step == 0) step = 1;
    while (step > 1 && gcd_u(step, len) != 1)
        step--;
    if (gcd_u(step, len) != 1)
        step = 1;
    return step;
}

/* 字节块交织：out[i] = in[(i*step) % len] */
static void interleave_bytes(const unsigned char *in, unsigned char *out,
                             unsigned int len, unsigned int step)
{
    for (unsigned int i = 0; i < len; i++)
        out[i] = in[(i * step) % len];
}

/* 解交织：in[(i*step) % len] = out[i] */
static void deinterleave_bytes(const unsigned char *in, unsigned char *out,
                               unsigned int len, unsigned int step)
{
    for (unsigned int i = 0; i < len; i++)
        out[(i * step) % len] = in[i];
}

/* ============================== 软件 AGC ============================== */

typedef struct {
    float power;   /* 功率估计 */
    float attack;  /* 信号变强时的平滑系数（快） */
    float decay;   /* 信号变弱时的平滑系数（慢） */
    float gain;    /* 当前增益 */
} simple_agc;

static void agc_init(simple_agc *a)
{
    a->power  = 1e-3f;
    a->attack = 0.05f;    /* fast attack */
    a->decay  = 0.0005f;  /* slow decay */
    a->gain   = 1.0f;
}

static float complex agc_execute(simple_agc *a, float complex x)
{
    float mag2 = crealf(x) * crealf(x) + cimagf(x) * cimagf(x);
    float alpha = (mag2 > a->power) ? a->attack : a->decay;
    a->power = (1.0f - alpha) * a->power + alpha * mag2;
    if (a->power < 1e-8f) a->power = 1e-8f;

    float target_gain = 1.0f / sqrtf(a->power);
    a->gain = 0.9f * a->gain + 0.1f * target_gain;
    if (a->gain > 1000.0f) a->gain = 1000.0f;
    if (a->gain < 1e-3f)   a->gain = 1e-3f;

    return x * a->gain;
}

/* 简单一阶直流抑制 */
static void dc_block(float complex *x, unsigned int n)
{
    float complex dc = 0.0f;
    const float alpha = 1e-4f;
    for (unsigned int i = 0; i < n; i++) {
        dc = dc * (1.0f - alpha) + x[i] * alpha;
        x[i] -= dc;
    }
}

/* ============================== 前导生成 ============================== */

/* 用 16-bit LFSR 生成长度为 total 的非周期 QPSK 前导（自相关尖锐）。
 * 同时输出符号索引和复数星座点。 */
static void gen_preamble(unsigned char *sym_idx, float complex *sym_cplx,
                         unsigned int total, modem mod)
{
    uint16_t lfsr = 0xACE1u;
    for (unsigned int i = 0; i < total; i++) {
        /* taps: x^16 + x^14 + x^13 + x^11 + 1 */
        unsigned int bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
        lfsr = (uint16_t)((lfsr >> 1) | (bit << 15));
        unsigned int s = lfsr & 0x3u;

        sym_idx[i] = (unsigned char)s;
        modem_modulate(mod, s, &sym_cplx[i]);
    }
}

/* ============================== 组包 / 编码 ============================== */

static void build_header_raw(unsigned char *raw, uint64_t file_size,
                             uint32_t file_crc, uint32_t total_chunks,
                             uint16_t repeat_idx)
{
    memset(raw, 0, PKT_RAW_LEN);
    uint32_t sync = SYNC_WORD;
    memcpy(raw + 0, &sync, 4);
    raw[4] = PKT_TYPE_HEADER;
    uint16_t rep = repeat_idx;
    memcpy(raw + 6, &rep, 2);
    memcpy(raw + 8, &total_chunks, 4);
    memcpy(raw + 12, &file_crc, 4);
    memcpy(raw + 16, &file_size, 8);
}

static void build_data_raw(unsigned char *raw, uint32_t chunk_idx,
                           const unsigned char *file_data, uint64_t file_size)
{
    memset(raw, 0, PKT_RAW_LEN);
    uint32_t sync = SYNC_WORD;
    memcpy(raw + 0, &sync, 4);
    raw[4] = PKT_TYPE_DATA;
    uint16_t lo = (uint16_t)(chunk_idx & 0xffffu);
    memcpy(raw + 6, &lo, 2);
    memcpy(raw + 8, &chunk_idx, 4);

    uint64_t off = (uint64_t)chunk_idx * PAYLOAD_CHUNK_SIZE;
    uint64_t remaining = file_size > off ? file_size - off : 0;
    unsigned int cur = remaining < PAYLOAD_CHUNK_SIZE ? (unsigned int)remaining
                                                      : PAYLOAD_CHUNK_SIZE;
    memcpy(raw + PKT_HDR_LEN, file_data + off, cur);
}

/* 把 raw 包编码成交织后的编码字节 */
static void encode_packet(packetizer pk, const unsigned char *raw,
                          unsigned char *enc, unsigned char *enc_int,
                          unsigned int enc_len, unsigned int step)
{
    packetizer_encode(pk, raw, enc);
    interleave_bytes(enc, enc_int, enc_len, step);
}

/* 生成一个包的符号序列（前导 + DQPSK 数据 + 保护零）。
 * DQPSK 用显式相位增量实现，避免 modem 符号索引顺序与相位旋转不一致的问题。
 * dibit -> 相位增量：0->0°, 1->+90°, 2->180°, 3->-90°。 */
static void build_packet_symbols(const unsigned char *enc_int,
                                 unsigned int enc_len, unsigned int enc_symbols,
                                 const float complex *pre_cplx,
                                 float complex *pkt_syms)
{
    const float complex delta[4] = {1.0f, _Complex_I, -1.0f, -_Complex_I};
    unsigned int sym_idx = 0;

    /* 前导：绝对 QPSK，已知序列 */
    for (unsigned int i = 0; i < PREAMBLE_LEN; i++)
        pkt_syms[sym_idx++] = pre_cplx[i];

    /* 数据：DQPSK，相位参考从前导末符号延续 */
    float complex carrier = pre_cplx[PREAMBLE_LEN - 1];
    for (unsigned int i = 0; i < enc_len; i++) {
        unsigned char b = enc_int[i];
        for (int sh = 6; sh >= 0; sh -= 2) {
            unsigned int d = (b >> sh) & 0x3u;
            carrier *= delta[d];
            pkt_syms[sym_idx++] = carrier;
        }
    }
    (void)enc_symbols; /* sym_idx 应等于 PREAMBLE_LEN + enc_symbols */

    /* 包间保护零符号 */
    for (unsigned int i = 0; i < GUARD_SYMBOLS; i++)
        pkt_syms[sym_idx++] = 0.0f;
}

/* ============================== 发送端 ============================== */

unsigned int qpsk_phy_build_tx(float complex *tx, unsigned int tx_capacity,
                               const unsigned char *file_data, uint64_t file_size,
                               uint32_t file_crc, uint32_t num_chunks)
{
    /* 编码长度 */
    packetizer pk = packetizer_create(PKT_RAW_LEN, LIQUID_CRC_32,
                                      LIQUID_FEC_HAMMING74, LIQUID_FEC_NONE);
    unsigned int enc_len = packetizer_get_enc_msg_len(pk);
    unsigned int enc_symbols = enc_len * 4;
    unsigned int packet_symbols = PREAMBLE_LEN + enc_symbols + GUARD_SYMBOLS;
    unsigned int interleave_step = interleave_step_for(enc_len);

    printf("[TX] enc_len=%u, enc_symbols=%u, packet_symbols=%u, interleave_step=%u\n",
           enc_len, enc_symbols, packet_symbols, interleave_step);

    /* RRC 插值器 */
    unsigned int h_len = 2 * TX_K * TX_M + 1;
    float h[h_len];
    liquid_firdes_prototype(LIQUID_FIRFILT_RRC, TX_K, TX_M, TX_BETA, 0.0f, h);
    firinterp_crcf interp = firinterp_crcf_create(TX_K, h, h_len);

    modem mod = modem_create(LIQUID_MODEM_QPSK);

    unsigned char pre_sym[PREAMBLE_LEN];
    float complex pre_cplx[PREAMBLE_LEN];
    gen_preamble(pre_sym, pre_cplx, PREAMBLE_LEN, mod);

    unsigned char *enc    = (unsigned char *)malloc(enc_len);
    unsigned char *enc_int = (unsigned char *)malloc(enc_len);
    unsigned char *raw    = (unsigned char *)malloc(PKT_RAW_LEN);
    float complex *pkt_syms = (float complex *)malloc(packet_symbols * sizeof(float complex));

    unsigned int total_packets = HEADER_REPEATS + num_chunks * REPEAT_FACTOR;
    unsigned int tx_cursor = 0;

    /* 起始训练段：用与包前导不同的随机 QPSK 序列，供接收端 AGC / symsync
     * 先收敛。该段不会与包前导产生相关峰，避免干扰后续检测。 */
    {
        uint16_t lfsr = 0x5A5Au;
        for (unsigned int i = 0; i < STARTUP_SYMBOLS; i++) {
            unsigned int bit = ((lfsr >> 0) ^ (lfsr >> 1) ^ (lfsr >> 4) ^ (lfsr >> 6)) & 1u;
            lfsr = (uint16_t)((lfsr >> 1) | (bit << 15));
            float complex s;
            modem_modulate(mod, lfsr & 0x3u, &s);
            if (tx_cursor + TX_K > tx_capacity) break;
            firinterp_crcf_execute(interp, s, &tx[tx_cursor]);
            tx_cursor += TX_K;
        }
    }

    for (unsigned int p = 0; p < total_packets; p++) {
        if (p < HEADER_REPEATS) {
            build_header_raw(raw, file_size, file_crc, num_chunks, (uint16_t)p);
        } else {
            unsigned int data_pkt = p - HEADER_REPEATS;
            uint32_t chunk_idx = data_pkt / REPEAT_FACTOR;
            build_data_raw(raw, chunk_idx, file_data, file_size);
        }

        encode_packet(pk, raw, enc, enc_int, enc_len, interleave_step);
        build_packet_symbols(enc_int, enc_len, enc_symbols, pre_cplx, pkt_syms);

        for (unsigned int i = 0; i < packet_symbols; i++) {
            if (tx_cursor + TX_K > tx_capacity) {
                fprintf(stderr, "[TX] tx buffer overflow!\n");
                packetizer_destroy(pk);
                firinterp_crcf_destroy(interp);
                modem_destroy(mod);
                free(enc);
                free(enc_int);
                free(raw);
                free(pkt_syms);
                return 0;
            }
            firinterp_crcf_execute(interp, pkt_syms[i], &tx[tx_cursor]);
            tx_cursor += TX_K;
        }
    }

    /* flush 尾符号 */
    for (unsigned int t = 0; t < TAIL_SYMBOLS; t++) {
        if (tx_cursor + TX_K > tx_capacity) break;
        firinterp_crcf_execute(interp, 0.0f, &tx[tx_cursor]);
        tx_cursor += TX_K;
    }

    free(enc);
    free(enc_int);
    free(raw);
    free(pkt_syms);
    firinterp_crcf_destroy(interp);
    modem_destroy(mod);
    packetizer_destroy(pk);

    return tx_cursor;
}

/* ============================== 接收端 ============================== */

/* 归一化前导相关 */
static float preamble_corr(const float complex *rx, unsigned int s,
                           const float complex *pre, unsigned int P)
{
    float complex acc = 0.0f;
    float energy = 0.0f;
    for (unsigned int n = 0; n < P; n++) {
        float complex x = rx[s + n];
        acc += x * conjf(pre[n]);
        energy += crealf(x) * crealf(x) + cimagf(x) * cimagf(x);
    }
    return cabsf(acc) / sqrtf(energy * (float)P + 1e-12f);
}

/* 在符号流中检测所有前导相关峰，返回候选起点个数 */
static unsigned int detect_candidates(const float complex *rx, unsigned int n,
                                      const float complex *pre, unsigned int P,
                                      unsigned int *candidates,
                                      unsigned int max_candidates)
{
    unsigned int count = 0;
    int last_peak = -1000000;
    int min_gap = (int)P;   /* 至少隔一个前导长度，避免同一前导重复计数 */

    for (unsigned int s = 0; s + P <= n; s++) {
        float c = preamble_corr(rx, s, pre, P);
        if (c < PREAMBLE_THRESH)
            continue;

        /* 局部峰值细化 */
        int best = (int)s;
        float best_c = c;
        for (int d = -2; d <= 2; d++) {
            int t = (int)s + d;
            if (t < 0 || (unsigned int)(t + P) > n)
                continue;
            float cc = preamble_corr(rx, (unsigned int)t, pre, P);
            if (cc > best_c) {
                best_c = cc;
                best = t;
            }
        }

        if (best > last_peak + min_gap) {
            if (count >= max_candidates)
                break;
            candidates[count++] = (unsigned int)best;
            last_peak = best;
            s = (unsigned int)best + (unsigned int)min_gap - 1; /* for 循环会再 +1 */
        }
    }
    return count;
}

/* 在候选起点处解码一个包。成功返回 1，并把 raw 包写入 raw_out。
 * DQPSK 解码：直接用相邻符号相位差，映射到 0°/±90°/180° 的 dibit，
 * 对固定相偏和小 CFO 免疫。 */
static int decode_packet_at(const float complex *rx, unsigned int s,
                            unsigned int P, packetizer pk,
                            unsigned int enc_len, unsigned int enc_symbols,
                            unsigned int deint_step,
                            unsigned char *raw_out)
{
    unsigned char *enc_int = (unsigned char *)malloc(enc_len);

    float complex prev = rx[s + P - 1];
    unsigned int bitbuf = 0;
    unsigned int nbits = 0;
    unsigned int byteidx = 0;

    for (unsigned int i = 0; i < enc_symbols && byteidx < enc_len; i++) {
        float complex cur = rx[s + P + i];
        float diff = cargf(cur * conjf(prev));
        prev = cur;

        /* 归一化到 [-pi, pi) 后量化到 90° 整数倍 */
        float a = diff;
        while (a >=  M_PI) a -= 2.0f * M_PI;
        while (a <  -M_PI) a += 2.0f * M_PI;
        int k = (int)roundf(a / (M_PI / 2.0f));
        unsigned int d = ((k % 4) + 4) % 4;

        bitbuf = (bitbuf << 2) | d;
        nbits += 2;
        if (nbits == 8) {
            enc_int[byteidx++] = (unsigned char)bitbuf;
            bitbuf = 0;
            nbits = 0;
        }
    }

    /* 解交织 + packetizer 解码 */
    unsigned char *enc = (unsigned char *)malloc(enc_len);
    deinterleave_bytes(enc_int, enc, enc_len, deint_step);
    int crc_pass = packetizer_decode(pk, enc, raw_out);

    free(enc_int);
    free(enc);

    return crc_pass;
}

/* 公共接收处理：样本 -> AGC/直流抑制 -> symsync -> 检测/解码/重组/校验 */
int qpsk_phy_rx_process(float complex *rx_samples, unsigned int rx_n,
                        uint64_t file_size, uint32_t file_crc,
                        uint32_t num_chunks, const char *out_filename)
{
    /* 1. 直流抑制 + AGC（原地处理） */
    dc_block(rx_samples, rx_n);
    simple_agc agc;
    agc_init(&agc);
    for (unsigned int i = 0; i < rx_n; i++)
        rx_samples[i] = agc_execute(&agc, rx_samples[i]);

    /* 2. 符号定时恢复（RRC 匹配滤波 + 定时环路） */
    unsigned int h_len = 2 * TX_K * TX_M + 1;
    float h[h_len];
    liquid_firdes_prototype(LIQUID_FIRFILT_RRC, TX_K, TX_M, TX_BETA, 0.0f, h);
    symsync_crcf symsync = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC,
                                                        TX_K, TX_M, TX_BETA, TX_FILTERS);
    symsync_crcf_set_lf_bw(symsync, 0.02f);

    unsigned int max_symbols = rx_n / TX_K + 4096;
    float complex *rx_symbols = (float complex *)malloc(max_symbols * sizeof(float complex));
    unsigned int rx_sym_count = 0;
    symsync_crcf_execute(symsync, rx_samples, rx_n, rx_symbols, &rx_sym_count);
    symsync_crcf_destroy(symsync);
    printf("[RX] Symbol timing recovered %u symbols.\n", rx_sym_count);

    /* 3. 前导与检测 */
    modem mod = modem_create(LIQUID_MODEM_QPSK);
    packetizer pk = packetizer_create(PKT_RAW_LEN, LIQUID_CRC_32,
                                      LIQUID_FEC_HAMMING74, LIQUID_FEC_NONE);
    unsigned int enc_len = packetizer_get_enc_msg_len(pk);
    unsigned int enc_symbols = enc_len * 4;
    unsigned int deint_step = interleave_step_for(enc_len);

    float complex pre_cplx[PREAMBLE_LEN];
    {
        unsigned char pre_sym[PREAMBLE_LEN];
        gen_preamble(pre_sym, pre_cplx, PREAMBLE_LEN, mod);
    }

    unsigned int max_candidates = HEADER_REPEATS + num_chunks * REPEAT_FACTOR + 64;
    unsigned int *candidates = (unsigned int *)malloc(max_candidates * sizeof(unsigned int));
    unsigned int num_candidates = detect_candidates(rx_symbols, rx_sym_count,
                                                    pre_cplx, PREAMBLE_LEN,
                                                    candidates, max_candidates);
    printf("[RX] Detected %u preamble candidates.\n", num_candidates);

    unsigned char *raw = (unsigned char *)malloc(PKT_RAW_LEN);

    /* 4. 第一遍：找文件头 */
    uint64_t file_size_rx = 0;
    uint32_t file_crc_rx = 0;
    uint32_t total_chunks_rx = 0;
    int header_ok = 0;

    for (unsigned int i = 0; i < num_candidates; i++) {
        if (!decode_packet_at(rx_symbols, candidates[i], PREAMBLE_LEN,
                              pk, enc_len, enc_symbols, deint_step, raw))
            continue;

        uint32_t sync = 0;
        memcpy(&sync, raw, 4);
        if (sync != SYNC_WORD || raw[4] != PKT_TYPE_HEADER)
            continue;

        memcpy(&file_size_rx, raw + 16, 8);
        memcpy(&file_crc_rx, raw + 12, 4);
        memcpy(&total_chunks_rx, raw + 8, 4);
        header_ok = 1;
        printf("[RX] Header decoded: file_size=%llu, crc=0x%08X, chunks=%u\n",
               (unsigned long long)file_size_rx, file_crc_rx, total_chunks_rx);
        break;
    }

    if (!header_ok) {
        printf("[RX] ERROR: no valid file header found.\n");
        free(raw);
        free(candidates);
        free(rx_symbols);
        modem_destroy(mod);
        packetizer_destroy(pk);
        return 0;
    }

    if (file_size_rx != file_size || total_chunks_rx != num_chunks) {
        printf("[RX] WARNING: header mismatch (expected size=%llu chunks=%u).\n",
               (unsigned long long)file_size, num_chunks);
    }

    /* 5. 第二遍：解数据包，逐包重同步 */
    unsigned char *file_rx = (unsigned char *)calloc(file_size > 0 ? file_size : 1, 1);
    unsigned char *chunk_flags = (unsigned char *)calloc(num_chunks > 0 ? num_chunks : 1, 1);
    unsigned int decoded_chunks = 0;

    for (unsigned int i = 0; i < num_candidates; i++) {
        if (!decode_packet_at(rx_symbols, candidates[i], PREAMBLE_LEN,
                              pk, enc_len, enc_symbols, deint_step, raw))
            continue;

        uint32_t sync = 0;
        memcpy(&sync, raw, 4);
        if (sync != SYNC_WORD || raw[4] != PKT_TYPE_DATA)
            continue;

        uint32_t idx = 0;
        memcpy(&idx, raw + 8, 4);
        if (idx >= num_chunks || chunk_flags[idx])
            continue;

        uint64_t off = (uint64_t)idx * PAYLOAD_CHUNK_SIZE;
        uint64_t remaining = file_size > off ? file_size - off : 0;
        unsigned int cur = remaining < PAYLOAD_CHUNK_SIZE ? (unsigned int)remaining
                                                          : PAYLOAD_CHUNK_SIZE;
        memcpy(file_rx + off, raw + PKT_HDR_LEN, cur);
        chunk_flags[idx] = 1;
        decoded_chunks++;
    }

    printf("[RX] Decoded %u / %u chunks (unique).\n", decoded_chunks, num_chunks);

    int success = 0;
    if (decoded_chunks == num_chunks) {
        unsigned int crc_rx = crc_generate_key(LIQUID_CRC_32, file_rx, (unsigned int)file_size);
        if (crc_rx == file_crc_rx) {
            printf("[RX] File CRC OK (0x%08X). Writing %s\n", crc_rx, out_filename);
            FILE *fout = fopen(out_filename, "wb");
            if (fout) {
                fwrite(file_rx, 1, file_size, fout);
                fclose(fout);
                success = 1;
            } else {
                fprintf(stderr, "[RX] ERROR: cannot write %s\n", out_filename);
            }
        } else {
            printf("[RX] ERROR: file CRC mismatch (rx=0x%08X, tx=0x%08X).\n",
                   crc_rx, file_crc_rx);
        }
    } else {
        printf("[RX] ERROR: %u chunks missing.\n", num_chunks - decoded_chunks);
    }

    free(raw);
    free(candidates);
    free(rx_symbols);
    free(file_rx);
    free(chunk_flags);
    modem_destroy(mod);
    packetizer_destroy(pk);
    return success;
}
