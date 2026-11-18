# Space vector pulse width modulation (SVPWM) for moteus - mjbots blog

URL: https://blog.mjbots.com/2024/11/20/space-vector-pulse-width-modulation-svpwm-for-moteus/

A permanent magnet motor controller like moteus has to, at the end of the day, apply voltages to the phase wires of a motor in order to induce currents. Those currents generate magnetic fields that push against permanent magnets in the rotor to make the motor move. I’ve looked at parts of this process before, see “Compensating for FET turn-on time”, but in this post we’ll look at an additional technique that can extend the effective modulation depth, thus increasing the maximum speed that a motor can be driven.

## Background

The control structure for moteus (and most similar controllers), is still the same as was shown in that post from 2021:

[img: /2024/11/20/space-vector-pulse-width-modulation-svpwm-for-moteus/images/20210820-blog-control-structure.png]

The Clark transform, when given a Q and D phase voltage in a rotating reference frame, generates A/B/C voltages that are centered about the 50% point of the PWM cycle. For a constant Q phase voltage and constant rotation rate, that results in sinusoidal phase terminal voltages that look like this:

[img: /2024/11/20/space-vector-pulse-width-modulation-svpwm-for-moteus/images/20241112-pwm-spwm.png]

The maximum difference between any given phase terminal and the 50% point is half of the input voltage, but given that the 3 sine waves are out of phase, the maximum voltage that will be applied between any two phase terminals is actually less than the full input voltage, about 87% of the full input voltage.

## Midpoint clamp

What if instead, at each step, the phase voltages were shifted such that the lowest and highest voltages were an equal distance away from the 50% point? That would result in using the maximum possible voltage from the input bus. The resulting waveform looks like the below:

[img: /2024/11/20/space-vector-pulse-width-modulation-svpwm-for-moteus/images/20241112-pwm-minmax.png]

Notably, there is now room to increase the maximum voltage applied while still staying within the 100% feasible duty cycle of the PWM output.

As it turns out, this construction is exactly identical to the space vector pulse width modulation scheme, which can also be derived in a much more complicated manner. Some of those “methods that involve unnecessary computation” can be found enumerated here:

- https://developerhelp.microchip.com/xwiki/bin/view/applications/motors/control-algorithms/zsm/zsm/best-zsm/

## Result

The upshot is, as of firmware release 2024-11-14, all moteus controllers now can drive a modulation depth that is 15% higher than before. The biggest win, is that for the same input voltage, it can now spin motors 15% faster. That applies equally to the moteus-r4, moteus-n1, and moteus-c1!

Enjoy!

		
		
			

	
	
		
- 
			math
		
		
- 
			moteus
		
		
- 
			moteus_c1
		
		
- 
			moteus_n1
		
		
- 
			moteus_r4
