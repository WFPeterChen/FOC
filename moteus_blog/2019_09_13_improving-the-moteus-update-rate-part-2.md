# Improving the moteus update rate, part 2 - mjbots blog

URL: https://blog.mjbots.com/2019/09/13/improving-the-moteus-update-rate-part-2/

Back in part 1, I looked at the driving factors that limited the update rate of the full quadruped.  Now in part 2, I’ll cover the first half of the solution.

## Background

To begin with, there were two major paths that I could take based around the network topology.  In one path, I would remove the active bridging capability from the junction board, and rely on the Raspberry Pi to drive all the servos directly, and in the other the active bridge would stick around.  There were a number of key disadvantages to both approaches:

Passive bridge: In this model, the raspberry pi has no choice but to rapidly turn around 12 separate queries and responses.  There is no hope for parallelization.

Active bridge: Here, the junction board’s STM32 can offload the multiple queries.  However, there are two big downsides.  The first are that the data must flow across 2 separate 485 busses.  The second, and possibly more problematic, is that it only works out better if the junction board can stream a large amount of data consecutively to the raspberry pi.  In my previous experiments, I had run into what I believe was a kernel bug that killed the serial port until a power cycle upon receive overruns.  Debugging that could easily be a large project.  Implementing the active bridge would also be a lot of work, as I don’t currently have a protocol client that runs on the STM32.

My initial back of the envelope calculations, surprisingly, indicated that both approaches could potentially get up to 250 or 300Hz with sufficient margin to still do other things.  I had expected that parallelization would win the day, but it turned out that duplicating the data across both busses had the potential to completely negate that advantage.

## Solution steps (first half)

### 1. RT_PREEMPT

The first thing I did was to switch to using a RT_PREEMPT enabled kernel with the governors set to performance.  This by itself reduced the Raspberry Pi’s reply to query turnaround from 200us to 100us.  I wanted to at this time also upgrade to the 4.19 kernel, as I hoped that it would have some fixes for high serial rates.  However, it doesn’t look like anyone has a RT enabled 4.19 kernel that supports USB and ethernet, both of which are moderately useful on a board with not many other interfaces.

### 2. “Passive” bridging

In lieu of spinning a new board right away, I instead modified the firmware of the junction board to remove almost all functionality and instead just busy loop shoving individual bytes around between the interfaces.  It is fast enough to just barely be able to achieve this at 3Mbit if interrupts are off.  The downside is that the IMU won’t be usable.  To fix that, I’ll have to spin a new board that just passively wires all the busses together and dangles the STM32 off the bus like any other node and hope that the star topology won’t matter over such short distances.

Doing this removed the double transmission penalty, as well as the additional 90us latency in the junction board on all transfers.  This, combined with RT_PREEMPT, got the overall cycle time down to about 6727us, or ~150Hz.

### 3. Controller turnaround time

Next, I started in on the moteus firmware, in order to improve the turnaround time between when a query is sent, and when the corresponding reply is generated.  This resulted in many optimizations throughout the code.  The first set were made to the primary control ISR, which runs at 30kHz currently.  Tiny improvements here make everything else run much faster.

- Constructors: The ARM gcc toolsuite, regardless of the optimization level, seems to implement a constructor of an all zero structure as a call to memset.  This is true even if the structure is a small number of bytes.  There were several places in the firmware where a structure was zeroed out by calling its default constructor.  Switching that to a “clear” method which manually assigned all the fields drastically reduced the time spent there.

- Appropriate types: When calculating a smoothed velocity, the filter was using an int64_t as an intermediate variable, which is not very efficient on an ARM.  Nothing more than an int32_t was actually needed.

- fmod: I was wrapping between zero and two pi by using fmod.  The cases where wrapping occurs were never more than one or two phases off, so just dividing and truncating was much faster with no appreciable loss of precision.

- Pre-computing some variables: Some of the calculations done in the ISR were repeated unnecessarily, so now they are on re-computed upon a configuration change.

- Make debug output optional: The moteus board has a high rate RS422 debug output, which is only rarely used.  I added a configuration knob to turn it off when not in use.

- SPI overhead: I was using the ST HAL API to access the position sensor over SPI.  I still do that for setup, but now just twiddle the raw registers to do the actual transfer, which saves a little bit of overhead in the HAL.

- SPI frequency: The AS5047P has a nominal SPI frequency limit of 10MHz.  I asked the HAL for 10MHz, but the closest it could actually do was 6MHz.  I decided to brave some flakiness and upped it to 12MHz to shave another microsecond off.

After these changes, the ISR uses around 7-8us when stopped, and around 13us per iteration when in position control mode, or around 40% of the CPU budget.

Next I made more optimizations to the software which runs in the main loop:

- StaticFunction: I was using a home-grown solution to a bounded size type-erased function callback that wasn’t particularly fast.  I switched it out for the SG14 inplace_function from github, modified to allow shrinking to a smaller type.  That sped up everything in the main loop by a fair amount.

- Protocol parsing: A number of steps in the protocol parsing and emitting eventually delegated to a call to “memcpy” into a particular type.  I broke some abstractions so that all that parsing and emitting eventually compiles down to just loads and stores.

- Reply encoding: The RS485 register protocol allows clients to query multiple consecutive registers.  As a shortcut, the server was responding to those with a series of single register replies.  I fixed that by implementing sending multiple replies at once to shave off a few bytes and some processing.

I experimented with using the STM32F446’s built in CRC unit, however it only implements a fixed CRC-32 variant.  So updating that would have required reflashing all my bootloaders.  Using that is probably best taken by updating to an STM32F7 or STM32G4 which have more configurable CRC units.  I also tried hand assembling an optimized version of the CCITT16 checksum I was using, but was only able to achieve the same 8 cycles per byte that boost already had.

With all of these changes, the average turnaround time for a single servo was down from 140us to in the 70-80us range for a full control update.

## Next steps

Coming up, in part 3, I’ll cover the remainder of the steps I took to improve the overall update rate.
