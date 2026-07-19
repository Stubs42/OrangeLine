/*
	XShared.hpp

	Author: Dieter Stubler

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
#ifndef X_SHARED_HPP
#define X_SHARED_HPP

#include "OrangeLine.hpp"

/*
	The X family (X8/X8D/X16/X16D) is a generic "param-access" Expander: any Host module with
	polyphonic param-paired inputs can let an X unit remote-control one of them per-channel. See
	ExpanderParamAccessSpec.md at the repo root for the full design (current as of commit
	aa6f650 - "virtual poly cable", multi-binding per Expander, Track & Hold, unfiltered but
	color-coded browse list).

	Unlike LANES (Lanes.hpp/LanesShared.hpp), a Host is only ever recognized attached to an
	Expander's own RIGHT side - these are inputs, and panel signal flow runs left to right, so
	control-from-outside sits upstream/left of the module it feeds. That makes the whole chain a
	strict singly-linked list (Expander -> Expander -> ... -> Host, walking rightward), so unlike
	LANES there is no possible fork and therefore no conflict/ambiguity case to detect at all.

	Binding (which Expander currently drives which candidate param) lives entirely on the Host
	side as a stable Rack module ID per param - nothing here needs to know about it beyond the
	passive getters below, which the Host reads during its own process(). The Expander never
	calls into the Host.
*/
enum XParamType {
	X_PARAM_CONTINUOUS,
	X_PARAM_TOGGLE,      // click flips state, stays until clicked again
	X_PARAM_CLICK,       // single fixed-length pulse fired on click, independent of hold duration
	X_PARAM_PUSH         // value is high only while the control is actively held down - named
	                     // "Push" (not "Momentary") to stay consistent with Dieter's YATOF
	                     // project terminology
};

// Text alignment for a numeric display (X8D/X16D's per-channel value readout) - a per-candidate
// property, same idea as getXParamColor(), since different value shapes read better differently
// (e.g. a signed float vs. a plain step count).
enum XAlign {
	X_ALIGN_LEFT,
	X_ALIGN_CENTER,
	X_ALIGN_RIGHT
};

struct XHostInterface
{
	virtual int getXParamCount() = 0;
	virtual const char* getXParamName(int index) = 0;      // full descriptive name
	// Compact name - X8 always displays THIS, never getXParamName(). Hard contract: max 5
	// characters, no exceptions - X8's name display is sized/laid out for exactly that width
	// and does not truncate or scroll. Every Host implementation must respect this.
	virtual const char* getXParamShortName(int index) = 0;
	virtual XParamType getXParamType(int index) = 0;
	// How X8D/X16D should align this candidate's numeric display text - continuous types only,
	// same reasoning as formatXParamValue() below (a digital type has no numeric display at all).
	virtual XAlign getXParamAlign(int index) = 0;
	virtual NVGcolor getXParamColor(int index) = 0;

	// Red/Green - derived from the binding, not separately stored: true iff this
	// param currently has a bound Expander. Also read directly by Expanders to
	// color their own (unfiltered) browse list.
	virtual bool isXParamEngaged(int index) = 0;

	// Which module currently holds the binding for this param, or -1. An Expander
	// compares this against its own Rack-native `id` to know whether it's the one.
	virtual int64_t getXParamBoundId(int index) = 0;

	// True while a real cable is patched into the host's own poly CV jack for this
	// param - overrides everything else, see "Real-cable override" in the spec.
	virtual bool isXParamCableConnected(int index) = 0;

	// Right-click action: clears the binding for this param -> Red.
	virtual void resetXParam(int index) = 0;

	// "The value this Host was actually using for this channel", in this candidate's own
	// human-readable DISPLAY units (e.g. steps for a loop-length candidate, percent for a
	// probability candidate) - the SAME units the Expander's own knob directly holds (see
	// XExpanderInterface::getXBrowsedParamDisplayMin/Max/Default() and moduleProcess()'s own
	// range-reconfiguration). An Expander reads this whenever it arrives on this candidate
	// (a fresh bind, or navigating back onto one - see isXKnobReady()) and sets its own knob(s)
	// to match, so engaging doesn't change anything the Host is currently outputting. A Host with
	// no meaningful scaling for a given candidate (e.g. a push/click/toggle type) can just return 0.
	virtual float getXParamTakeoverValue(int index, int channel) = 0;

	// Converts an Expander's own knob value (already in this candidate's own display units,
	// e.g. steps, percent, volts - see getXParamDisplayMin/Max() below) into whatever value THIS
	// Host's own poly input actually expects to receive for this candidate in CV/cable units -
	// i.e. exactly what a real patch cable would have to deliver to produce the same result. The
	// Expander itself has no idea about any Host's own CV scaling convention, so the Host must
	// apply this conversion itself whenever it copies a bound Expander's knob value into its own
	// live poly-input state - see Morpheus.cpp's moduleProcess() refresh loop for the call site.
	// continuous types only; a Host can just return the value unchanged for a digital
	// (Toggle/Click/Push) candidate, since those are read via a simple threshold, not a scaled
	// real-world unit.
	virtual float scaleXParamValue(int index, float displayValue) = 0;

	virtual std::string formatXParamValue(int index, float value) = 0; // continuous only - value
	                                                                     // already in display units

	// This candidate's own human-readable display range/behavior - the Expander's own knob gets
	// dynamically reconfigured (minValue/maxValue/defaultValue/snapEnabled) to exactly this
	// range whenever it's the currently-browsed candidate (see moduleProcess()'s own
	// range-reconfiguration and X8Knob::step()'s own unit sync), so Rack's built-in hover tooltip
	// and right-click "Enter value" editing both work correctly with zero custom parsing needed.
	// Meaningless for digital (Toggle/Click/Push) candidates - a Host can just return the same
	// harmless (0, 1, 0.5, false, "") defaults X8ModuleCommon.hpp's own disconnected fallback uses.
	virtual float getXParamDisplayMin(int index) = 0;
	virtual float getXParamDisplayMax(int index) = 0;
	virtual float getXParamDisplayDefault(int index) = 0;
	// Rounds the knob to the nearest whole display unit (Rack's own ParamQuantity::snapEnabled) -
	// only meaningful once minValue/maxValue directly span the display range, which the
	// reconfiguration above already ensures.
	virtual bool getXParamSnap(int index) = 0;
	// Rack's own separate ParamQuantity::unit field (e.g. "%") - deliberately never baked into
	// formatXParamValue()'s own returned string, since Rack's built-in value-entry parses that
	// string as a tinyexpr math expression and "%" is a registered (modulo) operator there.
	virtual const char* getXParamUnit(int index) = 0;

	// This module's own STYLE_JSON value (STYLE_ORANGE/BRIGHT/DARK) - purely for the cosmetic
	// seam-bridging strip (see getXNeighborStyle() below), unrelated to host-resolution health.
	virtual float getXStyle() = 0;

	// Optional user-editable display label for this Host instance (e.g. Morpheus's own
	// right-click "Name" field) - purely cosmetic, so an Expander's own bind/connection menu
	// items can tell apart several same-type Hosts by name rather than just showing the module
	// type over and over. May be empty (no custom name set) - callers fall back to the module's
	// own Model::slug plus its Rack id in that case, see addXBindMenuItems() in X8Common.hpp.
	virtual std::string getXHostName() = 0;

	virtual ~XHostInterface() {}
};

struct XExpanderInterface
{
	// The Host pointer this Expander itself resolved THIS TICK, fresh, from a stable id
	// (getXBoundHostId() below) - never cached as a raw pointer across ticks, so there is nothing
	// that can ever dangle even if the Host is deleted without warning. Falls back to whatever's
	// genuinely adjacent right now if not bound anywhere (browsing purposes only). Lets a further
	// Expander chained to this one's own left relay through it.
	virtual XHostInterface* getXHost() = 0;
	// Pushed directly by whichever Host currently binds this Expander, at the exact moment the
	// binding is granted or revoked (-1 = revoked/not bound) - a stable module id, never a raw
	// pointer, so it's always safe to hold onto: resolving it via APP->engine->getModule() just
	// returns nullptr if the target is gone, rather than risking a dangling reference. Both sides
	// also proactively clear this via their own onRemove() the instant either one is deleted (see
	// Morpheus.cpp's own onRemove()/resetXParam() and this interface's own concrete
	// implementation's onRemove()) - so a stale id should never even be observed in practice; this
	// is what makes it safe to trust without re-verifying against the whole rack every tick.
	// Binding changes are rare and Host-initiated (a user click, not continuous), so push is both
	// cheaper and avoids querying the whole rack ~1000x/second per Expander instance purely to
	// answer a question that only changes a few times per session. See CLAUDE.md's own Pitfalls
	// entry on this for the fuller reasoning, including why the earlier scan-based approach was
	// deliberately chosen first (structurally can't go stale) before being replaced here (push is
	// only as correct as its own discipline - every boundExpanderId mutation on the Host side
	// must route through a call that also pushes, see Morpheus.cpp's own resetXParam()).
	virtual void setXBoundHostId(int64_t hostId) = 0;
	virtual int64_t getXBoundHostId() = 0;
	// See XHostInterface::getXStyle() above.
	virtual float getXStyle() = 0;

	// Fully self-managed by the Expander (own debounce/edge-detection, own browse-index
	// bookkeeping) - the Host only ever reads these, during its OWN process(). The
	// Expander never calls back into the Host with them.
	virtual int getXKnobCount() = 0;              // up to 8 (X8/X8D) or 16 (X16/X16D) - the
	                                               // Expander's own "Channels" right-click
	                                               // menu can reduce this; it's the sender in
	                                               // the virtual-cable metaphor, so it alone
	                                               // decides how many channels it feeds in
	virtual float getXKnobValue(int channel) = 0; // one-line passthrough into OL_state -
	                                               // no separate storage needed
	virtual int getXBrowseIndex() = 0;             // which host param this Expander's
	                                                // controls currently point at - never
	                                                // locked, freely navigable
	virtual bool consumeEngagePress() = 0;         // one-shot: true exactly once per
	                                                // physical click, debounced locally -
	                                                // the Host decides what it means

	// The four methods below resolve "whatever the resolved Host reports for the currently
	// browsed param", each falling back to a sane default when no Host is resolved (or it has
	// zero candidates) - see any concrete Expander's own implementation for the exact resolve-
	// and-clamp logic. Promoted to the interface (rather than living only on X8/X8D directly) so
	// shared widget code (X8Common.hpp) can call them through a plain XExpanderInterface*,
	// without needing to know or dynamic_cast to the concrete Expander type at all - this is what
	// makes X8 and X8D able to share their entire widget/control implementation verbatim.
	virtual XParamType getXBrowsedParamType() = 0;
	virtual NVGcolor getXBrowsedParamColor() = 0;
	virtual XAlign getXBrowsedParamAlign() = 0;
	// Formats a value (already in the browsed candidate's own display units) as the Host would
	// display it - see XHostInterface::formatXParamValue(). Empty string when nothing meaningful
	// is resolved, or for a digital-type param (its own lit/unlit state already shows everything).
	virtual std::string formatXValue(float value) = 0;
	// The browsed candidate's own human-readable display range/behavior - see
	// XHostInterface::getXParamDisplayMin()'s own comment for the full reasoning. Same
	// resolve-Host-and-clamp-index-with-sane-fallback shape as getXBrowsedParamType()/Color()/
	// Align() above; fallback (0, 1, 0.5, false, "") when nothing meaningful is resolved matches
	// exactly what a disconnected X8's own knob already looks/behaves like.
	virtual float getXBrowsedParamDisplayMin() = 0;
	virtual float getXBrowsedParamDisplayMax() = 0;
	virtual float getXBrowsedParamDisplayDefault() = 0;
	virtual bool getXBrowsedParamSnap() = 0;
	virtual const char* getXBrowsedParamUnit() = 0;

	// One-shot request: the currently-browsed param is a Click type and this channel was just
	// clicked - the Expander owns the actual pulse timing (a fixed duration independent of how
	// long the mouse stays down), this just flags "start one now."
	virtual void requestXValueClick(int channel) = 0;

	// Live "how many hosts, how many total slots" summary - shown on the name display while
	// disconnected (see X8NameDisplay). Since an Expander id may now be bound to at most one
	// candidate, on one Host, anywhere in the rack (see xUnbindExpanderEverywhere(), XShared.hpp),
	// both hostCount and slotCount are always 0 or 1 by construction - kept as counts (not a
	// plain bool) rather than reshaping this method's own signature, since callers already treat
	// them as counts and a stale saved patch could theoretically still have more than one until
	// its first tick re-enforces the invariant. Recomputed fresh every call by scanning every
	// module currently in the rack (Engine::getModuleIds()) for anything implementing
	// XHostInterface and asking each one directly - nothing is remembered between calls, so
	// there's no persisted/stale state at all, and a host that's deleted or has since disengaged
	// everything just contributes 0.
	virtual void getXEngagedSummary(int &hostCount, int &slotCount) = 0;

	// True exactly when this Expander's own knob(s) are known to correctly reflect the Host's
	// held value for candidate `index` right now - i.e. safe for a Host to read
	// getXKnobValue()/getXKnobCount() for that candidate this tick. False for as long as a
	// resync is still pending (right after browsing back onto an already-bound candidate, or a
	// fresh engage) - the Expander clears this the instant it notices its own knob no longer
	// matches `index`, and only sets it again once it has actually pulled the Host's current
	// value into its own knob. A Host must treat false exactly like "not adjacent" (freeze, keep
	// holding its own last value) - this is a plain level check, not a fixed tick count, since
	// Rack's own engine does not guarantee any particular relative ordering between an
	// Expander's and a Host's own, independently-throttled process() calls (see CLAUDE.md's own
	// pitfall entry on the fixed-tick-count approach this replaces).
	virtual bool isXKnobReady(int index) = 0;

	virtual ~XExpanderInterface() {}
};

/**
	Resolves the Host reachable through a given immediate neighbor (always
	rightExpander.module for an X-family module - never leftExpander.module, see the
	left-only-attachment note above), or nullptr if that neighbor isn't part of the X family at
	all or doesn't (yet) reach a Host.
*/
inline XHostInterface* resolveXHost(Module *neighbor)
{
	if (!neighbor)
		return nullptr;
	XHostInterface *host = dynamic_cast<XHostInterface*>(neighbor);
	if (host)
		return host;
	XExpanderInterface *link = dynamic_cast<XExpanderInterface*>(neighbor);
	if (link)
		return link->getXHost();
	return nullptr;
}

/**
	Finds the module id of whichever Host currently binds a given Expander id, anywhere in the
	rack - a LAST-RESORT fallback, meant to run at most ONCE per module lifetime (right after a
	fresh patch load, when a binding can already be restored as bound without any attach/push
	event ever having fired this session - see X8ModuleCommon.hpp's own moduleProcess()). Normal,
	live operation never calls this at all: bindings are pushed directly by the Host the instant
	they're granted/revoked (XExpanderInterface::setXBoundHostId()), and both sides proactively
	clear their own reference via onRemove() the instant either one is deleted - so there is
	nothing to rediscover outside the one-time post-load case. Kept as a full-rack scan (not
	something cheaper) specifically because it's now rare enough that the cost doesn't matter -
	see CLAUDE.md's own Pitfalls entry on "resolve via push/id, not a per-tick scan" for the
	fuller history of why this was originally the ONLY mechanism, and why that turned out to be
	needlessly expensive for how rarely a binding actually changes. Returns -1 if nothing
	currently binds this Expander id anywhere (never bound, or the Host that had it bound was
	deleted/reset without the id having been persisted either) - the caller then falls back to
	plain physical adjacency for browsing purposes only.
*/
inline int64_t findXBoundHostId(int64_t expanderId)
{
	for (int64_t id : APP->engine->getModuleIds())
	{
		Module *m = APP->engine->getModule(id);
		XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
		if (!host)
			continue;
		int count = host->getXParamCount();
		for (int i = 0; i < count; i++)
			if (host->getXParamBoundId(i) == expanderId)
				return id;
	}
	return -1;
}

/**
	Clears every X-family binding for a given Expander id, anywhere in the rack, on every Host
	that currently holds it - enforces the "one candidate, one Host, at a time" invariant (an
	Expander's own connection to a Host can now persist across non-adjacency - see
	X8ModuleCommon.hpp's own moduleProcess() - so binding to a new candidate must first release
	any existing one, everywhere, rather than relying on physical adjacency to arbitrate which
	binding is "live" the way the old multi-host relay technique did). Used directly by
	X8Common.hpp's own XUnbindAllItem menu action, and by Morpheus.cpp's own bind-granting branch
	(called BEFORE setting the new boundExpanderId, while the target slot is still -1, so there's
	no need for a separate "except this one" parameter - the scan simply can't find a match there
	yet).
*/
inline void xUnbindExpanderEverywhere(int64_t expanderId)
{
	for (int64_t id : APP->engine->getModuleIds())
	{
		Module *m = APP->engine->getModule(id);
		XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
		if (!host)
			continue;
		int count = host->getXParamCount();
		for (int i = 0; i < count; i++)
			if (host->getXParamBoundId(i) == expanderId)
				host->resetXParam(i);
	}
}

/**
	Returns the X-family theme (STYLE_ORANGE/BRIGHT/DARK) of a given immediate neighbor, or -1
	if that neighbor isn't part of the X family at all. Purely for the seamless panel-merge
	strip - independent of host-resolution health, same reasoning as LANES' own
	getLanesNeighborStyle().
*/
inline float getXNeighborStyle(Module *neighbor)
{
	if (!neighbor)
		return -1.f;
	XHostInterface *host = dynamic_cast<XHostInterface*>(neighbor);
	if (host)
		return host->getXStyle();
	XExpanderInterface *link = dynamic_cast<XExpanderInterface*>(neighbor);
	if (link)
		return link->getXStyle();
	return -1.f;
}

// Panel background per theme (Dieter's Colors.txt) - same values as LanesShared.hpp's own
// EXT_STRIP_BG_* (kept as a separate copy here rather than shared/included, so the X family
// stays fully decoupled from LANES-specific internals).
#define X_STRIP_BG_ORANGE nvgRGB(0x15, 0x15, 0x2b)
#define X_STRIP_BG_DARK   nvgRGB(0x20, 0x20, 0x20)
#define X_STRIP_BG_BRIGHT nvgRGB(0xe6, 0xe6, 0xe6)

// Every other hardcoded color used by the shared X-family widgets in X8Common.hpp - collected
// here (rather than left as scattered nvgRGB() literals) so a future palette tweak only ever
// touches one place. Grouped by meaning, not by which widget happens to use them - several
// widgets share the exact same semantic color (e.g. "inactive/taken" grey).
#define X_BUTTON_FILL_ORANGE nvgRGB(0x10, 0x06, 0x00) // X8ButtonBase's own button-body fill, per
#define X_BUTTON_FILL_DARK   nvgRGB(0x17, 0x17, 0x17) // theme - a separate triplet from
#define X_BUTTON_FILL_BRIGHT nvgRGB(0xbb, 0xbb, 0xbb) // X_STRIP_BG_* even though BRIGHT's own
//#define X_BUTTON_FILL_BRIGHT nvgRGB(0x15, 0x15, 0x2b) // X_STRIP_BG_* even though BRIGHT's own
                                                       // value happens to coincide with
                                                       // X_STRIP_BG_ORANGE (a dark navy fill
                                                       // reads correctly against a light panel)

#define X_FRAME_ORANGE nvgRGB(0x80, 0x33, 0x00) // X8ValueButton's drawThemeFrame() stroke color,
#define X_FRAME_DARK   nvgRGB(0x60, 0x60, 0x60) // per theme (its background fill reuses
#define X_FRAME_BRIGHT nvgRGB(0x60, 0x60, 0x80) // X_STRIP_BG_* directly, no separate constant
                                                 // needed for that half)

// Per-theme accent color for X8ButtonBase/X8BindButtonBase/XOButtonBase's own frame-stroke+label
// while active, replacing a fixed `ORANGE` that only coincidentally matched the Orange theme's
// own value and stayed wrong (still bright orange, never themed) under Dark/Bright. Orange/Dark
// match tools/bake_panel_theme.py's own THEME_TEXT_COLOR (#ff6600/#c4bac4) - Bright deliberately
// does NOT (THEME_TEXT_COLOR's Bright value, #15152b, is meant for text drawn directly on the
// panel's own light background, not for an accent against this button's own dark "display" fill,
// X_BUTTON_FILL_BRIGHT - which is that exact same #15152b, making frame+label invisible against
// their own background). Reuses X_FRAME_BRIGHT (THEME_FRAME_COLOR's Bright value, #606080)
// instead - already the established, confirmed-visible accent color for this family's other
// controls against the same kind of dark display-style surface.
#define X_TEXT_COLOR_ORANGE nvgRGB(0xff, 0x66, 0x00)
#define X_TEXT_COLOR_DARK   nvgRGB(0xc4, 0xba, 0xc4)

// Shared by X8ButtonBase/X8BindButtonBase (X8Common.hpp) and XOButtonBase (XOCommon.hpp) - the
// themed accent color their frame stroke and label text both use while active.
inline NVGcolor xThemedTextColor(float style)
{
	return (style == STYLE_DARK) ? X_TEXT_COLOR_DARK
	     : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT
	     : X_TEXT_COLOR_ORANGE;
}

#define X_COLOR_INACTIVE_GREY nvgRGB(0x55, 0x55, 0x55) // disconnected/taken/unavailable - shared
                                                        // by X8ButtonBase's default accent,
                                                        // X8BindButtonBase's grey state, and
                                                        // X8NameDisplay's "taken" text color
#define X_COLOR_BOUND_RED   nvgRGB(0xdd, 0x00, 0x00)   // X8BindButtonBase - bound to THIS
                                                        // Expander at the browsed slot right now
                                                        // (was green - Dieter's own call)
#define X_COLOR_NO_HOST_RED nvgRGB(0xdd, 0x00, 0x00)   // X8NameDisplay - no Host resolved at all
#define X_VALUE_LIGHT_UNLIT nvgRGB(0x4a, 0x44, 0x3c)   // X8ValueButton's own square LIGHT
                                                        // indicator while unlit

#define X_STRIP_ACCENT_Y_MM 124.71525f
#define X_STRIP_ACCENT_THICKNESS_MM 0.3f

// How far past the bare seam-bridging sliver the whole strip (background fill AND the orange
// accent line together) reaches into EACH of the two neighboring panels - a first-guess tuning
// value (2026-07-16), not yet visually confirmed in Rack. Widen/narrow freely; Dieter reviews
// this live since it's a pixel-perfect panel-art judgement call, not something derivable from
// the code alone. See CLAUDE.md's Expander-modules section for the open coverage question this
// addresses (the accent line needs to visibly continue the panel's own orange line motif on
// both sides, not just bridge the physical gap between the two modules).
#define X_STRIP_SEEM_WIDTH_MM 2.f //1.524f
#define X_STRIP_LINE_ADD_MM   1.f

/**
	Seamless panel-merge strip 
*/
struct XExtStripWidget : Widget
{
	int style = STYLE_ORANGE;
	float topInsetMm = 0.f;
	// true for the Host's own left-edge strip (addXExtStripLeft) - mirrors which direction the
	// background's small alignment offset points, so the same draw() shape works for both the
	// Expander's right-edge strip and the Host's left-edge one without duplicating the logic.
	bool mirror = false;

	void draw(const DrawArgs &args) override
	{
		if (!visible)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, mirror ? -X_STRIP_LINE_ADD_MM : X_STRIP_LINE_ADD_MM, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, (style == STYLE_ORANGE) ? X_STRIP_BG_ORANGE : (style == STYLE_DARK) ? X_STRIP_BG_DARK : X_STRIP_BG_BRIGHT);
		nvgFill(args.vg);

		float accentLocalY = mm2px(X_STRIP_ACCENT_Y_MM - topInsetMm);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, accentLocalY - mm2px(X_STRIP_ACCENT_THICKNESS_MM) / 2.f, box.size.x, mm2px(X_STRIP_ACCENT_THICKNESS_MM));
		nvgFillColor(args.vg, ORANGE);
		nvgFill(args.vg);
	}
};

inline XExtStripWidget* addXExtStrip(ModuleWidget *w, float panelWidthMm)
{
	XExtStripWidget *strip = new XExtStripWidget();
	strip->box.pos = mm2px(Vec(panelWidthMm - X_STRIP_SEEM_WIDTH_MM / 2.f , 0.35f));
	strip->box.size = mm2px(Vec(X_STRIP_SEEM_WIDTH_MM + X_STRIP_LINE_ADD_MM, PANELHEIGHT - 0.5f));
	strip->topInsetMm = 0.35f;
	strip->visible = false;
	w->addChild(strip);
	return strip;
}

/**
	Called every widget step() (UI frame rate, cosmetic only). `self` is the widget's own
	module - works even though it's a generic Module* because every X-family module already
	implements XHostInterface or XExpanderInterface (that's what getXNeighborStyle() dynamic_casts
	against).
*/
inline void updateXExtStrip(XExtStripWidget *strip, Module *self, Module *rightNeighbor)
{
	float myStyle = getXNeighborStyle(self);
	if (myStyle < 0.f)
		return;
	float rightStyle = getXNeighborStyle(rightNeighbor);
	strip->style = (int) myStyle;
	strip->visible = (rightStyle >= 0.f) && (rightStyle == myStyle);
}

/**
	Host-side counterpart of addXExtStrip() - a mirror image around a module's own LEFT edge
	(x=0) instead of the right edge (panelWidthMm), same total width/constants. Needed because
	Rack clips a widget's rendering to its own ModuleWidget's bounds: the Expander's right-edge
	strip can only ever cover pixels within the Expander's own panel, never the Host's, so the
	seam only disappears if the Host draws its own matching strip on its own left edge too - see
	CLAUDE.md's Expander-modules section.
*/
inline XExtStripWidget* addXExtStripLeft(ModuleWidget *w)
{
	XExtStripWidget *strip = new XExtStripWidget();
	strip->box.pos = mm2px(Vec(-X_STRIP_SEEM_WIDTH_MM / 2.f - X_STRIP_LINE_ADD_MM, 0.35f));
	strip->box.size = mm2px(Vec(X_STRIP_SEEM_WIDTH_MM + X_STRIP_LINE_ADD_MM, PANELHEIGHT - 0.5f));
	strip->topInsetMm = 0.35f;
	strip->mirror = true;
	strip->visible = false;
	w->addChild(strip);
	return strip;
}

// See updateXExtStrip() above - identical logic, just checking the LEFT neighbor instead.
inline void updateXExtStripLeft(XExtStripWidget *strip, Module *self, Module *leftNeighbor)
{
	float myStyle = getXNeighborStyle(self);
	if (myStyle < 0.f)
		return;
	float leftStyle = getXNeighborStyle(leftNeighbor);
	strip->style = (int) myStyle;
	strip->visible = (leftStyle >= 0.f) && (leftStyle == myStyle);
}

#endif
