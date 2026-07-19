/*
	XOD16.cpp

	Code for the OrangeLine module XOD16

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

// XOD16 reuses XO16's own enums verbatim (identical shape - see XOModuleCommon.hpp's own
// comment on why the family shares this much) rather than declaring a separate XOD16.hpp.
#include "XO16.hpp"
#include "XOCommon.hpp"

#define XOD16_PANEL_WIDTH_MM 60.96f

struct XOD16 : Module, XOExpanderInterface, ExpanderBridgeInterface
{

#include "OrangeLineCommon.hpp"
#include "XOModuleCommon.hpp"

	XOD16()
	{
		initializeInstance();

		// xoConnectedHostId is int64_t, not a float OL_state slot - see its own member comment
		// (XOModuleCommon.hpp) for why.
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
	Main Module Widget - widest of the family, two (jack+display) columns, channels 8-15 shifted
	right by half the panel width, same doubling convention as X16D's own two knob+display
	columns.
*/
struct XOD16Widget : ModuleWidget
{
	struct XOStepButton : XOButtonBase { XOStepButton() { box.size = mm2px(Vec(27.432f, 4.572f)); } };

	XExtStripWidget *extStrip = nullptr;
	XExtStripWidget *extStripLeft = nullptr;
	XOLogoCover *logoCover1 = nullptr;
	XOLogoCover *logoCover2 = nullptr;

	PJ301MPort *ports[XO_CAPACITY] = {};
	XOValueDisplay *displays[XO_CAPACITY] = {};
	XOGateIndicator *gates[XO_CAPACITY] = {};
	XOButtonCover *buttonCovers[2] = {};

	XOD16Widget(XOD16 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XOD16Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XOD16Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XOD16Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		// Moved down into the open gap between the LEFT/RIGHT nav buttons and the first row
		// (centered horizontally, same y as XO8/XD8's own identical repositioning) rather than
		// the old top-right corner - see XO8.cpp's own comment for the full reasoning.
		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(XOD16_PANEL_WIDTH_MM / 2.f, 26.163f, 0.f), module, OVERFLOW_LIGHT));

		XOStepButton *leftButton = createParamCentered<XOStepButton>(calculateCoordinates(16.002f, 18.035f, 0.f), module, LEFT_PARAM);
		leftButton->label = "<";
		addParam(leftButton);
		XOStepButton *rightButton = createParamCentered<XOStepButton>(calculateCoordinates(44.958f, 18.035f, 0.f), module, RIGHT_PARAM);
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
		static const float displayY[8] = {
			38.873039f, 49.828312f, 60.78289f, 71.737473f,
			82.692047f, 93.646629f, 104.60121f, 115.55579f
		};
		static const float portColumnX[2] = { 7.62f, 38.1f };
		static const float displayColumnX[2] = { 15.12887f, 45.608871f };
		// The gate indicator's own frame needs to match the REAL decorative display cell's own
		// bounding box (measured directly from res/X16DWork.svg's own "rect5-*"/"rect5-*b"
		// elements: x=14.478/44.958, width=13.716, height=4.572 - not XOValueDisplay's baseline-
		// anchored text position above, see XOD8Widget's own comment on this same distinction).
		// box.size is now the cap's own natural (larger) size, set by XOGateIndicator's
		// constructor, not the panel rect's own smaller size, so gates below are positioned by
		// row CENTER (rect x + half width) rather than the rect's top-left corner.
		static const float gateBoxY[8] = {
			35.0533f, 46.0085f, 56.9631f, 67.9177f,
			78.8723f, 89.8268f, 100.7814f, 111.736f
		};
		static const float gateCenterX[2] = { 21.336f, 51.816f };

		// Always-visible display-column background per column (the panel's own static decoration
		// there has been removed entirely - see XD8Widget's own per-row cover for the same
		// reasoning). Column 2's geometry comes directly from Dieter's own MASK guide rect in
		// res/XOD16Work.svg; column 1's is the same rect shifted left by exactly one column's
		// worth (displayColumnX[1] - displayColumnX[0] = 30.48mm), matching the same offset the
		// guide rect itself sits at relative to displayColumnX[1]. Added BEFORE the per-channel
		// gate indicators/displays below so it draws underneath them (addChild order is also draw
		// order).
		static const float coverX[2] = { 11.822242f, 42.302242f };
		for (int col = 0; col < 2; col++)
		{
			XOButtonCover *cover = new XOButtonCover();
			cover->module = module;
			cover->box.pos = calculateCoordinates(coverX[col], 32.76725f, 0.f);
			cover->box.size = mm2px(Vec(17.387756f, 85.090004f));
			addChild(cover);
			buttonCovers[col] = cover;
		}

		for (int col = 0; col < 2; col++)
		{
			for (int row = 0; row < 8; row++)
			{
				int channel = col * 8 + row;

				// Plain jack, no accent ring - these outputs are plain poly outputs, not something
				// a further X-family Expander can browse/color-match (same as XR8/XR16).
				PJ301MPort *port = createOutputCentered<PJ301MPort>(calculateCoordinates(portColumnX[col], portY[row], 0.f), module, CHANNEL_OUTPUT + channel);
				addOutput(port);
				ports[channel] = port;

				XOValueDisplay *display = new XOValueDisplay();
				display->module = module;
				display->channel = channel;
				display->box.pos = calculateCoordinates(displayColumnX[col], displayY[row], 0.f);
				display->box.size = mm2px(Vec(15.f, 5.f));
				addChild(display);
				displays[channel] = display;

				XOGateIndicator *gate = new XOGateIndicator();
				gate->module = module;
				gate->channel = channel;
				gate->box.pos = calculateCoordinates(gateCenterX[col], gateBoxY[row] + 2.286f, 0.f).minus(gate->box.size.div(2.f));
				gate->visible = false;
				addChild(gate);
				gates[channel] = gate;
			}
		}

		extStrip = addXExtStrip(this, XOD16_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXOLogoCovers(this, XOD16_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		addOrangeLineTouchPorts(this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		XOD16 *xod16Module = dynamic_cast<XOD16 *>(module);
		if (xod16Module)
		{
			updateXOExtStrip(extStrip, xod16Module, xod16Module->rightExpander.module);
			updateXOExtStripLeft(extStripLeft, xod16Module, xod16Module->leftExpander.module);
			updateXOLogoCovers(logoCover1, logoCover2, xod16Module);
			bool showGate = xod16Module->getXOBrowsedType() == XO_TYPE_GATE;
			for (int i = 0; i < XO_CAPACITY; i++)
			{
				displays[i]->visible = !showGate;
				gates[i]->visible = showGate;
			}
			// The mask only needs to hide the panel's own printed decoration while a lit/unlit
			// gate square occupies that space instead of a number - showing it for the
			// continuous/value case would hide decoration that should stay visible there.
			for (int col = 0; col < 2; col++)
				buttonCovers[col]->visible = showGate;
		}
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		XOD16 *module;
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

		XOD16 *module = dynamic_cast<XOD16 *>(this->module);
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

Model *modelXOD16 = createModel<XOD16, XOD16Widget>("XOD16");
