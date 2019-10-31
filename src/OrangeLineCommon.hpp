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
dsp::SchmittTrigger *OL_inStateTrigger  [NUM_TRIGGERS];	//	trigger objects for param (buttons) and inputs (triggers)
dsp::PulseGenerator *OL_outStateTrigger [NUM_OUTPUTS];	//	pulse generator objects for outputs (triggers)
double OL_sampleTime;
bool OL_initialized = false;

// ********************************************************************************************************************************
/*
	Initialization

	Constructor has to create a valid and consistent initial state to present to the dataToJson call
	right after adding the module.

	Now to cases:

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
	initStateTypes ();			//	Initialize state types to defaults
	moduleInitStateTypes ();	//	Method to overwrite defaults by module specific settings 
	allocateTriggers();			//	Allocate triggers and pulse generators for trigger I/O
	moduleInitJsonConfig ();	//	Initialize json configuration like setting the json labels for json state attributes
	memset (         OL_state,   0.f, sizeof (OL_state));			//	Initialize state values
	memset ( OL_inStateChange, false, sizeof (OL_inStateChange));	//	Initialize incoming state changes
	memset (OL_outStateChange, false, sizeof (OL_outStateChange));	//	Initialize outgoing state changes
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
		if (getStateTypeInput (inputIdx) == STATE_TYPE_TRIGGER)
			OL_inStateTrigger[NUM_PARAMS + inputIdx] = new dsp::SchmittTrigger();
		else
			OL_inStateTrigger[NUM_PARAMS + inputIdx] = nullptr;
	}
	for (int outputIdx = 0; outputIdx < NUM_OUTPUTS; outputIdx++) {
		if (getStateTypeOutput (outputIdx) == STATE_TYPE_TRIGGER)
			OL_outStateTrigger[outputIdx] = new dsp::PulseGenerator();
		else
			OL_outStateTrigger[outputIdx] = nullptr;
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

// ********************************************************************************************************************************
/*
	Methods called from process ()
*/

/**
	Generic process() Method
*/
void process (const ProcessArgs &args) override {

	OL_sampleTime = 1.0 / (double)(APP->engine->getSampleRate ());

	initialize ();
	processParamsAndInputs ();
	moduleProcessState ();
	moduleProcess (args);
	moduleReflectChanges ();
	reflectChanges();

	OL_initialized = true;
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
	memset (OL_outStateChange,  false, sizeof(OL_outStateChange));
	if (OL_initialized)
		memset (OL_inStateChange,   false, sizeof( OL_inStateChange));
	else
		moduleInitialize ();
	
	//	We set OL_initialized to true at the end of process () 
	//	to give other methods the information that we are just initializing
}

/**
	Check all params and inputs, write their values to state and set state changes accordingly
*/
inline void processParamsAndInputs () {
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
			if (((dsp::SchmittTrigger*)OL_inStateTrigger[paramIdx])->process (OL_state[stateIdx]))
				OL_inStateChange[stateIdx] = true;
		}
		else {
			setInStateParam (paramIdx, params[paramIdx].getValue ());
		}
	}
	/*
		Process Inputs
	*/
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
			OL_inputConnected[inputIdx] = false;
			continue;	// not connected, so no processing of a value neccessary
		}            
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

//			if (((dsp::SchmittTrigger)(*OL_inStateTrigger[i])).process(value))

				this will not work because it looks like we get a new SchmittTigger instance for every call...
			*/
			if (((dsp::SchmittTrigger*)OL_inStateTrigger[NUM_PARAMS + inputIdx])->process (OL_state[stateIdx]))
				OL_inStateChange[stateIdx] = true;
		}
		else 
			setStateInput (inputIdx, inputs[inputIdx].getVoltage ());
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
  		if (changeOutput (outputIdx)) {
 			if (getStateTypeOutput (outputIdx) == STATE_TYPE_VOLTAGE) {
				outputs[outputIdx].setVoltage (getStateOutput (outputIdx));    
			 }
			else	// OL_stateType[stateIdx] == STATE_TYPE_TRIGGER
				((dsp::PulseGenerator*)(OL_outStateTrigger[outputIdx]))->trigger (0.001f);
		}
		/*
			Pulse generators of active Trigger outputs have to be processed independently of changes in current process() run
		*/
		if (getStateTypeOutput (outputIdx) == STATE_TYPE_TRIGGER && getStateOutput (outputIdx) > 0.f) {
			setStateOutput (outputIdx, ((dsp::PulseGenerator*)(OL_outStateTrigger[outputIdx]))->process ((float)OL_sampleTime) ? 10.0f : 0.0f);
			outputs[outputIdx].setVoltage (getStateOutput (outputIdx));
		}
	}
	/*
		Process Lights
	*/
	for (int stateIdx = stateIdxLight (0), lightIdx = 0; stateIdx <= maxStateIdxLight; stateIdx ++, lightIdx++) {
		if (!OL_initialized || changeLight (lightIdx)) {
			lights[lightIdx].value = getStateLight (lightIdx);
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
	// printf ("dataFromJson ()\n");
	json_t *pJson;
	for (int jsonIdx = 0; jsonIdx < NUM_JSONS; jsonIdx ++)
		if ((pJson = json_object_get (rootJ, OL_jsonLabel[jsonIdx])) != nullptr)
			setStateJson (jsonIdx, json_real_value (pJson));

	OL_initialized = false;	//  indiacte that we have to reinitialize
}

/**
  Callback for Right Click Initialize
*/
void onReset () override {
	moduleReset ();
	OL_initialized = false;	//	Request initialize
}