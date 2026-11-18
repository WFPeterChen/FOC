# Dealing with stator magnetic saturation - mjbots blog

URL: https://blog.mjbots.com/2020/07/31/dealing-with-stator-magnetic-saturation/

In my previous experiments demonstrating torque feedback (full rate inverse dynamics, ground truth torque testing), I’ve glossed over the fact that as the stator approaches magnetic saturation, the linear relationship between torque and current breaks down. Now finally I’ll take at least one step towards allowing moteus to accurately work in the torque domain as motors reach saturation.

## Background

The stator in a rotor consists of windings wrapped around usually an iron core. The iron in the core consists of lots of little sub-domains of magnetized material, that normally are randomly oriented resulting in a net zero magnetic field. As current is applied to the windings, those domains line up, greatly magnifying the resulting magnetic field. Eventually most of the sub-domains are aligned, at which point you don’t get any more magnifying effect from the iron core. In this region, the stator is said to be “saturated”. You can read about it in much more depth on wikipedia or with even more detail here. The end result is a curve of magnetic field versus applied current that looks something like this:

[img: /2020/07/31/dealing-with-stator-magnetic-saturation/images/magnetic_saturation.png]

To date, moteus assumes that you are operating completely in the “Linear” region, where the torque and current are linearly related.

## Operating in the Rotation Region

To operate in the “rotation” region I ended up using the following formula:

$$\tau = K_T \cdot I_c + ts \cdot log2(1 + (I - I_c) \cdot is)$$

Where `I` is the input current, $$K_T$$ is the motor torque constant, `ts`, $$I_c$$ and $$is$$ are three constants that I fit to measured torque data. With some approximations, this can be calculated relatively efficiently on the STM32G4 that drives the moteus controller, adding only a microsecond to the overall loop time to go in both directions.

I then ran a torque sweep with my load-cell fixture from before, and sure enough, the input and output torque match much better now across the entire range of operation, despite the fact that the phase current needs to start growing very rapidly near the top end:

[img: /2020/07/31/dealing-with-stator-magnetic-saturation/images/saturation_compensation.png]

		
		
			

	
	
		
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
