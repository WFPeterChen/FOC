# OpenOCD for STM32G4 - mjbots blog

URL: https://blog.mjbots.com/2019/10/04/openocd-for-stm32g4/

While bringing up an STM32G4 for the new revision of the moteus controller, I wanted to be able to flash and debug the system, and thus needed a working OpenOCD installation.  The NUCLEO-G474RE board has an ST-LINK-V3 debug interface, which no released version of OpenOCD supports, although thankfully that is working just fine at HEAD in git.  However, to make the STM32G4 work I had to pull some patches from the sysprogs/openocd tree.

My resulting work can be found at: https://github.com/mjbots/openocd

Hopefully the dependent OpenOCD gerrit review will eventually land: http://openocd.zylin.com/#/c/4932/ which will clear the way for getting G4 support into master there.

For now, I’ll just live with a custom compiled OpenOCD.
