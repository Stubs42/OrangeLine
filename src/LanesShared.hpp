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

	Chain-walk (same code in every expander, once per control-rate tick since the Rack
	neighborhood can change any time):
		LanesHubInterface *hub = nullptr;
		Module *left = leftExpander.module;
		if (left) {
			hub = dynamic_cast<LanesHubInterface*>(left);
			if (!hub) {
				auto *link = dynamic_cast<LanesExpanderInterface*>(left);
				if (link) hub = link->getLanesHub();
			}
		}
	dynamic_cast (not a model == check + reinterpret_cast) so any future expander type
	joins the chain automatically just by implementing LanesExpanderInterface, in any
	order/mix with other expander types.
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
	virtual ~LanesExpanderInterface() {}
};

#endif
