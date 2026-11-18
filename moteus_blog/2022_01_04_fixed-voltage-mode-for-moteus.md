# Fixed voltage mode for moteus - mjbots blog

URL: https://blog.mjbots.com/2022/01/04/fixed-voltage-mode-for-moteus/

The most recent moteus firmware release, 2021-12-03, added not one, but two new control modes for less common applications. Previously, mentioned was the “voltage_control_mode” for using gimbal style high resistance motors without changing the sense resistors. In this post, I’ll describe a similarly named, but very different mode “fixed voltage mode” for operating brushless motors as if they were a stepper motor.

For some applications, you don’t care about torque control, or about power consumption at all. Traditionally you would use a stepper motor in those applications, with a correspondingly less expensive stepper motor driver. However, in some cases you may still want to have high rate trajectory control, CAN based telemetry, or have already standardized on moteus controllers for other moving parts of your solution. There are two new options that can be used in such situations:

- servo.fixed_voltage_mode: When this is non-zero, the primary encoder is not used for control at all. Instead, positioning is performed entirely in the electrical phase space, with a fixed voltage applied always to the phase windings.

- servo.fixed_voltage_control_V: The fixed voltage to apply to the phase windings.

When this new mode is active, the controller will continually burn power in the windings of the motor and uses no feedback from the motor at all. As mentioned above, this makes it operate like a micro-stepping stepper motor, or an inexpensive gimbal motor controller. If the externally applied torque is greater than that produced by the fixed voltage, the motor will “skip” and lose track of its position.
