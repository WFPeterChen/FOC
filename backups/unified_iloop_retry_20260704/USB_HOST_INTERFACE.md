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
- `set_ctrl_mode=<n>` — 切控制模式
- `set_current_ctrl_bw=<Hz>` — 电流环带宽
- `set_vel_kp= / set_vel_ki= / set_pos_kp=` — 在线调 PID
实测: 发 `set_Iq=0.222` → VOFA 的 iq_set 通道即变 0.222(idle 下不驱动电机, 安全)。

## 现状 / 待办
- ✅ TX(遥测)12通道@998Hz 实测稳定; RX(命令)实测生效; /dev/falconfoc 免sudo。
- ⚠️ 注意: `vofa_Receive` 的 `receive_buf` memcpy 后未强制 null 结尾就 strstr —— 上位机发命令**务必带 `\n` 或 `\0` 终止**, 且别发超长串(MAX_RXBUFFER_SIZE)。要更稳可日后给固件加边界/终止符处理。
- 想要的话可把 CAN 那套 DM 协议也从这个口走(二选一或并存), 但当前 USB 用的是 VOFA(float遥测)+ASCII命令这套, 和 CAN 的 DM 二进制协议是两码事。
