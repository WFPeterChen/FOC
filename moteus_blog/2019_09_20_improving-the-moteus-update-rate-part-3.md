# Improving the moteus update rate, part 3 - mjbots blog

URL: https://blog.mjbots.com/2019/09/20/improving-the-moteus-update-rate-part-3/

Back in part 1 and part 2, I looked at problems that limited the rate at which the host computer could command the full quadruped and some of the solutions.  Now, in part 3, I’ll cover more of the solution.

## More solution steps

Previously, I switched to using PREEMPT_RT, switched bridging strategies, and optimized the turnaround of the individual servo.  Now, I’ll move on to optimizing the host software.

### 4. Host C++ software micro-optimizations

The primary contributor in the host software to the overall update rate is the time it takes to turn around from receiving a reply from one servo, to sending the next command.  I first did some easy micro-optimizations which came up in profiling.

- error_code: My implementation of error_code with strings attached was doing lots of string manipulation even when no one asked for it.  Fixing that saved a fair amount of time throughout.

- Memory allocation: There were a few sites in the code that generated packets where a persistent buffer could be used each time, instead of having to allocate a fresh one.

### 5. boost::asio

The host software was using boost::asio to interact with the serial port.  It is high performance for what it does, allowing multiple external operations to happen in the same thread, but necessarily relies on an epoll loop and non-blocking write operations.  These aren’t particularly fast in the linux kernel, and the best turnaround time on the rpi I could achieve with asio was around 80us.

I implemented a standalone proof of concept which just uses a single thread to read and write to the serial port in a blocking manner.  Doing that allowed me to get the turnaround down to around 30us.

### 6. Protocol design

The register protocol that is used for high rate control had one opcode for setting a single register, and another for setting multiple consecutive ones.  The multiple consecutive one requires an additional byte to identify how many registers are set.  The same thing is true for queries.

I shaved a byte off of the common case of both by allowing writes and reads of up to 3 registers to encode their length in the primary opcode.

With that change, the full query packet to each servo was 23 bytes, and the full reply packet was 28 bytes.

### 7. Linux serial driver latencies

With the blocking thread approach from step #5 and that thread set to real-time priority, the average turnaround was indeed 30us.  However, when run in the full control software (which does other processing as well), occasionally latencies would be in the several millisecond range.  Also, since the PL011 on the raspberry pi only has a 16 byte FIFO, reading any frames larger than that was unreliable, as the kernel didn’t always get around to servicing things fast enough.  Even with sub-16 byte frames, and a blocking reader, the kernel would still delay reads by a millisecond or so sometimes.  I believe this is because the PL011 only provides interrupt notification on even 4 byte boundaries of its FIFO, and otherwise relies on the kernel to poll it.

Well, I can poll too, so I fixed this by just disabling the kernel’s serial driver, opening up “/dev/mem” and polling the IO memory of the controller manually in a busy loop from a RT thread.  This let me get turnarounds down to 4us, and also let me receive packets of arbitrary length without loss.  See https://github.com/mjbots/mjmech/blob/master/mech/rpi3_raw_uart.h and https://github.com/mjbots/mjmech/blob/master/mech/rpi3_raw_uart.cc

### 8. Linux scheduling latencies

With the above changes, the serial port was doing fine, but linux still had problems scheduling the primary process to run with less than 1ms precision, even when it was RT priority.  To solve that, and make the serial thread a bit better performing too, I used the “isolcpus” feature of linux to exclude 2 of the 4 raspberry pi processors from normal scheduling.  Then the main thread of the application got processor 3, and the serial thread got processor 4.  With those changes, the time required to poll the full set of 12 servos is rock solid.  Here’s a plot showing the cycle time required to poll a full set of telemetry data  (but not command them, which adds a bit more time per query but doesn’t affect the variability).

[img: 20190919-improved-latency-jitter.png]

## Next steps

And in the next and final post of this series, I’ll demonstrate the final result, showing all of these changes are integrated into the primary control software.

		
		
			

	
	
		
- 
			linux
		
		
- 
			moteus
		
		
- 
			realtime
		
		
- 
			rpi
