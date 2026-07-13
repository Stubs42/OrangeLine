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
	LanesVoiceAllocator<POLY_CHANNELS> allocator;

	LanesCV()
	{
		initializeInstance();
	}

	LanesHubInterface* getLanesHub() override { return lanesHub; }

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
		Chain-walk to find the Hub, run our own voice allocator against its raw per-source
		state, then relay the result to our own CV ports. See LanesShared.hpp for why
		dynamic_cast (not a model==check) is used - this lets LanesCV sit anywhere in a
		chain of mixed expander types, not just directly next to the Hub.
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

		lanesHub = nullptr;
		Module *left = leftExpander.module;
		if (left)
		{
			lanesHub = dynamic_cast<LanesHubInterface*>(left);
			if (!lanesHub)
			{
				LanesExpanderInterface *link = dynamic_cast<LanesExpanderInterface*>(left);
				if (link)
					lanesHub = link->getLanesHub();
			}
		}

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

		static const float ROW0_Y     = 21.845226f;
		static const float ROW_PITCH  = 13.208002f;
		static const float COL_PITCH  = 9.398000f;
		static const float BLOCK_X[2] = { 7.112002f, 48.514002f };

		for (int block = 0; block < 2; block++)
		{
			float blockX = BLOCK_X[block];
			for (int row = 0; row < 8; row++)
			{
				int lane = block * 8 + row;
				float y = ROW0_Y + row * ROW_PITCH;
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 0.f * COL_PITCH, y, 0.f), module, VOCT_OUTPUT + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 1.f * COL_PITCH, y, 0.f), module, GATE_OUTPUT + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 2.f * COL_PITCH, y, 0.f), module, VEL_OUTPUT  + lane));
				addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (blockX + 3.f * COL_PITCH, y, 0.f), module, OVERFLOW_OUTPUT + lane));
				addChild  (createLightCentered<LargeLight<RedLight>> (calculateCoordinates (blockX + 3.f * COL_PITCH, y, 0.f), module, OVERFLOW_LIGHT + lane));
			}
		}

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
