# First closed loop vector control - mjbots blog

URL: https://blog.mjbots.com/2018/11/27/first-closed-loop-vector-control/

I’ve reached a minor milestone in developing improved actuators for Super Mega Microbot.  Previously I demonstrated basic closed loop control using a VESC.  Now I have a custom control board running closed loop vector-based current and position control on a single brushless servo!  I’ll hopefully write up pieces in more depth later, but this post can serve as a proof of existence.

First, boards as received from MacroFab:

	

	
	

	
		
	

	
		
	

	
	

	
	

	
		

			
			

			
			
				
			

			
				
			

			
			
				
					
				
			

			
			

			
				
				
					
					
				

				
				
					
					
				

				
					
[img: data:image/jpeg;base64,/9j/2wCEAAoHBwgHBgoICAgLCgoLDhgQDg0NDh0VFhEYIx8lJCIfIiEmKzcvJik0KSEiMEExNDk7Pj4+JS5ESUM8SDc9PjsBCgsLDg0OHBAQHDsoIig7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7O//AABEIACAAIAMBIgACEQEDEQH/xAGiAAABBQEBAQEBAQAAAAAAAAAAAQIDBAUGBwgJCgsQAAIBAwMCBAMFBQQEAAABfQECAwAEEQUSITFBBhNRYQcicRQygZGhCCNCscEVUtHwJDNicoIJChYXGBkaJSYnKCkqNDU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6g4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2drh4uPk5ebn6Onq8fLz9PX29/j5+gEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoLEQACAQIEBAMEBwUEBAABAncAAQIDEQQFITEGEkFRB2FxEyIygQgUQpGhscEJIzNS8BVictEKFiQ04SXxFxgZGiYnKCkqNTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqCg4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2dri4+Tl5ufo6ery8/T19vf4+fr/2gAMAwEAAhEDEQA/AHtdW0ECQx3JkuHJYQgAZPuSOB71ztx4jWOWUHcXH3CWI2/lVXVL9Ir92ZyVBwFCA+xrJlNvcXL5aXeTzjGBT33KUbJcu50D69O13AbiN/LT/WkSZ8z06+lbl5fQTWiTW0wMbA4jx93HJrmtGaKa7nRwj8qcyAEfe7fn0qe3iSDTiwaPKgjcANx+cj+RxWafvpeg6sU4srG3NrKzzIom28bsHcTnPX6Z/E+lVhYwzTkxgxksMsec5P1/GtPW7uJ/PtZJAkrKHglzxyeVPseT+NY7WHk2qTzQXOwuVZo5QVyOeuffr0q7MuE1e/UU2o52zOCf7qAZ/wDHqZHFJa3TwPl0VyqljjGD1x/n+eZLOwivZXWCGZ3xuRA3Jzwcfz78A1ow6aILUy3peAAYUFCxY+me1XCm27PQzrVvdvuz/9k=]

				
			
		
	
		

			
			

			
			
				
			

			
				
			

			
			
				
					
				
			

			
			

			
				
				
					
					
				

				
				
					
					
				

				
					
[img: data:image/jpeg;base64,/9j/2wCEAAoHBwgHBgoICAgLCgoLDhgQDg0NDh0VFhEYIx8lJCIfIiEmKzcvJik0KSEiMEExNDk7Pj4+JS5ESUM8SDc9PjsBCgsLDg0OHBAQHDsoIig7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7O//AABEIAB8AIAMBIgACEQEDEQH/xAGiAAABBQEBAQEBAQAAAAAAAAAAAQIDBAUGBwgJCgsQAAIBAwMCBAMFBQQEAAABfQECAwAEEQUSITFBBhNRYQcicRQygZGhCCNCscEVUtHwJDNicoIJChYXGBkaJSYnKCkqNDU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6g4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2drh4uPk5ebn6Onq8fLz9PX29/j5+gEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoLEQACAQIEBAMEBwUEBAABAncAAQIDEQQFITEGEkFRB2FxEyIygQgUQpGhscEJIzNS8BVictEKFiQ04SXxFxgZGiYnKCkqNTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqCg4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2dri4+Tl5ufo6ery8/T19vf4+fr/2gAMAwEAAhEDEQA/ALWr63pQ09oJIQk0iDhW5LY+9WLceLr6VgDFbCNcBU29ABgAkEHua57VL2S4uZIyQUSQ7eBn061VQnPVvwpuTudNOlFLU6ddeLMpe2TaMZCMf61vWtxY3ql0Kxp65OQfTFeegt1V2K+/b61oaDdGPUREzsPNyrMee2en+etTzPoOpQjy8yILuF/t0kUsCQmMAcA/Pyfm696ga2OPlZVPqAf8asPLJdtcXksm0nGxcZA9B+VRLcRiTLbvLVQX457jj8aUlJM6aNWhyLnWo3ypwjIsgIbk59at6PZSC5indGDB8qCccDOc5qulzE8RI+/nAHarsSyJGHd854Jx1P8Ak0KEmuxlia9FK1Pc/9k=]

				
			
		
	

Mounted onto the planetary gearbox:

[img: dsc_1164]

And finally, a brief video of operation, with tview (from the mjmech gimbal) alongside.

      
    

Amazingly, I’ve needed no blue wires or rework of any kind so far.  Next up is to get it communicating over the RS485 link instead of serial, build a second gearbox, and get it jumping!

		
		
			

	
	
		
- 
			actuator
		
		
- 
			smmb
		
	

		
	

			
			
  

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
