# Improving the moteus update rate, part 4 - mjbots blog

URL: https://blog.mjbots.com/2019/09/23/improving-the-moteus-update-rate-part-4/

In part 1, part 2, and part 3, I looked at what was limiting the update rate of the moteus controller when built into a quadruped configuration and how to improve that.  Now, it is time for the final demonstration!

      
    

That video was shot with a 150Hz overall update rate.  The plot shows the commanded and actual position of the three joints in the front right leg, although not all to the same vertical scale.  Updating the servos themselves only used about 3.5ms per cycle, but the gait logic used another 1-1.5ms, which made hitting 200Hz not super reliable, thus running at 150Hz.

## Future work

I would still like to be able to perform 1kHz full system updates.  These experiments have let me come up with a plan that I think will achieve that with plenty of margin from the servo side in the next revision.

- Switch the controllers to use FD-CAN:  I had initially not used CAN as the communication mechanism because I didn’t want to be limited to 1Mbps and 8 byte frames.  However, recent STM32 controllers come with FD-CAN support, which allows up to 64 byte frames and 8Mbps.  The hardware FD-CAN receiver implements CRCs for free, should be more reliable, and manages some amount of pre-filtering and processing, which should further reduce the turnaround time of querying a device.

- Integrate with the host computer over SPI: While I was able to make serial work by busy loop polling on a dedicated CPU, the SPI bus has an even higher possible bitrate and even if its kernel driver is just as problematic, it can still be polled in the same way.

- Operate 4 separate busses:  This will be done by having probably 2 STM32’s on the host computer daughterboard, each managing two busses.  This way each leg will have its own CAN bus.
