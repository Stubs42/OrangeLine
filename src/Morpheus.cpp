/*
	Morpheus.cpp

	Code for the OrangeLine module Morpheus

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

#include "Morpheus.hpp"

struct Morpheus : Module
{
	float oldClkInputVoltage = 0;
    int polyChannels = 1;
	bool hadReset = true;
	bool isShiftLeft[POLY_CHANNELS];
	bool isShiftRight[POLY_CHANNELS];

#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	bool widgetReady = false;
	float selectedMem = 1.f;
	NumberWidget *memWidget;

#include "MorpheusJsonLabels.hpp"

	// ********************************************************************************************************************************
	/*
		Initialization
	*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Morpheus()
	{
		initializeInstance();
	}
	/*
		Method to decide whether this call of process() should be skipped
	*/
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
   		setStateTypeInput  (RST_INPUT, STATE_TYPE_TRIGGER);
   		setStateTypeInput  (CLK_INPUT, STATE_TYPE_TRIGGER);
   		setStateTypeInput  (RCL_INPUT, STATE_TYPE_TRIGGER);
   		setStateTypeInput  (STO_INPUT, STATE_TYPE_TRIGGER);

   		setInPoly (LOCK_INPUT, true);
   		setInPoly (BALANCE_INPUT, true);
   		setInPoly (LOOP_LEN_INPUT, true);   
    	setInPoly (HLD_INPUT, true);
    	setInPoly (RND_INPUT, true);
    	setInPoly (SHIFT_LEFT_INPUT, true);
    	setInPoly (SHIFT_RIGHT_INPUT, true);
    	setInPoly (CLR_INPUT, true);
    	setInPoly (EXT_INPUT, true);
    	setInPoly (REC_INPUT, true);
    	setInPoly (GTP_INPUT, true);
    	setInPoly (SCL_INPUT, true);
    	setInPoly (OFS_INPUT, true);

   		setOutPoly (SRC_OUTPUT, true);
   		setOutPoly (GATE_OUTPUT, true);
		OL_isSteadyGate[GATE_OUTPUT] = true;
   		setStateTypeOutput (GATE_OUTPUT, STATE_TYPE_TRIGGER);
		// OL_isSteadyGate[GATE_OUTPUT] = true;
   		setOutPoly (CV_OUTPUT, true);
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
//	void configParam(int paramId, float minValue, float maxValue, float defaultValue, std::string label = "", std::string unit = "", 
//                   float displayBase = 0.f, float displayMultiplier = 1.f, float displayOffset = 0.f) {

   		configParam (LOCK_PARAM,        0.f, 100.f,  0.f, "Lock",      "%", 0.f, 1.f, 0.f);
   		configParam (BALANCE_PARAM,     0.f, 100.f,100.f, "Source(0%) to Random(100%) Balance", "%", 0.f, 1.f, 0.f);

   		configParam (LOOP_LEN_PARAM,    1.f,  MAX_LOOP_LEN, 16.f, "Loop Length", "", 0.f, 1.f, 0.f);
        paramQuantities[LOOP_LEN_PARAM]->snapEnabled = true;
        configParam (MEM_UP_PARAM,      0.f,   1.f,  0.f, "Memory Up",     "", 0.f, 1.f, 0.f);
        configParam (MEM_DOWN_PARAM,    0.f,   1.f,  0.f, "Memory Down",     "", 0.f, 1.f, 0.f);
        configParam (STO_PARAM,         0.f,   1.f,  0.f, "Store Selected Memory Slot",     "", 0.f, 1.f, 0.f);
        configParam (RCL_PARAM,         0.f,   1.f,  0.f, "Recall Selected Memory Slot",    "", 0.f, 1.f, 0.f);
		configSwitch(HLD_ON_PARAM, 0.0f, 1.0f, 0.0f, "Hold", {"Off", "On"});

        configParam (RND_PARAM,         0.f,  10.f,  0.f, "Randomize (Lock 0%, Balance 100% Shortcut)",    "", 0.f, 1.f, 0.f);
        configParam (SHIFT_LEFT_PARAM,  0.f,  10.f,  0.f, "Shift Left One Step",     "", 0.f, 1.f, 0.f);
        configParam (SHIFT_RIGHT_PARAM, 0.f,  10.f,  0.f, "Shift Right One Step",    "", 0.f, 1.f, 0.f);
        configParam (CLR_PARAM,         0.f,  10.f,  0.f, "Clear Loop (CV -> 0V)",        "", 0.f, 1.f, 0.f);

		configSwitch(EXT_ON_PARAM, 0.0f, 1.0f, 0.0f, "Source", {"MEM", "EXT"});
        configParam (REC_PARAM,         0.f,  10.f,  0.f, "Record from External Source (Lock 0%, Balance 0% Shortcut)",    "", 0.f, 1.f, 0.f);
   		configParam (GTP_PARAM,         0.f, 100.f, 50.f, "Random Gate Probability", "%", 0.f, 1.f, 0.f);
   		configParam (SCL_PARAM,       -10.f,  10.f, 10.f, "Random CV Scale",          "", 0.f, 1.f, 0.f);
   		configParam (OFS_PARAM,       -10.f,  10.f,  0.f, "Random CV Offset",         "", 0.f, 1.f, 0.f);

   		configOutput (SRC_OUTPUT, "Source (Ext/Active Memory Slot)");
   		configOutput (GATE_OUTPUT, "Gate");
		configOutput (CV_OUTPUT, "CV");
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
        setStateJson(POLY_CHANNELS_JSON, 0.f);

        for (int i = STEPS_JSON; i < STEPS_JSON + POLY_CHANNELS * MAX_LOOP_LEN; i++) {
            setStateJson(i, 0.f);
        }
        for (int i = MEM_JSON; i < MEM_JSON + MEM_SLOTS * POLY_CHANNELS * MAX_LOOP_LEN; i++) {
            setStateJson(i, 0.f);
        }
        for (int i = HEAD_JSON; i < HEAD_JSON + POLY_CHANNELS; i++) {
            setStateJson(i, 0.f);
        }
		setStateJson(ACTIVE_MEM_JSON, 0.f);
		setStateJson(SELECTED_MEM_JSON, 0.f);
		setStateJson(EXT_ON_JSON, 0.f);
		setStateJson(HLD_ON_JSON, 0.f);
		setStateJson(GATE_IS_TRG_JSON, 0.f);
		setStateJson(LOAD_ON_MEM_CV_CHANGE_JSON, 0.f);
		for (int i = 0; i < POLY_CHANNELS; i++) {
			isShiftLeft[i] = false;
			isShiftRight[i] = false;
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

	inline float getChannelLock(int channel)
	{
		float lock = getStateParam(LOCK_PARAM);
		if (getInputConnected(LOCK_INPUT)) {
			int channels = inputs[LOCK_INPUT].getChannels();
            if (channels == 1) {
				return lock + OL_statePoly[LOCK_INPUT * POLY_CHANNELS] * 10; // input is scaled so lock 10V is lock 100 %
            }
			else if (channels >= channel) {
				return lock + OL_statePoly[LOCK_INPUT * POLY_CHANNELS + channel] * 10; // input is scaled so lock 10V is lock 100 %
            }
		}
		return lock;
	}

	inline float getChannelBalance(int channel)
	{
		float balance = getStateParam(BALANCE_PARAM);
		if (getInputConnected(BALANCE_INPUT)) {
			int channels = inputs[BALANCE_INPUT].getChannels();
            if (channels == 1) {
				return balance + OL_statePoly[BALANCE_INPUT * POLY_CHANNELS] * 10; // input is scaled so balance 10V is lock 100 %
            }
			else if (channels >= channel) {
				return balance + OL_statePoly[BALANCE_INPUT * POLY_CHANNELS + channel] *10;  // input is scaled so balance 10V is lock 100 %
            }
		}
		return balance;
	}

	inline float getChannelLoopLength(int channel)
	{
		float loopLen = 0.f;
		if (getInputConnected(LOOP_LEN_INPUT)) {
			int channels = inputs[LOOP_LEN_INPUT].getChannels();
            if (channels == 1) {
				loopLen = floor(OL_statePoly[LOOP_LEN_INPUT * POLY_CHANNELS] * 100.f); // input is scaled so 0.16 is length 16
            }
			else if (channels >= channel) {
				loopLen = floor(OL_statePoly[LOOP_LEN_INPUT * POLY_CHANNELS + channel] * 100.f); // input is scaled so 0.16 is length 16
            }
		}
		if (loopLen < 1.f) {
			loopLen = getStateParam(LOOP_LEN_PARAM);
		}
		return loopLen;
	}

	inline float getChannelHld(int channel)
	{
		if (getStateJson(HLD_ON_JSON) > 0.f) {
			return 10.f;
		}
		if (getInputConnected(HLD_INPUT)) {
			int channels = inputs[HLD_INPUT].getChannels();
            if (channels == 1) {
				return OL_statePoly[HLD_INPUT * POLY_CHANNELS] > 5.f ? 10.f : 0.f;
            }
			else if (channels >= channel) {
				return OL_statePoly[HLD_INPUT * POLY_CHANNELS + channel] > 5.f ? 10.f : 0.f;
            }
		}
		return 0.f;
	}

	inline float getChannelRec(int channel)
	{
		if (getStateParam(REC_PARAM) > 5.f) {
			return getStateParam(REC_PARAM);
		}
		if (getInputConnected(REC_INPUT)) {
			int channels = inputs[REC_INPUT].getChannels();
            if (channels == 1) {
				return OL_statePoly[REC_INPUT * POLY_CHANNELS] > 5.f ? 10.f : 0.f;
            }
			else if (channels >= channel) {
				return OL_statePoly[REC_INPUT * POLY_CHANNELS + channel] > 5.f ? 10.f : 0.f;;
            }
		}
		return 0.f;
	}

	inline float getChannelRnd(int channel)
	{
		if (getStateParam(RND_PARAM) > 5.f) {
			return getStateParam(RND_PARAM);
		}
		if (getInputConnected(RND_INPUT)) {
			int channels = inputs[RND_INPUT].getChannels();
            if (channels == 1) {
				return OL_statePoly[RND_INPUT * POLY_CHANNELS] > 5.f ? 10.f : 0.f;
            }
			else if (channels >= channel) {
				return OL_statePoly[RND_INPUT * POLY_CHANNELS + channel] > 5.f ? 10.f : 0.f;;
            }
		}
		return 0.f;
	}

	inline float getChannelClr(int channel)
	{
		if (getStateParam(CLR_PARAM) > 5.f) {
			return getStateParam(CLR_PARAM);
		}
		if (getInputConnected(CLR_INPUT)) {
			int channels = inputs[CLR_INPUT].getChannels();
            if (channels == 1) {
				return OL_statePoly[CLR_INPUT * POLY_CHANNELS] > 5.f ? 10.f : 0.f;
            }
			else if (channels >= channel) {
				return OL_statePoly[CLR_INPUT * POLY_CHANNELS + channel] > 5.f ? 10.f : 0.f;;
            }
		}
		return 0.f;
	}

	inline float getChannelGtp(int channel)
	{
		if (getInputConnected(GTP_INPUT)) {
			int channels = inputs[GTP_INPUT].getChannels();
            if (channels == 1) {
				return OL_statePoly[GTP_INPUT * POLY_CHANNELS] * 10.f; // input is scaled so 5V is Probability 50%
            }
			else if (channels >= channel) {
				return OL_statePoly[GTP_INPUT * POLY_CHANNELS + channel] * 10.f; // input is scaled so 5V is Probability 50%
            }
		}
		return getStateParam(GTP_PARAM);
	}

	inline float getChannelScl(int channel)
	{
		if (getInputConnected(SCL_INPUT)) {
			int channels = inputs[SCL_INPUT].getChannels();
            if (channels == 1) {
				return OL_statePoly[SCL_INPUT * POLY_CHANNELS];
            }
			else if (channels >= channel) {
				return OL_statePoly[SCL_INPUT * POLY_CHANNELS + channel];
            }
		}
		return getStateParam(SCL_PARAM);
	}

	inline float getChannelOfs(int channel)
	{
		if (getInputConnected(OFS_INPUT)) {
			int channels = inputs[OFS_INPUT].getChannels();
            if (channels == 1) {
				return OL_statePoly[OFS_INPUT * POLY_CHANNELS];
            }
			else if (channels >= channel) {
				return OL_statePoly[OFS_INPUT * POLY_CHANNELS + channel];
            }
		}
		return getStateParam(OFS_PARAM);
	}

	inline void shiftChannel(int channel, int direction)
	{
		int loopLen = getChannelLoopLength(channel);
		int start = STEPS_JSON + channel * MAX_LOOP_LEN;
		
		for (int i = 0; i < loopLen - 1; i++) {
			int idx1 = (loopLen + ( i      * direction)) % loopLen;
			int idx2 = (loopLen + ((i + 1) * direction)) % loopLen;
			float tmp = getStateJson(start + idx1);
            setStateJson(start + idx1,   getStateJson(start + idx2));
            setStateJson(start + idx2, tmp);
        }
	}

	inline void loadChannel(int channel)
	{
		int loopLen = floor (getChannelLoopLength(channel));
		int startStep = STEPS_JSON + channel * MAX_LOOP_LEN;
		int startMem = MEM_JSON + getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + channel * MAX_LOOP_LEN;
		for (int i = 0; i < loopLen; i++) {
            setStateJson(startStep + i, getStateJson(startMem + i));
        }
	}

	inline void storeChannel(int channel)
	{
		int loopLen = floor (getChannelLoopLength(channel));
		int startStep = STEPS_JSON + channel * MAX_LOOP_LEN;
		int startMem = MEM_JSON + getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + channel * MAX_LOOP_LEN;
		for (int i = 0; i < loopLen; i++) {
            setStateJson(startMem + i, getStateJson(startStep + i));
        }
	}

	inline void advanceHeads()
	{
		// DEBUG("advanceHeads()");
		for (int channel = 0; channel < polyChannels; channel++) {
			int loopLen = floor (getChannelLoopLength(channel));
			setStateJson(HEAD_JSON + channel, float((int(getStateJson(HEAD_JSON + channel)) + 1) % loopLen));
		    // DEBUG(" head[%d] = %lf", channel, getStateJson(HEAD_JSON + channel));
		}
	}

	/**
		Module specific process method called from process () in OrangeLineCommon.hpp
	*/
	inline void moduleProcess(const ProcessArgs &args)
	{
		// Handle Style Change
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

		// Handle Reset
		if (changeInput (RST_INPUT) || OL_initialized == false) {
			for (int i = HEAD_JSON; i < HEAD_JSON + POLY_CHANNELS; i++) {
				setStateJson(i, 0.f);
			}
			hadReset = true;
		}

		// get Number of channels to process
		polyChannels = getStateJson(POLY_CHANNELS_JSON); 
		if (polyChannels == 0) {
			polyChannels = 1;
			// channels is auto, derive maximum channels of all polyphonic inputs
			for (int i = LOCK_INPUT; i <= OFS_INPUT; i ++) {
				if (getInputConnected(i)) {
					int channels = inputs[i].getChannels();
					if (channels > polyChannels) {
						polyChannels = channels;
					}
				}
			}
		}

		// Handle HLD_ON Button presses
		if (inChangeParam (HLD_ON_PARAM)) {	//	User clicked on tr/gt button
			setStateJson (HLD_ON_JSON, getStateParam(HLD_ON_PARAM));
		}

		// Handle Shift
        for (int channel = 0; channel < polyChannels; channel ++) {

			// Do not Shift channels on hold
			if (getChannelHld(channel) > 5.f) continue;

			// Shift all channels left if button is pressed
			if(changeParam(SHIFT_LEFT_PARAM) && getStateParam(SHIFT_LEFT_PARAM) > 0.f) {
				shiftChannel(channel, 1);
			}
			// Shift channel on polyphonic shift left input
            if (getInputConnected(SHIFT_LEFT_INPUT)) {
                if (channel < inputs[SHIFT_LEFT_INPUT].getChannels()) { 
                    if (OL_statePoly[SHIFT_LEFT_INPUT * POLY_CHANNELS + channel] > 0.f) {
						if (!isShiftLeft[channel]) {
		                    shiftChannel(channel, 1);
							isShiftLeft[channel] = true;
						}
						else{
							isShiftLeft[channel] = false;
						}
					}
				}
            }

			// shift all channels right if button is pressed
			if(changeParam(SHIFT_RIGHT_PARAM) && getStateParam(SHIFT_RIGHT_PARAM) > 0.f) {
				shiftChannel(channel, -1);
			}
			// shift channel on polyphonic shift right input
            if (getInputConnected(SHIFT_RIGHT_INPUT)) {
                if (channel < inputs[SHIFT_RIGHT_INPUT].getChannels()) {
                    if (OL_statePoly[SHIFT_RIGHT_INPUT * POLY_CHANNELS + channel] > 0.f) {
						if (!isShiftRight[channel]) {
		                    shiftChannel(channel, -1);
							isShiftRight[channel] = true;
						}
						else{
							isShiftRight[channel] = false;
						}
					}
				}
            }
        }

		if (getInputConnected(MEM_INPUT)) {
			int m = floor(getStateInput(MEM_INPUT) * 10.f); // input is scaled so 1.6 is mem slot 16
			if (m < 1) m = 1;
			if (m > MEM_SLOTS) m = MEM_SLOTS;
			if (getStateJson(ACTIVE_MEM_JSON) != m - 1.f) {
				setStateJson(SELECTED_MEM_JSON, m - 1.f);
				setStateJson(ACTIVE_MEM_JSON, m - 1.f);
				if (getStateJson(LOAD_ON_MEM_CV_CHANGE_JSON) == 1.0f) {
					for (int channel = 0; channel < polyChannels; channel++) {
						if (getChannelHld(channel) < 5.f) {
							loadChannel(channel);
						}
					}
				}
			}
		}
		else {
			// Handle MEM up/down
			int upDown = 0;
			if(changeParam(MEM_UP_PARAM) && getStateParam(MEM_UP_PARAM) > 0.f) {
				upDown = 1;
			}	
			if(changeParam(MEM_DOWN_PARAM) && getStateParam(MEM_DOWN_PARAM) > 0.f) {
				upDown = -1;
			}
			if (upDown != 0) {
				setStateJson(SELECTED_MEM_JSON, fmod(getStateJson(SELECTED_MEM_JSON) + MEM_SLOTS + upDown, MEM_SLOTS));
			}
		}
		selectedMem = getStateJson(SELECTED_MEM_JSON) + 1.f;

		// Handel MEM RCL
		if ((changeParam(RCL_PARAM) && (getStateParam(RCL_PARAM) > 0.f)) || 
		    (getInputConnected(RCL_INPUT) && changeInput (RCL_INPUT) && (getStateInput(RCL_INPUT) > 0.f))
		   ) {
			if (getStateJson(SELECTED_MEM_JSON) == getStateJson(ACTIVE_MEM_JSON)) {
				for (int channel = 0; channel < polyChannels; channel++) {
					if (getChannelHld(channel) < 5.f) {
						loadChannel(channel);
					}
				}
			}
			else {
				setStateJson(ACTIVE_MEM_JSON, getStateJson(SELECTED_MEM_JSON));
			}
		}	

		// Handel MEM STO
		if((changeParam(STO_PARAM) && (getStateParam(STO_PARAM) > 0.f)) || 
		   (getInputConnected(STO_INPUT) && changeInput (STO_INPUT) && (getStateInput(STO_INPUT) > 0.f))
		  ) {
			setStateJson(ACTIVE_MEM_JSON, getStateJson(SELECTED_MEM_JSON));
			for (int channel = 0; channel < polyChannels; channel++) {
				storeChannel(channel);
			}
		}	

		// Handle EXT_ON Button presses
		if (inChangeParam (EXT_ON_PARAM)) {	//	User clicked on tr/gt button
			setStateJson (EXT_ON_JSON, getStateParam(EXT_ON_PARAM));
		}

		// Main CLK Processing here
		if (changeInput (CLK_INPUT) && getStateInput(CLK_INPUT) > 0.f) {
			// DEBUG("polyChannels = %d", polyChannels);
			if (!hadReset) {
				advanceHeads();
			}
			else {
				hadReset = false;
			}
			// Process Channels
			for (int channel = 0; channel < polyChannels; channel ++) {
				int head = getStateJson(HEAD_JSON + channel);
				float scl = getChannelScl(channel);
				float ofs = getChannelOfs(channel);
				// HLD takes precedence no change of steps when channel is on hold
				if (getChannelHld(channel) < 5.f) {
					if (getStateJson(EXT_ON_JSON) > 0.f && getChannelRec(channel) > 5.f) {
						// User is holding REC and External Source is enabled, so copy xternal Source to step
						setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head, OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel]);
					}					
					else if (getChannelClr(channel) > 5.f) {
						// DEBUG("clearing channel %d at %d", channel, head);
						setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head, ofs);
					}
					else {
						bool randomize = false;
						if (getChannelRnd(channel) > 5.f ) {
							randomize = true;
						}
						else {
							float lock = getChannelLock(channel);
							if (lock / 100.f < getRandom(&globalRandom)) {
								// DEBUG("replace channel %d at %d", channel, head);
								// We have to replace the currect step
								// check for src or random
								float balance = getChannelBalance(channel);
								// DEBUG("balance = %lf", balance);
								if (balance / 100.f > getRandom(&globalRandom)) {
									// DEBUG(" from RND");
									// replace from random
									randomize = true;
								}
								else {
									// replace from source
									if (getStateJson(EXT_ON_JSON) > 0.f) {
										// source is extern
										// DEBUG(" from EXT");
										setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head, 
											OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel]);
									}
									else {
										// source is MEM
										// DEBUG(" from MEM");
										int step = STEPS_JSON + channel * MAX_LOOP_LEN + head;
										int mem = MEM_JSON + getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + channel * MAX_LOOP_LEN + head;
										setStateJson(step, getStateJson(mem));
									}
								}
							}
						}
						if (randomize) {
							// User is holding RND so we force a randomize of the currect step
							if (scl < 0 && ofs == -10.f) {
								// bipolar (scl < 0) and ofs -10 does not make sense
								// so we treat it special using the current step as offset
								ofs = getStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head);
							} 						
							float low = ofs;
							if (scl < 0) {
								low += scl;
							}
							float high = ofs + abs(scl);
							if (low < -10.f) low = -10.f;
							if (high > 10.f) high = 10.f;
							float rnd = low + getRandom(&globalRandom) * (high - low);
							// DEBUG ("scl = %lf, ofs = %lf, low = %lf, high = %lf, rnd = %lf", scl, ofs, low, high, rnd);
							setStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head, rnd);
						}
					}
				}
				// MEM display color
				if (getStateJson(ACTIVE_MEM_JSON) != getStateJson(SELECTED_MEM_JSON)) {
					memWidget->customForegroundColor = true;
					memWidget->foregroundColor = nvgRGB(255,0,0);
				}
				else {
					memWidget->customForegroundColor = false;
				}
				// SRC_OUTPUT
				if (getStateJson(EXT_ON_JSON) > 0.f) {
					int cvOutPolyIdx = SRC_OUTPUT * POLY_CHANNELS + channel;
					OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] = OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel];
					OL_outStateChangePoly[cvOutPolyIdx] = true;
					// setStateOutPoly(SRC_OUTPUT, channel, OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel]);
					// DEBUG(" SRC_OUTPUT[%d] from EXT-> %lf", channel, OL_statePoly[EXT_INPUT * POLY_CHANNELS + channel]);
				}
				else {
					int mem = MEM_JSON + getStateJson(ACTIVE_MEM_JSON) * POLY_CHANNELS * MAX_LOOP_LEN + channel * MAX_LOOP_LEN + head;
					setStateOutPoly(SRC_OUTPUT, channel, getStateJson(mem));
					// DEBUG(" SRC_OUTPUT[%d] from MEM-> %lf", channel, getStateJson(mem));
				}

				float cv = getStateJson(STEPS_JSON + MAX_LOOP_LEN * channel + head);
				// GATE_OUTPUT
				float gtp = getChannelGtp(channel); // Gate Probailitoy between 0 an 100
				// cv range is -10 to 10 so we scale the cv to [0:00]
				float gCv = 0;
				if (scl > 0) { 
					// unipolar cv
					gCv = cv * 10.f; // scaled to [0:100]
				}
				else {
					// bipolar cv
					gCv = (cv + 10.f) / 0.2f; // scaled to [0:100]
				}
				if (gCv < 0) gCv = 0;
				if (gCv > 100) gCv = 100;
				gCv = 100 - gCv;
				int gateOutPolyIdx = GATE_OUTPUT * POLY_CHANNELS + channel;
				if ((gCv + 0.001) < gtp) { // +0.001 to guarantee no gate is produced when gtp is 0
					OL_statePoly[NUM_INPUTS * POLY_CHANNELS + gateOutPolyIdx] = 10.f;
					OL_outStateChangePoly[gateOutPolyIdx] = true;
				}
				else {
					// Do not signal a output change, this will taken as a trigger !
					// OL_outStateChangePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
					outputs[GATE_OUTPUT].setVoltage (0.f, channel);
				}
				if (getStateJson(GATE_IS_TRG_JSON) != 1.f) {
					OL_isGatePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
					OL_isSteadyGate[GATE_OUTPUT * POLY_CHANNELS + channel] = true;
				}
				else {
					OL_isGatePoly[GATE_OUTPUT * POLY_CHANNELS + channel] = false;
					OL_isSteadyGate[GATE_OUTPUT * POLY_CHANNELS + channel] = false;
				}
				// CV_OUTPUT
				setStateOutPoly(CV_OUTPUT, channel, cv);
				// DEBUG(" CV_OUTPUT[%d] -> %lf", channel, cv);
			}
		}
		setOutPolyChannels(SRC_OUTPUT, polyChannels);
		setOutPolyChannels(GATE_OUTPUT, polyChannels);
		setOutPolyChannels(CV_OUTPUT, polyChannels);
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
		setStateLight (HLD_ON_LIGHT, getStateJson (HLD_ON_JSON) * 255.f);
		setStateLight (EXT_ON_LIGHT, getStateJson (EXT_ON_JSON) * 255.f);
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

/**
	Main Module Widget
*/
struct MorpheusWidget : ModuleWidget
{
	char memBuffer[3];

	MorpheusWidget(Morpheus *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorpheusOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorpheusBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MorpheusDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		addParam (createParamCentered<RoundLargeBlackKnob> (calculateCoordinates (11.430, 10.161, OFFSET_RoundLargeBlackKnob),  module, LOCK_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 1.891, 12.306, OFFSET_PJ301MPort), module, LOCK_INPUT));
		addParam (createParamCentered<RoundLargeBlackKnob> (calculateCoordinates (26.670, 10.161, OFFSET_RoundLargeBlackKnob),  module, BALANCE_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (40.499, 12.306, OFFSET_PJ301MPort), module, BALANCE_INPUT));

        RoundSmallBlackKnob *knob;
        knob = createParamCentered<RoundSmallBlackKnob> (calculateCoordinates ( 2.096, 34.117, OFFSET_RoundSmallBlackKnob),  module, LOOP_LEN_PARAM);
        knob->snap = true;
        addParam (knob);

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 1.891, 44.818, OFFSET_PJ301MPort), module, LOOP_LEN_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (13.367, 35.705, OFFSET_LEDButton),  module, MEM_UP_PARAM));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (13.367, 46.373, OFFSET_LEDButton),  module, MEM_DOWN_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (21.195, 44.818, OFFSET_PJ301MPort), module, MEM_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (32.671, 35.705, OFFSET_LEDButton),  module, STO_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (30.897, 44.818, OFFSET_PJ301MPort), module, STO_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (42.323, 35.705, OFFSET_LEDButton),  module, RCL_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (40.499, 44.818, OFFSET_PJ301MPort), module, RCL_INPUT));

		addParam (createParamCentered<VCVLatch> (calculateCoordinates  ( 3.789, 61.182, OFFSET_LEDButton),  module, HLD_ON_PARAM));
 		addChild (createLightCentered<LargeLight<YellowLight>>	(calculateCoordinates  ( 3.789, 61.182, OFFSET_LEDButton), module, HLD_ON_LIGHT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 1.891, 68.948, OFFSET_PJ301MPort), module, HLD_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (13.365, 61.105, OFFSET_LEDButton),  module, RND_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (11.543, 68.948, OFFSET_PJ301MPort), module, RND_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (23.019, 61.105, OFFSET_LEDButton),  module, SHIFT_LEFT_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (21.195, 68.948, OFFSET_PJ301MPort), module, SHIFT_LEFT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (32.669, 61.105, OFFSET_LEDButton),  module, SHIFT_RIGHT_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (30.847, 68.948, OFFSET_PJ301MPort), module, SHIFT_RIGHT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (42.321, 61.105, OFFSET_LEDButton),  module, CLR_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (40.499, 68.948, OFFSET_PJ301MPort), module, CLR_INPUT));

		addParam (createParamCentered<VCVLatch> (calculateCoordinates  ( 3.789, 86.594, OFFSET_LEDButton),  module, EXT_ON_PARAM));
 		addChild (createLightCentered<LargeLight<YellowLight>>	(calculateCoordinates  ( 3.789, 86.594, OFFSET_LEDButton), module, EXT_ON_LIGHT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 1.891, 95.364, OFFSET_PJ301MPort), module, EXT_INPUT));
		addParam (createParamCentered<LEDButton> (calculateCoordinates  (13.368, 86.594, OFFSET_LEDButton),  module, REC_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (11.543, 95.364, OFFSET_PJ301MPort), module, REC_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (21.400, 84.902, OFFSET_RoundSmallBlackKnob),  module, GTP_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (21.195, 95.364, OFFSET_PJ301MPort), module, GTP_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (31.052, 84.902, OFFSET_RoundSmallBlackKnob),  module, SCL_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (30.847, 95.364, OFFSET_PJ301MPort), module, SCL_INPUT));
		addParam (createParamCentered<RoundSmallBlackKnob> (calculateCoordinates (40.704, 84.902, OFFSET_RoundSmallBlackKnob),  module, OFS_PARAM));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (40.499, 95.364, OFFSET_PJ301MPort), module, OFS_INPUT));

		addInput (createInputCentered<PJ301MPort> (calculateCoordinates ( 1.891,111.369, OFFSET_PJ301MPort), module, RST_INPUT));
		addInput (createInputCentered<PJ301MPort> (calculateCoordinates (11.543,111.369, OFFSET_PJ301MPort), module, CLK_INPUT));
   		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (21.195,111.369, OFFSET_PJ301MPort),  module, SRC_OUTPUT));
   		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (30.847,111.369, OFFSET_PJ301MPort),  module, GATE_OUTPUT));
   		addOutput (createOutputCentered<PJ301MPort>	(calculateCoordinates (40.499,111.369, OFFSET_PJ301MPort),  module, CV_OUTPUT));

		float *pvalue = (module != nullptr ? &(module->selectedMem) : nullptr);
		if (module) {
			module->memWidget = NumberWidget::create(mm2px(Vec(22.3 - 0.25, 40.2f)), module, pvalue, 1.f, "%2.0f", memBuffer, 2);
			module->memWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
			addChild(module->memWidget);
		}
		else {
			NumberWidget *w = NumberWidget::create(mm2px(Vec(22.3 - 0.25, 40.2f)), module, pvalue, 1.f, "%2.0f", memBuffer, 2);
			w->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
			addChild(w);
		}
		if (module)
			module->widgetReady = true;
	}

	struct GateIsTrgItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[GATE_IS_TRG_JSON] == 0.f)
				module->OL_setOutState(GATE_IS_TRG_JSON, 1.f);
			else
				module->OL_setOutState(GATE_IS_TRG_JSON, 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[GATE_IS_TRG_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct LoadOnMemCvChangeItem : MenuItem
	{
		Morpheus *module;
		void onAction(const event::Action &e) override
		{
			if (module->OL_state[LOAD_ON_MEM_CV_CHANGE_JSON] == 0.f)
				module->OL_setOutState(LOAD_ON_MEM_CV_CHANGE_JSON, 1.f);
			else
				module->OL_setOutState(LOAD_ON_MEM_CV_CHANGE_JSON, 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[LOAD_ON_MEM_CV_CHANGE_JSON] == 1.0f) ? "✔" : "";
		}
	};

	struct MorpheusStyleItem : MenuItem
	{
		Morpheus *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = ( module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

    struct PolyChannelsItem : MenuItem
	{
		Morpheus *module;

		struct PolyChannelItem : MenuItem
		{
			Morpheus *module;
			int channels;
			void onAction(const event::Action &e) override
			{
				module->OL_setOutState(POLY_CHANNELS_JSON, float(channels));
			}
			void step() override
			{
				if (module)
					rightText = (module != nullptr && module->OL_state[POLY_CHANNELS_JSON] == channels) ? "✔" : "";
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			PolyChannelItem *polyChannelItem;

			for (int channel = 0; channel < 17; channel++)
			{
				polyChannelItem = new PolyChannelItem();
				polyChannelItem->module = module;
				polyChannelItem->channels = channel;
                if (channel == 0) {
                    polyChannelItem->text = "Auto";
                }
                else {
    				polyChannelItem->text = module->channelNumbers[channel - 1];
                }
				polyChannelItem->setSize(Vec(70, 20));

				menu->addChild(polyChannelItem);
			}
			return menu;
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Morpheus *module = dynamic_cast<Morpheus *>(this->module);
		assert(module);


		MenuLabel *behaviourLabel = new MenuLabel();
		behaviourLabel->text = "Behaviour";
		menu->addChild(behaviourLabel);

		GateIsTrgItem *gateisTrgItem = new GateIsTrgItem();
		gateisTrgItem->module = module;
		gateisTrgItem->text = "Output Trg instead of Gate";
		menu->addChild(gateisTrgItem);

		LoadOnMemCvChangeItem *loadOnMemCvChangeItem = new LoadOnMemCvChangeItem();
		loadOnMemCvChangeItem->module = module;
		loadOnMemCvChangeItem->text = "Load on Mem CV Change";
		menu->addChild(loadOnMemCvChangeItem);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

        MenuLabel *polyphonyLabel = new MenuLabel();
        polyphonyLabel->text = "Polyphony";
        menu->addChild(polyphonyLabel);

        PolyChannelsItem *polyChannelsItem = new PolyChannelsItem();
        polyChannelsItem->module = module;
        polyChannelsItem->text = "Channels";
        polyChannelsItem->rightText = RIGHT_ARROW;
        menu->addChild(polyChannelsItem);

        spacerLabel = new MenuLabel();
        menu->addChild(spacerLabel);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		MorpheusStyleItem *style1Item = new MorpheusStyleItem();
		style1Item->text = "Orange"; //
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		MorpheusStyleItem *style2Item = new MorpheusStyleItem();
		style2Item->text = "Bright"; //
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		MorpheusStyleItem *style3Item = new MorpheusStyleItem();
		style3Item->text = "Dark"; //
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);
	}
};

Model *modelMorpheus = createModel<Morpheus, MorpheusWidget>("Morpheus");
