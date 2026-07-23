/*
	X8ModuleCommon.hpp

	Shared module-struct body for X8 and X8D - #include'd literally inside both `struct X8 :
	Module, XExpanderInterface { ... }` and `struct X8D : Module, XExpanderInterface { ... }`,
	same composition pattern as `OrangeLineCommon.hpp` itself. The two modules are otherwise
	~95% identical code (same engagement/browsing/reload-persistence logic, same
	XExpanderInterface implementation) - only the panel layout (widget-side, in each module's own
	.cpp) and physical panel width genuinely differ.

	NOT included here: the constructor. A C++ constructor's name must exactly match its
	enclosing class, so `X8()`/`X8D()` can't be shared via a plain textual #include - each
	module's own .cpp keeps its own few-line constructor (content otherwise identical).

	Requires `NUM_X8_KNOBS` (from X8.hpp, #include'd by both X8.hpp and - via X8D.cpp's own
	`#include "X8.hpp"` - X8D as well, rather than each declaring their own separate macro).
*/

// X_VALUE_CLICK_SECONDS (the fixed pulse duration for a Click-type value button) is now a global
// constant in OrangeLine.hpp, not defined per-family here.

bool widgetReady = false;

// Pushed directly by whichever Host currently binds this Expander (Morpheus.cpp's own
// resetXParam()/bind-granting code, via setXBoundHostId() - XShared.hpp) - -1 when not bound
// anywhere. Both sides proactively clear this to -1 via their own onRemove() the instant either
// one is deleted (see this file's own onRemove() below and Morpheus.cpp's own onRemove()/
// resetXParam()) - so a stale id should never even be observed in normal operation. Replaces a
// per-tick full-rack scan entirely: binding changes are rare and Host-initiated, so there's no
// need to ask the whole rack every tick just to answer a question that only changes a few times
// per session.
int64_t xBoundHostId = -1;
// Cached Host pointer for xBoundHostId above - pushed directly by setXBoundHostId() below at the
// exact moment a bind is granted or restored, NEVER re-resolved via APP->engine->getModule() on
// any other tick (see setXBoundHostId()'s own comment, XShared.hpp, for why: that used to be a
// per-tick resolve and turned out to be a confirmed, live engine deadlock - a share-locking
// getModule() call from inside moduleProcess() racing a queued exclusive lock request during a
// module add/remove). Safe to cache permanently because both sides' onRemove() already clear
// xBoundHostId (and, via the same setXBoundHostId(-1) call, this pointer too) the instant either
// side is deleted - nothing can ever dangle.
XHostInterface *boundHost = nullptr;

// Live, NON-persisted "connection" for browsing only when not bound anywhere - recomputed fresh
// every moduleProcess() tick via ExpanderBridge.hpp's resolveBridgeHostId() (both sides, crossing
// any number of already-connected intermediate Expanders since each one's own getBridgeHostId()
// already reflects wherever it ended up). Deliberately NOT gated/cached via onExpanderChange like
// the exclusive bind is - an X-family Expander that was never bound simply loses this the instant
// physical adjacency is (Dieter's own call, unlike XO-family/NEO/LANES, which persist their own
// mere connection once touched). See ExpanderBridge.hpp's own file comment for the full
// per-family persistence-policy reasoning.
int64_t xConnectedId = -1;

// Resolved every moduleProcess() tick: the Host resolved from xBoundHostId if bound anywhere
// (regardless of physical position), else from xConnectedId (browsing only). See moduleProcess()'s
// own resolution block for the full reasoning, including the one place a full-rack scan
// (findXBoundHostId()) still runs - once, to re-establish xBoundHostId right after a patch load,
// where no push event ever fired this session.
XHostInterface *xHost = nullptr;

// Fully self-managed local state (see ExpanderParamAccessSpec.md's "Expander manages
// itself completely") - the Host only ever reads these through XExpanderInterface, it
// never writes them. The currently-browsed index itself lives in OL_state[BROWSE_INDEX_JSON]
// (not a plain member) so it round-trips through the normal JSON persistence automatically,
// same as CHANNEL_LIMIT_JSON - a separate cached member would go stale the moment
// dataFromJson() restores OL_state without also updating it.
dsp::SchmittTrigger engageTrigger, leftTrigger, rightTrigger;

// Edge-detects "the param I'm currently standing on just became bound to me, or I just
// arrived on it" so knobs can be resynced. Keyed to the index itself: browsing away and back
// onto a param this Expander already holds (Track & Hold) DOES need to re-resync (the knob is
// one shared physical Param reused for whatever's currently displayed, so after browsing
// elsewhere and back it's showing the wrong candidate's last value) - continuing to stand on
// the same already-bound index tick after tick must NOT re-fire, or live knob turning would
// be impossible.
bool xLastBoundHere = false;
int xLastCheckedIndex = -1;

// Tracks whether the knob's own minValue/maxValue/defaultValue/snapEnabled are already
// reconfigured for whatever (xHost, browseIndex) pair is current - see moduleProcess()'s own
// range-reconfiguration block. Compared against xHost directly (not just the index) since
// switching to a DIFFERENT Host while somehow landing on the same numeric index must still
// re-trigger a reconfiguration.
XHostInterface *xLastRangeHost = nullptr;

// See XExpanderInterface::isXKnobReady()'s own comment (XShared.hpp) - false for exactly the
// span between "just arrived on a (possibly bound) index" and "finished pulling the Host's
// held value into the knob", true otherwise. A Host must not read getXKnobValue() while this
// is false. Correctness-based (a level the Host polls every tick, however many it takes),
// not a fixed tick count - see CLAUDE.md's own pitfall entry on why a fixed-count grace
// period on the Host side alone was tried and reverted the same day this replaced it.
bool xKnobReady = true;

// X_PARAM_CLICK needs a fixed-length pulse "independent of hold duration" (XShared.hpp) -
// unlike TOGGLE/PUSH, which the widget can drive directly via plain setValue() calls, CLICK
// needs module-owned timing so the value (and therefore the light, which just mirrors it -
// see X8ValueButton) always drops back to 0 after a fixed duration regardless of how long
// the mouse stays down. pendingValueClick[c] is a one-shot request the widget sets on press;
// clickPulse[c] is the actual timer, consumed once per tick in moduleProcess().
dsp::PulseGenerator clickPulse[NUM_X8_KNOBS];
bool pendingValueClick[NUM_X8_KNOBS] = {false};

// The type governing all 8 channels right now - one type applies to the whole browsed param,
// never per-channel. Defaults to continuous (knob) whenever nothing meaningful is resolved,
// so callers never need their own separate "no Host" fallback.
XParamType getXBrowsedParamType() override
{
	if (!xHost)
		return X_PARAM_CONTINUOUS;
	int count = xHost->getXParamCount();
	if (count <= 0)
		return X_PARAM_CONTINUOUS;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->getXParamType(idx);
}

// The Host's own per-slot accent color for whatever's currently browsed - see
// XHostInterface::getXParamColor(). Falls back to the fixed panel ORANGE when nothing
// meaningful is resolved, same reasoning as getXBrowsedParamType() above.
NVGcolor getXBrowsedParamColor() override
{
	if (!xHost)
		return ORANGE;
	int count = xHost->getXParamCount();
	if (count <= 0)
		return ORANGE;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->getXParamColor(idx);
}

// The Host's own alignment choice for whatever's currently browsed - see
// XHostInterface::getXParamAlign(). Falls back to left when nothing meaningful is resolved,
// same reasoning as getXBrowsedParamType()/getXBrowsedParamColor() above.
XAlign getXBrowsedParamAlign() override
{
	if (!xHost)
		return X_ALIGN_LEFT;
	int count = xHost->getXParamCount();
	if (count <= 0)
		return X_ALIGN_LEFT;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->getXParamAlign(idx);
}

// The single formatting function shared by the knob's hover tooltip (X8KnobQuantity, in
// X8Common.hpp) and X8D's per-channel numeric display - both must always show identical text
// for the same raw value, so both call through this one place rather than each separately
// resolving xHost/browseIndex and calling formatXParamValue() themselves. Empty string (no
// tooltip/display text at all) whenever nothing meaningful is resolved, or for a digital-type
// param (its own lit/unlit state already shows everything there is to show).
std::string formatXValue(float value) override
{
	if (!xHost)
		return "";
	int count = xHost->getXParamCount();
	if (count <= 0)
		return "";
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->formatXParamValue(idx, value);
}

// See XHostInterface::getXParamDisplayMin()'s own comment (XShared.hpp) - resolve-and-fallback
// shape identical to getXBrowsedParamType()/Color()/Align() above. Fallback (0, 1, 0.5, false,
// "") matches exactly what a disconnected X8's own knob already looks/behaves like.
float getXBrowsedParamDisplayMin() override
{
	if (!xHost)
		return 0.f;
	int count = xHost->getXParamCount();
	if (count <= 0)
		return 0.f;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->getXParamDisplayMin(idx);
}
float getXBrowsedParamDisplayMax() override
{
	if (!xHost)
		return 1.f;
	int count = xHost->getXParamCount();
	if (count <= 0)
		return 1.f;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->getXParamDisplayMax(idx);
}
float getXBrowsedParamDisplayDefault() override
{
	if (!xHost)
		return 0.5f;
	int count = xHost->getXParamCount();
	if (count <= 0)
		return 0.5f;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->getXParamDisplayDefault(idx);
}
bool getXBrowsedParamSnap() override
{
	if (!xHost)
		return false;
	int count = xHost->getXParamCount();
	if (count <= 0)
		return false;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->getXParamSnap(idx);
}
const char* getXBrowsedParamUnit() override
{
	if (!xHost)
		return "";
	int count = xHost->getXParamCount();
	if (count <= 0)
		return "";
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->getXParamUnit(idx);
}

bool moduleSkipProcess()
{
	return (idleSkipCounter != 0);
}

void moduleInitStateTypes()
{
}

inline void moduleInitJsonConfig()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

	setJsonLabel(STYLE_JSON, "style");
	setJsonLabel(CHANNEL_LIMIT_JSON, "channelLimit");
	setJsonLabel(BROWSE_INDEX_JSON, "browseIndex");

#pragma GCC diagnostic pop
}

inline void moduleParamConfig()
{
	for (int i = 0; i < NUM_X8_KNOBS; i++)
		// Deliberately a generous placeholder range (-1000..1000), not the candidate-specific
		// range this knob actually gets reconfigured to every tick (moduleProcess()'s own range-
		// reconfiguration block) - Rack's own patch-load restoration writes the saved value into
		// this param BEFORE any of our code ever runs, and clamps it to whatever range is
		// configured AT THAT EXACT MOMENT. A tight placeholder (e.g. the old 0..1) silently
		// clamped a real saved value (e.g. GTP's own 0..100 range, a value like 48) down to the
		// placeholder's own max the instant a patch loaded, before moduleProcess() ever got a
		// chance to widen the range back out - by then the damage was already done, since
		// widening minValue/maxValue afterward does not un-clamp an already-corrupted value. No
		// real candidate's own display range gets anywhere near +-1000 (LOOP_LEN's 1..128 and
		// LOCK/BALANCE's -100..100 are the widest today), so this placeholder can never clamp a
		// genuine saved value, regardless of which candidate this knob turns out to be reconfigured
		// for once the Host resolves.
		configParam<X8KnobQuantity>(KNOB_PARAM + i, -1000.f, 1000.f, 0.5f, "Value");
	configParam(LEFT_PARAM, 0.f, 1.f, 0.f, "Previous parameter");
	configParam(RIGHT_PARAM, 0.f, 1.f, 0.f, "Next parameter");
	configParam(ENGAGE_PARAM, 0.f, 1.f, 0.f, "Bind");
}

// moduleCustomInitialize() runs on every single non-skipped process() tick (not just once -
// see initialize() in OrangeLineCommon.hpp), so it must never be used for a one-time
// default - see the constructor above instead for CHANNEL_LIMIT_JSON's default.
inline void moduleCustomInitialize() {}
inline void moduleInitialize() {}

void moduleReset()
{
	styleChanged = true;
	OL_state[BROWSE_INDEX_JSON] = 0.f;
}

inline void moduleProcess(const ProcessArgs &args)
{
	if (styleChanged && widgetReady)
	{
		switch (int(getStateJson(STYLE_JSON)))
		{
		case STYLE_ORANGE: brightPanel->visible = false; darkPanel->visible = false; break;
		case STYLE_BRIGHT: brightPanel->visible = true;  darkPanel->visible = false; break;
		case STYLE_DARK:   brightPanel->visible = false; darkPanel->visible = true;  break;
		}
		styleChanged = false;
	}

	// No restriction on which Host TYPE this resolves to - an Expander can be engaged with any
	// number of different Hosts (of the same or different types) over its lifetime, one at a
	// time, tracked via that Host's own boundExpanderId (see CLAUDE.md's Expander-modules
	// section) - nothing here needs to know or care what kind of Host it's currently looking at.
	//
	// Bound-first (pushed id AND pointer, never polled/scanned): if some Host currently binds
	// this Expander, xBoundHostId/boundHost already hold its module id and pointer directly -
	// pushed by that Host itself the moment the binding was granted (setXBoundHostId(), which
	// also caches the pointer - see its own comment) - no per-tick resolution of any kind here
	// anymore. This used to re-resolve boundHost fresh via APP->engine->getModule() every tick,
	// which turned out to be a confirmed, live engine deadlock (gdb-verified, same class as the
	// ExpanderBridge/XOD8 one found earlier this session) - per Dieter's own call, there is no
	// reason for this to ever resolve anything outside the exact moment a connection is actually
	// made or restored. The ONE exception needing a scan at all: right after a fresh patch load
	// (dataFromJsonCalled, OrangeLineCommon.hpp), a binding can already be restored as bound
	// without any push event ever having fired this session - so on that one tick only, fall back
	// to a full-rack scan (findXBoundHostId(), XShared.hpp) which resolves AND pushes both
	// xBoundHostId and boundHost together, exactly as if the Host had just pushed it live.
	// Clone/selection-load recovery: at most once, right after a fresh load, while this
	// Expander is both unbound and itself a fresh clone (OL_selfId, OrangeLineCommon.hpp,
	// mismatches this module's own real id) - see XShared.hpp's own architecture comment on
	// tryRecoverXBinding() for the full reasoning. Deliberately checked BEFORE the ordinary
	// findXBoundHostId() fallback right below: if this succeeds, xBoundHostId/boundHost are
	// already pushed and that fallback's own `xBoundHostId == -1` guard correctly skips.
	if (dataFromJsonCalled && xBoundHostId == -1 && xIsFreshClone(OL_selfId, (int64_t) this->id))
		tryRecoverXBinding(this, (int64_t) this->id, OL_selfId);
	if (dataFromJsonCalled && xBoundHostId == -1)
		findXBoundHostId((int64_t) this->id, this); // pushes xBoundHostId + boundHost itself if
		                                             // found; a no-op (still -1) otherwise
	// No defensive "did the target vanish without telling us" re-check here anymore - that would
	// need its own getModule() call, exactly what this whole rework removes. Both sides' onRemove()
	// already proactively push setXBoundHostId(-1) the instant either one is deleted, so boundHost
	// is trusted as-is between those events.
	// Not bound anywhere: re-derive a live, non-persisted browsing connection fresh every tick,
	// from BOTH sides now (ExpanderBridge.hpp's resolveBridgeHostId() - no more left/right
	// restriction, no more continuously-relayed chain, see XShared.hpp's own file comment). Lost
	// the instant physical adjacency is, since it was never bound - this Expander's own choice,
	// not shared by XO-family/NEO/LANES (see ExpanderBridge.hpp's own persistence-policy note).
	XHostInterface *connectedHost = nullptr;
	if (boundHost)
		xConnectedId = -1; // bound takes over entirely - no need to track a separate connection
	else
	{
		xConnectedId = resolveBridgeHostId({ FAMILY_X }, leftExpander.module, rightExpander.module);
		if (xConnectedId != -1)
		{
			Module *m = APP->engine->getModule(xConnectedId);
			connectedHost = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
			if (!connectedHost)
				xConnectedId = -1;
		}
	}
	xHost = boundHost ? boundHost : connectedHost;

	// Browsing: unconditional, unfiltered stepping through every candidate param the
	// currently-resolved Host reports - see "Browsing is never locked or filtered" in
	// ExpanderParamAccessSpec.md. If no Host is resolved (or it has zero candidates),
	// browseIndex just stays put and stepping has no visible effect. Lives in
	// OL_state[BROWSE_INDEX_JSON] (not a plain member) so it survives a patch reload - see
	// that json id's own comment in X8.hpp.
	int browseIndex = (int) OL_state[BROWSE_INDEX_JSON];
	int count = xHost ? xHost->getXParamCount() : 0;
	if (count > 0)
	{
		browseIndex = clamp(browseIndex, 0, count - 1);
		if (leftTrigger.process(params[LEFT_PARAM].getValue()))
			browseIndex = (browseIndex - 1 + count) % count;
		if (rightTrigger.process(params[RIGHT_PARAM].getValue()))
			browseIndex = (browseIndex + 1) % count;
	}
	else
	{
		// Deliberately NOT reset to 0 here - reconnecting (to the same Host, or another of
		// the same type) should land back on the same param it was last browsing, not jump
		// back to the first candidate. Gets clamped into range again above once a Host with
		// count > 0 resolves, so a narrower Host can't read this out of bounds.
		leftTrigger.process(params[LEFT_PARAM].getValue());
		rightTrigger.process(params[RIGHT_PARAM].getValue());
	}
	OL_state[BROWSE_INDEX_JSON] = (float) browseIndex;

	// See xLastBoundHere's comment above: resync whenever we just arrived on a bound-here
	// index (either a fresh engage, or navigating back onto one) - safe to do on every
	// arrival now, since isXKnobReady() (XShared.hpp) lets the Host wait however many
	// ticks this side actually needs, rather than assuming a fixed count.
	if (xHost && count > 0)
	{
		bool boundHere = xHost->getXParamBoundId(browseIndex) == (int64_t) this->id;

		// Reconfigure the knob's own minValue/maxValue/defaultValue/snapEnabled to whatever the
		// NEWLY-browsed candidate declares, in the SAME tick as (and strictly before) the value
		// resync/preview below - so range and value always change together, never leaving a tick
		// where a stale value is read against a fresh range or vice versa. Mirrors Dejavu.cpp's
		// own reconfigureForState() pattern (OrangeLine.hpp's reConfigParam* macros) - reassigning
		// plain float/bool ParamQuantity fields from moduleProcess() (the audio/engine thread) is
		// already an established, working pattern in this codebase. Deliberately does NOT touch
		// `unit` here (a std::string) - see X8Knob::step()'s own comment for why that half is
		// UI-thread-only.
		if (xHost != xLastRangeHost || browseIndex != xLastCheckedIndex)
		{
			float dispMin = xHost->getXParamDisplayMin(browseIndex);
			float dispMax = xHost->getXParamDisplayMax(browseIndex);
			float dispDefault = xHost->getXParamDisplayDefault(browseIndex);
			bool dispSnap = xHost->getXParamSnap(browseIndex);
			for (int c = 0; c < NUM_X8_KNOBS; c++)
			{
				ParamQuantity *pq = paramQuantities[KNOB_PARAM + c];
				pq->minValue = dispMin;
				pq->maxValue = dispMax;
				pq->defaultValue = dispDefault;
				pq->snapEnabled = dispSnap;
			}
			xLastRangeHost = xHost;
		}

		bool arrived = (browseIndex != xLastCheckedIndex) || (boundHere && !xLastBoundHere);
		if (boundHere)
		{
			if (arrived)
			{
				xKnobReady = false; // not observable outside this same tick (resnap below
				                    // completes synchronously), but keeps the state machine
				                    // explicit - see isXKnobReady()'s own comment
				// On the very first tick after a fresh patch load/reload (dataFromJsonCalled,
				// OrangeLineCommon.hpp - stays true for this entire tick, reset after), if this
				// exact binding already survived the reload (boundHere true without ever having
				// pressed Engage this session), do NOT resync FROM the Host - the knob is a real
				// Rack param Rack already restored directly from the saved patch, the ONLY
				// durable memory of this channel's actual value (the Host's own OL_statePoly is
				// explicitly session-only, never persisted). Pulling a "takeover" value here
				// would silently discard that correctly-restored value in favor of whatever the
				// Host's own unrelated fallback (e.g. its real panel knob) happens to hold right
				// now. A genuine fresh engage/re-arrival later in the same session
				// (dataFromJsonCalled false by then) still correctly pulls from the Host exactly
				// as before - only the just-loaded tick is different.
				if (!dataFromJsonCalled)
				{
					int channels = getXKnobCount();
					for (int c = 0; c < channels; c++)
						params[KNOB_PARAM + c].setValue(xHost->getXParamTakeoverValue(browseIndex, c));
				}
				xKnobReady = true;
			}
		}
		else
		{
			// Not bound here - continuously preview whatever value WOULD be taken over if
			// bound, every tick (not just on arrival), so the disabled knob visibly tracks
			// the Host's own live value (e.g. turning Morpheus's own real panel knob updates
			// this disabled knob's display in real time). Safe unconditionally: nothing else
			// ever writes to this Expander's own knob param while it isn't the bound owner -
			// Morpheus's own refresh loop only ever touches a bound Expander's knob.
			int channels = getXKnobCount();
			for (int c = 0; c < channels; c++)
				params[KNOB_PARAM + c].setValue(xHost->getXParamTakeoverValue(browseIndex, c));
		}
		xLastBoundHere = boundHere;
		xLastCheckedIndex = browseIndex;
	}
	else
	{
		xLastBoundHere = false;
		xLastCheckedIndex = -1;
		xLastRangeHost = nullptr;
	}

	// Engage button: calls straight into whatever Host we already resolved (xHost, above) the
	// instant the press edge is detected - no separate debounce-and-wait-for-the-Host-to-poll-us
	// step anymore (see XHostInterface::requestXBind()'s own comment, XShared.hpp, for why this
	// replaces the old physical-only chain-walk entirely). Works identically whether xHost came
	// from physical adjacency or a persistently-connected-but-detached neighbor further down the
	// chain - both are the exact same resolution browsing already trusted.
	if (engageTrigger.process(params[ENGAGE_PARAM].getValue()) && xHost)
		xHost->requestXBind((int) OL_state[BROWSE_INDEX_JSON], (int64_t) this->id, this);

	// X_PARAM_CLICK's fixed-length pulse, independent of hold duration - see
	// pendingValueClick's own comment above. Only touches the params while the browsed type
	// is actually CLICK, so it never fights TOGGLE/PUSH's own direct widget-driven values.
	if (getXBrowsedParamType() == X_PARAM_CLICK)
	{
		for (int c = 0; c < NUM_X8_KNOBS; c++)
		{
			if (pendingValueClick[c])
			{
				pendingValueClick[c] = false;
				clickPulse[c].trigger(X_VALUE_CLICK_SECONDS);
			}
			// moduleProcess() only runs on ~1 in idleSkip samples (control-rate throttling),
			// so a bare args.sampleTime here would underestimate elapsed real time by that
			// same factor, making the pulse take ~43x longer than intended - see CLAUDE.md's
			// "dsp::Timer inside moduleProcess() must scale by (samplesSkipped + 1)" pitfall.
			params[KNOB_PARAM + c].setValue(clickPulse[c].process(args.sampleTime * (samplesSkipped + 1)) ? 1.f : 0.f);
		}
	}
}

inline void moduleProcessState() {}
inline void moduleReflectChanges() {}

// Proactively clears this Expander's own binding on whichever Host holds it - using ONLY the
// stable id already known (xBoundHostId), a single targeted lookup, never a rack-wide scan -
// right before this Expander is actually destroyed, so the Host is never left holding a
// boundExpanderId that points at a module which no longer exists. Symmetric with Morpheus's own
// onRemove() (Morpheus.cpp), which does the same in the other direction when a Host is deleted -
// between the two, neither side can ever be left with a stale/dangling reference to the other.
//
// Rack's own removeModule() holds an exclusive lock for the ENTIRE duration of this callback, so
// EVERY engine lookup anywhere in this call chain must use the *_NoLock variant - both the direct
// getModule() call right here AND the Host's own clearing method (resetXParamNoLock(), not the
// ordinary resetXParam(), which internally uses the regular locking getModule() and would
// deadlock if reached from here). See XHostInterface::resetXParamNoLock()'s own interface comment
// (XShared.hpp) and CLAUDE.md's Pitfalls entry for the full reasoning - a share-locking call from
// within an already-exclusively-locked callback is a guaranteed self-deadlock, regardless of how
// many function calls deep it happens.
void onRemove(const RemoveEvent &e) override
{
	if (xBoundHostId != -1)
	{
		Module *m = APP->engine->getModule_NoLock(xBoundHostId);
		XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
		if (host)
		{
			int count = host->getXParamCount();
			for (int i = 0; i < count; i++)
				if (host->getXParamBoundId(i) == (int64_t) this->id)
				{
					host->resetXParamNoLock(i); // NOT resetXParam() - see comment above
					break; // single-binding invariant - never more than one match
				}
		}
	}
	Module::onRemove(e);
}

// XExpanderInterface
XHostInterface* getXHost() override { return xHost; }
// A real (non -1) hostId means an actual grant just happened - whether a normal manual Engage
// or the clone-recovery mechanism's own reclaim (XShared.hpp's tryRecoverXBinding()) - either
// way this Expander's own clone-recovery relevance is now resolved (it has a live, legitimate
// binding again), so OL_selfId is brought back in sync with reality right here, the single
// choke point every bind grant already goes through. Left untouched on an unbind (hostId == -1)
// - nothing to resolve there.
void setXBoundHostId(int64_t hostId, XHostInterface *hostPtr = nullptr) override
{
	xBoundHostId = hostId;
	boundHost = hostPtr; // cached directly - the only place this is ever set
	if (hostId != -1)
		OL_selfId = (int64_t) this->id;
}
int64_t getXBoundHostId() override { return xBoundHostId; }
int64_t getXSelfId() override { return OL_selfId; }
float getXStyle() override { return OL_state[STYLE_JSON]; }

// ExpanderBridgeInterface (ExpanderBridge.hpp) - bound takes priority (a real, exclusive grant),
// falling back to the live, non-persisted browsing connection - this is exactly what makes a
// merely-connected (not bound) X-family Expander still relay onward to a further neighbor
// touching it, same as a bound one already did.
int64_t getBridgeHostId() override { return xBoundHostId != -1 ? xBoundHostId : xConnectedId; }
std::vector<ExpanderFamily> getBridgeFamilies() override { return getModuleFamilies(model->slug); }
std::string getBridgeHostName() override { return ""; } // Expander, not a Host - nothing to name
int getXKnobCount() override { return (int) OL_state[CHANNEL_LIMIT_JSON]; }
float getXKnobValue(int channel) override { return getStateParam(KNOB_PARAM + channel); }
int getXBrowseIndex() override { return (int) OL_state[BROWSE_INDEX_JSON]; }
bool isXKnobReady(int index) override { return xKnobReady && index == (int) OL_state[BROWSE_INDEX_JSON]; }
void requestXValueClick(int channel) override { pendingValueClick[channel] = true; }

// See XExpanderInterface::getXEngagedSummary()'s own comment - live, no persisted memory at
// all: scans every module currently in the rack (Engine::getModuleIds(), Dieter's own idea) for
// anything implementing XHostInterface and asks each one directly whether any of its own
// candidates are bound to this Expander's id. Nothing here needs to survive a reload or worry
// about stale ids, since it never remembers anything between calls - it just asks fresh, every
// time. UI-thread only (drawLayer(), never audio-rate), so an O(modules x candidates) scan is
// fine for realistic patch sizes; revisit with throttling only if it ever actually shows up as
// a real cost in a very large patch.
void getXEngagedSummary(int &hostCount, int &slotCount) override
{
	hostCount = 0;
	slotCount = 0;
	for (int64_t moduleId : APP->engine->getModuleIds())
	{
		Module *m = APP->engine->getModule(moduleId);
		XHostInterface *host = m ? dynamic_cast<XHostInterface*>(m) : nullptr;
		if (!host)
			continue;
		int count = 0;
		int paramCount = host->getXParamCount();
		for (int i = 0; i < paramCount; i++)
			if (host->getXParamBoundId(i) == (int64_t) this->id)
				count++;
		if (count > 0)
		{
			hostCount++;
			slotCount += count;
		}
	}
}
