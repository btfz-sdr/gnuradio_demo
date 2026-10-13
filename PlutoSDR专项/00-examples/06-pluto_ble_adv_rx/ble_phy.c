// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ble_phy.c - BLE (LE 1M) GFSK receiver front-end for AD9361 / PlutoSDR
 *
 * The chain mirrors gnuradio's "GFSK Demod" (digital.gfsk_demod =
 * analog.quadrature_demod_cf -> digital.clock_recovery_mm_ff ->
 * digital.binary_slicer_fb) with two deliberate differences:
 *
 *  1) A 1-symbol boxcar (integrate & dump) follows the quadrature demodulator.
 *     That is the matched filter of an FSK receiver: it averages sps samples
 *     per symbol and buys ~10 dB of Eb/N0 that a single per-sample
 *     discriminator sample does not have.
 *
 *  2) The symbol clock is recovered *feed-forward*: the access-address
 *     correlator runs on all `sps` timing phases in parallel -- every sample
 *     pushes one bit into the 40-bit window of phase (sample % sps), and only
 *     that one window is tested.  Cost is O(1) per sample, i.e. the same
 *     order as the single-phase hard search of the previous version but with
 *     the whole phase space covered.
 *     A feedback Mueller&Muller loop (gnuradio's clock_recovery_mm_ff) needs a
 *     long preamble to acquire and has a second, useless equilibrium exactly
 *     on the eye crossing; on a real 8-symbol BLE preamble it frequently
 *     settles there (soft amplitude ~0.16 instead of ~1.5 in measurements).
 *
 * Everything is float, no dynamic allocation, static state only.
 *
 *   IQ -> quad demod -> boxcar(sps) -> bit
 *      -> per-phase 40-bit windows
 *      -> AA Hamming search -> collect PDU bits -> dewhiten -> CRC-24
 */
#include "ble_phy.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ tuning */
#define BLE_SYMBOL_RATE_HZ 1000000.0     /* LE 1M PHY: 1 Msym/s                */
#define MAX_SPS            16            /* largest samples/symbol we support  */
#define AA_BITS            40            /* 8 bit preamble + 32 bit access addr */
#define AA_ERR_MAX         3             /* accept <= 3 bit errors in AA window */
#define AA_ERR_REPORT      2             /* print candidates up to this many errors */
#define MAX_PDU_BYTES      48
#define AA_SLOT_BYTES      (MAX_PDU_BYTES + 3)
#define AA_SLOT_BITS       (AA_BITS + 8 * AA_SLOT_BYTES)

/* 关于 DC / 频偏：本解调器**不做** DC 扣除。
 *
 * 曾经加过"门控慢速 DC 跟踪"（只喂 |d| < 0.5 rad 的样点，时间常数 13 ms），
 * 理由是有一次抓包里鉴频输出带 0.086 rad/样点（≈137 kHz）的偏置。
 * 后来在 2.1 s 真实录波上量化，结论是它**有害**：
 *   - 该跟踪器收敛到的是**噪声的不对称性**（实测 -0.061 rad/样点），
 *     而包所在处的真实偏置约 0（原始鉴频输出在包内就是围绕 0 的）；
 *   - 扣掉这个"噪声偏置"等于把眼图整体平移 0.6（信号只有 ±1.5），
 *     弱符号成片翻错：同一段录波 门控 DC 6 包 vs 不扣 DC 16 包
 *     （独立 Python 参考实现同一段是 18 包）。
 * 因此链路回到与 GNU Radio/参考实现一致：鉴频 → 匹配滤波 → 判决，不扣 DC。
 * 如果哪天真的看到眼图整体偏在某一侧，先查增益（削顶会产生真实的鉴频偏置），
 * 不要用盲目估计器去补。 */

/* ------------------------------------------------------------------ state */
static int      s_sps     = 10;          /* samples/symbol (10 MSps / 1 Mbps) */
static int      s_channel = 37;
static int      s_ready   = 0;

static uint32_t s_pat_lo, s_pat_hi;      /* AA pattern in window layout */

static uint32_t s_lo[MAX_SPS], s_hi[MAX_SPS];   /* 40-bit window per timing phase */
static float    s_prev_i = 0.0f, s_prev_q = 1.0f;
static float    s_ring[MAX_SPS];         /* boxcar history */
static int      s_w = 0;                 /* boxcar write pointer */
static float    s_mf = 0.0f;             /* matched filter accumulator */
static int      s_phase = 0;             /* sample index % sps */

/* 每个定时相位各自一路收包状态。
 *
 * 为什么要并行：40 bit 的 AA 对采样时刻不敏感（判断裕量大），后面 ~300 bit 的
 * PDU 不是 —— 实测同一个真实包平均只有 ~5/10 个定时相位能通过 CRC。
 * 只认"最先命中 AA 的那个相位"会丢掉一半以上本可解出的包。
 * 所以每个相位只要 AA 命中就各自起一路收包，谁先过 CRC 谁算数；
 * 同一个包会有多路同时成功，靠 100 样点的报告去重只报一次。 */
typedef struct {
    int       active;
    int       err;                  /* 该路 AA 的汉明误差 */
    int       nbits;
    int       nsamp;                /* 该路已收样点数（超时放弃） */
    uint8_t   bits[AA_SLOT_BITS];
} coll_t;

static coll_t    s_coll[MAX_SPS];
static long long s_samp = 0;             /* global sample counter */
static long long s_last_pkt_samp = -1000000;  /* last reported packet (dedup) */

static uint8_t  s_pop8[256];

static long     s_calls = 0, s_samples = 0, s_cand = 0, s_pkt = 0;

/* ------------------------------------------------------------------ utils */
static void build_pop8(void)
{
    for (int i = 0; i < 256; i++) {
        int n = 0, v = i;
        while (v) { n += v & 1; v >>= 1; }
        s_pop8[i] = (uint8_t)n;
    }
}

static inline int pop_hamming(uint32_t a)
{
    return s_pop8[a & 0xFF] + s_pop8[(a >> 8) & 0xFF] +
           s_pop8[(a >> 16) & 0xFF] + s_pop8[(a >> 24) & 0xFF];
}

/* The window shifts left (newest bit in bit 0), so the FIRST transmitted bit
 * ends up in bit 39.
 *
 * NOTE ON BIT ORDER (this bit the "32/40 ceiling" bug): BLE transmits the
 * access address LSB-first, so for the advertising address 0x8E89BED6 the
 * FIRST AA byte on air is 0xD6 (its LSB), then 0xBE, 0x89, 0x8E.  Writing the
 * pattern out as the bytes {0x8E,0x89,0xBE,0xD6} in reading order produces the
 * time-reversed address, against which no real packet can ever match - the
 * correlation then sits at chance level (~20/40) no matter how good the
 * demodulator is.  Always derive the pattern from the 32-bit *value*. */
static void build_aa_pattern(void)
{
    const uint32_t aa = 0x8E89BED6u;   /* BLE advertising access address */
    uint64_t v = 0;
    int k = 0;
    for (int b = 0; b < 8; b++)
        v |= (uint64_t)((0xAAu >> b) & 1u) << (AA_BITS - 1 - k++);   /* preamble */
    for (int b = 0; b < 32; b++)
        v |= (uint64_t)((aa >> b) & 1u) << (AA_BITS - 1 - k++);      /* AA, LSB first */
    s_pat_lo = (uint32_t)(v & 0xFFFFFFFFu);
    s_pat_hi = (uint32_t)((v >> 32) & 0xFFu);
}

static void ble_reset_state(void)
{
    memset(s_lo, 0, sizeof(s_lo));
    memset(s_hi, 0, sizeof(s_hi));
    memset(s_ring, 0, sizeof(s_ring));
    s_prev_i = 0.0f; s_prev_q = 1.0f; s_w = 0; s_mf = 0.0f; s_phase = 0;
    memset(s_coll, 0, sizeof(s_coll));
}

/* Optional: tell the demodulator the RX sample rate in MHz (default 10 MHz) */
void ble_phy_set_sample_rate(double fs_mhz)
{
    int sps = (int)((fs_mhz * 1e6) / BLE_SYMBOL_RATE_HZ + 0.5);
    if (sps < 2) sps = 2;
    if (sps > MAX_SPS) sps = MAX_SPS;
    if (sps != s_sps) {
        s_sps = sps;
        s_ready = 0;
    }
}

/* ------------------------- BLE whitening / CRC-24 (unchanged, verified) */
uint8_t bt_swap_bits(uint8_t x)
{
    x = (uint8_t)(((x & 0xF0) >> 4) | ((x & 0x0F) << 4));
    x = (uint8_t)(((x & 0xCC) >> 2) | ((x & 0x33) << 2));
    x = (uint8_t)(((x & 0xAA) >> 1) | ((x & 0x55) << 1));
    return x;
}

void bt_dewhitening(const uint8_t *data, int data_len, int channel, uint8_t *dst)
{
    uint8_t lfsr = bt_swap_bits((uint8_t)channel) | 2;
    const uint8_t masks[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
    for (int k = 0; k < data_len; k++) {
        uint8_t d = bt_swap_bits(data[k]);
        for (int i = 0; i < 8; i++) {
            if (lfsr & 0x80) {
                lfsr ^= 0x11;
                d ^= masks[i];
            }
            lfsr <<= 1;
        }
        dst[k] = bt_swap_bits(d);
    }
}

void bt_crc(const uint8_t *data, int length, uint8_t *dst)
{
    uint8_t ret[3] = { (0x555555 >> 16) & 0xff, (0x555555 >> 8) & 0xff, 0x555555 & 0xff };

    for (int i = 0; i < length; i++) {
        uint8_t d = data[i];
        for (int v = 0; v < 8; v++) {
            uint32_t t = (ret[0] >> 7) & 1;

            ret[0] <<= 1;
            if (ret[1] & 0x80) ret[0] |= 1;

            ret[1] <<= 1;
            if (ret[2] & 0x80) ret[1] |= 1;

            ret[2] <<= 1;

            if ((d & 1) != t) {
                ret[2] ^= 0x5b;
                ret[1] ^= 0x06;
            }
            d >>= 1;
        }
    }

    dst[0] = bt_swap_bits(ret[0]);
    dst[1] = bt_swap_bits(ret[1]);
    dst[2] = bt_swap_bits(ret[2]);
}

/* ------------------------------------------------------------------ decode */
/* Try to finish the packet of one collection (c).
 * Returns 1 when the packet is done (decoded, or rejected as invalid) and that
 * collection may be freed, 0 when more bits are needed. */
static int ble_try_decode(coll_t *c, int channel)
{
    int nbytes = (c->nbits - AA_BITS) / 8;
    uint8_t raw[AA_SLOT_BYTES], dw[AA_SLOT_BYTES], calc[3];

    if (nbytes < 2)
        return 0;                       /* need the PDU header first */

    for (int k = 0; k < nbytes; k++) {
        uint8_t v = 0;
        const uint8_t *b = &c->bits[AA_BITS + k * 8];
        for (int j = 0; j < 8; j++)
            v |= (uint8_t)(b[j] << j);          /* LSB of each byte is sent first */
        raw[k] = v;
    }

    bt_dewhitening(raw, nbytes, channel, dw);

    int plen = dw[1] & 0x3F;
    int tot  = 2 + plen + 3;
    if (tot < 6 || tot > MAX_PDU_BYTES)
        return 1;                       /* impossible length: false sync, give up */
    if (nbytes < tot)
        return 0;                       /* wait for the remaining payload bytes */

    bt_crc(dw, tot - 3, calc);
    if (memcmp(calc, &dw[tot - 3], 3) != 0)
        return 1;

    /* 同一个包会在相邻的几个定时相位上都能解出来，只报一次。
     * 真实 BLE 包至少 88 个符号 = 880 样点，100 样点的窗口不可能吃掉隔壁的包。 */
    if (s_samp - s_last_pkt_samp < 100)
        return 1;
    s_last_pkt_samp = s_samp;

    s_pkt++;
    printf("\n==================================================================\n");
    printf("[PKT #%ld] BLE 广播包 (CH %d, 位置 %lld, AA 误差 %d/40, PDU %d bytes, CRC OK)\n",
           s_pkt, channel, (long long)s_samp, c->err, plen + 2);
    printf("[PDU] ");
    for (int k = 0; k < tot; k++) printf("%02X ", dw[k]);
    printf("\n");
    printf("[ADV] type=0x%X  AdvA=%02X:%02X:%02X:%02X:%02X:%02X\n",
           dw[0] & 0x0F, dw[7], dw[6], dw[5], dw[4], dw[3], dw[2]);
    printf("==================================================================\n");
    fflush(stdout);
    return 1;
}

/* ------------------------------------------------------------------ entry */
void demodulate_ble_packets(const int16_t *iq_samples, int num_samples, int channel)
{
    if (!s_ready || channel != s_channel) {
        s_channel = channel;
        build_pop8();
        build_aa_pattern();
        ble_reset_state();
        s_calls = s_samples = s_cand = s_pkt = 0;
        printf("[BLE] 解调链: 正交鉴频 -> %d点匹配滤波(1符号积分) -> "
               "全 %d 个定时相位并行搜 AA + 每相位各一路收包\n", s_sps, s_sps);
        printf("[BLE] AA 门限 %d/%d bit, 白化种子 = 信道 %d\n",
               AA_BITS - AA_ERR_MAX, AA_BITS, channel);
        fflush(stdout);
        s_ready = 1;
    }

    s_calls++;
    s_samples += num_samples;

    for (int i = 0; i < num_samples; i++, s_samp++) {
        float ii = (float)iq_samples[2 * i];
        float qq = (float)iq_samples[2 * i + 1];

        /* 1. quadrature demodulator (same maths as gnuradio quadrature_demod_cf) */
        float ci = ii * s_prev_i + qq * s_prev_q;   /* Re{x[n] conj(x[n-1])} */
        float cq = qq * s_prev_i - ii * s_prev_q;   /* Im{x[n] conj(x[n-1])} */
        float d  = atan2f(cq, ci);
        s_prev_i = ii;
        s_prev_q = qq;

        /* 2. 匹配滤波（1 符号积分）：不要在这里做 DC/频偏扣除，原因见文件头 */
        float x = d;

        /* 3. 1-symbol boxcar = matched filter (integrate & dump) */
        if (++s_w >= s_sps) s_w = 0;
        s_mf += x - s_ring[s_w];
        s_ring[s_w] = x;

        /* 4. hard bit into the window of this sample's timing phase */
        int ph = s_phase;
        uint8_t bit = (s_mf > 0.0f) ? 1u : 0u;
        uint32_t lo = (s_lo[ph] << 1) | bit;
        uint32_t hi = ((s_hi[ph] << 1) | (s_lo[ph] >> 31)) & 0xFFu;
        s_lo[ph] = lo;
        s_hi[ph] = hi;

        if (++s_phase >= s_sps) s_phase = 0;

        /* 5. access-address search: one 40-bit Hamming compare per sample.
         *
         * 这一级必须一直跑（收包期间也不停）：同一个包在相邻几个定时相位上都会
         * 命中，而 **AA 命中的那个相位不一定是能把 PDU 解出来的那个** ——
         * 40 bit 的 AA 对采样时刻不敏感，后面 ~300 bit 的 PDU 不是。
         * 实测（2.1 s 真实录波）同一个包平均只有 ~5/10 个相位能过 CRC，
         * 只认最先命中的相位会丢掉一半以上本可解出的包。
         * 所以每个相位各自起一路收包，谁先过 CRC 谁算数。 */
        int err = pop_hamming(lo ^ s_pat_lo) + pop_hamming(hi ^ s_pat_hi);
        int started = 0;
        if (err <= AA_ERR_MAX) {
            coll_t *c = &s_coll[ph];
            if (!c->active || err < c->err) {    /* 同一路里更优的窗口可以接管 */
                started = 1;                     /* 当前样点的 bit 已含在窗口里 */
                s_cand++;
                if (err <= AA_ERR_REPORT) {
                    // printf("[AA ] 定时相位 %d, 误差 %d/%d\n", ph, err, AA_BITS);
                    fflush(stdout);
                }
                /* seed the payload buffer with the 40 bits already in the window
                 * (window bit 39 = oldest = first transmitted -> bits[0]) */
                for (int k = 0; k < AA_BITS; k++) {
                    int j = AA_BITS - 1 - k;
                    c->bits[k] = (uint8_t)((j >= 32) ? ((hi >> (j - 32)) & 1u)
                                                     : ((lo >> j) & 1u));
                }
                c->nbits = AA_BITS;
                c->err   = err;
                c->nsamp = 0;
                c->active = 1;
            }
        }

        /* 6. payload collection: this sample's bit belongs to phase `ph`,
         *    so only that phase's collection advances (1 bit per symbol). */
        {
            coll_t *c = &s_coll[ph];
            if (c->active && !started) {
                if (c->nbits < AA_SLOT_BITS)
                    c->bits[c->nbits++] = bit;
                if (((c->nbits - AA_BITS) % 8) == 0 && ble_try_decode(c, channel))
                    c->active = 0;
                else if (++c->nsamp > AA_SLOT_BITS * s_sps)
                    c->active = 0;      /* nothing usable arrived: abandon */
            }
        }
    }

    if ((s_calls % 50) == 0) {
        printf("[STAT] 已处理 %.1f M样点 | AA 候选 %ld | 成功解包 %ld\n",
               s_samples / 1e6, s_cand, s_pkt);
        fflush(stdout);
    }
}
