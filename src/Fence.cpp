#include <string>
#include <stdio.h>

#include "plugin.hpp"

#include "Fence.hpp"

struct Fence : Module {

	// 
	// Static function to quantize a cv to a semitone value
	//
	static float quantize (float cv) {
		int cvX12  = round (cv * 12);
		return float (cvX12 % 12) / 12 + cvX12 / 12;
	}

	// 
	// Static function to get the note (quantized pitch modulo 1)
	//
	static float note (float cv) {
		int cvX12  = round (cv * 12);
		return float (cvX12 % 12) / 12;
	}

	//
	// Parameter Ids
	//
	enum ParamIds {
		//
		// Parameters not bound to any user interface component to save internal module state
		//
		H_INITIALIZED_PARAM,	// We store here whether this module has been initialized after adding it to a patch

		H_MODE_PARAM,			// Mode qtz/shpr/raw
		
		H_LOW_RAW_PARAM,		// knob values for min/max/step and link setting for raw mode (quantize and shpr off)
		H_HIGH_RAW_PARAM,
		H_LINK_RAW_PARAM,
		H_STEP_RAW_PARAM,
		
		H_LOW_QTZ_PARAM,		// knob values for min/max/step and link setting for qtz mode (quantize on and shpr off)
		H_HIGH_QTZ_PARAM,
		H_LINK_QTZ_PARAM,
		H_STEP_QTZ_PARAM,

		H_LOW_SHPR_PARAM,		// knob values for min/max/step and link setting for shpr mode (quantize off and shpr on)
		H_HIGH_SHPR_PARAM,
		H_LINK_SHPR_PARAM,
		H_STEP_SHPR_PARAM,

		H_LOWCLAMPED_PARAM,
		H_HIGHCLAMPED_PARAM,

		// Reserved Parameter Ids to keep backward compatibility with future releases
		// H_RESERVED_1, H_RESERVED_2 used up 
		H_RESERVED_3, H_RESERVED_4, H_RESERVED_5, H_RESERVED_6, H_RESERVED_7, H_RESERVED_8,
		
		//
		// Paramater for user interface components
		//
		LOW_PARAM,				// Range Low Knob
		HIGH_PARAM,				// Range High Knob
		LINK_PARAM,				// Range Link Button
		QTZ_PARAM,				// Quantize Button
		SHPR_PARAM,				// Shpr Button (Audio Mode)
		STEP_PARAM,				// Step Knob
		
		// Reserved Parameter Ids to keep backward compatibility with future releases
		P_RESERVED_1, P_RESERVED_2, P_RESERVED_3, P_RESERVED_4, P_RESERVED_5, P_RESERVED_6, P_RESERVED_7, P_RESERVED_8,
		
		NUM_PARAMS
	};

	//
	// Input Ids
	//
	enum InputIds {
		LOW_INPUT,				// Range Low CV Input
		HIGH_INPUT,				// Range High CV Input
		STEP_INPUT,				// Step CV Input
		TRG_INPUT,				// Trigger Input
		CV_INPUT,				// Cv In to process

		// Reserved Parameter Ids to keep backward compatibility with future releases
		I_RESERVED_1, I_RESERVED_2, I_RESERVED_3, I_RESERVED_4, I_RESERVED_5, I_RESERVED_6, I_RESERVED_7, I_RESERVED_8,

		NUM_INPUTS
	};

	//
	// Output Ids
	//
	enum OutputIds {
		TRG_OUTPUT,				// Trigger output for signaling Cv Out changes
		CV_OUTPUT,				// Cv Out

		// Reserved Parameter Ids to keep backward compatibility with future releases
		O_RESERVED_1, O_RESERVED_2, O_RESERVED_3, O_RESERVED_4, O_RESERVED_5, O_RESERVED_6, O_RESERVED_7, O_RESERVED_8,

		NUM_OUTPUTS
	};

	//
	// Ligh Ids
	//
	enum LightIds {
		QTZ_LIGHT,				// Light indicating quantized mode
		SHPR_LIGHT,				// Light indicating shpr (Audio mode)
		LINK_LIGHT,				// Light indicating range min/max linkage

		// Reserved Parameter Ids to keep backward compatibility with future releases
		L_RESERVED_1, L_RESERVED_2, L_RESERVED_3, L_RESERVED_4, L_RESERVED_5, L_RESERVED_6, L_RESERVED_7, L_RESERVED_8,

		NUM_LIGHTS
	};
	//
	// Cache Params as local variables to avoid getValue() calls where possible
	//
	float mode = DEFAULT_MODE;
	float oldMode = mode;

	float low  = (mode == MODE_QTZ ? Fence::quantize ( DEFAULT_LOW_QTZ) : (mode == MODE_SHPR ?  DEFAULT_LOW_SHPR :  DEFAULT_LOW_RAW));
	float high = (mode == MODE_QTZ ? Fence::quantize (DEFAULT_HIGH_QTZ) : (mode == MODE_SHPR ? DEFAULT_HIGH_SHPR : DEFAULT_HIGH_RAW));
	bool  link = (mode == MODE_QTZ ?                  DEFAULT_LINK_QTZ  : (mode == MODE_SHPR ? DEFAULT_LINK_SHPR : DEFAULT_LINK_RAW));
	float step = (mode == MODE_QTZ ? Fence::quantize (DEFAULT_STEP_QTZ) : (mode == MODE_SHPR ? DEFAULT_STEP_SHPR : DEFAULT_STEP_RAW));

	//
	// Input values and their old values from Inputs to detect changes	
    //
	float cvIn = 0;
	float lowCvIn = 0;
	float highCvIn = 0;
	float stepCvIn = 0;

	//
	// Sum of low knob and lowCvIn
	//
    float effectiveLow = low;
    float oldEffectiveLow = effectiveLow;
	//
	// Sum of high knob and highCvIn
	//
    float effectiveHigh = high;
    float oldEffectiveHigh = effectiveHigh;

	//
	// Old value of cvOut to detect changes for triggering trgOut
	//
	float oldCvOut = 0;

	// 
	// distance used to link low and high buttons
	//
	float linkDelta = high - low;
	bool lowClamped = false;
	bool highClamped = false;

	float effectiveStep = step;
	float oldEffectiveStep = effectiveStep;

	//
	// Triggers for Trigger Inputs
	//
	Trigger trgIn;

	//
	// Triggers for Buttons
	//
	Trigger btnQtz;
	Trigger btnLink;
	Trigger btnShpr;

	dsp::PulseGenerator trgOutPulse;
	double sampleTime;

	unsigned long changeBits = CHG_ALL;

	//
	// remember whether we are already initialized after (re)loading
	//
	bool initialized = false;

	bool presetLoaded = false;
	//
	// initialize param configs
	//
	void initParamConfig() {
		float minValue;
		float maxValue;
		float defaultValue;

		//
		// Config internal Parameters not bound to a user interface object
		//
		configParam (H_INITIALIZED_PARAM,           0.f,           1.f,                0.f, "");

		configParam (       H_MODE_PARAM,      MODE_MIN,      MODE_MAX,       DEFAULT_MODE, "");

		configParam (    H_LOW_RAW_PARAM,   LOW_MIN_RAW,   LOW_MAX_RAW,    DEFAULT_LOW_RAW, "");
		configParam (   H_HIGH_RAW_PARAM,  HIGH_MIN_RAW,  HIGH_MAX_RAW,   DEFAULT_HIGH_RAW, "");
		configParam (   H_LINK_RAW_PARAM,           0.f,           1.f,   DEFAULT_LINK_RAW, "");
		configParam (   H_STEP_RAW_PARAM,  STEP_MIN_RAW,  STEP_MAX_RAW,   DEFAULT_STEP_RAW, "");

		configParam (    H_LOW_QTZ_PARAM,   LOW_MIN_QTZ,   LOW_MAX_QTZ,    DEFAULT_LOW_QTZ, "");
		configParam (   H_HIGH_QTZ_PARAM,  HIGH_MIN_QTZ,  HIGH_MAX_QTZ,   DEFAULT_HIGH_QTZ, "");
		configParam (   H_LINK_QTZ_PARAM,           0.f,           1.f,   DEFAULT_LINK_QTZ, "");
		configParam (   H_STEP_QTZ_PARAM,  STEP_MIN_QTZ,  STEP_MAX_QTZ,   DEFAULT_STEP_QTZ, "");

		configParam (   H_LOW_SHPR_PARAM,  LOW_MIN_SHPR,  LOW_MAX_SHPR,   DEFAULT_LOW_SHPR, "");
		configParam (  H_HIGH_SHPR_PARAM, HIGH_MIN_SHPR, HIGH_MAX_SHPR,  DEFAULT_HIGH_SHPR, "");
		configParam (  H_LINK_SHPR_PARAM,           0.f,           1.f,  DEFAULT_LINK_SHPR, "");
		configParam (  H_STEP_SHPR_PARAM, STEP_MIN_SHPR,  STEP_MAX_RAW,  DEFAULT_STEP_SHPR, "");

		configParam (H_HIGHCLAMPED_PARAM,           0.f,           1.f,                0.f, "");
		configParam ( H_LOWCLAMPED_PARAM,           0.f,           1.f,                0.f, "");

		//
		// GUI Parameters
		//
		minValue     = (mode == MODE_QTZ ?     LOW_MIN_QTZ : (mode == MODE_SHPR ?       LOW_MIN_SHPR : LOW_MIN_RAW)); 
		maxValue     = (mode == MODE_QTZ ?     LOW_MAX_QTZ : (mode == MODE_SHPR ?       LOW_MAX_SHPR : LOW_MAX_RAW));

		defaultValue = (mode == MODE_QTZ ? DEFAULT_LOW_QTZ : (mode == MODE_SHPR ?   DEFAULT_LOW_SHPR : DEFAULT_LOW_RAW));

		configParam (          LOW_PARAM,     minValue,       maxValue,       defaultValue, "Lower Bound");

		// min/max are the same as for LOW_PARAM
		defaultValue = (mode == MODE_QTZ ? DEFAULT_HIGH_QTZ : (mode == MODE_SHPR ? DEFAULT_HIGH_SHPR : DEFAULT_HIGH_RAW));

		configParam (         HIGH_PARAM,     minValue,       maxValue,       defaultValue, "Upper Bound");

		minValue     = (mode == MODE_QTZ ?     STEP_MIN_QTZ : (mode == MODE_SHPR ?     STEP_MIN_SHPR : STEP_MIN_RAW)); 
		maxValue     = (mode == MODE_QTZ ?     STEP_MAX_QTZ : (mode == MODE_SHPR ?     STEP_MAX_SHPR : STEP_MAX_RAW));
		defaultValue = (mode == MODE_QTZ ? DEFAULT_STEP_QTZ : (mode == MODE_SHPR ? DEFAULT_STEP_SHPR : DEFAULT_STEP_RAW));

		configParam (         STEP_PARAM,      minValue,      maxValue,       defaultValue, "Step");
		
		configParam (          QTZ_PARAM,           0.f,           1.f,                0.f, "Toggle Quantize On/Off");
		configParam (         LINK_PARAM,           0.f,           1.f,                0.f, "Toggle Link Range On/Off");
		configParam (         SHPR_PARAM,           0.f,           1.f,                0.f, "Toggle Shpr On/Off");
	}

	//
	// initialize hidden parameter values not reset by Rack itself as it does with ui components like knobs
	// and local variables whenuser choses initialize from right click menu
	//
	void initializeFromMenu () {

		params[H_INITIALIZED_PARAM].setValue(               1.0f);

		params[       H_MODE_PARAM].setValue(       DEFAULT_MODE);
		
		params[    H_LOW_RAW_PARAM].setValue(    DEFAULT_LOW_RAW);
		params[   H_HIGH_RAW_PARAM].setValue(   DEFAULT_HIGH_RAW);
		params[   H_LINK_RAW_PARAM].setValue(   DEFAULT_LINK_RAW);
		params[   H_STEP_RAW_PARAM].setValue(   DEFAULT_STEP_RAW);

		params[    H_LOW_QTZ_PARAM].setValue(    DEFAULT_LOW_QTZ);
		params[   H_HIGH_QTZ_PARAM].setValue(   DEFAULT_HIGH_QTZ);
		params[   H_LINK_QTZ_PARAM].setValue(   DEFAULT_LINK_QTZ);
		params[   H_STEP_QTZ_PARAM].setValue(   DEFAULT_STEP_QTZ);

		params[   H_LOW_SHPR_PARAM].setValue(   DEFAULT_LOW_SHPR);
		params[  H_HIGH_SHPR_PARAM].setValue(  DEFAULT_HIGH_SHPR);
		params[  H_LINK_SHPR_PARAM].setValue(  DEFAULT_LINK_SHPR);
		params[  H_STEP_SHPR_PARAM].setValue(  DEFAULT_STEP_SHPR);

		params[H_HIGHCLAMPED_PARAM].setValue(                0.f);
		params[ H_LOWCLAMPED_PARAM].setValue(                0.f);

		mode = DEFAULT_MODE;
		oldMode = mode;

		low  = (mode == MODE_QTZ ? Fence::quantize ( DEFAULT_LOW_QTZ) : (mode == MODE_SHPR ?  DEFAULT_LOW_SHPR :  DEFAULT_LOW_RAW));
		high = (mode == MODE_QTZ ? Fence::quantize (DEFAULT_HIGH_QTZ) : (mode == MODE_SHPR ? DEFAULT_HIGH_SHPR : DEFAULT_HIGH_RAW));
		link = (mode == MODE_QTZ ?                  DEFAULT_LINK_QTZ  : (mode == MODE_SHPR ? DEFAULT_LINK_SHPR : DEFAULT_LINK_RAW));
		step = (mode == MODE_QTZ ? Fence::quantize (DEFAULT_STEP_QTZ) : (mode == MODE_SHPR ? DEFAULT_STEP_SHPR : DEFAULT_STEP_RAW));

		cvIn = 0;
		lowCvIn = 0;
		highCvIn = 0;
		stepCvIn = 0;

    	effectiveLow = low;
    	oldEffectiveLow = effectiveLow;
    	effectiveHigh = high;
    	oldEffectiveHigh = effectiveHigh;

		oldCvOut = 0;

		linkDelta = high - low;
		lowClamped = false;
		highClamped = false;

		effectiveStep = step;
		oldEffectiveStep = effectiveStep;

		changeBits = CHG_ALL;

		// module initialize still has to be done
		initialized = false;
	}

	//
	// Constructor
	//
	Fence () {
		config (NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		initParamConfig();
	}

	//
	// Function to switch between raw/qtz/shpr modes
	//
	void setMode (int newMode) {
		switch (newMode) {
			case int(MODE_RAW):
				low  = params[  H_LOW_RAW_PARAM].getValue ();
				high = params[ H_HIGH_RAW_PARAM].getValue ();
				step = params[ H_STEP_RAW_PARAM].getValue ();
				link = params[ H_LINK_RAW_PARAM].getValue ();
				configParam ( LOW_PARAM,  LOW_MIN_RAW,   LOW_MAX_RAW,   DEFAULT_LOW_RAW, "Lower Bound");
				configParam (HIGH_PARAM, HIGH_MIN_RAW,  HIGH_MAX_RAW,  DEFAULT_HIGH_RAW, "Upper Bound");
				configParam (STEP_PARAM, STEP_MIN_RAW,  STEP_MAX_RAW,  DEFAULT_STEP_RAW, "Step");
				params[ LOW_PARAM].setValue (low);
				params[HIGH_PARAM].setValue (high);
				params[STEP_PARAM].setValue (step);
				lights[ QTZ_LIGHT].value = 0;
				lights[SHPR_LIGHT].value = 0;
				break;
			case int(MODE_QTZ):
				low  = params[  H_LOW_QTZ_PARAM].getValue ();
				high = params[ H_HIGH_QTZ_PARAM].getValue ();
				step = params[ H_STEP_QTZ_PARAM].getValue ();
				link = params[ H_LINK_QTZ_PARAM].getValue ();

				configParam ( LOW_PARAM,  LOW_MIN_QTZ,   LOW_MAX_QTZ,   DEFAULT_LOW_QTZ, "Lower Bound");
				configParam (HIGH_PARAM, HIGH_MIN_QTZ,  HIGH_MAX_QTZ,  DEFAULT_HIGH_QTZ, "Upper Bound");
				configParam (STEP_PARAM, STEP_MIN_QTZ,  STEP_MAX_QTZ,  DEFAULT_STEP_QTZ, "Step");
				params[ LOW_PARAM].setValue (low);
				params[HIGH_PARAM].setValue (high);
				params[STEP_PARAM].setValue (step);
				lights[ QTZ_LIGHT].value = 1.f;
				lights[SHPR_LIGHT].value = 0.f;
				break;
			case int(MODE_SHPR):
				low  = params[ H_LOW_SHPR_PARAM].getValue ();
				high = params[H_HIGH_SHPR_PARAM].getValue ();
				step = params[H_STEP_SHPR_PARAM].getValue ();
				link = params[H_LINK_SHPR_PARAM].getValue ();
				configParam ( LOW_PARAM,  LOW_MIN_SHPR,  LOW_MAX_SHPR,  DEFAULT_LOW_SHPR, "Lower Bound");
				configParam (HIGH_PARAM, HIGH_MIN_SHPR, HIGH_MAX_SHPR, DEFAULT_HIGH_SHPR, "Upper Bound");
				configParam (STEP_PARAM, STEP_MIN_SHPR, STEP_MAX_SHPR, DEFAULT_STEP_SHPR, "Step");
				params[ LOW_PARAM].setValue (low);
				params[HIGH_PARAM].setValue (high);
				params[STEP_PARAM].setValue (step);
				lights[ QTZ_LIGHT].value = 0.f;
				lights[SHPR_LIGHT].value = 1.f;
				break;
		}

		lights[LINK_LIGHT].value = link;

		lowClamped = false;
		params[H_LOWCLAMPED_PARAM].setValue(0.f);
		highClamped = false;
		params[H_HIGHCLAMPED_PARAM].setValue(0.f);

		mode = oldMode = newMode;
		if (params[H_MODE_PARAM].getValue () != mode)
			params[H_MODE_PARAM].setValue (float(mode));
	}

	//
	// Callback for RightClick initialize
	//
	void onReset () override {
		initializeFromMenu ();
	}

	//
	// Method to initialize the module when added to a patch
	// Currently does nothing maybe remove later
	//
	unsigned long initNew (unsigned long changeBits) {
		if (params[H_INITIALIZED_PARAM].getValue () == 1.f) 
			// already initialized, nothing to do
			return changeBits;

		params[H_INITIALIZED_PARAM].setValue (1.f);
		return changeBits;
	}

	//
	// Method to initialize the module when loading a patch
	//
	unsigned long postLoad (unsigned long changeBits) {
		if (!initialized || presetLoaded) {
			//
			// Restore params for active mode
			//
			setMode (params[H_MODE_PARAM].getValue ());

			lowClamped  = (params[ H_LOWCLAMPED_PARAM].getValue () > 0.f);
			highClamped = (params[H_HIGHCLAMPED_PARAM].getValue () > 0.f);	

			presetLoaded = false;
			initialized = true;
		}
		return changeBits;
	}

	unsigned long processLow (float value, bool doSetParam) {
		if (low != value) {
			//
			// Save low value for active mode
			//
			switch (int(mode)) {
				case int(MODE_RAW):
					params[H_LOW_RAW_PARAM].setValue (value);
					break;
				case int(MODE_QTZ):
					params[H_LOW_QTZ_PARAM].setValue (value);
					break;
				case int(MODE_SHPR):
					params[H_LOW_SHPR_PARAM].setValue (value);
					break;
			}

			low = value;

			//
			// when called from a foreign change handler we have to set the param
			// 
			if (doSetParam) {
				params[LOW_PARAM].setValue (low);
			}

			return CHG_LOW;
		}
		return 0;
	}

	unsigned long processHigh (float value, bool doSetParam) {
		if (high != value) {
			//
			// Save high value for active mode
			//
			switch (int(mode)) {
				case int(MODE_RAW):
					params[H_HIGH_RAW_PARAM].setValue (value);
					break;
				case int(MODE_QTZ):
					params[H_HIGH_QTZ_PARAM].setValue (value);
					break;
				case int(MODE_SHPR):
					params[H_HIGH_SHPR_PARAM].setValue (value);
					break;
			}
			high = value;

			//
			// when called from a foreign change handler we have to set the param
			// 
			if (doSetParam)
				params[HIGH_PARAM].setValue (high);

			return CHG_HIGH;
		}
		return 0;
	}

	unsigned long processStep(float value, bool doSetParam) {
		//
		// Save step value for active mode
		//
		if (step != value) {
			switch (int(mode)) {
				case int(MODE_RAW):
					params[H_STEP_RAW_PARAM].setValue (value);
					break;
				case int(MODE_QTZ):
					params[H_STEP_QTZ_PARAM].setValue (value);
					break;
				case int(MODE_SHPR):
					params[H_STEP_SHPR_PARAM].setValue (value);
					break;
			}
			step = value;

			//
			// when called from a foreign change handler we have to set the param
			// 
			if (doSetParam)
				params[STEP_PARAM].setValue (high);

			return CHG_STEP;
		}
		return 0;
	}

	//
	// Method to check for user interactions
	//
	unsigned long checkUserInteraction (unsigned long changeBits) {
		//
		// Check for qtz button click
		//
		if (btnQtz.process (params[QTZ_PARAM].getValue ())) {
			if (mode != MODE_QTZ)
				mode = MODE_QTZ;
			else
				mode = MODE_RAW;

			changeBits |= CHG_MODE;
		}

		//
		// Check for shpr button click
		//
		if (btnShpr.process (params[SHPR_PARAM].getValue ())) {
			if (mode != MODE_SHPR)
				mode = MODE_SHPR;
			else
				mode = MODE_RAW;

			changeBits |= CHG_MODE;
		}

		//
		// Check for link button click
		//
		if (btnLink.process (params[LINK_PARAM].getValue ())) {
			float value;
			
			link = !link;
			
			if (link)
				value = 1.f;
			else
				value = 0.f;

			lights[LINK_LIGHT].value = value;

			//
			// Save link state for active mode
			//			
			switch(int(mode)) {
				case int(MODE_RAW):
					params[H_LINK_RAW_PARAM].setValue (value);
					break;
				case int(MODE_QTZ):
					params[H_LINK_QTZ_PARAM].setValue (value);
					break;
				case int(MODE_SHPR):
					params[H_LINK_SHPR_PARAM].setValue (value);
					break;
			}

			changeBits |= CHG_LINK;
		}

		//
		// Get low from knob
		//
		changeBits |=  processLow (params[ LOW_PARAM].getValue (), false);

		//
		// Get high from knob
		//
		changeBits |= processHigh (params[HIGH_PARAM].getValue (), false);

		//
		// Get step from knob
		//
		changeBits |= processStep (params[STEP_PARAM].getValue (), false);

		return changeBits;
	}

	unsigned long processLowCvIn (float value) {
		if (lowCvIn != value) {
			lowCvIn = value;
			return CHG_LOW_CV;
		}
		return 0x0;
	}

	unsigned long processHighCvIn (float value) {
		if (highCvIn != value) {
			highCvIn = value;
			return CHG_HIGH_CV;
		}
		return 0x0;
	}

	unsigned long processStepCvIn (float value) {
		if (stepCvIn != value) {
			stepCvIn = value;
			return CHG_STEP_CV;
		}
		return 0;
	}

	unsigned long processCvIn (float value) {
		if (cvIn != value) {
			cvIn = value;
			return CHG_CV;
		}
		return 0x0;
	}
	//
	// Method to check for input changes
	//
	unsigned long checkInputs (unsigned long changeBits) {
		//
		// LowCvIn
		//
		if (inputs[LOW_INPUT].isConnected ())
			changeBits |= processLowCvIn (inputs[LOW_INPUT].getVoltage ());
		//
		// HighCvIn
		//
		if (inputs[HIGH_INPUT].isConnected ())
			changeBits |= processHighCvIn (inputs[HIGH_INPUT].getVoltage ());
		//
		// StepCvIn
		//
		if (inputs[STEP_INPUT].isConnected ())
			changeBits |= processStepCvIn (inputs[STEP_INPUT].getVoltage ());
		//
		// cvIn
		//
		if (inputs[CV_INPUT].isConnected ())
			changeBits |= processCvIn (inputs[CV_INPUT].getVoltage ());

		return changeBits;
	}

	//
	// Method to retrieve minimum low value for mode
	//
	float getMinLow () {
		float minValue = 0;
		switch (int(mode)) {
			case int(MODE_RAW):
				minValue = LOW_MIN_RAW;
				break;
			case int(MODE_SHPR):
				minValue = LOW_MIN_SHPR;
				break;
			case int(MODE_QTZ):
				minValue = LOW_MIN_QTZ;
				break;
		}
		return minValue;
	}

	//
	// Method to retrieve maximum low value for mode
	//
	float getMaxLow () {
		float maxValue = 0;
		switch (int(mode)) {
			case int(MODE_RAW):
				maxValue = LOW_MAX_RAW;
				break;
			case int(MODE_SHPR):
				maxValue = LOW_MAX_SHPR;
				break;
			case int(MODE_QTZ):
				maxValue = LOW_MAX_QTZ;
				break;
		}
		return maxValue;
	}

	//
	// Method to retrieve minimum high value for mode
	//
	float getMinHigh () {
		float minValue = 0;
		switch (int(mode)) {
			case int(MODE_RAW):
				minValue = HIGH_MIN_RAW;
				break;
			case int(MODE_SHPR):
				minValue = HIGH_MIN_SHPR;
				break;
			case int(MODE_QTZ):
				minValue = HIGH_MIN_QTZ;
				break;
		}
		return minValue;
	}

	//
	// Method to retrieve maximum low value for mode
	//
	float getMaxHigh () {
		float maxValue = 0;
		switch (int(mode)) {
			case int(MODE_RAW):
				maxValue = HIGH_MAX_RAW;
				break;
			case int(MODE_SHPR):
				maxValue = HIGH_MAX_SHPR;
				break;
			case int(MODE_QTZ):
				maxValue = HIGH_MAX_QTZ;
				break;
		}
		return maxValue;
	}

	//
	// Method to retrieve minimum step value for mode
	//
	float getMinStep () {
		float minValue = 0;
		switch (int(mode)) {
			case int(MODE_RAW):
				minValue = STEP_MIN_RAW;
				break;
			case int(MODE_SHPR):
				minValue = STEP_MIN_SHPR;
				break;
			case int(MODE_QTZ):
				minValue = STEP_MIN_QTZ;
				break;
		}
		return minValue;
	}

	//
	// Method to retrieve maximum step value for mode
	//
	float getMaxStep () {
		float maxValue = 0;
		switch (int(mode)) {
			case int(MODE_RAW):
				maxValue = STEP_MAX_RAW;
				break;
			case int(MODE_SHPR):
				maxValue = STEP_MAX_SHPR;
				break;
			case int(MODE_QTZ):
				maxValue = STEP_MAX_QTZ;
				break;
		}
		return maxValue;
	}

	float getLowClamped (float value) {
		float lowClamped = value;
		float minValue = getMinLow ();
		float maxValue = getMaxLow ();
		if (value < minValue) lowClamped = minValue;
		if (value > maxValue) lowClamped = maxValue;
		return lowClamped;
	}

	float getHighClamped (float value) {
		float highClamped = value;
		float minValue = getMinHigh ();
		float maxValue = getMaxHigh ();
		if (value < minValue) highClamped = minValue;
		if (value > maxValue) highClamped = maxValue;
		return highClamped;
	}

	float getEffectiveStepClamped (float value) {
		float stepClamped = value;
		float minValue = getMinStep ();
		float maxValue = getMaxStep ();
		if (value < minValue) stepClamped = minValue;
		if (value > maxValue) stepClamped = maxValue;
		return stepClamped;
	}

	//
	// Method to determine new effective low value	
	//
	unsigned long determineEffectiveLow (unsigned long changeBits) {
		float lowCv = low;
		unsigned long chg_high_cv = 0x0;

		if (inputs[LOW_INPUT].isConnected ())
			lowCv += lowCvIn;
		else
			if (link && inputs[HIGH_INPUT].isConnected ()) {
				chg_high_cv = CHG_HIGH_CV;
				lowCv += highCvIn;
			}
		//
		// Calculate effective low value	
		//
		if (changeBits & (CHG_LOW | CHG_LOW_CV | chg_high_cv | CHG_MODE)) {
			effectiveLow = getLowClamped (lowCv);

			if (effectiveLow != oldEffectiveLow) {
				oldEffectiveLow = effectiveLow;
				changeBits |= CHG_EFF_LOW;
			}
		}
		return changeBits;
	}

	//
	// Method to determine new effective high value	
	//
	unsigned long determineEffectiveHigh (unsigned long changeBits) {
		float highCv = high;
		unsigned long chg_low_cv = 0x0;

		if (inputs[HIGH_INPUT].isConnected ())
			highCv += highCvIn;
		else
			if (link && inputs[LOW_INPUT].isConnected ()) {
				chg_low_cv = CHG_LOW_CV;
				highCv += lowCvIn;
			}

		//
		// Calculate effective high value	
		//
		if (changeBits & (CHG_HIGH | CHG_HIGH_CV | chg_low_cv | CHG_MODE)) {
			effectiveHigh = getHighClamped (highCv);

			if (effectiveHigh != oldEffectiveHigh) {
				oldEffectiveHigh = effectiveHigh;
				changeBits |= CHG_EFF_HIGH;
			}
		}
		return changeBits;
	}

	unsigned long doLink (unsigned long changeBits) {
		float clamped;

		if (!link) return changeBits;

		if (changeBits & CHG_LOW && lowClamped) {
			lowClamped = false;
			params[H_LOWCLAMPED_PARAM].setValue (0.f);
			linkDelta = high - low;
		}

		if (changeBits & CHG_HIGH && highClamped) {
			highClamped = false;
			params[H_HIGHCLAMPED_PARAM].setValue (0.f);
			linkDelta = high - low;
		}

		if (changeBits & CHG_LOW) {
			float newHigh = low + linkDelta;

			if ((clamped = getHighClamped (newHigh)) < newHigh) {
				highClamped = true;
				params[H_HIGHCLAMPED_PARAM].setValue(1.f);
			}
			newHigh = clamped;

			if (newHigh != high)
				changeBits |= processHigh (newHigh, true);
		}

		if (changeBits & CHG_HIGH) {
			float newLow = high - linkDelta;

			if ((clamped = getLowClamped (newLow)) > newLow) {
				lowClamped = true;
				params[H_LOWCLAMPED_PARAM].setValue(1.f);
			}
			newLow = clamped;

			if (newLow != low)
				changeBits |= processLow(newLow, true);
		}

		return changeBits;
	}

	unsigned long forceLowLeHigh (unsigned long changeBits) {
		if (changeBits & CHG_LOW && low > high)
			changeBits |= processHigh (low, true);

		if (changeBits & CHG_HIGH && low > high)
			changeBits |= processLow (high, true);

		return changeBits;
	}

	unsigned long determineEffectiveStep (unsigned long changeBits) {
		//
		// if step cv in is connected, the step knob will handled differently in the raw/qtz/ahpr modes
		// in QTZ mode step knob is a positive offset to the stepCv in
		// in RAW and SHPR mode it is used as an attenuator
		//
		if (CHG_STEP | CHG_MODE | CHG_STEP_CV) {
			effectiveStep = step;
			if (inputs[STEP_INPUT].isConnected ()) {
				switch (int(mode)) {
				case int(MODE_RAW):
				case int(MODE_SHPR):
					effectiveStep = (step / 10.f) * stepCvIn;
					break;
				case int(MODE_QTZ):
					effectiveStep = step + stepCvIn;
					break;
				}
			}
		}
		if (mode == MODE_QTZ)
			effectiveStep = note (effectiveStep);

		effectiveStep = getEffectiveStepClamped (effectiveStep);

		if (effectiveStep != oldEffectiveStep) {
			oldEffectiveStep = effectiveStep;
			changeBits |= CHG_EFF_STEP;
		}

		return changeBits;
	}

	//
	// Process Step
	//
	void process (const ProcessArgs &args) override {
		unsigned long changeBits = 0x0;

		sampleTime = 1.0 / (double)(APP->engine->getSampleRate ());

		changeBits = initNew (changeBits);
		changeBits = postLoad (changeBits);
		changeBits = checkUserInteraction (changeBits);
		changeBits = checkInputs (changeBits);
		changeBits = doLink (changeBits);
		changeBits = forceLowLeHigh (changeBits);

		if (changeBits & CHG_MODE)
			setMode (mode);

		changeBits = determineEffectiveLow  (changeBits);
		changeBits = determineEffectiveHigh (changeBits);
		changeBits = determineEffectiveStep (changeBits);

		//
		// Set linkDelta if link is switched on
		//
		if (changeBits & CHG_LINK) {
			if (link)
				linkDelta = high - low;
			else
				lowClamped = highClamped = false;
		}

		//
		// Process cvIn
		//
		float cvOut = oldCvOut;

		//
		// Check for Input Trigger
		// When trigger in is not connected we work like we would get a trigger for each process call
		//
		bool gotInTrg = true;
		if (inputs[TRG_INPUT].isConnected ()) {
			gotInTrg = trgIn.process (inputs[TRG_INPUT].getVoltage ());
		}

		if (gotInTrg && (changeBits & (CHG_CV | CHG_LOW | CHG_HIGH | CHG_STEP | CHG_MODE))) {
			cvOut = cvIn;

			float realStep = effectiveStep;

			if (mode == MODE_QTZ)
				cvOut = quantize (cvIn);

			//
			// In audio mode we make sure that step is <= range so we will always be in range
			// low and high is clamped to +-AUDIO_VOLTAGE 
			//
			float absRange = 0;
			if (mode == MODE_SHPR) {
				if (effectiveLow < -AUDIO_VOLTAGE)
					effectiveLow = -AUDIO_VOLTAGE;
				if (effectiveLow > AUDIO_VOLTAGE)
					effectiveLow = AUDIO_VOLTAGE;
				if (effectiveHigh < -AUDIO_VOLTAGE)
					effectiveHigh = -AUDIO_VOLTAGE;
				if (effectiveHigh > AUDIO_VOLTAGE)
					effectiveHigh = AUDIO_VOLTAGE;

				absRange = effectiveHigh - effectiveLow;
				if (absRange < 0)
					absRange *= -1.f;
				if (absRange < 2 * STEP_MIN_SHPR) {
					absRange = 2 * STEP_MIN_SHPR;
					effectiveLow  -= STEP_MIN_SHPR;
					effectiveHigh += STEP_MIN_SHPR;
				}
				if (realStep > absRange)
					realStep = absRange;
			}

			if (cvOut > effectiveHigh) {
				if (mode == MODE_QTZ) {
					cvOut -= floor (cvOut - effectiveHigh);
					if (cvOut > effectiveHigh)
						cvOut -= 1.;
				}
				else {
					cvOut -= floor ((cvOut - effectiveHigh) / realStep) * realStep;
					if (cvOut > effectiveHigh)
						cvOut -= realStep;
				}
			}

			if (cvOut < effectiveLow) {
				if (mode == MODE_QTZ) {
					cvOut += floor (effectiveLow - cvOut);
					if (cvOut < effectiveLow)
						cvOut += 1.;
				}
				else {
					cvOut += floor ((effectiveLow - cvOut) / realStep) * realStep;
					if (cvOut < effectiveLow)
						cvOut += realStep;
				}
			}

			if (mode == MODE_QTZ && cvOut > effectiveHigh) {
				//
				// We didn't find the same note in our range
				// Now we check whether cvIn + effectiveStep would match
				//
				float altCv = cvOut - 1 + effectiveStep;
				if (altCv >= effectiveLow && altCv <= effectiveHigh)
					cvOut = altCv;
				else
					//
					// Alternative note does note match also, we use altCv anyway if altCv is vetter than cvOut
					// 
					if (cvIn > effectiveHigh && altCv > effectiveHigh && altCv < cvIn)
						cvOut = altCv;
			}

			//
			// In audio mode we rescale the output so that range is mapped to -5, +5V
			//
			if (mode == MODE_SHPR) {
				cvOut -=  effectiveHigh - absRange / 2.f;
				cvOut *= 2 * AUDIO_VOLTAGE / absRange;
			}

			if (gotInTrg) {
				//
				// Set cvOut
				//
				if (cvOut != oldCvOut) {
					//
					// Send a trigger on change of cv out
					//
					trgOutPulse.trigger (0.001f);
					outputs[CV_OUTPUT].setVoltage (cvOut);
					oldCvOut = cvOut;
				}
			}
		}
		//
		// Trigger 
		//
		outputs[TRG_OUTPUT].setVoltage (trgOutPulse.process ((float)sampleTime) ? 10.0f : 0.0f);
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "dummy", json_integer(42));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		presetLoaded = true;
	}
};

//
// Widget to display cvOct values as floats or notes
//
struct VOctWidget : TransparentWidget {

	static constexpr const char*  notes = "CCDDEFFGGAAB";
	static constexpr const char* sharps = " # #  # # # ";

	std::shared_ptr<Font> pFont;

	float *pValue = NULL;
	float  defaultValue = 0;
	float *pMode = NULL;
	char   str[8]; // Space for 7 Chars
	int    type = TYPE_VOCT;
	//
	// Constructor
	//
	VOctWidget() {
		box.size = Vec(55, 80);
		pFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
	}
	//
	// Static function to convert a cv to a string and writes it to the
	// pointer address given by pStr.
	//
	// Make sure that there is at least 8 Bytes allocated to the string at this address !!!
	//
	// If noteMode is true, the cv will be quantized to the next semitone and displayed as note value
	// else the cv is displayed as float with a maximum of 7 chars in the format '-99.999'
	// If not in the range [-10,10] the string 'ERROR' will be
	//
	static char* cv2Str (char *pStr, float cv, float mode, int type) {
		if (cv < -10. || cv > 10.) {
			strcpy (pStr, "ERROR");
		}
		else {
			if (mode == MODE_QTZ) {
				if (type == TYPE_VOCT) {
					float octave = floor (cv);
					int note = int(round ((cv - octave) * 12)) % 12;
					sprintf (pStr, " %c%c%2.0f", notes[note], sharps[note], octave + 4);
				}
				if (type == TYPE_STEP) {
					int semis = Fence::note(cv) * 12;
					sprintf (pStr, "%2d ST", semis);
				}
			}
			else {
				sprintf (pStr, "% *.3f", 7, cv);
			}
		}
		return pStr;
	}
	void draw (const DrawArgs &drawArgs) override {
		nvgFontFaceId (drawArgs.vg, pFont->handle);
		nvgFontSize (drawArgs.vg, 18);
		nvgFillColor (drawArgs.vg, nvgRGB(255, 102, 0)); // Orange

		float value = pValue != NULL ? *pValue : defaultValue;
		float mode  = pMode  != NULL ? *pMode  : DEFAULT_MODE;

		float xOffset = 0;
		if (mode == MODE_QTZ) {
			xOffset = mm2px (2.25);
		}

		nvgText (drawArgs.vg, xOffset, 0, cv2Str (str, value, mode, type), NULL);
	}
};

struct FenceWidget : ModuleWidget {

	//
	// create and initialize a note display widget
	//
	static VOctWidget* createVOctWidget(Vec pos, float *pValue, float defaultValue, float *pMode, int type) {
		VOctWidget *w = new VOctWidget ();

		w->box.pos = pos;
		w->pValue = pValue;
		w->defaultValue = defaultValue;
		w->pMode = pMode;
		w->type = type;

		return w;
	}

	FenceWidget(Fence *module) {

		setModule (module);
		setPanel (APP->window->loadSvg(asset::plugin (pluginInstance, "res/Fence.svg")));

		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec (17.246 + 5,    128.5 - 92.970 - 5)),    module, Fence::HIGH_PARAM));
		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec ( 3.276 + 5,    128.5 - 92.970 - 5)),    module, Fence::LOW_PARAM));
		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec ( 3.276 + 5,    128.5 - 57.568 - 5)),    module, Fence::STEP_PARAM));
/*
		float *pHighValue = (module != NULL ? &(module->high) : NULL);
		float *pLowValue  = (module != NULL ? &(module->low)  : NULL);
		float *pStepValue = (module != NULL ? &(module->step) : NULL);
*/
		float *pLowValue  = (module != NULL ? &(module->effectiveLow)  : NULL);
		float *pHighValue = (module != NULL ? &(module->effectiveHigh) : NULL);
		float *pStepValue = (module != NULL ? &(module->effectiveStep) : NULL);

		float *pMode      = (module != NULL ? &(module->mode) : NULL);

		float mode;
		if (pMode)
			mode = *pMode;
		else
		    mode = DEFAULT_MODE;

		float defaultLow  = (mode == MODE_QTZ ? DEFAULT_LOW_QTZ  : (mode == MODE_SHPR ?   DEFAULT_LOW_SHPR  : DEFAULT_LOW_RAW));
		float defaultHigh = (mode == MODE_QTZ ? DEFAULT_HIGH_QTZ : (mode == MODE_SHPR ?   DEFAULT_HIGH_SHPR : DEFAULT_HIGH_RAW));
		float defaultStep = (mode == MODE_QTZ ? DEFAULT_STEP_QTZ : (mode == MODE_SHPR ?   DEFAULT_STEP_SHPR : DEFAULT_HIGH_RAW));

		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 - 113.252 - 0.25 )), pHighValue, defaultHigh, pMode, TYPE_VOCT));
		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 - 106.267 - 0.25 )), pLowValue,  defaultLow,  pMode, TYPE_VOCT));
		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 -  71.267 + 0.25 )), pStepValue, defaultStep, pMode, TYPE_STEP));

		addParam (createParamCentered<LEDButton>		(mm2px (Vec (12.858 + 2.38, 128.5 - 88.900 - 2.38)), module, Fence::LINK_PARAM));
 		addChild (createLightCentered<LargeLight<GreenLight>>	(mm2px (Vec (12.858 + 2.38, 128.5 - 88.900 - 2.38)), module, Fence::LINK_LIGHT));
		
		addParam (createParamCentered<LEDButton>		(mm2px (Vec (20.638 + 2.38, 128.5 - 56.525 - 2.38)), module, Fence::QTZ_PARAM));
 		addChild (createLightCentered<LargeLight<GreenLight>>	(mm2px (Vec (20.638 + 2.38, 128.5 - 56.525 - 2.38)), module, Fence::QTZ_LIGHT));
		
		addParam (createParamCentered<LEDButton>		(mm2px (Vec (20.638 + 2.38, 128.5 - 48.577 - 2.38)), module, Fence::SHPR_PARAM));
 		addChild (createLightCentered<LargeLight<GreenLight>>	(mm2px (Vec (20.638 + 2.38, 128.5 - 48.577 - 2.38)), module, Fence::SHPR_LIGHT));

		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.049 + 4.2 , 128.5 - 82.947 - 4.2)),  module, Fence::LOW_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec (18.019 + 4.2 , 128.5 - 82.947 - 4.2)),  module, Fence::HIGH_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 47.547 - 4.2)),  module, Fence::STEP_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 29.609 - 4.2)),  module, Fence::TRG_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 11.829 - 4.2)),  module, Fence::CV_INPUT));

		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (18.02  + 4.2 , 128.5 - 29.609 - 4.2)),  module, Fence::TRG_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (18.02  + 4.2 , 128.5 - 11.829 - 4.2)),  module, Fence::CV_OUTPUT));
	}
};

Model *modelFence = createModel<Fence, FenceWidget>("Fence");
