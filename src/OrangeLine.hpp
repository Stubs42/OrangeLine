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
#include <math.h>

#ifndef ORANGE_LINE_HPP
#define ORANGE_LINE_HPP

#include "plugin.hpp"

#define ORANGE		nvgRGB (255, 102,   0)
#define WHITE		nvgRGB (255, 255, 255)
#define RED			nvgRGB (255,   0,   0)

#define STYLE_ORANGE	0
#define STYLE_BRIGHT	1
#define STYLE_DARK	2

#define STATE_TYPE_VALUE   0
#define STATE_TYPE_VOLTAGE 0
#define STATE_TYPE_TRIGGER 1

#define POLY_CHANNELS	  16

#define LIGHT_TYPE_SINGLE  0
#define LIGHT_TYPE_RGB     1

#define NUM_NOTES   12

#define NUM_STATES			(NUM_JSONS + NUM_PARAMS + NUM_INPUTS + NUM_OUTPUTS + NUM_LIGHTS)
#define NUM_TRIGGERS			(NUM_PARAMS + NUM_INPUTS)

#define PRECISION       0.000001f

#define MAX_TEXT_SIZE  64
#define TEXT_SCROLL_DELAY   0.5f
#define TEXT_SCROLL_PRE_DELAY   TEXT_SCROLL_DELAY * 4.f

#define PI            3.14159265

#define VALUE_MODE_REPLACE 0
#define VALUE_MODE_ADD     1
#define VALUE_MODE_SCALE   2

#define NORMAL_MODE_NONE   0
#define NORMAL_MODE_ONE    1
#define NORMAL_MODE_LAST   2

#define SEMITONE       (1.f / 12.f)

/*
	Random implementation derived from the one used in Frozen Wastland Seeds of Change
*/
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */
typedef struct OrangeLineRandom {
	unsigned long mt[N]; /* the array for the state vector  */
	int mti=N+1; /* mti==N+1 means mt[N] is not initialized */
	unsigned long latestSeed = 0;
	unsigned long getCount = 0;
} OrangeLineRandom;

#define stateIdxJson(i)   (i)
#define stateIdxParam(i)  (NUM_JSONS + (i))
#define stateIdxInput(i)  (NUM_JSONS + NUM_PARAMS + (i))
#define stateIdxOutput(i) (NUM_JSONS + NUM_PARAMS + NUM_INPUTS + (i))
#define stateIdxLight(i)  (NUM_JSONS + NUM_PARAMS + NUM_INPUTS + NUM_OUTPUTS + (i))

#define changeJson(i)    OL_outStateChange[stateIdxJson   (i)]
#define inChangeParam(i) OL_inStateChange [stateIdxParam  (i)]
#define changeParam(i)   OL_outStateChange[stateIdxParam  (i)]
#define changeInput(i)   OL_inStateChange [stateIdxInput  (i)]
#define changeOutput(i)	 OL_outStateChange[stateIdxOutput (i)]
#define changeLight(i)   OL_outStateChange[stateIdxLight  (i)]

#define getStateJson(i)   OL_state[stateIdxJson   (i)]
#define getStateParam(i)  OL_state[stateIdxParam  (i)]
#define getStateInput(i)  OL_state[stateIdxInput  (i)]
#define getStateOutput(i) OL_state[stateIdxOutput (i)]
#define getStateLight(i)  OL_state[stateIdxLight  (i)]

#define setStateJson(i, v)       OL_setOutState (stateIdxJson   (i), v)
#define setInStateParam(i, v)    OL_setInState  (stateIdxParam  (i), v)
#define setStateParam(i, v)      OL_setOutState (stateIdxParam  (i), v)
#define setStateInput(i, v)      OL_setInState  (stateIdxInput  (i), v)
#define setStateOutput(i, v)     OL_setOutState (stateIdxOutput (i), v)
#define setStateOutPoly(i, c, v) OL_setOutStatePoly ((i), (c), v)
#define setStateLight(i, v)      OL_setOutState (stateIdxLight  (i), v) 

#define getStateTypeParam(i)  OL_stateType[stateIdxParam  (i)]
#define getStateTypeInput(i)  OL_stateType[stateIdxInput  (i)]
#define getStateTypeOutput(i) OL_stateType[stateIdxOutput (i)]
#define getStateTypeLight(i)  OL_stateType[stateIdxLight  (i)]

#define setStateTypeParam(i, t)  (OL_stateType[stateIdxParam  (i)] = (t))
#define setStateTypeInput(i, t)	 (OL_stateType[stateIdxInput  (i)] = (t))
#define setStateTypeOutput(i, t) (OL_stateType[stateIdxOutput (i)] = (t))
#define setStateTypeLight(i, t)  (OL_stateType[stateIdxLight  (i)] = (t))

#define maxStateIdxJson	  (stateIdxParam  (0) - 1)
#define maxStateIdxParam  (stateIdxInput  (0) - 1)
#define maxStateIdxInput  (stateIdxOutput (0) - 1)
#define maxStateIdxOutput (stateIdxLight  (0) - 1)
#define maxStateIdxLight  (NUM_STATES - 1)

#define getCustomChangeMask(i)      OL_customChangeMask[i]
#define getCustomChangeMaskParam(i) getCustomChangeMask(i)
#define getCustomChangeMaskInput(i) getCustomChangeMask(NUM_PARAMS + (i))

#define setCustomChangeMask(i, v)       (OL_customChangeMask[i] = (v))
#define setCustomChangeMaskParam(i, v)  setCustomChangeMask((i), (v))
#define setCustomChangeMaskInput(i, v)  setCustomChangeMask(NUM_PARAMS + (i), (v))

#define setInPoly(i, v)  (OL_isPoly[i] = (v))
#define setOutPoly(i, v) (OL_isPoly[NUM_INPUTS + i] = (v))
#define getInPoly(i)     OL_isPoly[i]
#define getOutPoly(i)    OL_isPoly[NUM_INPUTS + i]

// #define getIsHot(i)				OL_isHot[i]
// #define setIsHot(i, v)			(OL_isHot[i] = (v))

#define setOutPolyChannels(i, v)	(OL_polyChannels[i] = (v))
#define getOutPolyChannels(i)		OL_polyChannels[i]

#define customChangeBits		OL_customChangeBits

#define isGate(i)			OL_isGate[i]
#define isGatePoly(c, i)		OL_isGatePoly[c * POLY_CHANNELS + i]

#define getInputConnected(i)		OL_inputConnected[i]
#define initialized			OL_initialized

#define quantize(CV)			(round (CV * 12.f) / 12.f)
#define octave(CV)			int(floor (quantize (CV)))
#define note(CV)			(int(round ((CV + 10) * 12)) % 12)

#define reConfigParam(paramId, minVal, maxVal, defaultVal, pLabel) { \
	ParamQuantity *pq = paramQuantities[paramId]; \
	pq->minValue = minVal; \
	pq->maxValue = maxVal; \
	pq->defaultValue = defaultVal; \
}

/*
#define reConfigParam(paramId, minVal, maxVal, defaultVal, pLabel) { \
	ParamQuantity *pq = paramQuantities[paramId]; \
	pq->minValue = minVal; \
	pq->maxValue = maxVal; \
	pq->defaultValue = defaultVal; \
	pq->label = pLabel; \
}
*/

#define reConfigParamDefault(paramId, defaultVal) { \
	ParamQuantity *pq = paramQuantities[paramId]; \
	pq->defaultValue = defaultVal; \
}

#define reConfigParamLabel(paramId, pLabel) { \
	ParamQuantity *pq = paramQuantities[paramId]; \
	pq->label = pLabel; \
}

#define reConfigParamMinValue(paramId, value) { \
	ParamQuantity *pq = paramQuantities[paramId]; \
	pq->minValue = value; \
}

#define reConfigParamMaxValue(paramId, value) { \
	ParamQuantity *pq = paramQuantities[paramId]; \
	pq->maxValue = value; \
}

#define PANELHEIGHT 128.5f

#define OFFSET_NULL 0.f
#define OFFSET_PJ301MPort 4.2
#define OFFSET_Trimpot 3.15
#define OFFSET_RoundSmallBlackKnob 4.0
#define OFFSET_RoundHugeBlackKnob 9.5
#define OFFSET_LargeLight 4.762 / 2
#define OFFSET_LEDButton 4.762 / 2
#define OFFSET_RoundLargeBlackKnob 12.7 / 2

#define calculateCoordinates(x, y, offset) mm2px (Vec (x + offset ,  y + offset))

// ********************************************************************************************************************************
/**
	Widgets
*/

/**
	Widget to display cvOct values as floats or notes
*/
struct NumberWidget : TransparentWidget {

	Module     *module = nullptr;
	float      *pValue = nullptr;
 	const char *format = nullptr;
	char       *buffer = nullptr;
	int         length = 0;
	float       defaultValue = 0.f;
	float	   *pStyle = nullptr;
	bool 		customForegroundColor = false;
	NVGcolor 	foregroundColor = nvgRGB(0,0,0);
	float       fontSize;

	static NumberWidget* create (Vec pos, Module *module, float *pValue, float defaultValue, const char *format, char *buffer, int length) {
		NumberWidget *w = new NumberWidget();

		w->box.pos  = pos;
		w->box.size = mm2px (Vec (4 * length, 7));
		w->module   = module;
		w->pValue   = pValue;
		w->defaultValue = defaultValue;
		w->format   = format;
		w->buffer   = buffer;
		w->length   = length;
		w->fontSize = 18.f;

		return w;
	}
	/**
		Constructor
	*/
	NumberWidget () {
	}

	void drawLayer (const DrawArgs &drawArgs, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(drawArgs, layer);
			return;
		}
		std::shared_ptr<Font> pFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId (drawArgs.vg, pFont->handle);
		nvgFontSize (drawArgs.vg, fontSize);
		if (customForegroundColor) {
			nvgFillColor (drawArgs.vg, foregroundColor);
		}
		else {
			nvgFillColor (drawArgs.vg, (pStyle == nullptr || *pStyle == STYLE_ORANGE) ? ORANGE : WHITE);
		}
		float value = pValue != nullptr ? *pValue : defaultValue;
		snprintf (buffer, length + 1, format, value);
		buffer[length] = '\0';
		nvgText (drawArgs.vg, 0, 0, buffer, nullptr);
		Widget::drawLayer(drawArgs, 1);
	}
};

/**
	Widget to display cvOct values as floats or notes
*/
struct TextWidget : TransparentWidget {

	Module     *module = nullptr;
	const char *text   = nullptr;
	int	    length = 0;
	int	    scrollPos = 0;
	int	   *pTimer;
	const char *defaultText = nullptr;
	float	   *pStyle = nullptr;
	bool	    reset = false;
	bool 		customForegroundColor = false;
	NVGcolor 	foregroundColor = nvgRGB(0,0,0);

	static TextWidget* create (Vec pos, Module *module, const char *text, const char * defaultText, int length, int *pTimer) {
		TextWidget *w = new TextWidget();

		w->box.pos  = pos;
		w->box.pos.y  -= mm2px (5);
		w->box.size = mm2px (Vec (4 * length, 7));
		w->module   = module;
		w->text     = text;
		w->defaultText  = defaultText;
		w->length   = length;
		w->pTimer   = pTimer;

		return w;
	}

	/**
		Constructor
	*/
	TextWidget () {
	}

	// void unsetForegroundColor() {
	// 	customForegroundColor = false;
	// }

	// void setForegroundColor(NVGcolor color) {
	// 	customForegroundColor = true;
	// 	foregroundColor = color;
	// }

	void drawLayer (const DrawArgs &drawArgs, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(drawArgs, layer);
			return;
		}
		std::shared_ptr<Font> pFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		const char *delimiter = " - ";
		char buf[MAX_TEXT_SIZE * 2 + 1 + 3 /* delimiter length */];
        const char* str = (text != nullptr ? text : defaultText);
		int len = strlen(str);
		if (len > MAX_TEXT_SIZE)
			len = MAX_TEXT_SIZE;
		nvgFontFaceId (drawArgs.vg, pFont->handle);
		nvgFontSize (drawArgs.vg, 18);
		if (customForegroundColor) {
			nvgFillColor (drawArgs.vg, foregroundColor);
		}
		else {
			nvgFillColor (drawArgs.vg, (pStyle == nullptr || *pStyle == STYLE_ORANGE) ? ORANGE : WHITE);
		}
		if (len <= length) {
			nvgText (drawArgs.vg, 0, mm2px (5), str, nullptr);
		}
		else {
			if (pTimer != nullptr && len > length) {
				if (*pTimer <= 0) {
					*pTimer = int(TEXT_SCROLL_DELAY  * APP->engine->getSampleRate ());
					scrollPos = (scrollPos + 1) % (len + 3);
				}
				else {
					if (*pTimer > int(TEXT_SCROLL_DELAY * APP->engine->getSampleRate ())) {
						if (!reset) {
							reset = true;
							scrollPos = 0;
						}
					}
					else {
						reset = false;
					}
				}
			}
			strncpy (buf, str, len);
			strcpy (buf + len, delimiter);
			strncpy (buf + len + 3 /* delimiter length */, str, len);
			buf[MAX_TEXT_SIZE * 2 + 3] = '\0';
			buf[scrollPos + length] = '\0';
			nvgText (drawArgs.vg, 0, mm2px (5), buf + scrollPos, nullptr);
		}
		Widget::drawLayer(drawArgs, 1);
	}
};

/**
	Self-drawn interactive grid of NUM_CC_COLS x NUM_CC_ROWS cells, one per MIDI CC (cell
	[row][col] = CC row*NUM_CC_COLS+col). Click a cell to toggle it on/off (writes directly
	into the module's own bool[128] via the `enabled` pointer - no per-cell Params, far
	cheaper than 128 individual widgets). Cell brightness also reflects `activity[cc]`
	(0-1, expected to be set to 1 and decayed by the module itself) as a live traffic
	indicator, independent of the on/off state - shared by CC2CV and CV2CC.
*/
#define NUM_CC_COLS 16
#define NUM_CC_ROWS 8

// Grid cell colors: enabled base is a value-dependent gradient (dark orange at 0V, bright
// orange at 10V - still visibly orange-tinted rather than gray at 0V, so it stays distinct
// from GRID_OFF's neutral near-black), plus a distinct activity-flash color for each state,
// so a muted (disabled) CC's traffic reads unmistakably differently from an active one's.
#define GRID_ON_LOW    nvgRGB ( 80,  40,   5)	// enabled base @ 0V: dark orange
#define GRID_ON_HIGH   nvgRGB (255, 130,  40)	// enabled base @ 10V: bright orange, not blown out
#define GRID_ON_ACT    nvgRGB ( 70, 220, 120)	// enabled + activity flash: green
#define GRID_OFF       nvgRGB ( 30,  30,  30)	// disabled base: near black
#define GRID_OFF_ACT   nvgRGB (200,  70,  70)	// disabled + activity flash: muted red (not full ff0000)

struct CCGridWidget : OpaqueWidget {

	bool  *enabled  = nullptr;	// pointer to module's bool[128], toggled on click
	float *activity = nullptr;	// pointer to module's float[128], 0-1, read only here
	float *value    = nullptr;	// pointer to module's float[128], 0-10V, read only here

	static CCGridWidget* create (Vec pos, Vec size, bool *enabled, float *activity, float *value = nullptr) {
		CCGridWidget *w = new CCGridWidget ();
		w->box.pos  = pos;
		w->box.size = size;
		w->enabled  = enabled;
		w->activity = activity;
		w->value    = value;
		return w;
	}

	/** While painting (mouse held from a press on this widget), every cell the mouse passes
		over is force-set to this value instead of toggled - set once on press from the
		pressed cell's new state, so a whole drag stays consistently "turning on" or
		"turning off" regardless of what the cells being passed over used to be. */
	bool paintValue = false;

	/** Returns the CC index for a local position, or -1 if outside the grid. */
	int cellAt (Vec pos) {
		int col = int (pos.x / (box.size.x / NUM_CC_COLS));
		int row = int (pos.y / (box.size.y / NUM_CC_ROWS));
		if (col < 0 || col >= NUM_CC_COLS || row < 0 || row >= NUM_CC_ROWS) return -1;
		return row * NUM_CC_COLS + col;
	}

	void onButton (const ButtonEvent &e) override {
		OpaqueWidget::onButton (e);
		if (!enabled) return;
		if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		int cc = cellAt (e.pos);
		if (cc < 0) return;
		enabled[cc] = paintValue = !enabled[cc];
	}

	/** Fires every frame the mouse is over this widget while a drag (started by onButton
		above) is in progress - lets a click-drag "paint" the same on/off value across every
		cell it passes over, instead of only ever affecting the single cell first pressed. */
	void onDragHover (const DragHoverEvent &e) override {
		OpaqueWidget::onDragHover (e);
		if (!enabled) return;
		int cc = cellAt (e.pos);
		if (cc < 0) return;
		enabled[cc] = paintValue;
	}

	void draw (const DrawArgs &drawArgs) override {
		if (!enabled) return;
		float cw = box.size.x / NUM_CC_COLS;
		float ch = box.size.y / NUM_CC_ROWS;
		for (int row = 0; row < NUM_CC_ROWS; row++) {
			for (int col = 0; col < NUM_CC_COLS; col++) {
				int cc = row * NUM_CC_COLS + col;
				float x = col * cw;
				float y = row * ch;
				float pad = fminf (cw, ch) * 0.08f;

				NVGcolor base;
				if (enabled[cc]) {
					float v = value ? clamp (value[cc] / 10.f, 0.f, 1.f) : 0.f;
					base = nvgLerpRGBA (GRID_ON_LOW, GRID_ON_HIGH, v);
				} else {
					base = GRID_OFF;
				}
				NVGcolor activityColor = enabled[cc] ? GRID_ON_ACT : GRID_OFF_ACT;
				float act = activity ? clamp (activity[cc], 0.f, 1.f) : 0.f;
				NVGcolor fill = nvgLerpRGBA (base, activityColor, act);

				nvgBeginPath (drawArgs.vg);
				nvgRoundedRect (drawArgs.vg, x + pad, y + pad, cw - 2 * pad, ch - 2 * pad, pad * 0.5f);
				nvgFillColor (drawArgs.vg, fill);
				nvgFill (drawArgs.vg);
				nvgStrokeColor (drawArgs.vg, nvgRGB (15, 15, 15));
				nvgStrokeWidth (drawArgs.vg, pad * 0.3f);
				nvgStroke (drawArgs.vg);
			}
		}
	}
};

#endif
