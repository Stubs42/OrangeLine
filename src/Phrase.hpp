/*
	Phrase.hpp
 	
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

#define MIN_INC			-10.
#define MAX_INC			10.
#define DEFAULT_INC		0.31746
#define TROWFIX_PATTERN_OFFSET		0.31746 / 2.f

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	//
	// Parameters not bound to any user interface component to save internal module state
	//
	STYLE_JSON,
	RESET_JSON,
	PHRASELENCOUNTER_JSON,
	PHRASEDURCOUNTER_JSON,
	SLAVELENCOUNTER_JSON,
	SLAVEPATTERN_JSON,
	MASTERDELAYCOUNTER_JSON,
	TROWAFIX_JSON,
	DIVCOUNTER_JSON,
	CLOCKDELAYCOUNTER_JSON,
	CLOCKWITHRESET_JSON,
	CLOCKWITHSPA_JSON,
	CLOCKWITHSPH_JSON,

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
	DIV_PARAM,				// Clock Division
	MASTER_PTN_PARAM,		// Pattern Knob
	CLK_DLY_PARAM,			// Clock Delay for Slave Clock out
		
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
	DLEN_INPUT,				// Default length of phrase if MASTER_LEN_IMPUT is 0V if connected
							// If not connected, LEN_PARAM is used as default instead			

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
	SPH_OUTPUT,				// Start Phrase
	SPA_OUTPUT,				// Start Pattern
	ELEN_OUTPUT,			// Effective Length of Pattern from MASTER_LEN_INPUT if != 0V,
							// DEFAULT_LEN_INPUT if connected, LEN_PARAM otherwise

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {

	NUM_LIGHTS
};
