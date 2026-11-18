#include "motor_calib.h"
#include "cogging.h"   // cogging_map / cogging_valid 持久化
#include "flash_driver.h"
#include "bsp_flash.h" // ADDR_FLASH_PAGE_62/63
#include "string.h"

#define CALIB_ADDR ADDR_FLASH_PAGE_63 // 0x0801F800, 电机参数+LUT(2KB)
#define CALIB_PAGE 63u
#define COG_ADDR   ADDR_FLASH_PAGE_62 // 0x0801F000, cogging_map(512*4=2KB整页)
#define COG_PAGE   62u

/* 持久化的标定数据布局 (544 字节, 8 的倍数, 满足双字写) */
typedef struct __attribute__((aligned(8)))
{
    uint32_t magic;
    float    Rs;
    float    Ls;
    float    flux;
    float    encoder_offset;
    int32_t  pole_pairs;
    int32_t  direction;
    int32_t  offset_lut[128];
    uint32_t has_cogging; // 1=第62页存有有效 cogging_map
    uint32_t _pad;        // 凑8字节对齐, 保证 checksum 是最后一字(否则aligned(8)补的填充会破坏校验)
    uint32_t checksum;    // 前面所有字的和
} MotorCalib_t;

static uint32_t calib_checksum(const MotorCalib_t *c)
{
    const uint32_t *p = (const uint32_t *)c;
    uint32_t s = 0;
    for (uint32_t i = 0; i < (sizeof(MotorCalib_t) / 4) - 1; i++)
        s += p[i];
    return s;
}

bool MotorCalib_LoadFromFlash(MOTOR_DATA *motor)
{
    static MotorCalib_t c __attribute__((aligned(8)));
    flash_read_data(CALIB_ADDR, (uint32_t *)&c, sizeof(c));

    if (c.magic != MOTOR_CALIB_MAGIC) return false;
    if (calib_checksum(&c) != c.checksum) return false;
    if (c.pole_pairs != MOTOR_EXPECTED_POLE_PAIRS) return false;

    motor->parameters.Rs   = c.Rs;
    motor->parameters.Ls   = c.Ls;
    motor->parameters.flux = c.flux;
    motor->components.encoder->encoder_offset = c.encoder_offset;
    motor->components.encoder->pole_pairs     = (uint8_t)c.pole_pairs;
    motor->components.encoder->dir            = c.direction;
    memcpy(motor->components.encoder->offset_lut, c.offset_lut, sizeof(c.offset_lut));
    motor->components.encoder->calib_valid = true;

    if (c.has_cogging) // 第62页存有 cogging_map → 加载并启用前馈
    {
        flash_read_data(COG_ADDR, (uint32_t *)cogging_map, sizeof(cogging_map));
        cogging_valid = 1;
    }
    return true;
}

// 仅加载 cogging_map (供 PRE_CALIBRATED=1 用: 电机参数走编译期常数, 但齿槽图从flash读, 使FF开机即生效)。
// 校验 page63 的 magic+checksum+has_cogging 有效后才读 page62 的 map。
bool MotorCalib_LoadCoggingOnly(void)
{
    static MotorCalib_t c __attribute__((aligned(8)));
    flash_read_data(CALIB_ADDR, (uint32_t *)&c, sizeof(c));
    if (c.magic != MOTOR_CALIB_MAGIC)     return false;
    if (calib_checksum(&c) != c.checksum) return false;
    if (c.pole_pairs != MOTOR_EXPECTED_POLE_PAIRS) return false;
    if (!c.has_cogging)                   return false;
    flash_read_data(COG_ADDR, (uint32_t *)cogging_map, sizeof(cogging_map));
    cogging_valid = 1;
    return true;
}

void MotorCalib_SaveToFlash(MOTOR_DATA *motor)
{
    static MotorCalib_t c __attribute__((aligned(8)));
    c.magic          = MOTOR_CALIB_MAGIC;
    c.Rs             = motor->parameters.Rs;
    c.Ls             = motor->parameters.Ls;
    c.flux           = motor->parameters.flux;
    c.encoder_offset = motor->components.encoder->encoder_offset;
    c.pole_pairs     = motor->components.encoder->pole_pairs;
    c.direction      = motor->components.encoder->dir;
    memcpy(c.offset_lut, motor->components.encoder->offset_lut, sizeof(c.offset_lut));
    c.has_cogging    = cogging_valid;
    c.checksum       = calib_checksum(&c);

    flash_erase_page(CALIB_PAGE);          // 注意: 擦写期间 CPU 停转~40ms, 调用前须关 PWM
    flash_write_data(CALIB_ADDR, &c, sizeof(c));

    if (cogging_valid) // cogging_map 存第62页(整页2KB)
    {
        flash_erase_page(COG_PAGE);
        flash_write_data(COG_ADDR, cogging_map, sizeof(cogging_map));
    }
}

void MotorCalib_Erase(void)
{
    flash_erase_page(CALIB_PAGE);
}
