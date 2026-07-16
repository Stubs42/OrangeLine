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
	// never writes them. The currently-browsed index itself lives in OL_state[BROWSE_INDEX_JSON]
	// (not a plain member) so it round-trips through the normal JSON persistence automatically,
	// same as CHANNEL_LIMIT_JSON - a separate cached member would go stale the moment
	// dataFromJson() restores OL_state without also updating it.
	dsp::SchmittTrigger engageTrigger, leftTrigger, rightTrigger;
	bool pendingEngagePress = false;

	// One-shot guard: attempt, exactly once after a compatible Host first resolves, to
	// auto-restore an engagement that existed before a patch reload - see WAS_ENGAGED_JSON's
	// comment in X8.hpp and the auto-reengage block in moduleProcess().
	bool triedAutoReengage = false;

	// Edge-detects "the param I'm currently standing on just became bound to me" (a fresh
	// engage taking effect on the Host's side, possibly a few ticks after the physical press -
	// Host and Expander each run on their own throttled cycle) so knobs can be snapped exactly
	// once. Keyed to the index itself, not just a bare bool: browsing away and back onto a
	// param this Expander already holds (Track & Hold) must NOT re-fire and stomp live knob
	// positions with the old snapshot - only a genuine unbound->bound transition while standing
	// still on the same index counts.
	bool xLastBoundHere = false;
	int xLastCheckedIndex = -1;

	// Set the first time this Expander ever engages with a Host, to that Host's own
	// getXHostTypeName() - permanent (persisted) from then on, regardless of whether that
	// specific binding is later released. While set, a resolved Host of a *different* type is
	// treated as if nothing were connected at all (see moduleProcess()) - only a right-click
	// Initialize (moduleReset()) clears it. Empty = not yet locked to any type.
	std::string lockedHostType;

	// X_PARAM_CLICK needs a fixed-length pulse "independent of hold duration" (XShared.hpp) -
	// unlike TOGGLE/PUSH, which the widget can drive directly via plain setValue() calls, CLICK
	// needs module-owned timing so the value (and therefore the light, which just mirrors it -
	// see X8ValueButton) always drops back to 0 after a fixed duration regardless of how long
	// the mouse stays down. pendingValueClick[c] is a one-shot request the widget sets on press;
	// clickPulse[c] is the actual timer, consumed once per tick in moduleProcess().
	dsp::PulseGenerator clickPulse[NUM_X8_KNOBS];
	bool pendingValueClick[NUM_X8_KNOBS] = {false};

	// The type governing all 8 channels right now - one type applies to the whole browsed param,
	// never per-channel. Defaults to continuous (knob) whenever nothing meaningful is resolved,
	// so callers never need their own separate "no Host" fallback.
	XParamType getXBrowsedParamType()
	{
		if (!xHost)
			return X_PARAM_CONTINUOUS;
		int count = xHost->getXParamCount();
		if (count <= 0)
			return X_PARAM_CONTINUOUS;
		int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
		return xHost->getXParamType(idx);
	}

	// The Host's own per-slot accent color for whatever's currently browsed - see
	// XHostInterface::getXParamColor(). Falls back to the fixed panel ORANGE when nothing
	// meaningful is resolved, same reasoning as getXBrowsedParamType() above.
	NVGcolor getXBrowsedParamColor()
	{
		if (!xHost)
			return ORANGE;
		int count = xHost->getXParamCount();
		if (count <= 0)
			return ORANGE;
		int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
		return xHost->getXParamColor(idx);
	}

	X8()
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
		setJsonLabel(BROWSE_INDEX_JSON, "browseIndex");
		setJsonLabel(WAS_ENGAGED_JSON, "wasEngaged");

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
		OL_state[BROWSE_INDEX_JSON] = 0.f;
		OL_state[WAS_ENGAGED_JSON] = 0.f;
		lockedHostType.clear(); // right-click Initialize - the only way to release a host type-lock
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

		// Host type-lock: once this Expander has ever engaged with a Host type, a *different*
		// type resolving here is treated as if nothing were connected at all - see
		// lockedHostType's own comment. Green = connected (and compatible, or not yet locked to
		// anything), Red = something's there but the wrong type, blocked.
		XHostInterface *resolved = resolveXHost(rightExpander.module);
		bool typeBlocked = resolved && !lockedHostType.empty() && lockedHostType != resolved->getXHostTypeName();
		xHost = typeBlocked ? nullptr : resolved;
		setStateLight(CONN_LIGHT,     xHost ? 255.f : 0.f);
		setStateLight(CONN_LIGHT + 1, typeBlocked ? 255.f : 0.f);

		// Browsing: unconditional, unfiltered stepping through every candidate param the
		// currently-resolved Host reports - see "Browsing is never locked or filtered" in
		// ExpanderParamAccessSpec.md. If no Host is resolved (or it has zero candidates),
		// browseIndex just stays put and stepping has no visible effect. Lives in
		// OL_state[BROWSE_INDEX_JSON] (not a plain member) so it survives a patch reload - see
		// that json id's own comment in X8.hpp.
		int browseIndex = (int) OL_state[BROWSE_INDEX_JSON];
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
			// Deliberately NOT reset to 0 here - reconnecting (to the same Host, or another of
			// the same type) should land back on the same param it was last browsing, not jump
			// back to the first candidate. Gets clamped into range again above once a Host with
			// count > 0 resolves, so a narrower Host can't read this out of bounds.
			leftTrigger.process(params[LEFT_PARAM].getValue());
			rightTrigger.process(params[RIGHT_PARAM].getValue());
		}
		OL_state[BROWSE_INDEX_JSON] = (float) browseIndex;

		// See xLastBoundHere's comment above: only a fresh bind while standing still on the
		// same index snaps the knobs - browsing onto an already-bound index never re-fires.
		if (xHost && count > 0)
		{
			bool boundHere = xHost->getXParamBoundId(browseIndex) == (int64_t) this->id;

			// Auto-restore an engagement that existed before a patch reload: bindings themselves
			// are session-only (Rack ids aren't safe to persist/compare across a reload - see
			// WAS_ENGAGED_JSON's comment), so this just presses our own engage button once,
			// through the ordinary mechanism, using whatever id this Expander currently has.
			if (!triedAutoReengage)
			{
				triedAutoReengage = true;
				if (OL_state[WAS_ENGAGED_JSON] > 0.f && !boundHere)
					pendingEngagePress = true;
			}

			if (browseIndex == xLastCheckedIndex && boundHere && !xLastBoundHere)
			{
				int channels = getXKnobCount();
				for (int c = 0; c < channels; c++)
					params[KNOB_PARAM + c].setValue(xHost->getXParamTakeoverValue(browseIndex, c));
				// First-ever successful engage locks this Expander to the Host's type from now
				// on - see lockedHostType's own comment. Only Initialize releases it.
				if (lockedHostType.empty())
					lockedHostType = xHost->getXHostTypeName();
			}
			xLastBoundHere = boundHere;
			xLastCheckedIndex = browseIndex;
			OL_state[WAS_ENGAGED_JSON] = boundHere ? 1.f : 0.f;
		}
		else
		{
			xLastBoundHere = false;
			xLastCheckedIndex = -1;
		}

		// Engage button: local debounce only - this Expander has no idea whether a click will
		// actually bind, unbind, or do nothing at all. The Host decides that, during its own
		// process(), the next time it reads consumeEngagePress().
		if (engageTrigger.process(params[ENGAGE_PARAM].getValue()))
			pendingEngagePress = true;

		// X_PARAM_CLICK's fixed-length pulse, independent of hold duration - see
		// pendingValueClick's own comment above. Only touches the params while the browsed type
		// is actually CLICK, so it never fights TOGGLE/PUSH's own direct widget-driven values.
		if (getXBrowsedParamType() == X_PARAM_CLICK)
		{
			for (int c = 0; c < NUM_X8_KNOBS; c++)
			{
				if (pendingValueClick[c])
				{
					pendingValueClick[c] = false;
					clickPulse[c].trigger(0.1f);
				}
				// moduleProcess() only runs on ~1 in idleSkip samples (control-rate throttling),
				// so a bare args.sampleTime here would underestimate elapsed real time by that
				// same factor, making the pulse take ~43x longer than intended - see CLAUDE.md's
				// "dsp::Timer inside moduleProcess() must scale by (samplesSkipped + 1)" pitfall.
				params[KNOB_PARAM + c].setValue(clickPulse[c].process(args.sampleTime * (samplesSkipped + 1)) ? 1.f : 0.f);
			}
		}
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}

	// XExpanderInterface
	XHostInterface* getXHost() override { return xHost; }
	float getXStyle() override { return OL_state[STYLE_JSON]; }
	int getXKnobCount() override { return (int) OL_state[CHANNEL_LIMIT_JSON]; }
	float getXKnobValue(int channel) override { return getStateParam(KNOB_PARAM + channel); }
	int getXBrowseIndex() override { return (int) OL_state[BROWSE_INDEX_JSON]; }
	bool consumeEngagePress() override
	{
		bool fired = pendingEngagePress;
		pendingEngagePress = false;
		return fired;
	}
};

// Shared by X8EngageButton/X8Knob below: is the currently-browsed param bound to a *different*
// Expander, or is a real cable connected on the Host's own jack for it? Either way this Expander
// has no control over it right now, regardless of the Channels limit or connection state
// otherwise - matches ExpanderParamAccessSpec.md's "taken/unavailable" state (same computation
// X8NameDisplay's own color-coding already does, kept separate there since it also needs "mine").
static bool x8BrowsedParamTaken(X8 *module)
{
	if (!module || !module->xHost)
		return false;
	int count = module->xHost->getXParamCount();
	if (count <= 0)
		return false;
	int idx = clamp((int) module->OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	bool mine = module->xHost->isXParamEngaged(idx) && module->xHost->getXParamBoundId(idx) == (int64_t) module->id;
	return (module->xHost->isXParamEngaged(idx) && !mine) || module->xHost->isXParamCableConnected(idx);
}

/**
	Squarish rounded-rect button matching the panel's own LEFT/RIGHT/ENGAGE artwork (see
	ExpanderParamAccessSpec.md's "Button visual design") - DisplayFill background per theme,
	fixed orange (#ff6600) stroke/label, momentary (value 1 while held, 0 on release) so the
	module's own SchmittTrigger-based edge detection in moduleProcess() keeps working unchanged.
*/
struct X8ButtonBase : ParamWidget
{
	std::string label;

	// No function at all while disconnected (no Host resolved, or blocked by the host
	// type-lock) - LEFT/RIGHT/ENGAGE have nothing to step through or bind when there's nobody
	// to browse. No module context at all (e.g. the module browser's preview) defaults active.
	// Virtual so X8EngageButton can add its own extra "taken" condition on top - see there.
	virtual bool isActive()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		X8 *module = pq ? dynamic_cast<X8*>(pq->module) : nullptr;
		return !module || module->xHost != nullptr;
	}

	void onButton(const event::Button &e) override
	{
		engine::ParamQuantity *pq = getParamQuantity();
		// Only the LEFT-press press/release behavior is gated - right-click still passes
		// through to ParamWidget::onButton() below regardless, same reasoning as X8Knob's own
		// isActive() gating (see its comment): a dimmed/inactive control still shouldn't
		// swallow the right-click context menu.
		if (isActive() && e.button == GLFW_MOUSE_BUTTON_LEFT && pq)
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
		NVGcolor accent = isActive() ? ORANGE : nvgRGB(0x55, 0x55, 0x55); // grey - disconnected

		float r = mm2px(Vec(0.529f, 0.f)).x;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, r);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, mm2px(Vec(0.3f, 0.f)).x);
		nvgStrokeColor(args.vg, accent);
		nvgStroke(args.vg);

		if (!label.empty())
		{
			// 8pt, per Dieter (matches the panel's own BUTTON_TEXT tspan override,
			// "2.82222px" - this SVG's viewBox makes 1 user unit == 1mm, so that raw number
			// already IS the mm size directly: 8pt * 0.3528 mm/pt == 2.82222mm).
			std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(Vec(2.82222f, 0.f)).x);
			nvgFillColor(args.vg, accent);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, label.c_str(), nullptr);
		}
	}
};

// Sizes measured from the actual button-frame path's bounding box (including its rounded
// corners), not just the straight-edge segment lengths - the earlier version used the latter
// and came out visibly too small.
struct X8StepButton : X8ButtonBase { X8StepButton() { box.size = mm2px(Vec(4.6f, 4.6f)); } };

struct X8EngageButton : X8ButtonBase
{
	X8EngageButton() { box.size = mm2px(Vec(10.69f, 5.61f)); }

	// Also inert (grey) whenever the currently-browsed param is "taken" - a click there is a
	// no-op anyway per the spec ("the Host decides... do nothing at all if the browsed param is
	// grey"). LEFT/RIGHT deliberately stay active in this case - browsing away from a taken slot
	// must keep working (ExpanderParamAccessSpec.md: "browsing is never locked... stepping onto
	// a grey entry is harmless read-only viewing, no restriction of any kind").
	bool isActive() override
	{
		if (!X8ButtonBase::isActive())
			return false;
		engine::ParamQuantity *pq = getParamQuantity();
		X8 *module = pq ? dynamic_cast<X8*>(pq->module) : nullptr;
		return !x8BrowsedParamTaken(module);
	}
};

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
		if (!module)
			return true;
		if (!module->xHost)
			return false; // disconnected (or blocked by the host type-lock) - nothing to control
		if (x8BrowsedParamTaken(module))
			return false; // bound to a different Expander, or a real cable - not ours to turn
		return channel < (int) module->OL_state[CHANNEL_LIMIT_JSON];
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
	Toggle/Click/Push channel control - replaces the knob whenever the currently-browsed param
	isn't continuous (see X8Widget's own visibility-toggle in step()). Uses SquareButton.svg
	(Dieter's own squared-off cap art, built directly in Inkscape from the real
	RoundSmallBlackKnob artwork) plus one square "LIGHT" indicator drawn on top in C++ - one
	visual mechanism, three timings:
	  - Toggle: click flips it, the light holds either state with no animation.
	  - Click: light flashes for a fixed duration regardless of hold time (X8's own
	    moduleProcess() owns this timing via a PulseGenerator - see pendingValueClick).
	  - Push: light lit only while the mouse is actually held down.
	The light always just mirrors the underlying param's own current value (>0.5 = lit) - it
	never needs separate state, since all three interaction modes above drive that same value.
*/
struct X8ValueButton : ParamWidget
{
	int channel = 0;
	widget::SvgWidget *cap = nullptr;

	// LIGHT square - SquareButton.svg (unlike the earlier cap attempts) has no built-in overflow
	// to work around, so this is simply centred in the widget's own box rather than pinned to
	// absolute coordinates from an old file's specific internal layout.
	static constexpr float LIGHT_SIZE_MM = 4.572f;

	X8ValueButton()
	{
		cap = new widget::SvgWidget();
		cap->setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SquareButton.svg")));
		addChild(cap);
		box.size = cap->box.size; // matches the loaded SVG's own physical size exactly
	}

	// Same disconnected/taken/channel-limit gating as X8Knob - see its own comment for why each
	// case applies.
	bool isActive()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		X8 *module = pq ? dynamic_cast<X8*>(pq->module) : nullptr;
		if (!module)
			return true;
		if (!module->xHost)
			return false;
		if (x8BrowsedParamTaken(module))
			return false;
		return channel < (int) module->OL_state[CHANNEL_LIMIT_JSON];
	}

	bool isPressed()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		return pq && pq->getValue() > 0.5f;
	}

	// Covers the panel's own decorative knob-ring circle underneath (kept drawn normally, per
	// Dieter's call - "die circles ganz normal auf dem panel lassen") with a plain, solid,
	// themed window: Background fill + Frame stroke, straight from Colors.txt (NOT the fixed-
	// orange-accent convention X8ButtonBase's own LEFT/RIGHT/ENGAGE use - this one deliberately
	// follows the panel frame's own per-theme convention instead). No mask/hole needed - the new
	// cap has no overflow to work around, so this is just a solid rounded rect sized to hide the
	// circle, drawn BEHIND the cap (not on top, unlike the earlier masking attempt).
	void drawThemeFrame(const DrawArgs &args, float style)
	{
		NVGcolor background, frame;
		switch ((int) style)
		{
			case STYLE_DARK:   background = nvgRGB(0x20, 0x20, 0x20); frame = nvgRGB(0x60, 0x60, 0x60); break;
			case STYLE_BRIGHT: background = nvgRGB(0xe6, 0xe6, 0xe6); frame = nvgRGB(0x60, 0x60, 0x80); break;
			default:           background = nvgRGB(0x15, 0x15, 0x2b); frame = nvgRGB(0x80, 0x33, 0x00); break; // STYLE_ORANGE
		}
		float w = mm2px(9.144f), h = mm2px(9.144f), r = mm2px(1.852f);
		float x = box.size.x / 2.f - w / 2.f, y = box.size.y / 2.f - h / 2.f;

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, x, y, w, h, r);
		nvgFillColor(args.vg, background);
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, mm2px(0.3f));
		nvgStrokeColor(args.vg, frame);
		nvgStroke(args.vg);
	}

	void draw(const DrawArgs &args) override
	{
		// Themed background window drawn BEHIND the cap - see drawThemeFrame()'s own comment.
		engine::ParamQuantity *pq = getParamQuantity();
		X8 *module = pq ? dynamic_cast<X8*>(pq->module) : nullptr;
		drawThemeFrame(args, module ? module->OL_state[STYLE_JSON] : STYLE_ORANGE);

		nvgSave(args.vg);
		if (!isActive())
			nvgGlobalAlpha(args.vg, 0.3f);
		Widget::draw(args); // draws the cap child, simply centred - no offset needed
		if (!isActive())
			nvgGlobalAlpha(args.vg, 1.f);
		nvgRestore(args.vg);
	}

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		bool active = isActive();
		engine::ParamQuantity *pq = getParamQuantity();
		X8 *module = pq ? dynamic_cast<X8*>(pq->module) : nullptr;
		NVGcolor onColor = module ? module->getXBrowsedParamColor() : ORANGE;
		nvgSave(args.vg);
		if (!active)
			nvgGlobalAlpha(args.vg, 0.3f);
		float lightSize = mm2px(LIGHT_SIZE_MM);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, box.size.x / 2.f - lightSize / 2.f, box.size.y / 2.f - lightSize / 2.f, lightSize, lightSize);
		nvgStrokeColor(args.vg, (active && isPressed()) ? onColor : nvgRGB(0x4a, 0x44, 0x3c));
		nvgStrokeWidth(args.vg, mm2px(0.5f));
		nvgStroke(args.vg);
		if (!active)
			nvgGlobalAlpha(args.vg, 1.f);
		nvgRestore(args.vg);
		Widget::drawLayer(args, 1);
	}

	void onButton(const event::Button &e) override
	{
		engine::ParamQuantity *pq = getParamQuantity();
		if (isActive() && e.button == GLFW_MOUSE_BUTTON_LEFT && pq)
		{
			X8 *module = dynamic_cast<X8*>(pq->module);
			XParamType type = module ? module->getXBrowsedParamType() : X_PARAM_TOGGLE;
			if (e.action == GLFW_PRESS)
			{
				e.consume(this);
				switch (type)
				{
					case X_PARAM_TOGGLE:
						pq->setValue(pq->getValue() > 0.5f ? 0.f : 1.f);
						break;
					case X_PARAM_PUSH:
						pq->setValue(1.f);
						break;
					case X_PARAM_CLICK:
						if (module)
							module->pendingValueClick[channel] = true;
						break;
					default:
						break;
				}
			}
			else if (e.action == GLFW_RELEASE && type == X_PARAM_PUSH)
			{
				pq->setValue(0.f);
			}
		}
		ParamWidget::onButton(e);
	}
};

/**
	Currently-browsed param name, LCD-style like every other OrangeLine display - color-coded per
	ExpanderParamAccessSpec.md's "Name display": grey dashes when no Host is resolved (doubles as
	the "not connected" indicator, replacing an earlier "XXXXX" placeholder that misleadingly
	looked like real content); otherwise green when this instance is the bound provider for the
	browsed param, grey when it's taken (bound elsewhere, or a real cable is patched in), orange
	when it's available (nobody holds it).

	Always shows getXParamShortName(), never getXParamName() - this display is sized for and
	assumes the interface's hard contract (see XShared.hpp): a Host's short name must never
	exceed 5 characters. There is no truncation/scrolling fallback here, by design - respecting
	the contract is the Host's responsibility, not this widget's.
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

		// No Host resolved (or blocked by the type-lock) - red placeholder, more attention-
		// grabbing than the "taken" grey since it means this specific Expander has physically
		// moved out of its chain (see the adjacency-gating feature) while still remembering a
		// locked type. Shows that locked host type name (e.g. "MORPH") if this Expander has ever
		// engaged with one, so the lock is visible, not just enforced silently - plain dashes if
		// never locked at all.
		const char *text = (module && !module->lockedHostType.empty()) ? module->lockedHostType.c_str() : "-----";
		NVGcolor color = nvgRGB(0xdd, 0x00, 0x00); // red - no host / blocked by type-lock

		if (module && module->xHost)
		{
			int count = module->xHost->getXParamCount();
			if (count > 0)
			{
				int idx = clamp((int) module->OL_state[BROWSE_INDEX_JSON], 0, count - 1);
				text = module->xHost->getXParamShortName(idx);
				bool mine = module->xHost->isXParamEngaged(idx) && module->xHost->getXParamBoundId(idx) == (int64_t) module->id;
				bool taken = (module->xHost->isXParamEngaged(idx) && !mine) || module->xHost->isXParamCableConnected(idx);
				NVGcolor slotColor = module->xHost->getXParamColor(idx);
				if (mine)
					color = slotColor; // this slot's own color, full brightness - it's bound to me
				else if (taken)
					color = nvgRGB(0x55, 0x55, 0x55); // grey - taken/unavailable
				else
					color = nvgLerpRGBA(slotColor, nvgRGB(0x55, 0x55, 0x55), 0.5f); // available - same
					                                 // slot color, dimmed toward grey so "mine" still
					                                 // reads as visually distinct from "free to take"
			}
		}
		// Defense in depth beyond the documented contract: never even attempt to draw more than
		// 5 characters, and clip drawing to this widget's own box - a Host violating the
		// contract can't make text bleed outside the display area.
		char buffer[6];
		snprintf(buffer, sizeof(buffer), "%s", text);

		float fontSizePx = mm2px(Vec(4.49792f, 0.f)).x;
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSizePx);
		nvgFillColor(args.vg, color);

		// Shorter-than-max short names (e.g. "REC", "LOCK") are centered rather than left-hung -
		// only the full-width 5-char case actually needs the left edge as an anchor. Positions
		// measured by hand in Inkscape against the real font/panel (not computed from font
		// metrics, which drift off true visual center for this LCD-style font's side-bearings):
		// 5 chars x=1.570 (this is the widget's own local x=0 anchor, unchanged), 4 chars
		// x=2.812, 3 chars x=4.053, 2 chars x=5.295, 1 char x=9.389 - offsets below are each
		// relative to the 5-char reference.
		static const float xOffsetMm[6] = { 0.f, 7.819f, 3.725f, 2.483f, 1.242f, 0.f }; // index = strlen
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float x = mm2px(xOffsetMm[strlen(buffer)]);

		// Text is drawn at local x with baseline alignment, so glyphs extend upward (ascenders)
		// from y=0, not downward - box.size.y (an arbitrary hit-test size, not a text-metrics
		// box) doesn't describe where the glyphs actually render. Scissor a generous band around
		// the baseline instead of assuming (0,0,w,h) covers the glyphs.
		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, -fontSizePx * 1.2f, box.size.x, fontSizePx * 1.6f);
		nvgText(args.vg, x, 0.f, buffer, nullptr);
		nvgRestore(args.vg);
		Widget::drawLayer(args, 1);
	}
};

/**
	Main Module Widget
*/
struct X8Widget : ModuleWidget
{
	XExtStripWidget *extStrip = nullptr;     // right edge - toward the Host (or a further Expander)
	XExtStripWidget *extStripLeft = nullptr; // left edge - toward a further chained Expander

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

		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(X8_PANEL_WIDTH_MM - 3.5f, 4.f, 0.f), module, CONN_LIGHT));

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
			knobs[i] = knob;

			X8ValueButton *button = createParamCentered<X8ValueButton>(calculateCoordinates(7.62f, knobY[i] , 0.f), module, KNOB_PARAM + i);
			button->channel = i;
			button->visible = false; // default: knob shown (continuous) until step() knows better
			addParam(button);
			valueButtons[i] = button;
		}

		extStrip = addXExtStrip(this, X8_PANEL_WIDTH_MM);
		extStripLeft = addXExtStripLeft(this);

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
