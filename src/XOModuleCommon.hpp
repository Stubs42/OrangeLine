/*
	XOModuleCommon.hpp

	Shared module-struct body for the whole XO family (XO8/XD8/XOD8/XO16/XD16/XOD16) -
	#include'd literally inside every one of their own `struct <Name> : Module,
	XOExpanderInterface { ... }`, right after OrangeLineCommon.hpp, same composition pattern as
	X8ModuleCommon.hpp itself. Much simpler than X8ModuleCommon.hpp: no engagement/binding at all
	(reading a Host's output is never exclusive), so no Engage/Bind trigger, no takeover-snap
	logic, no auto-reengage-after-reload, no per-channel click-pulse timing.

	NOT included here: the constructor (same reasoning as X8ModuleCommon.hpp - a C++
	constructor's name must match its enclosing class).

	Requires `XO_CAPACITY` (8 or 16, from each module's own <Name>.hpp) and, only for the four
	jack-bearing modules (XO8/XOD8/XO16/XOD16), `XO_HAS_JACKS` defined before this file's own
	first use - gates the one block that writes real output voltages, so this single shared
	header still covers the two display-only modules (XD8/XD16) as well.
*/

bool widgetReady = false;

// Resolved every moduleProcess() tick by looking only at leftExpander.module (mirrored
// direction from the X family's own xHost - this family attaches to a Host's RIGHT side and
// chains further rightward among itself, so a Host is always reachable through the LEFT).
XOHostInterface *xoHost = nullptr;

dsp::SchmittTrigger leftTrigger, rightTrigger;

// Gate/trigger display stretch - see XOExpanderInterface::getXOBrowsedChannelGateLit()'s own
// comment for why a raw instantaneous read isn't enough. Sized to XO_CAPACITY (this module's own
// fixed display-cell count), indexed the same way the widget's own `channel` is - i.e. against
// the CURRENTLY BROWSED slot's channels, not against some fixed absolute channel identity, so
// switching slots naturally starts every cell's own edge-memory fresh (no stale carry-over).
dsp::PulseGenerator xoGateFlashPulse[XO_CAPACITY];
bool xoGateFlashAbove[XO_CAPACITY] = {false};
bool xoGateFlashLit[XO_CAPACITY] = {false};

// The type governing all channels right now - one type applies to the whole browsed slot, never
// per-channel. Defaults to continuous whenever nothing meaningful is resolved, so callers never
// need their own separate "no Host" fallback - same reasoning as X8ModuleCommon.hpp's own
// getXBrowsedParamType().
XOType getXOBrowsedType() override
{
	if (!xoHost)
		return XO_TYPE_CONTINUOUS;
	int count = xoHost->getXOCount();
	if (count <= 0)
		return XO_TYPE_CONTINUOUS;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xoHost->getXOType(idx);
}

NVGcolor getXOBrowsedColor() override
{
	if (!xoHost)
		return ORANGE;
	int count = xoHost->getXOCount();
	if (count <= 0)
		return ORANGE;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xoHost->getXOColor(idx);
}

XAlign getXOBrowsedAlign() override
{
	if (!xoHost)
		return X_ALIGN_LEFT;
	int count = xoHost->getXOCount();
	if (count <= 0)
		return X_ALIGN_LEFT;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xoHost->getXOAlign(idx);
}

std::string formatXOBrowsedValue(float raw) override
{
	if (!xoHost)
		return "";
	int count = xoHost->getXOCount();
	if (count <= 0)
		return "";
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xoHost->formatXOValue(idx, raw);
}

int getXOBrowsedChannelCount() override
{
	if (!xoHost)
		return 0;
	int count = xoHost->getXOCount();
	if (count <= 0)
		return 0;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xoHost->getXOChannelCount(idx);
}

float getXOBrowsedChannelValue(int channel) override
{
	if (!xoHost)
		return 0.f;
	int count = xoHost->getXOCount();
	if (count <= 0)
		return 0.f;
	int idx = clamp((int) OL_state[BROWSE_INDEX_JSON], 0, count - 1);
	return xoHost->getXOChannelValue(idx, channel);
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
	setJsonLabel(BROWSE_INDEX_JSON, "browseIndex");

#pragma GCC diagnostic pop
}

inline void moduleParamConfig()
{
	configParam(LEFT_PARAM, 0.f, 1.f, 0.f, "Previous output");
	configParam(RIGHT_PARAM, 0.f, 1.f, 0.f, "Next output");
#ifdef XO_HAS_JACKS
	for (int i = 0; i < XO_CAPACITY; i++)
		configOutput(CHANNEL_OUTPUT + i, string::f("Channel %d", i + 1));
#endif
}

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

	// No restriction on which Host TYPE this resolves to, same reasoning as the X family's own
	// resolveXHost() call - any module implementing XOHostInterface works transparently.
	xoHost = resolveXOHost(leftExpander.module);
	setStateLight(CONN_LIGHT, xoHost ? 255.f : 0.f);

	// Browsing: unconditional, unfiltered stepping through every output slot the currently-
	// resolved Host reports - no engagement to gate it, watching is never exclusive. If no Host
	// is resolved (or it has zero slots), browseIndex just stays put and stepping has no visible
	// effect. Lives in OL_state[BROWSE_INDEX_JSON] so it survives a patch reload.
	int browseIndex = (int) OL_state[BROWSE_INDEX_JSON];
	int count = xoHost ? xoHost->getXOCount() : 0;
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
		// Deliberately NOT reset to 0 here - reconnecting should land back on the same slot it
		// was last browsing, not jump back to the first one. Gets clamped into range again above
		// once a Host with count > 0 resolves.
		leftTrigger.process(params[LEFT_PARAM].getValue());
		rightTrigger.process(params[RIGHT_PARAM].getValue());
	}
	OL_state[BROWSE_INDEX_JSON] = (float) browseIndex;

	// Overflow warning: the browsed output's real channel count exceeds this module's own fixed
	// capacity - some channels simply aren't exposed. Purely informational, no other effect.
	bool overflow = xoHost && count > 0 && xoHost->getXOChannelCount(browseIndex) > XO_CAPACITY;
	setStateLight(OVERFLOW_LIGHT, overflow ? 255.f : 0.f);

	// Gate/trigger display stretch (see getXOBrowsedChannelGateLit()'s own comment in
	// XOShared.hpp) - runs unconditionally (not just under XO_HAS_JACKS) since XD8/XD16 have no
	// real jacks at all but still show a gate indicator. A rising edge on the raw Host voltage
	// starts a fixed X_VALUE_CLICK_SECONDS pulse; the display stays lit for as long as either the
	// pulse hasn't finished OR the real signal is still genuinely above threshold (a sustained
	// gate longer than the flash duration must not be cut short).
	{
		int liveChannelsForFlash = (xoHost && count > 0) ? xoHost->getXOChannelCount(browseIndex) : 0;
		for (int c = 0; c < XO_CAPACITY; c++)
		{
			float raw = (c < liveChannelsForFlash) ? xoHost->getXOChannelValue(browseIndex, c) : 0.f;
			bool above = raw > XO_GATE_THRESHOLD_V;
			if (above && !xoGateFlashAbove[c])
				xoGateFlashPulse[c].trigger(X_VALUE_CLICK_SECONDS);
			xoGateFlashAbove[c] = above;
			// See CLAUDE.md's "dsp::Timer inside moduleProcess() must scale by (samplesSkipped+1)"
			// pitfall - moduleProcess() only runs on ~1 in idleSkip samples.
			bool pulseActive = xoGateFlashPulse[c].process(args.sampleTime * (samplesSkipped + 1));
			xoGateFlashLit[c] = above || pulseActive;
		}
	}

#ifdef XO_HAS_JACKS
	int liveChannels = (xoHost && count > 0) ? xoHost->getXOChannelCount(browseIndex) : 0;
	for (int c = 0; c < XO_CAPACITY; c++)
		setStateOutput(CHANNEL_OUTPUT + c, (c < liveChannels) ? xoHost->getXOChannelValue(browseIndex, c) : 0.f);
#endif
}

inline void moduleProcessState() {}
inline void moduleReflectChanges() {}

// XOExpanderInterface
XOHostInterface* getXOHost() override { return xoHost; }
float getXOStyle() override { return OL_state[STYLE_JSON]; }
int getXOCapacity() override { return XO_CAPACITY; }
int getXOBrowseIndex() override { return (int) OL_state[BROWSE_INDEX_JSON]; }
bool getXOBrowsedChannelGateLit(int channel) override { return xoGateFlashLit[channel]; }
