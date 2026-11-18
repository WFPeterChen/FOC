#ifndef FOCMOTOR_H
#define FOCMOTOR_H

#include "general_def.h"
#include "pid.h"
#include "BLDCMotor.h"
#include "Motor_ADC.h"
#include "mt6816_encoder.h"

/**
 * @brief �ջ�����,�����Ҫ����ջ�,��ʹ�û�����
 *        ������Ҫ�ٶȻ��͵�����: CURRENT_LOOP | SPEED_LOOP
 *
 *  typedef struct
 *  {
 *    uint8_t current_loop : 1; // �����ջ�
 *    uint8_t speed_loop : 1;   // �ٶȱջ�
 *    uint8_t pos_loop : 1;     // λ�ñջ�
 *  } CONTROL_MODE;
 *
 *  ����ʹ��λ������Ҳ�ܷ���
 *
 *  �����g431��ô��֧�ֶ�����������
 * typedef enum
 * {
 *     CONTROL_OPEN_LOOP = 0b0000,    // �ٶȿ���
 *     CONTROL_CURRENT_LOOP = 0b0001, // �����ջ�
 *     CONTROL_SPEED_LOOP = 0b0010,   // �ٶȱջ�
 *     CONTROL_POS_LOOP = 0b0100,     // λ�ñջ�
 *
 *     // ���ģʽ
 *     CONTROL_CURRENT_SPEED_LOOP = 0b0011,     // �����ջ����ٶȱջ�
 *     CONTROL_CURRENT_POS_LOOP = 0b0101,       // �����ջ���λ�ñջ�
 *     CONTROL_SPEED_POS_LOOP = 0b0110,         // �ٶȱջ���λ�ñջ�
 *     CONTROL_CURRENT_SPEED_POS_LOOP = 0b0111, // �����ջ����ٶȱջ���λ�ñջ�
 * } CONTROL_MODE;
 */
typedef enum
{
    CONTROL_MODE_OPEN = 0,          // �ٶȿ���
    CONTROL_MODE_TORQUE = 1,        // ���ؿ���ģʽ
    CONTROL_MODE_VELOCITY = 2,      // �ٶȿ���ģʽ
    CONTROL_MODE_POSITION = 3,      // λ�ÿ���ģʽ
    CONTROL_MODE_VELOCITY_RAMP = 4, // �ٶ��ݶȿ���ģʽ
    CONTROL_MODE_POSITION_RAMP = 5, // λ���ݶ�
    CONTROL_MODE_MIT = 6,           // DM MIT �迹����: tau=kp(p_des-p)+kd(v_des-v)+t_ff // λ���ݶȿ���ģʽ
} CONTROL_MODE;

/**
 * @brief У׼״̬ģʽ
 */
typedef enum
{
    SUB_STATE_IDLE = 0,  // ������״̬
    CURRENT_CALIBRATING, // ����У׼״̬
    RSLS_CALIBRATING,    // �����б�����У׼״̬
    FLUX_CALIBRATING,    // ����У׼״̬
} SUB_STATE;

typedef enum
{
    CS_STATE_IDLE = 0,
    CS_MOTOR_R_START,
    CS_MOTOR_R_LOOP,
    CS_MOTOR_R_END,
    CS_MOTOR_L_START,
    CS_MOTOR_L_LOOP,
    CS_MOTOR_L_END,
    CS_DIR_PP_START,
    CS_DIR_PP_LOOP,
    CS_DIR_PP_END,
    CS_ENCODER_START,
    CS_ENCODER_CW_LOOP,
    CS_ENCODER_CCW_LOOP,
    CS_ENCODER_END,
    CS_REPORT_OFFSET_LUT,
} CS_STATE;

/**
 * @brief ���״̬ģʽ
 */
typedef enum
{
    STATE_MODE_IDLE = 0,  // ����ģʽ
    STATE_MODE_DETECTING, // ���ģʽ
    STATE_MODE_RUNNING,   // ����ģʽ
    STATE_MODE_GUARD,     // �ػ�ģʽ
} STATE_MODE;

/**
 * @brief ����״̬ģʽ
 */
typedef enum
{
    FAULT_STATE_NORMAL = 0,       // ����
    FAULT_STATE_OVER_CURRENT,     // ����
    FAULT_STATE_OVER_VOLTAGE,     // ��ѹ
    FAULT_STATE_UNDER_VOLTAGE,    // Ƿѹ
    FAULT_STATE_OVER_TEMPERATURE, // ����
    FAULT_STATE_SPEEDING,         // ����
} FAULT_STATE;

/**
 * @brief ����������Ͷ���
 */
typedef struct
{
    FOC_DATA *foc;         // �������
    ENCODER_DATA *encoder; // ����������
    CURRENT_DATA *current; // ��������
} MOTOR_COMPIONENTS;

/**
 * @brief ����������Ͷ���
 */
typedef struct
{
    float Rs;   // �����
    float Ls;   // ����
    float flux; // ����
} MOTOR_PARAMETERS;

typedef struct
{
    float inertia;              // [A/(turn/s^2)]
    float torque_ramp_rate;     // [Nm/s]
    float vel_ramp_rate;        // [(turn/s)/s]
    float traj_vel;             // [turn/s]
    float traj_accel;           // [(turn/s)/s]
    float traj_decel;           // [(turn/s)/s]
    float vel_limit;            // [turn/s]
    float torque_const;         // [Nm/A]
    float torque_limit;         // [Nm]
    float current_limit;        // [A]
    float voltage_limit;        // [V]
    float current_ctrl_p_gain;  // (Auto)
    float current_ctrl_i_gain;  // (Auto)
    int current_ctrl_bandwidth; // [rad/s] Current loop bandwidth 100~2000

    float input_position; // ����λ��
    float input_velocity; // �����ٶ�
    float input_torque;   // ��������
    float input_kp;       // DM MIT: kp (per-turn, dm_can has converted from rad)
    float input_kd;       // DM MIT: kd (per-turn/s)
    float input_current;  // �������

    float pos_setpoint;    // λ���趨ֵ
    float vel_setpoint;    // �ٶ��趨ֵ
    float torque_setpoint; // �����趨ֵ

    volatile bool input_updated; // ����λ�ø��±�־
} MOTOR_CONTROLLER;

/**
 * @brief ���״̬���Ͷ���
 */
typedef struct
{
    STATE_MODE State_Mode;     // ����״̬
    CONTROL_MODE Control_Mode; // �ջ�����
    SUB_STATE Sub_State;       // ���ģʽ
    CS_STATE Cs_State;         // �������
    FAULT_STATE Fault_State;   // ����״̬
} MOTOR_STATE;

typedef struct
{
    MOTOR_COMPIONENTS components; // ������
    MOTOR_STATE state;            // ���״̬
    MOTOR_PARAMETERS parameters;  // �������
    MOTOR_CONTROLLER Controller;  // �������

    PidTypeDef IqPID;  // ���Ƶ�����IQ��PID������
    PidTypeDef IdPID;  // ���Ƶ�����ID��PID������
    PidTypeDef VelPID; // ���Ƶ���ٶȵ�PID������
    PidTypeDef PosPID; // ���Ƶ��λ�õ�PID������
} MOTOR_DATA;

extern MOTOR_DATA motor_data;
void Init_Motor_No_Calib(MOTOR_DATA *motor);      // ��У׼
void Init_Motor_Calib(MOTOR_DATA *motor);         // У׼
void GetMotorADC1PhaseCurrent(MOTOR_DATA *motor); // ��ȡ����
void TempResultTask(MOTOR_DATA *motor);           // �¶Ȼ�ȡ����
void MotorStateTask(MOTOR_DATA *motor);           // FOC��������
void MotorGuardTask(MOTOR_DATA *motor);           // FOC�ػ�����
#endif                                            // FOCMOTOR_H
