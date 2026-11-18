# Auto-tuning current control loops - mjbots blog

URL: https://blog.mjbots.com/2021/04/13/auto-tuning-current-control-loops/

Since the moteus controller was first released, it has implemented a two-stage controller. The outer loop is a combined position/velocity/torque PID controller, which takes as an input a position trajectory, and outputs a desired torque. The inner loops accepts this torque, and uses a PI controller to generate the Q phase voltage necessary to achieve that torque.

Until now, the constants for that PI controller were left as an exercise for the reader. i.e. there were some semi-sensible defaults, but the end-user ultimately had to manually select those constants to achieve a given torque bandwidth. That isn’t too much of a problem for a sophisticated user, but for the rest of us, it is hard to know how to go from a desired torque bandwidth to reasonable PI gains.

Now, I’ve finally decided to tackle this problem! There are two phases, the first being how to measure the inductance of a given motor, which is last major motor parameter to be automatically measured. The second is how to generate a controller using the resistance and inductance which has a desired bandwidth.

## Measuring inductance

An inductor is an energy storage device where the rate of change of current is directly proportional to the voltage across its terminals. Most (probably all?) electrical motors act as inductors in various forms. The hobby outrunners typically driven by moteus controllers, surface permanent synchronous magnet motors, can be treated as two equal valued inductors across the imaginary D and Q axes.

Given an ideal inductor, an easy way to measure that inductance is to apply a constant voltage and see how fast the current rises. That’s roughly the approach I took with moteus, but instead of a fixed voltage, it applies a square wave voltage centered about 0 across the D axis. That way the motor doesn’t move, the net current is 0 over time, and the average rate of change can be integrated over many cycles for much higher accuracy.

This “square wave” mode is implemented as a new control mode in moteus, primarily intended to be invoked by `moteus_tool` during the calibration process. You can specify the half-period of the square wave in control cycles, and the voltage to apply across the D axis. Here’s a plot of the current, as reported by a moteus controller over its debug DAC connection during a test on an mj5208:

[img: /2021/04/13/auto-tuning-current-control-loops/images/20210324-inductance-measure.png]

This shows that yes, the current is roughly triangular for the input square wave.

## Designing a current controller

Given this newly measured inductance, and the phase resistance that the calibration process already measured, we have sufficient information to design a PI controller which achieves a desired bandwidth response.

For a PI controller, a simple second order approximation of the closed loop transfer function can be written as:

$$I(s)=\frac{K_p s + K_i}{L s^2 + (R+K_p) s + K_i}$$

Where L is the D/Q phase inductance, R is the phase resistance, and Kp and Ki are the PI gains. Taking that, if we constrain:

$$K_i = K_p \frac{R}{L}$$

We can then substitute this into the original transform, which results in a first order system:

$$I(s)=\frac{K_p}{L} \frac{1}{s + \frac{K_p}{L}}$$

This system has a 3db frequency at the sole pole:

$$w_{3db} = \frac{K_p}{L}$$

Which then allows us to fully determine all the remaining parameters:

$$K_p = w_{3db} L$$
$$K_i = w_{3db} R$$

Finally, when we double check our results, the “rise time” for a first order system (roughly the time it takes to go from 10% to 90%) can be described as:

$$t_{rise} = \frac{0.35}{BW_{hz}}$$

A motor like the mj5208 can be approximated as a phase resistance of 0.04 ohm, with a D/Q phase inductance of 25e-6 H. For a 3dB bandwidth of 1000 radians per second (~160Hz), the gains measured in radians per second would be:

$$K_p=0.025$$
$$K_i = 40.0$$

To set these on the controller, we would use the following parameters:

```
`servo.pid_dq.kp=0.025
servo.pid_dq.ki=40.0
```

## Empirical testing

To make sure these make sense, I used this process for a few different bandwidths on a few different motors. For each, I used an oscilloscope to measure the rise time of the current waveform in response to a step input of a relatively small 4A command. The shape of that curve lets us be pretty confident that the damping ratio is correct, and the total rise time of the curve gives a good measure of the overall bandwidth that was achieved.

[img: /2021/04/13/auto-tuning-current-control-loops/images/bwtest.jpg]

Across the motors I tested, the resistance varied from 35-65 milliohm, with inductances varying from 9uH to 33uH. The grid shows that the bandwidth worked out pretty close to the target in all those cases.

## moteus_tool integration

Now that the math is out of the way, you don’t need to worry about it at all, because as of version 0.3.17 of moteus_tool (with version 2021-04-09 of the firmware), this whole process is automated. When you run the calibration step, you can just specify the desired 3db bandwidth on the command line of moteus_tool and it will calculate and set the parameters for you. By default it will select 100Hz bandwidth.

```
`python3 -m moteus.moteus_tool -t 1 --calibrate --cal-bw-hz 100
```

## Video

If you want to see this content explained more verbosely, here’s a video that covers most of it!

      
    

		
		
			

	
	
		
- 
			control
		
		
- 
			moteus
		
		
- 
			moteus_r45
		
		
- 
			motor
