# Velocity and acceleration limited trajectories - mjbots blog

URL: https://blog.mjbots.com/2022/04/08/velocity-and-acceleration-limited-trajectories/

One of the oldest requested features for the moteus brushless controller has been a form of trajectory control beyond constant velocity trajectories. For most applications this is not an actual deal-breaker, because arbitrary trajectories can be approximated by piecewise linear constant velocity trajectories in the application layer. However, for many people, that is big hurdle to jump over to start with, and for some, it can actually limit application effectiveness because a fair amount of CAN bandwidth is required to achieve the high rate control necessary for smooth motion.

So, as of release 2022-04-07, moteus now supports velocity and acceleration limited trajectories out of the box. All devkits now come with human-eye-pleasing limits enabled by default, although bare boards leave it disabled as per board defaults. There are two parameters to control the feature:

- servo.velocity_limit - The maximum velocity used when moving to a particular target goal

- servo.acceleration_limit - The maximum acceleration used when moving to a particular target goal

Additionally, these limits can be overridden on a per-command basis, both using the diagnostic protocol “d pos” mechanism, and the register implementation.

Check out this video, then read below the fold for more details:

      
    

There are a few details to the implementation which help it fit better into the existing moteus control framework, while enabling some features that don’t exist elsewhere.

## Special-valued limits

First, either limit may be “nan”, (the usual special value in moteus configuration-speak). In that case, no limits are applied. If both are “nan”, that results in identical behavior to what was previously used in moteus, in that a “`d pos`” command immediate attempts to achieve the desired position and velocity. If velocity is limited, but not acceleration, all such commands result in constant velocity trajectories to reach the target position, followed by continuing at the target velocity indefinitely. If acceleration is limited but not velocity, then the velocity will be continuous, but unbounded (except possibly by `servo.max_velocity`). If both are limited, then the velocity will be continuous and bounded.

## Non-zero velocity targets

The target state is a position and a velocity. moteus will reach the position and be traveling at the desired velocity when it reaches it. After reaching the state, the controller will continue at that target velocity indefinitely, or until another command is received.

## Idempotent commands

For many cases, it is still possible to construct commands such that they are idempotent. That means that you can send the same command over and over and it will not affect the semantics of the control. This is particularly easy if the destination state has a zero target velocity.

If the destination state has a non-zero velocity, then the command will need to change sufficiently far in advance to avoid “looping around”. If a command with a non-zero velocity is received after the machine has passed through the target state, then the only way to achieve it is to slow down, go back, and accelerate again so as to be moving at the proper speed. For applications like these, it is best to always have the target be at least several control cycles in the future so there is no risk of such looping.

## Completion indication

It is now feasible, in many cases, to send moteus control commands very infrequently. A complete motion from stopped in one position to stopped in another can be initiated with a single command. So that the application code can advance in a timely manner, moteus now reports a “completion” indication that flags when the target position and velocity has been reached. This can be accessed as register `0x00b` in register mode, or as `servo_stats.trajectory_done` in diagnostics mode.

## Interaction with the “stop position”

Previously moteus had the “stop position” mechanism to emulate a constant velocity trajectory with a fixed endpoint. That mechanism is still present. While it has defined interactions with the acceleration and velocity limits, they are, shall we say, “not particularly useful”. Thus, it is recommended to not use the “stop position” feature combined with the velocity and acceleration limits unless you really know what you are doing.

## Jerk limited trajectories

The implementation in moteus only supports limiting acceleration, not jerk. Thus the acceleration is allowed to be discontinuous. For an application where jerk limited trajectories are required, piecewise interpolation is still required. For accelerating phases, this can be trivially accomplished by slewing the acceleration limit override in each moteus command. For decelerating phases, or where precise control over position is required, a full fledged jerk-limited planner (like ruckig) will be required. The output from such a planner is probably easiest to feed to moteus as a piecewise series of constant velocity trajectories with no limits configured as before.
