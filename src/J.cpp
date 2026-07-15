/*
	J.cpp

	Code for the OrangeLine module J (Join)

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

#include "J.hpp"

struct J : Module
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	dsp::PulseGenerator outPulse;
	bool wasAllHigh = false;

	J()
	{
		initializeInstance();
	}

	/*
		Never throttle - a plain simultaneous-AND across branches that may have diverged in
		length would defeat the whole purpose if we only checked every ~43 samples (see
		CLAUDE.md's Touch section - this module exists specifically to resync those branches).
	*/
	bool moduleSkipProcess()
	{
		return false;
	}

	void moduleInitStateTypes() {}

	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel (STYLE_JSON, "style");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		configParam (LEN_PARAM, 1.f, 10.f, 5.f, "Gate length", " ms");

		for (int i = 0; i < NUM_INPUTS; i++)
			configInput (IN_INPUT + i, string::f("Input %d", i + 1));

		configOutput (OUT_OUTPUT, "READY");
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		styleChanged = true;
	}

	/**
		Plain AND (no latch/barrier) of every *connected* input's level against
		AND_THRESHOLD. On the rising edge of that AND condition (false -> true), fires a
		fixed-length pulse on OUT_OUTPUT - length set by LEN_PARAM (1-10ms, default 5ms),
		independent of how long the inputs stay high afterward. Unconnected inputs are
		excluded from the AND, not treated as false.
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

		bool allHigh = true;
		for (int i = 0; i < NUM_INPUTS; i++)
		{
			if (!getInputConnected(i))
				continue;
			if (getStateInput(i) <= AND_THRESHOLD)
			{
				allHigh = false;
				break;
			}
		}

		if (allHigh && !wasAllHigh)
			outPulse.trigger (getStateParam(LEN_PARAM) / 1000.f);
		wasAllHigh = allHigh;

		setStateOutput (OUT_OUTPUT, outPulse.process ((float) args.sampleTime) ? 10.f : 0.f);
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

/**
	Main Module Widget

	Jack/knob centers measured directly from the "Controls" layer of res/JWork.svg, so the
	widget lines up exactly with the panel art. Single column at x=7.62mm: 8 inputs (y=14.478
	to y=85.598, 40-grid-unit pitch), the gate-length knob (y=100.07725), then the output
	(y=114.022, single-column corner-diagonal position per PanelDesignGuide.md).
*/
struct JWidget : ModuleWidget
{
	JWidget(J *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/JOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/JBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/JDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		static const float IN_Y[8] = { 14.478f, 24.638f, 34.798f, 44.958f, 55.118f, 65.278f, 75.438f, 85.598f };
		for (int i = 0; i < 8; i++)
			addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.62f, IN_Y[i], 0.f), module, IN_INPUT + i));

		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (7.62f, 100.07725f, 0.f), module, LEN_PARAM));

		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.62f, 114.022f, 0.f), module, OUT_OUTPUT));

		if (module)
			module->widgetReady = true;
	}

	struct JStyleItem : MenuItem
	{
		J *module;
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

		J *module = dynamic_cast<J *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		JStyleItem *style1Item = new JStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		JStyleItem *style2Item = new JStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		JStyleItem *style3Item = new JStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelJ = createModel<J, JWidget>("J");
