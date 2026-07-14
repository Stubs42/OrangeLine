/*
	K2C.hpp

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
// K2C is one of the slim panels - no room in the header for Touch In's usual top-left spot,
// so it goes to the bottom instead (same row as Touch Out, mirrored to the left side).
#define OL_TOUCH_IN_Y_MM 125.26f

#include "OrangeLine.hpp"

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
	VOCT_INPUT,
	GATE_INPUT,
	VEL_INPUT,
	KEYS_INPUT,

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
	VOCT_OUTPUT,
	GATE_OUTPUT,
	VEL_OUTPUT,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {

	NUM_LIGHTS
};
