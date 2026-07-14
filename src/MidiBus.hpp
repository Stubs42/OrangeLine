/*
	MidiBus.hpp

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
// Touch doesn't help a real MIDI-hardware module - see CC2CV.hpp / CLAUDE.md. (Briefly tried
// keeping Touch here for an auto-measured sDLY round-trip idea; reverted - KISS, unreliable
// and no panel room for the extra jack anyway.)
#define OL_TOUCH_DISABLED

#include "OrangeLine.hpp"

#define NUM_CC_BANKS 8

/*
	MIDIBUS: a single-channel MIDI processor insert. Bus-stop metaphor (Dieter): if nothing
	is patched into a signal's TX infix, it just drives straight through; to intervene, you
	build an infix - patch something into the TX side and it takes over from there.

	Every signal has an RX *output* (a read tap of what was just received) and a TX *input*
	(what actually gets sent - falls back to the RX value if left unpatched). See MidiBus.cpp
	for the exact fallback logic per signal.
*/

//
// Defaults
//

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
    STYLE_JSON,
    FLUSH_ON_START_JSON,
    SYSEX_PASSTHROUGH_JSON,
    CLOCK_PASSTHROUGH_JSON,
    TRANSPORT_PASSTHROUGH_JSON,
    PB_BIPOLAR_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
    RX_CHANNEL_PARAM,
    TX_CHANNEL_PARAM,
    GRID_LOCK_PARAM,
    FLUSH_PARAM,
    SDLY_PARAM,

	NUM_PARAMS
};

//
// Input Ids
//
enum InputIds {
    RX_CHANNEL_CV_INPUT,
    TX_CHANNEL_CV_INPUT,
    TX_VOCT_INPUT,
    TX_GATE_INPUT,
    TX_VEL_INPUT,
    TX_AT_INPUT,
    TX_PAT_INPUT,
    TX_PB_INPUT,
    TX_CC_INPUT,
    TX_CC_INPUT_LAST = TX_CC_INPUT + NUM_CC_BANKS - 1,
    TX_BANK_INPUT,
    TX_PROGRAM_INPUT,
    TX_PC_TRIGGER_INPUT,
    TX_CLOCK_INPUT,
    TX_START_INPUT,
    TX_STOP_INPUT,
    TX_CONTINUE_INPUT,
    FORCE_INPUT,

	NUM_INPUTS
};

//
// Output Ids
//
enum OutputIds {
    RX_VOCT_OUTPUT,
    RX_GATE_OUTPUT,
    RX_VEL_OUTPUT,
    RX_AT_OUTPUT,
    RX_PAT_OUTPUT,
    RX_PB_OUTPUT,
    RX_CC_OUTPUT,
    RX_CC_OUTPUT_LAST = RX_CC_OUTPUT + NUM_CC_BANKS - 1,
    RX_BANK_OUTPUT,
    RX_PROGRAM_OUTPUT,
    RX_PC_TRIGGER_OUTPUT,
    RX_CLOCK_OUTPUT,
    RX_START_OUTPUT,
    RX_STOP_OUTPUT,
    RX_CONTINUE_OUTPUT,

	NUM_OUTPUTS
};

//
// Ligh Ids
//
enum LightIds {
	GRID_LOCK_LIGHT,

	NUM_LIGHTS
};
