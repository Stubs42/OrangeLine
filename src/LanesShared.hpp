/*
	LanesShared.hpp

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
#ifndef LANES_SHARED_HPP
#define LANES_SHARED_HPP

#include "OrangeLine.hpp"
#include "ExpanderBridge.hpp"

#define NUM_SOURCES 16
#define NUM_LANES   16

/*
	LANES is split into a Hub (CVLanes.cpp/MidiLanes.cpp) and any number of Expanders (LanesCV,
	LanesMidi, ...). The Hub is deliberately "just" a shared input module: it collects the
	NUM_SOURCES raw sources' gate/pitch/velocity/lane-select CV (quantized, disconnected-input
	defaulted - the one genuinely input-side concern), and exposes that raw, per-source,
	*unmerged* state here. It does NOT do any merging or voice-stealing itself anymore -
	that decision is inherently tied to a capacity (how many simultaneous voices can this
	particular output represent?) that varies per expander (LanesCV is capped at Rack's own
	16-channel poly limit; a future expander type might want a different cap), so the Hub
	has no business deciding it. Each expander instead runs its own independent
	LanesVoiceAllocator (see LanesVoiceAllocator.hpp) against this raw state, with its own
	capacity, its own slot assignment, and its own overflow indicator.

	As of 2026-07-19, Hub discovery itself goes through the generic ExpanderBridge.hpp mechanism
	(both sides, touch-once-then-persist - see resolveLanesHubBridge() below), replacing the
	earlier live, every-tick, both-sides recompute with its own real-time "sandwiched between two
	Hubs" conflict detection. Dieter's own explicit call: adopt the same touch-once-persist model
	every other family uses, even though it means losing that live conflict signal - a genuinely
	ambiguous touch (both sides offering something at once) now just fails to connect at all
	instead of showing a warning light (see ExpanderBridge.hpp's own resolveBridgeHostId() for the
	"deny both" reasoning). These two small interfaces (the Hub's raw per-source data access) are
	unaffected - only the discovery half of this file changed.
*/
struct LanesHubInterface {
	virtual bool  getSourceGate(int source, int channel) = 0;
	virtual float getSourcePitch(int source, int channel) = 0;
	virtual float getSourceVelocity(int source, int channel) = 0;
	virtual int   getSourceLane(int source, int channel) = 0;
	// This module's own STYLE_JSON value (STYLE_ORANGE/BRIGHT/DARK) - purely for the cosmetic
	// seam-bridging "Ext" strip, unrelated to Hub resolution/health (see bridgeConnected(),
	// ExpanderBridge.hpp, for the actual connection-health check used there now).
	virtual float getLanesStyle() = 0;
	virtual ~LanesHubInterface() {}
};

struct LanesExpanderInterface {
	// Resolved directly from this Expander's own persisted connection every tick - no more live
	// relay through a chain of neighbors' own getLanesHub() calls, since the real Hub's id was
	// already captured once, at touch time (ExpanderBridge.hpp), and stays valid regardless of
	// later physical position.
	virtual LanesHubInterface* getLanesHub() = 0;
	// See LanesHubInterface::getLanesStyle() above.
	virtual float getLanesStyle() = 0;
	virtual ~LanesExpanderInterface() {}
};

/**
	Returns the LANES-family theme (STYLE_ORANGE/BRIGHT/DARK) of a given immediate neighbor, or
	-1 if that neighbor isn't part of the LANES family at all. Purely for the cosmetic
	seam-bridging "Ext" strip (res/Ext{Orange,Bright,Dark}.svg) that visually merges two
	touching same-themed panels - deliberately independent of Hub-resolution health (a
	same-themed neighbor bridges the seam regardless of whether the wider chain is "healthy").
*/
inline float getLanesNeighborStyle(Module *neighbor)
{
	if (!neighbor)
		return -1.f;
	LanesHubInterface *hub = dynamic_cast<LanesHubInterface*>(neighbor);
	if (hub)
		return hub->getLanesStyle();
	LanesExpanderInterface *link = dynamic_cast<LanesExpanderInterface*>(neighbor);
	if (link)
		return link->getLanesStyle();
	return -1.f;
}

/**
	Resolves this LANES Expander's Hub, using the generic touch-once-then-persist policy
	(ExpanderBridge.hpp's own file comment) - only ever attempts a fresh touch
	(resolveBridgeHostId(), both sides) while `connectedHubIdState` (the caller's own persisted
	int64_t member, passed by reference) is still -1; once connected, never re-touched by further
	physical movement, only ever re-resolved from the SAME persisted id (clearing it if the target
	has since vanished).

	`connectedHubIdState` is a real int64_t, NOT an OL_state float slot - observed Rack module ids
	run into the quadrillions, nowhere near "small sequential integers" - a float's ~7-significant-
	digit precision corrupts an id that large instantly (see XOShared.hpp's resolveXOHostBridge()
	for the fuller writeup of this exact bug, found and fixed there first). Each concrete Expander
	persists this member itself via its own moduleExtraDataToJson/FromJson (json_integer()).

	`cachedHub` (also the caller's own persisted-across-ticks member, by reference) is resolved via
	APP->engine->getModule() at most ONCE per actual connection - right when connectedHubIdState
	first becomes valid - then just returned as-is on every later call, mirroring XOShared.hpp's own
	resolveXOHostBridge() (see its comment for the full reasoning: an unconditional per-tick
	getModule() call here was the exact same class of confirmed engine deadlock found elsewhere in
	the session). Registers with the Hub's listener registry the moment the pointer is first
	cached, so the Hub's own onRemove() can proactively invalidate it before being destroyed - see
	ExpanderBridgeInterface's own comment (ExpanderBridge.hpp) for the full mechanism.
*/
inline LanesHubInterface* resolveLanesHubBridge(Module *self, ExpanderBridgeInterface *selfBridge,
                                                 int64_t &connectedHubIdState, LanesHubInterface *&cachedHub)
{
	if (connectedHubIdState == -1)
	{
		cachedHub = nullptr;
		int64_t newId = resolveBridgeHostId({ FAMILY_LANES }, self->leftExpander.module, self->rightExpander.module);
		if (newId == -1)
			return nullptr;
		connectedHubIdState = newId;
	}
	if (cachedHub)
		return cachedHub; // already resolved and registered with the Hub - no lookup needed
	Module *m = APP->engine->getModule(connectedHubIdState);
	LanesHubInterface *hub = m ? dynamic_cast<LanesHubInterface*>(m) : nullptr;
	if (!hub)
	{
		connectedHubIdState = -1; // target vanished/never existed - clear stale id
		return nullptr;
	}
	ExpanderBridgeInterface *hubBridge = dynamic_cast<ExpanderBridgeInterface*>(m);
	if (hubBridge)
		hubBridge->registerBridgeListener(selfBridge);
	cachedHub = hub;
	return cachedHub;
}

// Panel background per theme (Dieter's Colors.txt) - the "Orange" theme's own background is
// actually the dark navy 15152b (its warm tone comes from the ff6600 text/accent color, not
// the background), Dark is 202020, Bright is e6e6e6.
#define EXT_STRIP_BG_ORANGE nvgRGB(0x15, 0x15, 0x2b)
#define EXT_STRIP_BG_DARK   nvgRGB(0x20, 0x20, 0x20)
#define EXT_STRIP_BG_BRIGHT nvgRGB(0xe6, 0xe6, 0xe6)

// The "ORANGE LINE" brand accent stripe every panel already has (measured from
// res/CVLanesOrange.svg's PanelOrange/Bright/Dark layers - identical y/thickness/color, i.e.
// ff6600 (=ORANGE), across all three themes: <path d="M 0.762,124.71525 H 85.598" stroke="#ff6600"
// stroke-width="0.3"/>). The strip needs to continue this line across the seam too, not just the
// flat background.
#define EXT_STRIP_ACCENT_Y_MM 124.71525f
#define EXT_STRIP_ACCENT_THICKNESS_MM 0.3f

/**
	"Ext" strip (Dieter's design): a thin (1.524mm) full-height sliver drawn right at a module's
	own left/right edge, matching that theme's panel background color, so two touching same-
	themed LANES-family modules read as one continuous panel across the seam. Purely cosmetic
	(widget-side only, no moduleProcess() involvement) - Rack doesn't clip a widget to its parent
	module's own bounds, so a strip positioned straddling x=0 or x=panelWidth reaches right up to
	(and covers) the seam between two adjacent modules. Both sides of a seam draw their own
	independent copy (deliberately redundant - whichever module Rack happens to render on top of
	the other at that seam still shows the strip, regardless of draw order).

	Plain flat-color draw() instead of loading an SVG asset per theme - simpler to get pixel-
	perfect and no separate res/Ext*.svg files to keep in sync with Colors.txt.
*/
struct LanesExtStripWidget : Widget {
	int style = STYLE_ORANGE;
	// How far this widget's own box.pos.y sits above panel-global y=0 (the tuned top-inset
	// straddle) - needed to translate the accent line's panel-global y into this widget's own
	// local draw coordinates.
	float topInsetMm = 0.f;

	void draw(const DrawArgs &args) override
	{
		if (!visible)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, (style == STYLE_ORANGE) ? EXT_STRIP_BG_ORANGE : (style == STYLE_DARK) ? EXT_STRIP_BG_DARK : EXT_STRIP_BG_BRIGHT);
		nvgFill(args.vg);

		float accentLocalY = mm2px(EXT_STRIP_ACCENT_Y_MM - topInsetMm);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, accentLocalY - mm2px(EXT_STRIP_ACCENT_THICKNESS_MM) / 2.f, box.size.x, mm2px(EXT_STRIP_ACCENT_THICKNESS_MM));
		nvgFillColor(args.vg, ORANGE);
		nvgFill(args.vg);
	}
};

struct LanesExtStrips {
	LanesExtStripWidget *left = nullptr;
	LanesExtStripWidget *right = nullptr;
};

inline void addLanesExtStrips(ModuleWidget *w, float panelWidthMm, LanesExtStrips *strips)
{
	// Straddle the seam itself (half outside this module's own edge, half inside) rather than
	// sitting flush against it - the seam line is the gap/border BETWEEN two adjacent modules'
	// edges, so a strip that only touches it from one side still leaves it visible. Offsets
	// (+-0.1mm beyond the exact half-width straddle, 0.1mm inset from the top) tuned by eye
	// against a real two-module chain.
	strips->left = new LanesExtStripWidget();
	strips->left->box.pos = mm2px(Vec(-1.524f / 2.f - 0.1f, 0.35f));
	strips->left->box.size = mm2px(Vec(1.524f, PANELHEIGHT - 0.5f));
	strips->left->topInsetMm = 0.35f;
	strips->left->visible = false;
	w->addChild(strips->left);

	strips->right = new LanesExtStripWidget();
	strips->right->box.pos = mm2px(Vec(panelWidthMm - 1.524f / 2.f + 0.1f, 0.35f));
	strips->right->box.size = mm2px(Vec(1.524f, PANELHEIGHT - 0.5f));
	strips->right->topInsetMm = 0.35f;
	strips->right->visible = false;
	w->addChild(strips->right);
}

/**
	Called every widget step() (UI frame rate, not audio rate - this is purely cosmetic). `self`
	is the widget's own module - works even though it's a generic Module* because every LANES-
	family module already implements LanesHubInterface or LanesExpanderInterface (that's what
	getLanesNeighborStyle() dynamic_casts against), so this needs no per-module-type code at all.
*/
inline void updateLanesExtStrips(LanesExtStrips *strips, Module *self, Module *leftNeighbor, Module *rightNeighbor)
{
	float myStyle = getLanesNeighborStyle(self);
	if (myStyle < 0.f)
		return;

	strips->left->style = (int) myStyle;
	strips->left->visible = bridgeConnected(self, leftNeighbor);

	strips->right->style = (int) myStyle;
	strips->right->visible = bridgeConnected(self, rightNeighbor);
}

#endif
