# OrangeLine
OrangeLine VCV Plugin

## Fence

<p align="center"><img src="res/FenceWork.svg"></p>

### Short Description

The main function of Fence is to take a CV IN and send it to CV OUT, limited to certain range if possible.

This logically works by first substracting a defined STEP value from CV IN as long as the CV is larger than the upper limit of the range. Second, the STEP value is added to the CV as long it is smaller than the lower limit of the range.
Note that this may in a result in a CV OUT larger than the upper limit.

In quantized mode (green light), CV IN and STEP are quantized to semi tone voltages so CV OUT is quantized too. In quantized mode, STEP is working in a special way. Fence will first use a fixed step of 1V (1 octave). If the resulting cv is higher than the upper range limit, it uses STEP as alternative note.

In shaper mode (red light) Fence processes CV IN as an audio signal [-5:5]V, applying the described algorythm to the audio signal. CV OUT in SHPR mode is centered and scaled to [-5:5]V.

When the mode light is off, Fence is working in raw mode processing signals between -10 and 10 V. All voltages are used as they are without any conversion. Still output is clamped to [-10:10]V.

Fence will send a TRG OUT whenever cv out is changing.

If TRG IN is connected, Fence will not work continously but more like a S&H when a trigger in is detected.

## Swing

<p align="center"><img src="res/SwingImage.svg"></p>

### Short Description

Swing is a micro timing sequencer generating a micro timed clock to implement custom micro timing the easy way.

DIV controls how many beats are created for one clock in trg. 
LEN controls how many of the 16 timing settings will be applied before looping back to the first one.
Default setup is DIV 4 and LEN 16 which is fine for 16 16th of a 4/4 bar. For a simple swing LEN 2 is sufficient. You only have to dial in the first 2 knobs in this case.
AMT controls how much the timing knobs will influence the timing. AMT = 0% switches micro timing off.
The 16 timing knobs allow to setup the timing for each beat.
RST, BPM and CLK have to be connected to your clock. Swing will not work without BPM and Clock connected !
Start with setting up Clocked and connect the CLK and BPM to swing. With DIV 4 Swing will output a micro timed 16th clk.
tCLK (timed clock) will output the micro timed clock.
eCLK (early clock) will output a trg the eraliest time tCLK can appear. Use eCLK to run sequencers delivering values used when tCLK hits.
I recommend to always do a S&H of all values needed for a step when tCLK arrives to avoid note tails.
PHS and CMP are output to allow for further timing like humanization (a module using this will come!).

## Mother

<p align="center"><img src="res/MotherImage.svg"></p>

### Short Description

Mother is a probabilistic quantizer.
You can set up the notes available for the mother and its daughter scales with the buttons at the left side of Mother.
The root (lowest note) has to be always on for a mother scale, so this button is disabled.
Note on/off only can be changed for the mother scale when CHLD is set to 0. If CHLD is not 0, note on/off buttons are disabled.
Mother will detect known scales automatically and shows its name in the header.
You can setup up to 12 mother scales and select them using the SCL CV input or knob.
SCL, CHLD and ROOT inputs use quantized semiton values, so sending a C will select mother scale 1.
For each mother scale, you can set up probability weights for each note of each daughter scale.
CHLD 0 selects the mother scale itsself. CHLD N shifts note on/off and weights by position.
ROOT selects the root note of the mother scale.
Weights for daughter scales (CHLD > 0) have two special values. 50% tells Mother to use the weight of the mother scale at the same position. So if mother scale with root C has weight 100% on C (position 1 in mother scale), the daughter scale will use a weight of 100% for D (position 1 in daughter scale) if the weight of position 1 of the daughter scale is set to 50%. Blue lights indicate that weight for this note is 50% and the mother weight is used. A weight of 100% has a special meaning too (Grab). When processing of FATE hits a note with weight 100% it uses this note without further probability processing. The note light will turn red if Grab is active for that note (weight 100%).
FATE allows mother to choose notes by probability. If SPAN is 0 no probability processing is done and Mother works like a normal quantizer. With SPAN > 0 Mother will use neighbour notes and its weight to select one of them. The RND input allows to feed a seed to the internal random generator to get repeatable patterns. The internal random generator is reset to the seed at every process cycle. So you should change RND input for each trigger you send to Mother.
If TRG is connected Mother will only process CV in when a trigger is received. If TRG is not connected Mother will process on each (quantized) change of CV in. On change of CV out a gate is produced on the GATE output.
POW will output the weight of the selected note.

Example setup:

Initialize Mother. Setup a mother major scale. Set weights for c,e,g to 100% and the other weights to 0. Leave the child scales weight at 50%. Set SPAN to max and trigger Mother using a clock. Mother will doodle on a C major chord. No sending a D to the CHLD input will make Mother play the notes of a D minor chord.


## Phrase

<p align="center"><img src="res/PhraseWork.svg"></p>

### Short Description

Phrase is a Phrase Sequencer but has for itself no sequencing capabilities. Instead, Phrase uses one external sequencer (master) to provide informations on the sequence of phrases to feed another sequencer (slave). The slave sequencer has to have to ability to store multiple patterns (slave patterns) and to switch between them by providing a pattern cv input. Your patches will use the outputs of the slave sequencers channels.

The master sequencer provides three cv values for each phrase. The starting patterns cv value, the length of the pattern and the duration the phrase should play. If the duration is longer then the length, the pattern is repeated.

The LENgth knob tells Phrase the native number of steps the slave sequencer provides. If the length ofthe pattern given by the master sequencer is greater than LEN, the slave sequencer is advanced to the next pattern by adding the value of the INCrement knob to the current slave pattern cv.

After a phrase is played (duration past) the master sequencer is clocked to provide the information on the next phrase to play. Since the master sequencer may need a number of samples to provide the cv outputs, the processing of the master sequencer inputs is delayed by the number of samples provided by the DLY knob.

If the pattern length provided by the master is 0V, the length defauts to the value of the LEN knob. If the phrase duration provided by the master sequencer is 0V it defaults to the pattern length.

### The Panel

#### Top Row

RST: Reset trigger cv input from your patch (usually clock)

CLK: Clock trigger cv input

DIV: Knob to select the clock division Phrase should run with

PTN: Pattern cv input to select master sequencer patterns (allows for nesting of Phrases)

#### Left Column

RST: Master reset trigger output

CLK: Clock trigger out (triggered when nexi phrase infomations are needed)

PTN: Master sequencer pattern cv output, copied from top row PTN input when master CLK out is sent.

DLY: Number of samples to wait after master CLK out is sent before processig the master input cv

DLEN: Default pattern length input used if LEN below is not connceted or 0V 

DUR: Input for phrase duration (cv = #clockticks/100) 

PTN: Knob for master pattern CV or offset if connecte and master pattern CV input for slave sequencer start pattern cv

LEN: Input for the pattern length (cv = #clockticks/100), if not connected or 0V, defaults to DLEN, if connected or right column slave LEN knob 

DUR: Input for phrase duration (cv = #clockticks/100)

#### Right Column

LEN: Knob to set number of stes of the slave sequencer used

ELEN: Effective default pattern length from copied from left columns DLEN input if connected, or (LEN knob above * top rows DIV knob) / 100 otherwise.

INC: Knob to set the voltage increment used to advance the slave seuqnecers next pattern

DLY: #Samples the slave clock and reset outputs below shall be delayed

SPH: Trigger output signaling the start of a new phrase

SPA: Trigger output signalling the start of the pattern

RST: Reset trigger output to connect to the slave sequencers reset input

CLK: Clock trigger output to connect to the slave sequencers reset input

PTN: Pattern cv output to select the slave sequencers pattern

### Right Click Menu

The right click menu offers the usual option including the selection of three different panel style.

Since the trowasoft sequencer are not compatible on pattern cv in itsself, there is a trowa pattern offset fix selectable which add a slight offset to the pattern cv provided by the master sequencer.

Have fun

## Dejavu

<p align="center"><img src="res/Dejavu_Final_Screenshot.png"></p>

### Short Description

Dajavu is a polyphonic source for random gates/triggers as well as random cv.
It provides polyphonic output for up to 16 channels of trigger/gates and cv each. 
The number of channels provided can be set in the right clock menu of the module. 

Its unique property is the ability to repeat the generated random sequences in up to 4 levels of nested phrases. Why ? Random generative patterns are much more accepted when there is a good amount of recognition of somthing heard before.

Repitition justifies!

Example:

A simple Random generator generates values: ASDIGUEOIRJHSVMCXNSIEKZTAGSFDHD.... which is a chaotic sequence with no structure.
Dejavu allows to generate : ASDI GUEO ASDI GUEO IRJH SVMC IRJH SVMC ... and more up to 4 nesting levels of repition.

If you are familiar with Frozen Wastelands 'Seeds of Change' and 'The Gardener', you will be nearly all the way to understand Dejavu.
On the bottom end, Dejavu is logically a chain of 4 'Gardeners' with its respective 'Seeds Of Change' seed source plus all the cabeling and 
logical processing to sample and hold cv and trigger outputs. Thus Dejavu can free up a whole row some patches and frees a lot of CPU so.

### The Panel

#### Top Left Section

RST: Reset trigger cv input from your patch (usually clock)

CLK: Clock trigger cv input

DIV Input: CV input for DIV, if connected DIV knob will be ignored, input CV is scaled by factor 10 (6.4V represents the max DIV of 64) 

DIV Knob: Selects the clock division Dejavu should run with, if DIV input is not connected

SEED CV Input: Global Seed to initialize Dejavu an Reset. Scales by factor 1000 (9.999V represent the max seed of 9999) The resultig seed is clamped to [0..9999]

SEED Knob: Defines the starting seed Dejavu should reset to on RST trigger input

#### Left Display

The left display shows the current active seed, the random generator used to initilize the seeds of all generators needed for the outputs,
was last initialized with. It changes whenever the lowest active REP row reaches its end of duration. The random generator is reset to the this same seed whenever the lowest active REP row reaches its end of length.

If a knob is turned, the display switches to give a feedback of the current value of the moving knob for some time.

#### Middle Left Section

This section contains 4 rows to define the (nested) repitition of the generated random value stream. 
A row is active if it is switched on with the respective LED in the middle of the row and either LEN or DUR of this row is != 1. 
Each row holds a random generator to provide a seed for the active random generator row above or 
the active random generator for output seed initialization. 
On each end of duration (DUR length) the row initializes its random generator from a seed of an active row above or 
the global random generator if it is the highest active row. In this case, the random generator will start to create a new sequence of randoms. 
On each end of lenth (LEN length) the random generator is reset to the seed it was initialized last.
This will start to deliver the same sequence of seed to its lower row or the out processing random generator as after its last LEN end.

LEN Knob: Define the length of the random sequence to repeat.

LED Button: Switch this row on or off, the LED is dimmed if a row is on but incative because both LEN and DUR are set to 1

DUR Knob: Define the total length of the phrase. It is typically a multiple of LEN but other values are working as well. If lower than LEN, the sequence will never reach its end of length because it is terminated before by the lower DUR. If DUR is greater that LEN, Dejavu will just generate the same sequence as after the last len/dur end again.

The rows 2 to 4 will interpret their LEN und DUR values as multiples of the DUR length of the active REP row above. Having a DUR of 64 set for the first row and we set a LEN of 2 and a DUR of 4 in the second row, this will result in an effective LEN of 64 x 2 = 128 and an effective DUR of 64 x 4 = 256 for the second row. The effective value of DUR will again provide the unit for the rows below.

#### REP Input/Output Section

REP input: Polyphonic input to control the DUR and LEN of the 4 rows in the repeater section above

Channels: 

0: DUR Value for repeater row 1

1: LEN Value for repeater row 1

2/3, 4/5, 6/7 as above DUR/LEN for repeater rows 2,3 and 4

Channels 0 to 7 are interpreted in the same way as the knobs and scaled by a factor of 100 (0.64V represents a length of 64 clockticks or multiples).

Channels 8 to 15 follow the same layout as channels 1 to 7 but interpreted as raw length values and are 
scaled by a factor of 10.000 (10V represents a length of 10.000 clockticks.

All channels have be >= 1V and are ignored otherwise but allow any other length as long the effective length does not exceed max float.

TRG input: Polyphonic trigger inputs to force an end of duration event for rows 1 (channel 0) to row 4 (channel 3)

REP output: Polyphonic cv outputs for the effective DUR/LEN of each row. Updated whenever the respective end of duration or end of length trigger is triggert.

Channel 0 = DUR 1, Channel 1 = LEN 1, Channel 2 = DUR 2, ... . Values are scaled by a factor 10.000. so 10V represents an effective length of 100.000 clockticks.

TRG output: Polyphonic trigger output for end of duration (channel 0,2,4,6) and length (channel 1,3,5,7) for the 4 repeater rows.

#### Right Display

A visualisation of Dejavus repeat state. All active repeater rows ware represented by a circle of dots and an assotiated clock hand.
Whenever the hand reaches a dot, an end of length event has occured. Whenever the hand reaches 12:00 an end of duraction event has occured for
this row. Row 1 of repeater is the outer ring, row 4 the inner ring. inactive rows are not displayed

#### Heat Section

HEAT Knob: The HEAT knob defines the global probability of output triggers to be fired on each clock tick.

Left CV input: Polyphonic input for probabilities for the GATE output channels (bottom right output).

Right CV input: Attenuation input for the HEAT Knob (NOT the CV input!). defined how the HEAT knob will change the value given by the LEFT CV input.

#### Output Section

OFS Knob: Offset of the CV values to be generated

OFS CV input: Polyphonic input to set OFS per cv output channel

OFS Attenuation Trimpod: Attenuator for the CV input

SCL Knob: Scale of the CV output to be generated

SCL CV input: Polyphonic input to set SCL per cv output channel

SCL Attenuation Trimpod: Attenuator for the CV input

S&H Button. Switches whether a cv change on output should occur on evry clock tick (S&H off) or only when a trigger output on that channel occured.

S&H Input: Polyphonic input for S&H per channel. If S&H input channel n is > 5V. cv output channel n is set to s&h.

GATE Button: Defines whether the trigger output generates triggers (off) or gates (on).

GATE INPUT: Polyphonic GATE input. Defines GATE/TRG for each polychannel

CV Output: (Upper bottom right) Polyphonic CV output

GATE_Output: (Bottom right) Polyphonic Trigger/Gate output.
