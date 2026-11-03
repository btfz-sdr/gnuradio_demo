/*
 * ofdm_pipeline.c —— 主机端 OFDM 文件自收发仿真
 *
 * 工程根目录：11-pluto_ofdm_file_self_transfer/。
 * 本文件位于 doc/01-simulation/，用于在宿主机验证 OFDM 基带算法：
 *   ofdmflexframegen / ofdmflexframesync、文件头帧、数据帧重复、逐帧重同步、
 *   文件级 CRC 重组。
 *
 * 真机版把同一套算法拆到根目录的 ofdm_phy.c/h，本文件保持单文件便于快速回归。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>
#include <liquid/liquid.h>

#define OFDM_M                 64
#define OFDM_CP                16
#define OFDM_TAPER             4
#define OFDM_GAP_SYMBOLS       2
#define PAYLOAD_CHUNK_SIZE     128
#define HEADER_PAYLOAD_LEN     32
#define HDR_USER_LEN           8
#define FRAME_TYPE_HEADER      1
#define FRAME_TYPE_DATA        2
#define HEADER_REPEATS         4
#define REPEAT_FACTOR          2
#define RX_BLOCK_SAMPLES       2048

/* ---------- 动态发送缓冲 ---------- */
typedef struct {
    float complex *d;
    unsigned int cap;
    unsigned int len;
} txbuf_t;

static void txbuf_append(txbuf_t *b, const float complex *x, unsigned int n)
{
    if (b->len + n > b->cap) {
        unsigned int nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n)
            nc *= 2;
        b->d = (float complex *)realloc(b->d, nc * sizeof(float complex));
        b->cap = nc;
    }
    memcpy(&b->d[b->len], x, n * sizeof(float complex));
    b->len += n;
}

/* ---------- 发送端 ---------- */

static ofdmflexframegen create_fg(void)
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

static unsigned int write_frame(ofdmflexframegen fg, txbuf_t *tx,
                                const unsigned char *hdr,
                                const unsigned char *payload,
                                unsigned int payload_len)
{
    ofdmflexframegen_assemble(fg, hdr, payload, payload_len);
    unsigned int frame_symbols = ofdmflexframegen_getframelen(fg);
    unsigned int frame_len = OFDM_M + OFDM_CP;
    unsigned int total = (frame_symbols + 1 + OFDM_GAP_SYMBOLS) * frame_len;

    float complex *tmp = (float complex *)malloc(total * sizeof(float complex));
    ofdmflexframegen_write(fg, tmp, total);
    txbuf_append(tx, tmp, total);
    free(tmp);
    return total;
}

static unsigned int build_tx(txbuf_t *tx, const unsigned char *file_data,
                             uint64_t file_size, uint32_t file_crc,
                             uint32_t num_chunks)
{
    ofdmflexframegen fg = create_fg();
    if (!fg)
        return 0;

    unsigned char hdr[HDR_USER_LEN];
    unsigned char header_payload[HEADER_PAYLOAD_LEN];
    memset(header_payload, 0, sizeof(header_payload));
    memcpy(header_payload + 0,  &file_size, 8);
    memcpy(header_payload + 8,  &file_crc,  4);
    memcpy(header_payload + 12, &num_chunks, 4);

    unsigned int total = 0;

    for (unsigned int r = 0; r < HEADER_REPEATS; r++) {
        memset(hdr, 0, sizeof(hdr));
        hdr[0] = FRAME_TYPE_HEADER;
        hdr[1] = (unsigned char)r;
        total += write_frame(fg, tx, hdr, header_payload, HEADER_PAYLOAD_LEN);
    }

    unsigned char data_payload[PAYLOAD_CHUNK_SIZE];
    for (unsigned int r = 0; r < REPEAT_FACTOR; r++) {
        for (uint32_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
            uint64_t off = (uint64_t)chunk_idx * PAYLOAD_CHUNK_SIZE;
            uint64_t remaining = file_size > off ? file_size - off : 0;
            unsigned int cur = remaining < PAYLOAD_CHUNK_SIZE
                               ? (unsigned int)remaining : PAYLOAD_CHUNK_SIZE;

            memset(data_payload, 0, sizeof(data_payload));
            memcpy(data_payload, file_data + off, cur);

            memset(hdr, 0, sizeof(hdr));
            hdr[0] = FRAME_TYPE_DATA;
            hdr[1] = (unsigned char)r;
            memcpy(hdr + 2, &chunk_idx, 4);
            total += write_frame(fg, tx, hdr, data_payload, PAYLOAD_CHUNK_SIZE);
        }
    }

    ofdmflexframegen_destroy(fg);
    return total;
}

/* ---------- 接收端 ---------- */

typedef struct {
    int mode;
    int got_header;
    uint64_t file_size;
    uint32_t file_crc;
    uint32_t num_chunks;
    unsigned char *file_buf;
    unsigned char *chunk_flags;
    unsigned int decoded_chunks;
    unsigned int total_frames;
} rx_ctx_t;

static int rx_callback(unsigned char *header, int header_valid,
                       unsigned char *payload, unsigned int payload_len,
                       int payload_valid, framesyncstats_s stats, void *userdata)
{
    rx_ctx_t *c = (rx_ctx_t *)userdata;
    (void)stats;

    c->total_frames++;
    if (!header_valid)
        return 0;

    unsigned char type = header[0];

    if (c->mode == 0) {
        if (type == FRAME_TYPE_HEADER && payload_valid &&
            payload_len >= HEADER_PAYLOAD_LEN && !c->got_header) {
            memcpy(&c->file_size, payload + 0, 8);
            memcpy(&c->file_crc,  payload + 8, 4);
            memcpy(&c->num_chunks, payload + 12, 4);
            c->got_header = 1;
            printf("  [sim] OFDM header: size=%llu, crc=0x%08X, chunks=%u\n",
                   (unsigned long long)c->file_size, c->file_crc, c->num_chunks);
        }
    } else {
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

static void run_sync(ofdmflexframesync fs, const float complex *rx, unsigned int n)
{
    for (unsigned int i = 0; i < n; i += RX_BLOCK_SAMPLES) {
        unsigned int b = n - i < RX_BLOCK_SAMPLES ? n - i : RX_BLOCK_SAMPLES;
        ofdmflexframesync_execute(fs, (float complex *)&rx[i], b);
    }
}

/* ---------- 仿真信道 ---------- */

static float env_f(const char *name, float def)
{
    const char *s = getenv(name);
    return s ? (float)atof(s) : def;
}

static float env_i(const char *name, int def)
{
    const char *s = getenv(name);
    return s ? (float)atoi(s) : (float)def;
}

int main(void)
{
    const char *in_file  = "test.png";
    const char *out_file = "recovered.png";

    float cfo       = env_f("SIM_CFO", 0.02f);
    float phase     = env_f("SIM_PHASE", -1.0f); /* -1 表示随机 */
    float noise     = env_f("SIM_NOISE", 0.05f);
    float gain      = env_f("SIM_GAIN", 1.0f);
    unsigned int lead  = (unsigned int)env_i("SIM_LEAD", 0);
    unsigned int burst = (unsigned int)env_i("SIM_BURST", 0);
    float burst_pos    = env_f("SIM_BURST_POS", 0.5f);

    if (phase < 0.0f)
        phase = (float)(2.0 * M_PI * rand() / RAND_MAX);

    printf("== OFDM pipeline simulation ==\n");
    printf("cfo=%.5f phase=%.3f noise=%.3f gain=%.3f lead=%u burst=%u pos=%.2f\n",
           cfo, phase, noise, gain, lead, burst, burst_pos);

    /* 1. 读文件 */
    FILE *fin = fopen(in_file, "rb");
    if (!fin) {
        fprintf(stderr, "cannot open %s\n", in_file);
        return 1;
    }
    fseek(fin, 0, SEEK_END);
    long fsz_long = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    if (fsz_long <= 0) {
        fprintf(stderr, "empty file\n");
        fclose(fin);
        return 1;
    }
    uint64_t file_size = (uint64_t)fsz_long;
    unsigned char *file_data = (unsigned char *)malloc(file_size);
    fread(file_data, 1, file_size, fin);
    fclose(fin);

    uint32_t file_crc = crc_generate_key(LIQUID_CRC_32, file_data, (unsigned int)file_size);
    uint32_t num_chunks = (uint32_t)((file_size + PAYLOAD_CHUNK_SIZE - 1) / PAYLOAD_CHUNK_SIZE);
    printf("file=%s size=%llu crc=0x%08X chunks=%u\n",
           in_file, (unsigned long long)file_size, file_crc, num_chunks);

    /* 2. 生成发送信号 */
    txbuf_t tx = {0};
    unsigned int tx_n = build_tx(&tx, file_data, file_size, file_crc, num_chunks);
    printf("tx_n=%u samples (%.2f M)\n", tx_n, tx_n / 1e6f);

    /* 3. 信道损伤 */
    unsigned int rx_n = lead + tx_n;
    float complex *rx = (float complex *)calloc(rx_n, sizeof(float complex));

    for (unsigned int i = 0; i < tx_n; i++) {
        float complex s = tx.d[i] * gain * cexpf(_Complex_I * (cfo * (float)i + phase));
        rx[lead + i] = s;
    }

    /* 高斯噪声 */
    for (unsigned int i = 0; i < rx_n; i++) {
        float ni = (float)rand() / RAND_MAX * 2.0f - 1.0f;
        float nq = (float)rand() / RAND_MAX * 2.0f - 1.0f;
        rx[i] += (ni + _Complex_I * nq) * noise;
    }

    /* 中途突发清零 */
    if (burst > 0) {
        unsigned int pos = (unsigned int)(burst_pos * (float)rx_n);
        unsigned int end = pos + burst;
        if (end > rx_n) end = rx_n;
        for (unsigned int i = pos; i < end; i++)
            rx[i] = 0.0f;
    }

    /* 4. OFDM 同步 + 两遍解码 */
    unsigned char p[OFDM_M];
    ofdmframe_init_default_sctype(OFDM_M, p);

    rx_ctx_t c;
    memset(&c, 0, sizeof(c));

    ofdmflexframesync fs = ofdmflexframesync_create(OFDM_M, OFDM_CP, OFDM_TAPER,
                                                    p, rx_callback, &c);
    ofdmflexframesync_set_header_len(fs, HDR_USER_LEN);

    c.mode = 0;
    run_sync(fs, rx, rx_n);
    printf("pass1: frames=%u header=%s\n", c.total_frames, c.got_header ? "OK" : "MISSING");

    if (!c.got_header) {
        printf("FAIL: no OFDM file header\n");
        ofdmflexframesync_destroy(fs);
        free(rx);
        free(tx.d);
        free(file_data);
        return 1;
    }

    c.file_buf = (unsigned char *)calloc(c.file_size > 0 ? c.file_size : 1, 1);
    c.chunk_flags = (unsigned char *)calloc(c.num_chunks > 0 ? c.num_chunks : 1, 1);

    ofdmflexframesync_reset(fs);
    c.mode = 1;
    c.total_frames = 0;
    run_sync(fs, rx, rx_n);
    printf("pass2: frames=%u decoded=%u/%u chunks\n",
           c.total_frames, c.decoded_chunks, c.num_chunks);

    ofdmflexframesync_destroy(fs);

    /* 5. 文件 CRC + 写出 */
    int success = 0;
    if (c.decoded_chunks == c.num_chunks) {
        uint32_t crc_rx = crc_generate_key(LIQUID_CRC_32, c.file_buf, (unsigned int)c.file_size);
        if (crc_rx == c.file_crc) {
            FILE *fout = fopen(out_file, "wb");
            if (fout) {
                fwrite(c.file_buf, 1, c.file_size, fout);
                fclose(fout);
                success = 1;
                printf("PASS: file CRC OK, wrote %s\n", out_file);
            } else {
                printf("FAIL: cannot write %s\n", out_file);
            }
        } else {
            printf("FAIL: CRC mismatch (rx=0x%08X tx=0x%08X)\n", crc_rx, c.file_crc);
        }
    } else {
        printf("FAIL: %u chunks missing\n", c.num_chunks - c.decoded_chunks);
    }

    free(c.file_buf);
    free(c.chunk_flags);
    free(rx);
    free(tx.d);
    free(file_data);
    return success ? 0 : 1;
}
