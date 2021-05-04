/*
	Phraseq.hpp
 	
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

#define CHG_RST_IN		1
#define CHG_CLK_IN		(1 << 1)

#define MIN_INC			0.0001
#define DEFAULT_INC		0.31746

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
	DLY_PARAM,				// Master Sequencer Delay Knob
	LEN_PARAM,				// Slave Sequencer Length Knob
	INC_PARAM,				// Slave Sequencer Next Pattern Increment Knob
		
	NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
	RST_INPUT,				// Main Reset
	CLK_INPUT,				// Main Clock
	PTN_INPUT,				// Main Pattern CV
	MASTER_PTN_INPUT,		// Master Pattern Input
    MASTER_LEN_INPUT,       // Master Pattern Length CV	
    MASTER_DUR_INPUT,       // Master Phrase Duration CV	

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
	MASTER_RST_OUTPUT,		// Master Reset
	MASTER_CLK_OUTPUT,		// Master Clock
	MASTER_PTN_OUTPUT,		// Master Pattern CV
	SLAVE_RST_OUTPUT,		// Slave Reset
	SLAVE_CLK_OUTPUT,		// Slave Clock
	SLAVE_PTN_OUTPUT,		// Slave Pattern CV

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {

	NUM_LIGHTS
};
