/*
	CV2CC.cpp

	Code for the OrangeLine module CV2CC

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

#include "CV2CC.hpp"

#define RATE_LIMITER_PERIOD (1.f / 200.f)

struct CV2CC : Module
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	midi::Output midiOutput;
	/** Last 7 bit value actually sent per CC. Init/reset to an out-of-range sentinel (0xFF)
		so the first tick after adding/resetting the module force-resends everything once. */
	uint8_t lastSentValues[128];
	dsp::Timer rateLimiterTimer;

	CV2CC()
	{
		for (int cc = 0; cc < 128; cc++)
			lastSentValues[cc] = 0xFF;

		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_object_set_new(rootJ, "midi", midiOutput.toJson());
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *midiJ = json_object_get(rootJ, "midi");
			if (midiJ)
				midiOutput.fromJson(midiJ);
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
			setInPoly(CC_INPUT + n, true);
		setStateTypeInput(FORCE_INPUT, STATE_TYPE_TRIGGER);
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel(STYLE_JSON, "style");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		configParam(FLUSH_PARAM, 0.f, 1.f, 0.f, "Force resend all CCs");
		for (int n = 0; n < 8; n++)
			configInput(CC_INPUT + n, string::f("CC %d-%d", n * 16, n * 16 + 15));
		configInput(FORCE_INPUT, "Force resend all CCs");
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		for (int cc = 0; cc < 128; cc++)
			lastSentValues[cc] = 0xFF;
		midiOutput.reset();
		rateLimiterTimer.reset();

		styleChanged = true;
	}

	/**
		Rate-limited (200Hz, like VCV Core's CV-CC) to avoid flooding the MIDI output with a
		message for every audio sample - moduleProcess() itself is already only called every
		~0.9ms (idleSkipCounter throttling), but that alone isn't enough headroom if many of
		the 128 CCs change on the same tick. Since moduleProcess() is skipped most samples,
		a plain dsp::Timer fed only this call's args.sampleTime would undercount elapsed real
		time by the skip factor - scale by (samplesSkipped + 1) to compensate.

		Both the FORCE trigger input and the FLUSH panel button are checked before the rate
		limiter's early return, since changeInput()/changeParam() are one-shot edge flags tied
		to the framework's real tick cadence, not to this rate limiter - checking them after a
		possible early return would silently drop the edge on ticks the limiter isn't due.
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

		bool forceFlush = changeInput(FORCE_INPUT) || (changeParam(FLUSH_PARAM) && getStateParam(FLUSH_PARAM) > 0.f);
		if (forceFlush)
		{
			for (int cc = 0; cc < 128; cc++)
				lastSentValues[cc] = 0xFF;
		}

		float elapsed = args.sampleTime * float(samplesSkipped + 1);
		if (rateLimiterTimer.process(elapsed) < RATE_LIMITER_PERIOD)
			return;
		rateLimiterTimer.time -= RATE_LIMITER_PERIOD;

		for (int n = 0; n < 8; n++)
		{
			bool connected = getInputConnected(CC_INPUT + n);
			int  channels  = connected ? inputs[CC_INPUT + n].getChannels() : 0;
			for (int c = 0; c < 16; c++)
			{
				int cc = n * 16 + c;
				float raw = (c < channels) ? OL_statePoly[(CC_INPUT + n) * POLY_CHANNELS + c] : 0.f;
				uint8_t value = (uint8_t) clamp(std::round(raw / 10.f * 127.f), 0.f, 127.f);
				if (value == lastSentValues[cc])
					continue;
				lastSentValues[cc] = value;

				midi::Message msg;
				msg.setStatus(0xb);
				msg.setNote(cc);
				msg.setValue(value);
				msg.setFrame(args.frame);
				midiOutput.sendMessage(msg);
			}
		}
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

/**
	Main Module Widget

	Jack/button centers and the MidiDisplay bounds are measured directly from the "Controls"
	layer of res/CV2CCWork.svg, so the widget lines up exactly with the panel art. The 8 CC
	inputs are arranged in a circle; wired here in clockwise order starting from the top, so
	CC bank N (CC N*16..N*16+15) always advances clockwise around the ring - same order as
	CC2CV's output ring. FORCE (trigger jack) and FLUSH (panel button) sit above the ring and
	both do the same thing: force every CC to be resent on the next update.
*/
struct CV2CCWidget : ModuleWidget
{
	CV2CCWidget(CV2CC *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CV2CCOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CV2CCBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CV2CCDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		MidiDisplay *display = createWidget<MidiDisplay>(calculateCoordinates(3.552f, 10.411f, 0.f));
		display->box.size = mm2px(Vec(43.688f, 28.194f));
		display->setMidiPort(module ? &module->midiOutput : NULL);
		addChild(display);

		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(13.970f, 54.103f, 0.f), module, FORCE_INPUT));
		addParam(createParamCentered<LEDButton>(calculateCoordinates(36.830f, 54.103f, 0.f), module, FLUSH_PARAM));

		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(25.400f, 80.265f, 0.f), module, CC_INPUT + 0));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(36.830f, 84.837f, 0.f), module, CC_INPUT + 1));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(41.402f, 96.267f, 0.f), module, CC_INPUT + 2));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(36.830f, 107.697f, 0.f), module, CC_INPUT + 3));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(25.400f, 112.269f, 0.f), module, CC_INPUT + 4));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(13.970f, 107.697f, 0.f), module, CC_INPUT + 5));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(9.398f, 96.267f, 0.f), module, CC_INPUT + 6));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(14.224f, 85.091f, 0.f), module, CC_INPUT + 7));

		if (module)
			module->widgetReady = true;
	}

	struct CV2CCStyleItem : MenuItem
	{
		CV2CC *module;
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

		CV2CC *module = dynamic_cast<CV2CC *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		CV2CCStyleItem *style1Item = new CV2CCStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		CV2CCStyleItem *style2Item = new CV2CCStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		CV2CCStyleItem *style3Item = new CV2CCStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelCV2CC = createModel<CV2CC, CV2CCWidget>("CV2CC");
