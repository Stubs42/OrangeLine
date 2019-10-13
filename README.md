# OrangeLine
OranceLine VCV Plugin

New modules will first show up marked as [Beta].
Beta modules are published for early adopters and should not be considered stable.
They may even disappear again if the concept behind the module doesn't work es expected.
Functionality can change and no backward compatibility is promised between beta versions of a module.
So problems with upgrading between beta versions may arise.
After collecting feedback from the community and doing the polishing of GUI and functionalityi, those modules will hopefully enter a stable state.

When a module has reached its stable state, the [Beta] mark will disappear.
Modules not marked as beta will maintained in a backward compatible way if possible.

## Fence [Beta]

<p align="center"><img src="res/FenceWork.svg"></p>

### Short Description

The main function of Fence is to take a cv input and send it to a cv output, limited to certain range if possible.

This logically works by first substracting a defined step value from cv in as long as the cv is larger than then the upper limit of the range. Second, the step value is added to the cv as long it is smaller than the lower limit of the range.
Note that hhis may in a result in a cv out larger than the upper limit.

In quantized mode (QNT. button), cv and step are quantized to semi tone voltages so cv out is quantized too. In quantized mode, step is working in a special way. Fence will first use a step of 1V (1 octave) and if the resulting cv is higher than the upper range limit, it uses step to find a note which can replace the cv in note, fitting better (in)to the range. A step setting of 0 in QNT. mode is fine most of the time.

If QNT and SHPR are off, Fence is working in raw mode processing signals between -10 and 10 V. All voltages are used as they are without any conversion. Still output is clamped to [-10:10]V.

In SHPR. mode Fence processes the cv in as an audio signal [-5:5] V applying the described algorythm to the audio signal. The cv out in SHPR. mode is scale and centered around 0 V, matching the definied range.

Fence will send a trigger out whenever cv out is changing.

When a trigger in cv is connected, Fence will not work continously but more like a S&H when a trigger in is detected.
