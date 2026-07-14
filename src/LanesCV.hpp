/*
	LanesCV.hpp

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
// Includes LanesShared.hpp (not Lanes.hpp, which also defines Lanes' own module-specific
// enums) so NUM_LANES and the LanesHubInterface/LanesExpanderInterface used to pull state
// from the LANES hub stay a single shared definition across the whole Lanes/LanesCV/LanesMidi
// family instead of drifting copies.
#include "LanesShared.hpp"

//
// Defaults
//

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
    STYLE_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	NUM_PARAMS
};

//
// Input Ids
//
// LanesCV has no CV inputs of its own - it's a pure expander of the LANES Hub (either side,
// see LanesShared.hpp's resolveLanesHub()), reading the Hub's per-lane state via
// LanesHubInterface instead of jacks.
enum InputIds {
	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
	VOCT_OUTPUT,
    VOCT_OUTPUT_LAST = VOCT_OUTPUT + NUM_LANES - 1,
	GATE_OUTPUT,
    GATE_OUTPUT_LAST = GATE_OUTPUT + NUM_LANES - 1,
	VEL_OUTPUT,
    VEL_OUTPUT_LAST = VEL_OUTPUT + NUM_LANES - 1,
	OVERFLOW_OUTPUT,
    OVERFLOW_OUTPUT_LAST = OVERFLOW_OUTPUT + NUM_LANES - 1,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	OVERFLOW_LIGHT,
    OVERFLOW_LIGHT_LAST = OVERFLOW_LIGHT + NUM_LANES - 1,
	/*
		Tiny bi-color (GreenRedLight, 2 consecutive slots: green then red) corner lights,
		one per side. Per-side meaning (see LanesCV.cpp's moduleProcess()):
			off    - nothing connected on this side at all
			green  - connected, and exactly one Hub is reachable (this side or the other)
			yellow - connected, but no Hub is reachable through either side
			red    - connected, and a Hub is reachable through BOTH sides (ambiguous)
		The green/yellow/red judgement is the same for both lights (it reflects the whole
		module's chain health, not just this one side) - only the off/lit distinction is
		actually per-side.
	*/
	LEFT_CONN_LIGHT,
	LEFT_CONN_LIGHT_LAST = LEFT_CONN_LIGHT + 1,
	RIGHT_CONN_LIGHT,
	RIGHT_CONN_LIGHT_LAST = RIGHT_CONN_LIGHT + 1,

	NUM_LIGHTS
};
