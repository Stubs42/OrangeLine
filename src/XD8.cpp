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
	XOButtonCover *covers[XO_CAPACITY] = {};

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

		addOrangeLineConnectionLight<AutoHideLight<TinyLight<GreenRedLight>>>(this, calculateCoordinates(3.5f, 4.f, 0.f), module, CONN_LIGHT);
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
		// unlike XOD8's own wider per-channel display which sits beside a jack). Baseline Y
		// values are NOT the same as the knob/jack row Y above - text baseline sits a bit below
		// each row's own nominal center, exactly like XOD8Widget's own displayY (reused verbatim
		// here, since both share the same per-row hand-measured baseline positions).
		static const float displayY[XO_CAPACITY] = {
			38.873039f, 49.784458f, 60.717041f, 71.639038f,
			82.815041f, 93.646629f, 104.65906f, 115.58104f
		};
		// The gate indicator's own box needs to match the REAL decorative display cell's own
		// bounding box (measured directly from res/XD8Work.svg's own plain "rect1*" elements:
		// x=0.762, width=13.716, height=4.064 - not XOValueDisplay's baseline-anchored text
		// position above, see XOD8Widget's own comment on this same distinction).
		static const float gateBoxY[XO_CAPACITY] = {
			35.3073f, 46.2655f, 57.2238f, 68.1821f,
			79.1404f, 90.0987f, 101.057f, 112.0153f
		};
		for (int i = 0; i < XO_CAPACITY; i++)
		{
			// Always-visible per-row background, sized to fully cover the real panel decoration
			// underneath (res/XD8Work.svg's own plain "rect1*" elements: x=0.762, width=13.716,
			// height=4.064) - unlike XOButtonCover elsewhere in this family (which only shows while
			// a gate/button type is browsed), this one is never toggled: XD8's own display cell
			// spans nearly the whole narrow 15.24mm module, too wide for the gate cap's own fixed
			// 9.144mm frame to mask on its own, so the row is covered unconditionally and both the
			// value display and the gate indicator draw their own content on top of it.
			// Inset 0.15mm on all four sides (13.716x4.064mm -> 13.416x3.764mm) so this opaque
			// cover doesn't paint over the module's own outer frame artwork/decoration lines, which
			// run right up against the rect1 cell's own edges on this narrow (15.24mm) panel.
			XOButtonCover *cover = new XOButtonCover();
			cover->module = module;
			cover->box.pos = calculateCoordinates(0.762f + 0.15f, gateBoxY[i] + 0.15f, 0.f);
			cover->box.size = mm2px(Vec(13.716f - 0.3f, 4.064f - 0.3f));
			addChild(cover);
			covers[i] = cover;

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
			// Center on the row's own center point (0.762 + 13.716/2, gateBoxY[i] + 4.064/2) -
			// box.size is now the cap's own natural (larger) size, set by the constructor, not the
			// panel rect's own smaller size, so this can't be positioned by the rect's top-left
			// corner anymore (see XOGateIndicator's own comment on this).
			gate->box.pos = calculateCoordinates(7.62f, gateBoxY[i] + 2.032f, 0.f).minus(gate->box.size.div(2.f));
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

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		addXODisconnectMenuItem(menu, module);
	}
};

Model *modelXD8 = createModel<XD8, XD8Widget>("XD8");
