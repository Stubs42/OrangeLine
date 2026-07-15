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

#define NUM_SOURCES 16
#define NUM_LANES   16

/*
	LANES is split into a Hub (Lanes.cpp) and any number of Expanders (LanesCV, LanesMidi,
	...). The Hub is deliberately "just" a shared input module: it collects the NUM_SOURCES
	raw sources' gate/pitch/velocity/lane-select CV (quantized, disconnected-input
	defaulted - the one genuinely input-side concern), and exposes that raw, per-source,
	*unmerged* state here. It does NOT do any merging or voice-stealing itself anymore -
	that decision is inherently tied to a capacity (how many simultaneous voices can this
	particular output represent?) that varies per expander (LanesCV is capped at Rack's own
	16-channel poly limit; a future expander type might want a different cap), so the Hub
	has no business deciding it. Each expander instead runs its own independent
	LanesVoiceAllocator (see LanesVoiceAllocator.hpp) against this raw state, with its own
	capacity, its own slot assignment, and its own overflow indicator.

	The Hub's actual `Lanes` struct is private to Lanes.cpp (OrangeLine convention: every
	module is its own struct, not exposed via a header), so expanders can't know its layout
	or cast to it directly. These two small pure-virtual interfaces are the only thing
	shared across translation units - this header (not Lanes.hpp, which also defines Lanes'
	own module-specific enums) is what every family member includes for that purpose.

	Chain-walk, done independently for BOTH neighbors (there's only one Hub per patch
	region, but expanders may sit on either side of it - see resolveLanesHub() below) once
	per control-rate tick since the Rack neighborhood can change any time. dynamic_cast (not
	a model == check + reinterpret_cast) so any future expander type joins the chain
	automatically just by implementing LanesExpanderInterface, in any order/mix/side with
	other expander types.
*/
struct LanesHubInterface {
	virtual bool  getSourceGate(int source, int channel) = 0;
	virtual float getSourcePitch(int source, int channel) = 0;
	virtual float getSourceVelocity(int source, int channel) = 0;
	virtual int   getSourceLane(int source, int channel) = 0;
	// This module's own STYLE_JSON value (STYLE_ORANGE/BRIGHT/DARK) - purely for the cosmetic
	// seam-bridging "Ext" strip (see getLanesNeighborStyle() below), unrelated to Hub
	// resolution/health.
	virtual float getLanesStyle() = 0;
	virtual ~LanesHubInterface() {}
};

struct LanesExpanderInterface {
	// The Hub pointer this expander itself resolved via its own chain-walk, or nullptr.
	virtual LanesHubInterface* getLanesHub() = 0;
	// Whether this expander itself found a Hub reachable through BOTH its sides at once
	// (i.e. it's sitting between two Hubs) - used by a Hub further down the chain to tell
	// "my neighbor is happily serving me" apart from "my neighbor is caught between me and
	// some other Hub", which classifyLanesNeighborForHub() below can't tell just from
	// getLanesHub() alone (that only ever returns ONE, arbitrarily preferred, Hub).
	virtual bool getLanesHubAmbiguous() = 0;
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
	Resolves the Hub reachable through a given immediate neighbor (leftExpander.module or
	rightExpander.module), or nullptr if that neighbor isn't part of the LANES family at all
	or doesn't (yet) reach a Hub. Every expander calls this once per side per tick:
		LanesHubInterface *left  = resolveLanesHub(leftExpander.module);
		LanesHubInterface *right = resolveLanesHub(rightExpander.module);
		lanesHub = left ? left : right;	// prefer left, arbitrary but deterministic
	Each side's own (non-)result also directly drives that side's connection-indicator
	light (see classifyLanesNeighborForHub() for the Hub's own version of that light).
*/
inline LanesHubInterface* resolveLanesHub(Module *neighbor)
{
	if (!neighbor)
		return nullptr;
	LanesHubInterface *hub = dynamic_cast<LanesHubInterface*>(neighbor);
	if (hub)
		return hub;
	LanesExpanderInterface *link = dynamic_cast<LanesExpanderInterface*>(neighbor);
	if (link)
		return link->getLanesHub();
	return nullptr;
}

enum LanesNeighborKind { LANES_NEIGHBOR_NONE, LANES_NEIGHBOR_OK, LANES_NEIGHBOR_CONFLICT };

/**
	Classifies a given immediate neighbor from a Hub's own point of view (`self` is that
	Hub's own LanesHubInterface* identity, i.e. `this`). Unlike an Expander's
	resolveLanesHub() above, a Hub doesn't need to resolve anything further (it IS the Hub) -
	but it does need to see past a directly-adjacent Expander to check whether that Expander
	is ALSO reachable from some other Hub (through its far side), not just check its type:
		none     - not part of the LANES family at all (or no neighbor)
		ok       - a normal Expander unambiguously serving this Hub (or one not yet resolved
		           - benefit of the doubt during startup) - the healthy case
		conflict - another Hub directly adjacent, or an Expander that's ambiguous or is
		           actually serving a *different* Hub than this one
	This is what makes a whole chain like "HubA | Expander | HubB" light up red on the
	Expander-facing side of BOTH Hubs, not just on the Expander itself.
*/
inline LanesNeighborKind classifyLanesNeighborForHub(Module *neighbor, LanesHubInterface *self)
{
	if (!neighbor)
		return LANES_NEIGHBOR_NONE;
	if (dynamic_cast<LanesHubInterface*>(neighbor))
		return LANES_NEIGHBOR_CONFLICT;
	LanesExpanderInterface *link = dynamic_cast<LanesExpanderInterface*>(neighbor);
	if (!link)
		return LANES_NEIGHBOR_NONE;
	if (link->getLanesHubAmbiguous())
		return LANES_NEIGHBOR_CONFLICT;
	LanesHubInterface *theirHub = link->getLanesHub();
	if (theirHub && theirHub != self)
		return LANES_NEIGHBOR_CONFLICT;
	return LANES_NEIGHBOR_OK;
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

	float leftStyle = getLanesNeighborStyle(leftNeighbor);
	strips->left->style = (int) myStyle;
	strips->left->visible = (leftStyle >= 0.f) && (leftStyle == myStyle);

	float rightStyle = getLanesNeighborStyle(rightNeighbor);
	strips->right->style = (int) myStyle;
	strips->right->visible = (rightStyle >= 0.f) && (rightStyle == myStyle);
}

#endif
