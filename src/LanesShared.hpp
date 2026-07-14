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
	virtual ~LanesExpanderInterface() {}
};

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

#endif
