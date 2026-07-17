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

// { bipolar, voltage } per right-click range choice - index 0 (Uni 10V) is the default for a
// fresh instance. Order: Uni10/Uni5/Uni2/Uni1/Bi10/Bi5/Bi2/Bi1. File scope (not inside the module
// struct) so both XRModuleCommon.hpp's own moduleProcess() and the widget/menu code in XR8.cpp
// can see it.
struct XRRangeOption { bool bipolar; float voltage; const char *label; };
static const XRRangeOption XR_RANGE_OPTIONS[8] = {
	{ false, 10.f, "Uni 10V" }, { false, 5.f, "Uni 5V" }, { false, 2.f, "Uni 2V" }, { false, 1.f, "Uni 1V" },
	{ true,  10.f, "Bi 10V"  }, { true,  5.f, "Bi 5V"  }, { true,  2.f, "Bi 2V"  }, { true,  1.f, "Bi 1V"  },
};

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	STYLE_JSON,
	BROWSE_INDEX_JSON,   // persists the last browsed output slot - see XRModuleCommon.hpp's own
	                      // moduleProcess()
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
	CONN_LIGHT,
	CONN_LIGHT_LAST = CONN_LIGHT + 1, // GreenRedLight needs 2 consecutive ids (green, red)
	OVERFLOW_LIGHT,                   // lit whenever the browsed output has more real channels
	                                  // than this module's own fixed capacity

	NUM_LIGHTS
};
