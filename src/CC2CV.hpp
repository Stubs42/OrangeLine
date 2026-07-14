// Touch doesn't help a real MIDI-hardware module - MIDI's own transport latency dwarfs the
// sub-millisecond timing Touch is built to fix, and it would just be one more never-used
// hidden jack cluttering an already-busy panel (see CLAUDE.md).
#define OL_TOUCH_DISABLED

#include "OrangeLine.hpp"

enum jsonIds
{
	STYLE_JSON,
	SMOOTH_JSON,
	NUM_JSONS
};

enum ParamIds
{
	GRID_LOCK_PARAM,
	NUM_PARAMS
};

enum InputIds
{
	NUM_INPUTS
};

enum OutputIds
{
	CC_OUTPUT,
	CC_OUTPUT_LAST = CC_OUTPUT + 8 - 1,
	NUM_OUTPUTS
};

enum LightIds
{
	GRID_LOCK_LIGHT,
	NUM_LIGHTS
};
