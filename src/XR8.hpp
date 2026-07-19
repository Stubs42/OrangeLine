/*
	XR8.hpp

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
// XO-family Output Expander with a different job than XO8/XOD8: instead of passing the browsed
// Host output straight through, each channel takes the Host's live value as a seed for a
// deterministic pseudo-random generator (SplitMix64, OrangeLine.hpp) and outputs the result
// instead - "same input always yields the same random chain." Only continuous (value-carrying)
// Host outputs are browsable - gate-type candidates are skipped (see XRModuleCommon.hpp's own
// moduleProcess()). Always jack-bearing, full Touch, same as XO8.
#define XR_CAPACITY 8

#include "OrangeLine.hpp"
#include "XOShared.hpp"

// { bipolar, voltage } per right-click range choice - index 0 (0-10V) is the default for a
// fresh instance. Labels use Rack's own standard range notation (0-Nv / +-Nv), not a Uni/Bi
// abbreviation. File scope (not inside the module struct) so both XRModuleCommon.hpp's own
// moduleProcess() and the widget/menu code in XR8.cpp can see it.
struct XRRangeOption { bool bipolar; float voltage; const char *label; };
static const XRRangeOption XR_RANGE_OPTIONS[8] = {
	{ false, 10.f, "0-10V" }, { false, 5.f, "0-5V" }, { false, 2.f, "0-2V" }, { false, 1.f, "0-1V" },
	{ true,  10.f, "±10V" }, { true, 5.f, "±5V" }, { true, 2.f, "±2V" }, { true, 1.f, "±1V" },
};

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	STYLE_JSON,
	BROWSE_INDEX_JSON,   // persists the last browsed output slot - see XRModuleCommon.hpp's own
	                      // moduleProcess()
	// The non-adjacent stay-connected target host id used to live here as a float - moved to a
	// real int64_t member (xoConnectedHostId, XRModuleCommon.hpp), persisted via this module's
	// own moduleExtraDataToJson/FromJson instead (observed Rack module ids run into the
	// quadrillions - a float silently corrupts them, see XOShared.hpp's resolveXOHostBridge()).
	CHANNEL_RANGE_JSON,   // per-channel output range choice (index into XR_RANGE_OPTIONS)
	CHANNEL_RANGE_JSON_LAST = CHANNEL_RANGE_JSON + XR_CAPACITY - 1,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	LEFT_PARAM,
	RIGHT_PARAM,

	NUM_PARAMS
};

//
// Input Ids - XR8 has no real CV inputs at all
//
enum InputIds {
	NUM_INPUTS
};

//
// Output Ids - one mono jack per channel, the seeded-random value for whichever output slot is browsed
//
enum OutputIds {
	CHANNEL_OUTPUT,
	CHANNEL_OUTPUT_LAST = CHANNEL_OUTPUT + XR_CAPACITY - 1,

	NUM_OUTPUTS
};

//
// Light Ids
//
enum LightIds {
	OVERFLOW_LIGHT, // lit whenever the browsed output has more real channels than this module's
	                // own fixed capacity

	NUM_LIGHTS
};
