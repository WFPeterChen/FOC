# Compensating for FET turn-on time - mjbots blog

URL: https://blog.mjbots.com/2021/09/15/compensating-for-fet-turn-on-time/

A motor driver like moteus switches power to the phases of a brushless motor using a set of 6 (or possibly more), MOSFETs. The typical topology involves 3 high side N channel MOSFETs and 3 low side N channel MOSFETs arranged in 3 half bridges like this:

[img: (example 3 half-bridge from DRV8353 reference manual)]

      

(example 3 half-bridge from DRV8353 reference manual)
    

Since the gates of these FETs need to be driven with potentially high voltages, and you never want the high side and low side to be on at the same time, typically a gate driver is used. For the moteus r4.5 and earlier controllers, the DRV8323 driver from Texas Instruments is what performs this function. This driver lets you configure the drive current for each of the gates for both operations, charging up the gate and discharging it. For high power drive systems, charging up or discharging the gate too fast can result in undesired transients like accidentally switching the other FET on due to capacitive coupling, or inductive ringing as the current starts moving through the FET instead of the body diode. If the gate charges too slowly, then the FET spends much of its time not fully on, which increases power dissipation in the FETs.

For the purposes of this write-up, this means that the FETs do not turn on or off instantly, and the turn-on time can differ from the turn-off time.

[img: Control structure with PWM mapping shown]

      

Control structure with PWM mapping shown
    

One of the lowest levels of control in moteus accepts a set of desired voltages on each of the 3 phases, and then selects a PWM duty cycle for the 3 phases which attempts to achieve those voltages. For most control purposes this mapping isn’t too critical, as usually it is driven by an outer current loop, which selects these values to try and achieve a desired phase current. Thus any non-linearities are mostly washed out, just reducing the control performance a bit. The times it is most important is during motor calibration when a fixed voltage waveform is applied for spinning the motor, and when the phase resistance and inductance of the motor are estimated.

To date, that function has used a relatively simple model that treats the 3 phases independently. For each phase, the voltage is mapped to a PWM using a piecewise linear approximation with two segments. This works reasonably well on the r4.5, and gets the resistance and inductive measurements into a usable territory.

[img: /2021/09/15/compensating-for-fet-turn-on-time/images/20210820-blog-pwm-mapping-1.png]

## Problems brewing

As a consequence of the global semiconductor apocalypse, I’m working on a new version of the moteus controller that uses a gate driver which it was possible to purchase with less than a year lead time. Unfortunately, it is more sensitive to failure induced by ringing than the DRV8323 was. Because of the abbreviated design timeframe, I left the power stage of the r4.5 alone when making this switch, and it had a fair amount of ringing when using the drive currents it is configured with by default. So, for the new board, it needs to be run with much slower gate drive waveforms than the r4.5 board could achieve.

Using the lower current results in a very distorted mapping between PWM and voltage on the phase terminals, such that calibration wouldn’t complete and measuring the phase resistance was not practical. You can get an idea of the problem by looking at the plot below of phase current during the motor calibration phase. The driving voltage is a sine wave, and the current is supposed to be a sine wave also. Because of the additional distortion, low voltage commands result in nearly no actual current production and there is a large undesired waveform when the three phases are active, but at different levels.

[img: /2021/09/15/compensating-for-fet-turn-on-time/images/r48_old_cal.png]

Thankfully current control mostly worked, although it likely suffered from degraded performance at small currents. Despite that, not being able to calibrate is a big deal, so I had to take another look at this control step to make it able to handle the larger non-linearity.

## Quantifying the problem

I didn’t necessarily want to try and model the gate drive at a more detailed level to understand this, to tackled the quantification problem by brute force. I wrote a simple tool which commanded a fixed PWM on the A phase, and then did a 2D sweep over the B and C phases in a region where my power supply could provide sufficient current without undue heating. The plots of the B, and C currents as 3D surfaces are shown below (the A current can be found from those two currents).

[img: B and C phase current vs PWM]

      

B and C phase current vs PWM
    

I think numerically inverted that mapping, to determine the necessary B and C pwm values to achieve a given B and C phase current.

[img: /2021/09/15/compensating-for-fet-turn-on-time/images/20210820-blog-pwm-command-mapping.png]

The C phase plot looked the same just mirrored. Notably, this has two distinct regions. There is a region which extends to infinity where the command B is related to the desired B 1 to 1 with a fixed offset. However, that region only seems to exist beyond a line that is related to the C command. On the other side of that line, the fixed offset is no longer required. I did a rough curve fit to this, which looks like:

[img: /2021/09/15/compensating-for-fet-turn-on-time/images/20210820-blog-command-fit.png]

In the above plot, the sampled data is now faded out, and the simple surface fit is fully saturated. It works plenty well, and is controlled by 2 parameters as before (since the dividing line between the two regimes seems to not be something that is likely to need to be configured).

## Results

I reworked the mapping to always operate in terms of a B and C phase that are larger than the A phase, then shift things back to the correct phase and balance afterwards. That way this correction surface could be applied as is.

This new technique gives much better low-voltage performance, especially when more than one phase is active and when larger amounts of compensation are required. Here’s a plot showing the current waveform during a sinusoidal calibration sweep.

[img: Comparison of PWM compensation methods]

      

Comparison of PWM compensation methods
    

With the r4.5 board, you can see the low voltage performance improve a fair amount. With the newer r4.8 board and its higher required compensation, the improvement is dramatic.

		
		
			

	
	
		
- 
			moteus
		
		
- 
			moteus_r45
		
		
- 
			moteus_r48
		
	

		
	

			
			
  

#### Links
  
    
      
- mjbots.com
      
- YouTube | Bluesky | Instagram | X | Facebook 
      
- mjbots Discord server
      
- mjbots/moteus - brushless motor controller
      
- mjbots/quad - quadruped
      
- RSS feed
    
  

	

#### Recent Posts
	
		
			
- mjuart
			
- Host side SPMSM simulation for regression tests
			
- Limiting the maximum regen power in moteus
			
- RLS Orbis SPI support
			
- moteus firmware version 1.0.0!
			
- Moteus performance analysis tool - CLI mode
			
- Native socketcan support for mjcanfd-usb-1x
			
- moteus.move_to and moteus ruckig python example
			
- PWM input support
			
- BiSS-C encoder support
		
	

	

#### Categories
	
		
			
- 
				Development 
				294
				
			
- 
				Documentation 
				8
				
			
- 
				Events 
				6
				
			
- 
				Features 
				65
				
			
- 
				Mjbots 
				1
				
			
- 
				Products 
				37
				
			
- 
				Releases 
				9
				
			
- 
				Robots 
				11
				
		
	

  

#### Archives
  
    
      
        
- 2026 (14)
      
        
- 2025 (41)
      
        
- 2024 (19)
      
        
- 2023 (25)
      
        
- 2022 (16)
      
        
- 2021 (52)
      
        
- 2020 (132)
      
        
- 2019 (117)
      
        
- 2018 (15)
      
    
  

	

#### Tags
	
		3dprint
		8020
		Actuator
		Ai
		Allan_variance
		Amass
		Arduino
		Artisansasylum
		Aruco
		As5047
		As5048b
		As5600
		Asio
		Bazel
		Bazel_deps
		Bearings
		Beast
		Beta
		Bissc
		Build
		C
		Callgrind
		Cast
		Chassis
		Claude
		Clock
		Cnc
		Conduit
		Connectors
		Control
		Coroutines
		Crunch
		Cut
		Dart
		Devkit
		Diagnostics
		Discord
		Documentation
		Dynamics
		Dynamometer
		Enclosure
		Encoder
		Events
		Fail
		Fan
		Fast
		Fdcan
		Fdcanusb
		Ffmpeg
		Fixture
		Foot
		Fpv
		Fusion360
		Gait
		Gdb
		Gearbox
		Gears
		Gimbal
		Github
		Gstreamer
		Hackaday
		Hoverboard
		Hoverbot
		Html
		I2c
		Imgui
		Implot
		Imu
		It
		Joystick
		Js
		Juggling
		Jump
		Kcachegrind
		Lathe
		Leg
		Linux
		Live
		Log
		Ma600
		Ma732
		Mac
		Machining
		Magnet
		Maker_faire
		Math
		Matplotlib
		Mbed
		Mech_warfare
		Meld
		Microbom
		Microscope
		Mjbots
		Mjcanfd
		Mjlib
		Mjpower-Ss
		Mjuart
		Mlcc
		Moteus
		Moteus_c1
		Moteus_n1
		Moteus_r4
		Moteus_r41
		Moteus_r411
		Moteus_r42
		Moteus_r43
		Moteus_r45
		Moteus_r48
		Moteus_tool
		Moteus_x1
		Motor
		Mpat
		Newmachineday
		Nrf24l01
		Nrfusb
		Off_axis
		Opencv
		Opengl
		Openocd
		Packaging
		Pcb
		Photos
		Pi3hat
		Plm
		Pocketnc
		Power_dist
		Probe
		Profiling
		Pronk
		Pwm
		Python
		Qdd
		Qdd100
		Quad
		Quada0
		Quada1
		Realtime
		Release
		Rotor
		Rpi
		Rules_wix
		Serialization
		Servo
		Servomk2
		Shop
		Simulation
		Smmb
		Smoke
		Snowday
		Socketcan
		Softjaw
		Solder
		Spi
		Spreadspectrum
		State_estimation
		Stator
		Stm32
		Stm32g4
		Stm3g4
		Strip
		Svd
		Thermal
		Thermal_imager
		Thermistor
		Threadmill
		Thud
		Torque
		Tplot
		Trot
		Tumbler
		Turret
		Tview
		Uart
		Ukf
		Usb
		Video
		Vise
		Visitor
		Walk
		Wcubed
		Websocket
		Windows
		Workholding
	

		
		
	
		
		
			© 2026  mjbots Robotic Systems, LLC -  info@mjbots.com -
			All content on this website is licensed under a Creative Commons License unless otherwise explicitly stated.
