/*
	Cron.cpp

	Code for the OrangeLine module Cron

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

#include "Cron.hpp"

struct Cron : Module
{
#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	bool widgetReady = false;

    float selectedChannel = 1.f;
   	NumberWidget *chlWidget;

    float latency = 0.f;
   	NumberWidget *latencyWidget;

    bool  running = true;
    int   sampleCount = 0;
    float halfPhaseLengthHistory[HALF_PHASE_LENGTH_HISTORY];
    int   halfPhaseLengthHistoryPos   = 0;
    int   halfPhaseLengthHistoryCount = 0;
    int   clkCount = 0;
    bool  resetOnStop = true;
    float oldClkInputVoltage = 0;

	// ********************************************************************************************************************************
	/*
		Initialization
	*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Cron()
	{
		initializeInstance();
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
	bool moduleSkipProcess() {
		bool skip = (idleSkipCounter != 0);
		if (skip) {
			float clkInputVoltage = inputs[CLK_INPUT].getVoltage (); 
			if (clkInputVoltage != oldClkInputVoltage)
				skip = false;
			oldClkInputVoltage = clkInputVoltage;
		}
		return skip;
	}
	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes()
	{
    	setStateTypeOutput(CLK_OUTPUT, STATE_TYPE_TRIGGER);
    	setStateTypeOutput(RST_OUTPUT, STATE_TYPE_TRIGGER);
        setOutPoly (CMP_OUTPUT, true);
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
        setJsonLabel(LATENCY_JSON +  0, "latency01");
        setJsonLabel(LATENCY_JSON +  1, "latency02");
        setJsonLabel(LATENCY_JSON +  2, "latency03");
        setJsonLabel(LATENCY_JSON +  3, "latency04");
        setJsonLabel(LATENCY_JSON +  4, "latency05");
        setJsonLabel(LATENCY_JSON +  5, "latency06");
        setJsonLabel(LATENCY_JSON +  6, "latency07");
        setJsonLabel(LATENCY_JSON +  7, "latency08");
        setJsonLabel(LATENCY_JSON +  8, "latency09");
        setJsonLabel(LATENCY_JSON +  9, "latency10");
        setJsonLabel(LATENCY_JSON + 10, "latency11");
        setJsonLabel(LATENCY_JSON + 11, "latency12");
        setJsonLabel(LATENCY_JSON + 12, "latency13");
        setJsonLabel(LATENCY_JSON + 13, "latency14");
        setJsonLabel(LATENCY_JSON + 14, "latency15");
        setJsonLabel(LATENCY_JSON + 15, "latency16");

#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig()
	{
        configParam (CHL_PARAM,    1.f,  POLY_CHANNELS, 1.f, "Select Channel", "", 0.f, 1.f, 0.f);
        paramQuantities[CHL_PARAM]->snapEnabled = true;
        configParam (LATENCY_PARAM,    -100.f,  100.f, 0.f, "Latency", "", 0.f, 1.f, 0.f);       
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
        for (int i = 0; i < HALF_PHASE_LENGTH_HISTORY; i++)
        {
            halfPhaseLengthHistory[i] = 0;
        }
        halfPhaseLengthHistoryCount = 0;
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
		Currently called twice when add a module to patch ...
	*/
	void moduleReset()
	{
        for (int i = 0; i < POLY_CHANNELS; i++) {
            setStateJson(LATENCY_JSON + i, 0.f);
        }

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

        if (inChangeParam (CHL_PARAM))
        {
            selectedChannel =  getStateParam(CHL_PARAM);
            setStateParam (LATENCY_PARAM, getStateJson (LATENCY_JSON + int(selectedChannel) - 1));
            latency = getStateParam(LATENCY_PARAM);
        }

        if (inChangeParam (LATENCY_PARAM))
        {
            latency = getStateParam(LATENCY_PARAM);
            setStateJson(LATENCY_JSON + int(selectedChannel) - 1, latency);
        }

        if (changeInput (START_INPUT) && getStateInput(START_INPUT) > 0.f && !running)
        {
            setStateOutput (RST_OUTPUT, 10.f);
            clkCount = 0;
            sampleCount = 0;
            running = true;
        }

        if (changeInput (CONT_INPUT) && getStateInput(CONT_INPUT) > 0.f && !running)
        {
            sampleCount = 0;
            running = true;
        }

        if (changeInput (STOP_INPUT) && getStateInput(STOP_INPUT) > 0.f)
        {
            setStateOutput (RUN_OUTPUT, 0.f);
            if (resetOnStop)
            {
                setStateOutput (RST_OUTPUT, 10.f);
            }
            running = false;
        }

        if (running)
        {
            // runlight on
            setStateOutput (RUN_OUTPUT, 10.f);
        }
        else {
            // runlight off
            setStateOutput (RUN_OUTPUT,  0.f);
        }

        if (sampleCount < INT_MAX - samplesSkipped)
        {
            sampleCount += 1 + samplesSkipped;
        }

        if (changeInput (CLKN_INPUT) && getStateInput(CLKN_INPUT) > 0.f)
        {
            clkCount = 0;
        }

        if (changeInput (CLK_INPUT) && getStateInput(CLK_INPUT) > 0.f)
        {
          // DEBUG("args.sampleTime = %f", args.sampleTime);
          // DEBUG("sampleCount = %d", sampleCount);
			float halfPhaseLength = sampleCount * 24 * args.sampleTime / 8;	// 4 phases per beat
          // DEBUG("halfPhaseLength = %f", halfPhaseLength);
          // DEBUG("halfPhaseLengthHistoryCount = %d", halfPhaseLengthHistoryCount);
          // DEBUG("halfPhaseLengthHistoryPos = %d", halfPhaseLengthHistoryPos);

            halfPhaseLengthHistory[halfPhaseLengthHistoryPos] = halfPhaseLength;
            if (halfPhaseLengthHistoryCount < HALF_PHASE_LENGTH_HISTORY)
            {
                halfPhaseLengthHistoryCount += 1;
            }
            float halfPhaseLengthSum = 0.f;
            for (int i = 0; i < halfPhaseLengthHistoryCount; i++)
            {
                halfPhaseLengthSum += halfPhaseLengthHistory[(HALF_PHASE_LENGTH_HISTORY + halfPhaseLengthHistoryPos - i) % HALF_PHASE_LENGTH_HISTORY];
            }
            halfPhaseLength = halfPhaseLengthSum / halfPhaseLengthHistoryCount;     // Beats per Minute
			// DEBUG("halfPhaseLength (flattened) = %f", halfPhaseLength);
            halfPhaseLengthHistoryPos = (halfPhaseLengthHistoryPos + 1) % HALF_PHASE_LENGTH_HISTORY;
            sampleCount = 0;

			float bpm = log2 (0.5 / (halfPhaseLength * 8));
			// DEBUG("bpm = %f", bpm);			
            setStateOutput(BPM_OUTPUT, bpm);   // Umgerechnet in PBM v/Octave

            float cmpIn = getStateInput(CMP_INPUT);
			// DEBUG("cmpIn = %f", cmpIn);

			float latencyFactor = 10 / (halfPhaseLength * 1000);
			// DEBUG("latencyFactor = %f", latencyFactor);			
            for (int channel = 0; channel < POLY_CHANNELS; channel++) {
				// DEBUG("Latency[%d] = %f", channel, getStateJson(LATENCY_JSON + channel));
                setStateOutPoly(CMP_OUTPUT, channel, (getStateJson(LATENCY_JSON + channel) * latencyFactor) + cmpIn);
            }
            setOutPolyChannels(CMP_OUTPUT, POLY_CHANNELS);

            if (running)
            {
                if (clkCount == 0)
                {
                    setStateOutput(CLK_OUTPUT, 10.f);
                }
                clkCount = (clkCount + 1) % MIDI_CLK_PER_CLK;
            }
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
	Main Module Widget
*/
struct CronWidget : ModuleWidget
{
   	char memBuffer[16];

	CronWidget(Cron *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CronOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CronBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CronDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}
        addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 2.145, 12.052, OFFSET_PJ301MPort), module, CLKN_INPUT));
        addInput (createInputCentered<PJ301MPort> (calculateCoordinates (14.845, 12.052, OFFSET_PJ301MPort), module, CLK_INPUT));
        addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 2.145, 39.992, OFFSET_PJ301MPort), module, START_INPUT));
        addInput (createInputCentered<PJ301MPort> (calculateCoordinates (14.845, 39.992, OFFSET_PJ301MPort), module, STOP_INPUT));
        addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 2.145, 53.327, OFFSET_PJ301MPort), module, CONT_INPUT));
        addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 2.145,111.112, OFFSET_PJ301MPort), module, CMP_INPUT));
	
        Trimpot *chlKnob;
        chlKnob = createParamCentered<Trimpot>(calculateCoordinates (2.184, 79.401, OFFSET_Trimpot), module, CHL_PARAM);
        chlKnob->snap = true;
        addParam (chlKnob);
        addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (8.700, 91.760, OFFSET_RoundSmallBlackKnob),  module, LATENCY_PARAM));

		float *pvalue = nullptr;
        pvalue = (module != nullptr ? &(module->selectedChannel) : nullptr);
		if (module) {
			module->chlWidget = NumberWidget::create(calculateCoordinates (17.076, 80.742 + 3.7, OFFSET_NULL), module, pvalue, 1.f, "%2.0f", memBuffer, 2);
			module->chlWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
            module->chlWidget->fontSize = 16.f;
			addChild(module->chlWidget);
		}
		else {
			NumberWidget *w = NumberWidget::create(calculateCoordinates (17.076, 80.742 + 3.7, OFFSET_NULL), module, pvalue, 1.f, "%2.0f", memBuffer, 2);
			w->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
			addChild(w);
		}

        pvalue = (module != nullptr ? &(module->latency) : nullptr);
		if (module) {
			module->latencyWidget = NumberWidget::create(calculateCoordinates (2.856 - 3.7, 102.337 + 3.7, OFFSET_NULL), module, pvalue, 1.f, "%+ 8.2f", memBuffer, 8);
			module->latencyWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
            module->latencyWidget->fontSize = 16.f;
			addChild(module->latencyWidget);
		}
		else {
			NumberWidget *w = NumberWidget::create(calculateCoordinates (2.856 - 3.7, 102.337 + 3.7, OFFSET_NULL), module, pvalue, 1.f, "%+ 8.2f", memBuffer, 8);
			w->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
			addChild(w);
		}

        addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates ( 2.145, 24.752, OFFSET_PJ301MPort),  module, BPM_OUTPUT));
        addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (14.845, 24.752, OFFSET_PJ301MPort),  module, CLK_OUTPUT));
        addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates ( 2.145, 66.662, OFFSET_PJ301MPort),  module, RST_OUTPUT));
        addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (14.845, 66.662, OFFSET_PJ301MPort),  module, RUN_OUTPUT));
        addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (14.845,111.112, OFFSET_PJ301MPort),  module, CMP_OUTPUT));

		addOrangeLineTouchPorts (this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		if (module)
			module->widgetReady = true;
	}

	struct CronStyleItem : MenuItem
	{
		Cron *module;
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

		Cron *module = dynamic_cast<Cron *>(this->module);
		assert(module);

		addOrangeLineTouchMenuItem(menu, module->OL_touchInPort, module->OL_touchOutPort, &module->OL_touchVisible);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		CronStyleItem *style1Item = new CronStyleItem();
		style1Item->text = "Orange"; //
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		CronStyleItem *style2Item = new CronStyleItem();
		style2Item->text = "Bright"; //
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		CronStyleItem *style3Item = new CronStyleItem();
		style3Item->text = "Dark"; //
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelCron = createModel<Cron, CronWidget>("Cron");
