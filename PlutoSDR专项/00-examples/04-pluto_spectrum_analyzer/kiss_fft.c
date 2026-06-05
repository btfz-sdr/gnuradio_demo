#include "kiss_fft.h"

struct kiss_fft_state {
    int nfft;
    int inverse;
    int *twiddle_indexes;
    kiss_fft_cpx *twiddles;
};

/* 核心：蝶形运算旋转因子初始化 */
kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void * mem, size_t * lenmem) {
    if (lenmem != NULL) {
        *lenmem = sizeof(struct kiss_fft_state) + sizeof(kiss_fft_cpx) * nfft + sizeof(int) * nfft;
        if (mem == NULL) return NULL;
    }

    kiss_fft_cfg st = (kiss_fft_cfg)malloc(sizeof(struct kiss_fft_state));
    if (!st) return NULL;

    st->nfft = nfft;
    st->inverse = inverse_fft;
    st->twiddles = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx) * nfft);
    st->twiddle_indexes = (int*)malloc(sizeof(int) * nfft);

    if (!st->twiddles || !st->twiddle_indexes) {
        free(st->twiddles); free(st->twiddle_indexes); free(st);
        return NULL;
    }

    // 预计算旋转因子 (Twiddle Factors)
    for (int i = 0; i < nfft; ++i) {
        double phase = -2 * M_PI * i / nfft;
        if (inverse_fft) phase = -phase;
        st->twiddles[i].r = (kiss_fft_scalar)cos(phase);
        st->twiddles[i].i = (kiss_fft_scalar)sin(phase);
    }

    // 位反转 (Bit Reversal) 查表初始化
    int i, j, limit;
    st->twiddle_indexes[0] = 0;
    for (i = 1, j = 0, limit = nfft; i < limit; ++i) {
        int bit = limit >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        st->twiddle_indexes[i] = j;
    }

    return st;
}

/* 核心：标准的 Cooley-Tukey 基-2 快速傅里叶变换算法实现 */
void kiss_fft(kiss_fft_cfg st, const kiss_fft_cpx *fin, kiss_fft_cpx *fout) {
    int nfft = st->nfft;
    
    // 1. 搬移数据：利用预计算好的位反转索引进行时域乱序排列
    for (int i = 0; i < nfft; ++i) {
        fout[st->twiddle_indexes[i]] = fin[i];
    }

    // 2. 迭代执行蝶形运算阶段 (Stages)
    for (int size = 2; size <= nfft; size <<= 1) {
        int halfsize = size >> 1;
        int tabstep = nfft / size;
        
        for (int i = 0; i < nfft; i += size) {
            for (int j = i, k = 0; j < i + halfsize; ++j, k += tabstep) {
                kiss_fft_cpx t;
                kiss_fft_cpx twiddle = st->twiddles[k];
                
                // 复数乘法：t = fout[j + halfsize] * twiddle
                t.r = fout[j + halfsize].r * twiddle.r - fout[j + halfsize].i * twiddle.i;
                t.i = fout[j + halfsize].r * twiddle.i + fout[j + halfsize].i * twiddle.r;
                
                // 蝶形合并
                fout[j + halfsize].r = fout[j].r - t.r;
                fout[j + halfsize].i = fout[j].i - t.i;
                fout[j].r += t.r;
                fout[j].i += t.i;
            }
        }
    }
}

void kiss_fft_free(void* cfg) {
    if (cfg) {
        kiss_fft_cfg st = (kiss_fft_cfg)cfg;
        free(st->twiddles);
        free(st->twiddle_indexes);
        free(st);
    }
}
