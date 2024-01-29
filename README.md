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

SEED Knob: Defines the starting seed Dejavu should reset to on RST und TRG trigger input

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

REP input: Polyphonic input to control the LEN and DUR of the 4 rows in the repeater section above

Channels: 

0: LEN Value for repeater row 1

1: DUR Value for repeater row 1

2/3, 4/5, 6/7 as above LEN/DUR for repeater rows 2,3 and 4

REP input CVs are interpreted in the same way as the knobs and scaled by a factor of 100 (0.64V represents a length of 64 clockticks or multiples).

All channels with cv < 1V and are ignored but allow any other length as long the effective length does not exceed max float.

TRG input: trigger input to reset all counters to 0 ignorin any offset settings , not the same than reset, because reset sets offsets if configured

REP output: Polyphonic cv outputs for the effective DUR/LEN of each row.

Channel 0 = DUR 1, Channel 1 = LEN 1, Channel 2 = DUR 2, ... . Values are scaled by a factor 10.000. so 10V represents an length of 100.000 clockticks.

TRG output: Polyphonic trigger output for end of length (channel 0,2,4,6) and duration (channel 1,3,5,7) for the 4 repeater rows.

#### Right Display

A visualisation of Dejavus repeat state. All active repeater rows are represented by a circle of dots and an assotiated clock hand.
Inactive rows (switched off or both LEN and DUR of the row is 1) will not get displayed.
The outermost active ring is associated to the uppermost active REP row which feeds and resets the random generators producing all outputs 
for triggers/gates and CV to its current active seed.
The inner rings are associated with the lower active REP rows, which feed and reset the random generator of the next active outer ring with its current active seed.
Whenever it hits the 12:00 dot an end of duration event has occured and a new seed is fetched from the random generator of the next inner active ring 
or the global never repeating random generator if it is the innermost ring. 
After fetching the next seed from the next inner ring, the next outer ring is reset to the seed just fetched and an it will start a new random sequence.
Whenever the hand reaches another dot, an end of length event has occured and the random generator of the next outer ring is reset to its starting seed and its random sequence starts to reapeat.

#### Heat Section

HEAT Knob: The HEAT knob defines the global probability of output triggers to be fired on each clock tick.

Left CV input: Polyphonic input for probabilities for the GATE output channels (bottom right output).

Right CV input: Attenuation input for the HEAT Knob (NOT the CV input!). defined how the HEAT knob will change the value given by the LEFT CV input.

The HEAT knob is kind of a macro knob for trigger probabilities of all channels. If no cv input connected it defines the probability from 0 to 100% for all channels the same.
When the polyphonic HEAT offset cv is connected (left of HEAT knob) the HEAT knob is just added to the cv given for each channel. Putting Heat to 0, and connect a Knoly Pobs to the HEAT offset gives you 12 HEAT knobs for each channel.
When the polyphonic HEAT scale cv is connected (right of HEAT knob), it defines the amount of probabilities the HEAT knob is adding to the probabilities for each channel. So if no HEAT offset cv is connected it defines the maximum probability for each channel when the HEAT knob is put to 100%.
Both knobs cv inputs for offset and scale can be combined. If scale is negative, raising the HEAT knob will in fact reduce the probbility for that channel.

Normally if you have a cv input, you have two knobs to dial in the offset and scale for the cv input. With DEJAVU HEAT it's just the other way around. The cv inputs for offset and scale tell DEJAVU for each polyphonic channel the offset and scale for the HEAT knob.

#### Output Section

OFS Knob: Offset of the CV values to be generated

OFS CV input: Polyphonic input to set OFS per cv output channel

OFS Attenuation Trimpod: Attenuator for the CV input

SCL Knob: Scale of the CV output to be generated

SCL CV input: Polyphonic input to set SCL per cv output channel

SCL Attenuation Trimpod: Attenuator for the CV input, positiv (unipolar), negative (bipolar) 

S&H Button. Switches whether a cv change on output should occur on evry clock tick (S&H off), on each step if a trigger or gate is generated (S&H yellow) or only when a trigger output or new gate on that channel occured (s&h red).

S&H Input: Polyphonic input for S&H per channel. If the S&H input channel is < 1V, the cv output for this channel changes on on evry clock tick. If the S&H input channel is >= 1V and < 2V, the cv output for this channel changes for every trigger or gate generated only. If the S&H input channel is >= 2V, the cv output for this channel changes for every trigger or a new gate generated only. 

GATE Button: Defines whether the trigger output generates triggers (off), gates (yello) or contignous gates(red)).

GATE INPUT: Polyphonic GATE input. Defines GATE/TRG for each polychannel. < 1V is trigger mode, >=1 and < 2 is gate mode (new gate on every clock tick) and on >=2 the gate stays on contignously for postitiv following gates.

CV Output: (Upper bottom right) Polyphonic CV output

GATE_Output: (Bottom right) Polyphonic Trigger/Gate output.

## Gator

<p align="center"><img src="res/GatorWork.svg"></p>

### Short Description

Gator is a polyphonic, phase based gate generator. For all of its timing it uses a phase which is a ramp from -10V to 10V of the length of your sequencers clock resolution.
For example running on 1/16 (clock x 4) the phase is assumed to start 1/32 befor the 1/16 clock at -10V and end 1/32 after the 1/16 clock at 10V. The zero crossing is assumed to be in time with your 1/16 clock. To generate a trigger at the output, an input gate is required. So when just patching 10V to the GATE input of Gator and connect the phase from Swing, Gator will produce 1/16 beats in sync with your clock. Typically you run a gate sequencer on the early clock of Swing to select the 1/16 steps to play. THE Len Knob allows to set the Gate length of the gates Gator generates. The LEN input allows to controll the gate length per step using a step sequencer, best runing on the early clock of Swing. Notable is, that a gate LEN can span multiple phases to play longer notes up 100 phase lengths. 

The TIME input allows to offset the threshold value used to detect a gate to generate in the range of -95% to +95% of a half phase length. So shifting notes nearly 1/32 foreard and backward in time is possible. I decided to avoid the 100% because it would be very easy to generate missing notes due to overlapping gates. Needless to say that this CV can also be sequenced to achieve a per step timing of the gates.

The JIT CV input and knob allow to humanize the gates by applying a random offset to the threshold making the gates out of sync by the amount given by those parameters and inputs.

The Rachet section with its knobs and inputs (RAT/DLY) controlls the ratcheting. RAT is the number of additional gates to produce and DLY the time between the gates in phase lengths. The delay itself can not be as large as the Gate LEN but about two phase lengths are possible. The number of ratchets selected by the RAT knob and input can be up to 10.

Important to say that GATE, LEN, TIME, JTR, RAT and DLY inputs are polyphonic with the GATE input defining the number of the output channels Gator will run. Gator is able to control the timing, gate length, jitter as well as the ratcheting for up to 16 voices independently.

As an additional feature, Gator can apply a strum timing to the gates of the polyphonic output. The time and direction (negative values strum up, positiv values strum down) can be controlled by the STR knob and input. This is not polyphonic by nature because it already influences all polyphonic channels. Note that also strumming can span cross phase borders.

Any strumming or ratcheting is aborted if a later gate on the same channel starts a new gate event when crossing its give tie theshold.

### The Panel

PHS input: Clock Phase typical easiest to get from OrangeLie Swing

CMP input: Global threshold for general timing like swinging also best take from Swing 

GATE input: [polyphonic] The phase crossing the timing threshold will generate a gate (or more due to ratcheting) only if the GATE of the channel is high.

LEN knob: Gate length in units of phase length [0(min trg len) up to 100 phases] (ignored if LEN input is connected).

LEN input: [polyphonic] CV input for gate length 0.1 V is one phase. 10V is 100 phases.

JTR knob: humanizastion jitter applied to gate threshold 0%, accurate timeing , 100% very sloppy. (ignored if JTR input is connected).

JTR input: [polyphonic] Jitter input. values < 0 are ignored 10V is 100% sloppyness.

RAT Knob: Number of additional gates to generate [0..10]. (ignored if RAT input is connected).

RAT input: [polyphonic] cv input for the number gates to generate 1V is 1.

DLY Knob: Time between additional gates to generate [0..10]. (ignored if RAT input is connected).

DLY input: [polyphonic] cv input for the time between the gates to generate 1V is 1.

STR knob: Controls strumming. 0 position no strum timing is applied. All bates of the polyphonic output channels are offseted to each other by a strummin offset given by the knob value. If positive the lower channels will get gates first and higher channels latest. If negative its vice versa.

STR input: strum controll CV input

RST input: Reset. clears all ongoing gates, ratchetings and strumming.

Output: [polyphonic] The gate output of Gator

## Resc

<p align="center"><img src="res/RescWork.svg"></p>

### Short Description

Resc is a polyphonic, pitch transposer, transposing a pitch given in a source scale to a target scale/child harmonically by determing the pitch position in the source scale and outputting the pitch at the position of the target scale.

### The Panel

IN input: [polyphinic] Pitch given in the source scale to transpose to the target scale.

SCALE[Cmaj] input: [polyphonic] Source Scale. The channels contains the pitches of the scale. 

TARGET SCALE input: [polyphonic] Target Scale. The channels contains the pitches of the scale.

TARGET CHILD input: [polyphonic] Child of Target Scale to use as effective Target Scale. The channels contains the pitches of the scale.

OUT BASE ROOT: [polyphonic] Transposed pitch based on the Root TARGET SCALE without respecting the TARGET CHILD input. 

OUT BASE CHILD: [polyphonic] Transposed pitch based on the TARGET child SCALE respecting the TARGET CHILD input. 

CHILD SCALE: [polyphonic] Pitches of the TARGET child SCALE (TARGET root Scale rotated by TARGET CHILD.

## Morph

<p align="center"><img src="res/MorphWork.svg"></p>

### Short Description

Morph is a gate and cv looper with turing machine functionality. It is fully polyphonic except of the CLK input. It features two ring buffers of 65 steps max for gates and cvs with a playhead for each polyphonic channel. The channel playhead handels both of buffers. On each clock tick the playhead is advanced one step. Before advancing the playhead, Morph checks for an replacement events for one or both of its channel loop buffers and processes them. A replace event is triggered if the SRC_FORCE gate for the channel, or randomly depending on the setting of the three knobs in the LOCK section. When a replace event occurs for a loop buffer the loop buffers content of the playhead pointing to is replaced. In case of a SRC_FORCE for the channel, gate and cv values from the SRC inputs are copied to the lopp buffers for that channel independent of other settings. In case of a LOCK orginated replace event Morph determines where the new value(s) should come from depending on the setting of the S<>R knob or input. If the values are determined to come from the SRC inputs, the gate and/or cv values are read from SRC inputs. If the values should come from the internal random generator, the RND_GATE knob or input defines the probability a gate is produced and RND_SCL and RND_OFF knobs and inputs are used to generate a random CV from the internal random generator. After processing replace events, the gate/cv values from the playhead position of the buffers are sent to the outputs and the playhead is advanced one step. Buttons and inputs allow for shifting the playhead(s) forward and backward as well as clearing the loop(s) buffer.

### The Panel

#### Top Input Section

CLK input: [monohinic] Clock drivig Morph.

SRC_GATE input: [polyphonic] Source Gate. 

SRC_CV input: [polyphonic] Source Cv. 

SRC_FORCE input: [polyphonic] If high the source gate and cv a forced pushed through to the loop channel buffer overriding other processing for the channel. 

#### LOCK Section

LOCK_GATE knob,
LOCK_GATE input: [polyphonic] [0-10V] Determines whether a replace event for the gate buffer loop for a channel has to occure. LOCK_BOTH values are added.

LOCK_BOTH knob,
LOCK_BOTH input: [polyphonic] [-10-10V] Value to add to LOCK_GATE and LOCK_CV values.

LOCK_CV knob,
LOCK_CV input: [polyphonic] [0-10V] Determines whether a replace event for the Cv buffer loop for a channel has to occure. LOCK_BOTH Values are added.

#### Control Section

S<>R knob,
S<>R input: [polyphonic] [0-10V] 0V MORPH get new data on replace events from src only. 10V from random only. Inbetween mixed.

<<,>> buttons,
<<,>> inputs: [polyphonic] Shift theb loop left (<<) or right (>>)

CLR button,
CLR input: [polyphonic] Clear loop (all gates off, cv to 0V).

#### Lower Input Section

LOOP LEN knob,
LOOP LEN input: [polyphonic] [0-6.4V] * 10 = Number of steps to loop (max 64).

RND GATE knob,
RND GATE input: [polyphonic] [0-10V] probability of generating a gate on a replace event fetching from random.

RND SCL knob,
RNDSCL input: [polyphonic] [-10-10V] scale/10 for random cvs. Negative values bipolar. Positive values unipolar.

RND OFF knob,
RND OFF input: [-10-10V] offset to add to the scaled random cv.

#### Outputs

GATE output: [polyphonic] Gate output

CV output: [polyphonic] CV output

## Morpheus

<p align="center"><img src="res/MorpheusWork.svg"></p>

### Short Description

Morpheus is cv looper with turing machine and memory functionality which is able to mutate a sequence or morph 
between sequences. The heart of MORPHEUS is a 16 channel polyphonic cv sequencer loop with a maximum length of 128 steps.
The length of each channels loop can be different by using the polyphonic LEN input of MORPHEUS. On each clock input MORPHEUS uses 
the LOCK knobs value or the indivual channels of the polyphonic LOCK input to randomly decide whether the currect step should be
mutated or stay as it is. If it decides to mutate the step, the S<>R knob value or the the value of the corresponding
polyphonic S<>R input is used to determine where the replacement value should come from. If the new value should be taken from the 'source' the active memory slot or the external polyphonic SRC input (if the EXT button is switched on) is used the get the new step value(s).
If the new value should be randomized it uses the SCL and OFS knobs and corresponding polyphonic inputs to randomize and scale the
new step value. Negative SCL will result in bipolar, cv centered at OFS, positive SCL will create unipolar cv with OFS as lowest value.
If OFS is set to -10V and the SCL is negative (bipolar) MORPHEUS will create a bipolar, cv centered at the current steps value.
Note that SCL and OFS are used when calculating the new random cv. So the result will be stored as step cv value.
MORPHEUS also can output gates or triggers (settable in right click menu). The GTP knob and its polyphonic input thereby defined the cv threshold the current steps cv has to cross to create a gate. The higher the GTP value the lower the cv has to be, so creating more gates.
Vice versa the lower the GTP value, the higher the cv value has to be to create a gate, so resulting in less gates to produce. So changing
the GTP does not alter the step sequence in any way but sill allows for output between 0% and 100% gates. Since it depends in the current cv value GTP is not directly a propability but since the cv is created randomly it indirectly is.
The HLD button freezes the channels steps, so no change will be applied to the step. Also the step loop of a channel on hold is not
overwritten when loading from a memory slot. Using the HLD input, you can select which channels to be on HLD individually.
The buttons RND, <<, >>, CLR affect ALL channels except channels on HLD. The RND and CLR buttons are temporary, so they can used to affect only the current position of the step loops while pressed. If the corresponding polyphonic inputs are used only the gated channels will be affected by the operation. RND forces the current step to be randomized. CLR will initialize the current step to the channels OFS value. <<,>> shift the step loop left and right respectively.
When a nice loop was created, the polyphonic step loop can be stored to one of the 16 memory slots by selecting the slot with the up and down buttons and pressing of STO. When selecting a memory slot with up and down buttons, the slot display will show the new slots number in red. This indicates that MORPHEUS still is uing the previous active memory slot as a source for its replacement operations. 
When pressing RCL while the slot is displayed red, it will set the displayed slot as the new active slot for step replacement. So if not locked an S<>R < 50% the cannel with morph to the new memory slots stored loop. When pressing RCL while the display is not red, the whole loop of the memory slot is loaded to the step loop immediatelly. This allows for unmorphed pattern switching.
The EXT button switches the source for replacement to the polyphonic SRC input. So you can use MORPHEUS to mutate a sequence coming in
from another sequencer or midi. The REC button and its polyphonic inputs can be used to force MORPHEUS to replace the current steps value(s) be the external CV value(s), thus recording the external source to the step loop. While EXT is on, you can sill save the current loop to a memmory slot but RCL will have no effect on the current step loop.

### The Panel

#### Top Section

LOCK knob: Controls the probability of MORPHEUS will replace a step. 0% forces change of every step, 100% no change of step.
LOCK input: [polyphonic] [LOCK/10, 0..10V] is added to LOCK knobs value for each channel.

S<>R knob: Controls the probability of MORPHEUS will replace from source or randomize. 0% only use source, 100% alwas randomize.
S<>R input: [polyphonic] [S<>R/10, 0..10V] is added to s<>R knobs value for each channel.

#### MEM Section

LEN knob [1-128]: Length of the step loop
LEN input [polyphonic] [LEN/100, 0.01..1.28]: overides the LEN knob per channel. LEN knobs value is used if LEN input for a channel is 0V.

up, down buttons: select memory slot store or recall.
MEM display. Shows selected memory slot. If the acgive memory slot id a different one the number is displayed in red.
MEM input [monophonic] [slot/10, 0.1..1.6]: activate memory slot by cv. If the right click menu option 'Load on Mem CV Change' is set, the mem slot is not only activated but loaded instantly. 

STO button: Store current loop to selected memory slot. sets active slot to be the selected one.
STO input [monophonic]: trigger STO if > 0.V is given.

RCL button: Make the selected memory slot the active on. Load step loop from memory if selected and active memory slot already match. Channels on HLD will not be loaded.
RCL input [monophonic]: trigger RCL if > 0.V is given.

#### EDIT Section

HLD button: Freezes all channel steps ignoring HLD input.
HLD input [polyphonic]: Freezes individual channels steps if HLD value > 0.

RND button: Randomize all channel steps while RND is pressed, ignoring RND input. Channels on HLD will not be randomized.
RND input [polyphonic]: Randomize individual channels steps while HLD value > 0.

<<,>> buttons: shift all steps of all channels left or right. Channels on HLD will not be shifted
<<,>> inputs [polyphonic]: shift individual channels if cv > 0V.

CLR button: Clears all channel steps (step is set to channels OFS value) while CLR is pressed, ignoring CLR input. Channels on HLD will not be cleared.
CLR input [polyphonic]: Clear individual channels steps while HLD value > 0.

#### EXT and Ramdom Section

EXT button: Use the SRC input as source for non random replacement instead of active memory slot.
EXT input [polyphonic]: external cv input to process as source or record
REC button: Momentary button to force EXT input to be recorded to the step loop.
REC input [polyphonic]: Force record of individula channels if REC cv is > 5.0V

GTP knob: Gate 'probability'. Threshold at which the current steps cv generated a gate
GTP input [polyphonic]. GTP values for individual channels. Input overrides knob if a value is present for that channel.

SCL knob: Scale for randomly generated cv values. Negative bipolar, positive unipolar.
SCL input [polyphonic]. SCL values for individual channels. Input overrides knob if a value is present for that channel.

OFS knob: Offset for randomly generated cv values. Negative bipolar, positive unipolar.
OFS input [polyphonic]. OFS values for individual channels. Input overrides knob if a value is present for that channel.

#### Input/Output Section

RST input [monophonic]: Trigger input for Reset. Sets the head position of all step loops to 0.
CLK input [monophonic]: Trigger input for Clock

SRC output [polyphonic]: Values of the current EXT cv if EXT is on, or active memory slot if EXT is off
GATE output [polyphonic]: Gate out (can be switched to Trigger in right click menu)
CV out [polyphonic]: Value of the current step of the step loop.
