#include "motor_task.h"

#define ADJUST_EN 0 // ����ʹ��

// 诊断: 测 FOC ISR(20kHz)执行周期数, 排查是否超时(50us=8400cyc@168MHz)饿死 FDCAN RX 中断
volatile uint32_t g_foc_cyc = 0, g_foc_cyc_max = 0, g_foc_over_cnt = 0;

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
    }
}
