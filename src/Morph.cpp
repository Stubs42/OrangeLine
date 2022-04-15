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

   		configParam (LOOP_LEN_PARAM,    1.f,  64.f, 16.f, "Loop Length", "", 0.f, 1.f, 0.f);
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
