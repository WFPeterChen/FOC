/********************************************************************************
 * dm_can.c —— 达妙(DM)电机 CAN 协议实现
 * 规格见同目录 DM_CAN_PROTOCOL.md。基于 bsp_can 的 FDCANInstance(绕开 can_driver 多帧层)。
 * 线上单位 SI(rad/rad·s⁻¹/N·m)，固件内部 turn/turn·s⁻¹ —— 在本文件边界做 ×2π / Kt 换算。
 ********************************************************************************/
#include "dm_can.h"
#include "bsp_can.h"
#include "FOCMotor.h"
#include "cogging.h"
#include "general_def.h"
#include "string.h"

extern MOTOR_DATA motor_data;
extern volatile uint8_t g_sing_on; // FOCMotor.c: 唱歌开关

// 通信看门狗: 每收到一帧合法指令就刷新(ms)。控制端(FOCMotor)据此判掉线超时→GUARD 防失控。
volatile uint32_t g_can_last_rx_ms = 0;

// #2 FDCAN 总线关闭(bus-off)自恢复计数: 收发器抖动/总线故障使 TEC>255 → FDCAN 自动进 INIT 停机,
//    无自动恢复。健康检查检测到 bus-off/意外 INIT 就清 CCCR.INIT 触发总线恢复。SWD 读此计数看恢复次数。
volatile uint32_t g_can_recover_cnt = 0;

static FDCANInstance *dm_mit;    // rx=DM_ID_MIT, tx=DM_MASTER_ID(反馈也用它发)
static FDCANInstance *dm_posvel; // rx=DM_ID_POSVEL
static FDCANInstance *dm_vel;    // rx=DM_ID_VEL

/* ---------------- 量化 float <-> 定点 ---------------- */
static uint32_t float_to_uint(float x, float xmin, float xmax, uint8_t bits)
{
    float span = xmax - xmin;
    if (x < xmin) x = xmin;
    else if (x > xmax) x = xmax;
    return (uint32_t)((x - xmin) * (((float)((1u << bits) - 1u)) / span));
}

static float uint_to_float(uint32_t xi, float xmin, float xmax, uint8_t bits)
{
    float span = xmax - xmin;
    return ((float)xi) * span / ((float)((1u << bits) - 1u)) + xmin;
}

/* ---------------- 反馈帧 ---------------- */
static uint8_t dm_error_nibble(void)
{
    if (motor_data.state.Fault_State == FAULT_STATE_NORMAL)
        return (motor_data.state.State_Mode == STATE_MODE_RUNNING) ? 1u : 0u;
    switch (motor_data.state.Fault_State)
    {
    case FAULT_STATE_OVER_VOLTAGE:     return 8u;
    case FAULT_STATE_UNDER_VOLTAGE:    return 9u;
    case FAULT_STATE_OVER_CURRENT:     return 0xAu;
    case FAULT_STATE_OVER_TEMPERATURE: return 0xBu;
    case FAULT_STATE_SPEEDING:         return 0xEu;
    default:                           return 1u;
    }
}

static void DM_SendFeedback(void)
{
    float pos_rad = motor_data.components.encoder->pos_estimate_ * M_2PI;
    float vel_rad = motor_data.components.encoder->vel_estimate_ * M_2PI;
    float torque  = motor_data.components.foc->i_q * motor_data.Controller.torque_const;

    uint16_t p_int = (uint16_t)float_to_uint(pos_rad, -DM_P_MAX, DM_P_MAX, 16);
    uint16_t v_int = (uint16_t)float_to_uint(vel_rad, -DM_V_MAX, DM_V_MAX, 12);
    uint16_t t_int = (uint16_t)float_to_uint(torque, -DM_T_MAX, DM_T_MAX, 12);

    float tmp = motor_data.components.current->Temp_Result;
    if (tmp < 0.0f) tmp = 0.0f; else if (tmp > 255.0f) tmp = 255.0f;
    uint8_t temp8 = (uint8_t)tmp; // 仅一颗 NTC，T_mos 与 T_rotor 暂用同一值

    uint8_t *tx = dm_mit->tx_buff;
    tx[0] = (uint8_t)((dm_error_nibble() << 4) | (DM_MOTOR_ID & 0x0F));
    tx[1] = (uint8_t)(p_int >> 8);
    tx[2] = (uint8_t)(p_int & 0xFF);
    tx[3] = (uint8_t)(v_int >> 4);
    tx[4] = (uint8_t)(((v_int & 0xF) << 4) | (t_int >> 8));
    tx[5] = (uint8_t)(t_int & 0xFF);
    tx[6] = temp8;
    tx[7] = temp8;

    FDCANTransmit(dm_mit, 0.0f); // 非阻塞: 在 CAN RX 中断里发, TX FIFO 满则丢弃当帧反馈(不 busy-wait)
}

/* ---------------- 各模式解包 ---------------- */
static void DM_HandleMIT(const uint8_t *rx)
{
    uint16_t p_int  = (uint16_t)((rx[0] << 8) | rx[1]);
    uint16_t v_int  = (uint16_t)((rx[2] << 4) | (rx[3] >> 4));
    uint16_t kp_int = (uint16_t)(((rx[3] & 0xF) << 8) | rx[4]);
    uint16_t kd_int = (uint16_t)((rx[5] << 4) | (rx[6] >> 4));
    uint16_t t_int  = (uint16_t)(((rx[6] & 0xF) << 8) | rx[7]);

    float p_des = uint_to_float(p_int, -DM_P_MAX, DM_P_MAX, 16); // rad
    float v_des = uint_to_float(v_int, -DM_V_MAX, DM_V_MAX, 12); // rad/s
    float kp    = uint_to_float(kp_int, DM_KP_MIN, DM_KP_MAX, 12);
    float kd    = uint_to_float(kd_int, DM_KD_MIN, DM_KD_MAX, 12);
    float t_ff  = uint_to_float(t_int, -DM_T_MAX, DM_T_MAX, 12); // N·m

    // rad -> turn；kp/kd 由 (·/rad) 转 (·/turn)，乘 2π 使 kp_turn·Δturn == kp_rad·Δrad
    motor_data.Controller.input_position = p_des / M_2PI;
    motor_data.Controller.input_velocity = v_des / M_2PI;
    motor_data.Controller.input_kp       = kp * M_2PI;
    // MIT Kd 上限: 4010 Kt≈0.0265 → Kd/Kt≈38× 放大, SWD 实测稳定上限 CAN Kd≈0.016
    // (=input_kd≈0.1)。再高会自激剧烈振动(见 falconfoc-mit-kd-limit)。这里硬钳保护,
    // 上正经低齿槽/高 Kt 力矩电机时按该电机重新标定该上限。
    {
        float kd_turn = kd * M_2PI;
        if (kd_turn > MIT_KD_TURN_MAX) kd_turn = MIT_KD_TURN_MAX;
        motor_data.Controller.input_kd   = kd_turn;
    }
    motor_data.Controller.input_torque   = t_ff;
    motor_data.state.Control_Mode        = CONTROL_MODE_MIT; // 最后设模式，避免半更新
}

static void DM_HandlePosVel(const uint8_t *rx)
{
    float p, v;
    memcpy(&p, rx, 4);     // float32 小端
    memcpy(&v, rx + 4, 4);
    float new_pos = p / M_2PI; // turn
    // 只在目标位置真正变化(或刚切入位置模式)时才置 input_updated → 重规划梯形轨迹。
    // 否则以 100Hz 连发同一目标会每帧重规划, Traj.t 永远卡在起点、走不到减速段 → 过冲/发散撞满程。
    if (motor_data.state.Control_Mode != CONTROL_MODE_POSITION_RAMP ||
        fabsf(new_pos - motor_data.Controller.input_position) > 1e-4f)
        motor_data.Controller.input_updated = true;
    motor_data.Controller.input_position = new_pos;
    motor_data.Controller.input_velocity = v / M_2PI; // turn/s (作梯度限速)
    motor_data.state.Control_Mode        = CONTROL_MODE_POSITION_RAMP;
}

static void DM_HandleVel(const uint8_t *rx)
{
    float v;
    memcpy(&v, rx, 4);
    motor_data.Controller.input_velocity = v / M_2PI; // turn/s
    motor_data.state.Control_Mode        = CONTROL_MODE_VELOCITY_RAMP;
}

static void DM_HandleSpecial(const uint8_t *rx)
{
    switch (rx[7])
    {
    case DM_CMD_ENABLE:
        motor_data.state.State_Mode = STATE_MODE_RUNNING;
        break;
    case DM_CMD_DISABLE:
        Foc_Pwm_LowSides();
        motor_data.state.State_Mode = STATE_MODE_GUARD;
        break;
    case DM_CMD_SET_ZERO:
        // 把当前物理位置设为新零点(软件偏移, 不动flash编码器零位); 同时归零位置设定→位置模式不跳变。
        motor_data.components.encoder->pos_zero_offset += motor_data.components.encoder->pos_estimate_;
        motor_data.Controller.input_position = 0.0f;
        motor_data.Controller.pos_setpoint   = 0.0f;
        break;
    case DM_CMD_CLEAR_ERR:
        // 清故障必须先清残留指令(2026-07-12 修): 清错→IDLE→自动重标定→自动短暂回RUNNING(deadman 200ms才拦),
        // 这窗口里若上次故障时的 MIT/速度目标还在, 控制环立刻按旧指令猛打 → 24V下冲流>8A → OC再锁死循环,
        // 只能重启(12V幅度不够8A, 从未暴露)。清空目标后自动RUNNING窗口零输出, 安全。
        motor_data.Controller.input_torque   = 0.0f;
        motor_data.Controller.input_velocity = 0.0f;
        motor_data.Controller.input_kp       = 0.0f;
        motor_data.Controller.input_kd       = 0.0f;
        motor_data.Controller.torque_setpoint= 0.0f;
        motor_data.Controller.vel_setpoint   = 0.0f;
        motor_data.Controller.input_position = motor_data.components.encoder->pos_estimate_; // 位置模式: 原地保持不跳变
        motor_data.Controller.pos_setpoint   = motor_data.components.encoder->pos_estimate_;
        motor_data.components.foc->iq_set    = 0.0f;
        motor_data.components.foc->id_set    = 0.0f;
        motor_data.state.Fault_State = FAULT_STATE_NORMAL;
        motor_data.state.State_Mode  = STATE_MODE_IDLE;
        break;
    case DM_CMD_COGGING_CAL: // 扩展(非DM标准): 启动闭环慢速扫齿槽标定(清零首遍)
        Cogging_CalStart(&motor_data, true);
        break;
    case DM_CMD_SING: // 扩展(非DM标准): 让电机唱歌
        g_sing_on = 1;
        break;
    default:
        break;
    }
}

/* ---------------- RX 总回调(按 rx_id 分发) ---------------- */
static void DM_RxCallback(FDCANInstance *ins)
{
    const uint8_t *rx = ins->rx_buff;

    g_can_last_rx_ms = DWT->CYCCNT; // 喂通信看门狗(存DWT周期数; deadman 用DWT计时, 避开重载ISR饿死HAL时基)

    switch (ins->rx_id)
    {
    case DM_ID_MIT:
        // ID=电机ID：先判特殊命令(前7字节0xFF)，否则按 MIT 解析
        if (rx[0] == 0xFF && rx[1] == 0xFF && rx[2] == 0xFF && rx[3] == 0xFF &&
            rx[4] == 0xFF && rx[5] == 0xFF && rx[6] == 0xFF)
            DM_HandleSpecial(rx);
        else
            DM_HandleMIT(rx);
        break;
    case DM_ID_POSVEL:
        DM_HandlePosVel(rx);
        break;
    case DM_ID_VEL:
        DM_HandleVel(rx);
        break;
    default:
        return;
    }

    DM_SendFeedback(); // 每收到一帧控制/命令即回一帧反馈
}

/* ---------------- 初始化：注册 3 个 RX ID ---------------- */
void DM_CanInit(void)
{
    FDCAN_Init_Config_s cfg_mit = {
        .fdcan_handle = &hfdcan1,
        .tx_id = DM_MASTER_ID, // 反馈发往主控
        .rx_id = DM_ID_MIT,
        .fdcan_module_callback = DM_RxCallback,
        .id = NULL};
    dm_mit = FDCANRegister(&cfg_mit);

    FDCAN_Init_Config_s cfg_posvel = {
        .fdcan_handle = &hfdcan1,
        .tx_id = DM_MASTER_ID,
        .rx_id = DM_ID_POSVEL,
        .fdcan_module_callback = DM_RxCallback,
        .id = NULL};
    dm_posvel = FDCANRegister(&cfg_posvel);

    FDCAN_Init_Config_s cfg_vel = {
        .fdcan_handle = &hfdcan1,
        .tx_id = DM_MASTER_ID,
        .rx_id = DM_ID_VEL,
        .fdcan_module_callback = DM_RxCallback,
        .id = NULL};
    dm_vel = FDCANRegister(&cfg_vel);
}

/* ---------------- #2 FDCAN 健康检查 / bus-off 自恢复 ----------------
 * 周期调用(RobotCMDTask, 已内部节流)。bus-off 时硬件自动置 CCCR.INIT 停机且不自恢复;
 * 此处检测到 bus-off 或意外 INIT(运行中本应为 0) 就清 INIT, 触发硬件总线恢复(等 128×11 隐性位)。
 * 正常运行时此函数几乎零开销(只读一次 PSR)。 */
void DM_CanHealthCheck(void)
{
    static uint32_t last_ms = 0;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_ms) < 20u) // 节流: 20ms 一次足够(恢复本身需若干 ms)
        return;
    last_ms = now;

    FDCAN_ProtocolStatusTypeDef psr;
    HAL_FDCAN_GetProtocolStatus(&hfdcan1, &psr);
    // bus-off, 或运行中 INIT 被意外置位(停机) → 清 INIT 恢复
    if (psr.BusOff || (hfdcan1.Instance->CCCR & FDCAN_CCCR_INIT))
    {
        CLEAR_BIT(hfdcan1.Instance->CCCR, FDCAN_CCCR_INIT);
        g_can_recover_cnt++;
    }
}
