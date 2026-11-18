# Measuring torque ripple - mjbots blog

URL: https://blog.mjbots.com/2020/08/26/measuring-torque-ripple/

Recently I described some changes I made to improve the low speed torque ripple of the moteus controlle. I also built a dynamometer. I decided to use the dynamometer to quantify how much things had improved with the torque ripple, and to see how much room for improvement was left with any anti-cogging implementation.

## Dynamometer script

Here, the test script is relatively simple. I have the “fixture” controller sweep at a very low velocity (0.01Hz) through a bit more than one full revolution using a relatively high I term in the PID controller to ensure that it really holds that position no matter what external torque is applied. Then, the “device under test” controller is just commanded either to be powered off, or in position mode with a pd gain of 0 and a feedforward torque. Then I can just measure the result from the torque transducer while this sweeps through a full revolution, and correlate the measured torque with the encoder position.

## Results

For these plots, I subtracted out the DC offset to just concentrate on the ripple and not scaling factor errors. Here’s the plot from the pre-fixes controller:

[img: /2020/08/26/measuring-torque-ripple/images/20200815-pre-fix-torque-ripple.png]

In these plots, the higher torque values are displayed in lighter colors to make it easier to see the values at low torque. Also, the standard deviation and peak-to-peak values are rendered in the lower right hand corner. Before any fixes, when the controller was off (i.e. completely unpowered), there was about 0.026Nm of peak-to-peak ripple, however, when turning on the controller and commanding 0 torque, that jumped up to a 0.047 Nm peak-to-peak.

Now for the post-fix plot:

[img: /2020/08/26/measuring-torque-ripple/images/20200822-new-torque-ripple.png]

Post fixes, the torque ripple at all the tested torque levels is nearly indistinguishable from the torque of the completely unpowered motor, which is what we want to see!

		
		
			

	
	
		
- 
			dynamometer
		
		
- 
			moteus
		
		
- 
			moteus_r43
		
		
- 
			torque
