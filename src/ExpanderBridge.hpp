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
#include <chrono>

/*
	Generic, family-agnostic "how does an Expander find its Host" mechanism - shared by every
	Expander/Host family in this plugin (X, XO, LANES, NEO), replacing what used to be four
	separate, bespoke resolution mechanisms (a right-only live relay chain for X, a left-only
	persisted-id fallback for XO, a both-sides live conflict-detecting recompute for LANES, an
	inline both-sides persisted-id block for NEO).

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
// FAMILY_NEO simultaneously, since it implements XHostInterface/XOHostInterface/
// NeoHostInterface all on the same instance) - callers always check membership via
// getModuleFamilies(), never a single-value comparison. FAMILY_LANES is shared by both the
// CV-input and MIDI-input variants (CVLanes/MidiLanes Hubs, LanesCV/LanesMidi Expanders) -
// they already implement the exact same LanesHubInterface/LanesExpanderInterface and were
// already freely cross-compatible before this mechanism existed, so this preserves that.
enum ExpanderFamily
{
	FAMILY_X,
	FAMILY_XO,
	FAMILY_LANES,
	FAMILY_NEO
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
		{ "Morpheus", { FAMILY_X, FAMILY_XO, FAMILY_NEO } },
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
		{ "Neo",      { FAMILY_NEO } },
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
	// This module's own OL_state[STYLE_JSON] (STYLE_ORANGE/DARK/BRIGHT) - default body here only
	// matters for the same impossible-in-practice bespoke-implementer case as the data-store
	// methods below; every real module gets a real override via OrangeLineCommon.hpp. Used ONLY
	// by bridgeConnected() to decide whether a genuinely connected pair should also visually
	// close their seam - two modules can be logically bridged while running different themes
	// (nothing stops that), and a seam that closes anyway would stitch two mismatched colors
	// together, so bridgeConnected() additionally requires both sides to agree here. Dieter's
	// own instruction, 2026-07-20: this must live once, in the common bridge code every family
	// already shares, not be reimplemented (or forgotten) per family/module.
	virtual float getBridgeStyle() { return -1.f; }

	// Lifecycle-safety hooks for a module that caches a raw pointer to another bridge module
	// across ticks (XO/XR/LANES/NEO all do this - see XOShared.hpp's resolveXOHostBridge(),
	// LanesShared.hpp's resolveLanesHubBridge(), Neo.cpp's own inline equivalent; X-family
	// doesn't use any of this, it already has its own older, bespoke equivalent - the
	// xCandidates[] array + XHostInterface::resetXParamDuringRemoval(), Morpheus.cpp). The whole
	// point: NEVER call APP->engine->getModule() from inside moduleProcess() to keep a cached
	// pointer valid - that was confirmed (gdb, live freeze, 2026-07-19) to occasionally race a
	// queued exclusive lock request (module add/remove) and deadlock the engine. Instead, a
	// module that just cached a live pointer to `host` calls host->registerBridgeListener(this)
	// once, at that exact caching moment; `host`'s own onRemove() then calls
	// invalidateBridgeCache() on every still-registered listener before it's destroyed, and each
	// listener's own onRemove() calls host->unregisterBridgeListener(this) using its OWN cached
	// host pointer. None of this ever touches the engine (no getModule() anywhere in the whole
	// chain) - every call is a plain virtual dispatch on a pointer already known to be valid
	// (Rack never destroys a module concurrently with another module's onRemove() callback, and
	// onRemove() itself always runs before the module it belongs to is actually destructed), so
	// none of it is affected by whatever lock Rack's own removeModule() currently holds.
	// Default no-ops: a module that's never used as anyone's cached "host" (or that doesn't
	// participate in this mechanism at all) needs no extra code whatsoever.
	virtual void registerBridgeListener(ExpanderBridgeInterface *listener) {}
	virtual void unregisterBridgeListener(ExpanderBridgeInterface *listener) {}
	// Called ON a listener, BY whichever bridge module it registered with, right before that
	// module is destroyed - the listener must drop its own cached pointer/id to it immediately,
	// exactly mirroring how X-family's own setXBoundHostId(-1) already works.
	virtual void invalidateBridgeCache() {}

	// Generic, opaque per-Expander-type shared storage (Dieter's own design, 2026-07-19) - lets
	// an Expander persist/share arbitrary data on whichever Host it's attached to, WITHOUT that
	// Host's own code ever needing to know what the data means. Keyed by the *calling Expander's
	// own model->slug* (never a free-form string): since Rack already guarantees module slugs are
	// globally unique, this makes collisions between unrelated Expander types structurally
	// impossible with zero naming-convention discipline required, and since every instance of the
	// same Expander TYPE shares one slug, every instance automatically reads/writes the same slot
	// - which is the whole point (e.g. multiple NEO instances on one Morpheus agreeing on channel
	// names/colors). The value itself is a plain, already-serialized JSON string, not a json_t* -
	// deliberately: a Host that "just stores whatever string it's handed" needs zero Jansson
	// refcounting/lifetime awareness at all, unlike a scheme that hands around live json_t*
	// objects. Every module gets a real, working, storage-backed implementation of these three for
	// free via OrangeLineCommon.hpp's own ExpanderDataStore member (see its own comment) - default
	// bodies here only matter for the (impossible in practice, since every module includes
	// OrangeLineCommon.hpp) case of a bespoke ExpanderBridgeInterface implementer that doesn't.
	virtual void writeExpanderData(const std::string &slug, const std::string &json) {}
	virtual std::string readExpanderData(const std::string &slug) { return ""; } // "" = never written
	// Real (not simulated/logical) milliseconds, purely for cheap change detection - a caller
	// compares this against its own last-seen value and only re-parses readExpanderData()'s
	// (potentially large) JSON string when it actually changed, rather than every tick. Not
	// persisted across a save/reload - see ExpanderDataStore::timestampMs()'s own comment for why
	// that's fine.
	virtual int64_t getExpanderDataTimestampMs(const std::string &slug) { return 0; } // 0 = never written

	virtual ~ExpanderBridgeInterface() {}
};

/**
	Reusable listener-registry helper - any module that wants to let other modules safely cache a
	raw pointer to IT (i.e. any Host/Hub: Morpheus for XO/NEO, CVLanes/MidiLanes for LANES) just
	holds one of these as a plain member (composition, not inheritance - a module can't multiply-
	inherit two copies of the same base, and Morpheus needs exactly one registry shared by both its
	XO and NEO listeners, not two), forwards its own registerBridgeListener()/
	unregisterBridgeListener() overrides straight to it, and calls notifyAndClear() as part of its
	own onRemove(). See ExpanderBridgeInterface's own comment above for the full mechanism this
	supports.
*/
struct BridgeListenerRegistry
{
	std::vector<ExpanderBridgeInterface*> listeners;

	void add(ExpanderBridgeInterface *listener)
	{
		if (std::find(listeners.begin(), listeners.end(), listener) == listeners.end())
			listeners.push_back(listener);
	}
	void remove(ExpanderBridgeInterface *listener)
	{
		listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
	}
	// Called from the owning module's own onRemove(), right before it's destroyed - tells every
	// listener still registered to drop its cached pointer immediately. Cleared afterward so a
	// listener's own subsequent onRemove() (order between the two is never guaranteed) finds
	// nothing left to unregister.
	void notifyAndClear()
	{
		for (ExpanderBridgeInterface *listener : listeners)
			listener->invalidateBridgeCache();
		listeners.clear();
	}
};

/**
	Reusable backing store for ExpanderBridgeInterface::writeExpanderData()/readExpanderData()/
	getExpanderDataTimestampMs() - any module composes exactly one of these (OrangeLineCommon.hpp
	already does, for every module, see its own comment) and forwards those three interface
	methods straight to it. One entry per calling Expander's own slug - see the interface method's
	own comment for why keying on slug (rather than a free-form string) makes cross-Expander-type
	collisions structurally impossible without any naming convention.
*/
struct ExpanderDataStore
{
	struct Entry
	{
		std::string json;
		int64_t timestampMs = 0;
	};
	std::map<std::string, Entry> bySlug;

	void write(const std::string &slug, const std::string &json)
	{
		Entry &e = bySlug[slug];
		e.json = json;
		e.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}
	std::string read(const std::string &slug) const
	{
		auto it = bySlug.find(slug);
		return it != bySlug.end() ? it->second.json : "";
	}
	int64_t timestampMs(const std::string &slug) const
	{
		auto it = bySlug.find(slug);
		return it != bySlug.end() ? it->second.timestampMs : 0;
	}

	// Omits the whole key entirely when empty - the vast majority of modules never call write()
	// at all, and shouldn't carry a stray empty JSON object in every saved patch because of it.
	void toJson(json_t *rootJ) const
	{
		if (bySlug.empty())
			return;
		json_t *dataJ = json_object();
		for (const auto &pair : bySlug)
			json_object_set_new(dataJ, pair.first.c_str(), json_string(pair.second.json.c_str()));
		json_object_set_new(rootJ, "expanderData", dataJ);
	}
	// timestampMs is deliberately NOT persisted/restored - it only ever exists to answer "has
	// this changed since I last looked, this session", and a freshly loaded module's own readers
	// all start with no last-seen value cached anyway, so they correctly do one read to sync up
	// regardless of what timestamp a restored entry would otherwise carry.
	void fromJson(json_t *rootJ)
	{
		json_t *dataJ = json_object_get(rootJ, "expanderData");
		if (!dataJ)
			return;
		const char *slug;
		json_t *valueJ;
		json_object_foreach(dataJ, slug, valueJ)
			if (json_is_string(valueJ))
				bySlug[slug].json = json_string_value(valueJ);
	}
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
	connection away naturally makes this return -1 again next time. XO-family/NEO/LANES instead
	only ever call this while their OWN persisted host id is still -1, and write whatever it
	returns into that persisted field once - never calling this again afterward, so a later
	physical move can't un-connect them.

	If exactly one side offers a compatible, already-resolved host id, returns it. If both sides
	simultaneously offer something, the two offers are only genuinely ambiguous when they disagree
	- if both sides already point at the SAME host id, that's not a conflict at all (e.g. this
	module sandwiched between two other already-connected members of the same chain, both
	correctly relaying the one real Host further down) and connecting is exactly right; only a
	real disagreement (two different ids) returns -1, connect to neither, rather than picking a
	winner. (Revised 2026-07-19 after live testing: the original version of this rule denied BOTH
	the equal- and differing-id cases identically, which incorrectly refused a module sitting
	between two already-connected same-host neighbors - Dieter's own live catch, XD8 never
	connecting despite touching a fully legitimate chain on both sides.)

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
		// Both sides offer something: agreeing on the same host is not a conflict (sandwiched
		// between two already-connected members of the same chain) - only a genuine disagreement
		// means connect to neither.
		return (leftOffer == rightOffer) ? leftOffer : -1;
	return (leftOffer != -1) ? leftOffer : rightOffer;
}

/*
	Whether two Modules genuinely belong to the same connected group right now - own resolved
	bridge host id equals the neighbor's, and neither is simply "unconnected" (-1 == -1 must NOT
	count, or two unrelated, disconnected same-family modules sitting side by side would
	incorrectly read as belonging together) - AND both currently run the same theme, so a closed
	seam never stitches two mismatched panel colors together (2026-07-20 addition, Dieter's own
	instruction: a logical connection can span different themes, but the VISUAL seam-closing
	should not, and that rule belongs here once rather than in each family's own strip code).
	Drives the seam-bridging Ext-strip and logo-cover-hide visibility for every family. Either
	argument may be nullptr (no neighbor there).
*/
inline bool bridgeConnected(Module *a, Module *b)
{
	ExpanderBridgeInterface *bridgeA = a ? dynamic_cast<ExpanderBridgeInterface*>(a) : nullptr;
	ExpanderBridgeInterface *bridgeB = b ? dynamic_cast<ExpanderBridgeInterface*>(b) : nullptr;
	if (!bridgeA || !bridgeB)
		return false;
	int64_t idA = bridgeA->getBridgeHostId();
	if (idA == -1 || idA != bridgeB->getBridgeHostId())
		return false;
	return bridgeA->getBridgeStyle() == bridgeB->getBridgeStyle();
}

#endif
