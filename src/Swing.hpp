/*
	Swing.hpp
 	
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

#define DEFAULT_LEN 16

#define PHASE_LOW   -10.f
#define PHASE_HIGH   10.f

#define MIN_CMP      -9.5f
#define MAX_CMP       9.5f

// will be param later on i think
#define CLOCK_MULT  getStateParam (DIV_PARAM)

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
	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	//
	// Paramater for user interface components
	//
    RST_PARAM,
    DIV_PARAM,
    LEN_PARAM,
    AMT_PARAM,
    TIM_PARAM_01,
    TIM_PARAM_02,
    TIM_PARAM_03,
    TIM_PARAM_04,
    TIM_PARAM_05,
    TIM_PARAM_06,
    TIM_PARAM_07,
    TIM_PARAM_08,
    TIM_PARAM_09,
    TIM_PARAM_10,
    TIM_PARAM_11,
    TIM_PARAM_12,
    TIM_PARAM_13,
    TIM_PARAM_14,
    TIM_PARAM_15,
    TIM_PARAM_16,
    NUM_PARAMS,
};

//
// Input Ids
//
enum InputIds {
	BPM_INPUT,				// BPM from Clock
	CLK_INPUT,				// Clock Input to sync
	RST_INPUT,				// Clock Reset
	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
    PHS_OUTPUT,
    ECLK_OUTPUT,
    CMP_OUTPUT,
    TCLK_OUTPUT,
	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	NUM_LIGHTS
};
