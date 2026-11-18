# Filtering encoder values in moteus - mjbots blog

URL: https://blog.mjbots.com/2021/05/10/filtering-encoder-values-in-moteus/

TLDR: moteus can now filter the encoder, resulting in less audible noise. Use firmware version 2021-04-20 and ‘pip3 install moteus’ version 0.3.19, then re-calibrate to get the benefits.

## Background

The moteus controller uses an absolute magnetic encoder to measure the position of the rotor. It uses this knowledge to accurately control the current through the three phases of a brushless motor so that the desired torque is produced, i.e. “field oriented control”. This works well, but has some downsides. One, is that magnetic encoders work by sensing the magnetic field produced by a “sensing magnet” that is somehow affixed to the rotor. This sensing process always introduces some noise, so that the sensed rotor position is never perfect.

Because of this, even if the rotor is perfectly stationary, moteus will constantly be tweaking the phase of the motor current to track the noise. This results in slightly increased power consumption, and more important to most, audible noise as the slight variations in control current can induce vibrations in the phase windings of the motor.

Fortunately, in most applications, the rotor is not actually capable of accelerating with the full bandwidth that the magnetic sensor is capable of sensing. Thus, we can filter it to remove high frequency noise without any loss of performance.

## Approach

The filter method that moteus uses is an “all-digital phase locked loop”. Formulated in the traditional PLL terminology, this consists of three pieces: a numerically controlled oscillator, a phase detector, and a PI controller.

The “numerically controlled oscillator” is just a counter that tries to “guess” what the encoder is doing by propagating a value forward at an estimated velocity. For an ADPLL in this setup (where our encoder gives us an absolute phase measurement), the “phase detector” just subtracts the measured phase from the estimated phase. The PI controller determines how the estimated oscillator tracks disturbances in the actual encoder frequency.

In my post on automatic torque bandwidth selection for moteus, I derived the necessary equations for determining the torque bandwidth. Here, I’m just going to reference the excellent article by Neil Robertson, “Digital PLL’s” (part 1, and part 2), over at dsprelated.com. There, he derived the necessary proportional and derivative gains for a given bandwidth and damping ratio:

$$K_p = 2 \zeta \omega_{3db}$$

$$K_i = {\omega_{3db}}^ 2$$

Where ζ is the “damping ratio”. For our purposes, we’ll use 1.0, which is known as “critically damped” and is often a good balance between stability and response time.

## Experiments

To see this in action, I ran some experiments on a moteus devkit. I added back in a conditional ability to emit a few bytes of debugging information at the full control rate to the firmware, and used that to emit the encoder value as used by FOC at each control cycle. I made up a script, that can plot the power spectral density in the encoder measurements. This is basically the noise present at any given frequency. When run on an unmodified development kit with no encoder filtering, the result looks like:

[img: Power spectral density of moteus AS5047P encoder in a devkit]

      

Power spectral density of moteus AS5047P encoder in a devkit
    

This shows that there are peaks in the noise at around 700Hz and 2kHz and after that the noise drops off rather rapidly. Then if we enable filtering at a few different bandwidths, you can see how the plot changes:

[img: Power spectral density of AS5047P with various filter bandwidths configured]

      

Power spectral density of AS5047P with various filter bandwidths configured
    

The filter bandwidths here varied from 5000 rps (~800Hz) down to 100rps (~15Hz). The filtered noise profiles follow what one would expect for each of the filters.

## Audible noise

To measure the combined effect of this and torque bandwidth on audible noise, I switched to a controller attached to an 8108 motor, as I’ve seen those tend to demonstrate more audible noise. The motor had a constant back and forth motion at 0.5Hz for 60s and the result was captured with a studio mic set about 1cm away from the motor. In each test, I ran `moteus_tool --calibrate` and selected only a torque bandwidth, letting it pick an appropriate encoder bandwidth. For the 100Hz torque bandwidth and above tests, the same position PID values were used, although they needed to be tweaked for stability at the lower bandwidths. I similarly plotted the power spectral density for several filtering values and for silence. The total signal strength measured in negative dB is noted in the label:

[img: /2021/05/10/filtering-encoder-values-in-moteus/images/motion_8108_encoder_filtering.png]

It is not shown here, but the unfiltered noise is about the same as the 400Hz one. Thus, the maximum improvement is around 6dB of audible noise. Filtering at 100Hz gives most of that benefit for this motor, with slight improvements beyond that. Most of the audible energy is in the spectrum below 1kHz and there are several frequency bands where the encoder does not appear to be the dominant source of audible noise, as all options are equivalent in those.

For comparison purposes, the ODrive firmware defaults to filtering both the torque and encoder at around 160Hz (1000rps).

## moteus_tool integration

As of version 0.3.19, moteus_tool has support for configuring the encoder bandwidth during calibration. By default, it will select a value appropriate for the selected torque bandwidth, or you can specify it manually with `--encoder-bw-hz`.

		
		
			

	
	
		
- 
			control
		
		
- 
			encoder
		
		
- 
			moteus
		
		
- 
			moteus_r45
