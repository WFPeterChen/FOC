/********************************************************************************
 * @file        : cmd_task.c
 * @brief       : CAN 命令任务 —— 已切换为达妙(DM)电机协议
 *
 * @details:
 *  - 原自定义 HEADER+CRC8+TAIL 多帧协议(can_driver) 已替换为 DM 协议(dm_can)。
 *  - DM 收发在 CAN RX 回调(ISR)里完成：解包三种控制帧/特殊命令 + 每帧回反馈。
 *  - 规格见 MODULES/can_comm/DM_CAN_PROTOCOL.md。
 ********************************************************************************/

#include "cmd_task.h"
#include "dm_can.h"

void RobotCMDInit(void)
{
    DM_CanInit(); // 注册 DM 的 3 个 RX ID(MIT/位速/速度) + 反馈
}

void RobotCMDTask(void)
{
    // DM 协议是请求-应答式，收发都在 dm_can 的 CAN RX 回调里完成，此处无需轮询。
    DM_CanHealthCheck(); // #2 FDCAN bus-off 自恢复(内部节流 20ms, 正常时近零开销)
}
