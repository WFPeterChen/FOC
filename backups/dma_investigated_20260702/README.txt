FalconFOC 固件备份 — 2026-07-02 (DMA 调查版)
=================================================

内容:
  FalconFOC.elf / .bin  — 今天的构建(默认 MT6816_READ_MODE=0 阻塞读)
  firmware_src.tgz      — 对应源码快照(Core/ MODULES/ APP/)

与昨天基线 known_good_20260701_0929 的关系:
  - 电机运行行为【完全一致】。默认走 MODE0 阻塞读, 与昨天同一段代码;
    PLL首采样修复 / GetMotor_Angle / 角度链均相同。
  - 唯一差异 = 3 个文件(spi.c / stm32g4xx_it.c / mt6816_encoder.c)里新增的
    SPI3-DMA 脚手架 + MT6816_READ_MODE 读路径开关 + 诊断钩子。这些在 MODE0 下休眠。
  - 因此本版【无运行时优势】; 价值是保留了 DMA 调查成果与 SPI3 修复, 供将来接
    示波器时用(把宏切到 1/2)。

DMA 调查结论(详见 memory/falconfoc-encoder-dma-wip.md):
  - 编码器在 SPI3(不是引出的 GH1.25 SPI1 座子)。DMA 传输本身正确(A/B对拍验证)。
  - "高字节丢0x00"根因 = 冷启第一帧丢首字节, 加预热帧可修。
  - 但 MODE1(异步,~21µs) 角度延迟1~2拍 → 高速OC, 不可用;
    MODE2(同步+预热,~29µs) 正确但不比阻塞快。故定案 MODE0。
  - 想省ISR的原动机(91%CPU饿死FDCAN/时基/deadman)已由中断优先级修复独立解决。

验证(MODE0, 本版): pos 误差0.001rad, 速度扫频2→40rad/s干净, deadman正常。
  注: MIT模式抖动(std~0.07)是机械/温度问题, 昨天备份同样如此, 非固件回归。

注意: 板子当前烧的是【昨天】的 known_good_20260701_0929, 不是本版。
  要用本版: openocd ... program FalconFOC.elf verify reset exit
