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

// Resolved every moduleProcess() tick by looking only at rightExpander.module (never
// leftExpander.module - see XShared.hpp's left-only-attachment note).
XHostInterface *xHost = nullptr;

// Fully self-managed local state (see ExpanderParamAccessSpec.md's "Expander manages
// itself completely") - the Host only ever reads these through XExpanderInterface, it
// never writes them. The currently-browsed index itself lives in OL_state[BROWSE_INDEX_JSON]
// (not a plain member) so it round-trips through the normal JSON persistence automatically,
// same as CHANNEL_LIMIT_JSON - a separate cached member would go stale the moment
// dataFromJson() restores OL_state without also updating it.
dsp::SchmittTrigger engageTrigger, leftTrigger, rightTrigger;
bool pendingEngagePress = false;

// One-shot guard: attempt, exactly once after a compatible Host first resolves, to
// auto-restore an engagement that existed before a patch reload - see WAS_ENGAGED_JSON's
// comment in X8.hpp and the auto-reengage block in moduleProcess().
bool triedAutoReengage = false;

// Edge-detects "the param I'm currently standing on just became bound to me" (a fresh
// engage taking effect on the Host's side, possibly a few ticks after the physical press -
// Host and Expander each run on their own throttled cycle) so knobs can be snapped exactly
// once. Keyed to the index itself, not just a bare bool: browsing away and back onto a
// param this Expander already holds (Track & Hold) must NOT re-fire and stomp live knob
// positions with the old snapshot - only a genuine unbound->bound transition while standing
// still on the same index counts.
bool xLastBoundHere = false;
int xLastCheckedIndex = -1;

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
std::string formatXValue(float raw) override
{
	if (!xHost)
		return "";
	int count = xHost->getXParamCount();
	if (count <= 0)
		return "";
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xHost->formatXParamValue(idx, raw);
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
	setJsonLabel(WAS_ENGAGED_JSON, "wasEngaged");

#pragma GCC diagnostic pop
}

inline void moduleParamConfig()
{
	for (int i = 0; i < NUM_X8_KNOBS; i++)
		configParam<X8KnobQuantity>(KNOB_PARAM + i, 0.f, 1.f, 0.5f, "Value");
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
	OL_state[WAS_ENGAGED_JSON] = 0.f;
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
	// time, each tracked independently via that Host's own boundExpanderId/adjacency-gating (see
	// CLAUDE.md's Expander-modules section) - nothing here needs to know or care what kind of
	// Host it's currently looking at.
	xHost = resolveXHost(rightExpander.module);
	setStateLight(CONN_LIGHT, xHost ? 255.f : 0.f);

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

	// See xLastBoundHere's comment above: only a fresh bind while standing still on the
	// same index snaps the knobs - browsing onto an already-bound index never re-fires.
	if (xHost && count > 0)
	{
		bool boundHere = xHost->getXParamBoundId(browseIndex) == (int64_t) this->id;

		// Auto-restore an engagement that existed before a patch reload: bindings themselves
		// are session-only (Rack ids aren't safe to persist/compare across a reload - see
		// WAS_ENGAGED_JSON's comment), so this just presses our own engage button once,
		// through the ordinary mechanism, using whatever id this Expander currently has.
		if (!triedAutoReengage)
		{
			triedAutoReengage = true;
			if (OL_state[WAS_ENGAGED_JSON] > 0.f && !boundHere)
				pendingEngagePress = true;
		}

		if (browseIndex == xLastCheckedIndex && boundHere && !xLastBoundHere)
		{
			int channels = getXKnobCount();
			for (int c = 0; c < channels; c++)
				params[KNOB_PARAM + c].setValue(xHost->getXParamTakeoverValue(browseIndex, c));
		}
		xLastBoundHere = boundHere;
		xLastCheckedIndex = browseIndex;
		OL_state[WAS_ENGAGED_JSON] = boundHere ? 1.f : 0.f;
	}
	else
	{
		xLastBoundHere = false;
		xLastCheckedIndex = -1;
	}

	// Engage button: local debounce only - this Expander has no idea whether a click will
	// actually bind, unbind, or do nothing at all. The Host decides that, during its own
	// process(), the next time it reads consumeEngagePress().
	if (engageTrigger.process(params[ENGAGE_PARAM].getValue()))
		pendingEngagePress = true;

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

// XExpanderInterface
XHostInterface* getXHost() override { return xHost; }
float getXStyle() override { return OL_state[STYLE_JSON]; }
int getXKnobCount() override { return (int) OL_state[CHANNEL_LIMIT_JSON]; }
float getXKnobValue(int channel) override { return getStateParam(KNOB_PARAM + channel); }
int getXBrowseIndex() override { return (int) OL_state[BROWSE_INDEX_JSON]; }
bool consumeEngagePress() override
{
	bool fired = pendingEngagePress;
	pendingEngagePress = false;
	return fired;
}
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
