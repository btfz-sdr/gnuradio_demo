// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pluto_qpsk_file_self_transfer.c
 *
 * PlutoSDR(AD9361) 单板自收发 QPSK 文件传输主程序。
 *
 * 架构：
 *   1) 读取文件 -> 计算 CRC / 分片数
 *   2) qpsk_phy_build_tx() 生成完整基带 IQ（前导 + DQPSK 数据包）
 *   3) 配置 AD9361 RX/TX -> TX 用硬件循环缓冲整段推流一次
 *   4) RX 非循环缓冲连续 refill，采集 tx_n + margin 个样本
 *   5) qpsk_phy_rx_process() 完成定时/检测/解码/重组/CRC
 *
 * 基带算法在 qpsk_phy.c / qpsk_phy.h；本文件只负责文件与 AD9361 I/O。
 * 工程根目录：本文件所在目录（10-pluto_qpsk_file_self_transfer）。
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>
#include <iio.h>

#include "qpsk_phy.h"

#define MHZ(x) ((long long)((x) * 1000000.0 + 0.5))
#define GHZ(x) ((long long)((x) * 1000000000.0 + 0.5))

#define IIO_ENSURE(expr) { \
    if (!(expr)) { \
        (void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
        (void) abort(); \
    } \
}

/* RX is input, TX is output */
enum iodev { RX, TX };

/* common RX and TX streaming params */
struct stream_cfg {
    long long bw_hz;    /* analog bandwidth */
    long long fs_hz;    /* baseband sample rate */
    long long lo_hz;    /* local oscillator frequency */
    const char *rfport; /* port name */
};

static char tmpstr[64];

static struct iio_context *ctx   = NULL;
static struct iio_channel *rx0_i = NULL;
static struct iio_channel *rx0_q = NULL;
static struct iio_channel *tx0_i = NULL;
static struct iio_channel *tx0_q = NULL;
static struct iio_buffer  *rxbuf = NULL;
static struct iio_buffer  *txbuf = NULL;

static bool stop;

/* ---------- 默认参数 ---------- */
#define DEFAULT_LO_MHZ       2500.0
#define DEFAULT_SAMP_MHZ     2.5
#define DEFAULT_BW_MHZ       2.0
#define DEFAULT_RX_GAIN_DB   40.0
#define DEFAULT_TX_ATTN_DB   10.0
#define DEFAULT_IN_FILE      "test.png"
#define DEFAULT_OUT_FILE     "recovered.png"
#define RX_BUF_SAMPLES       (256 * 1024)   /* 一次 refill 的样本数 */
#define RX_MARGIN_SAMPLES    200000         /* 尾部多收余量 */

/* ---------- cleanup ---------- */
static void _shutdown(void)
{
    printf("* Destroying buffers\n");
    if (rxbuf) { iio_buffer_destroy(rxbuf); }
    if (txbuf) { iio_buffer_destroy(txbuf); }

    printf("* Disabling streaming channels\n");
    if (rx0_i) { iio_channel_disable(rx0_i); }
    if (rx0_q) { iio_channel_disable(rx0_q); }
    if (tx0_i) { iio_channel_disable(tx0_i); }
    if (tx0_q) { iio_channel_disable(tx0_q); }

    printf("* Destroying context\n");
    if (ctx) { iio_context_destroy(ctx); }

    printf("* Resources cleaned up.\n");
    exit(0);
}

static void handle_sig(int sig)
{
    printf("Waiting for process to finish... Got signal %d\n", sig);
    stop = true;
}

static void errchk(int v, const char *what)
{
    if (v < 0) {
        fprintf(stderr, "Error %d writing to channel \"%s\"\n", v, what);
        _shutdown();
    }
}

static void wr_ch_lli(struct iio_channel *chn, const char *what, long long val)
{
    errchk(iio_channel_attr_write_longlong(chn, what, val), what);
}

static void wr_ch_str(struct iio_channel *chn, const char *what, const char *str)
{
    errchk(iio_channel_attr_write(chn, what, str), what);
}

static void wr_ch_double(struct iio_channel *chn, const char *what, double val)
{
    errchk(iio_channel_attr_write_double(chn, what, val), what);
}

static char *get_ch_name(const char *type, int id)
{
    snprintf(tmpstr, sizeof(tmpstr), "%s%d", type, id);
    return tmpstr;
}

static struct iio_device *get_ad9361_phy(void)
{
    struct iio_device *dev = iio_context_find_device(ctx, "ad9361-phy");
    IIO_ENSURE(dev && "No ad9361-phy found");
    return dev;
}

static bool get_ad9361_stream_dev(enum iodev d, struct iio_device **dev)
{
    switch (d) {
    case TX: *dev = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc"); return *dev != NULL;
    case RX: *dev = iio_context_find_device(ctx, "cf-ad9361-lpc"); return *dev != NULL;
    default: IIO_ENSURE(0); return false;
    }
}

static bool get_ad9361_stream_ch(enum iodev d, struct iio_device *dev, int chid,
                                 struct iio_channel **chn)
{
    *chn = iio_device_find_channel(dev, get_ch_name("voltage", chid), d == TX);
    if (!*chn)
        *chn = iio_device_find_channel(dev, get_ch_name("altvoltage", chid), d == TX);
    return *chn != NULL;
}

static bool get_phy_chan(enum iodev d, int chid, struct iio_channel **chn)
{
    switch (d) {
    case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), false); return *chn != NULL;
    case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), true);  return *chn != NULL;
    default: IIO_ENSURE(0); return false;
    }
}

static bool get_lo_chan(enum iodev d, struct iio_channel **chn)
{
    switch (d) {
    case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("altvoltage", 0), true); return *chn != NULL;
    case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("altvoltage", 1), true); return *chn != NULL;
    default: IIO_ENSURE(0); return false;
    }
}

static bool cfg_ad9361_streaming_ch(struct stream_cfg *cfg, enum iodev type, int chid)
{
    struct iio_channel *chn = NULL;

    printf("* Acquiring AD9361 phy channel %d\n", chid);
    if (!get_phy_chan(type, chid, &chn)) { return false; }
    wr_ch_str(chn, "rf_port_select", cfg->rfport);
    wr_ch_lli(chn, "rf_bandwidth", cfg->bw_hz);
    wr_ch_lli(chn, "sampling_frequency", cfg->fs_hz);

    printf("* Acquiring AD9361 %s lo channel\n", type == TX ? "TX" : "RX");
    if (!get_lo_chan(type, &chn)) { return false; }
    wr_ch_lli(chn, "frequency", cfg->lo_hz);
    return true;
}

/* ---------- 帮助 ---------- */
static void print_help(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -d <uri>   PlutoSDR 设备 URI (默认: default)\n");
    printf("  -i <file>  待发送输入文件 (默认: %s)\n", DEFAULT_IN_FILE);
    printf("  -o <file>  接收重组输出文件 (默认: %s)\n", DEFAULT_OUT_FILE);
    printf("  -l <MHz>   中心频率 (默认: %.1f)\n", DEFAULT_LO_MHZ);
    printf("  -s <MHz>   采样率 (默认: %.2f)\n", DEFAULT_SAMP_MHZ);
    printf("  -b <MHz>   模拟带宽 (默认: %.1f)\n", DEFAULT_BW_MHZ);
    printf("  -g <dB>    RX 手动增益 (默认: %.1f)\n", DEFAULT_RX_GAIN_DB);
    printf("  -a <dB>    TX 衰减 (默认: %.1f)\n", DEFAULT_TX_ATTN_DB);
    printf("  -h         显示帮助\n");
}

int main(int argc, char **argv)
{
    struct iio_device *tx_dev;
    struct iio_device *rx_dev;
    struct stream_cfg rxcfg;
    struct stream_cfg txcfg;

    char param_dev[64] = "default";
    char param_in[128] = DEFAULT_IN_FILE;
    char param_out[128] = DEFAULT_OUT_FILE;
    double param_lo_mhz = DEFAULT_LO_MHZ;
    double param_samp_mhz = DEFAULT_SAMP_MHZ;
    double param_bw_mhz = DEFAULT_BW_MHZ;
    double param_rx_gain_db = DEFAULT_RX_GAIN_DB;
    double param_tx_attn_db = DEFAULT_TX_ATTN_DB;

    int opt;
    while ((opt = getopt(argc, argv, "d:i:o:l:s:b:g:a:h")) != -1) {
        switch (opt) {
        case 'd': strncpy(param_dev, optarg, sizeof(param_dev) - 1); break;
        case 'i': strncpy(param_in, optarg, sizeof(param_in) - 1); break;
        case 'o': strncpy(param_out, optarg, sizeof(param_out) - 1); break;
        case 'l': param_lo_mhz = atof(optarg); break;
        case 's': param_samp_mhz = atof(optarg); break;
        case 'b': param_bw_mhz = atof(optarg); break;
        case 'g': param_rx_gain_db = atof(optarg); break;
        case 'a': param_tx_attn_db = atof(optarg); break;
        case 'h':
        default:
            print_help(argv[0]);
            return 0;
        }
    }

    signal(SIGINT, handle_sig);

    /* ---------- 1. 读文件 ---------- */
    printf("[FILE] Loading %s ...\n", param_in);
    FILE *fin = fopen(param_in, "rb");
    if (!fin) {
        fprintf(stderr, "[FILE] cannot open %s\n", param_in);
        return 1;
    }
    fseek(fin, 0, SEEK_END);
    long file_size_long = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    if (file_size_long <= 0) {
        fprintf(stderr, "[FILE] empty file\n");
        fclose(fin);
        return 1;
    }
    uint64_t file_size = (uint64_t)file_size_long;
    unsigned char *file_data = (unsigned char *)malloc(file_size);
    if (fread(file_data, 1, file_size, fin) != file_size) {
        fprintf(stderr, "[FILE] read failed\n");
        fclose(fin);
        free(file_data);
        return 1;
    }
    fclose(fin);

    uint32_t file_crc = crc_generate_key(LIQUID_CRC_32, file_data, (unsigned int)file_size);
    uint32_t num_chunks = (uint32_t)((file_size + PAYLOAD_CHUNK_SIZE - 1) / PAYLOAD_CHUNK_SIZE);
    printf("[FILE] size=%llu, crc=0x%08X, chunks=%u\n",
           (unsigned long long)file_size, file_crc, num_chunks);

    /* ---------- 2. 生成基带 ---------- */
    packetizer pk = packetizer_create(PKT_RAW_LEN, LIQUID_CRC_32,
                                      LIQUID_FEC_HAMMING74, LIQUID_FEC_NONE);
    unsigned int enc_len = packetizer_get_enc_msg_len(pk);
    packetizer_destroy(pk);
    unsigned int enc_symbols = enc_len * 4;
    unsigned int packet_symbols = PREAMBLE_LEN + enc_symbols + GUARD_SYMBOLS;
    unsigned int total_packets = HEADER_REPEATS + num_chunks * REPEAT_FACTOR;
    unsigned int tx_capacity = (STARTUP_SYMBOLS + total_packets * packet_symbols + TAIL_SYMBOLS + 32) * TX_K;

    float complex *tx = (float complex *)malloc(tx_capacity * sizeof(float complex));
    unsigned int tx_n = qpsk_phy_build_tx(tx, tx_capacity, file_data, file_size, file_crc, num_chunks);
    if (tx_n == 0) {
        fprintf(stderr, "[TX] baseband generation failed\n");
        free(file_data);
        free(tx);
        return 1;
    }
    printf("[TX] Generated %u RF samples (%.2f M).\n", tx_n, tx_n / 1e6);

    /* ---------- 3. 配置 AD9361 ---------- */
    rxcfg.lo_hz  = MHZ(param_lo_mhz);
    rxcfg.fs_hz  = MHZ(param_samp_mhz);
    rxcfg.bw_hz  = MHZ(param_bw_mhz);
    rxcfg.rfport = "A_BALANCED";

    txcfg.lo_hz  = MHZ(param_lo_mhz);
    txcfg.fs_hz  = MHZ(param_samp_mhz);
    txcfg.bw_hz  = MHZ(param_bw_mhz);
    txcfg.rfport = "A";

    printf("* Acquiring IIO context\n");
    if (strcmp(param_dev, "default") == 0) {
        IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
    } else {
        IIO_ENSURE((ctx = iio_create_context_from_uri(param_dev)) && "No context");
    }
    IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

    printf("* Acquiring AD9361 streaming devices\n");
    IIO_ENSURE(get_ad9361_stream_dev(TX, &tx_dev) && "No TX dev found");
    IIO_ENSURE(get_ad9361_stream_dev(RX, &rx_dev) && "No RX dev found");

    printf("* Configuring AD9361 for streaming\n");
    IIO_ENSURE(cfg_ad9361_streaming_ch(&rxcfg, RX, 0) && "RX config failed");
    IIO_ENSURE(cfg_ad9361_streaming_ch(&txcfg, TX, 0) && "TX config failed");

    /* RX 手动增益，避免 AGC 在自环强信号下失真；TX 显式衰减 */
    {
        struct iio_channel *ch = NULL;
        if (get_phy_chan(RX, 0, &ch)) {
            wr_ch_str(ch, "gain_control_mode", "manual");
            wr_ch_double(ch, "hardwaregain", param_rx_gain_db);
            printf("[RF] RX gain = %.1f dB (manual)\n", param_rx_gain_db);
        }
        if (get_phy_chan(TX, 0, &ch)) {
            wr_ch_double(ch, "hardwaregain", -param_tx_attn_db);
            printf("[RF] TX atten = %.1f dB\n", param_tx_attn_db);
        }
    }

    printf("* Initializing AD9361 IIO streaming channels\n");
    IIO_ENSURE(get_ad9361_stream_ch(RX, rx_dev, 0, &rx0_i) && "RX chan I not found");
    IIO_ENSURE(get_ad9361_stream_ch(RX, rx_dev, 1, &rx0_q) && "RX chan Q not found");
    IIO_ENSURE(get_ad9361_stream_ch(TX, tx_dev, 0, &tx0_i) && "TX chan I not found");
    IIO_ENSURE(get_ad9361_stream_ch(TX, tx_dev, 1, &tx0_q) && "TX chan Q not found");

    printf("* Enabling IIO streaming channels\n");
    iio_channel_enable(rx0_i);
    iio_channel_enable(rx0_q);
    iio_channel_enable(tx0_i);
    iio_channel_enable(tx0_q);

    /* 清除 DDS 残留单音 */
    iio_channel_attr_write_double(tx0_i, "raw", 0);
    iio_channel_attr_write_double(tx0_q, "raw", 0);

    /* ---------- 4. TX：硬件循环缓冲整段推流一次 ---------- */
    float peak = 1e-6f;
    for (unsigned int i = 0; i < tx_n; i++) {
        float m = cabsf(tx[i]);
        if (m > peak) peak = m;
    }
    float tx_scale = 0.8f * 32767.0f / peak;

    txbuf = iio_device_create_buffer(tx_dev, (size_t)tx_n, true); /* cyclic */
    if (!txbuf) {
        perror("Could not create cyclic TX buffer");
        free(file_data);
        free(tx);
        _shutdown();
    }
    {
        ptrdiff_t p_inc = iio_buffer_step(txbuf);
        char *p_end = iio_buffer_end(txbuf);
        unsigned int cnt = 0;
        for (char *p = (char *)iio_buffer_first(txbuf, tx0_i); p < p_end; p += p_inc) {
            if (cnt < tx_n) {
                ((int16_t *)p)[0] = (int16_t)lrintf(crealf(tx[cnt]) * tx_scale);
                ((int16_t *)p)[1] = (int16_t)lrintf(cimagf(tx[cnt]) * tx_scale);
            } else {
                ((int16_t *)p)[0] = 0;
                ((int16_t *)p)[1] = 0;
            }
            cnt++;
        }
    }
    printf("[TX] Cyclic buffer filled (%u samples), pushing once...\n", tx_n);
    if (iio_buffer_push(txbuf) < 0) {
        perror("Error pushing TX cyclic buffer");
        free(file_data);
        free(tx);
        _shutdown();
    }

    /* ---------- 5. RX：非循环连续采集 ---------- */
    unsigned int rx_need = tx_n + RX_MARGIN_SAMPLES;
    float complex *rx = (float complex *)calloc(rx_need, sizeof(float complex));
    unsigned int rx_collected = 0;

    rxbuf = iio_device_create_buffer(rx_dev, RX_BUF_SAMPLES, false);
    if (!rxbuf) {
        perror("Could not create RX buffer");
        free(file_data);
        free(tx);
        free(rx);
        _shutdown();
    }

    size_t rx_sample_size = iio_device_get_sample_size(rx_dev);
    printf("[RX] Collecting %u samples...\n", rx_need);

    while (!stop && rx_collected < rx_need) {
        ssize_t nb = iio_buffer_refill(rxbuf);
        if (nb < 0) {
            perror("Error refilling RX buf");
            break;
        }

        ptrdiff_t p_inc = iio_buffer_step(rxbuf);
        char *p_end = iio_buffer_end(rxbuf);
        unsigned int cnt = 0;
        for (char *p = (char *)iio_buffer_first(rxbuf, rx0_i);
             p < p_end && rx_collected + cnt < rx_need;
             p += p_inc, cnt++) {
            int16_t i = ((int16_t *)p)[0];
            int16_t q = ((int16_t *)p)[1];
            rx[rx_collected + cnt] = ((float)i / 32768.0f) + _Complex_I * ((float)q / 32768.0f);
        }
        rx_collected += cnt;
        (void)rx_sample_size;

        if ((rx_collected & 0x3fffff) < 32768)
            printf("[RX] %u / %u samples\r", rx_collected, rx_need);
    }
    printf("\n[RX] Collected %u samples.\n", rx_collected);

    /* 停止射频流（先销毁 buffer 再拆 context） */
    iio_buffer_destroy(rxbuf); rxbuf = NULL;
    iio_buffer_destroy(txbuf); txbuf = NULL;
    iio_channel_disable(rx0_i); iio_channel_disable(rx0_q);
    iio_channel_disable(tx0_i); iio_channel_disable(tx0_q);
    iio_context_destroy(ctx); ctx = NULL;

    /* ---------- 6. 基带接收处理 ---------- */
    int ok = qpsk_phy_rx_process(rx, rx_collected, file_size, file_crc, num_chunks, param_out);

    free(rx);
    free(tx);
    free(file_data);
    return ok ? 0 : 1;
}
