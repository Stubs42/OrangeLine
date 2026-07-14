/*
	Resc.hpp
 	
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
// Resc is one of the slim panels - no room in the header for Touch In's usual top-left spot,
// so it goes to the bottom instead (same row as Touch Out, mirrored to the left side).
#define OL_TOUCH_IN_Y_MM 125.26f

#include "OrangeLine.hpp"

#define CHG_IN     1
#define CHG_SRCSCL 2
#define CHG_TRGSCL 4

#define CHILD_CV_IN_SCALE   0.f
#define CHILD_CV_RELATIVE   1.f

#define RELATIVE_FLATS_OFF	0.f
#define RELATIVE_FLATS_ON	1.f

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
    CHILD_CV_MODE_JSON,
	RELATIVE_FLATS_JSON,

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
	IN_INPUT,
	SRCSCL_INPUT,
	TRGSCL_INPUT,
	TRGCLD_INPUT,

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
    ROOTBASED_OUTPUT,
    CLDBASED_OUTPUT,
    CLDSCL_OUTPUT,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	NUM_LIGHTS
};
