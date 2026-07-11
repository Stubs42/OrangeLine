/*
	Lanes.cpp

	Code for the OrangeLine module Lanes

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

#include "Lanes.hpp"

struct Lanes : Module
{

#include "OrangeLineCommon.hpp"

#include "LanesJsonLabels.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	bool widgetReady = false;

	/*
		Per-source-channel edge-detection state (plain, non-JSON, reset in moduleInitialize()).
		Tracks the last committed gate/lane/pitch for each of the NUM_SOURCES * POLY_CHANNELS note channels,
		and which lane-slot (if any) that channel currently contributes to.
	*/
	bool  oldGate [NUM_SOURCES][POLY_CHANNELS];
	int   oldLane [NUM_SOURCES][POLY_CHANNELS];
	float oldPitch[NUM_SOURCES][POLY_CHANNELS];
	int   srcSlot [NUM_SOURCES][POLY_CHANNELS];

	/*
		Per-lane slot state.
		laneChannelCount only shrinks from the end, and only for a trailing slot that was
		already inactive for at least one full tick (see moduleProcess()), so a downstream
		CV->MIDI interface never loses a channel while its gate is still high (never a hung note).
	*/
	bool  slotActive      [NUM_LANES][POLY_CHANNELS];
	float slotPitch       [NUM_LANES][POLY_CHANNELS];
	float slotVelocity    [NUM_LANES][POLY_CHANNELS];
	int   slotContributors[NUM_LANES][POLY_CHANNELS];
	int   laneChannelCount[NUM_LANES];

	/*
		Age (tick number at last (re)allocation) of each slot, used to find the oldest active
		slot to steal when a lane is full. ageCounter increments once per control-rate tick.
	*/
	unsigned long slotAge[NUM_LANES][POLY_CHANNELS];
	unsigned long ageCounter;


	// ********************************************************************************************************************************
	/*
		Initialization
	*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Lanes()
	{
		initializeInstance();
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
	bool moduleSkipProcess()
	{
		bool skip = (idleSkipCounter != 0);
		return skip;
	}
	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes()
	{
		for (int i = 0; i < NUM_SOURCES; i++)
		{
			setInPoly (VOCT_IN_INPUT + i, true);
			setInPoly (GATE_IN_INPUT + i, true);
			setInPoly (VEL_IN_INPUT  + i, true);
			setInPoly (LANE_IN_INPUT + i, true);
		}
		for (int i = 0; i < NUM_LANES; i++)
		{
			setOutPoly (VOCT_OUT_OUTPUT + i, true);
			setOutPoly (GATE_OUT_OUTPUT + i, true);
			setOutPoly (VEL_OUT_OUTPUT  + i, true);
			setOutPoly (OVERFLOW_OUTPUT + i, false);	// mono
			/*
				Plain continuous gate (STATE_TYPE_VALUE, the default), not a trigger: overflow is an
				ongoing condition ("this lane is currently full"), not a one-shot event. A trigger's
				1ms pulse would fight with the ~0.9ms control-rate retry, causing flicker.
			*/
		}
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		for (int i = 0; i < NUM_JSONS; i++)
		{
			setJsonLabel(i, jsonLabel[i]);
		}

#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig()
	{
		char buffer[64];
		for (int i = 0; i < NUM_SOURCES; i++)
		{
			sprintf(buffer, "Source %d V/Oct", i + 1);
			configInput (VOCT_IN_INPUT + i, buffer);
			sprintf(buffer, "Source %d Gate", i + 1);
			configInput (GATE_IN_INPUT + i, buffer);
			sprintf(buffer, "Source %d Velocity", i + 1);
			configInput (VEL_IN_INPUT + i, buffer);
			sprintf(buffer, "Source %d Lane", i + 1);
			configInput (LANE_IN_INPUT + i, buffer);
		}
		for (int i = 0; i < NUM_LANES; i++)
		{
			sprintf(buffer, "Lane %d V/Oct", i + 1);
			configOutput (VOCT_OUT_OUTPUT + i, buffer);
			sprintf(buffer, "Lane %d Gate", i + 1);
			configOutput (GATE_OUT_OUTPUT + i, buffer);
			sprintf(buffer, "Lane %d Velocity", i + 1);
			configOutput (VEL_OUT_OUTPUT + i, buffer);
			sprintf(buffer, "Lane %d Overflow", i + 1);
			configOutput (OVERFLOW_OUTPUT + i, buffer);
		}
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
		memset (oldGate,           0, sizeof(oldGate));
		memset (oldPitch,        0.f, sizeof(oldPitch));
		memset (slotActive,        0, sizeof(slotActive));
		memset (slotPitch,       0.f, sizeof(slotPitch));
		memset (slotVelocity,    0.f, sizeof(slotVelocity));
		memset (slotContributors,  0, sizeof(slotContributors));
		memset (laneChannelCount,  0, sizeof(laneChannelCount));
		memset (slotAge,           0, sizeof(slotAge));
		ageCounter = 0;
		for (int s = 0; s < NUM_SOURCES; s++)
		{
			for (int c = 0; c < POLY_CHANNELS; c++)
			{
				oldLane[s][c] = -1;
				srcSlot[s][c] = -1;
			}
		}
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
		Currently called twice when add a module to patch ...
	*/
	void moduleReset()
	{
		styleChanged = true;
	}

	// ********************************************************************************************************************************
	/*
		Module specific utility methods
	*/

	/**
		Try to acquire (or merge into) a slot in the given lane for the given (already quantized) pitch.
		Always succeeds: if the lane is full, steals the oldest active slot (classic voice stealing) -
		the evicted source-channel(s) are NOT reassigned automatically even if still held (see
		moduleProcess()'s "held but unslotted" handling for the resulting overflow-demand indicator).
	*/
	inline int acquireLaneSlot (int lane, float pitch, float velocity)
	{
		// 1. merge into an existing active slot with the same pitch
		for (int slot = 0; slot < laneChannelCount[lane]; slot++)
		{
			if (slotActive[lane][slot] && slotPitch[lane][slot] == pitch)
			{
				slotContributors[lane][slot]++;
				return slot;
			}
		}
		// 2. reuse a freed (inactive) slot within the already grown channel count
		for (int slot = 0; slot < laneChannelCount[lane]; slot++)
		{
			if (!slotActive[lane][slot])
			{
				slotActive[lane][slot]       = true;
				slotPitch[lane][slot]        = pitch;
				slotVelocity[lane][slot]     = velocity;
				slotContributors[lane][slot] = 1;
				slotAge[lane][slot]          = ageCounter;
				return slot;
			}
		}
		// 3. grow the lane's channel count (never shrinks again afterwards)
		if (laneChannelCount[lane] < POLY_CHANNELS)
		{
			int slot = laneChannelCount[lane]++;
			slotActive[lane][slot]       = true;
			slotPitch[lane][slot]        = pitch;
			slotVelocity[lane][slot]     = velocity;
			slotContributors[lane][slot] = 1;
			slotAge[lane][slot]          = ageCounter;
			return slot;
		}
		// 4. lane is full: steal the oldest active slot
		int oldestSlot = 0;
		for (int slot = 1; slot < laneChannelCount[lane]; slot++)
		{
			if (slotAge[lane][slot] < slotAge[lane][oldestSlot])
				oldestSlot = slot;
		}
		// invalidate any held source-channel(s) currently pointing at the stolen slot - they
		// stay silent (srcSlot < 0) until their own gate re-triggers or their lane/pitch changes
		for (int s2 = 0; s2 < NUM_SOURCES; s2++)
		{
			for (int c2 = 0; c2 < POLY_CHANNELS; c2++)
			{
				if (oldLane[s2][c2] == lane && srcSlot[s2][c2] == oldestSlot)
					srcSlot[s2][c2] = -1;
			}
		}
		slotPitch[lane][oldestSlot]        = pitch;
		slotVelocity[lane][oldestSlot]     = velocity;
		slotContributors[lane][oldestSlot] = 1;
		slotAge[lane][oldestSlot]          = ageCounter;
		return oldestSlot;
	}

	/**
		Release a contribution to a lane slot. Only clears the slot (gate/velocity to 0)
		once the last contributor has released it.
	*/
	inline void releaseLaneSlot (int lane, int slot)
	{
		if (slot < 0)
			return;
		if (--slotContributors[lane][slot] <= 0)
		{
			slotActive[lane][slot]   = false;
			slotVelocity[lane][slot] = 0.f;
			slotContributors[lane][slot] = 0;
		}
	}

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

		/*
			Reclaim trailing free channels - but only ones that were already inactive BEFORE this
			tick, i.e. their gate=0 was already sent out to the poly cable for at least one full
			cycle by the output-writing loop below in the previous call. This keeps the invariant
			that a channel's gate is always explicitly dropped to 0 before it can ever disappear
			from the poly cable (never shrinking mid-note), while still reclaiming channels that
			are genuinely done instead of leaving the lane parked at its historical peak forever.
		*/
		for (int lane = 0; lane < NUM_LANES; lane++)
		{
			while (laneChannelCount[lane] > 0 && !slotActive[lane][laneChannelCount[lane] - 1])
				laneChannelCount[lane]--;
		}

		bool overflowThisTick[NUM_LANES];
		memset (overflowThisTick, 0, sizeof(overflowThisTick));
		ageCounter++;

		for (int s = 0; s < NUM_SOURCES; s++)
		{
			/*
				If a cable is unpatched, processParamsAndInputs() (OrangeLineCommon.hpp) skips refreshing
				OL_statePoly for it entirely, leaving the last known (possibly stale) value in place. Force
				sane defaults here (gate = off, pitch/velocity/lane CV = 0) so unplugging any of the 4 cables
				behaves like a normal unpatched Eurorack input instead of freezing on the last value - most
				importantly, unplugging GATE always releases held notes instead of leaving them stuck on.
			*/
			bool sourceGateConnected = getInputConnected (GATE_IN_INPUT + s);
			bool sourceVoctConnected = getInputConnected (VOCT_IN_INPUT + s);
			bool sourceVelConnected  = getInputConnected (VEL_IN_INPUT  + s);
			bool sourceLaneConnected = getInputConnected (LANE_IN_INPUT + s);
			for (int c = 0; c < POLY_CHANNELS; c++)
			{
				bool  gateIn  = sourceGateConnected && OL_statePoly[(GATE_IN_INPUT + s) * POLY_CHANNELS + c] > 5.f;
				float pitchIn = sourceVoctConnected ? quantize (OL_statePoly[(VOCT_IN_INPUT + s) * POLY_CHANNELS + c]) : 0.f;
				float velIn   = sourceVelConnected  ? OL_statePoly[(VEL_IN_INPUT  + s) * POLY_CHANNELS + c] : 0.f;
				float laneCV  = sourceLaneConnected ? OL_statePoly[(LANE_IN_INPUT + s) * POLY_CHANNELS + c] : 0.f;
				int   laneIn  = int(clamp (round (laneCV * 12.f), 0.f, float(NUM_LANES - 1)));

				bool  wasGate   = oldGate[s][c];
				int   prevLane  = oldLane[s][c];
				float prevPitch = oldPitch[s][c];

				bool needRelease = false;
				bool needAcquire = false;

				if (gateIn && !wasGate)
				{
					needAcquire = true;
				}
				else if (gateIn && wasGate && (laneIn != prevLane || pitchIn != prevPitch))
				{
					// lane or pitch changed while held: treat as note-off (old) + note-on (new)
					needRelease = true;
					needAcquire = true;
				}
				else if (!gateIn && wasGate)
				{
					needRelease = true;
				}
				// Note: no retry for a held note that's already unslotted (srcSlot < 0) - once
				// voice-stealing takes its slot, it classically stays silent until its own gate
				// re-triggers or its lane/pitch changes, it does not automatically reclaim a slot.

				if (needRelease)
				{
					releaseLaneSlot (prevLane, srcSlot[s][c]);
					srcSlot[s][c] = -1;
				}
				if (needAcquire)
				{
					srcSlot[s][c] = acquireLaneSlot (laneIn, pitchIn, velIn);	// always succeeds (steals if full)
				}

				// Overflow/demand indicator: this channel currently wants a slot on laneIn (gate held)
				// but doesn't have one - either just-evicted-by-stealing or an original overflow.
				if (gateIn && srcSlot[s][c] < 0)
					overflowThisTick[laneIn] = true;

				oldGate[s][c]  = gateIn;
				oldLane[s][c]  = laneIn;
				oldPitch[s][c] = pitchIn;
			}
		}

		for (int lane = 0; lane < NUM_LANES; lane++)
		{
			for (int slot = 0; slot < laneChannelCount[lane]; slot++)
			{
				setStateOutPoly (VOCT_OUT_OUTPUT + lane, slot, slotPitch[lane][slot]);
				setStateOutPoly (GATE_OUT_OUTPUT + lane, slot, slotActive[lane][slot] ? 10.f : 0.f);
				setStateOutPoly (VEL_OUT_OUTPUT  + lane, slot, slotVelocity[lane][slot]);
			}
			setOutPolyChannels (VOCT_OUT_OUTPUT + lane, laneChannelCount[lane]);
			setOutPolyChannels (GATE_OUT_OUTPUT + lane, laneChannelCount[lane]);
			setOutPolyChannels (VEL_OUT_OUTPUT  + lane, laneChannelCount[lane]);

			// Overflow is a state (currently more distinct pitches want this lane than it has
			// channels for), not a one-shot event - gate and light just mirror it directly.
			setStateOutput (OVERFLOW_OUTPUT + lane, overflowThisTick[lane] ? 10.f : 0.f);
			setStateLight  (OVERFLOW_LIGHT  + lane, overflowThisTick[lane] ? 255.f : 0.f);
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

	v1 panel: plain functional layout, no custom artwork yet (see res/LanesWork.svg).
	Jack coordinates below match 1:1 the grid documented in res/LanesWork.svg so the
	guide layer and the actual widget stay in sync once custom panels are authored.
*/
struct LanesWidget : ModuleWidget
{
	LanesWidget(Lanes *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		/*
			Jack centers below are measured directly from the "Controls" layer of res/LanesWork.svg
			(4 blocks x 4 columns x 8 rows), so the widget lines up exactly with the panel art.
			These are already true jack centers, so calculateCoordinates() is called with a 0 offset.
		*/
		static const float ROW0_Y     = 21.845226f;
		static const float ROW_PITCH  = 13.208002f;
		static const float COL_PITCH  = 9.398000f;
		static const float INPUT_BLOCK_X[2]  = { 7.112002f, 48.514002f };
		static const float OUTPUT_BLOCK_X[2] = { 90.932002f, 132.588001f };

		// Input blocks: Sources 1-8 (block 0) and Sources 9-16 (block 1)
		for (int block = 0; block < 2; block++)
		{
			float blockX = INPUT_BLOCK_X[block];
			for (int row = 0; row < 8; row++)
			{
				int source = block * 8 + row;
				float y = ROW0_Y + row * ROW_PITCH;
				addInput (createInputCentered<PJ301MPort> (calculateCoordinates (blockX + 0.f * COL_PITCH, y, 0.f), module, VOCT_IN_INPUT + source));
				addInput (createInputCentered<PJ301MPort> (calculateCoordinates (blockX + 1.f * COL_PITCH, y, 0.f), module, GATE_IN_INPUT + source));
				addInput (createInputCentered<PJ301MPort> (calculateCoordinates (blockX + 2.f * COL_PITCH, y, 0.f), module, VEL_IN_INPUT  + source));
				addInput (createInputCentered<PJ301MPort> (calculateCoordinates (blockX + 3.f * COL_PITCH, y, 0.f), module, LANE_IN_INPUT + source));
			}
		}

		// Output blocks: Lanes 1-8 (block 0) and Lanes 9-16 (block 1)
		for (int block = 0; block < 2; block++)
		{
			float blockX = OUTPUT_BLOCK_X[block];
			for (int row = 0; row < 8; row++)
			{
				int lane = block * 8 + row;
				float y = ROW0_Y + row * ROW_PITCH;
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 0.f * COL_PITCH, y, 0.f), module, VOCT_OUT_OUTPUT + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 1.f * COL_PITCH, y, 0.f), module, GATE_OUT_OUTPUT + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 2.f * COL_PITCH, y, 0.f), module, VEL_OUT_OUTPUT  + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 3.f * COL_PITCH, y, 0.f), module, OVERFLOW_OUTPUT + lane));
				addChild  (createLightCentered<LargeLight<RedLight>> (calculateCoordinates (blockX + 3.f * COL_PITCH, y, 0.f), module, OVERFLOW_LIGHT + lane));
			}
		}

		if (module)
			module->widgetReady = true;
	}

	struct LanesStyleItem : MenuItem
	{
		Lanes *module;
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

		Lanes *module = dynamic_cast<Lanes *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		LanesStyleItem *style1Item = new LanesStyleItem();
		style1Item->text = "Orange"; //
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		LanesStyleItem *style2Item = new LanesStyleItem();
		style2Item->text = "Bright"; //
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		LanesStyleItem *style3Item = new LanesStyleItem();
		style3Item->text = "Dark"; //
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelLanes = createModel<Lanes, LanesWidget>("Lanes");
