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

struct XHostInterface
{
	virtual int getXParamCount() = 0;
	virtual const char* getXParamName(int index) = 0;      // full descriptive name
	// Compact name - X8 always displays THIS, never getXParamName(). Hard contract: max 5
	// characters, no exceptions - X8's name display is sized/laid out for exactly that width
	// and does not truncate or scroll. Every Host implementation must respect this.
	virtual const char* getXParamShortName(int index) = 0;
	virtual XParamType getXParamType(int index) = 0;
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

	virtual std::string formatXParamValue(int index, float value) = 0; // continuous only

	// This module's own STYLE_JSON value (STYLE_ORANGE/BRIGHT/DARK) - purely for the cosmetic
	// seam-bridging strip (see getXNeighborStyle() below), unrelated to host-resolution health.
	virtual float getXStyle() = 0;

	virtual ~XHostInterface() {}
};

struct XExpanderInterface
{
	// The Host pointer this Expander itself resolved via its own right-side chain-walk, or
	// nullptr. Lets a further Expander chained to this one's own left relay through it.
	virtual XHostInterface* getXHost() = 0;
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

#define X_STRIP_ACCENT_Y_MM 124.71525f
#define X_STRIP_ACCENT_THICKNESS_MM 0.3f

/**
	Seamless panel-merge strip (Dieter's design, same mechanism as LANES' Ext strip): a thin
	(1.524mm) full-height sliver drawn right at a module's own edge, matching that theme's panel
	background color, so two touching same-themed X-family modules read as one continuous panel
	across the seam. X-family only ever needs this on the RIGHT edge (toward wherever the Host
	is, directly or via a further Expander) - there is deliberately no left-side strip, since the
	X family's own left side never connects to anything meaningful (see the left-only-attachment
	note above).
*/
struct XExtStripWidget : Widget
{
	int style = STYLE_ORANGE;
	float topInsetMm = 0.f;

	void draw(const DrawArgs &args) override
	{
		if (!visible)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
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
	strip->box.pos = mm2px(Vec(panelWidthMm - 1.524f / 2.f + 0.1f, 0.35f));
	strip->box.size = mm2px(Vec(1.524f, PANELHEIGHT - 0.5f));
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

#endif
