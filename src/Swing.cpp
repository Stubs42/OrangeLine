/*
	Swing.cpp
 
	Code for the OrangeLine module Swing

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

#include "Swing.hpp"

struct Swing : Module {

	#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
    float   phase = 0;
    float   phaseStep = 0;
    float   clkMultCnt = 0;
    bool    eClkFired = false;
    int     tPos = -1;
    float   cmp = 0;
    bool    tClkFired = true;
	int		tClkDelay = -1;
	bool  	widgetReady = false;

// ********************************************************************************************************************************
/*
	Initialization
*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Swing () {
		initializeInstance ();
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
	void moduleInitStateTypes () {
     	setStateTypeParam  (  RST_PARAM, STATE_TYPE_TRIGGER);

     	setStateTypeInput  (  CLK_INPUT, STATE_TYPE_TRIGGER);
     	setStateTypeInput  (  RST_INPUT, STATE_TYPE_TRIGGER);

     	setStateTypeOutput (ECLK_OUTPUT, STATE_TYPE_TRIGGER);
     	setStateTypeOutput (TCLK_OUTPUT, STATE_TYPE_TRIGGER);
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig () {

		#pragma GCC diagnostic push 
		#pragma GCC diagnostic ignored "-Wwrite-strings"

		//
		// Config internal Parameters not bound to a user interface object
		//

		setJsonLabel (      STYLE_JSON, "style");

		#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig () {
		configParam (DIV_PARAM, 1.f,  16.f,   4.f, "Clock Division", "",  0.f, 1.f, 0.f);
		configParam (LEN_PARAM, 1.f,  16.f,  16.f, "Length",         "",  0.f, 1.f, 0.f);
		configParam (AMT_PARAM, 0.f, 100.f, 100.f, "Amount",         "%", 0.f, 1.f, 0.f);

        for (int i = 0; i < 16; i ++) {
    		configParam (TIM_PARAM_01 + i,  -100.f, 100.f, 0.f, "Timing", "%", 0.f, 1.f, 0.f);
        }

		configInput ( RST_INPUT, "Reset");
		configInput ( CLK_INPUT, "Clock");
		configInput ( BPM_INPUT, "BPM");

		configOutput ( PHS_OUTPUT, "Phase");
		configOutput ( CMP_OUTPUT, "Compare (micro timing)");
		configOutput (ECLK_OUTPUT, "Early clock");
		configOutput (TCLK_OUTPUT, "Timed clock");

	}

	inline void moduleCustomInitialize () {
	}

	/**
		Method to initialize the module after loading a patch or a preset
		Called from initialize () included from from OrangeLineCommon.hpp
		to initialize module state from a valid
		json state after module was added to the patch, 
		a call to dataFromJson due to patch or preset load
		or a right click initialize (reset).
	*/
	inline void moduleInitialize () {
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
		Currently called twice when add a module to patch ...
	*/
	void moduleReset () {
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
	inline void moduleProcess (const ProcessArgs &args) {
		// if (!widgetReady) return;	// do not strt processing before the widget is ready
		if (styleChanged && widgetReady) {
			switch (int(getStateJson(STYLE_JSON))) {
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

        if (getInputConnected (BPM_INPUT)) {
            if (changeInput (BPM_INPUT) || changeParam (DIV_PARAM) || phaseStep == 0.f)
                phaseStep = CLOCK_MULT * 2.f * std::pow(2.f, getStateInput (BPM_INPUT)) * OL_sampleTime * (PHASE_HIGH - PHASE_LOW);            
        }
        else {
            phaseStep = 0.f;
        }
        if (changeInput (RST_INPUT) || inChangeParam (RST_PARAM)) {
            clkMultCnt = 0;
            eClkFired = false;
            tClkFired = true;
			tClkDelay = FIRST_TCLK_DELAY;
            phase = 0.f;
            tPos = -1;
        }
		if (changeInput (CLK_INPUT)) {
			clkMultCnt = CLOCK_MULT;
			phase = 0.f;
		}

		if (phase < 0.f || clkMultCnt > 0) {
			phase += phaseStep * (1 + samplesSkipped);
			if (phase > PHASE_HIGH) {
				clkMultCnt --;
				phase = PHASE_LOW;
				eClkFired = false;
			}
			if (!eClkFired && phase >= PHASE_LOW) {
				tPos ++;
				if (tPos >= getStateParam (LEN_PARAM))
					tPos = 0;
				cmp = getStateParam (TIM_PARAM_01 + tPos) * getStateParam (AMT_PARAM) / 100 / 10.f;
				cmp = clamp(cmp, MIN_CMP, MAX_CMP);
				setStateOutput (CMP_OUTPUT, cmp);
				setStateOutput (ECLK_OUTPUT, 10.f);
				eClkFired = true;
				tClkFired = false;
			}
		}
		setStateOutput (PHS_OUTPUT, phase);
		if ((!tClkFired && phase >= cmp) || tClkDelay == 0) {
			if (tClkDelay >= 0)
				tClkDelay --;
			else {
				setStateOutput (TCLK_OUTPUT, 10.f);
				tClkFired = true;
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
	inline void moduleProcessState () {
	}
	
	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges () {
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

/**
	Main Module Widget
*/
struct SwingWidget : ModuleWidget {

    char divBuffer[3];
    char lenBuffer[3];

	SwingWidget(Swing *module) {
        NumberWidget *numberWidget;
        float *pValue;

        setModule (module);
		setPanel (APP->window->loadSvg(asset::plugin (pluginInstance, "res/Swing.svg")));

		if (module) {
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SwingBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SwingDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}


		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 3.89  + 4.2 , 128.5 - 10.559 - 4.2)),  module, CLK_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 3.89  + 4.2 , 128.5 - 20.242 - 4.2)),  module, BPM_INPUT));
		addInput (createInputCentered<PJ301MPort>		(mm2px (Vec ( 3.89  + 4.2 , 128.5 - 29.926 - 4.2)),  module, RST_INPUT));
		addParam (createParamCentered<LEDButton>		(mm2px (Vec ( 5.712 + 2.38, 128.5 - 41.423 - 2.38)), module, RST_PARAM));

		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (33.419 + 4.2 , 128.5 - 39.61  - 4.2)),  module,  PHS_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (33.419 + 4.2 , 128.5 - 29.926 - 4.2)),  module,  CMP_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (33.419 + 4.2 , 128.5 - 20.24  - 4.2)),  module, ECLK_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>		(mm2px (Vec (33.419 + 4.2 , 128.5 - 10.559 - 4.2)),  module, TCLK_OUTPUT));

		RoundSmallBlackKnob *knob = createParamCentered<RoundSmallBlackKnob>		(mm2px (Vec ( 3.141 + 4,    128.5 - 99.019 - 4)),    module, DIV_PARAM);
		knob->snap = true;
   		addParam (knob);

		pValue  = (module != nullptr ? &(module->getStateParam (DIV_PARAM)) : nullptr);
		numberWidget = NumberWidget::create (mm2px (Vec(3.65, 128.5 - 110.35)), module, pValue, 0.f, "%2.0f", divBuffer, 2);
		numberWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
		addChild (numberWidget);

		knob = createParamCentered<RoundSmallBlackKnob>		(mm2px (Vec (34.576 + 4,    128.5 - 99.019 - 4)),    module, LEN_PARAM);
		knob->snap = true;
   		addParam (knob);

		pValue  = (module != nullptr ? &(module->getStateParam (LEN_PARAM)) : nullptr);
		numberWidget = NumberWidget::create (mm2px (Vec(35.2, 128.5 - 110.35)), module, pValue, 0.f, "%2.0f", lenBuffer, 2);
		numberWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
		addChild (numberWidget);

		addParam (createParamCentered<RoundLargeBlackKnob>		(mm2px (Vec (16.51 + 6.35,    128.5 - 102.553 - 6.35)),    module, AMT_PARAM));

		for (int i = 0; i < 16; i ++) {
			int x = i % 4;
			int y = i / 4;
			Vec pos = Vec (3.169 + 4 + x * (13.621 - 3.169), 128.5 - 85.535 - 4 + y * (13.621 - 3.169));
			addParam (createParamCentered<RoundSmallBlackKnob> (mm2px (pos), module, TIM_PARAM_01 +  i));
		}
  	    if (module) module->widgetReady = true;
	}

	struct SwingStyleItem : MenuItem {
		Swing *module;
		int style;
		void onAction(const event::Action &e) override {
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override {
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Swing *module = dynamic_cast<Swing*>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		SwingStyleItem *style1Item = new SwingStyleItem();
		style1Item->text = "Orange";// 
		style1Item->module = module;
		style1Item->style= STYLE_ORANGE;
		menu->addChild(style1Item);

		SwingStyleItem *style2Item = new SwingStyleItem();
		style2Item->text = "Bright";// 
		style2Item->module = module;
		style2Item->style= STYLE_BRIGHT;
		menu->addChild(style2Item);
			
		SwingStyleItem *style3Item = new SwingStyleItem();
		style3Item->text = "Dark";// 
		style3Item->module = module;
		style3Item->style= STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelSwing = createModel<Swing, SwingWidget>("Swing");
