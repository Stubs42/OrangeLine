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

struct XD16 : Module, XOExpanderInterface, ExpanderBridgeInterface
{

#include "OrangeLineCommon.hpp"
#include "XOModuleCommon.hpp"

	XD16()
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
	Themed background mask matching the exact outline Dieter authored in res/XD16Work.svg's own
	"MASK" layer - one jagged, hand-drawn path per column (left and right are NOT a pure mirror of
	each other - close, but with slightly different per-segment offsets - so both are replicated
	verbatim rather than sharing a flipped transform). Same reasoning/mechanism as XD8's own
	XD8DisplayMask (XD8.cpp) - literal NanoVG path commands in absolute panel-mm coordinates
	(mm2px()-converted), filled with the runtime X_STRIP_BG_* constants, not a loaded SVG asset;
	the guide path's own color in Work.svg is only Dieter's Inkscape visual marker. Each spans its
	whole 8-row column in one shape, always visible, drawn before the per-row loop so both the
	value displays and gate indicators land on top of it.
*/
struct XD16DisplayMask : TransparentWidget
{
	Module *module = nullptr;
	bool leftColumn = true;

	void draw(const DrawArgs &args) override
	{
		XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;
		float style = expander ? expander->getXOStyle() : STYLE_ORANGE;
		NVGcolor fill = (style == STYLE_DARK) ? X_STRIP_BG_DARK
		              : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT
		              : X_STRIP_BG_ORANGE;

		auto P = [](float xmm, float ymm) { return mm2px(Vec(xmm, ymm)); };
		nvgBeginPath(args.vg);
		Vec p;
		if (leftColumn)
		{
			p = P(1.27f, 34.79925f); nvgMoveTo(args.vg, p.x, p.y);
			p = P(1.27f, 39.87925f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 39.87925f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 45.72125f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.2700004f, 45.72125f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.3609504f, 50.801251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 50.801251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 56.643251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.2700004f, 56.643251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.27f, 61.977251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 61.977251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 67.565251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.2700004f, 67.565251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.27f, 72.899251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 72.899251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 78.487251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.2700004f, 78.487251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.27f, 83.821251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 83.821251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 89.409251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.27f, 89.409251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.27f, 94.743251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 94.743251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 100.585250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.2700004f, 100.585250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.27f, 105.665250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 105.665250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(3.81f, 111.507250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.2700004f, 111.507250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(1.27f, 116.587250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(14.732f, 116.587250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(14.732f, 34.799250f); nvgLineTo(args.vg, p.x, p.y);
			{
				Vec c1 = P(10.418633f, 34.800025f);
				Vec c2 = P(5.6002189f, 34.79605f);
				Vec e  = P(1.27f, 34.79925f);
				nvgBezierTo(args.vg, c1.x, c1.y, c2.x, c2.y, e.x, e.y);
			}
		}
		else
		{
			p = P(29.21f, 34.79925f); nvgMoveTo(args.vg, p.x, p.y);
			p = P(29.21f, 39.87925f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 39.87925f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 45.72125f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 45.72125f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.11905f, 50.801251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 50.801251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 56.643251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 56.643251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 61.977251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 61.977251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 67.565251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 67.565251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 72.899251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 72.899251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 78.487251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 78.487251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 83.821251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 83.821251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 89.409251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 89.409251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 94.743251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 94.743251f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 100.585250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 100.585250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 105.665250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 105.665250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(26.67f, 111.507250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 111.507250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(29.21f, 116.587250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(15.748f, 116.587250f); nvgLineTo(args.vg, p.x, p.y);
			p = P(15.748f, 34.799250f); nvgLineTo(args.vg, p.x, p.y);
			{
				Vec c1 = P(20.061367f, 34.800025f);
				Vec c2 = P(24.879781f, 34.79605f);
				Vec e  = P(29.21f, 34.79925f);
				nvgBezierTo(args.vg, c1.x, c1.y, c2.x, c2.y, e.x, e.y);
			}
		}
		nvgClosePath(args.vg);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
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
	XD16DisplayMask *displayMaskLeft = nullptr;
	XD16DisplayMask *displayMaskRight = nullptr;

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

		// Moved down into the open gap between the LEFT/RIGHT nav buttons and the first row
		// (centered horizontally, same y as XO8/XD8's own identical repositioning) rather than
		// the old top-right corner - see XO8.cpp's own comment for the full reasoning.
		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(XD16_PANEL_WIDTH_MM / 2.f, 26.163f, 0.f), module, OVERFLOW_LIGHT));

		XOStepButton *leftButton = createParamCentered<XOStepButton>(calculateCoordinates(8.382f, 18.035f, 0.f), module, LEFT_PARAM);
		leftButton->label = "<";
		addParam(leftButton);
		XOStepButton *rightButton = createParamCentered<XOStepButton>(calculateCoordinates(22.098f, 18.035f, 0.f), module, RIGHT_PARAM);
		rightButton->label = ">";
		addParam(rightButton);

		// Width spans this module's own (double-width) panel, not the narrow 8-channel sibling's
		// fixed 13mm - it had been left at that stale narrow value (Dieter's own catch, 2026-07-21:
		// "the shortnames in the output modules are still not centered" - not a centering-math
		// bug, the box itself was too narrow and sat flush left, matching X16/X16D's own already-
		// correct PANEL_WIDTH_MM-based sizing).
		XONameDisplay *nameDisplay = new XONameDisplay();
		nameDisplay->module = module;
		nameDisplay->box.pos = calculateCoordinates(XO_NAME_DISPLAY_MARGIN_MM, 12.449f, 0.f);
		nameDisplay->box.size = mm2px(Vec(XD16_PANEL_WIDTH_MM - 2.f * XO_NAME_DISPLAY_MARGIN_MM, 5.f));
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
		// Dieter's own measured centers (2026-07-19) - left/right column button centers moved
		// horizontally off the original rect5-* cell center to their own explicit position,
		// independent of the mask's own (unchanged) geometry.
		static const float gateCenterX[2] = { 9.15f, 21.3f };
		// Always-visible whole-column backgrounds (one per column), replacing the earlier per-row
		// plain-rect XOButtonCover instances - see XD16DisplayMask's own comment above. Added
		// before the per-row loop below so both draw underneath every value display / gate
		// indicator, same z-order the old per-row covers used.
		// Only shown in button/gate mode (toggled in step(), same showGate flag as the gate
		// indicators themselves) - hides decoration meant only to disappear behind the gate cap,
		// not the value display, which needs the real panel showing through underneath it.
		displayMaskLeft = new XD16DisplayMask();
		displayMaskLeft->module = module;
		displayMaskLeft->leftColumn = true;
		displayMaskLeft->box.pos = calculateCoordinates(0.f, 0.f, 0.f);
		displayMaskLeft->box.size = mm2px(Vec(XD16_PANEL_WIDTH_MM, PANELHEIGHT));
		displayMaskLeft->visible = false;
		addChild(displayMaskLeft);
		displayMaskRight = new XD16DisplayMask();
		displayMaskRight->module = module;
		displayMaskRight->leftColumn = false;
		displayMaskRight->box.pos = calculateCoordinates(0.f, 0.f, 0.f);
		displayMaskRight->box.size = mm2px(Vec(XD16_PANEL_WIDTH_MM, PANELHEIGHT));
		displayMaskRight->visible = false;
		addChild(displayMaskRight);

		for (int col = 0; col < 2; col++)
		{
			for (int row = 0; row < 8; row++)
			{
				int channel = col * 8 + row;

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
			displayMaskLeft->visible = showGate;
			displayMaskRight->visible = showGate;
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
