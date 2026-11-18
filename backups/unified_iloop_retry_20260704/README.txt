FalconFOC 固件+标定备份 — 2026-07-04 (统一电流环再试 + 相位超前假说验证) —— 板子当前就是这版
=================================================================================================

内容:
  FalconFOC.elf / .bin       — 已烧录并验证(Verified OK)
  firmware_src.tgz           — 源码快照(Core/MODULES/APP)
  CURRENT_LOOP_INVESTIGATION.md / CALIBRATION_AUDIT.md — 相关分析文档快照
  flash_calib_page63.bin     — flash page63 运行时标定 dump(2KB)
  flash_cogging_page62.bin   — flash page62 齿槽图 dump(2KB)
  ↑ flash 再被擦可直接写回恢复, 不用重跑标定:
     openocd ... -c "flash write_image erase flash_calib_page63.bin 0x0801F800" \
                 -c "flash write_image erase flash_cogging_page62.bin 0x0801F000"

本版在 cogging_restored_20260703 基础上做了什么(2026-07-04):
  1. 【统一电流环 #5 再试】加回 A/B 在线开关 g_dbg_unified_iloop(0=A默认 / 1=B 标定同款 FOC_current)。
     修了 FOC_current 让对比"公平"的 4 个真缺陷:
       (a) 双 Ts 积分 bug(i_gain 已含 Ts 又乘一次)→ 改对
       (b) 缺死区补偿 → 补 Deadtime_Compensate
       (c) 缺 bEMF/ωL 前馈 → 加 vd_ff/vq_ff 入参(与 A 同源)
       (d) 致命: foc->current_ctrl_p/i_gain 从没同步过(=0), 且 FOC_reset 每次清零
           → 在 B 分支每拍从 Controller 同步。不修则 B 零增益、位置飘走(会误判B烂)。
  2. 加 SWD 旋钮验证"相位超前×齿槽噪声"假说: g_padv_k(相位超前缩放,0=关) / g_padv_filt(相位超前速度低通)。

关键结论(见 CURRENT_LOOP_INVESTIGATION.md 第5节):
  - 修公平后: 低速 A≈B(B 甚至略好), 高速 B 差 ~30%(40rad/s: A≈2.3 vs B≈3.0 速度跟踪 std)。
  - 【假说被推翻】把相位超前完全关掉(g_padv_k=0), 高速 B 仍≈2.95, 没变好 → 相位超前不是元凶。诚实负结果。
  - 高速劣化根因未明(只剩 积分泄放抗饱和 / svm 边界 之别, 需示波器看相电流, SWD 层到此为止)。
  - B 另有起转鲁棒性问题(sweep 低速起始约 1/3 概率堵转)。
  → 工程决策: 默认继续走 A。B 修成功能正确、挂开关备查, 换低齿槽电机再评估。

SWD 旋钮地址(本 elf, 供实验; 默认值即部署态):
  g_dbg_unified_iloop @ 0x2000377c  (0=A默认)
  g_padv_k            @ 0x20000438  (float, 1.0=正常; 0=关相位超前)
  g_padv_filt         @ 0x20003778  (float, 0=不滤波)
  切B示例: mww 0x2000377c 1 ; 关相位超前: mww 0x20000438 0x00000000

验证(本版, 干净开机): A路 pos 误差 0.001, 扫频 2→40 干净(40=1.62), cogging FF 开机加载, Ls=171µH。

⚠️ 硬件提醒: 本次烧录期间 SWD 排线接触不良 + J-Link 卡 2MHz, 两度把 flash 擦坏进 lockup,
   靠"重插排线+断电重上电"恢复(见 falconfoc-swd-dfu-access)。再深挖电流环前建议换好线/上 ST-Link。

备份目录一览:
  known_good_20260701_0929/     昨天基线
  dma_investigated_20260702/    DMA调查版
  precal1_restored_20260702/    PRE_CAL=1恢复版(无齿槽)
  cogging_restored_20260703/    齿槽FF开机自动加载, MIT全位置平顺
  unified_iloop_retry_20260704/ 本版(当前运行): 齿槽 + 唱歌(简单爱) + 统一电流环B开关+相位超前旋钮; 默认A ★

--- 追加 2026-07-04 晚: 安全补丁 (查缺补漏) ---
本 elf 又加了3个安全补丁(见 SAFETY_HARDENING.md), 已实测不影响正常运行:
  1. 4个CPU故障处理器 首行关PWM (TIM1 MOE=0) —— 异常不再带电失控
  2. IWDG独立看门狗 ~250ms (StartGuardTask喂, DBGMCU冻结防调试误复位) —— 挂死兜底复位
  3. 20kHz ISR 快速硬过流 g_oc_trip_a@0x20000438 (默认8A=2×电流限, SWD可调/0关)
验证: 3s连续保持无掉线(IWDG不误复位), 扫频到40+保持无OC误触发(guard_reason=1非7), pos误差0.001。

--- 追加 2026-07-04 深夜: 功能改善 ---
5. SET_ZERO(0xFE) 实现(原是空TODO): 软件偏移把当前物理位置设为新零点(pos_zero_offset), 同时归零位置设定→位置模式不跳变。
   实测: 移到2.0→SET_ZERO→读0.00→新零点下pos1.0→0.999。不动flash编码器零位, 掉电复位后归零丢失(纯运行时)。
6. 摩擦前馈(库仑摩擦补偿, 默认关): 力矩/MIT模式 iq_set += g_friction_ff·tanh(vel/g_friction_vth)。
   提高低速/反向驱动顺滑度→更接近 τ=Kt·iq 理想, 利sim2real。SWD旋钮 g_friction_ff@(nm查, 默认0=关)/g_friction_vth(默认0.2turn/s)。
   实测0.08A受控不跑飞; 需上台按反向驱动手感微调(建议0.05起)。默认0=部署行为不变。

--- 追加 2026-07-05: USB 虚拟串口上位机通道打通 ---
本版 vofa_start() 已启用(APP/robot_task.h), USB-CDC 作为正式上位机通道:
  TX: VOFA JustFloat 12通道@1kHz; RX: ASCII命令(set_Iq/vel/pos/ctrl_mode...)。实测双向OK。
  端口固化: /dev/falconfoc (udev规则 /etc/udev/rules.d/99-falconfoc.rules, VID:PID 0483:5740)。
  详见仓库 USB_HOST_INTERFACE.md。之前"0字节"是读错stale端口+权限, 非固件问题。
注: 本次曾因克隆J-Link不稳把flash烧坏进lockup, 换ST-Link读不到核(SWDIO/SWCLK接线错位),
    最终J-Link(线对)一次烧回救活。教训: SWD随机掉线先停别硬烧; 救砖优先ST-Link+CubeProgrammer。
