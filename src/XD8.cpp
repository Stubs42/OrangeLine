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

struct XD8 : Module, XOExpanderInterface, ExpanderBridgeInterface
{

#include "OrangeLineCommon.hpp"
#include "XOModuleCommon.hpp"

	XD8()
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
	Themed background mask matching the exact outline Dieter authored in res/XD8Work.svg's own
	"MASK" layer - a jagged, hand-drawn path (dodging the per-row channel-number labels rather
	than a plain rect covering everything, which is what made the earlier rect-based
	XOButtonCover approach here complex/wrong) - replicated as literal NanoVG path commands
	(absolute panel-mm coordinates, converted via mm2px() same as every other custom-drawn widget
	in this codebase), NOT loaded as a separate SVG asset, so the per-theme fill still comes from
	the SAME X_STRIP_BG_* runtime constants every other cover already uses. The color baked into
	the guide path in Work.svg itself is only Dieter's own Inkscape visual marker - never read
	here. Spans the whole 8-row column in one shape, always visible (same "never toggled"
	reasoning as the previous per-row XOButtonCover instances this replaces - both the value
	display and the gate indicator draw their own content on top of it).
*/
struct XD8DisplayMask : TransparentWidget
{
	Module *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;
		float style = expander ? expander->getXOStyle() : STYLE_ORANGE;
		NVGcolor fill = (style == STYLE_DARK) ? X_STRIP_BG_DARK
		              : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT
		              : X_STRIP_BG_ORANGE;

		auto P = [](float xmm, float ymm) { return mm2px(Vec(xmm, ymm)); };
		nvgBeginPath(args.vg);
		Vec p = P(0.9103849f, 35.036577f);
		nvgMoveTo(args.vg, p.x, p.y);
		p = P(0.9103849f, 40.116577f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 40.116577f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 45.958577f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103853f, 45.958577f); nvgLineTo(args.vg, p.x, p.y);
		p = P(1.0013353f, 51.038578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 51.038578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 56.880578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103853f, 56.880578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103849f, 62.214578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 62.214578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 67.802578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103853f, 67.802578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103849f, 73.136578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 73.136578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 78.724578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103853f, 78.724578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103849f, 84.058578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 84.058578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 89.646578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103849f, 89.646578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103849f, 94.980578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 94.980578f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 100.822580f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103853f, 100.822580f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103849f, 105.902580f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 105.902580f); nvgLineTo(args.vg, p.x, p.y);
		p = P(3.4503849f, 111.744580f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103853f, 111.744580f); nvgLineTo(args.vg, p.x, p.y);
		p = P(0.9103849f, 116.824580f); nvgLineTo(args.vg, p.x, p.y);
		p = P(14.372385f, 116.824580f); nvgLineTo(args.vg, p.x, p.y);
		p = P(14.372385f, 35.036577f); nvgLineTo(args.vg, p.x, p.y);
		{
			Vec c1 = P(10.059018f, 35.037352f);
			Vec c2 = P(5.2406038f, 35.033377f);
			Vec e  = P(0.9103849f, 35.036577f);
			nvgBezierTo(args.vg, c1.x, c1.y, c2.x, c2.y, e.x, e.y);
		}
		nvgClosePath(args.vg);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
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
	XD8DisplayMask *displayMask = nullptr;

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

		// Moved down to the open gap between the LEFT/RIGHT nav buttons and the first display
		// cell - the old top-right corner position collided with the name display's own text on
		// this narrow panel and read as confusing next to the (separately, already-removed) old
		// connection light. X centered on the panel's own half-width (matches XO16/XD16/XOD8/
		// XOD16's own convention - this file had a stale hardcoded 6.72mm instead, visibly
		// off-center, Dieter's own catch 2026-07-21). Two-channel GreenRedLight (green = connected
		// and every channel fits, red = overflow, both off = not connected) - see
		// XOModuleCommon.hpp's own moduleProcess() comment.
		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(XD8_PANEL_WIDTH_MM / 2.f, 26.163f, 0.f), module, OVERFLOW_LIGHT));

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
		// Whole-column background, replacing the earlier per-row plain-rect XOButtonCover
		// instances - see XD8DisplayMask's own comment above for why (the real panel decoration
		// includes per-row channel-number labels a plain rect can't dodge without also hiding
		// them). Added before the per-row loop below so it draws underneath every value display /
		// gate indicator, same z-order the old per-row covers used. Only shown in button/gate mode
		// (toggled in step(), same showGate flag as the gate indicators themselves) - it hides
		// decoration meant only to disappear behind the gate cap, not the value display, which
		// needs the real panel showing through underneath it.
		displayMask = new XD8DisplayMask();
		displayMask->module = module;
		displayMask->box.pos = calculateCoordinates(0.f, 0.f, 0.f);
		displayMask->box.size = mm2px(Vec(XD8_PANEL_WIDTH_MM, PANELHEIGHT));
		displayMask->visible = false;
		addChild(displayMask);

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
			displayMask->visible = showGate;
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
