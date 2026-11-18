#include "FOCMotor.h"
#include "bsp_dwt.h"
#include "led.h"
#include "trapTraj.h"
#include "motor_calib.h" // Phase B: flash 持久化标定
#include "cogging.h"     // item2: 齿槽补偿
#include "dm_can.h"      // g_can_last_rx_ms: 通信看门狗

#define CMD_TIMEOUT_MS 200u // CAN 指令掉线超过此时长 → 进 GUARD 防失控(GUI 持续查询@30Hz 会自动喂狗)

// 高速速度记录(验证齿槽前馈用): g_vlog_idx=0 启动记录, 满512停; 每10个控制周期记一次(~2kHz)
float             g_vlog[512];
volatile uint16_t g_vlog_idx = 512;

// SWD调试钩子(绕开不稳定的CAN, 直接 mww 写这些全局即可):
//   g_cog_cal_trigger=1 → 启动闭环逐点齿槽标定(完成自动存flash)
//   g_dbg_vel_on=1 + g_dbg_vel=转速(turn/s) → 强制速度环(纹波验证); 置0并写GUARD停
volatile uint8_t  g_cog_cal_trigger = 0;
volatile uint8_t  g_dbg_vel_on      = 0;
volatile float    g_dbg_vel         = 0.0f;
// MIT/力矩阻抗调试钩子(SWD): on=1 → 强制 MIT 模式 tau=kd*(vdes-v)+tff (kp=0, 无积分无硬环, 测齿槽FF)
volatile uint8_t  g_dbg_mit_on = 0;
volatile float    g_dbg_mit_kd = 0.0f; // 阻尼 [Nm/(turn/s)]
volatile float    g_dbg_mit_v  = 0.0f; // 目标转速 [turn/s]
volatile float    g_dbg_mit_t  = 0.0f; // 前馈力矩 [N·m]
volatile uint8_t  g_dbg_save_flash = 0; // SWD写1 → 把当前RAM标定(参数+编码器+cogging)写回flash(关PWM后)
volatile uint8_t  g_sing_on = 0;        // =1(DM命令0xF9或SWD) → 从头播放旋律让电机唱歌; 播完自动清0
// #5 再试: 统一电流环 A/B 在线开关(SWD)。0=A(默认, 裸PI+前馈+commonInverseFOC); 1=B(标定同款 FOC_current: 相位超前+积分泄放抗饱和+死区补偿+同款前馈)
volatile uint8_t  g_dbg_unified_iloop = 0;
// B路相位超前的电角速度处理旋钮(验证"相位超前×齿槽噪声"假说): phase_vel = g_padv_k * LP(we_raw, g_padv_filt)
volatile float    g_padv_filt = 0.0f;   // 相位超前速度一阶低通系数(0=用未滤波we_raw即原行为; 0.01~0.05=重滤波抑噪)
volatile float    g_padv_k    = 1.0f;   // 相位超前缩放(1=正常; 0=完全关闭相位超前, 看B是否高速追平A)
float             g_padv_we_f = 0.0f;   // 低通状态
// 快速硬过流保护(20kHz ISR 判, 比5ms MotorGuardTask快100×): 相电流峰值超阈立即封波→GUARD。
// 高阈值(默认2×电流限)=只抓真故障不误触发正常电流; SWD可调, 0=关。上6S把电流限提到10A时同步上调此值。
volatile float    g_oc_trip_a = MOTOR_CURRENT_LIMIT * 2.0f;
// 摩擦前馈(库仑摩擦补偿, 提高低速/反向驱动性 → 更接近 τ=Kt·iq 理想, 利 sim2real): iq_set += g_friction_ff·tanh(vel/vth)
// 只力矩/MIT模式补(速度/位置环已自带积分抗摩擦); 默认0=关, SWD在线调, tanh平滑过零防抖。过大→过补偿自走, 从0.05保守起调。
volatile float    g_friction_ff  = 0.0f; // 库仑摩擦补偿电流幅值[A](0=关)
volatile float    g_friction_vth = 0.2f; // tanh 过零速度尺度[turn/s]
// (#5 调查的诊断脚手架 unified_iloop/padv/vlog_sel/iloop_cyc/spi_cyc/iloop_ff/ff_scale 已清理;
//  结论与 bEMF+ωL 前馈公式见 CURRENT_LOOP_INVESTIGATION.md, 上正经低齿槽 PMSM 时再按文档加回。)

// #4 回馈过压抑制阈值(V): 母线超 START 开始线性压制制动电流, 到 FULL 全切 → 改滑行降速防撞OV跳闸。
//   做成 RAM 变量便于按电池S数在线调 + SWD 测试(把 START 调到母线电压以下即可不真OV就验证)。
volatile float    g_vbus_regen_start = VBUS_REGEN_START; // 默认 OV跳闸-0.9V
volatile float    g_vbus_regen_full  = VBUS_REGEN_FULL;  // 默认 OV跳闸-0.2V
volatile uint32_t g_regen_foldback_cnt = 0;              // 触发抑制的累计次数(SWD验证用)

// 电流环前馈(bEMF + ωL 解耦, A路): 默认开, SWD可调/可关做A/B。
volatile uint8_t  g_ff_bemf     = 1;     // bEMF 前馈 v_q += ω_e·ψf
volatile uint8_t  g_ff_decouple = 1;     // ωL 解耦 v_q += ω_e·L·i_d; v_d -= ω_e·L·i_q
volatile float    g_ff_psi      = 0.0f;  // ψf[Wb]: 0=自动用 2·Kt/(3·pp); SWD写非0则覆盖
volatile float    g_ff_wfilt    = 0.0f;  // ω_e 一阶低通系数(0=不滤, 越大越平滑) 抑齿槽噪声注入
float             g_ff_we_f     = 0.0f;  // ω_e 低通状态

// 守护/掉线诊断(deadman 在 20kHz ISR 里判, 见 MotorControlTask): 1=deadman 2=UV 3=OV 4=OC 5=speed 6=temp
volatile uint8_t  g_guard_reason = 0;
volatile uint32_t g_guard_cnt = 0;       // 离开 RUNNING 次数
volatile uint32_t g_max_deadman_dt = 0;  // RUNNING 期间见过的最大 (tick-last_rx)
volatile uint32_t g_guard_dt = 0;        // 跳闸瞬间的 (tick-last_rx)
volatile float    g_guard_vbus = 0, g_guard_iq = 0, g_guard_vel = 0;
// 低速门控: 低速 bEMF 本就小(∝ω_e)、FF 无用且会把 ω_e 里的齿槽纹波放大注入 → 低于 vlo 关、vlo~vhi 线性渐入。
volatile float    g_ff_vlo      = 0.5f;  // [turn/s] 全关阈值 (≈3 rad/s)
volatile float    g_ff_vhi      = 1.5f;  // [turn/s] 全开阈值 (≈9 rad/s)

MOTOR_DATA motor_data           = {
    .components                 = {
        .foc                    = &foc_data,
        .encoder                = &encoder_data,
        .current                = &current_data,
    },
    .state = {
        .State_Mode             = STATE_MODE_IDLE,              // �������״̬
        .Control_Mode           = CONTROL_MODE_VELOCITY_RAMP,   // �������ģʽ
        .Sub_State              = SUB_STATE_IDLE,               // �����У׼״̬
        .Cs_State               = CS_STATE_IDLE,                // �����У׼״̬
        .Fault_State            = FAULT_STATE_NORMAL,           // �������״̬
    },
    .parameters = {
        .Rs                     = MOTOR_RS,     // �����
        .Ls                     = MOTOR_LS,     // ����
        .flux                   = MOTOR_FLUX, // ����
    },
    .Controller                 = {
        .inertia                = MOTOR_INERTIA,                // ת������ [A/(turn/s^2)]������Ϊ�������1תÿ��ļ��ٶ���תʱ��Ҫ�ṩ�����ڼ������ĵĵ�������Ҫ���ݵ�����������������������е��ԡ�
        .torque_ramp_rate       = MOTOR_CURRENT_RAMP_RATE,      // ������������[Nm/s]
        .vel_ramp_rate          = MOTOR_VEL_RAMP_RATE,          // ת�������ٶ� [(turn/s)/s]
        .traj_vel               = MOTOR_TRAJ_VEL,               // ���ι켣����ģʽ�����ת�� [turn/s]
        .traj_accel             = MOTOR_TRAJ_ACCEL,             // ���ι켣����ģʽ�¼��ٶ� [(turn/s)/s]
        .traj_decel             = MOTOR_TRAJ_DECEL,             // ���ι켣����ģʽ�¼��ٶ� [(turn/s)/s]
        .torque_const           = MOTOR_TORQUE_CONST,           // ���ת�س��� [Nm/A]
        .torque_limit           = MOTOR_TORQUE_LIMIT,           // ������ת�� [Nm]
        .vel_limit              = MOTOR_VEL_LIMIT,              // ת������ֵ [turn/s]
        .voltage_limit          = MOTOR_VOLTAGE_LIMIT,          // �����ѹ����ֵ [V]
        .current_limit          = MOTOR_CURRENT_LIMIT,          // �����������ֵ [A]
        .current_ctrl_p_gain    = MOTOR_CURRENT_CTRL_P_GAIN,    // ���������棬��У׼�������Զ�����ó���Ҳ���������� (Auto)
        .current_ctrl_i_gain    = MOTOR_CURRENT_CTRL_I_GAIN,    // �������������棬��У׼�������Զ�����ó���Ҳ���������� (Auto)
        .current_ctrl_bandwidth = MOTOR_CURRENT_CTRL_BANDWIDTH, // ����������[rad/s]����Χ 100~2000
        .input_current          = MOTOR_INPUT_CURRENT,          // Ŀ�����
        .input_torque           = MOTOR_INPUT_TORQUE,           // Ŀ������
        .input_velocity         = MOTOR_INPUT_VELOCITY,         // Ŀ��ת��
        .input_position         = MOTOR_INPUT_POSITION,         // Ŀ��λ��
        .input_updated          = true,                         // λ�û��趨ֵ�Ƿ��Ѹ���
    },
    .IqPID                      = {
        // ������IQ����
        .mode                   = PID_POSITION,
        .Kp                     = MOTOR_IQ_PID_KP,
        .Ki                     = MOTOR_IQ_PID_KI,
        .Kd                     = MOTOR_IQ_PID_KD,
        .max_out                = IQ_PID_MAX_OUT,   
        .max_iout               = IQ_PID_MAX_IOUT,
    },
    .IdPID                      = {
        // ������ID����
        .mode                   = PID_POSITION,
        .Kp                     = MOTOR_ID_PID_KP,
        .Ki                     = MOTOR_ID_PID_KI,
        .Kd                     = MOTOR_ID_PID_KD,
        .max_out                = ID_PID_MAX_OUT,
        .max_iout               = ID_PID_MAX_IOUT,
    },
    .VelPID                     = {
        // ת�ٻ�PID����
        .mode                   = PID_POSITION,
        .Kp                     = MOTOR_VEL_PID_KP,
        .Ki                     = MOTOR_VEL_PID_KI,
        .Kd                     = MOTOR_VEL_PID_KD,
        .max_out                = VEL_PID_MAX_OUT,
        .max_iout               = VEL_PID_MAX_IOUT,
    },
    .PosPID                     = {
        // λ�û�PID����
        .mode                   = PID_POSITION,
        .Kp                     = MOTOR_POS_PID_KP,
        .Ki                     = MOTOR_POS_PID_KI,
        .Kd                     = MOTOR_POS_PID_KD,
        .max_out                = POS_PID_MAX_OUT,
        .max_iout               = POS_PID_MAX_IOUT,
    },
};

static int *p_error_arr         = NULL;

/**
 * @brief ��ȡNTC�¶�����
 *
 * @param motor ָ�������ݽṹ��ָ��
 */
void TempResultTask(MOTOR_DATA *motor)
{
    GetTempNtc(adc2_median_filter(adc2_ch12), &motor->components.current->Temp_Result);
}

/**
 * @brief �޸�PID�޷�ֵ
 *
 * @param motor ָ�������ݽṹ��ָ��
 */
static void SetPIDLimit(MOTOR_DATA *motor,
                        float current_max_out,
                        float current_max_iout,
                        float vel_max_out,
                        float vel_max_iout,
                        float pos_limit)
{
    motor->IdPID.max_out   = CURRENT_PID_MAX_OUT;
    motor->IdPID.max_iout  = CURRENT_PID_MAX_OUT;
    motor->IqPID.max_out   = current_max_out;
    motor->IqPID.max_iout  = current_max_iout;
    motor->VelPID.max_out  = vel_max_out;
    motor->VelPID.max_iout = vel_max_iout;
    motor->PosPID.max_out  = pos_limit;
    motor->PosPID.max_iout = pos_limit;
}

/**
 * @brief �ٶȿ�����������
 *
 * �˺�������ִ�п����������񣬸���Ŀ���ٶȼ������ĵ�Ƕȣ������õ���ֵ��
 *
 * @param motor ָ�������ݽṹ��ָ��
 */
static void OpenControlMode(MOTOR_DATA *motor, float target_velocity)
{
    float Ts = 0.001f;
    if (Ts <= 0 || Ts > 0.5f)
        Ts = 1e-3f;

    motor->components.foc->theta = normalize_angle(motor->components.foc->theta + target_velocity * Ts); // �̶��Ƕȡ�����

    FOC_voltage(motor->components.foc, 0.0f, 0.5f, motor->components.foc->theta); // ��ѹ����
}

/**
 * @brief ��ȡ��������
 *
 * �˺����� ADC �ж�ȡ����������ֵ��������洢�� foc_data �ṹ���С�
 * �ú���ͨ���ڿ���ѭ���е��ã��Ի�ȡʵʱ�������ݡ�
 *
 * @param motor ָ�� foc_data �ṹ���ָ�룬�ýṹ��������״̬�Ϳ������ݡ�
 */
void GetMotorADC1PhaseCurrent(MOTOR_DATA *motor)
{
    // �������ƫ�á�ĸ�ߵ�ѹ����
    motor->components.foc->i_a  = ((float)motor->components.current->hadc->Instance->JDR3 - motor->components.current->Ia_offset) * FAC_CURRENT;
    motor->components.foc->i_b  = ((float)motor->components.current->hadc->Instance->JDR2 - motor->components.current->Ib_offset) * FAC_CURRENT;
    motor->components.foc->i_c  = ((float)motor->components.current->hadc->Instance->JDR1 - motor->components.current->Ic_offset) * FAC_CURRENT;
    motor->components.foc->vbus = ((float)motor->components.current->hadc->Instance->JDR4 * VOLTAGE_TO_ADC_FACTOR);
    // #3 母线电压实时补偿: SVPWM 用实测 vbus 归一(原来是编译期常数 INVBATVEL=1/BATVEL)。
    //    电池跌落/回馈/换电源时电流环增益不再漂; 启动 vbus 未稳时退回常数防除零。
    motor->components.foc->inv_vbus = (motor->components.foc->vbus > 4.0f)
                                          ? (1.0f / motor->components.foc->vbus)
                                          : INVBATVEL;
}

/**
 * @brief ���µ������������������
 *
 * �������������
 * P��Ls * �����ת��/60 * ��������* 2 * 3.14
 * I��Rs * �����ת��/60 * ��������* 2 * 3.14 * ��������Ƶ��
 *
 * @param bandwidth ��������
 */
#define CURRENT_AUTO_CALIBRATION 1 // �ֶ��������滹���Զ�����
static void FOC_update_current_gain(MOTOR_DATA *motor)
{
#if CURRENT_AUTO_CALIBRATION
    motor->Controller.current_ctrl_p_gain = motor->parameters.Ls * motor->Controller.vel_limit * motor->components.encoder->pole_pairs * M_2PI;
    motor->Controller.current_ctrl_i_gain = motor->parameters.Rs * motor->Controller.vel_limit * motor->components.encoder->pole_pairs * M_2PI * CURRENT_MEASURE_PERIOD;
#else
    motor->Controller.current_ctrl_p_gain = motor->parameters.Ls * motor->Controller.current_ctrl_bandwidth * 1.0f;
    motor->Controller.current_ctrl_i_gain = motor->parameters.Rs * motor->Controller.current_ctrl_bandwidth * CURRENT_MEASURE_PERIOD;
#endif
    motor->IdPID.Kp = motor->Controller.current_ctrl_p_gain;
    motor->IdPID.Ki = motor->Controller.current_ctrl_i_gain;
    motor->IqPID.Kp = motor->Controller.current_ctrl_p_gain;
    motor->IqPID.Ki = motor->Controller.current_ctrl_i_gain;
    // #5 统一电流环(B路 FOC_current)同款增益: 必须同步到 foc 结构, 否则 B 路 P/I=0 → 无电流调节(会误判B差)
    motor->components.foc->current_ctrl_p_gain = motor->Controller.current_ctrl_p_gain;
    motor->components.foc->current_ctrl_i_gain = motor->Controller.current_ctrl_i_gain; // 已含Ts(FOC_current内不再乘Ts)
}

void Init_Motor_No_Calib(MOTOR_DATA *motor)
{
    motor->state.Sub_State = SUB_STATE_IDLE;
    motor->state.Cs_State = CS_STATE_IDLE;
    motor->state.State_Mode = STATE_MODE_RUNNING;
    motor->components.encoder->calib_valid = true; // PRE_CAL=1也算已标定(用编译期常数), 否则齿槽标定的guard会挡
    MotorCalib_LoadCoggingOnly(); // 电机参数走编译期常数, 但齿槽图从flash加载 → FF开机即生效(page63有效时)
    Encoder_ResetPLL(); // flash加载换了 dir/offset/LUT, 下拍重新装载PLL避免cnt跳变触发超速
    FOC_update_current_gain(motor);
}

void Init_Motor_Calib(MOTOR_DATA *motor)
{
    if (p_error_arr == NULL)
    {
        p_error_arr = malloc(SAMPLES_PER_PPAIR * MOTOR_POLE_PAIRS_MAX * sizeof(int));
    }

    motor->components.encoder->calib_valid = false;
    Cogging_Reset(); // item2: 标定开始, 清零齿槽累加器
    motor->state.Sub_State                 = RSLS_CALIBRATING; // �ı���״̬
    motor->state.Cs_State                  = CS_MOTOR_R_START; // �ı���״̬
}

/**
 * @brief ��������
 *
 * @param motor ָ��������״̬�Ϳ������ݵĽṹ��ָ��
 */
static void CurrentCalibration(MOTOR_DATA *motor)
{
    static uint16_t loop_count = 0;
    loop_count++;

    // �ۼӵ���ƫ��ֵ
    motor->components.current->current_offset_sum_a += (float)(ADC1->JDR3);
    motor->components.current->current_offset_sum_b += (float)(ADC1->JDR2);
    motor->components.current->current_offset_sum_c += (float)(ADC1->JDR1);

    if (loop_count >= CURRENT_CALIBRATION_DURATION)
    {
        loop_count = 0;

#if PRE_CALIBRATED // �Ƿ���ҪУ׼
        Init_Motor_No_Calib(motor);
#else
        if (MotorCalib_LoadFromFlash(motor)) // flash 有有效标定 → 直接加载, 不重标
            Init_Motor_No_Calib(motor);
        else
            Init_Motor_Calib(motor); // 无 → 现场标定(完成后自动存flash)
#endif
        // У׼��ɣ����µ���ƫ��ֵ
        motor->components.current->Ia_offset = motor->components.current->current_offset_sum_a / (float)CURRENT_CALIBRATION_DURATION;
        motor->components.current->Ib_offset = motor->components.current->current_offset_sum_b / (float)CURRENT_CALIBRATION_DURATION;
        motor->components.current->Ic_offset = motor->components.current->current_offset_sum_c / (float)CURRENT_CALIBRATION_DURATION;

        // ����ƫ�����ۼ���
        motor->components.current->current_offset_sum_a = 0;
        motor->components.current->current_offset_sum_b = 0;
        motor->components.current->current_offset_sum_c = 0;
    }
}
/**
 * @brief У׼������衢���
 *
 * @param motor ָ��������״̬�Ϳ������ݵĽṹ��ָ��
 */
static void RSLSCalibration(MOTOR_DATA *motor)
{
    static uint32_t loop_count;

    // R
    static const float kI              = 2.0f;
    static const uint32_t num_R_cycles = CURRENT_MEASURE_HZ * 2;

    // L
    static float Ialphas[2];
    static float voltages[2];
    static const uint32_t num_L_cycles = CURRENT_MEASURE_HZ / 2;

    static const float calib_phase_vel = M_PI;

    static float phase_set;
    static float start_count;
    static int16_t sample_count;
    static float next_sample_time;

    float time                         = (float)loop_count * CURRENT_MEASURE_PERIOD;
    const float voltage                = CURRENT_MAX_CALIB * motor->parameters.Rs * 3.0f / 2.0f;

    switch (motor->state.Cs_State)
    {
    case CS_STATE_IDLE: // ����״̬
        break;

    case CS_MOTOR_R_START: // �趨��ղ���
        loop_count                     = 0;
        voltages[0]                    = 0.0f;
        motor->components.encoder->dir = CW;
        motor->state.Cs_State          = CS_MOTOR_R_LOOP;
        break;

    case CS_MOTOR_R_LOOP: // У׼����
        voltages[0] += kI * CURRENT_MEASURE_PERIOD * (CURRENT_MAX_CALIB - motor->components.foc->i_a);

        // Test voltage along phase A
        FOC_voltage(motor->components.foc, voltages[0], 0, 0);

        if (loop_count >= num_R_cycles)
        {
            Foc_Pwm_LowSides();
            motor->state.Cs_State = CS_MOTOR_R_END;
        }

        break;

    case CS_MOTOR_R_END: // У׼�������
        motor->parameters.Rs  = (voltages[0] / CURRENT_MAX_CALIB) * (2.0f / 3.0f);
        motor->state.Cs_State = CS_MOTOR_L_START;

        break;

    case CS_MOTOR_L_START: // �趨��ղ���
        loop_count  = 0;
        Ialphas[0]  = 0.0f;
        Ialphas[1]  = 0.0f;
        voltages[0] = -VOLTAGE_MAX_CALIB;
        voltages[1] = +VOLTAGE_MAX_CALIB;
        FOC_voltage(motor->components.foc, voltages[0], 0.0f, 0.0f);
        motor->state.Cs_State = CS_MOTOR_L_LOOP;
        break;

    case CS_MOTOR_L_LOOP: // У׼���
    {
        int i       = loop_count & 1;
        Ialphas[i] += motor->components.foc->i_a;

        // Test voltage along phase A
        FOC_voltage(motor->components.foc, voltages[i], 0.0f, 0.0f);

        if (loop_count >= (num_L_cycles << 1))
        {
            Foc_Pwm_LowSides();
            motor->state.Cs_State = CS_MOTOR_L_END;
        }
    }
    break;

    case CS_MOTOR_L_END: // У׼������
    {
        float dI_by_dt        = (Ialphas[1] - Ialphas[0]) / (float)(CURRENT_MEASURE_PERIOD * num_L_cycles);
        float L               = VOLTAGE_MAX_CALIB / dI_by_dt;
        motor->parameters.Ls  = fabsf(L * 2.0f / 3.0f); // FIXME ����ָ��������
        FOC_update_current_gain(motor);

        phase_set             = 0;
        loop_count            = 0;
        motor->state.Cs_State = CS_DIR_PP_START;
    }
    break;

    case CS_DIR_PP_START: // �趨��ղ���
        FOC_voltage(motor->components.foc, (voltage * time / 2.0f), 0.0f, phase_set);
        if (time >= 2.0f)
        {
            start_count           = (float)motor->components.encoder->shadow_count_;
            motor->state.Cs_State = CS_DIR_PP_LOOP;
            break;
        }
        break;

    case CS_DIR_PP_LOOP: // У׼����͵��������

        phase_set += calib_phase_vel * CURRENT_MEASURE_PERIOD;
        FOC_voltage(motor->components.foc, voltage, 0.0f, phase_set);
        if (phase_set >= 4.0f * M_2PI)
        {
            motor->state.Cs_State = CS_DIR_PP_END;
        }
        break;

    case CS_DIR_PP_END: // У׼����͵�����������
    {
        int32_t diff = motor->components.encoder->shadow_count_ - start_count;

        // Check direction
        if (diff > 0)
        {
            motor->components.encoder->dir = CW;
        }
        else
        {
            motor->components.encoder->dir = CCW;
        }

        // Motor pole pairs
        motor->components.encoder->pole_pairs = round(4.0f / ABS(diff / ENCODER_CPR_F));

        if (motor->components.encoder->pole_pairs > MOTOR_POLE_PAIRS_MAX)
        {
            break;
        }

        motor->state.Cs_State = CS_ENCODER_START;
    }
    break;

    case CS_ENCODER_START: // �趨��ղ���
        phase_set             = 0;
        loop_count            = 0;
        sample_count          = 0;
        next_sample_time      = 0;
        motor->state.Cs_State = CS_ENCODER_CW_LOOP;
        break;

    case CS_ENCODER_CW_LOOP: // У׼����������ת��
        // Test voltage along phase A
        //        FOC_voltage(motor->components.foc, 0, voltage, M_3PI_2);

        //        if (loop_count >= num_R_cycles/1.5)
        //        {
        //			motor->components.encoder->encoder_offset = motor->components.encoder->cnt;
        //			FOC_voltage(motor->components.foc, 0, 0, M_3PI_2);
        //
        //			free(p_error_arr);
        //            p_error_arr = NULL;
        //            motor->components.encoder->calib_valid = true;
        //            motor->state.Cs_State = CS_STATE_IDLE;
        //            motor->state.Sub_State = SUB_STATE_IDLE;      // �ı���״̬
        //            motor->state.State_Mode = STATE_MODE_RUNNING; // �ı���״̬
        //        }

        if (sample_count < (motor->components.encoder->pole_pairs * SAMPLES_PER_PPAIR))
        {
            if (time > next_sample_time)
            {
                next_sample_time         += M_2PI / ((float)SAMPLES_PER_PPAIR * calib_phase_vel);

                int count_ref             = (phase_set * ENCODER_CPR_F) / (M_2PI * (float)motor->components.encoder->pole_pairs);
                int error                 = motor->components.encoder->cnt - count_ref;
                error                    += ENCODER_CPR * (error < 0);
                p_error_arr[sample_count] = error;

                sample_count++;
            }

            phase_set += calib_phase_vel * CURRENT_MEASURE_PERIOD;
        }
        else
        {
            phase_set            -= calib_phase_vel * CURRENT_MEASURE_PERIOD;
            loop_count            = 0;
            sample_count--;
            next_sample_time      = 0;
            motor->state.Cs_State = CS_ENCODER_CCW_LOOP;
            break;
        }
        Cogging_RecordCW(motor); // item2: 寄生记录CW的iq vs机械角(开环匀速, 干净)
        FOC_voltage(motor->components.foc, voltage, 0, phase_set);
        break;

    case CS_ENCODER_CCW_LOOP: // У׼����������ת��
        if (sample_count >= 0)
        {
            if (time > next_sample_time)
            {
                next_sample_time         += M_2PI / ((float)SAMPLES_PER_PPAIR * calib_phase_vel);

                int count_ref             = (phase_set * ENCODER_CPR_F) / (M_2PI * (float)motor->components.encoder->pole_pairs);
                int error                 = motor->components.encoder->cnt - count_ref;
                error                    += ENCODER_CPR * (error < 0);
                p_error_arr[sample_count] = (p_error_arr[sample_count] + error) / 2;

                sample_count--;
            }

            phase_set -= calib_phase_vel * CURRENT_MEASURE_PERIOD;
        }
        else
        {
            Foc_Pwm_LowSides();
            motor->state.Cs_State = CS_ENCODER_END;
            break;
        }
        Cogging_RecordCCW(motor); // item2: 寄生记录CCW的iq vs机械角
        FOC_voltage(motor->components.foc, voltage, 0, phase_set);
        break;

    case CS_ENCODER_END: // У׼���������
    {
        // Calculate average offset
        int64_t moving_avg = 0;
        for (int i = 0; i < (motor->components.encoder->pole_pairs * SAMPLES_PER_PPAIR); i++)
        {
            moving_avg += p_error_arr[i];
        }
        motor->components.encoder->encoder_offset = moving_avg / (motor->components.encoder->pole_pairs * SAMPLES_PER_PPAIR);

        // FIR and map measurements to lut
        int window     = SAMPLES_PER_PPAIR;
        int lut_offset = p_error_arr[0] * OFFSET_LUT_NUM / ENCODER_CPR;
        for (int i = 0; i < OFFSET_LUT_NUM; i++)
        {
            moving_avg = 0;
            for (int j = (-window) / 2; j < (window) / 2; j++)
            {
                int index   = i * motor->components.encoder->pole_pairs * SAMPLES_PER_PPAIR / OFFSET_LUT_NUM + j;
                if (index < 0)
                {
                    index  += (SAMPLES_PER_PPAIR * motor->components.encoder->pole_pairs);
                }
                else if (index > (SAMPLES_PER_PPAIR * motor->components.encoder->pole_pairs - 1))
                {
                    index  -= (SAMPLES_PER_PPAIR * motor->components.encoder->pole_pairs);
                }
                moving_avg += p_error_arr[index];
            }
            moving_avg      = moving_avg / window;
            int lut_index   = lut_offset + i;
            if (lut_index > (OFFSET_LUT_NUM - 1))
            {
                lut_index  -= OFFSET_LUT_NUM;
            }
            motor->components.encoder->offset_lut[lut_index] = moving_avg - motor->components.encoder->encoder_offset;
        }

        loop_count            = 0;
        sample_count          = 0;
        next_sample_time      = 0;
        Cogging_Compute(); // item2: 标定结束, 就地算出齿槽前馈map + 摩擦
        motor->state.Cs_State = CS_REPORT_OFFSET_LUT;
    }
    break;

    case CS_REPORT_OFFSET_LUT: // ���У׼���
        if (sample_count < OFFSET_LUT_NUM)
        {
            if (time > next_sample_time)
            {
                next_sample_time += 0.001f;
                sample_count++;
            }
        }
        else
        {
            free(p_error_arr);
            p_error_arr                            = NULL;
            motor->state.Cs_State                  = CS_STATE_IDLE;
            motor->state.Sub_State                 = SUB_STATE_IDLE;    
            motor->state.State_Mode                = STATE_MODE_RUNNING;
            motor->components.encoder->calib_valid = true;
            Encoder_ResetPLL(); // 现场标定刚定下 dir/offset/LUT, 下拍重装PLL避免cnt跳变触发超速
            FOC_update_current_gain(motor); // 修复: 行323调此函数时pole_pairs尚未检测(=0)导致电流环增益算成0; 标定全完成、pole_pairs已知后重算一次
#if !PRE_CALIBRATED
            Foc_Pwm_LowSides();            // Phase B: flash擦写停CPU~40ms, 先关PWM(电机刚结束慢转、静止, 安全)
            MotorCalib_SaveToFlash(motor); // 存标定到flash, 下次开机直接加载、不重标
#endif
            PID_clear(&motor->IqPID);
            PID_clear(&motor->IdPID);
            PID_clear(&motor->VelPID);
            PID_clear(&motor->PosPID);
        }
        break;

    default:
        break;
    }
    loop_count++;
}

static void MotorInitializeTask(MOTOR_DATA *motor)
{
    g_dt_comp_on = 0; // 标定期间关死区补偿(否则会偏置 R/L 辨识与电流零偏)
    switch (motor->state.Sub_State)
    {
    case SUB_STATE_IDLE:      // ����״̬

        break;
    case CURRENT_CALIBRATING: // ��������
        CurrentCalibration(motor);
        break;
    case RSLS_CALIBRATING:    // �������н���
        RSLSCalibration(motor);
        break;
    case FLUX_CALIBRATING:    // ��������
        // ������
        break;

    default:
        break;
    }
}

/**
 * @brief FOC��������
 *
 * �˺�������ִ��FOC��Field Oriented Control���������񡣸��ݿ���ģʽ��ѡ����Ӧ�Ŀ�������
 *
 * @param motor ָ�������ݽṹ��ָ��
 *
 * ���رջ��ĵ��ڣ�
 * 1.����Q���趨ֵΪ0����D���趨ֵΪһ��ֵ��Ȼ������pid֮��鿴id���Σ�����id����һ����Ծ��Ӧ���۲��Ƿ�Ϊ���ǵ��趨ֵ
 * 2.�ٽ�Q������Ϊ����ֵ��D������Ϊ0��������ס������Ӧ���
 *
 * �ٶȱջ��ĵ��ڣ�
 * 1.����kp��ki��Ҫ���̫������Ȼ����ĵ�����Ծ�ͻ᲻�ȶ�����������ض���
 * 2.kp����Ҫʱ��������ֶ����������ٶ���ӦҪ������kiҪ��kpС��������
 *
 * λ�ñջ��ĵ��ڣ�
 * 1.����Ӧ�ٶ��Լ��Ƿ��й���
 */
#define TORQUE_ADJUST      0 // ���رջ�D�����
#define TORQUE_AND_CURRENT 0 // ���رջ�������ջ�
static void MotorControlTask(MOTOR_DATA *motor)
{
    static uint8_t loop_count;
    static float vel_set;

    // 死区补偿 v3 = 参考符号 + 速度门控(2026-07-11):
    // 保持态(vel≈0)真实死区畸变跟随20kHz电流噪声瞬时符号=前馈追不上的宽带噪声, 任何补偿只添增益
    // (实测: v1实测符号+2×/193Hz峰, v2参考符号+37%/210Hz峰) → 静止彻底关。
    // 转动时过零真实发生, 参考符号方向正确 → 满额补。迟滞门控(0.8/1.2 rad/s)防边界抖振。
    {
        float _av = fabsf(motor->components.encoder->vel_estimate_);
        static uint8_t _dt_gate = 0;
        if (_dt_gate) { if (_av < 0.8f * g_dt_comp_velgate) _dt_gate = 0; }
        else          { if (_av > 1.2f * g_dt_comp_velgate) _dt_gate = 1; }
        g_dt_comp_on = (g_dt_comp_master && _dt_gate) ? 1 : 0;
    }

    // 通信掉线保护(deadman)——放在 20kHz 控制 ISR 里(可靠), 用 DWT 周期计数(CPU时钟)计时:
    // 45us 的重载 FOC ISR 会饿死 HAL 时基 TIM(uwTick 在 RUNNING 下只跑~13%速率)→ 用 HAL_GetTick 的
    // deadman 超时会被拉长到~1.5s 且不稳。DWT->CYCCNT 走 CPU 时钟, 从不被饿死, 计时准。
    {
        uint32_t _dtc = DWT->CYCCNT - g_can_last_rx_ms;      // 周期差(dt<25s 内无符号回绕安全)
        uint32_t _dt  = _dtc / (SystemCoreClock / 1000u);    // → ms
        if (_dt > g_max_deadman_dt) g_max_deadman_dt = _dt;
        // 齿槽标定进行中(g_cogcal_phase!=0)时电机由 Cogging_CalUpdate 自主慢速逐点驱动, 不依赖CAN指令,
        // 故跳过通信掉线deadman(否则200ms无CAN即误停中断~20min标定); OC/OV/超速等硬件保护仍在。
        if (_dt > CMD_TIMEOUT_MS && g_cogcal_phase == 0)
        {
            Foc_Pwm_LowSides();
            motor->state.State_Mode = STATE_MODE_GUARD;
            g_guard_reason = 1; g_guard_dt = _dt; g_guard_cnt++;
            g_guard_vel = motor->components.encoder->vel_estimate_;
            return; // 本拍不再跑控制
        }
    }

    // 快速硬过流保护: 相电流峰值超 g_oc_trip_a 立即封波(20kHz, 远快于5ms守护任务; 无硬件OCP的次优兜底)
    if (g_oc_trip_a > 0.0f)
    {
        float ia = fabsf(motor->components.foc->i_a), ib = fabsf(motor->components.foc->i_b), ic = fabsf(motor->components.foc->i_c);
        float ipk = ia > ib ? (ia > ic ? ia : ic) : (ib > ic ? ib : ic);
        if (ipk > g_oc_trip_a)
        {
            Foc_Pwm_LowSides();
            motor->state.Fault_State = FAULT_STATE_OVER_CURRENT;
            motor->state.State_Mode  = STATE_MODE_GUARD;
            g_guard_reason = 7; g_guard_cnt++; g_guard_iq = ipk;
            return;
        }
    }

    { // 高速记录转速(环形缓冲, 始终记最近512点@~2kHz; 读取时一次halt即可, 不打断电机)
        static uint8_t vlog_d = 0;
        if (++vlog_d >= 10) { vlog_d = 0; g_vlog[g_vlog_idx & 511] = motor->components.encoder->vel_estimate_; g_vlog_idx++; }
    }

    // 闭环逐点齿槽标定(CAN 0xFA触发): 接管input_position, 走下面的位置环级联逐点取保持电流
    if (g_cogcal_phase != 0) // 0=COGCAL_IDLE
    {
        if (!Cogging_CalUpdate(motor)) return; // 本拍刚收尾(已关PWM/写flash/置GUARD), 不再驱动
        // 进行中: 落到 CONTROL_MODE_POSITION 分支, 用 Cogging_CalUpdate 设的 input_position
    }

    switch (motor->state.Control_Mode)
    {
    case CONTROL_MODE_OPEN: // �ٶȵ�ѹ����
    {
        OpenControlMode(motor, 5);
    }
    break;

    case CONTROL_MODE_TORQUE: // ���رջ�������ʱ����С��������ס��תʱ�����ﵽĿ�������
    {
        SetPIDLimit(motor, 2.0f, CURRENT_PID_MAX_OUT, 0.0f, 0.0f, 0.0f);

#if TORQUE_AND_CURRENT
#if TORQUE_ADJUST
        motor->components.foc->iq_set  = 0;
        motor->components.foc->id_set  = 0.5f;
#else
		motor->components.foc->theta   = motor->components.encoder->phase_;
        motor->Controller.input_current= CLAMP(motor->Controller.input_current, -motor->Controller.current_limit, motor->Controller.current_limit);
        float max_step_size            = fabsf(CURRENT_MEASURE_PERIOD * motor->Controller.torque_ramp_rate);
        float full_step                = motor->Controller.input_current - motor->components.foc->iq_set;
        float step                     = CLAMP(full_step, -max_step_size, max_step_size);
        motor->components.foc->iq_set += step;
        motor->components.foc->id_set  = 0.0f;
#endif
#else
        motor->components.foc->theta   = motor->components.encoder->phase_;
        motor->Controller.input_torque = CLAMP(motor->Controller.input_torque, -motor->Controller.torque_limit, motor->Controller.torque_limit);
        motor->Controller.input_current= CLAMP(motor->Controller.input_torque / motor->Controller.torque_const, -motor->Controller.current_limit, +motor->Controller.current_limit);
        float max_step_size            = fabsf(CURRENT_MEASURE_PERIOD * motor->Controller.torque_ramp_rate);
        float full_step                = motor->Controller.input_current - motor->components.foc->iq_set;
        float step                     = CLAMP(full_step, -max_step_size, max_step_size);
        motor->components.foc->iq_set += step;
        motor->components.foc->id_set  = 0.0f;
#endif
    }
    break;

    case CONTROL_MODE_VELOCITY: // �ٶ�->���رջ�
    {
        SetPIDLimit(motor, CURRENT_PID_MAX_OUT, CURRENT_PID_MAX_OUT, VEL_PID_MAX_OUT, VEL_PID_MAX_OUT, 0.0f);
        motor->components.foc->theta = motor->components.encoder->phase_;
    }
    break;

    case CONTROL_MODE_POSITION: // λ��->�ٶ�->���رջ�
    {
        SetPIDLimit(motor, CURRENT_PID_MAX_OUT, CURRENT_PID_MAX_OUT, VEL_PID_MAX_OUT, VEL_PID_MAX_OUT, POS_PID_MAX_OUT);
        motor->components.foc->theta = motor->components.encoder->phase_;
    }
    break;

    case CONTROL_MODE_VELOCITY_RAMP: // �ٶ�->�����ݶȱջ�
    {
        SetPIDLimit(motor, CURRENT_PID_MAX_OUT, CURRENT_PID_MAX_OUT, VEL_PID_MAX_OUT, VEL_PID_MAX_OUT, POS_PID_MAX_OUT);
        motor->components.foc->theta      = motor->components.encoder->phase_;
        float max_step_size               = fabsf(VEL_POS_PERIOD * motor->Controller.vel_ramp_rate);
        float full_step                   = motor->Controller.input_velocity - motor->Controller.vel_setpoint;
        float step                        = CLAMP(full_step, -max_step_size, max_step_size);
        motor->Controller.vel_setpoint   += step;
        motor->Controller.torque_setpoint = (step / VEL_POS_PERIOD) * motor->Controller.inertia;
    }
    break;

    case CONTROL_MODE_POSITION_RAMP: // λ��->�ٶ�->�����ݶȱջ�
    {
        SetPIDLimit(motor, CURRENT_PID_MAX_OUT, CURRENT_PID_MAX_OUT, VEL_PID_MAX_OUT, VEL_PID_MAX_OUT, POS_PID_MAX_OUT);
        motor->components.foc->theta = motor->components.encoder->phase_;
        if (motor->Controller.input_updated)
        {
            TRAJ_plan(motor->Controller.input_position,
                      motor->components.encoder->pos_estimate_,
                      motor->components.encoder->vel_estimate_,
                      motor->Controller.traj_vel,    // Velocity
                      motor->Controller.traj_accel,  // Acceleration
                      motor->Controller.traj_decel); // Deceleration

            Traj.t = 0.0f;
            Traj.trajectory_done = false;
            motor->Controller.input_updated = false;
        }

        // Avoid updating uninitialized trajectory
        if (Traj.trajectory_done)
        {
            break;
        }

        if (Traj.t > Traj.Tf_)
        {
            Traj.trajectory_done              = true;
            motor->Controller.pos_setpoint    = motor->Controller.input_position;
            motor->Controller.vel_setpoint    = 0.0f;
            motor->Controller.torque_setpoint = 0.0f;
        }
        else
        {
            TRAJ_eval(Traj.t);
            motor->Controller.pos_setpoint    = Traj.Y;
            motor->Controller.vel_setpoint    = Traj.Yd;
            motor->Controller.torque_setpoint = Traj.Ydd * motor->Controller.inertia;
            if (fabsf(motor->Controller.pos_setpoint - motor->components.encoder->pos_estimate_) < 1.0f)
            {
                Traj.t += VEL_POS_PERIOD;
            }
        }
    }
    break;

    case CONTROL_MODE_MIT: // DM MIT impedance: tau = kp*(p_des-p) + kd*(v_des-v) + t_ff
    {                      // p/v in turns (dm_can converts rad->turn; kp/kd are per-turn)
        // ⚠ Kd 阻尼有稳定上限: iq = Kd*(v_des-v)/Kt。本 4010 Kt≈0.0265 很小, 故 input_kd 的
        //   "电流/转速"等效增益 = Kd/Kt 被放大 ~38×。实测 input_kd>~0.1 (CAN Kd>~0.016 N·m/(rad/s))
        //   时速度阻尼环在 ~200Hz(PLL 带宽附近)相位穿越自激成极限环 → 疯狂振动。
        //   小 Kt/小惯量电机做高 Kd 阻抗本身受限; 高 Kd 需换大 Kt 电机, 或上位机按 Kt 缩放 Kd。
        SetPIDLimit(motor, 2.0f, CURRENT_PID_MAX_OUT, 0.0f, 0.0f, 0.0f);
        motor->components.foc->theta = motor->components.encoder->phase_;
        float tau = motor->Controller.input_kp * (motor->Controller.input_position - motor->components.encoder->pos_estimate_) + motor->Controller.input_kd * (motor->Controller.input_velocity - motor->components.encoder->vel_estimate_) + motor->Controller.input_torque;
        tau = CLAMP(tau, -motor->Controller.torque_limit, motor->Controller.torque_limit);
        motor->components.foc->iq_set = CLAMP(tau / motor->Controller.torque_const, -motor->Controller.current_limit, motor->Controller.current_limit);
        motor->components.foc->id_set = 0.0f;
    }
    break;

    default:
        break;
    }

    // ������20khz���ٶ���λ��5khz���ٶ���λ�ñջ���Ŀ�������ͨ��λ�ú��ٶȵĵ����Զ��趨�ģ�
    if (motor->state.Control_Mode > CONTROL_MODE_OPEN)
    {
        commonFOCOperations(motor->components.foc);

        if (motor->state.Control_Mode >= CONTROL_MODE_VELOCITY && motor->state.Control_Mode <= CONTROL_MODE_POSITION)
        {
            if (++loop_count >= 4)
            {
                loop_count  = 0;

                if (motor->state.Control_Mode == CONTROL_MODE_POSITION)
                {
                    vel_set = PID_Calc(&motor->PosPID, motor->components.encoder->pos_estimate_, motor->Controller.input_position);
					vel_set = CLAMP(vel_set, -motor->Controller.input_velocity, +motor->Controller.input_velocity);
                }
                else if (motor->state.Control_Mode == CONTROL_MODE_VELOCITY)
                {
                    vel_set = motor->Controller.input_velocity;
					vel_set = CLAMP(vel_set, -motor->Controller.vel_limit, +motor->Controller.vel_limit);
                }
                motor->components.foc->iq_set            = PID_Calc(&motor->VelPID, motor->components.encoder->vel_estimate_, vel_set);
                motor->components.foc->id_set            = 0.0f;
            }
        }
        else if (motor->state.Control_Mode >= CONTROL_MODE_VELOCITY_RAMP && motor->state.Control_Mode != CONTROL_MODE_MIT)
        {
            if (++loop_count >= 4)
            {
                loop_count  = 0;

                if (motor->state.Control_Mode == CONTROL_MODE_POSITION_RAMP)
                {
                    vel_set = PID_Calc(&motor->PosPID, motor->components.encoder->pos_estimate_, motor->Controller.pos_setpoint) + motor->Controller.vel_setpoint;
                    vel_set = CLAMP(vel_set, -motor->Controller.input_velocity, +motor->Controller.input_velocity);
                }
                else if (motor->state.Control_Mode == CONTROL_MODE_VELOCITY_RAMP)
                {
                    vel_set = motor->Controller.vel_setpoint;
					vel_set = CLAMP(vel_set, -motor->Controller.vel_limit, +motor->Controller.vel_limit);
                }
                motor->components.encoder->vel_estimate_ = CLAMP(motor->components.encoder->vel_estimate_, -motor->Controller.vel_limit, +motor->Controller.vel_limit);
                motor->components.foc->iq_set            = PID_Calc(&motor->VelPID, motor->components.encoder->vel_estimate_, vel_set) + motor->Controller.torque_setpoint;
                motor->components.foc->id_set            = 0.0f;
            }
        }

        // 齿槽前馈 + 摩擦前馈: 只在力矩/MIT模式补(无积分软环; 速度/位置环已自带积分抑齿槽/摩擦, 加FF反而变差→不补)
        if (motor->state.Control_Mode == CONTROL_MODE_TORQUE || motor->state.Control_Mode == CONTROL_MODE_MIT)
        {
            motor->components.foc->iq_set += Cogging_FF(motor->components.encoder->raw); // 齿槽
            if (g_friction_ff > 0.0f) // 库仑摩擦补偿(tanh过零平滑防抖): 抵消摩擦→反向驱动更顺、低速力矩更准
                motor->components.foc->iq_set += g_friction_ff * tanhf(motor->components.encoder->vel_estimate_ / g_friction_vth);
        }

        // #4 回馈过压抑制: 母线接近 OV 跳闸时, 把"制动"电流(与转向异号, 机械功率为负→能量回灌母线)
        //    按 vbus 线性压到 0 → 电机改滑行降速而非急刹回馈, 防急停高速旋转撞 OV→GUARD。"驱动"电流不受影响。
        if (motor->components.foc->vbus > g_vbus_regen_start)
        {
            float iq  = motor->components.foc->iq_set;
            float vel = motor->components.encoder->vel_estimate_;
            if (iq * vel < 0.0f) // 异号=制动/回馈
            {
                float k = (g_vbus_regen_full - motor->components.foc->vbus) / (g_vbus_regen_full - g_vbus_regen_start);
                k = CLAMP(k, 0.0f, 1.0f);
                motor->components.foc->iq_set = iq * k;
                g_regen_foldback_cnt++;
            }
        }
        motor->components.foc->iq_set = CLAMP(motor->components.foc->iq_set, -motor->Controller.current_limit, +motor->Controller.current_limit);
        motor->components.foc->i_q    = CLAMP(motor->components.foc->i_q, -motor->Controller.current_limit, +motor->Controller.current_limit);

        // 电流环(20kHz): 串级PI(+前馈) → v_q/v_d → SVPWM(commonInverseFOC, 用实测vbus归一#3)。
        // bEMF + ωL 解耦前馈(ODrive 同款, 见 [[foc-opensource-references]]): PMSM 电压方程
        //   v_d = PI_d - ω_e·L·i_q;  v_q = PI_q + ω_e·L·i_d + ω_e·ψf
        // ψf=2·Kt/(3·pp)(flux未真标, 由Kt推算)。前馈把随速度增长的 bEMF/互扰从 PI 卸下, 高速跟踪更准。
        // ω_e 取自速度估计(本4010含齿槽纹波 → 高齿槽下会注入纹波), 故全部挂开关+可选低通, 高速验证。
        {
            float we_raw = motor->components.encoder->vel_estimate_ * M_2PI * (float)motor->components.encoder->pole_pairs; // 电角速度 rad/s(相位超前用真值)
            float we = we_raw;
            if (g_ff_wfilt > 0.0f) { g_ff_we_f += g_ff_wfilt * (we - g_ff_we_f); we = g_ff_we_f; } // 可选一阶低通抑齿槽噪声(仅用于FF)
            float psi = (g_ff_psi > 0.0f) ? g_ff_psi
                      : (2.0f * motor->Controller.torque_const / (3.0f * (float)motor->components.encoder->pole_pairs));
            float Ls  = motor->parameters.Ls;
            float vq_ff = 0.0f, vd_ff = 0.0f;
            if (g_ff_bemf)       vq_ff += we * psi;                                 // bEMF
            if (g_ff_decouple) { vq_ff += we * Ls * motor->components.foc->i_d;     // ωL 解耦
                                 vd_ff -= we * Ls * motor->components.foc->i_q; }
            // 低速渐隐(实测低速 FF 注入齿槽纹波→变差; 高速才有用)
            float vmag = motor->components.encoder->vel_estimate_; if (vmag < 0.0f) vmag = -vmag;
            float ff_gain = (vmag <= g_ff_vlo) ? 0.0f : (vmag >= g_ff_vhi) ? 1.0f : (vmag - g_ff_vlo) / (g_ff_vhi - g_ff_vlo);
            vq_ff *= ff_gain; vd_ff *= ff_gain;
            if (g_dbg_unified_iloop) // B路: 标定同款 FOC_current(相位超前 phase+we·Ts + 积分泄放抗饱和 + 死区补偿 + 同款前馈), 内部含 SVM+SetPwm
            {
                motor->components.foc->current_ctrl_p_gain = motor->Controller.current_ctrl_p_gain; // 每拍同步: FOC_reset(GUARD/IDLE)会清零, 否则B路零增益
                motor->components.foc->current_ctrl_i_gain = motor->Controller.current_ctrl_i_gain;
                // 相位超前速度: 可选低通(抑齿槽噪声)+缩放(g_padv_k=0则关掉相位超前) —— 验证假说
                if (g_padv_filt > 0.0f) g_padv_we_f += g_padv_filt * (we_raw - g_padv_we_f);
                else                    g_padv_we_f = we_raw;
                float pv = g_padv_k * g_padv_we_f;
                FOC_current(motor->components.foc, motor->components.foc->id_set, motor->components.foc->iq_set,
                            motor->components.foc->theta, pv, vd_ff, vq_ff);
            }
            else // A路(默认): 裸 PI + 前馈 → commonInverseFOC
            {
                motor->components.foc->current_ctrl_integral_d = 0.0f; // 保持B积分清零, 切到B时无bump
                motor->components.foc->current_ctrl_integral_q = 0.0f;
                motor->components.foc->v_q = PID_Calc(&motor->IqPID, motor->components.foc->i_q, motor->components.foc->iq_set) + vq_ff;
                motor->components.foc->v_d = PID_Calc(&motor->IdPID, motor->components.foc->i_d, motor->components.foc->id_set) + vd_ff;
                commonInverseFOCOperations(motor->components.foc);
            }
        }
    }
}

/**
 * @brief ���״̬��������
 *
 * ������20khz���ٶ���λ��5khz
 *
 * @param motor ָ��������״̬�Ϳ������ݵĽṹ��ָ��
 */
// ====== 让电机唱歌 ======
// 在 20kHz 控制 ISR 里注入音频频率的 q 轴电压振荡 Vq=A·sin(2πft), 转子按音符频率抖动 → 发声。
// 零均值 → 电机不转; 用真实电角度(encoder->phase_)做换相, 力矩沿q轴线性 → 音高=f。播完关PWM。
// 音符频率[Hz], N_R=休止. 命名: 数字=八度(3=低, 4=中, 5=高), S=升半音
#define N_R  0.0f
#define N_G3 196.0f
#define N_A3 220.0f
#define N_B3 247.0f
#define N_C4 262.0f
#define N_D4 294.0f
#define N_E4 330.0f
#define N_F4 349.0f
#define N_G4 392.0f
#define N_A4 440.0f
#define N_B4 494.0f
#define N_C5 523.0f
#define N_D5 587.0f
#define N_E5 659.0f
// 旋律: 周杰伦《简单爱》副歌 —— 照实际简谱转写(1=G, 4/4; 见 zuoquba.com/jianpu/957)
//   简谱→音高(1=G4): 5̣=D4 1=G4 2=A4 3=B4 4=C5 5=D5 6=E5。每行=一句歌词, 音符与字对齐。
// 时值[ms]: 八分~230, 四分~430, 二分~720
#define E8 230
#define QN 430
#define HN 720
static const float sing_freq[] = {
    // 我   想   就   这   样   牵   着 | 你   的   手   不   放   开        (5̣12 32 4 | 5554 3 2)
    N_D4,N_G4,N_A4,N_B4,N_A4,N_B4,N_C5,  N_D5,N_D5,N_D5,N_C5,N_B4,N_A4, N_R,
    // 爱   能   不   能   够   永   远 | 单   纯   没   有   悲   哀        (5̣12 32 4 | 5556 5 2)
    N_D4,N_G4,N_A4,N_B4,N_A4,N_B4,N_C5,  N_D5,N_D5,N_D5,N_E5,N_D5,N_A4, N_R,
    // 我   想   带   你   骑   单   车   (读谱不确定, 按耳朵可调)
    N_G4,N_G4,N_E5,N_A4,N_A4,N_A4,N_B4, N_R,
    // 我   想   和   你   看   棒   球   (读谱不确定, 按耳朵可调)
    N_G4,N_G4,N_E5,N_A4,N_A4,N_B4,N_A4, N_R,
    // 像   这   样   没   担   忧 | 唱   着   歌   一   直   走            (1 6 22 33 | 4 4 4 3 2 1)
    N_G4,N_E5,N_A4,N_A4,N_B4,N_B4,  N_C5,N_C5,N_C5,N_B4,N_A4,N_G4,
};
static const uint16_t sing_dur_ms[] = {
    E8,E8,E8,E8,E8,E8,QN,  E8,E8,E8,E8,E8,HN, E8,
    E8,E8,E8,E8,E8,E8,QN,  E8,E8,E8,E8,E8,HN, E8,
    E8,E8,QN,E8,E8,E8,QN, E8,
    E8,E8,QN,E8,E8,E8,HN, E8,
    E8,E8,E8,E8,E8,QN,  E8,E8,E8,E8,E8,HN,
};
#define SING_N (sizeof(sing_freq) / sizeof(sing_freq[0]))
#define SING_AMP 0.45f  // Vq幅值[V]: 越大越响; 低音Z小电流大(~0.45V@C4≈1.3A), 保守留OC余量

static void Sing_Update(MOTOR_DATA *motor)
{
    static uint16_t note = 0;
    static uint32_t tick = 0;
    static float    ph   = 0.0f;
    static uint8_t  init = 0;
    if (!init) { note = 0; tick = 0; ph = 0.0f; init = 1; }   // 首拍复位到旋律开头
    if (note >= SING_N)                                        // 播完 → 关PWM停唱
    {
        Foc_Pwm_LowSides(); motor->state.State_Mode = STATE_MODE_GUARD; g_sing_on = 0; init = 0; return;
    }
    uint32_t dur_ticks  = (uint32_t)sing_dur_ms[note] * (CURRENT_MEASURE_HZ / 1000u);
    uint32_t gap_ticks  = 45u * (CURRENT_MEASURE_HZ / 1000u); // 每音符尾45ms静音分隔连音
    float    f          = sing_freq[note];
    if (f > 1.0f && tick + gap_ticks < dur_ticks)
    {
        ph += M_2PI * f / (float)CURRENT_MEASURE_HZ;
        if (ph > M_2PI) ph -= M_2PI;
        FOC_voltage(motor->components.foc, 0.0f, SING_AMP * sinf(ph), motor->components.encoder->phase_);
    }
    else { Foc_Pwm_LowSides(); }                               // 音符间/休止
    if (++tick >= dur_ticks) { tick = 0; note++; ph = 0.0f; }
}

void MotorStateTask(MOTOR_DATA *motor)
{
    if (g_sing_on) { Sing_Update(motor); return; } // 唱歌模式: 接管ISR, 绕开常规控制/deadman

    // --- SWD调试钩子(20kHz下检查; 绕开CAN) ---
    // g_cog_cal_trigger: 1=在现有map上迭代(ILC), 2=清零重测(首遍)
    if (g_cog_cal_trigger) { uint8_t t = g_cog_cal_trigger; g_cog_cal_trigger = 0; Cogging_CalStart(motor, t == 2); }
    if (g_dbg_save_flash) { g_dbg_save_flash = 0; Foc_Pwm_LowSides(); MotorCalib_SaveToFlash(motor); motor->state.State_Mode = STATE_MODE_GUARD; }
    if (g_dbg_vel_on || g_dbg_mit_on) // SWD 强制模式也喂通信看门狗, 否则会被掉线超时打成 GUARD
        g_can_last_rx_ms = DWT->CYCCNT; // 注: g_can_last_rx_ms 现存 DWT 周期数(不是ms), deadman 用之
    if (g_dbg_vel_on)      // 强制速度环(纹波验证)
    {
        motor->Controller.input_velocity = g_dbg_vel;
        motor->state.Control_Mode        = CONTROL_MODE_VELOCITY;
        motor->state.State_Mode          = STATE_MODE_RUNNING;
    }
    if (g_dbg_mit_on)      // 强制 MIT 阻抗(kp=0): tau=kd*(vdes-v)+tff, 无积分无硬速度环
    {
        motor->Controller.input_kp       = 0.0f;
        motor->Controller.input_kd       = g_dbg_mit_kd;
        motor->Controller.input_velocity = g_dbg_mit_v;
        motor->Controller.input_torque   = g_dbg_mit_t;
        motor->state.Control_Mode        = CONTROL_MODE_MIT;
        motor->state.State_Mode          = STATE_MODE_RUNNING;
    }

    switch (motor->state.State_Mode)
    {
    case STATE_MODE_IDLE: // ����ģʽ
        PID_clear(&motor->IqPID);
        PID_clear(&motor->IdPID);
        PID_clear(&motor->VelPID);
        PID_clear(&motor->PosPID);
        FOC_reset(motor->components.foc);
        Foc_Pwm_LowSides();
        motor->state.State_Mode = STATE_MODE_DETECTING;
        motor->state.Sub_State  = CURRENT_CALIBRATING;
        break;
    case STATE_MODE_DETECTING: // �������ģʽ
        MotorInitializeTask(motor);
        break;
    case STATE_MODE_RUNNING: // ����ģʽ
        MotorControlTask(motor);
        break;
    case STATE_MODE_GUARD: // �ػ�ģʽ
        PID_clear(&motor->IqPID);
        PID_clear(&motor->IdPID);
        PID_clear(&motor->VelPID);
        PID_clear(&motor->PosPID);
        FOC_reset(motor->components.foc);
        Foc_Pwm_LowSides();
        break;
    }
}

/**
 * @brief ����ػ�����
 *
 * �˺�������ִ�е�����ػ�����
 *
 * @param motor ָ��������״̬�Ϳ������ݵĽṹ��ָ��
 */
void MotorGuardTask(MOTOR_DATA *motor)
{
    if (motor->state.State_Mode == STATE_MODE_IDLE || motor->state.State_Mode == STATE_MODE_DETECTING)
    {
        RGB_DisplayColorById(9); // ���ϵ���ʾ����У׼
    }
    else if (motor->state.State_Mode == STATE_MODE_RUNNING)
    {
        RGB_DisplayColorById(3); // ��������ʾ��������

        // 注意: 通信掉线保护(deadman)已移到 20kHz 控制 ISR (MotorControlTask, 用 DWT 计时可靠)。
        // 本 5ms 守护任务只做电压/电流/温度/超速故障判定(这些量的时效性没 deadman 敏感)。
        // ��ص�ѹ�Ƿ��ѹ��Ƿѹ
        if (motor->components.foc->vbus <= BATVEL_MIN_LIMIT)
        {
            motor->state.Fault_State = FAULT_STATE_UNDER_VOLTAGE;
            motor->state.State_Mode  = STATE_MODE_GUARD;
            g_guard_reason = 2; g_guard_cnt++; g_guard_vbus = motor->components.foc->vbus;
        }
        else if (motor->components.foc->vbus >= BATVEL_MAX_LIMIT)
        {
            motor->state.Fault_State = FAULT_STATE_OVER_VOLTAGE;
            motor->state.State_Mode  = STATE_MODE_GUARD;
            g_guard_reason = 3; g_guard_cnt++; g_guard_vbus = motor->components.foc->vbus;
        }

        // 过流: 用相电流峰值 vs g_oc_trip_a(与20kHz ISR快路同阈), 而非 i_q vs 电流限
        //   —— 原来 i_q>=电流限(4A) 在正常最大电流工作点会误触发; 现改成"明显超限"才判故障(ISR快路已先抓)
        else if (g_oc_trip_a > 0.0f &&
                 (fabsf(motor->components.foc->i_a) > g_oc_trip_a ||
                  fabsf(motor->components.foc->i_b) > g_oc_trip_a ||
                  fabsf(motor->components.foc->i_c) > g_oc_trip_a))
        {
            motor->state.Fault_State = FAULT_STATE_OVER_CURRENT;
            motor->state.State_Mode  = STATE_MODE_GUARD;
            g_guard_reason = 4; g_guard_cnt++; g_guard_iq = motor->components.foc->i_q;
        }

        // ������ٶ��Ƿ���
        else if (fabsf(motor->components.encoder->vel_estimate_) >= SPEED_MAX_LIMIT)
        {
            motor->state.Fault_State = FAULT_STATE_SPEEDING;
            motor->state.State_Mode  = STATE_MODE_GUARD;
            g_guard_reason = 5; g_guard_cnt++; g_guard_vel = motor->components.encoder->vel_estimate_;
        }

        // ���mos�¶��Ƿ����
        else if (motor->components.current->Temp_Result >= TEMP_MAX_LIMIT)
        {
            motor->state.Fault_State = FAULT_STATE_OVER_TEMPERATURE;
            motor->state.State_Mode  = STATE_MODE_GUARD;
            g_guard_reason = 6; g_guard_cnt++;
        }
    }
    else if (motor->state.State_Mode == STATE_MODE_GUARD)
    {
        RGB_DisplayColorById(0); // �������ʾ����
    }
}
