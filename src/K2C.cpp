/*
	K2C.cpp

	Code for the OrangeLine module K2C

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

#include "K2C.hpp"

struct K2C : Module
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	K2C()
	{
		initializeInstance();
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
		setInPoly (VOCT_INPUT, true);
		setInPoly (GATE_INPUT, true);
		setInPoly (VEL_INPUT, true);
		setInPoly (MATCH_INPUT, true);

		setOutPoly (VOCT_OUTPUT, true);
		setOutPoly (GATE_OUTPUT, true);
		setOutPoly (VEL_OUTPUT, true);
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel (STYLE_JSON, "style");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		configInput (VOCT_INPUT, "V/Oct");
		configInput (GATE_INPUT, "Gate");
		configInput (VEL_INPUT, "Velocity");
		configInput (MATCH_INPUT, "Match pitch per channel");
		configOutput (VOCT_OUTPUT, "V/Oct");
		configOutput (GATE_OUTPUT, "Gate");
		configOutput (VEL_OUTPUT, "Velocity");
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		styleChanged = true;
	}

	/**
		Stateless: fully recomputed every control-rate tick, no persistent per-channel
		state needed since a channel's pitch identity never changes.
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

		/*
			1. Compute the 16 match pitches: real CV values for connected channels, then
			   continue chromatically (+1 semitone per channel) from the last known value.
			   If MATCH IN is fully unpatched, start the chromatic run at C4 (0V).
		*/
		float matchPitch[POLY_CHANNELS];
		bool  matchConnected = getInputConnected (MATCH_INPUT);
		int   connected      = matchConnected ? inputs[MATCH_INPUT].getChannels() : 0;
		/*
			Track the seed as an integer semitone count and always derive matchPitch[n] via
			the same "round(...)/12" division quantize() itself uses - not by repeatedly
			adding SEMITONE (1.f/12.f isn't exactly representable in float, so accumulating
			it channel by channel drifts from quantize()'s result and the exact == comparison
			below would silently stop matching after a few channels).
		*/
		int lastSemitone = 0;	// C4 default seed
		for (int n = 0; n < POLY_CHANNELS; n++)
		{
			if (n < connected)
			{
				lastSemitone = (int) round (OL_statePoly[MATCH_INPUT * POLY_CHANNELS + n] * 12.f);
			}
			else if (!(connected == 0 && n == 0))
			{
				lastSemitone += 1;
			}
			matchPitch[n] = lastSemitone / 12.f;
		}

		/*
			2. Scan the main input channels in order; the last (highest-index) match for a
			   given output channel wins - simple overwrite, no merge/contributor tracking.
		*/
		bool  outGate[POLY_CHANNELS];
		float outVel [POLY_CHANNELS];
		memset (outGate, 0,  sizeof(outGate));
		memset (outVel,  0.f, sizeof(outVel));

		bool gateConnected = getInputConnected (GATE_INPUT);
		bool voctConnected = getInputConnected (VOCT_INPUT);
		bool velConnected  = getInputConnected (VEL_INPUT);
		int  mainChannels  = gateConnected ? inputs[GATE_INPUT].getChannels() : 0;

		for (int c = 0; c < mainChannels; c++)
		{
			if (OL_statePoly[GATE_INPUT * POLY_CHANNELS + c] <= 5.f)
				continue;
			float pitch = voctConnected ? quantize (OL_statePoly[VOCT_INPUT * POLY_CHANNELS + c]) : 0.f;
			float vel   = velConnected  ? OL_statePoly[VEL_INPUT  * POLY_CHANNELS + c] : 0.f;
			for (int n = 0; n < POLY_CHANNELS; n++)
			{
				if (matchPitch[n] == pitch)
				{
					outGate[n] = true;
					outVel[n]  = vel;
				}
			}
		}

		/*
			3. Output: V/Oct always shows the channel's fixed pitch identity, Gate/Velocity
			   reflect whether a matching note is currently held.
		*/
		for (int n = 0; n < POLY_CHANNELS; n++)
		{
			setStateOutPoly (VOCT_OUTPUT, n, matchPitch[n]);
			setStateOutPoly (GATE_OUTPUT, n, outGate[n] ? 10.f : 0.f);
			setStateOutPoly (VEL_OUTPUT,  n, outGate[n] ? outVel[n] : 0.f);
		}
		// Channel-to-pitch assignment is static (channel N is always the identity for
		// matchPitch[N]), but OL_polyChannels is memset back to 0 by initializeInstance()
		// right after moduleInitStateTypes() runs - so it has to be (re)asserted every tick
		// here instead, like every other OrangeLine module does.
		setOutPolyChannels (VOCT_OUTPUT, POLY_CHANNELS);
		setOutPolyChannels (GATE_OUTPUT, POLY_CHANNELS);
		setOutPolyChannels (VEL_OUTPUT,  POLY_CHANNELS);
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

/**
	Main Module Widget

	Jack centers below are measured directly from the "Controls" layer of res/KeysWork.svg
	(single column, 3HP-wide panel), so the widget lines up exactly with the panel art.
	Order top to bottom: MATCH IN, then the note-input trio (V/Oct, Gate, Vel), then the
	output trio (V/Oct, Gate, Vel).
*/
struct K2CWidget : ModuleWidget
{
	K2CWidget(K2C *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/K2COrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/K2CBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/K2CDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.598f, 14.479f, 0.f), module, MATCH_INPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 35.815f, 0.f), module, VOCT_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 50.547f, 0.f), module, GATE_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 65.533f, 0.f), module, VEL_INPUT));

		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.621f, 84.075f,  0.f), module, VOCT_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.621f, 98.807f,  0.f), module, GATE_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.621f, 113.793f, 0.f), module, VEL_OUTPUT));

		if (module)
			module->widgetReady = true;
	}

	struct K2CStyleItem : MenuItem
	{
		K2C *module;
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

		K2C *module = dynamic_cast<K2C *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		K2CStyleItem *style1Item = new K2CStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		K2CStyleItem *style2Item = new K2CStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		K2CStyleItem *style3Item = new K2CStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelK2C = createModel<K2C, K2CWidget>("K2C");
