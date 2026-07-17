/*
	XD8.cpp

	Code for the OrangeLine module XD8

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

#include "XD8.hpp"
#include "XOCommon.hpp"

#define XD8_PANEL_WIDTH_MM 15.24f

struct XD8 : Module, XOExpanderInterface
{

#include "OrangeLineCommon.hpp"
#include "XOModuleCommon.hpp"

	XD8()
	{
		initializeInstance();
	}
};

/**
	Main Module Widget - pure monitor, no real output jacks at all: a per-channel value/gate
	display column occupies the space XO8's own jack column would have used.
*/
struct XD8Widget : ModuleWidget
{
	struct XOStepButton : XOButtonBase { XOStepButton() { box.size = mm2px(Vec(4.6f, 4.6f)); } };

	XExtStripWidget *extStrip = nullptr;
	XExtStripWidget *extStripLeft = nullptr;
	XOLogoCover *logoCover1 = nullptr;
	XOLogoCover *logoCover2 = nullptr;

	XOValueDisplay *displays[XO_CAPACITY] = {};
	XOGateIndicator *gates[XO_CAPACITY] = {};

	XD8Widget(XD8 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XD8Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XD8Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XD8Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(3.5f, 4.f, 0.f), module, CONN_LIGHT));
		addChild(createLightCentered<TinyLight<RedLight>>(calculateCoordinates(XD8_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, OVERFLOW_LIGHT));

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

		// Same column position as X8's own knobs / XO8's own jacks - a plain value/gate display
		// cell instead, same narrow 13mm width as the name display above (XD8's panel is narrow,
		// unlike XOD8's own wider per-channel display which sits beside a jack).
		static const float displayY[XO_CAPACITY] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		for (int i = 0; i < XO_CAPACITY; i++)
		{
			XOValueDisplay *display = new XOValueDisplay();
			display->module = module;
			display->channel = i;
			display->box.pos = calculateCoordinates(1.41287f, displayY[i], 0.f);
			display->box.size = mm2px(Vec(13.f, 5.f));
			addChild(display);
			displays[i] = display;

			XOGateIndicator *gate = new XOGateIndicator();
			gate->module = module;
			gate->channel = i;
			// Same box as the value display it morphs with (see step() below) - no separate
			// hand-guessed offset/size, so the lit square lands exactly where the number would.
			gate->box.pos = display->box.pos;
			gate->box.size = display->box.size;
			gate->visible = false;
			addChild(gate);
			gates[i] = gate;
		}

		extStrip = addXExtStrip(this, XD8_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXOLogoCovers(this, XD8_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		XD8 *xd8Module = dynamic_cast<XD8 *>(module);
		if (xd8Module)
		{
			updateXOExtStrip(extStrip, xd8Module, xd8Module->rightExpander.module);
			updateXOExtStripLeft(extStripLeft, xd8Module, xd8Module->leftExpander.module);
			updateXOLogoCovers(logoCover1, logoCover2, xd8Module);
			bool showGate = xd8Module->getXOBrowsedType() == XO_TYPE_GATE;
			for (int i = 0; i < XO_CAPACITY; i++)
			{
				displays[i]->visible = !showGate;
				gates[i]->visible = showGate;
			}
		}
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		XD8 *module;
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

		XD8 *module = dynamic_cast<XD8 *>(this->module);
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
	}
};

Model *modelXD8 = createModel<XD8, XD8Widget>("XD8");
