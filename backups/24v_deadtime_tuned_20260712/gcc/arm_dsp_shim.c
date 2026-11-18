/*
 * arm_dsp_shim.c —— GCC 构建用：CMSIS-DSP 在本仓库是纯头文件，
 * arm_sin_f32/arm_cos_f32 原由 Keil 链接的 DSP 库提供。
 * 这里用 general_def.h 里的 fast_sincos() 顶替这两个函数，避免依赖 DSP 库。
 */
#include "general_def.h"

float arm_sin_f32(float x)
{
    float s, c;
    fast_sincos(x, &s, &c);
    return s;
}

float arm_cos_f32(float x)
{
    float s, c;
    fast_sincos(x, &s, &c);
    return c;
}
