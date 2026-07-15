/*
	J.hpp

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
// J is itself Touch-adjacent infrastructure (syncs branches that used Touch), not a Touch
// consumer - deliberately gets no Wakeup/Ready ports of its own.
#define OL_TOUCH_DISABLED

#include "OrangeLine.hpp"

#define AND_THRESHOLD 5.f

enum jsonIds
{
	STYLE_JSON,
	NUM_JSONS
};

enum ParamIds
{
	LEN_PARAM,	// Output gate/trigger length (ms)
	NUM_PARAMS
};

enum InputIds
{
	IN_INPUT,
	IN_INPUT_LAST = IN_INPUT + 7,	// 8 mono inputs
	NUM_INPUTS
};

enum OutputIds
{
	OUT_OUTPUT,
	NUM_OUTPUTS
};

enum LightIds
{
	NUM_LIGHTS
};
