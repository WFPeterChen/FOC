#ifndef COGGING_H
#define COGGING_H

#include "FOCMotor.h"

/*
 * 齿槽转矩标定(寄生在编码器标定的开环旋转)+前馈补偿。
 * 索引 = 编码器 raw(14bit) >> COGGING_SHIFT。N=512(raw>>5)。
 */
#define COGGING_N     512
#define COGGING_SHIFT 5 // 16384/512 = 32 = 1<<5

void  Cogging_Reset(void);          // 标定开始(CS_ENCODER_START)清零累加器
void  Cogging_RecordCW(MOTOR_DATA *m);  // CW循环每周期累加iq
void  Cogging_RecordCCW(MOTOR_DATA *m); // CCW循环每周期累加iq
void  Cogging_Compute(void);        // 标定结束(CS_ENCODER_END)算出map, 置valid
float Cogging_FF(uint16_t raw);     // 前馈iq(valid时返回, 否则0)

// 闭环慢速匀速扫+ILC迭代标定: Start(reset_map: true=清零首遍/false=在现有map上迭代),
// 每控制拍调 Update 驱动位置环匀速扫、记录残差
void Cogging_CalStart(MOTOR_DATA *m, bool reset_map);
bool Cogging_CalUpdate(MOTOR_DATA *m); // 返回true=标定进行中(已接管input_position)

extern float   cogging_map[COGGING_N];
extern float   friction_map[COGGING_N];
extern uint8_t cogging_valid;
extern volatile uint8_t  g_cogcal_phase; // 0空闲/1正扫/2反扫/3收尾 (SWD观察进度)
extern volatile uint16_t g_cogcal_step;  // 0..COGGING_N

#endif // COGGING_H
