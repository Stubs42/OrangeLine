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
#include <functional>

#ifndef ORANGE_LINE_HPP
#define ORANGE_LINE_HPP

#include "plugin.hpp"
// Guarantees ExpanderBridgeInterface/ExpanderDataStore/etc. are always defined at global scope,
// for every module, before OrangeLineCommon.hpp's own #include "ExpanderBridge.hpp" (needed so
// every module gets a real ExpanderDataStore member - see that file's own comment) ever runs.
// OrangeLineCommon.hpp is always #include'd literally INSIDE a module's struct body - if
// ExpanderBridge.hpp's own first-ever expansion happened there (for a module that doesn't
// otherwise pull it in via XShared.hpp/XOShared.hpp/etc. at file scope first), every type in it
// would become a nested, module-private duplicate instead of the single global one everything
// else references. Safe despite the circular #include (ExpanderBridge.hpp includes this file
// right back) - neither file's own content actually depends on anything defined by the other.
#include "ExpanderBridge.hpp"

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

// Global fixed flash duration for any "Click" style momentary indicator (X8ValueButton's Click
// mode, and any future control that wants the same fixed-length flash regardless of hold time) -
// one shared value so every module's click flash reads consistently, not a per-module tuning
// knob. Raised from an original 0.06s (too short to reliably notice) to 0.25s, 2026-07-18.
#define X_VALUE_CLICK_SECONDS 0.05f

#define LIGHT_TYPE_SINGLE  0
#define LIGHT_TYPE_RGB     1

#define NUM_NOTES   12

#define NUM_STATES			(NUM_JSONS + NUM_PARAMS + NUM_INPUTS + NUM_OUTPUTS + NUM_LIGHTS)
#define NUM_TRIGGERS			(NUM_PARAMS + NUM_INPUTS)

#define PRECISION       0.000001f

#define MAX_TEXT_SIZE  64
#define TEXT_SCROLL_DELAY   0.5f
#define TEXT_SCROLL_PRE_DELAY   TEXT_SCROLL_DELAY * 4.f

// Shared "nothing bound anywhere yet" greeting, three tiers from shortest to longest - the
// X-family's own X8NameDisplay (X8Common.hpp) picks whichever one actually fits its own display
// width (X8: WORD1 alone; X8D/X16: WORD1+WORD2; X16D: OL_GREETING_LONG, its display being wide
// enough for essentially everything), and Mother's own startup greeting (Mother.hpp) always uses
// the full OL_GREETING_LONG, having plenty of room. One place to change the wording later -
// replaces Mother's old "Hey Gals !!!" (gendered wording, Dieter's call). Defined here (not
// OrangeLineCommon.hpp) so headers like Mother.hpp/X8Common.hpp that are processed before a
// module's own OrangeLineCommon.hpp include can still see it - see OrangeLine.hpp's own role as
// the "include this first" shared header in CLAUDE.md.
#define OL_GREETING_WORD1 "HELLO"
#define OL_GREETING_WORD2 "BUDDY"
#define OL_GREETING_LONG  "HELLO RACKHEADS !"

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

/*
	SplitMix64 - a small, fast, deterministic 64-bit mixing function (public domain, Vigna),
	used where a single seed needs to expand into one well-distributed pseudo-random draw with no
	persistent generator state at all - unlike OrangeLineRandom/MT19937 above (624 words of state,
	built for Dejavu's own advancing multi-draw sequences), this is a pure uint64_t -> uint64_t
	hash: same input always yields the same output, good statistical distribution for musical/
	non-cryptographic use, no clustering or short cycles to worry about. First user: XR8/XR16.
*/
inline uint64_t splitMix64(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ULL;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
	return x ^ (x >> 31);
}

// Low 32 bits of a splitMix64() draw, normalized to [0,1) - plenty of precision for CV purposes.
inline float splitMix64Normalized(uint64_t raw)
{
	return (float)(raw & 0xFFFFFFFFULL) / 4294967296.f;
}

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
	A connection-status light that goes fully invisible (not just dark) when its brightness is
	0 - standard Rack light components always render their bezel/background even when off,
	which reads as "a light exists here" regardless of state. Purely cosmetic (Dieter's call,
	2026-07-15): use createLightCentered<AutoHideLight<TinyLight<GreenLight>>>(...) etc. in
	place of the plain component wherever "nothing to see" should mean nothing visible at all.
*/
template <typename TBase>
struct AutoHideLight : TBase
{
	void step() override
	{
		if (this->module)
		{
			bool anyLit = false;
			for (size_t c = 0; c < this->baseColors.size(); c++)
				if (this->module->lights[this->firstLightId + c].getBrightness() > 0.0001f)
				{
					anyLit = true;
					break;
				}
			this->visible = anyLit;
		}
		TBase::step();
	}
};

// Single on/off switch for every connection-status light across the whole plugin (X-family,
// XO-family, XR-family, LANES-family, NEO, Morpheus) - flip this one value to re-enable them
// everywhere at once, instead of touching dozens of individual widget-construction call sites.
// Disabled 2026-07-18 (Dieter: more distracting than informative, breaks each panel header's own
// optics - connection state is already conveyed for free by the panel's own controls/displays
// going grey/dashed when nothing's resolved). The underlying per-module setStateLight()/
// setStateJson() tracking logic that feeds these lights' brightness is NOT touched by this flag -
// only whether the widget itself ever gets added to the panel.
#define OL_CONNECTION_LIGHTS_ENABLED false

// Call from a ModuleWidget's own constructor exactly where a connection light would otherwise be
// added, e.g.:
//   addOrangeLineConnectionLight<AutoHideLight<TinyLight<GreenRedLight>>>(this,
//       calculateCoordinates(3.5f, 4.f, 0.f), module, CONN_LIGHT);
// A no-op entirely when OL_CONNECTION_LIGHTS_ENABLED is false - no widget is constructed at all,
// not just hidden, so this costs nothing when disabled.
template <typename TLight>
inline void addOrangeLineConnectionLight(widget::Widget *parent, Vec pos, engine::Module *module, int firstLightId)
{
	if (!OL_CONNECTION_LIGHTS_ENABLED)
		return;
	parent->addChild(createLightCentered<TLight>(pos, module, firstLightId));
}

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
	Shared drawing for a "labeled state button" (2026-07-20) - filled rounded rect + stroke +
	centered text, all in one caller-supplied accent color. Extracted as a free function (not
	tied to any one widget class) specifically so it can back BOTH a plain OpaqueWidget-style
	direct-action button (OLLabelButton below, NEO's own LOCK/FREE) AND a real Rack ParamWidget
	button (X8ButtonBase/X8BindButtonBase, X8Common.hpp - X8's BIND/FREE, a genuine automatable
	param with its own press/hold/release semantics that don't fit OLLabelButton's simple
	onClick shape) - 2026-07-21 rollout, per Dieter's own instruction the day before ("let just
	NEO use this common code for now... roll it out to the rest of the modules... another day").
	Deliberately theme-agnostic itself (no X_BUTTON_FILL_* or X_COLOR_INACTIVE_GREY reference here -
	those are X-family-specific, XShared.hpp, which itself includes this file) - every color,
	the label text, and all sizing are passed in by the caller.

	Text is vertically (AND horizontally) centered using nvgTextBounds() against the ACTUAL
	rendered glyph bounds of the current text/font/size, with NVG_ALIGN_TOP for both the
	measurement and the actual draw call - not NVG_ALIGN_MIDDLE/CENTER's own font-metric-based
	guess (which drifts off-center differently at different font sizes for a pixel-style font
	like repetition-scrolling, confirmed live), and not NVG_ALIGN_BASELINE either (two separate
	baseline-relative attempts both went wrong in confusingly opposite-seeming directions before
	landing on TOP alignment, which sidesteps the ascender/descender sign reasoning entirely).
	textYNudgePx is a small tunable residual correction on top of that (font atlas padding/
	hinting keeps the measurement from landing pixel-perfect on its own) - expect to live-tune
	it per font/size rather than assume one caller's value transfers to another.
*/
inline void olDrawLabelButton(NVGcontext *vg, Vec size, NVGcolor fill, NVGcolor accent,
	const std::string &label, const char *fontPath, float fontSize,
	float cornerRadiusPx, float strokeWidthPx, float textYNudgePx = 0.f)
{
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0.f, 0.f, size.x, size.y, cornerRadiusPx);
	nvgFillColor(vg, fill);
	nvgFill(vg);
	nvgStrokeWidth(vg, strokeWidthPx);
	nvgStrokeColor(vg, accent);
	nvgStroke(vg);

	if (label.empty())
		return;

	std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, fontSize);
	nvgFillColor(vg, accent);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	float bounds[4];
	nvgTextBounds(vg, 0.f, 0.f, label.c_str(), nullptr, bounds);
	float textWidth = bounds[2] - bounds[0];
	float textHeight = bounds[3] - bounds[1];
	float x = (size.x - textWidth) / 2.f - bounds[0];
	float y = (size.y - textHeight) / 2.f - bounds[1] + textYNudgePx;
	nvgText(vg, x, y, label.c_str(), nullptr);
}

/**
	Generic drawn "labeled state button" widget, built on olDrawLabelButton() above (2026-07-20)
	- the caller resolves fill/accent/label however it needs to via callbacks (a three-state
	grey/themed/highlight scheme is the established convention - see X8BindButtonBase's own
	getAccentColor()/getLabel() pattern, X8Common.hpp, which this generalizes). First real
	caller: NEO's own LOCK/FREE button (Neo.cpp).

	A plain OpaqueWidget, not a Rack ParamWidget - a simple click callback (onClick) rather than a
	bound param, matching NEO's own toggleLock()-style direct action; X8's own real BIND param
	uses olDrawLabelButton() directly from its own ParamWidget-based X8ButtonBase instead of this
	widget, since its press/hold/release semantics don't fit a plain onClick.
*/
struct OLLabelButton : OpaqueWidget
{
	std::function<NVGcolor()> getFillColor;
	std::function<NVGcolor()> getAccentColor;
	std::function<std::string()> getLabel;
	std::function<void()> onClick;
	float fontSize = 12.f;
	float cornerRadiusPx = 1.f;
	float strokeWidthPx = 1.f;
	float textYNudgePx = 0.f;
	const char *fontPath = "res/repetition-scrolling.regular.ttf";

	void draw(const DrawArgs &args) override
	{
		NVGcolor fill = getFillColor ? getFillColor() : nvgRGB(0, 0, 0);
		NVGcolor accent = getAccentColor ? getAccentColor() : nvgRGB(255, 255, 255);
		std::string text = getLabel ? getLabel() : std::string();
		olDrawLabelButton(args.vg, box.size, fill, accent, text, fontPath, fontSize, cornerRadiusPx, strokeWidthPx, textYNudgePx);
	}

	void onButton(const event::Button &e) override
	{
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && onClick)
		{
			onClick();
			e.consume(this);
		}
		OpaqueWidget::onButton(e);
	}
};

// Shared "digital display" background colors (2026-07-21, lifted out of NEO's own
// NEO_ROW_DISPLAY_BG_* - see CLAUDE.md's "Code-drawn digital displays and knob rings" section)
// - a genuinely darker, distinct color from the plain panel/strip background, for a true LCD-
// style readout cell. Frame stroke color still comes from each family's own theme lookup
// (X_FRAME_ORANGE/DARK/BRIGHT, XShared.hpp) since that part was already shared.
#define OL_DISPLAY_BG_ORANGE nvgRGB(0x10, 0x06, 0x00)
#define OL_DISPLAY_BG_DARK   nvgRGB(0x17, 0x17, 0x17)
#define OL_DISPLAY_BG_BRIGHT nvgRGB(0x15, 0x15, 0x2b)

// Per-character glyph width AND inter-character spacing for res/repetition-scrolling.regular.ttf
// (the one house font every OrangeLine digital display/button already uses), each as a fraction
// of font size. Re-derived 2026-07-21 from real Inkscape measurements (Dieter's own data, not a
// guess): outer text widths of 1.7 / 3.648 / 5.595 / 7.542mm for 1/2/3/4 characters, AND 194.487mm
// for 100 characters, all at a 10pt font size. Solving width(N) = N*charWidth + (N-1)*spacing:
// N=1 pins charWidth exactly (a single character has no spacing term at all) = 1.7mm. spacing
// solved from the N=100 point rather than the small-N ones - Dieter's own catch: fitting spacing
// from 2-4 characters bakes in whatever rounding those small, already-rounded measurements carry,
// and that error then multiplies by (N-1) at any larger N (a 100-character field makes a
// 99x-amplified error impossible to miss, which is exactly why that data point was worth getting -
// it constrains spacing far more precisely than any of the short samples can). spacing =
// (194.487 - 100*1.7) / 99 = 0.24734mm at 10pt (not the small-N-implied ~0.248 - a real, if small,
// difference once multiplied out at length). Both re-verified against ALL five measured points
// (1/2/3/4/100 chars) with this single consistent (charWidth, spacing) pair - matches every one to
// within 0.001mm. Converted to a fraction of font size using the REAL pt->mm factor (1pt =
// 25.4/72mm = 0.352778mm, so 10pt = 3.52778mm - NOT "10mm", a first pass here mistakenly treated pt
// and mm as interchangeable and came out ~2.7x too small until caught): 1.7/3.52778 = 0.48189,
// 0.24734/3.52778 = 0.07011. Supersedes the older single-ratio model (a plain numChars*ratio with
// no separate spacing term, 0.553) - that model conflated per-glyph width and inter-character
// spacing into one number, a genuinely different (and less accurate, especially at higher
// character counts) shape than the real relationship, not just a mis-tuned constant.
#define OL_DISPLAY_CHAR_WIDTH_RATIO   0.48189f
#define OL_DISPLAY_CHAR_SPACING_RATIO 0.07011f
// Small flat correction (2026-07-21, Dieter's own instruction: "just add another 0.25 to the
// total width as correction for now") - the ratios above are fit against Inkscape's own text
// measurement, not NanoVG/fontstash's actual rendering in Rack; live-testing showed NanoVG renders
// this font very slightly wider, which eats asymmetrically into the right-side inset (the left
// side is pinned exactly at insetMm by construction, so any underestimate only ever shows up on
// the right). A pragmatic empirical patch for now, not a re-derived root-cause fix - revisit if a
// real Rack-measured (not Inkscape-measured) reference point ever becomes available.
#define OL_DISPLAY_WIDTH_CORRECTION_MM 0.25f

// A display field's own width, derived from its character count and font size rather than a
// hand-picked constant per field (Dieter's own instruction, originally for NEO, 2026-07-20: "the
// display width of those displays should be a result of a common function f(#chars, fontsize),
// not a field specific define") - moved here 2026-07-21 so any future module's own fixed-width
// digital display can reuse it too, not just NEO's. The DEFAULT result (nudgeMm = 0) must stay the
// single, generically-correct answer for a given (numChars, fontSizeMm) - a caller that finds it
// off for its own specific layout must NOT work around that by lying about numChars or reimplementing
// the formula locally (Dieter's own catch, 2026-07-21, after exactly that happened - a module
// passed numChars-1 to shrink an 8-character field that measured about one character too wide).
// `nudgeMm` is the one sanctioned escape hatch for a genuine per-field layout adjustment - an
// explicit, visible correction on top of the real character count, not a substitute for it.
inline float olDisplayWidthMm(int numChars, float fontSizeMm, float insetMm, float nudgeMm = 0.f)
{
	float spacingCount = (numChars > 0) ? (float) (numChars - 1) : 0.f;
	float charsWidth = (float) numChars * fontSizeMm * OL_DISPLAY_CHAR_WIDTH_RATIO;
	float spacingWidth = spacingCount * fontSizeMm * OL_DISPLAY_CHAR_SPACING_RATIO;
	return charsWidth + spacingWidth + 2.f * insetMm + OL_DISPLAY_WIDTH_CORRECTION_MM + nudgeMm;
}

/**
	Shared drawing for a code-drawn "digital display" cell's own background + frame (2026-07-21,
	lifted out of NEO's own NeoRowTextDisplayWidget - see CLAUDE.md's "Code-drawn digital displays
	and knob rings" section). Deliberately separate from olDrawDisplayText() below rather than one
	combined call: a display's background/frame is decoration (drawn every frame, not dimmable),
	while its text is real content (drawn from drawLayer(layer==1) so it participates in Rack's
	own "lights off" dim simulation like every other OrangeLine display) - the same draw()/
	drawLayer() split NeoRowHeaderFrameWidget and every other frame widget in this codebase
	already uses, matching the reasoning in the "Self-drawn widget backgrounds" section above.
*/
inline void olDrawDisplayFrame(NVGcontext *vg, Vec size, NVGcolor bg, NVGcolor frameColor, float cornerRadiusPx, float strokeWidthPx)
{
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0.f, 0.f, size.x, size.y, cornerRadiusPx);
	nvgFillColor(vg, bg);
	nvgFill(vg);
	nvgStrokeWidth(vg, strokeWidthPx);
	nvgStrokeColor(vg, frameColor);
	nvgStroke(vg);
}

/**
	Shared drawing for a code-drawn "digital display" cell's own text - one line (line2 empty) or
	two lines stacked top/bottom-half (both non-empty), each centered vertically within its own
	half the same way a single line centers within the whole box. Call from drawLayer(layer==1),
	AFTER olDrawDisplayFrame() has already drawn the background/frame from plain draw() - see its
	own comment for why these are two separate functions.
*/
inline void olDrawDisplayText(NVGcontext *vg, Vec size, NVGcolor textColor, const std::string &line1, const std::string &line2,
	const char *fontPath, float fontSize, float textInsetXPx, float textYOffsetPx, float line2YNudgePx = 0.f)
{
	if (line1.empty() && line2.empty())
		return;

	std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, fontSize);
	nvgFillColor(vg, textColor);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

	if (!line2.empty())
	{
		float halfHeight = size.y / 2.f;
		if (!line1.empty())
			nvgText(vg, textInsetXPx, halfHeight / 2.f + textYOffsetPx, line1.c_str(), nullptr);
		nvgText(vg, textInsetXPx, halfHeight + halfHeight / 2.f + textYOffsetPx + line2YNudgePx, line2.c_str(), nullptr);
	}
	else
		nvgText(vg, textInsetXPx, size.y / 2.f + textYOffsetPx, line1.c_str(), nullptr);
}

/**
	Self-drawn interactive grid of NUM_CC_COLS x NUM_CC_ROWS cells, one per MIDI CC (cell
	[row][col] = CC row*NUM_CC_COLS+col). Click a cell to toggle it on/off (writes directly
	into the module's own bool[128] via the `enabled` pointer - no per-cell Params, far
	cheaper than 128 individual widgets). Cell brightness also reflects `activity[cc]`
	(0-1, expected to be set to 1 and decayed by the module itself) as a live traffic
	indicator, independent of the on/off state - shared by CC2CV, CV2CC, RECALL and MidiBus.
*/
#define NUM_CC_COLS 16
#define NUM_CC_ROWS 8

// Grid cell colors: enabled base is a value-dependent gradient (dark orange at 0V, bright
// orange at 10V - still visibly orange-tinted rather than gray at 0V, so it stays distinct
// from GRID_OFF's neutral near-black), plus a distinct activity-flash color for each state,
// so a muted (disabled) CC's traffic reads unmistakably differently from an active one's.
// A second, blue-toned gradient (GRID_ON_PATCHED_*) is used instead when the optional
// `patched` pointer says this cell's underlying port currently has something patched into
// it (e.g. an infix override) - purely a display distinction, orthogonal to enabled/activity.
// Two more optional gradients (GRID_ON_SNAPSHOT_OVERRIDE_*, teal / GRID_ON_SEND_OVERRIDE_*,
// magenta) exist for modules with more than one stacked infix point (RECALL's TX grid: an
// override that only changes a frozen snapshot vs. one that changes what's actually sent
// right now) - see the `snapshotOverride`/`sendOverride` pointers below. Priority when several
// apply at once: sendOverride > snapshotOverride > patched > plain enabled.
#define GRID_ON_LOW    nvgRGB (110,  55,  10)	// enabled base @ 0V: dark orange (bumped up from 80,40,5 for contrast against GRID_OFF)
#define GRID_ON_HIGH   nvgRGB (255, 130,  40)	// enabled base @ 10V: bright orange, not blown out
#define GRID_ON_PATCHED_LOW  nvgRGB (  5,  40,  80)	// enabled+patched base @ 0V: dark blue
#define GRID_ON_PATCHED_HIGH nvgRGB ( 40, 130, 255)	// enabled+patched base @ 10V: bright blue
#define GRID_ON_SNAPSHOT_OVERRIDE_LOW  nvgRGB (  5,  60,  60)	// enabled+snapshot-override base @ 0V: dark teal
#define GRID_ON_SNAPSHOT_OVERRIDE_HIGH nvgRGB ( 40, 200, 200)	// enabled+snapshot-override base @ 10V: bright teal
#define GRID_ON_SEND_OVERRIDE_LOW  nvgRGB ( 70,   5,  70)	// enabled+send-override base @ 0V: dark magenta
#define GRID_ON_SEND_OVERRIDE_HIGH nvgRGB (220,  40, 220)	// enabled+send-override base @ 10V: bright magenta
#define GRID_ON_ACT    nvgRGB ( 70, 220, 120)	// enabled + activity flash: green
#define GRID_OFF       nvgRGB ( 30,  30,  30)	// disabled base: near black
#define GRID_OFF_ACT   nvgRGB (200,  70,  70)	// disabled + activity flash: muted red (not full ff0000)

struct CCGridWidget : OpaqueWidget {

	bool  *enabled  = nullptr;	// pointer to module's bool[128], toggled on click
	float *activity = nullptr;	// pointer to module's float[128], 0-1, read only here
	float *value    = nullptr;	// pointer to module's float[128], 0-10V, read only here
	bool  *locked   = nullptr;	// pointer to module's bool, read only here - while true, clicks/drags are ignored
	bool  *patched  = nullptr;	// pointer to module's bool[128], read only here - optional, picks the blue gradient when set
	bool  *snapshotOverride = nullptr;	// pointer to module's bool[128], read only here - optional, teal gradient (e.g. RECALL's frozen-snapshot infix actually diverged)
	bool  *sendOverride     = nullptr;	// pointer to module's bool[128], read only here - optional, magenta gradient, takes priority over snapshotOverride/patched

	static CCGridWidget* create (Vec pos, Vec size, bool *enabled, float *activity, float *value = nullptr, bool *locked = nullptr, bool *patched = nullptr, bool *snapshotOverride = nullptr, bool *sendOverride = nullptr) {
		CCGridWidget *w = new CCGridWidget ();
		w->box.pos  = pos;
		w->box.size = size;
		w->enabled  = enabled;
		w->activity = activity;
		w->value    = value;
		w->locked   = locked;
		w->patched  = patched;
		w->snapshotOverride = snapshotOverride;
		w->sendOverride     = sendOverride;
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

	/** While locked, this widget must behave as if it weren't an OpaqueWidget at all - not
		just skip its own click handling, but actually stop consuming/stopPropagating the
		event, so it bubbles up to the ModuleWidget underneath (e.g. so click-dragging to move
		the module works normally even when the drag starts on top of the grid). Falls through
		to plain Widget::onHover (no consumption) instead of OpaqueWidget::onHover. */
	void onHover (const HoverEvent &e) override {
		if (locked && *locked) { Widget::onHover (e); return; }
		OpaqueWidget::onHover (e);
	}

	void onButton (const ButtonEvent &e) override {
		if (locked && *locked) { Widget::onButton (e); return; }
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
		if (locked && *locked) { Widget::onDragHover (e); return; }
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
					if (sendOverride && sendOverride[cc])
						base = nvgLerpRGBA (GRID_ON_SEND_OVERRIDE_LOW, GRID_ON_SEND_OVERRIDE_HIGH, v);
					else if (snapshotOverride && snapshotOverride[cc])
						base = nvgLerpRGBA (GRID_ON_SNAPSHOT_OVERRIDE_LOW, GRID_ON_SNAPSHOT_OVERRIDE_HIGH, v);
					else if (patched && patched[cc])
						base = nvgLerpRGBA (GRID_ON_PATCHED_LOW, GRID_ON_PATCHED_HIGH, v);
					else
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

/**
	Touch: shared widget/menu helpers for the hidden force-process trigger pair every module can
	get for free (mechanism lives in OrangeLineCommon.hpp - see CLAUDE.md). Hidden by default -
	Rack's Widget::visible gates both drawing AND position-event hit-testing together (see
	widget/Widget.hpp's recursePositionEvent), so hidden here deliberately means both invisible
	and unpatchable at once, and shown means both visible and patchable at once - there's no
	in-between state. Uses setVisible()/show()/hide() rather than assigning ->visible directly,
	per the SDK's own doc comment on that field. Deliberately raw-pointer based (not templated
	on the module type) to match this file's existing convention (CCGridWidget/NumberWidget/
	TextWidget all take direct pointers to the relevant module members).
*/
/** Dieter's own hand-drawn 2.5x2.5mm icons (res/TouchIn.svg / TouchOut.svg), positioned by
	their top-left corner (not centered - these coordinates are exact Inkscape-derived
	placements, not a generic center point). TouchIn sits OL_TOUCH_IN_X_MM/Y_MM from the
	panel's own top-left corner; TouchOut sits OL_TOUCH_OUT_MARGIN_MM from the panel's *right*
	edge (mirroring TouchIn's left margin, accounting for the icon's own 2.5mm width) and
	OL_TOUCH_OUT_Y_MM down from the top. */
#ifndef OL_TOUCH_IN_X_MM
#define OL_TOUCH_IN_X_MM 0.611f
#endif
#ifndef OL_TOUCH_IN_Y_MM
#define OL_TOUCH_IN_Y_MM 0.763f
#endif
#ifndef OL_TOUCH_OUT_MARGIN_MM
#define OL_TOUCH_OUT_MARGIN_MM (0.611f + 2.5f)
#endif
#ifndef OL_TOUCH_OUT_Y_MM
#define OL_TOUCH_OUT_Y_MM 125.26f
#endif

struct TouchInPort : app::SvgPort {
	TouchInPort () {
		setSvg (Svg::load (asset::plugin (pluginInstance, "res/TouchIn.svg")));
	}
};
struct TouchOutPort : app::SvgPort {
	TouchOutPort () {
		setSvg (Svg::load (asset::plugin (pluginInstance, "res/TouchOut.svg")));
	}
};

inline void addOrangeLineTouchPorts (ModuleWidget *w, Module *module, int touchInId, int touchOutId,
                                      PortWidget **touchInPortOut, PortWidget **touchOutPortOut, bool *touchVisible) {
	Vec topLeft (mm2px (OL_TOUCH_IN_X_MM), mm2px (OL_TOUCH_IN_Y_MM));
	Vec bottomRight (w->box.size.x - mm2px (OL_TOUCH_OUT_MARGIN_MM), mm2px (OL_TOUCH_OUT_Y_MM));

	TouchInPort  *inPort  = createInput<TouchInPort>   (topLeft,     module, touchInId);
	TouchOutPort *outPort = createOutput<TouchOutPort> (bottomRight, module, touchOutId);
	bool visible = touchVisible ? *touchVisible : false;
	inPort->setVisible  (visible);
	outPort->setVisible (visible);
	w->addInput (inPort);
	w->addOutput (outPort);
	if (touchInPortOut)  *touchInPortOut  = inPort;
	if (touchOutPortOut) *touchOutPortOut = outPort;
}

/** For modules that already have their own equivalent of Wakeup (e.g. Mother's TRG_INPUT,
	which forces an early wake inside its own moduleSkipProcess()) - a second, redundant Wakeup
	would just be confusing, so these only get the Ready jack. Relaying still works via
	OL_touchOutRequest (see OrangeLineCommon.hpp) - the module sets that itself wherever its own
	logic decides a tick deserves a relay. */
inline void addOrangeLineTouchOutputOnly (ModuleWidget *w, Module *module, int touchOutId,
                                           PortWidget **touchOutPortOut, bool *touchVisible) {
	Vec bottomRight (w->box.size.x - mm2px (OL_TOUCH_OUT_MARGIN_MM), mm2px (OL_TOUCH_OUT_Y_MM));

	TouchOutPort *outPort = createOutput<TouchOutPort> (bottomRight, module, touchOutId);
	outPort->setVisible (touchVisible ? *touchVisible : false);
	w->addOutput (outPort);
	if (touchOutPortOut) *touchOutPortOut = outPort;
}

struct OrangeLineTouchMenuItem : MenuItem {
	PortWidget *inPort  = nullptr;
	PortWidget *outPort = nullptr;
	bool *visibleFlag   = nullptr;

	void onAction (const event::Action &e) override {
		if (!visibleFlag) return;
		*visibleFlag = !*visibleFlag;
		if (inPort)  inPort->setVisible  (*visibleFlag);
		if (outPort) outPort->setVisible (*visibleFlag);
	}
	void step () override {
		rightText = (visibleFlag && *visibleFlag) ? "✔" : "";
	}
};

inline void addOrangeLineTouchMenuItem (Menu *menu, PortWidget *inPort, PortWidget *outPort, bool *visibleFlag) {
	OrangeLineTouchMenuItem *item = new OrangeLineTouchMenuItem ();
	item->text = "Wakeup/Ready Ports";
	item->inPort = inPort;
	item->outPort = outPort;
	item->visibleFlag = visibleFlag;
	menu->addChild (item);
}

#endif
