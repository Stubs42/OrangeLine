/*
    OrangeLine.hpp

    Include file containing all #defines for constants and macros
	common to all OrangeLine moduls

	Should not included in <module_name>.cpp because this is done in OrangeLineCommon.hpp

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
#ifndef ORANGE_LINE_HPP
#define ORANGE_LINE_HPP

#include "plugin.hpp"

#define STATE_TYPE_VALUE   0
#define STATE_TYPE_VOLTAGE 0
#define STATE_TYPE_TRIGGER 1

#define LIGHT_TYPE_SINGLE  0
#define LIGHT_TYPE_RGB     1

#define NUM_STATES					(NUM_JSONS + NUM_PARAMS + NUM_INPUTS + NUM_OUTPUTS + NUM_LIGHTS)
#define NUM_TRIGGERS				(NUM_PARAMS + NUM_INPUTS)

#define stateIdxJson(i)				(i)
#define stateIdxParam(i)			(NUM_JSONS + (i))
#define stateIdxInput(i)			(NUM_JSONS + NUM_PARAMS + (i))
#define stateIdxOutput(i)			(NUM_JSONS + NUM_PARAMS + NUM_INPUTS + (i))
#define stateIdxLight(i)			(NUM_JSONS + NUM_PARAMS + NUM_INPUTS + NUM_OUTPUTS + (i))

#define changeJson(i)				OL_outStateChange[stateIdxJson   (i)]
#define inChangeParam(i)			OL_inStateChange [stateIdxParam  (i)]
#define changeParam(i)				OL_outStateChange[stateIdxParam  (i)]
#define changeInput(i)				OL_inStateChange [stateIdxInput  (i)]
#define changeOutput(i)				OL_outStateChange[stateIdxOutput (i)]
#define changeLight(i)				OL_outStateChange[stateIdxLight  (i)]

#define getStateJson(i)   			OL_state[stateIdxJson   (i)]
#define getStateParam(i)			OL_state[stateIdxParam  (i)]
#define getStateInput(i)			OL_state[stateIdxInput  (i)]
#define getStateOutput(i)			OL_state[stateIdxOutput (i)]
#define getStateLight(i)			OL_state[stateIdxLight  (i)]

#define setStateJson(i, v)			OL_setOutState (stateIdxJson   (i), v)
#define setInStateParam(i, v)		OL_setInState  (stateIdxParam  (i), v)
#define setStateParam(i, v)			OL_setOutState (stateIdxParam  (i), v)
#define setStateInput(i, v)			OL_setInState  (stateIdxInput  (i), v)
#define setStateOutput(i, v)		OL_setOutState (stateIdxOutput (i), v)
#define setStateLight(i, v)			OL_setOutState (stateIdxLight  (i), v) 

#define getStateTypeParam(i)		OL_stateType[stateIdxParam  (i)]
#define getStateTypeInput(i)		OL_stateType[stateIdxInput  (i)]
#define getStateTypeOutput(i)		OL_stateType[stateIdxOutput (i)]
#define getStateTypeLight(i)		OL_stateType[stateIdxLight  (i)]

#define setStateTypeParam(i, t)		(OL_stateType[stateIdxParam  (i)] = (t))
#define setStateTypeInput(i, t)		(OL_stateType[stateIdxInput  (i)] = (t))
#define setStateTypeOutput(i, t)	(OL_stateType[stateIdxOutput (i)] = (t))
#define setStateTypeLight(i, t)		(OL_stateType[stateIdxLight  (i)] = (t))

#define maxStateIdxJson				(stateIdxParam  (0) - 1)
#define maxStateIdxParam			(stateIdxInput  (0) - 1)
#define maxStateIdxInput			(stateIdxOutput (0) - 1)
#define maxStateIdxOutput			(stateIdxLight  (0) - 1)
#define maxStateIdxLight			(NUM_STATES - 1)

#define isGate(i)                   OL_isGate[i]
#define getInputConnected(i)		OL_inputConnected[i]
#define initialized					OL_initialized

#define quantize(CV) 				(round (CV * 12.f) / 12.f)
#define octave(CV)					int(floor (quantize (CV)))
#define note(CV)					(int(round ((CV + 10) * 12)) % 12)

#define reConfigParam(paramId, minVal, maxVal, defaultVal, pLabel) { \
	ParamQuantity *pq = paramQuantities[paramId]; \
	pq->minValue = minVal; \
	pq->maxValue = maxVal; \
	pq->defaultValue = defaultVal; \
	pq->label = pLabel; \
}

#define reConfigParamDefault(paramId, defaultVal) { \
	ParamQuantity *pq = paramQuantities[paramId]; \
	pq->defaultValue = defaultVal; \
}

#endif
