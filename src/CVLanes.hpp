/*
	CVLanes.hpp

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
// NUM_SOURCES/NUM_LANES and the LanesHubInterface/LanesExpanderInterface pull-API shared
// with the LanesCV/LanesMidi expanders live in LanesShared.hpp (kept separate from this
// file's own module-specific enums below, since LanesCV.hpp/LanesMidi.hpp include
// LanesShared.hpp too and must not pull in Lanes' own jsonIds/ParamIds/etc a second time).
#include "LanesShared.hpp"

//
// Defaults
//

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	//
	// Parameters not bound to any user interface component to save internal module state
	//
    STYLE_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	//
	// Paramater for user interface components
	//
    NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
	VOCT_IN_INPUT,
    VOCT_IN_INPUT_LAST = VOCT_IN_INPUT + NUM_SOURCES - 1,
	GATE_IN_INPUT,
    GATE_IN_INPUT_LAST = GATE_IN_INPUT + NUM_SOURCES - 1,
	VEL_IN_INPUT,
    VEL_IN_INPUT_LAST = VEL_IN_INPUT + NUM_SOURCES - 1,
	LANE_IN_INPUT,
    LANE_IN_INPUT_LAST = LANE_IN_INPUT + NUM_SOURCES - 1,

	NUM_INPUTS
};

//
// Output Ids
//
// Hub has no CV outputs of its own anymore - the 16-lane output distributor moved to
// LanesCV (an expander). See LanesShared.hpp's LanesHubInterface for how expanders read
// the Hub's per-lane state instead.
enum OutputIds {
	NUM_OUTPUTS
};

//
// Ligh Ids
//
/*
	Tiny bi-color (GreenRedLight, 2 consecutive slots: green then red) corner lights, one per
	side - see LanesShared.hpp's classifyLanesNeighborForHub(). Unlike an Expander's
	chain-health light, the Hub never shows yellow (it's complete by itself, it doesn't need
	to "find" anything) - only:
		off   - nothing (or nothing recognized) connected on this side
		green - a normal Expander (LanesCV, LanesMidi, ...) unambiguously serving this Hub
		red   - another Hub directly adjacent, or an Expander that's ambiguous or actually
		        serving a *different* Hub reachable through its far side (e.g. this Hub sits
		        on one side of an Expander that's also connected to another Hub on its other
		        side - both Hubs show red on the Expander-facing side, not just the Expander)
	Since there's only one Hub per chain, expanders may sit on either side of it.
*/
enum LightIds {
	LEFT_CONN_LIGHT,
	LEFT_CONN_LIGHT_LAST = LEFT_CONN_LIGHT + 1,
	RIGHT_CONN_LIGHT,
	RIGHT_CONN_LIGHT_LAST = RIGHT_CONN_LIGHT + 1,

	NUM_LIGHTS
};
