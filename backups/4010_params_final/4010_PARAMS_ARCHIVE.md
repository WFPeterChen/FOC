# 4010 电机参数终版归档(2026-07-12 退役封存)

> 换 5210(24N28P)前的完整快照。要换回 4010:按本档恢复 rb 块常数 + 烧回 flash 页(见下)。
> 配套完整源码快照: `backups/24v_deadtime_tuned_20260712/`(含当时部署的 FalconFOC_deployed.bin)。

## 电机本体
| 项 | 值 | 来源 |
|---|---|---|
| 型号 | 4010 航模电机, 12N14P | 磁铁14/槽12 |
| 极对数 MPTOR_P | **7** | 标定确认 |
| KV 铭牌/实测 | 360 / **379**(等效) | bEMF 真标 2026-07-11 |
| **λ_ph** | **2.40 mWb**(±0.5%平坦度, 3次独立测量一致) | bEMF 手挥法 |
| **Kt = MOTOR_TORQUE_CONST** | **0.0252 N·m/A** | =1.5·7·λ |
| bEMF THD | ≤1.7%(5次谐波1.5%主导), 高度正弦 | 示波器 |

## rb 块常数(BLDCMotor.h, PRE_CALIBRATED=1 生效值)
```c
#define MPTOR_P 7u
#define MOTOR_RS 0.18715331f        // 187.2 mΩ
#define MOTOR_LS 0.00017139669f     // 171.4 µH (⚠️on-board LS标定曾出3.36mH错20×, 此值为已知好值)
#define MOTOR_FLUX 0.0f
#define MOTOR_OFFSET 15702          // 编码器电角零位
#define MOTOR_DIRECTION CW
#define MOTOR_TORQUE_CONST 0.0252f
#define MOTOR_TORQUE_LIMIT 0.252f   // =10A×Kt 含义
#define MOTOR_CURRENT_LIMIT 4.0f    // bench限流电源匹配; 真电池+保险丝后10A
#define MOTOR_VEL_LIMIT 50.0f
#define MOTOR_VEL_PID_KP 0.8f       // 已验证稳定
#define MOTOR_VEL_PID_KI 0.02f
#define MOTOR_POS_PID_KP 120.0f
#define MOTOR_CURRENT_CTRL_BANDWIDTH 230.0f
```

## flash 标定页(板上 dump, 本目录 .bin)
- `flash_page63_calib.bin` @0x0801F800: magic 0xCA11B007 + {Rs/Ls/flux/enc零位15702/pp7/CW + offset_lut[128]}
- `flash_page62_cogging.bin` @0x0801F000: 齿槽map[512](静态保持法, 主导14/rev 0.088A, 极限标定版) + friction_map
- 恢复: openocd `flash write_image <bin> <addr>`(或 load_image), 配 PRE_CALIBRATED=0 使用

## 行为特性(实测结论存档)
- **MIT kd 窗口 0.005~0.016**: 上限=Kd/Kt天花板; 下限=47Hz欠阻尼共振(kp=2, J≈2.3e-5 kg·m²)
- MIT保持噪声本底: 12V 2.6~7.2 / 24V ~11 mNm, 随转子停位(齿槽14/rev)±30%, ∝vbus(死区抖动主导)
- 齿槽FF: 速度/位置环下无用(环路自抑), 力矩/MIT软环下↓82%纹波
- 摩擦前馈 g_friction_ff 默认0(未调)
- 速度纹波@高速主要=齿槽(电子噪声floor 23mA std@12V)

## 板级参数(与电机无关, 留在固件里不动)
死区补偿v3(0.7×430ns/参考符号/0.3A带/1rad/s门控)、N_BASE=6(24V)、OV27/UV19.8、
IWDG~250ms、OC快速跳闸 g_oc_trip_a=2×limit、PA7调试脚、CLEARERR清残留指令(2026-07-12修)。
