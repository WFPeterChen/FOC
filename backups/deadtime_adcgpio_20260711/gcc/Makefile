# FalconFOC —— GCC (arm-none-eabi) 构建
# 用法: 在 gcc/ 目录下 `make` 生成 build/FalconFOC.{elf,hex,bin}
######################################
TARGET    = FalconFOC
DEBUG     = 1
OPT       = -O2
BUILD_DIR = build
PROJ      = $(abspath ..)

#######################################
# 源文件
#######################################
# 工程自有源码(全部递归)
C_SOURCES  = $(shell find $(PROJ)/Core/Src $(PROJ)/APP $(PROJ)/MODULES $(PROJ)/BSP $(PROJ)/USB_Device -name '*.c')
# HAL 驱动(排除 *_template.c)
C_SOURCES += $(filter-out %_template.c,$(wildcard $(PROJ)/Drivers/STM32G4xx_HAL_Driver/Src/*.c))
# USB Device 库
C_SOURCES += $(wildcard $(PROJ)/Middlewares/ST/STM32_USB_Device_Library/Core/Src/*.c)
C_SOURCES += $(wildcard $(PROJ)/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/*.c)
# FreeRTOS 内核(仅顶层 Source/*.c) + GCC 端口 + heap + CMSIS-RTOS 封装
C_SOURCES += $(wildcard $(PROJ)/Middlewares/Third_Party/FreeRTOS/Source/*.c)
C_SOURCES += $(PROJ)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
C_SOURCES += $(PROJ)/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
C_SOURCES += $(wildcard $(PROJ)/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS/*.c)
# DSP shim
C_SOURCES += $(abspath arm_dsp_shim.c)

# 汇编启动文件
ASM_SOURCES = startup_stm32g431xx.s

#######################################
# 工具链
#######################################
PREFIX = arm-none-eabi-
CC  = $(PREFIX)gcc
AS  = $(PREFIX)gcc -x assembler-with-cpp
CP  = $(PREFIX)objcopy
SZ  = $(PREFIX)size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

#######################################
# CPU / FPU
#######################################
CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

#######################################
# 宏定义
#######################################
C_DEFS = -DUSE_HAL_DRIVER -DSTM32G431xx -DARM_MATH_CM4 -DARM_MATH_MATRIX_CHECK -DARM_MATH_ROUNDING

#######################################
# 头文件目录
#######################################
C_INCLUDES = \
-I$(PROJ)/Core/Inc \
-I$(PROJ)/APP -I$(PROJ)/APP/cmd_task -I$(PROJ)/APP/motor_task \
-I$(PROJ)/BSP -I$(PROJ)/BSP/adc -I$(PROJ)/BSP/can -I$(PROJ)/BSP/dwt -I$(PROJ)/BSP/flash -I$(PROJ)/BSP/log -I$(PROJ)/BSP/usart \
-I$(PROJ)/MODULES -I$(PROJ)/MODULES/algorithm/crc -I$(PROJ)/MODULES/can_comm -I$(PROJ)/MODULES/controller \
-I$(PROJ)/MODULES/daemon -I$(PROJ)/MODULES/encoder/MT6816 -I$(PROJ)/MODULES/flash -I$(PROJ)/MODULES/led \
-I$(PROJ)/MODULES/motor -I$(PROJ)/MODULES/vofa \
-I$(PROJ)/Drivers/STM32G4xx_HAL_Driver/Inc -I$(PROJ)/Drivers/STM32G4xx_HAL_Driver/Inc/Legacy \
-I$(PROJ)/Drivers/CMSIS/Device/ST/STM32G4xx/Include -I$(PROJ)/Drivers/CMSIS/Include \
-I$(PROJ)/Middlewares/ST/ARM/DSP/Inc \
-I$(PROJ)/Middlewares/ST/STM32_USB_Device_Library/Core/Inc \
-I$(PROJ)/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
-I$(PROJ)/Middlewares/Third_Party/FreeRTOS/Source/include \
-I$(PROJ)/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS \
-I$(PROJ)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F \
-I$(PROJ)/USB_Device/App -I$(PROJ)/USB_Device/Target

#######################################
# 编译/链接标志
#######################################
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
ifeq ($(DEBUG),1)
CFLAGS += -g -gdwarf-2
endif
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LDSCRIPT = STM32G431CBUx_FLASH.ld
LIBS = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

#######################################
# 构建规则
#######################################
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(BIN) $< $@

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR):
	mkdir -p $@

clean:
	-rm -rf $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)
.PHONY: all clean
