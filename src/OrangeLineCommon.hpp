/*
	OrangeLineModule.hpp
 
	Include file  member variables and methods common to all OrangeLine modules.
	To be included inside the module definition <module_name>.cpp like this:
		...
		struct <module_name> : Module {
			#include "OrangeLineCommon.hpp"
		...
	All methods starting with modulul.. like moduleInitStateTypes have to be implemented in
	<module_name>.cpp
Copyright (C) 2019 Dieter Stubler
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "OrangeLine.hpp"

// ********************************************************************************************************************************
/*
	Common member variables of all OrangeLine modules
	Never use OL_ member variables in module specific code like in <module_name>.cpp
	Use macros defined in OrangeLine.hpp instead
*/
float OL_state          [NUM_STATES];	//	state values
char  OL_stateType      [NUM_STATES];	//	type of state variable char used as tiny int here !
bool  OL_inStateChange  [NUM_STATES];	//	flags to control processing for incoming state changes
bool  OL_outStateChange [NUM_STATES];	//	flags to control reflection for outgoing state changes
bool  OL_inputConnected [NUM_INPUTS];	//	flags to remember connected inputs
char  *OL_jsonLabel     [NUM_JSONS];	//	lables of json state properties
unsigned long OL_customChangeMask[NUM_PARAMS + NUM_INPUTS];	// bitmask to speed up change detection in process
unsigned long OL_customChangeBits = 0;		// change bits set based on OL_customChangeMasks
dsp::SchmittTrigger *OL_inStateTrigger  [NUM_TRIGGERS];	//	trigger objects for param (buttons) and inputs (triggers)
dsp::PulseGenerator *OL_outStateTrigger [NUM_OUTPUTS];	//	pulse generator objects for outputs (triggers)
bool OL_isGate [NUM_OUTPUTS];
bool OL_wasTriggered [NUM_OUTPUTS];		// remember whether we triggered once at all only set when triggerd but never reset
bool OL_isPoly [NUM_INPUTS + NUM_OUTPUTS];
bool OL_isGatePoly [NUM_OUTPUTS * POLY_CHANNELS];
bool OL_isSteadyGate[NUM_OUTPUTS * POLY_CHANNELS];
int  OL_polyChannels[NUM_OUTPUTS];

const char *notes[NUM_NOTES] = {"C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "};
	
/*
	Poly data
*/
dsp::SchmittTrigger *OL_inStateTriggerPoly  [NUM_INPUTS * POLY_CHANNELS];	//	trigger objects for param (buttons) and inputs (triggers)
dsp::PulseGenerator *OL_outStateTriggerPoly [NUM_OUTPUTS * POLY_CHANNELS];	//	pulse generator objects for outputs (triggers)
float OL_statePoly          [(NUM_INPUTS + NUM_OUTPUTS) * POLY_CHANNELS];	//	state values
bool  OL_inStateChangePoly  [NUM_INPUTS * POLY_CHANNELS];	//	flags to control processing for incoming state changes
bool  OL_outStateChangePoly [NUM_OUTPUTS * POLY_CHANNELS];	//	flags to control reflection for outgoing state changes
bool  OL_wasTriggeredPoly   [NUM_OUTPUTS * POLY_CHANNELS];	// remember whether we triggered once at all only set when triggerd but never reset


double OL_sampleTime;
bool   OL_initialized = false;

bool   styleChanged = true;

SvgPanel *brightPanel;
SvgPanel *darkPanel;

const char *channelNumbers[16] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"
};

int    idleSkipCounter = 0;
int    samplesSkipped  = 0;

int    lastParamChanged = 0;
bool   paramChanged = false;
bool   dataFromJsonCalled = false;
bool   moduleInitJsonCalled = false;
/*
	Random implementation derived from the one used in Frozen Wastland Seeds of Change
*/
OrangeLineRandom globalRandom;
	
void initRandom (OrangeLineRandom *rnd, unsigned long s)
{
	rnd->mti=N+1; /* mti==N+1 means mt[N] is not initialized */
	rnd->latestSeed = s;
    rnd->mt[0]= s & 0xffffffffUL;
    for (rnd->mti=1; rnd->mti<N; rnd->mti++) {
        rnd->mt[rnd->mti] = 
	    (1812433253UL * (rnd->mt[rnd->mti-1] ^ (rnd->mt[rnd->mti-1] >> 30)) + rnd->mti); 
        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
        /* In the previous versions, MSBs of the seed affect   */
        /* only MSBs of the array mt[].                        */
        /* 2002/01/09 modified by Makoto Matsumoto             */
        rnd->mt[rnd->mti] &= 0xffffffffUL;
        /* for >32 bit machines */
    }
	rnd->getCount = 0;
}
unsigned long getRandomRaw (OrangeLineRandom *rnd)
{
    unsigned long y;
    static unsigned long mag01[2]={0x0UL, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */

    if (rnd->mti >= N) { /* generate N words at one time */
        int kk;
        for (kk=0;kk<N-M;kk++) {
            y = (rnd->mt[kk]&UPPER_MASK)|(rnd->mt[kk+1]&LOWER_MASK);
            rnd->mt[kk] = rnd->mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        for (;kk<N-1;kk++) {
            y = (rnd->mt[kk]&UPPER_MASK)|(rnd->mt[kk+1]&LOWER_MASK);
            rnd->mt[kk] = rnd->mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        y = (rnd->mt[N-1]&UPPER_MASK)|(rnd->mt[0]&LOWER_MASK);
        rnd->mt[N-1] = rnd->mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1UL];
       rnd->mti = 0;
    }
    y = rnd->mt[rnd->mti++];
    /* Tempering */
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);
	rnd->getCount++;
    return y;
}

double getRandom (OrangeLineRandom *rnd)
{
	return getRandomRaw (rnd) * (1.0/4294967296.0);
}

// ********************************************************************************************************************************
/*
	Initialization
	Constructor has to create a valid and consistent initial state to present to the dataToJson call
	right after adding the module.
	Now two cases:
	1.	dataToJson is called first:	module added to the patch
	2.	dataFromJson is called first: patch was (re)loaded or a preset was loaded
	In both cases we may have to init non persistent state of the module after both actions.
	This forces an initialize step at the beginning of process () which handles this 
	before further processing
	OL_initialized = false indicates that the module was just added to the patch or dataFromJson has been called
	and has to be initialized
*/

/**
	Initialize  Modul State
	Called from Constructor
	Set all default types and collect values/Voltages where appropriate.
	then calls module specific initStateTypes () method to set module specific types != defaults.
	and allocates dsp::SchmittTrigger and dsp::PulsGenerator() objects as needed afterwards.
*/
inline void initializeInstance () {
	memset (          OL_isPoly, false, sizeof (OL_isPoly));	// Must be before initStateTypes ()!
	memset (OL_customChangeMask,    0L, sizeof (OL_customChangeMask));	// Initialie customChangeMasks to 0s

	memset (    OL_isSteadyGate, false, sizeof (OL_isSteadyGate));		// Initialize SteadyGateFlags
	initStateTypes ();			//	Initialize state types to defaults
	moduleInitStateTypes ();	//	Method to overwrite defaults by module specific settings 
	allocateTriggers();			//	Allocate triggers and pulse generators for trigger I/O
	moduleInitJsonConfig ();	//	Initialize json configuration like setting the json labels for json state attributes
	moduleInitJsonCalled = true;
	memset (           OL_state,   0.f, sizeof (OL_state));				// Initialize state values
	memset (   OL_inStateChange, false, sizeof (OL_inStateChange));		// Initialize incoming state changes
	memset (  OL_outStateChange, false, sizeof (OL_outStateChange));	// Initialize outgoing state changes
	memset (          OL_isGate, false, sizeof (OL_isGate));			// Initialize trg outputs to TRIGGER = false (GATE = true)
	memset (    OL_wasTriggered, false, sizeof (OL_wasTriggered));		// Initialize trg outputs to TRIGGER = false (GATE = true)
	memset (    OL_polyChannels,     0, sizeof (OL_polyChannels));		// Initialize number of poly channels for outputs
	memset (      OL_isGatePoly,     0, sizeof (OL_isGatePoly));		// Initialize number isGate definitions of poly channels for outputs

	memset (          OL_statePoly,   0.f, sizeof (OL_statePoly));
	memset (  OL_inStateChangePoly, false, sizeof (OL_inStateChangePoly));
	memset ( OL_outStateChangePoly, false, sizeof (OL_outStateChangePoly));
	memset (   OL_wasTriggeredPoly, false, sizeof (OL_wasTriggeredPoly));
	/*
		Now we call moduleReset () to ensure that a valid json state is created before this constructor
		returns.
		Rack calls data2Json right after invoking this constructor and we have to make sure,
		that Rack doesn't see any invalid or corrupted not initialized data
	*/
	moduleReset ();
	/*
		VCV interface configuration
	*/
	config (NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	moduleParamConfig ();
	/*
		Initialize globalRandomGeneratorInstance with zero random seed
	*/
	rack::random::init ();
	uint64_t seed = rack::random::u64 ();	
	initRandom (&globalRandom, seed);
}

/**
	Initialize state types to default types
*/
inline void initStateTypes () {
	for (int stateIdx = stateIdxParam (0), paramIdx = 0; stateIdx <= maxStateIdxParam; stateIdx++, paramIdx ++)
		OL_stateType[stateIdx] = STATE_TYPE_VALUE;	
	for (int stateIdx = stateIdxInput (0), inputIdx = 0; stateIdx <= maxStateIdxInput; stateIdx++, inputIdx++)
		OL_stateType[stateIdx] = STATE_TYPE_VALUE;	
	for (int stateIdx = stateIdxOutput (0); stateIdx <= maxStateIdxOutput; stateIdx++)
		OL_stateType[stateIdx] = STATE_TYPE_VALUE;
	for (int stateIdx = stateIdxLight (0), lightIdx = 0; stateIdx <= maxStateIdxLight; stateIdx++, lightIdx++)
		OL_stateType[stateIdx] = LIGHT_TYPE_SINGLE;
}

/**
	Allocate triggers and pulse generators
*/
inline void allocateTriggers () {
	for (int paramIdx = 0; paramIdx < NUM_PARAMS; paramIdx++) {
		if (getStateTypeParam (paramIdx) == STATE_TYPE_TRIGGER)
			OL_inStateTrigger[paramIdx] = new dsp::SchmittTrigger();
		else
			OL_inStateTrigger[paramIdx] = nullptr;
	}
	for (int inputIdx = 0; inputIdx < NUM_INPUTS; inputIdx++) {
		if (getInPoly(inputIdx)) {
			for (int poly = 0; poly < POLY_CHANNELS; poly++) {
				if (getStateTypeInput (inputIdx) == STATE_TYPE_TRIGGER)
					OL_inStateTriggerPoly[inputIdx * POLY_CHANNELS + poly] = new dsp::SchmittTrigger();
				else
					OL_inStateTriggerPoly[inputIdx * POLY_CHANNELS + poly] = nullptr;
			}
		}
		else {
			if (getStateTypeInput (inputIdx) == STATE_TYPE_TRIGGER)
				OL_inStateTrigger[NUM_PARAMS + inputIdx] = new dsp::SchmittTrigger();
			else
				OL_inStateTrigger[NUM_PARAMS + inputIdx] = nullptr;
		}
	}
	for (int outputIdx = 0; outputIdx < NUM_OUTPUTS; outputIdx++) {
		if (getOutPoly(outputIdx)) {
			for (int poly = 0; poly < POLY_CHANNELS; poly++) {
				if (getStateTypeOutput (outputIdx) == STATE_TYPE_TRIGGER)
					OL_outStateTriggerPoly[outputIdx * POLY_CHANNELS + poly] = new dsp::PulseGenerator();
				else
					OL_outStateTriggerPoly[outputIdx * POLY_CHANNELS + poly] = nullptr;
			}
		}
		else {
			if (getStateTypeOutput (outputIdx) == STATE_TYPE_TRIGGER)
				OL_outStateTrigger[outputIdx] = new dsp::PulseGenerator();
			else
				OL_outStateTrigger[outputIdx] = nullptr;
		}
	}
}

/**
	Method to configure json labels
*/
inline void setJsonLabel (int idx, char *label) {
	OL_jsonLabel[idx] = label;
}

// ********************************************************************************************************************************
/*
	Utility methods allowed to use in <module_name>.cpp
*/

/**
	Clamp a value
*/
inline float clamp (float value, float minValue, float maxValue) {
	return value < minValue ? minValue : value > maxValue ? maxValue : value;
}

/**
	Set rgb light color from color given as 0xrrggbb
*/
inline void setRgbLight (int lightId, int color) {
	setStateLight (lightId    , float(color >> 16));
	setStateLight (lightId + 1, float((color & 0x00ff00) >> 8));
	setStateLight (lightId + 2, float(color & 0x0000ff));
}

// ********************************************************************************************************************************
/*
	Utility methods for common code
	Do not use OL_ methods in <module_name>.cpp
*/

/**
	Method to set the incoming state value of params
	input and output state changes are flagged
*/
inline void OL_setInState (int stateIdx, float value) {
	if (OL_state[stateIdx] != value) {
		OL_state[stateIdx] = value;
		OL_outStateChange[stateIdx] = OL_inStateChange[stateIdx] = true;
	}
}

inline void OL_setInStatePoly (int stateIdx, int channel, float value) {
	if (OL_statePoly[(NUM_INPUTS + stateIdx) * POLY_CHANNELS + channel] != value) {
		OL_statePoly[(NUM_INPUTS + stateIdx) * POLY_CHANNELS + channel] = value;
		if (stateIdxOutput (stateIdx) != STATE_TYPE_TRIGGER || value > 0.) {
			OL_inStateChangePoly[stateIdx * POLY_CHANNELS + channel] = true;
			OL_outStateChangePoly[stateIdx * POLY_CHANNELS + channel] = true;
		}
	}
}
/**
	Method to set the outgoing state of params
	output state change is flagged
*/
inline void OL_setOutState (int stateIdx, float value) {
	if (OL_state[stateIdx] != value) {
		OL_state[stateIdx]  = value;
		OL_outStateChange[stateIdx] = true;
	}
}
inline void OL_setOutStatePoly (int stateIdx, int channel, float value) {
	// if (OL_statePoly[(NUM_INPUTS + stateIdx) * POLY_CHANNELS + channel] != value) {
		OL_statePoly[(NUM_INPUTS + stateIdx) * POLY_CHANNELS + channel] = value;
		if (stateIdxOutput (stateIdx) != STATE_TYPE_TRIGGER || value > 0.) {
			OL_outStateChangePoly[stateIdx * POLY_CHANNELS + channel] = true;
		}
	// }
}
/**
	Method to configure json labels
*/
inline NVGcolor getTextColor () {
	return (OL_state[STYLE_JSON] == STYLE_ORANGE ? ORANGE : WHITE);
}

float getFromParamOrPolyInput(int param, int input, int channel, float inputScale, int valueMode, int normalMode) {
	float paramValue = getStateParam(param);
	float inputValue = paramValue;
	if (getInputConnected(input)) {
		int channels = inputs[input].getChannels();
		if (channel < channels) {
			inputValue = OL_statePoly[input * POLY_CHANNELS + channel] * inputScale;
		}
		else {
			switch (normalMode) {
				case NORMAL_MODE_NONE:
					break;
				case NORMAL_MODE_ONE:
					if (channels == 1) {
						inputValue = OL_statePoly[input * POLY_CHANNELS];
					}
					break;
				case NORMAL_MODE_LAST:
					inputValue = OL_statePoly[input * POLY_CHANNELS + channels - 1];
					break;
			}
			inputValue *= inputScale;
		}
	}
	else {
		return paramValue;
	}
	float value = paramValue;
	switch (valueMode) {
		case VALUE_MODE_REPLACE:
			return inputValue;
		case VALUE_MODE_ADD:
			return paramValue + inputValue;
		case VALUE_MODE_SCALE:
			// The value range of the parameter has to be in [-1:1] for this to work correctly
			return inputValue * paramValue;
	}
	return value;
}

// ********************************************************************************************************************************
/*
	Methods called from process ()
*/

/**
	Generic process() Method
*/
void process (const ProcessArgs &args) override {
	bool skip = moduleSkipProcess();
	idleSkipCounter = (idleSkipCounter + 1) % IDLESKIP;
	if (skip) {
		samplesSkipped ++;
		processActiveOutputTriggers ();
		return;
	}

	OL_sampleTime = 1.0 / (double)(APP->engine->getSampleRate ());

	initialize ();
	processParamsAndInputs ();
	moduleProcessState ();
	moduleProcess (args);
	moduleReflectChanges ();
	reflectChanges();

	OL_initialized = true;
	samplesSkipped = 0;
	dataFromJsonCalled = false;
}

/**
  Initialize Module after:
		adding the module to or 
		(re)loading a patch or 
		loading a preset or 
		reset call (right click initialize)
	Can and does assume that all params assotiated to ui components have
	been set or initialized and json state is valid.
	If already initialized, reset all state changes to false to prepare a clean
	state to the following state processing.
	If not yet initialized, moduleInitialize () is called and out state changes are
	flagged to ensure that reflectChanges () updates the ui to reflect the new state. 
	In state changes are not reset when moduleInitialize () is called because
	moduleInitialize () might leave things to do in later processModule ().
	When loading a patch or a preset is loaded, the called to dataFromJson () will set
	OL_initialized to false to request initialize ().
*/
inline void initialize () {
	moduleCustomInitialize();
	for (unsigned long i = stateIdxParam(0); i < NUM_STATES; i ++)
		OL_outStateChange[i] = false;
	for (int i = 0; i < NUM_OUTPUTS; i ++) {
		int idx = i * POLY_CHANNELS;
		if (getOutPoly(i))
			for (int j = 0; j < POLY_CHANNELS; j++)
				OL_outStateChangePoly[idx + j] = false;
		else
			OL_outStateChangePoly[idx] = false;
	}
	if (OL_initialized) {
		int idxParam = stateIdxParam(0);
		for (int i = 0; i < NUM_PARAMS + NUM_INPUTS; i ++) 
			OL_inStateChange[idxParam + i] = false;
		for (int i = 0; i < NUM_INPUTS; i ++) {
			int idx = i * POLY_CHANNELS;
			if (getInPoly(i))
				for (int j = 0; j < POLY_CHANNELS; j++)
					OL_inStateChangePoly[idx + j] = false;
			else
				OL_inStateChangePoly[idx] = false;
		}
	}
	else {
		moduleInitialize ();
		styleChanged = true;
	}
	
	//	We set OL_initialized to true at the end of process () 
	//	to give other methods the information that we are just initializing
}

/**
	Check all params and inputs, write their values to state and set state changes accordingly
*/
inline void processParamsAndInputs () {
	OL_customChangeBits = 0;

	/*
		Process Params
	*/
	for (int stateIdx = stateIdxParam (0), paramIdx = 0; stateIdx <= maxStateIdxParam; stateIdx++, paramIdx++) {
		if (getStateTypeParam (paramIdx) == STATE_TYPE_TRIGGER) {
			/*
				For triggers we do not use setInStateParam (), 
				because we only want to set OL_inStateChange if we got triggered
			*/
			OL_state[stateIdx] = params[paramIdx].getValue ();
			/*
				Processing triggers from buttons
			
				Big pit I fell in first!
				If you write the line below this comment as:
//			if (((dsp::SchmittTrigger)(*OL_inStateTrigger[i])).process(value))
				this will not work because it looks like we get a new SchmittTigger instance for every call...
			*/
			if (((dsp::SchmittTrigger*)OL_inStateTrigger[paramIdx])->process (OL_state[stateIdx])) {
				OL_inStateChange[stateIdx] = true;
				OL_customChangeBits |= getCustomChangeMaskParam (paramIdx);
			}
		}
		else {
			setInStateParam (paramIdx, params[paramIdx].getValue ());
			if (inChangeParam (paramIdx) && OL_initialized) {
				OL_customChangeBits |= getCustomChangeMaskParam (paramIdx);
				lastParamChanged = paramIdx;
				paramChanged = true;
			}
		}
	}
	/*
		Process Inputs
	*/
	int channels = 0;
	for (int stateIdx = stateIdxInput (0), inputIdx = 0; stateIdx <= maxStateIdxInput; stateIdx++, inputIdx ++) {
		/*
			TUNING: 
				If isConnected() shows up to be an expensive call, 
				check only every n process() calls. 100ms should be more than sufficiant.
				Want to see someone pluging in and out 10x a second ;-)
				because connecting still is user interaction and not modulated 
		*/
		if (inputs[inputIdx].isConnected()) {
			OL_inputConnected[inputIdx] = true;
		}
		else {
			if (OL_inputConnected[inputIdx]) {
				OL_inputConnected[inputIdx] = false;
				setStateInput (inputIdx, 0.f);
				if (changeInput(inputIdx))
					OL_customChangeBits |= getCustomChangeMaskInput (inputIdx);
			}
			continue;	// not connected, so no processing of a value neccessary
		}            
		if (getInPoly(inputIdx)) {
			channels = inputs[inputIdx].getChannels();
			int channel;
			for (channel = 0; channel < channels; channel ++) {
				int idx = inputIdx * POLY_CHANNELS + channel;
				if (getStateTypeInput (inputIdx) == STATE_TYPE_TRIGGER) {
					OL_statePoly[idx] = inputs[inputIdx].getVoltage (channel);
					if (((dsp::SchmittTrigger*)OL_inStateTriggerPoly[idx])->process (OL_statePoly[idx])) {
						OL_inStateChangePoly[idx] = true;
						OL_customChangeBits |= getCustomChangeMaskInput (inputIdx);
					}
				}
				else {
					float value = inputs[inputIdx].getVoltage (channel);
					if (!std::isfinite(value)) value = 0.f;
					if (OL_statePoly[idx] != value) {
						OL_statePoly[idx] = value;
						OL_inStateChangePoly[idx] = true;
						OL_customChangeBits |= getCustomChangeMaskInput (inputIdx);
					}
				}
			}
		}
		else {
			if (getStateTypeInput (inputIdx) == STATE_TYPE_TRIGGER) {
				/*
					For triggers we do not use setInStateInput (), 
					because we only want to set OL_inStateChange if we got triggered
				*/
				OL_state[stateIdx] = inputs[inputIdx].getVoltage ();
				/*
					Processing triggers from trigger inputs
				
					Big pit I fell in first!
					If you write the line below this comment as:

						if (((dsp::SchmittTrigger)(*OL_inStateTrigger[i])).process(value))

					this will not work because it looks like we get a new SchmittTigger instance for every call...
				*/
				if (((dsp::SchmittTrigger*)OL_inStateTrigger[NUM_PARAMS + inputIdx])->process (OL_state[stateIdx])) {
					OL_inStateChange[stateIdx] = true;
					OL_customChangeBits |= getCustomChangeMaskInput (inputIdx);
				}
			}
			else { 
				float value = inputs[inputIdx].getVoltage ();
				if (!std::isfinite(value)) value = 0.f;
				// Do not clamp because some modules might have to deal with lower and larger values
				// value = clamp(value, -10.f, 10.f);
				setStateInput (inputIdx, value);
				if (changeInput(inputIdx))
					OL_customChangeBits |= getCustomChangeMaskInput (inputIdx);
			}
		}
	}
}

/**
    Output processing of active triggers
*/
inline void processActiveOutputTriggers () {
	for (int stateIdx = stateIdxOutput (0), outputIdx = 0; stateIdx <= maxStateIdxOutput; stateIdx++, outputIdx++) {
		if (getStateTypeOutput (outputIdx) != STATE_TYPE_TRIGGER)
			continue;
		if (getOutPoly (outputIdx)) {
			int channel;
			for (channel = 0; channel < getOutPolyChannels (outputIdx); channel++) {
				int cvOutPolyIdx = outputIdx * POLY_CHANNELS + channel;
				/*
					Pulse generators of active Trigger outputs have to be processed independently of changes in current process() run
				*/
				if (OL_statePoly [NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] > 0.f) {
					bool trgActive = ((dsp::PulseGenerator*)(OL_outStateTriggerPoly[cvOutPolyIdx]))->process ((float)OL_sampleTime);
					if (trgActive) {
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] = 10.f;
						OL_wasTriggeredPoly[cvOutPolyIdx] = true;
					}
					else 
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] = 0.f;
					
					bool isGate = OL_isGate[outputIdx];
					if (OL_isGatePoly[outputIdx * POLY_CHANNELS + channel])
						isGate = true;

					if (isGate && OL_wasTriggeredPoly[cvOutPolyIdx]) {
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] = 10.f;
						if (OL_isSteadyGate[outputIdx * POLY_CHANNELS + channel])
							trgActive = true;
						else
							trgActive = !trgActive;
					}
					outputs[outputIdx].setVoltage (trgActive ? 10.f : 0.f, channel);
				}
			}
		}
		else {
			/*
				Pulse generators of active Trigger outputs have to be processed independently of changes in current process() run
			*/
			if (getStateOutput (outputIdx) > 0.f) {
				bool trgActive = ((dsp::PulseGenerator*)(OL_outStateTrigger[outputIdx]))->process ((float)OL_sampleTime);
				if (trgActive) {
					setStateOutput (outputIdx, 10.f);
					OL_wasTriggered[outputIdx] = true;
				}
				else 
					setStateOutput (outputIdx, 0.f);
				if (OL_isGate[outputIdx] && OL_wasTriggered[outputIdx]) {
					if (OL_isSteadyGate[outputIdx * POLY_CHANNELS])
						trgActive = true;
					else
						trgActive = !trgActive;
				}
				outputs[outputIdx].setVoltage (trgActive ? 10.f : 0.f);
			}
		}
	}
}

/**
	Set all Params, Outputs and Lights from state
	Called as last task from process()
*/
inline void reflectChanges () {
	/*
		Process Params
	*/
	for (int stateIdx = stateIdxParam (0), paramIdx = 0; stateIdx <= maxStateIdxParam; stateIdx++, paramIdx ++) {
		if (!initialized) {
			//	On initialize we have to set params and lights independently from its change state
			params[paramIdx].setValue (getStateParam (paramIdx));
		}
		else {
			if (changeParam (paramIdx) && ! inChangeParam (paramIdx)) {	//	VCV Rack does not allow to set a param currently changed (dragged) :-(
				params[paramIdx].setValue (getStateParam (paramIdx));
			}
		}
	}
	/*
		Process Outputs
	*/
	for (int stateIdx = stateIdxOutput (0), outputIdx = 0; stateIdx <= maxStateIdxOutput; stateIdx++, outputIdx++) {
		if (getOutPoly (outputIdx)) {
			int channel;
			for (channel = 0; channel < getOutPolyChannels (outputIdx); channel++) {
				int cvOutPolyIdx = outputIdx * POLY_CHANNELS + channel;
				if (getStateTypeOutput (outputIdx) == STATE_TYPE_VOLTAGE) 
					outputs[outputIdx].setVoltage (OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx], channel);    
				else
					if (OL_outStateChangePoly [cvOutPolyIdx])
						((dsp::PulseGenerator*)(OL_outStateTriggerPoly[cvOutPolyIdx]))->trigger (0.001f);

				/*
					Pulse generators of active Trigger outputs have to be processed independently of changes in current process() run
				*/
				if (getStateTypeOutput (outputIdx) == STATE_TYPE_TRIGGER && OL_statePoly [NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] > 0.f) {
					bool trgActive = ((dsp::PulseGenerator*)(OL_outStateTriggerPoly[cvOutPolyIdx]))->process ((float)OL_sampleTime);
					if (trgActive) {
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] = 10.f;
						OL_wasTriggeredPoly[cvOutPolyIdx] = true;
					}
					else {
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] = 0.f;
					}
					bool isGate = OL_isGate[outputIdx];
					if (OL_isGatePoly[outputIdx * POLY_CHANNELS + channel])
						isGate = true;
					if (isGate && OL_wasTriggeredPoly[cvOutPolyIdx]) {
						if (OL_isSteadyGate[outputIdx * POLY_CHANNELS + channel])
							trgActive = true;
						else
							trgActive = !trgActive;
					}
					outputs[outputIdx].setVoltage (trgActive ? 10.f : 0.f, channel);
				}
			}
			outputs[outputIdx].setChannels(channel);
		}
		else {
			if (getStateTypeOutput (outputIdx) == STATE_TYPE_VOLTAGE)
				outputs[outputIdx].setVoltage (getStateOutput (outputIdx));    
			else
				if (changeOutput (outputIdx))
					((dsp::PulseGenerator*)(OL_outStateTrigger[outputIdx]))->trigger (0.001f);
			/*
				Pulse generators of active Trigger outputs have to be processed independently of changes in current process() run
			*/
			if (getStateTypeOutput (outputIdx) == STATE_TYPE_TRIGGER && getStateOutput (outputIdx) > 0.f) {
				bool trgActive = ((dsp::PulseGenerator*)(OL_outStateTrigger[outputIdx]))->process ((float)OL_sampleTime);
				if (trgActive) {
					setStateOutput (outputIdx, 10.f);
					OL_wasTriggered[outputIdx] = true;
				}
				else 
					setStateOutput (outputIdx, 0.f);
				if (OL_isGate[outputIdx] && OL_wasTriggered[outputIdx]) {
					if (OL_isSteadyGate[outputIdx * POLY_CHANNELS])
						trgActive = true;
					else
						trgActive = !trgActive;
				}
				outputs[outputIdx].setVoltage (trgActive ? 10.f : 0.f);
			}
		}
	}
	/*
		Process Lights
	*/
	for (int stateIdx = stateIdxLight (0), lightIdx = 0; stateIdx <= maxStateIdxLight; stateIdx ++, lightIdx++) {
		if (!OL_initialized || changeLight (lightIdx)) {
			lights[lightIdx].value = getStateLight (lightIdx) / 255.f;
		}
	}
}

// ********************************************************************************************************************************
/*
	Callbacks for VCV events
*/

/**
	Create a json object for VCV to store as preset or when saving a patch (including autosave)
*/
json_t *dataToJson () override {
	json_t *rootJ = json_object ();
	if (!moduleInitJsonCalled) return rootJ;
	int jsonIdx;
	for (jsonIdx = 0; jsonIdx < NUM_JSONS; jsonIdx ++) {
		json_object_set_new (rootJ, OL_jsonLabel[jsonIdx], json_real (getStateJson (jsonIdx)));
	}
	return rootJ;
}

/**
	Restore json state values after loading a preset or (re)loading a patch
*/
void dataFromJson (json_t *rootJ) override {
	if (!moduleInitJsonCalled) return;
	json_t *pJson;
	for (int jsonIdx = 0; jsonIdx < NUM_JSONS; jsonIdx ++)
		if ((pJson = json_object_get (rootJ, OL_jsonLabel[jsonIdx])) != nullptr)
			setStateJson (jsonIdx, json_real_value (pJson));
	OL_initialized = false;		// indiacte that we have to reinitialize
	dataFromJsonCalled = true;	// indicate that we reloaded a patch or a preset
}

/**
  Callback for Right Click Initialize
*/
void onReset () override {
	moduleReset ();
	styleChanged = true;
	OL_initialized = false;	//	Request initialize
}
