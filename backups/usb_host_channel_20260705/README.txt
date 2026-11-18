FalconFOC 备份 — 2026-07-05 「USB上位机通道打通 + 砖机救回」 —— 板子当前就是这版
================================================================================
本版 = unified_iloop_retry_20260704 的全部成果 + 以下:
  • USB-CDC 上位机通道打通(vofa_start 已启用): TX=VOFA 12通道@1kHz, RX=ASCII命令。见 USB_HOST_INTERFACE.md
  • 端口固化 /dev/falconfoc (udev: 99-falconfoc.rules, VID:PID 0483:5740, 免sudo)
累计功能: 齿槽FF/唱歌(简单爱)/统一电流环B开关/相位超前旋钮/5个安全补丁/SET_ZERO(0xFE)/摩擦前馈(默认关)/USB遥测+命令
默认部署行为: 摩擦前馈关、统一电流环走A、VOFA开。CAN(DM协议)与USB并存。
标定: page63(Rs/Ls/flux/编码器零位/pp/dir/offset_lut/cogging) 有效, PRE_CALIBRATED=0 开机加载。
恢复烧录(J-Link线要对/或用ST-Link+CubeProgrammer):
  openocd -f interface/jlink.cfg -c "transport select swd" -c "adapter speed 200" \
    -f target/stm32g4x.cfg -c "init" -c "reset halt" -c "program FalconFOC.elf verify reset" -c "shutdown"
flash坏了可回写标定: flash write_image erase flash_calib_page63.bin 0x0801F800 (+ cogging 0x0801F000)
⚠️ 血泪教训: 克隆J-Link不稳(USB/SWD都掉过)→烧写中途掉线把flash烧坏进lockup; SWD随机掉线要"先停别硬烧";
   救砖: 优先ST-Link(注意SWDIO/SWCLK按标签接, 别按位置挪)+STM32CubeProgrammer(mode=UR)。

--- 追加 2026-07-05(晚): ISR降载 + USB命令口硬化 ---
• MT6816_READ_MODE=3 新增「寄存器级阻塞读」(mt6816_encoder.c): 绕开HAL开销, FOC ISR typical 30.1→25.2µs(省5µs/16%)。
  仍同步实时(无DMA延迟/无EOT握手坑)。实测: 静止pos std 2e-5, 双向定位+2→40扫频完美, 15s变速 rx_err=0/check_err=0。
  峰值66µs不变(那是FDCAN抢占非FOC计算量)。MODE 0(HAL阻塞)源码保留, 改#define即可回退。
• USB命令口硬化(vofa.c): null终止+NaN/Inf拒绝+全数值钳位; 加 set_telem=/set_telem_div= 遥测开关/降速。
