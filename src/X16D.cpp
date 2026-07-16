/*
	X16D.cpp

	Code for the OrangeLine module X16D

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

// X16D reuses X16's own enums verbatim (NUM_X8_KNOBS=16, identical shape otherwise - see
// X8ModuleCommon.hpp's own comment on why the family shares this much) rather than declaring a
// separate X16D.hpp.
#include "X16.hpp"
#include "X8Common.hpp"

#define X16D_PANEL_WIDTH_MM 60.96f

struct X16D : Module, XExpanderInterface
{

#include "OrangeLineCommon.hpp"
#include "X8ModuleCommon.hpp"

	X16D()
	{
		initializeInstance();
		// One-time default, set here (not in moduleCustomInitialize(), which runs every tick,
		// not just once - see the comment on that hook below) so a saved patch's own value
		// (applied later by dataFromJson(), if the key exists) can still override it correctly.
		OL_state[CHANNEL_LIMIT_JSON] = (float) NUM_X8_KNOBS;

		// lockedHostType is a plain string, not a float - OL_state's JSON array can't carry it,
		// so it uses the same moduleExtraDataToJson/FromJson hook CC2CV/CV2CC use for their own
		// non-float persisted data (see CLAUDE.md's ODR-safety note on this pattern).
		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_object_set_new(rootJ, "lockedHostType", json_string(lockedHostType.c_str()));
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *lockedHostTypeJ = json_object_get(rootJ, "lockedHostType");
			if (lockedHostTypeJ && json_is_string(lockedHostTypeJ))
				lockedHostType = json_string_value(lockedHostTypeJ);
		};
	}
};

/**
	Covers one knob+display column's worth of display area - see X16DWidget's own comment for why
	there are two of these (one per column). Same shape as X8D's own single "ButtonCover" rect,
	just placed twice, 30.48mm (half the panel width) apart - X16D is, geometrically, two X8D
	knob/display columns side by side under one shared, doubled-again header.
*/
struct X16DButtonCover : TransparentWidget
{
	Module *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;
		float style = expander ? expander->getXStyle() : STYLE_ORANGE;
		NVGcolor fill = (style == STYLE_DARK) ? nvgRGB(0x20, 0x20, 0x20)
		              : (style == STYLE_BRIGHT) ? nvgRGB(0xe6, 0xe6, 0xe6)
		              : nvgRGB(0x15, 0x15, 0x2b); // STYLE_ORANGE
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
	}
};

/**
	Per-channel numeric readout - identical logic to X8D's own X8DValueDisplay (see its comment),
	just under a family-neutral name since X16D needs its own instances too (16 of them, two per
	row across both columns).
*/
struct X16DValueDisplay : TransparentWidget
{
	Module *module = nullptr;
	int channel = 0;

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;
		if (!expander)
			return;

		char raw[6];
		snprintf(raw, sizeof(raw), "%s", expander->formatXValue(expander->getXKnobValue(channel)).c_str());
		if (raw[0] == '\0')
			return;

		XAlign align = expander->getXBrowsedParamAlign();
		char buffer[6];
		float x = 0.f;
		if (align == X_ALIGN_RIGHT)
			snprintf(buffer, sizeof(buffer), "%5s", raw);
		else
		{
			snprintf(buffer, sizeof(buffer), "%s", raw);
			if (align == X_ALIGN_CENTER)
				x = mm2px(X8_CENTER_OFFSET_MM[strlen(buffer)]);
		}

		float fontSizePx = mm2px(Vec(4.49792f, 0.f)).x;
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSizePx);
		nvgFillColor(args.vg, expander->getXBrowsedParamColor());
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, -fontSizePx * 1.2f, box.size.x, fontSizePx * 1.6f);
		nvgText(args.vg, x, 0.f, buffer, nullptr);
		nvgRestore(args.vg);
		Widget::drawLayer(args, 1);
	}
};

/**
	Main Module Widget

	12HP/60.96mm - X8D's own header widened again (LEFT/RIGHT/ENGAGE all measured directly from
	res/X16DWork.svg's Controls layer) plus a second knob+display column, exactly 30.48mm (half
	the panel width) to the right of the first - geometrically, two complete X8D columns side by
	side under one shared header, matching "X8D's width doubled again" from
	ExpanderParamAccessSpec.md's four-panel-variant table.
*/
struct X16DWidget : ModuleWidget
{
	// Sizes measured directly from res/X16DWork.svg's Controls layer (BUTTON_FRAME path bounding
	// boxes) - wider again than X8D's own, since the header spans the full 60.96mm now. Nested
	// per CLAUDE.md's ODR note for same-named sized subclasses.
	struct X8StepButton : X8ButtonBase { X8StepButton() { box.size = mm2px(Vec(27.432f, 4.572f)); } };
	struct X8EngageButton : X8EngageButtonBase { X8EngageButton() { box.size = mm2px(Vec(56.408f, 5.588f)); } };

	XExtStripWidget *extStrip = nullptr;     // right edge - toward the Host (or a further Expander)
	XExtStripWidget *extStripLeft = nullptr; // left edge - toward a further chained Expander
	X8LogoCover *logoCover1 = nullptr;
	X8LogoCover *logoCover2 = nullptr;

	X8Knob *knobs[NUM_X8_KNOBS] = {};
	X8ValueButton *valueButtons[NUM_X8_KNOBS] = {};
	X16DButtonCover *buttonCovers[2] = {};
	X16DValueDisplay *displays[NUM_X8_KNOBS] = {};

	X16DWidget(X16D *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X16DOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X16DBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X16DDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(X16D_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, CONN_LIGHT));

		// LEFT/RIGHT/ENGAGE positions/sizes measured directly from res/X16DWork.svg's Controls
		// layer (BUTTON_FRAME path bounding boxes).
		X8StepButton *leftButton = createParamCentered<X8StepButton>(calculateCoordinates(16.002f, 18.035f, 0.f), module, LEFT_PARAM);
		leftButton->label = "<";
		addParam(leftButton);
		X8StepButton *rightButton = createParamCentered<X8StepButton>(calculateCoordinates(44.958f, 18.035f, 0.f), module, RIGHT_PARAM);
		rightButton->label = ">";
		addParam(rightButton);
		X8EngageButton *engageButton = createParamCentered<X8EngageButton>(calculateCoordinates(30.48f, 24.639f, 0.f), module, ENGAGE_PARAM);
		engageButton->label = "ENGAGE";
		addParam(engageButton);

		X8NameDisplay *nameDisplay = new X8NameDisplay();
		nameDisplay->module = module;
		nameDisplay->box.pos = calculateCoordinates(1.41287f, 12.449f, 0.f);
		nameDisplay->box.size = mm2px(Vec(13.f, 5.f));
		addChild(nameDisplay);

		// 8 rows, same Y positions as every other family member's own knob column.
		static const float knobY[8] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		// Per-channel numeric display Y positions - measured directly from res/X16DWork.svg's own
		// "DISPLAY"-labeled guide texts (close to but not exactly knobY - each row's baseline sits
		// its own slightly different distance below its own knob's y, per the real measurements).
		static const float displayY[8] = {
			38.873039f, 49.828312f, 60.78289f, 71.737473f,
			82.692047f, 93.646629f, 104.60121f, 115.55579f
		};
		// Two knob+display columns, 8 channels each - column two sits exactly 30.48mm (half the
		// panel width) to the right of column one, per res/X16DWork.svg's own knob9../DISPLAY "b"
		// transforms/coordinates (each exactly +30.48mm past its column-one counterpart).
		static const float knobColumnX[2] = { 7.62f, 38.1f };
		static const float displayColumnX[2] = { 15.12887f, 45.608871f };
		static const float buttonCoverX[2] = { 12.342694f, 42.822694f };

		for (int col = 0; col < 2; col++)
		{
			for (int row = 0; row < 8; row++)
			{
				int channel = col * 8 + row;
				X8Knob *knob = createParamCentered<X8Knob>(calculateCoordinates(knobColumnX[col], knobY[row], 0.f), module, KNOB_PARAM + channel);
				knob->channel = channel;
				addParam(knob);
				knobs[channel] = knob;

				X8ValueButton *button = createParamCentered<X8ValueButton>(calculateCoordinates(knobColumnX[col], knobY[row], 0.f), module, KNOB_PARAM + channel);
				button->channel = channel;
				button->visible = false; // default: knob shown (continuous) until step() knows better
				addParam(button);
				valueButtons[channel] = button;

				X16DValueDisplay *display = new X16DValueDisplay();
				display->module = module;
				display->channel = channel;
				display->box.pos = calculateCoordinates(displayColumnX[col], displayY[row], 0.f);
				display->box.size = mm2px(Vec(15.f, 5.f));
				addChild(display);
				displays[channel] = display;
			}

			// Same shape as X8D's own single "ButtonCover" rect (see X16DButtonCover's own
			// comment), one per column.
			X16DButtonCover *cover = new X16DButtonCover();
			cover->module = module;
			cover->box.pos = calculateCoordinates(buttonCoverX[col], 32.76725f, 0.f);
			cover->box.size = mm2px(Vec(16.867304f, 85.090004f));
			cover->visible = false;
			addChild(cover);
			buttonCovers[col] = cover;
		}

		extStrip = addXExtStrip(this, X16D_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXLogoCovers(this, X16D_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		X16D *x16dModule = dynamic_cast<X16D *>(module);
		if (x16dModule)
		{
			updateXExtStrip(extStrip, x16dModule, x16dModule->rightExpander.module);
			updateXExtStripLeft(extStripLeft, x16dModule, x16dModule->leftExpander.module);
			updateXLogoCovers(logoCover1, logoCover2, x16dModule);
			// Type-based morph (ExpanderParamAccessSpec.md's "Type-based appearance"): continuous
			// -> knob, toggle/click/push -> button. One type governs all 16 channels at once,
			// since it's a property of the browsed param, not of any individual channel.
			bool showButton = x16dModule->getXBrowsedParamType() != X_PARAM_CONTINUOUS;
			for (int i = 0; i < NUM_X8_KNOBS; i++)
			{
				knobs[i]->visible = !showButton;
				valueButtons[i]->visible = showButton;
				displays[i]->visible = !showButton;
			}
			buttonCovers[0]->visible = showButton;
			buttonCovers[1]->visible = showButton;
		}
		ModuleWidget::step();
	}

	// Channel count is Expander-owned (the "sender" decides, see moduleCustomInitialize() in
	// the module struct above) - same radio-submenu pattern as Morpheus's own "Poly Channels".
	struct X8ChannelsItem : MenuItem
	{
		X16D *module;

		struct X8ChannelItem : MenuItem
		{
			X16D *module;
			int channels;
			void onAction(const event::Action &e) override
			{
				module->OL_setOutState(CHANNEL_LIMIT_JSON, float(channels));
			}
			void step() override
			{
				if (module)
					rightText = (module->OL_state[CHANNEL_LIMIT_JSON] == channels) ? "✔" : "";
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			for (int channel = 1; channel <= NUM_X8_KNOBS; channel++)
			{
				X8ChannelItem *item = new X8ChannelItem();
				item->module = module;
				item->channels = channel;
				item->text = module->channelNumbers[channel - 1];
				item->setSize(Vec(50, 20));
				menu->addChild(item);
			}
			return menu;
		}
	};

	struct X8StyleItem : MenuItem
	{
		X16D *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		X16D *module = dynamic_cast<X16D *>(this->module);
		assert(module);

		X8ChannelsItem *channelsItem = new X8ChannelsItem();
		channelsItem->module = module;
		channelsItem->text = "Channels";
		channelsItem->rightText = RIGHT_ARROW;
		menu->addChild(channelsItem);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		X8StyleItem *style1Item = new X8StyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		X8StyleItem *style2Item = new X8StyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		X8StyleItem *style3Item = new X8StyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelX16D = createModel<X16D, X16DWidget>("X16D");
