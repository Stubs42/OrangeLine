/*
	CC14.cpp

	Code for the OrangeLine module CC14

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

#include "CC14.hpp"

struct CC14 : Module
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	CC14()
	{
		initializeInstance();
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
		// All ports are mono - nothing to mark poly here.
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel (STYLE_JSON, "style");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		configInput (MSB_INPUT, "MIDI CC MSB (7 bit)");
		configInput (LSB_INPUT, "MIDI CC LSB (7 bit)");
		configOutput (CV_OUTPUT, "Combined 14 bit CV");

		configInput (CV_INPUT, "14 bit CV");
		configOutput (MSB_OUTPUT, "MIDI CC MSB (7 bit)");
		configOutput (LSB_OUTPUT, "MIDI CC LSB (7 bit)");
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		styleChanged = true;
	}

	/**
		Stateless: fully recomputed every control-rate tick. Two independent, unrelated
		conversions live in the same module purely because they're the inverse of each other:

		1. MSB IN + LSB IN (each 0-10V representing a MIDI CC's 7 bit value, 0-127) combine
		   into CV OUT (0-10V representing the resulting 14 bit value, 0-16383).
		2. CV IN (0-10V representing a 14 bit value) splits back into MSB OUT + LSB OUT
		   (each 0-10V representing a 7 bit value) - the exact inverse of (1).
	*/
	inline void moduleProcess(const ProcessArgs &args)
	{
		if (styleChanged && widgetReady)
		{
			switch (int(getStateJson(STYLE_JSON)))
			{
			case STYLE_ORANGE: brightPanel->visible = false; darkPanel->visible = false; break;
			case STYLE_BRIGHT: brightPanel->visible = true;  darkPanel->visible = false; break;
			case STYLE_DARK:   brightPanel->visible = false; darkPanel->visible = true;  break;
			}
			styleChanged = false;
		}

		// 1. MSB/LSB -> combined 14 bit CV
		int msb = (int) round (clamp (getStateInput (MSB_INPUT), 0.f, 10.f) / 10.f * 127.f);
		int lsb = (int) round (clamp (getStateInput (LSB_INPUT), 0.f, 10.f) / 10.f * 127.f);
		int combined14 = msb * 128 + lsb;
		setStateOutput (CV_OUTPUT, combined14 / 16383.f * 10.f);

		// 2. 14 bit CV -> MSB/LSB (exact inverse of step 1)
		int value14 = (int) round (clamp (getStateInput (CV_INPUT), 0.f, 10.f) / 10.f * 16383.f);
		setStateOutput (MSB_OUTPUT, (value14 >> 7)   / 127.f * 10.f);
		setStateOutput (LSB_OUTPUT, (value14 & 0x7F) / 127.f * 10.f);
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

/**
	Main Module Widget

	Jack centers below are measured directly from the "Controls" layer of res/CC14Work.svg,
	so the widget lines up exactly with the panel art. Order top to bottom: MSB IN, LSB IN,
	CV OUT (the "combine" group), then CV IN, MSB OUT, LSB OUT (the "split" group).
*/
struct CC14Widget : ModuleWidget
{
	CC14Widget(CC14 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CC14Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CC14Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CC14Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 14.479f, 0.f), module, MSB_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.626f, 28.692f, 0.f), module, LSB_INPUT));
		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.626f, 52.071f, 0.f), module, CV_OUTPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 71.629f, 0.f), module, CV_INPUT));
		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.620f, 93.981f, 0.f), module, MSB_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.626f, 109.221f, 0.f), module, LSB_OUTPUT));

		if (module)
			module->widgetReady = true;
	}

	struct CC14StyleItem : MenuItem
	{
		CC14 *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		CC14 *module = dynamic_cast<CC14 *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		CC14StyleItem *style1Item = new CC14StyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		CC14StyleItem *style2Item = new CC14StyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		CC14StyleItem *style3Item = new CC14StyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelCC14 = createModel<CC14, CC14Widget>("CC14");
