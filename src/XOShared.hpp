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
	X family (X8/X8D/X16/X16D, see XShared.hpp) and mirrors one of the Host's declared polyphonic
	OUTPUTS, either as real channel-split mono jacks (XO*), a colored/formatted per-channel value
	display (XD*), or both (XOD*). As of 2026-07-19, an Expander can dock on either side (no more
	left-only restriction) - discovery works via the generic ExpanderBridge.hpp mechanism (see its
	own file comment): physical adjacency is only ever a one-time touch, and (unlike X-family) the
	resulting connection persists regardless of later physical movement, since there's no
	exclusive bind to protect here at all.

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
	// Resolved directly from this Expander's own persisted connection (getXOConnectedHostId()
	// below) every tick - no more live relay through a chain of neighbors' own getXOHost() calls,
	// since the real Host's id was already captured once, at touch time (ExpanderBridge.hpp), and
	// stays valid regardless of later physical position.
	virtual XOHostInterface* getXOHost() = 0;
	virtual float getXOStyle() = 0;

	// The remembered target Host's own module id (the "NFC touch once, stays connected" memory -
	// see resolveXOHostBridge() below), or -1 if none. XO-family has no
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
	Resolves this XO-family Expander's Host, using the generic touch-once-then-persist policy
	(ExpanderBridge.hpp's own file comment) - only ever attempts a fresh touch
	(resolveBridgeHostId(), checking BOTH sides now) while `connectedHostIdState` (the caller's own
	persisted int64_t member, passed by reference) is still -1; once connected, never re-touched by
	further physical movement, only ever re-resolved from the SAME persisted id (clearing it if the
	target has since vanished). Shared here since both XOModuleCommon.hpp and XRModuleCommon.hpp
	need this identical logic at their own separate call sites.

	`connectedHostIdState` is a real int64_t, NOT an OL_state float slot - Rack module ids observed
	in practice (2026-07-19 live testing) run into the quadrillions, nowhere near "small sequential
	integers" - a float's ~7-significant-digit precision corrupts an id that large instantly, so
	storing it there produces exactly "resolve succeeds, gets corrupted on write, immediately fails
	to read back, resets to -1" every single tick forever. Each concrete module persists this member
	itself via its own moduleExtraDataToJson/FromJson (json_integer()), same pattern Morpheus's own
	boundExpanderId array already uses for the identical reason.

	`cachedHost` (also the caller's own persisted-across-ticks member, by reference) is resolved via
	APP->engine->getModule() at most ONCE per actual connection - right when connectedHostIdState
	first becomes valid (a fresh touch, or a saved id restored from a patch load) - and then just
	returned as-is on every subsequent call, exactly like X-family's own boundHost/boundExpander
	caching (X8ModuleCommon.hpp/Morpheus.cpp). This used to re-resolve fresh via getModule() every
	single tick - confirmed (gdb, live freeze, 2026-07-19) to be exactly the same class of engine
	deadlock as the one found and fixed in the X-family/ExpanderBridge compatibility check earlier
	the same session (a share-locking getModule() call from inside moduleProcess(), racing a queued
	exclusive lock request during a module add/remove) - just not yet observed to trip it live at
	the time that fix went in. The cached pointer stays safe to hold indefinitely because the
	Host's own onRemove() proactively calls invalidateBridgeCache() on every module registered via
	registerBridgeListener() (done here, the moment the pointer is first cached) before it's
	destroyed - see ExpanderBridgeInterface's own comment for the full mechanism.
*/
inline XOHostInterface* resolveXOHostBridge(Module *self, ExpanderBridgeInterface *selfBridge,
                                             int64_t &connectedHostIdState, XOHostInterface *&cachedHost)
{
	if (connectedHostIdState == -1)
	{
		cachedHost = nullptr;
		int64_t newId = resolveBridgeHostId({ FAMILY_XO }, self->leftExpander.module, self->rightExpander.module);
		if (newId == -1)
			return nullptr;
		connectedHostIdState = newId;
	}
	if (cachedHost)
		return cachedHost; // already resolved and registered with the Host - no lookup needed
	// Reached only once per actual connection: right after the fresh touch above, or on the
	// first tick after a patch load where connectedHostIdState is already restored from JSON but
	// cachedHost is still null on this freshly-constructed instance - never on any other tick.
	Module *m = APP->engine->getModule(connectedHostIdState);
	XOHostInterface *host = m ? dynamic_cast<XOHostInterface*>(m) : nullptr;
	if (!host)
	{
		connectedHostIdState = -1; // target vanished/never existed - clear stale id
		return nullptr;
	}
	ExpanderBridgeInterface *hostBridge = dynamic_cast<ExpanderBridgeInterface*>(m);
	if (hostBridge)
		hostBridge->registerBridgeListener(selfBridge);
	cachedHost = host;
	return cachedHost;
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
	strip->style = (int) myStyle;
	strip->visible = bridgeConnected(self, rightNeighbor);
}

// See updateXOExtStrip() above - identical logic, just checking the LEFT neighbor instead (an
// Expander's own left edge faces the Host, or a further Host-ward Expander).
inline void updateXOExtStripLeft(XExtStripWidget *strip, Module *self, Module *leftNeighbor)
{
	float myStyle = getXONeighborStyle(self);
	if (myStyle < 0.f)
		return;
	strip->style = (int) myStyle;
	strip->visible = bridgeConnected(self, leftNeighbor);
}

#endif
