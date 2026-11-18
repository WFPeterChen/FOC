# Improved moteus_tool calibration - mjbots blog

URL: https://blog.mjbots.com/2021/11/23/improved-moteus_tool-calibration/

To use the moteus brushless controller with a motor, you first have to calibrate it with `moteus_tool` (for history, see “Encoder autocalibration” and “Auto-tuning current control loops”). This calibration process is primarily used to measure the mapping between electrical phases and the encoder, but as a secondary parameters also measures the winding resistance and Kv of the motor and determines the parameters necessary to set the current control bandwidth.

## Motivation

To date, this process can be used with any motor, but making it work can involve fiddling with a number of inscrutably named command line parameters to moteus_tool. `--cal-power`, `--cal-voltage`, and `--cal-speed` are all there, however they don’t really do what you think based on their name, but it is necessary to adjust them to make many motors work.

I went to address this and make the calibration process more robust across a range of motor types, from small high speed motors, to large low Kv ones, high winding resistance, and low winding resistance. All of these can now be handled with mostly no additional options required.

## The new process

In the new world, with no additional options, `moteus_tool` gradually eases up the voltage applied when measuring resistance and when measuring speed, so that it can be safely used with many motors with no additional tweaks. The power expended in the windings is limited to roughly 5W, which is about the only remaining necessary configurable parameter. You’d only need to change that for a very small motor for which 5W would unnecessarily heat it up.

The existing parameters all get new names which more precisely describe what they do. You rarely need to specify any of them anymore, but you can if for some reason you don’t want to trust the auto-detection mechanism.

  
      
          New
          Old
          What it does?
      
  
  
      
          `--cal-ll-encoder-voltage`
          `--cal-power`
          The voltage used when spinning the motor open loop for encoder calibration.
      
      
          `--cal-ll-encoder-speed`
          `--cal-speed`
          Speed in electrical revolutions per second when spinning the motor open loop for encoder calibration.
      
      
          `--cal-ll-resistance-voltage`
          `--cal-voltage`
          The voltage used when measuring the winding resistance.
      
      
          `--cal-ll-kv-voltage`
          `--cal-kv-voltage`
          The voltage used when measuring the Kv parameter.
      
      
          `--cal-motor-power`
          
          This new parameter controls how much power is dissipated in the windings when performing the encoder and inductance calibration with a default of 5W.
      
      
          `--cal-motor-speed`
          
          This new parameter controls how fast the motor is spun when measuring Kv with a default of 6Hz.
      
  

The old parameter names will continue to work for now and just emit a deprecation warning. Even so, the new parameters should now only very rarely be needed, as every motor I have tested works fine without specifying anything at all.

## How to get it?

Just update your `moteus` python package to at least 0.3.30 and you’ll get all the new features in `moteus_tool`!

```
`pip3 install --upgrade moteus
```
