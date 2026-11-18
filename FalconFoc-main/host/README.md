# DM 电机 CAN 上位机 (dm_motor_gui.py)

带 UI、走 SocketCAN 的上位机，和固件 `MODULES/can_comm/dm_can.c` 的 DM 协议对齐。
对标社区工具(ODrive liveplotter / SimpleFOC Studio / moteus tview)的功能集，但**只用 python-can + 内置
tkinter**，不引入 matplotlib/numpy。单文件，零额外依赖。

## 功能 (4 个标签页)
- **控制 + 示波器**：使能/失能/清错/置零/齿槽标定；MIT 5 滑块阻抗(可存/读 JSON 预设)；快捷单发速度/位置-速度。
  多信号实时示波器(位置/速度/力矩/指令，每通道独立自动量程、可勾选、调时间窗、暂停、清空)；一键**录制反馈到 CSV**。
- **波形发生器**：const/step/square/sine/ramp/**chirp(扫频)** 注入 速度帧 / 位置帧 / MIT(P/V/T_ff)，发送率可调。
  做阶跃响应、扫频、齿槽激励、调参用；MIT 目标可单独设 Kp/Kd 看不同刚度下的响应。
- **频谱 FFT**：纯 Python FFT，对 速度/力矩/位置 做实时频谱，看纹波是**离散极限环(尖峰)**还是**宽带噪声**——诊断抖动神器。
- **CAN 日志**：收发帧十六进制 + 时间戳，像 tview 的 console。
- 顶部常驻：连接、反馈条(状态/位置/速度/力矩/温度/**帧率Hz**)。

## 安装
```bash
sudo apt install python3-tk python3-can     # tkinter(UI) + CAN
```

## CAN 自动拉起 (本机 gs_usb/candleLight 适配器)
**懒人法(GUI 内)**: 直接 `python3 dm_motor_gui.py` → 点「连接」。若 `can0` 没起, GUI 会用 **pkexec**(弹图形
授权框)自动 `up @1Mbps`; 也可单独点「拉起CAN」按钮。

**持久法(开机/插入自动起, 跑一次)**:
```bash
sudo ./can-autoup-setup.sh          # 默认 1Mbps; 可传参 sudo ./can-autoup-setup.sh 500000
```
装的是 systemd-networkd 的 `[Match] Type=can` 规则, 只接管 CAN 接口, 不动 NetworkManager 的其它网卡。
之后插上适配器/开机 can0(can1) 自动 up, GUI 直接连得上。

## 用
```bash
python3 dm_motor_gui.py
```
界面「连接」→「使能」→ 玩 MIT 滑块（勾"连续发送@50Hz"才有阻抗手感）。示波器实时画速度/力矩/位置。

## 反馈是请求-应答式 (重要)
固件**收到一帧命令才回一帧反馈**(`dm_can.c` 的 `DM_SendFeedback`)，**不主动周期上报**——和真实 DM 电机一致。
所以单点一下命令只会闪一帧反馈。要连续看反馈/示波器动起来，二选一：
- 勾「**持续查询反馈@30Hz**」(默认开)：周期发零副作用查询帧 `ffffffffffffff00`(未定义命令字→固件
  `default` 什么都不做，但仍回一帧反馈)，**不驱动电机、不改控制模式**，纯看反馈用。
- 勾「**连续发送@50Hz**」：持续发 MIT，**会真的驱动电机**，同时反馈 ~50Hz。
(两者都勾时以 MIT 优先，避免互相覆盖。)

## 协议要点(对齐固件)
- 电机 ID=0x01；反馈 id=0x00。线上单位 SI：位置 rad / 速度 rad·s⁻¹ / 力矩 N·m。
- MIT 帧(id 0x01)：tau = Kp·(P−p) + Kd·(V−v) + T_ff。量程 P±50rad、V±300rad/s、T±0.3N·m、Kp 0~500、Kd 0~5。
- 速度帧 id=0x201(float32 LE rad/s)；位置-速度帧 id=0x101(float32 p + float32 v)。
- 特殊命令(id 0x01, 前7字节 0xFF)：0xFC 使能 / 0xFD 失能 / 0xFE 置零 / 0xFB 清错 / 0xFA 齿槽标定(扩展)。

## MIT 模式抖动 — 根因(SWD 实测定论)
现象: 发 MIT 指令电机疯狂振动(~200Hz 极限环, 速度 std~3rad/s); 纯位置/速度没问题。
**根因: Kd 阻尼增益对本电机太高 —— 不是更新率, 是 `÷torque_const` 把增益放大了。**
- MIT 里 `iq = Kd·(v_des−v) / Kt`。本 4010 `Kt≈0.0265 N·m/A` 很小 → `Kd/Kt` 把"电流/转速"等效增益放大 **~38×**。
- 速度阻尼环的等效电流增益超过约 **3.8 A/(turn·s⁻¹)** 时, 在 ~200Hz(PLL 带宽 318Hz 附近)相位穿越自激成极限环。
- SWD 实测稳定上限: **`input_kd ≈ 0.1`**(0.1稳、0.15炸) → **CAN `Kd ≈ 0.1/2π ≈ 0.016 N·m/(rad/s)`**。
  用户发的 `Kd=0.3` → `input_kd=1.885`, **超上限 ~18×** → 必振。
- 对照: 纯速度环 `Kp=0.8`(电流/转速增益, 不除 Kt)静止时 std~0.1rad/s 稳如止水 —— 当年误以为是 20kHz 速率, 实为增益。
- **这是物理上限**: 小 `Kt`+小转动惯量的电机做高 Kd 阻抗本身受限。真实 DM 电机 Kt 大~10×、惯量大, 同 Kd 才稳。
  → sim2real 要么按 4010 真实 Kt 重训/缩放 Kd, 要么换大 Kt 电机。固件加滤波最多把上限抬 2~4×, 抬不到 18×。
- **现在能用的**: 手动玩把 `Kd` 压到 **≤0.015**(GUI 已把 Kd 滑块限到安全区并标了红线), 阻抗手感主要靠 `Kp`/`T_ff`。
- **诊断工具**: 「波形发生器」给定 Kp/Kd → 「频谱 FFT」看速度谱, 尖峰=极限环(降 Kd 让峰消失)。

## ⚠️ 注意
- **本板 CAN 收发器物理不稳**(整个调试期都没稳定过)，UI 连上但「无反馈」多半是收发器/接线，不是软件。
  排查见 `2.Firmware/FalconFoc` 记忆里的 CAN 章节；标定/调试本身可全走 SWD。
- 想玩齿槽前馈效果：用 MIT 模式给 `T_ff` 一点恒推力 + 小 `Kd` 让它慢转，对比有没有补偿(固件里 FF 已在力矩/MIT 模式自动生效)。
