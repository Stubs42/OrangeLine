/*
	XD16.cpp

	Code for the OrangeLine module XD16

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

#include "XD16.hpp"
#include "XOCommon.hpp"

#define XD16_PANEL_WIDTH_MM 30.48f

struct XD16 : Module, XOExpanderInterface
{

#include "OrangeLineCommon.hpp"
#include "XOModuleCommon.hpp"

	XD16()
	{
		initializeInstance();
	}
};

/**
	Main Module Widget - pure monitor, no real output jacks at all: two 8-cell value/gate display
	columns occupy the space XO16's own two jack columns would have used.
*/
struct XD16Widget : ModuleWidget
{
	struct XOStepButton : XOButtonBase { XOStepButton() { box.size = mm2px(Vec(12.192f, 4.572f)); } };

	XExtStripWidget *extStrip = nullptr;
	XExtStripWidget *extStripLeft = nullptr;
	XOLogoCover *logoCover1 = nullptr;
	XOLogoCover *logoCover2 = nullptr;

	XOValueDisplay *displays[XO_CAPACITY] = {};
	XOGateIndicator *gates[XO_CAPACITY] = {};
	XOButtonCover *covers[XO_CAPACITY] = {};

	XD16Widget(XD16 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XD16Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XD16Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XD16Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addOrangeLineConnectionLight<AutoHideLight<TinyLight<GreenRedLight>>>(this, calculateCoordinates(3.5f, 4.f, 0.f), module, CONN_LIGHT);
		addChild(createLightCentered<TinyLight<RedLight>>(calculateCoordinates(XD16_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, OVERFLOW_LIGHT));

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

		// Baseline Y values, NOT the same as the knob/jack row Y - text baseline sits a bit below
		// each row's own nominal center, exactly like XOD16Widget's own displayY (reused verbatim
		// here, since both share the same per-row hand-measured baseline positions).
		static const float displayY[8] = {
			38.873039f, 49.828312f, 60.78289f, 71.737473f,
			82.692047f, 93.646629f, 104.60121f, 115.55579f
		};
		static const float columnX[2] = { 7.62f, 22.86f };
		// The gate indicator's own frame needs to match the REAL decorative display cell's own
		// bounding box (measured directly from res/XD16Work.svg's own edited "rect5-*"/
		// "rect5-*b" paths: x=1.513/16.0, width=12.965, height=4.61 - not XOValueDisplay's
		// baseline-anchored text position above, see XOD8Widget's own comment on this same
		// distinction). box.size is now the cap's own natural (larger) size, set by
		// XOGateIndicator's constructor, not the panel rect's own smaller size, so gates below
		// are positioned by row CENTER (rect x + half width) rather than the rect's top-left.
		static const float gateBoxY[8] = {
			35.04f, 46.0f, 56.95f, 67.91f,
			78.86f, 89.82f, 100.77f, 111.73f
		};
		static const float gateCenterX[2] = { 7.9955f, 22.4825f };
		// Original rect5-* top-left x per column (1.513/16.0mm), matching gateCenterX - half the
		// 12.965mm cell width - see the always-visible cover below.
		static const float coverX[2] = { 1.513f, 16.0f };
		for (int col = 0; col < 2; col++)
		{
			for (int row = 0; row < 8; row++)
			{
				int channel = col * 8 + row;

				// Always-visible per-row background, sized to fully cover the real panel
				// decoration underneath (res/XD16Work.svg's own edited "rect5-*"/"rect5-*b" paths:
				// width=12.965, height=4.61) minus a 0.15mm inset on all four sides, same
				// reasoning/values as XD8Widget's own per-row cover - never toggled, since both the
				// value display and the gate indicator draw their own content on top of it.
				XOButtonCover *cover = new XOButtonCover();
				cover->module = module;
				cover->box.pos = calculateCoordinates(coverX[col] + 0.15f, gateBoxY[row] + 0.15f, 0.f);
				cover->box.size = mm2px(Vec(12.965f - 0.3f, 4.61f - 0.3f));
				addChild(cover);
				covers[channel] = cover;

				XOValueDisplay *display = new XOValueDisplay();
				display->module = module;
				display->channel = channel;
				display->box.pos = calculateCoordinates(columnX[col] - 6.5f, displayY[row], 0.f);
				display->box.size = mm2px(Vec(13.f, 5.f));
				addChild(display);
				displays[channel] = display;

				XOGateIndicator *gate = new XOGateIndicator();
				gate->module = module;
				gate->channel = channel;
				gate->box.pos = calculateCoordinates(gateCenterX[col], gateBoxY[row] + 2.305f, 0.f).minus(gate->box.size.div(2.f));
				gate->visible = false;
				addChild(gate);
				gates[channel] = gate;
			}
		}

		extStrip = addXExtStrip(this, XD16_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXOLogoCovers(this, XD16_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		XD16 *xd16Module = dynamic_cast<XD16 *>(module);
		if (xd16Module)
		{
			updateXOExtStrip(extStrip, xd16Module, xd16Module->rightExpander.module);
			updateXOExtStripLeft(extStripLeft, xd16Module, xd16Module->leftExpander.module);
			updateXOLogoCovers(logoCover1, logoCover2, xd16Module);
			bool showGate = xd16Module->getXOBrowsedType() == XO_TYPE_GATE;
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
		XD16 *module;
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

		XD16 *module = dynamic_cast<XD16 *>(this->module);
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
	}
};

Model *modelXD16 = createModel<XD16, XD16Widget>("XD16");
