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
// Per-tick decay for the CCGridWidget's activity flash, tuned for moduleProcess()'s
// ~1.1kHz control rate (idleSkipCounter throttling) - fades to 0 in roughly 300ms.
#define CC_ACTIVITY_DECAY 0.003f

struct CV2CC : Module
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	midi::Output midiOutput;
	/** Last 7 bit value actually sent per CC. Init/reset to an out-of-range sentinel (0xFF)
		so the first tick after adding/resetting the module force-resends everything once,
		unless FLUSH_ON_START_JSON is off (see needsInitialSeed below). */
	uint8_t lastSentValues[128];
	dsp::Timer rateLimiterTimer;
	/** True until the first time the send loop actually runs after construction/reset. Only
		meaningful when FLUSH_ON_START_JSON is off: instead of sending on that first pass (the
		sentinel mismatch would otherwise force one), silently seed lastSentValues from the
		live CV so nothing goes out until something genuinely changes. A real FORCE/FLUSH in
		the meantime still always sends, regardless of this flag. */
	bool needsInitialSeed = true;
	/** Per-CC on/off mask (CCGridWidget), persisted. A disabled CC is never sent, regardless
		of value changes or FORCE/FLUSH - lets you keep unused CCs from "polluting" the
		outgoing MIDI stream. */
	bool  ccEnabled[128];
	/** Shadow of ccEnabled from the previous tick, used to detect a disabled->enabled
		transition so that CC's lastSentValues sentinel can be reset - re-enabling a CC
		immediately re-syncs its current value instead of waiting for the next change. */
	bool  ccWasEnabled[128];
	/** Per-CC 0-1 activity level for the grid's live traffic display, decayed every tick.
		Only set when a CC is actually sent (so a disabled CC never flashes). Not persisted. */
	float ccActivity[128];
	/** Current incoming 0-10V value per CC (pre-mute - this is the same value the grid's cell
		color is based on), so a muted CC's live input value still shows. Not persisted. */
	float ccCvValue[128];

	CV2CC()
	{
		for (int cc = 0; cc < 128; cc++)
		{
			lastSentValues[cc] = 0xFF;
			ccEnabled[cc] = true;
			ccWasEnabled[cc] = true;
			ccActivity[cc] = 0.f;
			ccCvValue[cc] = 0.f;
		}

		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_t *enabledJ = json_array();
			for (int cc = 0; cc < 128; cc++)
				json_array_append_new(enabledJ, json_boolean(ccEnabled[cc]));
			json_object_set_new(rootJ, "enabled", enabledJ);
			json_object_set_new(rootJ, "midi", midiOutput.toJson());
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *enabledJ = json_object_get(rootJ, "enabled");
			if (enabledJ)
			{
				for (int cc = 0; cc < 128; cc++)
				{
					json_t *flagJ = json_array_get(enabledJ, cc);
					if (flagJ)
						ccEnabled[cc] = ccWasEnabled[cc] = json_boolean_value(flagJ);
				}
			}
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
		setJsonLabel(FLUSH_ON_START_JSON, "flushOnStart");

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
		{
			lastSentValues[cc] = 0xFF;
			ccEnabled[cc] = true;
			ccWasEnabled[cc] = true;
			ccActivity[cc] = 0.f;
			ccCvValue[cc] = 0.f;
		}
		midiOutput.reset();
		rateLimiterTimer.reset();
		needsInitialSeed = true;
		setStateJson(FLUSH_ON_START_JSON, 1.f);

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

		FLUSH_ON_START_JSON controls whether the very first send pass after construction/reset
		behaves like a FORCE (sends every CC once, the default - matches VCV Core's CV-CC,
		whose sentinel-init does the same) or is suppressed (silently seed lastSentValues from
		the live CV instead of sending, so nothing goes out until something really changes -
		useful e.g. to avoid an unwanted burst to a controller on patch load).
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
			needsInitialSeed = false;
		}

		float elapsed = args.sampleTime * float(samplesSkipped + 1);
		if (rateLimiterTimer.process(elapsed) < RATE_LIMITER_PERIOD)
			return;
		rateLimiterTimer.time -= RATE_LIMITER_PERIOD;

		bool suppressInitialFlush = needsInitialSeed && getStateJson(FLUSH_ON_START_JSON) == 0.f;
		needsInitialSeed = false;

		for (int n = 0; n < 8; n++)
		{
			bool connected = getInputConnected(CC_INPUT + n);
			int  channels  = connected ? inputs[CC_INPUT + n].getChannels() : 0;
			for (int c = 0; c < 16; c++)
			{
				int cc = n * 16 + c;

				// Re-enabling a CC resets its sentinel so the next real value always goes
				// out immediately, instead of waiting for a change relative to whatever was
				// last sent before it got disabled.
				if (ccEnabled[cc] && !ccWasEnabled[cc])
					lastSentValues[cc] = 0xFF;
				ccWasEnabled[cc] = ccEnabled[cc];

				ccActivity[cc] = std::max(0.f, ccActivity[cc] - CC_ACTIVITY_DECAY);

				float raw = (c < channels) ? OL_statePoly[(CC_INPUT + n) * POLY_CHANNELS + c] : 0.f;
				uint8_t value = (uint8_t) clamp(std::round(raw / 10.f * 127.f), 0.f, 127.f);
				ccCvValue[cc] = raw;

				if (suppressInitialFlush)
				{
					lastSentValues[cc] = value;
					continue;
				}

				bool changed = (value != lastSentValues[cc]);

				if (!ccEnabled[cc])
				{
					// Never send while disabled, but still track/flash activity (in red, via
					// the grid's color scheme) so a muted CC's ongoing traffic stays visible -
					// same idea as CC2CV showing activity on disabled input CCs.
					if (changed)
					{
						lastSentValues[cc] = value;
						ccActivity[cc] = 1.f;
					}
					continue;
				}

				if (!changed)
					continue;
				lastSentValues[cc] = value;
				ccActivity[cc] = 1.f;

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
	CC2CV's output ring. FORCE (trigger jack) and FLUSH (panel button) sit beside the ring and
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

		CCGridWidget *grid = CCGridWidget::create(calculateCoordinates(3.556f, 42.849f, 0.f), mm2px(Vec(43.688f, 22.0f)),
			module ? &module->ccEnabled[0] : NULL, module ? &module->ccActivity[0] : NULL, module ? &module->ccCvValue[0] : NULL);
		addChild(grid);

		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(42.672f, 72.899f, 0.f), module, FORCE_INPUT));
		addParam(createParamCentered<LEDButton>(calculateCoordinates(43.942f, 80.415f, 0.f), module, FLUSH_PARAM));

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

	struct CV2CCFlushOnStartItem : MenuItem
	{
		CV2CC *module;
		void onAction(const event::Action &e) override
		{
			module->setStateJson(FLUSH_ON_START_JSON, module->getStateJson(FLUSH_ON_START_JSON) == 0.f ? 1.f : 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module->getStateJson(FLUSH_ON_START_JSON) != 0.f) ? "✔" : "";
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

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		CV2CCFlushOnStartItem *flushOnStartItem = new CV2CCFlushOnStartItem();
		flushOnStartItem->module = module;
		flushOnStartItem->text = "Flush On Start";
		menu->addChild(flushOnStartItem);
	}
};

Model *modelCV2CC = createModel<CV2CC, CV2CCWidget>("CV2CC");
