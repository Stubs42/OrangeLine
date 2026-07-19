/*
	X16.cpp

	Code for the OrangeLine module X16

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

#include "X16.hpp"
#include "X8Common.hpp"

#define X16_PANEL_WIDTH_MM 30.48f

struct X16 : Module, XExpanderInterface
{

#include "OrangeLineCommon.hpp"
#include "X8ModuleCommon.hpp"

	X16()
	{
		initializeInstance();
		// One-time default, set here (not in moduleCustomInitialize(), which runs every tick,
		// not just once - see the comment on that hook below) so a saved patch's own value
		// (applied later by dataFromJson(), if the key exists) can still override it correctly.
		OL_state[CHANNEL_LIMIT_JSON] = (float) NUM_X8_KNOBS;
	}
};

/**
	Main Module Widget

	Panel shares X8D's exact header geometry (both are 6HP/30.48mm - verified directly against
	res/X16Work.svg's own Controls layer: identical ENGAGE/LEFT/RIGHT/name-display coordinates) -
	the only real difference from X8D is no per-channel display column; instead the freed-up
	space holds a second 8-knob column, channels 8-15, at the same 8 Y positions as column one,
	shifted right by exactly half the panel width (15.24mm - verified against the real knob
	group transforms in res/X16Work.svg: column two's translate x is column one's own -15.24mm).
*/
struct X16Widget : ModuleWidget
{
	// Sizes measured directly from res/X16Work.svg's Controls layer (BUTTON_FRAME path bounding
	// boxes) - identical to X8D's own, since both share the same 30.48mm-wide header. Nested
	// (rather than file-scope) per CLAUDE.md's ODR note for same-named sized subclasses.
	struct X8StepButton : X8ButtonBase { X8StepButton() { box.size = mm2px(Vec(12.192f, 4.572f)); } };
	struct X8BindButton : X8BindButtonBase { X8BindButton() { box.size = mm2px(Vec(25.908f, 5.588f)); } };

	XExtStripWidget *extStrip = nullptr;     // right edge - toward the Host (or a further Expander)
	XExtStripWidget *extStripLeft = nullptr; // left edge - toward a further chained Expander
	X8LogoCover *logoCover1 = nullptr;
	X8LogoCover *logoCover2 = nullptr;

	// One knob and one value-button share each of the 16 channel slots, same position, only one
	// ever visible - see step() below. Both stay fully constructed/bound to the same param the
	// whole time; morphing is purely a visibility (and therefore also hit-testing, per the
	// existing Wakeup/Ready port precedent - a hidden widget doesn't claim input either) switch.
	X8Knob *knobs[NUM_X8_KNOBS] = {};
	X8ValueButton *valueButtons[NUM_X8_KNOBS] = {};

	X16Widget(X16 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X16Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X16Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X16Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addOrangeLineConnectionLight<AutoHideLight<TinyLight<GreenRedLight>>>(this, calculateCoordinates(X16_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, CONN_LIGHT);

		// LEFT/RIGHT/ENGAGE positions/sizes measured directly from res/X16Work.svg's Controls
		// layer (BUTTON_FRAME path bounding boxes) - identical to X8D's own.
		X8StepButton *leftButton = createParamCentered<X8StepButton>(calculateCoordinates(8.382f, 18.035f, 0.f), module, LEFT_PARAM);
		leftButton->label = "<";
		addParam(leftButton);
		X8StepButton *rightButton = createParamCentered<X8StepButton>(calculateCoordinates(22.098f, 18.035f, 0.f), module, RIGHT_PARAM);
		rightButton->label = ">";
		addParam(rightButton);
		X8BindButton *bindButton = createParamCentered<X8BindButton>(calculateCoordinates(15.24f, 24.639f, 0.f), module, ENGAGE_PARAM);
		bindButton->label = "BIND";
		addParam(bindButton);

		X8NameDisplay *nameDisplay = new X8NameDisplay();
		nameDisplay->module = module;
		nameDisplay->box.pos = calculateCoordinates(1.41287f, 12.449f, 0.f);
		nameDisplay->box.size = mm2px(Vec(X16_PANEL_WIDTH_MM - 2.f * X8_NAME_DISPLAY_MARGIN_MM, 5.f));
		addChild(nameDisplay);

		// 8 rows, top (channel 1/9) to bottom (channel 8/16) - same Y positions as X8/X8D's own
		// single column, matching the real knob group positions in res/X16Work.svg directly.
		static const float knobY[8] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		// Two columns, 8 channels each - column two sits exactly 15.24mm (half the panel width)
		// to the right of column one, same Y positions, per res/X16Work.svg's own knob9..knob16
		// transforms (each exactly -15.24mm further in x than its knob1..knob8 counterpart).
		static const float columnX[2] = { 7.62f, 22.86f };
		for (int col = 0; col < 2; col++)
		{
			for (int row = 0; row < 8; row++)
			{
				int channel = col * 8 + row;
				X8Knob *knob = createParamCentered<X8Knob>(calculateCoordinates(columnX[col], knobY[row], 0.f), module, KNOB_PARAM + channel);
				knob->channel = channel;
				addParam(knob);
				knobs[channel] = knob;

				X8ValueButton *button = createParamCentered<X8ValueButton>(calculateCoordinates(columnX[col], knobY[row], 0.f), module, KNOB_PARAM + channel);
				button->channel = channel;
				button->visible = false; // default: knob shown (continuous) until step() knows better
				addParam(button);
				valueButtons[channel] = button;
			}
		}

		extStrip = addXExtStrip(this, X16_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXLogoCovers(this, X16_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		X16 *x16Module = dynamic_cast<X16 *>(module);
		if (x16Module)
		{
			updateXExtStrip(extStrip, x16Module, x16Module->rightExpander.module);
			updateXExtStripLeft(extStripLeft, x16Module, x16Module->leftExpander.module);
			updateXLogoCovers(logoCover1, logoCover2, x16Module);
			// Type-based morph (ExpanderParamAccessSpec.md's "Type-based appearance"): continuous
			// -> knob, toggle/click/push -> button. One type governs all 16 channels at once,
			// since it's a property of the browsed param, not of any individual channel.
			bool showButton = x16Module->getXBrowsedParamType() != X_PARAM_CONTINUOUS;
			for (int i = 0; i < NUM_X8_KNOBS; i++)
			{
				knobs[i]->visible = !showButton;
				valueButtons[i]->visible = showButton;
			}
		}
		ModuleWidget::step();
	}

	// Channel count is Expander-owned (the "sender" decides, see moduleCustomInitialize() in
	// the module struct above) - same radio-submenu pattern as Morpheus's own "Poly Channels".
	struct X8ChannelsItem : MenuItem
	{
		X16 *module;

		struct X8ChannelItem : MenuItem
		{
			X16 *module;
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
		X16 *module;
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

		X16 *module = dynamic_cast<X16 *>(this->module);
		assert(module);

		X8ChannelsItem *channelsItem = new X8ChannelsItem();
		channelsItem->module = module;
		channelsItem->text = "Channels";
		channelsItem->rightText = RIGHT_ARROW;
		menu->addChild(channelsItem);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MenuLabel *expandersLabel = new MenuLabel();
		expandersLabel->text = "Expanders";
		menu->addChild(expandersLabel);

		addXBindMenuItems(menu, module);

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

Model *modelX16 = createModel<X16, X16Widget>("X16");
