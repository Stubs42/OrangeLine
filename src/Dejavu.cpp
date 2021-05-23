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
	struct OrangeLineRandom lengthRandomGenerator[NUM_LENGTHS];
	unsigned long lengthSeed[NUM_LENGTHS];
	struct OrangeLineRandom channelRandomGeneratorGate[POLY_CHANNELS];
	struct OrangeLineRandom channelRandomGeneratorCv[POLY_CHANNELS];

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
   		setStateTypeInput (CLK_INPUT, STATE_TYPE_TRIGGER);
   		setStateTypeInput (RST_INPUT, STATE_TYPE_TRIGGER);

		for (int i = 0; i < NUM_LENGTHS; i++) {
	   		setStateTypeInput (TL_INPUT + i, STATE_TYPE_TRIGGER);
	   		setStateTypeOutput (TL_OUTPUT + i, STATE_TYPE_TRIGGER);
		}

		setInPoly (HEAT_INPUT, true);
		setInPoly (OFS_INPUT, true);
		setInPoly (SCL_INPUT, true);
		setStateTypeParam (SH_PARAM, STATE_TYPE_TRIGGER);

		setStateTypeParam (GATE_PARAM, STATE_TYPE_TRIGGER);
     	setStateTypeOutput (GATE_OUTPUT, STATE_TYPE_TRIGGER);
		setOutPoly         (GATE_OUTPUT, true);

		setOutPoly (CV_OUTPUT, true);
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
		setJsonLabel (COUNTER_JSON + 0, "conter_1");
		setJsonLabel (COUNTER_JSON + 1, "conter_2");
		setJsonLabel (COUNTER_JSON + 2, "conter_3");
		setJsonLabel (COUNTER_JSON + 3, "conter_4");
		setJsonLabel (GATE_JSON, "gate");
		setJsonLabel (SH_JSON, "sh");

		#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig () {
		configParam (DIV_PARAM,         1.f,        64.f,   1.f, "Clock Division",        "", 0.f, 1.f, 0.f);
		configParam (SEED_PARAM,        0.f,      9999.f,   0.f, "Seed",                  "", 0.f, 1.f, 0.f);
		
		float maxLength = pow (10.f, L_DIGITS) - 1;
		configParam (L_PARAM + 0,       1.f,   maxLength,   1.f, "Length 1",              "", 0.f, 1.f, 0.f);
		configParam (L_PARAM + 1,       1.f,   maxLength,   1.f, "Length 2",              "", 0.f, 1.f, 0.f);
		configParam (L_PARAM + 2,       1.f,   maxLength,   1.f, "Length 3",              "", 0.f, 1.f, 0.f);
		configParam (L_PARAM + 3,       1.f,   maxLength,   1.f, "Length 4",              "", 0.f, 1.f, 0.f);		

		configParam (HEAT_PARAM,        0.f,       100.f,  50.f, "Heat",                  "%", 0.f, 1.f, 0.f);
		configParam (HEAT_ATT_PARAM, -100.f,       100.f,   0.f, "Heat Attenuation",      "%", 0.f, 1.f, 0.f);
		
		configParam (OFS_PARAM,       -10.f,        10.f,   0.f, "Offset",                "",  0.f, 1.f, 0.f);
		configParam (OFS_ATT_PARAM,  -100.f,       100.f,   0.f, "Offset Attenuation",    "%", 0.f, 1.f, 0.f);
		
		configParam (SCL_PARAM,      -100.f,       100.f, 100.f, "Scale",                 "",  0.f, 1.f, 0.f);
		configParam (SCL_ATT_PARAM,  -100.f,       100.f,   0.f, "Scale Attenuation",     "%", 0.f, 1.f, 0.f);

		configParam (CHN_PARAM,         1.f,        16.f,   1.f, "# of Output Channels",  "",  0.f, 1.f, 0.f);

   		configParam (SH_PARAM,          0.f,         1.f,   0.f,  "Toggle CV S&H",         "", 0.f, 1.f, 0.f);
   		configParam (GATE_PARAM,        0.f,         1.f,   0.f,  "Toggle Trigger/Gate",   "", 0.f, 1.f, 0.f);
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
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
	*/
	void moduleReset () {
		setStateJson (RESET_JSON,       0.f);
		setStateJson (COUNTER_JSON + 0, 0.f);
		setStateJson (COUNTER_JSON + 1, 0.f);
		setStateJson (COUNTER_JSON + 2, 0.f);
		setStateJson (COUNTER_JSON + 3, 0.f);
		setStateJson (GATE_JSON, TRIGGER_MODE);
		setStateJson (SH_JSON, 0.f);
	}

// ********************************************************************************************************************************
/*
	Module specific utility methods
*/

// ********************************************************************************************************************************
/*
	Set all counters to 0 on reset and reinitialize global random generator
*/
void doReset () {
	for (int i = 0; i < NUM_LENGTHS; i ++)
		setStateJson (COUNTER_JSON + i, 0.f);

	float seed = 0; 
	if (getInputConnected (SEED_INPUT)) {
		float seedFloat = getStateInput (SEED_INPUT);
		if (seedFloat < 0)
			seedFloat *= -1;
		unsigned long seed = (unsigned long)seedFloat;
		seed %= (unsigned long)pow (10.f, L_DIGITS);
		setStateParam (SEED_PARAM, float(seed));
	}
	else
		seed = getStateParam (SEED_PARAM);

	initRandom (&globalRandom, seed);
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

		if (changeInput (RST_INPUT))
			doReset();

		// handle TLx triggers
		bool gotTLx = false;
		for (int i = NUM_LENGTHS - 1; i >= 0; i--) {
			if (gotTLx) {
				setStateJson (COUNTER_JSON + i, 0.f);
			}
			else {
				if (changeInput (TL_INPUT + i)) {
					setStateJson (COUNTER_JSON + i, 0.f);
					gotTLx = true;
				}
			}
		}
		/*
			Make shure that Ln >= Ln-1
		*/
		int lowerLength = int(getStateParam (L_PARAM)); // get Length of L1
		for (int i = 1; i < NUM_LENGTHS; i ++) {
			int thisLength = int(getStateParam (L_PARAM + i));
			if (thisLength < lowerLength) {
				OL_state[stateIdxParam(L_PARAM + i)] = float(lowerLength);
				params[L_PARAM + i].setValue (float(lowerLength));
			}
			else
				lowerLength = thisLength;
		}

		if (changeInput (CLK_INPUT)) {
			/*
				Decrement counts and process (restart) cycle(s) if one (or more) length counters are 0
			*/
			for (int i = NUM_LENGTHS - 1; i >= 0; i --) {
				int count = int(getStateJson (COUNTER_JSON + i));
				// DEBUG("Checking Counter for L[%d]", i + 1);

				if (count == 0) {
					// DEBUG("Counter reached 0");
					unsigned long seed;
					if (i == NUM_LENGTHS - 1) {
						seed = getRandomRaw(&globalRandom);
						// DEBUG("Get seed from globalRandom:");
					}
					else {
						seed = lengthSeed[i + 1];
						// DEBUG("Get seed from L[%d], lengthSeed[%d]:", i + 2, i + 1);
					}
					// DEBUG("intilize L[%d] random gegenrator with seed %ld", i + 1, seed);
					initRandom(&(lengthRandomGenerator[i]), seed);
					lengthSeed[i] = getRandomRaw(&(lengthRandomGenerator[i]));
					// DEBUG("Writing the new seed into lengthSeed[%d]", i);

					if (i == 0) {
						// DEBUG("Initializing channel random generators for L[%d]", i + 1);
						for (int channel = 0; channel < POLY_CHANNELS; channel ++) {
							seed = getRandomRaw(&(lengthRandomGenerator[0]));
							initRandom (&(channelRandomGeneratorGate[channel]), seed);
							initRandom (&(channelRandomGeneratorCv[channel]), seed);
						}
					}
					else {
						// DEBUG("Resetting L[%d]", i);
						setStateJson (COUNTER_JSON + i - 1, 0.f);	// reset for L[i-1]
					}

					if (getInputConnected (L_INPUT + i)) {
						count = int(getStateInput (L_INPUT + i) * 1000);
						if (count < 1)
							count = 1;
						if (count >= pow (10.f, L_DIGITS)) 
							count = pow (10.f, L_DIGITS) - 1;
						setStateParam (L_PARAM, float(count));
					}
					else
						count = getStateParam (L_PARAM + i);

					setStateOutput (TL_OUTPUT + i, 10.f);	// Send Trigger for this Length
				}
				else
					count -= 1;

				// DEBUG("Setting counter for L[%d] to %d", i + 1, count);
				setStateJson (COUNTER_JSON + i, float(count));
			}
			/*
				Process output channels
				TODO: Currently all channels 0-15 but will configurable in right click menu later
			*/
			setOutPolyChannels(GATE_OUTPUT, 16);
			setOutPolyChannels(CV_OUTPUT, 16);
			int heatPolyChannels = inputs[HEAT_INPUT].getChannels();
			int ofsPolyChannels = inputs[OFS_INPUT].getChannels();
			int sclPolyChannels = inputs[SCL_INPUT].getChannels();
			float lastHeatInput = 0;
			float lastOfsInput = 0;
			float lastSclInput = 0;
			for (int channel = 0; channel < POLY_CHANNELS; channel ++) {
				float gateRandom = getRandom (&(channelRandomGeneratorGate[channel]));
				float heat = getStateParam(HEAT_PARAM) / 100.f;
				bool fired = false;
				if (getInputConnected(HEAT_INPUT)) {
					int heatInput = 0.f;
					if (channel > heatPolyChannels)
						heatInput = lastHeatInput;
					else {
						heatInput = OL_statePoly[HEAT_INPUT * POLY_CHANNELS + channel];
						lastHeatInput = heatInput;
					}
					heat += (heatInput / 10.f) * (getStateParam(HEAT_ATT_PARAM) / 100.f);
				}
				if (heat >= gateRandom) {
					OL_statePoly[ NUM_INPUTS * POLY_CHANNELS + GATE_OUTPUT * POLY_CHANNELS + channel] = 10.f;
					OL_outStateChangePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
					fired = true;
				}
				if (fired || getStateJson(SH_JSON) == 0.f) {
					float cvRandom = -10.f + getRandom (&(channelRandomGeneratorCv[channel])) * 20.f;
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
					cvRandom *= scl;
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
	}

	/**
		Module specific input processing called from process () in OrangeLineCommon.hpp
		right after generic processParamsAndInputs ()

		moduleProcessState () should only be used to derive json state and module member variables
		from params and inputs collected by processParamsAndInputs ().

		This method should not do dsp or other logic processing.
	*/
	inline void moduleProcessState () {
		if (inChangeParam (GATE_PARAM))	{
			setStateJson (GATE_JSON, float((int(getStateJson (GATE_JSON)) + 1) % 2));
		}
		if (getStateJson (GATE_JSON) > 0.f)
			isGate (GATE_OUTPUT) = true;
		else
			isGate (GATE_OUTPUT) = false;

		if (inChangeParam (SH_PARAM))	{
			setStateJson (SH_JSON, float((int(getStateJson (SH_JSON)) + 1) % 2));
		}
	}

	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges () {
		if (!initialized || changeJson (GATE_JSON)) {
			if (getStateJson (GATE_JSON) == 1.)
				setRgbLight (GATE_LIGHT_RGB, GATE_COLOR_ON);
			else
				setRgbLight (GATE_LIGHT_RGB, GATE_COLOR_OFF);
		}
		if (!initialized || changeJson (SH_JSON)) {
			if (getStateJson (SH_JSON) == 1.)
				setRgbLight (SH_LIGHT_RGB, SH_COLOR_ON);
			else
				setRgbLight (SH_LIGHT_RGB, SH_COLOR_OFF);
		}
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/
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

		for (int i = 0; i < NUM_LENGTHS; i ++) {
			float y = 38.722 + (10.16 * i);
			addInput (createInputCentered<PJ301MPort> (calculateCoordinates (3.415, y, OFFSET_PJ301MPort), module, L_INPUT + i));
			knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (23.940, y, OFFSET_RoundSmallBlackKnob),  module, L_PARAM + i);
	        knob->snap = true;
			addParam (knob);
		}
		for (int i = 0; i < NUM_LENGTHS; i ++) {
			float y = 79.362 + (10.16 * i);
			addInput (createInputCentered<PJ301MPort> (calculateCoordinates (3.415, y, OFFSET_PJ301MPort), module, TL_INPUT + i));
			addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (23.940, y, OFFSET_PJ301MPort),  module, TL_OUTPUT + i));
		}
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (38.975, 62.852, OFFSET_PJ301MPort), module, HEAT_INPUT ));
		addParam (createParamCentered<Trimpot> (calculateCoordinates ( 40.030, 53.747, OFFSET_Trimpot),  module, HEAT_ATT_PARAM));
		addParam (createParamCentered<RoundHugeBlackKnob> (calculateCoordinates ( 48.920, 52.475, OFFSET_RoundHugeBlackKnob),  module, HEAT_PARAM));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (39.180, 79.568, OFFSET_RoundSmallBlackKnob),  module, OFS_PARAM));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (49.340, 79.568, OFFSET_RoundSmallBlackKnob),  module, SCL_PARAM));
		addParam (createParamCentered<Trimpot> (calculateCoordinates ( 40.030, 90.577, OFFSET_Trimpot),  module, OFS_ATT_PARAM));
		addParam (createParamCentered<Trimpot> (calculateCoordinates ( 50.190, 90.577, OFFSET_Trimpot),  module, SCL_ATT_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (38.975, 99.682, OFFSET_PJ301MPort), module, OFS_INPUT ));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (49.135, 99.682, OFFSET_PJ301MPort), module, SCL_INPUT ));
		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (59.295, 99.682, OFFSET_PJ301MPort),  module, CV_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (59.295, 109.842, OFFSET_PJ301MPort),  module, GATE_OUTPUT));

		addParam (createParamCentered<LEDButton>                   (calculateCoordinates (61.210, 81.186, OFFSET_LEDButton), module, SH_PARAM));
		light = createLightCentered<LargeLight<RedGreenBlueLight>> (calculateCoordinates (61.120, 81.186, OFFSET_LargeLight), module, SH_LIGHT_RGB);
		light->bgColor = nvgRGBA(0, 0, 0, 255);
	 	addChild (light);

		addParam (createParamCentered<LEDButton>                   (calculateCoordinates (50.959, 111.666, OFFSET_LEDButton), module, GATE_PARAM));
		light = createLightCentered<LargeLight<RedGreenBlueLight>> (calculateCoordinates (50.959, 111.666, OFFSET_LargeLight), module, GATE_LIGHT_RGB);
		light->bgColor = nvgRGBA(0, 0, 0, 255);
	 	addChild (light);
/* 
13,575
12,125
*/
/*
		keysWidget = KeysWidget::create(module);
		addChild (keysWidget);

		knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (24.098 + 4, 128.5 - 41.724 - 4)), module, ROOT_PARAM);
		knob->snap = true;
   		addParam (knob);

		knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (12.034 + 4, 128.5 - 75.057 - 4)), module,  SCL_PARAM);
    		    knob->snap = true;
   		addParam (knob);

		knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (25.686 + 4, 128.5 - 75.057 - 4)), module, CHLD_PARAM);
		knob->snap = true;
   		addParam (knob);

		knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (13.621 + 4, 128.5 - 99.822 - 4)), module, FATE_AMT_PARAM);
		knob->snap = true;
   		addParam (knob);

		knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (24.098 + 4, 128.5 - 99.822 - 4)), module, FATE_SHP_PARAM);
   		addParam (knob);

		for (int i = 0; i < NUM_NOTES; i++) {
			addParam (createParamCentered<LEDButton>                   (mm2px (Vec (3.175 + 2.381, 128.5 - 105.886 - 2.381 + float(i) * (105.886 - 25.559) / 11.f)), module, ONOFF_PARAM + NUM_NOTES - i - 1));
			light = createLightCentered<LargeLight<RedGreenBlueLight>> (mm2px (Vec (3.249 + 2.308, 128.5 - 105.96  - 2.308 + float(i) * (105.960 - 25.663) / 11.f)), module, NOTE_LIGHT_01_RGB + ((NUM_NOTES - i - 1) * 3));
			light->bgColor = nvgRGBA(0, 0, 0, 255);
	 		addChild (light);
		}

		for (int i = 0; i < NUM_NOTES; i++) {
			addParam (createParamCentered<Trimpot> (mm2px (Vec (37.014  + 3.15 ,128.5 - 105.118 - 3.15 + i * (105.118 - 24.790) / 11.f)),  module, WEIGHT_PARAM + NUM_NOTES - i - 1));
		}
		
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (13.416 + 4.2 , 128.5 - 41.515 - 4.2)), module, ROOT_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (11.829 + 4.2 , 128.5 - 64.375 - 4.2)), module,  SCL_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (25.481 + 4.2 , 128.5 - 64.375 - 4.2)), module, CHLD_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (13.416 + 4.2 , 128.5 - 25.640 - 4.2)), module,  RND_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec ( 3.256 + 4.2 , 128.5 -  9.765 - 4.2)), module,   CV_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (13.416 + 4.2 , 128.5 -  9.765 - 4.2)), module,  TRG_INPUT));

		addOutput (createOutputCentered<PJ301MPort>	(mm2px (Vec (23.894 + 4.2 , 128.5 - 25.640 - 4.2)), module,  POW_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>	(mm2px (Vec (23.894 + 4.2 , 128.5 -  9.765 - 4.2)), module, GATE_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>	(mm2px (Vec (34.128 + 4.2 , 128.5 -  9.765 - 4.2)), module,   CV_OUTPUT));

		const char *text;

		text = (module != nullptr ? module->headDisplayText : nullptr);
		headWidget = TextWidget::create (mm2px (Vec(3.183 - 0.25 - 0.35, 128.5 - 115.271)), module, text, "Major", 12, (module ? &(module->headScrollTimer) : nullptr));
		headWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
		addChild (headWidget);

		text = (module != nullptr ? module->rootText : nullptr);
		rootWidget = TextWidget::create (mm2px (Vec(24.996 - 0.25, 128.5 - 52.406)), module, text, "C", 2, nullptr);
		rootWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
		addChild (rootWidget);

		float *pvalue  = (module != nullptr ? &(module->effectiveScaleDisplay) : nullptr);
		scaleWidget = NumberWidget::create (mm2px (Vec(12.931 - 0.25, 128.5 - 86.537)), module, pvalue, 1.f, "%2.0f", scaleBuffer, 2);
		scaleWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));

		addChild (scaleWidget);

		text = (module != nullptr ? module->childText : nullptr);
		childWidget = TextWidget::create (mm2px (Vec(26.742 - 0.25, 128.5 - 86.537)), module, text, "C", 2, nullptr);
		childWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
		addChild (childWidget);
		*/
	}

/*
	struct MotherScalesItem : MenuItem {
		Mother *module;
		
		struct MotherScaleItem : MenuItem {
			Mother *module;
			int scaleIdx = 0;

			void onAction(const event::Action &e) override {
				module->scaleSelected = scaleIdx;
			}
		};

		Menu *createChildMenu() override {
			Menu *menu = new Menu;
			MotherScaleItem *scaleItem;
			for (int scale = 0; scale < SCALE_KEYS; scale++) {
				
				scaleItem = new MotherScaleItem ();
				scaleItem->module = module;
				scaleItem->scaleIdx = scale;
				scaleItem->text = module->scaleNames[scale];
				scaleItem->rightText = module->scaleKeys[scale];

				menu->addChild(scaleItem);
			}
			return menu;
		}

		void onEnter (const event::Enter &  e) override	 {
			if (module != nullptr && module->effectiveChild == 0)
				MenuItem::onEnter(e);
		}

		void step() override {
			if (module && module->effectiveChild == 0)
				disabled = false;
			else
				disabled = true;
		}
	};

	struct AutoChannelsItem : MenuItem {
		Mother *module;
		
		struct AutoChannelItem : MenuItem {
			Mother *module;
			int channels;
			void onAction(const event::Action &e) override {
				module->OL_setOutState(AUTO_CHANNELS_JSON, float(channels));
			}
			void step() override {
				if (module)
					rightText = (module != nullptr && module->OL_state[AUTO_CHANNELS_JSON] == channels) ? "✔" : "";
			}
		};

		Menu *createChildMenu() override {
			Menu *menu = new Menu;
			AutoChannelItem *autoChannelItem;
			for (int channel = 0; channel < 16; channel++) {
				
				autoChannelItem = new AutoChannelItem ();
				autoChannelItem->module = module;
				autoChannelItem->channels = channel + 1;
				autoChannelItem->text = module->channelNumbers[channel];
				autoChannelItem->setSize (Vec(50, 20));

				menu->addChild(autoChannelItem);
			}
			return menu;
		}
	};
*/
	struct MotherStyleItem : MenuItem {
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
/*
	struct MotherDisableVisualizationItem : MenuItem {
		Mother *module;
		void onAction(const event::Action &e) override {
			if (module->OL_state[VISUALIZATION_DISABLED_JSON] == 0.f)
				module->OL_setOutState(VISUALIZATION_DISABLED_JSON, 1.f);
			else
				module->OL_setOutState(VISUALIZATION_DISABLED_JSON, 0.f);
			module->visualizationDisabledChanged = true;
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->OL_state[VISUALIZATION_DISABLED_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct MotherDisableDnaItem : MenuItem {
		Mother *module;
		void onAction(const event::Action &e) override {
			if (module->OL_state[DNA_DISABLED_JSON] == 0.f)
				module->OL_setOutState(DNA_DISABLED_JSON, 1.f);
			else
				module->OL_setOutState(DNA_DISABLED_JSON, 0.f);
			module->disableDnaChanged = true;
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->OL_state[DNA_DISABLED_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct MotherDisableGrabItem : MenuItem {
		Mother *module;
		void onAction(const event::Action &e) override {
			if (module->OL_state[GRAB_DISABLED_JSON] == 0.f)
				module->OL_setOutState(GRAB_DISABLED_JSON, 1.f);
			else
				module->OL_setOutState(GRAB_DISABLED_JSON, 0.f);
			module->disableGrabChanged = true;
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->OL_state[GRAB_DISABLED_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct MotherRootBasedDisplayItem : MenuItem {
		Mother *module;
		void onAction(const event::Action &e) override {
			if (module->OL_state[ROOT_BASED_DISPLAY_JSON] == 0.f) {
				module->OL_setOutState(ROOT_BASED_DISPLAY_JSON, 1.f);
				module->OL_setOutState(C_BASED_DISPLAY_JSON, 0.f);
			}
			else
				module->OL_setOutState(ROOT_BASED_DISPLAY_JSON, 0.f);
			module->rootBasedDisplayChanged = true;
			module->cBasedDisplayChanged = true;
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->OL_state[ROOT_BASED_DISPLAY_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct MotherCBasedDisplayItem : MenuItem {
		Mother *module;
		void onAction(const event::Action &e) override {
			if (module->OL_state[C_BASED_DISPLAY_JSON] == 0.f) {
				module->OL_setOutState(C_BASED_DISPLAY_JSON, 1.f);
				module->OL_setOutState(ROOT_BASED_DISPLAY_JSON, 0.f);
			}
			else {
				module->OL_setOutState(C_BASED_DISPLAY_JSON, 0.f);
			}
			module->rootBasedDisplayChanged = true;
			module->cBasedDisplayChanged = true;
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->OL_state[C_BASED_DISPLAY_JSON] == 1.0f) ? "✔" : "";
		}
	};
*/
	void appendContextMenu(Menu *menu) override {
		if (module) {
			MenuLabel *spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			Dejavu *module = dynamic_cast<Dejavu*>(this->module);
			assert(module);
/*
			MenuLabel *motherLabel = new MenuLabel();
			motherLabel->text = "Dejavu";
			menu->addChild(motherLabel);

			MotherScalesItem *motherScalesItem = new MotherScalesItem();
			motherScalesItem->module = module;		
			motherScalesItem->text = "Scales";
			motherScalesItem->rightText = RIGHT_ARROW;
			menu->addChild(motherScalesItem);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			MenuLabel *behaviourLabel = new MenuLabel();
			behaviourLabel->text = "Behaviour";
			menu->addChild(behaviourLabel);

			MotherDisableVisualizationItem *motherDisableVisualizationItem = new MotherDisableVisualizationItem();
			motherDisableVisualizationItem->module = module;		
			motherDisableVisualizationItem->text = "Disable Visualization";
			menu->addChild(motherDisableVisualizationItem);

			MotherDisableDnaItem *motherDisableDnaItem = new MotherDisableDnaItem();
			motherDisableDnaItem->module = module;		
			motherDisableDnaItem->text = "Disable DNA";
			menu->addChild(motherDisableDnaItem);

			MotherDisableGrabItem *motherDisableGrabItem = new MotherDisableGrabItem();
			motherDisableGrabItem->module = module;		
			motherDisableGrabItem->text = "Disable Grab";
			menu->addChild(motherDisableGrabItem);

			MotherRootBasedDisplayItem *motherRootBasedDisplayItem = new MotherRootBasedDisplayItem();
			motherRootBasedDisplayItem->module = module;		
			motherRootBasedDisplayItem->text = "Root Based Display";
			menu->addChild(motherRootBasedDisplayItem);

			MotherCBasedDisplayItem *motherCBasedDisplayItem = new MotherCBasedDisplayItem();
			motherCBasedDisplayItem->module = module;		
			motherCBasedDisplayItem->text = "C Based Display";
			menu->addChild(motherCBasedDisplayItem);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			MenuLabel *polyphonyLabel = new MenuLabel();
			polyphonyLabel->text = "Polyphony";
			menu->addChild(polyphonyLabel);

			AutoChannelsItem *autoChannelsItem = new AutoChannelsItem();
			autoChannelsItem->module = module;		
			autoChannelsItem->text = "Auto Channels";
			autoChannelsItem->rightText = RIGHT_ARROW;
			menu->addChild(autoChannelsItem);
*/
			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			MenuLabel *styleLabel = new MenuLabel();
			styleLabel->text = "Style";
			menu->addChild(styleLabel);

			MotherStyleItem *style1Item = new MotherStyleItem();
			style1Item->text = "Orange";// 
			style1Item->module = module;
			style1Item->style= STYLE_ORANGE;
			menu->addChild(style1Item);

			MotherStyleItem *style2Item = new MotherStyleItem();
			style2Item->text = "Bright";// 
			style2Item->module = module;
			style2Item->style= STYLE_BRIGHT;
			menu->addChild(style2Item);
				
			MotherStyleItem *style3Item = new MotherStyleItem();
			style3Item->text = "Dark";// 
			style3Item->module = module;
			style3Item->style= STYLE_DARK;
			menu->addChild(style3Item);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

		}
	}
};
Model *modelDejavu = createModel<Dejavu, DejavuWidget> ("Dejavu");
