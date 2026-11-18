# Default bandwidths for moteus - mjbots blog

URL: https://blog.mjbots.com/2024/01/16/default-bandwidths-for-moteus/

moteus has long had both automatically tuned current control bandwidth and a built-in low-pass filter for encoder readings. During calibration, these are usually set with `moteus_tool`’s `--cal-bw-hz` option, which specifies the current loop (interchangeably called the torque loop) bandwidth. TLDR is that these heuristics have changed as of pypi 0.3.64, hopefully with only improved behavior. Read on if you want to understand more or if you need to resolve problems with something that changed for the worse.

## Overview

The encoder bandwidth selected, modulo some heuristics, was usually twice that of the current loop. This was a quick and dirty heuristic which worked alright for a long time, but didn’t have much of a basis behind it. While cleaning up calibration for gimbal-style motors, I decided to take another pass at this as well.

The two filters, current and encoder, serve different but inter-related purposes. The encoder filter, maybe obviously filters readings from the primary position sensor. Movement that is higher bandwidth than the filter will be not observable by moteus. Thus, if the platform is capable of movement at bandwidths higher than the filter, the control loop will be operating on incorrect data and will usually become unstable. However, as the bandwidth is increased, inherent noise from the encoder will result in the controller producing audible noise as it attempts to correct for the “noise” in the measurement in position and velocity.

The current loop similarly controls how quickly the controller can change the torque it is commanding. It is similar to the encoder filter in that higher cutoff values (i.e. larger bandwidth) results in higher performance but more audible noise. This should typically be around the same as the encoder filter, as most of the changes in the desired torque will come from encoder readings. However, the commanded torque (or position/velocity) can also change at potentially high rates in response to application demands, and thus there may be a need for it to be higher than the encoder filter.

## Diagnosing inappropriate bandwidth

If the bandwidth is too low for either filter, performance and stability will degrade. For either filter, oscillation or vibration may result. Commanded movements may overshoot their intended target, especially at relatively high accelerations. Commanded torque will not track accurately.

If the bandwidth is too high for either filter, the primary downside is audible noise. If the limits within `moteus_tool` are circumvented, the filters themselves may become unstable, resulting in erroneous operation.

A special case of problems with a too-high encoder bandwidth occurs with encoders of extremely low resolution. The most common case here are using hall effects as the output position sensor. In this case, too high of a bandwidth will result in poor velocity sensing, and thus poor velocity and position control. In those applications, the range of usable encoder bandwidths may be very small for velocity or position control (although for torque control going higher is always fine).

## Updated behavior

To better balance these factors, as of pypi 0.3.64, `moteus_tool --calibrate` is changing things in a few ways:

1. The biggest, is that it will now default to set the encoder bandwidth to be equal to the torque bandwidth rather than be equal to twice it. For most users, the only result of this is that motors will have less audible noise during operation. In some cases, the lower encoder bandwidth may result in instability or oscillation, but given that the torque bandwidth was already that low, this is unlikely.

2. Another change is with respect to high inductance motors. The most common types of high inductance motors are gimbal motors and hoverboard motors. Previously, a different heuristic would artificially limit the torque bandwidth to a low value. This has since been deemed unnecessary, so now these motors will calibrate with a larger torque bandwidth by default. For gimbal motors, that means they may become usable even without resorting to `servo.voltage_mode_control` (which bypasses the torque loop entirely).

3. Yet another change relates to hoverboard motors. There had been a heuristic to attempt to limit encoder bandwidth on all high-inductance motors. That inappropriately caught gimbal motors. Now this limit is only applied if hall effect sensors are used to measure output position, which is the only time it would be useful.

4. Finally, no bandwidths are allowed to be greater than 1/30th of the control update rate. With the default 30kHz control rate, that permits bandwidth up to 1kHz for both torque and encoder. At the lowest currently supported rate of 15kHz, that falls to 500Hz.
