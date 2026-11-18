# Bringing up FD-CAN on the STM32G4 - mjbots blog

URL: https://blog.mjbots.com/2019/10/09/bringing-up-fd-can-on-the-stm32g4/

To verify that I could make FD-CAN work in the next revision of the moteus controller, I made a simple desk setup between two NUCLEO-G474RE boards.  I started by soldering up some breakout boards for the TCAN334G CAN transceiver I’m planning on using:

[img: dsc_1549]

[img: dsc_1553.jpg]

And then wired those up with a lot of jumper wires:

[img: dsc_1555]

After a fair amount of fiddling, bisecting against the ST CUBE example project, and fixing some problems with my STM32G4 support in rules_mbed, I ended up with some 16 byte CAN frames being sent and received with a data rate of ~4Mbit.

[img: A whole frame]

      

A whole frame
    

[img: A single bit within the data part of the frame]

      

A single bit within the data part of the frame
    

		
		
			

	
	
		
- 
			fdcan
		
		
- 
			moteus
		
		
- 
			stm32
		
	

		
	

			
			
  

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
