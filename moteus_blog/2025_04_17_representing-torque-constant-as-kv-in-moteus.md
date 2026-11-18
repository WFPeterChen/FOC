# Representing torque constant as Kv in moteus - mjbots blog

URL: https://blog.mjbots.com/2025/04/17/representing-torque-constant-as-kv-in-moteus/

One of the characteristic metrics of brushless DC motors is the Kv value, which describes the relationship between the angular velocity of the motor and its back EMF. Somewhat unexpectedly, this constant also completely determines the torque constant of the motor, i.e. the relationship between phase current and mechanical torque output (see this Things In Motion post).

Since the very first release of moteus, this Kv constant has been stored in moteus using somewhat non-intuitive units as `motor.v_per_hz`. That makes a lot of sense internally, as nearly all math the controller has to do can be natively done with those dimensions. However, as a user visible motor constant, it is completely opaque. Further, as a result of my incremental discovery of the math behind BLDC motors, the constant used by moteus had some additional “fudge” factors baked in that were then backed out through other “fudge” factors in the firmware.

To clean all this up, as of firmware release 2025-03-27, moteus now records the torque constant of a motor as `motor.Kv`, and uses the convention that the Kv value is what is measured using the peak to peak voltage value between any two phases on an oscilloscope while the motor is spun at a fixed rate. That is the convention that most other controllers seem to use and should be relatively easy for anyone to verify based on first principles.

When upgrading with moteus_tool over CAN, the old constant will be transparently migrated to the new dimensions and any necessary fudge factors will be removed.
