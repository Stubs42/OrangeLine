/*
	Buckets.hpp
 	
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

#define NUM_SPLITS NUM_NOTES

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
    SPLIT_PARAM,
    SPLIT_PARAM_LAST = SPLIT_PARAM + NUM_SPLITS - 1,

    NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
	VOCT_INPUT,
	VELOCITY_INPUT,
    GATE_INPUT,

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
	VOCT_OUTPUT,
    VOCT_OUTPUT_LAST = VOCT_OUTPUT + NUM_SPLITS,
	VELOCITY_OUTPUT,
    VELOCITY_OUTPUT_LAST = VELOCITY_OUTPUT + NUM_SPLITS,
    GATE_OUTPUT,
    GATE_OUTPUT_LAST = GATE_OUTPUT + NUM_SPLITS,
 
	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {

	NUM_LIGHTS
};
