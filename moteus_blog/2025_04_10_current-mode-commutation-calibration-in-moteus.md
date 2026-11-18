# Current mode commutation calibration in moteus - mjbots blog

URL: https://blog.mjbots.com/2025/04/10/current-mode-commutation-calibration-in-moteus/

Way back in 2019, I documented the approach moteus has used for encoder commutation calibration ever since. In principle, it commands a fixed voltage that is swept through a range of electrical angles. Assuming the voltage is large enough, this will drag the rotor around with it similar to if the motor was a stepper motor. While that is happening, the commutation encoder reading is recorded over time, so that a mapping can be made between commutation encoder and the electrical angle of the motor. When combined with the old dead time compensation technique, it resulted in relatively sinusoidal current waveforms and thus smooth motion of the rotor and a smooth mapping.

However, with the recent removal of dead time compensation, we once again need a new way to achieve decently sinusoidal current waveforms to get smooth rotor motion during the calibration process. I took what was in hindsight the obvious approach - instead of commanding a fixed voltage that is swept through the electrical angle space, command a fixed current that is swept through the electrical angle space.

Getting this to work in the moteus firmware required a moderate amount of plumbing. The first step is a new option to position mode, a “fixed current override”. The behavior of this is nearly identical to the “fixed voltage override”, except that a constant current is commanded instead of a constant voltage. That means that the commutation encoder is ignored, and the output control position is used to determine the electrical theta. For now, there is no system-wide option to enable this, only an optional flag to position mode. It can be accessed with the diagnostic protocol using the “c” flag, with the argument in amps.

```
`d pos nan 1 nan c1
```

The second part of the equation is updating the onboard “d cal” routine. Now there exists a “d cali” routine which operates in current space instead of voltage space. The exact details of its interface remains implementation defined, as the only user is “moteus_tool”, but suffice it to say that it got updated.

Finally, “moteus_tool” looks at the firmware ABI version to decide if the current mode calibration is viable to use. Just because it is available does not necessarily mean it will be used though. “moteus_tool” looks to see if the noise present in the current sense path is small enough relative to the intended calibration current. If it isn’t, then it falls back to the old voltage control method.

The final result is an encoder commutation calibration that is at least slightly better or equivalent in all circumstances. Here are comparison plots of before and after with “utils/plot_cal_phase_currents.py” with a moteus-c1 driving a mj5208 for

[img: moteus-c1/mj5208: Voltage mode calibration]

      

moteus-c1/mj5208: Voltage mode calibration
    

[img: moteus-c1/mj5208: Current mode calibration]

      

moteus-c1/mj5208: Current mode calibration
    

		
		
			

	
	
		
- 
			control
		
		
- 
			moteus
