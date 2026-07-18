/*
	XOShared.hpp

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
#ifndef XO_SHARED_HPP
#define XO_SHARED_HPP

#include "OrangeLine.hpp"
#include "XShared.hpp"

/*
	The XO family (XO8/XD8/XOD8/XO16/XD16/XOD16) is the read-only, output-side mirror of the
	X family (X8/X8D/X16/X16D, see XShared.hpp) - an Expander attaches to a Host's RIGHT side
	(the X family attaches to a Host's LEFT) and mirrors one of the Host's declared polyphonic
	OUTPUTS, either as real channel-split mono jacks (XO*), a colored/formatted per-channel value
	display (XD*), or both (XOD*).

	There is NO engagement/binding mechanism at all here, unlike the X family - reading a Host's
	output is never exclusive, so any number of these Expanders (and further Expanders chained
	past them) can watch the exact same output slot simultaneously with zero conflict. That means
	no right-click menu beyond the base Style switch, no per-Expander "Channels" limit (capacity
	is a fixed constant per module), and no scale/takeover conversion at all: the Expander reads
	the Host's real, already-scaled port voltage directly (Module::outputs[id].getVoltage()) -
	there is no "Expander's own raw knob" concept to convert on this side.

	XAlign is reused directly from XShared.hpp (not redeclared) - Morpheus implements both
	XHostInterface and XOHostInterface in the same translation unit, so a duplicate enum would be
	a hard compile error, not just style.
*/
enum XOType
{
	XO_TYPE_CONTINUOUS, // numeric value, formatted/colored like the X family's own display
	XO_TYPE_GATE        // gate/trigger - shown as a non-interactive lit/unlit square instead
};

// Shared by XOModuleCommon.hpp's own edge-detect (raw Host voltage -> stretched display state)
// and XOCommon.hpp's XOGateIndicator (previously a locally-duplicated constant) - one threshold,
// same convention as a real gate/trigger jack's usual 0V/10V swing.
#define XO_GATE_THRESHOLD_V 5.f

struct XOHostInterface
{
	virtual int getXOCount() = 0;
	virtual const char* getXOName(int index) = 0;      // full descriptive name
	// Compact name - same hard contract as XHostInterface::getXParamShortName(): max 5
	// characters, no exceptions, no truncation/scrolling fallback.
	virtual const char* getXOShortName(int index) = 0;
	virtual XOType getXOType(int index) = 0;
	// How XD8/XOD8/XD16/XOD16 should align this slot's numeric display text - continuous types
	// only, same reasoning as formatXOValue() below (a gate type has no numeric display at all).
	virtual XAlign getXOAlign(int index) = 0;
	virtual NVGcolor getXOColor(int index) = 0;

	// Live channel count of this output's real Rack port right now - not cached anywhere, since
	// a Host's own polyphony can change every tick (e.g. Morpheus's own polyChannels).
	//
	// Pitfall for any Host implementation: do NOT simply return
	// outputs[id].getChannels() here. Rack's own engine::Port::setChannels() silently no-ops
	// while no real cable is connected ("if (this->channels == 0) return;", confirmed directly
	// in the SDK's Port.hpp) - so on a Host with no cable patched into this jack, the real port's
	// own channel count would stay frozen at 0 forever regardless of what the Host actually
	// computes every tick, even though getVoltage() itself is unaffected and keeps working fine.
	// Read whatever internal, engine-independent bookkeeping the Host already uses to drive its
	// own setOutPolyChannels() call instead (OrangeLine's own OL_polyChannels/
	// getOutPolyChannels() - see Morpheus.cpp's own implementation).
	virtual int getXOChannelCount(int index) = 0;
	// Live real voltage for one channel of this output - exactly what a real patch cable would
	// read at that jack, no scaling/conversion of any kind.
	virtual float getXOChannelValue(int index, int channel) = 0;

	virtual std::string formatXOValue(int index, float value) = 0; // continuous only

	// This module's own STYLE_JSON value (STYLE_ORANGE/BRIGHT/DARK) - purely for the cosmetic
	// seam-bridging strip, unrelated to host-resolution health. Named distinctly from
	// XHostInterface::getXStyle() so a Host implementing both interfaces (e.g. Morpheus) has no
	// ambiguity between them, even though both simply return the same OL_state[STYLE_JSON].
	virtual float getXOStyle() = 0;

	virtual ~XOHostInterface() {}
};

struct XOExpanderInterface
{
	// The Host pointer this Expander itself resolved via its own left-side chain-walk (mirrored
	// direction from the X family - this family chains rightward off the Host), or, if nothing's
	// currently adjacent, its own remembered connection (see getXOConnectedHostId() below) -
	// or nullptr.
	virtual XOHostInterface* getXOHost() = 0;
	virtual float getXOStyle() = 0;

	// The remembered target Host's own module id (the "NFC touch once, stays connected" memory -
	// see resolveXOHostPersistent() below), or -1 if none. XO-family has no
	// engagement/binding/exclusivity concept at all (reading a Host's output is never exclusive),
	// so unlike the X-family's own equivalent, there's nothing else this memory could conflict
	// with - purely a convenience so a detached XO-family Expander keeps reading its Host live.
	// Exposed purely for the right-click menu (a "Disconnect" item, mirroring the X-family's own).
	virtual int64_t getXOConnectedHostId() = 0;
	// Clears the remembered connection (getXOConnectedHostId() above) only. After this, the
	// Expander only resolves a Host again via genuine physical adjacency, until either happens
	// again.
	virtual void disconnectXOHost() = 0;

	// Fixed per-module constant (8 or 16) - there's no "Channels" menu on this side, since
	// there's no sender deciding how many channels to feed; this is just how many mono jacks/
	// display cells this particular module physically has.
	virtual int getXOCapacity() = 0;

	virtual int getXOBrowseIndex() = 0; // which host output this Expander currently points at -
	                                     // never locked, freely navigable, no binding involved

	// Resolve-and-clamp-and-fallback for "whatever the resolved Host reports for the currently
	// browsed output", each falling back to a sane default when no Host is resolved (or it has
	// zero slots) - promoted to the interface (rather than living only on one concrete Expander)
	// so shared widget code (XOCommon.hpp) can call them through a plain XOExpanderInterface*,
	// same reasoning as XExpanderInterface's own equivalents in XShared.hpp.
	virtual XOType getXOBrowsedType() = 0;
	virtual NVGcolor getXOBrowsedColor() = 0;
	virtual XAlign getXOBrowsedAlign() = 0;
	virtual std::string formatXOBrowsedValue(float raw) = 0; // see XOHostInterface::formatXOValue()
	virtual int getXOBrowsedChannelCount() = 0;
	virtual float getXOBrowsedChannelValue(int channel) = 0;

	// Stretched (not raw-instantaneous) lit state for a gate/trigger-type slot's display -
	// XOGateIndicator (XOCommon.hpp) reads this instead of comparing getXOBrowsedChannelValue()
	// against the threshold itself. A raw live read only catches a real trigger's own brief pulse
	// (often a hardcoded ~1ms convention, see CLAUDE.md) if the UI happens to redraw during that
	// exact window, which in practice is "barely visible, most clicks missed" - X8ModuleCommon.hpp
	// solves the equivalent problem on the input side with clickPulse; XOModuleCommon.hpp mirrors
	// that same fixed-length-pulse-stretch approach here, driven by an edge-detect on the Host's
	// real voltage instead of a user click, using the same shared X_VALUE_CLICK_SECONDS duration.
	virtual bool getXOBrowsedChannelGateLit(int channel) = 0;

	virtual ~XOExpanderInterface() {}
};

/**
	Resolves the Host reachable through a given immediate neighbor (always leftExpander.module
	for an XO-family module - the mirror image of resolveXHost()'s own rightExpander.module,
	since this family attaches to a Host's RIGHT side and chains further rightward among itself),
	or nullptr if that neighbor isn't part of the XO family at all or doesn't (yet) reach a Host.
*/
inline XOHostInterface* resolveXOHost(Module *neighbor)
{
	if (!neighbor)
		return nullptr;
	XOHostInterface *host = dynamic_cast<XOHostInterface*>(neighbor);
	if (host)
		return host;
	XOExpanderInterface *link = dynamic_cast<XOExpanderInterface*>(neighbor);
	if (link)
		return link->getXOHost();
	return nullptr;
}

/**
	Resolves via adjacency first (resolveXOHost() above); on failure, falls back to a persisted
	Host module id ("NFC touch once, stays connected until explicitly broken" - mirrors the
	X-family's own CONNECTED_HOST_ID_JSON handling in X8ModuleCommon.hpp exactly, promoted to a
	shared helper here since both XOModuleCommon.hpp and XRModuleCommon.hpp need this identical
	logic at their own separate call sites, unlike the X-family which has only one call site and
	keeps the logic inline). `connectedHostIdState` is the caller's own
	OL_state[CONNECTED_HOST_ID_JSON] slot, passed by reference so this can both read the current
	value and update/clear it in place.
*/
inline XOHostInterface* resolveXOHostPersistent(Module *neighbor, float &connectedHostIdState)
{
	XOHostInterface *host = resolveXOHost(neighbor);
	if (host)
	{
		// Persist the RESOLVED HOST's own module id, not just the immediate neighbor's - a
		// longer Expander chain may relay through intermediate XO-family members to reach the
		// real Host. XOHostInterface is a sibling base class of Module, not Module itself -
		// dynamic_cast across to the underlying Module is a valid C++ cross-cast here.
		Module *hostModule = dynamic_cast<Module*>(host);
		if (hostModule && (int64_t) connectedHostIdState != hostModule->id)
			connectedHostIdState = (float) hostModule->id;
		return host;
	}
	int64_t connectedId = (int64_t) connectedHostIdState;
	if (connectedId >= 0)
	{
		Module *m = APP->engine->getModule(connectedId);
		XOHostInterface *fallback = m ? dynamic_cast<XOHostInterface*>(m) : nullptr;
		if (fallback)
			return fallback;
		connectedHostIdState = -1.f; // target vanished - clear stale id
	}
	return nullptr;
}

/**
	Returns the XO-family theme (STYLE_ORANGE/BRIGHT/DARK) of a given immediate neighbor, or -1
	if that neighbor isn't part of the XO family at all. Purely for the seamless panel-merge
	strip - independent of host-resolution health, mirrors getXNeighborStyle() in XShared.hpp.
*/
inline float getXONeighborStyle(Module *neighbor)
{
	if (!neighbor)
		return -1.f;
	XOHostInterface *host = dynamic_cast<XOHostInterface*>(neighbor);
	if (host)
		return host->getXOStyle();
	XOExpanderInterface *link = dynamic_cast<XOExpanderInterface*>(neighbor);
	if (link)
		return link->getXOStyle();
	return -1.f;
}

// Reuses XExtStripWidget/addXExtStrip()/addXExtStripLeft() from XShared.hpp verbatim (that
// widget and its two factory functions contain no dynamic_cast at all - they just draw a themed
// rect + accent line from plain `style`/`mirror` fields set externally, see XShared.hpp's own
// comment). Only the neighbor-resolution logic differs per family, so only that part is
// duplicated here, keyed off XOHostInterface/XOExpanderInterface instead.

// Same call-site shape as updateXExtStrip() - `self` is the widget's own module, `rightNeighbor`
// is whichever module sits to its right (an Expander's own right edge faces a further-chained
// Expander; a Host's own right edge faces the first Expander in the chain, mirrored from the X
// family's left-edge Host strip).
inline void updateXOExtStrip(XExtStripWidget *strip, Module *self, Module *rightNeighbor)
{
	float myStyle = getXONeighborStyle(self);
	if (myStyle < 0.f)
		return;
	float rightStyle = getXONeighborStyle(rightNeighbor);
	strip->style = (int) myStyle;
	strip->visible = (rightStyle >= 0.f) && (rightStyle == myStyle);
}

// See updateXOExtStrip() above - identical logic, just checking the LEFT neighbor instead (an
// Expander's own left edge faces the Host, or a further Host-ward Expander).
inline void updateXOExtStripLeft(XExtStripWidget *strip, Module *self, Module *leftNeighbor)
{
	float myStyle = getXONeighborStyle(self);
	if (myStyle < 0.f)
		return;
	float leftStyle = getXONeighborStyle(leftNeighbor);
	strip->style = (int) myStyle;
	strip->visible = (leftStyle >= 0.f) && (leftStyle == myStyle);
}

#endif
