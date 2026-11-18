# Measuring thermal parameters empirically - mjbots blog

URL: https://blog.mjbots.com/2025/06/26/measuring-thermal-parameters-empirically/

In the last
post,
I gave an overview of what thermal metrics are relevant to motor drive
applications and how they drive the important performance metrics of
controllers and motors.  In this post, I’m going to look at how to
measure those thermal parameters empirically in at least a crude way,
but with enough accuracy to be useful for practical design
applications.

## What do we want to measure?

There are a set of parameters that we would like to be able to measure
that have some overlap between the controller and motor case.  For
both, we want to be able to measure:

- R_lab: The thermal resistance between the controller or motor
and ambient under lab conditions with no cooling.

- R_cooling: The thermal resistance under various cooling
conditions.

- C_bare: The heat capacity with no cooling present.

- C_cooling: The heat capacity with various additional features
present, like heat spreaders.

Then, some desired mappings are unique to one of controllers or motors:

- Pc(V, PWM, I): For controllers, a function that determines the
waste thermal energy from supply voltage, PWM frequency, and output
phase current.

- Pm(I, ω): For motors, a function that determines the waste
thermal energy from phase current and motor angular velocity.

## How to measure the controller parameters

With the newly improved
dynamometer,
it should be possible to measure all of these parameters for the
controller in a relatively straightforward manner, although the exact
order of which parameters to measure first is critical.  Here’s my
plan

1. Measure R_lab: The first parameter to measure is the thermal
resistance of the controller.  To do this, I’m going to take advantage
of two facts.  First, that the thermal loss varies based on supply
voltage and PWM frequency even if no motor is connected or zero
current is being driven by the motor.  Second, when using the Chroma
source for the DUT in the dynamometer, I can programmatically measure
the power input very accurately.  If no power is going to the motor,
then every watt of power coming out of the supply must by necessity
ending up as thermal loss.

Thus the idea is to step through a few different supply voltages and
PWM frequencies.  At each step, wait for the temperature of the
controller to completely stabilize.  Then I have a mapping between
power input and temperature which can be used to calculate R_lab.

2. Sample Pc(V, PWM, I): Now that R_lab is known, we can use
it to measure how much thermal loss is present in scenarios where some
power is going to the motor.  Just wait for the temperature to
stabilize, and solve for P in the thermal resistance formula:

$$P = {{T_{infinity} - T_{ambient}} \over R}$$

To get a better sense of the mapping, the sampling process I used was
to roughly equally sample across the supply voltage and PWM space.
For each point in that space, ramp up current linearly until the
maximum temperature I was willing to test with was hit.  This
generates a 3 dimensional sparse mapping of the Pc(V, PWM, I)
function.

3. Measure C_bare: The heat capacity is measured by using a
technique similar to that for R_lab.  First, the controller is
soaked at the supply voltage and PWM which results in the lowest
thermal loss.  The temperature and power required is recorded.  Then
it is soaked at the supply voltage and PWM which results in the
highest thermal loss, at which point the temperature and power is
recorded.  Finally, the controller is switched back to the original
supply voltage and PWM frequency, and the time required to reach 63.2%
of the way back to the original temperature is measured.  Those
parameters give us enough information to calculate the heat capacity.

$$C\_bare = {\Delta t \over R}$$

4. Measuring R_cooling: This can be done in the same way as
R_lab, but separately for each different cooling method.  For all
moteus controllers I tested, I used at least the largest fan that
would fit board mounted, the heat spreader for the board, and the heat
spreader for the board with a heat sink attached.

5. C_cooling Can be measured in the same was a C_bare, but
with each of the different cooling methods attached.  This will only
really be different when the cooling method involves the addition of
significant thermal mass, like the heat spreader or a heat sink.

## How to measure the motor parameters

Measuring the motor parameters doesn’t necessarily require the
dyanamometer, as we don’t need to apply non-zero torque.  Instead we
can generate copper losses by applying current purely in the D axis,
which makes the setup a bit simpler.  In contrast to the controller
parameter measurement though, a thermistor does need to be installed
in the windings of each motor so that we can sample the motor
temperature directly.

The procedure I used for the motors was to perform an integrated
measurement of thermal resistance and heat capacity in one go.  This
was similar to the procedure for the heat capacity of the controllers,
but does require knowing the thermal resistance of the controller as
well in order to accurately know the actual electrical energy making
it into the windings of the motor.  During the initial soak phase, no
power is applied to the motor and its temperature is sampled.  When
applying power, a D axis voltage of slowly varying angle is applied so
as to ensure all coils are evenly heated.  The input electrical power
to the motor is estimated by first estimating the thermal loss in the
controller from its temperature, then subtracting that power from the
power supply output.

$$P_{motor} = P_{psu} - {{T_{controller} - T_{ambient}} \over {R_{controller}}}$$

With the power to the motor and its temperature at the two operating
points, and the time required to fall 63.2% of the way back to the
lower point, it is possible to estimate both the thermal resistance
and heat capacity of the motor.

$$R_{motor} = {{T_{peak} - T_{ambient}} \over P_{motor}}$$

$$C_{motor} = {\Delta t \over R_{motor}}$$

Once again, the values of these for different cooling options can be
measured as well.  In order to just capture the spectrum, I measured
motors at ambient, and with the biggest fan I had on hand blowing
directly through the windings in order to get an estimate of the
“maximimal” cooling scenario.

For now, I’m going to ignore measuring the iron loss component of
Pm(I, ω), not because it isn’t important, but just because I ran
out of time and it isn’t that large of a factor in many robotics
applications.

## Next steps

In the next post, I’ll cover how I turned all this data into the first
version of something of a useful tool that can be used to help design
systems with moteus controllers.
