# Auxiliary encoders for moteus - mjbots blog

URL: https://blog.mjbots.com/2021/04/26/auxiliary-encoders-for-moteus/

The moteus controller uses an absolute magnetic encoder to sense the position of the rotor in order to conduct field oriented control of the motor. In many applications, this sensing is also sufficient to measure the output as well, particularly in direct drive applications. However, if the controller is driving the output through a gear reduction, multiple turns of the input are necessary to make one turn on the output. At power on, this results in an ambiguity, where the controller doesn’t know where the output is.

There are a couple of possible solutions to this, one is to do like the quad A1 does, and have a “known turn on position”. Another would be to have a rigid end stop and use a homing procedure on startup. Yet another would be to have a non-backdrivable mechanism and remember in the host application how many revolutions had been taken.

## Auxiliary encoders

What I’m going to cover here is yet another solution to this problem, an auxiliary encoder. In this approach, a second absolute encoder is used to measure the position at the output directly, thus directly resolving all ambiguity. All of the production moteus controllers have had a, to date unused, connector named ABS which has pins intended for I2C on it. As of revision 2021-04-09, moteus can now use these pins to read the position from an AS5048B absolute magnetic encoder.

After reading, it uses the values for two purposes. First, it reports the measured value out over both the diagnostic and register interface, so that host applications can use it. It also can be optionally used to initialize the value of “unwrapped_position” at startup.

## Setting it up

Getting an auxiliary encoder working is pretty easy. First, you need to wire something up. If you don’t already have hardware, you can use these cables from amazon, and this breakout board from digikey. Here’s a photo of those connected up with with a .1" connector in between.

[img: /2021/04/26/auxiliary-encoders-for-moteus/images/as5048b_breakout.jpg]

The pinout on the moteus board is described in the reference manual:

- Pin 1: 3.3V (closest to ABS label)

- Pin 2: SCL

- Pin 3: SDA

- Pin 4: GND

Then you connect it to the moteus (while off) and install the breakout board facing a magnet. Here, I made a simple 3d printed belt reducer:

[img: /2021/04/26/auxiliary-encoders-for-moteus/images/reducer-standalone.jpg]

Now we can go into tview and configure things. First, we use the config `abs_port.mode` from the reference manual, and set it to the value for an AS5048B (1). Then we will configure an offset using `abs_port.position_offset`, and finally set the position to be set from this encoder on startup with `servo.rezero_from_abs`.

And that’s all that is necessary!

## Using it

At this point, the current value of the encoder can be read at in tview at `'abs_port.position'` or register 0x006 as documented in the reference manual!

Here’s a video showing a bit more detail:

      
    

		
		
			

	
	
		
- 
			actuator
		
		
- 
			as5048b
		
		
- 
			i2c
		
		
- 
			moteus
		
		
- 
			moteus_r45
