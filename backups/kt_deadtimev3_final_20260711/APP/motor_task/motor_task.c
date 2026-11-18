#include "motor_task.h"

#define ADJUST_EN 0 // ����ʹ��

// 诊断: 测 FOC ISR(20kHz)执行周期数, 排查是否超时(50us=8400cyc@168MHz)饿死 FDCAN RX 中断
volatile uint32_t g_foc_cyc = 0, g_foc_cyc_max = 0, g_foc_over_cnt = 0;

// ADC采样点/ISR时序调试脚: PA7 (GH1.25座 SPI1_MOSI 脚, SPI1 无人用, 2026-07-11)
// 20kHz ISR 入口置高/出口置低 → 示波器上: 上升沿≈注入转换完成时刻(真实采样点在其前~1µs),
// 高电平宽度=FOC ISR 耗时。常开: 每拍只多 2 次寄存器写(ns级)。
void FocDbgPin_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin   = GPIO_PIN_7;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    GPIOA->BRR = GPIO_PIN_7;
}

/**
 * @brief ADC1 ����������ɻص�����
 *
 * ��������Ƶ��20khz
 *
 * @param hadc ����� ADC1
 */
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    // �����¼�������
    UNUSED(motor_data.components.current->hadc);
    // ������ת���� ADC �Ƿ�Ϊ ADC1
    if (hadc == &ADC_HSPI)
    {
        GPIOA->BSRR = GPIO_PIN_7; // 调试脚PA7↑: 注入转换完成/ISR开始
#if ADJUST_EN
        GetMotorADC1PhaseCurrent(&motor_data);
        GetMotor_Angle(motor_data.components.encoder);
#else
        uint32_t _c0 = DWT->CYCCNT;
        GetMotorADC1PhaseCurrent(&motor_data);
        GetMotor_Angle(motor_data.components.encoder);
        MotorStateTask(&motor_data);
        g_foc_cyc = DWT->CYCCNT - _c0;
        if (g_foc_cyc > g_foc_cyc_max) g_foc_cyc_max = g_foc_cyc;
        if (g_foc_cyc > 8400u) g_foc_over_cnt++; // 超过 50us 周期 = 超时
#endif
        GPIOA->BRR = GPIO_PIN_7; // 调试脚PA7↓: ISR结束
    }
}
