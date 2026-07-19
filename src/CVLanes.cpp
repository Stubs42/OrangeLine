/*
	CVLanes.cpp

	Code for the OrangeLine module CVLanes (the LANES Hub, CV-input variant)

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

#include "CVLanes.hpp"

struct CVLanes : Module, LanesHubInterface, ExpanderBridgeInterface
{

#include "OrangeLineCommon.hpp"

#include "CVLanesJsonLabels.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables

		LANES (the Hub) does no merging or voice-stealing of its own anymore - it's just a
		shared input module. It quantizes/defaults the NUM_SOURCES raw sources' gate/pitch/
		velocity/lane-select CV and exposes that raw, per-source, *unmerged* state via
		LanesHubInterface (see LanesShared.hpp for why). Each expander (LanesCV, LanesMidi,
		...) runs its own independent LanesVoiceAllocator against this raw state, with its
		own capacity and its own overflow indicator - see LanesVoiceAllocator.hpp.
	*/
	bool widgetReady = false;

	bool  sourceGate    [NUM_SOURCES][POLY_CHANNELS];
	float sourcePitch   [NUM_SOURCES][POLY_CHANNELS];
	float sourceVelocity[NUM_SOURCES][POLY_CHANNELS];
	int   sourceLane    [NUM_SOURCES][POLY_CHANNELS];

	// User-editable label, set via the right-click menu's text field (see CVLanesNameField) -
	// mirrors Morpheus's own customName exactly, same reasoning: once a patch has more than one
	// Hub, an Expander's own Disconnect-style menu item needs to say *which* one a connection
	// belongs to. Empty by default.
	std::string customName;

	// ********************************************************************************************************************************
	/*
		Initialization
	*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	CVLanes()
	{
		initializeInstance();

		// customName is a plain string, not a float OL_state slot - same reasoning/pattern as
		// Morpheus's own moduleExtraDataToJson/FromJson use for its own customName.
		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_object_set_new(rootJ, "customName", json_string(customName.c_str()));
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *nameJ = json_object_get(rootJ, "customName");
			if (nameJ && json_is_string(nameJ))
				customName = json_string_value(nameJ);
		};
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
		for (int i = 0; i < NUM_SOURCES; i++)
		{
			setInPoly (VOCT_IN_INPUT + i, true);
			setInPoly (GATE_IN_INPUT + i, true);
			setInPoly (VEL_IN_INPUT  + i, true);
			setInPoly (LANE_IN_INPUT + i, true);
		}
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

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
		char buffer[64];
		for (int i = 0; i < NUM_SOURCES; i++)
		{
			sprintf(buffer, "Source %d V/Oct", i + 1);
			configInput (VOCT_IN_INPUT + i, buffer);
			sprintf(buffer, "Source %d Gate", i + 1);
			configInput (GATE_IN_INPUT + i, buffer);
			sprintf(buffer, "Source %d Velocity", i + 1);
			configInput (VEL_IN_INPUT + i, buffer);
			sprintf(buffer, "Source %d Lane", i + 1);
			configInput (LANE_IN_INPUT + i, buffer);
		}
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
		memset (sourceGate,     0, sizeof(sourceGate));
		memset (sourcePitch,  0.f, sizeof(sourcePitch));
		memset (sourceVelocity, 0.f, sizeof(sourceVelocity));
		memset (sourceLane,     0, sizeof(sourceLane));
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
		LanesHubInterface implementation - raw, unmerged per-source state for expanders
		(LanesCV, LanesMidi, ...) to run their own LanesVoiceAllocator against. See
		LanesShared.hpp.
	*/
	bool  getSourceGate(int source, int channel) override { return sourceGate[source][channel]; }
	float getSourcePitch(int source, int channel) override { return sourcePitch[source][channel]; }
	float getSourceVelocity(int source, int channel) override { return sourceVelocity[source][channel]; }
	int   getSourceLane(int source, int channel) override { return sourceLane[source][channel]; }
	float getLanesStyle() override { return OL_state[STYLE_JSON]; }

	// ExpanderBridgeInterface (ExpanderBridge.hpp) - a Hub trivially reports its own id.
	int64_t getBridgeHostId() override { return (int64_t) this->id; }
	std::vector<ExpanderFamily> getBridgeFamilies() override { return getModuleFamilies(model->slug); }
	std::string getBridgeHostName() override { return customName; }
	// Listener registry backing registerBridgeListener()/unregisterBridgeListener() below - lets
	// any LanesCV/LanesMidi Expander that cached a raw pointer to this Hub find out immediately
	// if it's removed, instead of ever re-resolving that pointer via APP->engine->getModule() from
	// inside moduleProcess() (confirmed, 2026-07-19, to be a live engine deadlock risk under a
	// concurrent module add/remove - see ExpanderBridgeInterface's own comment, ExpanderBridge.hpp,
	// for the full mechanism this backs).
	BridgeListenerRegistry bridgeListeners;
	void registerBridgeListener(ExpanderBridgeInterface *listener) override { bridgeListeners.add(listener); }
	void unregisterBridgeListener(ExpanderBridgeInterface *listener) override { bridgeListeners.remove(listener); }
	// Fires right before this Hub is actually destroyed - proactively tells every still-connected
	// Expander to drop its cached pointer, using only pointers already held (no engine lookup of
	// any kind), so none of this is affected by whatever lock Rack's own removeModule() currently
	// holds for the duration of this callback.
	void onRemove(const RemoveEvent &e) override
	{
		bridgeListeners.notifyAndClear();
		Module::onRemove(e);
	}

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
			Just quantize/default the raw sources and store them - no merging, no voice
			stealing (that's each expander's own job now, see LanesVoiceAllocator.hpp).

			If a cable is unpatched, processParamsAndInputs() (OrangeLineCommon.hpp) skips
			refreshing OL_statePoly for it entirely, leaving the last known (possibly stale)
			value in place. Force sane defaults here (gate = off, pitch/velocity/lane CV = 0)
			so unplugging any of the 4 cables behaves like a normal unpatched Eurorack input
			instead of freezing on the last value - most importantly, unplugging GATE always
			releases held notes instead of leaving them stuck on.
		*/
		for (int s = 0; s < NUM_SOURCES; s++)
		{
			bool sourceGateConnected = getInputConnected (GATE_IN_INPUT + s);
			bool sourceVoctConnected = getInputConnected (VOCT_IN_INPUT + s);
			bool sourceVelConnected  = getInputConnected (VEL_IN_INPUT  + s);
			bool sourceLaneConnected = getInputConnected (LANE_IN_INPUT + s);
			for (int c = 0; c < POLY_CHANNELS; c++)
			{
				bool  gateIn  = sourceGateConnected && OL_statePoly[(GATE_IN_INPUT + s) * POLY_CHANNELS + c] > 5.f;
				float pitchIn = sourceVoctConnected ? quantize (OL_statePoly[(VOCT_IN_INPUT + s) * POLY_CHANNELS + c]) : 0.f;
				float velIn   = sourceVelConnected  ? OL_statePoly[(VEL_IN_INPUT  + s) * POLY_CHANNELS + c] : 0.f;
				float laneCV  = sourceLaneConnected ? OL_statePoly[(LANE_IN_INPUT + s) * POLY_CHANNELS + c] : 0.f;
				int   laneIn  = int(clamp (round (laneCV * 12.f), 0.f, float(NUM_LANES - 1)));

				sourceGate[s][c]     = gateIn;
				sourcePitch[s][c]    = pitchIn;
				sourceVelocity[s][c] = velIn;
				sourceLane[s][c]     = laneIn;
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

	v1 panel: plain functional layout, no custom artwork yet (see res/CVLanesWork.svg).
	Jack coordinates below match 1:1 the grid documented in res/CVLanesWork.svg so the
	guide layer and the actual widget stay in sync once custom panels are authored.
*/
struct CVLanesWidget : ModuleWidget
{
	LanesExtStrips extStrips;

	CVLanesWidget(CVLanes *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CVLanesOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CVLanesBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CVLanesDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		/*
			Jack centers below are measured directly from the "Controls" layer of
			res/CVLanesWork.svg (2 input blocks x 4 columns x 8 rows), so the widget lines
			up exactly with the panel art. These are already true jack centers, so
			calculateCoordinates() is called with a 0 offset. The two blocks' row-zero Y
			differ very slightly (sub-0.05mm, an artifact of the panel art) - kept as measured
			rather than forced to a single shared value.
		*/
		static const float ROW_PITCH  = 13.208002f;
		static const float COL_PITCH  = 9.398000f;
		static const float INPUT_BLOCK_X[2] = { 7.112002f, 51.054000f };
		static const float INPUT_ROW0_Y[2]  = { 21.866941f, 21.823511f };
		// Input blocks: Sources 1-8 (block 0) and Sources 9-16 (block 1)
		for (int block = 0; block < 2; block++)
		{
			float blockX = INPUT_BLOCK_X[block];
			float row0Y  = INPUT_ROW0_Y[block];
			for (int row = 0; row < 8; row++)
			{
				int source = block * 8 + row;
				float y = row0Y + row * ROW_PITCH;
				addInput (createInputCentered<PJ301MPort> (calculateCoordinates (blockX + 0.f * COL_PITCH, y, 0.f), module, VOCT_IN_INPUT + source));
				addInput (createInputCentered<PJ301MPort> (calculateCoordinates (blockX + 1.f * COL_PITCH, y, 0.f), module, GATE_IN_INPUT + source));
				addInput (createInputCentered<PJ301MPort> (calculateCoordinates (blockX + 2.f * COL_PITCH, y, 0.f), module, VEL_IN_INPUT  + source));
				addInput (createInputCentered<PJ301MPort> (calculateCoordinates (blockX + 3.f * COL_PITCH, y, 0.f), module, LANE_IN_INPUT + source));
			}
		}

		// Connection lights are gone (superseded by the seam/logo-cover mechanism - see
		// ExpanderBridge.hpp's own file comment).

		addOrangeLineTouchPorts (this, module, NUM_INPUTS, NUM_OUTPUTS,
			module ? &module->OL_touchInPort : nullptr, module ? &module->OL_touchOutPort : nullptr, module ? &module->OL_touchVisible : nullptr);

		addLanesExtStrips(this, 86.36f, &extStrips);

		if (module)
			module->widgetReady = true;
	}

	void step() override
	{
		if (module)
			updateLanesExtStrips(&extStrips, module, module->leftExpander.module, module->rightExpander.module);
		ModuleWidget::step();
	}

	struct CVLanesStyleItem : MenuItem
	{
		CVLanes *module;
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

	// Plain right-click-menu text field for customName (see its own member comment) - standard
	// Rack pattern: ui::TextField fires onChange() on every edit, so just mirror `text` into the
	// module straight away rather than needing a separate "confirm" step. Verbatim copy of
	// Morpheus's own MorpheusNameField pattern.
	struct CVLanesNameField : ui::TextField
	{
		CVLanes *module;
		void onChange(const ChangeEvent &e) override
		{
			if (module)
				module->customName = text;
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		CVLanes *module = dynamic_cast<CVLanes *>(this->module);
		assert(module);

		MenuLabel *nameLabel = new MenuLabel();
		nameLabel->text = "Name";
		menu->addChild(nameLabel);

		CVLanesNameField *nameField = new CVLanesNameField();
		nameField->module = module;
		nameField->text = module->customName;
		nameField->box.size = Vec(140.f, 20.f);
		menu->addChild(nameField);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		addOrangeLineTouchMenuItem(menu, module->OL_touchInPort, module->OL_touchOutPort, &module->OL_touchVisible);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		CVLanesStyleItem *style1Item = new CVLanesStyleItem();
		style1Item->text = "Orange"; //
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		CVLanesStyleItem *style2Item = new CVLanesStyleItem();
		style2Item->text = "Bright"; //
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		CVLanesStyleItem *style3Item = new CVLanesStyleItem();
		style3Item->text = "Dark"; //
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelCVLanes = createModel<CVLanes, CVLanesWidget>("CVLanes");
