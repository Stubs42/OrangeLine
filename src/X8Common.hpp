/*
	X8Common.hpp

	Shared widget/control classes for X8 and X8D - #include'd at file scope by both X8.cpp and
	X8D.cpp, before their own module struct definitions. See X8ModuleCommon.hpp's own comment for
	why the two modules share this much code in the first place.

	Everything here operates on a plain `Module *` + `dynamic_cast<XExpanderInterface*>`, never on
	`X8*`/`X8D*` directly - that's what makes verbatim sharing between the two modules possible
	without any macro trickery: XExpanderInterface (XShared.hpp) now exposes everything a control
	widget needs (browsed type/color/align/format, channel count, lock state, click requests), so
	this code never needs to know which concrete Expander it's attached to.

	NOT shared here (each module's own .cpp keeps these, since they differ in size/position or,
	for X8D's per-channel display, don't exist on X8 at all): the module struct's constructor,
	the sized X8StepButton/X8EngageButton subclasses (only the ENGAGE-specific isActive() logic is
	shared, via X8EngageButtonBase below), X8Widget/X8DWidget themselves, and X8D's own
	X8DButtonCover/X8DValueDisplay.
*/

// Position/size of the two OrangeLine wordmark pieces ("orange" + "Line", stacked) - measured
// directly from res/X8DWork.svg's own "LogoCover" guide layer. Fixed regardless of panel width
// (the logo itself never changes size) - only the horizontal centering shifts per module, see
// addXLogoCovers() below.
#define X8_LOGO_COVER1_Y_MM      121.41325f
#define X8_LOGO_COVER1_WIDTH_MM  12.7f
#define X8_LOGO_COVER1_HEIGHT_MM 2.9735825f
#define X8_LOGO_COVER2_Y_MM      125.03953f
#define X8_LOGO_COVER2_WIDTH_MM  7.6199994f
#define X8_LOGO_COVER2_HEIGHT_MM 2.9777272f

/**
	Static background-colored cover for one piece of the OrangeLine wordmark - purely cosmetic,
	shown only while this Expander is actually connected to a Host (see updateXLogoCovers()),
	hiding the brand text behind a plain rect matching the current theme's own panel background.
	Per Dieter: hide the logo TEXT specifically when connected, not the physical accent line.
*/
struct X8LogoCover : TransparentWidget
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

// Both rects are horizontally centered on the module regardless of panel width (verified against
// res/X8DWork.svg's own guide rects - each one's own x + width/2 lands exactly on panelWidthMm/2)
// - only the Y position/size are fixed absolutes, matching the logo's own fixed size.
inline void addXLogoCovers(ModuleWidget *w, float panelWidthMm, X8LogoCover **cover1Out, X8LogoCover **cover2Out)
{
	X8LogoCover *cover1 = new X8LogoCover();
	cover1->box.pos = calculateCoordinates(panelWidthMm / 2.f - X8_LOGO_COVER1_WIDTH_MM / 2.f, X8_LOGO_COVER1_Y_MM, 0.f);
	cover1->box.size = mm2px(Vec(X8_LOGO_COVER1_WIDTH_MM, X8_LOGO_COVER1_HEIGHT_MM));
	cover1->visible = false;
	w->addChild(cover1);
	*cover1Out = cover1;

	X8LogoCover *cover2 = new X8LogoCover();
	cover2->box.pos = calculateCoordinates(panelWidthMm / 2.f - X8_LOGO_COVER2_WIDTH_MM / 2.f, X8_LOGO_COVER2_Y_MM, 0.f);
	cover2->box.size = mm2px(Vec(X8_LOGO_COVER2_WIDTH_MM, X8_LOGO_COVER2_HEIGHT_MM));
	cover2->visible = false;
	w->addChild(cover2);
	*cover2Out = cover2;
}

// Called every widget step() (UI frame rate, cosmetic only) - both covers show together, exactly
// while a Host is resolved (regardless of whether this Expander is actually bound/engaged to any
// specific param - "connected to a host" per Dieter's own wording, not "actively controlling
// something").
inline void updateXLogoCovers(X8LogoCover *cover1, X8LogoCover *cover2, Module *module)
{
	XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;
	bool connected = expander && expander->getXHost() != nullptr;
	cover1->visible = connected;
	cover2->visible = connected;
}

// Shared by every LCD-style display in this file - per Dieter's own "simple and stupid"
// convention: text is ALWAYS drawn left-aligned (NVG_ALIGN_LEFT) at local x=0, never using
// NanoVG's own CENTER/RIGHT text-align modes (their metrics drift off true visual center for
// this LCD-style font's side-bearings). "Centered" and "right-aligned" are both simulated
// instead: centered text picks its own hand-tuned starting x offset from this table (index =
// strlen, still drawn flush-left from there); right-aligned text (X8D's per-channel display)
// gets left-padded with blanks out to the full 5-character field width first, so the visible
// characters end up flush against the right edge without ever touching NanoVG's own alignment
// modes. Positions measured by hand in Inkscape against the real font/panel (not computed from
// font metrics): 5 chars x=1.570 (the widget's own local x=0 anchor, unchanged), 4 chars x=2.812,
// 3 chars x=4.053, 2 chars x=5.295, 1 char x=9.389 - offsets below are each relative to the
// 5-char reference.
static const float X8_CENTER_OFFSET_MM[6] = { 0.f, 3.725f + 1.242f, 3.725f, 2.483f, 1.242f, 0.f }; // index = strlen

// Shared by X8EngageButtonBase/X8Knob/X8ValueButton below: is the currently-browsed param bound
// to a *different* Expander, or is a real cable connected on the Host's own jack for it? Either
// way this Expander has no control over it right now, regardless of the Channels limit or
// connection state otherwise - matches ExpanderParamAccessSpec.md's "taken/unavailable" state
// (same computation X8NameDisplay's own color-coding already does, kept separate there since it
// also needs "mine"). Takes a plain Module* (works for X8 or X8D transparently) rather than a
// concrete Expander type.
static bool x8BrowsedParamTaken(Module *module)
{
	XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;
	if (!expander)
		return false;
	XHostInterface *xHost = expander->getXHost();
	if (!xHost)
		return false;
	int count = xHost->getXParamCount();
	if (count <= 0)
		return false;
	int idx = clamp(expander->getXBrowseIndex(), 0, count - 1);
	bool mine = xHost->isXParamEngaged(idx) && xHost->getXParamBoundId(idx) == (int64_t) module->id;
	return (xHost->isXParamEngaged(idx) && !mine) || xHost->isXParamCableConnected(idx);
}

/**
	Custom ParamQuantity for a channel knob's own KNOB_PARAM - its native Rack hover tooltip calls
	through XExpanderInterface::formatXValue() (in turn the Host's own formatXParamValue()),
	instead of a meaningless raw 0..1 readout. The physical control represents a different
	candidate param over time, so a static configParam() unit string can never be right for more
	than one of them.
*/
struct X8KnobQuantity : ParamQuantity
{
	std::string getDisplayValueString() override
	{
		XExpanderInterface *expander = this->module ? dynamic_cast<XExpanderInterface*>(this->module) : nullptr;
		std::string formatted = expander ? expander->formatXValue(getValue()) : "";
		return formatted.empty() ? ParamQuantity::getDisplayValueString() : formatted;
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

	// No function at all while disconnected (no Host resolved, or blocked by the host
	// type-lock) - LEFT/RIGHT/ENGAGE have nothing to step through or bind when there's nobody
	// to browse. No module context at all (e.g. the module browser's preview) defaults active.
	// Virtual so X8EngageButtonBase can add its own extra "taken" condition on top - see there.
	virtual bool isActive()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		XExpanderInterface *expander = pq && pq->module ? dynamic_cast<XExpanderInterface*>(pq->module) : nullptr;
		return !pq || !pq->module || (expander && expander->getXHost() != nullptr);
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
		XExpanderInterface *expander = pq && pq->module ? dynamic_cast<XExpanderInterface*>(pq->module) : nullptr;
		if (expander)
			style = expander->getXStyle();
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

// Also inert (grey) whenever the currently-browsed param is "taken" - a click there is a no-op
// anyway per the spec ("the Host decides... do nothing at all if the browsed param is grey").
// LEFT/RIGHT deliberately stay plain X8ButtonBase (active in this case) - browsing away from a
// taken slot must keep working (ExpanderParamAccessSpec.md: "browsing is never locked... stepping
// onto a grey entry is harmless read-only viewing, no restriction of any kind"). Each module's own
// .cpp adds only a one-line constructor setting box.size (ENGAGE's size differs between X8/X8D).
struct X8EngageButtonBase : X8ButtonBase
{
	bool isActive() override
	{
		if (!X8ButtonBase::isActive())
			return false;
		engine::ParamQuantity *pq = getParamQuantity();
		return !x8BrowsedParamTaken(pq ? pq->module : nullptr);
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
		Module *module = pq ? pq->module : nullptr;
		XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;
		if (!module)
			return true;
		if (!expander || !expander->getXHost())
			return false; // disconnected (or blocked by the host type-lock) - nothing to control
		if (x8BrowsedParamTaken(module))
			return false; // bound to a different Expander, or a real cable - not ours to turn
		return channel < expander->getXKnobCount();
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
	isn't continuous (see each Widget's own visibility-toggle in step()). Uses SquareButton.svg
	(Dieter's own squared-off cap art, built directly in Inkscape from the real
	RoundSmallBlackKnob artwork) plus one square "LIGHT" indicator drawn on top in C++ - one
	visual mechanism, three timings:
	  - Toggle: click flips it, the light holds either state with no animation.
	  - Click: light flashes for a fixed duration regardless of hold time (the module's own
	    moduleProcess() owns this timing via a PulseGenerator - see requestXValueClick()).
	  - Push: light lit only while the mouse is actually held down.
	The light always just mirrors the underlying param's own current value (>0.5 = lit) - it
	never needs separate state, since all three interaction modes above drive that same value.
	No hover tooltip at all, per Dieter - a Toggle/Click/Push control's own lit/unlit state
	already shows everything there is to show; a raw 0/1 value readout is useless on top of that.
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

	// ParamWidget::onEnter() is what creates the tooltip - skipping it entirely (not calling the
	// base) suppresses it, same technique X8Knob's own comment describes for a different purpose.
	void onEnter(const EnterEvent &e) override {}

	// Same disconnected/taken/channel-limit gating as X8Knob - see its own comment for why each
	// case applies.
	bool isActive()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		Module *module = pq ? pq->module : nullptr;
		XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;
		if (!module)
			return true;
		if (!expander || !expander->getXHost())
			return false;
		if (x8BrowsedParamTaken(module))
			return false;
		return channel < expander->getXKnobCount();
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
		XExpanderInterface *expander = pq && pq->module ? dynamic_cast<XExpanderInterface*>(pq->module) : nullptr;
		drawThemeFrame(args, expander ? expander->getXStyle() : STYLE_ORANGE);

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
		XExpanderInterface *expander = pq && pq->module ? dynamic_cast<XExpanderInterface*>(pq->module) : nullptr;
		NVGcolor onColor = expander ? expander->getXBrowsedParamColor() : ORANGE;
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
			XExpanderInterface *expander = pq->module ? dynamic_cast<XExpanderInterface*>(pq->module) : nullptr;
			XParamType type = expander ? expander->getXBrowsedParamType() : X_PARAM_TOGGLE;
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
						if (expander)
							expander->requestXValueClick(channel);
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
	Module *module = nullptr;

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}

		XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;

		// No Host resolved (or blocked by the type-lock) - red placeholder, more attention-
		// grabbing than the "taken" grey since it means this specific Expander has physically
		// moved out of its chain (see the adjacency-gating feature) while still remembering a
		// locked type. Shows that locked host type name (e.g. "MORPH") if this Expander has ever
		// engaged with one, so the lock is visible, not just enforced silently - plain dashes if
		// never locked at all.
		const char *lockedType = expander ? expander->getXLockedHostType() : "";
		const char *text = (lockedType && lockedType[0] != '\0') ? lockedType : "-----";
		NVGcolor color = nvgRGB(0xdd, 0x00, 0x00); // red - no host / blocked by type-lock

		XHostInterface *xHost = expander ? expander->getXHost() : nullptr;
		if (xHost)
		{
			int count = xHost->getXParamCount();
			if (count > 0)
			{
				int idx = clamp(expander->getXBrowseIndex(), 0, count - 1);
				text = xHost->getXParamShortName(idx);
				bool mine = xHost->isXParamEngaged(idx) && xHost->getXParamBoundId(idx) == (int64_t) module->id;
				bool taken = (xHost->isXParamEngaged(idx) && !mine) || xHost->isXParamCableConnected(idx);
				NVGcolor slotColor = xHost->getXParamColor(idx);
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
		// only the full-width 5-char case actually needs the left edge as an anchor. See
		// X8_CENTER_OFFSET_MM's own comment above.
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float x = mm2px(X8_CENTER_OFFSET_MM[strlen(buffer)]);

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
