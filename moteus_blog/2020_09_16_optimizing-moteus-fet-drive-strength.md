# Optimizing moteus FET drive strength - mjbots blog

URL: https://blog.mjbots.com/2020/09/16/optimizing-moteus-fet-drive-strength/

The moteus controller uses a DRV8323 smart driver IC to drive the power MOSFETs as well as provide various safety functions. One of the capabilities it has which has so far been unexplored in moteus is its ability to control the drive strength and dead time through software configuration.

In a switching power supply or switching motor inverter, MOSFETs are arranged in a half bridge configuration. Depending upon the type of converter, one or more half bridges are used (3 phase inverters like moteus use 3 of them). Each “half bridge” has two MOSFETs, one connected between positive power and the output terminal, and the other connected between the output terminal and ground.

[img: /2020/09/16/optimizing-moteus-fet-drive-strength/images/2020-09-15-133937_403x425_scrot.png]

Power MOSFETs typically have relatively large gate capacitance, so to change their state quickly requires a lot of current. Additionally, you never want both MOSFETs conducting at the same time, otherwise current would flow straight from the supply to ground, called “shoot through”. Thus the driver has a configurable “dead time” which enforces that both are off for at least that long when switching (currents flow through the body diodes of the MOSFETs during this state).

## Optimization

Selecting these parameters is a balancing act. If the drive current is too low, the MOSFETs take a long time to turn on and off, which means they spend more time in a high resistance state. At some point however, higher drive currents don’t make the MOSFETs switch any faster, and just burn more power in the driver without any benefit. Similarly for the dead time, if it is too low this will result in shoot through, and if too high, it will result in current flowing through the body diodes for longer, which is much less efficient.

Until now, I hadn’t done any real optimization of these parameters, aside from ensuring the system was functional within a safety margin. In advance of some other to be announced developments, I decided to take another look.

To make a test, I set up a moteus controller with a test motor, but set up so that there was no thermal connection between the motor and the controller, and that the controller was not heatsinked at all. That would allow me to more easily determine how much heat was coming from the controller itself. Then, for various supply voltages, I commanded a fixed D phase current with just enough Q phase voltage so that the motor gently spun around. This ensured that all 3 of the half bridges were used equally. Then, I waited until things had reached thermal equilibrium, and used my DIY thermal board inspector, to measure the temperature of the motor windings, FETs, and the DRV8323.

[img: /2020/09/16/optimizing-moteus-fet-drive-strength/images/dsc_0968.jpg]

With that test methodology in hand, I was able to search and locate the optimal drive strength, and discovered that I can use the smallest available dead time with no problems.

  
      
          Measurement
          Old Settings
          New settings
      
  
  
      
          DRV8323 @ 24V / 8A phase current
          73C
          69C
      
      
          DRV8323 @ 32V / 8A phase current
          86C
          78C
      
      
          FETs @ 24V / 8A phase current
          64C
          56C
      
      
          FETS @ 32V / 8A phase current
          74C
          61C
      
  

Change in thermal equilibrium with no heatsinking

So, a nice win, especially at higher input voltages. The updated settings are in git master now, and will soon be in a new release.

		
		
			

	
	
		
- 
			control
		
		
- 
			moteus
		
		
- 
			moteus_r43
