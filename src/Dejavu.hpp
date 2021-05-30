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

// #define USE_DEBUG_OUTPUT

#define REP_INPUT_SCALE_COOKED   100
#define REP_INPUT_SCALE_RAW    10000
#define REP_INPUT_MAX          99999

// Seed CV 10V = 10.000
#define SEED_INPUT_SCALE 1000
#define SEED_MAX 9999
#define DIV_MAX 64

// Do not change if you do not have 4 rows of repition knobs
#define NUM_ROWS 4

#define HEAT_LOW   0.f
#define HEAT_HIGH 10.f

#define GATE_COLOR_ON   0xffff00
#define GATE_COLOR_OFF  0x000000
#define SH_COLOR_ON     0xffff00
#define SH_COLOR_OFF    0x000000
#define ONOFF_COLOR_ON  0x00ff00
#define ONOFF_COLOR_OFF 0x000000
#define ONOFF_COLOR_ON_INACTIVE 0x001100

#define GATE_MODE    1
#define TRIGGER_MODE 0

// DO NOT CHANGE LEN or DUR because they are used in integer calculations !
#define LEN 0
#define DUR 1

// DO NOT CHANGE RAW or COOKED because they are used in integer calculations !
#define COOKED 0
#define RAW 1

#define STATE_ACTIVE        0
#define STATE_EDIT_RANGES   1
#define STATE_EDIT_COUNTERS 2

#define PARAM_DISPLAY_CYCLES 30

#define FLASH_FRAMES  5
#define NUM_FLASHES	  9

#define DIRECTION_BACKWARD  0.f
#define DIRECTION_CLOCKWISE 1.f

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	//
	// Parameters not bound to any user interface component to save internal module state
	//
	STYLE_JSON,
	RESET_JSON,
	LEN_COUNTER_JSON,
	LEN_COUNTER_JSON_END = LEN_COUNTER_JSON +  NUM_ROWS - 1,
	ONOFF_JSON,
	ONOFF_JSON_END = ONOFF_JSON + NUM_ROWS - 1,
	DUR_COUNTER_JSON,
	DUR_COUNTER_JSON_END = DUR_COUNTER_JSON +  NUM_ROWS - 1,
	GATE_JSON,
	SH_JSON,
	DIVCOUNTER_JSON,
	POLY_CHANNELS_JSON,
	MODULE_STATE_JSON,
	DIRECTION_JSON,

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
	LEN_PARAM,
	LEN_PARAM_END = LEN_PARAM +  NUM_ROWS - 1,
	ONOFF_PARAM,
	ONOFF_PARAM_END = ONOFF_PARAM +  NUM_ROWS - 1,
	DUR_PARAM,
	DUR_PARAM_END = DUR_PARAM +  NUM_ROWS - 1,
 	HEAT_PARAM,
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
	REP_INPUT,
	TRG_INPUT,
	HEAT_INPUT,
	HEAT_KNOB_ATT_INPUT,
	OFS_INPUT,
	SCL_INPUT,
	GATE_INPUT,
	SH_INPUT,

	NUM_INPUTS
	};

//
// Output Ids
//
enum OutputIds {
	REP_OUTPUT,
	TRG_OUTPUT,
	GATE_OUTPUT,
	CV_OUTPUT,

#ifdef USE_DEBUG_OUTPUT
	DEBUG_OUTPUT,
#endif

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	REP_LIGHT_RGB,
	REP_LIGHT_END = REP_LIGHT_RGB + ( NUM_ROWS * 3) - 1,
	SH_LIGHT_RGB,
	SH_LIGHT__GB,
	SH_LIGHT___B,
	GATE_LIGHT_RGB,
	GATE_LIGHT__GB,
	GATE_LIGHT___B,

	NUM_LIGHTS
};
