#ifndef BLDCMotor_H
#define BLDCMotor_H

#include "general_def.h"
#include "tim.h"
#include "arm_math.h"
#include "mt6816_encoder.h"

// �������
#define MOTOR_PM3510 1
#define MOTOR_DJI2312 0

#define lf 0
#define rf 0
#define lb 0
#define rb 1

#define N_BASE 6.0f               // 3→6 (2026-07-12, 按 BATTERY_SWAP_TODO): 24V开关电源/6S. BATVEL自动=24V
#define BATVEL (4.0f * N_BASE)    // ��ص�ѹ
#define INVBATVEL (1.0f / BATVEL) // ��ص�ѹ�ĵ���

// 标定来源选择 (二者都让电机"已标定", 区别在数据从哪来):
//  =0 (部署推荐): 开机先 MotorCalib_LoadFromFlash —— 从 page63 加载完整标定(Rs/Ls/flux/编码器零位/
//      pp/方向 + offset_lut[128] + cogging), 不空转、秒启动; flash 无效时才现场重标(安全兜底)并回存。
//      当前 page63 已有效(magic 0xCA11B007, offset=15702, pp=7, dir=CW, has_cogging=1)。
//  =1: 跳过 flash, 直接用本文件编译期常数(MOTOR_OFFSET/MOTOR_DIRECTION/Rs/Ls)。注意: 不含 offset_lut
//      和 cogging, 也无重标兜底 → 比 =0 差, 仅在确定 flash 不可用时才用。
#define PRE_CALIBRATED 0
// ↑ 改回 0 (2026-07-12, 换5210): 首启全标定(编码器零位/方向/pp/LUT+Rs/Ls), flash旧4010标定已擦(备份在
//   backups/4010_params_final/)。⚠️ on-board Ls 已两次出坏值(20×/4×) → 标完必须 SWD 验 Ls(百µH级)和
//   IqPID.Kp(0.1~1)后再放行大电流; 离谱则 SWD 覆盖 RAM + g_dbg_save_flash 固化(runbook Step2)。
// (2026-07-02 曾因 Ls 辨识出 4.4e-5 改成 1 用编译期常数, 历史见 4010-bringup 记忆。)

#if MOTOR_DJI2312 // �������
#if PRE_CALIBRATED
#define MPTOR_P 7u              // ���������
#define MOTOR_RS 0.135551453f   // �����
#define MOTOR_LS 2.4615214e-05f // ����
#define MOTOR_FLUX 0.0f         // ����
#define MOTOR_OFFSET 3997       // ���ƫ����
#define MOTOR_DIRECTION CCW     // �������
#else
#define MPTOR_P 0u              // ���������
#define MOTOR_RS 0.0f           // �����
#define MOTOR_LS 0.0f           // ����
#define MOTOR_FLUX 0.0f         // ����
#define MOTOR_OFFSET 0.0f       // ���ƫ����
#define MOTOR_DIRECTION UNKNOWN // �������
#endif

// ���������ض��� ���ݶȼӼ��ٲ���̫�󣬲�Ȼ�����ڼ���ʱ����ֶ���ʽ�µ���λ�Ʋ����̿������������������ԣ�������
#define MOTOR_INERTIA 0.0001f               // ת������ [A/(turn/s^2)]������Ϊ�������1תÿ��ļ��ٶ���תʱ��Ҫ�ṩ�����ڼ������ĵĵ�������Ҫ���ݵ�����������������������е��ԡ�
#define MOTOR_CURRENT_RAMP_RATE 0.001f      // ������������ [Nm/s]
#define MOTOR_VEL_RAMP_RATE 160.0f          // ת�������ٶ� [(turn/s)/s]
#define MOTOR_TRAJ_VEL 160.0f               // ���ι켣����ģʽ�����ת�� [turn/s] (�趨ֵӦ��С�ڻ���� MOTOR_VEL_LIMIT)
#define MOTOR_TRAJ_ACCEL 40.0f              // ���ι켣����ģʽ�¼��ٶ� [(turn/s)/s]
#define MOTOR_TRAJ_DECEL 40.0f              // ���ι켣����ģʽ�¼��ٶ� [(turn/s)/s]
#define MOTOR_TORQUE_CONST 0.0f             // ת�س��� [Nm/A]
#define MOTOR_TORQUE_LIMIT 0.0f             // ת������ֵ [Nm] (��ֵ���Ƶ���ʵ�������е����ת�� torque) һ������Ϊ����������ת��
#define MOTOR_VEL_LIMIT 168.0f              // ת������ֵ [turn/s] (turn/s = ���ת��rpm / 60) һ������Ϊ����������ת��
#define MOTOR_VOLTAGE_LIMIT 12.0f           // �����ѹ����ֵ [V]
#define MOTOR_CURRENT_LIMIT 6.0f            // �����������ֵ [A] (��ֵ���Ƶ���ʵ�������е��������� foc->i_q_filt) һ������Ϊ��������������
#define MOTOR_CURRENT_CTRL_P_GAIN 0.0f      // ���������棬��У׼�������Զ�����ó���Ҳ���������� (Auto)
#define MOTOR_CURRENT_CTRL_I_GAIN 0.0f      // �������������棬��У׼�������Զ�����ó���Ҳ���������� (Auto)
#define MOTOR_CURRENT_CTRL_BANDWIDTH 230.0f // ����������[rad/s]����Χ (2~60000)
#define MOTOR_INPUT_CURRENT 5.0f            // Ŀ�����
#define MOTOR_INPUT_TORQUE 0.0f             // Ŀ������
#define MOTOR_INPUT_VELOCITY 0.0f           // Ŀ��ת��
#define MOTOR_INPUT_POSITION 0.0f           // Ŀ��λ��

// PID����
#define MOTOR_IQ_PID_KP 0.0f
#define MOTOR_IQ_PID_KI 0.0f
#define MOTOR_IQ_PID_KD 0.0f
#define MOTOR_ID_PID_KP 0.0f
#define MOTOR_ID_PID_KI 0.0f
#define MOTOR_ID_PID_KD 0.0f
#define MOTOR_VEL_PID_KP 0.40f
#define MOTOR_VEL_PID_KI 0.0040f // ����̫�󣬷���ᵼ�µ�����Ҷ������Ժ������޸�ʱ�л��Ͳ�˳����
#define MOTOR_VEL_PID_KD 0.0f
#define MOTOR_POS_PID_KP 165.0f // ���Կ�����ʱ�ٶ��Ƿ��׼��
#define MOTOR_POS_PID_KI 0.0f
#define MOTOR_POS_PID_KD 0.0f

/*��������ʱ��      �޷�ֵû��ʲôԼ�����ǲ��ܴ���ĸ�ߵ�ѹ/����3������ᵼ�µ������������
**�����ٶȻ�ʱ��    ��ΪҪ�Ⱦ����ٶȻ���Ȼ���ٰ��ٶȻ���ֵ���������������Ե��������޷�Ϊĸ�ߵ�ѹ/����3���ٶȻ����޷�Ϊ������ֵ
**�����ٶ�λ�û�ʱ����λ�û����޷���Ϊ������ת�ټ��ɣ����������޷�Ϊĸ�ߵ�ѹ/����3���ٶȻ����޷�Ϊ������ֵ
**
**λ�û����޷����ɹ��󣬲�Ȼ���кܴ�Ĺ���һ���Ӿͱ������ˣ�����
*/
#define CURRENT_PID_MAX_OUT ((BATVEL) / _SQRT3)
#define IQ_PID_MAX_OUT CURRENT_PID_MAX_OUT
#define IQ_PID_MAX_IOUT CURRENT_PID_MAX_OUT
#define ID_PID_MAX_OUT CURRENT_PID_MAX_OUT
#define ID_PID_MAX_IOUT CURRENT_PID_MAX_OUT
#define VEL_PID_MAX_OUT MOTOR_CURRENT_LIMIT
#define VEL_PID_MAX_IOUT MOTOR_CURRENT_LIMIT
#define POS_PID_MAX_OUT (MOTOR_VEL_LIMIT / 1.3f)
#define POS_PID_MAX_IOUT (MOTOR_VEL_LIMIT / 1.3f)
#endif

#if MOTOR_PM3510 // �������
#if PRE_CALIBRATED
#if lf
#define MPTOR_P 11u              // ���������
#define MOTOR_RS 1.73649645f     // �����
#define MOTOR_LS 0.000637838093f // ����
#define MOTOR_FLUX 0.0f          // ����
#define MOTOR_OFFSET 2552        // ���ƫ����
#define MOTOR_DIRECTION CW       // �������
#endif
#if rf
#define MPTOR_P 11u              // ���������
#define MOTOR_RS 1.74725103f     // �����
#define MOTOR_LS 0.000649235852f // ����
#define MOTOR_FLUX 0.0f          // ����
#define MOTOR_OFFSET 14868       // ���ƫ����
#define MOTOR_DIRECTION CCW      // �������
#endif
#if lb
#define MPTOR_P 11u              // ���������
#define MOTOR_RS 1.72242999f     // �����
#define MOTOR_LS 0.000622660271f // ����
#define MOTOR_FLUX 0.0f          // ����
#define MOTOR_OFFSET 15772       // ���ƫ����
#define MOTOR_DIRECTION CW       // �������
#endif
#if rb
// ===== 5210 (24N28P, 铭牌~300KV) 2026-07-12 换装; 4010 参数已归档 backups/4010_params_final/ =====
#define MPTOR_P 14u             // 磁铁28/2
// ⚠️ RS/LS/OFFSET/DIRECTION 均为 4010 遗留值, 仅 PRE_CALIBRATED=1 时使用——5210 走 PRE_CAL=0 现场标定,
// 标定后 SWD 验 Ls(应百µH级)/IqPID.Kp(0.1~1) 再决定是否固化到这里(runbook Step2, 勿信 on-board Ls 不验)。
#define MOTOR_RS 0.18715331f     // [4010遗留, 待5210标定/LCR后更新]
#define MOTOR_LS 0.00017139669f // [4010遗留, 待5210标定/LCR后更新]
#define MOTOR_FLUX 0.0f          // ����
#define MOTOR_OFFSET 15702        // [4010遗留, 5210标定后作废]
#define MOTOR_DIRECTION CW      // [4010遗留, 5210标定后作废]
#endif
#else
#define MPTOR_P 0u              // ���������
#define MOTOR_RS 0.0f           // �����
#define MOTOR_LS 0.0f           // ����
#define MOTOR_FLUX 0.0f         // ����
#define MOTOR_OFFSET 0.0f       // ���ƫ����
#define MOTOR_DIRECTION UNKNOWN // �������
#endif

// ���������ض���
#define MOTOR_INERTIA 0.0001f                                         // ת������ [A/(turn/s^2)]������Ϊ�������1תÿ��ļ��ٶ���תʱ��Ҫ�ṩ�����ڼ������ĵĵ�������Ҫ���ݵ�����������������������е��ԡ�
#define MOTOR_CURRENT_RAMP_RATE 0.001f                                // ������������ [Nm/s]
#define MOTOR_VEL_RAMP_RATE 15.0f                                     // ת�������ٶ� [(turn/s)/s]
#define MOTOR_TRAJ_VEL 15.0f                                          // ���ι켣����ģʽ�����ת�� [turn/s] (�趨ֵӦ��С�ڻ���� MOTOR_VEL_LIMIT)
#define MOTOR_TRAJ_ACCEL 5.0f                                         // ���ι켣����ģʽ�¼��ٶ� [(turn/s)/s]
#define MOTOR_TRAJ_DECEL 5.0f                                         // ���ι켣����ģʽ�¼��ٶ� [(turn/s)/s]
#define MOTOR_TORQUE_CONST 0.0306f // 5210 实测(bEMF法 2026-07-12): λ_ph=1.458mWb, Kt=1.5·14·λ. 两通道差0.3%, 等效KV312(铭牌300) //                                     // ת�س��� [Nm/A]
#define MOTOR_TORQUE_LIMIT 0.306f // 5210: 10A x Kt0.0306(实测). 保持10A钳位含义 //                                     // ת������ֵ [Nm] (��ֵ���Ƶ���ʵ�������е����ת�� torque) һ������Ϊ����������ת��
#define MOTOR_VEL_LIMIT 50.0f // 4010 bench(12V)限速; 上6S可提到~150. (原PM3510=15.75) //                                      // ת������ֵ [turn/s] (turn/s = ���ת��rpm / 60) һ������Ϊ����������ת��
#define MOTOR_VOLTAGE_LIMIT 12.0f                                     // �����ѹ����ֵ [V]
#define MOTOR_CURRENT_LIMIT 4.0f // bench匹配3A限流电源; 上6S改回10A(电机能力). 原为公式(算错) // �����������ֵ [A] (��ֵ���Ƶ���ʵ�������е��������� foc->i_q_filt) һ������Ϊ��������������
#define MOTOR_CURRENT_CTRL_P_GAIN 0.0f                                // ���������棬��У׼�������Զ�����ó���Ҳ���������� (Auto)
#define MOTOR_CURRENT_CTRL_I_GAIN 0.0f                                // �������������棬��У׼�������Զ�����ó���Ҳ���������� (Auto)
#define MOTOR_CURRENT_CTRL_BANDWIDTH 230.0f                           // ����������[rad/s]����Χ (2~60000)
#define MOTOR_INPUT_CURRENT 0.4f                                      // Ŀ�����
#define MOTOR_INPUT_TORQUE 0.08f                                      // Ŀ������
#define MOTOR_INPUT_VELOCITY 0.0f                                     // Ŀ��ת��
#define MOTOR_INPUT_POSITION 0.0f                                     // Ŀ��λ��

// PID����
#define MOTOR_IQ_PID_KP 0.0f
#define MOTOR_IQ_PID_KI 0.0f
#define MOTOR_IQ_PID_KD 0.0f
#define MOTOR_ID_PID_KP 0.0f
#define MOTOR_ID_PID_KI 0.0f
#define MOTOR_ID_PID_KD 0.0f
#define MOTOR_VEL_PID_KP 2.0f // 5210整定(2026-07-18, dtcomp关闭条件下扫描): 大惯量吃得下高增益; 4010时0.8
#define MOTOR_VEL_PID_KI 0.05f // 5210(4010时0.02)
#define MOTOR_VEL_PID_KD 0.0f
#define MOTOR_POS_PID_KP 120.0f
#define MOTOR_POS_PID_KI 0.0f
#define MOTOR_POS_PID_KD 0.0f

#define CURRENT_PID_MAX_OUT ((BATVEL) / _SQRT3)
#define IQ_PID_MAX_OUT CURRENT_PID_MAX_OUT
#define IQ_PID_MAX_IOUT CURRENT_PID_MAX_OUT
#define ID_PID_MAX_OUT CURRENT_PID_MAX_OUT
#define ID_PID_MAX_IOUT CURRENT_PID_MAX_OUT
#define VEL_PID_MAX_OUT MOTOR_CURRENT_LIMIT
#define VEL_PID_MAX_IOUT MOTOR_CURRENT_LIMIT
#define POS_PID_MAX_OUT (MOTOR_VEL_LIMIT - 3.0f)
#define POS_PID_MAX_IOUT (MOTOR_VEL_LIMIT - 3.0f)

#endif

// Active motor: LJW8115 KV76, star winding, 36N42P. 24V bring-up only.
// Datasheet line R=0.147 ohm; line L=57..85 uH. Initial phase model:
// R=line/2, L=min(line)/2 (saliency/mutual inductance still needs bench validation).
#define MOTOR_EXPECTED_POLE_PAIRS 21u
#define MOTOR_USE_FIXED_RL 1
#define MOTOR_PHASE_RS 0.0735f
#define MOTOR_PHASE_LS 0.0000285f
#if PRE_CALIBRATED
#error "LJW8115 requires fresh encoder calibration; legacy offsets are invalid"
#endif
#undef MOTOR_RS
#undef MOTOR_LS
#define MOTOR_RS MOTOR_PHASE_RS
#define MOTOR_LS MOTOR_PHASE_LS
#undef MOTOR_TORQUE_CONST
#define MOTOR_TORQUE_CONST 0.125f // Datasheet Nm/A; current convention must be verified on bench.
#undef MOTOR_TORQUE_LIMIT
#define MOTOR_TORQUE_LIMIT (MOTOR_CURRENT_LIMIT * MOTOR_TORQUE_CONST)
#undef MOTOR_VEL_LIMIT
#define MOTOR_VEL_LIMIT 10.0f // 600rpm bring-up ceiling, not rated speed.
#undef MOTOR_TRAJ_VEL
#define MOTOR_TRAJ_VEL 5.0f
#undef MOTOR_VEL_RAMP_RATE
#define MOTOR_VEL_RAMP_RATE 5.0f
#undef MOTOR_VEL_PID_KP
#define MOTOR_VEL_PID_KP 0.2f
#undef MOTOR_VEL_PID_KI
#define MOTOR_VEL_PID_KI 0.005f
#undef MOTOR_POS_PID_KP
#define MOTOR_POS_PID_KP 20.0f
#undef MOTOR_INPUT_CURRENT
#define MOTOR_INPUT_CURRENT 0.0f
#undef MOTOR_INPUT_TORQUE
#define MOTOR_INPUT_TORQUE 0.0f
// Keep 4A current limit, 2A alignment, 19.8..27V window and existing CAN scaling.

// Calib
#define CURRENT_MAX_CALIB 2.0f // 校准电流 0.5→2.0 (2026-07-12, 5210): 0.5A拖力~15mNm拖不动5210大转子(齿槽/摩擦大),
                               // pp检测打滑数出5(实际14)、零位/LUT同病。2A≈60mNm 拖得稳; 4010 时 0.5A 够是因为齿槽仅3mNm。
#define VOLTAGE_MAX_CALIB 3.0f // У׼����ѹ����(������У׼��ʱ�򣬵��û�з������١�������(������ͨ�������������һ��)������Ҫ�������ֵ)

#define OFFSET_LUT_NUM 128               // ƫ����LUT��
#define COGGING_MAP_NUM 3000             // ������
#define CALB_SPEED M_2PI                 // ���ת��
#define MOTOR_POLE_PAIRS_MAX 21          // ����⼫�������������ӵ���Ҫע��ѵĴ�С���䣩
#define SAMPLES_PER_PPAIR 32 // 每极对采样数 128→32 (2026-07-12, 5210事故): 原 =OFFSET_LUT_NUM(128) 时
// p_error_arr=malloc(128×PP_MAX20×4)=10KB, 但堆只剩~6.6KB(6月标4010时固件小够用, USB等加后不够)——
// malloc 与主栈重叠, 4010(7pp,896样本)没写到栈侥幸存活, 5210(14pp,1792样本)写穿ISR栈→硬fault→IWDG
// 36s重启循环, 标定永远做不完。32×14=448样本对128格LUT仍3.5×过采样; malloc缩至2.5KB安全。

// Control
// 2026-07-12 换6S/24V(BATTERY_SWAP_TODO 1b/1c): OV/UV 从比例改绝对值。
// 原 *1.2=28.8V 超 TVS SMBJ28CA 工作电压28V(保护形同虚设); 原 /1.5=16V=2.67V/cell 过放伤电芯。
// 保护梯: 满电25.2 < 回馈抑制26.1~26.8 < OV跳闸27 < TVS 28。⚠️12V台架在此固件下会UV, 回12V要改回N_BASE=3。
#define BATVEL_MAX_LIMIT 27.0f                   // ��ع�ѹ������ֵ
#define BATVEL_MIN_LIMIT 19.8f                   // ��ص�ѹ������ֵ (6S 3.3V/cell)
#define SPEED_MAX_LIMIT (MOTOR_VEL_LIMIT * 1.2f) // ��������
// #4 回馈过压抑制阈值: 在 OV 硬跳闸(BATVEL_MAX_LIMIT)前留窗口压制制动电流
#define VBUS_REGEN_START (BATVEL_MAX_LIMIT - 0.9f) // 母线≥此值: 开始线性压制制动电流
#define VBUS_REGEN_FULL  (BATVEL_MAX_LIMIT - 0.2f) // 母线≥此值: 制动电流全切(滑行降速)
#define TEMP_MAX_LIMIT 65.0f                     // ��������
// MIT 模式 Kd(per-turn)硬钳上限: 4010 实测稳定上限 input_kd≈0.1, 再高 Kd/Kt 放大自激振动。
// 见 falconfoc-mit-kd-limit。换高 Kt/低齿槽力矩电机时按该电机重标。
#define MIT_KD_TURN_MAX 0.1f

// ��������
#define TIMER1_CLK_MHz 168                                                                                         // ��ʱ��ʱ��Ƶ��
#define PWM_FREQUENCY 20000                                                                                        // PWMƵ��20KHz
#define PWM_MEASURE_PERIOD (float)(1.0f / (float)PWM_FREQUENCY)                                                    // PWM����
#define CURRENT_MEASURE_HZ PWM_FREQUENCY                                                                           // ����Ƶ��
#define CURRENT_MEASURE_PERIOD (float)(1.0f / (float)CURRENT_MEASURE_HZ)                                           // ��������
#define PWM_PERIOD_CYCLES (uint16_t)((TIMER1_CLK_MHz * (uint32_t)1000000u / ((uint32_t)(PWM_FREQUENCY))) & 0xFFFE) // ��ʱ��1��������
#define PWM_ARR (uint16_t)(PWM_PERIOD_CYCLES / 2u)                                                                 // 3500
#define VEL_POS_HZ 5000                                                                                            // �ٶ�λ�û�Ƶ��
#define VEL_POS_PERIOD (float)(1.0f / (float)VEL_POS_HZ)                                                           // �ٶ�λ�û�����
#define CURRENT_CALIBRATION_DURATION 1000u                                                                         // ����У׼ʱ��
#define DEADTIME_COMP 73u // 死区补偿时钟数=总有效死区430ns×170MHz. 示波器实测(2026-07-11,相节点二极管平台中位430ns,n=20): TIM1 DTG 20clk=118ns + FD6288Q内建~300ns. 原值20只补了TIM部分
#define TS 1.0f                                                                                                    // �������� (��ռ�ձ�������0-1֮��)

// Ӳ������
#define V_REG 1.65f                                                             // ADC�ο���ѹ
#define CURRENT_SHUNT_RES 0.001f                                                 // ������������
#define CURRENT_AMP_GAIN 50.0f                                                  // �����Ŵ���
#define VIN_R1 1000.0f                                                          // ��ѹ������ѹ����
#define VIN_R2 10000.0f                                                         // ��ѹ������ѹ����
#define FAC_CURRENT ((3.3f / 4095.0f) / (CURRENT_SHUNT_RES * CURRENT_AMP_GAIN)) // �����Ŵ���
#define VOLTAGE_TO_ADC_FACTOR (((VIN_R2 + VIN_R1) / VIN_R1) * (3.3f / 4095.0f)) // ��ѹ�Ŵ���

// ����PWMռ�ձȺ궨��
#define set_dtc_a(value) __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, value) // ����A��PWMռ�ձ�
#define set_dtc_b(value) __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, value) // ����B��PWMռ�ձ�
#define set_dtc_c(value) __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, value) // ����C��PWMռ�ձ�

#pragma pack(1)
// ����ö��
typedef enum
{
    SECTOR_1 = 1,
    SECTOR_2,
    SECTOR_3,
    SECTOR_4,
    SECTOR_5,
    SECTOR_6
} svpwm_sector_t;

typedef struct
{
    float vbus;     // ĸ�ߵ�ѹ
    float inv_vbus; // ĸ�ߵ�ѹ���������ڼ�������͵�ѹ
    float theta;    // �Ƕ�
    float i_q_filt, i_d_filt, i_bus, i_bus_filt, power_filt;

    float current_ctrl_integral_d, current_ctrl_integral_q; // Current error integrals
    float current_ctrl_p_gain;                              // (Auto)
    float current_ctrl_i_gain;                              // (Auto)
    // ������Ϣ������SVPWM��
    int sector; // ��ǰ����������SVPWM����

    // ����ϵ��ѹ����������
    float i_a; // A �����
    float i_b; // B �����
    float i_c; // C �����

    float v_a; // A ���ѹ
    float v_b; // B ���ѹ
    float v_c; // C ���ѹ

    float i_d; // D ����ϵ������FOC�е�ֱ�������
    float i_q; // Q ����ϵ������FOC�еĽ��������

    float v_d; // D ����ϵ��ѹ��FOC�е�ֱ���ѹ�� ����Ť��
    float v_q; // Q ����ϵ��ѹ��FOC�еĽ����ѹ�� ���ſ���

    float i_alpha; // Alpha ����ϵ������Clarke�任��ĵ�����
    float i_beta;  // Beta  ����ϵ������Clarke�任��ĵ�����

    float v_alpha; // Alpha ����ϵ��ѹ����Clarke�任��ĵ�ѹ��
    float v_beta;  // Beta  ����ϵ��ѹ����Clarke�任��ĵ�ѹ��

    float dtc_a; // A �� PWM ռ�ձ�
    float dtc_b; // B �� PWM ռ�ձ�
    float dtc_c; // C �� PWM ռ�ձ�

    float vd_set; // d ���ѹ����
    float vq_set; // q ���ѹ����
    float id_set; // d ���������
    float iq_set; // q ���������

    float sin_val; // ��Ƕȵ�����ֵ������FOC����
    float cos_val; // ��Ƕȵ�����ֵ������FOC����
} FOC_DATA;
#pragma pack()

extern FOC_DATA foc_data;

// ������������
void FOC_reset(FOC_DATA *foc);                                                             // ����FOC����
void Foc_Pwm_LowSides(void);                                                               // ����FOC ��ռ�ձ�Ϊ��
void Foc_Pwm_Start(void);                                                                  // ����FOC PWM
void Foc_Pwm_Stop(void);                                                                   // ֹͣFOC PWM
void SetPwm(FOC_DATA *foc);                                                                // ����PWMռ�ձ�
void Sin_Cos_Val(FOC_DATA *foc);                                                           // �������Һ�����ֵ
void Clarke(FOC_DATA *foc);                                                                // Clarke�任
void Park(FOC_DATA *foc);                                                                  // Park�任
void Inv_clarke(FOC_DATA *foc);                                                            // ��Clarke�任
void Inv_Park(FOC_DATA *foc);                                                              // ��Park�任
void Svpwm_Midpoint(FOC_DATA *foc);                                                        // SVPWM����
void Svpwm_Sector(FOC_DATA *foc);                                                          // SVPWM����ѡ��
void commonFOCOperations(FOC_DATA *foc);                                                   // ͨ��FOC����
void commonInverseFOCOperations(FOC_DATA *foc);                                            // ͨ����FOC����
void setPhaseVoltage(FOC_DATA *foc, float Vd_set, float Vq_set, float phase);              // �������ѹ�������
void FOC_voltage(FOC_DATA *foc, float Vd_set, float Vq_set, float phase);                  // ��ѹ����
void FOC_current(FOC_DATA *foc, float Id_set, float Iq_set, float phase, float phase_vel, float vd_ff, float vq_ff); // ��������
void Deadtime_Compensate(FOC_DATA *foc);                                                   // 死区补偿(abc占空比级)
extern volatile uint8_t g_dt_comp_on;      // 1=补偿生效(运行环), 0=关(标定)
extern volatile uint8_t g_dt_comp_master;  // 主开关(USB在线A/B), 运行环每拍 g_dt_comp_on=master
extern volatile float   g_dt_comp_duty;    // 每相补偿量[占空比], 取负=反号; 默认 deadtime/PWM周期
extern volatile float   g_dt_comp_ithresh; // 过零平滑带宽[A]
extern volatile float   g_dt_comp_velgate; // 速度门控阈值[rad/s], 迟滞0.8/1.2×(v3)
#endif                                                                                     // BLDCMotor_H
