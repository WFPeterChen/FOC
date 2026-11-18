# Flux braking with the moteus controller - mjbots blog

URL: https://blog.mjbots.com/2019/11/04/flux-braking-with-the-moteus-controller/

When I was trying my first pronking, I kept having over-voltage issues when the servos were trying to dump power back onto the DC bus, no matter how high I set the voltage limit.  During that test, I was running with a nearly full battery, so my working theory is that the battery protection circuit was disconnecting the battery either because of too high a charging current, or too high a system voltage.  When the battery was disconnected, and the servos were still putting power onto the bus, that resulted in the voltage spiking arbitrarily high, followed by a total loss of power when they all faulted and then nothing was powering the bus at all.

Clearly I needed somewhere to dump the power during those transient events.  One option would be to build a brake chopper, sticking a power resistor somewhere and using that to burn off the extra.  But, why bother with that when I already have 12 big power resistors attached to the robot!  Errr… motors that is.

Anyways, my solution for now is to create a “virtual resistor” in the moteus firmware  i.e. “flux braking” that just dumps current into the D-axis of each motor proportional to the magnitude of the DC bus voltage above some configurable threshold.  That results in each servo working independently to keep the overall system voltage from getting too high in a hopefully stable manner.

[img: 20191028-current_vs_voltage.png]

I ran the programmable power supply through a voltage sweep while commanding zero velocity on all 12 joints to verify this was working as expected.  It did get pretty audibly noisy at the higher voltages, probably because of some feedback with the voltage sensing so I added a little filtering after which the noise was manageable.

		
		
			

	
	
		
- 
			moteus
		
		
- 
			quada0
