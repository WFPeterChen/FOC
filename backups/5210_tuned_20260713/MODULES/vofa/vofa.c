#include "vofa.h"
#include "usbd_cdc_if.h"
#include "motor_task.h"
#include "robot_def.h"
#include "BLDCMotor.h"
#include <math.h>

#define MAX_TXBUFFER_SIZE 512        // 定义发送缓冲区大小
#define MAX_RXBUFFER_SIZE 512        // 定义接收缓冲区大小
uint8_t send_buf[MAX_TXBUFFER_SIZE]; // 发送缓冲区
uint8_t receive_buf[MAX_RXBUFFER_SIZE];
uint16_t cnt = 0; // 当前缓冲区计数

// —— 遥测开关/降速(上位机可在线控制, 省带宽) ——
volatile uint8_t  g_vofa_on  = 1; // 1=发遥测, 0=停
volatile uint16_t g_vofa_div = 1; // 每N次发1帧(1=1kHz全速, 10=100Hz)
// 命令数值消毒: 拒绝 NaN/Inf(atof 可能返回), 再钳到 [lo,hi]
static inline float vofa_sanitize(float x, float lo, float hi)
{
    if (!isfinite(x)) return 0.0f;
    return x < lo ? lo : (x > hi ? hi : x);
}

/***********************************************************************
 * @brief:      vofa_start(void)
 * @param:      void
 * @retval:     void
 * @details:    发送数据给上位机
 ***********************************************************************/
void vofa_start(void)
{
    Vofa_Packet(); // 生成并发送数据包
}

/***********************************************************************
 * @brief:      vofa_transmit(const uint8_t* buf, uint16_t len)
 * @param:      buf: 数据缓冲区指针
 * @param:      len: 数据长度
 * @retval:     void
 * @details:    发送数据到上位机，使用 USART 或 USB
 ***********************************************************************/
#define USART_OR_CDC 1 // 0: 使用 USART 发送，1: 使用 USB 发送
void vofa_transmit(const uint8_t *buf, uint16_t len)
{
    if (len > 0 && len <= MAX_TXBUFFER_SIZE) // 检查长度
    {
#if USART_OR_CDC == 0
        memcpy(send_buf, buf, len);
        HAL_UART_Transmit_DMA(&huart3, send_buf, len); // 使用 DMA 发送数据
#elif USART_OR_CDC == 1
        CDC_Transmit_FS((uint8_t *)buf, len);
#endif
    }
}

/***********************************************************************
 * @brief:      vofa_send_data(uint8_t num, float data)
 * @param[in]:   num: 数据编号
 * @param[in]:   data: 浮点数据
 * @retval:     void
 * @details:    将浮点数据拆分成单字节并存储到发送缓冲区
 ***********************************************************************/
void vofa_send_data(uint8_t num, float data)
{
    if (cnt + 4 < MAX_TXBUFFER_SIZE)
    {                                  // 检查缓冲区是否足够
        send_buf[cnt++] = byte0(data); // 拆分浮点数为字节
        send_buf[cnt++] = byte1(data);
        send_buf[cnt++] = byte2(data);
        send_buf[cnt++] = byte3(data);
    }
}

/***********************************************************************
 * @brief:      vofa_sendframetail(void)
 * @param:      NULL
 * @retval:     void
 * @details:    给数据包发送帧尾并发送整个数据包
 ***********************************************************************/
void vofa_sendframetail(void)
{
    if (cnt + 4 < MAX_TXBUFFER_SIZE)
    {                           // 检查缓冲区是否足够
        send_buf[cnt++] = 0x00; // 添加帧尾
        send_buf[cnt++] = 0x00;
        send_buf[cnt++] = 0x80;
        send_buf[cnt++] = 0x7f;

        vofa_transmit(send_buf, cnt); // 发送整个数据包
        cnt = 0;                      // 重置计数器
    }
}

/***********************************************************************
 * @brief:      Vofa_Packet(void)
 * @param:      NULL
 * @retval:     void
 * @details:    生成并发送数据包
 ***********************************************************************/
void Vofa_Packet(void)
{
    if (!g_vofa_on) return;                 // 遥测关
    static uint16_t _dc = 0;
    if (g_vofa_div > 1 && (++_dc % g_vofa_div) != 0) return; // 降速抽帧
    vofa_send_data(0, motor_data.components.foc->i_a);
    vofa_send_data(1, motor_data.components.foc->i_b);
    vofa_send_data(2, motor_data.components.foc->i_c);
    vofa_send_data(3, motor_data.components.foc->i_q);
    vofa_send_data(4, motor_data.components.foc->i_d);
    vofa_send_data(5, motor_data.components.foc->iq_set);
    vofa_send_data(6, motor_data.components.foc->id_set);
    vofa_send_data(7, motor_data.components.encoder->vel_estimate_);
    vofa_send_data(8, motor_data.components.encoder->pos_estimate_);
    vofa_send_data(9, motor_data.components.encoder->phase_);
    vofa_send_data(10, motor_data.components.current->Temp_Result);
    vofa_send_data(11, motor_data.components.foc->vbus);
    vofa_sendframetail(); // 发送帧尾并传输数据包
}

/**USB信息处理**/
static float vofa_cmd_parse(uint8_t *cmdBuf, const char *arg)
{
    return atof((char *)cmdBuf + strlen(arg));
}

/**接收中断服务函数**/
void vofa_Receive(uint8_t *buf, uint16_t len)
{
    if (len >= MAX_RXBUFFER_SIZE) len = MAX_RXBUFFER_SIZE - 1; // 防越界
    memcpy((char *)receive_buf, buf, len);
    receive_buf[len] = '\0'; // 强制终止: 否则 strstr/atof 会越读残留字节

    char *recvStr = (char *)receive_buf;
    const float ilim = MOTOR_CURRENT_LIMIT;
    const float tlim = MOTOR_CURRENT_LIMIT * motor_data.Controller.torque_const; // 力矩上限≈Ilim·Kt

    if (strstr(recvStr, "set_Iq="))
        motor_data.components.foc->iq_set = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_Iq="), -ilim, ilim);

    if (strstr(recvStr, "set_Id="))
        motor_data.components.foc->id_set = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_Id="), -ilim, ilim);

    if (strstr(recvStr, "set_torque="))
        motor_data.Controller.input_torque = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_torque="), -tlim, tlim);

    if (strstr(recvStr, "set_vel="))
        motor_data.Controller.input_velocity = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_vel="), -SPEED_MAX_LIMIT, SPEED_MAX_LIMIT);

    if (strstr(recvStr, "set_pos="))
    {
        motor_data.Controller.input_position = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_pos="), -100000.0f, 100000.0f);
        motor_data.Controller.input_updated = true;
    }

    if (strstr(recvStr, "set_current_ctrl_bw="))
        motor_data.Controller.current_ctrl_bandwidth = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_current_ctrl_bw="), 0.0f, 10000.0f);

    if (strstr(recvStr, "set_vel_kp="))
        motor_data.VelPID.Kp = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_vel_kp="), 0.0f, 100000.0f);

    if (strstr(recvStr, "set_vel_ki="))
        motor_data.VelPID.Ki = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_vel_ki="), 0.0f, 100000.0f);

    if (strstr(recvStr, "set_pos_kp="))
        motor_data.PosPID.Kp = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_pos_kp="), 0.0f, 100000.0f);

    if (strstr(recvStr, "set_dtcomp_on=")) // 死区补偿主开关 0/1 (在线A/B, 2026-07-11)
        g_dt_comp_master = (vofa_cmd_parse(receive_buf, "set_dtcomp_on=") != 0.0f) ? 1 : 0;

    if (strstr(recvStr, "set_dtcomp_duty=")) // 每相补偿量[占空比], 默认73/8500=0.0086; 负值=反号
        g_dt_comp_duty = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_dtcomp_duty="), -0.05f, 0.05f);

    if (strstr(recvStr, "set_dtcomp_ith=")) // 过零平滑带宽[A] (0=硬sign)
        g_dt_comp_ithresh = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_dtcomp_ith="), 0.0f, 2.0f);

    if (strstr(recvStr, "set_dtcomp_vgate=")) // 速度门控阈值[rad/s] (v3; 0=不门控常开)
        g_dt_comp_velgate = vofa_sanitize(vofa_cmd_parse(receive_buf, "set_dtcomp_vgate="), 0.0f, 50.0f);

    if (strstr(recvStr, "set_telem_div=")) // 遥测降速: 每N帧发1(1..1000)
    {
        int d = (int)vofa_cmd_parse(receive_buf, "set_telem_div=");
        if (d >= 1 && d <= 1000) g_vofa_div = (uint16_t)d;
    }
    else if (strstr(recvStr, "set_telem=")) // 遥测开关 0/1
        g_vofa_on = (vofa_cmd_parse(receive_buf, "set_telem=") != 0.0f) ? 1 : 0;

    if (strstr(recvStr, "set_ctrl_mode="))
    {
        int ctrlModeInt = (int)vofa_cmd_parse(receive_buf, "set_ctrl_mode=");
        if (ctrlModeInt >= CONTROL_MODE_OPEN && ctrlModeInt <= CONTROL_MODE_POSITION_RAMP)
            motor_data.state.Control_Mode = (CONTROL_MODE)ctrlModeInt;
    }

    if (strstr(recvStr, "calib="))
    {
        float calib_enable = vofa_cmd_parse(receive_buf, "calib=");
        if (calib_enable == 1)
        {
            PID_clear(&motor_data.IqPID);
            PID_clear(&motor_data.IdPID);
            PID_clear(&motor_data.VelPID);
            PID_clear(&motor_data.PosPID);
            FOC_reset(motor_data.components.foc);
            Foc_Pwm_LowSides();
            motor_data.state.State_Mode = STATE_MODE_DETECTING;
            motor_data.state.Sub_State = CURRENT_CALIBRATING;
            Init_Motor_Calib(&motor_data);
        }
    }

    // if (strstr(recvStr, "motor_enable="))
    // {
    //     float motor_enable = vofa_cmd_parse(receive_buf, "motor_enable=");
    // }
}
