# moteus (mjbots) 博客 —— FOC 学习精选存档

下载自 blog.mjbots.com(Josh Pieper),2026-07-04。全站 431 篇多为机器人搭建/CNC/产品公告;
这里**精选 53 篇 FOC/电机控制/标定/编码器/散热相关**的技术文,按主题分类。★=对 FalconFOC 最有参考价值。

> 全部 URL 见 `00_index_urls.txt`。文件名=日期_标题。

## 电流环 / 调制 / 驱动
- ★★ `2021_04_13_auto-tuning-current-control-loops` —— PI 增益自动整定: **Kp=ω_3db·L, Ki=Kp·R/L**(临界阻尼一阶闭环)。我们的 `current_ctrl_p_gain=Ls·bw` 同源。
- ★★ `2025_04_03_rethinking-dead-time-compensation` —— 死区补偿再思考: 用 SPICE 建 DRV 栅驱, 发现旧模型忽略了 bEMF 对非线性的影响 → **改为电流模式换相标定绕开死区**。直接对标我们刚做的死区补偿。
- ★ `2021_09_15_compensating-for-fet-turn-on-time` —— FET 导通时间/死区补偿的原始工作(上一篇的前身)。
- ★ `2024_11_20_space-vector-pwm-svpwm` —— SVPWM/中点钳位: 调制深度 +15%、最高转速 +15%。看我们 svm() 是否已做。
- `2020_09_16_optimizing-fet-drive-strength`, `2022_03_12_customizable-pwm-rate`
- `2021_12_06_voltage-mode-control-for-gimbal-motors`, `2022_01_04_fixed-voltage-mode`, `2025_07_10_automatic-selection-of-voltage-mode-control` —— 低电感云台电机的电压模式(避免电流环发散)。
- `2019_11_04_flux-braking` —— 弱磁/磁通制动(回馈能量处理, 对标我们的回馈过压抑制)。
- `2021_03_09_position-anti-windup` —— 位置环抗积分饱和。

## 标定(和我们最相关)
- ★★ `2025_04_24_improving-motor-constant-calibration` —— **R/L/Kt 标定改进**: R 用电流选点两点拟合避开过零非线性; L 扫多个方波频率+非零偏置(低感高阻电机从差10×→差2×); Kt(Kv)高静摩擦电机改进, 实测常在真值 5~25% 内。**直接回答"我们标定还缺啥、怎么标准"**。
- ★★ `2025_04_10_current-mode-commutation-calibration` —— **电流模式换相标定**(替代开环电压转), 对标我们的 encoder_offset/零位标定。
- ★★ `2025_02_06_nonlinear-encoder-compensation-with-no-reference` —— 无外部基准的编码器非线性补偿, 对标我们的 `offset_lut[128]`。
- ★ `2025_04_17_representing-torque-constant-as-kv` —— 用 Kv 表示力矩常数(Kt=Kv 的 SI 一致性), 对标我们估算的 Kt=0.0265。
- `2019_01_26_encoder-autocalibration`, `2021_11_23_improved-moteus_tool-calibration`
- `2025_07_24_correcting-the-encoder-filter-frequency`, `2021_05_10_filtering-encoder-values`, `2021_04_26_auxiliary-encoders`

## 测量 / 力矩 / 电机建模(sim2real)
- ★ `2020_08_26_measuring-torque-ripple` —— 测力矩纹波谱, 对标我们 cogging map 的谐波分析。
- ★ `2020_07_31_dealing-with-stator-magnetic-saturation` + `2025_12_04_fitting-motor-saturation-models` —— 定子磁饱和(大电流 L 下降), 我们尚未建模。
- ★ `2020_08_24_stm32g4-adc-and-low-torque-operation` —— 低力矩下 ADC 噪声/量化, 对标我们采样电阻精度的讨论。
- `2020_03_25_ground-truth-torque-testing`, `2020_08_12_torque-transducer`, `2020_08_17_spring-and-damping-constants`, `2020_08_19_initial-dynamometer-assembly` —— 力矩真值/测功机(未来交叉验证 Kt 用)。
- `2020_08_21_testing-alternate-magnetic-encoders`, `2020_08_28_up-rating-phase-current`

## 温度模型(我们只有过温保护, 无温补)
- ★ `2025_06_19_thermal-modeling` + `2025_06_26_measuring-thermal-parameters-empirically` —— 电机+控制器热建模、实测热参数。
- `2024_11_06_configurable-motor-thermistors`

## 高速 / 轨迹 / 系统
- `2020_07_08_high-speeds-with-the-moteus-controller`, `2020_09_21_unlimited-rotations`
- `2024_02_07_recapturing-position-and-velocity`, `2022_04_08_velocity-and-acceleration-limited-trajectories`

## 固件基础设施(健壮性/遥测, 对标我们的短板)
- ★ `2019_09_11/13/20/23_improving-the-moteus-update-rate-part-1..4` —— 提高控制环更新率的系统优化(4 篇)。
- ★ `2019_10_11_failing-more-gracefully` —— 故障优雅降级。
- ★ `2023_08_22_new-hold-position-watchdog-timeout` —— 掉线保护, 对标我们的 deadman(CMD_TIMEOUT)。
- ★ `2026_01_29_moteus-hardware-adc-triggering` —— 硬件 ADC 触发对齐 PWM 中点, 对标我们 TIM1_CC4 触发。
- ★ `2026_06_18_host-side-spmsm-simulation-for-regression-tests` —— **主机端 SPMSM 仿真做回归测试**(和你的 sim2real 目标高度相关)。
- `2019_10_04_openocd-for-stm32g4`, `2019_10_09_bringing-up-fd-can`, `2023_02_20_clock-synchronization`, `2023_07_24_stm32g4-adc-performance-part-2`, `2026_01_01_capturing-full-rate-debug-data`

## 建议阅读顺序(给本项目)
1. `auto-tuning-current-control-loops`(电流环增益的第一性原理, 我们已同源, 看它怎么系统化)
2. `improving-motor-constant-calibration` + `current-mode-commutation-calibration`(直接指导我们把 R/L/Kt/零位标准化)
3. `rethinking-dead-time-compensation`(我们刚做死区补偿, 看它踩的坑和更优解)
4. `nonlinear-encoder-compensation` + `measuring-torque-ripple`(对标 offset_lut / cogging)
5. `thermal-modeling` 两篇 + `host-side-spmsm-simulation`(补我们缺的温度模型 + sim2real 回归)
