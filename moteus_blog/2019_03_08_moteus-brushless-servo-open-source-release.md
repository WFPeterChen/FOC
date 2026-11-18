# moteus brushless servo open source release - mjbots blog

URL: https://blog.mjbots.com/2019/03/08/moteus-brushless-servo-open-source-release/

moteus is an open source brushless servo actuator designed for use in highly dynamic robots.  It consists of PCB designs, software, and mechanical designs necessary to construct powerful brushless servos, and link them together into legged robots.  Today I’ve published the full source and designs for all of this work on github under an Apache 2.0 License - https://github.com/mjbots/moteus

[img: moteus r3 controller installed on leg]

      

moteus r3 controller installed on leg
    

These are the software and designs I have been developing in order to replace the actuators on Super Mega Microbot (which will probably get a new name shortly as well).  It isn’t done, but at least the controller is working well enough now that I have a pre-production verification run of ~30 controllers in flight.  Even still, I expect that further evolution, both on the controller board and in the mechanical systems is inevitable.

The software is largely C++ and python, compiled with bazel and designed to run on an STM32F4.  The PCBs are all designed with Eagle, and the mechanical systems are all designed with FreeCAD.

I definitely want to acknowledge Ben Katz, who was a big inspiration for this effort.  While this isn’t a direct derivative of any of his work, I really appreciate the open source releases he did make.

CAVEATS: As with any actual hardware project, especially one that can apply large amounts of power to small brushless motors, actually using these designs risks burning down your house, injuring your body, and all sorts of other bad things.  If you choose to try these out, you are on your own!

		
		
			

	
	
		
- 
			actuator
		
		
- 
			moteus
		
		
- 
			smmb
