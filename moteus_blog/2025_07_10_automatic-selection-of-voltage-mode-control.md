# Automatic selection of voltage mode control - mjbots blog

URL: https://blog.mjbots.com/2025/07/10/automatic-selection-of-voltage-mode-control/

For some time now, moteus has supported operating in what is called
“voltage mode control”.  In that mode of operation, the
current control loop of the Field Oriented Control process is short
circuited.  Instead of modulating the output voltage to achieve a
desired current, instead it is assumed that there is no inductance and
the desired voltage can be selected purely using the phase resistance
and back EMF.  This is a useful mode of operation anytime the output
current range is small relative to moteus’s ability to sense it, often
but not always with gimbal motors.  The drawback of the mode was that:

- there is no torque control

- you had to know it existed

- you had to manually select it

However, as of release
2025-03-27
/ pypi 0.3.77, moteus_tool will now attempt to automatically select
voltage mode control if it looks like the motor is high resistance
relative to the controller being used.

## Implementation

To make this work, during the calibration process moteus_tool measures
the amount of random current sense noise captured by moteus.  Then it
compares the magnitude of the eventual calibration current to this
noise to create a “current quality” metric that is just the intended
calibration current divided by the standard deviation of the measured
noise.  A threshold is used, below which, voltage mode control is
automatically enabled.  This will result in comparable results across
all moteus boards, including the moteus-c1 which has a reduced current
sense range (re higher accuracy), or in cases where custom current
sense resistors have been installed.

This is actually the same metric used to determine if current mode
commutation calibration can be used, as documented in this post.

## How to use it

For this, all you need is the most recent `moteus_tool`.  To get it, merely update your host side tools:

```
`python -m pip install --upgrade moteus
```

You can still change `servo.voltage_mode_control` manually either way
after calibration has completed if your application needs it the other
way than the auto-detected version.
