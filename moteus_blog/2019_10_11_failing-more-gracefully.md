# Failing more gracefully - mjbots blog

URL: https://blog.mjbots.com/2019/10/11/failing-more-gracefully/

My outdoor filming for the project update video was cut short when the machine cut power to the motors, fell down, and one of the legs snapped off.  Fortunately, I already had plenty of footage when that happened, so it didn’t really impact the video.

[img: Robot down]

      

Robot down
    

[img: Nice infill shot]

      

Nice infill shot
    

First, this demonstrates the not too surprising fact that this particular part of the leg design could use to be improved.  Second, and the topic of this post, is improving what the machine does when the inevitable failure does occur.

## What happened – just the facts ma’am

In this particular instance, I had been running the machine outside continuously outside for an hour or so on a relatively warm day.  This iteration of the servos has basically no heatsinking whatsoever on the servo control board.  With the gearboxes, it isn’t necessary for short duration or low power testing.  However in this case, one of the servos eventually reached its temperature limit and entered a fault state.

As implemented now, any individual servo that hits a hard fault immediately cuts all power.  Also, at that time, the overall gait control, upon sensing any servo failure, immediately cuts power to all the other servos too.  As you can imagine, when a machine that weights 10kg loses all power to all joints with a few milliseconds, it falls down pretty hard.

## Areas to improve

There are of course many areas of improvement which this event demonstrates.  One, the servos need better thermal properties.  This was known, and I hope to address in the second revision where I can heatsink the controller properly.  Second, the leg should be strong enough to handle falling down.  And third, it would be nice, if it could, if the machine would do something more graceful when continuing on in a controlled manner isn’t possible.

I tackled that third step right now in two phases.  First, I set up an optional communications watchdog in the servo.  If control commands aren’t received in a timely manner, the servo enters a new mode where it merely commands a zero velocity with no position control at all.  This means that if the control software segfaults, or the primary computer goes out to lunch, the machine will gently lower to the ground rather than dropping like a rock.

Second, I modified the control software’s reaction to an individual servo fault.  Now, it commands this new zero-velocity state of all unfaulted servos so as to minimize the amount of damage done to the overall machine.  If only one or two servos have faulted, this will still result in a relatively gradual let down.

Here’s a quick video demonstrating the two failure types:

      
    

		
		
			

	
	
		
- 
			dynamics
		
		
- 
			moteus
		
		
- 
			quada0
		
		
- 
			realtime
