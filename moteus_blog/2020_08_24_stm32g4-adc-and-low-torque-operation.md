# STM32G4 ADC and low-torque operation - mjbots blog

URL: https://blog.mjbots.com/2020/08/24/stm32g4-adc-and-low-torque-operation/

Recently dlickindorf pointed out in the mjbots discord that he was having problems with very low torques on his large PMSM hobby-grade motor. While moteus doesn’t have any anti-cogging support yet, it should still be capable of driving motors such that the unexpected torque isn’t much worse than the baseline cogging torque of the motor. However, he was seeing much worse behavior with controlling to 0 current, as much as a full percent of the maximum torque of the motor.

Needless to say, I wanted to try and understand the behavior and figure out how to make it better. I was able to reproduce the problem locally, so that was a good start. It was easiest to see when commanding a position with a low maxmum torque. Say on a 8108 motor, with 0.05 N*m maximum torque, the cogging-like torque was high enough that the position control could get stuck in a local minima far from the control point.

      
    

In other experiments with higher torque, the switching noise because audibly “squirrely” at certain electrical theta angles. One often occurred at a 0 theta pre-magnetic offset compensation, and another appeared when one phase current was near maximum and the other two phase currents were opposite sign but near equal magnitude.

## Calibration smoothing

When the moteus controller is calibrated, it calculates an offset between the electrical commutation angle and the magnetic encoder not just as an average, but at 64 separate points in the revolution of the magnetic encoder. This allows the calibration to take out some of the effect of magnets that are not perfectly axis-aligned with the encoder.

However, when calculating the moving average of the offset value, I neglected to take into account the fact that the encoder wraps around between 0 and 2*pi, and those ends of the offset curve were smoothed independently. If the calibration was somewhat steep in that particular window, it resulted in a discontinuity in the offset curve, which resulted in high frequency noise when controlling near that position.

That problem was relatively easy to solve, by just filtering in a circular domain.

## Current noise

The next issue was dealing where the phase current became obviously very noisy. Ultimately I tracked this down to a combination of mis-configuration of the STM32G4 ADC peripheral and coupling between the digital and analog domains.

The latter is the easiest to cover. In the core FOC loop, I use a debugging pin that goes high during the loop to be able to easily identify how long it takes on an oscilloscope. The problem was that I was starting the ADC conversions, then immediately switching that pin. That results in a large enough current transient either within the chip or on the PCB that the analog samples have significant noise introduced. Solving that was easy enough by just moving the debug pin assignment to after the ADC samples are completed.

The former was a result of bugs introduced when I ported the moteus code from the STM32F4 to the G4. The ADC peripherals are significantly different in the G4 (more like the F3 peripherals) and the configuration registers are almost entirely different. At one point several months ago I was trying to configure the sample time for the ADCs but not actually doing so at all. When I resolved that issue, I didn’t look at the resulting sampled waveforms very closely and didn’t notice that the current waveforms were still pretty bad. This was compounded by a further issue. The moteus has 3 current shunts, but it was only sampling two of them and inferring the 3rd by Kirchoff’s law.

Here’s a plot showing the 3 current values as the voltage phase was advanced through a few cycles:

[img: /2020/08/24/stm32g4-adc-and-low-torque-operation/images/20200822-old-broken-phase-currents.png]

The biggest problem here was that the ADC prescaler was too low for the system clock I was running. Once fixing the prescaler, and making a few additional changes, the current waveforms now look like:

[img: /2020/08/24/stm32g4-adc-and-low-torque-operation/images/20200822-new-fixed-currents.png]

So yes, there is now much less unusual discretization going on across all 3 current channels.

## Final results

And now for a video showing the final result:

      
    

		
		
			

	
	
		
- 
			discord
		
		
- 
			moteus
		
		
- 
			moteus_r43
		
		
- 
			torque
