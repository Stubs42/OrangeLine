// Touch doesn't help a real MIDI-hardware module - see CC2CV.hpp / CLAUDE.md.
#define OL_TOUCH_DISABLED

#include "OrangeLine.hpp"

enum jsonIds
{
	STYLE_JSON,
	AUTOSYNC_JSON,
	NUM_JSONS
};

enum ParamIds
{
	SYNC_PARAM,
	RX_GRID_LOCK_PARAM,
	TX_GRID_LOCK_PARAM,
	NUM_PARAMS
};

enum InputIds
{
	RX_INPUT,
	RX_INPUT_LAST = RX_INPUT + 8 - 1,
	TX_INPUT,
	TX_INPUT_LAST = TX_INPUT + 8 - 1,
	GATE_INPUT,
	NUM_INPUTS
};

enum OutputIds
{
	RX_OUTPUT,
	RX_OUTPUT_LAST = RX_OUTPUT + 8 - 1,
	TX_OUTPUT,
	TX_OUTPUT_LAST = TX_OUTPUT + 8 - 1,
	NUM_OUTPUTS
};

enum LightIds
{
	RX_GRID_LOCK_LIGHT,
	TX_GRID_LOCK_LIGHT,
	NUM_LIGHTS
};
