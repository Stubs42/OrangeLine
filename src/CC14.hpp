// CC14 is one of the slim panels - no room in the header for Wakeup's usual top-left spot,
// so it goes to the bottom instead (same row as Ready, mirrored to the left side).
#define OL_TOUCH_IN_Y_MM 125.26f

#include "OrangeLine.hpp"

enum jsonIds
{
	STYLE_JSON,
	NUM_JSONS
};

enum ParamIds
{
	NUM_PARAMS
};

enum InputIds
{
	MSB_INPUT,
	LSB_INPUT,
	CV_INPUT,
	NUM_INPUTS
};

enum OutputIds
{
	CV_OUTPUT,
	MSB_OUTPUT,
	LSB_OUTPUT,
	NUM_OUTPUTS
};

enum LightIds
{
	NUM_LIGHTS
};
