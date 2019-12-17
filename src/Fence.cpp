/*
	Fence.cpp
 
	Code for the OrangeLine module Fence

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

#include "Fence.hpp"

struct Fence : Module {

	#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	float minLow = 0.f;
	float maxLow = 0.f;
	float defaultLow = 0.f;
	float minHigh = 0.f;
	float maxHigh = 0.f;
	float defaultHigh = 0.f;
	float minStep = 0.f;
	float maxStep = 0.f;
	float defaultStep = 0.f;
	float minRange = 0.f;
    float effectiveLow = 0.f;
	float oldEffectiveLow = effectiveLow;
    float effectiveHigh = 0.f;
    float oldEffectiveHigh = effectiveHigh;
	float effectiveStep = 0.f;
	float oldCvOut[POLY_CHANNELS];	//	Old value of cvOut to detect changes for triggering trgOut
	float oldCvIn[POLY_CHANNELS];	//	Old value of cvOut to detect changes of quantized input
	/*
		Hack for Making knobs display correctly
		When rescaling a knob and the value does not change, the knob will not be redrawn
		thus not reflecting the new knob position
		So we fake a real change and take it back the next process run
	*/
	bool knobFake = false;
	int  knobFakeResetCnt = -1;


	int link = LINK_NONE_INT;
	int mode = MODE_RAW_INT;

	float processLow = 0.f;
	float processHigh = 0.f;
	float processStep = 0.f;
	float cvOut = 0.f;

	int   trgChannels = 0;

	/*
		Variables used speed up processing
	*/
// ********************************************************************************************************************************
/*
	Initialization
*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Fence () {
		initializeInstance ();
	}

	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes () {
		setStateTypeParam (LINK_PARAM, STATE_TYPE_TRIGGER);
		setStateTypeParam (MODE_PARAM, STATE_TYPE_TRIGGER);
		setStateTypeParam (GATE_PARAM, STATE_TYPE_TRIGGER);

		setStateTypeInput        (TRG_INPUT, STATE_TYPE_TRIGGER);
		setCustomChangeMaskInput (CV_INPUT, CHG_CV_IN);
		setInPoly                (TRG_INPUT, true);

		setStateTypeOutput (TRG_OUTPUT, STATE_TYPE_TRIGGER);
		setOutPoly         (TRG_OUTPUT, true);

		setCustomChangeMaskInput (TRG_INPUT, CHG_TRG_IN);
		setInPoly                (CV_INPUT, true);

		setOutPoly (CV_OUTPUT, true);

		setStateTypeLight (LINK_LIGHT_RGB, LIGHT_TYPE_RGB    );  
		setStateTypeLight (MODE_LIGHT_RGB, LIGHT_TYPE_RGB    );  
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
		setJsonLabel (       MODE_JSON, "mode");

		setJsonLabel (    LOW_RAW_JSON, "rawLow");
		setJsonLabel (   HIGH_RAW_JSON, "rawHigh");
		setJsonLabel (   LINK_RAW_JSON, "rawLink");
		setJsonLabel (   STEP_RAW_JSON, "rawStep");

		setJsonLabel (    LOW_QTZ_JSON, "qtzLow");
		setJsonLabel (   HIGH_QTZ_JSON, "qtzHigh");
		setJsonLabel (   LINK_QTZ_JSON, "qtzLink");
		setJsonLabel (   STEP_QTZ_JSON, "qtzStep");

		setJsonLabel (   LOW_SHPR_JSON, "shprLow");
		setJsonLabel (  HIGH_SHPR_JSON, "shprHigh");
		setJsonLabel (  LINK_SHPR_JSON, "shprLink");
		setJsonLabel (  STEP_SHPR_JSON, "shprStep");

		setJsonLabel ( LOWCLAMPED_JSON, "lowClamped");
		setJsonLabel (HIGHCLAMPED_JSON, "highClamped");

		setJsonLabel (       LINK_JSON, "link");
		setJsonLabel ( LINK_DELTA_JSON, "linkDelta");
		setJsonLabel (       GATE_JSON, "gate");
		setJsonLabel (      STYLE_JSON, "style");

		#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig () {	
		configParam (          LOW_PARAM,  getMinLow  (), getMaxLow  (), getDefaultLow  (), getLowLabel ());
		configParam (         HIGH_PARAM,  getMinHigh (), getMaxHigh (), getDefaultHigh (), getHighLabel ());
		configParam (         STEP_PARAM,  getMinStep (), getMaxStep (), getDefaultStep (), getStepLabel ());
		configParam (         LINK_PARAM,            0.f,           1.f,               0.f, "Toggle Link");
		configParam (         MODE_PARAM,            0.f,           1.f,               0.f, "Toggle Mode");
		configParam (         GATE_PARAM,            0.f,           1.f,               0.f, "Toggle Trg/Gate");
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
		initializeForMode ();
		memset (oldCvOut, 0.f, sizeof (oldCvOut));
		memset ( oldCvIn, 0.f, sizeof ( oldCvIn));
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
		Currently called twice when add a module to patch ...
	*/
	void moduleReset () {

		setStateJson (       MODE_JSON,       DEFAULT_MODE);

		setStateJson (    LOW_RAW_JSON,    DEFAULT_LOW_RAW);
		setStateJson (   HIGH_RAW_JSON,   DEFAULT_HIGH_RAW);
		setStateJson (   LINK_RAW_JSON,   DEFAULT_LINK_RAW);
		setStateJson (   STEP_RAW_JSON,   DEFAULT_STEP_RAW);

		setStateJson (    LOW_QTZ_JSON,    DEFAULT_LOW_QTZ);
		setStateJson (   HIGH_QTZ_JSON,   DEFAULT_HIGH_QTZ);
		setStateJson (   LINK_QTZ_JSON,   DEFAULT_LINK_QTZ);
		setStateJson (   STEP_QTZ_JSON,   DEFAULT_STEP_QTZ);

		setStateJson (   LOW_SHPR_JSON,   DEFAULT_LOW_SHPR);
		setStateJson (  HIGH_SHPR_JSON,  DEFAULT_HIGH_SHPR);
		setStateJson (  LINK_SHPR_JSON,  DEFAULT_LINK_SHPR);
		setStateJson (  STEP_SHPR_JSON,  DEFAULT_STEP_SHPR);

		setStateJson ( LOWCLAMPED_JSON,                0.f);
		setStateJson (HIGHCLAMPED_JSON,                0.f);

		setStateJson (       GATE_JSON,                0.f);

		setStateJson (      STYLE_JSON,                0.f);

		setStateJson (       LINK_JSON, getForMode (DEFAULT_LINK_RAW, DEFAULT_LINK_QTZ, DEFAULT_LINK_SHPR));

		if (getStateJson (LINK_JSON) == LINK_RANGE) {
			float low  = getForMode ( DEFAULT_LOW_RAW,  DEFAULT_LOW_QTZ,  DEFAULT_LOW_SHPR);
			float high = getForMode (DEFAULT_HIGH_RAW, DEFAULT_HIGH_QTZ, DEFAULT_HIGH_SHPR);
			setStateJson ( LINK_DELTA_JSON, high - low);
		}
		else
			setStateJson ( LINK_DELTA_JSON, 0.f);
	}

// ********************************************************************************************************************************
/*
	Module specific utility methods
*/

	/**
		Method to get a value for current mode
	*/
	inline float getForMode (float raw_value, float qtz_value, float shpr_value) {
		switch (int(getStateJson (MODE_JSON))) {
			case MODE_RAW_INT:	return raw_value;
			case MODE_QTZ_INT:	return qtz_value;
			case MODE_SHPR_INT:	return shpr_value;
		}
		return 0.f; // just to calm down lint
	}
	/**
		Method to save a value for current mode
	*/
	inline void setForMode (float value, int rawIdx, int qtzIdx, int shprIdx) {
		switch (int(getStateJson (MODE_JSON))) {
			case MODE_RAW_INT:	setStateJson ( rawIdx, value); break;
			case MODE_QTZ_INT:	setStateJson ( qtzIdx, value); break;
			case MODE_SHPR_INT:	setStateJson (shprIdx, value); break;
		}
	}

	/**
		Method to select the correct labels for current link state
	*/
	inline const char *getForLink(const char *raw_str, const char *qtz_str, const char *shpr_str) {
		switch (int(getStateJson (LINK_JSON))) {
			case int(MODE_RAW):	 return raw_str;
			case int(MODE_QTZ):	 return qtz_str;
			case int(MODE_SHPR): return shpr_str;
		}
		return ""; // just to calm down lint
	}

	/**
		Calculate variables used multiple times in process
	*/
	inline void initializeForMode () {
		minLow      = getMinLow      ();
		maxLow      = getMaxLow      ();
		defaultLow  = getDefaultLow  ();
		minHigh     = getMinHigh     ();
		maxHigh     = getMaxHigh     ();
		defaultHigh = getDefaultHigh ();
		minStep     = getMinStep     ();
		maxStep     = getMaxStep     ();
		defaultStep = getDefaultStep ();
		minRange    = getMinRange    ();
	}

	/*
		Methods to retrieve min/max for low/high, for current mode
	*/
	inline float getMinLow      () { return getForMode (LOW_MIN_RAW,      LOW_MIN_QTZ,      LOW_MIN_SHPR);      }
	inline float getMaxLow      () { return getForMode (LOW_MAX_RAW,      LOW_MAX_QTZ,      LOW_MAX_SHPR);      }
	inline float getDefaultLow  () { return getForMode (DEFAULT_LOW_RAW,  DEFAULT_LOW_QTZ,  DEFAULT_LOW_SHPR);  }
	inline float getMinHigh     () { return getForMode (HIGH_MIN_RAW,     HIGH_MIN_QTZ,     HIGH_MIN_SHPR);     }
	inline float getMaxHigh     () { return getForMode (HIGH_MAX_RAW,     HIGH_MAX_QTZ,     HIGH_MAX_SHPR);     }
	inline float getDefaultHigh () { return getForMode (DEFAULT_HIGH_RAW, DEFAULT_HIGH_QTZ, DEFAULT_HIGH_SHPR); }
	inline float getMinStep     () { return getForMode (STEP_MIN_RAW,     STEP_MIN_QTZ,     STEP_MIN_SHPR);     }
	inline float getMaxStep     () { return getForMode (STEP_MAX_RAW,     STEP_MAX_QTZ,     STEP_MAX_SHPR);     }
	inline float getDefaultStep () { return getForMode (DEFAULT_STEP_RAW, DEFAULT_STEP_QTZ, DEFAULT_STEP_SHPR); }
	inline float getMinRange    () { return getForMode (MIN_RANGE_RAW,    MIN_RANGE_QTZ,    MIN_RANGE_SHPR);    }

	/**
		Function to get the color for link
	*/
	inline int getLinkColor (float link) {
		switch (int(link)) {
			case 0: return LINK_COLOR_NONE;
			case 1: return LINK_COLOR_RANGE;
			case 2: return LINK_COLOR_CENTER;
		}
		return LINK_COLOR_NONE; // just for savety
	}
	/**
		Function to get the color for mode
	*/
	inline int getModeColor (float link) {
		switch (int(mode)) {
			case 0: return MODE_COLOR_RAW;
			case 1: return MODE_COLOR_QTZ;
			case 2: return MODE_COLOR_SHPR;
		}
		return MODE_COLOR_RAW; // just for savety
	}
	/*
		Methods to get labels dependent from link
	*/
	inline const char * getLowLabel  () { return getForLink ( LABEL_LOW_LINK_NONE,  LABEL_LOW_LINK_RANGE,  LABEL_LOW_LINK_CENTER); }
	inline const char * getHighLabel () { return getForLink (LABEL_HIGH_LINK_NONE, LABEL_HIGH_LINK_RANGE, LABEL_HIGH_LINK_CENTER); }
	inline const char * getStepLabel () { return getForLink (LABEL_STEP_LINK_NONE, LABEL_STEP_LINK_RANGE, LABEL_STEP_LINK_CENTER); }

// ********************************************************************************************************************************
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

		bool lastWasTrigger = false;
		
		//
		//	Undo knob fakes
		//
		if (knobFakeResetCnt >= 0) {
			if (knobFakeResetCnt == 0) {
				setStateParam  (LOW_PARAM, getStateParam ( LOW_PARAM) - KNOB_FAKE_DELTA);
				setStateParam (HIGH_PARAM, getStateParam (HIGH_PARAM) - KNOB_FAKE_DELTA);
				setStateParam (STEP_PARAM, getStateParam (STEP_PARAM) - KNOB_FAKE_DELTA);
			}
			knobFakeResetCnt --;
		}

		if (changeJson (MODE_JSON))	//	Recalculate cached values on mode change
			initializeForMode ();

		if (changeJson (LINK_JSON)) {	//	Set linkDelta if link is switched on
			if (link == LINK_RANGE_INT) {
				setStateJson (LINK_DELTA_JSON, getStateParam (HIGH_PARAM) - getStateParam (LOW_PARAM));
			}
			setStateJson (LOWCLAMPED_JSON, 0.f);
			setStateJson (HIGHCLAMPED_JSON, 0.f);
		}

		/*
			Process cvIn
		*/
		/*
			Check whether we have to do anything at all
		*/
		bool run = 	customChangeBits & (CHG_CV_IN | CHG_TRG_IN);
		bool change =	changeInput ( LOW_INPUT) || changeInput (HIGH_INPUT) || changeInput (STEP_INPUT) ||
						changeParam ( LOW_PARAM) || changeParam (HIGH_PARAM) || changeParam (STEP_PARAM) || 
						changeJson  ( MODE_JSON) || !initialized;
		if (change) {
			processRangeKnobs ();
			determineEffectiveRange ();
			determineEffectiveStep ();
			processLow  = clamp (effectiveLow, minLow, maxHigh - minRange);
			processHigh = effectiveHigh >= processLow + minRange ? effectiveHigh : processLow + minRange;
			processStep = effectiveStep < minStep ? minStep : effectiveStep;
		}
		
		bool inConnected = getInputConnected (CV_INPUT);
		if (inConnected & (run || change)) {
			int channels = inputs[CV_INPUT].getChannels();
			trgChannels = inputs[TRG_INPUT].getChannels ();
			setOutPolyChannels(CV_OUTPUT, channels);
			setOutPolyChannels(TRG_OUTPUT, channels);
			bool trgConnected = getInputConnected (TRG_INPUT); 
			for (int channel = 0; channel < channels; channel++) {
				int cvInPolyIdx = CV_INPUT * POLY_CHANNELS + channel;
				int trgInPolyIdx = TRG_INPUT * POLY_CHANNELS + channel;
				int trgOutPolyIdx = TRG_OUTPUT * POLY_CHANNELS + channel;
				int cvOutPolyIdx = CV_OUTPUT * POLY_CHANNELS + channel;

				if ((!trgConnected && OL_inStateChangePoly[cvInPolyIdx]) || OL_inStateChangePoly[trgInPolyIdx] || (channel >= trgChannels  &&  lastWasTrigger) || change) {
					cvOut = OL_statePoly[cvInPolyIdx];
					if (channel < trgChannels) {
						lastWasTrigger = OL_inStateChangePoly[trgInPolyIdx];
					}
					if (mode == MODE_QTZ_INT)
						cvOut = quantize (cvOut);
					if (cvOut == oldCvIn[channel] && !(OL_inStateChangePoly[trgInPolyIdx] || lastWasTrigger) && !change)
						continue;				
					oldCvIn[channel]  = cvOut;
					if (cvOut > processHigh) {
						if (mode == MODE_QTZ_INT) {
							cvOut -= floor (cvOut - processHigh);
							if (cvOut > processHigh + PRECISION)
								cvOut -= 1.f;
						}
						else {
							cvOut -= floor ((cvOut - processHigh) / processStep) * processStep;
							if (cvOut > processHigh + PRECISION)
								cvOut -= processStep;
						}
					}
					if (cvOut < processLow - PRECISION) {
						if (mode == MODE_QTZ_INT) {
							cvOut += floor (processLow - cvOut);
							if (cvOut < processLow - PRECISION)
								cvOut += 1.f;
						}
						else {
							cvOut += floor ((processLow - cvOut) / processStep) * processStep;
							if (cvOut < processLow - PRECISION)
								cvOut += processStep;
						}
					}
					if (mode == MODE_QTZ_INT && cvOut > processHigh + PRECISION) {
						/*
							We didn't find the same note in our range
							We use note(processStep) as alternative note
						*/
						float dLow  = processLow - (cvOut - 1.f);
						float dHigh = cvOut - processHigh;
						int altNote = note (processStep);
						if (dLow > dHigh)
							while (note (cvOut) != altNote)
								cvOut -= SEMITONE;
						else {
							cvOut -= 1.f;
							while (note (cvOut) != altNote)
								cvOut += SEMITONE;
						}
					}
					/*
						In audio mode we rescale the output so that range is mapped to -5, +5V
					*/
					if (mode == MODE_SHPR_INT) {
						float range = processHigh - processLow;
						cvOut -=  processHigh - range / 2.f;	//	Move cvOut relatively to the center of the range representing 0V
						cvOut *= 2 * AUDIO_VOLTAGE / range;		//	Scale range
					}
					/*
						Set cvOut and trigger if changed
					*/
					if (mode == MODE_QTZ_INT)
						cvOut = quantize (cvOut);
					cvOut = clamp (cvOut, minLow, maxHigh);
					if (abs (cvOut - oldCvOut[channel]) > PRECISION) {
						if (OL_statePoly[NUM_INPUTS * POLY_CHANNELS + trgOutPolyIdx] != 10.f) {
							OL_statePoly[NUM_INPUTS * POLY_CHANNELS + trgOutPolyIdx] = 10.f;
							OL_outStateChangePoly[trgOutPolyIdx] = true;
						}
					}
					if (OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] != cvOut) {
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] = cvOut;
						OL_outStateChangePoly[cvOutPolyIdx] = true;
					}
					oldCvOut[channel] = cvOut;
				}
				else
					lastWasTrigger = false;				
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

		if (inChangeParam (MODE_PARAM))	//	User clicked on mode button
			setStateJson (MODE_JSON, float((int(getStateJson (MODE_JSON)) + 1) % 3));

		if (inChangeParam (LINK_PARAM))	//	User clicked on link button
			setStateJson (LINK_JSON, float((int(getStateJson (LINK_JSON)) + 1) % 3));

		if (inChangeParam (GATE_PARAM)) {	//	User clicked on tr/gt button
			if (getStateJson (GATE_JSON) == 0.f)
				setStateJson (GATE_JSON, 1.f);
			else {
				setStateJson (GATE_JSON, 0.f);
			}
		}

		if (changeJson (MODE_JSON)) {	//	Mode has changed. Restore low, high, step and link for new mode
			//
			switch (int(getStateJson (MODE_JSON))) {
				case MODE_RAW_INT:
					setStateParam ( LOW_PARAM, getStateJson ( LOW_RAW_JSON));
					setStateParam (HIGH_PARAM, getStateJson (HIGH_RAW_JSON));
					setStateParam (STEP_PARAM, getStateJson (STEP_RAW_JSON));
					setStateJson  ( LINK_JSON, getStateJson (LINK_RAW_JSON));
					break;
				case MODE_QTZ_INT:
					setStateParam ( LOW_PARAM, getStateJson ( LOW_QTZ_JSON));
					setStateParam (HIGH_PARAM, getStateJson (HIGH_QTZ_JSON));
					setStateParam (STEP_PARAM, getStateJson (STEP_QTZ_JSON));
					setStateJson  ( LINK_JSON, getStateJson (LINK_QTZ_JSON));
					break;
				case MODE_SHPR_INT:
					setStateParam ( LOW_PARAM, getStateJson ( LOW_SHPR_JSON));
					setStateParam (HIGH_PARAM, getStateJson (HIGH_SHPR_JSON));
					setStateParam (STEP_PARAM, getStateJson (STEP_SHPR_JSON));
					setStateJson  ( LINK_JSON, getStateJson (LINK_SHPR_JSON));
					break;
			}
		}
		else {
			/*
				If mode related settings have changed, we have to save them for mode
			*/
			if (changeParam (LOW_PARAM))
				setForMode (getStateParam ( LOW_PARAM),  LOW_RAW_JSON,  LOW_QTZ_JSON,  LOW_SHPR_JSON);
			if (changeParam (HIGH_PARAM))
				setForMode (getStateParam (HIGH_PARAM), HIGH_RAW_JSON, HIGH_QTZ_JSON, HIGH_SHPR_JSON);
			if (changeParam (STEP_PARAM))
				setForMode (getStateParam (STEP_PARAM), STEP_RAW_JSON, STEP_QTZ_JSON, STEP_SHPR_JSON);
			if (changeJson (LINK_JSON))
				setForMode (getStateJson  ( LINK_JSON), LINK_RAW_JSON, LINK_QTZ_JSON, LINK_SHPR_JSON);
		}
		/*
			Set member variables to be used in moduleProcess() and moduleReflectChanges () later on
		*/
		link = int(getStateJson (LINK_JSON));
		mode = int(getStateJson (MODE_JSON));
		/*
			Check gate button
		*/
		if (getStateJson (GATE_JSON) > 0.f)
			isGate (TRG_OUTPUT) = true;
		else
			isGate (TRG_OUTPUT) = false;
	}
	
	/**
		Method to handle linkage of range knobs depending on link state
	*/
	void processRangeKnobs () {
		/*
			In center mode low and high knobs are used for spread and center
			So no linkage between low and high knobs
		*/
		if (link == LINK_CENTER_INT)	return;

		float low    = getStateParam ( LOW_PARAM);
		float high   = getStateParam (HIGH_PARAM);
		bool  change = false;

		if (link == LINK_RANGE_INT) {
			/*
				Check for clamped state
			*/
			if (inChangeParam (LOW_PARAM) && getStateJson (LOWCLAMPED_JSON) > 0.f) {
				setStateJson (LOWCLAMPED_JSON, 0.f);
				setStateJson (LINK_DELTA_JSON, high - low);
			}
			if (inChangeParam (HIGH_PARAM) && getStateJson (HIGHCLAMPED_JSON) > 0.f) {
				setStateJson (HIGHCLAMPED_JSON, 0.f);
				setStateJson (LINK_DELTA_JSON, high - low);
			}
			/*
				Move high knob if user moved low knob and vice versa
			*/ 
			if (inChangeParam (HIGH_PARAM)) {
				low = clamp (high - getStateJson (LINK_DELTA_JSON), minLow, maxLow);	
				if (low != high - getStateJson (LINK_DELTA_JSON))
					setStateJson (LOWCLAMPED_JSON, 1.f);
				setStateParam (LOW_PARAM, low);
				change = true;
			}
			if (inChangeParam (LOW_PARAM)) {
				high = clamp (low + getStateJson (LINK_DELTA_JSON), minHigh, maxHigh);
				if (high != low + getStateJson (LINK_DELTA_JSON))
					setStateJson (HIGHCLAMPED_JSON, 1.f);
				setStateParam (HIGH_PARAM, high);
				change = true;
			}
		}
		/*
			Ensure that high is not lower than low for link mode != center
		*/
		if (link != LINK_CENTER_INT && high < low) {
			if (inChangeParam (LOW_PARAM)) {
				setStateParam (HIGH_PARAM, low);
				change = true;
			}
			else 
				if (inChangeParam (HIGH_PARAM)) {
					setStateParam (LOW_PARAM,  high);
					change = true;
				}
		}
		/*
			Save changes for mode
		*/
		if (change) {
			setForMode (getStateParam ( LOW_PARAM),  LOW_RAW_JSON,  LOW_QTZ_JSON,  LOW_SHPR_JSON);
			setForMode (getStateParam (HIGH_PARAM), HIGH_RAW_JSON, HIGH_QTZ_JSON, HIGH_SHPR_JSON);
		}
	}

	/**
		Method to determine new effective low value	
	*/
	void determineEffectiveRange () {
		effectiveLow  = getStateParam ( LOW_PARAM);
		effectiveHigh = getStateParam (HIGH_PARAM);

		if (getInputConnected (LOW_INPUT))
			effectiveLow += getStateInput (LOW_INPUT);

		if (getInputConnected (HIGH_INPUT))
			effectiveHigh += getStateInput (HIGH_INPUT);
		
		if (link == LINK_CENTER_INT) {
			float spread = effectiveLow;
			effectiveLow  = effectiveHigh - spread;
			effectiveHigh = effectiveHigh + spread;
		}
		if (mode == MODE_QTZ_INT) {
			effectiveLow = quantize (effectiveLow);
			effectiveHigh = quantize (effectiveHigh);
		}

		effectiveLow  = clamp (effectiveLow,  minLow,  maxLow);
		effectiveHigh = clamp (effectiveHigh, minHigh, maxHigh);
	}

	/**
		Method to determine new effective step value
	*/
	void determineEffectiveStep () {
		if (!(changeInput(STEP_INPUT) || effectiveLow != oldEffectiveLow || effectiveHigh != oldEffectiveHigh || changeParam (STEP_PARAM) || changeJson (MODE_JSON)))
			return;

		oldEffectiveLow = effectiveLow;
		oldEffectiveHigh = effectiveHigh;

		effectiveStep = getStateParam (STEP_PARAM);

		/*
			Add step cv input to effective step if step input is connected
		*/
		if (getInputConnected (STEP_INPUT))
			effectiveStep = effectiveStep + getStateInput (STEP_INPUT);

		if (mode == MODE_QTZ_INT)
			effectiveStep = float(note (effectiveStep)) / 12.f;	//	In QTZ mode step is quantized to semi tone values
		else
			if (mode == MODE_SHPR_INT)
				//	In shpr mode effective step is scaled to the effective range in SHPR mode
				effectiveStep *= (effectiveHigh - effectiveLow) / (maxStep - minStep);

		effectiveStep = clamp (effectiveStep, minStep, maxStep);	//	Ensure effective step to be in allowed range
	}

	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges () {
		/*
			Set link light and defaults for low and high
		*/
		if (!initialized || changeJson (LINK_JSON) || changeJson (MODE_JSON)) {

			setRgbLight (LINK_LIGHT_RGB, getLinkColor (link));

			switch (link) {
				case LINK_NONE_INT:
				case LINK_RANGE_INT:
					switch (mode) {
						case MODE_RAW_INT:
							reConfigParam ( LOW_PARAM,   LOW_MIN_RAW,    LOW_MAX_RAW,    DEFAULT_LOW_RAW, "Lower Bound");
							reConfigParam (HIGH_PARAM,  HIGH_MIN_RAW,   HIGH_MAX_RAW,   DEFAULT_HIGH_RAW, "Upper Bound");
							reConfigParam (STEP_PARAM,  STEP_MIN_RAW,   STEP_MAX_RAW,   DEFAULT_STEP_RAW, "Step");
							break;
						case MODE_QTZ_INT:
							reConfigParam ( LOW_PARAM,   LOW_MIN_QTZ,    LOW_MAX_QTZ,    DEFAULT_LOW_QTZ, "Lower Bound");
							reConfigParam (HIGH_PARAM,  HIGH_MIN_QTZ,   HIGH_MAX_QTZ,   DEFAULT_HIGH_QTZ, "Upper Bound");
							reConfigParam (STEP_PARAM,  STEP_MIN_QTZ,   STEP_MAX_QTZ,   DEFAULT_STEP_QTZ, "Step");
							break;
						case MODE_SHPR_INT:
							reConfigParam ( LOW_PARAM,  LOW_MIN_SHPR,   LOW_MAX_SHPR,   DEFAULT_LOW_SHPR, "Lower Bound");
							reConfigParam (HIGH_PARAM, HIGH_MIN_SHPR,  HIGH_MAX_SHPR,  DEFAULT_HIGH_SHPR, "Upper Bound");
							reConfigParam (STEP_PARAM, STEP_MIN_SHPR,  STEP_MAX_SHPR,  DEFAULT_STEP_SHPR, "Step");
							break;
					}
					/*
						When leaving link mode center we have to rescale low and high
					*/ 
					if (changeJson (LINK_JSON) && !changeJson (MODE_JSON) && link != LINK_RANGE_INT) {
						float low = getStateParam (LOW_PARAM);
						setStateParam (HIGH_PARAM, getStateParam (HIGH_PARAM) +      low);
						setStateParam ( LOW_PARAM, getStateParam (HIGH_PARAM) -  2 * low);
						setForMode (getStateParam ( LOW_PARAM),  LOW_RAW_JSON,  LOW_QTZ_JSON,  LOW_SHPR_JSON);
						setForMode (getStateParam (HIGH_PARAM), HIGH_RAW_JSON, HIGH_QTZ_JSON, HIGH_SHPR_JSON);
					}
					break;
				case LINK_CENTER_INT:
					switch (mode) {
						case MODE_RAW_INT:
							reConfigParam ( LOW_PARAM,           0.f,    LOW_MAX_RAW,    DEFAULT_LOW_RAW, "Spread");
							reConfigParam (HIGH_PARAM,  HIGH_MIN_RAW,   HIGH_MAX_RAW,                0.f, "Center");
							reConfigParam (STEP_PARAM,  STEP_MIN_RAW,   STEP_MAX_RAW,   DEFAULT_STEP_RAW, "Step");
							break;
						case MODE_QTZ_INT:
							reConfigParam ( LOW_PARAM,           0.f,    LOW_MAX_QTZ,    DEFAULT_LOW_QTZ, "Spread");
							reConfigParam (HIGH_PARAM,  HIGH_MIN_QTZ,   HIGH_MAX_QTZ,  float(1 / 12) / 2, "Center");
							reConfigParam (STEP_PARAM,  STEP_MIN_QTZ,   STEP_MAX_QTZ,   DEFAULT_STEP_QTZ, "Step");
							break;
						case MODE_SHPR_INT:
							reConfigParam ( LOW_PARAM,           0.f,   LOW_MAX_SHPR,       LOW_MAX_SHPR, "Spread");
							reConfigParam (HIGH_PARAM, HIGH_MIN_SHPR,  HIGH_MAX_SHPR,                0.f, "Center");
							reConfigParam (STEP_PARAM, STEP_MIN_SHPR,  STEP_MAX_SHPR,  DEFAULT_STEP_SHPR, "Step");
							break;
					}
					/*
						When entering link mode center we have to rescale low and high
					*/ 
					if (changeJson (LINK_JSON) && !changeJson (MODE_JSON) && link == LINK_CENTER_INT) {
						setStateParam ( LOW_PARAM, (getStateParam (HIGH_PARAM) - getStateParam (LOW_PARAM)) / 2.f);
						setStateParam (HIGH_PARAM,  getStateParam (HIGH_PARAM) - getStateParam (LOW_PARAM));
						setForMode (getStateParam ( LOW_PARAM),  LOW_RAW_JSON,  LOW_QTZ_JSON,  LOW_SHPR_JSON);
						setForMode (getStateParam (HIGH_PARAM), HIGH_RAW_JSON, HIGH_QTZ_JSON, HIGH_SHPR_JSON);
					}
					break;
			}
			/*
				Do a fake change to ensure VCV displaying knob correctly after rescaling the low knob
			*/
			setStateParam  (LOW_PARAM, getStateParam ( LOW_PARAM) + KNOB_FAKE_DELTA);
			setStateParam (HIGH_PARAM, getStateParam (HIGH_PARAM) + KNOB_FAKE_DELTA);
			setStateParam (STEP_PARAM, getStateParam (STEP_PARAM) + KNOB_FAKE_DELTA);
			knobFake = true;
		}
		/*
			Setlights
		*/
		if (!initialized || changeJson (MODE_JSON))
			setRgbLight (MODE_LIGHT_RGB, getModeColor (mode));

		setStateLight (GATE_LIGHT, getStateJson (GATE_JSON) * 255.f);
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

/**
	Widget to display cvOct values as floats or notes
*/
struct VOctWidget : TransparentWidget {

	static constexpr const char*  notes = "CCDDEFFGGAAB";
	static constexpr const char* sharps = " # #  # # # ";

	std::shared_ptr<Font> pFont;

	float *pValue = NULL;
	float  defaultValue = 0;
	float *pMode = NULL;
	char   str[8]; // Space for 7 Chars
	int    type = TYPE_VOCT;

	Fence *module;

	/**
		Constructor
	*/
	VOctWidget() {
		box.size = mm2px (Vec(26, 7));
		pFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		module = nullptr;
	}
	/**
		Static function to convert a cv to a string and writes it to the
		pointer address given by pStr.

		Make sure that there is at least 8 Bytes allocated to the string at this address !!!

		If noteMode is true, the cv will be quantized to the next semitone and displayed as note value
		else the cv is displayed as float with a maximum of 7 chars in the format '-99.999'
		If not in the range [-10,10] the string 'ERROR' will be
	*/
	static char* cv2Str (char *pStr, float cv, float mode, int type) {
		if (cv < -10. || cv > 10.) {
			strcpy (pStr, "ERROR");
		}
		else {
			if (mode == MODE_QTZ_INT) {
				int note = note (cv);
				if (type == TYPE_VOCT) {
					int octave = octave (cv);
					if (int(sizeof(str)) <= snprintf (pStr, sizeof(str), " %c%c%2d", notes[note], sharps[note], octave + 4))
						fprintf (stderr, "OrangeLine:cv2Str():Unxpected format overflow\n");
				}
				if (type == TYPE_STEP) {
					if (int(sizeof(str)) <= snprintf (pStr, sizeof(str), "  %c%c", notes[note], sharps[note]))
						fprintf (stderr, "OrangeLine:cv2Str():Unxpected format overflow\n");
				}
			}
			else {
				if (int(sizeof(str)) <= snprintf (pStr, sizeof(str), "% *.3f", 7, cv))
					fprintf (stderr, "OrangeLine:cv2Str():Unxpected format overflow\n");
			}
		}
		return pStr;
	}

	void draw (const DrawArgs &drawArgs) override {
		/*
			Knob rescale hack
		*/
		if (module != nullptr && module->knobFake) {
			module->knobFake = false;
			module->knobFakeResetCnt = KNOB_FAKE_STEPS;
		}
		nvgFontFaceId (drawArgs.vg, pFont->handle);
		nvgFontSize (drawArgs.vg, 18);
		nvgFillColor (drawArgs.vg, (module != nullptr ? module->getTextColor () : ORANGE));

		float value = pValue != NULL ? *pValue : defaultValue;
		float mode  = pMode  != NULL ? *pMode  : DEFAULT_MODE;

		float xOffset = 0;
		if (mode == MODE_QTZ) {
			xOffset = mm2px (2.25);
		}

		nvgText (drawArgs.vg, xOffset, 0, cv2Str (str, value, mode, type), NULL);
	}
};

/**
	Main Module Widget
*/
struct FenceWidget : ModuleWidget {

	//
	// create and initialize a note display widget
	//
	static VOctWidget* createVOctWidget(Vec pos, float *pValue, float defaultValue, float *pMode, int type, Fence *module) {
		VOctWidget *w = new VOctWidget ();

		w->box.pos = pos;
		w->pValue = pValue;
		w->defaultValue = defaultValue;
		w->pMode = pMode;
		w->type = type;
		w->module = module;

		return w;
	}

	FenceWidget(Fence *module) {

		setModule (module);
		setPanel (APP->window->loadSvg(asset::plugin (pluginInstance, "res/Fence.svg")));

		if (module) {
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/FenceBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/FenceDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec ( 3.276 + 5,    128.5 - 92.970 - 5)),    module, LOW_PARAM));
		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec (17.246 + 5,    128.5 - 92.970 - 5)),    module, HIGH_PARAM));
		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec ( 3.276 + 5,    128.5 - 57.568 - 5)),    module, STEP_PARAM));

		float *pLowValue  = (module != nullptr ? &(module->effectiveLow)  : nullptr);
		float *pHighValue = (module != nullptr ? &(module->effectiveHigh) : nullptr);
		float *pStepValue = (module != nullptr ? &(module->effectiveStep) : nullptr);

		float *pMode      = (module != nullptr ? &(module->getStateJson (MODE_JSON)) : nullptr);

		float mode;
		if (pMode != nullptr)
			mode = *pMode;
		else
		    mode = DEFAULT_MODE;

		float defaultLow  = (mode == MODE_QTZ ? DEFAULT_LOW_QTZ  : (mode == MODE_SHPR ?   DEFAULT_LOW_SHPR  : DEFAULT_LOW_RAW));
		float defaultHigh = (mode == MODE_QTZ ? DEFAULT_HIGH_QTZ : (mode == MODE_SHPR ?   DEFAULT_HIGH_SHPR : DEFAULT_HIGH_RAW));
		float defaultStep = (mode == MODE_QTZ ? DEFAULT_STEP_QTZ : (mode == MODE_SHPR ?   DEFAULT_STEP_SHPR : DEFAULT_STEP_RAW));

		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 - 113.252 - 0.25 )), pHighValue, defaultHigh, pMode, TYPE_VOCT, module));
		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 - 106.267 - 0.25 )), pLowValue,  defaultLow,  pMode, TYPE_VOCT, module));
		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 -  71.267 + 0.25 )), pStepValue, defaultStep, pMode, TYPE_STEP, module));

		addParam (createParamCentered<LEDButton>		(mm2px (Vec (12.858 + 2.38, 128.5 - 88.900 - 2.38)), module, LINK_PARAM));
 		addChild (createLightCentered<LargeLight<RedGreenBlueLight>>	(mm2px (Vec (12.858 + 2.38, 128.5 - 88.900 - 2.38)), module, LINK_LIGHT_RGB));
		
		addParam (createParamCentered<LEDButton>		(mm2px (Vec (20.638 + 2.38, 128.5 - 56.525 - 2.38)), module, MODE_PARAM));
 		addChild (createLightCentered<LargeLight<RedGreenBlueLight>>	(mm2px (Vec (20.638 + 2.38, 128.5 - 56.525 - 2.38)), module, MODE_LIGHT_RGB));
		
		addParam (createParamCentered<LEDButton>		(mm2px (Vec (20.638 + 2.38, 128.5 - 48.577 - 2.38)), module, GATE_PARAM));
 		addChild (createLightCentered<LargeLight<YellowLight>>	(mm2px (Vec (20.638 + 2.38, 128.5 - 48.577 - 2.38)), module, GATE_LIGHT));

		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.049 + 4.2 , 128.5 - 82.947 - 4.2)),  module, LOW_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec (18.019 + 4.2 , 128.5 - 82.947 - 4.2)),  module, HIGH_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 47.547 - 4.2)),  module, STEP_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 29.609 - 4.2)),  module, TRG_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 11.829 - 4.2)),  module, CV_INPUT));

		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (18.02  + 4.2 , 128.5 - 29.609 - 4.2)),  module, TRG_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (18.02  + 4.2 , 128.5 - 11.829 - 4.2)),  module, CV_OUTPUT));
	}

	struct FenceStyleItem : MenuItem {
		Fence *module;
		int style;
		void onAction(const event::Action &e) override {
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override {
			if (module)
				rightText = (module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override {
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Fence *module = dynamic_cast<Fence*>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		FenceStyleItem *style1Item = new FenceStyleItem();
		style1Item->text = "Orange";// 
		style1Item->module = module;
		style1Item->style= STYLE_ORANGE;
		menu->addChild(style1Item);

		FenceStyleItem *style2Item = new FenceStyleItem();
		style2Item->text = "Bright";// 
		style2Item->module = module;
		style2Item->style= STYLE_BRIGHT;
		menu->addChild(style2Item);
			
		FenceStyleItem *style3Item = new FenceStyleItem();
		style3Item->text = "Dark";// 
		style3Item->module = module;
		style3Item->style= STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelFence = createModel<Fence, FenceWidget>("Fence");
