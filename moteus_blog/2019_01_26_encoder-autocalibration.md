# Encoder autocalibration - mjbots blog

URL: https://blog.mjbots.com/2019/01/26/encoder-autocalibration/

I have been continuing to iterate on the control and mechanical aspects of the improved actuators for SMMB.  While working on an alternate board mounting strategy, I ended up with a magnet that was much much further from the absolute encoder than before.  This resulted in significant errors in the estimated motor phase at various points in the revolution of the absolute encoder.  In the spirit of copying every single thing Ben Katz did in his project, I implemented a piecewise linear encoder calibration technique.

My effort largely uses the same approach: The virtual “d-axis” current is advanced through phase space at a constant rate and the encoder is sampled simultaneously.  This proceeds until the encoder has advanced through one full revolution.  It then reverses direction and does it again.  Then, a script consumes those values, and first determines the number of poles and the encoder direction.  Then for each encoder value, it estimates what the phase should have been and computes the difference between the actual commanded phase and what was measured.  The approach here is slightly different from Ben’s approach, as it works in the encoder space instead of the phase space.  As with Ben’s work though, this difference has a moving window average applied to it with a size equal to one electrical phase.

[img: encoder_calibration]

The plot above shows one run of the calibration.  The top plot shows the phase value in radians plotted against the absolute encoder value as a uint16.  The bottom plot shows the difference between the perfect mapping and what was actually measured, with the moving average in orange.

For this particular mounting configuration, the peak to peak phase error was almost 0.5 radians, or 28 degrees, which results in a fair amount of torque ripple.  Automated calibration also gives significantly more repeatable performance than my previous approach of “command a few phases and eyeball a suitable average”.  This does still leave two other parameters to auto-calibrate, the motor resistance and kV rating.  However, both of those are much easier to measure reliably by hand so fixing them is not as urgent.

		
		
			

	
	
		
- 
			actuator
		
		
- 
			smmb
