/*
 * industrial_pipeline4.c
 *
 * PlutoSDR QPSK 自收发文件传输链路（改进版）。
 *
 * 相比 industrial_pipeline3.c 的改进点：
 *   1. 每包带已知前导（周期 QPSK 序列），用于突发检测、粗定时、
 *      载波频偏估计和 AGC 稳定，不再用 0..199 符号盲目扫描。
 *   2. 数据采用 DQPSK（差分 QPSK），消除固定相偏和 90° 相位模糊问题。
 *   3. 逐包前导重同步：每包前都有前导，解码失败后自动在下一次相关峰
 *      重新同步，不再按固定 980 符号死步进。
 *   4. 增加字节交织 + CRC32 + Hamming(7,4) + 文件级 CRC32 + 分组重复
 *      （REPEAT_FACTOR），提高抗突发错误能力。
 *   5. 补齐 PlutoSDR libiio 真机收发层（PLUTOSDR_HW 宏开启），
 *      int16<->float 转换、幅度标定；未定义 PLUTOSDR_HW 时跑内存仿真。
 *   6. AGC 改为快速 attack / 慢速 decay 的软件 AGC，并带简单直流抑制。
 *
 * 主机仿真编译：
 *   gcc -Wall -O2 -o industrial_pipeline4 industrial_pipeline4.c -lm -lc -lliquid
 *
 * PlutoSDR 交叉编译（示例）：
 *   arm-linux-gnueabihf-gcc -Wall -O2 -DPLUTOSDR_HW \
 *       -o industrial_pipeline4 industrial_pipeline4.c -lm -lc -lliquid -liio
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>
#include <time.h>
#include <liquid/liquid.h>

/* ============================== 常量配置 ============================== */

#define PAYLOAD_CHUNK_SIZE   128          /* 数据包净荷字节数 */
#define PKT_HDR_LEN          16           /* 包头固定字段长度 */
#define PKT_RAW_LEN          (PKT_HDR_LEN + PAYLOAD_CHUNK_SIZE) /* 144 */

#define SYNC_WORD            0xAA55AA55u
#define PKT_TYPE_HEADER      1
#define PKT_TYPE_DATA        2

#define PREAMBLE_HALF        32           /* 前导序列半长 */
#define PREAMBLE_LEN         (PREAMBLE_HALF * 2)  /* 64 个 QPSK 符号 */
#define GUARD_SYMBOLS        16           /* 包间保护零符号 */
#define TAIL_SYMBOLS         64           /* 发送尾部 flush 符号 */
#define STARTUP_SYMBOLS      1024         /* 起始训练符号，让 AGC/symsync 收敛 */

#define REPEAT_FACTOR        2            /* 每个数据包重复发送次数 */
#define HEADER_REPEATS       4            /* 文件头包重复次数 */

#define TX_K                 2            /* 每符号采样数 (sps) */
#define TX_M                 7            /* RRC 滤波器符号延迟 */
#define TX_BETA              0.30f        /* RRC rolloff */
#define TX_FILTERS           32           /* symsync 滤波器组数 */

#define PREAMBLE_THRESH      0.55f        /* 归一化相关峰检测门限 */

/* 仿真信道默认值（可用环境变量覆盖） */
#define SIM_DEF_CFO          0.0002f      /* rad/sample */
#define SIM_DEF_NOISE        0.05f
#define SIM_DEF_GAIN         0.50f

#ifdef PLUTOSDR_HW
/* 真机 AD9361 参数，按实际环路调整 */
#define HW_SAMPLE_RATE       2500000      /* 2.5 MSPS */
#define HW_BANDWIDTH         2000000      /* 2 MHz */
#define HW_LO_FREQ           2500000000ULL /* 2.5 GHz */
#define HW_RX_PORT           "A_BALANCED"
#define HW_TX_PORT           "A"
#define HW_RX_GAIN           40.0f        /* dB, 手动增益 */
#define HW_TX_ATTEN          10.0f        /* dB */
#define HW_BUF_SIZE          (1024*1024)  /* 1 MiS */
#define HW_RX_MARGIN_SAMPLES 200000       /* 尾部多收 0.2 MS 余量 */
#endif

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
 * 同时输出符号索引（用于差分状态衔接）和复数星座点（用于相关检测）。 */
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

/* 生成完整基带采样信号，返回实际采样数 */
static unsigned int build_tx_signal(float complex *tx, unsigned int tx_capacity,
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
                exit(1);
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

/* ============================== 信道损伤（仿真） ============================== */

#ifndef PLUTOSDR_HW

static void apply_channel_impairments(float complex *buf, unsigned int n,
                                      float cfo, float phase_init,
                                      float noise_sigma, float gain)
{
    for (unsigned int i = 0; i < n; i++) {
        buf[i] *= gain * cexpf(_Complex_I * (phase_init + i * cfo));

        /* Box-Muller，I/Q 独立噪声 */
        float u1 = (float)rand() / RAND_MAX;
        float u2 = (float)rand() / RAND_MAX;
        float u3 = (float)rand() / RAND_MAX;
        float u4 = (float)rand() / RAND_MAX;
        if (u1 < 1e-9f) u1 = 1e-9f;
        if (u3 < 1e-9f) u3 = 1e-9f;
        float n_real = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * M_PI * u2) * noise_sigma;
        float n_imag = sqrtf(-2.0f * logf(u3)) * sinf(2.0f * M_PI * u4) * noise_sigma;
        buf[i] += n_real + _Complex_I * n_imag;
    }
}

#endif /* !PLUTOSDR_HW */

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
                                      unsigned int packet_symbols,
                                      unsigned int *candidates,
                                      unsigned int max_candidates)
{
    unsigned int count = 0;
    int last_peak = -1000000;
    int min_gap = (int)P;   /* 至少隔一个前导长度，避免同一前导重复计数 */
    (void)packet_symbols;

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
static int rx_process_and_reassemble(float complex *rx_samples, unsigned int rx_n,
                                     uint64_t file_size, uint32_t file_crc,
                                     uint32_t num_chunks)
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
    unsigned int packet_symbols = PREAMBLE_LEN + enc_symbols + GUARD_SYMBOLS;
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
                                                    packet_symbols,
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
            printf("[RX] File CRC OK (0x%08X). Writing recovered.png\n", crc_rx);
            FILE *fout = fopen("recovered.png", "wb");
            if (fout) {
                fwrite(file_rx, 1, file_size, fout);
                fclose(fout);
                success = 1;
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

/* ============================== 仿真主流程 ============================== */

#ifndef PLUTOSDR_HW

static int sim_main(float complex *tx, unsigned int tx_n,
                    uint64_t file_size, uint32_t file_crc, uint32_t num_chunks)
{
    float cfo   = getenv("SIM_CFO")   ? atof(getenv("SIM_CFO"))   : SIM_DEF_CFO;
    float noise = getenv("SIM_NOISE") ? atof(getenv("SIM_NOISE")) : SIM_DEF_NOISE;
    float gain  = getenv("SIM_GAIN")  ? atof(getenv("SIM_GAIN"))  : SIM_DEF_GAIN;
    float phase = getenv("SIM_PHASE") ? atof(getenv("SIM_PHASE"))
                                      : 2.0f * M_PI * (float)rand() / RAND_MAX;
    unsigned int lead  = getenv("SIM_LEAD")  ? (unsigned int)atoi(getenv("SIM_LEAD"))  : 0;
    unsigned int burst = getenv("SIM_BURST") ? (unsigned int)atoi(getenv("SIM_BURST")) : 0;
    float burst_pos = getenv("SIM_BURST_POS") ? atof(getenv("SIM_BURST_POS")) : 0.5f;

    unsigned int rx_n = tx_n + lead;
    float complex *rx = (float complex *)calloc(rx_n, sizeof(float complex));
    memcpy(rx + lead, tx, tx_n * sizeof(float complex));

    printf("[SIM] cfo=%.4f rad/sample, phase=%.3f rad, noise=%.3f, gain=%.2f, lead=%u, burst=%u\n",
           cfo, phase, noise, gain, lead, burst);

    apply_channel_impairments(rx, rx_n, cfo, phase, noise, gain);

    if (burst > 0) {
        unsigned int bstart = (unsigned int)((float)rx_n * burst_pos);
        for (unsigned int i = 0; i < burst && bstart + i < rx_n; i++)
            rx[bstart + i] = 0.0f;
        printf("[SIM] Zeroed %u samples at sample %u.\n", burst, bstart);
    }

    int ok = rx_process_and_reassemble(rx, rx_n, file_size, file_crc, num_chunks);
    free(rx);
    return ok ? 0 : 1;
}

#endif /* !PLUTOSDR_HW */

/* ============================== PlutoSDR 硬件层 ============================== */

#ifdef PLUTOSDR_HW

#include <stdbool.h>
#include <signal.h>
#include <iio.h>

static struct iio_context  *ctx   = NULL;
static struct iio_channel  *rx0_i = NULL;
static struct iio_channel  *rx0_q = NULL;
static struct iio_channel  *tx0_i = NULL;
static struct iio_channel  *tx0_q = NULL;
static struct iio_buffer   *rxbuf = NULL;
static struct iio_buffer   *txbuf = NULL;

static void hw_shutdown(void)
{
    if (rxbuf) iio_buffer_destroy(rxbuf);
    if (txbuf) iio_buffer_destroy(txbuf);
    if (rx0_i) iio_channel_disable(rx0_i);
    if (rx0_q) iio_channel_disable(rx0_q);
    if (tx0_i) iio_channel_disable(tx0_i);
    if (tx0_q) iio_channel_disable(tx0_q);
    if (ctx)   iio_context_destroy(ctx);
    exit(1);
}

static struct iio_device *hw_find_phy(void)
{
    struct iio_device *dev = iio_context_find_device(ctx, "ad9361-phy");
    if (!dev) { fprintf(stderr, "No ad9361-phy found\n"); hw_shutdown(); }
    return dev;
}

static struct iio_device *hw_find_stream_dev(int tx)
{
    struct iio_device *dev = iio_context_find_device(ctx,
        tx ? "cf-ad9361-dds-core-lpc" : "cf-ad9361-lpc");
    if (!dev) { fprintf(stderr, "No %s stream dev\n", tx ? "TX" : "RX"); hw_shutdown(); }
    return dev;
}

static struct iio_channel *hw_find_stream_ch(struct iio_device *dev, int chid, int tx)
{
    char name[32];
    snprintf(name, sizeof(name), "voltage%d", chid);
    struct iio_channel *ch = iio_device_find_channel(dev, name, tx);
    if (!ch) {
        snprintf(name, sizeof(name), "altvoltage%d", chid);
        ch = iio_device_find_channel(dev, name, tx);
    }
    if (!ch) { fprintf(stderr, "No stream ch %d\n", chid); hw_shutdown(); }
    return ch;
}

static struct iio_channel *hw_find_phy_ch(int chid, int tx)
{
    char name[32];
    snprintf(name, sizeof(name), "voltage%d", chid);
    struct iio_channel *ch = iio_device_find_channel(hw_find_phy(), name, tx);
    if (!ch) { fprintf(stderr, "No phy ch %d\n", chid); hw_shutdown(); }
    return ch;
}

static void hw_wr_lli(struct iio_channel *ch, const char *attr, long long val)
{
    if (iio_channel_attr_write_longlong(ch, attr, val) < 0) {
        fprintf(stderr, "write %s failed\n", attr);
        hw_shutdown();
    }
}

static void hw_wr_str(struct iio_channel *ch, const char *attr, const char *val)
{
    if (iio_channel_attr_write(ch, attr, val) < 0) {
        fprintf(stderr, "write %s failed\n", attr);
        hw_shutdown();
    }
}

static void hw_wr_double(struct iio_channel *ch, const char *attr, double val)
{
    if (iio_channel_attr_write_double(ch, attr, val) < 0) {
        fprintf(stderr, "write %s failed\n", attr);
        hw_shutdown();
    }
}

static void hw_init(void)
{
    ctx = iio_create_default_context();
    if (!ctx) { fprintf(stderr, "No IIO context\n"); hw_shutdown(); }

    struct iio_device *tx = hw_find_stream_dev(1);
    struct iio_device *rx = hw_find_stream_dev(0);

    /* RX 配置 */
    struct iio_channel *ch = hw_find_phy_ch(0, 0);
    hw_wr_str(ch, "rf_port_select", HW_RX_PORT);
    hw_wr_lli(ch, "rf_bandwidth", HW_BANDWIDTH);
    hw_wr_lli(ch, "sampling_frequency", HW_SAMPLE_RATE);
    hw_wr_str(ch, "gain_control_mode", "manual");
    hw_wr_double(ch, "hardwaregain", HW_RX_GAIN);

    /* TX 配置 */
    ch = hw_find_phy_ch(0, 1);
    hw_wr_str(ch, "rf_port_select", HW_TX_PORT);
    hw_wr_lli(ch, "rf_bandwidth", HW_BANDWIDTH);
    hw_wr_lli(ch, "sampling_frequency", HW_SAMPLE_RATE);
    hw_wr_double(ch, "hardwaregain", -HW_TX_ATTEN);

    /* LO */
    struct iio_channel *lo = iio_device_find_channel(hw_find_phy(), "altvoltage0", true);
    if (lo) hw_wr_lli(lo, "frequency", HW_LO_FREQ);
    lo = iio_device_find_channel(hw_find_phy(), "altvoltage1", true);
    if (lo) hw_wr_lli(lo, "frequency", HW_LO_FREQ);

    rx0_i = hw_find_stream_ch(rx, 0, 0);
    rx0_q = hw_find_stream_ch(rx, 1, 0);
    tx0_i = hw_find_stream_ch(tx, 0, 1);
    tx0_q = hw_find_stream_ch(tx, 1, 1);

    iio_channel_enable(rx0_i);
    iio_channel_enable(rx0_q);
    iio_channel_enable(tx0_i);
    iio_channel_enable(tx0_q);

    rxbuf = iio_device_create_buffer(rx, HW_BUF_SIZE, false);
    txbuf = iio_device_create_buffer(tx, HW_BUF_SIZE, false);
    if (!rxbuf || !txbuf) { perror("create buffer"); hw_shutdown(); }
}

/* 把 float complex 基带样点按 12-bit MSB 对齐写入 TX 交织 int16 缓冲。
 * 先整缓冲清零，避免最后一包不足一块时把陈旧数据发射出去。 */
static void hw_fill_tx(const float complex *src, unsigned int n, float scale)
{
    ptrdiff_t step = iio_buffer_step(txbuf);
    char *first = (char *)iio_buffer_first(txbuf, tx0_i);
    char *end = iio_buffer_end(txbuf);

    for (char *p = first; p < end; p += step) {
        ((int16_t *)p)[0] = 0;
        ((int16_t *)p)[1] = 0;
    }

    unsigned int cnt = 0;
    for (char *p = first; p < end && cnt < n; p += step, cnt++) {
        int16_t i = (int16_t)lrintf(crealf(src[cnt]) * scale);
        int16_t q = (int16_t)lrintf(cimagf(src[cnt]) * scale);
        ((int16_t *)p)[0] = i;
        ((int16_t *)p)[1] = q;
    }
}

/* 从 RX 交织 int16 缓冲读取为 float complex */
static unsigned int hw_read_rx(float complex *dst, unsigned int max_n)
{
    ptrdiff_t step = iio_buffer_step(rxbuf);
    char *first = (char *)iio_buffer_first(rxbuf, rx0_i);
    char *end = iio_buffer_end(rxbuf);
    unsigned int cnt = 0;
    for (char *p = first; p < end && cnt < max_n; p += step, cnt++) {
        int16_t i = ((int16_t *)p)[0];
        int16_t q = ((int16_t *)p)[1];
        dst[cnt] = ((float)i / 32768.0f) + _Complex_I * ((float)q / 32768.0f);
    }
    return cnt;
}

static int hw_main(float complex *tx, unsigned int tx_n,
                   uint64_t file_size, uint32_t file_crc, uint32_t num_chunks)
{
    hw_init();
    printf("[HW] AD9361 streaming started (fs=%.1f MSPS).\n", HW_SAMPLE_RATE / 1e6);

    /* 幅度标定：峰值归一化到 0.8 满量程 */
    float peak = 1e-6f;
    for (unsigned int i = 0; i < tx_n; i++) {
        float m = cabsf(tx[i]);
        if (m > peak) peak = m;
    }
    float tx_scale = 0.8f * 32767.0f / peak;

    unsigned int rx_need = tx_n + HW_RX_MARGIN_SAMPLES;
    float complex *rx = (float complex *)calloc(rx_need, sizeof(float complex));
    unsigned int rx_collected = 0;
    unsigned int tx_sent = 0;

    while (rx_collected < rx_need) {
        if (tx_sent < tx_n) {
            unsigned int n = HW_BUF_SIZE;
            if (n > tx_n - tx_sent) n = tx_n - tx_sent;
            hw_fill_tx(tx + tx_sent, n, tx_scale);
            if (iio_buffer_push(txbuf) < 0) { perror("tx push"); hw_shutdown(); }
            tx_sent += n;
        }

        ssize_t nb = iio_buffer_refill(rxbuf);
        if (nb < 0) { perror("rx refill"); hw_shutdown(); }
        unsigned int n = (unsigned int)(nb / iio_device_get_sample_size(
            iio_context_find_device(ctx, "cf-ad9361-lpc")));
        unsigned int got = hw_read_rx(rx + rx_collected, n);
        rx_collected += got;
        if ((rx_collected & 0x3fffff) < 32768)
            printf("[HW] RX %u / %u samples, TX %u / %u\r",
                   rx_collected, rx_need, tx_sent, tx_n);
    }
    printf("\n[HW] Streaming done. RX=%u samples.\n", rx_collected);

    iio_buffer_destroy(rxbuf); rxbuf = NULL;
    iio_buffer_destroy(txbuf); txbuf = NULL;
    iio_channel_disable(rx0_i); iio_channel_disable(rx0_q);
    iio_channel_disable(tx0_i); iio_channel_disable(tx0_q);
    iio_context_destroy(ctx); ctx = NULL;

    int ok = rx_process_and_reassemble(rx, rx_collected, file_size, file_crc, num_chunks);
    free(rx);
    return ok ? 0 : 1;
}

#endif /* PLUTOSDR_HW */

/* ============================== 主函数 ============================== */

int main(void)
{
    srand((unsigned int)time(NULL));

    printf("[TX] Loading test.png...\n");
    FILE *fin = fopen("test.png", "rb");
    if (!fin) {
        fprintf(stderr, "Error: unable to open test.png for reading.\n");
        return 1;
    }
    fseek(fin, 0, SEEK_END);
    long file_size_long = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    if (file_size_long <= 0) {
        fprintf(stderr, "Error: empty or unreadable file.\n");
        fclose(fin);
        return 1;
    }
    uint64_t file_size = (uint64_t)file_size_long;

    unsigned char *file_data = (unsigned char *)malloc(file_size);
    if (fread(file_data, 1, file_size, fin) != file_size) {
        fprintf(stderr, "Error: file read failed.\n");
        fclose(fin);
        return 1;
    }
    fclose(fin);

    uint32_t file_crc = crc_generate_key(LIQUID_CRC_32, file_data, (unsigned int)file_size);
    uint32_t num_chunks = (uint32_t)((file_size + PAYLOAD_CHUNK_SIZE - 1) / PAYLOAD_CHUNK_SIZE);
    printf("[TX] File size=%llu, CRC=0x%08X, chunks=%u\n",
           (unsigned long long)file_size, file_crc, num_chunks);

    /* 计算发送采样容量 */
    packetizer pk = packetizer_create(PKT_RAW_LEN, LIQUID_CRC_32,
                                      LIQUID_FEC_HAMMING74, LIQUID_FEC_NONE);
    unsigned int enc_len = packetizer_get_enc_msg_len(pk);
    packetizer_destroy(pk);
    unsigned int enc_symbols = enc_len * 4;
    unsigned int packet_symbols = PREAMBLE_LEN + enc_symbols + GUARD_SYMBOLS;
    unsigned int total_packets = HEADER_REPEATS + num_chunks * REPEAT_FACTOR;
    unsigned int tx_capacity = (STARTUP_SYMBOLS + total_packets * packet_symbols + TAIL_SYMBOLS + 32) * TX_K;

    float complex *tx = (float complex *)malloc(tx_capacity * sizeof(float complex));
    unsigned int tx_n = build_tx_signal(tx, tx_capacity, file_data, file_size,
                                        file_crc, num_chunks);
    printf("[TX] Generated %u RF samples (%.2f M).\n", tx_n, tx_n / 1e6);

#ifdef PLUTOSDR_HW
    int rc = hw_main(tx, tx_n, file_size, file_crc, num_chunks);
#else
    int rc = sim_main(tx, tx_n, file_size, file_crc, num_chunks);
#endif

    free(tx);
    free(file_data);
    return rc;
}
