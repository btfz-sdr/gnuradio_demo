// main.c - test linking to liquid
#include <stdio.h>
#include <stdlib.h>
#include <liquid/liquid.h>

int main()
{
    // 创建重采样对象，打印信息并释放
    printf("creating test object...\n");
    resamp_crcf q = resamp_crcf_create(0.12345f, 12, 0.25f, 60.0f, 256);
    resamp_crcf_print(q);
    resamp_crcf_destroy(q);
    return 0;
}
