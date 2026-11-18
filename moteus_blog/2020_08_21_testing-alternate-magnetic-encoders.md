# Testing alternate magnetic encoders - mjbots blog

URL: https://blog.mjbots.com/2020/08/21/testing-alternate-magnetic-encoders/

The moteus controller, uses an absolute magnetic encoder to sense the position of the rotor and thus be capable of field oriented control FOC of brushless motors. To date, all the iterations of the controller have used the AS5047P encoder from ams. This is relatively common, works fine over SPI and hasn’t caused any problems. While investigating some other issues, I decided to take a stab at trying some alternate encoders. First, I tried the AS5047U, which is the same basic encoder, but incorporates a digital filter. I also tried the MA732, from Monolithic Power, which uses a different operating principle and also includes a digital filter. The plus side of the MA732 is that it reports full 16 bit values, even if not all of them provide a lot of value.

Testing the MA732 wasn’t as easy as the AS5047U, since it doesn’t have the same pinout as the AS5047 family. Fortunately, the footprint is smaller, so I was able to make my first “castellated” adapter board:

[img: /2020/08/21/testing-alternate-magnetic-encoders/images/dsc_0914.jpg]

It is from oshpark – I had to hand dremel the edges to get all the vias exposed. Since it was a one-off, I didn’t worry too much about getting perfect alignment when soldering it up. Here’s a microscope image of the side with the best alignment.

[img: /2020/08/21/testing-alternate-magnetic-encoders/images/dsc_0919.jpg]

And finally with the MA732 installed:

[img: /2020/08/21/testing-alternate-magnetic-encoders/images/dsc_0922.jpg]

It worked the first time, although I had to tweak the code slightly to use the alternate SPI protocol of the MA732. I’ll write up the results in a later post.

		
		
			

	
	
		
- 
			as5047
		
		
- 
			encoder
		
		
- 
			ma732
		
		
- 
			moteus
		
		
- 
			moteus_r43
