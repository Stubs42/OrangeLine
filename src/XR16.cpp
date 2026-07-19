/*
	XR16.cpp

	Code for the OrangeLine module XR16

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

#include "XR16.hpp"
#include "XOCommon.hpp"

#define XR16_PANEL_WIDTH_MM 30.48f

struct XR16 : Module, XOExpanderInterface, ExpanderBridgeInterface
{

#include "OrangeLineCommon.hpp"
#include "XRModuleCommon.hpp"

	XR16()
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
	Main Module Widget - two 8-jack columns, channels 8-15 shifted right by half the panel width,
	same doubling convention as XO16's own two jack columns.
*/
struct XR16Widget : ModuleWidget
{
	struct XOStepButton : XOButtonBase { XOStepButton() { box.size = mm2px(Vec(12.192f, 4.572f)); } };

	XExtStripWidget *extStrip = nullptr;
	XExtStripWidget *extStripLeft = nullptr;
	XOLogoCover *logoCover1 = nullptr;
	XOLogoCover *logoCover2 = nullptr;

	PJ301MPort *ports[XR_CAPACITY] = {};

	XR16Widget(XR16 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XR16Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XR16Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XR16Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(XR16_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, OVERFLOW_LIGHT));

		XOStepButton *leftButton = createParamCentered<XOStepButton>(calculateCoordinates(8.382f, 18.035f, 0.f), module, LEFT_PARAM);
		leftButton->label = "<";
		addParam(leftButton);
		XOStepButton *rightButton = createParamCentered<XOStepButton>(calculateCoordinates(22.098f, 18.035f, 0.f), module, RIGHT_PARAM);
		rightButton->label = ">";
		addParam(rightButton);

		XONameDisplay *nameDisplay = new XONameDisplay();
		nameDisplay->module = module;
		nameDisplay->box.pos = calculateCoordinates(1.41287f, 12.449f, 0.f);
		nameDisplay->box.size = mm2px(Vec(13.f, 5.f));
		addChild(nameDisplay);

		static const float portY[8] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		static const float columnX[2] = { 7.62f, 22.86f };
		for (int col = 0; col < 2; col++)
		{
			for (int row = 0; row < 8; row++)
			{
				int channel = col * 8 + row;
				// Plain jack, no XOOutputPort accent ring - see XR8Widget's own comment on why:
				// this output is a generated, terminal signal, not a browsable XO-family slot.
				PJ301MPort *port = createOutputCentered<PJ301MPort>(calculateCoordinates(columnX[col], portY[row], 0.f), module, CHANNEL_OUTPUT + channel);
				addOutput(port);
				ports[channel] = port;
			}
		}

		extStrip = addXExtStrip(this, XR16_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXOLogoCovers(this, XR16_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		addOrangeLineTouchPorts(this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		XR16 *xr16Module = dynamic_cast<XR16 *>(module);
		if (xr16Module)
		{
			updateXOExtStrip(extStrip, xr16Module, xr16Module->rightExpander.module);
			updateXOExtStripLeft(extStripLeft, xr16Module, xr16Module->leftExpander.module);
			updateXOLogoCovers(logoCover1, logoCover2, xr16Module);
		}
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		XR16 *module;
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
		XR16 *module;

		struct XRChannelItem : MenuItem
		{
			XR16 *module;
			int channel;

			struct XRRangeItem : MenuItem
			{
				XR16 *module;
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

		XR16 *module = dynamic_cast<XR16 *>(this->module);
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

Model *modelXR16 = createModel<XR16, XR16Widget>("XR16");
