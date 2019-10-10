#include <string>
#include <stdio.h>

#include "plugin.hpp"

#define RANGE_MIN -10
#define RANGE_MAX  10

#define MIN_VOCTSTEP  0.001f

#define TYPE_VOCT 1
#define TYPE_STEP 2

#define MODE_NORMAL false
#define MODE_AUDIO  true

#define AUDIO_VOLTAGE  5.f

//
// Trigger copied from ImpromptuMopdular
//
struct Trigger : dsp::SchmittTrigger {
	// implements a 0.1V - 1.0V SchmittTrigger (see include/dsp/digital.hpp) instead of
	//   calling SchmittTriggerInstance.process(math::rescale(in, 0.1f, 1.f, 0.f, 1.f))
	bool process(float in) {
		if (state) {
			// HIGH to LOW
			if (in <= 0.1f) {
				state = false;
			}
		}
		else {
			// LOW to HIGH
			if (in >= 1.0f) {
				state = true;
				return true;
			}
		}
		return false;
	}
};

struct Fence : Module {
	//
	// Module defaults
	//
	static constexpr float defaultHigh = 11. / 12;
	static constexpr float defaultLow  = 0;
	static constexpr float defaultLinked = true;
	static constexpr float defaultQuantized = true;
	static constexpr float defaultMode = MODE_NORMAL;

	static constexpr float defaultVOctStep = 1.;
	static constexpr float defaultQtzStep = 0.;
	static constexpr float defaultStep = defaultQtzStep;

	// 
	// Static function to quantize a cv to a semitone value
	//
	static float quantize (float cv) {
		int cvX12  = round (cv * 12);
		return float (cvX12 % 12) / 12 + cvX12 / 12;
	}

	// 
	// Static function to get the note (quantized pitch modulo 1)
	//
	static float note (float cv) {
		int cvX12  = round (cv * 12);
		return float (cvX12 % 12) / 12;
	}

	//
	// VCV Component Ids
	//
	enum ParamIds {
		H_INITIALIZED_PARAM,
		H_QUANTIZED_PARAM,
		H_MODE_PARAM,
		H_LINKED_PARAM,
		H_VOCTSTEP_PARAM,
		H_QTZSTEP_PARAM,
		HIGH_PARAM,
		LOW_PARAM,
		LINK_PARAM,
		QUANTIZE_PARAM,
		MODE_PARAM,
		STEP_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		HIGH_INPUT,
		LOW_INPUT,
		CV_INPUT,
		STEP_INPUT,
		TRG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CV_OUTPUT,
		TRG_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		QUANTIZE_LIGHT,
		MODE_LIGHT,
		LINK_LIGHT,
		NUM_LIGHTS
	};
	//
	// Module State
	//
	bool  mode = defaultMode;
	bool  oldMode = mode;
	bool  modeHold = false;

	bool  quantized = defaultQuantized;
	bool  oldQuantized = quantized;
	bool  quantizedHold = false;

	bool  linked = defaultLinked;
	bool  oldLinked = linked;
	bool  linkedHold = false;

	float high = quantized ? Fence::quantize (defaultHigh) : defaultHigh;
	float oldHigh = high;

	float low = quantized ? Fence::quantize (defaultLow)  : defaultLow;
	float oldLow = low;

	float vOctStep = defaultVOctStep;
	float qtzStep = defaultQtzStep;
	float step = defaultStep;
	float oldStep = step;
	float effectiveStep = step;
	float oldEffectiveStep = effectiveStep;

	float cvIn = 0;
	float oldCvIn = 0;

	float cvOut = 0;
	float oldCvOut = 0;

	float linkDelta = high - low;

	bool  initialized = false;

	Trigger trgIn;
	dsp::PulseGenerator trgOutPulse;
	double sampleTime;

	//
	// Constructor
	//
	Fence () {
		config (NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		//
		// Hidden Memory
		//
		configParam (H_INITIALIZED_PARAM,   0.f,     1.f, 0.f,    "");
		configParam (  H_QUANTIZED_PARAM,   0.f,     1.f, 0.f,    "");
		configParam (     H_LINKED_PARAM,   0.f,     1.f, 0.f,    "");
		configParam (       H_MODE_PARAM,   0.f,     1.f, 0.f,    "");
		configParam (    H_QTZSTEP_PARAM,   0.f,     1.f, 0.916f, "");
		configParam (   H_VOCTSTEP_PARAM,   MIN_VOCTSTEP, 10.f, 1.f,    "");
		//
		// GUI Parameters
		//
		configParam (          LOW_PARAM,        -10.f, 10.f,    0.f, "Lower Bound");
		configParam (         HIGH_PARAM,        -10.f, 10.f, 0.916f, "Upper Bound");
		configParam (         STEP_PARAM, MIN_VOCTSTEP, 10.f,    1.f, "Step");
		configParam (     QUANTIZE_PARAM,          0.f,  1.f,    0.f, "Toggle Quantize On/Off");
		configParam (         LINK_PARAM,          0.f,  1.f,    0.f, "Toggle Link Range On/Off");
		configParam (         MODE_PARAM,          0.f,  1.f,    0.f, "Change Mode");
	}

	//
	// Change Range Knobs Value Range depending on Mode
	//
	void setRange() {
		float tmp;
		if (mode == MODE_AUDIO) {
			if (high > AUDIO_VOLTAGE) {
				high = AUDIO_VOLTAGE;
				params[HIGH_PARAM].setValue (high);
			}
			tmp = high;
			configParam (HIGH_PARAM, -AUDIO_VOLTAGE, AUDIO_VOLTAGE, AUDIO_VOLTAGE, "High");
			params[HIGH_PARAM].setValue (tmp);

			if (low < -AUDIO_VOLTAGE) {
				low = -AUDIO_VOLTAGE;
				params[LOW_PARAM].setValue (low);
			}
			tmp = low;
			configParam (LOW_PARAM, -AUDIO_VOLTAGE, AUDIO_VOLTAGE, -AUDIO_VOLTAGE, "Low");
			params[LOW_PARAM].setValue (tmp);
		}
		else {
			tmp = low;
			params[LOW_PARAM].setValue (tmp-0.001f);
			configParam (LOW_PARAM,  -10.f, 10.f,    0.f, "Lower Bound");
			params[LOW_PARAM].setValue (tmp);
			tmp = high;
			params[HIGH_PARAM].setValue (tmp-0.001f);
			if (quantized)
				configParam (HIGH_PARAM,  -10.f, 10.f, 0.916f, "Upper Bound");
			else
				configParam (HIGH_PARAM,  -10.f, 10.f, 1.f, "Upper Bound");
			params[HIGH_PARAM].setValue (tmp);
		}
	}

	//
	// Process Step
	//
	void process (const ProcessArgs &args) override {
		bool highChanged      = false;
		bool lowChanged       = false;
		bool quantizedChanged = false;
		bool linkedChanged    = false;
		bool stepChanged      = false;
		bool modeChanged      = false;

		sampleTime = 1.0 / (double)(APP->engine->getSampleRate());

		//
		// Initialize Module Params when added to patch
		//
		if (params[H_INITIALIZED_PARAM].getValue () == 0) {
			params[HIGH_PARAM].setValue (high);
			highChanged = true;
			params[LOW_PARAM].setValue (low);
			lowChanged = true;
			params[STEP_PARAM].setValue (step);
			stepChanged = true;
			lights[QUANTIZE_LIGHT].value = quantized ? 1. : 0.;
			quantizedChanged = true;
			lights[LINK_LIGHT].value = linked ? 1. : 0.;
			linkedChanged = true;
			params[H_QUANTIZED_PARAM].setValue (quantized);
			params[H_LINKED_PARAM].setValue (linked);
			params[H_QTZSTEP_PARAM].setValue (qtzStep);
			params[H_VOCTSTEP_PARAM].setValue (vOctStep);
			params[H_MODE_PARAM].setValue (mode);

			params[H_INITIALIZED_PARAM].setValue (1);
		}

		//
		// Initialize Module after add to patch or reload
		//
		if (!initialized) {
			mode = oldMode = params[H_MODE_PARAM].getValue ();
			modeChanged = true;

			quantized = oldQuantized = params[H_QUANTIZED_PARAM].getValue ();
			quantizedChanged = true;

			setRange();

			if (quantized)
				oldStep = step = qtzStep = params[H_QTZSTEP_PARAM].getValue ();
			else
				oldStep = step = vOctStep = params[H_VOCTSTEP_PARAM].getValue ();
			stepChanged = true;

			linked = oldLinked = params[H_LINKED_PARAM].getValue ();
			linkedChanged = true;

			initialized = true;
		}


		//
		// Handle Mode Switch
		// Implicitly disabeling quantized mode
		//
		if (params[MODE_PARAM].getValue () == 1) {
			if (!modeHold) {
				if (quantized) {
					quantized = false;
					lights[QUANTIZE_LIGHT].value = 0;
					params[H_QUANTIZED_PARAM].setValue (quantized);
					configParam (STEP_PARAM, MIN_VOCTSTEP, 10.f, 1.f, "Step");
				}
				mode = !mode;
				params[H_MODE_PARAM].setValue (mode);
				modeChanged = true;
				modeHold = true;
			}
		}
		else
			modeHold = false;

		if (modeChanged) {
			if (mode) {
				lights[MODE_LIGHT].value = 1;

			}
			else {
			 	lights[MODE_LIGHT].value = 0;
			}
			params[H_MODE_PARAM].setValue(mode);
			setRange();
			modeChanged = true;
		}
		//
		//
		// Handle Quantize Switch
		// Implicitly disabeling audio mode
		//
		if (params[QUANTIZE_PARAM].getValue () == 1) {
			if (!quantizedHold) {
				if (mode == MODE_AUDIO) {
					mode = MODE_NORMAL;
					lights[MODE_LIGHT].value = 0;
					params[H_MODE_PARAM].setValue (mode);
				}
				quantized = !quantized;
				params[H_QUANTIZED_PARAM].setValue (quantized);
				quantizedChanged = true;
				quantizedHold = true;
			}
		}
		else
			quantizedHold = false;

		if (quantizedChanged) {
			if (quantized) {
				oldStep = step = qtzStep;
				configParam (STEP_PARAM, 0.f, (1.f / 12) * 11, 0.f, "Step");
				lights[QUANTIZE_LIGHT].value = 1;
				low  = quantize (low);
				high = quantize (high);
			}
			else {
				oldStep = step = vOctStep;
				configParam (STEP_PARAM, MIN_VOCTSTEP, 10.f, 1.f, "Step");
			 	lights[QUANTIZE_LIGHT].value = 0;
				low  = params[LOW_PARAM].getValue ();
				high = params[LOW_PARAM].getValue ();
			}
			params[STEP_PARAM].setValue(step);
			setRange();
			stepChanged = true;
		}

		//
		// Get high from knob
		//
		high = params[HIGH_PARAM].getValue ();

		//
		// Add high input if connected
		//
		if (inputs[HIGH_INPUT].isConnected ())
			high += inputs[HIGH_INPUT].getVoltage ();

		//
		// Handle high change
		//
		if (quantized)
			high = quantize (high);

		if (high != oldHigh) {
			if (linked) {
				low = high - linkDelta;

				if (quantized)
					low = quantize(low);

				if (mode == MODE_AUDIO) {
					if (low < -AUDIO_VOLTAGE)
						low = -AUDIO_VOLTAGE;
					else if (low > AUDIO_VOLTAGE)
						low = AUDIO_VOLTAGE;
				}
				else {
					if (low < RANGE_MIN)
						low = RANGE_MIN;
					else if (low > RANGE_MAX)
						low = RANGE_MAX;
				}

				params[LOW_PARAM].setValue (low);
			}
			else {
				if (high < low) {
					low = high;
					params[LOW_PARAM].setValue (low);
				}
			}
			oldHigh = high;
			highChanged = true;
		}

		//
		// Get low from knob
		//
		low = params[LOW_PARAM].getValue();

		//
		// Add low input if connected
		//
		if (inputs[LOW_INPUT].isConnected ())
			low += inputs[LOW_INPUT].getVoltage ();

		//
		// Handle low change
		//
		if (quantized)
			low = quantize (low);

		if (low != oldLow) {
			if (linked) {
				high = low + linkDelta;

				if (quantized)
					high = quantize(high);

				if (mode == MODE_AUDIO) {
					if (high < -AUDIO_VOLTAGE)
						high = -AUDIO_VOLTAGE;
					else if (high > AUDIO_VOLTAGE)
						high = AUDIO_VOLTAGE;
				}
				else {
					if (high < RANGE_MIN)
						high = RANGE_MIN;
					else if (high > RANGE_MAX)
						high = RANGE_MAX;
				}

				params[HIGH_PARAM].setValue (high);
			}
			else {
				if (low > high) {
					high = low;
					params[HIGH_PARAM].setValue (high);
				}
			}
			oldLow = low;
			lowChanged = true;
		}

		//
		// Handle Link Switch
		//
		if (params[LINK_PARAM].getValue () == 1) {
			if (!linkedHold) {
				linked = !linked;
				params[H_LINKED_PARAM].setValue (linked);
				linkedChanged = true;
				linkedHold = true;
			}
		}
		else
			linkedHold = false;

		if (linkedChanged) {
			if (linked) {
				lights[LINK_LIGHT].value = 1;
				linkDelta = high - low;
			}
			else {
			 	lights[LINK_LIGHT].value = 0;
			}
		}

		//
		// Get step from knob
		//
		step = params[STEP_PARAM].getValue();

		//
		// Handle step change
		//
		if (quantized) {
			qtzStep = step = note (step);
			if (step != oldStep)
				params[H_QTZSTEP_PARAM].setValue (qtzStep);
		}
		else {
			vOctStep = step;
			if (step != oldStep)
				params[H_VOCTSTEP_PARAM].setValue (vOctStep);
		}
		effectiveStep = step;

		//
		// knob setting is ignored by overwriting effectiveStep, if step cv in is connected
		//
		if (inputs[STEP_INPUT].isConnected ()) {
			effectiveStep = inputs[STEP_INPUT].getVoltage ();
			if (quantized)
				effectiveStep = note (effectiveStep); 
			else
				if (effectiveStep < MIN_VOCTSTEP)
					effectiveStep = MIN_VOCTSTEP;
		}
		if (effectiveStep != oldEffectiveStep) {
			oldEffectiveStep = effectiveStep;
			stepChanged = true;
		}
		if (effectiveStep <= 0)
			effectiveStep = MIN_VOCTSTEP;

		//
		// Process cvIn
		//
		//
		// Check for Input Trigger 
		//
		bool trgInConnected = inputs[TRG_INPUT].isConnected ();
		bool gotInTrg = false;
		if (trgInConnected) {
		       	if ((gotInTrg = trgIn.process(inputs[TRG_INPUT].getVoltage()))) 
				cvIn = inputs[CV_INPUT].getVoltage ();
		}
		else {
			cvIn = inputs[CV_INPUT].getVoltage ();
		}
		if (cvIn != oldCvIn || highChanged || lowChanged || quantizedChanged || stepChanged || modeChanged) {
			cvOut = cvIn;
			float effectiveLow = low;
			float effectiveHigh = high;
			float realStep = effectiveStep;

			if (quantized)
				cvOut = quantize (cvIn);

			//
			// In audio mode we make sure that step is <= range so we will always be in range
			// low and high is clamped to +-AUDIO_VOLTAGE 
			//
			float absRange = 0;
			if (mode == MODE_AUDIO) {
				if (effectiveLow < -AUDIO_VOLTAGE)
					effectiveLow = -AUDIO_VOLTAGE;
				if (effectiveLow > AUDIO_VOLTAGE)
					effectiveLow = AUDIO_VOLTAGE;
				if (effectiveHigh < -AUDIO_VOLTAGE)
					effectiveHigh = -AUDIO_VOLTAGE;
				if (effectiveHigh > AUDIO_VOLTAGE)
					effectiveHigh = AUDIO_VOLTAGE;

				absRange = effectiveHigh - effectiveLow;
				if (absRange < 0)
					absRange *= -1.f;
				if (absRange < 2 * MIN_VOCTSTEP) {
					absRange = 2 * MIN_VOCTSTEP;
					effectiveLow  -= MIN_VOCTSTEP;
					effectiveHigh += MIN_VOCTSTEP;
				}
				if (realStep > absRange)
					realStep = absRange;
			}

			if (cvOut > effectiveHigh) {
				if (quantized) {
					cvOut -= floor(cvOut - effectiveHigh);
					if (cvOut > effectiveHigh)
						cvOut -= 1.;
				}
				else {
					cvOut -= floor((cvOut - effectiveHigh) / realStep) * realStep;
					if (cvOut > effectiveHigh)
						cvOut -= realStep;
				}
			}
			if (cvOut < effectiveLow) {
				if (quantized) {
					cvOut += floor(effectiveLow - cvOut);
					if (cvOut < effectiveLow)
						cvOut += 1.;
				}
				else {
					cvOut += floor((effectiveLow - cvOut) / realStep) * realStep;
					if (cvOut < effectiveLow)
						cvOut += realStep;
				}
			}

			if (quantized && cvOut > effectiveHigh) {
				//
				// We didn't find the same note in our range
				// Now we check whether cvIn + effectiveStep would match
				//
				float altCv = cvOut - 1 + effectiveStep;
				// printf("effectiveLow = %f, altCv = %f\n",effectiveLow,altCv);
				if (altCv >= effectiveLow && altCv <= effectiveHigh)
					cvOut = altCv;
				else
					//
					// Alternative note does note match also
					// 
					if (cvIn > effectiveHigh && altCv > effectiveHigh && altCv < cvIn)
						cvOut = altCv;
			}

			//
			// In audio mode we rescale the output so that range is mapped to -5, +5V
			//
			if (mode == MODE_AUDIO) {
				cvOut -=  effectiveHigh - absRange / 2.f;
				cvOut *= 2 * AUDIO_VOLTAGE / absRange;
			}

			if (!trgInConnected || gotInTrg) {
				//
				// Set cvOut
				//
				if (cvOut != oldCvOut) {
					//
					// Send a trigger on change of cv out
					//
					trgOutPulse.trigger(0.001f);
					outputs[CV_OUTPUT].setVoltage (cvOut);
					oldCvOut = cvOut;
				}
				oldCvIn = cvIn;
			}
		}
		//
		// Trigger 
		//
		outputs[TRG_OUTPUT].setVoltage(trgOutPulse.process((float)sampleTime) ? 10.0f : 0.0f);
	}
};

//
// Widget to display cvOct values as floats or notes
//
struct VOctWidget : TransparentWidget {

	static constexpr const char*  notes = "CCDDEFFGGAAB";
	static constexpr const char* sharps = " # #  # # # ";

	std::shared_ptr<Font> pFont;

	float *pValue = NULL;
	float  defaultValue = 0;
	bool  *pQuantized = NULL;
	bool   defaultQuantized = true;
	char   str[8]; // Space for 7 Chars
	int    type = TYPE_VOCT;
	//
	// Constructor
	//
	VOctWidget() {
		box.size = Vec(55, 80);
		pFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
	}
	//
	// Static function to convert a cv to a string and writes it to the
	// pointer address given by pStr.
	//
	// Make sure that there is at least 8 Bytes allocated to the string at this address !!!
	//
	// If noteMode is true, the cv will be quantized to the next semitone and displayed as note value
	// else the cv is displayed as float with a maximum of 7 chars in the format '-99.999'
	// If not in the range [-10,10] the string 'ERROR' will be
	//
	static char* cv2Str (char *pStr, float cv, bool quantized, int type) {
		if (cv < -10. || cv > 10.) {
			strcpy (pStr, "ERROR");
		}
		else {
			if (quantized) {
				if (type == TYPE_VOCT) {
					float octave = floor (cv);
					int note = round ((cv - octave) * 12);
					sprintf (pStr, " %c%c%2.0f", notes[note], sharps[note], octave + 4);
				}
				if (type == TYPE_STEP) {
					int semis = Fence::note(cv) * 12;
					sprintf (pStr, "%2d ST", semis);
				}
			}
			else {
				sprintf (pStr, "% *.3f", 7, cv);
			}
		}
		return pStr;
	}
	void draw (const DrawArgs &drawArgs) override {
		nvgFontFaceId (drawArgs.vg, pFont->handle);
		nvgFontSize (drawArgs.vg, 18);
		nvgFillColor (drawArgs.vg, nvgRGB(255, 102, 0)); // Orange

		float    value = pValue     != NULL ? *pValue     : defaultValue;
		bool quantized = pQuantized != NULL ? *pQuantized : defaultQuantized;

		float xOffset = 0;
		if (quantized) {
			xOffset = mm2px (2.25);
		}

		nvgText (drawArgs.vg, xOffset, 0, cv2Str (str, value, quantized, type), NULL);
	}
};

struct FenceWidget : ModuleWidget {

	//
	// create and initialize a note display widget
	//
	static VOctWidget* createVOctWidget(Vec pos, float *pValue, float defaultValue, bool *pQuantized, bool defaultQuantized, int type) {
		VOctWidget *w = new VOctWidget ();

		w->box.pos = pos;
		w->pValue = pValue;
		w->defaultValue = defaultValue;
		w->pQuantized = pQuantized;
		w->defaultQuantized = defaultQuantized;
		w->type = type;

		return w;
	}

	FenceWidget(Fence *module) {

		setModule (module);
		setPanel (APP->window->loadSvg(asset::plugin (pluginInstance, "res/Fence.svg")));

		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec (17.246 + 5,    128.5 - 92.970 - 5)),    module, Fence::HIGH_PARAM));
		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec ( 3.276 + 5,    128.5 - 92.970 - 5)),    module, Fence::LOW_PARAM));
		addParam (createParamCentered<RoundBlackKnob>		(mm2px (Vec ( 3.276 + 5,    128.5 - 57.568 - 5)),    module, Fence::STEP_PARAM));

		float *pHighValue = module != NULL ? &(module->high)       : NULL;
		float *pLowValue  = module != NULL ? &(module->low)        : NULL;
		bool  *pQuantized = module != NULL ? &(module->quantized)  : NULL;
		float *pStepValue = module != NULL ? &(module->step)       : NULL;

		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 - 113.252 - 0.25 )), pHighValue, Fence::defaultHigh, pQuantized, Fence::defaultQuantized, TYPE_VOCT));
		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 - 106.267 - 0.25 )), pLowValue,  Fence::defaultLow,  pQuantized, Fence::defaultQuantized, TYPE_VOCT));
		addChild (FenceWidget::createVOctWidget (mm2px (Vec(5.09 - 2, 128.5 -  71.267 + 0.25 )), pStepValue, Fence::defaultStep, pQuantized, Fence::defaultQuantized, TYPE_STEP));

		addParam (createParamCentered<LEDButton>		(mm2px (Vec (12.858 + 2.38, 128.5 - 88.900 - 2.38)), module, Fence::LINK_PARAM));
 		addChild (createLightCentered<LargeLight<GreenLight>>	(mm2px (Vec (12.858 + 2.38, 128.5 - 88.900 - 2.38)), module, Fence::LINK_LIGHT));
		
		addParam (createParamCentered<LEDButton>		(mm2px (Vec (20.638 + 2.38, 128.5 - 56.525 - 2.38)), module, Fence::QUANTIZE_PARAM));
 		addChild (createLightCentered<LargeLight<GreenLight>>	(mm2px (Vec (20.638 + 2.38, 128.5 - 56.525 - 2.38)), module, Fence::QUANTIZE_LIGHT));
		
		addParam (createParamCentered<LEDButton>		(mm2px (Vec (20.638 + 2.38, 128.5 - 48.577 - 2.38)), module, Fence::MODE_PARAM));
 		addChild (createLightCentered<LargeLight<GreenLight>>	(mm2px (Vec (20.638 + 2.38, 128.5 - 48.577 - 2.38)), module, Fence::MODE_LIGHT));

		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.049 + 4.2 , 128.5 - 82.947 - 4.2)),  module, Fence::LOW_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec (18.019 + 4.2 , 128.5 - 82.947 - 4.2)),  module, Fence::HIGH_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 47.547 - 4.2)),  module, Fence::STEP_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 29.609 - 4.2)),  module, Fence::TRG_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 4.05  + 4.2 , 128.5 - 11.829 - 4.2)),  module, Fence::CV_INPUT));

		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (18.02  + 4.2 , 128.5 - 29.609 - 4.2)),  module, Fence::TRG_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (18.02  + 4.2 , 128.5 - 11.829 - 4.2)),  module, Fence::CV_OUTPUT));
	}
};

Model *modelFence = createModel<Fence, FenceWidget>("Fence");
