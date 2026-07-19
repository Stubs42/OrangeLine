/*
	XOD8.cpp

	Code for the OrangeLine module XOD8

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

// XOD8 reuses XO8's own enums verbatim (identical shape - see XOModuleCommon.hpp's own comment
// on why the family shares this much) rather than declaring a separate XOD8.hpp.
#include "XO8.hpp"
#include "XOCommon.hpp"

#define XOD8_PANEL_WIDTH_MM 30.48f

struct XOD8 : Module, XOExpanderInterface
{

#include "OrangeLineCommon.hpp"
#include "XOModuleCommon.hpp"

	XOD8()
	{
		initializeInstance();
	}
};

/**
	Main Module Widget

	Panel is XO8's own layout doubled in width (6HP vs 3HP), same "doubling" convention as X8D's
	own relationship to X8 - the jack column stays at the exact same x/y as XO8's own, with the
	newly available space to the right of each jack holding the per-channel value/gate display.
*/
struct XOD8Widget : ModuleWidget
{
	struct XOStepButton : XOButtonBase { XOStepButton() { box.size = mm2px(Vec(12.192f, 4.572f)); } };

	XExtStripWidget *extStrip = nullptr;
	XExtStripWidget *extStripLeft = nullptr;
	XOLogoCover *logoCover1 = nullptr;
	XOLogoCover *logoCover2 = nullptr;

	PJ301MPort *ports[XO_CAPACITY] = {};
	XOValueDisplay *displays[XO_CAPACITY] = {};
	XOGateIndicator *gates[XO_CAPACITY] = {};
	XOButtonCover *buttonCover = nullptr;

	XOD8Widget(XOD8 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XOD8Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XOD8Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/XOD8Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addOrangeLineConnectionLight<AutoHideLight<TinyLight<GreenRedLight>>>(this, calculateCoordinates(3.5f, 4.f, 0.f), module, CONN_LIGHT);
		addChild(createLightCentered<AutoHideLight<TinyLight<RedLight>>>(calculateCoordinates(XOD8_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, OVERFLOW_LIGHT));

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

		// Always-visible display-column background (the panel's own static decoration there has
		// been removed entirely - see XD8Widget's own per-row cover for the same reasoning) -
		// covers the whole column at once, geometry taken directly from Dieter's own MASK guide
		// rect in res/XOD8Work.svg. Added BEFORE the per-channel gate indicators/displays below so
		// it draws underneath them (addChild order is also draw order).
		buttonCover = new XOButtonCover();
		buttonCover->module = module;
		buttonCover->box.pos = calculateCoordinates(12.342694f, 32.76725f, 0.f);
		buttonCover->box.size = mm2px(Vec(16.867304f, 85.090004f));
		addChild(buttonCover);

		static const float portY[XO_CAPACITY] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		// Per-channel display cell position - same offset from the jack column as X8D's own
		// per-channel value display (res/X8DWork.svg's own "DISPLAY"-labeled guide texts).
		static const float displayY[XO_CAPACITY] = {
			38.873039f, 49.784458f, 60.717041f, 71.639038f,
			82.815041f, 93.646629f, 104.65906f, 115.58104f
		};
		// The gate indicator's own box, by contrast, needs to match the REAL decorative display
		// cell's own bounding box (measured directly from res/X8DWork.svg's own "rect5-*"
		// elements: x=14.478, width=13.716, height=4.572, one row top per channel below) - not
		// XOValueDisplay's baseline-anchored text position above, which has different geometry
		// (box.size there barely matters beyond text width/scissor clipping, whereas the gate
		// indicator paints an actual themed background frame that must land exactly on the real
		// panel decoration it's covering).
		static const float gateBoxY[XO_CAPACITY] = {
			35.0532f, 45.9647f, 56.8973f, 67.8193f,
			78.9953f, 89.8268f, 100.8393f, 111.7612f
		};
		for (int i = 0; i < XO_CAPACITY; i++)
		{
			// Plain jack, no accent ring - these outputs are plain poly outputs, not something a
			// further X-family Expander can browse/color-match (same reasoning as XR8/XR16).
			PJ301MPort *port = createOutputCentered<PJ301MPort>(calculateCoordinates(7.62f, portY[i], 0.f), module, CHANNEL_OUTPUT + i);
			addOutput(port);
			ports[i] = port;

			XOValueDisplay *display = new XOValueDisplay();
			display->module = module;
			display->channel = i;
			display->box.pos = calculateCoordinates(15.12887f, displayY[i], 0.f);
			display->box.size = mm2px(Vec(15.f, 5.f));
			addChild(display);
			displays[i] = display;

			XOGateIndicator *gate = new XOGateIndicator();
			gate->module = module;
			gate->channel = i;
			// Center on the row's own center point (14.478 + 13.716/2, gateBoxY[i] + 4.572/2) -
			// box.size is now the cap's own natural (larger) size, set by the constructor, not the
			// panel rect's own smaller size, so this can't be positioned by the rect's top-left
			// corner anymore (see XOGateIndicator's own comment on this).
			gate->box.pos = calculateCoordinates(21.336f, gateBoxY[i] + 2.286f, 0.f).minus(gate->box.size.div(2.f));
			gate->visible = false;
			addChild(gate);
			gates[i] = gate;
		}

		extStrip = addXExtStrip(this, XOD8_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXOLogoCovers(this, XOD8_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		addOrangeLineTouchPorts(this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		XOD8 *xod8Module = dynamic_cast<XOD8 *>(module);
		if (xod8Module)
		{
			updateXOExtStrip(extStrip, xod8Module, xod8Module->rightExpander.module);
			updateXOExtStripLeft(extStripLeft, xod8Module, xod8Module->leftExpander.module);
			updateXOLogoCovers(logoCover1, logoCover2, xod8Module);
			// Type-based morph, mirrors X8Widget's own knob/button morph: continuous -> numeric
			// value display, gate -> lit/unlit square. One type governs every channel at once,
			// since it's a property of the browsed output slot, not of any individual channel.
			bool showGate = xod8Module->getXOBrowsedType() == XO_TYPE_GATE;
			for (int i = 0; i < XO_CAPACITY; i++)
			{
				displays[i]->visible = !showGate;
				gates[i]->visible = showGate;
			}
			// The mask only needs to hide the panel's own printed decoration while a lit/unlit
			// gate square occupies that space instead of a number - showing it for the
			// continuous/value case would hide decoration that should stay visible there.
			buttonCover->visible = showGate;
		}
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		XOD8 *module;
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

		XOD8 *module = dynamic_cast<XOD8 *>(this->module);
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

Model *modelXOD8 = createModel<XOD8, XOD8Widget>("XOD8");
