/*
	LanesCV.cpp

	Code for the OrangeLine module LanesCV

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

#include "LanesCV.hpp"
#include "LanesVoiceAllocator.hpp"

/*
	LanesCV is a right-side expander of LANES (the Hub): it reads the Hub's raw per-source
	state via LanesHubInterface (see LanesShared.hpp), runs its own LanesVoiceAllocator
	(capacity = POLY_CHANNELS, Rack's own poly-cable limit) to merge/voice-steal
	independently of any other expander, and turns the result into the same
	16x(V/Oct+Gate+Velocity+Overflow) CV jacks that used to live directly on LANES before
	the Hub/Expander split. Zero CV inputs/params of its own - it's a pure "pull" reader.
*/
struct LanesCV : Module, LanesExpanderInterface
{

#include "OrangeLineCommon.hpp"

#include "LanesCVJsonLabels.hpp"

	bool widgetReady = false;

	LanesHubInterface *lanesHub = nullptr;
	// Set when a Hub is reachable through BOTH sides at once - see LanesShared.hpp's
	// classifyLanesNeighborForHub(), which a neighboring Hub uses this to detect being
	// caught between two Hubs even though we're not directly adjacent to the other one.
	bool lanesHubAmbiguous = false;
	LanesVoiceAllocator<POLY_CHANNELS> allocator;

	LanesCV()
	{
		initializeInstance();
	}

	LanesHubInterface* getLanesHub() override { return lanesHub; }
	bool getLanesHubAmbiguous() override { return lanesHubAmbiguous; }

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
		for (int i = 0; i < NUM_LANES; i++)
		{
			setOutPoly (VOCT_OUTPUT + i, true);
			setOutPoly (GATE_OUTPUT + i, true);
			setOutPoly (VEL_OUTPUT  + i, true);
			setOutPoly (OVERFLOW_OUTPUT + i, false);	// mono
			/*
				Plain continuous gate (STATE_TYPE_VALUE, the default), not a trigger: overflow is
				an ongoing condition ("this lane is currently full"), not a one-shot event.
			*/
		}
	}

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

	inline void moduleParamConfig()
	{
		char buffer[64];
		for (int i = 0; i < NUM_LANES; i++)
		{
			sprintf(buffer, "Lane %d V/Oct", i + 1);
			configOutput (VOCT_OUTPUT + i, buffer);
			sprintf(buffer, "Lane %d Gate", i + 1);
			configOutput (GATE_OUTPUT + i, buffer);
			sprintf(buffer, "Lane %d Velocity", i + 1);
			configOutput (VEL_OUTPUT + i, buffer);
			sprintf(buffer, "Lane %d Overflow", i + 1);
			configOutput (OVERFLOW_OUTPUT + i, buffer);
		}
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize()
	{
		allocator.reset();
	}

	void moduleReset()
	{
		styleChanged = true;
	}

	/**
		Chain-walk to find the Hub (checked on both sides - there's only one Hub, but an
		expander may sit to its left or its right, see LanesShared.hpp's resolveLanesHub()),
		run our own voice allocator against its raw per-source state, then relay the result
		to our own CV ports.
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

		LanesHubInterface *hubLeft  = resolveLanesHub(leftExpander.module);
		LanesHubInterface *hubRight = resolveLanesHub(rightExpander.module);
		lanesHub = hubLeft ? hubLeft : hubRight;
		// Only a real conflict if left and right resolve to two DIFFERENT Hubs - in a plain
		// chain (Hub | LanesCV | LanesMidi), the middle expander reaches the same Hub both
		// directly (left) and indirectly through its other neighbor (right), which is
		// perfectly healthy, not ambiguous.
		bool hubConflict = hubLeft && hubRight && hubLeft != hubRight;
		lanesHubAmbiguous = hubConflict;

		// Chain-health color, shared by both corner lights (see LanesCV.hpp) - only whether
		// each light is lit at all depends on that specific side's own connection.
		float healthGreen, healthRed;
		if (hubConflict)               { healthGreen = 0.f;   healthRed = 255.f; }	// red: two different Hubs reachable
		else if (hubLeft || hubRight)  { healthGreen = 255.f; healthRed = 0.f;   }	// green: healthy, one Hub (either or both sides)
		else                           { healthGreen = 255.f; healthRed = 255.f; }	// yellow: connected, but no Hub anywhere
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

		for (int lane = 0; lane < NUM_LANES; lane++)
		{
			if (!lanesHub)
			{
				setOutPolyChannels (VOCT_OUTPUT + lane, 0);
				setOutPolyChannels (GATE_OUTPUT + lane, 0);
				setOutPolyChannels (VEL_OUTPUT  + lane, 0);
				setStateOutput (OVERFLOW_OUTPUT + lane, 0.f);
				setStateLight  (OVERFLOW_LIGHT  + lane, 0.f);
				continue;
			}

			int channelCount = allocator.getLaneChannelCount(lane);
			for (int slot = 0; slot < channelCount; slot++)
			{
				setStateOutPoly (VOCT_OUTPUT + lane, slot, allocator.getLaneVoct(lane, slot));
				setStateOutPoly (GATE_OUTPUT + lane, slot, allocator.getLaneGate(lane, slot) ? 10.f : 0.f);
				setStateOutPoly (VEL_OUTPUT  + lane, slot, allocator.getLaneVelocity(lane, slot));
			}
			setOutPolyChannels (VOCT_OUTPUT + lane, channelCount);
			setOutPolyChannels (GATE_OUTPUT + lane, channelCount);
			setOutPolyChannels (VEL_OUTPUT  + lane, channelCount);

			bool overflow = allocator.getLaneOverflow(lane);
			setStateOutput (OVERFLOW_OUTPUT + lane, overflow ? 10.f : 0.f);
			setStateLight  (OVERFLOW_LIGHT  + lane, overflow ? 255.f : 0.f);
		}
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

/**
	Main Module Widget

	v1 panel: plain functional layout, no custom artwork yet (see res/LanesCVWork.svg).
	Jack coordinates below match 1:1 the grid documented in res/LanesCVWork.svg so the
	guide layer and the actual widget stay in sync once custom panels are authored -
	same grid pitch as LANES' former output blocks, before the Hub/Expander split.
*/
struct LanesCVWidget : ModuleWidget
{
	LanesCVWidget(LanesCV *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesCVOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesCVBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LanesCVDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		// Measured from the "Controls" layer of res/LanesCVWork.svg - identical grid to
		// LANES' own input blocks (see Lanes.cpp), since this panel continues that same grid.
		static const float ROW_PITCH  = 13.208002f;
		static const float COL_PITCH  = 9.398000f;
		static const float BLOCK_X[2] = { 7.112002f, 51.054000f };
		static const float ROW0_Y[2]  = { 21.866941f, 21.823511f };

		for (int block = 0; block < 2; block++)
		{
			float blockX = BLOCK_X[block];
			for (int row = 0; row < 8; row++)
			{
				int lane = block * 8 + row;
				float y = ROW0_Y[block] + row * ROW_PITCH;
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 0.f * COL_PITCH, y, 0.f), module, VOCT_OUTPUT + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 1.f * COL_PITCH, y, 0.f), module, GATE_OUTPUT + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 2.f * COL_PITCH, y, 0.f), module, VEL_OUTPUT  + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 3.f * COL_PITCH, y, 0.f), module, OVERFLOW_OUTPUT + lane));
				addChild  (createLightCentered<LargeLight<RedLight>> (calculateCoordinates (blockX + 3.f * COL_PITCH, y, 0.f), module, OVERFLOW_LIGHT + lane));
			}
		}

		// Tiny bi-color corner lights - off/green/yellow/red chain-health signal (see
		// moduleProcess()'s resolveLanesHub() calls and LanesCV.hpp). Placeholder position
		// (panel is 86.36mm wide) until Dieter places guide art for them.
		addChild (createLightCentered<TinyLight<GreenRedLight>> (calculateCoordinates (3.5f, 4.f, 0.f), module, LEFT_CONN_LIGHT));
		addChild (createLightCentered<TinyLight<GreenRedLight>> (calculateCoordinates (82.86f, 4.f, 0.f), module, RIGHT_CONN_LIGHT));

		if (module)
			module->widgetReady = true;
	}

	struct LanesCVStyleItem : MenuItem
	{
		LanesCV *module;
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

		LanesCV *module = dynamic_cast<LanesCV *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		LanesCVStyleItem *style1Item = new LanesCVStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		LanesCVStyleItem *style2Item = new LanesCVStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		LanesCVStyleItem *style3Item = new LanesCVStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelLanesCV = createModel<LanesCV, LanesCVWidget>("LanesCV");
