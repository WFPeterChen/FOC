# moteus clock synchronization - mjbots blog

URL: https://blog.mjbots.com/2023/02/20/moteus-clock-synchronization/

The moteus controller, when it implements its control algorithms, uses the internal RC oscillator of the onboard STM32G4 microcontroller to calculate things like velocity and to advance position over time. Typically, this is accurate to within 0.5% which is more than sufficient for most applications. However, there are cases where it does matter.

One common case is when multiple moteus controllers are operated together, and either the relative velocities of the controllers must match closely, or the time required to complete long trajectories must match closely. For example, if a trajectory would take 100s to complete, then a 0.5% difference in clock rate between two controllers would result in one completing 0.5s before the other.

As of firmware release 2023-02-01, there is now a mechanism whereby an application can synchronize the clock of a moteus to the host time base. If this is done for multiple devices, then they will all share the same time base and, on average, produce identical velocities and trajectory time-to-completes.

## Implementation

To make this work, there are two pieces. First we need to be able to change the rate at which the microcontroller’s clock operates. The microcontroller does provide a trim mechanism for exactly this purpose. At the factory it is calibrated and then the firmware is able to further tweak the result in approximately 40kHz increments, as compared to the 16MHz base RC oscillator frequency. For moteus, that works out to about 0.25% increments of speed. This trim was already exposed as an undocumented configuration option `clock.hsitrim`, but not in a form suitable for modification online.

So, the first part of this work creates a new register, “0x071 Clock Trim” to which an integer can be written. This integer is an offset to apply to the factory programmed trim, so 0 results in running at the factory default rate, 1 results in running ~0.25% faster and -1 results in running ~0.25% slower.

The second part of this work provides a way for the host application to measure how fast the moteus time base is progressing relative to the host time. This operates through the “0x070 Millisecond counter” register, which merely reports the number of elapsed milliseconds as counted by moteus.

Using these two features, an application works as follows: At a regular rate, it polls the millisecond counter to see how many milliseconds moteus thinks has passed. If that is more than have passed on the host, the moteus clock can be slowed down. If it is less, then the moteus clock can be sped up. Additionally, a measure of the total time elapsed since application start can be used to zero out the overall drift. Doing this in a robust way requires a little bit of thought, but there is an example python implementation in the moteus repository showing how it can be done:

- lib/python/examples/synchronized_movement.py

## Caveats and Conclusion

One caveat is that since the resolution of time rate adjustment is relatively large, 0.25%, velocities on multiple controllers may instantaneously differ by approximately that much. Only averaged over time would they match exactly. Secondly, the synchronization procedure is more complex than base control and may introduce other failure modes.

Synchronizing clocks is not likely to be used all that often, but in some cases it can be simpler than other approaches. If it might be useful to you, take a look a the example script above, or drop into the mjbots Discord #moteus channel and ask!
