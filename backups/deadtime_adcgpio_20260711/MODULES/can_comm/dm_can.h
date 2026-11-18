#ifndef DM_CAN_H
#define DM_CAN_H

#include "stdint.h"

/*
 * 达妙(DM)电机 CAN 协议实现 —— 见 DM_CAN_PROTOCOL.md
 * 经典 CAN 1Mbps, 8 字节, 标准帧。三种模式: MIT / 位置-速度 / 速度 + 特殊命令 + 反馈。
 */

/* ---- ID 分配 ---- */
#define DM_MOTOR_ID   0x01u   // 本板电机 ID
#define DM_MASTER_ID  0x00u   // 反馈目标(主控) ID
#define DM_ID_MIT     (DM_MOTOR_ID)          // MIT / 特殊命令
#define DM_ID_POSVEL  (0x100u + DM_MOTOR_ID) // 位置-速度
#define DM_ID_VEL     (0x200u + DM_MOTOR_ID) // 速度

/* ---- 量化范围(与 DM_CAN_PROTOCOL.md / 上位机 必须一致) ---- */
#define DM_P_MAX   50.0f    // 位置 ±rad   (16 bit)
#define DM_V_MAX   300.0f   // 速度 ±rad/s (12 bit)
#define DM_T_MAX   0.3f     // 力矩 ±N·m   (12 bit)
#define DM_KP_MIN  0.0f
#define DM_KP_MAX  500.0f   // (12 bit)
#define DM_KD_MIN  0.0f
#define DM_KD_MAX  5.0f     // (12 bit)

/* ---- 特殊命令尾字节(前 7 字节 0xFF) ---- */
#define DM_CMD_ENABLE     0xFCu
#define DM_CMD_DISABLE    0xFDu
#define DM_CMD_SET_ZERO   0xFEu
#define DM_CMD_CLEAR_ERR  0xFBu
#define DM_CMD_COGGING_CAL 0xFAu // 扩展(非DM标准): 启动齿槽标定
#define DM_CMD_SING        0xF9u // 扩展(非DM标准): 让电机唱歌(播放旋律)

void DM_CanInit(void);        // 注册 3 个 RX ID + 反馈 TX
void DM_CanHealthCheck(void); // FDCAN bus-off 自恢复(周期调用, 内部节流)

extern volatile uint32_t g_can_last_rx_ms;  // 上次收到合法 CAN 指令的时间戳(DWT CYCCNT 周期数), deadman 用
extern volatile uint32_t g_can_recover_cnt; // FDCAN bus-off 自恢复触发次数

#endif // DM_CAN_H
