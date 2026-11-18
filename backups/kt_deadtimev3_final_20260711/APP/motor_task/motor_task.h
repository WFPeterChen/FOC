#ifndef MOTOR_TASK_H
#define MOTOR_TASK_H

#include "general_def.h"
#include "FOCMotor.h"
#include "BLDCMotor.h"
#include "mt6816_encoder.h"

void FocDbgPin_Init(void); // PA7 调试脚(ADC采样点/ISR时序, 2026-07-11)

#endif // MOTOR_TASK_H
