/*
	X8D.cpp

	Code for the OrangeLine module X8D

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

// X8D reuses X8's own enums verbatim (identical shape - see X8ModuleCommon.hpp's own comment on
// why the two modules share this much) rather than declaring a separate X8D.hpp.
#include "X8.hpp"
#include "X8Common.hpp"

#define X8D_PANEL_WIDTH_MM 30.48f

struct X8D : Module, XExpanderInterface
{

#include "OrangeLineCommon.hpp"
#include "X8ModuleCommon.hpp"

	X8D()
	{
		initializeInstance();
		// One-time default, set here (not in moduleCustomInitialize(), which runs every tick,
		// not just once - see the comment on that hook below) so a saved patch's own value
		// (applied later by dataFromJson(), if the key exists) can still override it correctly.
		OL_state[CHANNEL_LIMIT_JSON] = (float) NUM_X8_KNOBS;
	}
};

/**
	Static background-colored cover for the per-channel numeric display column - shown only while
	buttons are visible (Toggle/Click/Push params have no numeric value worth showing), hiding the
	whole display area behind a plain rect matching the current theme's own panel background.
	Position/size measured directly from res/X8DWork.svg's own "ButtonCover" guide layer (one rect
	spanning all 8 rows at once, not per-channel). X8DWidget::step() toggles this and the 8
	X8DValueDisplay widgets as exact opposites of the same showButton flag, so exactly one of the
	two ever shows for a given row. X8-only - X8's own panel has no room for a display column at
	all, so it has no equivalent.
*/
struct X8DButtonCover : TransparentWidget
{
	Module *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;
		float style = expander ? expander->getXStyle() : STYLE_ORANGE;
		NVGcolor fill = (style == STYLE_DARK) ? X_STRIP_BG_DARK
		              : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT
		              : X_STRIP_BG_ORANGE;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
	}
};

/**
	Per-channel numeric readout, LCD-style like every other OrangeLine display - shows exactly the
	same formatted text as this channel's own knob tooltip (X8KnobQuantity/formatXValue() in
	X8Common.hpp), so the two can never disagree, per Dieter's own request. Reads THIS channel's
	own knob value (not just channel 0 - each of the 8 channels has its own independent value),
	colored to match the browsed candidate's own slot color (same convention as
	X8NameDisplay/X8ValueButton). Only relevant while a continuous param is browsed - X8DWidget's
	own step() hides these exactly when X8DButtonCover shows instead (a digital Toggle/Click/Push
	param has no numeric value worth showing). X8D-only, same reasoning as X8DButtonCover above.
*/
struct X8DValueDisplay : TransparentWidget
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

		// Same hard contract as X8NameDisplay's own getXParamShortName() text: max 5 characters,
		// no truncation/scrolling fallback beyond a defensive cutoff - a Host that returns more
		// can't make text bleed outside this widget either way.
		char raw[6];
		snprintf(raw, sizeof(raw), "%s", expander->formatXValue(expander->getXKnobValue(channel)).c_str());
		if (raw[0] == '\0')
			return;

		// "Simple and stupid" per Dieter: always draw NVG_ALIGN_LEFT, never NanoVG's own
		// CENTER/RIGHT modes (see X8_CENTER_OFFSET_MM's own comment on why) - right-alignment is
		// simulated by left-padding the STRING itself with blanks out to the full 5-character
		// field width, and centering reuses the exact same hand-tuned offset table as the name
		// display above, keyed on the un-padded length.
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

		// Same baseline-anchor/scissor reasoning as X8NameDisplay above.
		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, -fontSizePx * 1.2f, box.size.x, fontSizePx * 1.6f);
		nvgText(args.vg, x, 0.f, buffer, nullptr);
		nvgRestore(args.vg);
		Widget::drawLayer(args, 1);
	}
};

/**
	Main Module Widget

	Panel is X8's own layout doubled in width (6HP vs 3HP) - shared header (name display,
	LEFT/RIGHT step buttons, ENGAGE, both seam strips) widened to fill the new width, LEFT/RIGHT
	becoming the left/right half respectively (mirrored 8.382mm margins from each edge), ENGAGE
	spanning almost the full width, while the 8-channel knob column stays at the EXACT same
	x=7.62mm / y-positions as X8 (all measured directly from res/X8DWork.svg's own Controls
	layer, matching CLAUDE.md's Expander-family doubling convention). The newly available space
	to the right of each knob (around x=15.13mm, same y as each knob +~1.5mm) holds the per-
	channel numeric display (X8DValueDisplay) / cover (X8DButtonCover) pair.
*/
struct X8DWidget : ModuleWidget
{
	// BIND/LEFT/RIGHT sizes measured directly from res/X8DWork.svg's Controls layer
	// (BUTTON_FRAME path bounding boxes) - the only thing that actually differs from X8 here, see
	// X8Common.hpp for the shared X8ButtonBase/X8BindButtonBase logic. Nested (rather than
	// file-scope) so this can't ever collide with X8.cpp's own same-named, differently-sized
	// X8Widget::X8StepButton/X8BindButton across translation units.
	struct X8StepButton : X8ButtonBase { X8StepButton() { box.size = mm2px(Vec(12.192f, 4.572f)); } };
	struct X8BindButton : X8BindButtonBase { X8BindButton() { box.size = mm2px(Vec(25.908f, 5.588f)); } };

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
	X8DButtonCover *buttonCover = nullptr;
	X8DValueDisplay *displays[NUM_X8_KNOBS] = {};

	X8DWidget(X8D *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8DOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8DBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/X8DDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		// Connection light disabled 2026-07-18 (Dieter: more distracting than informative, breaks
		// the header's optics - connection is already visible via the panel's own controls) -
		// underlying setStateLight(CONN_LIGHT, ...) tracking logic left intact, only the widget
		// itself is no longer added.
		// addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(X8D_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, CONN_LIGHT));

		// LEFT/RIGHT/BIND positions/sizes measured directly from res/X8DWork.svg's Controls
		// layer (BUTTON_FRAME path bounding boxes) - see X8StepButton/X8BindButton comments.
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
		nameDisplay->box.size = mm2px(Vec(X8D_PANEL_WIDTH_MM - 2.f * X8_NAME_DISPLAY_MARGIN_MM, 5.f));
		addChild(nameDisplay);

		// 8 channel knobs, top (channel 1) to bottom (channel 8) - identical x/y to X8's own
		// knob column (verified against res/X8DWork.svg's knob group positions directly; "the
		// knob column itself stays put" per ExpanderParamAccessSpec.md's doubling recipe).
		static const float knobY[NUM_X8_KNOBS] = {
			37.339944f, 48.294524f, 59.249103f, 70.203682f,
			81.158262f, 92.112841f, 103.06742f, 114.022f
		};
		// Per-channel numeric display position - measured directly from res/X8DWork.svg's own
		// "DISPLAY"-labeled guide texts (not simply knobY + a fixed offset - each row's baseline
		// sits a slightly different distance below its own knob's y, per the real measurements).
		static const float displayY[NUM_X8_KNOBS] = {
			38.873039f, 49.784458f, 60.717041f, 71.639038f,
			82.815041f, 93.646629f, 104.65906f, 115.58104f
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

			X8DValueDisplay *display = new X8DValueDisplay();
			display->module = module;
			display->channel = i;
			display->box.pos = calculateCoordinates(15.12887f, displayY[i], 0.f);
			display->box.size = mm2px(Vec(15.f, 5.f));
			addChild(display);
			displays[i] = display;
		}

		extStrip = addXExtStrip(this, X8D_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);
		addXLogoCovers(this, X8D_PANEL_WIDTH_MM, &logoCover1, &logoCover2);

		// Position/size measured directly from res/X8DWork.svg's own "ButtonCover" guide layer -
		// see X8DButtonCover's own comment. Hidden by default (matches the knob column's own
		// "continuous shown until step() knows better" default above).
		buttonCover = new X8DButtonCover();
		buttonCover->module = module;
		buttonCover->box.pos = calculateCoordinates(12.342694f, 32.76725f, 0.f);
		buttonCover->box.size = mm2px(Vec(16.867304f, 85.090004f));
		buttonCover->visible = false;
		addChild(buttonCover);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		X8D *x8dModule = dynamic_cast<X8D *>(module);
		if (x8dModule)
		{
			updateXExtStrip(extStrip, x8dModule, x8dModule->rightExpander.module);
			updateXExtStripLeft(extStripLeft, x8dModule, x8dModule->leftExpander.module);
			updateXLogoCovers(logoCover1, logoCover2, x8dModule);
			// Type-based morph (ExpanderParamAccessSpec.md's "Type-based appearance"): continuous
			// -> knob, toggle/click/push -> button. One type governs all 8 channels at once,
			// since it's a property of the browsed param, not of any individual channel.
			bool showButton = x8dModule->getXBrowsedParamType() != X_PARAM_CONTINUOUS;
			for (int i = 0; i < NUM_X8_KNOBS; i++)
			{
				knobs[i]->visible = !showButton;
				valueButtons[i]->visible = showButton;
				displays[i]->visible = !showButton;
			}
			buttonCover->visible = showButton;
		}
		ModuleWidget::step();
	}

	// Channel count is Expander-owned (the "sender" decides, see moduleCustomInitialize() in
	// the module struct above) - same radio-submenu pattern as Morpheus's own "Poly Channels".
	struct X8ChannelsItem : MenuItem
	{
		X8D *module;

		struct X8ChannelItem : MenuItem
		{
			X8D *module;
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
		X8D *module;
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

		X8D *module = dynamic_cast<X8D *>(this->module);
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

Model *modelX8D = createModel<X8D, X8DWidget>("X8D");
