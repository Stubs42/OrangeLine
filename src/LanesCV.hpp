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
	// The touch-once-then-persist target Hub id used to live here as a float - moved to a real
	// int64_t member (lanesConnectedHostId, this module's own .cpp), persisted via
	// moduleExtraDataToJson/FromJson instead (observed Rack module ids run into the quadrillions -
	// a float silently corrupts them, see LanesShared.hpp's resolveLanesHubBridge()).

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
// The connection lights are gone (superseded by the seam/logo-cover mechanism, which now derives
// directly from the bridge host id - see ExpanderBridge.hpp).
enum LightIds {
	OVERFLOW_LIGHT,
    OVERFLOW_LIGHT_LAST = OVERFLOW_LIGHT + NUM_LANES - 1,

	NUM_LIGHTS
};
