/*
	LanesMidi.hpp

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

// See LanesCV.hpp for why this includes LanesShared.hpp (not Lanes.hpp) - NUM_LANES and
// the LanesHubInterface/LanesExpanderInterface pull-API shared across the whole
// Lanes/LanesCV/LanesMidi family, without colliding with Lanes' own module-specific enums.
#include "LanesShared.hpp"

//
// Defaults
//

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
    STYLE_JSON,
	// The touch-once-then-persist target Hub id used to live here as a float - moved to a real
	// int64_t member (lanesConnectedHostId, this module's own .cpp), persisted via
	// moduleExtraDataToJson/FromJson instead (observed Rack module ids run into the quadrillions -
	// a float silently corrupts them, see LanesShared.hpp's resolveLanesHubBridge()).

	NUM_JSONS
};

//
// Parameter Ids
//
// One selector per MIDI channel (index 0-15 = MIDI channel 1-16), each freely choosing
// which lane (0 = off, 1-16) feeds that channel. This is deliberately channel-indexed, not
// lane-indexed: a channel can only ever have one source lane at a time (a plain single
// selector), so two different lanes can never collide over the same channel - there is no
// arbitration/contention to resolve, unlike the lane-indexed alternative this replaced.
enum ParamIds {
	LANE_PARAM,
    LANE_PARAM_LAST = LANE_PARAM + NUM_LANES - 1,

	NUM_PARAMS
};

//
// Input Ids
//
// LanesMidi has no CV inputs of its own - it's a pure right-side expander of LANES,
// reading the Hub's per-lane state via LanesHubInterface instead of jacks.
enum InputIds {
	NUM_INPUTS
};

//
// Output Ids
//
// No CV outputs either - output is exclusively the MIDI device (midiOutput member).
enum OutputIds {
	NUM_OUTPUTS
};

//
// Ligh Ids - the connection lights are gone (superseded by the seam/logo-cover mechanism, which
// now derives directly from the bridge host id - see ExpanderBridge.hpp). Each channel's own
// lane-number display still doubles as the overflow indicator (color-coded, see LanesMidi.cpp's
// LaneDisplayWidget).
//
enum LightIds {
	NUM_LIGHTS
};
