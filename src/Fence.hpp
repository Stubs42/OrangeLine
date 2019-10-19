/*
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

/*
 * Fence.hpp
 *
 * Author: Dieter Stubler
 */
#include "plugin.hpp"

#define AUDIO_VOLTAGE  5.f

#define MODE_MIN       0.f
#define MODE_RAW       0.f
#define MODE_QTZ       1.f
#define MODE_SHPR      2.f
#define MODE_MAX       2.f

//
// Value Ranges
//
#define SEMITONE       (1.f / 12.f)
#define PRECISION       0.001f

#define LOW_MIN_RAW   -10.f
#define LOW_MAX_RAW   (10.f - PRECISION)
#define HIGH_MIN_RAW (-10.f + PRECISION)
#define HIGH_MAX_RAW   10.f
#define STEP_MIN_RAW    PRECISION
#define STEP_MAX_RAW   10.f

#define LOW_MIN_QTZ   -10.f
#define LOW_MAX_QTZ    10.f - SEMITONE
#define HIGH_MIN_QTZ  -10.f + SEMITONE
#define HIGH_MAX_QTZ   10.f
#define STEP_MIN_QTZ    0.f
#define STEP_MAX_QTZ   (SEMITONE * 11.f)

#define LOW_MIN_SHPR   -5.f
#define LOW_MAX_SHPR   (5.f - PRECISION)
#define HIGH_MIN_SHPR (-5.f + PRECISION)
#define HIGH_MAX_SHPR   5.f
#define STEP_MIN_SHPR   PRECISION
#define STEP_MAX_SHPR  10.f

//
// Defaults
//

//
// DEFAULT_QTZ and DEFAULT_SHPR are mutally exclusive !
// If DEFAULT_QTZ and/or DEFAULT_SHPR are changed change initial param config in Fence.cpp also!
//
#define DEFAULT_MODE      MODE_QTZ
#define DEFAULT_QTZ       (DEFAULT_MODE == MODE_QTZ)
#define DEFAULT_SHPR      (DEFAULT_MODE == MODE_SHPR)

#define DEFAULT_LOW_RAW   -10.f
#define DEFAULT_HIGH_RAW   10.f
#define DEFAULT_LINK_RAW    0.f
#define DEFAULT_STEP_RAW  STEP_MIN_RAW

// qtz range defaults to [C4, B4]
#define DEFAULT_LOW_QTZ     0.f
#define DEFAULT_HIGH_QTZ   (11.f / 12.f)
#define DEFAULT_LINK_QTZ    1.f
#define DEFAULT_STEP_QTZ    STEP_MIN_QTZ

#define DEFAULT_LOW_SHPR  -AUDIO_VOLTAGE
#define DEFAULT_HIGH_SHPR  AUDIO_VOLTAGE
#define DEFAULT_LINK_SHPR   0.f
#define DEFAULT_STEP_SHPR  STEP_MIN_SHPR

// VOctWidget Types
#define TYPE_VOCT 1
#define TYPE_STEP 2

//
// Change bits
//
#define CHG_LOW          0x1
#define CHG_HIGH         0x2
#define CHG_STEP         0x4
#define CHG_MODE         0x8
#define CHG_LOW_CV      0x10
#define CHG_HIGH_CV     0x20
#define CHG_STEP_CV     0x40
#define CHG_TRG         0x80
#define CHG_EFF_LOW    0x100
#define CHG_EFF_HIGH   0x200
#define CHG_LINK       0x400
#define CHG_EFF_STEP   0x800
#define CHG_CV        0x1000

#define CHG_ALL   0xFFFFFFFF

struct Trigger : dsp::SchmittTrigger {
	bool process(float value) {
		if (state) {
			if (value <= 0.1f) {
				state = false;
			}
		}
		else {
			if (value >= 1.0f) {
				state = true;
				return true;
			}
		}
		return false;
	}
};
