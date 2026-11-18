#ifndef MOTOR_CALIB_H
#define MOTOR_CALIB_H

#include "FOCMotor.h"
#include "stdbool.h"

/*
 * 电机标定结果的 flash 持久化 (Phase B)。
 * 标定一次 → 存 flash 末页(page63=0x0801F800) → 开机加载、不重标。
 * 换电机: MotorCalib_Erase() 后下次开机会重标并存新值。
 * 存的内容: Rs/Ls/flux/编码器零位/极对数/方向/offset_lut[128]。
 */
#define MOTOR_CALIB_MAGIC 0xCA117621u // LJW8115 KV76 profile; rejects legacy 4010/5210 pages

bool MotorCalib_LoadFromFlash(MOTOR_DATA *motor); // flash有效则加载到motor并返回true
bool MotorCalib_LoadCoggingOnly(void);            // 仅加载cogging_map(PRE_CAL=1时用, 使FF开机生效)
void MotorCalib_SaveToFlash(MOTOR_DATA *motor);   // 把当前标定写入flash(调用前应关PWM)
void MotorCalib_Erase(void);                      // 擦除flash标定 → 下次开机重标

#endif // MOTOR_CALIB_H
