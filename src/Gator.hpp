/*
	Gator.hpp
 	
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

#define MIN_CMP      -9.5f
#define MAX_CMP       9.5f
#define MIN_LEN       0.01f

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
	LEN_PARAM,	// Gate LEN Knob
	JTR_PARAM,	// Jitter Knob
	RAT_PARAM,	// Ratchet Knob
	DLY_PARAM,	// Ratchet Delay Knob
	STR_PARAM,	// Strum Knob
		
	NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
	 PHS_INPUT,  // Phase Input (typically from Swing) [mono]
	 CMP_INPUT,  // Comapre Input (typically from Swing)) [mono]
	GATE_INPUT, // Gate Input [poly]
	TIME_INPUT, // Microtime Input	[poly]
	 LEN_INPUT,  // Gate LEN input [poly]
     JTR_INPUT,  // Jitter Input [poly]
     RAT_INPUT,  // Ratched Input [poly]
     DLY_INPUT,  // Ratchet Delay [poly]
     STR_INPUT,  // Strum Input [mono]
     RST_INPUT,  // Reset

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
	GATE_OUTPUT,    // Gate output [poly]

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {

	NUM_LIGHTS
};
