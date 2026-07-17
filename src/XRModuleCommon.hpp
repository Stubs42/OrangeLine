/*
	XRModuleCommon.hpp

	Shared module-struct body for the XR family (XR8/XR16) - #include'd literally inside both of
	their own `struct <Name> : Module, XOExpanderInterface { ... }`, right after
	OrangeLineCommon.hpp, same composition pattern XOModuleCommon.hpp uses for its own (larger)
	family. Kept as its own header rather than a thin wrapper around XOModuleCommon.hpp: this
	family's moduleProcess() genuinely diverges (filtered browsing, seed-driven generation instead
	of passthrough) even though several accessor methods below are identical copies of
	XOModuleCommon.hpp's own generic "look up whatever's browsed" logic.

	NOT included here: the constructor (same reasoning as XOModuleCommon.hpp - a C++ constructor's
	name must match its enclosing class; XR8/XR16 also each need their own one-time `lastSeed[]`
	initialization, see their own .cpp files).

	Requires `XR_CAPACITY` (8 or 16, from each module's own <Name>.hpp). Always jack-bearing - no
	display-only variant exists in this family, unlike XO's XD8/XD16.
*/

bool widgetReady = false;

// Resolved every moduleProcess() tick by looking only at leftExpander.module (mirrored direction
// from the X family's own xHost - this family attaches to a Host's RIGHT side, same as the XO
// family it reuses XOHostInterface/XOExpanderInterface from).
XOHostInterface *xoHost = nullptr;

dsp::SchmittTrigger leftTrigger, rightTrigger;

// Per-channel change-detection state and last-generated output - NOT persisted (see XR8/XR16's
// own constructor for the one-time NAN initialization; output is a pure function of the Host's
// current value, nothing here needs to survive a patch reload).
float lastSeed[XR_CAPACITY];
float randomValue[XR_CAPACITY];

// XRRangeOption/XR_RANGE_OPTIONS (the per-channel output-range table) live at file scope in each
// sibling's own <Name>.hpp instead of here - this header's content is #include'd literally inside
// the module struct, so a member here would be invisible to the widget/menu code in <Name>.cpp,
// which is a separate struct entirely.

// Steps `dir` (+1/-1, wrapping mod count) from `from`, up to `count` times, returning the first
// index whose type is NOT XO_TYPE_GATE - lands `from` unchanged if every candidate happens to be
// a gate (avoids an infinite loop; browsing just has no visible effect in that edge case).
int nextContinuousIndex(XOHostInterface *host, int from, int count, int dir)
{
	int idx = from;
	for (int i = 0; i < count; i++)
	{
		idx = ((idx + dir) % count + count) % count;
		if (host->getXOType(idx) != XO_TYPE_GATE)
			return idx;
	}
	return from;
}

// Same generic "look up whatever's browsed" accessors as XOModuleCommon.hpp - untouched by this
// family's own filtering/generation differences, copied verbatim.
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

// Persistent per-instance storage for the generated per-channel json labels below -
// setJsonLabel() stores the raw char* handed to it rather than copying the string (see
// OrangeLineCommon.hpp's own OL_jsonLabel[idx] = label;), so a temporary std::string's c_str()
// would dangle the instant the temporary is destroyed. This member buffer lives as long as the
// module instance does, matching moduleInitJsonConfig()'s own "called once from the constructor,
// via initializeInstance()" lifetime.
char channelRangeLabelBuf[XR_CAPACITY][24];

inline void moduleInitJsonConfig()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

	setJsonLabel(STYLE_JSON, "style");
	setJsonLabel(BROWSE_INDEX_JSON, "browseIndex");
	for (int c = 0; c < XR_CAPACITY; c++)
	{
		snprintf(channelRangeLabelBuf[c], sizeof(channelRangeLabelBuf[c]), "channelRange%d", c);
		setJsonLabel(CHANNEL_RANGE_JSON + c, channelRangeLabelBuf[c]);
	}

#pragma GCC diagnostic pop
}

inline void moduleParamConfig()
{
	configParam(LEFT_PARAM, 0.f, 1.f, 0.f, "Previous output");
	configParam(RIGHT_PARAM, 0.f, 1.f, 0.f, "Next output");
	for (int i = 0; i < XR_CAPACITY; i++)
		configOutput(CHANNEL_OUTPUT + i, string::f("Channel %d", i + 1));
}

inline void moduleCustomInitialize() {}
inline void moduleInitialize() {}

void moduleReset()
{
	styleChanged = true;
	OL_state[BROWSE_INDEX_JSON] = 0.f;
	for (int c = 0; c < XR_CAPACITY; c++)
		OL_state[CHANNEL_RANGE_JSON + c] = 0.f; // Uni 10V default
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

	xoHost = resolveXOHost(leftExpander.module);
	setStateLight(CONN_LIGHT, xoHost ? 255.f : 0.f);

	// Browsing: same unconditional/unfiltered-by-engagement stepping as the XO family, but
	// additionally skips gate-type candidates entirely - only continuous (value-carrying) outputs
	// are ever selectable here, since there's nothing meaningful to seed a random draw from a
	// gate. The current index is also snapped forward if it's ever found sitting on a gate type
	// (e.g. a freshly loaded/default index), keeping the invariant true regardless of how
	// browseIndex got set.
	int browseIndex = (int) OL_state[BROWSE_INDEX_JSON];
	int count = xoHost ? xoHost->getXOCount() : 0;
	if (count > 0)
	{
		browseIndex = clamp(browseIndex, 0, count - 1);
		if (xoHost->getXOType(browseIndex) == XO_TYPE_GATE)
			browseIndex = nextContinuousIndex(xoHost, browseIndex, count, +1);

		if (leftTrigger.process(params[LEFT_PARAM].getValue()))
			browseIndex = nextContinuousIndex(xoHost, browseIndex, count, -1);
		if (rightTrigger.process(params[RIGHT_PARAM].getValue()))
			browseIndex = nextContinuousIndex(xoHost, browseIndex, count, +1);
	}
	else
	{
		leftTrigger.process(params[LEFT_PARAM].getValue());
		rightTrigger.process(params[RIGHT_PARAM].getValue());
	}
	OL_state[BROWSE_INDEX_JSON] = (float) browseIndex;

	bool overflow = xoHost && count > 0 && xoHost->getXOChannelCount(browseIndex) > XR_CAPACITY;
	setStateLight(OVERFLOW_LIGHT, overflow ? 255.f : 0.f);

	// Per-channel independent seed -> deterministic random value. Each channel's own lastSeed[]
	// starts at NAN (see the constructor) so the first-ever comparison is always true (NAN != x
	// is always true per IEEE754) - the very first tick always counts as a change, matching
	// Dieter's "first tick calculates through immediately" requirement with no separate flag.
	int liveChannels = (xoHost && count > 0) ? xoHost->getXOChannelCount(browseIndex) : 0;
	for (int c = 0; c < XR_CAPACITY; c++)
	{
		float value = (c < liveChannels) ? xoHost->getXOChannelValue(browseIndex, c) : 0.f;
		if (value != lastSeed[c])
		{
			uint32_t bits;
			memcpy(&bits, &value, sizeof(bits));
			uint64_t raw = splitMix64((uint64_t) bits);
			float normalized = splitMix64Normalized(raw); // [0,1)
			int rangeIdx = clamp((int) OL_state[CHANNEL_RANGE_JSON + c], 0, 7);
			const XRRangeOption &opt = XR_RANGE_OPTIONS[rangeIdx];
			randomValue[c] = opt.bipolar ? (normalized * 2.f - 1.f) * opt.voltage : normalized * opt.voltage;
			lastSeed[c] = value;
		}
		setStateOutput(CHANNEL_OUTPUT + c, randomValue[c]);
	}
}

inline void moduleProcessState() {}
inline void moduleReflectChanges() {}

// XOExpanderInterface
XOHostInterface* getXOHost() override { return xoHost; }
float getXOStyle() override { return OL_state[STYLE_JSON]; }
int getXOCapacity() override { return XR_CAPACITY; }
int getXOBrowseIndex() override { return (int) OL_state[BROWSE_INDEX_JSON]; }
