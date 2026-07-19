/*
	Hold.cpp

	Code for the OrangeLine module Hold

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
#include <vector>
#include <cstring>
#include <stdio.h>
#include <limits.h>

#include "Hold.hpp"

// Held CVs are arbitrary-precision patch voltages (pitch, modulation, anything), not
// MIDI-CC-derived data - unlike RECALL/CC2CV/CV2CC's byte-quantized CC persistence, these are
// packed losslessly as raw 4 byte floats rather than quantized to a byte each.
static std::vector<uint8_t> floatsToBytes(const float *values, int count)
{
	std::vector<uint8_t> bytes(count * sizeof(float));
	std::memcpy(bytes.data(), values, bytes.size());
	return bytes;
}
static void bytesToFloats(const std::vector<uint8_t> &bytes, float *values, int count)
{
	size_t copyBytes = std::min(bytes.size(), count * sizeof(float));
	std::memcpy(values, bytes.data(), copyBytes);
	for (size_t i = copyBytes / sizeof(float); i < (size_t) count; i++)
		values[i] = 0.f;
}

struct Hold : Module
{

#include "OrangeLineCommon.hpp"

#include "HoldJsonLabels.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	bool widgetReady = false;
	float oldGates[NUM_ROWS];

	// ********************************************************************************************************************************
	/*
		Initialization
	*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Hold()
	{
		// STORE_JSON's 160 slots (NUM_ROWS * POLY_CHANNELS) used to be written/read as 160
		// individual json_real() objects via the generic per-jsonId loop in
		// OrangeLineCommon.hpp - expensive since dataToJson() runs often (autosave, undo/redo
		// history). Skipping that range there and packing it into one base64 string via the
		// hooks below cuts it to a single JSON object, losslessly (raw float bytes, not
		// quantized - these are arbitrary patch CVs, not 7 bit MIDI CC data).
		OL_jsonSkipFrom = STORE_JSON;

		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			float values[NUM_ROWS * POLY_CHANNELS];
			for (int i = 0; i < NUM_ROWS * POLY_CHANNELS; i++)
				values[i] = getStateJson(STORE_JSON + i);
			json_object_set_new(rootJ, "store", json_string(string::toBase64(floatsToBytes(values, NUM_ROWS * POLY_CHANNELS)).c_str()));
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *storeJ = json_object_get(rootJ, "store");
			if (storeJ && json_is_string(storeJ))
			{
				float values[NUM_ROWS * POLY_CHANNELS];
				bytesToFloats(string::fromBase64(json_string_value(storeJ)), values, NUM_ROWS * POLY_CHANNELS);
				for (int i = 0; i < NUM_ROWS * POLY_CHANNELS; i++)
					setStateJson(STORE_JSON + i, values[i]);
			}
			else
			{
				// Patch/preset saved by an older version of this module, before "store"
				// existed - the individual "r01c01".."r10c16" fields are still in rootJ (the
				// generic loop just doesn't read them into STORE_JSON anymore since it now
				// stops at OL_jsonSkipFrom), so read them directly here instead. Without this
				// fallback, every already-released patch using Hold would silently lose its
				// held values on load.
				for (int i = 0; i < NUM_ROWS * POLY_CHANNELS; i++)
				{
					json_t *fieldJ = json_object_get(rootJ, jsonLabel[STORE_JSON + i]);
					if (fieldJ)
						setStateJson(STORE_JSON + i, json_real_value(fieldJ));
				}
			}
		};

		initializeInstance();
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
	bool moduleSkipProcess()
	{
		bool skip = (idleSkipCounter != 0);
		return skip;
	}
	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes()
	{
        setInPoly (GATE_INPUT, true);

        for (int i = 0; i < NUM_ROWS; i++) 
		{
            setInPoly (CV_INPUT + i, true);
            setOutPoly(CV_OUTPUT + i, true);
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
#pragma GCC diagnostic ignored "-Wwrite-strings"
		//
		// Config internal Parameters not bound to a user interface object
		//
		for (int i = 0; i < NUM_JSONS; i++)
		{
			setJsonLabel(i, jsonLabel[i]);
		}

#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig()
	{
        configInput (GATE_INPUT, "Polyphonic Gate");
		char buffer[64];
        for (int i = 0; i < NUM_ROWS; i++)
		{
            sprintf(buffer, "CV in %d", i + 1);
            configInput (CV_INPUT + i, buffer);
            sprintf(buffer, "CV out %d", i + 1);
            configOutput (CV_OUTPUT + i, buffer);
		}
		configSwitch(TRK_ON_PARAM, 0.0f, 1.0f, 0.0f, "Track", {"OFF", "ON"});
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
		memset (oldGates, 0, sizeof(oldGates));	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
		Currently called twice when add a module to patch ...
	*/
	void moduleReset()
	{
		styleChanged = true;
        for (int i = STORE_JSON; i < STORE_JSON_LAST; i++) {
            setStateJson(i, 0.f);
        }
		setStateJson(TRK_ON_JSON, 0.f);
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

		// Handle TRK_ON Button presses
		if (inChangeParam (TRK_ON_PARAM)) {	//	User clicked on tr/gt button
			setStateJson (TRK_ON_JSON, getStateParam(TRK_ON_PARAM));
		}

		// Now do the real processing
		int gateChannels = inputs[GATE_INPUT].getChannels();
		float gate = 0.0f;
		int cvInput = -1;
        for (int row = 0; row < NUM_ROWS; row++)
        {
			// check for active gate
			if (row < gateChannels) {
				gate = OL_statePoly[GATE_INPUT + row];
			}
			if (getInputConnected(CV_INPUT + row)) {
				cvInput = CV_INPUT + row;
			}

			// Skip rows with no cv input connected
			if (cvInput >= 0) {
				int channels = inputs[cvInput].getChannels();
		        for (int channel = 0; channel < channels; channel++)
        		{
					float cv = OL_statePoly[cvInput * POLY_CHANNELS + channel];
					if (gate > 5.0f) {
						if (getStateParam(TRK_ON_PARAM) == 1.0f ||	// gate for track and hold
					   	    oldGates[row] < 5.0f					// rising edge for sample and hold
						   ) {
							setStateJson(STORE_JSON + row * POLY_CHANNELS + channel, cv);
						}
					}
					setStateOutPoly(CV_OUTPUT + row, channel, getStateJson(STORE_JSON + row * POLY_CHANNELS + channel));
				}
				setOutPolyChannels(CV_OUTPUT + row, channels);
			}
			// remember gate for rising edge detection in s&h mode
			oldGates[row] = gate;
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
		setStateLight (TRK_ON_LIGHT, getStateJson (TRK_ON_JSON) * 255.f);
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

/**
	Main Module Widget
*/
struct HoldWidget : ModuleWidget
{
	HoldWidget(Hold *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/HoldOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/HoldBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/HoldDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addOrangeLineTouchPorts (this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (2.653, 12.052, OFFSET_PJ301MPort),  module, GATE_INPUT));
        for (int i = 0; i < NUM_ROWS; i++)
		{
			addInput  (createInputCentered<PJ301MPort>  (calculateCoordinates (2.653,  26.276 + i * 9.37, OFFSET_PJ301MPort),  module, CV_INPUT  + i));
            addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (14.337, 26.276 + i * 9.37, OFFSET_PJ301MPort),  module, CV_OUTPUT + i));
		}
		addParam (createParamCentered<VCVLatch> (calculateCoordinates  ( 16.161, 13.876, OFFSET_LEDButton),  module, TRK_ON_PARAM));
 		addChild (createLightCentered<LargeLight<YellowLight>>	(calculateCoordinates  ( 16.161, 13.876, OFFSET_LEDButton), module, TRK_ON_LIGHT));
		
		if (module)
			module->widgetReady = true;
	}

	struct HoldStyleItem : MenuItem
	{
		Hold *module;
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

		Hold *module = dynamic_cast<Hold *>(this->module);
		assert(module);

		addOrangeLineTouchMenuItem(menu, module->OL_touchInPort, module->OL_touchOutPort, &module->OL_touchVisible);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		HoldStyleItem *style1Item = new HoldStyleItem();
		style1Item->text = "Orange"; //
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		HoldStyleItem *style2Item = new HoldStyleItem();
		style2Item->text = "Bright"; //
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		HoldStyleItem *style3Item = new HoldStyleItem();
		style3Item->text = "Dark"; //
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelHold = createModel<Hold, HoldWidget>("Hold");
