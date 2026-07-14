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
// Touch doesn't help a real MIDI-hardware module - see CC2CV.hpp / CLAUDE.md.
#define OL_TOUCH_DISABLED

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
// Each channel's own lane-number display is enough, no per-row light needed.
/*
	Tiny bi-color (GreenRedLight, 2 consecutive slots: green then red) corner lights, one per
	side - see LanesShared.hpp's classifyLanesNeighborForHub(). Unlike an Expander's
	chain-health light, the Hub never shows yellow (it's complete by itself, it doesn't need
	to "find" anything) - only:
		off   - nothing (or nothing recognized) connected on this side
		green - a normal Expander (LanesCV, LanesMidi, ...) unambiguously serving this Hub
		red   - another Hub directly adjacent, or an Expander that's ambiguous or actually
		        serving a *different* Hub reachable through its far side (e.g. this Hub sits
		        on one side of an Expander that's also connected to another Hub on its other
		        side - both Hubs show red on the Expander-facing side, not just the Expander)
*/
enum LightIds {
	LEFT_CONN_LIGHT,
	LEFT_CONN_LIGHT_LAST = LEFT_CONN_LIGHT + 1,
	RIGHT_CONN_LIGHT,
	RIGHT_CONN_LIGHT_LAST = RIGHT_CONN_LIGHT + 1,

	NUM_LIGHTS
};
