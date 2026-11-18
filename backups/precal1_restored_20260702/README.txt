FalconFOC 固件备份 — 2026-07-02 (PRE_CALIBRATED=1 恢复版) —— 板子当前就是这版
=============================================================================

内容:
  FalconFOC.elf / .bin  — PRE_CALIBRATED=1, 已烧录并功能验证(program时Verified OK)
  firmware_src.tgz      — 对应源码快照

背景: 当天排查 MIT 振动过程中, 2MHz SWD 烧录一度损坏 flash → mass_erase 恢复 →
  清掉了原厂标定(含 cogging 图 + offset_lut)。现场重标又量出错误 Ls(4.4e-5 vs 实际1.7e-4)
  → 电流环增益错 → MIT 零流保持自激OC。故改用编译期常数(见下), 绕开不可靠的现场L辨识。

关键改动 (相对 known_good_20260701_0929):
  - PRE_CALIBRATED = 1 (原0): 开机用本文件 rb 段编译期常数, 不读flash、不空转标定。
      Rs=0.18715, Ls=1.71e-4, offset=15702, pp=7, dir=CW (与原厂flash一致; offset与现场标定电角度等价)。
      代价: 无 offset_lut(编码器线性化) / 无 cogging 前馈图。
  - ENCODER_PLL_BANDWIDTH 保持 2000 (排查中试过1000/4000, 定论与PLL无关, 已复原)。
  - MIT_KD_TURN_MAX 保持 0.1 (排查中临时开到1.0, 已复原)。
  - MT6816 读: MODE0 阻塞读(DMA 脚手架仍在, 见 falconfoc-encoder-dma-wip)。

功能验证(本版): pos 误差0.001rad, 速度扫频2→40rad/s干净, deadman正常, 开机干净。

MIT 振动定论(用户主诉): 位置相关极限环 —— 有些转子位置平顺(iq_pp~1A), 有些剧振(iq_pp~7.5A)。
  成因 = 齿槽/摩擦位置相关扰动 与 Kp 直通阻抗环(无速度内环阻尼)相互作用; 低Kt+12N14P强齿槽放大。
  非采样电阻(已排除)、非PLL、非Ls。真正的解 = 一张准确的 cogging 前馈图(未做)。详见 memory/falconfoc-mit-kd-limit。

landmine: flash page63 仍存着那次坏标定(坏Ls), PRE_CALIBRATED=1 会忽略它。
  若日后改回 PRE_CALIBRATED=0, 必须先重标覆盖或擦除 page62/63, 否则会加载坏Ls致MIT OC。

备份目录一览:
  known_good_20260701_0929/     昨天基线(PRE_CALIBRATED=0, 含cogging; 但flash标定已被本日mass_erase清掉!)
  dma_investigated_20260702/    DMA调查版(功能≈昨天)
  precal1_restored_20260702/    本版(当前板上运行, 唯一开机即可用、不依赖flash标定的)
