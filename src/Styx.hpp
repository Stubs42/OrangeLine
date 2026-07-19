/*
	Styx.hpp

	Author: Dieter Stubler

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
// STYX v1: bidirectional Morpheus tape/memory editor. No real jacks at all yet (pure Expander,
// standalone/X-family-input modes deferred - see the approved plan). Always 16 rows for v1
// (8/16-row mode toggle deferred); panel width IS resizable (see StyxResizeHandle in Styx.cpp).
#define STYX_NUM_ROWS POLY_CHANNELS

// No real jacks at all in v1 - Touch/Wakeup-Ready exists to relay a real CV signal through a real
// jack at low latency, which doesn't apply here (same reasoning as CC2CV/RECALL/MidiBus).
#define OL_TOUCH_DISABLED

#include "OrangeLine.hpp"
#include "XShared.hpp"
#include "XOShared.hpp"
#include "StyxShared.hpp"

// Minimum panel width, in HP - enough room for the fixed left-hand controls (name display,
// per-row MEM/TAPE + FOLLOW buttons) plus at least a handful of step columns.
#define STYX_MIN_WIDTH_HP 16

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	STYLE_JSON,
	PANEL_WIDTH_HP_JSON, // current panel width in HP - persisted, applied to box.size.x on load

	// The non-adjacent stay-connected target host id used to live here as a float
	// (CONNECTED_HOST_ID_JSON) - that assumption ("Rack module ids are small sequential
	// integers") turned out to be wrong in practice (observed ids run into the quadrillions,
	// which silently corrupts when stored in a float) - moved to a real int64_t member
	// (styxConnectedHostId, Styx.cpp), persisted via moduleExtraDataToJson/FromJson instead.

	// Which of the 16 real Morpheus channels each row currently displays - "each row has exactly
	// one selector," not the other way around, so two rows can (harmlessly) show the same
	// channel but no row can ever end up ambiguous about which channel it shows.
	ROW_CHANNEL_JSON,
	ROW_CHANNEL_JSON_LAST = ROW_CHANNEL_JSON + STYX_NUM_ROWS - 1,

	// Per-channel color, packed as a single 24-bit RGB integer stored in a float (exact for any
	// value up to 2^24 - well within range) - channel-owned, not row-owned, so it stays with the
	// channel when a row's assignment changes (confirmed explicitly during spec discussion).
	CHANNEL_COLOR_JSON,
	CHANNEL_COLOR_JSON_LAST = CHANNEL_COLOR_JSON + POLY_CHANNELS - 1,

	// Per-row MEM/TAPE toggle (0 = TAPE, 1 = MEM) and FOLLOW toggle (0 = off/manual page, 1 = on/
	// auto-follow play cursor) - real button Params below, these mirror their current value.
	ROW_MEMTAPE_JSON,
	ROW_MEMTAPE_JSON_LAST = ROW_MEMTAPE_JSON + STYX_NUM_ROWS - 1,
	ROW_FOLLOW_JSON,
	ROW_FOLLOW_JSON_LAST = ROW_FOLLOW_JSON + STYX_NUM_ROWS - 1,

	// Per-row manual page (only meaningful while that row's own FOLLOW is off) - which page of
	// this row's own step content is currently visible.
	ROW_PAGE_JSON,
	ROW_PAGE_JSON_LAST = ROW_PAGE_JSON + STYX_NUM_ROWS - 1,

	// Per-row cell display/editor type (0 = Gate, 1 = Value in v1 - Pitch/Curve are follow-up
	// work, see the plan) - user-configurable via right-click, not auto-detected from Morpheus's
	// own data (a step is just a raw float either way, see StyxShared.hpp's own comment).
	ROW_CELLTYPE_JSON,
	ROW_CELLTYPE_JSON_LAST = ROW_CELLTYPE_JSON + STYX_NUM_ROWS - 1,

	NUM_JSONS
};

//
// Parameter Ids - per-row toggle buttons only; the step cells themselves are not Params (they
// write directly through StyxHostInterface to Morpheus, not to Styx's own state)
//
enum ParamIds {
	ROW_MEMTAPE_PARAM,
	ROW_MEMTAPE_PARAM_LAST = ROW_MEMTAPE_PARAM + STYX_NUM_ROWS - 1,
	ROW_FOLLOW_PARAM,
	ROW_FOLLOW_PARAM_LAST = ROW_FOLLOW_PARAM + STYX_NUM_ROWS - 1,
	ROW_LEFT_PARAM,  // manual page-back, active only while that row's FOLLOW is off
	ROW_LEFT_PARAM_LAST = ROW_LEFT_PARAM + STYX_NUM_ROWS - 1,
	ROW_RIGHT_PARAM, // manual page-forward, same condition
	ROW_RIGHT_PARAM_LAST = ROW_RIGHT_PARAM + STYX_NUM_ROWS - 1,

	NUM_PARAMS
};

//
// Input/Output Ids - none in v1 (standalone/input-Expander modes deferred, see the plan)
//
enum InputIds {
	NUM_INPUTS
};

enum OutputIds {
	NUM_OUTPUTS
};

//
// Light Ids - the connection light is gone (superseded by the seam/logo-cover mechanism, which
// now derives directly from the bridge host id - see ExpanderBridge.hpp)
//
enum LightIds {
	NUM_LIGHTS
};
