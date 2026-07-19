/*
	NeoShared.hpp

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
#ifndef NEO_SHARED_HPP
#define NEO_SHARED_HPP

#include "OrangeLine.hpp"
#include "ExpanderBridge.hpp"

/*
	NEO is a bidirectional Expander - unlike the read-only XO family (XOShared.hpp) or the
	param-binding X family (XShared.hpp), it gets both read AND write access to a Host's internal
	tape/memory state. Kept genuinely generic (not Morpheus-specific in name) since Dieter
	anticipates other sequencer-type Hosts getting their own NEO-compatible support later - a
	name like "MorpheusTapeHostInterface" would have needed renaming the moment a second Host
	implemented it, "NeoHostInterface" doesn't.

	As of 2026-07-19, discovery goes through the generic ExpanderBridge.hpp mechanism (both sides,
	touch-once-then-persist - see Neo.cpp's own moduleProcess()), replacing NEO's earlier
	bespoke inline resolve-then-remember block. resolveNeoHost() below is kept as the final,
	family-specific dynamic_cast step once a host id has been resolved - NEO itself still needs
	to relay the *other* families' own discovery though - see Neo.cpp's own
	XExpanderInterface/XOExpanderInterface implementation for that (a real, physically sensible
	rack layout like `Morpheus | NEO | XO8` must keep working).
*/
struct NeoHostInterface
{
	virtual ~NeoHostInterface() {}

	// Read - "step" is 0..127 (MAX_LOOP_LEN), "channel" is 0..15 (POLY_CHANNELS). A step is a
	// single raw float, full stop - no separate gate/microtiming/volume/pitchbend sub-values;
	// NEO's own per-row cell-type widgets are purely different display/edit interpretations of
	// this one same value, confirmed explicitly during spec discussion.
	virtual float getTapeStep(int channel, int step) = 0;
	// Whichever of the Host's stored memory slots is currently active/selected - implicit, not a
	// freely choosable slot from NEO's side (NEO's own per-row toggle only ever picks TAPE vs.
	// MEM, never a specific one of several memory slots).
	virtual float getMemStep(int channel, int step) = 0;
	// How many of the up-to-128 steps are actually active/looping right now for this channel -
	// used to decide whether NEO's own per-row paging UI needs to appear at all.
	virtual int getLoopLen(int channel) = 0;
	// Current play-cursor position (0..loopLen-1) for this channel - used to auto-page a row
	// when its own FOLLOW toggle is on.
	virtual int getPlayCursor(int channel) = 0;

	// Write - same addressing as the read side above. No engagement/exclusivity concept at all;
	// concurrent writers (a real cable, a second Expander, etc.) resolve via plain last-write-
	// wins, confirmed explicitly during spec discussion as sufficient (not security/safety
	// critical, this is a musical instrument).
	virtual void setTapeStep(int channel, int step, float value) = 0;
	virtual void setMemStep(int channel, int step, float value) = 0;

	// This Host's own STYLE_JSON value - purely cosmetic (seam-bridging strip), unrelated to
	// resolution health. Named distinctly per-interface so a Host implementing several of these
	// families' interfaces at once (Morpheus already does, for X/XO) has no ambiguity.
	virtual float getNeoStyle() = 0;

	// Editable Host display name now lives on ExpanderBridgeInterface::getBridgeHostName() -
	// every Host implements that interface too (see its own comment for why this was
	// generalized out of being an X-family/NEO-only concept).
};

inline NeoHostInterface* resolveNeoHost(Module *neighbor)
{
	return neighbor ? dynamic_cast<NeoHostInterface*>(neighbor) : nullptr;
}

// Pure marker, no methods beyond identification via dynamic_cast - lets a Host's own connection
// light detect "the thing attached to me is specifically a NEO," without conflating it with an
// unrelated X/XO-family member that also happens to implement XExpanderInterface/
// XOExpanderInterface purely for chain-relay purposes (see Neo.cpp's own comment on why NEO
// implements those two interfaces at all).
struct NeoExpanderInterface { virtual ~NeoExpanderInterface() {} };

#endif
