/*
	Buckets.cpp

	Code for the OrangeLine module Buckets

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
#include <string>
#include <stdio.h>
#include <limits.h>

#include "Buckets.hpp"

struct Buckets : Module
{

#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	bool widgetReady = false;

	// ********************************************************************************************************************************
	/*
		Initialization
	*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Buckets()
	{
		initializeInstance();
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
	bool moduleSkipProcess() {
		bool skip = (idleSkipCounter != 0);
		return skip;
	}
	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes()
	{
        setInPoly (VOCT_INPUT, true);
        setInPoly (VELOCITY_INPUT, true);
        setInPoly (GATE_INPUT, true);

        for (int i = 0; i <= NUM_SPLITS; i++)   // >= because of additional > output
		{
            setOutPoly(VOCT_OUTPUT + i, true);
            setOutPoly(VELOCITY_OUTPUT + i, true);
            setOutPoly(GATE_OUTPUT + i, true);
        }
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		//
		// Config internal Parameters not bound to a user interface object
		//

		setJsonLabel(STYLE_JSON, "style");

#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig()
	{
        configInput (VOCT_INPUT, "Polyphonic V/Oct");
        configInput (VELOCITY_INPUT, "Aux");
        configInput (GATE_INPUT, "Polyphonic Gate");

		char buffer[64];
        for (int i = 0; i < NUM_SPLITS; i++)
		{
            sprintf(buffer, "Split Point %d", i + 1);
            configParam (SPLIT_PARAM + i, -5.f,  (5.f - SEMITONE),  0.f, buffer, "", 0.f, 1.f, 0.f);
            sprintf(buffer, "V/Oct <= Split Point %d", i + 1);
            configOutput (VOCT_OUTPUT + i, buffer);
            sprintf(buffer, "Aux <= Split Point %d", i + 1);
            configOutput (VELOCITY_OUTPUT + i, buffer);
            sprintf(buffer, "Gates <= Split Point %d", i + 1);
            configOutput (GATE_OUTPUT + i, buffer);
		}
        configOutput (VOCT_OUTPUT_LAST, "V/Oct > Split Point 12");
        configOutput (VELOCITY_OUTPUT_LAST, "Aux > Split Point 12");
        configOutput (GATE_OUTPUT_LAST, "Gates > Split Point 12");
	}

	inline void moduleCustomInitialize()
	{
	}

	/**
		Method to initialize the module after loading a patch or a preset
		Called from initialize () included from from OrangeLineCommon.hpp
		to initialize module state from a valid
		json state after module was added to the patch,
		a call to dataFromJson due to patch or preset load
		or a right click initialize (reset).
	*/
	inline void moduleInitialize()
	{
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
		Currently called twice when add a module to patch ...
	*/
	void moduleReset()
	{
		styleChanged = true;
	}

	// ********************************************************************************************************************************
	/*
		Module specific utility methods
	*/

	// ********************************************************************************************************************************
	/*
		Methods called directly or indirectly called from process () in OrangeLineCommon.hpp
	*/
	/**
		Module specific process method called from process () in OrangeLineCommon.hpp
	*/
	inline void moduleProcess(const ProcessArgs &args)
	{
        int outputChannels[NUM_SPLITS + 1]; // + 1 because of the chain output
        memset (outputChannels, 0, sizeof(outputChannels));

		if (styleChanged && widgetReady)
		{
			switch (int(getStateJson(STYLE_JSON)))
			{
			case STYLE_ORANGE:
				brightPanel->visible = false;
				darkPanel->visible = false;
				break;
			case STYLE_BRIGHT:
				brightPanel->visible = true;
				darkPanel->visible = false;
				break;
			case STYLE_DARK:
				brightPanel->visible = false;
				darkPanel->visible = true;
				break;
			}
			styleChanged = false;
		}
        int voctInputChannels     = inputs[VOCT_INPUT].getChannels();
        int gateInputChannels     = inputs[GATE_INPUT].getChannels();
        int velocityInputChannels = inputs[VELOCITY_INPUT].getChannels();
        if (voctInputChannels < gateInputChannels)
        {
            gateInputChannels = voctInputChannels;
        }
        if (velocityInputChannels < gateInputChannels)
        {
            gateInputChannels = velocityInputChannels;
        }
        for (int channel = 0; channel < gateInputChannels; channel++)
        {
            if (OL_statePoly[GATE_INPUT * POLY_CHANNELS + channel] > 5.f)
            {
                float vOct = quantize(OL_statePoly[VOCT_INPUT * POLY_CHANNELS + channel]);
                float velocity = OL_statePoly[VELOCITY_INPUT * POLY_CHANNELS + channel];
                bool belowSplit = false;
                for (int split = 0; split < NUM_SPLITS; split++)
                {
                    float splitPoint = quantize(getStateParam(SPLIT_PARAM + split));
                    if (vOct <= splitPoint) {
                        setStateOutPoly(VOCT_OUTPUT + split, outputChannels[split], vOct);
                        setStateOutPoly(VELOCITY_OUTPUT + split, outputChannels[split], velocity);
                        setStateOutPoly(GATE_OUTPUT + split, outputChannels[split], 10.f);
                        outputChannels[split]++;
                        belowSplit = true;
                        break;
                    }
                }
                if (!belowSplit)
                {
                        setStateOutPoly(VOCT_OUTPUT + NUM_SPLITS, outputChannels[NUM_SPLITS], vOct);
                        setStateOutPoly(VELOCITY_OUTPUT + NUM_SPLITS, outputChannels[NUM_SPLITS], velocity);
                        setStateOutPoly(GATE_OUTPUT + NUM_SPLITS, outputChannels[NUM_SPLITS], 10.f);
                        outputChannels[NUM_SPLITS]++;
                }
            }
        }
        for (int output = 0; output <= NUM_SPLITS; output ++) // <= because of las cv out for greater last split
        {
            setOutPolyChannels(VOCT_OUTPUT + output, outputChannels[output]);
            setOutPolyChannels(VELOCITY_OUTPUT + output, outputChannels[output]);
            setOutPolyChannels(GATE_OUTPUT + output, outputChannels[output]);
        }
	}

	/**
		Module specific input processing called from process () in OrangeLineCommon.hpp
		right after generic processParamsAndInputs ()

		moduleProcessState () should only be used to derive json state and module member variables
		from params and inputs collected by processParamsAndInputs ().

		This method should not do dsp or other logic processing.
	*/
	inline void moduleProcessState()
	{
	}

	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges()
	{
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/
/**
	Widget to display cvOct values as floats or notes
*/
struct SplitWidget : TransparentWidget {

	int  idx = 0;;
	char str[16]; // Space for 12 Chars (need 4 only but compiler complained)

	Buckets *module;

	/**
		Constructor
	*/
	SplitWidget(int pIdx) {
        idx = pIdx;
		box.size = mm2px (Vec(26, 7));
		module = nullptr;
	}

	//
	// create and initialize a note display widget
	//
	static SplitWidget* createSplitWidget(Vec pos, int idx,  Buckets *module) {
		SplitWidget *w = new SplitWidget (idx);
		w->box.pos = pos;
		w->module = module;
		return w;
	}

	void drawLayer (const DrawArgs &drawArgs, int layer) override {
        if (module) {
			constexpr const char* notes = "CdDeEFgGaAbB";

            float cv = module->OL_state[stateIdxParam (SPLIT_PARAM + idx)];
            int note = note (cv);
            int octave = octave (cv) + 4;
			char octChar = '?';
			if (octave < 0) {
				octChar = 'L';
			}
			else if (octave > 9) {
				octChar = 'H';
			}
			else {
				octChar = char('0' + octave);
			}
            snprintf (str, sizeof(str) - 1, "%c %c", notes[note], octChar);
        }
        else {
            strncpy (str, "C 0", sizeof(str));
        }

		if (layer != 1) {
			Widget::drawLayer(drawArgs, layer);
			return;
		}
		std::shared_ptr<Font> pFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId (drawArgs.vg, pFont->handle);
		nvgFontSize (drawArgs.vg, 18);
		nvgFillColor (drawArgs.vg, (module != nullptr ? module->getTextColor () : ORANGE));        
		nvgText (drawArgs.vg, 0, 0, str, NULL);
		Widget::drawLayer(drawArgs, 1);
	}
};

/**
	Main Module Widget
*/
struct BucketsWidget : ModuleWidget
{
	BucketsWidget(Buckets *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BucketsOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BucketsBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BucketsDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}
        for (int i = 0; i < NUM_SPLITS; i++)
		{
            addChild (SplitWidget::createSplitWidget (calculateCoordinates (11.2, 10.656 + 4.1 + i * (18.187 - 9.551), OFFSET_NULL), i, module));
		}
		
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (1.129, 112.128, OFFSET_PJ301MPort),  module, VOCT_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (5.447, 112.128, OFFSET_PJ301MPort),  module, VELOCITY_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (9.765, 112.128, OFFSET_PJ301MPort),  module, GATE_INPUT));

        for (int i = 0; i < NUM_SPLITS; i++)
		{
			addParam(createParamCentered<Trimpot>(calculateCoordinates (2.184, 9.551 + i * (18.187 - 9.551), OFFSET_Trimpot), module, SPLIT_PARAM + i));
		}

        for (int i = 0; i <= NUM_SPLITS; i++)
		{
            addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (27.545, 8.496 + i * (18.187 - 9.551), OFFSET_PJ301MPort),  module, VOCT_OUTPUT + i));
            addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (31.863, 8.496 + i * (18.187 - 9.551), OFFSET_PJ301MPort),  module, VELOCITY_OUTPUT + i));
            addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (36.181, 8.496 + i * (18.187 - 9.551), OFFSET_PJ301MPort),  module, GATE_OUTPUT + i));
		}
		

		if (module)
			module->widgetReady = true;
	}

	struct BucketsStyleItem : MenuItem
	{
		Buckets *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Buckets *module = dynamic_cast<Buckets *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		BucketsStyleItem *style1Item = new BucketsStyleItem();
		style1Item->text = "Orange"; //
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		BucketsStyleItem *style2Item = new BucketsStyleItem();
		style2Item->text = "Bright"; //
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		BucketsStyleItem *style3Item = new BucketsStyleItem();
		style3Item->text = "Dark"; //
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelBuckets = createModel<Buckets, BucketsWidget>("Buckets");
