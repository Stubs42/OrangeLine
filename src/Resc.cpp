/*
	Resc.cpp

	Code for the OrangeLine module Resc

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

#include "Resc.hpp"

struct Resc : Module
{

	float srcScale[POLY_CHANNELS];
	int srcScaleNotes = 0;
	float trgScale[POLY_CHANNELS];
	int trgScaleNotes = 0;
	int trgCld = 0;
	bool trgSclInputWasConnected = false;
	bool trgCldInputWasConnected = false;

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
	Resc()
	{
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
		setInPoly(IN_INPUT, true);
		setCustomChangeMaskInput(IN_INPUT, CHG_IN);
		setInPoly(SRCSCL_INPUT, true);
		setCustomChangeMaskInput(SRCSCL_INPUT, CHG_SRCSCL);
		setInPoly(TRGSCL_INPUT, true);
		setCustomChangeMaskInput(TRGSCL_INPUT, CHG_TRGSCL);
		setOutPoly(ROOTBASED_OUTPUT, true);
		setOutPoly(CLDBASED_OUTPUT, true);
		setOutPoly(CLDSCL_OUTPUT, true);
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
		setJsonLabel(CHILD_CV_MODE_JSON, "childCvMode");

#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig()
	{
		configInput(IN_INPUT, "Input Pitch to rescale");
		configInput(SRCSCL_INPUT, "Reference Scale of Input Pitch defaults to Cmaj");
		configInput(TRGSCL_INPUT, "Target (Root) Scale to rescale the pitch to");
		configInput(TRGCLD_INPUT, "Child of Target Root Scale");

		configOutput(ROOTBASED_OUTPUT, "Rescaled Pitch based on Root Position");
		configOutput(CLDBASED_OUTPUT, "Rescaled Pitch based on Child Position");
		configOutput(CLDSCL_OUTPUT, "Child Scale of Target (Root) Scale");
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
		setStateJson(CHILD_CV_MODE_JSON, CHILD_CV_IN_SCALE);
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
		/*
			Setup Source Scale
		*/
		if (getInputConnected(SRCSCL_INPUT))
		{
			srcScaleNotes = inputs[SRCSCL_INPUT].getChannels();
			for (int channel = 0; channel < srcScaleNotes; channel++)
			{
				srcScale[channel] = OL_statePoly[SRCSCL_INPUT * POLY_CHANNELS + channel];
			}
		}
		else
		{
			// Default Cmaj scale
			srcScaleNotes = 7;
			srcScale[0] = quantize(0.f);
			srcScale[1] = quantize(2.f * 1.f / 12.f);
			srcScale[2] = quantize(4.f * 1.f / 12.f);
			srcScale[3] = quantize(5.f * 1.f / 12.f);
			srcScale[4] = quantize(7.f * 1.f / 12.f);
			srcScale[5] = quantize(9.f * 1.f / 12.f);
			srcScale[6] = quantize(11.f * 1.f / 12.f);
		}
		/*
			Setup Target Scale
		*/
		if (getInputConnected(TRGSCL_INPUT))
		{
			trgScaleNotes = inputs[TRGSCL_INPUT].getChannels();
			for (int channel = 0; channel < trgScaleNotes; channel++)
			{
				trgScale[channel] = OL_statePoly[TRGSCL_INPUT * POLY_CHANNELS + channel];
			}
			trgSclInputWasConnected = false;
		}
		else
		{
			// Default Cmaj scale
			trgScaleNotes = 7;
			trgScale[0] = quantize(0.f);
			trgScale[1] = quantize(2.f * 1.f / 12.f);
			trgScale[2] = quantize(4.f * 1.f / 12.f);
			trgScale[3] = quantize(5.f * 1.f / 12.f);
			trgScale[4] = quantize(7.f * 1.f / 12.f);
			trgScale[5] = quantize(9.f * 1.f / 12.f);
			trgScale[6] = quantize(11.f * 1.f / 12.f);
			trgSclInputWasConnected = false;
		}
		/*
			Get Target Child
		*/
		float childOct = 0.f;
		if (getInputConnected(TRGCLD_INPUT))
		{
			float cld = quantize(getStateInput(TRGCLD_INPUT));
			if (getStateJson(CHILD_CV_MODE_JSON) == CHILD_CV_RELATIVE)
			{
				cld += trgScale[0];
			}
			childOct = 0.f;
			while (cld > trgScale[trgScaleNotes - 1] + PRECISION)
			{
				cld -= 1.f;
				childOct += 1.f;
			}
			while (cld < trgScale[0] - PRECISION)
			{
				cld += 1.f;
				childOct -= 1.f;
			}
			// find position in srcScale
			for (trgCld = trgScaleNotes - 1; trgCld > 0; trgCld--)
			{
				if (trgScale[trgCld] <= cld + PRECISION)
				{
					break;
				}
			}
		}
		else
		{
			if (trgCld != 0)
			{
				trgCld = 0;
			}
		}
		/*
			output the Child Scale
		*/
		for (int position = 0; position < trgScaleNotes; position++)
		{
			float pitch = trgScale[(position + trgCld) % trgScaleNotes];
			if (position + trgCld >= trgScaleNotes)
			{
				pitch += 1.0f;
			}
			int cldSclPolyIdx = CLDSCL_OUTPUT * POLY_CHANNELS + position;
			OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cldSclPolyIdx] = pitch + childOct;
			OL_outStateChangePoly[cldSclPolyIdx] = true;
		}
		setOutPolyChannels(CLDSCL_OUTPUT, trgScaleNotes);
		/*
			Now do the real job for each poly channel of CHG_IN
		*/
		for (int channel = 0; channel < inputs[IN_INPUT].getChannels(); channel++)
		{
			float oct = 0;
			float srcPitch = quantize(OL_statePoly[IN_INPUT * POLY_CHANNELS + channel]);
			// DEBUG(" srcPitch = %lf", srcPitch);

			// Move srcPitch into srcScale octave
			while (srcPitch >= srcScale[0] + 1.f - PRECISION)
			{
				srcPitch -= 1.f;
				oct += 1.f;
			}
			while (srcPitch < srcScale[0] - PRECISION)
			{
				srcPitch += 1.f;
				oct -= 1.f;
			}
			// DEBUG(" srcPitch = %lf (normalized to srcScale), oct = %lf", srcPitch, oct);

			// find position of srcPitch in srcScale
			int position;
			for (position = srcScaleNotes - 1; position > 0; position--)
			{
				if (srcScale[position] <= srcPitch + PRECISION)
				{
					srcPitch = srcScale[position] + oct;
					break;
				}
			}
			// DEBUG("position = %d", position);

			int cvRootBasedPolyIdx = ROOTBASED_OUTPUT * POLY_CHANNELS + channel;
			// DEBUG("cvRootBasedPolyIdx = %d", cvRootBasedPolyIdx

			// get target pitch at position from target scale
			float trgPitch = trgScale[position % trgScaleNotes] + oct;
			// DEBUG ("trgPitch = %lf", trgPitch);

			OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvRootBasedPolyIdx] = trgPitch;
			OL_outStateChangePoly[cvRootBasedPolyIdx] = true;

			int cvCldBasedPolyIdx = CLDBASED_OUTPUT * POLY_CHANNELS + channel;
			// DEBUG("cvCldBasedPolyIdx = %d", cvCldBasedPolyIdx);

			// get target pitch at position for child
			position += trgCld;
			if (position > trgScaleNotes - 1) {
				position -= trgScaleNotes;
				oct += 1;
				// DEBUG ("correcting oct + 1 = %lf", oct);
			}
			if (position < 0) {
				position += trgScaleNotes;
				oct -= 1;
				// DEBUG ("correcting oct - 1 = %lf", oct);
			}

			// DEBUG ("[cldTrgScale]:trgScale[position] + oct + childOct = %lf", trgScale[position] + oct + childOct);
			OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvCldBasedPolyIdx] = trgScale[position] + oct + childOct;
			OL_outStateChangePoly[cvCldBasedPolyIdx] = true;
		}
		setOutPolyChannels(ROOTBASED_OUTPUT, inputs[IN_INPUT].getChannels());
		setOutPolyChannels(CLDBASED_OUTPUT, inputs[IN_INPUT].getChannels());
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
struct RescWidget : ModuleWidget
{

	char divBuffer[3];
	char lenBuffer[3];

	RescWidget(Resc *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RescOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RescBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RescDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(3.415, 12.052, OFFSET_PJ301MPort), module, IN_INPUT));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(3.415, 24.752, OFFSET_PJ301MPort), module, SRCSCL_INPUT));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(3.415, 48.882, OFFSET_PJ301MPort), module, TRGSCL_INPUT));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(3.415, 61.582, OFFSET_PJ301MPort), module, TRGCLD_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(3.415, 82.918, OFFSET_PJ301MPort), module, ROOTBASED_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(3.415, 95.642, OFFSET_PJ301MPort), module, CLDBASED_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(3.415, 108.342, OFFSET_PJ301MPort), module, CLDSCL_OUTPUT));

		if (module)
			module->widgetReady = true;
	}

	struct RescStyleItem : MenuItem
	{
		Resc *module;
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

	struct ChildCvModeItem : MenuItem
	{
		Resc *module;

		struct ModeItem : MenuItem
		{
			Resc *module;
			int mode;
			void onAction(const event::Action &e) override
			{
				module->OL_setOutState(CHILD_CV_MODE_JSON, float(mode));
			}
			void step() override
			{
				if (module)
					rightText = (module != nullptr && module->OL_state[CHILD_CV_MODE_JSON] == float(mode)) ? "✔" : "";
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			ModeItem *modeItem;
			for (int mode = 0; mode < 2; mode++)
			{

				modeItem = new ModeItem();
				modeItem->module = module;
				modeItem->mode = mode;
				modeItem->text = mode == 0 ? "In Scale" : "Relative [C4]";
				modeItem->setSize(Vec(110, 20));

				menu->addChild(modeItem);
			}
			return menu;
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		if (module)
		{
			MenuLabel *spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			Resc *module = dynamic_cast<Resc *>(this->module);
			assert(module);

			MenuLabel *styleLabel = new MenuLabel();
			styleLabel->text = "Style";
			menu->addChild(styleLabel);

			RescStyleItem *style1Item = new RescStyleItem();
			style1Item->text = "Orange"; //
			style1Item->module = module;
			style1Item->style = STYLE_ORANGE;
			menu->addChild(style1Item);

			RescStyleItem *style2Item = new RescStyleItem();
			style2Item->text = "Bright"; //
			style2Item->module = module;
			style2Item->style = STYLE_BRIGHT;
			menu->addChild(style2Item);

			RescStyleItem *style3Item = new RescStyleItem();
			style3Item->text = "Dark"; //
			style3Item->module = module;
			style3Item->style = STYLE_DARK;
			menu->addChild(style3Item);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			MenuLabel *behaviourLabel = new MenuLabel();
			behaviourLabel->text = "Behaviour";
			menu->addChild(behaviourLabel);

			ChildCvModeItem *childCvModeItem = new ChildCvModeItem();
			childCvModeItem->module = module;
			childCvModeItem->text = "Child CV Mode";
			childCvModeItem->rightText = RIGHT_ARROW;
			menu->addChild(childCvModeItem);
		}
	}
};

Model *modelResc = createModel<Resc, RescWidget>("Resc");
