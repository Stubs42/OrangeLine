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

#define REP_INPUT_SCALE   100
#define REP_INPUT_MAX   99999

// Seed CV 10V = 10.000
#define SEED_INPUT_SCALE 10000
#define DEFAULT_SEED	   66	// = 0x42 ;-)
#define SEED_MAX 		 99999
#define DIV_MAX 64

// Do not change if you do not have 4 rows of repition knobs
#define NUM_ROWS 4

#define HEAT_LOW   0.f
#define HEAT_HIGH 10.f

#define GATE_COLOR_OFF  0x000000
#define GATE_COLOR_ON   0xffff00
#define GATE_COLOR_CONT 0xff0000
#define SH_COLOR_OFF    0x000000
#define SH_COLOR_ON     0xffff00
#define SH_COLOR_CONT   0xff0000
#define ONOFF_COLOR_ON  0x00ff00
#define ONOFF_COLOR_OFF 0x000000
#define ONOFF_COLOR_ON_INACTIVE 0x001100

#define GATE_OFF  0
#define GATE_ON   1
#define GATE_CONT 2

#define SH_OFF  0
#define SH_ON   1
#define SH_CONT 2

// DO NOT CHANGE LEN or DUR because they are used in integer calculations !
#define LEN 0
#define DUR 1

#define STATE_ACTIVE             0
#define STATE_EDIT_RANGES        1
#define STATE_EDIT_OFFSETS  	 2

#define PARAM_DISPLAY_CYCLES    50

#define FLASH_FRAMES  5
#define NUM_FLASHES	  9

#define DIRECTION_BACKWARD  0.f
#define DIRECTION_CLOCKWISE 1.f

#define RANGE_INIT 	256
#define RANGE_MAX  	REP_INPUT_MAX
#define COUNTER_MAX REP_INPUT_MAX

#define WOBBLE_AMOUNT 0.000001

#define INIT_DISPLAY_ALPHA 50
#define DEFAULT_HEAT 50

#define GREETING_HEAD  "-- Orange Line --"
#define GREETING_VALUE " DEJAVU "
#define GREETING_CYCLES	200
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
	RANGE_JSON,
	RANGE_JSON_END = RANGE_JSON +  (NUM_ROWS * 2) - 1,
	ACTIVE_PARAM_JSON,
	ACTIVE_PARAM_JSON_END = ACTIVE_PARAM_JSON + (NUM_ROWS * 2) - 1,
	RESET_DUR_OFFSET_JSON,
	RESET_DUR_OFFSET_JSON_END = RESET_DUR_OFFSET_JSON + NUM_ROWS - 1,
	ACTIVE_HEAT_PARAM_JSON,
	GLOBAL_RANDOM_GETS_JSON,
	DISPLAY_ALPHA_JSON,
	LOOP_JSON,

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
