/*
	X8.cpp

	Code for the OrangeLine module X8

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

#include "X8.hpp"

#define X8_PANEL_WIDTH_MM 15.24f

struct X8 : Module, XExpanderInterface
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	// Resolved every moduleProcess() tick by looking only at rightExpander.module (never
	// leftExpander.module - see XShared.hpp's left-only-attachment note).
	XHostInterface *xHost = nullptr;

	// Fully self-managed local state (see ExpanderParamAccessSpec.md's "Expander manages
	// itself completely") - the Host only ever reads these through XExpanderInterface, it
	// never writes them.
	int browseIndex = 0;
	dsp::SchmittTrigger engageTrigger, leftTrigger, rightTrigger;
	bool pendingEngagePress = false;

	X8()
	{
		initializeInstance();
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
	}

	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel(STYLE_JSON, "style");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		for (int i = 0; i < NUM_X8_KNOBS; i++)
			configParam(KNOB_PARAM + i, 0.f, 1.f, 0.5f, "Value");
		configParam(LEFT_PARAM, 0.f, 1.f, 0.f, "Previous parameter");
		configParam(RIGHT_PARAM, 0.f, 1.f, 0.f, "Next parameter");
		configParam(ENGAGE_PARAM, 0.f, 1.f, 0.f, "Engage");
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		styleChanged = true;
		browseIndex = 0;
	}

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

		xHost = resolveXHost(rightExpander.module);
		setStateLight(CONN_LIGHT, xHost ? 255.f : 0.f);

		// Browsing: unconditional, unfiltered stepping through every candidate param the
		// currently-resolved Host reports - see "Browsing is never locked or filtered" in
		// ExpanderParamAccessSpec.md. If no Host is resolved (or it has zero candidates),
		// browseIndex just stays at 0 and stepping has no visible effect.
		int count = xHost ? xHost->getXParamCount() : 0;
		if (count > 0)
		{
			browseIndex = clamp(browseIndex, 0, count - 1);
			if (leftTrigger.process(params[LEFT_PARAM].getValue()))
				browseIndex = (browseIndex - 1 + count) % count;
			if (rightTrigger.process(params[RIGHT_PARAM].getValue()))
				browseIndex = (browseIndex + 1) % count;
		}
		else
		{
			browseIndex = 0;
			leftTrigger.process(params[LEFT_PARAM].getValue());
			rightTrigger.process(params[RIGHT_PARAM].getValue());
		}

		// Engage button: local debounce only - this Expander has no idea whether a click will
		// actually bind, unbind, or do nothing at all. The Host decides that, during its own
		// process(), the next time it reads consumeEngagePress().
		if (engageTrigger.process(params[ENGAGE_PARAM].getValue()))
			pendingEngagePress = true;
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}

	// XExpanderInterface
	XHostInterface* getXHost() override { return xHost; }
	float getXStyle() override { return OL_state[STYLE_JSON]; }
	int getXKnobCount() override { return NUM_X8_KNOBS; }
	float getXKnobValue(int channel) override { return getStateParam(KNOB_PARAM + channel); }
	int getXBrowseIndex() override { return browseIndex; }
	bool consumeEngagePress() override
	{
		bool fired = pendingEngagePress;
		pendingEngagePress = false;
		return fired;
	}
};

/**
	Main Module Widget
*/
struct X8Widget : ModuleWidget
{
	XExtStripWidget *extStrip = nullptr;

	X8Widget(X8 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addChild(createLightCentered<AutoHideLight<TinyLight<GreenLight>>>(calculateCoordinates(X8_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, CONN_LIGHT));

		// LEFT/RIGHT/ENGAGE - functional placeholders using the stock LEDButton component for
		// now; the panel's own custom rounded-rect button art (res/X8Work.svg's LEFT/RIGHT/
		// ENGAGE groups) still needs a dedicated custom ParamWidget to actually match visually -
		// deferred, tracked as follow-up work.
		addParam(createParamCentered<LEDButton>(calculateCoordinates(4.713f, 18.998f, OFFSET_LEDButton), module, LEFT_PARAM));
		addParam(createParamCentered<LEDButton>(calculateCoordinates(10.527f, 18.998f, OFFSET_LEDButton), module, RIGHT_PARAM));
		addParam(createParamCentered<LEDButton>(calculateCoordinates(7.62f, 25.601f, OFFSET_LEDButton), module, ENGAGE_PARAM));

		// 8 channel knobs, top (channel 1) to bottom (channel 8) - matches the panel's own
		// "1".."8" labels, which run top-to-bottom while the underlying knobring elements in the
		// SVG are numbered bottom-to-top (knobring8 is physically at the top).
		static const float knobY[NUM_X8_KNOBS] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		for (int i = 0; i < NUM_X8_KNOBS; i++)
			addParam(createParamCentered<RoundSmallBlackKnob>(calculateCoordinates(7.62f, knobY[i], OFFSET_RoundSmallBlackKnob), module, KNOB_PARAM + i));

		extStrip = addXExtStrip(this, X8_PANEL_WIDTH_MM);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		X8 *x8Module = dynamic_cast<X8 *>(module);
		if (x8Module)
			updateXExtStrip(extStrip, x8Module, x8Module->rightExpander.module);
		ModuleWidget::step();
	}

	struct X8StyleItem : MenuItem
	{
		X8 *module;
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

		X8 *module = dynamic_cast<X8 *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		X8StyleItem *style1Item = new X8StyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		X8StyleItem *style2Item = new X8StyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		X8StyleItem *style3Item = new X8StyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelX8 = createModel<X8, X8Widget>("X8");
