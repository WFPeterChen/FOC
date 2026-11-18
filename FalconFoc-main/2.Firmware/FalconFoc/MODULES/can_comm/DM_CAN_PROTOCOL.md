# FalconFOC —— 达妙(DM)电机 CAN 协议规格

> 目标：让 FalconFOC 板在 CAN 总线上**表现得像一台达妙(DM)电机**，与 DM 上位机 /
> USB-CAN / 其它 DM 电机即插即用。本规格是**收发双方（固件 + 上位机/SocketCAN）的唯一依据**。
> 物理层：经典 CAN 2.0（非 FD），1 Mbps，采样点 75%，标准帧(11bit ID)，每帧 8 字节。

---

## 1. ID 分配

设电机 ID = `MOTOR_ID`（本板默认 `0x01`），主控 ID = `MASTER_ID`（默认 `0x00`）。

| 方向 | 帧 | CAN ID | 说明 |
|---|---|---|---|
| 主控→电机 | MIT 控制 / 特殊命令 | `MOTOR_ID` (0x01) | 靠数据内容区分是 MIT 还是特殊命令 |
| 主控→电机 | 位置-速度 | `0x100 + MOTOR_ID` (0x101) | |
| 主控→电机 | 速度 | `0x200 + MOTOR_ID` (0x201) | |
| 电机→主控 | 反馈 | `MASTER_ID` (0x00) | 每收到一条控制/命令帧就回一帧反馈 |

---

## 2. 量化范围（已确认，2026-06-23）

基于 4010 航模电机 KV360：Kt ≈ 9.55/360 ≈ 0.0265 N·m/A，最大相电流 10A → 最大力矩 ≈ 0.265 N·m。

| 符号 | 含义 | 范围 | 位宽 | 备注 |
|---|---|---|---|---|
| `P_MAX` | 位置 | **−50 … +50 rad** (±约8圈) | 16 | MIT 位置上限；连续多圈转动用速度模式不受此限 |
| `V_MAX` | 速度 | **−300 … +300 rad/s** (±约2860rpm) | 12 | MIT 的 v 编码 + 反馈 |
| `T_MAX` | 力矩 | **−0.3 … +0.3 N·m** | 12 | ≈10A×Kt 留余量 |
| `KP_MAX` | 位置刚度 | **0 … 500** | 12 | (rad,N·m) 单位 |
| `KD_MAX` | 阻尼 | **0 … 5** | 12 | |

> ⚠️ 上位机与固件**必须用同一套范围常量**，否则数值对不上。改范围 = 两边一起改。

---

## 3. 量化函数（float ↔ 定点）

```c
// 浮点 -> 无符号定点
static inline uint32_t float_to_uint(float x, float xmin, float xmax, uint8_t bits)
{
    float span = xmax - xmin;
    if (x < xmin) x = xmin;
    else if (x > xmax) x = xmax;
    return (uint32_t)((x - xmin) * (((1 << bits) - 1) / span));
}

// 无符号定点 -> 浮点
static inline float uint_to_float(uint32_t xi, float xmin, float xmax, uint8_t bits)
{
    float span = xmax - xmin;
    return ((float)xi) * span / ((1 << bits) - 1) + xmin;
}
```

---

## 4. MIT 控制帧（主控→电机，ID = `MOTOR_ID`，8 字节）

控制律：`τ = kp·(p_des − p) + kd·(v_des − v) + t_ff`（p/v 为编码器实测位置/速度）。

打包：
```
p_int  = float_to_uint(p_des, -P_MAX, P_MAX, 16)
v_int  = float_to_uint(v_des, -V_MAX, V_MAX, 12)
kp_int = float_to_uint(kp,     0,     KP_MAX, 12)
kd_int = float_to_uint(kd,     0,     KD_MAX, 12)
t_int  = float_to_uint(t_ff,  -T_MAX, T_MAX, 12)
```
| 字节 | 内容 |
|---|---|
| data[0] | p_int[15:8] |
| data[1] | p_int[7:0] |
| data[2] | v_int[11:4] |
| data[3] | (v_int[3:0] << 4) \| kp_int[11:8] |
| data[4] | kp_int[7:0] |
| data[5] | kd_int[11:4] |
| data[6] | (kd_int[3:0] << 4) \| t_int[11:8] |
| data[7] | t_int[7:0] |

> 纯力矩：kp=kd=0，只给 t_ff。纯位置：给大 kp、适当 kd、p_des。纯速度：kp=0、给 kd 和 v_des。

---

## 5. 位置-速度帧（主控→电机，ID = `0x100 + MOTOR_ID`，8 字节）

| 字节 | 内容 |
|---|---|
| data[0..3] | p_des，IEEE754 float32，**小端** |
| data[4..7] | v_des，float32，小端 |

→ 映射到 `CONTROL_MODE_POSITION_RAMP`（input_position=p_des，input_velocity=v_des 作梯度限速）。

---

## 6. 速度帧（主控→电机，ID = `0x200 + MOTOR_ID`，4 字节有效）

| 字节 | 内容 |
|---|---|
| data[0..3] | v_des，float32，小端 |

→ 映射到 `CONTROL_MODE_VELOCITY_RAMP`（input_velocity=v_des）。

---

## 7. 特殊命令帧（主控→电机，ID = `MOTOR_ID`，8 字节，前 7 字节 0xFF）

| 命令 | 8 字节 | 动作（固件） |
|---|---|---|
| 使能 | `FF FF FF FF FF FF FF FC` | State_Mode → RUNNING（开 PWM，进入控制） |
| 失能 | `FF FF FF FF FF FF FF FD` | Foc_Pwm_LowSides + State_Mode → IDLE |
| 保存位置零点 | `FF FF FF FF FF FF FF FE` | 把当前编码器位置设为零点 |
| 清除错误 | `FF FF FF FF FF FF FF FB` | Fault_State → NORMAL，State_Mode → IDLE |

> ID=`MOTOR_ID` 的帧：先判断是不是这 4 条特殊命令（data[0..6] 全 0xFF），否则按 MIT 控制帧解析。

---

## 8. 反馈帧（电机→主控，ID = `MASTER_ID`，8 字节）

每收到一条控制/命令帧后回传一帧。

```
p_int = float_to_uint(pos,    -P_MAX, P_MAX, 16)   // 当前位置
v_int = float_to_uint(vel,    -V_MAX, V_MAX, 12)   // 当前速度
t_int = float_to_uint(torque, -T_MAX, T_MAX, 12)   // 当前力矩 = Iq · Kt
```
| 字节 | 内容 |
|---|---|
| data[0] | (ERR[3:0] << 4) \| (MOTOR_ID & 0x0F) |
| data[1] | p_int[15:8] |
| data[2] | p_int[7:0] |
| data[3] | v_int[11:4] |
| data[4] | (v_int[3:0] << 4) \| t_int[11:8] |
| data[5] | t_int[7:0] |
| data[6] | T_mos —— MOS 温度 (°C, uint8) |
| data[7] | T_rotor —— 电机/转子温度 (°C, uint8) |

### 错误码 ERR（高 4 位，沿用 DM 习惯）
| ERR | 含义 | 对应固件 `FAULT_STATE` |
|---|---|---|
| 0 | 失能 | (State_Mode==IDLE) |
| 1 | 使能/正常 | FAULT_STATE_NORMAL & RUNNING |
| 8 | 过压 | FAULT_STATE_OVER_VOLTAGE |
| 9 | 欠压 | FAULT_STATE_UNDER_VOLTAGE |
| A | 过流 | FAULT_STATE_OVER_CURRENT |
| B | 过温 | FAULT_STATE_OVER_TEMPERATURE |
| E | 超速/过载 | FAULT_STATE_SPEEDING |

---

## 9. 单位与内部换算（重要）

DM 协议**线上单位是 SI**：位置 rad、速度 rad/s、力矩 N·m。
FalconFOC 固件内部编码器单位是 **圈(turn) / 圈每秒(turn/s)**（实现时需核实 `encoder->pos_estimate_`）。

换算（在 dm_can 收发边界做）：
```
rad      = turn   × 2π          ;  turn   = rad      / 2π
rad/s    = turn/s × 2π          ;  turn/s = rad/s    / 2π
torque(N·m) = Iq(A) × Kt        ;  Iq = torque / Kt   (Kt≈0.0265)
```
> 收命令：rad→turn 后写入 Controller；发反馈：turn→rad 后打包。**线上始终是 rad，固件内部始终是 turn。**

---

## 10. 固件落地改动清单

1. `FOCMotor.h`：`CONTROL_MODE` 增加 `CONTROL_MODE_MIT`；`MOTOR_CONTROLLER` 增加 `input_kp`、`input_kd`。
2. 控制环（`FOCMotor.c` MotorControlTask）：新增 MIT 分支，
   `torque_setpoint = kp·(p_des−p) + kd·(v_des−v) + t_ff`，限幅到 `torque_limit`，转 Iq 喂电流环。
3. 新模块 `MODULES/can_comm/dm_can.c/.h`：
   - 注册 3 个 RX ID（`MOTOR_ID` / `0x100+ID` / `0x200+ID`），基于 `bsp_can` 的 `FDCANRegister`（**绕开** `can_driver.c` 多帧层）。
   - 解包三种控制帧 + 特殊命令；打包反馈帧。
   - float↔uint 量化、turn↔rad 换算。
4. `cmd_task.c`：用 dm_can 替换原 `FDCANComm` 自定义协议；放开被注释的反馈 TX。
5. `robot_def.h`：`MOTOR_ID`/`MASTER_ID` 定义；移除/弃用旧 `Cmd_Id_e`、`Cmd_Rx_s`。

---

## 11. SocketCAN 测试速查

```bash
sudo apt install -y can-utils
sudo ip link set can0 up type can bitrate 1000000        # 1Mbps
candump can0                                              # 看反馈
# 使能 (电机ID=0x01)
cansend can0 001#FFFFFFFFFFFFFFFC
# MIT 纯力矩 t_ff=+0.1N·m (kp=kd=0,p=v=0) —— 具体字节由量化算
# 速度模式 v_des=10rad/s (ID=0x201, float32小端 0x41200000 -> 00 00 20 41)
cansend can0 201#0000204100000000
# 失能
cansend can0 001#FFFFFFFFFFFFFFFD
```
