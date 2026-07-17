/*
	XD16.hpp

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
// Display-only member of the XO family - a pure monitor, no real output jacks at all, so
// Wakeup/Ready would just be one more unused hidden jack (nothing is ever re-emitted here).
#define OL_TOUCH_DISABLED
#define XO_CAPACITY 16

#include "OrangeLine.hpp"
#include "XOShared.hpp"

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	STYLE_JSON,
	BROWSE_INDEX_JSON, // persists the last browsed output slot - see XOModuleCommon.hpp's own
	                   // moduleProcess()

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
// Input Ids - XD16 has no real CV inputs at all
//
enum InputIds {
	NUM_INPUTS
};

//
// Output Ids - XD16 has no real CV outputs at all, it's a pure display/monitor
//
enum OutputIds {
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
