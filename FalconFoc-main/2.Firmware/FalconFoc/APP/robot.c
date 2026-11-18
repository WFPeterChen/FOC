#include "bsp_init.h"
#include "robot.h"
#include "robot_def.h"
#include "robot_task.h"
#include "cmd_task.h"
#include "bsp_log.h"

void RobotInit(void)
{
    // 关闭中断,防止在初始化过程中发生中断
    // 请不要在初始化过程中使用中断和延时函数！
    // 若必须,则只允许使用DWT_Delay()
    __disable_irq();

    DWT_Delay(0.016f); // MT6816上电的16ms无输出数据
    BSPInit();         // 初始化DWT
    LogInit(&huart1);  // 初始化日志
    adc_bsp_init();    // 初始化ADC，配置ADC硬件
    Foc_Pwm_Start();   // foc启动PWM
    RGB_DisplayColorById(0);

    __enable_irq(); // 初始化完成,开启中断

    // 独立看门狗 IWDG (~250ms): 固件挂死/hardfault(其handler已关PWM)时兜底复位MCU→重启PWM默认关。
    // 由 StartGuardTask(5ms健康循环)喂狗; 寄存器级配置, 不依赖HAL模块。
    DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP; // 调试halt时冻结IWDG, 否则每次SWD halt都复位
    IWDG->KR  = 0x5555; // 使能PR/RLR写
    IWDG->PR  = 4;      // LSI32kHz /64 ≈ 500Hz
    IWDG->RLR = 125;    // 重载125 ≈ 250ms 超时
    IWDG->KR  = 0xAAAA; // 刷新
    IWDG->KR  = 0xCCCC; // 启动(启动后LSI强制ON)
}

void RobotTask(void)
{
}
