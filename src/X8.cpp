/*
	X8.cpp

	Code for the OrangeLine module X8

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

#include "X8.hpp"
#include "X8Common.hpp"

#define X8_PANEL_WIDTH_MM 15.24f

struct X8 : Module, XExpanderInterface, ExpanderBridgeInterface
{

#include "OrangeLineCommon.hpp"
#include "X8ModuleCommon.hpp"

	X8()
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
*/
struct X8Widget : ModuleWidget
{
	// BIND/LEFT/RIGHT sizes are the only thing that actually differs from X8D here - everything
	// else (X8ButtonBase/X8BindButtonBase's own logic) is shared, see X8Common.hpp. Nested
	// (rather than file-scope) so a same-named-but-differently-sized X8D counterpart in X8D.cpp's
	// own X8DWidget can't ever collide with this one across translation units - sizes measured
	// from the actual button-frame path's bounding box (including its rounded corners), not just
	// the straight-edge segment lengths - an earlier version used the latter and came out
	// visibly too small.
	struct X8StepButton : X8ButtonBase { X8StepButton() { box.size = mm2px(Vec(4.6f, 4.6f)); } };
	struct X8BindButton : X8BindButtonBase { X8BindButton() { box.size = mm2px(Vec(10.69f, 5.61f)); } };

	XExtStripWidget *extStrip = nullptr;     // right edge - toward the Host (or a further Expander)
	XExtStripWidget *extStripLeft = nullptr; // left edge - toward a further chained Expander
	X8LogoCover *logoCover1 = nullptr;
	X8LogoCover *logoCover2 = nullptr;

	// One knob and one value-button share each of the 8 channel slots, same position, only one
	// ever visible - see step() below. Both stay fully constructed/bound to the same param the
	// whole time; morphing is purely a visibility (and therefore also hit-testing, per the
	// existing Wakeup/Ready port precedent - a hidden widget doesn't claim input either) switch.
	X8Knob *knobs[NUM_X8_KNOBS] = {};
	X8ValueButton *valueButtons[NUM_X8_KNOBS] = {};

	X8Widget(X8 *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8Orange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8Bright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8Dark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		X8StepButton *leftButton = createParamCentered<X8StepButton>(calculateCoordinates(4.550f, 18.034f, 0.f), module, LEFT_PARAM);
		leftButton->label = "<";
		addParam(leftButton);
		X8StepButton *rightButton = createParamCentered<X8StepButton>(calculateCoordinates(10.657f, 18.035f, 0.f), module, RIGHT_PARAM);
		rightButton->label = ">";
		addParam(rightButton);
		X8BindButton *bindButton = createParamCentered<X8BindButton>(calculateCoordinates(7.609f, 24.629f, 0.f), module, ENGAGE_PARAM);
		bindButton->label = "BIND";
		addParam(bindButton);

		X8NameDisplay *nameDisplay = new X8NameDisplay();
		nameDisplay->module = module;
		nameDisplay->box.pos = calculateCoordinates(1.41287f, 12.449f, 0.f);
		nameDisplay->box.size = mm2px(Vec(13.f, 5.f));
		addChild(nameDisplay);

		// 8 channel knobs, top (channel 1) to bottom (channel 8) - matches the panel's own
		// "1".."8" labels, which run top-to-bottom while the underlying knobring elements in the
		// SVG are numbered bottom-to-top (knobring8 is physically at the top).
		static const float knobY[NUM_X8_KNOBS] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		for (int i = 0; i < NUM_X8_KNOBS; i++)
		{
			X8Knob *knob = createParamCentered<X8Knob>(calculateCoordinates(7.62f, knobY[i], 0.f), module, KNOB_PARAM + i);
			knob->channel = i;
			addParam(knob);
			knobs[i] = knob;

			X8ValueButton *button = createParamCentered<X8ValueButton>(calculateCoordinates(7.62f, knobY[i] , 0.f), module, KNOB_PARAM + i);
			button->channel = i;
			button->visible = false; // default: knob shown (continuous) until step() knows better
			addParam(button);
			valueButtons[i] = button;
		}

		extStrip = addXExtStrip(this, X8_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXLogoCovers(this, X8_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		X8 *x8Module = dynamic_cast<X8 *>(module);
		if (x8Module)
		{
			updateXExtStrip(extStrip, x8Module, x8Module->rightExpander.module);
			updateXExtStripLeft(extStripLeft, x8Module, x8Module->leftExpander.module);
			updateXLogoCovers(logoCover1, logoCover2, x8Module);
			// Type-based morph (ExpanderParamAccessSpec.md's "Type-based appearance"): continuous
			// -> knob, toggle/click/push -> button. One type governs all 8 channels at once,
			// since it's a property of the browsed param, not of any individual channel.
			bool showButton = x8Module->getXBrowsedParamType() != X_PARAM_CONTINUOUS;
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
		X8 *module;

		struct X8ChannelItem : MenuItem
		{
			X8 *module;
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
		X8 *module;
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

		X8 *module = dynamic_cast<X8 *>(this->module);
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

Model *modelX8 = createModel<X8, X8Widget>("X8");
