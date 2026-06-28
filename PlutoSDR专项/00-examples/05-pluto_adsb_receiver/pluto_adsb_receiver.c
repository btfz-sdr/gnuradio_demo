// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD9361 IIO streaming example for ADS-B 1090ES Receiver
 * 保持与前几集完全一致的底层框架
 * 支持动态命令行参数解析：中心频率、采样率、目标 IP
 * 板载端完成：脉冲包络检测 + 前导码匹配 + PPM 解码 + CRC24 校验 + Mode-S 报文解析
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
#include <getopt.h>  /* ---- 【核心】Linux 标准命令行参数解析库 ---- */
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
// ADS-B 1090ES 协议相关逻辑（全新升级：板载边缘端实时飞机识别解码）
//////////////////////////////////////////////////////////////////////////
#define DEFAULT_LO_FREQ_MHZ   1090.0f   // 中心频率：ADS-B 1090 MHz 航管频段
#define DEFAULT_SAMP_RATE_MHZ 4.0f     // 4 MSPS（同 gr-air-modes），每位 4 个采样点
#define DEFAULT_RX_BANDWIDTH  3.2f      // 模拟带宽 3.2 MHz（采样率 80%）

/* ---- ADS-B 协议核心常量 ---- */
#define ADS_B_MSG_BITS        112            // ADS-B 报文长度：112 位（14 字节）
#define ADS_B_MSG_BYTES       14             // 14 字节
#define ADS_B_PREAMBLE_US     8              // 前导码长度 8 µs
#define ADS_B_BIT_US          1              // 每位持续时间 1 µs
#define SAMP_PER_US           4              // 4 MSPS → 每微秒 4 个采样点
#define PREAMBLE_WINDOW       (ADS_B_PREAMBLE_US * SAMP_PER_US)  // 32 采样点
#define HALF_BIT_SAMPS        (SAMP_PER_US / 2)  // 半位采样数 2
#define BIT_SAMPS             SAMP_PER_US        // 每位采样数 4

/* ---- 前导码脉冲位置（以采样点为单位的偏移） ---- */
// 标准 Mode-S 前导码：4 个脉冲，每个 0.5µs
// 位置：0µs, 1.0µs, 3.5µs, 4.5µs
#define PRE_PULSE1_START  0
#define PRE_PULSE2_START  4
#define PRE_PULSE3_START  14
#define PRE_PULSE4_START  18

/* ---- 自适应阈值参数 ---- */
#define NF_EMA_ALPHA        0.05f    // 跨 buffer NF 追踪：per-buffer 最小值 EMA 系数
                                       //（脉冲能量远大于噪声，不会成为 per-buffer 最小值）
#define THRESHOLD_FACTOR   1.5f     // 脉冲检测阈值 = 噪声基底 × 系数

/* ---- 默认网络参数 ---- */
#define DEFAULT_DEST_IP    "192.168.1.119"
#define DEFAULT_DEST_PORT  1234

/* 打印帮助菜单 */
void print_help(char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d <device>  plutosdr 设备 url (默认: default)\n");
    printf("  -f <freq>    中心频率 Center Frequency in MHz (默认: 1090.0)\n");
    printf("  -s <rate>    采样率 Sampling Rate in MHz (默认: 10.0)\n");
    printf("  -i <ip>      目标 PC IP 地址 (默认: 192.168.1.119)\n");
    printf("  -p <port>    目标 PC PORT 地址 (默认: 1234)\n");
    printf("  -h           显示当前帮助菜单\n");
}

/* ---- 【功能抽象】CRC-24 表生成和校验：Mode-S 多项式 0xFFF409 ---- */
// gr-air-modes 兼容实现：字节查找表 + XOR 校验
static unsigned int crc_table[256];
static void generate_crc_table(void) {
    for (int n = 0; n < 256; n++) {
        unsigned int crc = n << 16;
        for (int k = 0; k < 8; k++) {
            if (crc & 0x800000) crc = ((crc << 1) ^ 0xFFF409) & 0xFFFFFF;
            else crc = (crc << 1) & 0xFFFFFF;
        }
        crc_table[n] = crc;
    }
}

// Mode-S CRC-24 校验：计算前 (len_bytes-3) 字节的 CRC，与最后 3 字节 XOR
// 结果为 0 表示报文有效
static uint32_t crc24_modes(const uint8_t *msg, int len_bytes) {
    if (crc_table[1] != 0xFFF409) generate_crc_table();
    uint32_t crc = 0;
    for (int i = 0; i < len_bytes - 3; i++) {
        crc = (crc_table[((crc >> 16) ^ msg[i]) & 0xFF] ^ (crc << 8)) & 0xFFFFFF;
    }
    uint32_t ap = (uint32_t)msg[len_bytes-3] << 16
                | (uint32_t)msg[len_bytes-2] << 8
                | (uint32_t)msg[len_bytes-1];
    return (crc ^ ap) & 0xFFFFFF;
}

/* ---- 【功能抽象】6-bit 字符解码（ITU-R M.1371-4 民航字符集） ---- */
static char decode_char_6bit(int code) {
    if (code >= 1 && code <= 26) return 'A' + code - 1;   // 1-26 → A-Z
    if (code >= 48 && code <= 57) return '0' + code - 48;  // 48-57 → 0-9
    return ' ';  // 其他 → 空格
}

/* ---- 【功能抽象】解码 Aircraft Identification（航班号） ---- */
// Type Code 1-4：ME 中包含 8 个 6-bit 字符
static int decode_callsign(const uint8_t *msg, char *cs, int cs_len) {
    int tc = (msg[4] >> 3) & 0x1F;
    if (tc < 1 || tc > 4) return -1;

    // 完美对齐：从 msg[5] 到 msg[10] 精准拼装 48 位民航字符流
    uint64_t raw = 0;
    raw = ((uint64_t)msg[5]) << 40 |
          ((uint64_t)msg[6]) << 32 |
          ((uint64_t)msg[7]) << 24 |
          ((uint64_t)msg[8]) << 16 |
          ((uint64_t)msg[9]) << 8  |
          ((uint64_t)msg[10]);

    int chars[8];
    for (int i = 0; i < 8; i++) {
        // 精准截取 8 个 6-bit 字符，移位修正为 42 - i * 6
        chars[i] = (int)((raw >> (42 - i * 6)) & 0x3F);
    }

    int end = 7;
    while (end >= 0 && (chars[end] == 32 || chars[end] == 0)) end--;

    int out_idx = 0;
    for (int i = 0; i <= end && out_idx < cs_len - 1; i++) {
        char c = decode_char_6bit(chars[i]);
        if (c != ' ') {
            cs[out_idx++] = c;
        }
    }
    cs[out_idx] = '\0';
    return 0;
}

/* ---- 【功能抽象】解码 Barometric Altitude（气压高度） ---- */
// Type Code 9-18：ME bits 41-52 为 12-bit 高度编码
// Q 位（bit 48）为 1 时：N×25-1000 英尺
static int decode_altitude(const uint8_t *msg) {
    int tc = (msg[4] >> 3) & 0x1F;
    if (tc < 9 || tc > 18) return -9999;

    // 精准提取 12-bit 高度
    int alt12 = (((int)msg[5] << 4) | (msg[6] >> 4)) & 0xFFF;

    if (alt12 & 0x010) {  // Q=1: 25 ft 精度
        int n = ((alt12 & 0xFF0) >> 1) | (alt12 & 0x00F);
        return n * 25 - 1000;
    } else {              // Q=0: Gillham 格雷码 100 ft 精度解码
        int m = ((alt12 & 0xFF0) >> 1) | (alt12 & 0x00F);
        int gray = m & 0x1F;
        int binary = 0;
        for (int mask = gray >> 1; mask > 0; mask >>= 1) {
            gray ^= mask;
        }
        binary = gray;
        return binary * 100 - 1200;
    }
}
static int decode_altitude2(const uint8_t *msg) {
    int tc = (msg[4] >> 3) & 0x1F;
    if (tc < 9 || tc > 18) return -9999;  // 不是位置消息

    // 提取 12-bit 高度字段（bits 41-52）
    // bit41=msg[5] bit7, bit52=msg[6] bit4
    int alt12 = (((int)msg[5] << 4) | (msg[6] >> 4)) & 0xFFF;

    // 检查 Q 位（第 8 位，即 alt12 bit4）
    if (alt12 & 0x010) {  // Q=1: 25 ft 精度
        // 拼接 bit11-5 和 bit3-0，跳过 Q 位
        int n = ((alt12 & 0xFF0) >> 1) | (alt12 & 0x00F);
        return n * 25 - 1000;
    }
    // Q=0: Gillham 编码（简化处理，返回 N×100-1000）
    return ((alt12 & 0xFE0) >> 1 | (alt12 & 0x00F)) * 100 - 1000;
}

/* ---- 【功能抽象】解码 Airborne Velocity（速度/航向） ---- */
// Type Code 19：包含地速/真空速 + 航向 + 垂直速度
static void decode_velocity(const uint8_t *msg, char *out, int out_len) {
    int tc = (msg[4] >> 3) & 0x1F;
    if (tc != 19) { snprintf(out, out_len, "N/A"); return; }

    int sub_type = msg[4] & 0x07; 

    float heading = 0.0f;
    int speed = 0;
    char spd_type = 'G'; // G: 地速, A: 空速

    if (sub_type == 1 || sub_type == 2) {
        // ---- 地速模式：由东西、南北两个速度分量合成 ----
        spd_type = 'G';
        
        int ew_sign = (msg[5] & 0x04) >> 2;
        int ew_raw  = ((msg[5] & 0x03) << 8) | msg[6];
        int ns_sign = (msg[7] & 0x80) >> 7;
        int ns_raw  = ((msg[7] & 0x7F) << 3) | (msg[8] >> 5);

        // 如果是 Subtype 2，速度需要乘以 4 (超音速模式，极少见)
        int v_scale = (sub_type == 2) ? 4 : 1;
        
        int v_ew = (ew_raw > 0) ? (ew_raw - 1) * v_scale : 0;
        int v_ns = (ns_raw > 0) ? (ns_raw - 1) * v_scale : 0;
        
        if (ew_sign) v_ew = -v_ew;
        if (ns_sign) v_ns = -v_ns;

        // 矢量合成真正的地速
        speed = (int)sqrtf(v_ew * v_ew + v_ns * v_ns);

        // 计算航向 (从数学坐标系转换到航向方位角)
        if (speed > 0) {
            heading = atan2f(v_ew, v_ns) * 180.0f / M_PI;
            if (heading < 0) heading += 360.0f;
        }
    } 
    else if (sub_type == 3 || sub_type == 4) {
        // ---- 空速模式：直接给出航向和速度 ----
        spd_type = 'A';
        
        int hdg_avail = (msg[5] & 0x04) >> 2;
        int hdg_raw   = ((msg[5] & 0x03) << 8) | msg[6];
        heading = hdg_avail ? (hdg_raw * 360.0f / 1024.0f) : 0.0f;

        int air_raw = ((msg[7] & 0x7F) << 3) | (msg[8] >> 5);
        int v_scale = (sub_type == 4) ? 4 : 1;
        speed = (air_raw > 0) ? (air_raw - 1) * v_scale : 0;
    }

    // ---- 垂直速率解析 (精准对齐 9-bit：msg[8]低5位 + msg[9]高4位) ----
    // 1. 垂直速率源：msg[8] 的 bit 0 (如果是1，说明是几何高度，比例尺要变)
    int vr_source = msg[8] & 0x01; 
    
    // 2. 符号位：msg[9] 的最高位 (Bit 7)
    int vr_sign = (msg[9] & 0x80) >> 7;
    
    // 3. 数值位：msg[9] 的低 7 位左移 2 位拼接 msg[10] 的高 2 位
    int vr_raw = ((msg[9] & 0x7F) << 2) | ((msg[10] >> 6) & 0x03);
    int vr_fpm = 0;
    if (vr_raw > 0) {
        int res = (vr_source == 0) ? 64 : 512;
        vr_fpm = (vr_raw - 1) * res;
        if (vr_sign) vr_fpm = -vr_fpm;

        // 🛑 【终极防御】：过滤掉物理上不可能的射频噪声闪烁值
        if (vr_fpm > 8000 || vr_fpm < -8000) {
            vr_fpm = 0; // 或者直接让它回归 0（平飞/未知状态），保持看板优雅
        }
    }

    snprintf(out, out_len, "HDG=%.0f° SPD=%d%c VR=%dfpm", heading, speed, spd_type, vr_fpm);
}

/* ---- 【功能抽象】ADS-B 报文格式化：完整字段解码 ---- */
static void format_adsb_msg(const uint8_t *msg, char *out, int out_len) {
    // 1. DF + ICAO
    int df = (msg[0] >> 3) & 0x1F;
    uint32_t icao = ((uint32_t)msg[1] << 16) | ((uint32_t)msg[2] << 8) | msg[3];
    int tc = (msg[4] >> 3) & 0x1F;  // Type Code

    // 2. 根据 Type Code 解码
    char extra[128] = "";
    char callsign[16] = "";

    if (tc >= 1 && tc <= 4) {
        // Aircraft Identification
        decode_callsign(msg, callsign, sizeof(callsign));
        snprintf(extra, sizeof(extra), "CALL=%s", callsign);
    } else if (tc >= 9 && tc <= 18) {
        // Airborne Position
        int alt = decode_altitude(msg);

        // ---- 【微调添加】：剥离 CPR 原始数据() ----
        // 在 ADS-B 的 CPR 算法中，想要解出经纬度，在 Python 侧做奇偶帧联立解算是最优雅、最强大的方案。
        // 核心思路是：让 Pluto 负责把原始的 CPR 关键数据剥离出来吐给 UDP，由 Python 负责在飞机状态机里存台账并执行三角函数运算。

        // F 标志位在整个报文的第 56 位 (从1开始算，第55位是Time) -> 对应 msg[6] 的 bit 0 (最低位)
        int cpr_f = msg[6] & 0x01;

        // 纬度 CPR 编码在 bits 57-73 (17位)
        // 跨越 msg[7] 全部 (8位) + msg[8] 全部 (8位) + msg[9] 的最高 1 位
        uint32_t cpr_lat = ((uint32_t)msg[7] << 9) | ((uint32_t)msg[8] << 1) | (msg[9] >> 7);
        cpr_lat &= 0x1FFFF; // 保持 17 位

        // 经度 CPR 编码在 bits 74-90 (17位)
        // 跨越 msg[9] 的低 7 位 + msg[10] 全部 (8位) + msg[11] 的最高 2 位
        uint32_t cpr_lon = (((uint32_t)msg[9] & 0x7F) << 10) | ((uint32_t)msg[10] << 2) | (msg[11] >> 6);
        cpr_lon &= 0x1FFFF; // 保持 17 位

        if (alt > -9999)
            snprintf(extra, sizeof(extra), "ALT=%dft F=%d LATBIN=%u LONBIN=%u", alt, cpr_f, cpr_lat, cpr_lon);
        else
            snprintf(extra, sizeof(extra), "ALT=--- F=%d LATBIN=%u LONBIN=%u", cpr_f, cpr_lat, cpr_lon);
    } else if (tc == 19) {
        // Airborne Velocity
        decode_velocity(msg, extra, sizeof(extra));
    } else if (tc >= 5 && tc <= 8) {
        snprintf(extra, sizeof(extra), "SURFACE");
    } else if (tc >= 20 && tc <= 22) {
        snprintf(extra, sizeof(extra), "POS-GNSS");
    } else {
        snprintf(extra, sizeof(extra), "TC=%02d", tc);
    }

    // 3. 格式化输出
    char hex_str[64];
    int pos = 0;
    for (int i = 0; i < ADS_B_MSG_BYTES; i++) {
        pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X", msg[i]);
    }

    snprintf(out, out_len, "DF=%02d ICAO=%06X %s | %s",
             df, icao, extra, hex_str);
}

/* ---- 全局噪声基底（供 detect_preamble/decode_ppm_bit 使用） ---- */
static float g_noise_floor;

/* ---- 【功能抽象】前导码检测：使用脉冲峰值比判别 ---- */
// 在 8 µs 窗口内检查 4 个脉冲位置的能量是不是远高于间隙位置。
// 返回 1 表示检测到有效前导码，0 表示没有。
static int detect_preamble(const float *mag, int idx, int buf_len) {
    if (idx + PREAMBLE_WINDOW >= buf_len) return 0;

    // 计算 4 个脉冲位置的平均能量
    float pulse_energy = 0;
    int   pulse_count  = 0;

    for (int s = PRE_PULSE1_START; s < PRE_PULSE1_START + HALF_BIT_SAMPS; s++, pulse_count++)
        pulse_energy += mag[idx + s];
    for (int s = PRE_PULSE2_START; s < PRE_PULSE2_START + HALF_BIT_SAMPS; s++, pulse_count++)
        pulse_energy += mag[idx + s];
    for (int s = PRE_PULSE3_START; s < PRE_PULSE3_START + HALF_BIT_SAMPS; s++, pulse_count++)
        pulse_energy += mag[idx + s];
    for (int s = PRE_PULSE4_START; s < PRE_PULSE4_START + HALF_BIT_SAMPS; s++, pulse_count++)
        pulse_energy += mag[idx + s];

    float avg_pulse = pulse_energy / pulse_count;

    // 计算间隙位置的平均能量（脉冲之间的"静默"区）
    float gap_energy = 0;
    int   gap_count  = 0;

    for (int s = PRE_PULSE1_START + HALF_BIT_SAMPS; s < PRE_PULSE2_START; s++, gap_count++)
        gap_energy += mag[idx + s];
    for (int s = PRE_PULSE2_START + HALF_BIT_SAMPS; s < PRE_PULSE3_START; s++, gap_count++)
        gap_energy += mag[idx + s];
    for (int s = PRE_PULSE3_START + HALF_BIT_SAMPS; s < PRE_PULSE4_START; s++, gap_count++)
        gap_energy += mag[idx + s];
    for (int s = PRE_PULSE4_START + HALF_BIT_SAMPS; s < PREAMBLE_WINDOW; s++, gap_count++)
        gap_energy += mag[idx + s];

    float avg_gap = gap_energy / gap_count;

    // 检查脉冲/间隙比和信噪比：脉冲区能量需显著大于间隙区
    if (avg_gap < 1e-10f) avg_gap = 1e-10f;
    float ratio = avg_pulse / avg_gap;

    return (ratio > 3.0f && avg_pulse > 3.0f * g_noise_floor) ? 1 : 0;
}

/* ---- 【功能抽象】PPM 位解码 ---- */
// 每个位持续 10 个采样点：
//   Bit 1: 前 5 个采样点有脉冲（高能量），后 5 个无脉冲（低能量）
//   Bit 0: 前 5 个采样点无脉冲（低能量），后 5 个有脉冲（高能量）
// 返回 0 或 1，-1 表示无法判决
static int decode_ppm_bit(const float *mag, int idx) {
    float first_half  = 0;
    float second_half = 0;

    for (int s = 0; s < HALF_BIT_SAMPS; s++) {
        first_half  += mag[idx + s];
        second_half += mag[idx + HALF_BIT_SAMPS + s];
    }

    float diff = first_half - second_half;
    float threshold = g_noise_floor * 0.3f;
    if (threshold < 10.0f) threshold = 10.0f;

    if (diff > threshold)  return 1;
    if (diff < -threshold) return 0;
    return -1;  // 无法判决
}

/* ---- 【功能抽象】将 PPM 解码出的位写入字节数组 ---- */
static void write_bit(uint8_t *msg, int bit_idx, int bit_val) {
    int byte_idx = bit_idx / 8;
    int bit_off  = 7 - (bit_idx % 8);
    if (bit_val)
        msg[byte_idx] |= (1 << bit_off);
    else
        msg[byte_idx] &= ~(1 << bit_off);
}

/* ---- 【功能抽象】获取位值（从字节数组中） ---- */
static int get_bit(const uint8_t *msg, int bit_idx) {
    int byte_idx = bit_idx / 8;
    int bit_off  = 7 - (bit_idx % 8);
    return (msg[byte_idx] >> bit_off) & 1;
}

/* ---- 全局统计信息 ---- */
static int total_messages   = 0;
static int valid_messages   = 0;
static int total_buffers    = 0;

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
    // 【硬核自适应】：模拟硬件低通滤波器带宽设为采样率的 80%
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

    /* ---- 2.5 设置手动增益（防止 ADC 削波，保留脉冲动态范围） ---- */
    printf("* Configuring RX gain\n");
    struct iio_device *phy_dev = get_ad9361_phy();
    struct iio_channel *rx_gain = iio_device_find_channel(phy_dev, "voltage0", false);
    if (rx_gain) {
        // ADS-B 是脉冲信号，手动增益避免 AGC 推高噪声
        iio_channel_attr_write(rx_gain, "gain_control_mode", "manual");
        iio_channel_attr_write_longlong(rx_gain, "hardwaregain", 60);  // 60 dB：充分利用 ADC 动态范围
    }

    printf("* Initializing AD9361 IIO streaming channels\n");
    IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 0, &rx0_i) && "RX chan I not found");
    IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 1, &rx0_q) && "RX chan Q not found");

    printf("* Enabling IIO streaming channels\n");
    iio_channel_enable(rx0_i);
    iio_channel_enable(rx0_q);

    printf("* Creating non-cyclic IIO buffers with 128 MiS\n");
    rxbuf = iio_device_create_buffer(rx, 128 * 1024, false);
    if (!rxbuf) {
        perror("Could not create RX buffer");
        _shutdown();
    }

    /* ---- 3. 初始化 ADS-B 解码器 ---- */
    // 计算单个 buffer 的最大采样数
    char *buf_end   = (char*)iio_buffer_end(rxbuf);
    char *buf_first = (char*)iio_buffer_first(rxbuf, rx0_i);
    ptrdiff_t p_inc = iio_buffer_step(rxbuf);
    int max_samples = (int)((buf_end - buf_first) / p_inc);

    // 分配幅度缓冲区（可容纳整个 buffer）
    float *magnitude = (float*)malloc(max_samples * sizeof(float));
    IIO_ENSURE(magnitude && "Magnitude buffer allocation failed");

    // 噪声基底（自适应阈值）
    float noise_floor = 100.0f;
    g_noise_floor = noise_floor;

    // 已解码报文缓冲区
    uint8_t msg_buf[ADS_B_MSG_BYTES];

    printf("\n>>> Edge-Computing ADS-B Receiver Started <<<\n");
    printf("Target Dev  : %s\n", param_dev);
    printf("Target PC IP: %s:%d\n", param_ip, param_port);
    printf("Center Freq : %.2f MHz\n", param_lo_mhz);
    printf("Sample Rate : %.2f MHz (Hardware BW: %.2f MHz)\n", param_samp_mhz, param_samp_mhz * 0.8);

    /* ---- 4. 主循环处理 ---- */
    while (!stop) {
        // Refill RX buffer
        ssize_t nbytes_rx = iio_buffer_refill(rxbuf);
        if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); _shutdown(); }

        total_buffers++;

        // READ: Get pointers to RX buf and read IQ from RX buf port 0
        char *p_end     = iio_buffer_end(rxbuf);
        char *p_dat;
        int sample_count = 0;
        float peak_mag = 0;
        float sum_mag  = 0;

        // 第一步：遍历所有采样点，计算幅度 + 统计 + per-buffer 最小值（用于噪声基底）
        float buf_min = 1e30f;
        for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {

            // 提取12位ADC数据，左移4位对齐符号位，转换为浮点数
            float i_val = (float)(((int16_t*)p_dat)[0] << 4);
            float q_val = (float)(((int16_t*)p_dat)[1] << 4);

            // 计算幅度（避免 sqrt，使用平方和）
            float mag_sq = i_val * i_val + q_val * q_val;
            magnitude[sample_count] = mag_sq;

            // 统计：峰值 + 均值 —— 合并到幅度循环中，省去一次全buffer遍历
            if (mag_sq > peak_mag) peak_mag = mag_sq;
            sum_mag += mag_sq;

            // 【修复】per-buffer 最小值追踪：脉冲能量远大于噪声，不会成为最小值
            if (mag_sq < buf_min) buf_min = mag_sq;

            sample_count++;
        }
        // 【修复】跨 buffer NF 追踪：per-buffer 最小值做 EMA，彻底避免脉冲污染
        noise_floor += NF_EMA_ALPHA * (buf_min - noise_floor);
        if (noise_floor < 10.0f) noise_floor = 10.0f;

        g_noise_floor = noise_floor;
        float avg_mag = sum_mag / (sample_count > 0 ? sample_count : 1);

        // 第二步：搜索前导码（步进2采样点，4 MSPS下脉冲宽度为2不会漏检，CPU减半）
        int search_end = sample_count - PREAMBLE_WINDOW - ADS_B_MSG_BITS * BIT_SAMPS;
        int  preamble_candidates = 0;
        float pulse_thresh = noise_floor * THRESHOLD_FACTOR;
        // 安全钳：NF 未收敛时（首个 buffer），以信号均值为基准避免误触发
        float min_thresh = avg_mag * 0.3f;
        if (pulse_thresh < min_thresh) pulse_thresh = min_thresh;

        for (int i = 0; i < search_end; i += HALF_BIT_SAMPS) {
            // 快速预检：第一个脉冲位置的能量要显著高于噪声基底
            float first_pulse_check = 0;
            for (int s = 0; s < HALF_BIT_SAMPS; s++)
                first_pulse_check += magnitude[i + s];
            first_pulse_check /= HALF_BIT_SAMPS;

            if (first_pulse_check < pulse_thresh)
                continue;

            // 完整前导码检测
            if (!detect_preamble(magnitude, i, sample_count))
                continue;
            preamble_candidates++;

            // 前导码通过！尝试解码 112 位 PPM 数据
            int data_start = i + PREAMBLE_WINDOW;  // 数据段开始位置
            int msg_end   = data_start + ADS_B_MSG_BITS * BIT_SAMPS;

            if (msg_end >= sample_count) break;

            // 清空报文缓冲区
            memset(msg_buf, 0, ADS_B_MSG_BYTES);

            // 逐个解码 112 位
            int undecided_bits = 0;
            for (int bit = 0; bit < ADS_B_MSG_BITS; bit++) {
                int bit_start = data_start + bit * BIT_SAMPS;
                int bit_val = decode_ppm_bit(magnitude, bit_start);
                if (bit_val < 0) {
                    undecided_bits++;
                    bit_val = 0;  // 无法判决的位默认为 0
                }
                write_bit(msg_buf, bit, bit_val);
            }

            total_messages++;

            // CRC-24 校验
            uint32_t crc_result = crc24_modes(msg_buf, ADS_B_MSG_BYTES);

            if (crc_result == 0 && undecided_bits == 0) {
                // CRC 通过！消息有效
                valid_messages++;

                // 格式化报文为文本
                char msg_str[256];
                format_adsb_msg(msg_buf, msg_str, sizeof(msg_str));

                // 通过 UDP 发送到上位机（纯文本）
                sendto(sockfd, msg_str, strlen(msg_str), 0,
                       (struct sockaddr *)&servaddr, sizeof(servaddr));

                // 同时打印到串口
                printf(">>> %s\n", msg_str);
            }

            // 跳过已解码的消息区域，避免重复检测
            i = msg_end;
        }

        // 【诊断】每 20 个 buffer 打印一次信号统计
        if (total_buffers % 20 == 0) {
            printf("[DIAG] buf#%d | NF=%.0f | Avg=%.0f | Peak=%.0f | PreCand=%d | Msg=%d | Valid=%d\n",
                   total_buffers, noise_floor, avg_mag, peak_mag,
                   preamble_candidates, total_messages, valid_messages);
        }
    }

    /* ---- 5. 统计输出 + 清理退出 ---- */
    printf("\n=== ADS-B Receiver Statistics ===\n");
    printf("Total buffers processed : %d\n", total_buffers);
    printf("Total messages detected : %d\n", total_messages);
    printf("Valid (CRC-passed) msgs : %d\n", valid_messages);

    free(magnitude);
    _shutdown();
    return 0;
}
