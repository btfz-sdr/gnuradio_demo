// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD9361 IIO streaming example
 *
 * Copyright (C) 2014 IABG mbH
 * Author: Michael Feilen <feilen_at_iabg.de>
 **/
// https://github.com/analogdevicesinc/libiio/blob/v0.25/examples/ad9361-iiostream.c

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iio.h>

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
static struct iio_channel *rx0_i = NULL;
static struct iio_channel *rx0_q = NULL;
//static struct iio_channel *tx0_i = NULL;
//static struct iio_channel *tx0_q = NULL;
static struct iio_buffer  *rxbuf = NULL;
//static struct iio_buffer  *txbuf = NULL;
static int sockfd = -1;

static bool stop;

/* cleanup and exit */
static void _shutdown(void)
{
	printf("* Destroying buffers\n");
	if (rxbuf) { iio_buffer_destroy(rxbuf); }
	//if (txbuf) { iio_buffer_destroy(txbuf); }

	printf("* Disabling streaming channels\n");
	if (rx0_i) { iio_channel_disable(rx0_i); }
	if (rx0_q) { iio_channel_disable(rx0_q); }
	//if (tx0_i) { iio_channel_disable(tx0_i); }
	//if (tx0_q) { iio_channel_disable(tx0_q); }

	printf("* Destroying context\n");
	if (ctx) { iio_context_destroy(ctx); }

    printf("* Destroying sockfd\n");
    if (sockfd >= 0) close(sockfd);
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
// FM 相关的逻辑
//////////////////////////////////////////////////////////////////////////
#define FM_LO_FREQ      93000000ULL       // 目标 FM 频率 93.0 MHz
#define RX_SAMPLING_RATE 2400000          // 调整为 2.4 MHz 采样率 (保证后续整除)
#define RX_BANDWIDTH    2000000           // 模拟带宽 2.0 MHz

/* ---- 降采样与音频参数 ---- */
#define DECIM_1         5                 // 第一级降采样: 2.4 MHz -> 480 kHz
#define DECIM_2         10                // 第二级降采样: 480 kHz -> 48 kHz 音频
#define AUDIO_BUF_MAX  16384

/* ---- 滤波器系数配置 ---- */
// α = 1 - exp(-1 / (fs * τ)) = 1 - exp(-1 / (480000 * 50e-6)) ≈ 0.0408f
#define DEEMPH_ALPHA   0.0408f           // 中国 FM 去加重常数 (50µs @ 480kHz)
// DC 隔直系数 (fc ≈ 40Hz @ 48kHz 音频速率下)
// R = exp(-2π * fc / fs_audio) = exp(-2π * 40 / 48000) ≈ 0.9948f
#define DC_R           0.9948f
#define LPF_ALPHA      0.250f            // 抗混叠滤波器系数保持

/* ---- 网络配置 ---- */
#define DEST_IP        "192.168.1.119"   // 接收端 PC 的 IP
#define DEST_PORT      1234              // 接收端 PC 的端口

/* ---- 【优雅抽象】FM解调与滤波器内核状态结构体 ---- */
typedef struct {
    float last_phase;
    float deemph_state;
    float dc_x_prev;
    float dc_y_prev;
    // 降采样前级低通滤波器状态
    float lpf_i;
    float lpf_q;
} FmDemodState;

/* ---- 【功能抽象】核心 FM 解调与去加重 DSP 算法引擎 ---- */
static inline float iio_fm_demod_kernel(float i, float q, FmDemodState *state) {
    // 1. FM 鉴频器 (反正切差分)
    float phase = atan2f(q, i);
    float diff  = phase - state->last_phase;
    
    // 相位解卷绕映射
    if (diff >  M_PI) diff -= 2.0f * M_PI;
    if (diff < -M_PI) diff += 2.0f * M_PI;
    state->last_phase = phase;

    // 2. 去加重低通滤波
    state->deemph_state = DEEMPH_ALPHA * diff + (1.0f - DEEMPH_ALPHA) * state->deemph_state;
    return state->deemph_state;
}

/* ---- 【功能抽象】音频隔直高通滤波器 ---- */
static inline int16_t iio_audio_dc_block(float input, FmDemodState *state) {
    float dc_out = input - state->dc_x_prev + DC_R * state->dc_y_prev;
    state->dc_x_prev = input;
    state->dc_y_prev = dc_out;

    // 音频增益放大与严格截断控制
    float am_val = dc_out * 800000.0f; 
    if (am_val >  32767.0f) am_val =  32767.0f;
    if (am_val < -32768.0f) am_val = -32768.0f;

    return (int16_t)am_val;
}

int main(int argc, char **argv) {
    struct iio_device *rx;
    struct stream_cfg rxcfg;
    struct sockaddr_in servaddr;

    // Listen to ctrl+c and IIO_ENSURE
    signal(SIGINT, handle_sig);

    /* ---- 1. 网络 UDP 初始化 ---- */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    IIO_ENSURE(sockfd >= 0 && "Failed to create socket");
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(DEST_PORT);
    servaddr.sin_addr.s_addr = inet_addr(DEST_IP);

    /* ---- 2. 配置流参数 ---- */
    rxcfg.lo_hz   = FM_LO_FREQ;
    rxcfg.fs_hz   = RX_SAMPLING_RATE;
    rxcfg.bw_hz   = RX_BANDWIDTH;
    rxcfg.rfport  = "A_BALANCED"; 

    printf("* Acquiring IIO context\n");
    if (argc == 1) {
        IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
    }
    else if (argc == 2) {
        IIO_ENSURE((ctx = iio_create_context_from_uri(argv[1])) && "No context");
    }
    IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

    printf("* Acquiring AD9361 streaming devices\n");
    IIO_ENSURE(get_ad9361_stream_dev(RX, &rx) && "No RX dev found");
    
    printf("* Configuring AD9361 for streaming\n");
    IIO_ENSURE(cfg_ad9361_streaming_ch(&rxcfg, RX, 0) && "RX port configuration failed");
    
    printf("* Initializing AD9361 IIO streaming channels\n");
    IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 0, &rx0_i) && "RX chan I not found");
    IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 1, &rx0_q) && "RX chan Q not found");

    printf("* Enabling IIO streaming channels\n");
    iio_channel_enable(rx0_i);
    iio_channel_enable(rx0_q);

    printf("* Creating non-cyclic IIO buffers with 256 MiS\n");
    rxbuf = iio_device_create_buffer(rx, 256 * 1024, false);
    if (!rxbuf) {
        perror("Could not create RX buffer");
        _shutdown();
    }

    /* ---- 3. 初始化解调状态机结构体 ---- */
    FmDemodState demod_state = {0};
    int16_t *audio_out = malloc(AUDIO_BUF_MAX * sizeof(int16_t));
    IIO_ENSURE(audio_out && "Memory allocation failed");

    printf("\n>>> FM Demodulator Started (Radio Grade) <<<\n");
    printf("Target Freq: %.1f MHz | Out Audio: 48 kHz S16LE (Bit-Perfect)\n", (double)FM_LO_FREQ / 1e6);

    /* 效率优化：使用减法计数器代替取模操作 */
    int decim1_cnt = 0;
    int decim2_cnt = 0;

    /* ---- 4. 主循环处理 ---- */
    while (!stop) {
        // Refill RX buffer
        ssize_t nbytes_rx = iio_buffer_refill(rxbuf);
        if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); _shutdown(); }

        // READ: Get pointers to RX buf and read IQ from RX buf port 0
        ptrdiff_t p_inc = iio_buffer_step(rxbuf);
        char *p_end     = iio_buffer_end(rxbuf);
        char *p_dat;

        int out_idx = 0;

        for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {
            
            // 提取12位ADC数据并移位完成符号位对齐
            float i_val = (float)(((int16_t*)p_dat)[0] << 4);
            float q_val = (float)(((int16_t*)p_dat)[1] << 4);

            // 【DSP升级】：抗混叠基带低通滤波器 (IIR 低通，为降采样护航)
            demod_state.lpf_i = LPF_ALPHA * i_val + (1.0f - LPF_ALPHA) * demod_state.lpf_i;
            demod_state.lpf_q = LPF_ALPHA * q_val + (1.0f - LPF_ALPHA) * demod_state.lpf_q;

            // 第一级抽取：5:1 (计数器替代取模)
            if (++decim1_cnt >= DECIM_1) {
                decim1_cnt = 0; // 清零计数器

                // 调用抽象解调内核
                float audio_raw = iio_fm_demod_kernel(demod_state.lpf_i, demod_state.lpf_q, &demod_state);

                // 第二级抽取：10:1
                if (++decim2_cnt >= DECIM_2) {
                    decim2_cnt = 0;

                    // 调用抽象隔直内核并存入发送缓冲区
                    if (out_idx < AUDIO_BUF_MAX) {
                        audio_out[out_idx++] = iio_audio_dc_block(audio_raw, &demod_state);
                    }
                }
            }
        }

        if (out_idx > 0) {
            sendto(sockfd, audio_out, out_idx * sizeof(int16_t), 0,
                   (struct sockaddr *)&servaddr, sizeof(servaddr));
        }
    }

    /* ---- 5. 清理退出 ---- */
    free(audio_out);
    _shutdown();
    return 0;
}
