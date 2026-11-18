# Rethinking dead time compensation in moteus - mjbots blog

URL: https://blog.mjbots.com/2025/04/03/rethinking-dead-time-compensation-in-moteus/

Way back in 2021, I wrote up a post detailing a method for improving the linearity of the relationship between applied voltage and current for moteus, particularly during the calibration phase. At the time, this did solve a real problem – during calibration, moteus applied a fixed voltage to the phase terminals, swept the electrical angle of that voltage, and hoped that the mechanical angle as sensed with the on-axis sense magnet matched well. However, as a result of some new work, I’ve found that the premise behind that approach was flawed and it needs some re-thinking. This describes what I found, and what’s being done to resolve it going forward.

## Background

First off, all the data and modeling captured during the experimentation phase 4 years ago still holds. The non-linearity between applied PWM duty cycle and measured current definitely exists and follows the multi-dimensional pattern as shown in the plots there.

What that data misses, is the effect of back-emf on the observed non-linearity. All of the data collected during those experiments was at 0 speed, so there was no back-emf observed from the motor. My assumption at the time was that the non-linearities would be related to the PWM duty cycle and would be centered around a relative duty cycle of 0, regardless of how much back-emf was present.

## Updating my assumptions

While working on something new (and bigger!), I spent some time trying to find a more accurate model of this dead time so as to fit alternate drive circuits. After having gotten tired of devising increasingly detailed lab experiments, I decided to attempt to model the drive topology in SPICE and see how close it could get. This turned out to be quite an exercise in frustration!

To start with, I modeled the DRV83x3 gate driver as a set of time controlled current sources:

[img: /2025/04/03/rethinking-dead-time-compensation-in-moteus/images/2025-02-28-145708_2382x993_scrot.png]

Each half of the H bridge included a current source that would either charge or discharge the FET using a current that matched the configured drive strength. Clamping voltage sources and diodes ensured that the gate drive voltage matched what the actual device provided. Much of the non-linearity was directly related to the behavior of the DRV83x3 gate driver, particularly its shoot through protection. To model that, a separate “sense” circuit was used which charged a capacitor to time how long the opposing gate had been turned off.

The biggest problem here was finding appropriate MOSFET models. ngspice, which is integrated into KiCAD, has some selection, but basically no modern power MOSFETs are included in its library. Most vendors do provide simulation models, but typically only for one of the major proprietary SPICE solvers and usually in encrypted form. I ended up using LTSpice, mostly because it was free, but also because it was reported to run under Wine relatively well. I hoped to use that to accomplish broad parameter sweeps programmatically across many cores.

Unfortunately, the above circuit turned out to be highly unstable when simulated on LTSpice, depending upon the simulated parameters. Usually I could get it to work, but doing so often required configuring a very low maximum time step which would make simulations take a very long time, particularly near 0 output current when dead time effects were most pronounced.

## Gripes about LTSpice

I would have course preferred to use ngspice for this effort, but since I could not get a FET model that reproduced the effects I cared about, LTSpice was what I was stuck with. Here’s a list of some of the gripes I had after working with it:

- 

Parallelism: Yay, in that it can run on multiple cores. However, big boo that it uses some heuristic to determine how many cores to use that seems both a) largely random when run under Wine, and b) drastically underutilizes machines with large numbers of CPUs.

- 

Instability: The “time step too small” was the bane of my existence during this effort. This may be an artifact of trying to model a highly non-linear system, but getting usable results required a fine balance of selecting the maximum time step. If it was too large, errors resulted. If it was too small, a single simulation run could take hours to complete.

- 

Scriptability: You might think that you could use LTSpice’s built in scripting methods to sweep across the parameter space. However, if any step in the simulation triggered a “time step too small” error (see above), the whole run would be aborted without printing the results!

- 

User Interface: Well, I guess you have to expect the UI to be crufty for a free EDA tool that has been around as long as LTSpice, but boy, just driving the mouse with it was painful.

- 

Broken release: I first went and downloaded the most recent release (24.1.something), which turned out to be nearly completely broken in its ability to not fail during simulations. Fortunately, I was able to locate a working version 24.0.12.

[img: Snapshot of https://groups.io/g/LTspice]

      

Snapshot of https://groups.io/g/LTspice
    

## Modeling results

Despite the pain, with sufficient dedication I was able to tune my DRV83x3 simulation and the motor models to get results that closely aligned with my observed results for a range of motor parameters and supply voltages.

[img: /2025/04/03/rethinking-dead-time-compensation-in-moteus/images/20250403-average-current-vs-pwm.png]

[img: LTSpice simulated gate waveforms for a single phase]

      

LTSpice simulated gate waveforms for a single phase
    

The surprise came when I ran the simulation assuming that the motor was spinning and was producing back EMF. This was hard for me to capture clean traces of on the oscilloscope, but it was trivial to capture in LTSpice. Once I did that, I had my big a-ha moment. The non-linearity was not in fact around 0 PWM duty cycle, but instead around 0 current!

[img: /2025/04/03/rethinking-dead-time-compensation-in-moteus/images/20250403-average-current-vs-pwm-back-emf.png]

That means that while the earlier moteus compensation technique would improve linearity when a motor was stationary, it would strictly degrade linearity at any other motor speed. It also had the effect of increasing audible noise near 0 speed, so this meant that it had some problems at basically any speed!

## Future directions

At this point, I decided to just scrap the entire compensation framework. Having it gone wasn’t the end of the world… nearly all moteus control operates in current controlled mode, where the PWM duty cycle is modulated to converge onto a desired phase current. The only thing the compensation was helping with was 0 speed calibration, for which I had some alternate approaches in mind. For basically every other use case it was just increasing audible noise and decreasing the tracking performance of the current controller.

		
		
			

	
	
		
- 
			control
		
		
- 
			moteus
		
	

		
	

			
			
  

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
