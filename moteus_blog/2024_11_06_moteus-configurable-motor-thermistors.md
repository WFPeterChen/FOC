# moteus configurable motor thermistors - mjbots blog

URL: https://blog.mjbots.com/2024/11/06/moteus-configurable-motor-thermistors/

When operating a moteus controller with a brushless motor there are two main things that can get hot: the FETs (field effect transistors) on moteus and the motor itself. By default, moteus has built in thermal throttling and fault detection if the FET temperature exceeds rated limits. In many configurations, the motor can be thermally connected to the moteus controller, so that the same FET temperature sensing can be used to prevent damage to the motor, but that isn’t always the case.

To help prevent damage to the motor, all current moteus controllers (n1, c1, and r4) have solderable pads that allow a thermistor mounted on the motor to be connected. Unfortunately, this has been less useful than intended for two reasons, one of which has recently been removed.

## Limitations

First, the current electrical design of moteus requires that the thermistor not be ground referenced. A thermistor, like any other resistor, is a two terminal device, which has a resistance that varies in a known way with temperature. moteus has two terminals which are intended to connect to the two terminals of the thermistor and requires that neither terminal of the resistor be connected to anything else. However, in many practical motor designs, one side of the thermistor is tied to the ground line of concurrently installed hall effect sensors. If you do need to use the hall effect sensors with moteus, this would prevent the thermistor from being used.

The second, and more annoying limitation, was that until firmware 2024-10-29, moteus only supported 47k NTC thermistors as a motor thermistor. Unfortunately, almost no motors in existence come with 47k thermistors, so this made the feature almost useless unless you installed your own.

It is this second limitation that has now been removed. As of firmware 2024-10-29, moteus now not only defaults to a 10k motor thermistor, but it can also be changed in configuration!

## How to configure

If you want to use a motor thermistor, you will need to change at least some parameters. The first ones are:

```
`servo.enable_motor_temperature 1
servo.motor_derate_temperature XX
servo.motor_fault_temperature XX
```

“enable_motor_temperature” turns on the motor temperature sensor at all. The second, “motor_derate_temperature”, specifies the temperature in celsius when moteus will start reducing the current provided to the motor in order to keep it cool. “motor_fault_temperature” specifies the maximum temperature the motor can achieve before a fault is declared.

Finally, while unlikely, you may also need to change:

```
`servo.motor_thermistor_ohm 10000
```

Given that most motor thermistors are 10k, this is likely fine for most applications, but you never know!

## Future work

At some point in the future, later versions of moteus will hopefully be updated so that ground referenced thermistors can be used too. Until then, hopefully at least some more applications are possible!
