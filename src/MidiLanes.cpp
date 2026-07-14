/*
	MidiLanes.cpp

	Code for the OrangeLine module MidiLanes (the LANES Hub, MIDI-input variant)

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

#include "MidiLanes.hpp"

/*
	MidiLanes is the mirror of CVLanes: instead of 16 CV/Gate/Velocity/Lane-select source
	jacks, it reads one MIDI input device and treats each of its 16 MIDI channels as one
	"source" - the exact same NUM_SOURCES the LanesHubInterface already expects. A channel's
	target lane is a knob (LANE_PARAM, 0 = off, 1-16), mirroring LanesMidi's channel-indexed
	design exactly (Dieter: "knob positions etc bleiben identisch mit LANES-MIDI").

	Per channel, an rack::dsp::MidiParser<POLY_CHANNELS> handles the actual note-to-slot
	polyphony assignment (ROTATE_MODE, its library default) - the input-side counterpart to
	LanesMidi's dsp::MidiGenerator, so voice assignment within one incoming MIDI channel is
	"for free" instead of hand-rolled. Only Gate/V-Oct/Velocity are read (no pitch/mod wheel
	processing - see getPitchVoltage()'s comment below), matching LanesMidi's own restricted
	scope on the output side.

	Like CVLanes, MidiLanes does no cross-source merging or voice-stealing itself - it just
	exposes raw per-source-channel state via LanesHubInterface. Each expander (LanesCV,
	LanesMidi, ...) still runs its own independent LanesVoiceAllocator against it, exactly as
	it would against CVLanes - MidiLanes is a drop-in alternative Hub, not a special case.
*/
struct MidiLanes : Module, LanesHubInterface
{

#include "OrangeLineCommon.hpp"

#include "MidiLanesJsonLabels.hpp"

	bool widgetReady = false;

	midi::InputQueue midiInput;
	dsp::MidiParser<POLY_CHANNELS> parser[NUM_SOURCES];	// one per incoming MIDI channel

	MidiLanes()
	{
		for (int i = 0; i < NUM_SOURCES; i++)
			parser[i].setChannels(POLY_CHANNELS);

		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_object_set_new(rootJ, "midi", midiInput.toJson());
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *midiJ = json_object_get(rootJ, "midi");
			if (midiJ)
				midiInput.fromJson(midiJ);
		};

		initializeInstance();
	}

	// ****************************************************************************************
	/*
		LanesHubInterface implementation - raw, unmerged per-source (= per MIDI channel)
		state for expanders (LanesCV, LanesMidi, ...) to run their own LanesVoiceAllocator
		against. See LanesShared.hpp. A channel whose LANE_PARAM is 0 (off) always reports
		no gate, regardless of what MIDI activity that channel actually has - so an expander
		downstream never acquires a slot for it, with no separate cleanup needed here.
	*/
	bool getSourceGate(int source, int channel) override
	{
		if ((int) getStateParam(LANE_PARAM + source) == 0)
			return false;
		return parser[source].gates[channel];
	}
	float getSourcePitch(int source, int channel) override
	{
		// getPitchVoltage() also folds in pitch-wheel data, but since we never call
		// processFilters() (no pitch/mod wheel handling - out of scope, matches LanesMidi's
		// own restriction), its filter output stays at its initial 0 forever, so this always
		// reduces to the plain (note - 60) / 12 conversion regardless of incoming pitch-wheel
		// messages.
		return parser[source].getPitchVoltage(channel);
	}
	float getSourceVelocity(int source, int channel) override
	{
		// Inverse of LanesMidi's CV-to-MIDI velocity scaling (vel = round(cv * 12.7)).
		return parser[source].velocities[channel] / 12.7f;
	}
	int getSourceLane(int source, int channel) override
	{
		int lane = (int) getStateParam(LANE_PARAM + source);
		return lane == 0 ? 0 : lane - 1;
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes() {}

	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		for (int i = 0; i < NUM_JSONS; i++)
			setJsonLabel(i, jsonLabel[i]);

#pragma GCC diagnostic pop
	}

	// KISS per Dieter: no dedicated overflow concept on the Hub side (that's each expander's
	// own LanesVoiceAllocator concern) - the display just shows the assigned lane, "Off" for 0.
	struct LaneParamQuantity : ParamQuantity
	{
		std::string getDisplayValueString() override
		{
			int v = (int) getValue();
			return v == 0 ? std::string("Off") : string::f("Lane %d", v);
		}
	};

	inline void moduleParamConfig()
	{
		char buffer[32];
		for (int i = 0; i < NUM_LANES; i++)
		{
			sprintf(buffer, "MIDI Channel %d Target Lane", i + 1);
			configParam<LaneParamQuantity>(LANE_PARAM + i, 0.f, float(NUM_LANES), 0.f, buffer);
			paramQuantities[LANE_PARAM + i]->snapEnabled = true;
		}
	}

	inline void moduleCustomInitialize() {}

	inline void moduleInitialize()
	{
		// MidiParser::reset() resets `channels` back to its own default of 1 (mono) - reassert
		// our polyphonic capacity right after, same "must be reasserted, not just set once"
		// pitfall as setOutPolyChannels() elsewhere in this codebase.
		for (int i = 0; i < NUM_SOURCES; i++)
		{
			parser[i].reset();
			parser[i].setChannels(POLY_CHANNELS);
		}
	}

	void moduleReset()
	{
		midiInput.reset();
		styleChanged = true;
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

		LanesNeighborKind leftKind  = classifyLanesNeighborForHub(leftExpander.module,  this);
		LanesNeighborKind rightKind = classifyLanesNeighborForHub(rightExpander.module, this);
		setStateLight(LEFT_CONN_LIGHT,      leftKind  == LANES_NEIGHBOR_OK       ? 255.f : 0.f);
		setStateLight(LEFT_CONN_LIGHT + 1,  leftKind  == LANES_NEIGHBOR_CONFLICT ? 255.f : 0.f);
		setStateLight(RIGHT_CONN_LIGHT,     rightKind == LANES_NEIGHBOR_OK       ? 255.f : 0.f);
		setStateLight(RIGHT_CONN_LIGHT + 1, rightKind == LANES_NEIGHBOR_CONFLICT ? 255.f : 0.f);

		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame))
		{
			int channel = msg.getChannel();
			if (channel >= 0 && channel < NUM_SOURCES)
				parser[channel].processMessage(msg);
		}
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

// ********************************************************************************************************************************
/*
	Widget implementation
*/

/**
	Read-only per-channel display (no SVG frames yet, placeholder): shows the lane number
	currently assigned via the knob below it (blank for 0/off). Unlike LanesMidi's
	LaneDisplayWidget, no overflow color-coding - the Hub itself has no capacity concept to
	report (see MidiLanes' comment on getSourceGate/etc above).
*/
struct MidiLaneDisplayWidget : TransparentWidget
{
	MidiLanes *module = nullptr;
	int channel = 0;

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		if (!module)
			return;

		int lane = (int) module->params[LANE_PARAM + channel].getValue();
		if (lane == 0)
			return;

		char buffer[8];
		snprintf(buffer, sizeof(buffer), "%d", lane);

		std::shared_ptr<Font> pFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, pFont->handle);
		nvgFontSize(args.vg, 12.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, ORANGE);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, buffer, nullptr);

		Widget::drawLayer(args, 1);
	}
};

/**
	Main Module Widget

	v1 panel: plain functional layout, no custom artwork yet (see res/MidiLanesWork.svg).
	Same panel width and knob/display grid as LanesMidi, per Dieter's call ("knob positionen
	etc bleiben identisch mit LANES-MIDI") - only the MidiDisplay is bound to the input port
	instead of an output, and there's no per-row overflow indicator (see above).
*/
struct MidiLanesWidget : ModuleWidget
{
	MidiLanesWidget(MidiLanes *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MidiLanesOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MidiLanesBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MidiLanesDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		MidiDisplay *display = createWidget<MidiDisplay>(calculateCoordinates(3.552f, 10.411251f, 0.f));
		display->box.size = mm2px(Vec(43.688f, 18.799999f));
		display->setMidiPort(module ? &module->midiInput : NULL);
		// No per-channel device-wide channel filter widget needed: channel routing is the 16
		// knobs below, so the standard channel dropdown is removed after construction rather
		// than left visible but ineffective (same as LanesMidi's output-side MidiDisplay).
		display->removeChild(display->channelChoice);
		delete display->channelChoice;
		display->channelChoice = nullptr;
		addChild(display);

		// Identical grid to LanesMidi's (measured from res/LanesMidiWork.svg, reused here
		// per Dieter's call rather than re-measuring res/MidiLanesWork.svg).
		static const float ROW0_Y        = 37.847943f;
		static const float ROW_PITCH     = 10.769670f;
		static const float COL_KNOB_X[2] = { 8.128683f, 31.242683f };
		static const float DISPLAY_DX    = 11.683317f;
		static const Vec   DISPLAY_SIZE  = mm2px(Vec(8.128f, 6.096f));

		for (int col = 0; col < 2; col++)
		{
			for (int row = 0; row < 8; row++)
			{
				int channel = col * 8 + row;
				float y = ROW0_Y + row * ROW_PITCH;

				addParam(createParamCentered<RoundSmallBlackKnob>(calculateCoordinates(COL_KNOB_X[col], y, 0.f), module, LANE_PARAM + channel));

				MidiLaneDisplayWidget *disp = new MidiLaneDisplayWidget();
				disp->box.pos = calculateCoordinates(COL_KNOB_X[col] + DISPLAY_DX, y, 0.f).minus(DISPLAY_SIZE.div(2.f));
				disp->box.size = DISPLAY_SIZE;
				disp->module = module;
				disp->channel = channel;
				addChild(disp);
			}
		}

		// Tiny bi-color corner lights - off/green/red neighbor signal (see moduleProcess()'s
		// classifyLanesNeighborForHub() calls and MidiLanes.hpp). Placeholder position (panel is
		// 50.8mm wide) until Dieter places guide art for them.
		addChild (createLightCentered<TinyLight<GreenRedLight>> (calculateCoordinates (3.5f, 4.f, 0.f), module, LEFT_CONN_LIGHT));
		addChild (createLightCentered<TinyLight<GreenRedLight>> (calculateCoordinates (47.3f, 4.f, 0.f), module, RIGHT_CONN_LIGHT));

		if (module)
			module->widgetReady = true;
	}

	struct MidiLanesStyleItem : MenuItem
	{
		MidiLanes *module;
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

		MidiLanes *module = dynamic_cast<MidiLanes *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		MidiLanesStyleItem *style1Item = new MidiLanesStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		MidiLanesStyleItem *style2Item = new MidiLanesStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		MidiLanesStyleItem *style3Item = new MidiLanesStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelMidiLanes = createModel<MidiLanes, MidiLanesWidget>("MidiLanes");
