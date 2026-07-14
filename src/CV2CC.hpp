// Touch doesn't help a real MIDI-hardware module - see CC2CV.hpp / CLAUDE.md.
#define OL_TOUCH_DISABLED

#include "OrangeLine.hpp"

enum jsonIds
{
	STYLE_JSON,
	FLUSH_ON_START_JSON,
	NUM_JSONS
};

enum ParamIds
{
	FLUSH_PARAM,
	GRID_LOCK_PARAM,
	NUM_PARAMS
};

enum InputIds
{
	CC_INPUT,
	CC_INPUT_LAST = CC_INPUT + 8 - 1,
	FORCE_INPUT,
	NUM_INPUTS
};

enum OutputIds
{
	NUM_OUTPUTS
};

enum LightIds
{
	GRID_LOCK_LIGHT,
	NUM_LIGHTS
};
