# Unlimited rotations for moteus - mjbots blog

URL: https://blog.mjbots.com/2020/09/21/unlimited-rotations-for-moteus/

The moteus controller has always supported multiple turns when counting positions. It has a one-revolution magnetic encoder built in, but after turn on, it keeps track of how many turns have occurred. However, if you’ve followed previous moteus tutorials, you have probably noticed a persistent caveat that for accurate control, the position of the output shaft needs to stay within a hundred revolutions of 0.0 or so. Now, I’ll describe why that was, and what I’ve done to remove the limitation, allowing unlimited rotations!

## Background

The moteus controller uses a somewhat unique integrated position / velocity / torque control loop. This formulation gives a couple of advantages: First, there is no bandwidth loss due to having a cascaded position and velocity controller. Second, when driven by a higher level controller, it can seamlessly switch between position, velocity, and torque control, or any combination of them without having to manage mode transitions.

The command consists of the following values, all as 32 bit floating point values (optionally upscaled from integer values using the register protocol).

- Desired Position

- Desired Velocity

- Feedforward Torque

- kp scale

- kd scale

- Maximum Torque

The control loop measures two quantities as input, the “current position” and the “current velocity”. The position is measured as a 32 bit signed integer, where one revolution of the magnetic encoder equals 65536 counts. The velocity is numerically differentiated across the most recent 6.4ms of movement.

There are two internal state variables as well: One is the “target position”. This captures the most recent position command, and is advanced by the velocity command at the full control rate. The other is the integrative term of the PID controller. Both of these are stored as 32 bit floating point values.

## The problem

This structure poses a few inherent limitations. One, being that as the control position is sent as a floating point value, the resolution available for positioning decreases as you get further from 0. That probably isn’t a big limitation, as there aren’t many applications where you want to have both absolute positions and also unlimited revolutions.

The bigger limitation is in the “target position” internal state variable. It needs to be updated to take into account the current velocity command at every control cycle, or 1/40000 of a second. For a commanded speed of 0.01 revolutions per second, this incremental update is only 2.5e-7 of a revolution. Given that 32 bit floating point values only have roughly 7 decimal digits of mantissa available to them, you don’t have to get far beyond 0 before an update that small doesn’t even change the value at all.

The command format also has an option, such that if the command position is set to a floating point NaN value, it will “capture” the current position. This can be used to command velocity-only control with an implicit integrative term, or when combined with a stop position to move to a target at a fixed velocity. However, since “capturing” stores the value as a floating point value, significant precision can be lost. This was only a problem at larger position values, but at the maximum position before wraparound, the available capture resolution was measured in multiple degrees.

## The resolution

The resolution was relatively straightforward. Instead of storing the “target position” as a floating point value, it is now stored as a 64 bit integer measured in 1/(2**32) of a magnetic encoder revolution. This gives sufficient precision to represent velocities as small as 0.0001Hz (0.036 dps) uniformly at all positions, while still having more absolute range than the measured current position value. The final PID controller is then expressed relative to the target position. This lets it still operate in floating point coordinates, but with no worry about large artifacts due to a position offset.

The only other implementation hurdle was making it run fast enough. Largely that revolved around ensuring there was never a need to convert between 64 bit integers and floating point values, which is relatively slow on the STM32G4.

## The result

With this fix in place, it is possible to operate the controller safely at high velocities for arbitrary periods of time. Even when the “current position” value wraps around from positive to negative! Also, low speed control works just as well at any position offset. When operating in those “continuous rotation” applications, the user should just be careful about if the “desired position” field of the command should be set. Largely, it should be left as NaN for when used in continuous rotation applications.

Here’s a video showing high speed wraparound and low speed at arbitrary offsets.
