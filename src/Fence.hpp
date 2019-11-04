/*
	Fence.hpp
 	
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

#define AUDIO_VOLTAGE  5.f

#define MODE_MIN        0.f
#define MODE_RAW        0.f
#define MODE_RAW_INT    0
#define MODE_QTZ        1.f
#define MODE_QTZ_INT    1
#define MODE_SHPR       2.f
#define MODE_SHPR_INT   2
#define MODE_MAX        2.f

#define LINK_NONE       0.f
#define LINK_NONE_INT   0
#define LINK_RANGE      1.f
#define LINK_RANGE_INT  1
#define LINK_CENTER     2.f
#define LINK_CENTER_INT 2

#define LINK_COLOR_NONE   0x000000
#define LINK_COLOR_RANGE  0x00ff00
#define LINK_COLOR_CENTER 0x0000ff

#define MODE_COLOR_RAW    0x000000
#define MODE_COLOR_QTZ    0x00ff00
#define MODE_COLOR_SHPR   0xff0000

//
// Value Ranges
//
#define SEMITONE       (1.f / 12.f)

#define LOW_MIN_RAW   -10.f
#define LOW_MAX_RAW    10.f
#define HIGH_MIN_RAW  -10.f
#define HIGH_MAX_RAW   10.f
#define STEP_MIN_RAW    0.0001f
#define STEP_MAX_RAW   10.f
#define MIN_RANGE_RAW   0.1f

#define LOW_MIN_QTZ   -10.f
#define LOW_MAX_QTZ    10.f 
#define HIGH_MIN_QTZ  -10.f 
#define HIGH_MAX_QTZ   10.f
#define STEP_MIN_QTZ    0.f
#define STEP_MAX_QTZ   (SEMITONE * 11.f)
#define MIN_RANGE_QTZ   0.f

#define LOW_MIN_SHPR  -AUDIO_VOLTAGE
#define LOW_MAX_SHPR   AUDIO_VOLTAGE
#define HIGH_MIN_SHPR -AUDIO_VOLTAGE
#define HIGH_MAX_SHPR  AUDIO_VOLTAGE
#define STEP_MIN_SHPR   0.0001f
#define STEP_MAX_SHPR  10.f
#define MIN_RANGE_SHPR  0.1f

#define PRECISION       0.0000000001f
#define KNOB_FAKE_DELTA 0.0001f
#define KNOB_FAKE_STEPS 5000

//
//	Dynamic Labels
//
#define LABEL_LOW_LINK_NONE    "Low"
#define LABEL_HIGH_LINK_NONE   "High"
#define LABEL_STEP_LINK_NONE   "Step"
#define LABEL_LOW_LINK_RANGE   "Low"
#define LABEL_HIGH_LINK_RANGE  "High"
#define LABEL_STEP_LINK_RANGE  "Step"
#define LABEL_LOW_LINK_CENTER  "Spread"
#define LABEL_HIGH_LINK_CENTER "Center"
#define LABEL_STEP_LINK_CENTER "Step"

//
// Defaults
//

//
// DEFAULT_QTZ and DEFAULT_SHPR are mutally exclusive !
// If DEFAULT_QTZ and/or DEFAULT_SHPR are changed, change initial param config in Fence.cpp also!
//
#define DEFAULT_MODE      MODE_QTZ
#define DEFAULT_QTZ       (DEFAULT_MODE == MODE_QTZ)
#define DEFAULT_SHPR      (DEFAULT_MODE == MODE_SHPR)

#define DEFAULT_LOW_RAW   -10.f
#define DEFAULT_HIGH_RAW   10.f
#define DEFAULT_LINK_RAW    0.f
#define DEFAULT_STEP_RAW  STEP_MIN_RAW

// qtz range defaults to [C4, B4]
#define DEFAULT_LOW_QTZ     0.f
#define DEFAULT_HIGH_QTZ   (11.f / 12.f)
#define DEFAULT_LINK_QTZ    1.f
#define DEFAULT_STEP_QTZ    STEP_MIN_QTZ

#define DEFAULT_LOW_SHPR  -AUDIO_VOLTAGE
#define DEFAULT_HIGH_SHPR  AUDIO_VOLTAGE
#define DEFAULT_LINK_SHPR  0.f
#define DEFAULT_STEP_SHPR  0.f

// VOctWidget Types
#define TYPE_VOCT 1
#define TYPE_STEP 2

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	//
	// Parameters not bound to any user interface component to save internal module state
	//
	MODE_JSON,			//	Mode qtz/shpr/raw
	
	LOW_RAW_JSON,		// 	knob values for min/max/step and link setting for raw mode (quantize and shpr off)
	HIGH_RAW_JSON,
	LINK_RAW_JSON,
	STEP_RAW_JSON,
		
	LOW_QTZ_JSON,		// knob values for min/max/step and link setting for qtz mode (quantize on and shpr off)
	HIGH_QTZ_JSON,
	LINK_QTZ_JSON,
	STEP_QTZ_JSON,

	LOW_SHPR_JSON,		// knob values for min/max/step and link setting for shpr mode (quantize off and shpr on)
	HIGH_SHPR_JSON,
	LINK_SHPR_JSON,
	STEP_SHPR_JSON,

	LOWCLAMPED_JSON,
	HIGHCLAMPED_JSON,

	LINK_JSON,
	LINK_DELTA_JSON,

	GATE_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	//
	// Paramater for user interface components
	//
	LOW_PARAM,				// Range Low Knob
	HIGH_PARAM,				// Range High Knob
	LINK_PARAM,				// Range Link Button
	MODE_PARAM,				// Quantize Button
	GATE_PARAM,				// Shpr Button (Audio Mode)
	STEP_PARAM,				// Step Knob
		
	NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
	LOW_INPUT,				// Range Low CV Input
	HIGH_INPUT,				// Range High CV Input
	STEP_INPUT,				// Step CV Input
	TRG_INPUT,				// Trigger Input
	CV_INPUT,				// Cv In to process

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
	TRG_OUTPUT,				// Trigger output for signaling Cv Out changes
	CV_OUTPUT,				// Cv Out

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	MODE_LIGHT_RGB,			// Light indicating quantized mode
	MODE_LIGHT__G_,
	MODE_LIGHT___B,
	GATE_LIGHT,				// Light indicating trigger or gate mode
	LINK_LIGHT_RGB,			// Light indicating range min/max link mode
	LINK_LIGHT__G,
	LINK_LIGHT___B,

	NUM_LIGHTS
};
