/*
	ExpanderBridge.hpp

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
#ifndef EXPANDER_BRIDGE_HPP
#define EXPANDER_BRIDGE_HPP

#include "OrangeLine.hpp"
#include <vector>
#include <map>
#include <string>
#include <algorithm>

/*
	Generic, family-agnostic "how does an Expander find its Host" mechanism - shared by every
	Expander/Host family in this plugin (X, XO, LANES, STYX), replacing what used to be four
	separate, bespoke resolution mechanisms (a right-only live relay chain for X, a left-only
	persisted-id fallback for XO, a both-sides live conflict-detecting recompute for LANES, an
	inline both-sides persisted-id block for STYX).

	Core idea (Dieter, 2026-07-19): every Expander already needs to resolve "which Host am I
	talking to" somehow - rather than each family reinventing that, there's exactly ONE mechanism:
	physical adjacency (either side, no left/right restriction) is used ONLY as a one-time "NFC
	touch" moment, seeding a completely unconnected module with a host id the instant it becomes
	adjacent to something that already knows one. Every Expander thereby becomes a "bridge" to its
	own Host - it doesn't matter how many hops away the real Host is, since propagation happens
	once, at touch time, not via a continuously re-walked/re-read chain. Once a module has ANY
	known host id, further physical movement never re-triggers anything - see
	tryBridgeTouch()'s own comment for the (per-family-different) persistence semantics this
	implies.

	Deliberately dumb: this layer only ever deals in plain module ids and slug/family membership.
	It has no idea `XHostInterface`/`LanesHubInterface`/etc. exist at all - the actual "can I really
	talk to you" decision (a family-specific dynamic_cast) stays entirely inside each concrete
	family's own code, exactly as before. This is what lets a completely new family be added later
	without ever touching this file except for its own one-line family-registry entry.
*/

// One slug can belong to more than one family at once (Morpheus: FAMILY_X, FAMILY_XO,
// FAMILY_STYX simultaneously, since it implements XHostInterface/XOHostInterface/
// StyxHostInterface all on the same instance) - callers always check membership via
// getModuleFamilies(), never a single-value comparison. FAMILY_LANES is shared by both the
// CV-input and MIDI-input variants (CVLanes/MidiLanes Hubs, LanesCV/LanesMidi Expanders) -
// they already implement the exact same LanesHubInterface/LanesExpanderInterface and were
// already freely cross-compatible before this mechanism existed, so this preserves that.
enum ExpanderFamily
{
	FAMILY_X,
	FAMILY_XO,
	FAMILY_LANES,
	FAMILY_STYX
};

/*
	The ONE central, explicit slug -> families registry - deliberately the single source of truth
	for "which Expander is compatible with which Host". Dieter's own reasoning: this compatibility
	list has to exist as documentation somewhere anyway (for a user manual, or just for us to
	reason about it) - maintaining it only as separate prose risks drifting out of sync with the
	actual code, so the code IS the documentation instead. Every new Host/Expander module's own
	checklist step from now on (see CLAUDE.md): add its slug here, alongside whichever family
	interface(s) it implements.
*/
inline const std::vector<ExpanderFamily>& getModuleFamilies(const std::string &slug)
{
	static const std::map<std::string, std::vector<ExpanderFamily>> registry = {
		{ "Morpheus", { FAMILY_X, FAMILY_XO, FAMILY_STYX } },
		{ "X8",       { FAMILY_X } },
		{ "X8D",      { FAMILY_X } },
		{ "X16",      { FAMILY_X } },
		{ "X16D",     { FAMILY_X } },
		{ "XO8",      { FAMILY_XO } },
		{ "XD8",      { FAMILY_XO } },
		{ "XOD8",     { FAMILY_XO } },
		{ "XO16",     { FAMILY_XO } },
		{ "XD16",     { FAMILY_XO } },
		{ "XOD16",    { FAMILY_XO } },
		{ "XR8",      { FAMILY_XO } },
		{ "XR16",     { FAMILY_XO } },
		{ "CVLanes",  { FAMILY_LANES } },
		{ "MidiLanes",{ FAMILY_LANES } },
		{ "LanesCV",  { FAMILY_LANES } },
		{ "LanesMidi",{ FAMILY_LANES } },
		{ "Styx",     { FAMILY_STYX } },
	};
	static const std::vector<ExpanderFamily> none;
	auto it = registry.find(slug);
	return it != registry.end() ? it->second : none;
}

inline bool familiesIntersect(const std::vector<ExpanderFamily> &a, const std::vector<ExpanderFamily> &b)
{
	for (ExpanderFamily fa : a)
		for (ExpanderFamily fb : b)
			if (fa == fb)
				return true;
	return false;
}

/*
	Implemented by every Host AND every Expander/bridge module. A genuine Host returns its own
	module id from getBridgeHostId() (trivially - it IS its own host); an Expander returns
	whatever host id it currently knows (-1 if none) - this is what makes "every expander becomes
	a bridge to its host" true for free: a neighbor touching an already-connected Expander gets
	told the REAL host's id directly, never the Expander's own id.

	getBridgeFamilies() should always be implemented as `return getModuleFamilies(model->slug);`
	(deriving from the one central registry via this module's own, already-available, lock-free
	`model->slug` - never a separately hardcoded list) - this is what lets resolveBridgeHostId()
	below check compatibility via a plain virtual call on an already-held neighbor pointer,
	without ever needing a fresh APP->engine->getModule() lookup. That distinction matters: a
	live deadlock was confirmed (2026-07-19, gdb stack trace) between a UI-thread clone/paste
	operation (Engine::addModule(), holding/awaiting the exclusive lock) and an audio-thread
	moduleProcess() call stuck acquiring a share lock inside Engine::getModule() - Rack's
	SharedMutex is writer-preferring, so a queued exclusive request blocks *new* share-lock
	acquisitions even from a thread that already holds the block-level share lock, which
	deadlocks the moment a paste operation's addModule() calls overlap with any module's own
	per-tick getModule() call. The fix is to not need that per-tick lookup at all for the
	compatibility check specifically (the actual "read this Host's live data" getModule() calls
	elsewhere, gated by an established connection rather than firing every tick for every
	still-unconnected module, are the same low-frequency pattern this codebase already used
	safely before this session).
*/
struct ExpanderBridgeInterface
{
	virtual int64_t getBridgeHostId() = 0; // -1 = nothing known yet
	virtual std::vector<ExpanderFamily> getBridgeFamilies() = 0; // this module's own family/families
	// Optional editable display name for a Host module (empty if not applicable, or no custom
	// name set) - see each family's own Free/Disconnect-style menu item, which is where this
	// gets shown; no separate display widget needed anywhere.
	virtual std::string getBridgeHostName() = 0;
	virtual ~ExpanderBridgeInterface() {}
};

/*
	The single touch resolver - re-checks BOTH sides together every time it's called, regardless
	of which side's adjacency actually changed, so a module dropped into a gap exactly its own
	size (both adjacencies established in the same moment) is judged correctly regardless of
	Rack's own event delivery order for the two sides.

	Deliberately does NOT decide persistence/gating itself - that's a per-family policy decision
	left entirely to the caller (see ExpanderBridge.hpp's own file comment): X-family calls this
	fresh every tick for its own non-exclusive "connection" (lost again the instant adjacency is,
	since it was never bound - Dieter's own call), so the same physical detach that took the
	connection away naturally makes this return -1 again next time. XO-family/STYX/LANES instead
	only ever call this while their OWN persisted host id is still -1, and write whatever it
	returns into that persisted field once - never calling this again afterward, so a later
	physical move can't un-connect them.

	If exactly one side offers a compatible, already-resolved host id, returns it. If both sides
	simultaneously offer something (compatible or not, equal or different - deliberately not
	special-cased, see the plan's own reasoning), returns -1: connect to neither, rather than
	picking a winner.

	Deliberately does NO engine lookup at all (no APP->engine->getModule() anywhere in this
	function) - see ExpanderBridgeInterface's own comment above for why that matters (a confirmed
	deadlock against Engine::addModule() during a clone/paste operation). Compatibility is judged
	purely from `neighbor` itself (a pointer we're already holding, from Rack's own adjacency
	system, not fetched via the engine) - specifically its OWN getBridgeFamilies(), not its
	ultimate Host's. This is a deliberate, narrow trade-off: a relay chained through a member of a
	DIFFERENT single-family type than the one it's ultimately bridging to (e.g. touching an
	X-family-only relay while trying to reach an XO-compatible Host through it) won't propagate
	through that specific hop - touching a same-family relay, or the actual (often multi-family)
	Host directly, both still work correctly, which covers every case actually in use.
*/
inline int64_t resolveBridgeHostId(const std::vector<ExpanderFamily> &selfFamilies, Module *leftNeighbor, Module *rightNeighbor)
{
	auto offeredHostId = [&](Module *neighbor) -> int64_t
	{
		if (!neighbor)
			return -1;
		ExpanderBridgeInterface *bridge = dynamic_cast<ExpanderBridgeInterface*>(neighbor);
		if (!bridge)
			return -1;
		int64_t hostId = bridge->getBridgeHostId();
		if (hostId == -1)
			return -1;
		if (!familiesIntersect(selfFamilies, bridge->getBridgeFamilies()))
			return -1;
		return hostId;
	};

	int64_t leftOffer = offeredHostId(leftNeighbor);
	int64_t rightOffer = offeredHostId(rightNeighbor);

	if (leftOffer != -1 && rightOffer != -1)
		return -1; // sandwiched between two offers at once - connect to neither
	return (leftOffer != -1) ? leftOffer : rightOffer;
}

/*
	Whether two Modules genuinely belong to the same connected group right now - own resolved
	bridge host id equals the neighbor's, and neither is simply "unconnected" (-1 == -1 must NOT
	count, or two unrelated, disconnected same-family modules sitting side by side would
	incorrectly read as belonging together). Drives the seam-bridging Ext-strip and logo-cover-
	hide visibility for every family (replaces the old per-family theme/style-only comparison) -
	a genuinely simpler AND more correct condition, since it reflects actual logical connection
	rather than merely matching color schemes. Either argument may be nullptr (no neighbor there).
*/
inline bool bridgeConnected(Module *a, Module *b)
{
	ExpanderBridgeInterface *bridgeA = a ? dynamic_cast<ExpanderBridgeInterface*>(a) : nullptr;
	ExpanderBridgeInterface *bridgeB = b ? dynamic_cast<ExpanderBridgeInterface*>(b) : nullptr;
	if (!bridgeA || !bridgeB)
		return false;
	int64_t idA = bridgeA->getBridgeHostId();
	return idA != -1 && idA == bridgeB->getBridgeHostId();
}

#endif
