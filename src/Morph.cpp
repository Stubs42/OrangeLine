/*
	Morph.cpp

	Code for the OrangeLine module Morph

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

#include "Morph.hpp"

struct Morph : Module
{
	float oldClkInputVoltage = 0;
    int polyChannels = 1;
    int shift[POLY_CHANNELS];
    bool clear[POLY_CHANNELS];

#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	bool widgetReady = false;

	// ********************************************************************************************************************************
	/*
		Initialization
	*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Morph()
	{
		initializeInstance();
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
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
	void moduleInitStateTypes()
	{
   		setStateTypeInput  (CLK_INPUT, STATE_TYPE_TRIGGER);

   		setInPoly (SRC_GATE_INPUT, true);
   		setInPoly (SRC_CV_INPUT, true);
   		setInPoly (SRC_FORCE_INPUT, true);

   		setInPoly (LOCK_GATE_INPUT, true);
   		setInPoly (LOCK_BOTH_INPUT, true);
   		setInPoly (LOCK_CV_INPUT, true);

   		setInPoly (SRC_RND_INPUT, true);
   		setInPoly (SHIFT_LEFT_INPUT, true);
   		setInPoly (SHIFT_RIGHT_INPUT, true);
   		setInPoly (CLR_INPUT, true);

   		setInPoly (LOOP_LEN_INPUT, true);
   		setInPoly (RND_GATE_INPUT, true);
   		setInPoly (RND_SCL_INPUT, true);
   		setInPoly (RND_OFF_INPUT, true);

   		setOutPoly (GATE_OUTPUT, true);
   		setOutPoly (CV_OUTPUT, true);
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		//
		// Config internal Parameters not bound to a user interface object
		//

		setJsonLabel(STYLE_JSON, "style");
		setJsonLabel(POLY_CHANNELS_JSON, "polyChannels");

#include "MorphJsonLoopLabels.hpp"

#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig()
	{
//	void configParam(int paramId, float minValue, float maxValue, float defaultValue, std::string label = "", std::string unit = "", 
//                   float displayBase = 0.f, float displayMultiplier = 1.f, float displayOffset = 0.f) {

   		configParam (LOCK_GATE_PARAM,   0.f, 100.f,  0.f, "Lock Gate",      "%", 0.f, 1.f, 0.f);
   		configParam (LOCK_BOTH_PARAM,   0.f, 100.f,  0.f, "Lock Gate & CV", "%", 0.f, 1.f, 0.f);
   		configParam (LOCK_CV_PARAM,     0.f, 100.f,  0.f, "Lock CV",        "%", 0.f, 1.f, 0.f);

   		configParam (SRC_RND_PARAM,     0.f, 100.f,100.f, "Source(0%) to Random(100%) Balance", "%", 0.f, 1.f, 0.f);
        configParam (SHIFT_LEFT_PARAM,  0.f,   1.f,  0.f, "Shift Left (Stop Loop) one step",     "", 0.f, 1.f, 0.f);
        configParam (SHIFT_RIGHT_PARAM, 0.f,   1.f,  0.f, "Shift Right (Skip Loop) one step",    "", 0.f, 1.f, 0.f);
        configParam (CLR_PARAM,         0.f,   1.f,  0.f, "Clear Loop (no Gates, CV 0V)",        "", 0.f, 1.f, 0.f);

   		configParam (LOOP_LEN_PARAM,    1.f,  MAX_LOOP_LEN, 16.f, "Loop Length", "", 0.f, 1.f, 0.f);
        paramQuantities[LOOP_LEN_PARAM]->snapEnabled = true;

   		configParam (RND_GATE_PARAM,    0.f, 100.f, 50.f, "Random Gate Probability", "%", 0.f, 1.f, 0.f);
   		configParam (RND_SCL_PARAM,    -1.f,   1.f,  1.f, "Random CV Scale",          "", 0.f, 1.f, 0.f);
   		configParam (RND_OFF_PARAM,   -10.f,  10.f,  0.f, "Random CV Offset",         "", 0.f, 1.f, 0.f);

		configInput (CLK_INPUT, "Clock");
		configInput (SRC_GATE_INPUT, "Source Gate [poly]");
		configInput (SRC_CV_INPUT, "Source CV [poly]");
		configInput (SRC_FORCE_INPUT, "Source Force write through [poly]");

		configInput (LOCK_GATE_INPUT, "Lock Gate [poly]");
		configInput (LOCK_BOTH_INPUT, " Lock gate & CV [poly]");
		configInput (LOCK_CV_INPUT, "Lock CV [poly]");

   		configInput (SRC_RND_INPUT, "Source Random Balance [poly]");
		configInput (SHIFT_LEFT_INPUT, "Shift Left[poly]");
		configInput (SHIFT_RIGHT_INPUT, "Shift Right [poly]");
		configInput (CLR_INPUT, "Clear Loop [poly]");

		configInput (LOOP_LEN_INPUT, "Loop Length [poly]");
		configInput (RND_GATE_INPUT, "Random Gate Propability (Gate Density) [poly]");
		configInput (RND_SCL_INPUT, "Random CV Scale [poly]");
		configInput (RND_OFF_INPUT, "Random CV Offset [poly]");

   		configOutput (GATE_OUTPUT, "Gate");
		configOutput (CV_OUTPUT, "CV");

   		configBypass (SRC_GATE_INPUT, GATE_OUTPUT);
   		configBypass (SRC_CV_INPUT, CV_OUTPUT);
	}

	inline void moduleCustomInitialize()
	{
	}

	/**
		Method to initialize the module after loading a patch or a preset
		Called from initialize () included from from OrangeLineCommon.hpp
		to initialize module state from a valid
		json state after module was added to the patch,
		a call to dataFromJson due to patch or preset load
		or a right click initialize (reset).
	*/
	inline void moduleInitialize()
	{
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
		Currently called twice when add a module to patch ...
	*/
	void moduleReset()
	{
        setStateJson(POLY_CHANNELS_JSON, 0.f);

        for (int i = STEPS_JSON; i < STEPS_JSON + POLY_CHANNELS * MAX_LOOP_LEN * 2; i++) {
            setStateJson(i, 0.f);
        }
        for (int i = HEAD_JSON; i < HEAD_JSON + POLY_CHANNELS; i++) {
            setStateJson(i, 0.f);
        }
        for (int i = 0; i < POLY_CHANNELS; i ++) {
            shift[i] = 0;
            clear[i] = 0;
        }
		styleChanged = true;
	}

	// ********************************************************************************************************************************
	/*
		Module specific utility methods
	*/

	// ********************************************************************************************************************************
	/*
		Methods called directly or indirectly called from process () in OrangeLineCommon.hpp
	*/
	/**
		Module specific process method called from process () in OrangeLineCommon.hpp
	*/
	inline void moduleProcess(const ProcessArgs &args)
	{
		if (styleChanged && widgetReady)
		{
			switch (int(getStateJson(STYLE_JSON)))
			{
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

        // Collect shifts and Clears

        for (int channel = 0; channel < polyChannels; channel ++) { 
            
            // get shifts
            if (!getInputConnected(SHIFT_LEFT_INPUT)) {
                if(changeParam(SHIFT_LEFT_PARAM) && getStateParam(SHIFT_LEFT_PARAM) > 0.f) {
                    shift[channel] -= 1;
                }
            }
            else {
                if (channel < inputs[SHIFT_LEFT_INPUT].getChannels() && 
                    OL_statePoly[SHIFT_LEFT_INPUT * POLY_CHANNELS + channel] > 0.f) {
                    shift[channel] -= 1;
                }
            }
            if (!getInputConnected(SHIFT_RIGHT_INPUT)) {
                if(changeParam(SHIFT_RIGHT_PARAM) && getStateParam(SHIFT_RIGHT_PARAM) > 0.f) {
                    shift[channel] += 1;
                }
            }
            else {
                if (channel < inputs[SHIFT_RIGHT_INPUT].getChannels() && 
                    OL_statePoly[SHIFT_RIGHT_INPUT * POLY_CHANNELS + channel] > 0.f) {
                    shift[channel] += 1;
                }
            } 

            // get clears
            if (!getInputConnected(CLR_INPUT)) {
                if(changeParam(CLR_PARAM) && getStateParam(CLR_PARAM) > 0.f) {
                    clear[channel] = true;
                }
            }
            else {
                if (channel < inputs[CLR_INPUT].getChannels() && 
                    OL_statePoly[CLR_INPUT * POLY_CHANNELS + channel] > 0.f) {
                    clear[channel] = true;
                }
            }   
        }

        // No furuther Action if no clock arrived
        if (changeInput (CLK_INPUT) && getStateInput(CLK_INPUT) > 0.f) {

// DEBUG("CLK");

            // get polyChannels
            polyChannels = getStateJson(POLY_CHANNELS_JSON);
            if (polyChannels == 0) {
                polyChannels = 1;
                // channels is auto, derive maximum channels of all polyphonic inputs
                for (int i = SRC_GATE_INPUT; i <= RND_OFF_INPUT; i ++) {
                    if (getInputConnected(i)) {
                        int channels = inputs[i].getChannels();
                        if (channels > polyChannels) {
                            polyChannels = channels;
                        }
                    }
                }
            }

// DEBUG("polyChannels = %d", polyChannels);

            // proces for each channel
            for (int channel = 0; channel < polyChannels; channel ++) { 
                // get Loop length
                int loopLen = getStateParam(LOOP_LEN_PARAM);
                if (getInputConnected(LOOP_LEN_INPUT)) {
                    if (channel < inputs[LOOP_LEN_INPUT].getChannels()) {
                        loopLen = OL_statePoly[LOOP_LEN_INPUT * POLY_CHANNELS + channel];
                    }
                }
// DEBUG("[%d] loopLen = %d", channel, loopLen);
                // clear if requested
                if (clear[channel]) {
                    for (int step = 0; step < loopLen; step ++) {
                        setStateJson(STEPS_JSON + channel * MAX_LOOP_LEN * 2 + step * 2, 0.f);
                        setStateJson(STEPS_JSON + channel * MAX_LOOP_LEN * 2 + step * 2 + 1, 0.f);
                        setStateJson(HEAD_JSON + channel, 0.f);
                    }
// DEBUG("[%d] CLR", channel);
                    clear[channel] = false;
                    shift[channel] = 0;
                }
                // Do shift
                int head = getStateJson(HEAD_JSON + channel);
                head += shift[channel];
                shift[channel] = 0;
                while (head < 0) {
                    head += loopLen;
                }
                head %= loopLen;

                // read from loop
                float gate = getStateJson(STEPS_JSON + channel * MAX_LOOP_LEN * 2 + head * 2);
                float cv   = getStateJson(STEPS_JSON + channel * MAX_LOOP_LEN * 2 + head * 2 + 1);

// DEBUG("[%d] from loop [%lf,%lf], head = %d", channel, gate, cv, head);

                // get force
                bool force = false;
                if (getInputConnected(SRC_FORCE_INPUT)) {
                    if (channel < inputs[SRC_FORCE_INPUT].getChannels()) {
                        if (OL_statePoly[SRC_FORCE_INPUT * POLY_CHANNELS + channel] >= 5.f) {
                            force = true;
                        }
                    }
                } 
                // get and evaluate lock values
                float lockGate = getStateParam(LOCK_GATE_PARAM);
                if (getInputConnected(LOCK_GATE_INPUT)) {
                    if (channel < inputs[LOCK_GATE_INPUT].getChannels()) {
                        lockGate = OL_statePoly[LOCK_GATE_INPUT * POLY_CHANNELS + channel];
                    }
                }
                float lockCv = getStateParam(LOCK_CV_PARAM);
                if (getInputConnected(LOCK_CV_INPUT)) {
                    if (channel < inputs[LOCK_CV_INPUT].getChannels()) {
                        lockCv = OL_statePoly[LOCK_CV_INPUT * POLY_CHANNELS + channel];
                    }
                }
                float lockBoth = getStateParam(LOCK_BOTH_PARAM);
                if (getInputConnected(LOCK_BOTH_INPUT)) {
                    if (channel < inputs[LOCK_BOTH_INPUT].getChannels()) {
                        lockBoth = OL_statePoly[LOCK_BOTH_INPUT * POLY_CHANNELS + channel];
                    }
                }
                float srcRnd = -1.f;
                // process gates
                lockGate += lockBoth;
                float rndGate = getRandom(&globalRandom);
// DEBUG("[%d] rndGate = %lf, lockGate = %lf]", channel, rndGate, lockGate);
                if (rndGate * 100 > lockGate || force) {
// DEBUG("[%d] Gate Morph", channel);
                    // We have to get the gate from src or random
                    srcRnd = getStateParam(SRC_RND_PARAM);
                    if (getInputConnected(SRC_RND_INPUT)) {
                        if (channel < inputs[SRC_RND_INPUT].getChannels()) {
                            srcRnd = OL_statePoly[SRC_RND_INPUT * POLY_CHANNELS + channel];
                        }
                    }
                    // check where to go
                    float rndSrcRnd = getRandom(&globalRandom);
                    if (rndSrcRnd * 100 > srcRnd || force) {
// DEBUG("[%d] from Source", channel);
                        // we go src
                        if (getInputConnected(SRC_GATE_INPUT) && inputs[SRC_GATE_INPUT].getChannels() > channel) {
                            if (OL_statePoly[SRC_GATE_INPUT * POLY_CHANNELS + channel] < 5.0) {
                                gate = 0.f;
                            }
                            else {
                                gate = 10.f;
                            }
                        }
                    }
                    else {
// DEBUG("[%d] from Random", channel);
                        // we go random
                        float rndGateInp = getStateParam(RND_GATE_PARAM);
                        if (getInputConnected(RND_GATE_INPUT)) {
                            if (channel < inputs[RND_GATE_INPUT].getChannels()) {
                                rndGateInp = OL_statePoly[RND_GATE_INPUT * POLY_CHANNELS + channel];
                            }
                        }
                        float rndGateRnd = getRandom(&globalRandom);
// DEBUG("[%d] rndGateRnd = %lf, rndGateInp = %lf", channel, rndGateRnd, rndGateInp);
                        if (rndGateRnd * 100.f > rndGateInp) {
                            gate = 0.f;
                        }
                        else {
                            gate = 10.f;
                        }
                    }
                    // write back to loop
// DEBUG("[%d] setStateJson(%d, %lf), head = %d", channel, STEPS_JSON + channel * MAX_LOOP_LEN * 2 + head * 2, gate, head);
                    setStateJson(STEPS_JSON + channel * MAX_LOOP_LEN * 2 + head * 2, gate);
               }

                // process cvs
                lockCv += lockBoth;
                float rndCv = getRandom(&globalRandom);
                if (rndCv * 100 > lockCv || force) {
                    // We have to get the cv from src or random
                    if (srcRnd == -1.f) {
                        srcRnd = getStateParam(SRC_RND_PARAM);
                        if (getInputConnected(SRC_RND_INPUT)) {
                            if (channel < inputs[SRC_RND_INPUT].getChannels()) {
                                srcRnd = OL_statePoly[SRC_RND_INPUT * POLY_CHANNELS + channel];
                            }
                        }
                    }
                    // check where to go
                    float rndSrcRnd = getRandom(&globalRandom);
                    if (rndSrcRnd * 100 > srcRnd || force) {
                        // we go src
                        if (getInputConnected(SRC_CV_INPUT) && inputs[SRC_CV_INPUT].getChannels() > channel) {
                            cv = OL_statePoly[SRC_CV_INPUT * POLY_CHANNELS + channel];
                        }
                    }
                    else {
                        // we go random
                        float rndSclInp = getStateParam(RND_SCL_PARAM) * 10.f;
                        if (getInputConnected(RND_SCL_INPUT)) {
                            if (channel < inputs[RND_SCL_INPUT].getChannels()) {
                                rndSclInp = OL_statePoly[RND_SCL_INPUT * POLY_CHANNELS + channel];
                            }
                        }
                        float rndOffInp = getStateParam(RND_OFF_PARAM);
                        if (getInputConnected(RND_OFF_INPUT)) {
                            if (channel < inputs[RND_OFF_INPUT].getChannels()) {
                                rndOffInp = OL_statePoly[RND_OFF_INPUT * POLY_CHANNELS + channel];
                            }
                        }

                        float rndCvRnd = getRandom(&globalRandom);
                        if (rndSclInp >= 0) {
                            // unipolar cv
                            cv = rndCvRnd * rndSclInp + rndOffInp;
                        }
                        else {
                            cv = rndCvRnd * -rndSclInp * 2 - 10.f + rndOffInp;
                        }
                    }
                    // write back change to loop
// DEBUG("[%d] setStateJson(%d, %lf), head = %d", channel, STEPS_JSON + channel * MAX_LOOP_LEN * 2 + head * 2 + 1, gate, head);
                    setStateJson(STEPS_JSON + channel * MAX_LOOP_LEN * 2 + head * 2 + 1, cv);
                }

                // further processing here

                // write outputs
// DEBUG("[%d] final [%lf,%lf]", channel, gate, cv);

                setStateOutPoly(GATE_OUTPUT, channel, gate);
                setStateOutPoly(CV_OUTPUT, channel, cv);

                // Advance head
                head ++;
                head %= loopLen;
                setStateJson(HEAD_JSON + channel, head);
            }
            setOutPolyChannels(GATE_OUTPUT, polyChannels);
            setOutPolyChannels(CV_OUTPUT, polyChannels);
        } 
	}

	/**
		Module specific input processing called from process () in OrangeLineCommon.hpp
		right after generic processParamsAndInputs ()

		moduleProcessState () should only be used to derive json state and module member variables
		from params and inputs collected by processParamsAndInputs ().

		This method should not do dsp or other logic processing.
	*/
	inline void moduleProcessState()
	{
	}

	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges()
	{
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

/**
	Main Module Widget
*/
struct MorphWidget : ModuleWidget
{

	char divBuffer[3];
	char lenBuffer[3];

	MorphWidget(Morph *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorphOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorphBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorphDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates ( 2.858, 31.307, OFFSET_RoundSmallBlackKnob),  module, LOCK_GATE_PARAM));
   		addParam (createParamCentered<RoundLargeBlackKnob> (calculateCoordinates (16.510, 28.957, OFFSET_RoundLargeBlackKnob),  module, LOCK_BOTH_PARAM));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (34.862, 31.307, OFFSET_RoundSmallBlackKnob),  module, LOCK_CV_PARAM));

		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates ( 2.653, 58.993, OFFSET_RoundSmallBlackKnob),  module, SRC_RND_PARAM));

        addParam (createParamCentered<LEDButton> (calculateCoordinates (15.145, 60.612, OFFSET_LEDButton), module, SHIFT_LEFT_PARAM));
        addParam (createParamCentered<LEDButton> (calculateCoordinates (25.814, 60.612, OFFSET_LEDButton), module, SHIFT_RIGHT_PARAM));
        addParam (createParamCentered<LEDButton> (calculateCoordinates (36.482, 60.612, OFFSET_LEDButton), module, CLR_PARAM));

        RoundSmallBlackKnob *knob;
        knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates ( 2.858, 87.187, OFFSET_RoundSmallBlackKnob),  module, LOOP_LEN_PARAM);
        knob->snap = true;
        addParam (knob);

		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (13.526, 87.187, OFFSET_RoundSmallBlackKnob),  module, RND_GATE_PARAM));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (24.194, 87.187, OFFSET_RoundSmallBlackKnob),  module, RND_SCL_PARAM));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (34.862, 87.187, OFFSET_RoundSmallBlackKnob),  module, RND_OFF_PARAM));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 2.653, 11.544, OFFSET_PJ301MPort), module, CLK_INPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (13.321, 11.544, OFFSET_PJ301MPort), module, SRC_GATE_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (23.989, 11.544, OFFSET_PJ301MPort), module, SRC_CV_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (34.657, 11.544, OFFSET_PJ301MPort), module, SRC_FORCE_INPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 2.653, 43.294, OFFSET_PJ301MPort), module, LOCK_GATE_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (18.655, 43.294, OFFSET_PJ301MPort), module, LOCK_BOTH_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (34.657, 43.294, OFFSET_PJ301MPort), module, LOCK_CV_INPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 2.653, 68.694, OFFSET_PJ301MPort), module, SRC_RND_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (13.321, 68.694, OFFSET_PJ301MPort), module, SHIFT_LEFT_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (23.989, 68.694, OFFSET_PJ301MPort), module, SHIFT_RIGHT_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (34.657, 68.694, OFFSET_PJ301MPort), module, CLR_INPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 2.653, 96.634, OFFSET_PJ301MPort), module, LOOP_LEN_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (13.321, 96.634, OFFSET_PJ301MPort), module, RND_GATE_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (23.989, 96.634, OFFSET_PJ301MPort), module, RND_SCL_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (34.657, 96.634, OFFSET_PJ301MPort), module, RND_OFF_INPUT));

   		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (13.321, 110.604, OFFSET_PJ301MPort),  module, GATE_OUTPUT));
   		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (23.989, 110.604, OFFSET_PJ301MPort),  module, CV_OUTPUT));


		if (module)
			module->widgetReady = true;
	}

	struct MorphStyleItem : MenuItem
	{
		Morph *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = ( module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

    struct PolyChannelsItem : MenuItem
	{
		Morph *module;

		struct PolyChannelItem : MenuItem
		{
			Morph *module;
			int channels;
			void onAction(const event::Action &e) override
			{
				module->OL_setOutState(POLY_CHANNELS_JSON, float(channels));
			}
			void step() override
			{
				if (module)
					rightText = (module != nullptr && module->OL_state[POLY_CHANNELS_JSON] == channels) ? "✔" : "";
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			PolyChannelItem *polyChannelItem;

			for (int channel = 0; channel < 17; channel++)
			{
				polyChannelItem = new PolyChannelItem();
				polyChannelItem->module = module;
				polyChannelItem->channels = channel;
                if (channel == 0) {
                    polyChannelItem->text = "Auto";
                }
                else {
    				polyChannelItem->text = module->channelNumbers[channel - 1];
                }
				polyChannelItem->setSize(Vec(70, 20));

				menu->addChild(polyChannelItem);
			}
			return menu;
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Morph *module = dynamic_cast<Morph *>(this->module);
		assert(module);

        MenuLabel *polyphonyLabel = new MenuLabel();
        polyphonyLabel->text = "Polyphony";
        menu->addChild(polyphonyLabel);

        PolyChannelsItem *polyChannelsItem = new PolyChannelsItem();
        polyChannelsItem->module = module;
        polyChannelsItem->text = "Channels";
        polyChannelsItem->rightText = RIGHT_ARROW;
        menu->addChild(polyChannelsItem);

        spacerLabel = new MenuLabel();
        menu->addChild(spacerLabel);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		MorphStyleItem *style1Item = new MorphStyleItem();
		style1Item->text = "Orange"; //
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		MorphStyleItem *style2Item = new MorphStyleItem();
		style2Item->text = "Bright"; //
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		MorphStyleItem *style3Item = new MorphStyleItem();
		style3Item->text = "Dark"; //
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelMorph = createModel<Morph, MorphWidget>("Morph");
