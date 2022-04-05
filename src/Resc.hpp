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
#include "OrangeLine.hpp"

#define NUM_POLYS	1   // 0 produces lots of compiler warnings

#define CHG_IN     1
#define CHG_SRCSCL 2
#define CHG_TRGSCL 4

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
