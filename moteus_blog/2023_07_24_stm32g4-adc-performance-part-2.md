# STM32G4 ADC performance part 2 - mjbots blog

URL: https://blog.mjbots.com/2023/07/24/stm32g4-adc-performance-part-2/

Back last year, I walked through bisecting and debugging an annoying problem that caused the STM32G4 ADC on the moteus controller to exhibit higher than expected noise in result largely to either the exact placement in flash of the initialization code, or to the exact timing of the initialization. While the immediate glaring sharp edge was removed, the resulting performance still was confusing to me, and looked like it was not yet optimal. Further, a moderate percentage (2% or so), of production boards failed end-of-line tests related to the current sense noise in ways that were hard to fix by swapping components. Because of this, I wanted to dive in and investigate further. This is that process.

## Tips and theories from the interwebs

In response to the original article, I received a *wide* variety of tips, leads, theories, and proclamations, some of which were more credible than others. Here are the top categories:

- 

Incorrect clock or prescaler configuration

- 

Some sort of clock domain synchronization problem causing prescaler configuration to be incorrectly set

- 

Insufficient VREF+ decoupling

- 

ADC initialization phasing

Some of these could be easily ruled out using just the information in the original article. Notably that the prescaler was documented to have been set to a frequency that was well within the range of what the ADC was capable of and that it was also read back from the peripheral and shown to have the correct value later in time.

That rules out the first two issues. To tackle the rest, I first needed to build some tools.

## Quantifying the problem

Before attempting to confirm or refute the remaining hypotheses, I wanted to be able to better quantify the problem. In that vein, I made some tools and scripts which would attempt to render what noise was present as a function of reported ADC counts. The idea was to sweep each phase of the motor through a range of fixed voltages, and capture a time domain signal at a variety of steady state points. Then, we would plot the magnitude of the noise, and what the the frequency components looked like throughout the range.

The resulting tool set consists of a set of scripts to perform the sampling and then an interactive matplotlib based tool to render the results. It looks like this:

[img: /2023/07/24/stm32g4-adc-performance-part-2/images/2023-05-31-102503_1409x1454_scrot.png]

The top 3 plots in the top window show log-normalized spectrograms at each ADC count for each channel. The line graph in the top window shows the overall standard deviation of each channel at each ADC count. The bottom window shows the time domain signal at any given point – clicking on any of the top channels will switch to that given time domain plot. This lets you explore many possible hypotheses, especially those that are not necessarily best represented as noise in the frequency domain. Since the sweeps are over fixed voltages on each channel, the range of ADC values sampled are not the same for each channel, as the zero point bias is different for each channel and board. The big “X"s on the standard deviation plot shows where the zero point was for each channel.

In this particular plot (which is from a random intermediate test), you can see that there are definitely some frequencies which have noise that is periodic at every 8 LSB interval, channels 1 and 2 have significant wide-band noise from around 2030 to 2048, and channel 0 has an additional high frequency noise component from around 2041 to 2048.

## Cycle accurate phasing

The next piece of infrastructure I needed to investigate this was to be able to make cycle accurate delays on the STM32G4, and have a delay end at a specific value of the global cycle counter. This is actually relatively hard, as there exists a flash accelerator, instruction cache, and data cache which all work to make accurate timing difficult, yet cannot be permanently disabled or things run too slowly. This is combined with the fact that when operating under the debugger, peripherals like the cycle counter are unreliable.

My solution was to first run the delay completely from the STM32G4’s CCM SRAM (which the ISR in moteus already was using). This takes the flash accelerator out of consideration. Next, the instruction and data caches are manually flushed before entering the critical section. Finally, the entire routine is implemented in assembly, with a superstitious number of alignment directives thrown in for good measure.

- https://github.com/mjbots/moteus/blob/79b79c0649a8893c021193cd170864156a039c71/fw/stm32g4_adc.cc#L52

To calibrate the offset constants, I carefully set a breakpoint only on the first instruction after the sequence, and ensured that the CPU’s cycle counter was equal to the intended calculated one at every instance across a few different shufflings of code and compilations.

At this point, I could cause each ADC to be initialized at a specific CPU cycle counter value (or really any other operation), in a repeatable manner.

## Preemptive solution: VREF+ decoupling

The ADC in the STM32G4 series operates on a successive approximation principal using switched capacitors. With that technique, at each clock cycle, various capacitors are either switched to VREF+ or to ground in order to more closely approximate the captured input voltage. Thus large current transients on VREF+ can occur, especially when the higher order bits in the sample are altered. These large transients, coupled through higher than desired impedance in the VREF+ signal path, can the VREF+ voltage to sag, resulting in faulty comparisons. The low-pin count STM32G4 variant that moteus uses is even more susceptible to this phenomenom as it has no dedicated analog ground pin. For the UQFN48 package, all grounds are tied to the exposed pad under the chip.

This particular failure mode is most likely to occur at voltages just below the halfway point, so below 2048 for the 12 bit STM32G4 ADC. If the first comparison is performed with a sagging VREF, and the actual voltage should have been, say 2044, it may erroneously be placed in the >= 2048 bucket, and then all subsequent comparisons will report it to be smaller thus resulting in exactly 2048 being the final result.

Even before attempting diagnosis, I went ahead and improved the situation for the moteus-n1, as it was in the design process anyway. The moteus r4.11 VREF+ decoupling did meet the official datasheet constraints of a 1uF ceramic capacitor close to the chip, but the ground path for that capacitor may not have been optimal, and 1uF may be insufficient for 5 simultaneous ADC operations. For the moteus-n1 r1.3 board, both a larger 4.7uF capacitor was used for the bulk, along with a separate smaller, 0.1uF capacitor. Also, additional ground vias were placed under the exposed pad along the entire side of VREF+, so that the full current path is short. Here is the resulting layout:

[img: /2023/07/24/stm32g4-adc-performance-part-2/images/2023-05-31-100058_1653x1155_scrot.png]

Here, pin 20 is VREF+, C10 is the primary 4.7uF decoupling capacitor, and C87 is the smaller capacitor. The ground path for the large capacitor is basically as short as physically possible, and 4 vias connect the ground plane to the STM32G4 all along that side.

## Experimental flailing

Given these tools, I was able to make some progress.

First, I ran the before and after on the fix from the original post:

[img: Problematic on left, "improved" on right]

      

Problematic on left, "improved" on right
    

The fix definitely made things better, in that channel 0 and 2 had only a small non-linearity around 2048, although channel 1 still had a drastic deviation around 2048.

From looking at this, and fiddling with things, it seemed that with the moteus ADC configuration, the ADCs were definitely sensitive both to the exact cycle count phasing between their initialization, and the exact cycle phasing between any ADC and some system level phenomena. After a lot of fiddling around with the above tools, I hit upon the idea of sweeping through different possible options of inter-ADC cycle timing and from-boot cycle timing.

Note, for the purposes of the results below, the moteus controller was configured with a 170MHz AHB clock, a 2x divider to the APB1/APB2 clocks, the ADCs were configured to run asynchronously, their clock source was the peripheral clock, and the ADCs had an 8x prescaler.

Here are some example plots showing a few of the sample permutations:

[img: 8 LSB periodic noise that various with the CPU cycle modulo 2]

      

8 LSB periodic noise that various with the CPU cycle modulo 2
    

[img: In this instance, ADC3/4 were initialized with a 4 CPU cycle offset from ADC1/2]

      

In this instance, ADC3/4 were initialized with a 4 CPU cycle offset from ADC1/2
    

No matter the spacing between ADC initializations, the results of all ADCs were materially better or worse depending upon when the first was enabled on a 2 CPU clock cycle boundary. I did not have a repeatable reference from power-on for the system, so it was arbitrary which was good or bad, but in any given firmware image it would look like:

- 

first adc enable at CPU cycle offset 0: good

- 

first adc enable at CPU cycle offset 1: bad

- 

first adc enable at CPU cycle offset 2: good

- 

first adc enable at CPU cycle offset 3: bad

- 

…

Similarly, no matter what the state of the system from the first ADC enable phase, things could get worse if the individual ADCs were started in different phases relative to each other. This relationship was more complicated, but roughly followed a 16 CPU clock cycle. Results were best when the CPU cycle counts between when the ADCs were enabled was an even 16 cycles and was various degrees of worse at different phasings.

## The “actual” problem

After figuring out the above, I was close to a fix. I could easily control the spacing between ADC initializations, but had not discovered a way to control the phasing between the first ADC initialization and whatever system level phenomena led the to the 2x period (likely due to the 2x AHB/APBx divider). It was at this point that I decided to go looking through the ST errata for the STM32G474 another time to see if I had missed anything.

And lo, while I hadn’t missed anything before, a new errata had been added in March 2023 which pointed to a more systematic resolution!

[img: /2023/07/24/stm32g4-adc-performance-part-2/images/2023-06-05-143427_1488x447_scrot.png]

The errata gives three possible resolutions:

1: Do not execute ADC conversions concurrently. This is both not feasible in moteus, and in my experiments it does not completely resolve problems anyways.

2: Use the same clock for all ADCs AND an ADC prescaler of 1x. This is also not feasible for moteus, as a prescaler of 1x would have the ADC running much faster than the maximum possible speed.

3: Use a synchronous clock, the same clock configuration for all ADCs, and trigger them all simultaneously from a hardware timer with a compatible prescaler. This seems like a pointer to the best option, and hints at explanations for the problems I had before.

This errata perfectly describes all the confounding symptoms I initially had, where inserting various NOPs or re-arranging the code would cause the ADCs to either work well or give terrible results. Initializing the ADCs back to back made it much more likely that poor phasing would result, where doing them separately gave a larger room for chance to intervene.

## Resolution

With the above errata and my experiments above, I now had a concrete path to a fix. The problem which had been hardest for me to resolve was the ambiguity about which of the APB phases the ADCs were enabled in. Using synchronous clocking presumably ensures that it is always the same one, and hopefully that phase is the “good” one.

For the triggering aspect, I tested two approaches, each of which gave identical ADC performance in my experiments. First, I switched the moteus software triggering to use a hand-rolled assembly block which triggers all the ADCs in 3 consecutive CPU cycles. Second, I had the PWM timer trigger a second timer through the internal hardware matrix, which then triggered the ADCs through the internal triggering mechanism. For whatever reason, TIM5 is incapable of driving the ADCs directly, thus the indirection requirement. This triggering improvement combined with the synchronous timer configuration removed all cases of periodic 8 LSB noise and instances of extremely large count 2048 noise.

With those changes in place, the results, while not perfect, are much improved. I verified them by using a variation of the cycle accurate delays above to add various delay phases of 1-8 cycles into the pre-main execution and between each ADC initialization to verify that the results were just as good in all cases. Interestingly, across a range of boards the moteus-n1 r1.3 results are about as good as the r4.11 results, which indicates that the VREF+ decoupling improvements, while a good thing overall, were not a significant contributor to at least these problems.

[img: Final results, r4.11 left, n1 right]

      

Final results, r4.11 left, n1 right
    

The git change implementing this can be found here:

- https://github.com/mjbots/moteus/commit/4db361035ac5adc999769b530727a5067b6d457b

## Future work

As should be obvious, all of the above results above have one channel with significantly higher count 2048 noise. Interestingly, which ADC and channel it is differs between the r4.11 and n1. On r4.11 the offender is ADC1/IN12 (PB1) while on n1 it is ADC2/IN3 (PA6). I don’t know if it is channel specific within the MCU, or some board level deficiency that manages to only impact one channel or what. It still occurs even if the ADCs conversions are performed sequentially rather than in parallel, which at least rules out ADC cross contamination.

For now though, I’m leaving that as “future work”! Send me your thoughts!

		
		
			

	
	
		
- 
			moteus
		
		
- 
			moteus_n1
		
		
- 
			moteus_r411
		
		
- 
			stm32
		
		
- 
			stm32g4
		
	

		
	

			
			
  

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
