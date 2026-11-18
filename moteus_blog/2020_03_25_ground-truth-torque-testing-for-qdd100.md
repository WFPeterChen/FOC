# Ground truth torque testing for qdd100 - mjbots blog

URL: https://blog.mjbots.com/2020/03/25/ground-truth-torque-testing-for-qdd100/

First, a limited number of qdd100 servos are available for sale to beta testers!  Check them out at mjbots.com.

After building up the first set of qdd100 servos, I wanted to empirically measure their performance parameters.  Some astute commenters uncovered in my terrible juggling video, that I didn’t actually have any ground truth measure of torque with these actuators.  Given that the ultimate torque is a pretty useful performance metric, it’s a good thing to have a solid understanding of.

To measure this, I built a simple test fixture (which is also the qdd100 beta development kit), consisting of two brackets.  The first lets the servo be bolted to a table, and the second mounts to the output and has set screws to hold a 1" diameter pipe.  I used this to insert a 1 meter pipe which then can press against a digital scale.

[img: 1m is pretty long for my workshop!]

      

1m is pretty long for my workshop!
    

Then I created a simple C++ application which emitted torque commands in response to joystick input and reported back telemetry from the servo: qdd100_test

Using these I was able to generate a plot of actual torque vs motor phase current:

[img: qdd100 torque vs phase current]

There are a couple of interesting things here, one is that the torque constant at low phase currents is slightly lower than I had estimated based on the motor’s Kv rating.  Second, the torque constant drops off faster at higher currents than I had anticipated, and third, the motor Kv rating was lower than I had predicted.  Those things combined result in a peak torque of between 12.5 and 15Nm depending upon the servo.  That’s still enough torque to do some serious jumping, but exploring those discrepancies is now on my backlog.

Here’s a video showing how this testing (and max speed testing) was done:

      
    

		
		
			

	
	
		
- 
			qdd100
		
		
- 
			servo
		
		
- 
			servomk2
		
		
- 
			shop
		
		
- 
			torque
