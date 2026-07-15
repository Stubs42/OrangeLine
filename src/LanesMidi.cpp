/*
	LanesMidi.cpp

	Code for the OrangeLine module LanesMidi

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

#include "LanesMidi.hpp"
#include "LanesVoiceAllocator.hpp"

/*
	One instance per MIDI channel (index == its fixed channel, set once in the constructor).
	dsp::MidiGenerator<POLY_CHANNELS> already solves exactly the problem of turning a poly
	gate/pitch/velocity stream into correct Note On/Off (including a note changing while
	gate stays high - the LANES voice-steal case, and also exactly what happens when the
	user switches which lane feeds this channel while notes are held). onMessage() just
	stamps the (fixed) channel before forwarding to the shared midi::Output.
*/
struct LaneMidiGenerator : dsp::MidiGenerator<POLY_CHANNELS>
{
	midi::Output *output = nullptr;
	int channel = 0;	// 0-15, fixed to this instance's array index - never changes at runtime

	void onMessage(const midi::Message &m) override
	{
		if (!output) return;
		midi::Message msg = m;
		msg.setChannel(channel);
		output->sendMessage(msg);
	}
};

/*
	LanesMidi is a right-side expander of LANES (the Hub, possibly through other expanders
	- see LanesShared.hpp's chain-walk): one MIDI device, 16 channel slots (one per MIDI
	channel 1-16), each freely choosing which lane (0 = off, 1-16) feeds it. Any number of
	LanesMidi (and/or LanesCV) instances can chain together in any order/mix, e.g. for
	layering the same lanes across multiple MIDI devices at once.

	Deliberately channel-indexed rather than lane-indexed: a channel-select-per-lane design
	(16 lane knobs, each picking a channel) would let two lanes claim the same channel,
	needing an arbitration/contention decision - exactly the kind of second, independent
	voice-stealing Dieter wanted to avoid. Indexing by channel instead means each channel
	slot structurally has at most one source lane at a time (a single selector), so that
	conflict simply cannot arise - no arbitration code needed at all. Two different channels
	choosing the same lane is fine (that's just the same lane's notes going out twice, e.g.
	for layering) since each channel's own LaneMidiGenerator still only ever reads one lane.

	No overflow light - each channel's own LaneDisplayWidget shows the assigned lane number
	and color-codes itself (see laneOverflowDisplay below) when the lane currently feeding
	that channel has more distinct notes wanting it than this expander's own
	LanesVoiceAllocator (capacity POLY_CHANNELS) can represent. Same overflow concept as
	LanesCV's OVERFLOW_LIGHT, just relayed via display color instead of a light - not a
	channel conflict (which can't happen, see above).
*/
struct LanesMidi : Module, LanesExpanderInterface
{

#include "OrangeLineCommon.hpp"

#include "LanesMidiJsonLabels.hpp"

	bool widgetReady = false;

	LanesHubInterface *lanesHub = nullptr;
	// Set when a Hub is reachable through BOTH sides at once - see LanesShared.hpp's
	// classifyLanesNeighborForHub(), which a neighboring Hub uses this to detect being
	// caught between two Hubs even though we're not directly adjacent to the other one.
	bool lanesHubAmbiguous = false;
	LanesVoiceAllocator<POLY_CHANNELS> allocator;

	midi::Output midiOutput;
	LaneMidiGenerator gen[NUM_LANES];

	// Set while a channel is genuinely sending; used only to detect the transition into
	// "muted" (lane 0 selected, or hub gone) so panic() fires once, not every tick - avoids
	// a hung note stuck on the device. Switching directly between two different active
	// lanes needs no such special-casing - the per-slot loop below already turns off any
	// note that doesn't carry over, the same way a lane's own pitch changing while held does.
	bool channelWasActive[NUM_LANES];

	// Cached each tick for LaneDisplayWidget to read directly (plain member, not JSON/allocator
	// state - the widget-reads-a-cached-module-member pattern established for Morpheus'
	// scaleOnOutput/visualOn, since a widget reading module state some other way is fragile).
	bool laneOverflowDisplay[NUM_LANES];

	LanesMidi()
	{
		for (int i = 0; i < NUM_LANES; i++)
		{
			gen[i].output = &midiOutput;
			gen[i].channel = i;
		}

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

	LanesHubInterface* getLanesHub() override { return lanesHub; }
	bool getLanesHubAmbiguous() override { return lanesHubAmbiguous; }
	float getLanesStyle() override { return OL_state[STYLE_JSON]; }

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

	// KISS per Dieter: no dedicated display, the current lane (0 = "Off", else "Lane N")
	// only shows up as the standard Rack hover tooltip.
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
			sprintf(buffer, "MIDI Channel %d Source Lane", i + 1);
			configParam<LaneParamQuantity>(LANE_PARAM + i, 0.f, float(NUM_LANES), 0.f, buffer);
			paramQuantities[LANE_PARAM + i]->snapEnabled = true;
		}
	}

	inline void moduleCustomInitialize() {}

	inline void moduleInitialize()
	{
		for (int i = 0; i < NUM_LANES; i++)
		{
			channelWasActive[i] = false;
			laneOverflowDisplay[i] = false;
			gen[i].reset();
		}
		allocator.reset();
	}

	void moduleReset()
	{
		midiOutput.reset();
		styleChanged = true;
	}

	/**
		Chain-walk to find the Hub (checked on both sides, see LanesShared.hpp's
		resolveLanesHub()), run our own voice allocator, then for each MIDI channel relay
		whichever lane it's currently tuned to into its own LaneMidiGenerator.
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

		// Defensive: midi::Output::channel defaults to -1 (pass-through) already, but force
		// it every tick so our own per-channel msg.setChannel() always wins regardless of
		// anything else that might have touched it (stray preset, UI, ...).
		midiOutput.channel = -1;

		LanesHubInterface *hubLeft  = resolveLanesHub(leftExpander.module);
		LanesHubInterface *hubRight = resolveLanesHub(rightExpander.module);
		lanesHub = hubLeft ? hubLeft : hubRight;
		// Only a real conflict if left and right resolve to two DIFFERENT Hubs - in a plain
		// chain (Hub | LanesCV | LanesMidi), the middle expander reaches the same Hub both
		// directly (left) and indirectly through its other neighbor (right), which is
		// perfectly healthy, not ambiguous.
		bool hubConflict = hubLeft && hubRight && hubLeft != hubRight;
		lanesHubAmbiguous = hubConflict;

		// Chain-health color, shared by both corner lights (see LanesMidi.hpp) - only
		// whether each light is lit at all depends on that specific side's own connection.
		float healthGreen, healthRed;
		if (hubConflict)               { healthGreen = 0.f;   healthRed = 255.f; }	// red: two different Hubs reachable
		else if (hubLeft || hubRight)  { healthGreen = 255.f; healthRed = 0.f;   }	// green: healthy, one Hub (either or both sides)
		else                           { healthGreen = 0.f;   healthRed = 0.f;   }	// off: connected, but no Hub anywhere (dropped yellow - cosmetic call, Dieter 2026-07-15)
		bool leftConnected  = leftExpander.module  != nullptr;
		bool rightConnected = rightExpander.module != nullptr;
		setStateLight(LEFT_CONN_LIGHT,      leftConnected  ? healthGreen : 0.f);
		setStateLight(LEFT_CONN_LIGHT + 1,  leftConnected  ? healthRed   : 0.f);
		setStateLight(RIGHT_CONN_LIGHT,     rightConnected ? healthGreen : 0.f);
		setStateLight(RIGHT_CONN_LIGHT + 1, rightConnected ? healthRed   : 0.f);

		if (lanesHub)
			allocator.process(lanesHub);
		else
			allocator.reset();

		for (int channel = 0; channel < NUM_LANES; channel++)
		{
			int cur = (int) getStateParam(LANE_PARAM + channel);
			bool active = lanesHub && cur != 0;

			if (!active)
			{
				if (channelWasActive[channel])
					gen[channel].panic();
				channelWasActive[channel] = false;
				laneOverflowDisplay[channel] = false;
				continue;
			}
			channelWasActive[channel] = true;

			int lane = cur - 1;
			laneOverflowDisplay[channel] = allocator.getLaneOverflow(lane);

			// This Rack-SDK's dsp::MidiGenerator is fixed at its POLY_CHANNELS template
			// capacity - no setChannels()/live channel count concept like the newer API.
			// So instead of only touching slots < channelCount, walk every slot: real data
			// below channelCount, explicit gate-off above it - that's what correctly turns
			// off a note when the lane's live channel count shrinks (a shorter chord), since
			// setNoteGate()'s note-off path uses its own remembered notes[slot], not our
			// passed note, so passing 0 here is safe even if that slot was already off.
			int channelCount = allocator.getLaneChannelCount(lane);
			for (int slot = 0; slot < POLY_CHANNELS; slot++)
			{
				if (slot >= channelCount)
				{
					gen[channel].setNoteGate(0, false, slot);
					continue;
				}

				float voct = allocator.getLaneVoct(lane, slot);
				bool  gateOn = allocator.getLaneGate(lane, slot);
				float velV = allocator.getLaneVelocity(lane, slot);

				// 0V = C4 = MIDI note 60 (OrangeLine's octave()/note() macros, OrangeLine.hpp)
				int midiNote = 60 + 12 * octave(voct) + note(voct);
				midiNote = (int) clamp((float) midiNote, 0.f, 127.f);
				int vel = (int) clamp(std::round(velV * 12.7f), 0.f, 127.f);

				gen[channel].setVelocity((int8_t) vel, slot);
				gen[channel].setNoteGate((int8_t) midiNote, gateOn, slot);
			}
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
	currently assigned via the knob below it (blank for 0/off), color-coded orange normally
	and red while that lane is overflowing (see LanesMidi's laneOverflowDisplay member) -
	replaces a separate overflow light entirely, per Dieter's call. Reads two plain module
	members directly (drawLayer(layer==1), matching OrangeLine.hpp's NumberWidget/TextWidget
	convention) rather than computing anything itself - the knob's own Param is the single
	source of truth for the lane number, laneOverflowDisplay is cached once per tick in
	moduleProcess().
*/
struct LaneDisplayWidget : TransparentWidget
{
	LanesMidi *module = nullptr;
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
		bool overflow = module->laneOverflowDisplay[channel];

		char buffer[8];
		snprintf(buffer, sizeof(buffer), "%d", lane);

		std::shared_ptr<Font> pFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, pFont->handle);
		nvgFontSize(args.vg, 12.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, overflow ? RED : ORANGE);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, buffer, nullptr);

		Widget::drawLayer(args, 1);
	}
};

/**
	Main Module Widget

	v1 panel: plain functional layout, no custom artwork yet (see res/LanesMidiWork.svg).
	Panel width matches CV2CC (50.8mm / 10HP, Dieter's call) - a MidiDisplay (driver+device
	only, no channel dropdown since each of the 16 rows below already fixes one specific
	MIDI channel, only the source lane is selectable) plus 2 columns x 8 rows of lane-select
	button + overflow light (one row per MIDI channel), same block layout convention as
	LANES/LanesCV's 16-lane grids.
*/
struct LanesMidiWidget : ModuleWidget
{
	LanesExtStrips extStrips;

	LanesMidiWidget(LanesMidi *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesMidiOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesMidiBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesMidiDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		MidiDisplay *display = createWidget<MidiDisplay>(calculateCoordinates(3.552f, 10.411251f, 0.f));
		display->box.size = mm2px(Vec(43.688f, 18.799999f));
		display->setMidiPort(module ? &module->midiOutput : NULL);
		// No per-lane device-wide channel: channel routing is the 16 buttons below, so the
		// standard channel dropdown (which would fight our per-lane channel assignment) is
		// removed after construction rather than left visible but ineffective.
		display->removeChild(display->channelChoice);
		delete display->channelChoice;
		display->channelChoice = nullptr;
		addChild(display);

		/*
			Measured from res/LanesMidiWork.svg: the knob grid (from the "Layer 1" imported
			knob symbol instances in the Controls layer) and the lane-display backgrounds
			(the small rounded-rect path2128-* shapes in the PanelOrange layer - there's no
			dedicated guide layer entry for them since they're baked into the panel art, not
			a separate control). Both share the same row grid; the display sits at a fixed
			11.683317mm offset to the right of its knob.
		*/
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

				LaneDisplayWidget *disp = new LaneDisplayWidget();
				disp->box.pos = calculateCoordinates(COL_KNOB_X[col] + DISPLAY_DX, y, 0.f).minus(DISPLAY_SIZE.div(2.f));
				disp->box.size = DISPLAY_SIZE;
				disp->module = module;
				disp->channel = channel;
				addChild(disp);
			}
		}

		// Tiny bi-color corner lights - off/green/yellow/red chain-health signal (see
		// moduleProcess()'s resolveLanesHub() calls and LanesMidi.hpp). Placeholder position
		// (panel is 50.8mm wide) until Dieter places guide art for them.
		addChild (createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>> (calculateCoordinates (3.5f, 4.f, 0.f), module, LEFT_CONN_LIGHT));
		addChild (createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>> (calculateCoordinates (47.3f, 4.f, 0.f), module, RIGHT_CONN_LIGHT));

		addLanesExtStrips(this, 50.8f, &extStrips);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		if (module)
			updateLanesExtStrips(&extStrips, module, module->leftExpander.module, module->rightExpander.module);
		ModuleWidget::step();
	}

	struct LanesMidiStyleItem : MenuItem
	{
		LanesMidi *module;
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

		LanesMidi *module = dynamic_cast<LanesMidi *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		LanesMidiStyleItem *style1Item = new LanesMidiStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		LanesMidiStyleItem *style2Item = new LanesMidiStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		LanesMidiStyleItem *style3Item = new LanesMidiStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelLanesMidi = createModel<LanesMidi, LanesMidiWidget>("LanesMidi");
