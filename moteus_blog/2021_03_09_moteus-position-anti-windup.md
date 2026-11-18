# moteus position anti-windup - mjbots blog

URL: https://blog.mjbots.com/2021/03/09/moteus-position-anti-windup/

The moteus controller uses a somewhat unique integrated position / velocity / torque controller with per-command configurable proportional and derivative gains. Through various combinations of these settings, it can emulate many different types of controllers, but one that it has struggled with until now was a pure velocity controller.

It has been minimally possible to use moteus as a purely velocity controlled since wraparound support was implemented, but that came with a caveat. Either the proportional term needed to be set to 0, in which case velocity tracking performance was poor, or if the proportional term was non-zero, an external torque would cause the position to drift arbitrarily far from the target position. Then if the external torque were released, the controller would “catch up” for all the lost ground, moving very rapidly.

Now, however, as of release 2021-03-05, an optional configurable parameter has been added to the moteus firmware which enables the “servo.max_position_slip” option. When this is finite, it acts as an anti-windup term on the position tracker, keeping it from getting far out of line. Tuning this lets you control how hard the controller tries to track velocity in the face of external disturbance, and how much catching up it will do when that external disturbance is relaxed.

This wasn’t conceptually hard to implement, but needed careful construction to interact properly with the existing stop position and position bounds that the firmware implements.

Here’s a video demonstrating the problem, and how the new configurable lets you resolve it:
