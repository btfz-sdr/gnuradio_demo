#ifndef KISS_FFT_H
#define KISS_FFT_H

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 根据 PlutoSDR 的 ARM 架构，默认使用 float（单精度浮点）以配合 NEON 加速 */
#ifndef kiss_fft_scalar
#define kiss_fft_scalar float
#endif

typedef struct {
    kiss_fft_scalar r;
    kiss_fft_scalar i;
} kiss_fft_cpx;

typedef struct kiss_fft_state* kiss_fft_cfg;

/* * 初始化 FFT 配置
 * nfft: FFT 点数 (例如 1024)
 * inverse_fft: 0 为正变换 (FFT)，1 为反变换 (IFFT)
 */
kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void * mem, size_t * lenmem);

/*
 * 执行 FFT 变换
 * cfg: 经过 alloc 初始化的配置结构体
 * fin: 输入时域 I/O 数组 (大小为 nfft)
 * fout: 输出频域 I/O 数组 (大小为 nfft)
 */
void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_cpx *fin, kiss_fft_cpx *fout);

/* 释放内存 */
void kiss_fft_free(void* cfg);

#ifdef __cplusplus
}
#endif

#endif
