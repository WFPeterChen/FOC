# FalconFOC USB 虚拟串口 上位机接口 (2026-07-05 打通)

板子的 STM32 原生 USB(CDC 虚拟串口)已作为**上位机通道**打通,双向。适合写原生上位机
(比 CAN 带宽大、免 USB-CAN 适配器)。CAN 通道不受影响,两者可并存。

## 端口 (已做 udev 固化)
- 稳定名: **`/dev/falconfoc`**(符号链接, ttyACM 号怎么变都指向它), 免 sudo 可读写。
- udev 规则: `/etc/udev/rules.d/99-falconfoc.rules`
  `SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5740", SYMLINK+="falconfoc", MODE="0666", GROUP="plugdev"`
- 波特率随意(USB-CDC 忽略), 代码里用 2000000 只是占位。
- ⚠️ 坑记录: 板子复位会重枚举 → ttyACM 号会变(常出现 stale 的 ttyACM0 死节点); **一律用 `/dev/falconfoc`**, 别写死 ttyACMx。

## 收 (板 → 上位机): VOFA JustFloat, ~1kHz
每帧 = 12 个小端 float32 + 帧尾 `00 00 80 7F` (共 52 字节)。通道顺序:
| # | 通道 | # | 通道 |
|---|---|---|---|
| 0 | i_a [A] | 6 | id_set [A] |
| 1 | i_b [A] | 7 | vel [rad/s] |
| 2 | i_c [A] | 8 | pos [rad] |
| 3 | i_q [A] | 9 | phase [elec rad] |
| 4 | i_d [A] | 10 | temp [°C] |
| 5 | iq_set [A] | 11 | vbus [V] |
解析: 找帧尾 `00 00 80 7F`, 其前 48 字节即 12 个 float(`struct.unpack('<12f', ...)`)。
固件位置: MODULES/vofa/vofa.c `Vofa_Packet()`; 开关在 APP/robot_task.h 的 `vofa_start()`(StartDefaultTask 1ms 调一次)。

## 发 (上位机 → 板): ASCII 命令
固件位置: MODULES/vofa/vofa.c `vofa_Receive()`。发字符串即可(含数字, atof 解析), 建议以 `\n` 结尾:
- `set_Iq=<A>` / `set_Id=<A>` — 直接设 iq/id 设定
- `set_torque=<Nm>` — MIT/力矩前馈
- `set_vel=<rad/s>` / `set_pos=<rad>` — 速度/位置设定
- `set_ctrl_mode=<n>` — 切控制模式(校验 OPEN..POSITION_RAMP 范围)
- `set_current_ctrl_bw=<Hz>` — 电流环带宽
- `set_vel_kp= / set_vel_ki= / set_pos_kp=` — 在线调 PID
- `set_telem=<0/1>` — 遥测流开/关(关了省带宽)
- `set_telem_div=<N>` — 遥测降速, 每N帧发1(1=1kHz, 10≈100Hz, 范围1..1000)
- `calib=1` — 触发重标定(慎用)
实测: 发 `set_Iq=0.222` → VOFA 的 iq_set 通道即变 0.222(idle 下不驱动电机, 安全)。

## 命令口硬化 (2026-07-05 已做)
- ✅ **null 终止**: `vofa_Receive` 现在 bound len + `receive_buf[len]='\0'`, strstr/atof 不再越读。
- ✅ **NaN/Inf 拒绝 + 钳位**: 所有数值经 `vofa_sanitize()`——非有限值→0; 再钳到安全范围:
  Iq/Id ±电流限, torque ±(电流限·Kt), vel ±SPEED_MAX_LIMIT, pos ±1e5, 带宽[0,1e4], 增益[0,1e5]。
  实测: `set_Iq=999`→4.0, `set_Iq=-999`→-4.0, `set_Iq=inf`→0。
- 建议上位机仍以 `\n` 结尾发送(习惯好); 但即使不带、即使发超长/垃圾, 固件也不会越界了。

## 现状
- ✅ TX(遥测)12通道@998Hz; RX(命令)钳位+消毒+null终止; 遥测可开关/降速; /dev/falconfoc 免sudo。
- ✅ CAN(DM二进制协议)与 USB(VOFA遥测+ASCII命令)并存, 互不影响。两套是不同协议, 各走各口。
