# OrangeLine
OrangeLine VCV Plugin

New modules will first show up marked as [Beta].
Beta modules are published for early adopters and should not be considered stable.
They may even disappear again if the concept behind the module doesn't work es expected.
Functionality can change and no backward compatibility is promised between beta versions of a module.
So problems with upgrading between beta versions may arise.
After collecting feedback from the community and doing the polishing of GUI and functionality, those modules will hopefully enter a stable state.

When a module has reached its stable state, the [Beta] mark will disappear.
Modules not marked as beta will maintained in a backward compatible way if possible.

## Fence [Beta]

<p align="center"><img src="res/FenceWork.svg"></p>

### Short Description

The main function of Fence is to take a CV IN and send it to CV OUT, limited to certain range if possible.

This logically works by first substracting a defined STEP value from CV IN as long as the CV is larger than the upper limit of the range. Second, the STEP value is added to the CV as long it is smaller than the lower limit of the range.
Note that this may in a result in a CV OUT larger than the upper limit.

In quantized (QNT) mode , CV IN and STEP are quantized to semi tone voltages so CV OUT is quantized too. In QNT mode, step is working in a special way. Fence will first use a fixed step of 1V (1 octave). If the resulting cv is higher than the upper range limit, it uses STEP to determine an alternative note (alt = CV - 1 + STEP). If alt is in in range or high < alt < CV, alt will be sent to CV OUT. This way you can allow Fence for example to replace a note by its 5th (STEP = 7 ST) or minor third (STEP = 3 ST) if those would fit in range. A STEP of 0 disables alternative notes and is fine in most use cases.

In shaper (SHPR) mode Fence processes CV IN as an audio signal [-5:5]V, applying the described algorythm to the audio signal. CV OUT in SHPR mode is centered and scaled to [-5:5]V.

If QNT and SHPR are off, Fence is working in raw mode processing signals between -10 and 10 V. All voltages are used as they are without any conversion. Still output is clamped to [-10:10]V.

Fence will send a TRG OUT whenever cv out is changing.

If TRG IN is connected, Fence will not work continously but more like a S&H when a trigger in is detected.
