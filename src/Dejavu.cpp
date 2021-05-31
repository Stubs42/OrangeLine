/*
	Dejavu.cpp
 
	Code for the OrangeLine module Dejavu

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
#include <string>
#include <stdio.h>
#include <limits.h>
#include <sys/time.h>

#include "Dejavu.hpp"

struct Dejavu : Module {

	#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	float oldClkInputVoltage = 0;

	struct OrangeLineRandom repeatRandomGenerator[ NUM_ROWS];
	unsigned long                      repeatSeed[ NUM_ROWS];

	struct OrangeLineRandom channelRandomGeneratorGate[POLY_CHANNELS];
	struct OrangeLineRandom   channelRandomGeneratorCv[POLY_CHANNELS];

	#pragma GCC diagnostic push 
	#pragma GCC diagnostic ignored "-Wwrite-strings"

	char *displayHeading = "ACTIVE SEED:";

	#pragma GCC diagnostic pop

	OrangeLineRandom *p_srcRandomGenerator = &globalRandom;

	int effectiveCount[8] = {1,1,1,1,1,1,1,1};

	bool flashEvent[8] = {false, false, false, false, false, false, false, false };

	float oldModuleState = STATE_ACTIVE;

	bool wobbleParamActive = false;

// ********************************************************************************************************************************
/*
	Initialization
*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Dejavu () {
		initializeInstance ();
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
	bool moduleSkipProcess() {
		bool skip = (idleSkipCounter != 0);
		if (skip) {
			float clkInputVoltage = inputs[CLK_INPUT].getVoltage (); 
			if (clkInputVoltage != oldClkInputVoltage)
				skip = false;
			oldClkInputVoltage = clkInputVoltage;
		}
		return skip;
	}
	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes () {
   		setStateTypeInput  (CLK_INPUT, STATE_TYPE_TRIGGER);
   		setStateTypeInput  (RST_INPUT, STATE_TYPE_TRIGGER);

		for (int row = 0; row < NUM_ROWS;row ++)
			setStateTypeParam  (ONOFF_PARAM + row, STATE_TYPE_TRIGGER);

		setInPoly          (REP_INPUT, true);
		setOutPoly         (REP_OUTPUT, true);

		setInPoly          (TRG_INPUT, true);
		setStateTypeOutput (TRG_OUTPUT, STATE_TYPE_TRIGGER);
		setOutPoly         (TRG_OUTPUT, true);

		setInPoly          (HEAT_INPUT, true);
		setInPoly          (HEAT_KNOB_ATT_INPUT, true);

		setInPoly          (OFS_INPUT, true);
		setInPoly          (SCL_INPUT, true);

		setStateTypeParam  (SH_PARAM, STATE_TYPE_TRIGGER);
		setInPoly          (SH_INPUT, true);

		setStateTypeParam  (GATE_PARAM, STATE_TYPE_TRIGGER);
		setInPoly          (GATE_INPUT, true);

     	setStateTypeOutput (GATE_OUTPUT, STATE_TYPE_TRIGGER);
		setOutPoly         (GATE_OUTPUT, true);

		setOutPoly         (CV_OUTPUT, true);

#ifdef USE_DEBUG_OUTPUT
		setOutPoly         (DEBUG_OUTPUT, true);
#endif

	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig () {

		#pragma GCC diagnostic push 
		#pragma GCC diagnostic ignored "-Wwrite-strings"
		//
		// Config internal Parameters not bound to a user interface object
		//
		setJsonLabel (STYLE_JSON, "style");
		setJsonLabel (RESET_JSON, "reset");
		setJsonLabel (LEN_COUNTER_JSON + 0, "lenConter1");
		setJsonLabel (LEN_COUNTER_JSON + 1, "lenConter2");
		setJsonLabel (LEN_COUNTER_JSON + 2, "lenConter3");
		setJsonLabel (LEN_COUNTER_JSON + 3, "lenConter4");
		setJsonLabel (ONOFF_JSON + 0, "onOff1");
		setJsonLabel (ONOFF_JSON + 1, "onOff2");
		setJsonLabel (ONOFF_JSON + 2, "onOff3");
		setJsonLabel (ONOFF_JSON + 3, "onOff4");
		setJsonLabel (DUR_COUNTER_JSON + 0, "durConter1");
		setJsonLabel (DUR_COUNTER_JSON + 1, "durConter2");
		setJsonLabel (DUR_COUNTER_JSON + 2, "durConter3");
		setJsonLabel (DUR_COUNTER_JSON + 3, "durConter4");
		setJsonLabel (GATE_JSON, "gate");
		setJsonLabel (SH_JSON, "sh");
		setJsonLabel (DIVCOUNTER_JSON, "divCounter");
		setJsonLabel (POLY_CHANNELS_JSON, "polyChannels");
		setJsonLabel (MODULE_STATE_JSON, "moduleState");
		setJsonLabel (DIRECTION_JSON, "directon");
		setJsonLabel (ACTIVE_PARAM_JSON + 0, "ActiveLength1");
		setJsonLabel (ACTIVE_PARAM_JSON + 1, "ActiveLength2");
		setJsonLabel (ACTIVE_PARAM_JSON + 2, "ActiveLength3");
		setJsonLabel (ACTIVE_PARAM_JSON + 3, "ActiveLength4");
		setJsonLabel (ACTIVE_PARAM_JSON + 4, "ActiveDuration1");
		setJsonLabel (ACTIVE_PARAM_JSON + 5, "ActiveDuration2");
		setJsonLabel (ACTIVE_PARAM_JSON + 6, "ActiveDuration3");
		setJsonLabel (ACTIVE_PARAM_JSON + 7, "ActiveDuration4");
		setJsonLabel (RANGE_JSON + 0, "MaxLength1");
		setJsonLabel (RANGE_JSON + 1, "MaxLength2");
		setJsonLabel (RANGE_JSON + 2, "MaxLength3");
		setJsonLabel (RANGE_JSON + 3, "MaxLength4");
		setJsonLabel (RANGE_JSON + 4, "MaxDuration1");
		setJsonLabel (RANGE_JSON + 5, "MaxDuration2");
		setJsonLabel (RANGE_JSON + 6, "MaxDuration3");
		setJsonLabel (RANGE_JSON + 7, "MaxDuration4");
		setJsonLabel (RESET_DUR_OFFSET_JSON + 0, "ResetDurationOffset1");
		setJsonLabel (RESET_DUR_OFFSET_JSON + 1, "ResetDurationOffset2");
		setJsonLabel (RESET_DUR_OFFSET_JSON + 2, "ResetDurationOffset3");
		setJsonLabel (RESET_DUR_OFFSET_JSON + 3, "ResetDurationOffset4");
		
		#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig () {
		configParam (DIV_PARAM,             1.f,   DIV_MAX,   1.f, "Clock Division",       "", 0.f, 1.f, 0.f);
		configParam (SEED_PARAM,            0.f,  SEED_MAX,   DEFAULT_SEED, "Seed",                 "", 0.f, 1.f, 0.f);
		
		configParam (LEN_PARAM + 0,       1.f,  RANGE_INIT,   1.f, "Length 1",               "", 0.f, 1.f, 0.f);
		configParam (LEN_PARAM + 1,       1.f,  RANGE_INIT,   1.f, "Length 2",               "", 0.f, 1.f, 0.f);
		configParam (LEN_PARAM + 2,       1.f,  RANGE_INIT,   1.f, "Length 3",               "", 0.f, 1.f, 0.f);
		configParam (LEN_PARAM + 3,       1.f,  RANGE_INIT,   1.f, "Length 4",               "", 0.f, 1.f, 0.f);		
 
   		configParam (ONOFF_PARAM + 0,     0.f,         1.f,   0.f, "Repeater 1 On/Offf",     "", 0.f, 1.f, 0.f);
   		configParam (ONOFF_PARAM + 1,     0.f,         1.f,   0.f, "Repeater 2 On/Offf",     "", 0.f, 1.f, 0.f);
   		configParam (ONOFF_PARAM + 2,     0.f,         1.f,   0.f, "Repeater 3 On/Offf",     "", 0.f, 1.f, 0.f);
   		configParam (ONOFF_PARAM + 3,     0.f,         1.f,   0.f, "Repeater 4 On/Offf",     "", 0.f, 1.f, 0.f);

		configParam (DUR_PARAM + 0,       1.f,  RANGE_INIT,   1.f, "Duration 1",             "", 0.f, 1.f, 0.f);
		configParam (DUR_PARAM + 1,       1.f,  RANGE_INIT,   1.f, "Duration 2",             "", 0.f, 1.f, 0.f);
		configParam (DUR_PARAM + 2,       1.f,  RANGE_INIT,   1.f, "Duration 3",             "", 0.f, 1.f, 0.f);
		configParam (DUR_PARAM + 3,       1.f,  RANGE_INIT,   1.f, "Duration 4",             "", 0.f, 1.f, 0.f);		

		configParam (HEAT_PARAM,            0.f,       100.f,  50.f, "Heat",                 "%", 0.f, 1.f, 0.f);
		
		configParam (OFS_PARAM,           -10.f,        10.f,   0.f, "Offset",               "",  0.f, 1.f, 0.f);
		configParam (OFS_ATT_PARAM,      -100.f,       100.f,   0.f, "Offset Attenuation",   "%", 0.f, 1.f, 0.f);
		
		configParam (SCL_PARAM,          -100.f,       100.f, 100.f, "Scale",                "",  0.f, 1.f, 0.f);
		configParam (SCL_ATT_PARAM,      -100.f,       100.f,   0.f, "Scale Attenuation",    "%", 0.f, 1.f, 0.f);

		configParam (CHN_PARAM,             1.f,        16.f,   1.f, "# of Output Channels", "", 0.f, 1.f, 0.f);

   		configParam (SH_PARAM,              0.f,         1.f,   0.f,  "Toggle CV S&H",       "", 0.f, 1.f, 0.f);
   		configParam (GATE_PARAM,            0.f,         1.f,   0.f,  "Toggle Trigger/Gate", "", 0.f, 1.f, 0.f);
	}
	
	inline void moduleCustomInitialize () {
	}

	/**
		Method to initialize the module after loading a patch or a preset
		Called from initialize () included from from OrangeLineCommon.hpp
		to initialize module state from a valid
		json state after module was added to the patch, 
		a call to dataFromJson due to patch or preset load
		or a right click initialize (reset).
	*/
	inline void moduleInitialize () {
		styleChanged = true;
		reconfigureForState ();
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
	*/
	void moduleReset () {
		setStateJson (RESET_JSON,       0.f);
		setStateJson (LEN_COUNTER_JSON + 0, 0.f);
		setStateJson (LEN_COUNTER_JSON + 1, 0.f);
		setStateJson (LEN_COUNTER_JSON + 2, 0.f);
		setStateJson (LEN_COUNTER_JSON + 3, 0.f);
		setStateJson (ONOFF_JSON + 0, 0.f);
		setStateJson (ONOFF_JSON + 1, 0.f);
		setStateJson (ONOFF_JSON + 2, 0.f);
		setStateJson (ONOFF_JSON + 3, 0.f);
		setStateJson (DUR_COUNTER_JSON + 0, 0.f);
		setStateJson (DUR_COUNTER_JSON + 1, 0.f);
		setStateJson (DUR_COUNTER_JSON + 2, 0.f);
		setStateJson (DUR_COUNTER_JSON + 3, 0.f);
		setStateJson (GATE_JSON, TRIGGER_MODE);
		setStateJson (SH_JSON, 0.f);
		setStateJson (DIVCOUNTER_JSON, 0.f);
		setStateJson (POLY_CHANNELS_JSON, 1.f);
		setStateJson (MODULE_STATE_JSON, STATE_ACTIVE);
		setStateJson (DIRECTION_JSON, DIRECTION_BACKWARD);

		setStateJson (MODULE_STATE_JSON, STATE_ACTIVE);
		oldModuleState = STATE_ACTIVE;

		setStateJson (RANGE_JSON + 0, RANGE_INIT);
		setStateJson (RANGE_JSON + 1, RANGE_INIT);
		setStateJson (RANGE_JSON + 2, RANGE_INIT);
		setStateJson (RANGE_JSON + 3, RANGE_INIT);
		setStateJson (RANGE_JSON + 4, RANGE_INIT);
		setStateJson (RANGE_JSON + 5, RANGE_INIT);
		setStateJson (RANGE_JSON + 6, RANGE_INIT);
		setStateJson (RANGE_JSON + 7, RANGE_INIT);

		setStateJson (ACTIVE_PARAM_JSON + 0, 1);
		setStateJson (ACTIVE_PARAM_JSON + 1, 1);
		setStateJson (ACTIVE_PARAM_JSON + 2, 1);
		setStateJson (ACTIVE_PARAM_JSON + 3, 1);
		setStateJson (ACTIVE_PARAM_JSON + 4, 1);
		setStateJson (ACTIVE_PARAM_JSON + 5, 1);
		setStateJson (ACTIVE_PARAM_JSON + 6, 1);
		setStateJson (ACTIVE_PARAM_JSON + 7, 1);

		setStateJson (RESET_DUR_OFFSET_JSON + 0, 0);
		setStateJson (RESET_DUR_OFFSET_JSON + 1, 0);
		setStateJson (RESET_DUR_OFFSET_JSON + 2, 0);
		setStateJson (RESET_DUR_OFFSET_JSON + 3, 0);
	}

#ifdef USE_DEBUG_OUTPUT
void debugOutput (int channel, float value) {
	setOutPolyChannels (DEBUG_OUTPUT, 16);
	OL_statePoly[NUM_INPUTS * POLY_CHANNELS + DEBUG_OUTPUT * POLY_CHANNELS + channel - 1] = value;
	OL_outStateChangePoly[DEBUG_OUTPUT * POLY_CHANNELS + channel - 1] = true;
}
#endif


// ********************************************************************************************************************************
/*
	Module specific utility methods
*/

// ********************************************************************************************************************************
/*
	Initialize global random generator 
	initializerepeatRandomGenerators
	Set all counters to 0 on reset
*/
	void doReset () {
		float seed = 0; 
		if (getInputConnected (SEED_INPUT)) {
			float seedFloat = getStateInput (SEED_INPUT) * SEED_INPUT_SCALE;
			if (seedFloat < 0)
				seedFloat *= -1;
			unsigned long seed = (unsigned long)seedFloat;
			if (seed > SEED_MAX)
				seed = SEED_MAX;
			setStateParam (SEED_PARAM, float(seed));
		}
		else
			seed = getStateParam (SEED_PARAM);
		initRandom (&globalRandom, (unsigned long)seed);

		int preOffset = 0;
		for (int row = 0; row <  NUM_ROWS; row ++) {

			DEBUG("doReset (): resetting counters for row %d", row);
			DEBUG("doReset (): preOffset = %d", preOffset);
			int offset = int(getStateJson(RESET_DUR_OFFSET_JSON + row));
			DEBUG("doReset (): offset = %d", offset);
			int effDurCnt = effectiveCount[2 * row + DUR];
			DEBUG("doReset (): effDurCnt (duration of row) = %d", effDurCnt);
			int multiple = (row == 0 ? 1 : effectiveCount[2 * (row - 1) + DUR]);
			DEBUG("doReset (): multiple (DUR length of previous row or 1 if row == 0) = %d", multiple);
			offset *=  multiple;
			DEBUG("doReset (): offset *= multiple = %d", offset);
			offset += preOffset;
			DEBUG("doReset (): offset += preOffset = %d", offset);
			int durCounter = effDurCnt - (offset % effDurCnt);
			DEBUG("doReset (): durCounter = effDurCnt - (offset % effDurCnt) = %d", durCounter);
			DEBUG("doReset (): Setting DUR_COUNTER_JSON for row to %d", durCounter);
			setStateJson (DUR_COUNTER_JSON + row, durCounter);
			int effLenCnt = effectiveCount[(row * 2) + LEN];
			DEBUG("doReset (): effLenCnt(length of row) = effectiveCount[(row * 2) + LEN] (length = %d", effLenCnt);
			int lenCounter = effLenCnt - durCounter % effLenCnt;
			DEBUG("doReset (): lenCounter = effLenCnt - durCounter % effLenCnt = %d", lenCounter);
			DEBUG("doReset (): Setting LEN_COUNTER_JSON for row to %d", lenCounter);
			setStateJson (LEN_COUNTER_JSON + row, lenCounter);

			preOffset = offset;
			initRandom (&(repeatRandomGenerator[row]), getRandomRaw(&globalRandom));
		}
	}

	void prepareEffectiveCounts() {
		int effectiveCountIndex = 0;
		int channels = inputs[REP_INPUT].getChannels();
		for (int row = 0; row < NUM_ROWS; row ++) {
			int multiple = (row == 0) ? 1 : effectiveCount[(row - 1) * 2 + DUR];
			for (int lenOrDur = LEN; lenOrDur <= DUR; lenOrDur++) {
				int value = 0;
				if (getInputConnected (REP_INPUT)) {
					for (int mode = RAW; mode >= COOKED; mode--) {
						int inputScale = ( mode == RAW ? REP_INPUT_SCALE_RAW : REP_INPUT_SCALE_COOKED );
						int channel = row * 2 + (mode * 8) + lenOrDur;
						if (channel < channels) {
							int inputValue = floor(OL_statePoly[REP_INPUT * POLY_CHANNELS + channel] * inputScale + 0.5);
							if (inputValue >= 1) {
								value = inputValue * ( mode == RAW ? 1 : multiple );
								break;
							}
						}
					}
				}
				if (value == 0)
					value = int(getStateJson (ACTIVE_PARAM_JSON + lenOrDur * 4 + row)) * multiple;
				// DEBUG("prepareEffectiveCounts(): effectiveCount[%d] = %d", effectiveCountIndex, effectiveCount[effectiveCountIndex]);
				effectiveCount[effectiveCountIndex++] = value;
			}
		}
	}

	bool rowActive (int row) {
		if (int(getStateJson (ONOFF_PARAM + row)) == 0)
			return false;
		if (effectiveCount[row * 2 + LEN] == 1 && effectiveCount[row * 2 + DUR] == 1)
			return false;
		if (row > 0) 
			if (effectiveCount[row * 2 + DUR] == effectiveCount[(row - 1) * 2 + DUR] && 
			    effectiveCount[row * 2 + LEN] == effectiveCount[(row - 1) * 2 + DUR])
				return false;
		return true;
	}

/*
	Methods called directly or indirectly called from process () in OrangeLineCommon.hpp
*/
	/**
		Module specific process method called from process () in OrangeLineCommon.hpp
	*/
	inline void moduleProcess (const ProcessArgs &args) {
		if (styleChanged) {
			switch (int(getStateJson(STYLE_JSON))) {
				case STYLE_ORANGE:
					brightPanel->visible = false;
					darkPanel->visible = false;
					break;
				case STYLE_BRIGHT:
					brightPanel->visible = true;
					darkPanel->visible = false;
					break;
				case STYLE_DARK:
					brightPanel->visible = false;
					darkPanel->visible = true;
					break;
			}
			styleChanged = false;
		}
		syncParams();
		prepareEffectiveCounts();

		if (changeInput (RST_INPUT) && getStateJson(MODULE_STATE_JSON) == STATE_ACTIVE)
			doReset();

		// handle polyphonic TRG_INPUT
		if (getInputConnected (TRG_INPUT) && getStateJson(MODULE_STATE_JSON) == STATE_ACTIVE) {
			int channels = inputs[TRG_INPUT].getChannels();
			for (int channel =  NUM_ROWS - 1; channel >= 0; channel--) {
				if (channel > channels)
					break;
				if (OL_inStateChangePoly[TRG_INPUT * POLY_CHANNELS + channel]) { 
					setStateJson (DUR_COUNTER_JSON + channel, 0.f);
					break;
				}
			}
		}
		if (changeInput (CLK_INPUT)) { // && getStateJson(MODULE_STATE_JSON) == STATE_ACTIVE) {
			if (int(getStateJson (DIVCOUNTER_JSON)) == 0) {
				int div;
				if (getInputConnected (DIV_INPUT))
					div = int(getStateInput(DIV_INPUT) * 10);
				else
					div = int(getStateParam(DIV_PARAM));
				if (div < 1) div = 1;
				if (div > DIV_MAX) div = DIV_MAX;
				setStateJson (DIVCOUNTER_JSON, float(div));
				setOutPolyChannels(REP_OUTPUT, 8);
				p_srcRandomGenerator = &globalRandom;
				for (int row =  NUM_ROWS - 1; row >= 0; row --) {
					if (!rowActive (row))
						continue;	// Nothing to do here
					for (int lenOrDur = DUR; lenOrDur >= LEN; lenOrDur--) {
						int counterJson;
						if (lenOrDur == DUR)
							counterJson = DUR_COUNTER_JSON;
						else
							counterJson = LEN_COUNTER_JSON;
						int count = int(getStateJson (counterJson + row));
						if (count <= 0) {
							flashEvent[(row*2)+lenOrDur] = true;
							if (lenOrDur == DUR) {
								repeatSeed[row] = getRandomRaw (p_srcRandomGenerator);
								setStateJson (LEN_COUNTER_JSON + row, 0.f);
								if (row > 0)
									setStateJson (DUR_COUNTER_JSON + (row - 1), 0.f);
							}
							else {
								initRandom (&(repeatRandomGenerator[row]), repeatSeed[row]);
							}
							count = effectiveCount[(row * 2) + lenOrDur];
							setStateOutPoly (REP_OUTPUT, row * 2 + lenOrDur, float(count) / 10000);
							setStateOutPoly (TRG_OUTPUT, row * 2 + lenOrDur, 10.f);
							setStateJson (counterJson + row, float(count)); 
						}
					}
					p_srcRandomGenerator = &(repeatRandomGenerator[row]);
					setStateJson (DUR_COUNTER_JSON + row, getStateJson (DUR_COUNTER_JSON + row) - 1);
					setStateJson (LEN_COUNTER_JSON + row, getStateJson (LEN_COUNTER_JSON + row) - 1);
				}
				// Reinitialize Gate and CV random generators
				// ge a new seed from the srcRandomGenerator 
				// DEBUG("channelRandomGenerator initialized from lengthRandomGenerator[0] seed : % 8lX", seed);
				// get the seed for the next one by advancing the last initialized one for diversity
				// all generators would work in sync otherwise
				// We intensionally do not use POLY_CHANNELS_JSON here but use POLY_CHANNELS constan 16 here to 
				// not change random pattern due to changing the number of poly channels
				unsigned long seed = getRandomRaw (p_srcRandomGenerator);
				for (int channel = 0; channel < POLY_CHANNELS; channel ++) {
					initRandom (&(channelRandomGeneratorGate[channel]), seed);
					initRandom (&(channelRandomGeneratorCv[channel]), getRandomRaw (&(channelRandomGeneratorGate[channel])));
					seed = getRandomRaw (&(channelRandomGeneratorCv[channel]));
				}
				/*
					Process output channels
				*/
				setOutPolyChannels(TRG_OUTPUT, 8);
				setOutPolyChannels(GATE_OUTPUT, getStateJson(POLY_CHANNELS_JSON));
				setOutPolyChannels(CV_OUTPUT, getStateJson(POLY_CHANNELS_JSON));
				int heatPolyChannels = inputs[HEAT_INPUT].getChannels();
				int ofsPolyChannels = inputs[OFS_INPUT].getChannels();
				int sclPolyChannels = inputs[SCL_INPUT].getChannels();
				int knbPolyChannels = inputs[HEAT_KNOB_ATT_INPUT].getChannels();
				float lastHeatInput = 0;
				float lastOfsInput = 0;
				float lastSclInput = 0;
				float lastKnbInput = 0;
				float lastShValue = 0;
				for (int channel = 0; channel < getStateJson(POLY_CHANNELS_JSON); channel ++) {
					float gateRandom = getRandom (&(channelRandomGeneratorGate[channel]));
					float heat = getStateParam(HEAT_PARAM) / 100.f;
					bool fired = false;
					if (getInputConnected(HEAT_INPUT)) {
						float heatInput = 0.f;

						if (channel > heatPolyChannels)
							heatInput = lastHeatInput;
						else {
							heatInput = OL_statePoly[HEAT_INPUT * POLY_CHANNELS + channel] / 10.f;
							lastHeatInput = heatInput;
						}

						if (getInputConnected(HEAT_KNOB_ATT_INPUT)) {
							float knobAttInput = 1.f;
							if (channel > knbPolyChannels)
								knobAttInput = lastKnbInput;
							else {
								knobAttInput = OL_statePoly[HEAT_KNOB_ATT_INPUT * POLY_CHANNELS + channel] / 10.f;
								lastKnbInput = knobAttInput;
							}
							heat *= knobAttInput;
						}
						heat +=  heatInput;
					}
					if (heat >= gateRandom) {
						OL_statePoly[ (NUM_INPUTS + GATE_OUTPUT) * POLY_CHANNELS + channel] = 10.f;
						OL_outStateChangePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
						fired = true;
					}
					else {
						OL_statePoly[ (NUM_INPUTS + GATE_OUTPUT) * POLY_CHANNELS + channel] = 0.f;
						// Do not signal a output change, this will taken as a trigger !
						// OL_outStateChangePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
						outputs[GATE_OUTPUT].setVoltage (0.f, channel);
					}
					bool sh = false;
					if (!fired) {
						sh = (getStateJson(SH_JSON) == 1.f);
						if (getInputConnected(SH_INPUT)) {
							float value = lastShValue;
							if (channel <= inputs[SH_INPUT].getChannels()) {
								value = OL_statePoly[SH_INPUT * POLY_CHANNELS + channel];
							}
							lastShValue = value;
							sh = (value > 5.f);
						}
					}
					if (!sh || fired) {
						float scl = getStateParam (SCL_PARAM) / 100.f;
						if (getInputConnected (SCL_INPUT)) {
							int sclInput = 0.f;
							if (channel > sclPolyChannels)
								sclInput = lastSclInput;
							else {
								sclInput = OL_statePoly[SCL_INPUT * POLY_CHANNELS + channel];
								lastSclInput = sclInput;
							}
							scl += (getStateParam(SCL_ATT_PARAM) / 100.f) * sclInput / 10.f;
						}
						float cvRandom = getRandom (&(channelRandomGeneratorCv[channel]));
						if (scl < 0)
							cvRandom = (cvRandom - 0.5) * -scl * 20.f;		// bipolar scale
						else
							cvRandom *= scl * 10.f;							// unipolar scale
						float ofs = getStateParam (OFS_PARAM);
						if (getInputConnected (OFS_INPUT)) {
							int ofsInput = 0.f;
							if (channel > ofsPolyChannels)
								ofsInput = lastOfsInput;
							else {
								ofsInput = OL_statePoly[OFS_INPUT * POLY_CHANNELS + channel];
								lastOfsInput = ofsInput;
							}
							ofs += (getStateParam(OFS_ATT_PARAM) / 100.f) * ofsInput;
						}
						cvRandom += ofs;
						cvRandom = clamp (cvRandom, -10.f, 10.f);
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + CV_OUTPUT * POLY_CHANNELS + channel] = cvRandom;
						OL_outStateChangePoly[CV_OUTPUT * POLY_CHANNELS + channel] = true;
					}
				}	
			}	
			setStateJson(DIVCOUNTER_JSON, getStateJson(DIVCOUNTER_JSON) - 1);
		}
	}

	void wobbleParams() {
		if (!wobbleParamActive) {
			for (int row = 0; row < NUM_ROWS; row++) {
				setStateParam(LEN_PARAM + row, getStateParam(LEN_PARAM + row) + WOBBLE_AMOUNT);
				setStateParam(DUR_PARAM + row, getStateParam(DUR_PARAM + row) + WOBBLE_AMOUNT);
			}
			wobbleParamActive = true;
		}
		else {
			for (int row = 0; row < NUM_ROWS; row++) {
				float value = getStateParam(LEN_PARAM + row);
				if (float(int(value)) != value)
					setStateParam(LEN_PARAM + row, value);
				value = getStateParam(DUR_PARAM + row);
				if (float(int(value)) != value)
					setStateParam(DUR_PARAM + row, value);
			}
			wobbleParamActive = false;
		}
	}

	void syncParams () {
		if (oldModuleState != getStateJson (MODULE_STATE_JSON)) {
			paramsFromJson ();
			reconfigureForState ();
			wobbleParams ();
			oldModuleState = getStateJson (MODULE_STATE_JSON);
		}
		paramsToJson();
	}

	void paramsFromJson () {
		if (getStateJson(MODULE_STATE_JSON) == STATE_ACTIVE && oldModuleState != STATE_ACTIVE) {
			// restore user settings for length and duration
			int activeParamIdx = 0;
			for (int i = 0; i < NUM_ROWS; i++, activeParamIdx++)
				setStateParam(LEN_PARAM + i, getStateJson(ACTIVE_PARAM_JSON + activeParamIdx));
			for (int i = 0; i < NUM_ROWS; i++, activeParamIdx++)
				setStateParam(DUR_PARAM + i, getStateJson(ACTIVE_PARAM_JSON + activeParamIdx));
		}
		if (getStateJson(MODULE_STATE_JSON) == STATE_EDIT_RANGES && oldModuleState != STATE_EDIT_RANGES) {
			// set param to values for ranges
			// take care of missig values due to upgrade of module
			for (int i = 0; i < NUM_ROWS * 2; i++)
				if (getStateJson(RANGE_JSON + i) < 1)
					setStateJson(RANGE_JSON + i, RANGE_INIT);
			int activeParamIdx = 0;
			for (int i = 0; i < NUM_ROWS; i++, activeParamIdx++)
				setStateParam(LEN_PARAM + i, getStateJson(RANGE_JSON + activeParamIdx));
			for (int i = 0; i < NUM_ROWS; i++, activeParamIdx++)
				setStateParam(DUR_PARAM + i, getStateJson(RANGE_JSON + activeParamIdx));
		}
		if (getStateJson(MODULE_STATE_JSON) == STATE_EDIT_OFFSETS && oldModuleState != STATE_EDIT_OFFSETS) {
			for (int i = 0; i < NUM_ROWS; i++)
				setStateParam(DUR_PARAM + i, getStateJson(RESET_DUR_OFFSET_JSON + i));
		}
	}

	void paramsToJson () {
		if (getStateJson(MODULE_STATE_JSON) == STATE_ACTIVE) {
			int activeParamIdx = 0;
			for (int i = 0; i < NUM_ROWS; i++, activeParamIdx++)
				setStateJson(ACTIVE_PARAM_JSON + activeParamIdx, getStateParam(LEN_PARAM + i));
			for (int i = 0; i < NUM_ROWS; i++, activeParamIdx++)
				setStateJson(ACTIVE_PARAM_JSON + activeParamIdx, getStateParam(DUR_PARAM + i));
		}
		if (getStateJson(MODULE_STATE_JSON) == STATE_EDIT_RANGES) {
			int activeParamIdx = 0;
			for (int i = 0; i < NUM_ROWS; i++, activeParamIdx++)
				setStateJson(RANGE_JSON + activeParamIdx, getStateParam(LEN_PARAM + i));
			for (int i = 0; i < NUM_ROWS; i++, activeParamIdx++)
				setStateJson(RANGE_JSON + activeParamIdx, getStateParam(DUR_PARAM + i));
		}
		if (getStateJson(MODULE_STATE_JSON) == STATE_EDIT_OFFSETS) {
			for (int i = 0; i < NUM_ROWS; i++)
				setStateJson(RESET_DUR_OFFSET_JSON + i, getStateParam(DUR_PARAM + i));
		}
	}

	void reconfigureForState () {
		if (getStateJson(MODULE_STATE_JSON) == STATE_ACTIVE) {
			int activeParamIdx = 0;
			for (int row = 0; row < NUM_ROWS; row++, activeParamIdx++) {
				reConfigParamMinValue (LEN_PARAM + row, 1);				
				reConfigParamMaxValue (LEN_PARAM + row, getStateJson (RANGE_JSON + activeParamIdx));
				reConfigParamDefault  (LEN_PARAM + row, 1.0);
			}
			for (int row = 0; row < NUM_ROWS; row++, activeParamIdx++) {
				reConfigParamMinValue (DUR_PARAM + row, 1);
				reConfigParamMaxValue (DUR_PARAM + row, getStateJson (RANGE_JSON + activeParamIdx));
				reConfigParamDefault  (DUR_PARAM + row, 1.0);
			}
		}
		if (getStateJson(MODULE_STATE_JSON) == STATE_EDIT_RANGES) {
			int activeParamIdx = 0;
			for (int row = 0; row < NUM_ROWS; row++, activeParamIdx++) {
				reConfigParamMinValue (LEN_PARAM + row, getStateJson (ACTIVE_PARAM_JSON + activeParamIdx));
				reConfigParamMaxValue (LEN_PARAM + row, RANGE_MAX);
				reConfigParamDefault  (LEN_PARAM + row, getStateJson (ACTIVE_PARAM_JSON + activeParamIdx));
			}
			for (int row = 0; row < NUM_ROWS; row++, activeParamIdx++) {
				reConfigParamMinValue (DUR_PARAM + row, getStateJson (ACTIVE_PARAM_JSON + activeParamIdx));
				reConfigParamMaxValue (DUR_PARAM + row, RANGE_MAX);
				reConfigParamDefault  (DUR_PARAM + row, getStateJson (ACTIVE_PARAM_JSON + activeParamIdx));
			}
		}
		if (getStateJson(MODULE_STATE_JSON) == STATE_EDIT_OFFSETS) {
			for (int row = 0; row < NUM_ROWS; row++) {
				reConfigParamMinValue (DUR_PARAM + row, 0);
				reConfigParamMaxValue (DUR_PARAM + row, getStateJson (ACTIVE_PARAM_JSON + DUR * 4 + row));
				reConfigParamDefault  (DUR_PARAM + row, getStateJson (RESET_DUR_OFFSET_JSON + row));
			}
		}
	}

	/**
		Module specific input processing called from process () in OrangeLineCommon.hpp
		right after generic processParamsAndInputs ()

		moduleProcessState () should only be used to derive json state and module member variables
		from params and inputs collected by processParamsAndInputs ().

		This method should not do dsp or other logic processing.
	*/
	inline void moduleProcessState () {
		if (inChangeParam (GATE_PARAM)) {	//	User clicked on tr/gt button
			if (getStateJson (GATE_JSON) == 0.f)
				setStateJson (GATE_JSON, 1.f);
			else {
				setStateJson (GATE_JSON, 0.f);
			}
		}

		if (getStateJson (GATE_JSON) > 0.f && !getInputConnected(GATE_INPUT))
			isGate (GATE_OUTPUT) = true;
		else
			isGate (GATE_OUTPUT) = false;

		for (int row = 0; row <  NUM_ROWS; row++) {
			if (inChangeParam (ONOFF_PARAM + row))
				setStateJson (ONOFF_JSON + row, float((int(getStateJson (ONOFF_JSON + row)) + 1) % 2));
		}

		if (inChangeParam (SH_PARAM))	{
			setStateJson (SH_JSON, float((int(getStateJson (SH_JSON)) + 1) % 2));
		}
	}

	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges () {

		if (getInputConnected (GATE_INPUT)) {
			int channels = inputs[GATE_INPUT].getChannels();
			float lastValue = 0;
			for (int channel = 0; channel < POLY_CHANNELS; channel ++) {
				float value;
				if (channel > channels)
					value = lastValue;
				else {
					value = OL_statePoly[GATE_INPUT * POLY_CHANNELS + channel];
				}
				OL_isGatePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = value;
				lastValue = value;
			}
		} else {
			for (int channel = 0; channel < POLY_CHANNELS; channel ++)
				OL_isGatePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = 0.f;
		}

		for (int row = 0; row < NUM_ROWS; row++) {
			if (getStateJson (ONOFF_JSON + row) == 1.) {
				if (int(getStateJson (ACTIVE_PARAM_JSON + LEN * 4 + row)) == 1 && 
				    int(getStateJson (ACTIVE_PARAM_JSON + DUR * 4 + row)) == 1)
					setRgbLight (REP_LIGHT_RGB + (3 * row), ONOFF_COLOR_ON_INACTIVE);
				else
					setRgbLight (REP_LIGHT_RGB + (3 * row), ONOFF_COLOR_ON);
			}
			else
				setRgbLight (REP_LIGHT_RGB + (3 * row), ONOFF_COLOR_OFF);
		}

		if (getStateJson (GATE_JSON) == 1.)
			setRgbLight (GATE_LIGHT_RGB, GATE_COLOR_ON);
		else
			setRgbLight (GATE_LIGHT_RGB, GATE_COLOR_OFF);

		if (getStateJson (SH_JSON) == 1.)
			setRgbLight (SH_LIGHT_RGB, SH_COLOR_ON);
		else
			setRgbLight (SH_LIGHT_RGB, SH_COLOR_OFF);
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/
// Creates new circle arc shaped sub-path. The arc center is at cx,cy, the arc radius is r,
// and the arc is drawn from angle a0 to a1, and swept in direction dir (NVG_CCW, or NVG_CW).
// Angles are specified in radians.
// void nvgArc(NVGcontext* ctx, float cx, float cy, float r, float a0, float a1, int dir);

/**
	Widget to display cvOct values as floats or notes
*/
struct LeftWidget : TransparentWidget {

	std::shared_ptr<Font> pFont;

	Dejavu     *module = nullptr;
	int       paramDisplayCycles = 0;

	static LeftWidget* create (Dejavu *module) {
		LeftWidget *w = new LeftWidget();

		w->pFont    = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		w->box.pos  = mm2px (Vec (1.524, 36.831));
		w->box.size = mm2px (Vec (32.512, 10.668));
		// w->box.pos.y += w->box.size.y;
		w->module   = module;

		return w;
	}

	/**
		Constructor
	*/
	LeftWidget () {
	}

	bool redParam (int param) {
		float moduleState = module->getStateJson (MODULE_STATE_JSON);
		if (moduleState == STATE_EDIT_RANGES) {
			if ((param >= LEN_PARAM && param <= LEN_PARAM_END) ||
				(param >= DUR_PARAM && param <= DUR_PARAM_END) || param == -1)
				return true;
		}
		if (moduleState == STATE_EDIT_OFFSETS) {
			if ((param >= DUR_PARAM && param <= DUR_PARAM_END) || param == -1)
				return true;
		}
		return false;
	}

	void draw (const DrawArgs &drawArgs) override {
		if (module) {
			float style = module->getStateJson(STYLE_JSON);
			char headBuffer[18];
			char valueBuffer[18];
			headBuffer[17] = '\0';
			valueBuffer[17] = '\0';

			//nvgBeginPath(drawArgs.vg);
			//nvgFillColor (drawArgs.vg, nvgRGB(0, 0, 255));
			//nvgRect(drawArgs.vg, 0, 0, box.size.x, box.size.y);
			//nvgFill(drawArgs.vg);

			if (module->paramChanged) {
				paramDisplayCycles = PARAM_DISPLAY_CYCLES;
				module->paramChanged = false;
			} 
			float moduleState = module->getStateJson (MODULE_STATE_JSON);
			int param = module->lastParamChanged;
			if (paramDisplayCycles == 0)
				module->lastParamChanged = -1;
			if (paramDisplayCycles > 0) {
				ParamQuantity *pq = module->paramQuantities[param];
				const char *label = pq->label.data();
				const char *unit  = pq->unit.data();

				if (!redParam (param)) {
					if (*unit != '\0')
						snprintf (headBuffer, 17, "%s[%s]:", label, unit);
					else
						snprintf (headBuffer, 17, "%s:", label);
				}
				else {
					if (moduleState == STATE_EDIT_RANGES)
						snprintf (headBuffer, 17, "Max %s:", label);
					else
						snprintf (headBuffer, 17, "Offset %s:", label);
				}

				float value = module->getStateParam (param);
				if (param == SEED_PARAM)
					sprintf (valueBuffer, "%08lX", (unsigned long)(module->getStateParam (param)));
				else {
					if (value == float(int(value)))
						snprintf (valueBuffer, 17, "%8.0lf", module->getStateParam (param));
					else
						snprintf (valueBuffer, 17, "%8.3lf", module->getStateParam (param));
				}
				paramDisplayCycles --;
			}
			else {
				if (moduleState == STATE_ACTIVE) {
					if (module->p_srcRandomGenerator != nullptr) {
						strncpy (headBuffer, module->displayHeading, 17);
						sprintf (valueBuffer, "%08lX", module->p_srcRandomGenerator->latest_seed);
					}
					else {
						strcpy(valueBuffer, "null");
					}
				}
				else {
					snprintf (headBuffer,  17, "Edit:");
					if (moduleState == STATE_EDIT_RANGES)
						snprintf (valueBuffer, 17, " RANGES ");
					if (moduleState == STATE_EDIT_OFFSETS)
						snprintf (valueBuffer, 17, " OFFSETS");
				}
			}
			nvgFontFaceId (drawArgs.vg, pFont->handle);
			nvgFontSize (drawArgs.vg, 20);
			nvgFillColor (drawArgs.vg, redParam (param) ? RED : (style == STYLE_ORANGE ? ORANGE : WHITE));
			nvgText (drawArgs.vg, mm2px(2.447) - box.pos.x + mm2px(0.5), mm2px(41.283) - box.pos.y + mm2px(4.812), valueBuffer, nullptr);

			nvgFontSize (drawArgs.vg, 10);
			nvgText (drawArgs.vg, mm2px(0.5), mm2px(0.5) + mm2px(2.406), headBuffer, nullptr);
			//nvgText (drawArgs.vg, mm2px(2.447) - box.pos.x + mm2px(0.5), mm2px(41.283) - box.pos.y + mm2px(4.812), valueBuffer, nullptr);
		}
	}
};

struct RigthWidget : TransparentWidget {

	std::shared_ptr<Font> pFont;

	Dejavu     *module = nullptr;

	int flashFrameCounter[NUM_FLASHES] = 
		{0, 0, 0, 0, 	// [0..3]:	urLength hits (newSeeds)
		 0, 0, 0, 0,	// [4..7]: 	lenLength hits (reSeeds)
		 0};			//      8:	middle Circle flashing on dur Enf of top active row

	// int oldCounter[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	int flashDot[4] = {0, 0, 0, 0};

	bool clockwise = false;

	static RigthWidget* create (Dejavu *module) {
		RigthWidget *w = new RigthWidget();

		w->pFont    = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		w->box.pos  = mm2px (Vec (37.084, 8.891));
		w->box.size = mm2px (Vec (32.512, 38.608));
		w->module   = module;

		return w;
	}

	/**
		Constructor
	*/
	RigthWidget () {
	}

	inline float xForAlpha(float alpha) {
		if (!clockwise)
			return sin(alpha+PI);
		else
			return cos(alpha-PI/2);
	}

	inline float yForAlpha(float alpha) {
		if (!clockwise)
			return cos(alpha+PI);
		else
			return sin(alpha-PI/2);
	}

	void drawCircle(NVGcontext *nvg, float x, float y, float radius, NVGcolor color, float strokeWidth) {
		nvgBeginPath (nvg);
		nvgCircle (nvg, x, y, radius);

		nvgStrokeWidth (nvg, strokeWidth);
		if (strokeWidth == 0){ 
			nvgFillColor (nvg, color);
			nvgFill (nvg);
		}
		else {
			nvgStrokeColor (nvg, color);
			nvgStroke(nvg);
		}
	}

	unsigned char flashAlpha (int i, unsigned char alpha) {
		int a = alpha; 
		if (flashFrameCounter[i] > 0) {
			// DEBUG("Flashing for row i = %d, flashFrameCounter = %d", i, flashFrameCounter[i]);
			a += (float(255 - alpha) / FLASH_FRAMES) * flashFrameCounter[i];
			if (a > 255) 
				a = 255;
		}
		return a;
	}

	void decrementFlashCounters() {
		for (int i = 0; i < NUM_FLASHES; i++)
			if (flashFrameCounter[i] > 0)
				flashFrameCounter[i]--;
	}

	void draw (const DrawArgs &drawArgs) override {
		if (module) {
			if (module->getStateJson(MODULE_STATE_JSON) != STATE_ACTIVE)
				return;
			nvgGlobalCompositeOperation(drawArgs.vg, NVG_SOURCE_OVER);
			//nvgGlobalCompositeBlendFunc(drawArgs.vg, NVG_SRC_COLOR, NVG_ZERO);

			clockwise = (module->getStateJson(DIRECTION_JSON) == DIRECTION_CLOCKWISE);

			// float style = module->getStateJson(STYLE_JSON);

			//nvgBeginPath(drawArgs.vg);
			//nvgFillColor (drawArgs.vg, nvgRGB(0, 0, 255));
			//nvgRect(drawArgs.vg, 0, 0, box.size.x, box.size.y);
			//nvgFill(drawArgs.vg);

			NVGcolor dotColor[4] = {
				nvgRGB (0x00, 0xff, 0x00),
				nvgRGB (0xff, 0xff, 0x00),
				nvgRGB (0xff, 0x00, 0x00),
				nvgRGB (0xff, 0x00, 0xff)
			};
			NVGcolor trackColor[4] = {
				nvgRGB (0x00, 0xff, 0x00),
				nvgRGB (0xff, 0xff, 0x00),
				nvgRGB (0xff, 0x00, 0x00),
				nvgRGB (0xff, 0x00, 0xff)
			};
			NVGcolor armColor[4] = {
				nvgRGB (0x00, 0xff, 0x00),
				nvgRGB (0xff, 0xff, 0x00),
				nvgRGB (0xff, 0x00, 0x00),
				nvgRGB (0xff, 0x00, 0xff)
			};
			// NVGcolor midColor = nvgRGB (0x00, 0x00, 0xff);

			int dotAlpha = 125; // 200;
			int trkAlpha = 125; // 100;
			int armAlpha = 125; // 150;
			// int midAlpha =   0;

			float maxTrackRadius    = (box.size.x / 2) - mm2px(1.5f);
			float radiusDistance    = mm2px(2.5f);
			float radiusDot         = mm2px(1);
			float trackStrokeWidth  = mm2px(0.25);
			float armStrokeWidth[4] = { mm2px(0.75), mm2px(1), mm2px(1.5), mm2px(2) };

			// float radiusMid = maxTrackRadius - (4 * radiusDistance);
			Vec center = Vec(box.size.x / 2, box.size.y / 2);
			float rowRadius = maxTrackRadius - (3 * radiusDistance);

			int topActive = -1;
			for (int row = NUM_ROWS - 1;row >= 0; row--)
				if (module->rowActive (row)) {
					if (topActive == -1)
						topActive = row;
				}

			for (int row = NUM_ROWS - 1;row >= 0; row--) {

				if (module->rowActive (row)) {

					bool reverse = false;
					NVGcolor colorTrk = trackColor[reverse ? NUM_ROWS - 1 - row : row];
					NVGcolor colorDot =   dotColor[reverse ? NUM_ROWS - 1 - row : row];
					NVGcolor colorArm =   armColor[reverse ? NUM_ROWS - 1 - row : row];
					NVGcolor color;

					int durCounter = int(module->getStateJson(DUR_COUNTER_JSON + row));
					// int lenCounter = int(module->getStateJson(LEN_COUNTER_JSON + row));

					int duration = module->effectiveCount[row * 2 + DUR];
					int length   = module->effectiveCount[row * 2 + LEN];

					float cycles = (float(duration) / float(length));

					bool durEnd = false;
					// if (durCounter > oldCounter[row]) {
					if (module->flashEvent[(row*2)+DUR]) {
						flashFrameCounter[(row*2)+DUR] = FLASH_FRAMES;
						if (row == topActive)
							flashFrameCounter[8] = FLASH_FRAMES;	// flash the middle Circle also
						durEnd = true;
						module->flashEvent[(row*2)+DUR] = false;
					}
					// oldCounter[row] = durCounter;
	
					// if (lenCounter > oldCounter[row+NUM_ROWS]) { // lenCounter was reset
					if (module->flashEvent[(row*2)+LEN]) {
						if (durEnd)
							flashDot[row] = -1;	// flash all dots of circle just hit a durution end
						else
							flashDot[row] = (duration - durCounter) / length;
						flashFrameCounter[(row*2)+LEN] = FLASH_FRAMES;
						module->flashEvent[(row*2)+LEN] = false;
					}
					// oldCounter[row+NUM_ROWS] = lenCounter;

/*
					// Draw middle circle representing the global random generator
					if (row == topActive) {
						color = nvgTransRGBA (midColor, flashAlpha (8, midAlpha));
						drawCircle (drawArgs.vg, center.x, center.y, radiusMid, color, trackStrokeWidth);
					}
*/
					// Draw track circla
					color = nvgTransRGBA (colorTrk, trkAlpha);
					drawCircle (drawArgs.vg, center.x, center.y, rowRadius, color, trackStrokeWidth);
					// Draw dots on track circle
					float radAlpha  = (2 * PI) / cycles;
					for (int point = 0; point < cycles; point ++) {
						float x = xForAlpha (radAlpha * point) * rowRadius;
						float y = yForAlpha (radAlpha * point) * rowRadius;
						nvgBeginPath (drawArgs.vg);
						// nvgShapeAntiAlias(drawArgs.vg, false);
						NVGcolor color = nvgTransRGBA (colorDot, dotAlpha);
						// DEBUG("Level: %d, point = %d, flashDot[%d] = %d", row, point, row, flashDot[row]);
						if (point == flashDot[row] || flashDot[row] == -1)
							color = nvgTransRGBA (color, flashAlpha ((row*2)+LEN, dotAlpha));
						drawCircle(drawArgs.vg, x + center.x, y + center.y, radiusDot, color, 0);
					}

					// Draw the Arm of the clock for this row
					nvgBeginPath (drawArgs.vg);
					nvgMoveTo (drawArgs.vg, center.x, center.y);
					float progress = 1 - (float(durCounter) / float(duration));
					radAlpha = (2 * PI) * progress;
					nvgLineTo (drawArgs.vg, center.x + (xForAlpha(radAlpha) * rowRadius), center.y + (yForAlpha(radAlpha) * rowRadius));
					// nvgClosePath(NVGcontext* ctx);
					nvgLineCap (drawArgs.vg, NVG_ROUND);
					color = nvgTransRGBA (colorArm, flashAlpha ((row*2)+DUR, armAlpha));
					nvgStrokeColor (drawArgs.vg, color);
					nvgStrokeWidth (drawArgs.vg, armStrokeWidth[row]);
					nvgStroke (drawArgs.vg);
				}
				rowRadius += radiusDistance;
			}
			decrementFlashCounters();
		}
	}
};
/**
	Main Module Widget
*/
#define PANELHEIGHT 128.5f
#define OFFSET_PJ301MPort 4.2
#define OFFSET_Trimpot 3.15
#define OFFSET_RoundSmallBlackKnob 4.0
#define OFFSET_RoundHugeBlackKnob 9.5
#define OFFSET_LargeLight 4.762 / 2
#define OFFSET_LEDButton 4.762 / 2
#define OFFSET_RoundLargeBlackKnob 12.7 / 2

#define calculateCoordinates(x, y, offset) mm2px (Vec (x + offset ,  y + offset))

struct DejavuWidget : ModuleWidget {

	DejavuWidget(Dejavu *module) {
        RoundSmallBlackKnob *knob;
		LargeLight<RedGreenBlueLight> *light;
        setModule (module);
		setPanel (APP->window->loadSvg(asset::plugin (pluginInstance, "res/DejavuOrange.svg")));

		if (module) {
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DejavuBright.svg")));
			brightPanel->visible = false;
			
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DejavuDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}
		
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 3.415, 12.125, OFFSET_PJ301MPort), module,  RST_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 3.415, 22.212, OFFSET_PJ301MPort), module,  CLK_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (13.575, 12.125, OFFSET_PJ301MPort), module,  DIV_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (13.575, 22.212, OFFSET_PJ301MPort), module, SEED_INPUT));
		knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (23.940, 12.258, OFFSET_RoundSmallBlackKnob),  module, DIV_PARAM);
        knob->snap = true;
   		addParam (knob);
		knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (23.940, 22.418, OFFSET_RoundSmallBlackKnob),  module, SEED_PARAM);
        knob->snap = true;
		addParam (knob);

		for (int i = 0; i <  NUM_ROWS; i ++) {
			float y = 54.168 + (10.16 * i);
			float yb = 55.786 + (10.16 * i);
			knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (3.62, y, OFFSET_RoundSmallBlackKnob),  module, LEN_PARAM + i);
	        knob->snap = true;
			addParam (knob);
			addParam (createParamCentered<LEDButton> (calculateCoordinates (15.399, yb, OFFSET_LEDButton), module, ONOFF_PARAM + i));
			light = createLightCentered<LargeLight<RedGreenBlueLight>> (calculateCoordinates (15.399, yb, OFFSET_LargeLight), module, REP_LIGHT_RGB + (3 * i));
			light->bgColor = nvgRGBA(0, 0, 0, 255);
			addChild (light);
			knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (23.940, y, OFFSET_RoundSmallBlackKnob),  module, DUR_PARAM + i);
	        knob->snap = true;
			addParam (knob);
		}

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (3.415, 99.682, OFFSET_PJ301MPort), module, REP_INPUT));
		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (23.940, 99.682, OFFSET_PJ301MPort),  module, REP_OUTPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (3.415, 109.842, OFFSET_PJ301MPort), module, TRG_INPUT));
		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (23.940, 109.842, OFFSET_PJ301MPort),  module, TRG_OUTPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (38.975, 52.438, OFFSET_PJ301MPort), module, HEAT_INPUT ));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (59.295, 52.438, OFFSET_PJ301MPort), module, HEAT_KNOB_ATT_INPUT ));
		addParam (createParamCentered<RoundLargeBlackKnob> (calculateCoordinates (46.990, 57.659, OFFSET_RoundLargeBlackKnob),  module, HEAT_PARAM));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (39.180, 79.568, OFFSET_RoundSmallBlackKnob),  module, OFS_PARAM));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (49.340, 79.568, OFFSET_RoundSmallBlackKnob),  module, SCL_PARAM));
		addParam (createParamCentered<Trimpot> (calculateCoordinates ( 40.030, 90.577, OFFSET_Trimpot),  module, OFS_ATT_PARAM));
		addParam (createParamCentered<Trimpot> (calculateCoordinates ( 50.190, 90.577, OFFSET_Trimpot),  module, SCL_ATT_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (38.975, 99.682, OFFSET_PJ301MPort), module, OFS_INPUT ));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (49.135, 99.682, OFFSET_PJ301MPort), module, SCL_INPUT ));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (59.295, 89.522, OFFSET_PJ301MPort), module, SH_INPUT ));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (49.135, 109.842, OFFSET_PJ301MPort), module, GATE_INPUT ));
		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (59.295, 99.682, OFFSET_PJ301MPort),  module, CV_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (59.295, 109.842, OFFSET_PJ301MPort),  module, GATE_OUTPUT));

		addParam (createParamCentered<LEDButton>                   (calculateCoordinates (61.210, 81.186, OFFSET_LEDButton), module, SH_PARAM));
		light = createLightCentered<LargeLight<RedGreenBlueLight>> (calculateCoordinates (61.120, 81.186, OFFSET_LargeLight), module, SH_LIGHT_RGB);
		light->bgColor = nvgRGBA(0, 0, 0, 255);
	 	addChild (light);

		addParam (createParamCentered<LEDButton>                   (calculateCoordinates (40.800, 111.666, OFFSET_LEDButton), module, GATE_PARAM));
		light = createLightCentered<LargeLight<RedGreenBlueLight>> (calculateCoordinates (40.800, 111.666, OFFSET_LargeLight), module, GATE_LIGHT_RGB);
		light->bgColor = nvgRGBA(0, 0, 0, 255);
	 	addChild (light);

 		addChild (LeftWidget::create (module));
 		addChild (RigthWidget::create (module));

#ifdef USE_DEBUG_OUTPUT
		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (0, 0, OFFSET_PJ301MPort),  module, DEBUG_OUTPUT));
#endif
	}

    struct PolyChannelsItem : MenuItem {
    Dejavu *module;

   		struct PolyChannelItem : MenuItem {
        	Dejavu *module;
        	int channels;
        	void onAction(const event::Action &e) override {
           		module->OL_setOutState(POLY_CHANNELS_JSON, float(channels));
        	}
        	void step() override {
            	if (module)
            		rightText = (module != nullptr && module->OL_state[POLY_CHANNELS_JSON] == channels) ? "✔" : "";
        	}
    	};

        Menu *createChildMenu() override {
            Menu *menu = new Menu;
			PolyChannelItem *polyChannelItem;
			for (int channel = 0; channel < 16; channel++) {
				polyChannelItem = new PolyChannelItem ();
				polyChannelItem->module = module;
				polyChannelItem->channels = channel + 1;
				polyChannelItem->text = module->channelNumbers[channel];
				polyChannelItem->setSize (Vec(50, 20));
				menu->addChild(polyChannelItem);
			}
			return menu;
		}
    };

	struct DirectionItem : MenuItem {
		Dejavu *module;
		float direction;
		void onAction(const event::Action &e) override {
			module->setStateJson(DIRECTION_JSON, direction);
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->getStateJson(DIRECTION_JSON) == direction) ? "✔" : "";
		}
	};

	struct ModuleStateItem : MenuItem {
		Dejavu *module;
		int state;
		void onAction(const event::Action &e) override {
			module->setStateJson(MODULE_STATE_JSON, state);
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->getStateJson(MODULE_STATE_JSON) == state) ? "✔" : "";
		}
	};

	struct DejavuStyleItem : MenuItem {
		Dejavu *module;
		int style;
		void onAction(const event::Action &e) override {
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override {
		if (module) {
			MenuLabel *spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			Dejavu *module = dynamic_cast<Dejavu*>(this->module);
			assert(module);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			PolyChannelsItem *polyChannelsItem = new PolyChannelsItem();
			polyChannelsItem->module = module;
			polyChannelsItem->text = "Poly Channels";
			polyChannelsItem->rightText = RIGHT_ARROW;
			menu->addChild(polyChannelsItem);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			MenuLabel *visualLabel = new MenuLabel();
			visualLabel->text = "Visual";
			menu->addChild(visualLabel);

			MenuLabel *directionLabel = new MenuLabel();
			directionLabel->text = "Direction";
			menu->addChild(directionLabel);

			DirectionItem *directionItem1 = new DirectionItem ();
			directionItem1->module = module;
			directionItem1->direction = DIRECTION_CLOCKWISE;
			directionItem1->text = "Clockwise";
			directionItem1->setSize (Vec(50, 20));
			menu->addChild(directionItem1);

			DirectionItem *directionItem2 = new DirectionItem ();
			directionItem2->module = module;
			directionItem2->direction = DIRECTION_BACKWARD;
			directionItem2->text = "Backward";
			directionItem2->setSize (Vec(50, 20));
			menu->addChild(directionItem2);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			MenuLabel *moduleStateLabel = new MenuLabel();
			moduleStateLabel->text = "Module State";
			menu->addChild(moduleStateLabel);

			ModuleStateItem *moduleStateItem1 = new ModuleStateItem ();
			moduleStateItem1->module = module;
			moduleStateItem1->state = STATE_ACTIVE;
			moduleStateItem1->text = "Active";
			moduleStateItem1->setSize (Vec(50, 20));
			menu->addChild(moduleStateItem1);

			ModuleStateItem *moduleStateItem2 = new ModuleStateItem ();
			moduleStateItem2->module = module;
			moduleStateItem2->state = STATE_EDIT_RANGES;
			moduleStateItem2->text = "Edit Ranges";
			moduleStateItem2->setSize (Vec(50, 20));
			menu->addChild(moduleStateItem2);

			ModuleStateItem *moduleStateItem3 = new ModuleStateItem ();
			moduleStateItem3->module = module;
			moduleStateItem3->state = STATE_EDIT_OFFSETS;
			moduleStateItem3->text = "Edit Offsets";
			moduleStateItem3->setSize (Vec(50, 20));
			menu->addChild(moduleStateItem3);
/*
			ModuleStateItem *moduleStateItem4 = new ModuleStateItem ();
			moduleStateItem4->module = module;
			moduleStateItem4->state = STATE_EDIT_OFFSET_RANGES;
			moduleStateItem4->text = "Edit Offsets Ranges";
			moduleStateItem4->setSize (Vec(50, 20));
			menu->addChild(moduleStateItem4);
*/
			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			MenuLabel *styleLabel = new MenuLabel();
			styleLabel->text = "Style";
			menu->addChild(styleLabel);

			DejavuStyleItem *style1Item = new DejavuStyleItem();
			style1Item->text = "Orange";// 
			style1Item->module = module;
			style1Item->style= STYLE_ORANGE;
			menu->addChild(style1Item);

			DejavuStyleItem *style2Item = new DejavuStyleItem();
			style2Item->text = "Bright";// 
			style2Item->module = module;
			style2Item->style= STYLE_BRIGHT;
			menu->addChild(style2Item);
				
			DejavuStyleItem *style3Item = new DejavuStyleItem();
			style3Item->text = "Dark";// 
			style3Item->module = module;
			style3Item->style= STYLE_DARK;
			menu->addChild(style3Item);
		}
	}
};
Model *modelDejavu = createModel<Dejavu, DejavuWidget> ("Dejavu");
