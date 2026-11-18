FalconFOC 固件+标定备份 — 2026-07-03 (齿槽重标定完成版) —— 板子当前就是这版
=============================================================================

内容:
  FalconFOC.elf / .bin       — 已烧录并验证(program时Verified OK)
  firmware_src.tgz           — 源码快照
  flash_calib_page63.bin     — flash page63 运行时标定 dump(2KB: Rs/Ls/offset/pp/dir/lut/has_cogging)
  flash_cogging_page62.bin   — flash page62 齿槽图 dump(2KB: cogging_map[512])
  ↑ 这两个是【运行时数据】, 源码里没有。若 flash 再被擦, 直接写回即可恢复标定, 不用重跑23min:
     openocd ... -c "flash write_image erase flash_calib_page63.bin 0x0801F800" \
                 -c "flash write_image erase flash_cogging_page62.bin 0x0801F000"

本版做了什么(2026-07-03):
  1. 重做了一张好的齿槽图(cogging map)—— 512点×2方向静态保持法, ~23min, 空洞仅5/512(0.98%)。
     标定在 PRE_CALIBRATED=1 下跑, 所以存进 flash 的 Ls=171µH(RAM好值), 绕开了不可靠的现场L辨识。
  2. 加 MotorCalib_LoadCoggingOnly(): PRE_CALIBRATED=1 开机时, 电机参数仍用编译期常数(确定性、无坏Ls兜底风险),
     但齿槽图从 flash page63/62 加载 → 齿槽FF 开机即生效。两全其美。
  3. 加 deadman 跳过: 齿槽标定期间(g_cogcal_phase!=0)不触发通信掉线保护(否则200ms无CAN即中断标定);
     OC/OV/超速等硬件保护仍在; 正常运行 deadman 照常触发(已验证)。

关键成果 —— MIT 位置相关振动【已解决】:
  之前(无齿槽FF): MIT保持在某些转子位置剧振(iq_pp~7.5A, @0.8/2.9/4.0rad), 某些位置平顺。
  现在(齿槽FF开机自动加载): 全位置平顺, iq_pp 0.45~0.98A, tau_std<0.006。剧振位置降到<0.8A。
  → 根因确认 = 位置相关齿槽扰动 vs Kp阻抗弹簧的极限环; 准确齿槽FF抵消之。非采样电阻/非PLL/非Ls。

配置: PRE_CALIBRATED=1 (Rs=0.187/Ls=171µH/offset=15702/pp=7/dir=CW 编译期rb段) + 开机加载flash齿槽图。
  ENCODER_PLL_BANDWIDTH=2000, MIT_KD_TURN_MAX=0.1, MT6816读=MODE0阻塞。
验证(本版, 干净开机零手动标定): Ls=171µH, cogging_valid=1, MIT全位置平顺, pos误差0.000, 扫频2→40干净, deadman正常。

备份目录一览:
  known_good_20260701_0929/     昨天基线(flash标定已被后续mass_erase清掉, 仅固件可用)
  dma_investigated_20260702/    DMA调查版
  precal1_restored_20260702/    PRE_CAL=1恢复版(无齿槽, MIT位置相关振动)
  cogging_restored_20260703/    本版(当前运行, 齿槽FF开机自动加载, MIT全位置平顺) ★推荐
