/*
	XO8.hpp

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
// Shared verbatim by XOD8.cpp too (identical enum shape - XOD8 just adds a display column on
// top in its own widget, same "X8D reuses X8.hpp" economy CLAUDE.md documents). Jack-bearing
// member of the XO family - relays a real Host signal into a re-emitted output jack at
// control-rate throttle, exactly the latency problem Wakeup/Ready exists for (unlike X8/X16,
// which have no real jacks at all) - gets full Touch, not disabled.
#define XO_CAPACITY 8
#define XO_HAS_JACKS

#include "OrangeLine.hpp"
#include "XOShared.hpp"

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	STYLE_JSON,
	BROWSE_INDEX_JSON, // persists the last browsed output slot - see XOModuleCommon.hpp's own
	                   // moduleProcess()
	// Persistent target Host module id for the non-adjacent stay-connected mechanism - mirrors
	// Styx.hpp's own CONNECTED_HOST_ID_JSON. -1 means "none, resolve via left-side physical
	// adjacency only". Unconditional/automatic, not a menu-driven Connect action like STYX's.
	CONNECTED_HOST_ID_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
enum ParamIds {
	LEFT_PARAM,
	RIGHT_PARAM,

	NUM_PARAMS
};

//
// Input Ids - XO8 has no real CV inputs at all
//
enum InputIds {
	NUM_INPUTS
};

//
// Output Ids - one mono jack per channel, the channel-split of whichever output slot is browsed
//
enum OutputIds {
	CHANNEL_OUTPUT,
	CHANNEL_OUTPUT_LAST = CHANNEL_OUTPUT + XO_CAPACITY - 1,

	NUM_OUTPUTS
};

//
// Light Ids
//
enum LightIds {
	CONN_LIGHT,
	CONN_LIGHT_LAST = CONN_LIGHT + 1, // GreenRedLight needs 2 consecutive ids (green, red)
	OVERFLOW_LIGHT,                   // lit whenever the browsed output has more real channels
	                                  // than this module's own fixed capacity

	NUM_LIGHTS
};
