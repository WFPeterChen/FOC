# 换电池(开关电源 → 锂电)待办备忘

> 现在用开关电源, 后续换 **6S 锂电**。换之前/换的时候照这个清单做一遍。
> 背景: 开关电源单向只出不进、输出很硬; 锂电双向(吸回馈)、有内阻会下垂、按 S 数定电压窗口。
> 关联: 健壮性项见 `falconfoc-firmware-architecture` 记忆(deadman/vbus补偿/bus-off恢复/回馈抑制都已做,
> 换电池后更管用); 硬件 6S 约束见 `falconfoc-6s-power-supply` 记忆。

---

## 0. 一句话结论
换 **6S** 不是即插即用: 固件 `N_BASE` 必须从 3 改 6, 否则上电 vbus(25V) 远超 3S 的 OV 阈值(14.4V) → **立刻 OV→GUARD 跑不起来**。
其余是阈值适配(护电芯/护逆变器)和一次硬件确认(TVS)。

---

## 1. 必改 (不改就开不了机 / 不安全)

### a) S 数 — `MODULES/motor/BLDCMotor.h` (rb 变体)
```c
#define N_BASE 6.0f      // 3 → 6。BATVEL 自动 = 4*6 = 24V (6S 标称22.2, 满25.2)
```
连锁自动更新(不用手动改): `CURRENT_PID_MAX_OUT = BATVEL/√3`、回馈抑制阈值(由 BATVEL_MAX_LIMIT 派生)。

### b) 过压 OV 阈值 — 当前 `BATVEL_MAX_LIMIT = BATVEL*1.2`
- 6S 下 *1.2 = **28.8V**, **超过 TVS SMBJ28CA 工作电压 28V** → 危险(TVS 进入导通/发热, 保护形同虚设)。
- **改成绝对值 ~27V**(在 6S 满 25.2V 之上留 1.8V, 在 TVS 之下留余量):
```c
#undef  BATVEL_MAX_LIMIT
#define BATVEL_MAX_LIMIT 27.0f   // 6S: 高于满电25.2, 低于TVS标称28(28CA)/30(30CA)
```
- 这样回馈抑制阈值自动变 26.1V(start)/26.8V(full), 形成漂亮的保护梯: 满电25.2 < 抑制26.1~26.8 < OV跳闸27 < TVS 28/30。
- 满电(25.2)静置时 26.1 不会误触发回馈抑制 ✓。

### c) 欠压 UV 阈值 — 当前 `BATVEL_MIN_LIMIT = BATVEL/1.5`
- 6S 下 /1.5 = **16V = 2.67V/cell**, **过放伤电芯**!(逆变器能扛, 但电池不行)
- **改成护电芯的值 ~19.8V(3.3V/cell), 至少 18V(3.0V/cell)**:
```c
#undef  BATVEL_MIN_LIMIT
#define BATVEL_MIN_LIMIT 19.8f   // 6S 护电芯: 3.3V/cell 停; 别再用 /1.5 那个逆变器下限
```

### d) 电流上限 — `MOTOR_CURRENT_LIMIT`
- 现在 4.0A(为配开关电源 3A 限流而压低, 代码里已有注释 "上6S改回10A")。
- 电池没有限流保护伞, 可放开到电机能力:
```c
#define MOTOR_CURRENT_LIMIT 10.0f  // 4010 电机能力; 但务必串保险丝补上丢掉的限流保护
```

---

## 2. 不用改 (已确认兼容 6S)
- **vbus 采样分压**: `VIN_R1=1k / VIN_R2=10k` = 1:11, ADC 满量程可读 **36.3V**。6S 满 25.2V→ADC 2.29V, OV 27V→2.45V, 都 <3.3V 不饱和 ✓。`VOLTAGE_TO_ADC_FACTOR` 不用动。
- **#3 母线电压实时补偿**(`inv_vbus=1/vbus`): 换电池后才真正发挥作用(电池下垂+放电曲线让 vbus 真在动) ✓。
- **#1 deadman / #2 bus-off恢复 / #4 回馈抑制**: 与 S 数无关(回馈阈值随 BATVEL_MAX_LIMIT 自动跟), 直接受用。
- 母线电容 50V、MOS 60V、RY8411 输入 42V、栅驱 FD6288Q: 6S(25V/OV27/TVS钳<50)全在耐压内 ✓。

---

## 3. 硬件确认 (上 6S 前)
- **TVS D2**: 原板是 **SMBJ28CA**(工作28V, 6S满25.2仅~2.8V余量, 急减速回馈易顶到/烧)。
  已决定升级为 **SMBJ30CA**(工作30V, 余量~4.8V, 钳位48.4V仍<50V电容耐压)。**上6S建议先把 D2 换成 30CA**。
  ⚠️ 别用 SMBJ33CA: 钳位53.3V > 母线电容50V, 要换就得同时升 DCBUS 电容到 63V。
- **串保险丝**: 电池能瞬间灌大电流, 没了开关电源的限流伞 → 加合适保险丝兜底。
- 大包**防反插/防火花头**(热插拔涌流尖峰)。
- 若用带 BMS 的电池: 注意 **BMS 过流/过充切断会突然断电** → 感性/回馈能量没处去 → vbus 尖峰打 MOS。选 BMS 限流留足余量, 或别在大电流时硬断。

---

## 4. 上电测试顺序 (别直接上电池)
1. **先用开关电源调到 24V**(不是电池)上电 → SWD/CAN 读 `foc->vbus` 应≈24(不饱和、不乱跳); 确认不误报 OV/UV; 空载使能能转。
2. 验证 OV/UV/回馈抑制阈值在 24V 下都合理(回馈抑制不在 24~25V 误触发; OV 27 不误跳)。
3. 再换真电池, 重复 1~2。满电(25.2)静置确认不触发回馈抑制。
4. 轻载跑起来, 看 vbus 在加减速时下垂/回升, 确认 #3 补偿后电流环手感稳定。

---

## 5. 改完回归验证 (照搬前几条的 SWD 验证法)
- deadman: 静默>200ms→GUARD, 喂@30Hz→RUNNING。
- 回馈抑制: 临时把 `g_vbus_regen_start` 调到母线电压以下(SWD), 刹车看电机滑行不刹 + `g_regen_foldback_cnt` 跳; 恢复阈值。
- 启动读 `Ls`/`IqPID.Kp` 合理、`cogging_valid=1`、`Fault=0`、心跳跳。

---
做完把本文件标记完成日期, 并更新 `falconfoc-6s-power-supply` / `falconfoc-firmware-architecture` 记忆。
