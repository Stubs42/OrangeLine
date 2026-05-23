/*
	Hold.hpp
 	
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

#define NUM_ROWS 10

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
	TRK_ON_JSON,
	STORE_JSON,
	STORE_JSON_LAST = STORE_JSON + NUM_ROWS * POLY_CHANNELS - 1, 

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	//
	// Paramater for user interface components
	//
    TRK_ON_PARAM,

    NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
	GATE_INPUT,
    CV_INPUT,
	CV_INPUT_LAST = CV_INPUT + NUM_ROWS - 1,

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
	CV_OUTPUT,
    CV_OUTPUT_LAST = CV_OUTPUT + NUM_ROWS - 1,
 
	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	TRK_ON_LIGHT,

	NUM_LIGHTS
};
