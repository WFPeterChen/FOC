/********************************************************************************
 * cogging.c —— 齿槽转矩标定与前馈补偿 (开环寄生版)
 * 寄生在编码器标定(CS_ENCODER)的开环电压匀速旋转上记录 iq vs 机械角:
 *   CW 一圈累加到 cogging_map, CCW 一圈累加到 friction_map(临时复用)。
 *   完成: cogging_map=(cw+ccw)/2(齿槽前馈电流), friction_map=(cw-ccw)/2(摩擦)。
 * 开环旋转无 PID 振荡, 故 iq 干净反映负载=齿槽+摩擦。
 * 补偿: 控制环 iq_set += cogging_map[raw>>SHIFT]。
 ********************************************************************************/
#include "cogging.h"
#include "general_def.h"
#include "motor_calib.h" // 标定结束写flash
#include "string.h"

// 标定期间 cogging_map 累加CW, friction_map 累加CCW, 完成时就地算出最终值(省RAM)
float   cogging_map[COGGING_N] __attribute__((aligned(8))); // 8字节对齐: flash双字写要求
float   friction_map[COGGING_N];
uint8_t cogging_valid = 0;

static uint16_t cw_cnt[COGGING_N], ccw_cnt[COGGING_N];

void Cogging_Reset(void)
{
    memset(cogging_map, 0, sizeof(cogging_map));
    memset(friction_map, 0, sizeof(friction_map));
    memset(cw_cnt, 0, sizeof(cw_cnt));
    memset(ccw_cnt, 0, sizeof(ccw_cnt));
    cogging_valid = 0;
}

// 编码器标定 CW 循环里每周期调用: 累加 iq 到当前机械角 bin
void Cogging_RecordCW(MOTOR_DATA *m)
{
    uint16_t b = m->components.encoder->raw >> COGGING_SHIFT;
    cogging_map[b] += m->components.foc->i_q;
    cw_cnt[b]++;
}

void Cogging_RecordCCW(MOTOR_DATA *m)
{
    uint16_t b = m->components.encoder->raw >> COGGING_SHIFT;
    friction_map[b] += m->components.foc->i_q;
    ccw_cnt[b]++;
}

// 编码器标定结束时调用: 就地算出 cogging(前馈) 和 friction
void Cogging_Compute(void)
{
    for (int i = 0; i < COGGING_N; i++)
    {
        float cw  = cw_cnt[i] ? cogging_map[i] / cw_cnt[i] : 0.0f;
        float ccw = ccw_cnt[i] ? friction_map[i] / ccw_cnt[i] : 0.0f;
        cogging_map[i]  = 0.5f * (cw + ccw); // 位置相关 = 齿槽前馈电流
        friction_map[i] = 0.5f * (cw - ccw); // 方向性 = 摩擦
    }
    cogging_valid = 1;
}

float Cogging_FF(uint16_t raw)
{
    if (!cogging_valid) return 0.0f;
    // 线性插值(原为最近邻零阶保持→512个bin边界每转跳变, 实测相邻台阶最大0.11A→力矩台阶→"哒"声)。
    // 像编码器 offset_lut 那样在相邻 bin 间插值, FF 电流连续无台阶。COGGING_N=512(2幂)→ &(N-1) 圆周回绕。
    uint16_t i = raw >> COGGING_SHIFT;                  // bin 索引 0..N-1
    uint16_t f = raw & ((1u << COGGING_SHIFT) - 1u);    // bin 内小数 0..(2^SHIFT-1)
    float a = cogging_map[i];
    float b = cogging_map[(i + 1u) & (COGGING_N - 1u)]; // 下一 bin, 圆周回绕
    return a + (b - a) * ((float)f * (1.0f / (float)(1u << COGGING_SHIFT)));
}

/********************************************************************************
 * 闭环逐点静态保持标定 —— 零速保持电流 = 纯齿槽扰动(无环路相位滞后), 最适合做前馈
 * 位置环把转子*停*在一系列离散角度, 每点等速度≈0后取保持电流均值入 bin:
 *   零速时 Kt·i_hold = -T_cog(θ) ⇒ i_hold = -T_cog/Kt = 纯齿槽补偿电流
 *   (不像匀速扫那样记的是"速度环带相位滞后的反应", 那个当 FF 反而使纹波翻倍, 已验证不行)。
 *   CW(从+approach)入 cw_resid, CCW(从-approach)入 friction_map ⇒ (cw+ccw)/2抵消静摩擦得纯齿槽, (cw-ccw)/2=静摩擦。
 * 前提: 电流环稳定(早先 hunting 是 flash 里 Ls 坏致电流环增益20×, 已修)。
 * 触发: g_cog_cal_trigger=2 清零重测, =1 在现有 map 上迭代。每控制拍调 Update。
 ********************************************************************************/
// 极限质量版(2026-06-27): 每点收敛到目标+静止后保持1s, 取1s(2万样)电流均值压逐bin噪声; 阈值收紧到~0.1bin;
// 收不进死区的点用"静止期间均值"兜底; 两向都无样的空洞圆周线性插值; g_cogcal_holes报空洞数。
#define COGCAL_SETTLE_VEL    0.03f  // turn/s, |vel|低于此视为已静止(编码器<0.006turn/s会snap到0)
#define COGCAL_POS_THR  (0.50f / (float)COGGING_N) // turn, |目标-实际|<0.5bin算收敛(0.1bin<静摩擦死区→永远收不敛+每点超时, 0.5bin可达→真收敛+均匀)
#define COGCAL_SETTLE_TICKS  20000u // 收敛后再保持1s墙钟(@20kHz)→取该窗内静止样均值(~2万样, 降噪~140×)
#define COGCAL_TIMEOUT_TICKS 60000u // 单点最长3s(给积分蠕动到位; 超时用静止期间累计均值兜底)
#define COGCAL_VEL_CLAMP     0.5f   // 位置环 vel_set 钳位(点间移动限速)

typedef enum { COGCAL_IDLE = 0, COGCAL_CW, COGCAL_CCW, COGCAL_FINISH } cogcal_phase_t;

// 进度量(非static, 便于SWD观察): phase 0空闲/1正扫/2反扫/3收尾, step 0..COGGING_N(点序号)
volatile uint8_t  g_cogcal_phase = COGCAL_IDLE;
volatile uint16_t g_cogcal_step  = 0;
volatile uint16_t g_cogcal_holes = 0; // 两向都无样的bin数(插值前); /512=空洞率, SWD读

static float    cw_resid[COGGING_N]; // CW 保持电流累加(friction_map 作 CCW 累加)
static float    cogcal_base;         // 本向扫描起点位置(turn, 多圈连续)
static uint32_t cogcal_settle;       // 连续"收敛+静止"拍计数(达1s→取样)
static uint32_t cogcal_timer;        // 本点总拍计数(超时保护)
static double   cogcal_iq_acc;       // 静止期间 iq 累加(2万样, double防精度损失)
static uint32_t cogcal_iq_n;         // 累加样本数

#define COG_FF_SIGN (+1.0f) // 物理推导符号(静态测量无环路滞后, iq_set+=map 抵消齿槽)

// 把保持电流 iq 记入当前 raw 所在 bin(收敛后 raw≈目标, 落点正确)
static void cogcal_record(MOTOR_DATA *m, float iq)
{
    uint16_t bin = (uint16_t)m->components.encoder->raw >> COGGING_SHIFT;
    if (g_cogcal_phase == COGCAL_CW) { cw_resid[bin]    += iq; cw_cnt[bin]++;  }
    else                             { friction_map[bin] += iq; ccw_cnt[bin]++; }
}

// 结束: 每bin算均值→齿槽/摩擦; 空洞圆周插值; 去直流; 累加进 map; 存 flash。
static void cogcal_finish(MOTOR_DATA *m)
{
    for (int i = 0; i < COGGING_N; i++) // 1) 每bin均值 → 齿槽(暂存cw_resid)/摩擦(friction_map)
    {
        float cw  = cw_cnt[i]  ? cw_resid[i]     / cw_cnt[i]  : 0.0f;
        float ccw = ccw_cnt[i] ? friction_map[i] / ccw_cnt[i] : 0.0f;
        cw_resid[i]     = 0.5f * (cw + ccw);
        friction_map[i] = 0.5f * (cw - ccw);
    }
    uint16_t holes = 0; // 2) 空洞(两向都无样)圆周线性插值
    for (int i = 0; i < COGGING_N; i++) if (!cw_cnt[i] && !ccw_cnt[i]) holes++;
    g_cogcal_holes = holes;
    if (holes && holes < COGGING_N)
        for (int i = 0; i < COGGING_N; i++)
        {
            if (cw_cnt[i] || ccw_cnt[i]) continue;          // 有样, 跳过
            int dl, dr, l = i, r = i;
            for (dl = 1; dl < COGGING_N; dl++) { int j = (i - dl + COGGING_N) % COGGING_N; if (cw_cnt[j] || ccw_cnt[j]) { l = j; break; } }
            for (dr = 1; dr < COGGING_N; dr++) { int j = (i + dr) % COGGING_N;             if (cw_cnt[j] || ccw_cnt[j]) { r = j; break; } }
            float t = (float)dl / (float)(dl + dr);
            cw_resid[i]     = cw_resid[l]     + t * (cw_resid[r]     - cw_resid[l]);
            friction_map[i] = friction_map[l] + t * (friction_map[r] - friction_map[l]);
        }
    float sum = 0.0f; // 3) 去直流(电流采样偏置, 齿槽应零均值) + 累加进 map
    for (int i = 0; i < COGGING_N; i++) sum += cw_resid[i];
    float mean = sum / (float)COGGING_N;
    for (int i = 0; i < COGGING_N; i++)
        cogging_map[i] += COG_FF_SIGN * (cw_resid[i] - mean);
    cogging_valid = 1;
    Foc_Pwm_LowSides();        // 关PWM再写flash(擦写停CPU~40ms)
    MotorCalib_SaveToFlash(m); // 连同calib持久化到 page62/63
    m->state.State_Mode = STATE_MODE_GUARD;
    g_cogcal_phase      = COGCAL_IDLE;
}

void Cogging_CalStart(MOTOR_DATA *m, bool reset_map)
{
    if (!m->components.encoder->calib_valid) return; // 需先有offset_lut/电流环增益/极对数
    if (reset_map) { memset(cogging_map, 0, sizeof(cogging_map)); cogging_valid = 0; } // 首遍: FF关
    memset(cw_resid, 0, sizeof(cw_resid));
    memset(friction_map, 0, sizeof(friction_map)); // 复用作 CCW 累加
    memset(cw_cnt, 0, sizeof(cw_cnt));
    memset(ccw_cnt, 0, sizeof(ccw_cnt));
    cogcal_base   = m->components.encoder->pos_estimate_;
    cogcal_settle = 0; cogcal_timer = 0; cogcal_iq_acc = 0.0; cogcal_iq_n = 0;
    g_cogcal_step = 0; g_cogcal_holes = 0;
    m->components.foc->id_set    = 0.0f;
    m->state.Control_Mode        = CONTROL_MODE_POSITION;
    m->state.State_Mode          = STATE_MODE_RUNNING;
    g_cogcal_phase               = COGCAL_CW;
}

// 推进到下一点; 一向扫完则掉头/收尾
static void cogcal_next(MOTOR_DATA *m)
{
    cogcal_settle = 0; cogcal_timer = 0; cogcal_iq_acc = 0.0; cogcal_iq_n = 0;
    if (++g_cogcal_step >= COGGING_N)
    {
        g_cogcal_step = 0;
        if (g_cogcal_phase == COGCAL_CW) { g_cogcal_phase = COGCAL_CCW; cogcal_base = m->components.encoder->pos_estimate_; }
        else                             { g_cogcal_phase = COGCAL_FINISH; }
    }
}

// 每个控制拍(20kHz)调用; 返回true=标定进行中(已接管 input_position)
bool Cogging_CalUpdate(MOTOR_DATA *m)
{
    if (g_cogcal_phase == COGCAL_IDLE) return false;
    if (g_cogcal_phase == COGCAL_FINISH) { cogcal_finish(m); return false; }

    // 命令当前点的离散目标位置(每点间隔 1/COGGING_N 圈)
    float dir = (g_cogcal_phase == COGCAL_CW) ? +1.0f : -1.0f;
    m->components.foc->id_set    = 0.0f;
    m->Controller.input_velocity = COGCAL_VEL_CLAMP;
    m->Controller.input_position = cogcal_base + dir * ((float)g_cogcal_step / (float)COGGING_N);

    bool still  = ABS(m->components.encoder->vel_estimate_) < COGCAL_SETTLE_VEL;
    bool at_tgt = ABS(m->Controller.input_position - m->components.encoder->pos_estimate_) < COGCAL_POS_THR;
    cogcal_timer++;
    if (cogcal_settle == 0) // 收敛前: 等"静止且到目标"(一旦到目标即开启1s平均窗); 顺带兜底累计静止样
    {
        if (still && at_tgt) { cogcal_settle = 1; cogcal_iq_acc = 0.0; cogcal_iq_n = 0; } // 收敛 → 开窗(丢弃收敛前的兜底样)
        else if (still)      { cogcal_iq_acc += (double)m->components.foc->i_q; cogcal_iq_n++; }
        else                 { cogcal_iq_acc = 0.0; cogcal_iq_n = 0; }
    }
    else // 已收敛: 1s墙钟平均窗(容忍偶发vel抖动, 不像"连续静止"那样被打断→不会卡到超时)
    {
        if (still) { cogcal_iq_acc += (double)m->components.foc->i_q; cogcal_iq_n++; }
        if (++cogcal_settle >= COGCAL_SETTLE_TICKS)
        {
            if (cogcal_iq_n) cogcal_record(m, (float)(cogcal_iq_acc / cogcal_iq_n));
            cogcal_next(m);
            return true;
        }
    }
    if (cogcal_timer >= COGCAL_TIMEOUT_TICKS) // 始终没收敛: 用累计静止均值兜底(无样→留空洞由插值补)
    {
        if (cogcal_iq_n) cogcal_record(m, (float)(cogcal_iq_acc / cogcal_iq_n));
        cogcal_next(m);
    }
    return true;
}
