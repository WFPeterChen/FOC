# New "hold position" watchdog timeout mode for moteus - mjbots blog

URL: https://blog.mjbots.com/2023/08/22/new-hold-position-watchdog-timeout-mode-for-moteus/

For a while now moteus has had the ability to configure what action takes place upon a CAN message watchdog timeout during position control mode. If configured with `['servo.default_timeout_s'](https://github.com/mjbots/moteus/blob/main/docs/reference.md#servodefault_timeout_s)` moteus requires that CAN messages be sent at a regular rate. If ever a message is delayed by more than the timeout period, the mode switches in a latching manner to the “timeout” mode, where a special action is undertaken. This is configured with ’`servo.timeout_mode`’ in tview, and as of firmware release 2023-07-25 the available values are as follows:

0: When set to 0, upon timeout, the driver will enter the equivalent of the “stopped” mode and all power will be removed from the motor phases. This will cause the motor to free spin as if there was no driver attached.

10: “hold position”. Here the controller will first decelerate to 0 speed, and then hold position as if a “d pos nan 0 nan” command were issued. Deceleration happens using the default configured acceleration limit in `'servo.default_accel_limit'` and the default PID gains are used to hold that position.

12: “zero velocity”. Here the controller enters a mode where 0 speed is commanded with no constraint on position. It provides a damping effect on the motor to remove energy from the system. The default `kd` PID gain is used to control this effect and the configured kp and ki terms are ignored.

15: “brake”. For mode 15, the controller shorts all three motor phases together. This also provides a braking force. The amount of braking force is not configurable, but is also not reliant on firmware executing to operate so will function over a larger range of potential failure modes.

Mode “10” was newly released in firmware version 2023-07-25 and it works in the same manner across the moteus-r4, moteus-n1, and qdd100.
