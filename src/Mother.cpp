/*
	Mother.cpp
 
	Code for the OrangeLine module Mother

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
#include <sys/time.h>

#include "Mother.hpp"

struct Mother : Module {

	#include "OrangeLineCommon.hpp"

	// ********************************************************************************************************************************
	/*
		Module member variables
	*/
	char headText[MAX_TEXT_SIZE + 1] = "  headText";
	char headDisplayText[MAX_TEXT_SIZE + 1] = "    INIT";
	char tmpHeadText[13] = "????????????";
	int tmpHeadCounter = -1;
	int reflectCounter = -1;
	int reflectFateCounter = -1;
	char rootText[3];
	char childText[3];

	const char *notes[NUM_NOTES] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	const char *interval = "0123456789ABO";
	
	int   	effectiveRoot  = 0;
	int   	effectiveScale = 0;
	float 	effectiveScaleDisplay = 0.f;
	int   	effectiveChild = 0;
	int   	jsonOnOffBaseIdx = ONOFF_JSON;
	int   	jsonWeightBaseIdx = WEIGHT_JSON;
	float	motherWeights[NUM_NOTES];
	float  	pCvOut[NUM_NOTES];
	float	pProb[NUM_NOTES];
	bool	pMother[NUM_NOTES];
	float	pNoteIdx[NUM_NOTES];
	int		noteIdx;
	int		noteIdxIn;
	int		pCnt = 0;
	float	pTotal = 0;
	bool	triggered = false;
	int 	cvChannels = 0;
	int		trgChannels = 0;
	int		rndChannels = 0;
	int		channels = 0;
	float   oldCvOut[POLY_CHANNELS];	//	Old value of cvOut to detect changes for triggering trgOut
	float   oldCvIn [POLY_CHANNELS];	//	Old value of cvOut to detect changes of quantized input
	int	  	headScrollTimer = 0;

	bool	headClick;
	int		scaleSelected = -1;
		
	#include "MotherJsonLabels.hpp"
	#include "MotherScales.hpp"

// ********************************************************************************************************************************
/*
	Initialization
*/
	/**
		Constructor

		Typically just calls initializeInstance included from OrangeLineCommon.hpp
	*/
	Mother () {
		initializeInstance ();
	}

	/**
		Method to set stateTypes != default types set by initializeInstance() in OrangeLineModule.hpp
		which is called from constructor
	*/
	void moduleInitStateTypes () {
		for (int i = ONOFF_PARAM; i <= ONOFF_PARAM_LAST; i ++)
	     	setStateTypeParam  (i, STATE_TYPE_TRIGGER);
     	setStateTypeInput  (TRG_INPUT, STATE_TYPE_TRIGGER);

		setCustomChangeMaskInput (TRG_INPUT, CHG_TRG_IN);
		setInPoly                (TRG_INPUT, true);

		setCustomChangeMaskInput (CV_INPUT, CHG_CV_IN);
		setInPoly          (CV_INPUT, true);

		setInPoly          (RND_INPUT, true);

     	setStateTypeOutput (GATE_OUTPUT, STATE_TYPE_TRIGGER);
		setOutPoly         (GATE_OUTPUT, true);

		setOutPoly         (CV_OUTPUT, true);
		setOutPoly         (POW_OUTPUT, true);
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
		for (int i = 0; i < NUM_JSONS; i ++) {
			setJsonLabel (i, jsonLabel[i]);
		}
		#pragma GCC diagnostic pop
	}

	/**
		Initialize param configs
	*/
	inline void moduleParamConfig () {
		configParam (    ROOT_PARAM,  0.f, 11.f, 0.f, "Root"        , "", 0.f, 1.f / 12.f, 0.f);
		setCustomChangeMaskParam (ROOT_PARAM, CHG_ROOT);
		configParam (     SCL_PARAM,  1.f, 12.f, 1.f, "Mother Scale", "", 0.f, 1.f / 12.f,  -1.f / 12.f);
		setCustomChangeMaskParam ( SCL_PARAM, CHG_SCL);
		configParam (    CHLD_PARAM,  0.f, 11.f, 0.f, "Child Scale" , "", 0.f, 1.f / 12.f, 0.f);
		setCustomChangeMaskParam (CHLD_PARAM, CHG_CHLD);
		configParam (FATE_AMT_PARAM,  0.f,  6.f, 0.f, "Amount"      , "", 0.f, 1.f, 0.f);
		configParam (FATE_SHP_PARAM,  0.f,  1.f, 0.5f, "Shape"       , "", 0.f, 1.f, 0.f);

		for (int i = NUM_NOTES - 1; i >= 0; i--) {
    		configParam (WEIGHT_PARAM + i,  0.f, 1.f, 0.5f, "Weight",      "", 0.f, 1.f, 0.f);
			setCustomChangeMaskParam (WEIGHT_PARAM + i, CHG_WEIGHT);
    		configParam ( ONOFF_PARAM + i,  0.f, 1.f, 0.f,  "Note On/Off", "", 0.f, 1.f, 0.f);
			setCustomChangeMaskParam ( ONOFF_PARAM + i, CHG_ONOFF);
        }
		setCustomChangeMaskInput ( SCL_INPUT, CHG_SCL);
		setCustomChangeMaskInput (CHLD_INPUT, CHG_CHLD);
		setCustomChangeMaskInput (ROOT_INPUT, CHG_ROOT);
		isGate (GATE_OUTPUT) = true;
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
		memset (rootText , 0, sizeof (rootText));
		memset (childText, 0, sizeof (childText));

		memset (oldCvOut, 0.f, sizeof (oldCvOut));
		memset ( oldCvIn, 0.f, sizeof ( oldCvIn));

		strcpy (childText, notes[int(round (getStateParam (CHLD_PARAM)))]);
		strcpy ( rootText, notes[int(round (getStateParam (ROOT_PARAM)))]);

		strcpy (headDisplayText, headText);
	}

	/**
		Method to set the module in its initial state after adding to a patch or right click initialize
	*/
	void moduleReset () {
		for (int i = ONOFF_JSON; i <= ONOFF_JSON_LAST; i ++) {
			if ((i - ONOFF_JSON) % NUM_NOTES == 0)
				setStateJson (i, 1.f);
			else
				setStateJson (i, 0.f);
		}
		for (int i = WEIGHT_JSON; i <= WEIGHT_JSON_LAST; i ++)
			setStateJson (i, 0.5f);

		updateMotherWeights ();
		struct timeval tp;
		gettimeofday(&tp, NULL);
		unsigned long int seed = tp.tv_sec * 1000 + tp.tv_usec / 1000;
		init_genrand (seed);
	}

// ********************************************************************************************************************************
/*
	Module specific utility methods
*/
	void setHeadScale () {
		int bufIdx = 0;
		char buf[MAX_TEXT_SIZE + 1];
		buf[0] = '\0';
		int start = ONOFF_JSON + effectiveScale * NUM_NOTES + 1;
		int lightIdx;
		int i = 1;
		for (int jsonIdx = start; jsonIdx < start + NUM_NOTES; jsonIdx ++) {
			lightIdx = (jsonIdx - start + effectiveChild) % NUM_NOTES;
			if (getStateJson (start + lightIdx) > 0.f) {
				buf[bufIdx] = interval[i];
				bufIdx++;
				buf[bufIdx] = '\0';
				i = 0;
			}
			i ++;
		}
		for (long unsigned int i = 0; i < SCALE_KEYS; i ++) {
			if (!strcmp (buf, scaleKeys[i]))
				strcpy (buf, scaleNames[i]);
		}
		strcpy (headText, buf);
		strcpy (headDisplayText, buf);
		headScrollTimer = TEXT_SCROLL_PRE_DELAY;
	}

	inline void setTmpHead (char *tmpHead) {
		strcpy (headDisplayText, tmpHead);
		tmpHeadCounter = TMP_HEAD_DURATION;
	}

	inline void checkTmpHead () {
		if (tmpHeadCounter >= 0) {
			if (tmpHeadCounter == 0) {
				strcpy (headDisplayText, headText);
			}
			tmpHeadCounter--;
		}
	}

// ********************************************************************************************************************************
/*
	Methods called directly or indirectly called from process () in OrangeLineCommon.hpp
*/
	/**
		Module specific process method called from process () in OrangeLineCommon.hpp
	*/
	inline void moduleProcess (const ProcessArgs &args) {
		if (reflectCounter >= 0)
			reflectCounter --;

		if (reflectFateCounter >= 0)
			reflectFateCounter --;

		checkTmpHead ();

		if (styleChanged) {
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
		
		bool rndConnected = getInputConnected (RND_INPUT);
		bool trgConnected = getInputConnected (TRG_INPUT);
		triggered = false;
		float cvOut = 0.f;
		float cvIn;
		float semiAmt = getStateParam (FATE_AMT_PARAM) / 12;
		float shp = getStateParam (FATE_SHP_PARAM);
		float d;
		float weight;
		float rnd;
		bool grab = false;
		bool fromMother = false;

		if ((customChangeBits & CHG_TRG_IN) || (!trgConnected && (customChangeBits & CHG_CV_IN))) {
			cvChannels = inputs[CV_INPUT].getChannels ();
			trgChannels = inputs[TRG_INPUT].getChannels ();
			rndChannels = inputs[RND_INPUT].getChannels ();
			channels = cvChannels > trgChannels ? cvChannels : trgChannels;
			setOutPolyChannels (CV_OUTPUT, channels);
			setOutPolyChannels (GATE_OUTPUT, channels);
			setOutPolyChannels (POW_OUTPUT, channels);
			for (int channel = 0; channel < channels; channel++) {
				weight = 0;
				grab = false;
				int cvInPolyIdx   =    CV_INPUT * POLY_CHANNELS + channel;
				int trgInPolyIdx  =   TRG_INPUT * POLY_CHANNELS + channel;
				int rndInPolyIdx  =   RND_INPUT * POLY_CHANNELS + channel;
				int trgOutPolyIdx = GATE_OUTPUT * POLY_CHANNELS + channel;
				int cvOutPolyIdx  =   CV_OUTPUT * POLY_CHANNELS + channel;
				int powOutPolyIdx =  POW_OUTPUT * POLY_CHANNELS + channel;

				if ((!trgConnected && OL_inStateChangePoly[cvInPolyIdx]) || OL_inStateChangePoly[trgInPolyIdx]) {
					reflectCounter = REFLECT_DURATION;
					if (rndConnected && channel <= rndChannels)
						init_genrand (int(round (OL_statePoly[rndInPolyIdx] * 100000)));
					
					pCnt = 0;
					pTotal = 0.f;

					if (OL_inStateChangePoly[trgInPolyIdx] && (!getInputConnected (CV_INPUT) || channel >= cvChannels))
						cvIn = genrand_real () * 20.f - 10.f;
					else
						cvIn = OL_statePoly[cvInPolyIdx] - (float(effectiveRoot) / 12.f);
					cvOut = quantize (cvIn);
					int note = note(cvOut);
					noteIdx = (note - effectiveChild + NUM_NOTES) % NUM_NOTES;
					noteIdxIn = (note (cvIn) - effectiveChild + NUM_NOTES) % NUM_NOTES;
					if (getStateJson (jsonOnOffBaseIdx + note) > 0.f && semiAmt > 0.f) {
						d = abs (cvIn - cvOut);
						pCvOut[pCnt] = cvOut;
						weight = getStateParam (WEIGHT_PARAM + noteIdx);
						if (weight == 0.5f && effectiveChild > 0) {
							fromMother = true;
							weight = motherWeights[noteIdx];
						}
						else
							fromMother = false;
						if (weight == 1.f) {
							pProb[0] = 1.f;
							pNoteIdx[0] = noteIdx;
							pMother[0] = fromMother;
							pTotal = 1.f;
							pCnt = 1;
							grab = true;
						}
						else {
							pProb[pCnt] = weight;
							pNoteIdx[pCnt] = noteIdx;
							pMother[pCnt] = fromMother;
							pTotal += weight;
							pCnt ++;
						}
					}
					if ((getStateJson (jsonOnOffBaseIdx + note) == 0.f || semiAmt > 0.f) && !grab) {
						float step = -SEMITONE;
						if (cvIn > cvOut)
							step = SEMITONE;
						for (int i = 0; i < NUM_NOTES; i++) {
							cvOut += step;
							note = note (cvOut);
							noteIdx = (note - effectiveChild + NUM_NOTES) % NUM_NOTES;
							if (getStateJson (jsonOnOffBaseIdx + note) > 0.f) {
								if (semiAmt == 0.f)
									break;
								d = abs (cvIn - cvOut);
								if (d > semiAmt)
									break;
								pCvOut[pCnt] = cvOut;
								weight = getStateParam (WEIGHT_PARAM + noteIdx);
								if (weight == 0.5f && effectiveChild > 0) {
									fromMother = true;
									weight = motherWeights[noteIdx];
								}
								else
									fromMother = false;
								if (weight == 1.f) {
									pProb[0] = 1.f;
									pNoteIdx[0] = noteIdx;
									pMother[0] = fromMother;
									pTotal =  1.f;
									pCnt = 1;
									grab = true;
									break;
								}
								if (weight > 0) {
									//
									// New polynomial style:
									// 
									//	Ronald supposed:
									// 		f(x) = 1 - (x/s)**n
									//
									//	Python test:
									//		def weight(dist, span, shape):
									//    		if shape < 0.5:
									//        		shape *= 2
									//    		else:
									//        		shape = (shape - 0.5) * 20
									//    	return 1.0 - (float(dist)/float(span))**shape
									//
									if (shp < 1) {
										float f = (1.f - pow (abs (d - SEMITONE) / semiAmt, shp < 0.5 ? shp * 2.f : 1.f + (shp - 0.5) * 20));
										weight *= f;
									}
									//
									// Old linear style (faster but not as fancy) elts keep it for now if performance is needed:
									//	weight *= ((shp == 1.f) ? 1.f : 1.f - (d * (1 - shp)) / semiAmt);
									//

									pProb[pCnt] = weight;
									pNoteIdx[pCnt] = noteIdx;
									pMother[pCnt] = fromMother;
									pTotal += weight;
									pCnt ++;
								}
							}
							step = step > 0.f ? -step - SEMITONE : -step + SEMITONE;
						}
					}
					if (pCnt > 0 && !grab) {
						float sum = 0.f;
						rnd = genrand_real () * pTotal;
						for (int i = 0; i < pCnt; i++) {
							sum += pProb[i];
							if (sum >= rnd) {
								cvOut = pCvOut[i];
								noteIdx = pNoteIdx[i];
								break;
							}
						}
					}
					if (pCnt == 0) {
						pCvOut[0] = cvOut;
						pProb[0] = weight;
						pNoteIdx[0] = noteIdx;
						pMother[0] = fromMother;
						pTotal = weight;
						pCnt = 1;
					}

					cvOut += (float(effectiveRoot) / 12.f);
					cvOut = quantize (cvOut);

					if (abs (cvOut - oldCvOut[channel]) > PRECISION) {
						if (OL_statePoly[NUM_INPUTS * POLY_CHANNELS + trgOutPolyIdx] != 10.f) {
							OL_statePoly[NUM_INPUTS * POLY_CHANNELS + trgOutPolyIdx] = 10.f;
							OL_outStateChangePoly[trgOutPolyIdx] = true;
						}
						note = note (cvOut);
						noteIdx = (note - effectiveChild + NUM_NOTES) % NUM_NOTES;
						weight = getStateParam (WEIGHT_PARAM + noteIdx);
						if (weight == 0.5f && effectiveChild > 0)
							weight = motherWeights[noteIdx];
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + powOutPolyIdx] = weight * 10.f;
						OL_outStateChangePoly[powOutPolyIdx] = true;
					}
					if (OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx] != cvOut) {
						OL_statePoly[NUM_INPUTS * POLY_CHANNELS + cvOutPolyIdx]  = cvOut;
						OL_outStateChangePoly[cvOutPolyIdx] = true;
					}
					oldCvOut[channel] = cvOut;
				}
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

		effectiveScale = (int(getStateParam (SCL_PARAM)) - 1 + note (getStateInput (SCL_INPUT))) % NUM_NOTES;
		effectiveScaleDisplay = float(effectiveScale + 1);
		effectiveChild = (int(getStateParam (CHLD_PARAM)) + note (getStateInput (CHLD_INPUT))) % NUM_NOTES;
		/*
			quantize down to next lower active note if effectiveChild is not in scale
		*/
		while (effectiveChild > 0) {
			if (getStateJson (ONOFF_JSON + effectiveScale * NUM_NOTES + effectiveChild) > 0.f)
				break;
			effectiveChild --;			
		}

		effectiveRoot  = (int(getStateParam (ROOT_PARAM)) + note (getStateInput (ROOT_INPUT))) % NUM_NOTES;

		int jsonOnOffBaseIdx = ONOFF_JSON + effectiveScale * NUM_NOTES;
		int jsonIdx;
		float f;

		bool didSelectScale = false;
		float selectedNotes[NUM_NOTES];
		selectedNotes[0] = 1.f;
		if (scaleSelected >= 0) {
			// Scale selected from right click menu
			int noteIdx = 1;
			int interval;
			for (const char *p = scaleKeys[scaleSelected]; *p != '\0'; p++) {
				interval = *p - '0' - 1;
				while (interval--)
					if (noteIdx < NUM_NOTES) {
						selectedNotes[noteIdx] = 0.f;
						noteIdx++;
					}
				if (noteIdx < NUM_NOTES) {
					selectedNotes[noteIdx] = 1.f;
					noteIdx++;
				}
			}
			didSelectScale = true;
			scaleSelected = -1;
			customChangeBits |= CHG_ONOFF;
		}

		if (customChangeBits & CHG_ONOFF) {
			if (effectiveChild == 0) {
				for (int paramIdx = ONOFF_PARAM, i = 0; paramIdx <= ONOFF_PARAM_LAST; paramIdx ++, i++) {
					if ((inChangeParam (paramIdx) && paramIdx - ONOFF_PARAM != 0) || didSelectScale) {
						jsonIdx = jsonOnOffBaseIdx + i;
						if (didSelectScale)
							f = selectedNotes[i];
						else {
							f = getStateJson (jsonIdx);
							if (f == 0.f)
								f = 1.f;
							else
								f = 0.f;
						}
						setStateJson (jsonIdx, f);
					}
				}
			}
			didSelectScale = false;
		}

		jsonWeightBaseIdx = WEIGHT_JSON + effectiveScale * NUM_CHLD * NUM_NOTES + effectiveChild * NUM_NOTES;
		if (customChangeBits & CHG_WEIGHT) {
			float weight;
			int pct;
			for (int paramIdx = WEIGHT_PARAM, i = 0; paramIdx <= WEIGHT_PARAM_LAST; paramIdx ++, i++) {
				if (inChangeParam (paramIdx) && initialized) {
					jsonIdx = jsonWeightBaseIdx + i;
					weight = getStateParam (paramIdx);
					setStateJson (jsonIdx, weight);
					pct = int(round (weight * 100.f));
					if (getStateJson (jsonOnOffBaseIdx + (i + effectiveChild) % NUM_NOTES) == 0.f)
						snprintf(tmpHeadText, sizeof(tmpHeadText), "Wgt. %02d  n/a", paramIdx - WEIGHT_PARAM + 1);
					else if (pct == 50 && getStateParam (CHLD_PARAM) > 0.f)
						snprintf(tmpHeadText, sizeof(tmpHeadText), "Wgt. %02d  DNA", paramIdx - WEIGHT_PARAM + 1);
					else if (pct == 100)
						snprintf(tmpHeadText, sizeof(tmpHeadText), "Wgt. %02d GRAB", paramIdx - WEIGHT_PARAM + 1);
					else 
						snprintf(tmpHeadText, sizeof(tmpHeadText), "Wgt. %02d %3d%%", paramIdx - WEIGHT_PARAM + 1, pct);
					setTmpHead (tmpHeadText);
				}
			}
		}
		if (customChangeBits & (CHG_SCL | CHG_CHLD)) {
			for (int paramIdx = WEIGHT_PARAM, i = 0; paramIdx <= WEIGHT_PARAM_LAST; paramIdx ++, i++) {
				jsonIdx = jsonWeightBaseIdx + i;
				setStateParam (paramIdx, getStateJson (jsonIdx));
			}
			updateMotherWeights ();
		}
		if ((inChangeParam (FATE_AMT_PARAM) || inChangeParam (FATE_SHP_PARAM))  && initialized) {
			reflectFateCounter = REFLECT_FATE_DURATION;
		}
	}

	void updateMotherWeights () {
		int onOffJsonBaseIdx = ONOFF_JSON + effectiveScale * NUM_NOTES;
		int steps;
		int motherIdx;
		int childIdx;
		for (int i = 0; i < NUM_NOTES; i++) {
			childIdx = i;

			steps = 0;
			while (childIdx > 0) {
				if (getStateJson (onOffJsonBaseIdx + (childIdx + effectiveChild) % NUM_NOTES) > 0.f) {
					steps++;
				}
				childIdx--;
			}
			for (motherIdx = 0; motherIdx < NUM_NOTES; motherIdx ++) {
				if (getStateJson (onOffJsonBaseIdx + motherIdx) > 0.f) {
					steps --;
					if (steps < 0)
						break;
				}
			}
			motherWeights[i] = getStateJson (WEIGHT_JSON + effectiveScale * NUM_CHLD * NUM_NOTES + motherIdx);
		}
	}

	inline void setNoteLight (int lightIdx, float state) {

		int color = 0x000000;
		float weight;
		float motherWeight;
		float reflectWeight = 0;
		int r = 0, g = 0, b = 0;

		if (reflectFateCounter > 0) {
			float semiAmt = getStateParam (FATE_AMT_PARAM) / 12;
			float shp = getStateParam (FATE_SHP_PARAM);
			float d;
			if (lightIdx < NUM_NOTES / 2)
				d = abs (5.5f / 12.f - lightIdx / 12.f);
			else
				d = abs (lightIdx / 12.f - 5.5f / 12.f);
			if (d > semiAmt)
				weight = 0;
			else
				weight = (1.f - pow (abs (d - SEMITONE) / semiAmt, shp < 0.5 ? shp * 2.f : 1.f + (shp - 0.5) * 20));
			r = 0;
			g = int(weight * 255.f);
			b = 0;
			color = (r << 16) + (g << 8) + b;
		}
		else {
			weight = getStateParam (WEIGHT_PARAM + lightIdx);
			if (channels == 1) {
				if (state > 0.f || (reflectCounter > 0 && noteIdxIn == lightIdx)) {
					int r = 0, g = 0, b = 0;
					if (reflectCounter > 0) {
						if (noteIdxIn == lightIdx) {
								g = 32;
								b = 32;
								r = 32;
						}
						else {
							int i;
							for (i = 0; i < pCnt; i++) {
								if (pNoteIdx[i] == lightIdx) {
									reflectWeight = pProb[i];
									break;
								}
							}
							if (reflectWeight == 1.f) {
								if (weight == 1.f)
									r = 255;
								else if (pMother[i]) {
									b = 64;
									r = 196;
								}
							}
							else {
								if (pMother[i])
									b = int(reflectWeight * 255.f);
								else
									g = int(reflectWeight * 255.f);
							}
						}
						if (noteIdx == lightIdx) {
							r = 255;
							g = 255;
							b = 255;
						}
					}
					else {
						if (weight == 0.5f && effectiveChild > 0) {
							r = 0;
							g = 0;
							motherWeight = motherWeights[lightIdx];
							if (motherWeight == 1.f) {
								r = 196;
								b = 64;
							}
							else
								b = int(motherWeight * 223.f + 32.f);
						}
						else if (weight == 1.f) {
							r = 255;
							g = 0;
							b = 0;
						}
						else {
							r = 0;
							g = int(weight * 223.f + 32.f);
							b = 0;
						}
					}
					color = (r << 16) + (g << 8) + b;
				}
				else {
					color = 0x000000;
				}
			}
			else {
				if (state > 0.f) {
					bool hit = false;
					if (reflectCounter > 0) {
						for (int channel = 0; channel < channels; channel++) {
							int note = note (oldCvOut[channel]);
							noteIdx = (note - effectiveChild + NUM_NOTES) % NUM_NOTES;
							if (noteIdx == lightIdx) {
								r = 255;
								g = 255;
								b = 255;
								hit = true;
								break;
							}
						}
					}
					if (!hit) {
						if (weight == 0.5f && effectiveChild > 0) {
							r = 0;
							g = 0;
							motherWeight = motherWeights[lightIdx];
							if (motherWeight == 1.f) {
								r = 196;
								b = 64;
							}
							else
								b = int(motherWeight * 223.f + 32.f);
						}
						else if (weight == 1.f) {
							r = 255;
							g = 0;
							b = 0;
						}
						else {
							r = 0;
							g = int(weight * 223.f + 32.f);
							b = 0;
						}
					}
					color = (r << 16) + (g << 8) + b;
				}
				else {
					color = 0x000000;
				}
			}
		}
		setRgbLight (NOTE_LIGHT_01_RGB + 3 * lightIdx, color);
	}	

	/*
		Non standard reflect processing results to user interface components and outputs
	*/
	inline void moduleReflectChanges () {
		if ((customChangeBits & (CHG_ONOFF | CHG_SCL | CHG_CHLD)) || !initialized) 
			setHeadScale ();

		if ((customChangeBits & (CHG_SCL | CHG_CHLD | CHG_ROOT)) || !initialized) {
			strcpy (childText, notes[(effectiveChild + effectiveRoot) % NUM_NOTES]);
		}
		if ((customChangeBits & CHG_ROOT) || !initialized) {
			strcpy ( rootText, notes[effectiveRoot]);
		}
		if (triggered || (customChangeBits & (CHG_WEIGHT | CHG_ONOFF | CHG_SCL | CHG_CHLD)) || !initialized || reflectCounter >= 0 || reflectFateCounter >= 0) {
			int start = ONOFF_JSON + effectiveScale * NUM_NOTES;
			int lightIdx;
			for (int jsonIdx = start; jsonIdx < start + NUM_NOTES; jsonIdx ++) {
				lightIdx = (jsonIdx - start - effectiveChild + NUM_NOTES) % NUM_NOTES;
				setNoteLight (lightIdx, getStateJson (jsonIdx));
			}
		}
		if (headScrollTimer > 0)
			headScrollTimer --;
	}
};

// ********************************************************************************************************************************
/*
	Module widget implementation
*/

/**
	Main Module Widget
*/
struct MotherWidget : ModuleWidget {
	TextWidget *headWidget;
	TextWidget *rootWidget;
	NumberWidget *scaleWidget;
	TextWidget *childWidget;

    char scaleBuffer[3];

	MotherWidget(Mother *module) {
        RoundSmallBlackKnob *knob;
		LargeLight<RedGreenBlueLight> *light;
        setModule (module);
		setPanel (APP->window->loadSvg(asset::plugin (pluginInstance, "res/Mother.svg")));

		if (module) {
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MotherBright.svg")));
			brightPanel->visible = false;
			
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MotherDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (24.098 + 4, 128.5 - 41.724 - 4)), module, ROOT_PARAM);
        knob->snap = true;
   		addParam (knob);

        knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (12.034 + 4, 128.5 - 75.057 - 4)), module,  SCL_PARAM);
        knob->snap = true;
   		addParam (knob);

        knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (25.686 + 4, 128.5 - 75.057 - 4)), module, CHLD_PARAM);
        knob->snap = true;
   		addParam (knob);

        knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (13.621 + 4, 128.5 - 99.822 - 4)), module, FATE_AMT_PARAM);
        knob->snap = true;
   		addParam (knob);

        knob = createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (24.098 + 4, 128.5 - 99.822 - 4)), module, FATE_SHP_PARAM);
        // knob->snap = true;
   		addParam (knob);

		for (int i = 0; i < NUM_NOTES; i++) {
			addParam (createParamCentered<LEDButton>                   (mm2px (Vec (3.175 + 2.381, 128.5 - 105.886 - 2.381 + float(i) * (105.886 - 25.559) / 11.f)), module, ONOFF_PARAM + NUM_NOTES - i - 1));
			light = createLightCentered<LargeLight<RedGreenBlueLight>> (mm2px (Vec (3.249 + 2.308, 128.5 - 105.96  - 2.308 + float(i) * (105.960 - 25.663) / 11.f)), module, NOTE_LIGHT_01_RGB + ((NUM_NOTES - i - 1) * 3));
			light->bgColor = nvgRGBA(0, 0, 0, 255);
	 		addChild (light);
		}

		for (int i = 0; i < NUM_NOTES; i++) {
			addParam (createParamCentered<Trimpot> (mm2px (Vec (37.014  + 3.15 ,128.5 - 105.118 - 3.15 + i * (105.118 - 24.790) / 11.f)),  module, WEIGHT_PARAM + NUM_NOTES - i - 1));
		}
		
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (13.416 + 4.2 , 128.5 - 41.515 - 4.2)), module, ROOT_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (11.829 + 4.2 , 128.5 - 64.375 - 4.2)), module,  SCL_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (25.481 + 4.2 , 128.5 - 64.375 - 4.2)), module, CHLD_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (13.416 + 4.2 , 128.5 - 25.640 - 4.2)), module,  RND_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec ( 3.256 + 4.2 , 128.5 -  9.765 - 4.2)), module,   CV_INPUT));
		addInput (createInputCentered<PJ301MPort>   (mm2px (Vec (13.416 + 4.2 , 128.5 -  9.765 - 4.2)), module,  TRG_INPUT));

		addOutput (createOutputCentered<PJ301MPort>	(mm2px (Vec (23.894 + 4.2 , 128.5 - 25.640 - 4.2)), module,  POW_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>	(mm2px (Vec (23.894 + 4.2 , 128.5 -  9.765 - 4.2)), module, GATE_OUTPUT));
		addOutput (createOutputCentered<PJ301MPort>	(mm2px (Vec (34.128 + 4.2 , 128.5 -  9.765 - 4.2)), module,   CV_OUTPUT));

		const char *text;

		text = (module != nullptr ? module->headDisplayText : nullptr);
        headWidget = TextWidget::create (mm2px (Vec(3.183 - 0.25 - 0.35, 128.5 - 115.271)), module, text, "Major", 12, (module ? &(module->headScrollTimer) : nullptr));
		headWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
        addChild (headWidget);

		text = (module != nullptr ? module->rootText : nullptr);
        rootWidget = TextWidget::create (mm2px (Vec(24.996 - 0.25, 128.5 - 52.406)), module, text, "C", 2, nullptr);
		rootWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
        addChild (rootWidget);

		float *pvalue  = (module != nullptr ? &(module->effectiveScaleDisplay) : nullptr);
        scaleWidget = NumberWidget::create (mm2px (Vec(12.931 - 0.25, 128.5 - 86.537)), module, pvalue, 1.f, "%2.0f", scaleBuffer, 2);
		scaleWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));

        addChild (scaleWidget);

		text = (module != nullptr ? module->childText : nullptr);
        childWidget = TextWidget::create (mm2px (Vec(26.742 - 0.25, 128.5 - 86.537)), module, text, "C", 2, nullptr);
		childWidget->pStyle = (module == nullptr ? nullptr : &(module->OL_state[STYLE_JSON]));
        addChild (childWidget);
	}

	struct MotherScalesItem : MenuItem {
		Mother *module;
		
		struct MotherScaleItem : MenuItem {
			Mother *module;
			int scaleIdx = 0;

			void onAction(const event::Action &e) override {
				module->scaleSelected = scaleIdx;
			}
		};

		Menu *createChildMenu() override {
			Menu *menu = new Menu;
			MotherScaleItem *scaleItem;
			for (int scale = 0; scale < SCALE_KEYS; scale++) {
				
				scaleItem = new MotherScaleItem ();
				scaleItem->module = module;
				scaleItem->scaleIdx = scale;
				scaleItem->text = module->scaleNames[scale];
				scaleItem->rightText = module->scaleKeys[scale];

				menu->addChild(scaleItem);
			}
			return menu;
		}
	};

	struct MotherStyleItem : MenuItem {
		Mother *module;
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
		if (module) {
			MenuLabel *spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			Mother *module = dynamic_cast<Mother*>(this->module);
			assert(module);

			MenuLabel *styleLabel = new MenuLabel();
			styleLabel->text = "Mother";
			menu->addChild(styleLabel);

			MotherScalesItem *motherScalesItem = new MotherScalesItem();
			motherScalesItem->module = module;		
			motherScalesItem->text = "Scales";
			motherScalesItem->rightText = RIGHT_ARROW;
			menu->addChild(motherScalesItem);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			styleLabel = new MenuLabel();
			styleLabel->text = "Style";
			menu->addChild(styleLabel);

			MotherStyleItem *style1Item = new MotherStyleItem();
			style1Item->text = "Orange";// 
			style1Item->module = module;
			style1Item->style= STYLE_ORANGE;
			menu->addChild(style1Item);

			MotherStyleItem *style2Item = new MotherStyleItem();
			style2Item->text = "Bright";// 
			style2Item->module = module;
			style2Item->style= STYLE_BRIGHT;
			menu->addChild(style2Item);
				
			MotherStyleItem *style3Item = new MotherStyleItem();
			style3Item->text = "Dark";// 
			style3Item->module = module;
			style3Item->style= STYLE_DARK;
			menu->addChild(style3Item);

			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);
			
		}
	}
};

Model *modelMother = createModel<Mother, MotherWidget> ("Mother");
