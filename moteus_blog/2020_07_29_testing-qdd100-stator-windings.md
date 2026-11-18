# Testing qdd100 stator windings - mjbots blog

URL: https://blog.mjbots.com/2020/07/29/testing-qdd100-stator-windings/

My initial design torque for the qdd100 was a little over 17 Nm. However, when I did my first ground truth torque testing, I found that some servos had a lower maximum torque than I had specified. While working to diagnose those, I built a qdd100 that used an alternate stator winding of 105Kv instead of the 135Kv that are in all the beta units. The Kv rating of a stator describes how fast the motor will spin for a given applied voltage. If you assume the same amount of copper mass of wiring, a lower Kv will mean that there are thinner wires that wrap around the stator more turns (or fewer wires in parallel). A higher Kv will have thicker wires with fewer overall turns.

On paper, if you assume a perfect controller, this shouldn’t make much of a difference. The same input power should be required for the same output torque. The only differences should come into play once you have a controller with either a limited maximum voltage or a limited maximum current. The higher Kv motor will be able to go faster given a fixed maximum voltage, and the lower Kv motor will have more torque for a given maximum current.

I wanted to verify that this was true as part of my evaluation to identify the cause of my decreased torque, so I used a slightly upgraded torque testing fixture:

[img: /2020/07/29/testing-qdd100-stator-windings/images/dsc_0872.jpg]

For now, I rigged up the world’s cheapest load cell from amazon to a Nucleo configured to report the load in grams over the serial port. I also wired up my Chroma power supply over USB using the linux USBTMC driver. With those two things hooked up, I was able to run tests that sweeped across torque commands, while recording output torque, phase current, and input power.

At higher torques, the input power was pretty sensitive to the temperature of the windings – hotter windings increased the resistance, which increased the power required to achieve a given phase current, thus my plot isn’t perfect as it was grabbed over several different runs. For the highest power samples I couldn’t use my Chroma, as it is limited to around 600W. Thus those samples don’t record the input power.

[img: /2020/07/29/testing-qdd100-stator-windings/images/20200726-stator-comparison.png]

Plotting the input power vs output torque on the same chart shows that indeed, modulo some measurement error, they are the same for the two stators:

[img: /2020/07/29/testing-qdd100-stator-windings/images/20200726-torque-vs-power.png]

So, this experiment reaffirmed my understanding of stator magnetics and confirmed that the stator winding was not the cause of my decreased torque.

		
		
			

	
	
		
- 
			actuator
		
		
- 
			moteus
		
		
- 
			moteus_r43
		
		
- 
			qdd100
		
		
- 
			torque
