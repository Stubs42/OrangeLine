/*
	Lanes.hpp

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
#include "OrangeLine.hpp"

#define NUM_SOURCES 16
#define NUM_LANES   16

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
enum OutputIds {
	VOCT_OUT_OUTPUT,
    VOCT_OUT_OUTPUT_LAST = VOCT_OUT_OUTPUT + NUM_LANES - 1,
	GATE_OUT_OUTPUT,
    GATE_OUT_OUTPUT_LAST = GATE_OUT_OUTPUT + NUM_LANES - 1,
	VEL_OUT_OUTPUT,
    VEL_OUT_OUTPUT_LAST = VEL_OUT_OUTPUT + NUM_LANES - 1,
	OVERFLOW_OUTPUT,
    OVERFLOW_OUTPUT_LAST = OVERFLOW_OUTPUT + NUM_LANES - 1,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {

	NUM_LIGHTS
};
