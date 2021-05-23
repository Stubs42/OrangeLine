/*
	Dejavu.hpp
 	
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

#define NUM_LENGTHS 4
#define L_DIGITS    5

#define HEAT_LOW   0.f
#define HEAT_HIGH 10.f

#define GATE_COLOR_ON  0xffff00
#define GATE_COLOR_OFF 0x000000
#define SH_COLOR_ON    0xffff00
#define SH_COLOR_OFF   0x000000

#define GATE_MODE 1.f
#define TRIGGER_MODE 0.f

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	//
	// Parameters not bound to any user interface component to save internal module state
	//
	STYLE_JSON,
	RESET_JSON,
	COUNTER_JSON,
	COUNTER_END_JSON = COUNTER_JSON + NUM_LENGTHS - 1,
	GATE_JSON,
	SH_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	//
	// Paramater for user interface components
	//
	DIV_PARAM,
	SEED_PARAM,
	L_PARAM,
	L_END_PARAM = L_PARAM + NUM_LENGTHS - 1,
 	HEAT_PARAM,
	HEAT_ATT_PARAM,
	SH_PARAM,
	GATE_PARAM,
	OFS_PARAM,
	OFS_ATT_PARAM,
	SCL_PARAM,
	SCL_ATT_PARAM,
	CHN_PARAM,
	
	NUM_PARAMS,
};

//
// Input Ids
//
enum InputIds {
	RST_INPUT,
	CLK_INPUT,
	DIV_INPUT,
	SEED_INPUT,
	L_INPUT,
	L_END_INPUT = L_INPUT + NUM_LENGTHS,
	TL_INPUT,
	TL_END_INPUT = TL_INPUT + NUM_LENGTHS - 1,
	HEAT_INPUT,
	OFS_INPUT,
	SCL_INPUT,

	NUM_INPUTS}
;

//
// Output Ids
//
enum OutputIds {
	TL_OUTPUT,
	TL_END_OUTPUT = TL_OUTPUT + NUM_LENGTHS - 1,
	GATE_OUTPUT,
	CV_OUTPUT,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	SH_LIGHT_RGB,
	SH_LIGHT__GB,
	SH_LIGHT___B,
	GATE_LIGHT_RGB,
	GATE_LIGHT__GB,
	GATE_LIGHT___B,

	NUM_LIGHTS
};
