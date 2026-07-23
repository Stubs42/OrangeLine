/*
	Morpheus.cpp

	Code for the OrangeLine module Morpheus

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
#include <string>
#include <stdio.h>
#include <limits.h>

#include "Morpheus.hpp"

// X-family candidate params ("potential virtual cables" - see ExpanderParamAccessSpec.md's
// "Architecture: Expander = a virtual poly cable"). Everything belonging to *one* candidate
// lives together in a single struct - its constant identity (which InputIds it is, its display
// name/type) right alongside the one piece of mutable session state (which Expander, if any,
// currently holds it) - rather than several parallel arrays indexed by the same int. Order/
// content matches ExpanderParamAccessSpec.md's "Confirmed candidate list: Morpheus" table.
// EXT_INPUT is deliberately excluded - it's a real signal path (MEM/EXT source switch), not a
// CV-modulates-a-knob candidate.
enum XCandidateIndex {
	XC_LOCK, XC_BALANCE, XC_LOOP_LEN, XC_HLD, XC_RND, XC_SHIFT_LEFT, XC_SHIFT_RIGHT,
	XC_CLR, XC_REC, XC_GTP, XC_SCL, XC_OFS,
	NUM_X_CANDIDATES
};

// XCandidate::format's own signature - takes the value already in DISPLAY units (the same value
// the Expander's own knob directly holds - see the struct's own comment on displayMin/Max/Default/
// cvScale below for why there's no separate "cable value" conversion step here anymore) and
// returns the exact text XHostInterface::formatXParamValue() should hand back. A plain function
// pointer (not std::function) so a non-capturing lambda literal (or, as used below, a plain named
// function) can sit right in the xCandidates[] table with zero overhead and no lifetime concerns -
// nullptr for every digital (Toggle/Click/Push) candidate, which has no numeric display at all.
typedef std::string (*XFormatFn)(float displayValue);

struct XCandidate
{
	// A constructor (rather than plain aggregate init) because a default member initializer
	// on boundExpanderId would otherwise disqualify this struct from C++11 aggregate init
	// (relaxed in C++14+, but this project builds with -std=c++11).
	XCandidate(int inputId, const char *name, const char *shortName, XParamType type, NVGcolor color,
	           float displayMin = 0.f, float displayMax = 1.f, float displayDefault = 0.5f, bool snap = false,
	           const char *unit = "", float cvScale = 1.f, XFormatFn format = nullptr, XAlign align = X_ALIGN_LEFT)
		: inputId(inputId), name(name), shortName(shortName), type(type), color(color),
		  displayMin(displayMin), displayMax(displayMax), displayDefault(displayDefault), snap(snap),
		  unit(unit), cvScale(cvScale), format(format), align(align) {}

	int inputId;                   // which InputIds enum value this candidate corresponds to
	const char *name;               // full descriptive name - XHostInterface::getXParamName()
	const char *shortName;          // matches the real Morpheus panel's own printed labels -
	                                 // XHostInterface::getXParamShortName()
	XParamType type;
	NVGcolor color;                 // this slot's own accent color - XHostInterface::getXParamColor(),
	                                 // shown on a bound Expander's display and as its value button's
	                                 // "on" light color
	// The Expander's own knob directly holds a value in THIS candidate's own human-readable
	// display range (e.g. 1..128 steps for LEN, -10..10 for SCL/OFS, -100..100% for LOCK/BALANCE)
	// - not a universal 0..1 raw fraction anymore (see CLAUDE.md's own pitfall entry on the
	// three-layer raw/CV/display confusion this replaces, and X8ModuleCommon.hpp's own
	// moduleProcess() for where these get pushed onto the Expander's own ParamQuantity every time
	// the browsed candidate changes). `snap` rounds to the nearest whole display unit (Rack's own
	// ParamQuantity::snapEnabled) - only meaningful once minValue/maxValue directly span the
	// display range, which is exactly what this achieves. `unit` is Rack's own separate
	// ParamQuantity::unit field (never baked into a format string - see formatXPercent/etc below),
	// so Rack's built-in right-click "Enter value" parsing (tinyexpr) never sees a stray "%" and
	// silently fail to compile, which is what happened before this redesign. Defaults (0..1,
	// default 0.5, no snap, no unit) are a no-op, correct for every digital (Toggle/Click/Push)
	// candidate, which never uses any of this beyond a plain 0/1 threshold via X8ValueButton's own
	// independent convention. Only the six continuous candidates below override these.
	float displayMin, displayMax, displayDefault;
	bool snap;
	const char *unit;
	// scaleXParamValue()'s own per-candidate factor: cv = displayValue * cvScale, where cv is
	// exactly what a real patch cable would need to deliver on this input for the same result -
	// per Dieter: "the X modules do not know anything about that scaling, those are the values a
	// connected cable has to deliver." This is now the ONLY conversion needed (display <-> cv),
	// since the Expander's own knob already holds the display value directly - no more separate
	// raw<->display hop.
	float cvScale;
	// XHostInterface::formatXParamValue() delegates straight to this - see XFormatFn's own
	// comment above. Deliberately kept right here, in the same row as color/display range/align,
	// so everything about one candidate's own display behavior lives in exactly one place instead
	// of being scattered across separate functions elsewhere in the file.
	XFormatFn format;
	// XHostInterface::getXParamAlign() - X8D's numeric display alignment. Irrelevant for digital
	// types (no numeric display at all), left at the default.
	XAlign align;
	// -1 = unbound. Persisted (see Morpheus's own moduleExtraDataToJson/FromJson) as a
	// best-effort id: if Rack happens to preserve module ids across a reload (the common case
	// for a plain save+reopen of the same patch), the binding resolves live again exactly as
	// before - read fully live regardless of the bound Expander's own physical position (no more
	// adjacency-gating - see the refresh loop's own comment); if the id doesn't resolve to
	// anything, the existing "bound module gone" handling in the refresh loop already shows this
	// as engaged (green) and frozen until an explicit Reset - so restoring a stale/foreign id is
	// always safe, never silently wrong. Chosen deliberately over dropping the binding (and its
	// green indicator) on every reload. At most one candidate, on one Host, anywhere in the rack,
	// may hold a given Expander id at any time - see xUnbindExpanderEverywhere() (XShared.hpp).
	int64_t boundExpanderId = -1;
	// Cached pointer for boundExpanderId above - PUSHED directly at the exact moment a bind is
	// granted or restored (requestXBind() already has the Expander's own pointer in hand;
	// XShared.hpp's findXBoundHostId()/tryRecoverXParamSlot() resolve it once, during their own
	// one-time reconnect/recovery scan, and push it straight through recoverXParamBinding()'s
	// `expander` parameter) - NEVER re-resolved via APP->engine->getModule() from inside
	// moduleProcess() on any other tick. That per-tick resolve is what this replaced: a
	// share-locking getModule() call from inside moduleProcess(), once per bound candidate per
	// tick, occasionally raced a queued exclusive lock request (module add/remove) and hung the
	// engine (gdb-confirmed) - same root cause as the ExpanderBridge/XOD8 deadlock found and
	// fixed earlier the same session, different call site, not covered by that fix. Safe to cache
	// permanently because every path that changes boundExpanderId also sets this in the same
	// place, and both sides' onRemove() already proactively clear boundExpanderId (and, via the
	// same clearXParamBinding()/setXBoundHostId(-1) path, this pointer too) the instant either
	// this Host or the bound Expander is deleted - a stale cached pointer can never outlive the
	// id that would have caught it anyway.
	XExpanderInterface *boundExpander = nullptr;
};

// Per-candidate format functions for the xCandidates[] table below - see XFormatFn's own comment.
// Kept immediately above the table that uses them, so "what does candidate X's display show" is
// always answerable by looking in exactly one place, not a separate switch statement elsewhere in
// the file. Each takes the value already in display units - no unit suffix baked in here anymore
// (unit is now Rack's own separate ParamQuantity::unit field, see the XCandidate struct's own
// comment on why - tinyexpr's own "%" collision).
static std::string formatXPercent(float display) { return string::f("%.0f", display); }
static std::string formatXSigned(float display)  { return string::f("%.2f", display); }
static std::string formatXLoopLen(float display) { return string::f("%.0f", display); }

// XO-family candidates - Morpheus's own real outputs, declared as selectable slots for any
// XO8/XD8/XOD8/XO16/XD16/XOD16 attached to its RIGHT side (see XOShared.hpp's own architecture
// comment). Unlike xCandidates[] above, there is no engagement/binding here at all - reading a
// Host's output is never exclusive, so this table only needs display metadata, never a
// "currently bound to" slot.
enum XOCandidateIndex { XOC_SRC, XOC_GATE, XOC_CV, NUM_XO_CANDIDATES };

// XOCandidate::format's own signature - takes the real Host voltage directly (no scaling step
// exists on this side at all, unlike XFormatFn's own "cable value" - see XOHostInterface's own
// comment in XOShared.hpp) and returns the exact text XOHostInterface::formatXOValue() should
// hand back. nullptr for XO_TYPE_GATE, which has no numeric display at all.
typedef std::string (*XOFormatFn)(float value);
static std::string formatXOSigned(float value) { return string::f("%.2f", value); }

struct XOCandidate
{
	int outputId;           // which OutputIds enum value this candidate corresponds to
	const char *name;       // full descriptive name - XOHostInterface::getXOName()
	const char *shortName;  // XOHostInterface::getXOShortName() - same 5-char hard contract
	XOType type;
	NVGcolor color;         // this slot's own accent color, same convention as XCandidate::color -
	                        // picked distinct from every xCandidates[] hue above so an input slot
	                        // and an output slot are never confused at a glance
	XAlign align;
	XOFormatFn format;      // nullptr for XO_TYPE_GATE
};

struct Morpheus : Module, XHostInterface, XOHostInterface, NeoHostInterface, ExpanderBridgeInterface
{
	float oldClkInputVoltage = 0;
    int polyChannels = 1;
	bool hadReset = true;
	bool isShiftLeft[POLY_CHANNELS];
	bool isShiftRight[POLY_CHANNELS];

	// User-editable label, set via the right-click menu's text field (see MorpheusNameField) -
	// purely a human-facing identifier, mainly useful once a patch has more than one Morpheus
	// instance and an X-family Expander's own "Engages" menu needs to say *which* one a slot
	// belongs to (rather than just "Morpheus" for all of them). Empty by default.
	std::string customName;

	// Short names match what's actually printed on the real Morpheus panel (verified directly
	// in res/MorpheusWork.svg: LOCK/LEN/HLD/RND/CLR/REC/GTP/SCL/OFS are already there; BAL and
	// <</>> for Shift Left/Right per Dieter). All are <= 5 characters - X8's display has no
	// truncation fallback, see XShared.hpp's getXParamShortName() contract.
	// Per-slot accent color (XHostInterface::getXParamColor()) - a first-pass palette, deliberately
	// just a literal per row so it's trivial to retune any single one later. CLR is red per
	// Dieter's own example; REC gets a distinct amber rather than the same red so the two don't
	// read as the same action at a glance.
	XCandidate xCandidates[NUM_X_CANDIDATES] = {
		// displayMin/Max/Default/snap/unit/cvScale: the Expander's own knob directly holds a
		// value in each candidate's own human-readable range - LOCK/BALANCE are additive/relative
		// (bipolar -100..+100%, default 0 = "no contribution", matching real-world intuition with
		// no raw-space special case needed); GTP is 0..100%; LOOP_LEN is 1..128 whole steps
		// (snap=true); SCL/OFS are their own full -10..10 range directly (cvScale=1, no
		// conversion needed - the display value already IS the cable value for these two).
		// align: X_ALIGN_LEFT (the default) is what was already sitting correctly on the panel
		// before this field existed - only retune a specific row's alignment if it actually needs
		// to be different, don't default the whole table to something else.
		{ LOCK_INPUT,        "Lock",              "LOCK", X_PARAM_CONTINUOUS, nvgRGB(0x33, 0x99, 0xff), -100.f, 100.f,   0.f, false, "%", 0.1f,  formatXPercent, X_ALIGN_RIGHT },
		{ BALANCE_INPUT,     "Balance",           "BAL",  X_PARAM_CONTINUOUS, nvgRGB(0x33, 0xcc, 0xcc), -100.f, 100.f,   0.f, false, "%", 0.1f,  formatXPercent, X_ALIGN_RIGHT },
		{ LOOP_LEN_INPUT,    "Loop Length",       "LEN",  X_PARAM_CONTINUOUS, nvgRGB(0xff, 0x99, 0x00),    1.f, 128.f,  16.f, true,  "",  0.01f, formatXLoopLen, X_ALIGN_CENTER },
		{ HLD_INPUT,         "Hold",              "HLD",  X_PARAM_TOGGLE,     nvgRGB(0x66, 0x66, 0xff) },
		{ RND_INPUT,         "Randomize",         "RND",  X_PARAM_PUSH,       nvgRGB(0xcc, 0x00, 0xcc) }, // pushbutton, effect while held
		{ SHIFT_LEFT_INPUT,  "Shift Left",        "<<",   X_PARAM_CLICK,      nvgRGB(0x00, 0xaa, 0x88) },
		{ SHIFT_RIGHT_INPUT, "Shift Right",       ">>",   X_PARAM_CLICK,      nvgRGB(0x00, 0x88, 0xaa) },
		{ CLR_INPUT,         "Clear Loop",        "CLR",  X_PARAM_PUSH,       nvgRGB(0xdd, 0x00, 0x00) }, // pushbutton, effect while held
		{ REC_INPUT,         "Record",            "REC",  X_PARAM_CLICK,      nvgRGB(0xff, 0xaa, 0x00) },
		{ GTP_INPUT,         "Gate Probability",  "GTP",  X_PARAM_CONTINUOUS, nvgRGB(0x99, 0xcc, 0x00),    0.f, 100.f,  50.f, false, "%", 0.1f,  formatXPercent, X_ALIGN_RIGHT },
		{ SCL_INPUT,         "CV Scale",          "SCL",  X_PARAM_CONTINUOUS, nvgRGB(0x00, 0xcc, 0xff),  -10.f,  10.f,  10.f, false, "",  1.f,   formatXSigned, X_ALIGN_RIGHT  },
		{ OFS_INPUT,         "CV Offset",         "OFS",  X_PARAM_CONTINUOUS, nvgRGB(0xff, 0x00, 0x66),  -10.f,  10.f,   0.f, false, "",  1.f,   formatXSigned, X_ALIGN_RIGHT  },
	};

	// XO-family candidates - Morpheus's own real outputs, declared as selectable slots for any
	// XO8/XD8/XOD8/XO16/XD16/XOD16 attached to its RIGHT side (see XOShared.hpp's own
	// architecture comment). Unlike xCandidates[] above, there is no engagement/binding here at
	// all - reading a Host's output is never exclusive, so this table only needs display
	// metadata. First-pass palette (trivially retuned later, same as xCandidates[] above): SRC
	// teal, GATE pink, CV purple, none overlapping any xCandidates[] color above.
	XOCandidate xOutputCandidates[NUM_XO_CANDIDATES] = {
		{ SRC_OUTPUT,  "Source", "SRC",  XO_TYPE_CONTINUOUS, nvgRGB(0x00, 0xff, 0xaa), X_ALIGN_RIGHT, formatXOSigned },
		{ GATE_OUTPUT, "Gate",   "GATE", XO_TYPE_GATE,        nvgRGB(0xff, 0x33, 0x99), X_ALIGN_LEFT,  nullptr },
		{ CV_OUTPUT,   "CV",     "CV",   XO_TYPE_CONTINUOUS, nvgRGB(0x99, 0x33, 0xff), X_ALIGN_RIGHT, formatXOSigned },
	};

	// Mirrors inputs[i].getChannels() for a virtually-bound candidate input, since a receiving
	// module can't override a real Port's own channel count - see getXAwareChannels() below.
	// Zeroed whenever that input has no live virtual source this tick (real cable, or nothing
	// bound/live) so existing reader code falls back to its own default behavior correctly.
	int xVirtualChannels[NUM_INPUTS] = {0};

	// Existing per-candidate reader functions (getChannelLock() etc.) call this instead of
	// inputs[i].getChannels() directly, so they transparently see a bound Expander's channel
	// count exactly as if it were a real cable's - the rest of each function (mono/poly
	// branching, scaling formulas) needs no changes at all.
	inline int getXAwareChannels(int inputId)
	{
		return inputs[inputId].isConnected() ? inputs[inputId].getChannels() : xVirtualChannels[inputId];
	}

	// Deliberately NOT reusing the framework's own OL_inputConnected[] for this - that array is
	// unconditionally overwritten every tick by processParamsAndInputs() (real cable state only,
	// runs *before* this module's own moduleProcess()), so writing "bound = true" into it here
	// would only ever be visible for the remainder of the same tick; from the very start of the
	// *next* tick it reads false again until moduleProcess() catches up. Both states exist for a
	// real slice of audio-thread time every ~1ms, and the widget's draw() runs on a separate,
	// unsynchronized UI thread that can sample mid-flip - observed as the loop-length (or any
	// other candidate) briefly showing the local knob's value instead of the bound Expander's,
	// with nothing touched. A dedicated flag, written only here, removes the flip entirely.
	bool xVirtualConnected[NUM_INPUTS] = {false};

	inline bool getXAwareConnected(int inputId)
	{
		return inputs[inputId].isConnected() || xVirtualConnected[inputId];
	}

#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	bool widgetReady = false;
	float selectedMem = 1.f;
	NumberWidget *memWidget;
	bool haveEditHld = false;
	bool scaleOnOutput = false;
	bool visualOn = true;
	/** One-shot per-channel step-exchange event (EVENT_NONE/EVENT_SOURCE/EVENT_RANDOM), set
		for a single tick at the LOCK/S<>R decision point, consumed and reset back to
		EVENT_NONE by MorpheusDisplayWidget as soon as it notices a non-NONE value. */
	int eventStatus[POLY_CHANNELS];

#include "MorpheusJsonLabels.hpp"

	// ********************************************************************************************************************************
	/*
		Initialization
	*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Morpheus()
	{
		initializeInstance();

		// boundExpanderId is int64_t, not a float OL_state slot - same reasoning/pattern as
		// CC2CV/CV2CC's own moduleExtraDataToJson/FromJson use for non-float persisted data.
		// customName (a plain string, see its own member comment) rides along in the same hook.
		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_t *boundJ = json_array();
			for (int i = 0; i < NUM_X_CANDIDATES; i++)
				json_array_append_new(boundJ, json_integer(xCandidates[i].boundExpanderId));
			json_object_set_new(rootJ, "xBoundExpanderId", boundJ);
			json_object_set_new(rootJ, "customName", json_string(customName.c_str()));
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *boundJ = json_object_get(rootJ, "xBoundExpanderId");
			if (boundJ)
			{
				for (int i = 0; i < NUM_X_CANDIDATES; i++)
				{
					json_t *idJ = json_array_get(boundJ, i);
					if (idJ && json_is_integer(idJ))
					{
						xCandidates[i].boundExpanderId = json_integer_value(idJ);
						xCandidates[i].boundExpander = nullptr; // re-resolved lazily on first use
					}
				}
			}
			json_t *nameJ = json_object_get(rootJ, "customName");
			if (nameJ && json_is_string(nameJ))
				customName = json_string_value(nameJ);
		};
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
	/*
		Method to decide whether this call of process() should be skipped
	*/
	bool moduleSkipProcess() {
		bool skip = (idleSkipCounter != 0);
		if (skip) {
			float clkInputVoltage = inputs[CLK_INPUT].getVoltage ();
			if (clkInputVoltage != oldClkInputVoltage) {
				skip = false;
#ifndef OL_TOUCH_DISABLED
				OL_touchOutRequest = true;	// relay Ready in step with this early wake - see CLAUDE.md
#endif
			}
			oldClkInputVoltage = clkInputVoltage;
		}
		return skip;
	}
	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes()
	{
   		setStateTypeInput  (RST_INPUT, STATE_TYPE_TRIGGER);
   		setStateTypeInput  (CLK_INPUT, STATE_TYPE_TRIGGER);
   		setStateTypeInput  (RCL_INPUT, STATE_TYPE_TRIGGER);
   		setStateTypeInput  (STO_INPUT, STATE_TYPE_TRIGGER);

   		setInPoly (LOCK_INPUT, true);
   		setInPoly (BALANCE_INPUT, true);
   		setInPoly (LOOP_LEN_INPUT, true);   
    	setInPoly (HLD_INPUT, true);
    	setInPoly (RND_INPUT, true);
    	setInPoly (SHIFT_LEFT_INPUT, true);
    	setInPoly (SHIFT_RIGHT_INPUT, true);
    	setInPoly (CLR_INPUT, true);
    	setInPoly (EXT_INPUT, true);
    	setInPoly (REC_INPUT, true);
    	setInPoly (GTP_INPUT, true);
    	setInPoly (SCL_INPUT, true);
    	setInPoly (OFS_INPUT, true);

   		setOutPoly (SRC_OUTPUT, true);
   		setOutPoly (GATE_OUTPUT, true);
		OL_isSteadyGate[GATE_OUTPUT] = true;
   		setStateTypeOutput (GATE_OUTPUT, STATE_TYPE_TRIGGER);
		// OL_isSteadyGate[GATE_OUTPUT] = true;
   		setOutPoly (CV_OUTPUT, true);
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
		//
		// Config internal Parameters not bound to a user interface object
		//
		for (int i = 0; i < NUM_JSONS; i++)
		{
			setJsonLabel(i, jsonLabel[i]);
		}
#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig()
	{
//	void configParam(int paramId, float minValue, float maxValue, float defaultValue, std::string label = "", std::string unit = "", 
//                   float displayBase = 0.f, float displayMultiplier = 1.f, float displayOffset = 0.f) {

   		configParam (LOCK_PARAM,        0.f, 100.f,  0.f, "Lock",      "%", 0.f, 1.f, 0.f);
   		configParam (BALANCE_PARAM,     0.f, 100.f,100.f, "Source(0%) to Random(100%) Balance", "%", 0.f, 1.f, 0.f);

   		configParam (LOOP_LEN_PARAM,    1.f,  MAX_LOOP_LEN, 16.f, "Loop Length", "", 0.f, 1.f, 0.f);
        paramQuantities[LOOP_LEN_PARAM]->snapEnabled = true;
        configParam (MEM_UP_PARAM,      0.f,   1.f,  0.f, "Memory Up",     "", 0.f, 1.f, 0.f);
        configParam (MEM_DOWN_PARAM,    0.f,   1.f,  0.f, "Memory Down",     "", 0.f, 1.f, 0.f);
        configParam (STO_PARAM,         0.f,   1.f,  0.f, "Store Selected Memory Slot",     "", 0.f, 1.f, 0.f);
        configParam (RCL_PARAM,         0.f,   1.f,  0.f, "Recall Selected Memory Slot",    "", 0.f, 1.f, 0.f);
		configSwitch(HLD_ON_PARAM, 0.0f, 1.0f, 0.0f, "Hold", {"Off", "On"});

        configParam (RND_PARAM,         0.f,  10.f,  0.f, "Randomize (Lock 0%, Balance 100% Shortcut)",    "", 0.f, 1.f, 0.f);
        configParam (SHIFT_LEFT_PARAM,  0.f,  10.f,  0.f, "Shift Left One Step",     "", 0.f, 1.f, 0.f);
        configParam (SHIFT_RIGHT_PARAM, 0.f,  10.f,  0.f, "Shift Right One Step",    "", 0.f, 1.f, 0.f);
        configParam (CLR_PARAM,         0.f,  10.f,  0.f, "Clear Loop (CV -> 0V)",        "", 0.f, 1.f, 0.f);

		configSwitch(EXT_ON_PARAM, 0.0f, 1.0f, 0.0f, "Source", {"MEM", "EXT"});
        configParam (REC_PARAM,         0.f,  10.f,  0.f, "Record from External Source (Lock 0%, Balance 0% Shortcut)",    "", 0.f, 1.f, 0.f);
   		configParam (GTP_PARAM,         0.f, 100.f, 50.f, "Random Gate Probability", "%", 0.f, 1.f, 0.f);
   		configParam (SCL_PARAM,       -10.f,  10.f, 10.f, "Random CV Scale",          "", 0.f, 1.f, 0.f);
   		configParam (OFS_PARAM,       -10.f,  10.f,  0.f, "Random CV Offset",         "", 0.f, 1.f, 0.f);

   		configOutput (SRC_OUTPUT, "Source (Ext/Active Memory Slot)");
   		configOutput (GATE_OUTPUT, "Gate");
		configOutput (CV_OUTPUT, "CV");
	}

	inline void moduleCustomInitialize()
	{
	}

	/**
		Method to initialize the module after loading a patch or a preset
		Called from initialize () included from from OrangeLineCommon.hpp
		to initialize module state from a valid
		json state after module was added to the patch,
		a call to dataFromJson due to patch or preset load
		or a right click initialize (reset).
	*/
	inline void moduleInitialize()
	{
		this->scaleOnOutput = getStateJson(SCALE_MODE_JSON) == ON_OUTPUT;
		this->visualOn = getStateJson(VISUAL_ON_JSON) != 0.f;
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
		Currently called twice when add a module to patch ...
	*/
	void moduleReset()
	{
        setStateJson(POLY_CHANNELS_JSON, 0.f);

        for (int i = STEPS_JSON; i < STEPS_JSON + POLY_CHANNELS * MAX_LOOP_LEN; i++) {
            setStateJson(i, 0.f);
        }
        for (int i = MEM_JSON; i < MEM_JSON + MEM_SLOTS * POLY_CHANNELS * MAX_LOOP_LEN; i++) {
            setStateJson(i, 0.f);
        }
        for (int i = HEAD_JSON; i < HEAD_JSON + POLY_CHANNELS; i++) {
            setStateJson(i, 0.f);
        }
		setStateJson(ACTIVE_MEM_JSON, 0.f);
		setStateJson(SELECTED_MEM_JSON, 0.f);
		setStateJson(EXT_ON_JSON, 0.f);
		setStateJson(HLD_ON_JSON, 0.f);
		setStateJson(GATE_IS_TRG_JSON, 0.f);
		setStateJson(LOAD_ON_MEM_CV_CHANGE_JSON, 0.f);
		setStateJson(RECALL_ON_MEM_CV_CHANGE_JSON, 1.f);
		setStateJson(SMART_HOLD_JSON, 0.f);
		setStateJson(MEM_IS_HALFTONES_JSON, 0.f);
		setStateJson(LOAD_HLD_CHANNELS_JSON, 0.f);
		setStateJson(SCALE_MODE_JSON, 0.f);
		this->scaleOnOutput = 0.f;
		setStateJson(VISUAL_ON_JSON, 1.f);
		this->visualOn = true;

		for (int i = 0; i < POLY_CHANNELS; i++) {
			isShiftLeft[i] = false;
			isShiftRight[i] = false;
			eventStatus[i] = EVENT_NONE;
		}
		// Right-click Initialize also releases every X-family candidate binding - a fresh/reset
		// Morpheus should not silently keep controlling some Expander's knobs (Dieter's own call).
		// Routed through resetXParamNoLock() - NEVER the ordinary resetXParam() - so the bound
		// Expander still gets pushed a "you're not bound anymore" notification without ever
		// leaving it holding a stale id, WITHOUT deadlocking: moduleReset() runs from onReset(),
		// which Rack calls from Engine::resetModule() (Right-click "Initialize") while THAT
		// method still holds the engine's own EXCLUSIVE lock for the entire callback - exactly
		// the same lock-holding shape onRemove()/removeModule() has below, just discovered later.
		// Real bug, gdb-confirmed live (2026-07-23): the ordinary resetXParam() used to be called
		// here, and its own share-locking getModule() call self-deadlocked the UI thread the
		// instant any X-family candidate was actually bound - reproducible via "connect an X8 to
		// Morpheus, then right-click Morpheus -> Initialize." See resetXParamNoLock()'s own
		// interface comment (XShared.hpp) and CLAUDE.md's Pitfalls section for the general lesson.
		for (int i = 0; i < NUM_X_CANDIDATES; i++)
			resetXParamNoLock(i);
		styleChanged = true;
	}

	// Rack's own module-lifecycle event - fires right before this Morpheus is actually destroyed,
	// WHILE Rack's own removeModule() still holds an exclusive lock for the entire callback (see
	// resetXParamNoLock()'s own interface comment, XShared.hpp) - must use that method here,
	// NEVER the ordinary resetXParam() (its own getModule() call would deadlock). Proactively
	// clears every candidate binding this instance still holds, using ONLY the stable ids already
	// known (xCandidates[]'s own boundExpanderId) - a handful of single targeted lookups, never a
	// rack-wide scan. Symmetric with each Expander's own onRemove() (X8ModuleCommon.hpp), which
	// does the same in the other direction when an Expander is deleted - between the two, neither
	// side can ever be left holding a dangling/stale reference to the other, regardless of which
	// one gets deleted first or in what order.
	void onRemove(const RemoveEvent &e) override
	{
		for (int i = 0; i < NUM_X_CANDIDATES; i++)
			if (xCandidates[i].boundExpanderId != -1)
				resetXParamNoLock(i);
		// Separate, newer mechanism (ExpanderBridge.hpp) covering XO-family Expanders and any
		// NEO instance that cached a raw pointer to this Morpheus as their Host - unlike the
		// X-family candidates above, these were never tracked via a fixed-size array with its own
		// exclusivity, just a plain listener list. No engine lookups involved (every listener
		// pointer is already held directly), so this is unaffected by the exclusive lock this
		// callback runs under either way.
		bridgeListeners.notifyAndClear();
		Module::onRemove(e);
	}

	// ********************************************************************************************************************************
	/*
		Module specific utility methods
	*/

	// ********************************************************************************************************************************
	/*
		Methods called directly or indirectly called from process () in OrangeLineCommon.hpp
	*/

	inline float getChannelLock(int channel)
	{
		float lock = getStateParam(LOCK_PARAM);
		if (getXAwareConnected(LOCK_INPUT)) {
			int channels = getXAwareChannels(LOCK_INPUT);
            if (channels == 1) {
				return lock + OL_statePoly[LOCK_INPUT * POLY_CHANNELS] * 10; // input is scaled so lock 10V is lock 100 %
            }
			else if (channels > channel) {
				return lock + OL_statePoly[LOCK_INPUT * POLY_CHANNELS + channel] * 10; // input is scaled so lock 10V is lock 100 %
            }
		}
		return lock;
	}

	inline float getChannelBalance(int channel)
	{
		float balance = getStateParam(BALANCE_PARAM);
		if (getXAwareConnected(BALANCE_INPUT)) {
			int channels = getXAwareChannels(BALANCE_INPUT);
            if (channels == 1) {
				return balance + OL_statePoly[BALANCE_INPUT * POLY_CHANNELS] * 10; // input is scaled so balance 10V is lock 100 %
            }
			else if (channels > channel) {
				return balance + OL_statePoly[BALANCE_INPUT * POLY_CHANNELS + channel] *10;  // input is scaled so balance 10V is lock 100 %
            }
		}
		return balance;
	}

	inline float getChannelLoopLength(int channel)
	{
		float loopLen = 0.f;
		if (getXAwareConnected(LOOP_LEN_INPUT)) {
			int channels = getXAwareChannels(LOOP_LEN_INPUT);
            if (channels == 1) {
				loopLen = floor(OL_statePoly[LOOP_LEN_INPUT * POLY_CHANNELS] * 100.f + 0.001); // input is scaled so 0.16 is length 16
            }
			else if (channels > channel) {
				loopLen = floor(OL_statePoly[LOOP_LEN_INPUT * POLY_CHANNELS + channel] * 100.f + 0.001); // input is scaled so 0.16 is length 16
            }
		}
		if (loopLen < 1.f) {
			loopLen = getStateParam(LOOP_LEN_PARAM);
		}
		return loopLen;
	}

	inline bool checkForEditHld() {
		int channels = 0;
		if (getXAwareConnected(HLD_INPUT) && getStateJson(SMART_HOLD_JSON) == 1.0f) {
			channels = getXAwareChannels(HLD_INPUT);
			if (channels > 1) {
				for (int channel = 0; channel < channels; channel ++) {
					if (OL_statePoly[HLD_INPUT * POLY_CHANNELS + channel] > 7.5) {
						return true;
					}
				}
			}
		}
		return false;
	}

	inline bool getChannelHld(int channel)
	{
		if (getStateJson(HLD_ON_JSON) > 0.f) {
			return true;
		}
		float hld = 0.f;
		if (getXAwareConnected(HLD_INPUT)) {
			int channels = getXAwareChannels(HLD_INPUT);
            if (channels == 1) {
				hld = OL_statePoly[HLD_INPUT * POLY_CHANNELS];
            }
			else if (channels > channel) {
				hld = OL_statePoly[HLD_INPUT * POLY_CHANNELS + channel];
				if (getStateJson(SMART_HOLD_JSON) == 1.0f) {
					if (haveEditHld) {
						// 0.75, not 7.5 - X8's own knob/button raw range is 0..1 (see
						// getChannelRnd()'s own comment on this same X8-vs-real-cable mismatch).
						if (hld <= 0.75f) {
							return true;
						}
						else {
							return false;
						}
					}
				}
            }
		}
		// 0.5, not 5.f - see getChannelRnd()'s own comment.
		return (hld > 0.5f);
	}

	inline float getChannelRec(int channel)
	{
		if (getStateParam(REC_PARAM) > 5.f) {
			return getStateParam(REC_PARAM);
		}
		if (getXAwareConnected(REC_INPUT)) {
			int channels = getXAwareChannels(REC_INPUT);
            // 0.5, not 5.f - see getChannelRnd()'s own comment on this same threshold.
            if (channels == 1) {
				return OL_statePoly[REC_INPUT * POLY_CHANNELS] > 0.5f ? 10.f : 0.f;
            }
			else if (channels > channel) {
				return OL_statePoly[REC_INPUT * POLY_CHANNELS + channel] > 0.5f ? 10.f : 0.f;;
            }
		}
		return 0.f;
	}

	inline float getChannelRnd(int channel)
	{
		if (getStateParam(RND_PARAM) > 5.f) {
			return getStateParam(RND_PARAM);
		}
		if (getXAwareConnected(RND_INPUT)) {
			int channels = getXAwareChannels(RND_INPUT);
            // Threshold is 0.5, not 5.f, here (and in the analogous REC/CLR/HLD reader
            // functions) - unlike REC_PARAM/RND_PARAM/CLR_PARAM above (Morpheus's own physical
            // buttons, correctly calibrated 0..10 via their own configParam), a bound candidate
            // input can be a real cable (0..10V) OR a virtual X8 Expander, whose own knob/button
            // raw range is 0..1 (X8.cpp's configParam(KNOB_PARAM+i, 0.f, 1.f, ...)) - its max
            // reachable value (1.0) never crosses a 5.f threshold, so RND/REC/CLR/HLD could never
            // register as engaged through X8 at all. 0.5 correctly distinguishes on/off for both
            // conventions (a real gate's usual 0V/10V swing clears 0.5 by just as wide a margin
            // as it clears 5.f) - LOOP_LEN never had this problem since its own scale (raw*100)
            // already assumes a 0..1-ish raw range, not a full 0..10V swing.
            if (channels == 1) {
				return OL_statePoly[RND_INPUT * POLY_CHANNELS] > 0.5f ? 10.f : 0.f;
            }
			else if (channels > channel) {
				return OL_statePoly[RND_INPUT * POLY_CHANNELS + channel] > 0.5f ? 10.f : 0.f;;
            }
		}
		return 0.f;
	}

	inline float getChannelClr(int channel)
	{
		if (getStateParam(CLR_PARAM) > 5.f) {
			return getStateParam(CLR_PARAM);
		}
		if (getXAwareConnected(CLR_INPUT)) {
			int channels = getXAwareChannels(CLR_INPUT);
            // 0.5, not 5.f - see getChannelRnd()'s own comment on this same threshold.
            if (channels == 1) {
				return OL_statePoly[CLR_INPUT * POLY_CHANNELS] > 0.5f ? 10.f : 0.f;
            }
			else if (channels > channel) {
				return OL_statePoly[CLR_INPUT * POLY_CHANNELS + channel] > 0.5f ? 10.f : 0.f;;
            }
		}
		return 0.f;
	}

	// realCableOnly=true is for computeTakeoverDisplay() specifically (see its own comment on
	// XC_GTP/XC_SCL/XC_OFS): getXAwareConnected() is true the instant a candidate is BOUND, even
	// before OL_statePoly has ever actually been populated from a genuinely live Expander read -
	// on a fresh patch load a candidate can already be restored as bound from the very first tick,
	// so that population (which itself only happens via a live Expander read) never got a chance
	// to run, leaving OL_statePoly at its zeroed construction-time default. Reading it back for
	// the TAKEOVER value in that state creates a circular "takeover value comes from a buffer only
	// the takeover itself would populate" dependency. realCableOnly=true sidesteps this by only
	// trusting OL_statePoly when a REAL cable is connected (never subject to this timing issue -
	// a real cable's own state is written directly from the jack, not from a bound Expander's
	// knob), falling straight to the panel param otherwise - exactly what "nothing bound at all"
	// would already compute, which is the only sane answer when nothing live has flowed yet.
	// Every other caller (real-time audio processing) keeps the original getXAwareConnected()
	// behavior unchanged (realCableOnly defaults to false).
	inline float getChannelGtp(int channel, bool realCableOnly = false)
	{
		bool connected = realCableOnly ? inputs[GTP_INPUT].isConnected() : getXAwareConnected(GTP_INPUT);
		if (connected) {
			int channels = realCableOnly ? inputs[GTP_INPUT].getChannels() : getXAwareChannels(GTP_INPUT);
            if (channels == 1) {
				return OL_statePoly[GTP_INPUT * POLY_CHANNELS] * 10.f; // input is scaled so 5V is Probability 50%
            }
			else if (channels > channel) {
				return OL_statePoly[GTP_INPUT * POLY_CHANNELS + channel] * 10.f; // input is scaled so 5V is Probability 50%
            }
		}
		return getStateParam(GTP_PARAM);
	}

	inline float getChannelScl(int channel, bool realCableOnly = false)
	{
		bool connected = realCableOnly ? inputs[SCL_INPUT].isConnected() : getXAwareConnected(SCL_INPUT);
		if (connected) {
			int channels = realCableOnly ? inputs[SCL_INPUT].getChannels() : getXAwareChannels(SCL_INPUT);
            if (channels == 1) {
				return OL_statePoly[SCL_INPUT * POLY_CHANNELS];
            }
			else if (channels > channel) {
				return OL_statePoly[SCL_INPUT * POLY_CHANNELS + channel];
            }
		}
		return getStateParam(SCL_PARAM);
	}

	inline float getChannelOfs(int channel, bool realCableOnly = false)
	{
		bool connected = realCableOnly ? inputs[OFS_INPUT].isConnected() : getXAwareConnected(OFS_INPUT);
		if (connected) {
			int channels = realCableOnly ? inputs[OFS_INPUT].getChannels() : getXAwareChannels(OFS_INPUT);
            if (channels == 1) {
				return OL_statePoly[OFS_INPUT * POLY_CHANNELS];
            }
			else if (channels > channel) {
				return OL_statePoly[OFS_INPUT * POLY_CHANNELS + channel];
            }
		}
		return getStateParam(OFS_PARAM);
	}

	// Returns the DISPLAY value ("what Morpheus is actually using for this channel right now",
	// in the browsed candidate's own human-readable units - e.g. steps for LOOP_LEN, percent for
	// GTP) - called continuously, every tick, for every still-unbound candidate (see the refresh
	// loop below) to keep OL_statePoly's own CV/cable-unit convention correct (via
	// scaleXParamValue(), applied by the caller - this function itself never touches CV units).
	// A fresh bind then finds the right value ready to read via getXParamTakeoverValue() (which
	// calls this directly) - no separate snapshot storage needed. Clamped into the candidate's
	// own display range - Morpheus's internal state can in principle drift outside a knob's
	// configured display bounds (e.g. via direct JSON editing), same defensive reasoning as
	// before, not new.
	inline float computeTakeoverDisplay(int index, int channel)
	{
		switch (index)
		{
			case XC_LOOP_LEN: return clamp((float) getChannelLoopLength(channel), xCandidates[XC_LOOP_LEN].displayMin, xCandidates[XC_LOOP_LEN].displayMax);
			// realCableOnly=true - see getChannelGtp()/Scl()/Ofs()'s own comment: avoids reading
			// OL_statePoly back for a channel that's bound but hasn't had a live Expander value
			// populated into it yet (a fresh patch load restoring an already-bound candidate from
			// the very first tick, before that population could ever run).
			case XC_GTP:      return clamp(getChannelGtp(channel, true), xCandidates[XC_GTP].displayMin, xCandidates[XC_GTP].displayMax);
			case XC_SCL:      return clamp(getChannelScl(channel, true), xCandidates[XC_SCL].displayMin, xCandidates[XC_SCL].displayMax);
			case XC_OFS:      return clamp(getChannelOfs(channel, true), xCandidates[XC_OFS].displayMin, xCandidates[XC_OFS].displayMax);
			// LOCK/BALANCE are additive (panel knob + a bipolar -100..+100% contribution, see the
			// xCandidates table's own comment) - the neutral display value is simply 0 ("no
			// contribution"), matching displayDefault exactly, no raw-space special case needed
			// anymore.
			case XC_LOCK:     return 0.f;
			case XC_BALANCE:  return 0.f;
			// HLD/RND/SHIFT_LEFT/SHIFT_RIGHT/CLR/REC are digital push/click/toggle types -
			// "not currently triggered" is 0.
			default: return 0.f;
		}
	}

	inline void shiftChannel(int channel, int direction)
	{
		// DEBUG("shiftChannel(%d,%d)", channel, direction);
		int loopLen = getChannelLoopLength(channel);
		int start = STEPS_JSON + channel * MAX_LOOP_LEN;
		
		for (int i = 0; i < loopLen - 1; i++) {
			int idx1 = (loopLen + ( i      * direction)) % loopLen;
			int idx2 = (loopLen + ((i + 1) * direction)) % loopLen;
			float tmp = getStateJson(start + idx1);
            setStateJson(start + idx1,   getStateJson(start + idx2));
            setStateJson(start + idx2, tmp);
        }
	}

	inline void loadChannel(int channel)
	{
		int loopLen = floor (getChannelLoopLength(channel));
		int startStep = STEPS_JSON + channel * MAX_LOOP_LEN;
		int startMem = MEM_JSON + getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + channel * MAX_LOOP_LEN;
		for (int i = 0; i < loopLen; i++) {
            setStateJson(startStep + i, getStateJson(startMem + i));
        }
	}

	inline void storeChannel(int channel)
	{
		int loopLen = floor (getChannelLoopLength(channel));
		int startStep = STEPS_JSON + channel * MAX_LOOP_LEN;
		int startMem = MEM_JSON + getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + channel * MAX_LOOP_LEN;
		for (int i = 0; i < loopLen; i++) {
            setStateJson(startMem + i, getStateJson(startStep + i));
        }
	}

	inline void advanceHeads()
	{
		// DEBUG("advanceHeads()");
		for (int channel = 0; channel < polyChannels; channel++) {
			int loopLen = floor (getChannelLoopLength(channel));
			setStateJson(HEAD_JSON + channel, float((int(getStateJson(HEAD_JSON + channel)) + 1) % loopLen));
		    // DEBUG(" head[%d] = %lf", channel, getStateJson(HEAD_JSON + channel));
		}
	}

	/**
		Module specific process method called from process () in OrangeLineCommon.hpp
	*/
	inline void moduleProcess(const ProcessArgs &args)
	{
		// Handle Style Change
		if (styleChanged && widgetReady)
		{
			switch (int(getStateJson(STYLE_JSON)))
			{
			case STYLE_ORANGE:
				brightPanel->visible = false;
				darkPanel->visible = false;
				break;
			case STYLE_BRIGHT:
				brightPanel->visible = true;
				darkPanel->visible = false;
				break;
			case STYLE_DARK:
				brightPanel->visible = false;
				darkPanel->visible = true;
				break;
			}
			styleChanged = false;
		}

		// Clone/selection-load recovery: at most once, right after a fresh load, and only while
		// this Morpheus is itself a fresh clone (OL_selfId, OrangeLineCommon.hpp, mismatches its
		// own real id) - see XShared.hpp's own architecture comment on tryRecoverXParamSlot() for
		// the full reasoning, including why an unaffected, long-standing Morpheus must never run
		// this at all (it could otherwise reassign a slot away from an Expander that's still
		// perfectly legitimately bound to it). Resolves OL_selfId back to this module's own real
		// id right after, regardless of whether any slot actually found a match - this Morpheus's
		// own clone-recovery relevance is settled either way once this one-time pass has run.
		if (dataFromJsonCalled && xIsFreshClone(OL_selfId, (int64_t) this->id))
		{
			for (int i = 0; i < NUM_X_CANDIDATES; i++)
				tryRecoverXParamSlot(this, i, (int64_t) this->id);
			OL_selfId = (int64_t) this->id;
		}

		// Binding itself is no longer decided here at all - see requestXBind() below. Every
		// Expander already resolves its own Host (getXHost(), crossing both physical adjacency
		// and a persistently-connected-but-detached neighbor) and calls straight into it the
		// instant its own Engage button is pressed, so this Host stays fully passive: it never
		// walks anything, it only ever responds to whoever calls requestXBind(). See
		// XHostImplementationGuide.md for why the earlier physical-only chain-walk was retired.

		// Once per tick, per candidate param: let a real cable win outright (and actively
		// clear any stale binding rather than leaving it dormant), or pull live values from
		// whichever bound Expander is currently browsing that exact param (Track & Hold - if
		// it's bound but looking elsewhere right now, leave last tick's values/channel count
		// exactly as they were). See "No separate per-channel value buffer" in the spec.
		for (int i = 0; i < NUM_X_CANDIDATES; i++)
		{
			int inputId = xCandidates[i].inputId;

			if (inputs[inputId].isConnected())
			{
				if (xCandidates[i].boundExpanderId != -1)
					resetXParam(i); // also pushes setXBoundHostId(-1) to the displaced Expander
				xVirtualChannels[inputId] = 0;
				xVirtualConnected[inputId] = false;
				continue;
			}

			if (xCandidates[i].boundExpanderId < 0)
			{
				// Idle: keep OL_statePoly continuously consistent with "the value Morpheus is
				// actually using for this channel right now", in CV/cable units (same convention
				// scaleXParamValue() produces in the active branch below) - computeTakeoverDisplay()
				// itself returns DISPLAY units (what the Expander's own knob would show), so it
				// still needs the same display->cv conversion scaleXParamValue() applies to a live
				// knob read. No separate snapshot storage needed - a fresh bind always finds the
				// right value already sitting here for getXParamTakeoverValue() (which computes the
				// display value directly via computeTakeoverDisplay(), not from this array).
				for (int c = 0; c < POLY_CHANNELS; c++)
					OL_statePoly[inputId * POLY_CHANNELS + c] = scaleXParamValue(i, computeTakeoverDisplay(i, c));
				xVirtualChannels[inputId] = 0;
				xVirtualConnected[inputId] = false;
				continue;
			}

			// Pure cache read - no APP->engine->getModule() call here at all, on any tick. Every
			// path that can set boundExpanderId also pushes the matching pointer in the same
			// place (requestXBind() has the Expander's own pointer directly; the clone-recovery
			// and post-load-reconnect scans in XShared.hpp resolve it once, during that exact
			// one-time event, and push it through recoverXParamBinding()'s own `expander`
			// parameter) - so there is never a tick where boundExpanderId is set but boundExpander
			// isn't, except transiently on the very first tick after a fresh patch load, before
			// the bound Expander's own moduleProcess() has had a chance to run its one-time
			// findXBoundHostId() push (order between an Expander's and a Host's own process()
			// calls is never guaranteed - see XExpanderInterface::isXKnobReady()'s own comment).
			// That transient gap self-resolves within a tick or two and is handled the same way
			// as a genuinely gone module: hold/skip until the pointer shows up.
			XExpanderInterface *exp = xCandidates[i].boundExpander;
			if (!exp)
				continue; // not pushed yet (transient post-load gap) or bound module genuinely
				          // gone - stays stale (per spec) until an explicit Reset

			xVirtualConnected[inputId] = true; // bound (even if not the live one right now) = Green
			if (exp->getXBrowseIndex() != i)
				continue; // bound, but looking elsewhere right now - hold last tick's values

			if (!exp->isXKnobReady(i))
				continue; // Expander hasn't finished resyncing its own knob for this candidate
				          // yet (fresh engage, or just navigated back onto an already-bound
				          // slot) - hold last tick's values until it says it's ready. A plain
				          // level check, not a fixed tick count - see isXKnobReady()'s own
				          // comment (XShared.hpp) for why that matters.

			int channels = exp->getXKnobCount(); // sender (the Expander) decides, not us
			xVirtualChannels[inputId] = channels;
			for (int c = 0; c < channels; c++)
				// scaleXParamValue() converts the Expander's own knob value (already in this
				// candidate's own display units) into whatever THIS input actually expects in
				// CV/cable units - the Expander has no idea about that scaling itself, see the
				// interface method's own comment.
				OL_statePoly[inputId * POLY_CHANNELS + c] = scaleXParamValue(i, exp->getXKnobValue(c));
			// Channels beyond what THIS Expander actually supplies still need a correct
			// fallback value - Morpheus's own per-channel reader functions fall back to the
			// knob for any channel index >= channels, so keep those slots consistent too, same
			// reasoning as the idle branch above (same display->cv conversion, for the same
			// reason - computeTakeoverDisplay() returns display units, not cv). Otherwise a wider
			// Expander binding later (or per-channel monitoring while not engaged) would find
			// stale leftovers instead.
			for (int c = channels; c < POLY_CHANNELS; c++)
				OL_statePoly[inputId * POLY_CHANNELS + c] = scaleXParamValue(i, computeTakeoverDisplay(i, c));
		}

		// Handle Reset
		if (changeInput (RST_INPUT) || OL_initialized == false) {
			for (int i = HEAD_JSON; i < HEAD_JSON + POLY_CHANNELS; i++) {
				setStateJson(i, 0.f);
			}
			for (int channel = 0; channel < getOutPolyChannels(GATE_OUTPUT); channel ++) {
				outputs[GATE_OUTPUT].setVoltage (0.f, channel);
			}
			hadReset = true;
		}

		// get Number of channels to process
		polyChannels = getStateJson(POLY_CHANNELS_JSON); 
		if (polyChannels == 0) {
			polyChannels = 1;
			// channels is auto, derive maximum channels of all polyphonic inputs - EXT_INPUT
			// (not a candidate) falls out of this range harmlessly, both helpers below reduce
			// to plain real-cable behavior for any input the X-family refresh loop never touches.
			for (int i = LOCK_INPUT; i <= OFS_INPUT; i ++) {
				if (getXAwareConnected(i)) {
					int channels = getXAwareChannels(i);
					if (channels > polyChannels) {
						polyChannels = channels;
					}
				}
			}
		}

		// Handle HLD_ON Button presses
		if (inChangeParam (HLD_ON_PARAM)) {	//	User clicked on HLD button
			setStateJson (HLD_ON_JSON, getStateParam(HLD_ON_PARAM));
		}
		else {
			haveEditHld = checkForEditHld();
		}

		// Handle Shift
        for (int channel = 0; channel < polyChannels; channel ++) {

			// Do not Shift channels on hold
			if (getChannelHld(channel)) {
				continue;
			}

			// Shift all channels left if button is pressed
			if(changeParam(SHIFT_LEFT_PARAM) && getStateParam(SHIFT_LEFT_PARAM) > 0.f) {
				shiftChannel(channel, 1);
			}
			// Shift channel on polyphonic shift left input
            if (getXAwareConnected(SHIFT_LEFT_INPUT)) {
				int channels = getXAwareChannels(SHIFT_LEFT_INPUT);
				float state;
				if (channels == 1) {
					state = OL_statePoly[SHIFT_LEFT_INPUT * POLY_CHANNELS];
				}
				else {
					state = OL_statePoly[SHIFT_LEFT_INPUT * POLY_CHANNELS + channel];
				}
				if (state > 0.f) {
					if (!isShiftLeft[channel]) {
						shiftChannel(channel, 1);
						isShiftLeft[channel] = true;
					}
				}
				else {
						isShiftLeft[channel] = false;
				}
            }

			// shift all channels right if button is pressed
			if(changeParam(SHIFT_RIGHT_PARAM) && getStateParam(SHIFT_RIGHT_PARAM) > 0.f) {
				shiftChannel(channel, -1);
			}
			// shift channel on polyphonic shift right input
            if (getXAwareConnected(SHIFT_RIGHT_INPUT)) {
				int channels = getXAwareChannels(SHIFT_RIGHT_INPUT);
				float state;
				if (channels == 1) {
					state = OL_statePoly[SHIFT_RIGHT_INPUT * POLY_CHANNELS];
				}
				else {
					state = OL_statePoly[SHIFT_RIGHT_INPUT * POLY_CHANNELS + channel];
				}
				if (state > 0.f) {
					if (!isShiftRight[channel]) {
						shiftChannel(channel, -1);
						isShiftRight[channel] = true;
					}
				}
				else{
					isShiftRight[channel] = false;
				}
            }
        }

		if (getInputConnected(MEM_INPUT)) {
			int mem;
			if (getStateJson(MEM_IS_HALFTONES_JSON) == 1.0f) {
				mem = note(getStateInput(MEM_INPUT)) + 1;
			}
			else {
				mem = floor(getStateInput(MEM_INPUT) * 10.f); // input is scaled so 1.6 is mem slot 16
			}
			if (mem < 1) mem = 1;
			if (mem > MEM_SLOTS) mem = MEM_SLOTS;
			if (int(round(getStateJson(SELECTED_MEM_JSON))) != mem - 1) {
				setStateJson(SELECTED_MEM_JSON, mem - 1.f);
				if (getStateJson(RECALL_ON_MEM_CV_CHANGE_JSON) == 1.0f) {
					setStateJson(ACTIVE_MEM_JSON, mem - 1.f);
				}
				if (getStateJson(LOAD_ON_MEM_CV_CHANGE_JSON) == 1.0f) {
					setStateJson(ACTIVE_MEM_JSON, mem - 1.f);
					for (int channel = 0; channel < polyChannels; channel++) {
						loadChannel(channel);
					}
				}
			}
		}
		else {
			// Handle MEM up/down
			int upDown = 0;
			if(changeParam(MEM_UP_PARAM) && getStateParam(MEM_UP_PARAM) > 0.f) {
				upDown = 1;
			}	
			if(changeParam(MEM_DOWN_PARAM) && getStateParam(MEM_DOWN_PARAM) > 0.f) {
				upDown = -1;
			}
			if (upDown != 0) {
				setStateJson(SELECTED_MEM_JSON, fmod(getStateJson(SELECTED_MEM_JSON) + MEM_SLOTS + upDown, MEM_SLOTS));
			}
		}
		selectedMem = getStateJson(SELECTED_MEM_JSON) + 1.f;	// Used in MEM Widget Display

		// Handle MEM RCL
		if ((changeParam(RCL_PARAM) && (getStateParam(RCL_PARAM) > 0.f)) || 
		    (getInputConnected(RCL_INPUT) && changeInput (RCL_INPUT) && (getStateInput(RCL_INPUT) > 0.f))
		   ) {
			if (getStateJson(SELECTED_MEM_JSON) == getStateJson(ACTIVE_MEM_JSON)) {
				for (int channel = 0; channel < polyChannels; channel++) {
					if (!getChannelHld(channel) || getStateJson(LOAD_HLD_CHANNELS_JSON) == 1.0f) {
						loadChannel(channel);
					}
				}
			}
			else {
				setStateJson(ACTIVE_MEM_JSON, getStateJson(SELECTED_MEM_JSON));
			}
		}	

		// Handle MEM STO
		if((changeParam(STO_PARAM) && (getStateParam(STO_PARAM) > 0.f)) || 
		   (getInputConnected(STO_INPUT) && changeInput (STO_INPUT) && (getStateInput(STO_INPUT) > 0.f))
		  ) {
			setStateJson(ACTIVE_MEM_JSON, getStateJson(SELECTED_MEM_JSON));
			for (int channel = 0; channel < polyChannels; channel++) {
				storeChannel(channel);
			}
		}	

		// Handle EXT_ON Button presses
		if (inChangeParam (EXT_ON_PARAM)) {	//	User clicked on tr/gt button
			setStateJson (EXT_ON_JSON, getStateParam(EXT_ON_PARAM));
		}

		// Main CLK Processing here
		int extChannels = inputs[EXT_INPUT].getChannels(); 
		if (changeInput (CLK_INPUT) && getStateInput(CLK_INPUT) > 0.f) {
			// DEBUG("polyChannels = %d", polyChannels);
			if (!hadReset) {
				advanceHeads();
			}
			else {
				hadReset = false;
			}
			// Process Channels
			for (int channel = 0; channel < polyChannels; channel ++) {
				int head = getStateJson(HEAD_JSON + channel);
				float scl = getChannelScl(channel);
				float ofs = getChannelOfs(channel);
				// HLD takes precedence no change of steps when channel is on hold
				if (!getChannelHld(channel)) {
					if (getStateJson(EXT_ON_JSON) > 0.f && getChannelRec(channel) > 5.f) {
						// User is holding REC and External Source is enabled, so copy external Source to step
						if (channel < extChannels) {
							setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head, OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel]);
							eventStatus[channel] = EVENT_SOURCE;
						}					
					}
					else if (getChannelClr(channel) > 5.f) {
						// DEBUG("clearing channel %d at %d", channel, head);
						setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head, ofs);
					}
					else {
						bool randomize = false;
						if (getChannelRnd(channel) > 5.f ) {
							randomize = true;
						}
						else {
							float lock = getChannelLock(channel);
							if (lock / 100.f < getRandom(&globalRandom)) {
								// DEBUG("replace channel %d at %d", channel, head);
								// We have to replace the currect step
								// check for src or random
								float balance = getChannelBalance(channel);
								// DEBUG("balance = %lf", balance);
								if (balance / 100.f > getRandom(&globalRandom)) {
									// DEBUG(" from RND");
									// replace from random
									randomize = true;
								}
								else {
									int index = STEPS_JSON + MAX_LOOP_LEN * channel + head;
									float v = getStateJson(index);
									// replace from source
									if (getStateJson(EXT_ON_JSON) > 0.f) {
										// source is extern
										// DEBUG(" from EXT");
										if (channel < extChannels) {
											setStateJson(index, OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel]);
										}
									}
									else {
										// source is MEM
										// DEBUG(" from MEM");
										int mem = MEM_JSON + getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + channel * MAX_LOOP_LEN + head;
										setStateJson(index, getStateJson(mem));
									}
									if (getStateJson(index) == v) {
										eventStatus[channel] = EVENT_SOURCE_EQUAL;
									}
									else {
										eventStatus[channel] = EVENT_SOURCE;
									}
								}
							}
						}
						float rnd;
						if (randomize) {
							// User is holding RND so we force a randomize of the currect step
							if (getStateJson(SCALE_MODE_JSON) == ON_RANDOMIZE) {
								float low = ofs;
								if (scl < 0) {
									low += scl;
								}
								float high = ofs + abs(scl);
								if (low < -10.f) low = -10.f;
								if (high > 10.f) high = 10.f;
								if (getStateJson(SCALE_MODE_JSON) == ON_RANDOMIZE)
									rnd = low + getRandom(&globalRandom) * (high - low);
								// DEBUG ("scl = %lf, ofs = %lf, low = %lf, high = %lf, rnd = %lf", scl, ofs, low, high, rnd);
								setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head, rnd);
							}
							else {
								rnd = getRandom(&globalRandom) * 10.f;
								setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head, rnd);							
							}
							eventStatus[channel] = EVENT_RANDOM;
						}
					}
				}
				// MEM display color
				if (memWidget != NULL) {
					if (getStateJson(ACTIVE_MEM_JSON) != getStateJson(SELECTED_MEM_JSON)) {
						memWidget->customForegroundColor = true;
						memWidget->foregroundColor = nvgRGB(255,0,0);
					}
					else {
						memWidget->customForegroundColor = false;
					}
				}
				// SRC_OUTPUT
				if (getStateJson(EXT_ON_JSON) > 0.f) {
					int cvOutPolyIdx = SRC_OUTPUT * POLY_CHANNELS + channel;
					OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] = OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel];
					OL_outStateChangePoly[cvOutPolyIdx] = true;
					// setStateOutPoly(SRC_OUTPUT, channel, OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel]);
					// DEBUG(" SRC_OUTPUT[%d] from EXT-> %lf", channel, OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel]);
				}
				else {
					int mem = MEM_JSON + getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + channel * MAX_LOOP_LEN + head;
					setStateOutPoly(SRC_OUTPUT, channel, getStateJson(mem));
					// DEBUG(" SRC_OUTPUT[%d] from MEM-> %lf", channel, getStateJson(mem));
				}

				float cv = getStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head);
				// GATE_OUTPUT
				float gtp = getChannelGtp(channel); // Gate Probailitoy between 0 an 100

				float gCv = 0;
				if (getStateJson(SCALE_MODE_JSON) == ON_RANDOMIZE) {
					// cv range is -10 to 10 so we scale the cv to [0:00]
					if (scl > 0) { 
						// unipolar cv
						gCv = cv * 10.f; // scaled to [0:100]
					}
					else {
						// bipolar cv
						gCv = (cv + 10.f) / 0.2f; // scaled to [0:100]
					}
					if (gCv < 0) gCv = 0;
					if (gCv > 100) gCv = 100;
					gCv = 100 - gCv;
				}
				else {
					gCv = cv * 10.f;
				}
				int gateOutPolyIdx = GATE_OUTPUT * POLY_CHANNELS + channel;
				if ((gCv + 0.001) < gtp || gtp == 10.f) { // +0.001 to guarantee no gate is produced when gtp is 0
					OL_statePoly[NUM_INPUTS * POLY_CHANNELS + gateOutPolyIdx] = 10.f;
					OL_outStateChangePoly[gateOutPolyIdx] = true;
				}
				else {
					// Do not signal a output change, this will taken as a trigger !
					// OL_outStateChangePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
					outputs[GATE_OUTPUT].setVoltage (0.f, channel);
				}
				if (getStateJson(GATE_IS_TRG_JSON) != 1.f) {
					OL_isGatePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
					OL_isSteadyGate[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
				}
				else {
					OL_isGatePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = false;
					OL_isSteadyGate[GATE_OUTPUT * POLY_CHANNELS + channel] = false;
				}
				// CV_OUTPUT
				if (getStateJson(SCALE_MODE_JSON) == ON_OUTPUT) {
					float low = ofs;
					if (scl < 0) {
							low += scl;
					}
					float high = ofs + abs(scl);
					if (low < -10.f) low = -10.f;
					if (high > 10.f) high = 10.f;
					cv = low + cv / 10 * (high - low);
				}
				setStateOutPoly(CV_OUTPUT, channel, cv);
				// DEBUG(" CV_OUTPUT[%d] -> %lf", channel, cv);
			}
		}
		setOutPolyChannels(SRC_OUTPUT, polyChannels);
		setOutPolyChannels(GATE_OUTPUT, polyChannels);
		setOutPolyChannels(CV_OUTPUT, polyChannels);
	}

	/**
		Module specific input processing called from process () in OrangeLineCommon.hpp
		right after generic processParamsAndInputs ()

		moduleProcessState () should only be used to derive json state and module member variables
		from params and inputs collected by processParamsAndInputs ().

		This method should not do dsp or other logic processing.
	*/
	inline void moduleProcessState()
	{
	}

	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges()
	{
		setStateLight (HLD_ON_LIGHT, getStateJson (HLD_ON_JSON) * 255.f);
		setStateLight (EXT_ON_LIGHT, getStateJson (EXT_ON_JSON) * 255.f);
	}

	// XHostInterface - see ExpanderParamAccessSpec.md's "Host Interface" section. Candidate
	// list/order matches xCandidates[] above (verified against moduleInitStateTypes()/
	// moduleParamConfig() directly, not just enum names - EXT_INPUT excluded, it's a real
	// signal path, not a CV-modulates-a-knob candidate).
	int getXParamCount() override { return NUM_X_CANDIDATES; }
	const char* getXParamName(int index) override { return xCandidates[index].name; }
	const char* getXParamShortName(int index) override { return xCandidates[index].shortName; }
	XParamType getXParamType(int index) override { return xCandidates[index].type; }
	XAlign getXParamAlign(int index) override { return xCandidates[index].align; }
	NVGcolor getXParamColor(int index) override { return xCandidates[index].color; }
	bool isXParamEngaged(int index) override { return xCandidates[index].boundExpanderId != -1; }
	int64_t getXParamBoundId(int index) override { return xCandidates[index].boundExpanderId; }
	bool isXParamCableConnected(int index) override { return inputs[xCandidates[index].inputId].isConnected(); }
	// See XHostInterface::requestXBind()'s own interface comment (XShared.hpp) - replaces the
	// former physical-only left-side chain-walk entirely. Called directly by whichever Expander's
	// own moduleProcess() just detected its Engage button being pressed, using whatever Host it
	// already resolved via getXHost() - works identically whether that resolution came from
	// physical adjacency or a persistently-connected-but-detached neighbor further down the
	// chain, since both are already the same trusted path browsing/reading already uses. Exactly
	// the same three-way decision the old chain-walk's grant branch made.
	void requestXBind(int index, int64_t expanderId, XExpanderInterface *expander) override
	{
		if (index < 0 || index >= NUM_X_CANDIDATES)
			return;
		if (xCandidates[index].boundExpanderId == -1)
		{
			// An Expander may be bound to at most one candidate, on one Host, anywhere in the
			// rack, at any time - clear any existing binding (including a different candidate on
			// THIS SAME Host) before granting this one. See xUnbindExpanderEverywhere()'s own
			// comment (XShared.hpp) for why (retires the old multi-host relay technique).
			xUnbindExpanderEverywhere(expanderId);
			xCandidates[index].boundExpanderId = expanderId; // bind (was available)
			xCandidates[index].boundExpander = expander;     // cache directly - we already have
			                                                  // the Expander's own pointer here,
			                                                  // no lookup needed at all
			expander->setXBoundHostId(this->id, this);       // push the new binding directly,
			                                                  // pointer included both ways - we
			                                                  // already have the Expander's own
			                                                  // pointer, and it already has ours
			                                                  // (`this`), no lookup needed at all
		}
		else if (xCandidates[index].boundExpanderId == expanderId)
			resetXParam(index); // disengage (toggle) - also pushes setXBoundHostId(-1)
		// else: taken by someone else, or cable-connected - no-op
	}
	// Shared by resetXParam() and resetXParamNoLock() below - the ONLY place that clears a
	// candidate binding, so the push-notify can never be missed by a future edit that adds yet
	// another clearing path. noLock selects APP->engine->getModule_NoLock() instead of the
	// regular, share-locking getModule() - required whenever called from a context that already
	// holds Rack's own engine exclusive lock, onRemove() OR onReset()/Initialize (see
	// resetXParamNoLock()'s own interface comment, XShared.hpp, for why), harmless overhead-wise
	// either way for the normal (non-locked) case since it's still just one single targeted
	// lookup, not a scan.
	inline void clearXParamBinding(int index, bool noLock)
	{
		int64_t expanderId = xCandidates[index].boundExpanderId;
		if (expanderId != -1)
		{
			Module *m = noLock ? APP->engine->getModule_NoLock(expanderId) : APP->engine->getModule(expanderId);
			XExpanderInterface *exp = m ? dynamic_cast<XExpanderInterface*>(m) : nullptr;
			if (exp)
				exp->setXBoundHostId(-1);
		}
		xCandidates[index].boundExpanderId = -1;
		xCandidates[index].boundExpander = nullptr;
	}
	void resetXParam(int index) override { clearXParamBinding(index, false); }
	// See XHostInterface::resetXParamNoLock()'s own comment (XShared.hpp) for why this exists as
	// a separate method at all, rather than just always using resetXParam().
	void resetXParamNoLock(int index) override { clearXParamBinding(index, true); }
	// See XHostInterface::recoverXParamBinding()'s own interface comment (XShared.hpp) - a raw
	// overwrite with no push to anyone, safe to call even before this Morpheus has ever ticked
	// process() (touches only the candidate array, already valid straight out of the
	// constructor/dataFromJson()).
	void recoverXParamBinding(int index, int64_t expanderId, XExpanderInterface *expander = nullptr) override
	{
		xCandidates[index].boundExpanderId = expanderId;
		xCandidates[index].boundExpander = expander; // cached directly if the caller already has
		                                              // it (every real caller does - see
		                                              // XShared.hpp's findXBoundHostId()/
		                                              // tryRecoverXParamSlot())
	}
	int64_t getXSelfId() override { return OL_selfId; }

	// ExpanderBridgeInterface (ExpanderBridge.hpp) - a Host trivially reports its own id; belongs
	// to all three families it implements at once (X/XO/NEO), so a touching neighbor of any of
	// them recognizes it as compatible; customName is the same editable label every family's own
	// Free/Disconnect-style menu item can now show uniformly.
	int64_t getBridgeHostId() override { return (int64_t) this->id; }
	std::vector<ExpanderFamily> getBridgeFamilies() override { return getModuleFamilies(model->slug); }
	std::string getBridgeHostName() override { return customName; }
	// Listener registry backing registerBridgeListener()/unregisterBridgeListener() below - see
	// BridgeListenerRegistry's own comment (ExpanderBridge.hpp). One shared registry covers both
	// XO-family Expanders and any NEO instance caching a pointer to this Morpheus, since neither
	// needs to be told apart at notification time (invalidateBridgeCache() is generic).
	BridgeListenerRegistry bridgeListeners;
	void registerBridgeListener(ExpanderBridgeInterface *listener) override { bridgeListeners.add(listener); }
	void unregisterBridgeListener(ExpanderBridgeInterface *listener) override { bridgeListeners.remove(listener); }
	// Not a direct OL_statePoly read - that array holds CV/cable units, not display units (see
	// computeTakeoverDisplay()'s own comment and CLAUDE.md's Pitfalls entry on the divergent
	// feedback loop the old raw/CV mismatch caused for SCL/OFS specifically).
	// computeTakeoverDisplay() is the one place that already knows, per candidate, how to convert
	// "whatever Morpheus is actually using right now" into the display units the Expander's own
	// knob directly holds - reusing it here keeps the takeover always correct regardless of
	// binding state, instead of assuming OL_statePoly already holds the right unit.
	float getXParamTakeoverValue(int index, int channel) override { return computeTakeoverDisplay(index, channel); }
	// The Expander's own knob value (already in this candidate's own display units, e.g. steps
	// for LOOP_LEN, percent for GTP/LOCK/BALANCE) -> the CV/cable value this input actually
	// expects - see the interface method's own comment. Digital types just pass the value straight
	// through, unused (they're read via a threshold, not scaled).
	float scaleXParamValue(int index, float displayValue) override
	{
		return displayValue * xCandidates[index].cvScale;
	}
	// Continuous candidates only. Delegates entirely to xCandidates[index].format - see
	// XFormatFn's own comment for why: every candidate's own display behavior (color, range,
	// align, AND format) now lives in one row of that one table, not scattered across a separate
	// switch statement here. `value` already arrives in display units (the Expander's own knob
	// value) - no scaleXParamValue() hop needed here, unlike before this redesign.
	std::string formatXParamValue(int index, float value) override
	{
		XFormatFn format = xCandidates[index].format;
		if (!format)
			return ""; // digital types (Toggle/Click/Push) - no numeric display at all
		return format(value);
	}
	float getXParamDisplayMin(int index) override     { return xCandidates[index].displayMin; }
	float getXParamDisplayMax(int index) override     { return xCandidates[index].displayMax; }
	float getXParamDisplayDefault(int index) override { return xCandidates[index].displayDefault; }
	bool getXParamSnap(int index) override            { return xCandidates[index].snap; }
	const char* getXParamUnit(int index) override     { return xCandidates[index].unit; }
	float getXStyle() override { return OL_state[STYLE_JSON]; }

	// XOHostInterface - see XOShared.hpp's own architecture comment. No engagement-related
	// methods at all here (unlike XHostInterface above) - reading an output is never exclusive.
	int getXOCount() override { return NUM_XO_CANDIDATES; }
	const char* getXOName(int index) override { return xOutputCandidates[index].name; }
	const char* getXOShortName(int index) override { return xOutputCandidates[index].shortName; }
	XOType getXOType(int index) override { return xOutputCandidates[index].type; }
	XAlign getXOAlign(int index) override { return xOutputCandidates[index].align; }
	NVGcolor getXOColor(int index) override { return xOutputCandidates[index].color; }
	// NOT outputs[id].getChannels() - Rack's own Port::setChannels() silently no-ops while
	// disconnected ("if (this->channels == 0) return;", confirmed directly in the SDK's
	// Port.hpp), so a real cable being unplugged would freeze this at 0 forever regardless of
	// what Morpheus actually computes every tick. getOutPolyChannels() reads the framework's own
	// OL_polyChannels bookkeeping instead - the same indirection setOutPolyChannels() already
	// exists for, unaffected by real-cable connection state.
	int getXOChannelCount(int index) override { return getOutPolyChannels(xOutputCandidates[index].outputId); }
	float getXOChannelValue(int index, int channel) override { return outputs[xOutputCandidates[index].outputId].getVoltage(channel); }
	std::string formatXOValue(int index, float value) override
	{
		XOFormatFn format = xOutputCandidates[index].format;
		if (!format)
			return ""; // XO_TYPE_GATE - no numeric display at all
		return format(value);
	}
	float getXOStyle() override { return OL_state[STYLE_JSON]; }

	// NeoHostInterface - see NeoShared.hpp's own architecture comment. Unlike XHostInterface/
	// XOHostInterface above, this is bidirectional (NEO can write back), but still exposes only
	// proper named methods - NEO never touches OL_state/STEPS_JSON/MEM_JSON addressing directly.
	// 18 tracks (2026-07-20 generic track/channel redesign): trackId 0 = TAPE (STEPS_JSON,
	// today's live/in-progress tape), trackId 1 = MSEL (MEM_JSON at whichever slot
	// ACTIVE_MEM_JSON currently selects - the same indirection the original getMemStep() always
	// used), trackId 2..17 = M-01..M-16 (MEM_JSON at a FIXED slot index, addressed directly,
	// bypassing ACTIVE_MEM_JSON entirely - Dieter's own spec, "M-01 to M-16"). memSlotForTrack()
	// is the one place that trackId -> MEM_JSON slot mapping is decided.
	int memSlotForTrack(int trackId)
	{
		return (trackId == 1) ? (int) getStateJson(ACTIVE_MEM_JSON) : (trackId - 2);
	}
	int getTrackCount() override { return 2 + MEM_SLOTS; }
	std::string getTrackName(int trackId) override
	{
		if (trackId == 0)
			return "TAPE";
		if (trackId == 1)
			return "MSEL";
		char buf[8];
		snprintf(buf, sizeof(buf), "M-%02d", trackId - 2 + 1);
		return std::string(buf);
	}
	// Every one of Morpheus's own 18 tracks uses the same POLY_CHANNELS structure - the per-track
	// count exists on the interface purely for Hosts that genuinely differ per track, not because
	// Morpheus itself needs to.
	int getTrackChannelCount(int trackId) override { (void) trackId; return POLY_CHANNELS; }
	float getTrackStep(int trackId, int channel, int step) override
	{
		if (trackId == 0)
			return getStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + step);
		int mem = MEM_JSON + memSlotForTrack(trackId) * POLY_CHANNELS * MAX_LOOP_LEN
		        + channel * MAX_LOOP_LEN + step;
		return getStateJson(mem);
	}
	void setTrackStep(int trackId, int channel, int step, float value) override
	{
		if (trackId == 0)
		{
			setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + step, value);
			return;
		}
		int mem = MEM_JSON + memSlotForTrack(trackId) * POLY_CHANNELS * MAX_LOOP_LEN
		        + channel * MAX_LOOP_LEN + step;
		setStateJson(mem, value);
	}
	// Reuses the existing getChannelLoopLength() helper (already resolves the LOOP_LEN_INPUT-
	// poly-vs-LOOP_LEN_PARAM-knob distinction) rather than duplicating that logic here. Always
	// the live TAPE's own length regardless of which track a row is currently viewing (Dieter's
	// own instruction, 2026-07-20 - stored memory has no length concept of its own, "we just
	// access the part which is accessible by LEN").
	int getLoopLen(int channel) override { return (int) getChannelLoopLength(channel); }
	int getMaxLoopLen() override { return MAX_LOOP_LEN; }
	int getPlayCursor(int channel) override { return (int) getStateJson(HEAD_JSON + channel); }
	// See NeoHostInterface::getChannelRange()'s own comment (NeoShared.hpp) for the full
	// reasoning - scaleOnOutput is module-wide, not per-track/channel, so the dimensions list is
	// deliberately ignored entirely here; every query gets the same answer regardless of which
	// track/channel (or anything else) it names.
	void getChannelRange(const std::vector<NeoDimensionCoord> &dimensions, float &outMin, float &outMax) override
	{
		(void) dimensions;
		if (scaleOnOutput) { outMin = 0.f; outMax = 10.f; }
		else { outMin = -10.f; outMax = 10.f; }
	}
	float getNeoStyle() override { return OL_state[STYLE_JSON]; }
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

// Only 48 columns fit at a readable size within the display's 49mm width (64 would make the
// step squares too small to see/hit) - a lane with loopLen > 48 needs up to 3 pages
// (0-47, 48-95, 96-127) to cover the full MAX_LOOP_LEN of 128. DISPLAY_PAGE_SIZE and the
// display colors are #defined in Morpheus.hpp for easy configuration.

/**
	Visualization display for Morpheus's 16 channels x up to 128 steps. Each of the 16 rows
	(one per poly channel) is 1mm tall, row r starting at local y = r*1mm. Columns are steps,
	1mm apart, page-relative: step i on a given page is at x=(i - page*48)*1mm. A lane's end
	marker is only drawn once the channel's play head has actually reached the page containing
	that end step, so a long lane doesn't show a later page's content prematurely while still
	playing through an earlier one.

	First piece of content: a small unfilled square marking each channel's last active step
	(0-based index loopLen-1), white for now.
*/
struct MorpheusDisplayWidget : Widget
{
	Morpheus *module = nullptr;

	/** Per-(channel, step) flash state, owned entirely by the widget - the module only ever
		delivers a one-shot trigger (Morpheus::eventStatus) for the channel's *current* step.
		Keyed per step (not just per channel) because the flash must stay pinned to the step
		that actually changed, fading out at that fixed position - if it were tracked only per
		channel, the very next clock tick (head moving to a new step) would make the flash
		jump to the new position instead of staying on the one that changed.
		flashStartTime holds the glfwGetTime() timestamp of the most recent trigger for that
		step (far in the past initially, so no flash is active before the first real event);
		flashEventType remembers which of the three event colors (SOURCE/SOURCE_EQUAL/RANDOM)
		to fade from at that moment. */
	double flashStartTime[POLY_CHANNELS][MAX_LOOP_LEN];
	int flashEventType[POLY_CHANNELS][MAX_LOOP_LEN];

	static MorpheusDisplayWidget* create(Vec pos, Vec size, Morpheus *module)
	{
		MorpheusDisplayWidget *w = new MorpheusDisplayWidget();
		w->box.pos = pos;
		w->box.size = size;
		w->module = module;
		for (int i = 0; i < POLY_CHANNELS; i++)
		{
			for (int s = 0; s < MAX_LOOP_LEN; s++)
			{
				w->flashStartTime[i][s] = -1000.0;
				w->flashEventType[i][s] = EVENT_NONE;
			}
		}
		return w;
	}

	void draw(const DrawArgs &args) override
	{
		// Plain background only - always drawn regardless of the room-brightness/dim-lights
		// setting, which only applies to the self-illuminating step/cursor content in
		// drawLayer() below (same split NumberWidget/TextWidget already use for their own
		// glowing text - this display just also needs its own non-glowing background layer).
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, DISPLAY_BG_COLOR);
		nvgFill(args.vg);
	}

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		// do not try to draw if module is not initielized yet.
		if (!module)
			return;

		// "Visual On/Off" right-click toggle - skip the per-channel/per-step content
		// entirely (the actual CPU cost) when off, leaving just the plain black background.
		if (!module->visualOn)
			return;

		for (int row = 0; row < POLY_CHANNELS; row++)
		{
			int loopLen = (int) module->getChannelLoopLength(row);
			if (loopLen < 1)
				continue;
			int pos = (int) module->getStateJson(HEAD_JSON + row);
			int page = pos / DISPLAY_PAGE_SIZE;

			// Consume a fresh step-exchange event (one-shot from moduleProcess(), always for
			// the channel's *current* step) into our own per-step time-based fade timer, then
			// reset it so it isn't picked up again.
			if (module->eventStatus[row] != EVENT_NONE)
			{
				flashEventType[row][pos] = module->eventStatus[row];
				flashStartTime[row][pos] = glfwGetTime();
				module->eventStatus[row] = EVENT_NONE;
			}

			// Step value colors for the currently visible page - steps at or past loopLen
			// stay black (background already covers that). Same box geometry as the pos
			// cursor below, drawn first so the cursor/end marker render on top of it.
			int pageStart = page * DISPLAY_PAGE_SIZE;
			int pageEnd = std::min(loopLen, pageStart + DISPLAY_PAGE_SIZE);
			for (int step = pageStart; step < pageEnd; step++)
			{
				float cv = module->getStateJson(STEPS_JSON + MAX_LOOP_LEN * row + step);
				float t = clamp((cv + 10.f) / 20.f, 0.f, 1.f);

				NVGcolor valueColor;
				if (module->scaleOnOutput) {
					// Unipolar (0-10V, no negatives) - simple red-to-green by value, source
					// match still shown via RGB (matches) vs CMY (drifted from source), since
					// there's screen real estate to spare without a bipolar range to represent.
					// Same source selection as the LOCK/S<>R exchange logic: EXT_INPUT if
					// EXT_ON, else the active MEM slot.
					float sourceValue;
					if (module->getStateJson(EXT_ON_JSON) > 0.f) {
						sourceValue = module->OL_statePoly[EXT_INPUT * POLY_CHANNELS + row];
					}
					else {
						int mem = MEM_JSON + (int) module->getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + row * MAX_LOOP_LEN + step;
						sourceValue = module->getStateJson(mem);
					}
					bool matchesSource = (cv == sourceValue);

					if (matchesSource)
						valueColor = nvgLerpRGBA(DISPLAY_MATCH_LOW_COLOR, DISPLAY_MATCH_HIGH_COLOR, t);
					else
						valueColor = nvgLerpRGBA(DISPLAY_DIRTY_LOW_COLOR, DISPLAY_DIRTY_HIGH_COLOR, t);
				}
				else {
					// Bipolar (-10..+10V) - no source-match distinction, that'd be too many
					// colors at once on top of the already-meaningful red/green value range.
					valueColor = nvgLerpRGBA(DISPLAY_BIPOLAR_LOW_COLOR, DISPLAY_BIPOLAR_HIGH_COLOR, t);
				}

				double flashElapsed = glfwGetTime() - flashStartTime[row][step];
				if (flashElapsed >= 0.0 && flashElapsed < DISPLAY_FLASH_FADE_TIME)
				{
					float flashAmount = 1.f - (float) (flashElapsed / DISPLAY_FLASH_FADE_TIME);
					NVGcolor flashColor;
					switch (flashEventType[row][step])
					{
						case EVENT_SOURCE:       flashColor = DISPLAY_FLASH_SOURCE_COLOR; break;
						case EVENT_SOURCE_EQUAL: flashColor = DISPLAY_FLASH_SOURCE_EQUAL_COLOR; break;
						default:                 flashColor = DISPLAY_FLASH_RANDOM_COLOR; break;
					}
					valueColor = nvgLerpRGBA(valueColor, flashColor, flashAmount);
				}

				float vx = mm2px((step - pageStart) * 1.f + 0.25f);
				float vy = mm2px(row * 1.f + 0.25f);
				float vside = mm2px(0.75f);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, vx, vy, vside, vside);
				nvgFillColor(args.vg, valueColor);
				nvgFill(args.vg);
			}

			float x = mm2px((pos % DISPLAY_PAGE_SIZE) * 1.f + 0.25f);
			float y = mm2px(row * 1.f + 0.25f);
			float side = mm2px(0.75f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x, y, side, side);
			nvgFillColor(args.vg, DISPLAY_POS_COLOR);
			nvgFill(args.vg);

			// Display Loop End Marker if last page is visible
			if (loopLen > (page + 1 ) * DISPLAY_PAGE_SIZE)
				continue;
			x = mm2px(((loopLen - 1) % DISPLAY_PAGE_SIZE) * 1.f + 0.125f);
			y = mm2px(row * 1.f + 0.125f);
			side = mm2px(1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x, y, side, side);
			nvgStrokeWidth(args.vg, mm2px(0.25f));
			nvgStrokeColor(args.vg, DISPLAY_END_MARKER_COLOR);
			nvgStroke(args.vg);
		}
		Widget::drawLayer(args, 1);
	}
};

// Tunable by eye - play with these until the ring sits right against the jack's own footprint.
#define X_SLOT_RING_RADIUS_OFFSET_MM -1.9f
#define X_SLOT_RING_STROKE_WIDTH_MM  0.6f

/**
	A plain PJ301MPort with a thin ring drawn around it in its X-candidate's own accent color
	(XHostInterface::getXParamColor()) - a static "this socket offers an X-slot" marker, always
	shown regardless of connection/binding state, so a glance at the panel tells you which jacks
	are X8-bindable and lets you match them up against the Expander's own color-coded display.
*/
struct MorpheusXSlotPort : PJ301MPort
{
	void drawLayer(const DrawArgs &args, int layer) override
	{
		PJ301MPort::drawLayer(args, layer);
		if (layer != 1)
			return;
		Morpheus *m = module ? dynamic_cast<Morpheus*>(module) : nullptr;
		if (!m)
			return;
		for (int i = 0; i < NUM_X_CANDIDATES; i++)
		{
			if (m->xCandidates[i].inputId != portId)
				continue;
			float r = box.size.x / 2.f + mm2px(X_SLOT_RING_RADIUS_OFFSET_MM);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, box.size.x / 2.f, box.size.y / 2.f, r);
			nvgStrokeWidth(args.vg, mm2px(X_SLOT_RING_STROKE_WIDTH_MM));
			nvgStrokeColor(args.vg, m->xCandidates[i].color);
			nvgStroke(args.vg);
			break;
		}
	}
};

/**
	Output-side counterpart of MorpheusXSlotPort above - same static accent-ring marker, but for
	xOutputCandidates[] and matched against portId as an OUTPUT id instead of an input id. Kept
	Morpheus-specific like its input-side sibling rather than promoted into XOCommon.hpp - would
	only be worth generalizing once a second real Host needs the exact same ring.
*/
struct MorpheusXOSlotPort : PJ301MPort
{
	void drawLayer(const DrawArgs &args, int layer) override
	{
		PJ301MPort::drawLayer(args, layer);
		if (layer != 1)
			return;
		Morpheus *m = module ? dynamic_cast<Morpheus*>(module) : nullptr;
		if (!m)
			return;
		for (int i = 0; i < NUM_XO_CANDIDATES; i++)
		{
			if (m->xOutputCandidates[i].outputId != portId)
				continue;
			float r = box.size.x / 2.f + mm2px(X_SLOT_RING_RADIUS_OFFSET_MM);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, box.size.x / 2.f, box.size.y / 2.f, r);
			nvgStrokeWidth(args.vg, mm2px(X_SLOT_RING_STROKE_WIDTH_MM));
			nvgStrokeColor(args.vg, m->xOutputCandidates[i].color);
			nvgStroke(args.vg);
			break;
		}
	}
};

/**
	Main Module Widget
*/
struct MorpheusWidget : ModuleWidget
{
	char memBuffer[3];
	XExtStripWidget *extStrip = nullptr;
	XExtStripWidget *extStripXO = nullptr; // right edge - toward the first XO-family Expander

	MorpheusWidget(Morpheus *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorpheusOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorpheusBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorpheusDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addChild(MorpheusDisplayWidget::create(calculateCoordinates(1.25f, 25.95f, 0.f), mm2px(Vec(48.4f, 16.3f)), module));

		// Connection lights are gone (superseded by the seam/logo-cover mechanism - see
		// ExpanderBridge.hpp's own file comment).

		// Positions extracted from res/MorpheusWorkTest.svg's Controls layer (2026-07-13) -
		// panel reorganized to make room for the future visualization display (reserved band
		// at y=25.6-42.4). All values below are already true geometric centers, so offset=0.f
		// throughout (adding the usual OFFSET_* constants on top would double-shift, as it did
		// briefly for RECALL/CV2CC's lock buttons - see CLAUDE.md).
		addParam (createParamCentered<RoundLargeBlackKnob> (calculateCoordinates (17.780, 16.511, 0.f),  module, LOCK_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates ( 6.154, 16.511, 0.f), module, LOCK_INPUT));
		addParam (createParamCentered<RoundLargeBlackKnob> (calculateCoordinates (33.020, 16.511, 0.f),  module, BALANCE_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (44.704, 16.511, 0.f), module, BALANCE_INPUT));

        RoundSmallBlackKnob *knob;
        knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates ( 6.117, 52.275, 0.f),  module, LOOP_LEN_PARAM);
        knob->snap = true;
        addParam (knob);

		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates ( 6.154, 61.469, 0.f), module, LOOP_LEN_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (15.769, 52.245, 0.f),  module, MEM_UP_PARAM));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (15.806, 61.200, 0.f),  module, MEM_DOWN_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (25.458, 61.469, 0.f), module, MEM_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (35.073, 52.260, 0.f),  module, STO_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (35.110, 61.454, 0.f), module, STO_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (44.725, 52.245, 0.f),  module, RCL_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (44.762, 61.454, 0.f), module, RCL_INPUT));

		addParam (createParamCentered<VCVLatch> (calculateCoordinates  ( 6.111, 72.948, 0.f),  module, HLD_ON_PARAM));
 		addChild (createLightCentered<LargeLight<YellowLight>>	(calculateCoordinates  ( 6.111, 72.948, 0.f), module, HLD_ON_LIGHT));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates ( 6.096, 80.519, 0.f), module, HLD_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (15.762, 72.948, 0.f),  module, RND_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (15.748, 80.519, 0.f), module, RND_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (25.415, 72.948, 0.f),  module, SHIFT_LEFT_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (25.400, 80.519, 0.f), module, SHIFT_LEFT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (35.066, 72.948, 0.f),  module, SHIFT_RIGHT_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (35.052, 80.519, 0.f), module, SHIFT_RIGHT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (44.718, 72.948, 0.f),  module, CLR_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (44.704, 80.519, 0.f), module, CLR_INPUT));

		addParam (createParamCentered<VCVLatch> (calculateCoordinates  ( 6.096, 93.219, 0.f),  module, EXT_ON_PARAM));
 		addChild (createLightCentered<LargeLight<YellowLight>>	(calculateCoordinates  ( 6.096, 93.219, 0.f), module, EXT_ON_LIGHT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 6.096,102.363, 0.f), module, EXT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (15.749, 93.219, 0.f),  module, REC_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (15.748,102.363, 0.f), module, REC_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (25.400, 93.219, 0.f),  module, GTP_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (25.400,102.363, 0.f), module, GTP_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (35.052, 93.219, 0.f),  module, SCL_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (35.052,102.363, 0.f), module, SCL_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (44.704, 93.219, 0.f),  module, OFS_PARAM));
		addInput (createInputCentered<MorpheusXSlotPort> (calculateCoordinates (44.704,102.363, 0.f), module, OFS_INPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 6.096,115.574, 0.f), module, RST_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (15.748,115.574, 0.f), module, CLK_INPUT));
   		addOutput (createOutputCentered<MorpheusXOSlotPort>	(calculateCoordinates (25.400,115.574, 0.f),  module, SRC_OUTPUT));
   		addOutput (createOutputCentered<MorpheusXOSlotPort>	(calculateCoordinates (35.052,115.574, 0.f),  module, GATE_OUTPUT));
   		addOutput (createOutputCentered<MorpheusXOSlotPort>	(calculateCoordinates (44.704,115.574, 0.f),  module, CV_OUTPUT));

		float *pvalue = (module != nullptr ? &(module->selectedMem) : nullptr);
		if (module) {
			module->memWidget = NumberWidget::create(mm2px(Vec(22.3 - 0.25, 54.35f)), module, pvalue, 1.f, "%2.0f", memBuffer, 2);
			module->memWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
			addChild(module->memWidget);
		}
		else {
			NumberWidget *w = NumberWidget::create(mm2px(Vec(22.3 - 0.25, 54.54f)), module, pvalue, 1.f, "%2.0f", memBuffer, 2);
			w->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
			addChild(w);
		}

		addOrangeLineTouchOutputOnly (this, module, NUM_OUTPUTS,
			module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		extStrip = addXExtStripLeft(this);
		extStripXO = addXExtStrip(this, MORPHEUS_PANEL_WIDTH_MM);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		if (module)
		{
			updateXExtStripLeft(extStrip, module, module->leftExpander.module);
			updateXOExtStrip(extStripXO, module, module->rightExpander.module);
		}
		ModuleWidget::step();
	}

	struct GateIsTrgItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[GATE_IS_TRG_JSON] == 0.f)
				module->OL_setOutState(GATE_IS_TRG_JSON, 1.f);
			else
				module->OL_setOutState(GATE_IS_TRG_JSON, 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[GATE_IS_TRG_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct RecallOnMemCvChangeItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[RECALL_ON_MEM_CV_CHANGE_JSON] == 0.f)
				module->OL_setOutState(RECALL_ON_MEM_CV_CHANGE_JSON, 1.f);
			else
				module->OL_setOutState(RECALL_ON_MEM_CV_CHANGE_JSON, 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[RECALL_ON_MEM_CV_CHANGE_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct LoadOnMemCvChangeItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[LOAD_ON_MEM_CV_CHANGE_JSON] == 0.f)
				module->OL_setOutState(LOAD_ON_MEM_CV_CHANGE_JSON, 1.f);
			else
				module->OL_setOutState(LOAD_ON_MEM_CV_CHANGE_JSON, 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[LOAD_ON_MEM_CV_CHANGE_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct SmartHoldItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[SMART_HOLD_JSON] == 0.f)
				module->OL_setOutState(SMART_HOLD_JSON, 1.f);
			else
				module->OL_setOutState(SMART_HOLD_JSON, 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[SMART_HOLD_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct MemIsHalftonesItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[MEM_IS_HALFTONES_JSON] == 0.f)
				module->OL_setOutState(MEM_IS_HALFTONES_JSON, 1.f);
			else
				module->OL_setOutState(MEM_IS_HALFTONES_JSON, 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[MEM_IS_HALFTONES_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct LoadHldChannelsItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[LOAD_HLD_CHANNELS_JSON] == 0.f)
				module->OL_setOutState(LOAD_HLD_CHANNELS_JSON, 1.f);
			else
				module->OL_setOutState(LOAD_HLD_CHANNELS_JSON, 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[LOAD_HLD_CHANNELS_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct ScaleOnOutputItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[SCALE_MODE_JSON] == ON_RANDOMIZE) {
				module->OL_setOutState(SCALE_MODE_JSON, ON_OUTPUT);
				module->scaleOnOutput = true;
			}
			else {
				module->OL_setOutState(SCALE_MODE_JSON, ON_RANDOMIZE);
				module->scaleOnOutput = false;
			}
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[SCALE_MODE_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct VisualOnItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			module->visualOn = !module->visualOn;
			module->OL_setOutState(VISUAL_ON_JSON, module->visualOn ? 1.f : 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[VISUAL_ON_JSON] == 1.0f) ? "✔" : "";
		}
	};

	// Collapses the 8 flat behaviour toggles above into one "Behaviour ->" submenu entry -
	// same reasoning/pattern as Channels/Style, keeps the top-level menu short. Each leaf item
	// needs its own explicit setSize() here (see CLAUDE.md's submenu pitfall) even though the
	// exact same item classes didn't need it while added directly to the top-level menu.
	struct BehaviourItem : MenuItem
	{
		Morpheus *module;

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;

			GateIsTrgItem *gateisTrgItem = new GateIsTrgItem();
			gateisTrgItem->module = module;
			gateisTrgItem->text = "Output Trg instead of Gate";
			gateisTrgItem->setSize(Vec(220, 20));
			menu->addChild(gateisTrgItem);

			RecallOnMemCvChangeItem *recallOnMemCvChangeItem = new RecallOnMemCvChangeItem();
			recallOnMemCvChangeItem->module = module;
			recallOnMemCvChangeItem->text = "Recall on Mem CV Change";
			recallOnMemCvChangeItem->setSize(Vec(220, 20));
			menu->addChild(recallOnMemCvChangeItem);

			LoadOnMemCvChangeItem *loadOnMemCvChangeItem = new LoadOnMemCvChangeItem();
			loadOnMemCvChangeItem->module = module;
			loadOnMemCvChangeItem->text = "Load on Mem CV Change";
			loadOnMemCvChangeItem->setSize(Vec(220, 20));
			menu->addChild(loadOnMemCvChangeItem);

			SmartHoldItem *smartHoldItem = new SmartHoldItem();
			smartHoldItem->module = module;
			smartHoldItem->text = "Smart HLD";
			smartHoldItem->setSize(Vec(220, 20));
			menu->addChild(smartHoldItem);

			MemIsHalftonesItem *memIsHalftonesItem = new MemIsHalftonesItem();
			memIsHalftonesItem->module = module;
			memIsHalftonesItem->text = "MEM is Note";
			memIsHalftonesItem->setSize(Vec(220, 20));
			menu->addChild(memIsHalftonesItem);

			LoadHldChannelsItem *loadHldChannelsItem = new LoadHldChannelsItem();
			loadHldChannelsItem->module = module;
			loadHldChannelsItem->text = "Load Channels on HLD";
			loadHldChannelsItem->setSize(Vec(220, 20));
			menu->addChild(loadHldChannelsItem);

			ScaleOnOutputItem *scaleOnOutputItem = new ScaleOnOutputItem();
			scaleOnOutputItem->module = module;
			scaleOnOutputItem->text = "Scale on Output";
			scaleOnOutputItem->setSize(Vec(220, 20));
			menu->addChild(scaleOnOutputItem);

			VisualOnItem *visualOnItem = new VisualOnItem();
			visualOnItem->module = module;
			visualOnItem->text = "Visual On/Off";
			visualOnItem->setSize(Vec(220, 20));
			menu->addChild(visualOnItem);

			return menu;
		}
	};

	// Last-resort recovery: clears every candidate's binding at once, same effect as calling
	// resetXParam() on all of them individually. Meant for "I deleted an X module (or otherwise
	// made a mess) and something's stuck bound" - the normal way to release one binding is either
	// the bound Expander's own Bind button, or a real cable taking over that input; this is the
	// blunt manual override when that's not practical anymore.
	struct MorpheusUnbindAllItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			for (int i = 0; i < NUM_X_CANDIDATES; i++)
				module->resetXParam(i);
		}
	};

	// Morpheus's own "Input Binds" tree - simpler than the X-family Expander's own flat bind
	// items (see X8Common.hpp's addXBindMenuItems()): Morpheus can still have several of its OWN
	// candidate slots bound simultaneously, each by a different Expander (the new "one candidate,
	// one Host, at a time" invariant limits a given EXPANDER to a single binding, not how many
	// different Expanders a single HOST can have bound at once) - so there's no hostname level
	// here at all, just Unbind All plus whichever of Morpheus's own candidate slots are currently
	// bound, each with a checkmark (this list only ever contains bound slots) and a click to
	// unbind just that one.
	struct MorpheusInputBindsItem : MenuItem
	{
		Morpheus *module;

		struct SlotItem : MenuItem
		{
			Morpheus *module;
			int index;
			void onAction(const event::Action &e) override { module->resetXParam(index); }
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;

			MorpheusUnbindAllItem *unbindAllItem = new MorpheusUnbindAllItem();
			unbindAllItem->module = module;
			unbindAllItem->text = "Free All";
			unbindAllItem->setSize(Vec(160, 20));
			menu->addChild(unbindAllItem);

			for (int i = 0; i < NUM_X_CANDIDATES; i++)
			{
				if (module->xCandidates[i].boundExpanderId == -1)
					continue;
				SlotItem *item = new SlotItem();
				item->module = module;
				item->index = i;
				item->text = module->xCandidates[i].name;
				item->rightText = "✔";
				item->setSize(Vec(160, 20));
				menu->addChild(item);
			}
			return menu;
		}
	};

	struct MorpheusStyleItem : MenuItem
	{
		Morpheus *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = ( module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

    struct PolyChannelsItem : MenuItem
	{
		Morpheus *module;

		struct PolyChannelItem : MenuItem
		{
			Morpheus *module;
			int channels;
			void onAction(const event::Action &e) override
			{
				module->OL_setOutState(POLY_CHANNELS_JSON, float(channels));
			}
			void step() override
			{
				if (module)
					rightText = (module != nullptr && module->OL_state[POLY_CHANNELS_JSON] == channels) ? "✔" : "";
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			PolyChannelItem *polyChannelItem;

			for (int channel = 0; channel < 17; channel++)
			{
				polyChannelItem = new PolyChannelItem();
				polyChannelItem->module = module;
				polyChannelItem->channels = channel;
                if (channel == 0) {
                    polyChannelItem->text = "Auto";
                }
                else {
    				polyChannelItem->text = module->channelNumbers[channel - 1];
                }
				polyChannelItem->setSize(Vec(70, 20));

				menu->addChild(polyChannelItem);
			}
			return menu;
		}
	};

	// Plain right-click-menu text field for customName (see its own member comment) - standard
	// Rack pattern: ui::TextField fires onChange() on every edit, so just mirror `text` into the
	// module straight away rather than needing a separate "confirm" step.
	struct MorpheusNameField : ui::TextField
	{
		Morpheus *module;
		void onChange(const ChangeEvent &e) override
		{
			if (module)
				module->customName = text;
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Morpheus *module = dynamic_cast<Morpheus *>(this->module);
		assert(module);

		MenuLabel *nameLabel = new MenuLabel();
		nameLabel->text = "Name";
		menu->addChild(nameLabel);

		MorpheusNameField *nameField = new MorpheusNameField();
		nameField->module = module;
		nameField->text = module->customName;
		nameField->box.size = Vec(140.f, 20.f);
		menu->addChild(nameField);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		BehaviourItem *behaviourItem = new BehaviourItem();
		behaviourItem->module = module;
		behaviourItem->text = "Behaviour";
		behaviourItem->rightText = RIGHT_ARROW;
		menu->addChild(behaviourItem);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MenuLabel *expandersLabel = new MenuLabel();
		expandersLabel->text = "Expanders";
		menu->addChild(expandersLabel);

		MorpheusInputBindsItem *inputBindsItem = new MorpheusInputBindsItem();
		inputBindsItem->module = module;
		inputBindsItem->text = "Input Binds";
		inputBindsItem->rightText = RIGHT_ARROW;
		menu->addChild(inputBindsItem);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

        MenuLabel *polyphonyLabel = new MenuLabel();
        polyphonyLabel->text = "Polyphony";
        menu->addChild(polyphonyLabel);

        PolyChannelsItem *polyChannelsItem = new PolyChannelsItem();
        polyChannelsItem->module = module;
        polyChannelsItem->text = "Channels";
        polyChannelsItem->rightText = RIGHT_ARROW;
        menu->addChild(polyChannelsItem);

        spacerLabel = new MenuLabel();
        menu->addChild(spacerLabel);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		MorpheusStyleItem *style1Item = new MorpheusStyleItem();
		style1Item->text = "Orange"; //
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		MorpheusStyleItem *style2Item = new MorpheusStyleItem();
		style2Item->text = "Bright"; //
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		MorpheusStyleItem *style3Item = new MorpheusStyleItem();
		style3Item->text = "Dark"; //
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		addOrangeLineTouchMenuItem(menu, module->OL_touchInPort, module->OL_touchOutPort, &module->OL_touchVisible);
	}
};

Model *modelMorpheus = createModel<Morpheus, MorpheusWidget>("Morpheus");
