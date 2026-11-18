# Voltage mode control for gimbal motors - mjbots blog

URL: https://blog.mjbots.com/2021/12/06/voltage-mode-control-for-gimbal-motors/

The moteus brushless controller can drive many motors out of the box, but until now it has been challenging to use with gimbal style brushless motors. They are wound with thin wire so that they have a very high winding resistance, and thus can be driven by inexpensive low current controllers. Using something like moteus with a gimbal motor isn’t absolutely necessary, but does give benefits in terms of high performance trajectory tracking and torque control.

## The problem

The primary challenge when using moteus with high winding resistance motors is the mechanism moteus uses to sense current through the motor phases. It has a set of 3 current sense shunts, basically resistors, that have a small voltage induced across them proportional to the amount of current flowing. When commanding a specific current, moteus continually adjusts the voltage on each of the phase windings until the desired current is achieved. These resistors are sized so that moteus has reasonable resolution up to its 100A maximum current rating. When used with a motor that has a maximum current of 1A though, almost all of this resolution is wasted.

In the past I have replaced the sense resistors on moteus boards in order to drive gimbal motors. That is the optimal solution, as it allows moteus to continue to sense the current through the windings and provide accurate torque control. But not everyone is set up to perform surface mount soldering, and accurate torque control at all speeds and temperatures is not required in many applications.

## Voltage mode control

As of release 2021-12-03, moteus now has a new configurable option, called “voltage_mode_control”. In this mode, torque commands no longer use the current sense resistors for feedback. Instead, they use the calibrated phase resistance of the motor to derive what voltage would be necessary to achieve a given torque.

The downsides of this approach are that there are many factors that will make the mechanical output torque not match what was intended. For instance, the back EMF if the motor is moving at non-zero velocity, changes in the winding resistance due to temperature, or inductive coupling between the quadrature frames at non-zero velocity. All of these factors will reduce the accuracy of the torque command, so that the mechanical torque output will not match what was commanded. However, for many applications, the motor is not spinning quickly and precise torque control is not required.

Since this control mode takes effect only at the end of the normal moteus control loop, it means that all of the “position” mode control works as before. Thus the constant velocity mode, stop position, and the “within” mode can all be used without regard to whether the controlled motor is running in the normal “current mode control” mode, or in the new “voltage mode control”.

## Results

With the new `moteus_tool` calibration mechanism, this means that gimbal motors can be used in a basic fashion out of the box with moteus by tweaking only a single configurable value: `servo.voltage_control_mode`. Here’s a video showing how that works with a real motor.
