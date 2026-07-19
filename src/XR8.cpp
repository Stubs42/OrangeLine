/*
	XR8.cpp

	Code for the OrangeLine module XR8

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
#include <cmath>
#include <cstring>

#include "XR8.hpp"
#include "XOCommon.hpp"

#define XR8_PANEL_WIDTH_MM 15.24f

struct XR8 : Module, XOExpanderInterface, ExpanderBridgeInterface
{

#include "OrangeLineCommon.hpp"
#include "XRModuleCommon.hpp"

	XR8()
	{
		initializeInstance();
		// One-time init, not moduleCustomInitialize() (which runs every tick, not once) - see
		// CLAUDE.md's own pitfall on this. NAN so the very first per-channel comparison in
		// moduleProcess() always registers as "changed" with no separate first-tick flag needed.
		for (int c = 0; c < XR_CAPACITY; c++)
		{
			lastSeed[c] = NAN;
			for (int k = 0; k < POLY_CHANNELS; k++)
				randomValue[c][k] = 0.f;
		}

		// xoConnectedHostId is int64_t, not a float OL_state slot - see its own member comment
		// (XRModuleCommon.hpp) for why.
		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_object_set_new(rootJ, "connectedHostId", json_integer(xoConnectedHostId));
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *idJ = json_object_get(rootJ, "connectedHostId");
			if (idJ && json_is_integer(idJ))
				xoConnectedHostId = json_integer_value(idJ);
		};
	}
};

/**
	Main Module Widget - same panel layout as XO8 (LEFT/RIGHT buttons, name display, jack column),
	just reading a seeded-random value instead of the Host's raw output.
*/
struct XR8Widget : ModuleWidget
{
	struct XOStepButton : XOButtonBase { XOStepButton() { box.size = mm2px(Vec(4.6f, 4.6f)); } };

	XExtStripWidget *extStrip = nullptr;     // right edge - toward a further-chained Expander
	XExtStripWidget *extStripLeft = nullptr; // left edge - toward the Host

	XOLogoCover *logoCover1 = nullptr;
	XOLogoCover *logoCover2 = nullptr;

	PJ301MPort *ports[XR_CAPACITY] = {};

	XR8Widget(XR8 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XR8Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XR8Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XR8Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addChild(createLightCentered<AutoHideLight<TinyLight<RedLight>>>(calculateCoordinates(XR8_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, OVERFLOW_LIGHT));

		XOStepButton *leftButton = createParamCentered<XOStepButton>(calculateCoordinates(4.550f, 18.034f, 0.f), module, LEFT_PARAM);
		leftButton->label = "<";
		addParam(leftButton);
		XOStepButton *rightButton = createParamCentered<XOStepButton>(calculateCoordinates(10.657f, 18.035f, 0.f), module, RIGHT_PARAM);
		rightButton->label = ">";
		addParam(rightButton);

		XONameDisplay *nameDisplay = new XONameDisplay();
		nameDisplay->module = module;
		nameDisplay->box.pos = calculateCoordinates(1.41287f, 12.449f, 0.f);
		nameDisplay->box.size = mm2px(Vec(13.f, 5.f));
		addChild(nameDisplay);

		static const float portY[XR_CAPACITY] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		for (int i = 0; i < XR_CAPACITY; i++)
		{
			// Plain jack, no XOOutputPort accent ring - that ring signals "this port mirrors a
			// browsable XO-family candidate slot," which doesn't apply here: XR8's own output is
			// a generated, terminal signal, not something a further Expander browses/matches by
			// color (confirmed explicitly - a ring here would be misleading).
			PJ301MPort *port = createOutputCentered<PJ301MPort>(calculateCoordinates(7.62f, portY[i], 0.f), module, CHANNEL_OUTPUT + i);
			addOutput(port);
			ports[i] = port;
		}

		extStrip = addXExtStrip(this, XR8_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXOLogoCovers(this, XR8_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		addOrangeLineTouchPorts(this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		XR8 *xr8Module = dynamic_cast<XR8 *>(module);
		if (xr8Module)
		{
			updateXOExtStrip(extStrip, xr8Module, xr8Module->rightExpander.module);
			updateXOExtStripLeft(extStripLeft, xr8Module, xr8Module->leftExpander.module);
			updateXOLogoCovers(logoCover1, logoCover2, xr8Module);
		}
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		XR8 *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = (module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	// Per-channel output range submenu, two levels deep - see CLAUDE.md's "submenu/radio-style
	// item" section: leaf structs nested inside their own parent, every leaf item needs an
	// explicit setSize() or the submenu renders empty. "Channel Ranges" -> "Channel N" -> one of
	// the 8 XR_RANGE_OPTIONS.
	struct XRChannelRangesItem : MenuItem
	{
		XR8 *module;

		struct XRChannelItem : MenuItem
		{
			XR8 *module;
			int channel;

			struct XRRangeItem : MenuItem
			{
				XR8 *module;
				int channel;
				int rangeIdx;
				void onAction(const event::Action &e) override
				{
					module->OL_setOutState(CHANNEL_RANGE_JSON + channel, float(rangeIdx));
				}
				void step() override
				{
					if (module)
						rightText = (module->OL_state[CHANNEL_RANGE_JSON + channel] == rangeIdx) ? "✔" : "";
				}
			};

			Menu *createChildMenu() override
			{
				Menu *menu = new Menu;
				for (int i = 0; i < 8; i++)
				{
					XRRangeItem *item = new XRRangeItem();
					item->module = module;
					item->channel = channel;
					item->rangeIdx = i;
					item->text = XR_RANGE_OPTIONS[i].label;
					item->setSize(Vec(70, 20));
					menu->addChild(item);
				}
				return menu;
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			for (int c = 0; c < XR_CAPACITY; c++)
			{
				XRChannelItem *item = new XRChannelItem();
				item->module = module;
				item->channel = c;
				item->text = string::f("Channel %d", c + 1);
				item->rightText = RIGHT_ARROW;
				item->setSize(Vec(70, 20));
				menu->addChild(item);
			}
			return menu;
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		XR8 *module = dynamic_cast<XR8 *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		XOStyleItem *style1Item = new XOStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		XOStyleItem *style2Item = new XOStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		XOStyleItem *style3Item = new XOStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		addXODisconnectMenuItem(menu, module);

		XRChannelRangesItem *rangesItem = new XRChannelRangesItem();
		rangesItem->module = module;
		rangesItem->text = "Channel Ranges";
		rangesItem->rightText = RIGHT_ARROW;
		menu->addChild(rangesItem);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		addOrangeLineTouchMenuItem(menu, module->OL_touchInPort, module->OL_touchOutPort, &module->OL_touchVisible);
	}
};

Model *modelXR8 = createModel<XR8, XR8Widget>("XR8");
