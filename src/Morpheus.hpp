/*
	Morpheus.hpp
 	
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
#include "XShared.hpp"
#include "XOShared.hpp"

// Real panel width (confirmed directly from res/MorpheusOrange.svg's own width="50.799999mm") -
// needed for the new XO-family seam-bridging strip on Morpheus's own right edge (see
// addXOExtStrip() in MorpheusWidget's constructor).
#define MORPHEUS_PANEL_WIDTH_MM 50.8f

#define MAX_LOOP_LEN    128
#define MEM_SLOTS        16
#define ON_RANDOMIZE    0
#define ON_OUTPUT       1

// Per-channel step-exchange event, set for one tick by moduleProcess() at the LOCK/S<>R
// decision point, consumed (and reset to EVENT_NONE) by the widget as soon as it notices it.
#define EVENT_NONE          0
#define EVENT_SOURCE        1
#define EVENT_SOURCE_EQUAL  2  
#define EVENT_RANDOM        3

// Visualization display (MorpheusDisplayWidget) layout/colors

#define DISPLAY_PAGE_SIZE         48
#define DISPLAY_FLASH_FADE_TIME   0.5f

#define DISPLAY_BG_COLOR          nvgRGB(  0,   0,   0)
#define DISPLAY_END_MARKER_COLOR  nvgRGB( 96,  96,  96)
#define DISPLAY_POS_COLOR         nvgRGB( 96,  96,  96)

// Step value color gradient: -10V = light red, +10V = light green, linearly interpolated
// (so 0V lands on the natural red+green mix, a yellow/olive tone - no separate color needed).
// Used when the step's current value still matches its source (MEM slot, or EXT_INPUT if
// EXT_ON) - i.e. it hasn't drifted/been randomized away from it.
#define DISPLAY_BIPOLAR_LOW_COLOR       nvgRGB(164,  32, 32)
#define DISPLAY_BIPOLAR_HIGH_COLOR      nvgRGB( 32, 164, 32)
#define DISPLAY_MATCH_LOW_COLOR         nvgRGB(  4,  16,  4)
#define DISPLAY_MATCH_HIGH_COLOR        nvgRGB( 64, 127, 64)
#define DISPLAY_DIRTY_LOW_COLOR         nvgRGB( 16,   4,  4)
#define DISPLAY_DIRTY_HIGH_COLOR        nvgRGB(127,  64, 64)

// Pos-cursor flash on a step-exchange event - color depends on EVENT_SOURCE/EVENT_RANDOM,
// fading back to DISPLAY_POS_COLOR over DISPLAY_FLASH_FADE_TIME seconds (widget-managed,
// time-based via glfwGetTime() so it's independent of GUI frame rate).
#define DISPLAY_FLASH_SOURCE_COLOR          nvgRGB( 40, 255,  40)
#define DISPLAY_FLASH_SOURCE_EQUAL_COLOR    nvgRGB( 48, 196,  48)
#define DISPLAY_FLASH_RANDOM_COLOR          nvgRGB(255,  40,  40)

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
    POLY_CHANNELS_JSON,
	STEPS_JSON,
	STEPS_JSON_LAST = STEPS_JSON + POLY_CHANNELS * MAX_LOOP_LEN - 1,
	MEM_JSON,
	MEM_JSON_LAST = MEM_JSON + MEM_SLOTS * POLY_CHANNELS * MAX_LOOP_LEN - 1,
    HEAD_JSON,
	HEAD_JSON_LAST = HEAD_JSON + POLY_CHANNELS - 1,
    ACTIVE_MEM_JSON,
    SELECTED_MEM_JSON,
    EXT_ON_JSON,
    HLD_ON_JSON,
    GATE_IS_TRG_JSON,
    LOAD_ON_MEM_CV_CHANGE_JSON,
    RECALL_ON_MEM_CV_CHANGE_JSON,
    SMART_HOLD_JSON,
    MEM_IS_HALFTONES_JSON,
    LOAD_HLD_CHANNELS_JSON,
    SCALE_MODE_JSON,
    VISUAL_ON_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	//
	// Paramater for user interface components
	//
    LOCK_PARAM,
    BALANCE_PARAM,
    LOOP_LEN_PARAM,
    MEM_UP_PARAM,
    MEM_DOWN_PARAM,
    STO_PARAM,
    RCL_PARAM,
    HLD_ON_PARAM,
    RND_PARAM,
    SHIFT_LEFT_PARAM,
    SHIFT_RIGHT_PARAM,
    CLR_PARAM,
    EXT_ON_PARAM,
    REC_PARAM,
    GTP_PARAM,
    SCL_PARAM,
    OFS_PARAM,

    NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
    LOCK_INPUT,
    BALANCE_INPUT,
    LOOP_LEN_INPUT,
    HLD_INPUT,
    RND_INPUT,
    SHIFT_LEFT_INPUT,
    SHIFT_RIGHT_INPUT,
    CLR_INPUT,
    EXT_INPUT,
    REC_INPUT,
    GTP_INPUT,
    SCL_INPUT,
    OFS_INPUT,
    MEM_INPUT,
    STO_INPUT,
    RCL_INPUT,
    RST_INPUT,
    CLK_INPUT,

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
    SRC_OUTPUT,
    GATE_OUTPUT,
    CV_OUTPUT,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	HLD_ON_LIGHT,
	EXT_ON_LIGHT,
	X_CONN_LIGHT,
	XO_CONN_LIGHT, // mirrors X_CONN_LIGHT's own single-color convention, but for the new
	               // XO-family attaching to this Host's RIGHT side instead

	NUM_LIGHTS
};
