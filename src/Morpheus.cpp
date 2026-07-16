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

struct XCandidate
{
	// A constructor (rather than plain aggregate init) because a default member initializer
	// on boundExpanderId would otherwise disqualify this struct from C++11 aggregate init
	// (relaxed in C++14+, but this project builds with -std=c++11).
	XCandidate(int inputId, const char *name, const char *shortName, XParamType type, NVGcolor color)
		: inputId(inputId), name(name), shortName(shortName), type(type), color(color) {}

	int inputId;                   // which InputIds enum value this candidate corresponds to
	const char *name;               // full descriptive name - XHostInterface::getXParamName()
	const char *shortName;          // matches the real Morpheus panel's own printed labels -
	                                 // XHostInterface::getXParamShortName()
	XParamType type;
	NVGcolor color;                 // this slot's own accent color - XHostInterface::getXParamColor(),
	                                 // shown on a bound Expander's display and as its value button's
	                                 // "on" light color
	// -1 = unbound. Persisted (see Morpheus's own moduleExtraDataToJson/FromJson) as a
	// best-effort id: if Rack happens to preserve module ids across a reload (the common case
	// for a plain save+reopen of the same patch), the binding resolves live again exactly as
	// before, including adjacency-gating; if the id doesn't resolve to anything, the existing
	// "bound module gone" handling in the refresh loop already shows this as engaged (green) and
	// frozen until an explicit Reset - so restoring a stale/foreign id is always safe, never
	// silently wrong. Chosen deliberately over dropping the binding (and its green indicator) on
	// every reload.
	int64_t boundExpanderId = -1;
};

struct Morpheus : Module, XHostInterface
{
	float oldClkInputVoltage = 0;
    int polyChannels = 1;
	bool hadReset = true;
	bool isShiftLeft[POLY_CHANNELS];
	bool isShiftRight[POLY_CHANNELS];

	// X-family param-access Expander connection. An Expander only ever attaches to a Host's
	// LEFT side. xConnected drives the cosmetic connection light only; the real mechanism is
	// xCandidates[]/xVirtualChannels[] below, walked/refreshed every moduleProcess() tick.
	bool xConnected = false;

	// Short names match what's actually printed on the real Morpheus panel (verified directly
	// in res/MorpheusWork.svg: LOCK/LEN/HLD/RND/CLR/REC/GTP/SCL/OFS are already there; BAL and
	// <</>> for Shift Left/Right per Dieter). All are <= 5 characters - X8's display has no
	// truncation fallback, see XShared.hpp's getXParamShortName() contract.
	// Per-slot accent color (XHostInterface::getXParamColor()) - a first-pass palette, deliberately
	// just a literal per row so it's trivial to retune any single one later. CLR is red per
	// Dieter's own example; REC gets a distinct amber rather than the same red so the two don't
	// read as the same action at a glance.
	XCandidate xCandidates[NUM_X_CANDIDATES] = {
		{ LOCK_INPUT,        "Lock",              "LOCK", X_PARAM_CONTINUOUS, nvgRGB(0x33, 0x99, 0xff) },
		{ BALANCE_INPUT,     "Balance",           "BAL",  X_PARAM_CONTINUOUS, nvgRGB(0x33, 0xcc, 0xcc) },
		{ LOOP_LEN_INPUT,    "Loop Length",       "LEN",  X_PARAM_CONTINUOUS, nvgRGB(0xff, 0x99, 0x00) },
		{ HLD_INPUT,         "Hold",              "HLD",  X_PARAM_TOGGLE,     nvgRGB(0x66, 0x66, 0xff) },
		{ RND_INPUT,         "Randomize",         "RND",  X_PARAM_PUSH,       nvgRGB(0xcc, 0x00, 0xcc) }, // pushbutton, effect while held
		{ SHIFT_LEFT_INPUT,  "Shift Left",        "<<",   X_PARAM_CLICK,      nvgRGB(0x00, 0xaa, 0x88) },
		{ SHIFT_RIGHT_INPUT, "Shift Right",       ">>",   X_PARAM_CLICK,      nvgRGB(0x00, 0x88, 0xaa) },
		{ CLR_INPUT,         "Clear Loop",        "CLR",  X_PARAM_PUSH,       nvgRGB(0xdd, 0x00, 0x00) }, // pushbutton, effect while held
		{ REC_INPUT,         "Record",            "REC",  X_PARAM_CLICK,      nvgRGB(0xff, 0xaa, 0x00) },
		{ GTP_INPUT,         "Gate Probability",  "GTP",  X_PARAM_CONTINUOUS, nvgRGB(0x99, 0xcc, 0x00) },
		{ SCL_INPUT,         "CV Scale",          "SCL",  X_PARAM_CONTINUOUS, nvgRGB(0x00, 0xcc, 0xff) },
		{ OFS_INPUT,         "CV Offset",         "OFS",  X_PARAM_CONTINUOUS, nvgRGB(0xff, 0x00, 0x66) },
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
		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_t *boundJ = json_array();
			for (int i = 0; i < NUM_X_CANDIDATES; i++)
				json_array_append_new(boundJ, json_integer(xCandidates[i].boundExpanderId));
			json_object_set_new(rootJ, "xBoundExpanderId", boundJ);
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *boundJ = json_object_get(rootJ, "xBoundExpanderId");
			if (!boundJ)
				return;
			for (int i = 0; i < NUM_X_CANDIDATES; i++)
			{
				json_t *idJ = json_array_get(boundJ, i);
				if (idJ && json_is_integer(idJ))
					xCandidates[i].boundExpanderId = json_integer_value(idJ);
			}
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
		styleChanged = true;
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
			else if (channels >= channel) {
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
			else if (channels >= channel) {
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
			else if (channels >= channel) {
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
			else if (channels >= channel) {
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
			else if (channels >= channel) {
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
			else if (channels >= channel) {
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
			else if (channels >= channel) {
				return OL_statePoly[CLR_INPUT * POLY_CHANNELS + channel] > 0.5f ? 10.f : 0.f;;
            }
		}
		return 0.f;
	}

	inline float getChannelGtp(int channel)
	{
		if (getXAwareConnected(GTP_INPUT)) {
			int channels = getXAwareChannels(GTP_INPUT);
            if (channels == 1) {
				return OL_statePoly[GTP_INPUT * POLY_CHANNELS] * 10.f; // input is scaled so 5V is Probability 50%
            }
			else if (channels >= channel) {
				return OL_statePoly[GTP_INPUT * POLY_CHANNELS + channel] * 10.f; // input is scaled so 5V is Probability 50%
            }
		}
		return getStateParam(GTP_PARAM);
	}

	inline float getChannelScl(int channel)
	{
		if (getXAwareConnected(SCL_INPUT)) {
			int channels = getXAwareChannels(SCL_INPUT);
            if (channels == 1) {
				return OL_statePoly[SCL_INPUT * POLY_CHANNELS];
            }
			else if (channels >= channel) {
				return OL_statePoly[SCL_INPUT * POLY_CHANNELS + channel];
            }
		}
		return getStateParam(SCL_PARAM);
	}

	inline float getChannelOfs(int channel)
	{
		if (getXAwareConnected(OFS_INPUT)) {
			int channels = getXAwareChannels(OFS_INPUT);
            if (channels == 1) {
				return OL_statePoly[OFS_INPUT * POLY_CHANNELS];
            }
			else if (channels >= channel) {
				return OL_statePoly[OFS_INPUT * POLY_CHANNELS + channel];
            }
		}
		return getStateParam(OFS_PARAM);
	}

	// Inverse of whatever scaling each getChannelXXX() above applies to a bound candidate's raw
	// input - called continuously, every tick, for every still-unbound candidate (see the
	// refresh loop below), to keep OL_statePoly always holding "what a bound Expander's raw
	// knob would need to read to reproduce the current fallback value". A fresh bind then finds
	// the right value already sitting there, ready to read via getXParamTakeoverValue() - no
	// separate snapshot storage needed. KISS on purpose: this mirrors each reader's own existing
	// formula, it doesn't attempt to redesign their value ranges.
	inline float computeTakeoverRaw(int index, int channel)
	{
		switch (index)
		{
			// LOOP_LEN/GTP/SCL/OFS are pure overrides (no cable/binding -> knob only) - inverse
			// of "raw*100"/"raw*10"/"raw" respectively, clamped into the Expander's own 0..1
			// knob range (values outside what a 0..1 knob can reach - e.g. SCL/OFS's negative
			// half - clamp to the nearest end, same limitation as today, not new).
			case XC_LOOP_LEN: return clamp(getChannelLoopLength(channel) / 100.f, 0.f, 1.f);
			case XC_GTP:       return clamp(getChannelGtp(channel) / 10.f, 0.f, 1.f);
			case XC_SCL:       return clamp(getChannelScl(channel), 0.f, 1.f);
			case XC_OFS:       return clamp(getChannelOfs(channel), 0.f, 1.f);
			// LOCK/BALANCE are additive (knob + raw*10) - right now nothing is contributing, so
			// the CV-neutral raw value is 0, not an inverse of the knob (that would double it).
			// HLD/RND/SHIFT_LEFT/SHIFT_RIGHT/CLR/REC are digital push/click/toggle types -
			// "not currently triggered" is also 0.
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

		// X-family param-access Expander connection - only ever check leftExpander, never
		// rightExpander (see ExpanderParamAccessSpec.md's left-only-attachment architecture).
		xConnected = dynamic_cast<XExpanderInterface*>(leftExpander.module) != nullptr;
		setStateLight(X_CONN_LIGHT, xConnected ? 255.f : 0.f);

		// Walk the *whole* left-side chain (not just the immediate neighbor - several
		// Expanders can be strung together) looking for a fresh engage/disengage request from
		// any of them. Each Expander debounces its own button locally and exposes only a
		// one-shot event - Morpheus does all the deciding here.
		//
		// freshlyBound is purely local to this one tick (not persisted state) - it marks a
		// candidate that just transitioned from unbound to bound right here, so the refresh
		// loop below can leave OL_statePoly exactly as the idle branch last left it for one
		// more tick, instead of immediately overwriting it with the Expander's own (not yet
		// caught up) raw knob value. See the refresh loop's comments for why.
		bool freshlyBound[NUM_X_CANDIDATES] = {false};
		// candidateAdjacent is also purely local to this one tick - marks a candidate whose
		// bound Expander is genuinely found in the chain walk right now (not just resolvable by
		// id). The refresh loop below only treats a binding as truly LIVE while this holds -
		// bound-but-not-currently-adjacent freezes exactly like Track & Hold. This is what makes
		// moving one Expander between several same-type Hosts safe (only ever one Host reads it
		// live at a time, whichever it's actually plugged into right now) and, per Dieter, is
		// deliberately reused as a value-relay technique: engage on Host A, move the Expander to
		// Host B and engage there too (A freezes, its last value stays exactly as it was), read
		// off B's takeover value, then move back to A - A resumes live reading with whatever the
		// Expander now holds, effectively having carried B's value over to A.
		bool candidateAdjacent[NUM_X_CANDIDATES] = {false};
		{
			Module *m = leftExpander.module;
			while (m)
			{
				XExpanderInterface *exp = dynamic_cast<XExpanderInterface*>(m);
				if (!exp)
					break; // chain broken - not an X-family module, stop walking
				for (int i = 0; i < NUM_X_CANDIDATES; i++)
					if (xCandidates[i].boundExpanderId == m->id)
						candidateAdjacent[i] = true;
				if (exp->consumeEngagePress())
				{
					int idx = exp->getXBrowseIndex();
					if (idx >= 0 && idx < NUM_X_CANDIDATES)
					{
						int64_t myId = m->id;
						if (xCandidates[idx].boundExpanderId == -1)
						{
							xCandidates[idx].boundExpanderId = myId;   // bind (was available)
							freshlyBound[idx] = true;
							candidateAdjacent[idx] = true;
						}
						else if (xCandidates[idx].boundExpanderId == myId)
							xCandidates[idx].boundExpanderId = -1;     // disengage (toggle)
						// else: taken by someone else, or cable-connected - no-op
					}
				}
				m = m->leftExpander.module;
			}
		}

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
					xCandidates[i].boundExpanderId = -1;
				xVirtualChannels[inputId] = 0;
				xVirtualConnected[inputId] = false;
				continue;
			}

			if (xCandidates[i].boundExpanderId < 0)
			{
				// Idle: keep OL_statePoly continuously consistent with "the value Morpheus is
				// actually using for this channel right now", already inverse-scaled into the
				// same raw units a bound Expander's own knob supplies (see computeTakeoverRaw()
				// above). No separate snapshot storage needed - a fresh bind always finds the
				// right value already sitting here, ready for the Expander to read via
				// getXParamTakeoverValue() and take over as its own knob's starting position.
				for (int c = 0; c < POLY_CHANNELS; c++)
					OL_statePoly[inputId * POLY_CHANNELS + c] = computeTakeoverRaw(i, c);
				xVirtualChannels[inputId] = 0;
				xVirtualConnected[inputId] = false;
				continue;
			}

			Module *bm = APP->engine->getModule(xCandidates[i].boundExpanderId);
			XExpanderInterface *exp = bm ? dynamic_cast<XExpanderInterface*>(bm) : nullptr;
			if (!exp)
				continue; // bound module gone - stays stale (per spec) until an explicit Reset

			xVirtualConnected[inputId] = true; // bound (even if not the live one right now) = Green
			if (exp->getXBrowseIndex() != i)
				continue; // bound, but looking elsewhere right now - hold last tick's values

			if (!candidateAdjacent[i])
				continue; // bound, but the Expander has physically moved out of this chain right
				          // now - freeze exactly like the "looking elsewhere" case above, don't
				          // keep reading a knob that isn't even pointed at us anymore

			if (freshlyBound[i])
				continue; // just bound this very tick - hold the takeover value one more tick
				          // so the Expander has a chance to read and apply it before its own
				          // (not yet updated) raw knob value starts being read instead

			int channels = exp->getXKnobCount(); // sender (the Expander) decides, not us
			xVirtualChannels[inputId] = channels;
			for (int c = 0; c < channels; c++)
				OL_statePoly[inputId * POLY_CHANNELS + c] = exp->getXKnobValue(c);
			// Channels beyond what THIS Expander actually supplies still need a correct
			// fallback value - Morpheus's own per-channel reader functions fall back to the
			// knob for any channel index >= channels, so keep those slots consistent too, same
			// reasoning as the idle branch above. Otherwise a wider Expander binding later (or
			// per-channel monitoring while not engaged) would find stale leftovers instead.
			for (int c = channels; c < POLY_CHANNELS; c++)
				OL_statePoly[inputId * POLY_CHANNELS + c] = computeTakeoverRaw(i, c);
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
	const char* getXHostTypeName() override { return "MORPH"; }
	int getXParamCount() override { return NUM_X_CANDIDATES; }
	const char* getXParamName(int index) override { return xCandidates[index].name; }
	const char* getXParamShortName(int index) override { return xCandidates[index].shortName; }
	XParamType getXParamType(int index) override { return xCandidates[index].type; }
	NVGcolor getXParamColor(int index) override { return xCandidates[index].color; }
	bool isXParamEngaged(int index) override { return xCandidates[index].boundExpanderId != -1; }
	int64_t getXParamBoundId(int index) override { return xCandidates[index].boundExpanderId; }
	bool isXParamCableConnected(int index) override { return inputs[xCandidates[index].inputId].isConnected(); }
	void resetXParam(int index) override { xCandidates[index].boundExpanderId = -1; }
	float getXParamTakeoverValue(int index, int channel) override { return OL_statePoly[xCandidates[index].inputId * POLY_CHANNELS + channel]; }
	std::string formatXParamValue(int index, float value) override { return ""; } // TODO: needs X8's custom ParamQuantity first, deferred
	float getXStyle() override { return OL_state[STYLE_JSON]; }
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

/**
	Main Module Widget
*/
struct MorpheusWidget : ModuleWidget
{
	char memBuffer[3];

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

		// X-family param-access Expander connection light (step 1 only, see
		// ExpanderParamAccessSpec.md) - placeholder position near the top-left corner, since an
		// Expander only ever attaches to a Host's LEFT side. Same corner-light convention as the
		// LANES family (CLAUDE.md).
		addChild(createLightCentered<AutoHideLight<TinyLight<GreenLight>>>(calculateCoordinates(3.5f, 4.f, 0.f), module, X_CONN_LIGHT));

		// Positions extracted from res/MorpheusWorkTest.svg's Controls layer (2026-07-13) -
		// panel reorganized to make room for the future visualization display (reserved band
		// at y=25.6-42.4). All values below are already true geometric centers, so offset=0.f
		// throughout (adding the usual OFFSET_* constants on top would double-shift, as it did
		// briefly for RECALL/CV2CC's lock buttons - see CLAUDE.md).
		addParam (createParamCentered<RoundLargeBlackKnob> (calculateCoordinates (17.780, 16.511, 0.f),  module, LOCK_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 6.154, 16.511, 0.f), module, LOCK_INPUT));
		addParam (createParamCentered<RoundLargeBlackKnob> (calculateCoordinates (33.020, 16.511, 0.f),  module, BALANCE_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (44.704, 16.511, 0.f), module, BALANCE_INPUT));

        RoundSmallBlackKnob *knob;
        knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates ( 6.117, 52.275, 0.f),  module, LOOP_LEN_PARAM);
        knob->snap = true;
        addParam (knob);

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 6.154, 61.469, 0.f), module, LOOP_LEN_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (15.769, 52.245, 0.f),  module, MEM_UP_PARAM));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (15.806, 61.200, 0.f),  module, MEM_DOWN_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (25.458, 61.469, 0.f), module, MEM_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (35.073, 52.260, 0.f),  module, STO_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (35.110, 61.454, 0.f), module, STO_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (44.725, 52.245, 0.f),  module, RCL_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (44.762, 61.454, 0.f), module, RCL_INPUT));

		addParam (createParamCentered<VCVLatch> (calculateCoordinates  ( 6.111, 72.948, 0.f),  module, HLD_ON_PARAM));
 		addChild (createLightCentered<LargeLight<YellowLight>>	(calculateCoordinates  ( 6.111, 72.948, 0.f), module, HLD_ON_LIGHT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 6.096, 80.519, 0.f), module, HLD_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (15.762, 72.948, 0.f),  module, RND_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (15.748, 80.519, 0.f), module, RND_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (25.415, 72.948, 0.f),  module, SHIFT_LEFT_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (25.400, 80.519, 0.f), module, SHIFT_LEFT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (35.066, 72.948, 0.f),  module, SHIFT_RIGHT_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (35.052, 80.519, 0.f), module, SHIFT_RIGHT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (44.718, 72.948, 0.f),  module, CLR_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (44.704, 80.519, 0.f), module, CLR_INPUT));

		addParam (createParamCentered<VCVLatch> (calculateCoordinates  ( 6.096, 93.219, 0.f),  module, EXT_ON_PARAM));
 		addChild (createLightCentered<LargeLight<YellowLight>>	(calculateCoordinates  ( 6.096, 93.219, 0.f), module, EXT_ON_LIGHT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 6.096,102.363, 0.f), module, EXT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (15.749, 93.219, 0.f),  module, REC_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (15.748,102.363, 0.f), module, REC_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (25.400, 93.219, 0.f),  module, GTP_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (25.400,102.363, 0.f), module, GTP_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (35.052, 93.219, 0.f),  module, SCL_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (35.052,102.363, 0.f), module, SCL_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (44.704, 93.219, 0.f),  module, OFS_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (44.704,102.363, 0.f), module, OFS_INPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 6.096,115.574, 0.f), module, RST_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (15.748,115.574, 0.f), module, CLK_INPUT));
   		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (25.400,115.574, 0.f),  module, SRC_OUTPUT));
   		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (35.052,115.574, 0.f),  module, GATE_OUTPUT));
   		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (44.704,115.574, 0.f),  module, CV_OUTPUT));

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

		if (module)
			module->widgetReady = true;
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

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Morpheus *module = dynamic_cast<Morpheus *>(this->module);
		assert(module);

		addOrangeLineTouchMenuItem(menu, module->OL_touchInPort, module->OL_touchOutPort, &module->OL_touchVisible);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MenuLabel *behaviourLabel = new MenuLabel();
		behaviourLabel->text = "Behaviour";
		menu->addChild(behaviourLabel);

		GateIsTrgItem *gateisTrgItem = new GateIsTrgItem();
		gateisTrgItem->module = module;
		gateisTrgItem->text = "Output Trg instead of Gate";
		menu->addChild(gateisTrgItem);

		RecallOnMemCvChangeItem *recallOnMemCvChangeItem = new RecallOnMemCvChangeItem();
		recallOnMemCvChangeItem->module = module;
		recallOnMemCvChangeItem->text = "Recall on Mem CV Change";
		menu->addChild(recallOnMemCvChangeItem);

		LoadOnMemCvChangeItem *loadOnMemCvChangeItem = new LoadOnMemCvChangeItem();
		loadOnMemCvChangeItem->module = module;
		loadOnMemCvChangeItem->text = "Load on Mem CV Change";
		menu->addChild(loadOnMemCvChangeItem);

		SmartHoldItem *smartHoldItem = new SmartHoldItem();
		smartHoldItem->module = module;
		smartHoldItem->text = "Smart HLD";
		menu->addChild(smartHoldItem);

		MemIsHalftonesItem *memIsHalftonesItem = new MemIsHalftonesItem();
		memIsHalftonesItem->module = module;
		memIsHalftonesItem->text = "MEM is Note";
		menu->addChild(memIsHalftonesItem);

		LoadHldChannelsItem *loadHldChannelsItem = new LoadHldChannelsItem();
		loadHldChannelsItem->module = module;
		loadHldChannelsItem->text = "Load Channels on HLD";
		menu->addChild(loadHldChannelsItem);

		ScaleOnOutputItem *scaleOnOutputItem = new ScaleOnOutputItem();
		scaleOnOutputItem->module = module;
		scaleOnOutputItem->text = "Scale on Output";
		menu->addChild(scaleOnOutputItem);

		VisualOnItem *visualOnItem = new VisualOnItem();
		visualOnItem->module = module;
		visualOnItem->text = "Visual On/Off";
		menu->addChild(visualOnItem);

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
	}
};

Model *modelMorpheus = createModel<Morpheus, MorpheusWidget>("Morpheus");
