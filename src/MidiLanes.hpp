/*
	MidiLanes.hpp

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
// See LanesCV.hpp for why this includes LanesShared.hpp (not CVLanes.hpp) - NUM_LANES and
// the LanesHubInterface/LanesExpanderInterface pull-API shared across the whole LANES
// family, without colliding with any other family member's own module-specific enums.
#include "LanesShared.hpp"

//
// Defaults
//

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
    STYLE_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
// One selector per incoming MIDI channel (index 0-15 = MIDI channel 1-16), each freely
// choosing which lane (0 = off, 1-16) that channel's notes are merged into - the exact
// mirror of LanesMidi's LANE_PARAM, just for input instead of output. Channel-indexed for
// the same reason: a channel's notes always go to exactly one lane, no ambiguity.
enum ParamIds {
	LANE_PARAM,
    LANE_PARAM_LAST = LANE_PARAM + NUM_LANES - 1,

	NUM_PARAMS
};

//
// Input Ids
//
// No CV inputs - this Hub's input is exclusively the MIDI device (midiInput member).
enum InputIds {
	NUM_INPUTS
};

//
// Output Ids
//
// No CV outputs either - this Hub exposes its state only via LanesHubInterface, for
// LanesCV/LanesMidi (or any other expander) to read.
enum OutputIds {
	NUM_OUTPUTS
};

//
// Ligh Ids
//
// No lights - each channel's own lane-number display (mirrors LanesMidi's LaneDisplayWidget)
// is enough, no separate light needed.
enum LightIds {
	NUM_LIGHTS
};
