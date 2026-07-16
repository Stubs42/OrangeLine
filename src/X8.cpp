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

#define X8_PANEL_WIDTH_MM 15.24f

struct X8 : Module, XExpanderInterface
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	// Resolved every moduleProcess() tick by looking only at rightExpander.module (never
	// leftExpander.module - see XShared.hpp's left-only-attachment note).
	XHostInterface *xHost = nullptr;

	// Fully self-managed local state (see ExpanderParamAccessSpec.md's "Expander manages
	// itself completely") - the Host only ever reads these through XExpanderInterface, it
	// never writes them.
	int browseIndex = 0;
	dsp::SchmittTrigger engageTrigger, leftTrigger, rightTrigger;
	bool pendingEngagePress = false;

	X8()
	{
		initializeInstance();
		// One-time default, set here (not in moduleCustomInitialize(), which runs every tick,
		// not just once - see the comment on that hook below) so a saved patch's own value
		// (applied later by dataFromJson(), if the key exists) can still override it correctly.
		OL_state[CHANNEL_LIMIT_JSON] = (float) NUM_X8_KNOBS;
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
	}

	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel(STYLE_JSON, "style");
		setJsonLabel(CHANNEL_LIMIT_JSON, "channelLimit");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		for (int i = 0; i < NUM_X8_KNOBS; i++)
			configParam(KNOB_PARAM + i, 0.f, 1.f, 0.5f, "Value");
		configParam(LEFT_PARAM, 0.f, 1.f, 0.f, "Previous parameter");
		configParam(RIGHT_PARAM, 0.f, 1.f, 0.f, "Next parameter");
		configParam(ENGAGE_PARAM, 0.f, 1.f, 0.f, "Engage");
	}

	// moduleCustomInitialize() runs on every single non-skipped process() tick (not just once -
	// see initialize() in OrangeLineCommon.hpp), so it must never be used for a one-time
	// default - see the constructor above instead for CHANNEL_LIMIT_JSON's default.
	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		styleChanged = true;
		browseIndex = 0;
	}

	inline void moduleProcess(const ProcessArgs &args)
	{
		if (styleChanged && widgetReady)
		{
			switch (int(getStateJson(STYLE_JSON)))
			{
			case STYLE_ORANGE: brightPanel->visible = false; darkPanel->visible = false; break;
			case STYLE_BRIGHT: brightPanel->visible = true;  darkPanel->visible = false; break;
			case STYLE_DARK:   brightPanel->visible = false; darkPanel->visible = true;  break;
			}
			styleChanged = false;
		}

		xHost = resolveXHost(rightExpander.module);
		setStateLight(CONN_LIGHT, xHost ? 255.f : 0.f);

		// Browsing: unconditional, unfiltered stepping through every candidate param the
		// currently-resolved Host reports - see "Browsing is never locked or filtered" in
		// ExpanderParamAccessSpec.md. If no Host is resolved (or it has zero candidates),
		// browseIndex just stays at 0 and stepping has no visible effect.
		int count = xHost ? xHost->getXParamCount() : 0;
		if (count > 0)
		{
			browseIndex = clamp(browseIndex, 0, count - 1);
			if (leftTrigger.process(params[LEFT_PARAM].getValue()))
				browseIndex = (browseIndex - 1 + count) % count;
			if (rightTrigger.process(params[RIGHT_PARAM].getValue()))
				browseIndex = (browseIndex + 1) % count;
		}
		else
		{
			browseIndex = 0;
			leftTrigger.process(params[LEFT_PARAM].getValue());
			rightTrigger.process(params[RIGHT_PARAM].getValue());
		}

		// Engage button: local debounce only - this Expander has no idea whether a click will
		// actually bind, unbind, or do nothing at all. The Host decides that, during its own
		// process(), the next time it reads consumeEngagePress().
		if (engageTrigger.process(params[ENGAGE_PARAM].getValue()))
			pendingEngagePress = true;
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}

	// XExpanderInterface
	XHostInterface* getXHost() override { return xHost; }
	float getXStyle() override { return OL_state[STYLE_JSON]; }
	int getXKnobCount() override { return (int) OL_state[CHANNEL_LIMIT_JSON]; }
	float getXKnobValue(int channel) override { return getStateParam(KNOB_PARAM + channel); }
	int getXBrowseIndex() override { return browseIndex; }
	bool consumeEngagePress() override
	{
		bool fired = pendingEngagePress;
		pendingEngagePress = false;
		return fired;
	}
};

/**
	Squarish rounded-rect button matching the panel's own LEFT/RIGHT/ENGAGE artwork (see
	ExpanderParamAccessSpec.md's "Button visual design") - DisplayFill background per theme,
	fixed orange (#ff6600) stroke/label, momentary (value 1 while held, 0 on release) so the
	module's own SchmittTrigger-based edge detection in moduleProcess() keeps working unchanged.
*/
struct X8ButtonBase : ParamWidget
{
	std::string label;

	void onButton(const event::Button &e) override
	{
		engine::ParamQuantity *pq = getParamQuantity();
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && pq)
		{
			if (e.action == GLFW_PRESS)
			{
				pq->setValue(1.f);
				e.consume(this);
			}
			else if (e.action == GLFW_RELEASE)
			{
				pq->setValue(0.f);
			}
		}
		ParamWidget::onButton(e);
	}

	void draw(const DrawArgs &args) override
	{
		float style = STYLE_ORANGE;
		engine::ParamQuantity *pq = getParamQuantity();
		if (pq && pq->module)
		{
			X8 *module = dynamic_cast<X8*>(pq->module);
			if (module)
				style = module->OL_state[STYLE_JSON];
		}
		NVGcolor fill = (style == STYLE_DARK) ? nvgRGB(0x17, 0x17, 0x17)
		              : (style == STYLE_BRIGHT) ? nvgRGB(0x15, 0x15, 0x2b)
		              : nvgRGB(0x10, 0x06, 0x00);

		float r = mm2px(Vec(0.529f, 0.f)).x;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, r);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, mm2px(Vec(0.3f, 0.f)).x);
		nvgStrokeColor(args.vg, ORANGE);
		nvgStroke(args.vg);

		if (!label.empty())
		{
			// 8pt, per Dieter (matches the panel's own BUTTON_TEXT tspan override,
			// "2.82222px" - this SVG's viewBox makes 1 user unit == 1mm, so that raw number
			// already IS the mm size directly: 8pt * 0.3528 mm/pt == 2.82222mm).
			std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(Vec(2.82222f, 0.f)).x);
			nvgFillColor(args.vg, ORANGE);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, label.c_str(), nullptr);
		}
	}
};

// Sizes measured from the actual button-frame path's bounding box (including its rounded
// corners), not just the straight-edge segment lengths - the earlier version used the latter
// and came out visibly too small.
struct X8StepButton : X8ButtonBase { X8StepButton() { box.size = mm2px(Vec(4.6f, 4.6f)); } };
struct X8EngageButton : X8ButtonBase { X8EngageButton() { box.size = mm2px(Vec(10.69f, 5.61f)); } };

/**
	Channel knob that dims and stops accepting input once its own channel index is at or beyond
	the Expander's current "Channels" limit (see the right-click menu) - "channels above the
	limit simply aren't there", same as ExpanderParamAccessSpec.md already says for a real
	under-populated cable. No built-in Rack concept for this (checked Knob.hpp/ParamWidget.hpp/
	Widget.hpp - nothing like a generic "disabled" state), so both the dimming and the input
	block are done here by hand.
*/
struct X8Knob : RoundSmallBlackKnob
{
	int channel = 0;

	bool isActive()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		X8 *module = pq ? dynamic_cast<X8*>(pq->module) : nullptr;
		return !module || channel < (int) module->OL_state[CHANNEL_LIMIT_JSON];
	}

	void draw(const DrawArgs &args) override
	{
		if (isActive())
		{
			RoundSmallBlackKnob::draw(args);
			return;
		}
		nvgGlobalAlpha(args.vg, 0.3f);
		RoundSmallBlackKnob::draw(args);
		nvgGlobalAlpha(args.vg, 1.f);
	}

	// Value-changing interactions only - onButton() is left alone so right-click (context menu)
	// still works even on a dimmed/inactive knob.
	void onDragMove(const DragMoveEvent &e) override
	{
		if (!isActive())
			return;
		RoundSmallBlackKnob::onDragMove(e);
	}

	void onHoverScroll(const HoverScrollEvent &e) override
	{
		if (!isActive())
			return;
		RoundSmallBlackKnob::onHoverScroll(e);
	}

	// ParamWidget::onEnter() is what creates the hover tooltip, and Knob::onHover() is what
	// claims the hover (cursor/highlight) - skip both while inactive so a dimmed knob reads as
	// truly inert, not just visually dimmed. onLeave() always runs the base, unconditionally,
	// so a tooltip that was already open (e.g. channel limit changed while hovering) still gets
	// cleaned up properly.
	void onEnter(const EnterEvent &e) override
	{
		if (!isActive())
			return;
		RoundSmallBlackKnob::onEnter(e);
	}

	void onHover(const HoverEvent &e) override
	{
		if (!isActive())
			return;
		RoundSmallBlackKnob::onHover(e);
	}
};

/**
	Currently-browsed param name, green LCD-style like every other OrangeLine display - shows a
	placeholder when no Host is resolved or it has no candidate params yet.
*/
struct X8NameDisplay : TransparentWidget
{
	X8 *module = nullptr;

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, mm2px(Vec(4.49792f, 0.f)).x);
		nvgFillColor(args.vg, nvgRGB(0x00, 0xdd, 0x00));
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

		const char *text = "XXXXX";
		if (module && module->xHost)
		{
			int count = module->xHost->getXParamCount();
			if (count > 0)
				text = module->xHost->getXParamName(clamp(module->browseIndex, 0, count - 1));
		}
		nvgText(args.vg, 0.f, 0.f, text, nullptr);
		Widget::drawLayer(args, 1);
	}
};

/**
	Main Module Widget
*/
struct X8Widget : ModuleWidget
{
	XExtStripWidget *extStrip = nullptr;

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

		addChild(createLightCentered<AutoHideLight<TinyLight<GreenLight>>>(calculateCoordinates(X8_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, CONN_LIGHT));

		X8StepButton *leftButton = createParamCentered<X8StepButton>(calculateCoordinates(4.550f, 18.034f, 0.f), module, LEFT_PARAM);
		leftButton->label = "<";
		addParam(leftButton);
		X8StepButton *rightButton = createParamCentered<X8StepButton>(calculateCoordinates(10.657f, 18.035f, 0.f), module, RIGHT_PARAM);
		rightButton->label = ">";
		addParam(rightButton);
		X8EngageButton *engageButton = createParamCentered<X8EngageButton>(calculateCoordinates(7.609f, 24.629f, 0.f), module, ENGAGE_PARAM);
		engageButton->label = "ENGAGE";
		addParam(engageButton);

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
		}

		extStrip = addXExtStrip(this, X8_PANEL_WIDTH_MM);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		X8 *x8Module = dynamic_cast<X8 *>(module);
		if (x8Module)
			updateXExtStrip(extStrip, x8Module, x8Module->rightExpander.module);
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
