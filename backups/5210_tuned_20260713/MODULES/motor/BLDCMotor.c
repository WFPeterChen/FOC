#include "BLDCMotor.h"

// ===== 死区补偿 (A类标定: 逆变器死区导致的相压误差补偿) =====
// 死区期间上下桥都关, 相节点被续流二极管钳到某轨 → 实际相压在"电流流出"方向偏低 ΔU。
// 模型: V_actual = V_cmd - sign(i_phase)*ΔU, ΔU≈(t_dead/T_pwm)*Vdc。补偿=按相电流符号把 ΔU 加回相压。
// 本工程占空比是反相的(已验证: 相压↑ ⇔ dtc↓), 故 dtc -= comp*s(i)。
// L2 过零平滑: 用线性饱和 s(i)=clamp(i/ithresh,±1) 取代硬 sign(), 避免零流附近电流噪声让符号抖动。
// 三个旋钮都 volatile 可 SWD 在线调(g_dt_comp_duty 取负即反号, 便于现场定符号)。补在 abc 级, 不折进 d-q。
volatile uint8_t g_dt_comp_on      = 0;   // 运行环开/标定关 (MotorControl/InitializeTask 每拍设置)
// ⚠️默认改回关(2026-07-13, 5210事故): 低感电机(Ls=8.4µH)纹波±4A≥电流限, 全工作域纹波自线性化→无畸变可补,
// 而v3仍按4010参数(0.3A带宽)满额注入±147mV@6fe → 把贴延迟极限的电流环激炸(20rad/s OC墙, 关补偿立愈爬到40)。
// 死区补偿只适用于 纹波<<工作电流 的高感电机(4010勉强算); 低感+低压场景 = 环路+纹波已自动解决。USB可再开。
volatile uint8_t g_dt_comp_master  = 0;
volatile float   g_dt_comp_duty    = 0.7f * (float)DEADTIME_COMP / (float)PWM_PERIOD_CYCLES; // =0.7×430ns≈0.0061
// 0.7 = 环内残余系数(2026-07-12, 24V+1.5A d轴注入+40rad/s 实测): 电流环PI在5/7次畸变频率(221/310Hz)仍有增益,
// 已吸收部分畸变 → 满额0.0087过补(7次谐波22→12.5mV反涨), 0.005~0.0065 总畸变最低(22→13mV, -40%)。
// 低速(f_e≲50Hz)环路几乎全吸收, 补偿量无感; 该系数只在高速+负载边界起作用。
volatile float   g_dt_comp_ithresh = 0.30f; // 过零平滑带宽[A] ≈1.4×PWM电流纹波幅值(实测Ls=171µH→纹波~0.22A峰, 2026-07-11)。
// 物理含义: |i|<纹波时死区误差被20kHz纹波过零自我平均成~线性平滑函数(实测: 空载0.15A下OFF的5/7次畸变谐波仅1-3mV,
// 满额补偿反而过补+6mV) → 线性带按纹波定, 带内按比例补=模拟纹波平均, |i|>纹波才满额。重载(>0.5A)方波畸变成形, 补偿足额生效。
volatile float   g_dt_comp_velgate = 1.0f;  // 速度门控阈值 [rad/s] (迟滞0.8/1.2×): 静止关(畸变=宽带噪声不可前馈), 转动开(v3, 2026-07-11)

static inline float dt_sat(float i, float thr)
{
    if (thr < 1e-6f) return (i > 0.0f) ? 1.0f : (i < 0.0f ? -1.0f : 0.0f);
    float s = i / thr;
    return (s > 1.0f) ? 1.0f : (s < -1.0f ? -1.0f : s);
}

void Deadtime_Compensate(FOC_DATA *foc)
{
    if (!g_dt_comp_on) return;
    float c = g_dt_comp_duty;
    // 2026-07-11 v2: 符号源从"实测电流"改为"电流参考"(id_set/iq_set 逆Park→abc)。
    // 实测电流做符号时, 电流噪声经 sat() 反馈回相压 = 负电阻 ΔU/ithresh(0.2A带宽时0.52Ω>相阻R→193Hz激振,
    // 1A带宽仍+12%噪声)。参考无测量噪声回路, 负电阻约束消失, 带宽可收窄、保持态只余mV级抖动。
    // Park逆变换与本工程正变换(i_d=iα·cos+iβ·sin; i_q=iβ·cos−iα·sin)配套。
    float ialf = foc->id_set * foc->cos_val - foc->iq_set * foc->sin_val;
    float ibet = foc->id_set * foc->sin_val + foc->iq_set * foc->cos_val;
    float ia   = ialf;
    float ib   = -0.5f * ialf + _SQRT3_2 * ibet;
    float ic   = -0.5f * ialf - _SQRT3_2 * ibet;
    foc->dtc_a = CLAMP(foc->dtc_a - c * dt_sat(ia, g_dt_comp_ithresh), 0.0f, 1.0f);
    foc->dtc_b = CLAMP(foc->dtc_b - c * dt_sat(ib, g_dt_comp_ithresh), 0.0f, 1.0f);
    foc->dtc_c = CLAMP(foc->dtc_c - c * dt_sat(ic, g_dt_comp_ithresh), 0.0f, 1.0f);
}

// ȫ�ֱ�������������������
FOC_DATA foc_data = {
    .inv_vbus                = INVBATVEL,                 // ������ѹ
    .vd_set                  = 0.0f,                      // d ���ѹ����
    .vq_set                  = 0.0f,                      // q ���ѹ����
    .id_set                  = 0.0f,                      // d ���������
    .iq_set                  = MOTOR_INPUT_CURRENT,       // q ���������
    .i_d_filt                = 0.0f,                      // ��ͨ�˲���� d �����
    .i_q_filt                = 0.0f,                      // ��ͨ�˲���� q �����
    .i_bus                   = 0.0f,                      // �ܵ���
    .i_bus_filt              = 0.0f,                      // ��ͨ�˲�����ܵ���
    .power_filt              = 0.0f,                      // ��ͨ�˲�����ܹ���
    .current_ctrl_integral_d = 0.0f,                      // d �����������ֵ
    .current_ctrl_integral_q = 0.0f,                      // q �����������ֵ
    .current_ctrl_p_gain     = MOTOR_CURRENT_CTRL_P_GAIN, // ���������棬��У׼�������Զ�����ó���Ҳ���������� (Auto)
    .current_ctrl_i_gain     = MOTOR_CURRENT_CTRL_I_GAIN, // �������������棬��У׼�������Զ�����ó���Ҳ���������� (Auto)
};

void FOC_reset(FOC_DATA *foc)
{
    foc->i_d                 = 0.0f;
    foc->i_q                 = 0.0f;
    foc->current_ctrl_p_gain = 0.0f;
    foc->current_ctrl_i_gain = 0.0f;
}

/**
 * @brief ����PWM���
 *
 * ������ʱ��ͨ��1��2��3��PWM��������ڿ��Ƶ�����������
 *
 * @param
 */
void Foc_Pwm_Start(void)
{
    set_dtc_a(PWM_ARR >> 1); // ����ͨ��1��ռ�ձ�
    set_dtc_b(PWM_ARR >> 1); // ����ͨ��2��ռ�ձ�
    set_dtc_c(PWM_ARR >> 1); // ����ͨ��3��ռ�ձ�

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);

    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
}

/**
 * @brief ֹͣPWM���
 *
 * ֹͣ��ʱ��ͨ��1��2��3��PWM��������ڿ��Ƶ�����������
 *
 * @param
 */
void Foc_Pwm_Stop(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_4);

    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_3);
}

void Foc_Pwm_LowSides(void)
{
    /* Set all duty to 0% */
    set_dtc_a(0); // ����ͨ��1��ռ�ձ�
    set_dtc_b(0); // ����ͨ��2��ռ�ձ�
    set_dtc_c(0); // ����ͨ��3��ռ�ձ�

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
}

/**
 * @brief ����PWMռ�ձ�
 *
 * ��������ֵ��������ͨ����PWMռ�ձȣ����ڿ��Ƶ��ת�ٺ�ת��
 *
 * @param dtc_a ͨ��1��ռ�ձ�ֵ��0-100%��
 * @param dtc_b ͨ��2��ռ�ձ�ֵ��0-100%��
 * @param dtc_c ͨ��3��ռ�ձ�ֵ��0-100%��
 */
void SetPwm(FOC_DATA *foc)
{
    // ���ø���ͨ����Ӧ�Ĵ����Ƚ�ֵ��ʵ��PWM�������
    set_dtc_a((uint16_t)(foc->dtc_a * PWM_ARR)); // ����ͨ��1��ռ�ձ�
    set_dtc_b((uint16_t)(foc->dtc_b * PWM_ARR)); // ����ͨ��2��ռ�ձ�
    set_dtc_c((uint16_t)(foc->dtc_c * PWM_ARR)); // ����ͨ��3��ռ�ձ�
}

/**
 * @brief �������Һ�����ֵ
 *
 * �����Ƕ� theta ��Ӧ�� sin �� cos ֵ
 *
 * @param foc ָ�������ݽṹ��ָ��
 */
void Sin_Cos_Val(FOC_DATA *foc)
{
    foc->sin_val = arm_sin_f32(foc->theta); // ��������ֵ
    foc->cos_val = arm_cos_f32(foc->theta); // ��������ֵ
}

/**
 * @brief Clarke�任
 *
 * Clarke�任���������ת��Ϊ Alpha-Beta ����ϵ�µĵ���
 *
 * @param foc ָ�������ݽṹ��ָ��
 */
void Clarke(FOC_DATA *foc)
{
    foc->i_alpha = foc->i_a;                            // �����������a�����
    foc->i_beta  = (foc->i_b - foc->i_c) * ONE_BY_SQRT3; // ����������㣬ʹ�á�3�ĵ������й�һ��
}

/**
 * @brief ��Clarke�任
 *
 * ��Clarke�任�� Alpha-Beta ����ϵ�µĵ�ѹת��Ϊ�����ѹ
 *
 * @param foc ָ�������ݽṹ��ָ��
 */
void Inv_clarke(FOC_DATA *foc)
{
    foc->v_a = foc->v_alpha;                                  // a���ѹ���ڦ����ѹ
    foc->v_b = -0.5f * foc->v_alpha + _SQRT3_2 * foc->v_beta; // b���ѹ����
    foc->v_c = -0.5f * foc->v_alpha - _SQRT3_2 * foc->v_beta; // c���ѹ����
}

/**
 * @brief Park�任
 *
 * Park�任�� Alpha-Beta ����ϵ�µĵ���ת��Ϊ dq ����ϵ�µĵ�����ת�����������㶨��ֱ������
 *
 * @param foc ָ�������ݽṹ��ָ��
 */
void Park(FOC_DATA *foc)
{
    foc->i_d = foc->i_alpha * foc->cos_val + foc->i_beta * foc->sin_val; // d���������
    foc->i_q = foc->i_beta * foc->cos_val - foc->i_alpha * foc->sin_val; // q���������
}

/**
 * @brief ��Park�任
 *
 * ��Park�任�� dq ����ϵ�µĵ�ѹת��Ϊ Alpha-Beta ����ϵ�µĵ�ѹ
 *
 * @param foc ָ�������ݽṹ��ָ��
 */
void Inv_Park(FOC_DATA *foc)
{
    foc->v_alpha = (foc->v_d * foc->cos_val - foc->v_q * foc->sin_val); // �����ѹ����
    foc->v_beta  = (foc->v_d * foc->sin_val + foc->v_q * foc->cos_val);  // �����ѹ����
}

/**
 * @brief ��ֵ�������ע��
 *
 * �Ԧ�-������ϵ�ĵ�ѹ���о�ֵ�������ע�룬�Ա����SVPWM����
 * ��ѹ�Ƕ��Ǵ�ֱ��ת�ӽǶȵģ�n�������Ǻϳɵ�ѹ������������ת�����ڵ�����
 *
 * @param foc ָ�������ݽṹ��ָ��
 */
void Svpwm_Midpoint(FOC_DATA *foc)
{
    // ����ĸ�ߵ�ѹ�������ͦ����ѹ
    foc->v_alpha = foc->inv_vbus * foc->v_alpha;
    foc->v_beta  = foc->inv_vbus * foc->v_beta;

    float va     = foc->v_alpha;                                  // ��������ѹ
    float vb     = -0.5f * foc->v_alpha + _SQRT3_2 * foc->v_beta; // ����b���ѹ
    float vc     = -0.5f * foc->v_alpha - _SQRT3_2 * foc->v_beta; // ����c���ѹ

    float vmax   = max(max(va, vb), vc); // �ҵ�����ѹֵ
    float vmin   = min(min(va, vb), vc); // �ҵ���С��ѹֵ

    float vcom   = (vmax + vmin) * 0.5f; // �����������

    // ��������ռ�ձȵ���ֵ��ȷ����0��1֮��
    foc->dtc_a   = CLAMP(((vcom - va) + 0.5f), 0.0f, 1.0f);
    foc->dtc_b   = CLAMP(((vcom - vb) + 0.5f), 0.0f, 1.0f);
    foc->dtc_c   = CLAMP(((vcom - vc) + 0.5f), 0.0f, 1.0f);
}

/**
 * @brief �����ж�
 *
 * ���ݦ�-������ϵ�еĵ�ѹֵ�жϵ�ǰ��������������������ռ�ձ�
 *
 * @param foc ָ�������ݽṹ��ָ��
 */
void Svpwm_Sector(FOC_DATA *foc)
{
    float ta    = 0.0f, tb = 0.0f, tc = 0.0f;                    // ����ռ�ձ�ʱ���ʼ��
    float k     = (TS * _SQRT3) * foc->inv_vbus;                 // ����ĸ�ߵ�ѹ����kֵ
    float va    = foc->v_beta;                                   // �����ѹ
    float vb    = (_SQRT3 * foc->v_alpha - foc->v_beta) * 0.5f;  // b���ѹ����
    float vc    = (-_SQRT3 * foc->v_alpha - foc->v_beta) * 0.5f; // c���ѹ����
    int a       = (va > 0.0f) ? 1 : 0;                           // �ж�a���Ƿ�����㣬���Ϊ1��0
    int b       = (vb > 0.0f) ? 1 : 0;                           // �ж�b���Ƿ�����㣬���Ϊ1��0
    int c       = (vc > 0.0f) ? 1 : 0;                           // �ж�c���Ƿ�����㣬���Ϊ1��0
    int sextant = (c << 2) + (b << 1) + a;                       // ����a��b��c���״̬ȷ������

    switch (sextant)
    {
    case SECTOR_3: // ����3
    {
        float t4 = k * vb;
        float t6 = k * va;
        float t0 = (TS - t4 - t6) * 0.5f; // ��ʸ��������ʱ��

        ta       = t4 + t6 + t0; // ����a��ռ�ձ�ʱ��
        tb       = t6 + t0;      // ����b��ռ�ձ�ʱ��
        tc       = t0;           // c��ռ�ձ�ʱ��Ϊt0
    }
    break;

    case SECTOR_1: // ����1
    {
        float t6 = -k * vc;
        float t2 = -k * vb;
        float t0 = (TS - t2 - t6) * 0.5f;

        ta       = t6 + t0;      // a��ռ�ձ�ʱ�����
        tb       = t2 + t6 + t0; // b��ռ�ձ�ʱ�����
        tc       = t0;           // c��ռ�ձ�ʱ��Ϊt0
    }
    break;

    case SECTOR_5: // ����5
    {
        float t2 = k * va;
        float t3 = k * vc;
        float t0 = (TS - t2 - t3) * 0.5f;

        ta       = t0;           // a��ռ�ձ�ʱ��Ϊt0
        tb       = t2 + t3 + t0; // b��ռ�ձ�ʱ�����
        tc       = t3 + t0;      // c��ռ�ձ�ʱ�����
    }
    break;

    case SECTOR_4: // ����4
    {
        float t1 = -k * va;
        float t3 = -k * vb;
        float t0 = (TS - t1 - t3) * 0.5f;

        ta       = t0;           // a��ռ�ձ�ʱ��Ϊt0
        tb       = t3 + t0;      // b��ռ�ձ�ʱ�����
        tc       = t1 + t3 + t0; // c��ռ�ձ�ʱ�����
    }
    break;

    case SECTOR_6: // ����6
    {
        float t1 = k * vc;
        float t5 = k * vb;
        float t0 = (TS - t1 - t5) * 0.5f;

        ta       = t5 + t0;      // a��ռ�ձ�ʱ�����
        tb       = t0;           // b��ռ�ձ�ʱ��Ϊt0
        tc       = t1 + t5 + t0; // c��ռ�ձ�ʱ�����
    }
    break;

    case SECTOR_2: // ����2
    {
        float t4 = -k * vc;
        float t5 = -k * va;
        float t0 = (TS - t4 - t5) * 0.5f;

        ta       = t4 + t5 + t0; // a��ռ�ձ�ʱ�����
        tb       = t0;           // b��ռ�ձ�ʱ��Ϊt0
        tc       = t5 + t0;      // c��ռ�ձ�ʱ�����
    }
    break;

    default:
        break; // Ĭ���������������
    }

    // ���¸����ռ�ձȵ���ֵ����ת����ӦSVPWM���Ʒ�ʽ��ʹ����[0,1]��Χ�ڡ�
    foc->dtc_a   = CLAMP((1.0f - ta), 0.0f, 1.0f);
    foc->dtc_b   = CLAMP((1.0f - tb), 0.0f, 1.0f);
    foc->dtc_c   = CLAMP((1.0f - tc), 0.0f, 1.0f);
}

/**
 * @brief FOC�û�����
 *
 * �˺������ڰ�װFOC��һЩ��������
 *
 * @param motor ָ�������ݽṹ��ָ��
 */
void commonFOCOperations(FOC_DATA *foc)
{
    Sin_Cos_Val(foc);
    Clarke(foc);
    Park(foc);
}

void commonInverseFOCOperations(FOC_DATA *foc)
{
    Inv_Park(foc);
    Inv_clarke(foc);
    Svpwm_Sector(foc);
    Deadtime_Compensate(foc); // 死区补偿(abc占空比级, 按相电流符号; g_dt_comp_on 由运行/标定态控制)
    SetPwm(foc);
}
void setPhaseVoltage(FOC_DATA *foc, float Vd_set, float Vq_set, float phase)
{
    foc->v_q   = Vq_set;
    foc->v_d   = Vd_set;
    foc->theta = phase;
    Sin_Cos_Val(foc);
    commonInverseFOCOperations(foc);
}

void FOC_voltage(FOC_DATA *foc, float Vd_set, float Vq_set, float phase)
{
    // Clarke transform
    float i_alpha, i_beta;
    clarke_transform(foc->i_a, foc->i_b, foc->i_c, &i_alpha, &i_beta);

    // Park transform
    float i_d, i_q;
    park_transform(i_alpha, i_beta, phase, &i_d, &i_q);

    // Used for report
    foc->i_q = i_q;
    UTILS_LP_FAST(foc->i_q_filt, foc->i_q, 0.01f);
    foc->i_d = i_d;
    UTILS_LP_FAST(foc->i_d_filt, foc->i_d, 0.01f);

    // Modulation
    float V_to_mod = 1.0f / (foc->vbus * 2.0f / 3.0f);
    float mod_d    = V_to_mod * Vd_set;
    float mod_q    = V_to_mod * Vq_set;

    // Vector modulation saturation, lock integrator if saturated
    float mod_scalefactor = 0.9f * _SQRT3_2 / sqrtf(SQ(mod_d) + SQ(mod_q));
    if (mod_scalefactor < 1.0f)
    {
        mod_d *= mod_scalefactor;
        mod_q *= mod_scalefactor;
    }

    // Inverse park transform
    float mod_alpha;
    float mod_beta;
    inverse_park(mod_d, mod_q, phase, &mod_alpha, &mod_beta);

    // SVM
    if (0 == svm(mod_alpha, mod_beta, &foc->dtc_a, &foc->dtc_b, &foc->dtc_c))
    {
        SetPwm(foc);
    }
}

void FOC_current(FOC_DATA *foc, float Id_set, float Iq_set, float phase, float phase_vel, float vd_ff, float vq_ff)
{
    // Clarke transform
    float i_alpha, i_beta;
    clarke_transform(foc->i_a, foc->i_b, foc->i_c, &i_alpha, &i_beta);

    // Park transform
    float i_d, i_q;
    park_transform(i_alpha, i_beta, phase, &i_d, &i_q);

    float mod_to_V = foc->vbus * 2.0f / 3.0f;
    float V_to_mod = 1.0f / mod_to_V;

    // Apply PI control
    float Ierr_d = Id_set - i_d;
    float Ierr_q = Iq_set - i_q;
    float mod_d  = V_to_mod * (foc->current_ctrl_integral_d + Ierr_d * foc->current_ctrl_p_gain + vd_ff); // +bEMF/ωL 前馈(与A路一致)
    float mod_q  = V_to_mod * (foc->current_ctrl_integral_q + Ierr_q * foc->current_ctrl_p_gain + vq_ff);

    // Vector modulation saturation, lock integrator if saturated
    float mod_scalefactor = 0.9f * _SQRT3_2 / sqrtf(SQ(mod_d) + SQ(mod_q));
    if (mod_scalefactor < 1.0f)
    {
        mod_d *= mod_scalefactor;
        mod_q *= mod_scalefactor;
        foc->current_ctrl_integral_d *= 0.99f;
        foc->current_ctrl_integral_q *= 0.99f;
    }
    else
    {
        foc->current_ctrl_integral_d += Ierr_d * foc->current_ctrl_i_gain; // i_gain 已含Ts(见FOC_update_current_gain), 不再乘Ts → 修双Ts bug
        foc->current_ctrl_integral_q += Ierr_q * foc->current_ctrl_i_gain;
    }

    // Inverse park transform
    float mod_alpha, mod_beta;
    float pwm_phase = phase + phase_vel * CURRENT_MEASURE_PERIOD;
    inverse_park(mod_d, mod_q, pwm_phase, &mod_alpha, &mod_beta);

    // Used for reportZ
    foc->i_q        = i_q;
    UTILS_LP_FAST(foc->i_q_filt, foc->i_q, 0.01f);
    foc->i_d        = i_d;
    UTILS_LP_FAST(foc->i_d_filt, foc->i_d, 0.01f);
    foc->i_bus      = (mod_d * i_d + mod_q * i_q);
    UTILS_LP_FAST(foc->i_bus_filt, foc->i_bus, 0.01f);
    foc->power_filt = foc->vbus * foc->i_bus_filt;

    // SVM
    if (0 == svm(mod_alpha, mod_beta, &foc->dtc_a, &foc->dtc_b, &foc->dtc_c))
    {
        Deadtime_Compensate(foc); // #5: 与A路一致的死区补偿(公平对比)
        SetPwm(foc);
    }
}
