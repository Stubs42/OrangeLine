/*
	XO16.cpp

	Code for the OrangeLine module XO16

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

#include "XO16.hpp"
#include "XOCommon.hpp"

#define XO16_PANEL_WIDTH_MM 30.48f

struct XO16 : Module, XOExpanderInterface
{

#include "OrangeLineCommon.hpp"
#include "XOModuleCommon.hpp"

	XO16()
	{
		initializeInstance();
	}
};

/**
	Main Module Widget - two 8-jack columns, channels 8-15 shifted right by half the panel
	width, same doubling convention as X16's own two knob columns.
*/
struct XO16Widget : ModuleWidget
{
	struct XOStepButton : XOButtonBase { XOStepButton() { box.size = mm2px(Vec(12.192f, 4.572f)); } };

	XExtStripWidget *extStrip = nullptr;
	XExtStripWidget *extStripLeft = nullptr;
	XOLogoCover *logoCover1 = nullptr;
	XOLogoCover *logoCover2 = nullptr;

	PJ301MPort *ports[XO_CAPACITY] = {};

	XO16Widget(XO16 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XO16Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XO16Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XO16Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addOrangeLineConnectionLight<AutoHideLight<TinyLight<GreenRedLight>>>(this, calculateCoordinates(3.5f, 4.f, 0.f), module, CONN_LIGHT);
		addChild(createLightCentered<AutoHideLight<TinyLight<RedLight>>>(calculateCoordinates(XO16_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, OVERFLOW_LIGHT));

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
				// Plain jack, no accent ring - these outputs are plain poly outputs, not something
				// a further X-family Expander can browse/color-match (same as XR8/XR16).
				PJ301MPort *port = createOutputCentered<PJ301MPort>(calculateCoordinates(columnX[col], portY[row], 0.f), module, CHANNEL_OUTPUT + channel);
				addOutput(port);
				ports[channel] = port;
			}
		}

		extStrip = addXExtStrip(this, XO16_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXOLogoCovers(this, XO16_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		addOrangeLineTouchPorts(this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		XO16 *xo16Module = dynamic_cast<XO16 *>(module);
		if (xo16Module)
		{
			updateXOExtStrip(extStrip, xo16Module, xo16Module->rightExpander.module);
			updateXOExtStripLeft(extStripLeft, xo16Module, xo16Module->leftExpander.module);
			updateXOLogoCovers(logoCover1, logoCover2, xo16Module);
		}
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		XO16 *module;
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

		XO16 *module = dynamic_cast<XO16 *>(this->module);
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

		addOrangeLineTouchMenuItem(menu, module->OL_touchInPort, module->OL_touchOutPort, &module->OL_touchVisible);
	}
};

Model *modelXO16 = createModel<XO16, XO16Widget>("XO16");
