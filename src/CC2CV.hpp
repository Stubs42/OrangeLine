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
