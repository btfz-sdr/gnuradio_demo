// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD9361 IIO streaming example for BLE ADV Receiver
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

#include "ble_phy.h"

#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define GHZ(x) ((long long)(x*1000000000.0 + .5))

#define IIO_ENSURE(expr) { \
    if (!(expr)) { \
        (void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
        (void) abort(); \
    } \
}

enum iodev { RX, TX };

struct stream_cfg {
    long long bw_hz;    
    long long fs_hz;    
    long long lo_hz;    
    const char* rfport; 
};

static char tmpstr[64];
static struct iio_context *ctx   = NULL;
static struct iio_channel *rx0_i = NULL;
static struct iio_channel *rx0_q = NULL;
static struct iio_buffer  *rxbuf = NULL;
static bool stop;

static void _shutdown(void)
{
    printf("* Destroying buffers\n");
    if (rxbuf) { iio_buffer_destroy(rxbuf); }

    printf("* Disabling streaming channels\n");
    if (rx0_i) { iio_channel_disable(rx0_i); }
    if (rx0_q) { iio_channel_disable(rx0_q); }

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

static void errchk(int v, const char* what) {
     if (v < 0) { fprintf(stderr, "Error %d writing to channel \"%s\"\n", v, what); _shutdown(); }
}

static void wr_ch_lli(struct iio_channel *chn, const char* what, long long val) {
    errchk(iio_channel_attr_write_longlong(chn, what, val), what);
}

static void wr_ch_str(struct iio_channel *chn, const char* what, const char* str) {
    errchk(iio_channel_attr_write(chn, what, str), what);
}

static char* get_ch_name(const char* type, int id) {
    snprintf(tmpstr, sizeof(tmpstr), "%s%d", type, id);
    return tmpstr;
}

static struct iio_device* get_ad9361_phy(void) {
    struct iio_device *dev =  iio_context_find_device(ctx, "ad9361-phy");
    IIO_ENSURE(dev && "No ad9361-phy found");
    return dev;
}

static bool get_ad9361_stream_dev(enum iodev d, struct iio_device **dev) {
    switch (d) {
    case TX: *dev = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc"); return *dev != NULL;
    case RX: *dev = iio_context_find_device(ctx, "cf-ad9361-lpc");  return *dev != NULL;
    default: IIO_ENSURE(0); return false;
    }
}

static bool get_ad9361_stream_ch(enum iodev d, struct iio_device *dev, int chid, struct iio_channel **chn) {
    *chn = iio_device_find_channel(dev, get_ch_name("voltage", chid), d == TX);
    if (!*chn)
        *chn = iio_device_find_channel(dev, get_ch_name("altvoltage", chid), d == TX);
    return *chn != NULL;
}

static bool get_phy_chan(enum iodev d, int chid, struct iio_channel **chn) {
    switch (d) {
    case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), false); return *chn != NULL;
    case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), true);  return *chn != NULL;
    default: IIO_ENSURE(0); return false;
    }
}

static bool get_lo_chan(enum iodev d, struct iio_channel **chn) {
    switch (d) {
    case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("altvoltage", 0), true); return *chn != NULL;
    case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("altvoltage", 1), true); return *chn != NULL;
    default: IIO_ENSURE(0); return false;
    }
}

bool cfg_ad9361_streaming_ch(struct stream_cfg *cfg, enum iodev type, int chid) {
    struct iio_channel *chn = NULL;

    printf("* Acquiring AD9361 phy channel %d\n", chid);
    if (!get_phy_chan(type, chid, &chn)) { return false; }
    wr_ch_str(chn, "rf_port_select",     cfg->rfport);
    wr_ch_lli(chn, "rf_bandwidth",       cfg->bw_hz);
    wr_ch_lli(chn, "sampling_frequency", cfg->fs_hz);

    if (type == RX) {
        // 🌟 针对突发性 BLE 广播包，推荐使用 fast_attack AGC 模式，防止强信号削顶失真
        wr_ch_str(chn, "gain_control_mode", "fast_attack");
        printf("[RF INFO] AD9361 增益模式已设为 fast_attack (AGC)\n");
    }

    printf("* Acquiring AD9361 %s lo channel\n", type == TX ? "TX" : "RX");
    if (!get_lo_chan(type, &chn)) { return false; }
    wr_ch_lli(chn, "frequency", cfg->lo_hz);
    return true;
}

#define DEFAULT_SAMP_RATE_MHZ 10.0f     
#define DEFAULT_RX_BANDWIDTH  3.0f     
#define DEFAULT_BLE_CHANNEL   37

/* BLE 广播信道中心频率 (MHz) */
static double ble_channel_freq_mhz(int ch)
{
    switch (ch) {
    case 37: return 2402.0;
    case 38: return 2426.0;
    case 39: return 2480.0;
    default: return 0.0;
    }
}

void print_help(char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d <device>  plutosdr 设备 URI (默认: default)\n");
    printf("  -c <channel> 蓝牙广播信道号 37/38/39 (默认: 37)，同时决定中心频率\n");
    printf("               37=2402MHz  38=2426MHz  39=2480MHz\n");
    printf("  -f <freq>    中心频率 Center Frequency in MHz (默认: 跟随 -c)\n");
    printf("  -s <rate>    采样率 Sampling Rate in MHz (默认: %.1f)\n", DEFAULT_SAMP_RATE_MHZ);
    printf("  -h           显示当前帮助菜单\n");
}

int main(int argc, char **argv) {
    struct iio_device *rx;
    struct stream_cfg rxcfg;

    char param_dev[32] = "default";
    double param_lo_mhz = 0.0;                    /* 0 = 跟随 -c 自动选择 */
    double param_samp_mhz = DEFAULT_SAMP_RATE_MHZ;
    int param_ble_channel = DEFAULT_BLE_CHANNEL; 

    int opt;
    while ((opt = getopt(argc, argv, "d:f:s:c:h")) != -1) {
        switch (opt) {
            case 'd': strncpy(param_dev, optarg, sizeof(param_dev) - 1); break;
            case 'f': param_lo_mhz = atof(optarg); break;
            case 's': param_samp_mhz = atof(optarg); break;
            case 'c': param_ble_channel = atoi(optarg); break;
            case 'h':
            default:
                print_help(argv[0]);
                return 0;
        }
    }

    if (ble_channel_freq_mhz(param_ble_channel) == 0.0) {
        printf("[ERROR] 不支持的信道 %d，仅支持 37 / 38 / 39\n", param_ble_channel);
        print_help(argv[0]);
        return 1;
    }
    if (param_lo_mhz <= 0.0)
        param_lo_mhz = ble_channel_freq_mhz(param_ble_channel);

    /* 采样率决定每个符号的样点数（10 MHz -> 10 样点/符号） */
    ble_phy_set_sample_rate(param_samp_mhz);

    signal(SIGINT, handle_sig);

    rxcfg.lo_hz   = (long long)(param_lo_mhz * 1000000.0 + .5);
    rxcfg.fs_hz   = (long long)(param_samp_mhz * 1000000.0 + .5);
    rxcfg.bw_hz   = (long long)(DEFAULT_RX_BANDWIDTH * 1000000.0 + .5);
    rxcfg.rfport  = "A_BALANCED"; 

    printf("* Acquiring IIO context\n");
    if (memcmp(param_dev, "default", 7) == 0) {
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

    printf("\n>>> Edge-Computing BLE ADV Receiver Started <<<\n");
    printf("Target Dev  : %s\n", param_dev);
    printf("Center Freq : %.2f MHz\n", param_lo_mhz);
    printf("Sample Rate : %.2f MHz\n", param_samp_mhz);
    printf("BLE Channel : %d (De-whitening Seed)\n\n", param_ble_channel);

    int sample_block_size = 1024 * 256; 
    rxbuf = iio_device_create_buffer(rx, sample_block_size, false);
    if (!rxbuf) {
        perror("Could not create RX buffer");
        _shutdown();
    }

    int16_t *local_demod_buf = malloc(sample_block_size * 2 * sizeof(int16_t));

    while (!stop) {
        ssize_t nbytes_rx = iio_buffer_refill(rxbuf);
        if (nbytes_rx < 0) { printf("Error refilling RX buf %d\n", (int)nbytes_rx); break; }

        ptrdiff_t p_inc = iio_buffer_step(rxbuf);
        char *p_end     = iio_buffer_end(rxbuf);
        char *p_dat;

        int sample_idx = 0;
        for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {
            local_demod_buf[2 * sample_idx]     = ((int16_t*)p_dat)[0];
            local_demod_buf[2 * sample_idx + 1] = ((int16_t*)p_dat)[1];
            sample_idx++;
        }

        demodulate_ble_packets(local_demod_buf, sample_idx, param_ble_channel);
    }

    free(local_demod_buf);
    _shutdown();
    return 0;
}
