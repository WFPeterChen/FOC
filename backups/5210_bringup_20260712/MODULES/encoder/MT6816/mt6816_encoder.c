#include "mt6816_encoder.h"

/**
 * @brief ��������ʼ������
 *
 * �˺������ڳ�ʼ����������ز����� SPI ���衣
 *
 * @param None
 */
ENCODER_DATA encoder_data = {
    .hspi                 = &MT6816_SPI_Get_HSPI, // ָ�� SPI �����ָ��
    .CS_Port              = SPI1_CS_GPIO_Port,    // Ƭѡ���ŵĶ˿�
    .CS_Pin               = SPI1_CS_Pin,          // Ƭѡ���ŵ����ź�
    .pole_pairs           = MPTOR_P,              // ���ü�����Ϊ�������
    .encoder_offset       = MOTOR_OFFSET,         // ���õ�Ƕ�ƫ����
    .dir                  = MOTOR_DIRECTION,      // ���ñ�������ת����Ϊ˳ʱ��
    .raw                  = 0,                    // ��ʼ��ԭʼ����ֵΪ0
    .cnt                  = 0,                    // ������
    .theta_acc            = 0.01f,                // ���ýǶ��ۼӲ���Ϊ0.01����
    .count_in_cpr_        = 0,                    // ԭʼֵ�������ͣ�
    .shadow_count_        = 0,                    // ԭʼֵ�ۼӼ����������ͣ�
    .pos_estimate_counts_ = 0.0f,                 // ԭʼֵ�ۼӼ����������ͣ�
    .vel_estimate_counts_ = 0.0f,                 // ԭʼֵת��
    .pos_cpr_counts_      = 0.0f,                 // ԭʼֵ�������ͣ�
    .pos_estimate_        = 0.0f,                 // Ȧ��
    .pos_zero_offset      = 0.0f,                 // SET_ZERO 偏移
    .vel_estimate_        = 0.0f,                 // ���ٶ�rad/s
    .pos_cpr_             = 0.0f,                 // 0-1
    .phase_               = 0.0f,                 // ��Ƕ�
    .interpolation_       = 0.0f,                 // ��ֵϵ��
    .elec_angle           = 0.0f,                 // ��Ƕ�
    .mec_angle            = 0.0f,                 // ��е�Ƕ�
    .calib_valid          = false,                // ��ʾУ׼�������Ƿ���Ч
};

/**
 * @brief ���Ƕȹ�һ���� [0, 2��] ��Χ��
 *
 * �˺����������ĽǶ�ֵ��һ���� [0, 2��] ��Χ�ڡ�
 *
 * @param angle Ҫ��һ���ĽǶ�
 */
float normalize_angle(float angle)
{
    float a = fmod(angle, M_2PI);    // ʹ��ȡ��������й�һ��
    return a >= 0 ? a : (a + M_2PI); // ȷ������ֵ�� [0, 2��] ��Χ��
}

/**
 * @brief �������������
 *
 * �˺���ͨ�� SPI �ӿڷ��ͺͽ���һ���ֽڵ����ݡ�
 *
 * @param txdata ָ��Ҫ���͵����ݵ�ָ��
 * @param rxdata ָ��������ݵ�ָ��
 */
bool MT6816_read_write_byte(uint8_t *txdata, uint8_t *rxdata)
{
    MT6816_SPI_CS_L();
    bool transmitResult = (HAL_OK == HAL_SPI_Transmit(&MT6816_SPI_Get_HSPI, txdata, 1, MT6816_MAX_DELAY));
    bool receiveResult  = (HAL_OK == HAL_SPI_Receive(&MT6816_SPI_Get_HSPI, rxdata, 1, MT6816_MAX_DELAY));
    MT6816_SPI_CS_H();
    return transmitResult && receiveResult;
}

/**
 * @brief �� MT6816 ��������ȡԭʼ�Ƕ�����
 *
 * �˺�����ȡ MT6816 ��������ԭʼ�Ƕ�ֵ
 *
 * @param encoder ָ��������״̬�Ϳ������ݵĽṹ��ָ��
 */
// ===== 寄存器级 DMA 读(绕开 HAL 经典SPI 走不完的 TxRx-DMA 完成路径): FOC ISR 只"启动+取上拍结果", 不阻塞。
//   两帧链(每寄存器独立CS): 帧0(RX=Ch5/TX=Ch6, 读reg0x83) → Ch5-TC中断→翻CS起帧1(读0x84) → 组装16位原始角。
//   DMAMUX/通道基配置由 spi.c 的 HAL_DMA_Init 已设(Ch5=SPI1_RX/Ch6=SPI1_TX)。
#define MT_SPI (MT6816_SPI_Get_HSPI.Instance)
static volatile uint8_t  mt_state = 0;       // 0=空闲 1=预热帧(丢) 2=角度帧 3=告警帧
static uint8_t  mt_t0[2] = {MT6816_Angle_Reg, 0}, mt_r0[2];
static uint8_t  mt_t1[2] = {MT6816_Warning_Reg, 0}, mt_r1[2];
static uint8_t  mt_rd[2];                     // 预热帧丢弃缓冲
static volatile uint16_t mt_raw16 = 0;
static volatile uint8_t  mt_valid = 0;
volatile uint8_t g_mt_parity_bypass = 1;     // 诊断标志: 1=奇偶校验错也接受读数(便于验证DMA角度是否跟随旋转)
// ---- A/B 诊断: 同一静止位置对比阻塞读与同步DMA读的原始16位 ----
volatile uint16_t g_diag_block = 0;          // 阻塞读原始值(基准, 偶校验)
volatile uint16_t g_diag_dma   = 0;          // 同步DMA读原始值(待比对)
volatile uint8_t  g_diag_run   = 0;          // openocd 置1触发一次对比, 完成后固件自清0
static inline void mt_csh_delay(void) { uint32_t t = DWT->CYCCNT; while (DWT->CYCCNT - t < 100u) {} } // ~600ns CS高

static void mt_dma_frame(uint8_t *tx, uint8_t *rx)   // 启一帧(2字节 全双工 DMA)
{
    DMA1_Channel5->CCR = 0; DMA1_Channel6->CCR = 0;                    // 先关
    CLEAR_BIT(MT_SPI->CR1, SPI_CR1_SPE);                              // 冲 RX FIFO 残留(见同步版说明)
    while (MT_SPI->SR & SPI_SR_FRLVL) { volatile uint8_t d = *(volatile uint8_t *)&MT_SPI->DR; (void)d; }
    DMA1->IFCR = DMA_IFCR_CGIF5 | DMA_IFCR_CGIF6;
    DMA1_Channel5->CPAR = (uint32_t)&MT_SPI->DR; DMA1_Channel5->CMAR = (uint32_t)rx; DMA1_Channel5->CNDTR = 2;
    DMA1_Channel6->CPAR = (uint32_t)&MT_SPI->DR; DMA1_Channel6->CMAR = (uint32_t)tx; DMA1_Channel6->CNDTR = 2;
    DMA1_Channel5->CCR = DMA_CCR_MINC | DMA_CCR_TCIE | DMA_CCR_EN;     // RX: 外设→内存, byte, 完成中断
    DMA1_Channel6->CCR = DMA_CCR_MINC | DMA_CCR_DIR  | DMA_CCR_EN;     // TX: 内存→外设, byte
    MT6816_SPI_CS_L();
    SET_BIT(MT_SPI->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    SET_BIT(MT_SPI->CR1, SPI_CR1_SPE);
}

void DMA1_Channel5_MT_ISR(void)   // RX-TC: it.c 的 DMA1_Channel5_IRQHandler 调本函数
{
    DMA1->IFCR = DMA_IFCR_CGIF5;                                       // 清 Ch5 全部标志
    DMA1_Channel5->CCR = 0; DMA1_Channel6->CCR = 0;                    // 关通道
    CLEAR_BIT(MT_SPI->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    uint32_t g = 0; while ((MT_SPI->SR & SPI_SR_BSY) && ++g < 2000u) {} // BSY 应立即清(有界防卡)
    CLEAR_BIT(MT_SPI->CR1, SPI_CR1_SPE);
    MT6816_SPI_CS_H();
    // 预热帧(state1)冷启会丢高字节, 丢弃后再读角度; 全程在后台DMA中断链完成, 不占FOC ISR
    if      (mt_state == 1) { mt_csh_delay(); mt_state = 2; mt_dma_frame(mt_t0, mt_r0); }  // 预热完→读角度
    else if (mt_state == 2) { mt_csh_delay(); mt_state = 3; mt_dma_frame(mt_t1, mt_r1); }  // 角度完→读告警
    else                    { mt_raw16 = ((uint16_t)mt_r0[1] << 8) | mt_r1[1]; mt_valid = 1; mt_state = 0; }
}

// ===== 读路径开关 =====================================================================
//  A/B诊断(g_diag_run)证实: 同步DMA读与阻塞读同一时刻 bit 完全一致(0xd380 偶校验) → DMA传输本身正确。
//  之前"运动下抖动"的锅在 MODE 1 的中断链状态机(两帧异步链 与 20kHz FOC ISR 竞态), 不在 SPI/DMA 位时序。
//  === 结论(2026-07-02, 已完整定位, 详见 [[falconfoc-encoder-dma-wip]]) ===
//  DMA传输本身正确(A/B诊断 sync-DMA 与阻塞读同一时刻 bit 完全一致 0xd380)。
//  "高字节丢成0x00"根因 = 冷启第一帧(SPE空闲后)丢首个数据字节, 用一个丢弃的"预热帧"可修。
//  但两种 DMA 读法都无净收益, 故默认仍用阻塞读:
//   MODE 1 (异步链, ISR~21µs 最省): 流水线使角度延迟 1~2 拍(50~100µs), 20kHz电流环下高速换相滞后 → OC。
//                                   角度延迟对 FOC 是硬伤, 不适用。
//   MODE 2 (同步+预热, ISR~29µs): 角度实时正确(6/7), 但预热帧吃掉了提速, 不比阻塞快。
//   MODE 0 (HAL阻塞, ISR~30µs)   : 出厂路径, 角度实时, 功能完好 7/7 —— 默认。
//  另: 想省 ISR 的原始动机(91%CPU 饿死 FDCAN/时基/deadman)已由中断优先级修复独立解决, DMA 非必需。
#define MT6816_READ_MODE 3

// 同步DMA读一帧(2字节 全双工, 忙等完成, 不依赖中断链) —— 供 A/B 诊断复现 DMA 机制本身的读值
static void mt_dma_frame_sync(uint8_t *tx, uint8_t *rx)
{
    DMA1_Channel5->CCR = 0; DMA1_Channel6->CCR = 0;
    // 关键: 冲掉上一帧遗留在 RX FIFO 的字节。否则残留字节会把本帧读数整体错位一字节
    //   (表现: 第一次读正确, 之后每次高字节丢成0x00) —— 这正是同步/异步DMA读失败的根因。
    CLEAR_BIT(MT_SPI->CR1, SPI_CR1_SPE);
    while (MT_SPI->SR & SPI_SR_FRLVL) { volatile uint8_t d = *(volatile uint8_t *)&MT_SPI->DR; (void)d; }
    DMA1->IFCR = DMA_IFCR_CGIF5 | DMA_IFCR_CGIF6;
    DMA1_Channel5->CPAR = (uint32_t)&MT_SPI->DR; DMA1_Channel5->CMAR = (uint32_t)rx; DMA1_Channel5->CNDTR = 2;
    DMA1_Channel6->CPAR = (uint32_t)&MT_SPI->DR; DMA1_Channel6->CMAR = (uint32_t)tx; DMA1_Channel6->CNDTR = 2;
    DMA1_Channel5->CCR = DMA_CCR_MINC | DMA_CCR_EN;
    DMA1_Channel6->CCR = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_EN;
    MT6816_SPI_CS_L();
    SET_BIT(MT_SPI->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    SET_BIT(MT_SPI->CR1, SPI_CR1_SPE);
    uint32_t g = 0; while (DMA1_Channel5->CNDTR && ++g < 100000u) {}   // 等 RX-DMA 搬完2字节
    while ((MT_SPI->SR & SPI_SR_BSY) && ++g < 200000u) {}
    DMA1_Channel5->CCR = 0; DMA1_Channel6->CCR = 0;
    CLEAR_BIT(MT_SPI->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    CLEAR_BIT(MT_SPI->CR1, SPI_CR1_SPE);
    MT6816_SPI_CS_H();
}
__attribute__((unused)) static uint16_t mt6816_dma_read_sync(void)
{
    uint8_t r0[2] = {0}, r1[2] = {0}, rd[2] = {0};
    uint8_t t0[2] = {MT6816_Angle_Reg, 0}, t1[2] = {MT6816_Warning_Reg, 0};
    mt_dma_frame_sync(t0, rd);                 // 预热帧(丢弃): 冷启第一帧高字节会丢, 先空跑一帧
    mt_csh_delay();
    mt_dma_frame_sync(t0, r0);
    mt_csh_delay();
    mt_dma_frame_sync(t1, r1);
    return ((uint16_t)r0[1] << 8) | r1[1];
}

// 校验+落角度(三种读法共用): rawAngle 偶校验通过则更新 encoder->angle 并返回true
static bool mt6816_apply_raw(ENCODER_DATA *encoder, uint16_t rawAngle, bool spi_ok)
{
    if (!spi_ok) { if (encoder->rx_err_count < 0xFF) encoder->rx_err_count++; return false; }
    if (encoder->rx_err_count) encoder->rx_err_count--;
    uint8_t h_count = 0;
    for (uint8_t j = 0; j < 16; j++) if (rawAngle & (0x01u << j)) h_count++;
    if (h_count & 0x01) { if (encoder->check_err_count < 0xFF) encoder->check_err_count++; return false; }
    if (encoder->check_err_count) encoder->check_err_count--;
    encoder->angle = rawAngle >> 2;
    return true;
}

// 寄存器级读用的 CS 建立/保持短延时(tCSS/tCSH, ~几百ns), volatile 循环防优化掉
static inline void mt3_cs_delay(void) { for (volatile uint32_t _i = 0; _i < 6; _i++) { } }

bool mt6816_read_raw(ENCODER_DATA *encoder)
{
#if MT6816_READ_MODE == 1
    uint16_t rawAngle = mt_raw16;
    uint8_t  had = mt_valid;
    static uint8_t stall = 0;                 // 看门狗: 传输没在几拍内完成→非阻塞复位
    if (mt_state != 0) { if (++stall > 6) { DMA1_Channel5->CCR = 0; DMA1_Channel6->CCR = 0; CLEAR_BIT(MT_SPI->CR2, SPI_CR2_RXDMAEN|SPI_CR2_TXDMAEN); CLEAR_BIT(MT_SPI->CR1, SPI_CR1_SPE); MT6816_SPI_CS_H(); mt_state = 0; stall = 0; } }
    else stall = 0;
    if (mt_state == 0) { mt_state = 1; mt_dma_frame(mt_t0, mt_rd); }   // 启预热帧(后台链完成: 预热→角度→告警)
    if (!had) { if (encoder->rx_err_count < 0xFF) encoder->rx_err_count++; return false; }
    return mt6816_apply_raw(encoder, rawAngle, true);

#elif MT6816_READ_MODE == 2
    // 同步DMA读: 忙等~4µs, 已验证 bit 与阻塞读一致, 无中断链竞态
    uint16_t rawAngle = mt6816_dma_read_sync();
    g_diag_dma = rawAngle;                    // 诊断: 记录最近一次原始读值
    uint8_t hc = 0; for (uint8_t j = 0; j < 16; j++) if (rawAngle & (0x01u << j)) hc++;
    if (hc & 0x01) g_diag_block++;            // 诊断: 累计奇偶校验失败次数
    return mt6816_apply_raw(encoder, rawAngle, true);

#elif MT6816_READ_MODE == 3
    // 寄存器级阻塞读: 绕开HAL开销(~12.7µs→~5µs), 仍同步实时、无DMA延迟。
    // SPI3=Mode3/8bit/MSB, FRXTH=1(HAL_Init已配); 每寄存器独立CS, 取第2返回字节。
    volatile uint8_t *pdr = (volatile uint8_t *)&MT_SPI->DR;
    SET_BIT(MT_SPI->CR1, SPI_CR1_SPE);                      // 确保SPE开(HAL不再管)
    while (MT_SPI->SR & SPI_SR_FRLVL) { (void)*pdr; }       // 清残留RX
    bool ok = true; uint32_t to; uint8_t hi = 0, lo = 0;
    // 事务1: 角度高字节(reg 0x83)
    MT6816_SPI_CS_L(); mt3_cs_delay();
    *pdr = MT6816_Angle_Reg; *pdr = 0x00;
    to = 0; while (!(MT_SPI->SR & SPI_SR_RXNE) && ++to < 4000u); (void)*pdr;               // 丢首字节
    to = 0; while (!(MT_SPI->SR & SPI_SR_RXNE) && ++to < 4000u); hi = *pdr; ok &= (to < 4000u);
    to = 0; while ((MT_SPI->SR & SPI_SR_BSY)  && ++to < 4000u);
    MT6816_SPI_CS_H(); mt3_cs_delay();
    // 事务2: 角度低字节(reg 0x84)
    MT6816_SPI_CS_L(); mt3_cs_delay();
    *pdr = MT6816_Warning_Reg; *pdr = 0x00;
    to = 0; while (!(MT_SPI->SR & SPI_SR_RXNE) && ++to < 4000u); (void)*pdr;
    to = 0; while (!(MT_SPI->SR & SPI_SR_RXNE) && ++to < 4000u); lo = *pdr; ok &= (to < 4000u);
    to = 0; while ((MT_SPI->SR & SPI_SR_BSY)  && ++to < 4000u);
    MT6816_SPI_CS_H();
    uint16_t rawAngle = ((uint16_t)hi << 8) | lo;
    return mt6816_apply_raw(encoder, rawAngle, ok);

#else
    // HAL阻塞全双工读(出厂路径): 每寄存器独立CS, 数据在第2字节返回, raw=(hi<<8)|lo
    uint8_t t0[2] = {MT6816_Angle_Reg, 0x00}, r0[2] = {0};
    uint8_t t1[2] = {MT6816_Warning_Reg, 0x00}, r1[2] = {0};
    MT6816_SPI_CS_L();
    bool ok0 = (HAL_OK == HAL_SPI_TransmitReceive(&MT6816_SPI_Get_HSPI, t0, r0, 2, MT6816_MAX_DELAY));
    MT6816_SPI_CS_H();
    MT6816_SPI_CS_L();
    bool ok1 = (HAL_OK == HAL_SPI_TransmitReceive(&MT6816_SPI_Get_HSPI, t1, r1, 2, MT6816_MAX_DELAY));
    MT6816_SPI_CS_H();
    uint16_t rawAngle = ((uint16_t)r0[1] << 8) | r1[1];
    bool ok = mt6816_apply_raw(encoder, rawAngle, ok0 && ok1);
    if (g_diag_run) { g_diag_block = rawAngle; g_diag_dma = mt6816_dma_read_sync(); g_diag_run = 0; }
    return ok;
#endif
}

/**
 * @brief ����ȡ��ԭʼֵת����ʵ�ʽǶȺ͵�Ƕ�
 *
 * �˺�������ȡ��ԭʼֵת����ʵ�ʽǶȺ͵�Ƕȡ�
 *
 * @param encoder ָ��������״̬�Ϳ������ݵĽṹ��ָ��
 */
// PLL 首次装载标志; 上电=false, 标定/flash加载切换 dir/offset/LUT 后由 Encoder_ResetPLL() 复位以重新装载。
static bool s_pll_primed = false;
void Encoder_ResetPLL(void) { s_pll_primed = false; }

void GetMotor_Angle(ENCODER_DATA *encoder)
{
    static const float pll_kp_ = 2.0f * ENCODER_PLL_BANDWIDTH;
    static const float pll_ki_ = 0.25f * SQ(pll_kp_);
    static const float snap_threshold = 0.5f * CURRENT_MEASURE_PERIOD * pll_ki_;
    bool _spi_ok = mt6816_read_raw(encoder);
    if (_spi_ok)
    {
        if (encoder->dir == CW)
        {
            encoder->raw = encoder->angle;
        }
        else
        {
            encoder->raw = (ENCODER_CPR - encoder->angle);
        }
    }

    // offset compensation
    int off_1      = encoder->offset_lut[encoder->raw >> 7];
    int off_2      = encoder->offset_lut[((encoder->raw >> 7) + 1) % 128];
    int off_interp = off_1 + ((off_2 - off_1) * (encoder->raw - ((encoder->raw >> 7) << 7)) >> 7); // Interpolate between lookup table entries
    int cnt        = encoder->raw - off_interp;                                                           // Correct for nonlinearity with lookup table from calibration
    if (cnt >= ENCODER_CPR) // 原为 >, 漏掉 cnt==CPR 的边界(会留在 16384 越界); >= 才正确回绕到 [0,CPR-1]
    {
        cnt -= ENCODER_CPR;
    }
    else if (cnt < 0)
    {
        cnt += ENCODER_CPR;
    }
    encoder->cnt   = cnt;

    // 首次有效采样(及标定/flash加载完成后重置时): 把 PLL/计数器装入当前真实位置。否则:
    // ①上电从0起跳首拍 delta≈整圈计数; ②标定/flash加载会换 dir/encoder_offset/offset_lut,
    // 使 cnt 一拍跳变几十~上百计数。两者都会把 PLL 速度积分器猛踢出巨大尖峰 → 静止也误触发 SPEEDING。
    if (!s_pll_primed)
    {
        encoder->count_in_cpr_        = encoder->cnt;
        encoder->shadow_count_        = encoder->cnt;
        encoder->pos_estimate_counts_ = (float)encoder->cnt;
        encoder->pos_cpr_counts_      = (float)encoder->cnt;
        encoder->vel_estimate_counts_ = 0.0f;
        s_pll_primed = true;
    }

    int delta_enc  = encoder->cnt - encoder->count_in_cpr_;
    delta_enc      = mod(delta_enc, ENCODER_CPR);
    if (delta_enc > ENCODER_CPR_DIV)
    {
        delta_enc -= ENCODER_CPR;
    }

    encoder->shadow_count_ += delta_enc;       // 多圈累计(给 pos_estimate)
    encoder->count_in_cpr_  = encoder->cnt;    // 单圈计数 = 已校正 raw。(原来这之前还有 +=delta+mod 两行是死代码, 被本行覆盖, 已删)

    //// run pll (for now pll is in units of encoder counts)
    // Predict current pos
    encoder->pos_estimate_counts_ += CURRENT_MEASURE_PERIOD * encoder->vel_estimate_counts_;
    encoder->pos_cpr_counts_      += CURRENT_MEASURE_PERIOD * encoder->vel_estimate_counts_;
    // discrete phase detector
    float delta_pos_counts         = (float)(encoder->shadow_count_ - (int32_t)floor(encoder->pos_estimate_counts_));
    float delta_pos_cpr_counts     = (float)(encoder->count_in_cpr_ - (int32_t)floor(encoder->pos_cpr_counts_));
    delta_pos_cpr_counts           = wrap_pm(delta_pos_cpr_counts, ENCODER_CPR_DIV);
    // pll feedback
    encoder->pos_estimate_counts_ += CURRENT_MEASURE_PERIOD * pll_kp_ * delta_pos_counts;
    encoder->pos_cpr_counts_      += CURRENT_MEASURE_PERIOD * pll_kp_ * delta_pos_cpr_counts;
    encoder->pos_cpr_counts_       = fmodf_pos(encoder->pos_cpr_counts_, ENCODER_CPR_F);
    encoder->vel_estimate_counts_ += CURRENT_MEASURE_PERIOD * pll_ki_ * delta_pos_cpr_counts;
    bool snap_to_zero_vel          = false;
    if (ABS(encoder->vel_estimate_counts_) < snap_threshold) // 100
    {
        encoder->vel_estimate_counts_ = 0.0f; // align delta-sigma on zero to prevent jitter
        snap_to_zero_vel = true;
    }

    // Outputs from Encoder for Controller
    encoder->pos_estimate_ = encoder->pos_estimate_counts_ / ENCODER_CPR_F - encoder->pos_zero_offset; // SET_ZERO 偏移(默认0)
    encoder->vel_estimate_ = encoder->vel_estimate_counts_ / ENCODER_CPR_F;
    encoder->pos_cpr_      = encoder->pos_cpr_counts_ / ENCODER_CPR_F;

    //// run encoder count interpolation
    int32_t corrected_enc  = encoder->count_in_cpr_ - encoder->encoder_offset;
    // if we are stopped, make sure we don't randomly drift
    if (snap_to_zero_vel)
    {
        encoder->interpolation_ = 0.5f;
        // reset interpolation if encoder edge comes
        // TODO: This isn't correct. At high velocities the first phase in this count may very well not be at the edge.
    }
    else if (delta_enc > 0)
    {
        encoder->interpolation_ = 0.0f;
    }
    else if (delta_enc < 0)
    {
        encoder->interpolation_ = 1.0f;
    }
    else
    {
        // Interpolate (predict) between encoder counts using vel_estimate,
        encoder->interpolation_ += CURRENT_MEASURE_PERIOD * encoder->vel_estimate_counts_;
        // don't allow interpolation indicated position outside of [enc, enc+1)
        if (encoder->interpolation_ > 1.0f)
            encoder->interpolation_ = 1.0f;
        if (encoder->interpolation_ < 0.0f)
            encoder->interpolation_ = 0.0f;
    }
    float interpolated_enc = corrected_enc + encoder->interpolation_;

    //// compute electrical phase
    float elec_rad_per_enc = encoder->pole_pairs * M_2PI * (1.0f / ENCODER_CPR_F);
    float ph               = elec_rad_per_enc * interpolated_enc;
    encoder->phase_        = wrap_pm_pi(ph); // ��Ƕ�
    encoder->mec_angle     = normalize_angle(corrected_enc * (360.0f / ENCODER_CPR_F) * (M_PI / 180.0f));
    encoder->elec_angle    = normalize_angle(encoder->pole_pairs * encoder->mec_angle); // ��Ƕ�
}

/**
 * @brief �Ƕ���������
 *
 * ���ݵ������ת������ٶȣ����㲢���µ���ĵ�Ƕȡ�
 *
 * @param encoder ָ�������ݽṹ��ָ��
 */
void Theta_ADD(ENCODER_DATA *encoder)
{
    encoder->elec_angle    += encoder->theta_acc;
    if (encoder->elec_angle > M_2PI)
        encoder->elec_angle = 0.0f;
    else if (encoder->elec_angle < 0)
        encoder->elec_angle = M_2PI;
}
