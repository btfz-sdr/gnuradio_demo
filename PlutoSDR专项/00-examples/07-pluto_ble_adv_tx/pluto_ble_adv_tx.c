// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD9361 IIO streaming example for BLE ADV Transmitter
 * 保持与前几集完全一致的底层框架
 * 支持动态命令行参数解析：中心频率、采样率、目标 URI
 **/
// https://github.com/analogdevicesinc/libiio/blob/v0.25/examples/ad9361-iiostream.c

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
#include <getopt.h>  /* ---- 【全新引入】Linux 标准命令行参数解析库 ---- */
#include <iio.h>

/* 引入剥离的蓝牙基带算法 */
#include "ble_phy.h"

/* helper macros */
#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define GHZ(x) ((long long)(x*1000000000.0 + .5))

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
	long long bw_hz; // Analog banwidth in Hz
	long long fs_hz; // Baseband sample rate in Hz
	long long lo_hz; // Local oscillator frequency in Hz
	const char* rfport; // Port name
};

/* static scratch mem for strings */
static char tmpstr[64];

/* IIO structs required for streaming */
static struct iio_context *ctx   = NULL;
//static struct iio_channel *rx0_i = NULL;
//static struct iio_channel *rx0_q = NULL;
static struct iio_channel *tx0_i = NULL;
static struct iio_channel *tx0_q = NULL;
//static struct iio_buffer  *rxbuf = NULL;
static struct iio_buffer  *txbuf = NULL;

static bool stop;

/* cleanup and exit */
static void _shutdown(void)
{
	printf("* Destroying buffers\n");
	//if (rxbuf) { iio_buffer_destroy(rxbuf); }
	if (txbuf) { iio_buffer_destroy(txbuf); }

	printf("* Disabling streaming channels\n");
	//if (rx0_i) { iio_channel_disable(rx0_i); }
	//if (rx0_q) { iio_channel_disable(rx0_q); }
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

/* check return value of attr_write function */
static void errchk(int v, const char* what) {
	 if (v < 0) { fprintf(stderr, "Error %d writing to channel \"%s\"\nvalue may not be supported.\n", v, what); _shutdown(); }
}

/* write attribute: long long int */
static void wr_ch_lli(struct iio_channel *chn, const char* what, long long val)
{
	errchk(iio_channel_attr_write_longlong(chn, what, val), what);
}

/* write attribute: string */
static void wr_ch_str(struct iio_channel *chn, const char* what, const char* str)
{
	errchk(iio_channel_attr_write(chn, what, str), what);
}

/* helper function generating channel names */
static char* get_ch_name(const char* type, int id)
{
	snprintf(tmpstr, sizeof(tmpstr), "%s%d", type, id);
	return tmpstr;
}

/* returns ad9361 phy device */
static struct iio_device* get_ad9361_phy(void)
{
	struct iio_device *dev =  iio_context_find_device(ctx, "ad9361-phy");
	IIO_ENSURE(dev && "No ad9361-phy found");
	return dev;
}

/* finds AD9361 streaming IIO devices */
static bool get_ad9361_stream_dev(enum iodev d, struct iio_device **dev)
{
	switch (d) {
	case TX: *dev = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc"); return *dev != NULL;
	case RX: *dev = iio_context_find_device(ctx, "cf-ad9361-lpc");  return *dev != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds AD9361 streaming IIO channels */
static bool get_ad9361_stream_ch(enum iodev d, struct iio_device *dev, int chid, struct iio_channel **chn)
{
	*chn = iio_device_find_channel(dev, get_ch_name("voltage", chid), d == TX);
	if (!*chn)
		*chn = iio_device_find_channel(dev, get_ch_name("altvoltage", chid), d == TX);
	return *chn != NULL;
}

/* finds AD9361 phy IIO configuration channel with id chid */
static bool get_phy_chan(enum iodev d, int chid, struct iio_channel **chn)
{
	switch (d) {
	case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), false); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), true);  return *chn != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds AD9361 local oscillator IIO configuration channels */
static bool get_lo_chan(enum iodev d, struct iio_channel **chn)
{
	switch (d) {
	 // LO chan is always output, i.e. true
	case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("altvoltage", 0), true); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("altvoltage", 1), true); return *chn != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* applies streaming configuration through IIO */
bool cfg_ad9361_streaming_ch(struct stream_cfg *cfg, enum iodev type, int chid)
{
	struct iio_channel *chn = NULL;

	// Configure phy and lo channels
	printf("* Acquiring AD9361 phy channel %d\n", chid);
	if (!get_phy_chan(type, chid, &chn)) {	return false; }
	wr_ch_str(chn, "rf_port_select",     cfg->rfport);
	wr_ch_lli(chn, "rf_bandwidth",       cfg->bw_hz);
	wr_ch_lli(chn, "sampling_frequency", cfg->fs_hz);

	// Configure LO channel
	printf("* Acquiring AD9361 %s lo channel\n", type == TX ? "TX" : "RX");
	if (!get_lo_chan(type, &chn)) { return false; }
	wr_ch_lli(chn, "frequency", cfg->lo_hz);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// 蓝牙发射相关的默认逻辑配置
//////////////////////////////////////////////////////////////////////////
#define DEFAULT_SAMP_RATE_MHZ   4.0f    // 采样率固定 4.0 MHz (对应过采样倍数 4)
#define DEFAULT_TX_BANDWIDTH    2.0f    // 模拟基带滤波器带宽 2.0 MHz
#define DEFAULT_ADV_CH          37      // 默认 37 信道 (2402 MHz)
#define DEFAULT_ADV_INTERVAL_MS 100     // 默认广播间隔 100 ms (BLE 规范推荐值, 最短 20 ms)
#define DEFAULT_TX_ATTN_DB      0.0     // 默认发射衰减 0 dB (最大输出)

/* BLE 信道 -> 中心频率 (MHz)。
 * 白化种子 = 信道号本身, 由这里统一决定 —— 这样"改了频率没改种子"的坑不会再出现。
 * 37/38/39 是广播信道, 0~36 是数据信道。 */
static double ble_ch_freq_mhz(int ch) {
    if (ch == 37) return 2402.0;
    if (ch == 38) return 2426.0;
    if (ch == 39) return 2480.0;
    if (ch >= 0  && ch <= 10) return 2404.0 + 2.0 * ch;
    if (ch >= 11 && ch <= 36) return 2402.0 + 2.0 * (ch + 2);
    return -1.0;
}

/* 打印帮助菜单 */
void print_help(char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d <device>  plutosdr 设备 url (默认: default)\n");
    printf("  -c <channel> BLE 信道 0~39 (默认: 37), 同时决定中心频率与白化种子\n");
    printf("  -f <freq>    中心频率 MHz (手动覆盖 -c, 与 -c 不一致会告警)\n");
    printf("  -s <rate>    采样率 MHz (默认: 4.0, 只有 4.0 才是标准 BLE 符号率)\n");
    printf("  -i <ms>      广播间隔 ms (默认: 100, 下限 = 包长+留白 ≈ 1.3 ms)\n");
    printf("  -a <dB>      发射衰减 dB (默认: 0 = 最大输出)\n");
    printf("  -h           显示当前帮助菜单\n");
}

int main(int argc, char **argv) {
    struct iio_device *tx;
    struct stream_cfg txcfg;

    // 默认变量初始化
    char param_dev[32] = "default";
    int    param_ch          = DEFAULT_ADV_CH;
    double param_samp_mhz    = DEFAULT_SAMP_RATE_MHZ;
    int    param_interval_ms = DEFAULT_ADV_INTERVAL_MS;
    double param_attn_db     = DEFAULT_TX_ATTN_DB;
    int    freq_manual       = 0;                              // -f 是否被显式指定
    double param_lo_mhz      = ble_ch_freq_mhz(param_ch);

    /* ---- 【核心升级】：使用 getopt 动态解析传入参数 ---- */
    int opt;
    while ((opt = getopt(argc, argv, "d:c:f:s:i:a:h")) != -1) {
        switch (opt) {
            case 'd':
                strncpy(param_dev, optarg, sizeof(param_dev) - 1);
                break;
            case 'c':
                param_ch = atoi(optarg);
                break;
            case 'f':
                param_lo_mhz = atof(optarg);
                freq_manual = 1;
                break;
            case 's':
                param_samp_mhz = atof(optarg);
                break;
            case 'i':
                param_interval_ms = atoi(optarg);
                break;
            case 'a':
                param_attn_db = atof(optarg);
                break;
            case 'h':
            default:
                print_help(argv[0]);
                return 0;
        }
    }

    /* ---- 信道合法性 & 频率/白化种子一致性检查 ---- */
    double ch_freq = ble_ch_freq_mhz(param_ch);
    if (ch_freq < 0) {
        fprintf(stderr, "错误: 不支持的 BLE 信道 %d (合法范围 0~39)\n", param_ch);
        return 1;
    }
    if (!freq_manual) param_lo_mhz = ch_freq;
    if (freq_manual && fabs(param_lo_mhz - ch_freq) > 0.5) {
        fprintf(stderr,
                "[WARN] 频率被 -f 改成 %.2f MHz, 但白化种子仍按信道 %d (%.2f MHz) 算。\n"
                "       接收端必须用 -c %d 才能解出这个包, 标准设备解不出来。\n",
                param_lo_mhz, param_ch, ch_freq, param_ch);
    }
    if (param_interval_ms < 20)
        fprintf(stderr, "[WARN] 广播间隔 %d ms 低于 BLE 规范最短 20 ms, 只适合自测/压力测试。\n"
                        "       这个速率下每秒要发 %ld 个包, 手机/nRF Connect 的蓝牙栈会限流,\n"
                        "       看起来断续是显示端的问题, 不代表包没发出去。\n",
                param_interval_ms, (long)(1000.0 / (param_interval_ms > 0 ? param_interval_ms : 1)));

    // Listen to ctrl+c and IIO_ENSURE
    signal(SIGINT, handle_sig);

    /* ---- 1. 配置动态流参数 ---- */
    txcfg.lo_hz   = (long long)(param_lo_mhz * 1000000.0 + .5);
    txcfg.fs_hz   = (long long)(param_samp_mhz * 1000000.0 + .5);
    txcfg.bw_hz   = (long long)(DEFAULT_TX_BANDWIDTH * 1000000.0 + .5);
    txcfg.rfport  = "A"; 

    printf("* Acquiring IIO context\n");
    if (memcmp(param_dev, "default", 7) == 0) {
        IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
    } else {
        IIO_ENSURE((ctx = iio_create_context_from_uri(param_dev)) && "No context");
    }
    IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

    printf("* Acquiring AD9361 streaming devices\n");
    IIO_ENSURE(get_ad9361_stream_dev(TX, &tx) && "No TX dev found");
    
    printf("* Configuring AD9361 for streaming\n");
    IIO_ENSURE(cfg_ad9361_streaming_ch(&txcfg, TX, 0) && "TX port configuration failed");
    
    printf("* Initializing AD9361 IIO streaming channels\n");
    IIO_ENSURE(get_ad9361_stream_ch(TX, tx, 0, &tx0_i) && "TX chan I not found");
    IIO_ENSURE(get_ad9361_stream_ch(TX, tx, 1, &tx0_q) && "TX chan Q not found");

    printf("* Enabling IIO streaming channels\n");
    iio_channel_enable(tx0_i);
    iio_channel_enable(tx0_q);

    // 🔴【清理DDS】：清除底层 DDS 核心带来的固定单音串扰，确保纯净智造基带
    iio_channel_attr_write_double(tx0_i, "raw", 0);
    iio_channel_attr_write_double(tx0_q, "raw", 0);

    /* 🔴【显式设置发射衰减】：以前完全靠上电默认值（注释却假定 0dB），输出电平不可复现。
     * 有些固件的 hardwaregain 会返回 EINVAL，所以这里只告警、不致命。 */
    {
        struct iio_channel *tx_phy = NULL;
        if (get_phy_chan(TX, 0, &tx_phy) && iio_channel_find_attr(tx_phy, "hardwaregain")) {
            int e = iio_channel_attr_write_double(tx_phy, "hardwaregain", -param_attn_db);
            if (e < 0)
                fprintf(stderr, "[WARN] 写 TX hardwaregain 失败 (%d), 发射衰减仍是硬件默认值\n", e);
            else
                printf("[RF INFO] AD9361 发射衰减已设为 %.2f dB (0 = 最大输出)\n", param_attn_db);
        } else {
            fprintf(stderr, "[WARN] 该固件没有 TX hardwaregain 属性, 发射衰减未设置\n");
        }
    }

    /* ---- 2. 核心链路层组装 ---- */
    uint8_t mac[6] = {0xDC, 0x23, 3, 4, 5, 6};
    const char *adv_name = "hello btfz's gnu-radio";
    int adv_name_len = strlen(adv_name);

    int adv_data_len = adv_name_len + 2;
    uint8_t *adv_datas = malloc(adv_data_len);
    adv_datas[0] = adv_name_len + 1;
    adv_datas[1] = 0x09;
    memcpy(&adv_datas[2], adv_name, adv_name_len);

    int pdu_len = 2 + 6 + adv_data_len; 
    uint8_t *pdu_adv = malloc(pdu_len);
    pdu_adv[0] = 0x42;
    pdu_adv[1] = (adv_data_len + 6) & 0xFF;
    for (int i = 0; i < 6; i++) pdu_adv[2 + i] = mac[5 - i];
    memcpy(&pdu_adv[8], adv_datas, adv_data_len);

    printf("\n==================================================================\n");
    printf("> pre_wt_adv_pdu:\nHEX: ");
    for (int i = 0; i < pdu_len; i++) printf("%02X ", pdu_adv[i]);
    printf("\n");

    uint8_t crc_bytes[3];
    bt_crc(pdu_adv, pdu_len, crc_bytes);
    // 提示里不要再写死具体数值: MAC/设备名一改, 值就变, 写死只会误导 (旧版曾写死 E8 7D 36)
    printf("> Calculated CRC-24 (空中字节序): %02X %02X %02X\n", crc_bytes[0], crc_bytes[1], crc_bytes[2]);

    int pdu_crc_len = pdu_len + 3;
    uint8_t *pdu_adv_crc = malloc(pdu_crc_len);
    memcpy(pdu_adv_crc, pdu_adv, pdu_len);
    pdu_adv_crc[pdu_len]     = crc_bytes[0];
    pdu_adv_crc[pdu_len + 1] = crc_bytes[1];
    pdu_adv_crc[pdu_len + 2] = crc_bytes[2];

    uint8_t *pdu_adv_crc_wt = malloc(pdu_crc_len);
    /* 白化种子必须等于实际发射信道, 否则接收端(按信道解白)一个包都解不出来 */
    bt_dewhitening(pdu_adv_crc, pdu_crc_len, param_ch, pdu_adv_crc_wt);

    int ll_payload_len = 5 + pdu_crc_len;
    uint8_t *ll_payload = malloc(ll_payload_len);
    ll_payload[0] = 0xAA; ll_payload[1] = 0xD6; ll_payload[2] = 0xBE; ll_payload[3] = 0x89; ll_payload[4] = 0x8E;
    memcpy(&ll_payload[5], pdu_adv_crc_wt, pdu_crc_len);

    printf("------------------------------------------------------------------\n");
    printf("> ll data final:\nHEX: ");
    for (int i = 0; i < ll_payload_len; i++) printf("%02X ", ll_payload[i]);
    printf("\n==================================================================\n\n");

    /* ---- 3. LSB 比特展开与调制计算 ---- */
    int max_bits = ll_payload_len * 8;
    uint8_t *ll_datas_bit = malloc(max_bits);
    int total_bits = bytes_to_bits_lsb(ll_payload, ll_payload_len, ll_datas_bit);

    int max_samples = (total_bits * SAMPLE_PER_SYMBOL) + (LEN_GAUSS_FILTER * SAMPLE_PER_SYMBOL) + IFS_BLANK_SAMPLES;
    int16_t *mod_iq_buf = malloc(max_samples * 2 * sizeof(int16_t));
    int generated_samples = gen_sample_from_phy_bit(ll_datas_bit, total_bits, mod_iq_buf);

    if (generated_samples <= 0) {
        fprintf(stderr, "[ERROR] 基带生成失败 (返回 %d), 退出\n", generated_samples);
        _shutdown();
    }
    printf("[SUCCESS] 基带转换完成。IQ 发射采样总数 (含留白): %d\n", generated_samples);

    printf("\n>>> Edge-Computing BLE ADV Transmitter Started <<<\n");
    printf("Target Dev  : %s\n", param_dev);
    printf("BLE Channel : %d (%.2f MHz, 白化种子 %d)\n", param_ch, param_lo_mhz, param_ch);
    printf("Sample Rate : %.2f MHz\n", param_samp_mhz);
    printf("TX Atten    : %.2f dB\n", param_attn_db);
    printf("ADV Interval: %d ms\n\n", param_interval_ms);

    /* ---- 4. 计算目标广播周期的总样点数并创建硬件循环缓冲区 ---- */
    // https://gemini.google.com/app/73658645a2006097
    // 改为硬件循环缓冲，广播间隔才能极其精准
    long target_samples = (long)(param_samp_mhz * 1000000.0 * ((double)param_interval_ms / 1000.0));
    if (target_samples < generated_samples) {
        target_samples = generated_samples; // 确保总样点数大于实际包长度
        fprintf(stderr, "[WARN] 设定的广播间隔太短，已自动调整为包的最小物理时长\n");
    }

    // 创建硬件循环缓冲区 (最后一个参数设为 true，代表硬件自动循环播放)
    txbuf = iio_device_create_buffer(tx, target_samples, true);
    if (!txbuf) {
        perror("Could not create TX buffer");
        _shutdown();
    }

    /* 缓冲区填充：前半部分填入实际的突发包，剩余空间全部填零（静默等待） */
    {
        ptrdiff_t p_inc = iio_buffer_step(txbuf);
        char *p_end     = iio_buffer_end(txbuf);
        int iq_ptr = 0;
        for (char *p_dat = (char *)iio_buffer_first(txbuf, tx0_i); p_dat < p_end; p_dat += p_inc) {
            if (iq_ptr < generated_samples) {
                ((int16_t*)p_dat)[0] = mod_iq_buf[2 * iq_ptr];     // I
                ((int16_t*)p_dat)[1] = mod_iq_buf[2 * iq_ptr + 1]; // Q
            } else {
                ((int16_t*)p_dat)[0] = 0; // 填零作为静默期
                ((int16_t*)p_dat)[1] = 0;
            }
            iq_ptr++;
        }
    }

    printf("[TX] 硬件循环周期样点数 = %ld (对应设定间隔 %d ms)\n", target_samples, param_interval_ms);

    /* 硬件循环模式下，只需将缓冲区推送一次，硬件即会按周期自主无限循环发送 */
    ssize_t nbytes_tx = iio_buffer_push(txbuf);
    if (nbytes_tx < 0) { 
        fprintf(stderr, "Error pushing TX buf %d\n", (int)nbytes_tx); 
    }

    /* 保持程序存活，避免进程退出导致发射停止 */
    while (!stop) {
        sleep(1);
    }

    /* ---- 5. 清理退出 ---- */
    free(adv_datas); free(pdu_adv); free(pdu_adv_crc); 
    free(pdu_adv_crc_wt); free(ll_payload); free(ll_datas_bit); free(mod_iq_buf);
    _shutdown();
    return 0;
}
