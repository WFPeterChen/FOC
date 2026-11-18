# customizable PWM rate for moteus - mjbots blog

URL: https://blog.mjbots.com/2022/03/12/customizable-pwm-rate-for-moteus/

Being a switch mode 3 phase motor driver, the moteus controller changes the current flowing through the phases of a motor by rapidly switching the phase terminals between the positive input voltage and the input ground. The control of this switching is denoted “pulse width modulation”, or PWM for short. To date, the rate at which it has switched has been fixed in firmware at 40kHz. As of release 2022-03-12, this can now be altered anywhere between 15kHz and 60kHz to better optimize peak power capability, control bandwidth, maximum speed, and heat generation.

Read on for more details:

## Peak power

Switch mode motor controllers typically have large banks of capacitors across the input immediately next to the active switching elements. This bulk capacitance has the primary goal of minimizing voltage ripple during switching events. When a switch is engaged to apply more current into the motor, that energy is pulled from the capacitors, and when a switch is engaged that pulls energy back out, that energy is stored back into the capacitors. Only more slowly is the energy pulled to and from the primary input terminal. The magnitude of the ripple is determined by the effective capacitance, the power applied to the motor, and the switching frequency. More capacitance decreases the ripple, more power increases it, and higher frequencies decrease it, all in roughly linear relationships.

For moteus r4.5 and r4.8 (and likely subsequent revisions of this design), this voltage ripple is what controls the peak power that moteus can drive. moteus is designed to have a very small form factor, and thus uses only MLCC capacitors for its bulk DC decoupling. This results in it having an abnormally small bulk capacitance compared to otherwise similar controllers (doubly so because of MLCC capacitor derating).

The rated 500W of peak power for moteus (limited to 450W by default in config), can be achieved with the default 40kHz switching rate. By increasing the switching rate, the peak power can be increased to 750W, and if the switching rate is decreased to the minimum of 15kHz, then the rated peak power is around 190W.

## Heat dissipation

Switch mode controllers generate heat while operating as a result of many factors. The two biggest ones for moteus can be broken down into “dynamic” and “static” components of the gate drive and switch MOSFETs.

The easiest to understand is the “static” component of the switching losses. When the power MOSFETs are fully turned on, the power dissipated in them is determined by their on resistance and the amount of current, using the formula P=I^2*R. The on resistance for MOSFETs generally increases with temperature, so the worst case will be at the maximum rated temperature of the device.

The dynamic component encompasses a few different pieces. One, is that while the MOSFETs are in the process of turning on and off, their resistance is much higher than when fully on. Another is that during the “dead-time” window between when the high side and low side of the H-Bridge is enabled and neither is actively driven, the current is conducted through the body diode of one of the MOSFETs, which has what can be modeled as a fixed voltage drop. The third big component of this is the energy required to charge and discharge the gate capacitors of the primary switching MOSFETs. The energy used in the dynamic region for a given power output stays roughly constant for each switching event. Thus switching faster results in more energy used per unit time scaling in a roughly linear manner.

Further, the dynamic component significantly depends upon the input voltage. Higher input voltages mean that the MOSFETs take longer to switch for a given gate drive current, that the power dissipated is larger during the switching events, and that more energy is required to charge and discharge the gate capacitors.

Thus as the switching frequency increases, the power used consumed during the switching events goes up, while the total time spent in the static region goes down. That means the efficiency goes down as the switching frequency goes up.

## Control bandwidth and motor speed

For PWM rates of 40kHz or less, moteus implements its entire control loop at the PWM update rate. When this control rate is decreased, the bandwidth available for control of torque, and subsequently position decreases.

Also, the maximum electrical RPM that the controller can achieve is also directly related ot the control rate. However, few applications reach the 1.5kHz electrical frequency possible at 15kHz, so this is not often a concern.

Due to the implementation details, selecting PWM rates above 40kHz results in the control cycle running every other switching event. That means that the maximum control bandwidth and electrical speed is achieved for a pwm rate of exactly 40kHz. Higher or lower and those bandwidths and speeds are decreased.

## Thermal experiments

To show the effects of changing the PWM rate on thermal performance, I made up a simple experiment with a moteus r4.8 and an mj5208 attached to a devkit bracket but disconnected from the moteus so that they would be thermally decoupled.

[img: /2022/03/12/customizable-pwm-rate-for-moteus/images/pxl_20220311_143323496.jpg]

The fixed voltage mode was used to slowly spin the motor while progressively larger amounts of current were applied to the motor. This approximates operating with full torque at a standstill, where the motor performs almost no mechanical work. For each current, I waited for the temperature in the motor windings and the controller to stabilize.

[img: /2022/03/12/customizable-pwm-rate-for-moteus/images/fet_temp_vs_pwm_frequency-1.png]

Using this data, I was able to roughly estimate a few parameters at these different operating points. First, in ambient air with no heat sinking or forced cooling the mj5208’s thermal conductivity is around 4 C/W. moteus’s thermal conductivity in the same environment is around 40 C/W. The thermal efficiency and effective idle thermal generation of the controller at various operating points can be seen below along with the available peak power:

  
      
          PWM Frequency
          Peak Power
          24V
          36V
      
  
  
      
          15kHz
          190W
          94% / 0.20W
          92% / 0.25W
      
      
          40kHz
          500W
          88% / 0.25W
          85% / 0.30W
      
      
          50kHz
          625W
          84% / 0.30W
          84% / 0.35W
      
  

Thermal efficiency for moteus r4.8 driving a nearly-stationary motor

These efficiency tables can be used to roughly determine the thermal design for moteus in a setting with air cooling, no heat sink, and any motor heating dealt with independently. If you are running the default 40kHz PWM rate at 24V and are applying 5W of power to your motor, that means the total power consumed will be 5W/0.88% +0.25W=5.93W. The 0.93W of waste power dissipated in moteus will result in its temperature being approximately 0.93W* 40C/W=37C higher than ambient.

That value can be improved either by lowering the PWM frequency or input voltage to improve efficiency, or by using heatsinking and/or forced air or liquid cooling to decrease the effective thermal conductivity of the controller.

## How to configure moteus

Once you know the PWM frequency you want to use, selecting it is as simple as setting the `servo.pwm_rate_hz` configuration value to something between 15000 and 60000.

It is important to note that the maximum power configurable value, `servo.max_power_W` is now defined as the power when configured at 40kHz. Internally it is scaled accordingly if the PWM rate is changed. This means you can change the PWM rate without worrying about accidentally having too large of a power limit configured.

		
		
			

	
	
		
- 
			control
		
		
- 
			moteus
		
		
- 
			moteus_r48
