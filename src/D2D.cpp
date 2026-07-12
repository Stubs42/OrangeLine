/*
	D2D.cpp

	Code for the OrangeLine module D2D

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

#include "D2D.hpp"

struct D2D : Module
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	D2D()
	{
		initializeInstance();
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
		setInPoly (GATE_INPUT, true);
		setInPoly (VEL_INPUT, true);
		setInPoly (SHAPE_INPUT, true);
		setInPoly (ATTEN_INPUT, true);

		setOutPoly (HEATOFF_OUTPUT, true);
		setOutPoly (HEATSCL_OUTPUT, true);
		setOutPoly (VEL_OUTPUT, true);
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel (STYLE_JSON, "style");
		setJsonLabel (CURVE_UNI_JSON, "unipolarCurve");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		configInput (GATE_INPUT, "Gate");
		configInput (VEL_INPUT, "Velocity");
		configInput (SHAPE_INPUT, "Velocity curve shape [-5:+5]");
		configInput (ATTEN_INPUT, "Heat scale attenuation");
		configOutput (HEATOFF_OUTPUT, "Heat offset gate");
		configOutput (HEATSCL_OUTPUT, "Heat scale (attenuated)");
		configOutput (VEL_OUTPUT, "Processed velocity");
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		setStateJson (CURVE_UNI_JSON, 0.f);

		styleChanged = true;
	}

	/**
		Stateless: fully recomputed every control-rate tick. Channels are fixed 1:1
		(channel N in -> channel N out, no reordering) since the inputs already come
		from a fixed-channel source (K2C).

		Per channel, while GATE is high and VEL > 0:
		1. Split by VEL: > 5V is the "offset" half, <= 5V is the "scale" half.
		2. Rescale the half the value fell into back to the full 0-10V range.
		3. Apply the velocity curve to the rescaled value - this result is what VEL OUT
		   always reports, regardless of which half fired. SHAPE is normally bipolar CV
		   in [-5:+5] (0 = linear); the "Unipolar Curve" context menu option instead reads
		   it as unipolar 0-10V (as MIDI CC delivers it) and remaps to [-5:+5] via -5V.
		4. HEATOFF OUT is a plain gate (0V or 10V) for the offset half - no curve, no
		   attenuation, "always plays" at full value when it fires.
		   HEATSCL OUT is the curved value from step 3, further linearly scaled by
		   ATTEN - only the scale half gets attenuated, offset never does.
	*/
	inline void moduleProcess(const ProcessArgs &args)
	{
		if (styleChanged && widgetReady)
		{
			switch (int(getStateJson(STYLE_JSON)))
			{
			case STYLE_ORANGE: brightPanel->visible = false; darkPanel->visible = false; break;
			case STYLE_BRIGHT: brightPanel->visible = true;  darkPanel->visible = false; break;
			case STYLE_DARK:   brightPanel->visible = false; darkPanel->visible = true;  break;
			}
			styleChanged = false;
		}

		bool gateConnected  = getInputConnected (GATE_INPUT);
		bool velConnected   = getInputConnected (VEL_INPUT);
		bool shapeConnected = getInputConnected (SHAPE_INPUT);
		bool attenConnected = getInputConnected (ATTEN_INPUT);

		for (int c = 0; c < POLY_CHANNELS; c++)
		{
			float gate = gateConnected ? OL_statePoly[GATE_INPUT * POLY_CHANNELS + c] : 0.f;
			float vel  = velConnected  ? OL_statePoly[VEL_INPUT  * POLY_CHANNELS + c] : 0.f;

			if (gate <= 5.f || vel <= 0.f)
			{
				setStateOutPoly (HEATOFF_OUTPUT, c, 0.f);
				setStateOutPoly (HEATSCL_OUTPUT, c, 0.f);
				setStateOutPoly (VEL_OUTPUT,     c, 0.f);
				continue;
			}

			bool  upperHalf = vel > 5.f;
			float rescaled  = upperHalf ? (vel - 5.f) / 5.f * 10.f : vel / 5.f * 10.f;
			rescaled = clamp (rescaled, 0.f, 10.f);

			float shape = shapeConnected ? 
			  	((getStateJson(CURVE_UNI_JSON) == 0.f) ? 
			  		OL_statePoly[SHAPE_INPUT * POLY_CHANNELS + c] :
			  		OL_statePoly[SHAPE_INPUT * POLY_CHANNELS + c] - 5.f
				) : 
			  0.f;
			float v01   = rescaled / 10.f;
			if (shape > 0.f)
				v01 = 1.f - pow (1.f - v01, shape + 1.f);
			else if (shape < 0.f)
				v01 = pow (v01, -shape + 1.f);
			float curved = v01 * 10.f;

			float attenFactor = attenConnected ? clamp (OL_statePoly[ATTEN_INPUT * POLY_CHANNELS + c], 0.f, 10.f) / 10.f : 1.f;

			setStateOutPoly (HEATOFF_OUTPUT, c, upperHalf ? 10.f : 0.f);
			setStateOutPoly (HEATSCL_OUTPUT, c, upperHalf ? 0.f  : curved * attenFactor);
			setStateOutPoly (VEL_OUTPUT,     c, curved);
		}

		setOutPolyChannels (HEATOFF_OUTPUT, POLY_CHANNELS);
		setOutPolyChannels (HEATSCL_OUTPUT, POLY_CHANNELS);
		setOutPolyChannels (VEL_OUTPUT,     POLY_CHANNELS);
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

/**
	Main Module Widget

	Jack centers below are measured directly from the "Controls" layer of res/D2DWork.svg,
	so the widget lines up exactly with the panel art. Order top to bottom: GATE IN, VEL IN,
	ATTEN IN, HEATOFF OUT, HEATSCL OUT, SHAPE IN, VEL OUT.
*/
struct D2DWidget : ModuleWidget
{
	D2DWidget(D2D *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/D2DOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/D2DBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/D2DDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 14.479f, 0.f), module, GATE_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 28.957f, 0.f), module, VEL_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 43.435f, 0.f), module, ATTEN_INPUT));

		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.620f, 58.929f, 0.f), module, HEATOFF_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.620f, 74.423f, 0.f), module, HEATSCL_OUTPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (7.620f, 93.981f, 0.f), module, SHAPE_INPUT));
		addOutput (createOutputCentered<PJ301MPort> (calculateCoordinates (7.626f, 109.440f, 0.f), module, VEL_OUTPUT));

		if (module)
			module->widgetReady = true;
	}

	struct D2DStyleItem : MenuItem
	{
		D2D *module;
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
	struct D2DUnipolarCurveItem : MenuItem {
		D2D *module;
		void onAction(const event::Action &e) override {
			module->setStateJson(CURVE_UNI_JSON, module->getStateJson(CURVE_UNI_JSON) == 0.f ? 1.f : 0.f);
		}
		void step() override {
			if (module)
				rightText = (module != nullptr && module->getStateJson(CURVE_UNI_JSON) == 1.f) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		D2D *module = dynamic_cast<D2D *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		D2DStyleItem *style1Item = new D2DStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		D2DStyleItem *style2Item = new D2DStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		D2DStyleItem *style3Item = new D2DStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		D2DUnipolarCurveItem *unipolarCurveItem = new D2DUnipolarCurveItem();
		unipolarCurveItem->module = module;
		unipolarCurveItem->text = "Unipolar Curve";
		menu->addChild(unipolarCurveItem);


	}
};

Model *modelD2D = createModel<D2D, D2DWidget>("D2D");
