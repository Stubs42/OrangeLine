/*
	CC2CV.cpp

	Code for the OrangeLine module CC2CV

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

#include "CC2CV.hpp"

struct CC2CV : Module
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	midi::InputQueue midiInput;
	/** Last known raw 7 bit value per CC (0-127), persisted so a reload doesn't need the
		controller to be touched again. */
	uint8_t ccValues[128];
	dsp::ExponentialFilter valueFilters[128];

	CC2CV()
	{
		for (int cc = 0; cc < 128; cc++)
		{
			ccValues[cc] = 0;
			valueFilters[cc].setTau(1.f / 30.f);
		}

		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_t *valuesJ = json_array();
			for (int cc = 0; cc < 128; cc++)
				json_array_append_new(valuesJ, json_integer(ccValues[cc]));
			json_object_set_new(rootJ, "values", valuesJ);
			json_object_set_new(rootJ, "midi", midiInput.toJson());
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *valuesJ = json_object_get(rootJ, "values");
			if (valuesJ)
			{
				for (int cc = 0; cc < 128; cc++)
				{
					json_t *valueJ = json_array_get(valuesJ, cc);
					if (valueJ)
						ccValues[cc] = json_integer_value(valueJ);
				}
			}
			json_t *midiJ = json_object_get(rootJ, "midi");
			if (midiJ)
				midiInput.fromJson(midiJ);
		};

		initializeInstance();
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
		for (int n = 0; n < 8; n++)
			setOutPoly(CC_OUTPUT + n, true);
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel(STYLE_JSON, "style");
		setJsonLabel(SMOOTH_JSON, "smooth");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		for (int n = 0; n < 8; n++)
			configOutput(CC_OUTPUT + n, string::f("CC %d-%d", n * 16, n * 16 + 15));
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		for (int cc = 0; cc < 128; cc++)
			ccValues[cc] = 0;
		midiInput.reset();
		setStateJson(SMOOTH_JSON, 1.f);

		styleChanged = true;
	}

	/**
		Drains the incoming MIDI CC queue into ccValues[], then maps all 128 values onto the
		8 poly outputs (channel c of output n = CC n*16+c). Optional exponential smoothing
		(SMOOTH_JSON) turns the stepped 7 bit resolution into a continuous CV, with a jump
		detection escape hatch so MIDI buttons still snap instantly.
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

		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame))
		{
			if (msg.getStatus() == 0xb && msg.getSize() >= 3)
				ccValues[msg.getNote()] = msg.getValue();
		}

		bool smooth = getStateJson(SMOOTH_JSON) != 0.f;

		for (int n = 0; n < 8; n++)
		{
			for (int c = 0; c < 16; c++)
			{
				int cc = n * 16 + c;
				float value = ccValues[cc] / 127.f * 10.f;
				if (smooth)
				{
					if (std::fabs(valueFilters[cc].out - value) < 1.f)
						valueFilters[cc].process(args.sampleTime, value);
					else
						valueFilters[cc].out = value;
					value = valueFilters[cc].out;
				}
				else
				{
					valueFilters[cc].out = value;
				}
				setStateOutPoly(CC_OUTPUT + n, c, value);
			}
			setOutPolyChannels(CC_OUTPUT + n, 16);
		}
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

/**
	Main Module Widget

	Jack centers and the MidiDisplay bounds are measured directly from the "Controls" layer
	of res/CC2CVWork.svg, so the widget lines up exactly with the panel art. The 8 outputs
	are arranged in a circle on the panel; they're wired here in clockwise order starting
	from the top, so CC bank N (CC N*16..N*16+15) always advances clockwise around the ring.
*/
struct CC2CVWidget : ModuleWidget
{
	CC2CVWidget(CC2CV *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CC2CVOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CC2CVBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CC2CVDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		MidiDisplay *display = createWidget<MidiDisplay>(calculateCoordinates(3.552f, 10.411f, 0.f));
		display->box.size = mm2px(Vec(43.688f, 28.194f));
		display->setMidiPort(module ? &module->midiInput : NULL);
		addChild(display);

		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(25.400f, 80.265f, 0.f), module, CC_OUTPUT + 0));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(36.830f, 84.837f, 0.f), module, CC_OUTPUT + 1));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(41.402f, 96.267f, 0.f), module, CC_OUTPUT + 2));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(36.830f, 107.697f, 0.f), module, CC_OUTPUT + 3));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(25.400f, 112.269f, 0.f), module, CC_OUTPUT + 4));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(13.970f, 107.697f, 0.f), module, CC_OUTPUT + 5));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(9.398f, 96.267f, 0.f), module, CC_OUTPUT + 6));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(13.970f, 84.837f, 0.f), module, CC_OUTPUT + 7));

		if (module)
			module->widgetReady = true;
	}

	struct CC2CVStyleItem : MenuItem
	{
		CC2CV *module;
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

	struct CC2CVSmoothItem : MenuItem
	{
		CC2CV *module;
		void onAction(const event::Action &e) override
		{
			module->setStateJson(SMOOTH_JSON, module->getStateJson(SMOOTH_JSON) == 0.f ? 1.f : 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module->getStateJson(SMOOTH_JSON) != 0.f) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		CC2CV *module = dynamic_cast<CC2CV *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		CC2CVStyleItem *style1Item = new CC2CVStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		CC2CVStyleItem *style2Item = new CC2CVStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		CC2CVStyleItem *style3Item = new CC2CVStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		CC2CVSmoothItem *smoothItem = new CC2CVSmoothItem();
		smoothItem->module = module;
		smoothItem->text = "Smooth CC";
		menu->addChild(smoothItem);
	}
};

Model *modelCC2CV = createModel<CC2CV, CC2CVWidget>("CC2CV");
