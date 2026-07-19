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
#include "ExpanderBridge.hpp"

/*
	The X family (X8/X8D/X16/X16D) is a generic "param-access" Expander: any Host module with
	polyphonic param-paired inputs can let an X unit remote-control one of them per-channel. See
	ExpanderParamAccessSpec.md at the repo root for the full design (current as of commit
	aa6f650 - "virtual poly cable", multi-binding per Expander, Track & Hold, unfiltered but
	color-coded browse list).

	As of 2026-07-19, an Expander can dock on EITHER side (no more left/right restriction) -
	discovery works via the generic ExpanderBridge.hpp mechanism (see its own file comment):
	physical adjacency is only ever a one-time "touch" seeding a completely unconnected neighbor
	with a host id, never a continuously re-walked chain. A bound candidate's own exclusivity
	(requestXBind() below) is the one thing X-family still protects that the other families don't
	need to.

	Binding (which Expander currently drives which candidate param) lives entirely on the Host
	side as a stable Rack module ID per param - nothing here needs to know about it beyond the
	passive getters below, which the Host reads during its own process().
*/
enum XParamType {
	X_PARAM_CONTINUOUS,
	X_PARAM_TOGGLE,      // click flips state, stays until clicked again
	X_PARAM_CLICK,       // single fixed-length pulse fired on click, independent of hold duration
	X_PARAM_PUSH         // value is high only while the control is actively held down - named
	                     // "Push" (not "Momentary") to stay consistent with Dieter's YATOF
	                     // project terminology
};

// Text alignment for a numeric display (X8D/X16D's per-channel value readout) - a per-candidate
// property, same idea as getXParamColor(), since different value shapes read better differently
// (e.g. a signed float vs. a plain step count).
enum XAlign {
	X_ALIGN_LEFT,
	X_ALIGN_CENTER,
	X_ALIGN_RIGHT
};

struct XExpanderInterface; // forward decl - XHostInterface::requestXBind() takes one, but
                            // XExpanderInterface itself is declared further below

struct XHostInterface
{
	virtual int getXParamCount() = 0;
	virtual const char* getXParamName(int index) = 0;      // full descriptive name
	// Compact name - X8 always displays THIS, never getXParamName(). Hard contract: max 5
	// characters, no exceptions - X8's name display is sized/laid out for exactly that width
	// and does not truncate or scroll. Every Host implementation must respect this.
	virtual const char* getXParamShortName(int index) = 0;
	virtual XParamType getXParamType(int index) = 0;
	// How X8D/X16D should align this candidate's numeric display text - continuous types only,
	// same reasoning as formatXParamValue() below (a digital type has no numeric display at all).
	virtual XAlign getXParamAlign(int index) = 0;
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

	// Called directly by an Expander's own moduleProcess() the instant its physical Engage button
	// is pressed - replaces an earlier design where the Host itself walked its left-side physical
	// neighbor chain looking for a pending press (2026-07-18/19 history, see
	// XHostImplementationGuide.md). That walk predated the Expander having any notion of "which
	// Host am I resolved to" at all - now that every Expander already resolves getXHost() itself
	// (crossing both physical adjacency AND a persistently-connected-but-detached neighbor, the
	// same relay already trusted for reading), there is no reason granting a bind should need a
	// *stricter* mechanism than reading already uses. `expander` is the calling Expander itself
	// (as an XExpanderInterface*) so the Host can push setXBoundHostId() straight back onto it
	// without a second lookup; `expanderId` is its stable module id, for the single source of
	// truth this Host's own candidate table stores. Implements exactly the same three-way
	// decision the old chain-walk's grant branch did: bind if free (after globally unbinding this
	// Expander id first - xUnbindExpanderEverywhere()), toggle-disengage if already bound here,
	// no-op if taken by someone else or a real cable is connected.
	virtual void requestXBind(int index, int64_t expanderId, XExpanderInterface *expander) = 0;
	// Right-click action: clears the binding for this param -> Red.
	virtual void resetXParam(int index) = 0;
	// Directly overwrites this slot's binding to expanderId (may be -1) with no side effects
	// beyond the raw write itself - does NOT push setXBoundHostId() to anyone, unlike
	// resetXParam()/the chain-walk bind grant. Used exclusively by the clone-recovery mechanism
	// (XShared.hpp's tryRecoverXBinding()) for an Expander that found a stale slot referencing
	// its own pre-clone id and is reclaiming it directly - the caller pushes setXBoundHostId()
	// itself right afterward, since it already has the Expander pointer in hand. Safe to call on
	// a Host that has never ticked process() yet (see getXSelfId()'s own comment) - touches only
	// the raw, constructor/dataFromJson-populated candidate array, nothing derived.
	// `expander`, when the caller already has it in hand (every real caller does - see
	// findXBoundHostId()/tryRecoverXParamSlot() below), lets the Host cache the actual pointer
	// for this slot directly, at this exact one-time reconnect/recovery event, instead of ever
	// having to resolve it itself later via APP->engine->getModule(). This replaced an earlier
	// design where the Host's own per-tick refresh loop re-resolved a bound candidate's Expander
	// pointer fresh every tick - confirmed (gdb, live freeze) to occasionally race a queued
	// exclusive lock request (module add/remove) and deadlock the engine, exactly like the
	// ExpanderBridge/XOD8 deadlock found earlier the same session (same root cause - a
	// share-locking getModule() call from inside moduleProcess() - different call site). Per
	// Dieter's own call: a Host must never have any reason to resolve anything outside the exact
	// moment an Expander itself is doing the connecting - every other call site pushes its
	// already-known pointer directly instead.
	virtual void recoverXParamBinding(int index, int64_t expanderId, XExpanderInterface *expander = nullptr) = 0;
	// This Host's own OL_selfId (OrangeLineCommon.hpp) - see that member's comment for the full
	// "birth id" mechanism. Exposed here so another module's clone-recovery scan can tell a
	// genuinely fresh clone of this exact Host (mismatched: getXSelfId() != this module's own
	// Rack id) apart from a settled, long-standing one - see xIsFreshClone() below.
	virtual int64_t getXSelfId() = 0;
	// Identical effect to resetXParam() above, but MUST be used instead whenever called as part
	// of an onRemove() chain (this Host's own, or a bound Expander's own) - never call this from
	// anywhere else. Rack's own removeModule() holds an EXCLUSIVE lock for the entire duration of
	// any onRemove() callback it triggers, and the ordinary resetXParam() internally calls the
	// regular, share-locking APP->engine->getModule() - calling that from within an
	// already-exclusively-locked callback is a guaranteed deadlock (the same thread re-entering a
	// non-reentrant lock; Rack's own Engine.hpp documents "exclusively locking methods cannot be
	// called simultaneously or recursively with a share-locking method"). This variant uses
	// APP->engine->getModule_NoLock() instead, which is only safe because the caller is already
	// guaranteed to be running inside that same lock. See CLAUDE.md's own Pitfalls entry on this.
	virtual void resetXParamDuringRemoval(int index) = 0;

	// "The value this Host was actually using for this channel", in this candidate's own
	// human-readable DISPLAY units (e.g. steps for a loop-length candidate, percent for a
	// probability candidate) - the SAME units the Expander's own knob directly holds (see
	// XExpanderInterface::getXBrowsedParamDisplayMin/Max/Default() and moduleProcess()'s own
	// range-reconfiguration). An Expander reads this whenever it arrives on this candidate
	// (a fresh bind, or navigating back onto one - see isXKnobReady()) and sets its own knob(s)
	// to match, so engaging doesn't change anything the Host is currently outputting. A Host with
	// no meaningful scaling for a given candidate (e.g. a push/click/toggle type) can just return 0.
	virtual float getXParamTakeoverValue(int index, int channel) = 0;

	// Converts an Expander's own knob value (already in this candidate's own display units,
	// e.g. steps, percent, volts - see getXParamDisplayMin/Max() below) into whatever value THIS
	// Host's own poly input actually expects to receive for this candidate in CV/cable units -
	// i.e. exactly what a real patch cable would have to deliver to produce the same result. The
	// Expander itself has no idea about any Host's own CV scaling convention, so the Host must
	// apply this conversion itself whenever it copies a bound Expander's knob value into its own
	// live poly-input state - see Morpheus.cpp's moduleProcess() refresh loop for the call site.
	// continuous types only; a Host can just return the value unchanged for a digital
	// (Toggle/Click/Push) candidate, since those are read via a simple threshold, not a scaled
	// real-world unit.
	virtual float scaleXParamValue(int index, float displayValue) = 0;

	virtual std::string formatXParamValue(int index, float value) = 0; // continuous only - value
	                                                                     // already in display units

	// This candidate's own human-readable display range/behavior - the Expander's own knob gets
	// dynamically reconfigured (minValue/maxValue/defaultValue/snapEnabled) to exactly this
	// range whenever it's the currently-browsed candidate (see moduleProcess()'s own
	// range-reconfiguration and X8Knob::step()'s own unit sync), so Rack's built-in hover tooltip
	// and right-click "Enter value" editing both work correctly with zero custom parsing needed.
	// Meaningless for digital (Toggle/Click/Push) candidates - a Host can just return the same
	// harmless (0, 1, 0.5, false, "") defaults X8ModuleCommon.hpp's own disconnected fallback uses.
	virtual float getXParamDisplayMin(int index) = 0;
	virtual float getXParamDisplayMax(int index) = 0;
	virtual float getXParamDisplayDefault(int index) = 0;
	// Rounds the knob to the nearest whole display unit (Rack's own ParamQuantity::snapEnabled) -
	// only meaningful once minValue/maxValue directly span the display range, which the
	// reconfiguration above already ensures.
	virtual bool getXParamSnap(int index) = 0;
	// Rack's own separate ParamQuantity::unit field (e.g. "%") - deliberately never baked into
	// formatXParamValue()'s own returned string, since Rack's built-in value-entry parses that
	// string as a tinyexpr math expression and "%" is a registered (modulo) operator there.
	virtual const char* getXParamUnit(int index) = 0;

	// This module's own STYLE_JSON value (STYLE_ORANGE/BRIGHT/DARK) - purely cosmetic (panel
	// theme), unrelated to host-resolution health or the seam (which now keys off bridge host id
	// equality, not theme - see ExpanderBridge.hpp).
	virtual float getXStyle() = 0;

	// Editable Host display name now lives on ExpanderBridgeInterface::getBridgeHostName()
	// (every Host implements that interface too) - see its own comment for why this was
	// generalized out of being an X-family/NEO-only concept.

	virtual ~XHostInterface() {}
};

struct XExpanderInterface
{
	// The Host pointer for whichever Host currently binds this Expander (if any) - a cached
	// pointer, pushed directly by setXBoundHostId() below at the exact moment a bind is granted
	// or restored, never re-resolved via APP->engine->getModule() on any other tick (see that
	// method's own comment for why - this used to re-resolve fresh every tick and turned out to
	// be a confirmed, live deadlock). Safe to cache because both sides proactively clear it via
	// their own onRemove() the instant either the Host or this Expander is deleted - a stale
	// pointer can never outlive the id that would have caught it anyway. Falls back to whatever's
	// genuinely adjacent right now if not bound anywhere (browsing purposes only, still resolved
	// fresh each tick via ExpanderBridge.hpp - a deliberately different, genuinely-live case, not
	// a persisted connection - see that file's own persistence-policy note).
	virtual XHostInterface* getXHost() = 0;
	// Pushed directly by whichever Host currently binds this Expander, at the exact moment the
	// binding is granted or revoked (-1 = revoked/not bound) - a stable module id, never a raw
	// pointer, so it's always safe to hold onto: resolving it via APP->engine->getModule() just
	// returns nullptr if the target is gone, rather than risking a dangling reference. Both sides
	// also proactively clear this via their own onRemove() the instant either one is deleted (see
	// Morpheus.cpp's own onRemove()/resetXParam() and this interface's own concrete
	// implementation's onRemove()) - so a stale id should never even be observed in practice; this
	// is what makes it safe to trust without re-verifying against the whole rack every tick.
	// Binding changes are rare and Host-initiated (a user click, not continuous), so push is both
	// cheaper and avoids querying the whole rack ~1000x/second per Expander instance purely to
	// answer a question that only changes a few times per session. See CLAUDE.md's own Pitfalls
	// entry on this for the fuller reasoning, including why the earlier scan-based approach was
	// deliberately chosen first (structurally can't go stale) before being replaced here (push is
	// only as correct as its own discipline - every boundExpanderId mutation on the Host side
	// must route through a call that also pushes, see Morpheus.cpp's own resetXParam()).
	//
	// `hostPtr`, when the caller already has it in hand (every real caller does: requestXBind()
	// passes its own `this`, findXBoundHostId()/tryRecoverXBinding() pass whatever they just
	// resolved during their own one-time scan), lets this Expander cache the actual Host pointer
	// directly at this exact bind/reconnect event - getXHost() below then just returns this
	// cached pointer, never re-resolving it via APP->engine->getModule() on any other tick. This
	// replaced an unconditional per-tick resolve that turned out to be exactly the same class of
	// confirmed deadlock as the Host-side one described in recoverXParamBinding()'s own comment
	// above (a share-locking getModule() call from inside moduleProcess(), racing a queued
	// exclusive lock request) - just never yet observed to trip it live, not actually safer.
	virtual void setXBoundHostId(int64_t hostId, XHostInterface *hostPtr = nullptr) = 0;
	virtual int64_t getXBoundHostId() = 0;
	// This Expander's own OL_selfId (OrangeLineCommon.hpp) - see XHostInterface::getXSelfId()'s
	// own comment above, same mechanism, mirrored for the other family role. A Host's own
	// clone-recovery search (XShared.hpp's findFreshXExpanderClone()) uses this to recognize a
	// fresh clone of the Expander that used to legitimately hold one of its candidate slots.
	virtual int64_t getXSelfId() = 0;
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

	// The four methods below resolve "whatever the resolved Host reports for the currently
	// browsed param", each falling back to a sane default when no Host is resolved (or it has
	// zero candidates) - see any concrete Expander's own implementation for the exact resolve-
	// and-clamp logic. Promoted to the interface (rather than living only on X8/X8D directly) so
	// shared widget code (X8Common.hpp) can call them through a plain XExpanderInterface*,
	// without needing to know or dynamic_cast to the concrete Expander type at all - this is what
	// makes X8 and X8D able to share their entire widget/control implementation verbatim.
	virtual XParamType getXBrowsedParamType() = 0;
	virtual NVGcolor getXBrowsedParamColor() = 0;
	virtual XAlign getXBrowsedParamAlign() = 0;
	// Formats a value (already in the browsed candidate's own display units) as the Host would
	// display it - see XHostInterface::formatXParamValue(). Empty string when nothing meaningful
	// is resolved, or for a digital-type param (its own lit/unlit state already shows everything).
	virtual std::string formatXValue(float value) = 0;
	// The browsed candidate's own human-readable display range/behavior - see
	// XHostInterface::getXParamDisplayMin()'s own comment for the full reasoning. Same
	// resolve-Host-and-clamp-index-with-sane-fallback shape as getXBrowsedParamType()/Color()/
	// Align() above; fallback (0, 1, 0.5, false, "") when nothing meaningful is resolved matches
	// exactly what a disconnected X8's own knob already looks/behaves like.
	virtual float getXBrowsedParamDisplayMin() = 0;
	virtual float getXBrowsedParamDisplayMax() = 0;
	virtual float getXBrowsedParamDisplayDefault() = 0;
	virtual bool getXBrowsedParamSnap() = 0;
	virtual const char* getXBrowsedParamUnit() = 0;

	// One-shot request: the currently-browsed param is a Click type and this channel was just
	// clicked - the Expander owns the actual pulse timing (a fixed duration independent of how
	// long the mouse stays down), this just flags "start one now."
	virtual void requestXValueClick(int channel) = 0;

	// Live "how many hosts, how many total slots" summary - shown on the name display while
	// disconnected (see X8NameDisplay). Since an Expander id may now be bound to at most one
	// candidate, on one Host, anywhere in the rack (see xUnbindExpanderEverywhere(), XShared.hpp),
	// both hostCount and slotCount are always 0 or 1 by construction - kept as counts (not a
	// plain bool) rather than reshaping this method's own signature, since callers already treat
	// them as counts and a stale saved patch could theoretically still have more than one until
	// its first tick re-enforces the invariant. Recomputed fresh every call by scanning every
	// module currently in the rack (Engine::getModuleIds()) for anything implementing
	// XHostInterface and asking each one directly - nothing is remembered between calls, so
	// there's no persisted/stale state at all, and a host that's deleted or has since disengaged
	// everything just contributes 0.
	virtual void getXEngagedSummary(int &hostCount, int &slotCount) = 0;

	// True exactly when this Expander's own knob(s) are known to correctly reflect the Host's
	// held value for candidate `index` right now - i.e. safe for a Host to read
	// getXKnobValue()/getXKnobCount() for that candidate this tick. False for as long as a
	// resync is still pending (right after browsing back onto an already-bound candidate, or a
	// fresh engage) - the Expander clears this the instant it notices its own knob no longer
	// matches `index`, and only sets it again once it has actually pulled the Host's current
	// value into its own knob. A Host must treat false exactly like "not adjacent" (freeze, keep
	// holding its own last value) - this is a plain level check, not a fixed tick count, since
	// Rack's own engine does not guarantee any particular relative ordering between an
	// Expander's and a Host's own, independently-throttled process() calls (see CLAUDE.md's own
	// pitfall entry on the fixed-tick-count approach this replaces).
	virtual bool isXKnobReady(int index) = 0;

	virtual ~XExpanderInterface() {}
};

/**
	Finds the module id of whichever Host currently binds a given Expander id, anywhere in the
	rack - a LAST-RESORT fallback, meant to run at most ONCE per module lifetime (right after a
	fresh patch load, when a binding can already be restored as bound without any attach/push
	event ever having fired this session - see X8ModuleCommon.hpp's own moduleProcess()). Normal,
	live operation never calls this at all: bindings are pushed directly by the Host the instant
	they're granted/revoked (XExpanderInterface::setXBoundHostId()), and both sides proactively
	clear their own reference via onRemove() the instant either one is deleted - so there is
	nothing to rediscover outside the one-time post-load case. Kept as a full-rack scan (not
	something cheaper) specifically because it's now rare enough that the cost doesn't matter -
	see CLAUDE.md's own Pitfalls entry on "resolve via push/id, not a per-tick scan" for the
	fuller history of why this was originally the ONLY mechanism, and why that turned out to be
	needlessly expensive for how rarely a binding actually changes. Returns -1 if nothing
	currently binds this Expander id anywhere (never bound, or the Host that had it bound was
	deleted/reset without the id having been persisted either) - the caller then falls back to
	plain physical adjacency for browsing purposes only.

	`self` is this Expander's own pointer - already resolved once, right here, during this exact
	scan (`host`, matched below) - so both sides' cached pointers get pushed directly at THIS one
	reconnect event (self->setXBoundHostId(id, host), host->recoverXParamBinding(i, expanderId,
	self)) rather than either one ever having to resolve the other again later. This is the ONLY
	place a fresh, no-push-yet binding restored from JSON ever gets its pointers filled in -
	after this, neither side's own moduleProcess() calls APP->engine->getModule() for the bound
	case again, ever, matching every other family's own "resolve only at the actual connect
	event" rule.
*/
inline int64_t findXBoundHostId(int64_t expanderId, XExpanderInterface *self)
{
	for (int64_t id : APP->engine->getModuleIds())
	{
		Module *m = APP->engine->getModule(id);
		XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
		if (!host)
			continue;
		int count = host->getXParamCount();
		for (int i = 0; i < count; i++)
			if (host->getXParamBoundId(i) == expanderId)
			{
				host->recoverXParamBinding(i, expanderId, self);
				self->setXBoundHostId(id, host);
				return id;
			}
	}
	return -1;
}

/**
	Clears every X-family binding for a given Expander id, anywhere in the rack, on every Host
	that currently holds it - enforces the "one candidate, one Host, at a time" invariant (an
	Expander's own connection to a Host can now persist across non-adjacency - see
	X8ModuleCommon.hpp's own moduleProcess() - so binding to a new candidate must first release
	any existing one, everywhere, rather than relying on physical adjacency to arbitrate which
	binding is "live" the way the old multi-host relay technique did). Used directly by
	X8Common.hpp's own XUnbindAllItem menu action, and by Morpheus.cpp's own bind-granting branch
	(called BEFORE setting the new boundExpanderId, while the target slot is still -1, so there's
	no need for a separate "except this one" parameter - the scan simply can't find a match there
	yet).
*/
inline void xUnbindExpanderEverywhere(int64_t expanderId)
{
	for (int64_t id : APP->engine->getModuleIds())
	{
		Module *m = APP->engine->getModule(id);
		XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
		if (!host)
			continue;
		int count = host->getXParamCount();
		for (int i = 0; i < count; i++)
			if (host->getXParamBoundId(i) == expanderId)
				host->resetXParam(i);
	}
}

/**
	Duplicate/selection-load recovery for the X family.

	Neither Rack's own module-level clone action (ModuleWidget::cloneAction()) nor an imported
	selection ever tells a Module it was cloned - both are indistinguishable from an ordinary
	dataFromJson() load from a Module's own point of view (confirmed directly against the Rack
	SDK headers: cloneAction() isn't even virtual, so a plugin can't hook it at all, and "Import
	Selection" is RackWidget::loadSelection() - a wholly separate code path from
	RackWidget::cloneSelectionAction() that goes through the same plain JSON-load mechanism as
	opening a patch). The only signal available at all is OL_selfId (OrangeLineCommon.hpp): Rack
	assigns a brand new `id` on every load, but only a genuine clone/paste ever produces one that
	differs from what was saved - a plain reload of the same patch preserves ids, so OL_selfId
	(written fresh into every dataToJson() call) matches `id` again after an ordinary reload and
	mismatches only right after an actual duplication.

	Design, arrived at after rejecting several more complex alternatives (an origId "birth
	certificate" shared across clone generations, a full rack scan gated on physical adjacency,
	comparing timestamps) - all had either a real ambiguity across repeated duplications of the
	same original, or a race depending on which side's process() tick happened to run first:

	- A Host's own repair pass (tryRecoverXParamSlot(), called once per candidate from Morpheus's
	  own moduleProcess(), gated on the HOST ITSELF being a fresh clone - see xIsFreshClone()) and
	  an Expander's own reverse search (tryRecoverXBinding(), called once from
	  X8ModuleCommon.hpp's own moduleProcess(), gated on the EXPANDER ITSELF being a fresh clone)
	  are fully symmetric and independent - whichever side's own one-time, dataFromJsonCalled-
	  gated check happens to run first performs the actual repair (both sides of the binding, via
	  recoverXParamBinding() + setXBoundHostId()); the side that runs second finds the
	  relationship already correct and does nothing. There is deliberately no "who goes first"
	  branching anywhere - both directions use the exact same underlying facts (a stale
	  boundExpanderId value on one side, a matching stale OL_selfId on the other), so the result
	  is identical regardless of tick order.
	- Both directions gate every candidate they consider - not just themselves - on
	  xIsFreshClone(): a Host only ever repairs its OWN slots if IT is itself a fresh clone (never
	  an unaffected, long-standing Host, which could otherwise be tricked into reassigning a
	  slot away from an Expander that's still perfectly legitimately bound to it elsewhere - the
	  "only the Expander got duplicated, the Host didn't" case); an Expander's reverse search only
	  ever considers a Host that IS itself a fresh clone as a valid target, for the same reason
	  mirrored. This is checked purely from data already valid the instant dataFromJson() runs
	  (OL_selfId, Module::id) - no dependency whatsoever on whether the OTHER side has ticked yet,
	  which is why there is no "wait one tick" grace period or ordering assumption anywhere here
	  (see CLAUDE.md's own pitfall entry on why a fixed-tick assumption is a race, not a fix).
	- Deliberately additive-only, never destructive on its own: if no matching fresh clone is
	  found for a stale slot/binding, both tryRecoverXParamSlot() and tryRecoverXBinding() just
	  leave things exactly as they already were rather than clearing anything - genuine orphan
	  cleanup (the bound module was actually deleted, not cloned) is already handled by the
	  existing onRemove()-based mechanism and needs no help from this one.

	Known, deliberately accepted gap: if only a Host is duplicated (its Expanders are not), the
	clone's own inherited-but-stale candidate slots are left untouched by this mechanism (nothing
	to repair them with) - AND that Host's own refresh loop does not currently re-verify that a
	still-resolvable occupant module actually agrees it's bound here before treating it as
	engaged (it only checks "does this id still resolve to a module at all"). In the ordinary
	case this is harmless (the stale reference just sits there, quietly never read live, since
	the occupant is off browsing candidates on its own, real Host and only coincidentally shares
	an id here); it stops being harmless only in the unlikely event that occupant happens to
	browse to the exact same candidate index the stale slot points at, which could then read a
	value meant for a completely different Host. Fixing this would mean adding a live "does the
	occupant still agree" check to the refresh loop itself (mirroring this mechanism's own
	xIsFreshClone()-style reasoning) - deliberately not done here since it touches already-working,
	delicate live-reading code for an edge case outside what was actually asked for; flagged here
	for a future pass rather than silently left undiscovered.
*/
inline bool xIsFreshClone(int64_t selfId, int64_t realId)
{
	return selfId >= 0 && selfId != realId;
}

/**
	Scans every module for an XExpanderInterface that is itself a fresh clone (xIsFreshClone())
	whose own OL_selfId equals staleExpanderId - i.e. an Expander that used to be, before it was
	duplicated, the very module a stale boundExpanderId value is still referring to. Returns its
	current module id, or -1 if none exists. Called at most once per candidate per Host lifetime
	(gated by dataFromJsonCalled at the call site, Morpheus.cpp) - a full-rack scan is acceptable
	here for the same reason findXBoundHostId() above already established: rare enough (only
	right after an actual duplication) that the cost never matters.
*/
inline int64_t findFreshXExpanderClone(int64_t staleExpanderId)
{
	for (int64_t id : APP->engine->getModuleIds())
	{
		Module *m = APP->engine->getModule(id);
		XExpanderInterface *exp = m ? dynamic_cast<XExpanderInterface*>(m) : nullptr;
		if (exp && exp->getXSelfId() == staleExpanderId && xIsFreshClone(exp->getXSelfId(), id))
			return id;
	}
	return -1;
}

/**
	Host-initiated half of clone recovery - see the architecture comment above. Called once per
	candidate, only while `host` is itself a fresh clone (checked by the caller before looping,
	Morpheus.cpp). If this slot's stale boundExpanderId matches a currently-existing fresh
	Expander clone, repairs both sides of the relationship directly; otherwise leaves the slot
	exactly as it was (see the "additive-only" note above - never clears on its own).
*/
inline void tryRecoverXParamSlot(XHostInterface *host, int index, int64_t hostId)
{
	int64_t staleExpanderId = host->getXParamBoundId(index);
	if (staleExpanderId == -1)
		return;
	int64_t freshId = findFreshXExpanderClone(staleExpanderId);
	if (freshId == -1)
		return;
	Module *m = APP->engine->getModule(freshId);
	XExpanderInterface *exp = m ? dynamic_cast<XExpanderInterface*>(m) : nullptr;
	host->recoverXParamBinding(index, freshId, exp); // cache the pointer directly, resolved once
	                                                  // right here - never resolved again later
	if (exp)
		exp->setXBoundHostId(hostId, host); // also resolves the Expander's own OL_selfId
		                                     // mismatch - see its concrete implementation's own
		                                     // comment; caches the Host pointer directly too
}

/**
	Expander-initiated half of clone recovery - see the architecture comment above. Called at
	most once (gated by dataFromJsonCalled and the Expander's own xIsFreshClone() check at the
	call site, X8ModuleCommon.hpp) while this Expander has no live binding yet. Scans every Host
	for one that is ALSO itself a fresh clone (never a settled, long-standing Host - see the
	architecture comment's own reasoning) with a slot whose stale boundExpanderId still equals
	this Expander's own pre-clone id, and reclaims it directly if found. `selfId` is this
	Expander's own current real id; `staleSelfId` is its own (mismatched) OL_selfId.
*/
inline void tryRecoverXBinding(XExpanderInterface *self, int64_t selfId, int64_t staleSelfId)
{
	for (int64_t id : APP->engine->getModuleIds())
	{
		Module *m = APP->engine->getModule(id);
		XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
		if (!host || !xIsFreshClone(host->getXSelfId(), id))
			continue;
		int count = host->getXParamCount();
		for (int i = 0; i < count; i++)
		{
			if (host->getXParamBoundId(i) != staleSelfId)
				continue;
			host->recoverXParamBinding(i, selfId, self); // cache the pointer directly - already
			                                              // resolved as `self`, no lookup needed
			self->setXBoundHostId(id, host); // also resolves this Expander's own OL_selfId
			                                  // mismatch; caches the Host pointer directly too
			return; // single-binding invariant - never more than one match anywhere
		}
	}
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

// Every other hardcoded color used by the shared X-family widgets in X8Common.hpp - collected
// here (rather than left as scattered nvgRGB() literals) so a future palette tweak only ever
// touches one place. Grouped by meaning, not by which widget happens to use them - several
// widgets share the exact same semantic color (e.g. "inactive/taken" grey).
#define X_BUTTON_FILL_ORANGE nvgRGB(0x10, 0x06, 0x00) // X8ButtonBase's own button-body fill, per
#define X_BUTTON_FILL_DARK   nvgRGB(0x17, 0x17, 0x17) // theme - a separate triplet from
#define X_BUTTON_FILL_BRIGHT nvgRGB(0xbb, 0xbb, 0xbb) // X_STRIP_BG_* even though BRIGHT's own
//#define X_BUTTON_FILL_BRIGHT nvgRGB(0x15, 0x15, 0x2b) // X_STRIP_BG_* even though BRIGHT's own
                                                       // value happens to coincide with
                                                       // X_STRIP_BG_ORANGE (a dark navy fill
                                                       // reads correctly against a light panel)

#define X_FRAME_ORANGE nvgRGB(0x80, 0x33, 0x00) // X8ValueButton's drawThemeFrame() stroke color,
#define X_FRAME_DARK   nvgRGB(0x60, 0x60, 0x60) // per theme (its background fill reuses
#define X_FRAME_BRIGHT nvgRGB(0x60, 0x60, 0x80) // X_STRIP_BG_* directly, no separate constant
                                                 // needed for that half)

// Per-theme accent color for X8ButtonBase/X8BindButtonBase/XOButtonBase's own frame-stroke+label
// while active, replacing a fixed `ORANGE` that only coincidentally matched the Orange theme's
// own value and stayed wrong (still bright orange, never themed) under Dark/Bright. Orange/Dark
// match tools/bake_panel_theme.py's own THEME_TEXT_COLOR (#ff6600/#c4bac4) - Bright deliberately
// does NOT (THEME_TEXT_COLOR's Bright value, #15152b, is meant for text drawn directly on the
// panel's own light background, not for an accent against this button's own dark "display" fill,
// X_BUTTON_FILL_BRIGHT - which is that exact same #15152b, making frame+label invisible against
// their own background). Reuses X_FRAME_BRIGHT (THEME_FRAME_COLOR's Bright value, #606080)
// instead - already the established, confirmed-visible accent color for this family's other
// controls against the same kind of dark display-style surface.
#define X_TEXT_COLOR_ORANGE nvgRGB(0xff, 0x66, 0x00)
#define X_TEXT_COLOR_DARK   nvgRGB(0xc4, 0xba, 0xc4)

// Shared by X8ButtonBase/X8BindButtonBase (X8Common.hpp) and XOButtonBase (XOCommon.hpp) - the
// themed accent color their frame stroke and label text both use while active.
inline NVGcolor xThemedTextColor(float style)
{
	return (style == STYLE_DARK) ? X_TEXT_COLOR_DARK
	     : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT
	     : X_TEXT_COLOR_ORANGE;
}

#define X_COLOR_INACTIVE_GREY nvgRGB(0x55, 0x55, 0x55) // disconnected/taken/unavailable - shared
                                                        // by X8ButtonBase's default accent,
                                                        // X8BindButtonBase's grey state, and
                                                        // X8NameDisplay's "taken" text color
#define X_COLOR_BOUND_RED   nvgRGB(0xdd, 0x00, 0x00)   // X8BindButtonBase - bound to THIS
                                                        // Expander at the browsed slot right now
                                                        // (was green - Dieter's own call)
#define X_COLOR_NO_HOST_RED nvgRGB(0xdd, 0x00, 0x00)   // X8NameDisplay - no Host resolved at all
#define X_VALUE_LIGHT_UNLIT nvgRGB(0x4a, 0x44, 0x3c)   // X8ValueButton's own square LIGHT
                                                        // indicator while unlit

#define X_STRIP_ACCENT_Y_MM 124.71525f
#define X_STRIP_ACCENT_THICKNESS_MM 0.3f

// How far past the bare seam-bridging sliver the whole strip (background fill AND the orange
// accent line together) reaches into EACH of the two neighboring panels - a first-guess tuning
// value (2026-07-16), not yet visually confirmed in Rack. Widen/narrow freely; Dieter reviews
// this live since it's a pixel-perfect panel-art judgement call, not something derivable from
// the code alone. See CLAUDE.md's Expander-modules section for the open coverage question this
// addresses (the accent line needs to visibly continue the panel's own orange line motif on
// both sides, not just bridge the physical gap between the two modules).
#define X_STRIP_SEEM_WIDTH_MM 2.f //1.524f
#define X_STRIP_LINE_ADD_MM   1.f

/**
	Seamless panel-merge strip 
*/
struct XExtStripWidget : Widget
{
	int style = STYLE_ORANGE;
	float topInsetMm = 0.f;
	// true for the Host's own left-edge strip (addXExtStripLeft) - mirrors which direction the
	// background's small alignment offset points, so the same draw() shape works for both the
	// Expander's right-edge strip and the Host's left-edge one without duplicating the logic.
	bool mirror = false;

	void draw(const DrawArgs &args) override
	{
		if (!visible)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, mirror ? -X_STRIP_LINE_ADD_MM : X_STRIP_LINE_ADD_MM, 0.f, box.size.x, box.size.y);
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
	strip->box.pos = mm2px(Vec(panelWidthMm - X_STRIP_SEEM_WIDTH_MM / 2.f , 0.35f));
	strip->box.size = mm2px(Vec(X_STRIP_SEEM_WIDTH_MM + X_STRIP_LINE_ADD_MM, PANELHEIGHT - 0.5f));
	strip->topInsetMm = 0.35f;
	strip->visible = false;
	w->addChild(strip);
	return strip;
}

/**
	Called every widget step() (UI frame rate, cosmetic only). `self` is the widget's own
	module - works even though it's a generic Module* because every X-family module already
	implements XHostInterface or XExpanderInterface (that's what getXNeighborStyle() dynamic_casts
	against). Style still picks the strip's own color; visibility now comes from
	bridgeConnected() (ExpanderBridge.hpp) - do we actually belong to the same connected group as
	this neighbor - rather than merely sharing a theme.
*/
inline void updateXExtStrip(XExtStripWidget *strip, Module *self, Module *rightNeighbor)
{
	float myStyle = getXNeighborStyle(self);
	if (myStyle < 0.f)
		return;
	strip->style = (int) myStyle;
	strip->visible = bridgeConnected(self, rightNeighbor);
}

/**
	Host-side counterpart of addXExtStrip() - a mirror image around a module's own LEFT edge
	(x=0) instead of the right edge (panelWidthMm), same total width/constants. Needed because
	Rack clips a widget's rendering to its own ModuleWidget's bounds: the Expander's right-edge
	strip can only ever cover pixels within the Expander's own panel, never the Host's, so the
	seam only disappears if the Host draws its own matching strip on its own left edge too - see
	CLAUDE.md's Expander-modules section.
*/
inline XExtStripWidget* addXExtStripLeft(ModuleWidget *w)
{
	XExtStripWidget *strip = new XExtStripWidget();
	strip->box.pos = mm2px(Vec(-X_STRIP_SEEM_WIDTH_MM / 2.f - X_STRIP_LINE_ADD_MM, 0.35f));
	strip->box.size = mm2px(Vec(X_STRIP_SEEM_WIDTH_MM + X_STRIP_LINE_ADD_MM, PANELHEIGHT - 0.5f));
	strip->topInsetMm = 0.35f;
	strip->mirror = true;
	strip->visible = false;
	w->addChild(strip);
	return strip;
}

// See updateXExtStrip() above - identical logic, just checking the LEFT neighbor instead.
inline void updateXExtStripLeft(XExtStripWidget *strip, Module *self, Module *leftNeighbor)
{
	float myStyle = getXNeighborStyle(self);
	if (myStyle < 0.f)
		return;
	strip->style = (int) myStyle;
	strip->visible = bridgeConnected(self, leftNeighbor);
}

#endif
