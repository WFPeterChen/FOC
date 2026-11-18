# recapturing position and velocity with moteus - mjbots blog

URL: https://blog.mjbots.com/2024/02/07/recapturing-position-and-velocity-with-moteus/

Recently I covered a new feature for the moteus brushless controller that improved robustness for velocity control when external torques exceed the maximum allowable control torque. In this post, I’ll cover a feature that can be used for a similar situation in position control, “recapturing position and velocity”.

In some applications, while in position mode, it can make sense to limit the maximum torque during specific periods so that external torques are able to overpower the controller. The most obvious case would be if the maximum torque is set to 0, or the kp and kd scale are set to 0 temporarily. A problem then arises when resuming control, as the “control position” will be wherever it last was, despite the fact that the actual motor have have been dragged by the external torque some distance away. When the torque limit is released, a large transient can develop as the controller tries to instantaneously get back to the old control position.

Previously, the only suitable workaround was to stop the motor driver entirely by issuing a “stop” command, then re-starting control by sending a new position mode command. This works, but causes a moderate delay before control can be re-established, as moteus executes a self calibration procedure of several milliseconds before resuming control.

As of firmware release 2024-01-06, there is now a new command, “recapture position and velocity”, which can be used in these circumstances. It forces the control position and control velocity to match the measured values without exiting position mode. It can be sent using all the normal methods, i.e. through the diagnostic protocol with tview, or the register protocol with raw CAN frames, the python library or C++ library. A typical operation might look like:

```
`initially start control by sending position mode commandafter some time, set maximum torque to 0 to let the motor free-wheelan external torque drags the motor far from its initial positionsend the "recapture position and velocity" commandimmediately after send a position mode command with nonzero max torque
```

Assuming that a velocity limit is in place, the following plot shows what the control position and actual position would look like with no mitigations in place, and with using the new recapture command.

[img: /2024/02/07/recapturing-position-and-velocity-with-moteus/images/20240117-recapture_position_velocity.png]

For reference, the different means of commanding this look like:

- 

Diagnostic channel: `d recapture`

- 

Python: `await c.set_recapture_position_velocity()`

- 

C++: `c.SetRecapturePositionVelocity()`

		
		
			

	
	
		
- 
			control
		
		
- 
			moteus
