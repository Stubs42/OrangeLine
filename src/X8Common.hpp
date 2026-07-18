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
	the sized X8StepButton/X8BindButton subclasses (only the BIND-specific isActive() logic is
	shared, via X8BindButtonBase below), X8Widget/X8DWidget themselves, and X8D's own
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
		NVGcolor fill = (style == STYLE_DARK) ? X_STRIP_BG_DARK
		              : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT
		              : X_STRIP_BG_ORANGE;
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
	cover1->module = w->module; // was never set at all - draw() then always saw a null module,
	                            // silently falling back to STYLE_ORANGE's own dark background
	                            // regardless of the module's actual selected style
	cover1->box.pos = calculateCoordinates(panelWidthMm / 2.f - X8_LOGO_COVER1_WIDTH_MM / 2.f, X8_LOGO_COVER1_Y_MM, 0.f);
	cover1->box.size = mm2px(Vec(X8_LOGO_COVER1_WIDTH_MM, X8_LOGO_COVER1_HEIGHT_MM));
	cover1->visible = false;
	w->addChild(cover1);
	*cover1Out = cover1;

	X8LogoCover *cover2 = new X8LogoCover();
	cover2->module = w->module;
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
	XHostInterface *host = expander ? expander->getXHost() : nullptr;
	// Only hide the logo when the connected Host's own theme actually MATCHES this module's own -
	// mirrors the seam-strip's own "only merge when themes match" rule (see XOCommon.hpp's own
	// updateXOLogoCovers(), fixed the same way and for the same reason).
	bool connected = host && host->getXStyle() == expander->getXStyle();
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

// Shared by X8BindButtonBase/X8Knob/X8ValueButton below: is the currently-browsed param
// actually bound to THIS Expander right now? Only this state means the knob/button's own value
// is genuinely being read by the Host - an available-but-unbound slot never reads the raw knob
// at all until Engage is pressed (see the takeover-value mechanism in Morpheus.cpp's own
// moduleProcess()), so "available" alone is NOT enough to call a control active/live - it would
// otherwise let the knob be turned while doing nothing at all, silently. Takes a plain Module*
// (works for X8/X8D/X16/X16D transparently) rather than a concrete Expander type.
static bool x8BrowsedParamMine(Module *module)
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
	return xHost->isXParamEngaged(idx) && xHost->getXParamBoundId(idx) == (int64_t) module->id;
}

// Is the currently-browsed param bound to a *different* Expander, or is a real cable connected
// on the Host's own jack for it? Either way this Expander has no control over it right now,
// regardless of the Channels limit or connection state otherwise - matches
// ExpanderParamAccessSpec.md's "taken/unavailable" state (same computation X8NameDisplay's own
// color-coding already does, kept separate there since it also needs "mine").
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
	return (xHost->isXParamEngaged(idx) && !x8BrowsedParamMine(module)) || xHost->isXParamCableConnected(idx);
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

	// No function at all while disconnected (no Host resolved) - LEFT/RIGHT/ENGAGE have nothing
	// to step through or bind when there's nobody to browse. No module context at all (e.g. the
	// module browser's preview) defaults active.
	// Virtual so X8BindButtonBase can add its own extra "taken" condition on top - see there.
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

	// Stroke/label color - split out from draw() as its own virtual so X8BindButtonBase can
	// add a third, visually distinct "currently bound here" state (green) on top of the plain
	// active/inactive (orange/grey) look every other button uses unchanged.
	virtual NVGcolor getAccentColor()
	{
		return isActive() ? ORANGE : X_COLOR_INACTIVE_GREY;
	}

	// Displayed text - split out from draw() as its own virtual so X8BindButtonBase can show
	// "BIND"/"UNBIND" depending on state instead of the fixed `label` every other button uses.
	virtual std::string getLabel() { return label; }

	void draw(const DrawArgs &args) override
	{
		float style = STYLE_ORANGE;
		engine::ParamQuantity *pq = getParamQuantity();
		XExpanderInterface *expander = pq && pq->module ? dynamic_cast<XExpanderInterface*>(pq->module) : nullptr;
		if (expander)
			style = expander->getXStyle();
		NVGcolor fill = (style == STYLE_DARK) ? X_BUTTON_FILL_DARK
		              : (style == STYLE_BRIGHT) ? X_BUTTON_FILL_BRIGHT
		              : X_BUTTON_FILL_ORANGE;
		NVGcolor accent = getAccentColor();

		float r = mm2px(Vec(0.529f, 0.f)).x;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, r);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, mm2px(Vec(0.3f, 0.f)).x);
		nvgStrokeColor(args.vg, accent);
		nvgStroke(args.vg);

		std::string text = getLabel();
		if (!text.empty())
		{
			// 8pt, per Dieter (matches the panel's own BUTTON_TEXT tspan override,
			// "2.82222px" - this SVG's viewBox makes 1 user unit == 1mm, so that raw number
			// already IS the mm size directly: 8pt * 0.3528 mm/pt == 2.82222mm).
			std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(Vec(2.82222f, 0.f)).x);
			nvgFillColor(args.vg, accent);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), nullptr);
		}
	}
};

// Also inert (grey) whenever the currently-browsed param is "taken" - a click there is a no-op
// anyway per the spec ("the Host decides... do nothing at all if the browsed param is grey").
// LEFT/RIGHT deliberately stay plain X8ButtonBase (active in this case) - browsing away from a
// taken slot must keep working (ExpanderParamAccessSpec.md: "browsing is never locked... stepping
// onto a grey entry is harmless read-only viewing, no restriction of any kind"). Each module's own
// .cpp adds only a one-line constructor setting box.size (BIND's size differs between X8/X8D).
struct X8BindButtonBase : X8ButtonBase
{
	bool isActive() override
	{
		if (!X8ButtonBase::isActive())
			return false;
		engine::ParamQuantity *pq = getParamQuantity();
		return !x8BrowsedParamTaken(pq ? pq->module : nullptr);
	}

	// Red (not just plain orange) while the currently-browsed slot is actually bound to THIS
	// Expander - makes "am I bound here right now" readable at a glance, not just inferrable
	// from the knobs/buttons no longer being dimmed. Still plain grey whenever inactive
	// (disconnected or taken elsewhere - X8ButtonBase's own getAccentColor() default already
	// covers that case correctly, so only the active case needs a further distinction here).
	NVGcolor getAccentColor() override
	{
		if (!isActive())
			return X_COLOR_INACTIVE_GREY;
		engine::ParamQuantity *pq = getParamQuantity();
		if (x8BrowsedParamMine(pq ? pq->module : nullptr))
			return X_COLOR_BOUND_RED; // bound to us at this exact slot right now
		return ORANGE; // available - clickable to bind, but not bound to us yet
	}

	// "UNBIND" while bound here (clicking would release it), else the base "BIND" label
	// (unchanged whether available, taken, or disconnected - color already distinguishes those).
	std::string getLabel() override
	{
		engine::ParamQuantity *pq = getParamQuantity();
		if (x8BrowsedParamMine(pq ? pq->module : nullptr))
			return "UNBIND";
		return label;
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
	float lastValue = NAN;

	// SvgKnob caches its rotated needle in a FramebufferWidget and only re-rasterizes on its own
	// onChange() (normally dispatched by ParamWidget's own drag-driven interaction path) - a value
	// set directly by the module itself (e.g. the takeover resnap in X8ModuleCommon.hpp, when
	// browsing back onto an already-bound param) never goes through that path, so the cached
	// bitmap stays stale until some unrelated interaction (e.g. a click) happens to bust it. Force
	// a redraw here instead, by comparing against the engine value directly every UI frame -
	// robust regardless of what triggered the change.
	void step() override
	{
		engine::ParamQuantity *pq = getParamQuantity();
		if (pq)
		{
			float value = pq->getValue();
			if (value != lastValue)
			{
				lastValue = value;
				fb->dirty = true;
			}
		}
		RoundSmallBlackKnob::step();
	}

	bool isActive()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		Module *module = pq ? pq->module : nullptr;
		XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;
		if (!module)
			return true;
		if (!expander || !expander->getXHost())
			return false; // disconnected - nothing to control
		if (!x8BrowsedParamMine(module))
			return false; // not engaged at this exact slot yet - the Host never reads this knob's
			              // raw value until Engage is pressed, so turning it now would be a
			              // silent no-op; merely "available" (nobody else holds it) isn't enough
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

	// Same disconnected/mine/channel-limit gating as X8Knob - see its own comment for why each
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
		if (!x8BrowsedParamMine(module))
			return false;
		return channel < expander->getXKnobCount();
	}

	bool isPressed()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		return pq && pq->getValue() > 0.5f;
	}

	// Tracks the physical mouse hold, independent of the underlying param value - a Click type's
	// own value is a short fixed pulse (X_VALUE_CLICK_SECONDS), so isPressed() alone would let go
	// of the "pressed" look well before the user actually releases the mouse on a longer hold.
	// Set in onButton() below, used by draw()'s mirror effect so the cap still reads as held down
	// for the whole physical press, "for reality feeling" (Dieter).
	bool mouseHeld = false;

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
			case STYLE_DARK:   background = X_STRIP_BG_DARK;   frame = X_FRAME_DARK;   break;
			case STYLE_BRIGHT: background = X_STRIP_BG_BRIGHT; frame = X_FRAME_BRIGHT; break;
			default:           background = X_STRIP_BG_ORANGE; frame = X_FRAME_ORANGE; break; // STYLE_ORANGE
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

		bool active = isActive();
		nvgSave(args.vg);
		if (!active)
			nvgGlobalAlpha(args.vg, 0.3f);
		// Mirror axis is horizontal (top and bottom swap) while lit/held OR physically held down -
		// unlike the earlier abandoned attempt at this same flip (wrong pivot point, hunted
		// against the OLD cap's asymmetric bounding box), the new SquareButton.svg cap is
		// symmetric top-to-bottom, so flipping around the box's own plain geometric center
		// (box.size.y/2) needs no separately measured pivot at all. mouseHeld (not just
		// isPressed()) so a Click's own short pulse value doesn't let go of the "pressed" look
		// before the user actually releases the mouse on a longer hold - purely a display
		// decision, doesn't touch the value/pulse timing itself at all.
		if (active && (isPressed() || mouseHeld))
		{
			nvgTranslate(args.vg, 0.f, box.size.y);
			nvgScale(args.vg, 1.f, -1.f);
		}
		Widget::draw(args); // draws the cap child, simply centred - no offset needed
		if (!active)
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
		nvgStrokeColor(args.vg, (active && isPressed()) ? onColor : X_VALUE_LIGHT_UNLIT);
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
				mouseHeld = true; // display-only - see its own comment, no effect on the value/timing below
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
			else if (e.action == GLFW_RELEASE)
			{
				mouseHeld = false;
				if (type == X_PARAM_PUSH)
					pq->setValue(0.f);
			}
		}
		ParamWidget::onButton(e);
	}
};

// Left/right margin this display's own box.pos/box.size are measured against in every widget
// constructor (X8/X8D/X16/X16D all position it at this same x offset from their own left edge) -
// shared here so the "how much room do we actually have" math below (widen for X8D/X16/X16D,
// leave X8 at its own hand-tuned 13mm untouched) stays in one place.
#define X8_NAME_DISPLAY_MARGIN_MM 1.41287f

/**
	Currently-browsed param name, LCD-style like every other OrangeLine display - color-coded per
	ExpanderParamAccessSpec.md's "Name display": grey dashes when no Host is resolved (doubles as
	the "not connected" indicator, replacing an earlier "XXXXX" placeholder that misleadingly
	looked like real content); otherwise green when this instance is the bound provider for the
	browsed param, grey when it's taken (bound elsewhere, or a real cable is patched in), orange
	when it's available (nobody holds it).

	Three display-width tiers, all handled by ONE generic "does it fit?" measurement
	(nvgTextBounds()) against this widget's own box.size.x - no per-module special-casing
	anywhere, the tiering falls out purely from each widget's own box width (Dieter's own
	breakdown): X8's narrow ~13mm display always shows getXParamShortName() centered (or just
	OL_GREETING_WORD1 while unbound) - long content never fits there. X8D/X16's wider ~27mm
	display shows the full getXParamName() left-aligned when it fits, else falls back to
	getXParamShortName() centered (or the full "WORD1 WORD2" greeting, else just WORD1, while
	unbound). X16D's widest ~58mm display uses the exact same logic, but its width means the long
	name fits for essentially every candidate - "mostly showing the Long name of a slot" in
	practice, not a separate code path.
*/
struct X8NameDisplay : TransparentWidget
{
	Module *module = nullptr;

	// True if `text` renders within this widget's own box width at the currently-set font/size -
	// requires the font face/size to already be set on `vg` (drawLayer() does this before any
	// call here).
	bool fits(NVGcontext *vg, const std::string &text)
	{
		float bounds[4];
		nvgTextBounds(vg, 0.f, 0.f, text.c_str(), nullptr, bounds);
		return (bounds[2] - bounds[0]) <= box.size.x;
	}

	// Centers <=5-char content using the existing hand-tuned X8_CENTER_OFFSET_MM table (pixel-
	// identical to the previous behavior); anything longer (a long name, or the full two-word
	// greeting) is centered via a measured nvgTextBounds() width instead, since the hand-tuned
	// table only ever covered up to 5 characters.
	float centerX(NVGcontext *vg, const std::string &text)
	{
		if (text.size() <= 5)
			return mm2px(X8_CENTER_OFFSET_MM[text.size()]);
		float bounds[4];
		nvgTextBounds(vg, 0.f, 0.f, text.c_str(), nullptr, bounds);
		float x = (box.size.x - (bounds[2] - bounds[0])) / 2.f;
		return (x > 0.f) ? x : 0.f;
	}

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}

		XExpanderInterface *expander = module ? dynamic_cast<XExpanderInterface*>(module) : nullptr;

		float fontSizePx = mm2px(Vec(4.49792f, 0.f)).x;
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSizePx);

		// No Host resolved - show a live "HH:SS engaged" summary (HH = distinct hosts currently
		// bound to at least one slot, SS = total bound slots across all of them) instead of a
		// plain placeholder, using this otherwise-idle display slot - see
		// XExpanderInterface::getXEngagedSummary(). Falls back to a friendly greeting (see
		// OrangeLine.hpp's OL_GREETING_WORD1/2/LONG) rather than a plain "nothing here"
		// placeholder when nothing's bound anywhere at all - widest-fitting tier wins (X16D's own
		// generous width fits OL_GREETING_LONG, X8D/X16 fall back to WORD1+WORD2, X8's narrow
		// display falls back all the way to just WORD1). Orange (not the red used elsewhere for
		// "disconnected") since this is a welcome, not a problem.
		std::string text = OL_GREETING_WORD1;
		NVGcolor color = ORANGE;
		bool leftAlign = false;
		if (expander)
		{
			int hostCount = 0, slotCount = 0;
			expander->getXEngagedSummary(hostCount, slotCount);
			if (hostCount > 0)
			{
				char summaryBuffer[6];
				snprintf(summaryBuffer, sizeof(summaryBuffer), "%02d:%02d", clamp(hostCount, 0, 99), clamp(slotCount, 0, 99));
				text = summaryBuffer;
			}
			else if (fits(args.vg, OL_GREETING_LONG))
				text = OL_GREETING_LONG;
			else
			{
				std::string fullGreeting = std::string(OL_GREETING_WORD1) + " " + OL_GREETING_WORD2;
				if (fits(args.vg, fullGreeting))
					text = fullGreeting;
			}
		}

		XHostInterface *xHost = expander ? expander->getXHost() : nullptr;
		if (xHost)
		{
			int count = xHost->getXParamCount();
			if (count > 0)
			{
				int idx = clamp(expander->getXBrowseIndex(), 0, count - 1);
				bool mine = xHost->isXParamEngaged(idx) && xHost->getXParamBoundId(idx) == (int64_t) module->id;
				bool taken = (xHost->isXParamEngaged(idx) && !mine) || xHost->isXParamCableConnected(idx);
				NVGcolor slotColor = xHost->getXParamColor(idx);
				if (mine)
					color = slotColor; // this slot's own color, full brightness - it's bound to me
				else if (taken)
					color = X_COLOR_INACTIVE_GREY;
				else
					color = nvgLerpRGBA(slotColor, X_COLOR_INACTIVE_GREY, 0.5f); // available - same
					                                 // slot color, dimmed toward grey so "mine" still
					                                 // reads as visually distinct from "free to take"

				// Prefer the full descriptive name, left-aligned, whenever this display is wide
				// enough to show it without clipping - falls back to the short name, centered,
				// otherwise (X8's narrow display always takes this branch).
				std::string longName = xHost->getXParamName(idx);
				if (fits(args.vg, longName))
				{
					text = longName;
					leftAlign = true;
				}
				else
				{
					// Defense in depth beyond the documented contract: never even attempt to draw
					// more than 5 characters here - a Host violating getXParamShortName()'s own
					// 5-char limit still can't make text bleed outside this widget.
					char buffer[6];
					snprintf(buffer, sizeof(buffer), "%s", xHost->getXParamShortName(idx));
					text = buffer;
				}
			}
		}

		nvgFillColor(args.vg, color);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float x = leftAlign ? 0.f : centerX(args.vg, text);

		// Text is drawn at local x with baseline alignment, so glyphs extend upward (ascenders)
		// from y=0, not downward - box.size.y (an arbitrary hit-test size, not a text-metrics
		// box) doesn't describe where the glyphs actually render. Scissor a generous band around
		// the baseline instead of assuming (0,0,w,h) covers the glyphs.
		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, -fontSizePx * 1.2f, box.size.x, fontSizePx * 1.6f);
		nvgText(args.vg, x, 0.f, text.c_str(), nullptr);
		nvgRestore(args.vg);
		Widget::drawLayer(args, 1);
	}
};

// ********************************************************************************************
// Right-click "Binds" tree - see CLAUDE.md's submenu/radio-style item convention. Structure:
// Binds -> Unbind All (every host at once) + one entry per Host currently holding at least one
// of this Expander's slots -> that Host's own Unbind All + its individual bound slots
// (checkmark, click unbinds just that one). Everything below operates on a plain Module*
// (dynamic_cast to XExpanderInterface internally), so X8/X8D/X16/X16D all share this verbatim
// via one call in each of their own appendContextMenu() - see CLAUDE.md's "near-identical
// siblings" convention. "Bind"/"Unbind" replaces the earlier "Engage" wording throughout the
// user-facing text (Dieter: too nerdy) - only the display strings changed, the underlying
// interface methods (resetXParam() etc.) keep their original names.

// Leaf: one slot this Expander currently holds on a specific Host - always shown checked (this
// list only ever contains bound slots to begin with), click clears just this one.
struct XBindSlotItem : MenuItem
{
	XHostInterface *host;
	int index;
	void onAction(const event::Action &e) override { host->resetXParam(index); }
};

// Clears every slot this Expander holds on ONE specific Host (not the other Hosts it may also
// be bound to elsewhere) - the per-host counterpart of XUnbindAllItem below.
struct XUnbindHostItem : MenuItem
{
	XHostInterface *host;
	int64_t expanderId;
	void onAction(const event::Action &e) override
	{
		int count = host->getXParamCount();
		for (int i = 0; i < count; i++)
			if (host->getXParamBoundId(i) == expanderId)
				host->resetXParam(i);
	}
};

// One entry per bound Host - text is "<custom name> (<slug>)" if a name was set (see
// XHostInterface::getXHostName()), else "<slug> #<id>" so several unnamed same-type Hosts stay
// distinguishable. Opens to that Host's own Unbind All plus its individual bound slots.
struct XBindHostItem : MenuItem
{
	XHostInterface *host;
	int64_t expanderId;

	Menu *createChildMenu() override
	{
		Menu *menu = new Menu;

		XUnbindHostItem *allItem = new XUnbindHostItem();
		allItem->host = host;
		allItem->expanderId = expanderId;
		allItem->text = "Unbind All";
		allItem->setSize(Vec(140, 20));
		menu->addChild(allItem);

		int count = host->getXParamCount();
		for (int i = 0; i < count; i++)
		{
			if (host->getXParamBoundId(i) != expanderId)
				continue;
			XBindSlotItem *item = new XBindSlotItem();
			item->host = host;
			item->index = i;
			item->text = host->getXParamName(i);
			item->rightText = "✔";
			item->setSize(Vec(140, 20));
			menu->addChild(item);
		}
		return menu;
	}
};

// Clears every slot this Expander holds, on every Host it's currently bound to anywhere - same
// "blunt manual override" reasoning as Morpheus's own MorpheusUnbindAllItem, just scanning
// every Host in the rack instead of one fixed instance's own candidate list.
struct XUnbindAllItem : MenuItem
{
	Module *module;
	void onAction(const event::Action &e) override
	{
		int64_t myId = module->id;
		for (int64_t id : APP->engine->getModuleIds())
		{
			Module *m = APP->engine->getModule(id);
			XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
			if (!host)
				continue;
			int count = host->getXParamCount();
			for (int i = 0; i < count; i++)
				if (host->getXParamBoundId(i) == myId)
					host->resetXParam(i);
		}
	}
};

// Top-level "Binds" item - see this section's own header comment for the full tree shape.
struct XBindsItem : MenuItem
{
	Module *module;

	Menu *createChildMenu() override
	{
		Menu *menu = new Menu;
		int64_t myId = module->id;

		XUnbindAllItem *allItem = new XUnbindAllItem();
		allItem->module = module;
		allItem->text = "Unbind All";
		allItem->setSize(Vec(140, 20));
		menu->addChild(allItem);

		for (int64_t id : APP->engine->getModuleIds())
		{
			Module *m = APP->engine->getModule(id);
			XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
			if (!host)
				continue;
			int count = host->getXParamCount();
			int bound = 0;
			for (int i = 0; i < count; i++)
				if (host->getXParamBoundId(i) == myId)
					bound++;
			if (bound == 0)
				continue;

			std::string name = host->getXHostName();
			std::string label = name.empty()
				? string::f("%s #%lld", m->model->slug.c_str(), (long long) id)
				: string::f("%s (%s)", name.c_str(), m->model->slug.c_str());

			XBindHostItem *hostItem = new XBindHostItem();
			hostItem->host = host;
			hostItem->expanderId = myId;
			hostItem->text = label;
			hostItem->rightText = RIGHT_ARROW;
			hostItem->setSize(Vec(180, 20));
			menu->addChild(hostItem);
		}
		return menu;
	}
};

inline void addXBindsMenuItem(Menu *menu, Module *module)
{
	XBindsItem *item = new XBindsItem();
	item->module = module;
	item->text = "Binds";
	item->rightText = RIGHT_ARROW;
	menu->addChild(item);
}
