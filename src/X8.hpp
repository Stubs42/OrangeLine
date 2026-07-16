/*
	X8.hpp

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
// X8 has no real CV jacks at all (it's a param-access Expander, no sockets by design - see
// ExpanderParamAccessSpec.md), so Wakeup/Ready would just be one more unused hidden jack.
#define OL_TOUCH_DISABLED

#include "OrangeLine.hpp"
#include "XShared.hpp"

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	STYLE_JSON,
	CHANNEL_LIMIT_JSON,

	NUM_JSONS
};

//
// Parameter Ids
//
#define NUM_X8_KNOBS 8
enum ParamIds {
	KNOB_PARAM,
	KNOB_PARAM_LAST = KNOB_PARAM + NUM_X8_KNOBS - 1,
	LEFT_PARAM,
	RIGHT_PARAM,
	ENGAGE_PARAM,

	NUM_PARAMS
};

//
// Input Ids - X8 has no real CV inputs at all
//
enum InputIds {
	NUM_INPUTS
};

//
// Output Ids - X8 has no real CV outputs at all
//
enum OutputIds {
	NUM_OUTPUTS
};

//
// Light Ids
//
enum LightIds {
	CONN_LIGHT,

	NUM_LIGHTS
};
