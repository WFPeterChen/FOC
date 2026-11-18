# 换电机标定工作流 (FalconFOC)

> 目的: 下次换电机能快速复用。整理自 4010(12N14P/KV360) 全程标定(2026-06)。
> 核心: 把**电机相关**的几项重标并写回 flash, **电机无关**的(方法/代码/工具)不用动。

---

## 0. 电机无关 vs 电机相关
- **无关(不用重做)**: 标定方法代码(`cogging.c`/`motor_calib.c`)、GCC 构建、SWD 调试钩子、
  flash 布局(**page63=0x0801F800 标定**, **page62=0x0801F000 cogging map**)。
- **相关(每个电机要重标)**: Rs, Ls, 极对数, 编码器零位/方向, Kt(torque_const),
  cogging map, 电流/转速限幅, 电池 S 数(N_BASE)。

---

## 1. 工具链 / 构建 / 烧录

```bash
cd 2.Firmware/FalconFoc/gcc
make -j4                                   # → build/FalconFOC.elf
arm-none-eabi-objcopy -O binary build/FalconFOC.elf build/FalconFOC.bin   # ⚠️make不自动更新bin, 手动转
```

**烧录(可靠法 — openocd 先 halt 住 CPU, 躲开 PWM 噪声损坏 SWD):**
```bash
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c "init" -c "adapter speed 480" -c "reset halt" \
  -c "flash write_image erase build/FalconFOC.elf" \
  -c "verify_image build/FalconFOC.elf" -c "reset run" -c "exit"
```
看到 `verified NNNNN bytes` = 成功; 接触不稳时循环重试几次。
**st-flash 在电机跑/接触差时会写花 flash, 不要用**。`flash write_image erase` 只擦 image 占的页(0~44), **page62/63 不动**。

---

## 2. SWD 调试钩子 (CAN 收发器本板长期不稳, 标定全走 SWD)

> 地址随每次编译漂移 → 用 `arm-none-eabi-nm build/FalconFOC.elf | grep <符号>` 重新定位。
> openocd: `mdw`读word / `mwb`写字节 / `mww`写word; 连接会 halt 核心, 命令末尾必带 `-c "resume"`。

| 符号 | 用法 |
|---|---|
| `g_cog_cal_trigger` | `mwb=2` 清零重测齿槽 / `=1` 在现有map上ILC迭代 |
| `g_dbg_vel_on`/`g_dbg_vel` | 强制速度环@转速(turn/s)验证 |
| `g_dbg_mit_on`/`_kd`/`_v`/`_t` | 强制 MIT 阻抗 `tau=kd*(vdes-v)+tff`(测齿槽FF用) |
| `g_dbg_save_flash` | `=1` 把当前 RAM 标定写回 flash |
| `g_cogcal_phase`/`_step`/`_holes` | 进度: phase 1正扫/2反扫/0完成; step 0..512; holes 空洞数 |
| `cogging_map`(512×f32) / `cogging_valid` / `g_vlog`(512×f32@2kHz转速环形缓冲) | 读结果 |

- **心跳判活**: 连读两次 `g_vlog_idx`, 变化=固件在跑(RUNNING态才跳)。
- **关键结构偏移**(读RAM诊断): `motor_data`(nm查, 一般0x2000027c) → 参数 Rs@+0x18 Ls@+0x1c;
  PID 顺序 IqPID/IdPID/VelPID/PosPID 各 72B, 每个内 `Kp` 在 +4(用 nm 找 `g_cogcal_phase` 等锚点 + dump 比对定位)。

---

## 3. 换电机标定步骤

### 准备
- 限流电源**限流给 1~2A 兜底**; 相线/编码器/SWD 接好; 电机能自由转。
- **烧录时电机断电(留 USB 供 SWD), 测试时通电**。

### Step 1 — 填电机参数 (`MODULES/motor/BLDCMotor.h`, rb 变体)
`MPTOR_P`(极对数)、`MOTOR_TORQUE_CONST`(Kt≈9.55/KV)、`MOTOR_CURRENT_LIMIT`/`MOTOR_VEL_LIMIT`、
`N_BASE`(电池S数, 6S→6)。速度环 `0.8/0.02`、位置环 `120`(4010验证稳, 起点)。

### Step 2 — 电气/编码器标定 (Rs/Ls/零位/方向/极对数/LUT)
- **⚠️⚠️ on-board Ls 标定疑有 bug**: 4010 那次 flash 里 Ls 标成 **3.36mH(应171µH, 错20×)**
  → 电流环增益 `IqPID.Kp = Ls·vel_limit·pp·2π` 算成 **7.39(应~0.38)** → **上电疯狂高频振荡+发烫**。
- **安全做法**: 若有 Ls 真值(LCR表/数据手册), 直接写编译期常数 `MOTOR_LS`, **别信 on-board Ls cal**。
- 跑全标定: `PRE_CALIBRATED=0` + 擦 flash(`MotorCalib_Erase` 或 openocd 擦 0x0801F800) → 开机自动
  `Init_Motor_Calib` 标 零位/方向/极对数/LUT(+Rs/Ls)。
- **标完务必 SWD 验**: 读 `Ls`(应百µH级) 和 `IqPID.Kp`(应 0.1~1, **不是几mH/几**)。**离谱就用编译期好值覆盖再存**。

### Step 3 — 齿槽标定 (cogging map, 闭环逐点静态保持)
- 确认 `calib_valid` 且通电。SWD: `mwb g_cog_cal_trigger 2` 启动。
- 监视 `g_cogcal_phase` 1→2→0、`g_cogcal_step`。约 **23min**(每点收敛后保持1s取2万样均值降噪)。
- 完成 `g_cogcal_holes` 应 ≈0(空洞已圆周插值)。
- 关键参数(`cogging.c`, 已调好): 阈值 **0.5bin**(太紧 <静摩擦死区→永远收不敛每点超时)、
  **收敛后1s墙钟窗**(不能要求"连续静止1s", 会被检测器微抖打断→卡超时)、空洞插值。
- map 也直接**喂 sim2real 仿真**(真实力矩纹波谱; 4010: 14/rev 磁铁数主导)。

### Step 4 — 写回 flash + 部署
- `PRE_CAL=0` 时齿槽 cal 完成会**自动存 flash**。手动存: load cogging 到 RAM + `mwb g_dbg_save_flash 1`。
- 验 flash page63(0x0801F800): magic=`0xCA11B007`、`Ls@+8` 合理、`has_cogging@+540`=1。
- 确认 `PRE_CALIBRATED=0`, 重烧 → 开机自动加载 calib+cogging、FF 启用。
- **验开机**: `Ls`/`IqPID.Kp` 合理、`cogging_valid=1`、`Fault=0`、无振荡、心跳跳。

---

## 4. 齿槽前馈 — 用法与结论 (重要)
- **FF 只在 力矩/MIT 模式补**(代码已 gate 到 `CONTROL_MODE_TORQUE/MIT`)。
  MIT(无积分软环)实测速度纹波 **↓82%**(CV 11%→2%)。这正是 **DM 电机真实用法(MIT阻抗)**。
- **速度/位置环别用 FF**: 反馈环的积分自己已抑齿槽, 加 FF 反而 **+40~80% 变差**。
- **测 FF 效果**: `g_dbg_mit`: `vdes=0` + 恒推力 `tff=0.03Nm`(>>峰值齿槽, 顶过 detent 保证稳定转) + `kd=0.05`
  → 读 `g_vlog` std 比 FF开/关。(注: MIT 太软无 tff 会卡在 detent 来回, mean 乱跳不可比)。

---

## 5. 常见坑速查
- **SWD "no target"/烧录失败**: 接触不良 → **换 SWD 线**(量通断没用, 信号断); ST-Link 半挂死(版本号乱跳)→ **重插其 USB**。
- **flash 擦写停 CPU ~40ms**: `SaveToFlash` 前必关 PWM(已做)。
- **CAN 无反馈**: 收发器 5V(USB VBUS 经 D6)或信号线问题; 本板长期不稳, 标定走 SWD。
- **PRE_CAL=1 不读 flash** → LUT/cogging 不加载、不持久化(只用编译期常数, 应急绕坏 flash 用)。
- **shell 里调 openocd**: 别用 `func(){ openocd ... $@ }`(不带引号会拆词写失败), 用 `"$@"` 或显式调用; 验电机真转看 `g_vlog` mean 或 `enc.vel_estimate`(encoder+588), 别只看 std。

---
相关: `MOTOR_MODELING_SIM2REAL.md`(仿真建模), 记忆 `falconfoc-4010-bringup` / `falconfoc-build-flash`。

---

## 6. 5210(14极对) 换装实战新增教训 (2026-07-12, 三个深水bug)

1. **标定电流要按转子大小配**: `CURRENT_MAX_CALIB` 0.5A(拖力~15mNm)拖 4010 够、拖 5210 大转子打滑
   → **pp 检测数错(数出5)**。现默认 2.0A(~60mNm)。换更大电机先估: 拖力=Kt×I 需 ≳3×齿槽峰值。
2. **IWDG 会咬死标定**: LUT 扫描阶段 ISR 负载饿慢 FreeRTOS 时基 → GuardTask 喂狗超 250ms → 标定中途
   复位死循环(~36s一轮, 症状=电机反复跳同一段"标定舞")。已修: DETECTING 态由 20kHz ISR 喂狗。
3. **标定样本数组曾踩栈**: `p_error_arr=malloc(SAMPLES_PER_PPAIR×PP_MAX×4)` 10KB > 剩余堆 6.6KB,
   高极对数电机(样本×pp)写穿 ISR 栈 → 硬fault。已修: SAMPLES_PER_PPAIR 128→32 + malloc NULL 防线。
   **换更高极对数电机前算一遍: 32×pp×4 字节 vs `end`~0x20008000 的余量**。

**排障工具箱(这次验证有效)**: uwTick(nm 查地址)两读判重启; RCC_CSR(0x40021094) 累计复位标志先写
RMVF(bit23) 清再抓现行; gdb 挂 4 个 fault handler 断点(⚠️ DBG_IWDG_STOP 只在 halt 时冻狗, continue
期间狗照跑——"挂着调试器没死"不能证明无 fault)。标定进度: Cs_State 字节(gdb 查地址), 0x0b=LUT正扫。
