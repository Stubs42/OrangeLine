/*
	Morpheus.hpp
 	
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

#define MAX_LOOP_LEN    128
#define MEM_SLOTS        16

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
	STEPS_JSON_LAST = STEPS_JSON + POLY_CHANNELS * MAX_LOOP_LEN - 1,
	MEM_JSON,
	MEM_JSON_LAST = MEM_JSON + MEM_SLOTS * POLY_CHANNELS * MAX_LOOP_LEN - 1,
    HEAD_JSON,
	HEAD_JSON_LAST = HEAD_JSON + POLY_CHANNELS - 1,
    ACTIVE_MEM_JSON,
    SELECTED_MEM_JSON,
    EXT_ON_JSON,
    HLD_ON_JSON,
    GATE_IS_TRG_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	//
	// Paramater for user interface components
	//
    LOCK_PARAM,
    BALANCE_PARAM,
    LOOP_LEN_PARAM,
    MEM_UP_PARAM,
    MEM_DOWN_PARAM,
    STO_PARAM,
    RCL_PARAM,
    HLD_ON_PARAM,
    RND_PARAM,
    SHIFT_LEFT_PARAM,
    SHIFT_RIGHT_PARAM,
    CLR_PARAM,
    EXT_ON_PARAM,
    REC_PARAM,
    GTP_PARAM,
    SCL_PARAM,
    OFS_PARAM,

    NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
    LOCK_INPUT,
    BALANCE_INPUT,
    LOOP_LEN_INPUT,
    HLD_INPUT,
    RND_INPUT,
    SHIFT_LEFT_INPUT,
    SHIFT_RIGHT_INPUT,
    CLR_INPUT,
    EXT_INPUT,
    REC_INPUT,
    GTP_INPUT,
    SCL_INPUT,
    OFS_INPUT,
    MEM_INPUT,
    STO_INPUT,
    RCL_INPUT,
    RST_INPUT,
    CLK_INPUT,

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
    SRC_OUTPUT,
    GATE_OUTPUT,
    CV_OUTPUT,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	HLD_ON_LIGHT,
	EXT_ON_LIGHT,

	NUM_LIGHTS
};
