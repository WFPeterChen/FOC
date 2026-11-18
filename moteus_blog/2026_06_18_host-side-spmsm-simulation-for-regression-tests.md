# Host side SPMSM simulation for regression tests - mjbots blog

URL: https://blog.mjbots.com/2026/06/18/host-side-spmsm-simulation-for-regression-tests/

When developing features for the moteus controller, testing on actual hardware has long been the primary way that new motor control features were validated.  That includes “hero” testing on the lab bench, and the hardware-in-the-loop (HIL) dynamometer fixture used to validate each new firmware release.

While those are valuable techniques, dynamometer testing is not something that you can do rapidly, is limited to validating a small number of things at a time, and has a non-zero fraction of false positive failures due to unmodeled effects.  To enable more rapid feature development, I wanted to test a lot of what was validated on the dynamometer fixture in a host side simulation.  This would provide a lot of benefits:

- It could run much faster than real time

- 100% deterministic

- Multiple instances can run in parallel

- Agents can use it as success criteria for validating development

Read on to see how it was done:

## Implementation

Enter, the moteus simulation-based testing infrastructure!

- https://github.com/mjbots/moteus/commit/14e5da2e035e0f33c0517fd923058f5c2ced5740

This simulates the motor in the DQ frame by numerically integrating the basic SPMSM differential equations:

$$L_d {d_{id} \over dt} = v_d - R i_d + \omega_e L i_q$$
$$L_q {d_{iq} \over dt} = v_q - R i_q - \omega_e L i_d - \omega_e \lambda_m$$

Electromagnetic torque is derived from the Q axis current using the same non-linear model that moteus and mpat use.

The firmware itself was restructured so that the majority of the control ISR is extracted into a separate header that can be compiled either for the target or for the host.  When integrated with the simulation, the output phase terminal voltages are, instead of driving the PWM generators, feed phase terminal voltages to this simulation.  Similarly, the sensed phase currents and encoder value are populated with simulated values instead of from actual hardware.

The Park and Clarke transforms are used to basically undo the equivalent transforms done in the firmware itself, so that the interface between the firmware and simulation is completely in the phase terminal reference frame.

## Tests

The baseline test setup is more comprehensive than you might imagine.  I started by porting basically every test I could from the automated dynamometer tests to the simulation framework, attempting to keep the test success criteria as close as possible to what was checked on the physical hardware.

All of those tests had largely been devised to catch specific problems that had made it to the release stage before.  Now all of them are validated in CI!

In addition to ensuring non-regression of previous fixes, this has
already been used in the development of some features, like
`max_regen_power_W`
and has been critical in development of more as-yet-unannounced
features.

## Limitations

There are important effects that the simulation does not model yet.  An almost certainly not exhaustive list:

- Iron losses: At non-zero speed, energy is lost in eddy currents in the windings of the motor.

- Cogging torque: With a permanent magnet motor, the magnets interact with the iron stator which creates a periodic electromagnetic torque in the absence of any winding current.

- Encoder noise: While the actual firmware’s encoder filtering is used, the simulated encoder itself is modeled with zero noise.  In practice, assuming the filter bandwidth was set conservatively, this does not introduce much error.
