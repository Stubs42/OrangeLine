/*
	Morph.hpp
 	
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

#define MAX_LOOP_LEN    64
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
    POLY_CHANNELS_JSON,
	STEPS_JSON,
	STEPS_JSON_LAST = STEPS_JSON + POLY_CHANNELS * MAX_LOOP_LEN * 2 - 1,
    HEAD_JSON,
	HEAD_JSON_LAST = HEAD_JSON + POLY_CHANNELS - 1,
	GATE_FROM_CV_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	//
	// Paramater for user interface components
	//
    LOCK_GATE_PARAM,
    LOCK_BOTH_PARAM,
    LOCK_CV_PARAM,

    SRC_RND_PARAM,
    SHIFT_LEFT_PARAM,
    SHIFT_RIGHT_PARAM,
    CLR_PARAM,

    LOOP_LEN_PARAM,
    RND_GATE_PARAM,
    RND_SCL_PARAM,
    RND_OFF_PARAM,

    NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
    CLK_INPUT,

    SRC_GATE_INPUT,
    SRC_CV_INPUT,
    SRC_FORCE_INPUT,

    LOCK_GATE_INPUT,
    LOCK_BOTH_INPUT,
    LOCK_CV_INPUT,

    SRC_RND_INPUT,
    SHIFT_LEFT_INPUT,
    SHIFT_RIGHT_INPUT,
    CLR_INPUT,

    LOOP_LEN_INPUT,
    RND_GATE_INPUT,
    RND_SCL_INPUT,
    RND_OFF_INPUT,

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
    GATE_OUTPUT,
    CV_OUTPUT,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {

	NUM_LIGHTS
};
