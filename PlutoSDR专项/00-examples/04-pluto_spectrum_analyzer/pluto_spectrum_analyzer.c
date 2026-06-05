// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD9361 IIO streaming example for Spectrum Analyzer
 * 保持与前几集完全一致的底层框架
 * 支持动态命令行参数解析：中心频率、采样率、目标 IP
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
#include <getopt.h>  /* ---- 【全新引入】Linux 标准命令行参数解析库 ---- */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iio.h>

/* 引入轻量级 FFT 库 */
#include "kiss_fft.h"

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
// 频谱仪相关的逻辑（全新升级：边缘端高级数据处理与压缩）
//////////////////////////////////////////////////////////////////////////
#define DEFAULT_LO_FREQ_MHZ   93.0f    // 中心频率（如观察FM频段：93.0 MHz）
#define DEFAULT_SAMP_RATE_MHZ 10.0f    // 拉满到 5.0 MHz 采样率，增大观测带宽
#define DEFAULT_RX_BANDWIDTH  8.0f     // 模拟带宽 8 MHz

/* ---- FFT 核心配置 ---- */
#define FFT_POINTS        1024            // 1024点高精度频谱
#define FRAME_AERAGE_NUM  10              // 在板载端平滑10帧再发一次，既能防抖，又能将网络负载暴降90%！

/* ---- 网络配置 ---- */
#define DEFAULT_DEST_IP    "192.168.1.119" // 接收端 PC 的 IP
#define DEFAULT_DEST_PORT  1234            // 接收端 PC 的端口

/* ---- 【功能抽象】汉宁窗（Hanning Window）初始化，压制频谱泄露（旁瓣） ---- */
static float hanning_window[FFT_POINTS];
void init_hanning_window() {
    for (int i = 0; i < FFT_POINTS; i++) {
        hanning_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_POINTS - 1)));
    }
}

/* ---- 【功能抽象】动态范围转换（将幅值平方转为标准 dB 功率，并完成 FFT 移频） ---- */
// 提示：SDR 读取到的 IQ 经 FFT 后，前半部分是正频率，后半部分是负频率。
// 转换时需要执行 FFT-Shift 逻辑，把中心频点（直流分量）挪到屏幕正中间。
static inline void process_fft_output(kiss_fft_cpx *out, float *output_db, float *avg_buffer, int frame_cnt) {
    float norm_factor = 1.0f / (FFT_POINTS * 2048.0f); // 归一化因子
    
    for (int i = 0; i < FFT_POINTS; i++) {
        // 优雅实现 FFT-Shift：后半部分移到左边，前半部分移到右边
        int shift_idx = (i < FFT_POINTS / 2) ? (i + FFT_POINTS / 2) : (i - FFT_POINTS / 2);
        
        // 计算幅值平方（功率）
        float power = (out[i].r * out[i].r + out[i].i * out[i].i) * norm_factor;
        
        // 防止 log10(0) 导致死机
        if (power < 1e-10f) power = 1e-10f;
        
        // 换算成相对 dB 值
        float db_val = 10.0f * log10f(power);
        
        // 在板载端对多帧数据进行无缝累加
        avg_buffer[shift_idx] += db_val;
        
        // 当达到平滑帧数时，求出平均值并输出
        if (frame_cnt == FRAME_AERAGE_NUM) {
            output_db[shift_idx] = avg_buffer[shift_idx] / FRAME_AERAGE_NUM;
            avg_buffer[shift_idx] = 0.0f; // 清空以便下一轮统计
        }
    }
}

/* 打印帮助菜单 */
void print_help(char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d <device>  plutosdr 设备 url (默认: default)\n");
    printf("  -f <freq>    中心频率 Center Frequency in MHz (默认: 93.0)\n");
    printf("  -s <rate>    采样率 Sampling Rate in MHz (默认: 10.0)\n");
    printf("  -i <ip>      目标 PC IP 地址 (默认: 192.168.1.119)\n");
    printf("  -p <port>    目标 PC PORT 地址 (默认: 1234)\n");
    printf("  -h           显示当前帮助菜单\n");
}

int main(int argc, char **argv) {
    struct iio_device *rx;
    struct stream_cfg rxcfg;
    struct sockaddr_in servaddr;

    // 默认变量初始化
    char param_dev[32] = "default";
    double param_lo_mhz = DEFAULT_LO_FREQ_MHZ;
    double param_samp_mhz = DEFAULT_SAMP_RATE_MHZ;
    char param_ip[32] = DEFAULT_DEST_IP;
    int param_port = DEFAULT_DEST_PORT;

    /* ---- 【核心升级】：使用 getopt 动态解析传入参数 ---- */
    int opt;
    while ((opt = getopt(argc, argv, "d:f:s:i:p:h")) != -1) {
        switch (opt) {
            case 'd':
                strncpy(param_dev, optarg, sizeof(param_dev) - 1);
                break;
            case 'f':
                param_lo_mhz = atof(optarg);
                break;
            case 's':
                param_samp_mhz = atof(optarg);
                break;
            case 'i':
                strncpy(param_ip, optarg, sizeof(param_ip) - 1);
                break;
            case 'p':
                param_port = atoi(optarg);
                break;
            case 'h':
            default:
                print_help(argv[0]);
                return 0;
        }
    }

    // Listen to ctrl+c and IIO_ENSURE
    signal(SIGINT, handle_sig);

    /* ---- 1. 网络 UDP 初始化 ---- */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    IIO_ENSURE(sockfd >= 0 && "Failed to create socket");
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(param_port);
    servaddr.sin_addr.s_addr = inet_addr(param_ip);

    /* ---- 2. 配置动态流参数 ---- */
    rxcfg.lo_hz   = (long long)(param_lo_mhz * 1000000.0 + .5);
    rxcfg.fs_hz   = (long long)(param_samp_mhz * 1000000.0 + .5);
    // 【硬核自适应】：模拟硬件低通滤波器带宽设为采样率的 80%，完美解决抛物线“出界”和混叠问题
    rxcfg.bw_hz   = (long long)(param_samp_mhz * 0.8 * 1000000.0 + .5);
    rxcfg.rfport  = "A_BALANCED"; 

    // 注意：如果有 URI 环境参数传入，getopt 之后的剩余参数可以通过 optind 获取
    printf("* Acquiring IIO context\n");
    if (memcmp(param_dev,"default", 8) == 0) {
        IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
    } else {
        IIO_ENSURE((ctx = iio_create_context_from_uri(param_dev)) && "No context");
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

    /* ---- 3. 初始化频谱仪算法组件与内存 ---- */
    init_hanning_window();
    
    // 分配 kissFFT 句柄（0表示不执行反FFT）
    kiss_fft_cfg fft_plan = kiss_fft_alloc(FFT_POINTS, 0, NULL, NULL);
    IIO_ENSURE(fft_plan && "FFT allocation failed");
    
    kiss_fft_cpx *fft_in  = malloc(FFT_POINTS * sizeof(kiss_fft_cpx));
    kiss_fft_cpx *fft_out = malloc(FFT_POINTS * sizeof(kiss_fft_cpx));
    float *spectrum_avg_buf = calloc(FFT_POINTS, sizeof(float));
    float *spectrum_send_payload = malloc(FFT_POINTS * sizeof(float));
    
    IIO_ENSURE(fft_in && fft_out && spectrum_avg_buf && spectrum_send_payload);

    printf("\n>>> Edge-Computing Spectrum Analyzer Started <<<\n");
    printf("Target Dev  : %s\n", param_dev);
    printf("Target PC IP: %s:%d\n", param_ip, param_port);
    printf("Center Freq : %.2f MHz\n", param_lo_mhz);
    printf("Sample Rate : %.2f MHz (Hardware BW: %.2f MHz)\n", param_samp_mhz, param_samp_mhz * 0.8);
    printf("Resolution  : %d points\n\n", FFT_POINTS);

    int fft_sample_idx = 0;
    int frame_counter = 0;

    /* ---- 4. 主循环处理 ---- */
    while (!stop) {
        // Refill RX buffer
        ssize_t nbytes_rx = iio_buffer_refill(rxbuf);
        if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); _shutdown(); }

        // READ: Get pointers to RX buf and read IQ from RX buf port 0
        ptrdiff_t p_inc = iio_buffer_step(rxbuf);
        char *p_end     = iio_buffer_end(rxbuf);
        char *p_dat;

        for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {
            
            // 提取12位ADC数据，对齐符号位并转换为浮点数
            float i_val = (float)(((int16_t*)p_dat)[0] << 4);
            float q_val = (float)(((int16_t*)p_dat)[1] << 4);

            // 【DSP升级】：加窗处理，平滑灌入 FFT 输入缓冲区
            fft_in[fft_sample_idx].r = i_val * hanning_window[fft_sample_idx];
            fft_in[fft_sample_idx].i = q_val * hanning_window[fft_sample_idx];

            // 当缓冲区攒够 1024 个点，立刻就地解决，在板载端执行 FFT 变换！
            if (++fft_sample_idx >= FFT_POINTS) {
                fft_sample_idx = 0; // 滚回复位
                frame_counter++;

                // 核心计算：执行快速傅里叶变换
                kiss_fft(fft_plan, fft_in, fft_out);

                // 将复数输出转化为 dB 刻度值，并完成移动和片上帧平滑
                process_fft_output(fft_out, spectrum_send_payload, spectrum_avg_buf, frame_counter);

                // 积攒到设定帧数后，打包通过 UDP 发回给 PC 上位机绘图
                if (frame_counter >= FRAME_AERAGE_NUM) {
                    frame_counter = 0; // 平滑帧清零
                    
                    // 发送 1024 帧 float 数组，数据量极小，完全不卡网络
                    sendto(sockfd, spectrum_send_payload, FFT_POINTS * sizeof(float), 0,
                           (struct sockaddr *)&servaddr, sizeof(servaddr));
                }
            }
        }
    }

    /* ---- 5. 清理退出 ---- */
    free(fft_in);
    free(fft_out);
    free(spectrum_avg_buf);
    free(spectrum_send_payload);
    free(fft_plan);
    _shutdown();
    return 0;
}
