#ifndef MT6816_ENCODER_H
#define MT6816_ENCODER_H

#include "general_def.h"
#include "gpio.h"
#include "spi.h"
#include "BLDCMotor.h"
#include <stm32g431xx.h>

// ��ʱ��ض���
#define MT6816_MAX_DELAY (10) // ��ʱ���ֵ����λ�����룩

// SPIƬѡ�źſ��ƺ궨��
//#define MT6816_IN_OUT

#ifdef MT6816_IN_OUT // ���
#define MT6816_SPI_Get_HSPI (hspi1) // ��ȡSPI���
#define MT6816_SPI_CS_L() HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET)
#define MT6816_SPI_CS_H() HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET)
#else // ����
#define MT6816_SPI_Get_HSPI (hspi3) // ��ȡSPI���
#define MT6816_SPI_CS_L() HAL_GPIO_WritePin(SPI3_CS_GPIO_Port, SPI3_CS_Pin, GPIO_PIN_RESET)
#define MT6816_SPI_CS_H() HAL_GPIO_WritePin(SPI3_CS_GPIO_Port, SPI3_CS_Pin, GPIO_PIN_SET)
#endif

// MT6816�Ĵ�����ַ����
#define MT6816_Init_Reg (0x00)              // ��ʼ���Ĵ�����ַ
#define MT6816_Angle_Reg (0x80 | 0x03)      // ��ȡ�Ƕ���Ϣ�Ĵ�����ַ
#define MT6816_Warning_Reg (0x80 | 0x04)    // ��ȡ������Ϣ�Ĵ�����ַ��������żУ��λ��
#define MT6816_Over_Speed_Reg (0x80 | 0x05) // ��ȡ������Ϣ�Ĵ�����ַ

// ��������
// PLL 带宽(速度估计): 2000rad/s≈318Hz。注: 2026-07-02 排查 MIT 振动一度怀疑此值并试过 1000/4000,
//   最终定论 MIT 振动真凶是 cogging 前馈图(见 falconfoc-mit-kd-limit / cogging), 与 PLL 无关, 故保持原 2000。
#define ENCODER_PLL_BANDWIDTH 2000.0f
#define ENCODER_CPR 16384u     // �������ֱ���u
#define ENCODER_CPR_F 16384.0f // �������ֱ���f
#define ENCODER_CPR_DIV (ENCODER_CPR >> 1)
#define MAX_ANGLE 360.0f      // 360��ΪһȦ
#define MAX_ANGLE_HALF 180.0f // 180��

// ����ö�����Ͷ���
typedef enum
{
    CW = 1,     // ˳ʱ�뷽��
    CCW = -1,   // ��ʱ�뷽��
    UNKNOWN = 0 // δ֪����Ч״̬
} Direction;

typedef struct
{
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *CS_Port;
    uint16_t CS_Pin;

    uint32_t angle;
    uint8_t rx_err_count;
    uint8_t check_err_count;

    int32_t offset_lut[128]; // ���ڴű��������Ի��Ĳ��ұ�
    int32_t count;           // �ű������ĵ�ǰ����ֵ����һ�����������������ڵļ���ֵ��

    // �Ƕ���Ϣ
    uint8_t pole_pairs; // ��������������ڼ����Ƕ�
    int cnt;
    int raw; // �ű�������ԭʼ����ֵ
    int dir; // �ű���������ת����+1 ��ʾ˳ʱ�룬-1 ��ʾ��ʱ��
    int pos_abs_;
    int count_in_cpr_;
    int count_in_cpr_prev;
    int shadow_count_;
    int EncoderInit_Flag;
    float pos_estimate_counts_;
    float vel_estimate_counts_;
    float pos_cpr_counts_;
    float pos_estimate_;
    float pos_zero_offset; // SET_ZERO(0xFE)用: 上报圈数 = 原始pos_estimate_ - 此偏移(默认0)
    float vel_estimate_;
    float pos_cpr_;
    float phase_;
    float interpolation_;
    int calib_valid; // (Auto)
    float mec_angle;
    float elec_angle;     // ��Ƕȣ���Ӧ�ڻ�е�Ƕȵĵ��ת��λ�ã����ݵ���ļ���������õ���
    float encoder_offset; // offset
    float speed;          // �ٶ�
    float last_angle;     // ��һ�εĽǶ�

    // �ٶ���Ϣ
    float theta_acc; // ��Ƕ�Ŀ������
} ENCODER_DATA;

extern ENCODER_DATA encoder_data;

float low_pass_filter(float input);                    // ��ͨ�˲�����
float normalize_angle(float angle);                    // �Ƕȹ�һ������
void GetMotor_Angle(ENCODER_DATA *encoder);            // ���±��������ݺ���
void Theta_ADD(ENCODER_DATA *encoder);                 // ��Ƕ��������㺯��
void Encoder_ResetPLL(void);                           // 复位PLL装载(标定/flash加载后调用, 防超速误触发)
#endif                                                 // MT6816_ENCODER_H
