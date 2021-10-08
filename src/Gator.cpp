/*
	Gator.cpp
 
	Code for the OrangeLine module Gator

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

#include "Gator.hpp"

struct Gator : Module {

	#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
    float oldPhs = 10;
    float oldPhsSkip = 10;

    bool  channelActive[POLY_CHANNELS] = { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false };
    bool  gateProcessed[POLY_CHANNELS] = { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false };
    float gateLen[POLY_CHANNELS];
    int   offPhsCnt[POLY_CHANNELS];
    float offCmp[POLY_CHANNELS];
    float rnd[POLY_CHANNELS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int   ratCnt[POLY_CHANNELS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int   ratNum[POLY_CHANNELS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    float ratDly[POLY_CHANNELS];
    float ratCmp[POLY_CHANNELS];
    int   ratPhsCnt[POLY_CHANNELS];


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
	Gator () {
		initializeInstance ();
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
	/*
		Method to decide whether this call of process() should be skipped

		Only process when idleSkipCOunter provided by OrangeLineCommmon.hpp == 0
		or we are running some delayed processing
		or if we have a clock trigger.
		We must not do a trigger process here but just check if the clock trigger input changed
	*/
	bool moduleSkipProcess() {
		float phs = getStateInput (PHS_INPUT);
		// Make sure that whe skip when a ne phase behins because the cv inputs
		// from sequencers might not be ready yet !
		if (phs < oldPhsSkip) 
			idleSkipCounter = IDLESKIP;
		oldPhsSkip = phs;
		return (idleSkipCounter != 0);
	}
	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes () {
		setInPoly  (GATE_INPUT, true);
		setInPoly  (TIME_INPUT, true);
		setInPoly  (LEN_INPUT,  true);
		setInPoly  (JTR_INPUT,  true);
		setInPoly  (RAT_INPUT,  true);
		setInPoly  (DLY_INPUT,  true);

		setStateTypeInput (RST_INPUT, STATE_TYPE_TRIGGER);

		setOutPoly (GATE_OUTPUT, true);
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig () {
        setStateJson (STYLE_JSON, float(STYLE_ORANGE));

   		#pragma GCC diagnostic push 
		#pragma GCC diagnostic ignored "-Wwrite-strings"

		//
		// Config internal Parameters not bound to a user interface object
		//
		setJsonLabel (      STYLE_JSON, "style");

		#pragma GCC diagnostic pop

	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig () {	
		configParam (LEN_PARAM,    0.f, 100.f, 0.f, "Gate Length",               "" , 0.f, 1.f, 0.f);
		configParam (JTR_PARAM,    0.f, 100.f, 0.f, "Jitter Amount",             "%", 0.f, 1.f, 0.f);
		configParam (RAT_PARAM,  -10.f,  10.f, 0.f, "Ratchets",                  "" , 0.f, 1.f, 0.f);
		configParam (DLY_PARAM,    0.f,  10.f, 0.f, "Ratchet Delay",             "" , 0.f, 1.f, 0.f);
		configParam (STR_PARAM, -100.f, 100.f, 0.f, "Strum Direction and Delay", "%", 0.f, 1.f, 0.f);
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
		Currently called twice when add a module to patch ...
	*/
	void moduleReset () {
	}

// ********************************************************************************************************************************
/*
	Module specific utility methods
*/

// ********************************************************************************************************************************
/*
	Methods called directly or indirectly called from process () in OrangeLineCommon.hpp
*/
    void checkRatchets (float phs) {
        for (int channel = 0; channel < POLY_CHANNELS; channel ++) {
            if (ratCnt[channel] > 0 && ratPhsCnt[channel] == 0 && ratCmp[channel] <= phs) {
                channelActive[channel] = true;
                offPhsCnt[channel] = int (gateLen[channel]);
                offCmp[channel] = phs + (gateLen[channel] - offPhsCnt[channel]) * 20;
                while (offCmp[channel] >= 10) {
                    offPhsCnt[channel] ++;
                    offCmp[channel] -= 20;
                }
                ratCnt[channel]--;
                if (ratCnt[channel] > 0) {
                    ratCmp[channel] += ratDly[channel] * 20.f;
                    while (ratCmp[channel] >= 10) {
                        ratPhsCnt[channel] ++;
                        ratCmp[channel] -= 20;
                    }
                }
            }
        }
    }

    void processActiveGates (float phs) {
        // process active gates
        int channels = inputs[GATE_INPUT].getChannels();
        for (int channel = 0; channel < channels; channel++) {
            if (!channelActive[channel]) {
                setStateOutPoly (GATE_OUTPUT, channel, 0.f);
                continue;
            }
            if (offPhsCnt[channel] == 0 && offCmp[channel] <= phs) {
                setStateOutPoly (GATE_OUTPUT, channel, 0.f);
                channelActive[channel] = false;
            }
            else {
				if (ratCnt[channel] != ratNum[channel])
	                setStateOutPoly (GATE_OUTPUT, channel, 9.5f);
				else
	                setStateOutPoly (GATE_OUTPUT, channel, 10.f);
            }
        }
    }

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

        if (changeInput (RST_INPUT)) {
            for (int channel = 0; channel < POLY_CHANNELS; channel ++) {
                oldPhs = 10;
                channelActive[channel] = false;
                gateProcessed[channel] = false;
                ratCnt[channel] = 0;
                setStateOutPoly (GATE_OUTPUT, channel, 0.f);
            }
        }

        float phs = getStateInput (PHS_INPUT);

        bool newPhs = (oldPhs > phs);
        if (newPhs) {
            // catch missen events around 10V me have never seen 10V as phase in process
            // fire them now as latest possible time
            checkRatchets (10);
            processActiveGates (10);

            for (int channel = 0; channel < POLY_CHANNELS; channel ++) {
                // reset gates processed for this new phase
                gateProcessed[channel] = false;
                // prepare randoms for jitter
                float r = getRandom (&globalRandom) * 2 - 1;  // get a random between -1 and 1
                r *= fabs(r); // square it t get more in the middle around 0
                rnd[channel] = r;
                // decrement phase counter for gate length
                if (offPhsCnt[channel] > 0)
                    offPhsCnt[channel]--;
                // decrement phase counter for ratchets
                if (ratPhsCnt[channel] > 0)
                    ratPhsCnt[channel]--;
            }
        }
        oldPhs = phs;

        int channels = inputs[GATE_INPUT].getChannels();
        setOutPolyChannels (GATE_OUTPUT, channels);
        int timeChannels = inputs[TIME_INPUT].getChannels();
        float lastTime = 0;
        int lenChannels = inputs[LEN_INPUT].getChannels();
        float lastLen = 0;
        int jtrChannels = inputs[JTR_INPUT].getChannels();
        float lastJtr = 0;
        int dlyChannels = inputs[DLY_INPUT].getChannels();
        float lastDly = 0;
        int ratChannels = inputs[RAT_INPUT].getChannels();
        float lastRat = 0;
        float cmpInput = 0;
        float strum = getStateParam (STR_PARAM) / 10;
        if (getInputConnected (STR_INPUT))
            strum = getStateInput(STR_INPUT);
        if (getInputConnected (CMP_INPUT))
            cmpInput = getStateInput (CMP_INPUT);
        for (int channel = 0; channel < channels; channel++) {
            if (!gateProcessed[channel]) {
                if (OL_statePoly[GATE_INPUT * POLY_CHANNELS + channel] >= 5.f) {
                    float cmp = cmpInput;
                    if (getInputConnected (TIME_INPUT)) {
                        if (channel < timeChannels)
                            lastTime = OL_statePoly[TIME_INPUT * POLY_CHANNELS + channel];
                        cmp += lastTime;
                    }
                    float jtr = getStateParam (JTR_PARAM) / 10;
                    if (getInputConnected (JTR_INPUT)) {
                        if (channel < jtrChannels)
                            lastJtr = OL_statePoly[JTR_INPUT * POLY_CHANNELS + channel];
                        jtr += lastJtr;
                    }
                    if (jtr < 0 ) jtr =  0;
                    if (jtr > 10) jtr = 10;
                    cmp += jtr * rnd[channel];
                    // clamp here, strumming might cross borders, thats ok 
                    if (cmp < -10)
                        cmp = -10;
                    if (cmp > 9.95)
                        cmp = 9.95;
                    // Add strumming
                    if (strum >= 0) {
                        cmp += channel * strum;
                    }
                    else {
                        cmp -= (channels - channel - 1) * strum;
                    }
                    int cmpPhsCnt = 0;
                    while (cmp >= 10) {
                        cmpPhsCnt++;
                        cmp -= 20;
                    }
                    float rat = getStateParam (RAT_PARAM);
					if (getInputConnected (RAT_INPUT)) {
						if (channel < ratChannels)
							lastRat = OL_statePoly[RAT_INPUT * POLY_CHANNELS + channel];
						rat = lastRat;
					}
					float dly = getStateParam (DLY_PARAM);
					if (getInputConnected (DLY_INPUT)) {
						if (channel < dlyChannels)
							lastDly = OL_statePoly[DLY_INPUT * POLY_CHANNELS + channel];
						dly = lastDly;
					}
					if (dly == 0.f)
						rat = 0.f;
					if (rat < 0) {
						rat *= -1;
						// DEBUG("before: rat = %lf, dly = %lf, cmp = %lf", rat, dly, cmp);
						while (cmp - dly * 20.f * int(rat) < -10.f) {
							rat --;
						}
						cmp -= dly * 20.f * int(rat);
						// DEBUG("after: rat = %lf, dly = %lf, cmp = %lf", rat, dly, cmp);
					}
                    if ((cmpPhsCnt == 0 && cmp <= phs) || cmpPhsCnt > 0) {
                        gateProcessed[channel] = true;
                        ratCnt[channel] = 1;
                        ratPhsCnt[channel] = cmpPhsCnt;
                        ratCmp[channel] = cmp;
                        float len = getStateParam (LEN_PARAM);
                        if (getInputConnected (LEN_INPUT)) {
                            if (channel < lenChannels)
                                lastLen = OL_statePoly[LEN_INPUT * POLY_CHANNELS + channel] * 10;
                            len += lastLen;
                        }
                        if (len < MIN_LEN)
							len = MIN_LEN;
                        gateLen[channel] = len;
						ratNum[channel] = rat;
                        ratCnt[channel] += rat;
                        ratDly[channel] = dly;
                    }
                }
            }
            checkRatchets (phs);
        }
        processActiveGates (phs);
	}

	inline void moduleCustomInitialize () {
	}

	/**
		Module specific input processing called from process () in OrangeLineCommon.hpp
		right after generic processParamsAndInputs ()

		moduleProcessState () should only be used to derive json state and module member variables
		from params and inputs collected by processParamsAndInputs ().

		This method should not do dsp or other logic processing.
	*/
	inline void moduleProcessState () {
	}	

	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges () {
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

/**
	Main Module Widget
*/
struct GatorWidget : ModuleWidget {

	GatorWidget(Gator *module) {

    // double sampleRate = (double)(APP->engine->getSampleRate ());

		setModule (module);
		setPanel (APP->window->loadSvg(asset::plugin (pluginInstance, "res/GatorOrange.svg")));

		if (module) {
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/GatorBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/GatorDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates ( 3.415,   9.512, OFFSET_PJ301MPort),          module,  PHS_INPUT ));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates (13.575,   9.512, OFFSET_PJ301MPort),          module,  CMP_INPUT ));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates ( 3.415,  27.292, OFFSET_PJ301MPort),          module, GATE_INPUT ));
        addParam  (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (13.780,  27.497, OFFSET_RoundSmallBlackKnob), module,  LEN_PARAM ));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates ( 3.415,  37.452, OFFSET_PJ301MPort),          module, TIME_INPUT ));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates (13.575,  37.452, OFFSET_PJ301MPort),          module,  LEN_INPUT ));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates ( 3.415,  49.644, OFFSET_PJ301MPort),          module,  JTR_INPUT ));
        addParam  (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (13.780,  49.849, OFFSET_RoundSmallBlackKnob), module,  JTR_PARAM ));
        RoundSmallBlackKnob *knob = 
                   createParamCentered<RoundSmallBlackKnob> (calculateCoordinates ( 3.620,  66.867, OFFSET_RoundSmallBlackKnob), module,  RAT_PARAM );
        knob->snap = true;
        addParam  (knob);
        addParam  (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (13.780,  66.867, OFFSET_RoundSmallBlackKnob), module,  DLY_PARAM ));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates ( 3.415,  76.963, OFFSET_PJ301MPort),          module,  RAT_INPUT ));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates (13.575,  76.963, OFFSET_PJ301MPort),          module,  DLY_INPUT ));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates ( 3.415,  93.078, OFFSET_PJ301MPort),          module,  STR_INPUT ));
        addParam  (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (13.780,  93.283, OFFSET_RoundSmallBlackKnob), module,  STR_PARAM ));
		addOutput (createOutputCentered<PJ301MPort>		    (calculateCoordinates (13.780, 109.842, OFFSET_PJ301MPort),          module, GATE_OUTPUT));
		addInput  (createInputCentered<PJ301MPort>          (calculateCoordinates ( 3.415, 109.842, OFFSET_PJ301MPort),          module,  RST_INPUT ));
	}

	struct GatorStyleItem : MenuItem {
		Gator *module;
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

		Gator *module = dynamic_cast<Gator*>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		GatorStyleItem *style1Item = new GatorStyleItem();
		style1Item->text = "Orange";// 
		style1Item->module = module;
		style1Item->style= STYLE_ORANGE;
		menu->addChild(style1Item);

		GatorStyleItem *style2Item = new GatorStyleItem();
		style2Item->text = "Bright";// 
		style2Item->module = module;
		style2Item->style= STYLE_BRIGHT;
		menu->addChild(style2Item);
			
		GatorStyleItem *style3Item = new GatorStyleItem();
		style3Item->text = "Dark";// 
		style3Item->module = module;
		style3Item->style= STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelGator = createModel<Gator, GatorWidget>("Gator");
