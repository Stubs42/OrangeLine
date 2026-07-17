/*
	XO8.cpp

	Code for the OrangeLine module XO8

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

#include "XO8.hpp"
#include "XOCommon.hpp"

#define XO8_PANEL_WIDTH_MM 15.24f

struct XO8 : Module, XOExpanderInterface
{

#include "OrangeLineCommon.hpp"
#include "XOModuleCommon.hpp"

	XO8()
	{
		initializeInstance();
	}
};

/**
	Main Module Widget
*/
struct XO8Widget : ModuleWidget
{
	// LEFT/RIGHT sizes match X8's own step buttons exactly - same panel width, same header shape.
	struct XOStepButton : XOButtonBase { XOStepButton() { box.size = mm2px(Vec(4.6f, 4.6f)); } };

	XExtStripWidget *extStrip = nullptr;     // right edge - toward a further-chained Expander
	XExtStripWidget *extStripLeft = nullptr; // left edge - toward the Host
	XOLogoCover *logoCover1 = nullptr;
	XOLogoCover *logoCover2 = nullptr;

	XOOutputPort *ports[XO_CAPACITY] = {};

	XO8Widget(XO8 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XO8Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XO8Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XO8Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		// CONN_LIGHT near the Host-facing (left) edge, OVERFLOW_LIGHT near the opposite (right)
		// edge - mirrors the X family's own two-corner-light placement, just facing the other way
		// since the Host sits on this family's LEFT instead of its RIGHT.
		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(3.5f, 4.f, 0.f), module, CONN_LIGHT));
		addChild(createLightCentered<TinyLight<RedLight>>(calculateCoordinates(XO8_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, OVERFLOW_LIGHT));

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

		// 8 channel-split output jacks, top (channel 1) to bottom (channel 8) - same column
		// position/spacing as X8's own knob column (res/X8Work.svg's own knobY positions).
		static const float portY[XO_CAPACITY] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		for (int i = 0; i < XO_CAPACITY; i++)
		{
			XOOutputPort *port = createOutputCentered<XOOutputPort>(calculateCoordinates(7.62f, portY[i], 0.f), module, CHANNEL_OUTPUT + i);
			port->channel = i;
			addOutput(port);
			ports[i] = port;
		}

		extStrip = addXExtStrip(this, XO8_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXOLogoCovers(this, XO8_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		addOrangeLineTouchPorts(this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		XO8 *xo8Module = dynamic_cast<XO8 *>(module);
		if (xo8Module)
		{
			updateXOExtStrip(extStrip, xo8Module, xo8Module->rightExpander.module);
			updateXOExtStripLeft(extStripLeft, xo8Module, xo8Module->leftExpander.module);
			updateXOLogoCovers(logoCover1, logoCover2, xo8Module);
		}
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		XO8 *module;
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

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		XO8 *module = dynamic_cast<XO8 *>(this->module);
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

		addOrangeLineTouchMenuItem(menu, module->OL_touchInPort, module->OL_touchOutPort, &module->OL_touchVisible);
	}
};

Model *modelXO8 = createModel<XO8, XO8Widget>("XO8");
