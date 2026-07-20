/*
	Neo.hpp

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
// NEO v1: bidirectional Morpheus tape/memory editor. No real jacks at all yet (pure Expander,
// standalone/X-family-input modes deferred - see the approved plan). Always 16 rows for v1
// (8/16-row mode toggle deferred); panel width IS resizable (see NeoResizeHandle in Neo.cpp).
#define NEO_NUM_ROWS POLY_CHANNELS

// No real jacks at all in v1 - Touch/Wakeup-Ready exists to relay a real CV signal through a real
// jack at low latency, which doesn't apply here (same reasoning as CC2CV/RECALL/MidiBus).
#define OL_TOUCH_DISABLED

#include "OrangeLine.hpp"
#include "XShared.hpp"
#include "XOShared.hpp"
#include "NeoShared.hpp"

// ================================================================================================
// NEO layout constants - THE single source of truth for every row/cell/global-area measurement
// and widget size/position used anywhere in Neo.cpp (NeoPanelWidget, NeoWidget's own constructor,
// NeoRowCellsWidget, NeoRowNameWidget, neoMinWidthHp()/neoMaxWidthHp()). Change a value here to
// rescale or repad the whole module - nothing in Neo.cpp should hardcode a raw mm number of its
// own for any of these measurements (Dieter's own instruction, 2026-07-20: "all measures of
// cellsized row positions, number of rows per NEO should be always be configurable at one source
// location file including the widget sizes and proportions, so we can scale and pad freely").
// Lives here (Neo.hpp), not Neo.cpp, specifically so NEO_NUM_ROWS above (needed by this file's
// own jsonIds/ParamIds enums) and every other layout constant sit in the one same file.
// ================================================================================================
#define NEO_DEFAULT_WIDTH_HP 24

// Reserved left-hand sidebar, full panel height, for module-wide (not per-row) controls -
// sockets, knobs, displays, whatever NEO eventually needs that isn't tied to one specific row.
// Deliberately abstract for now (2026-07-20 design discussion) - nothing lives here yet, just an
// empty framed placeholder, sized to XOD8's own panel width (6HP = 30.48mm) purely so Dieter has a
// concrete sense of scale/usability while testing, not a considered final size. Defined as a whole
// HP count, not a bare mm value - the global area's own right edge is a real seam a neighboring
// OrangeLine module's own edge can sit flush against, so it must always land on the same 5.08mm
// grid every module's width already snaps to, not an arbitrary mm figure that could drift off it.
#define NEO_GLOBAL_AREA_WIDTH_HP 6
#define NEO_GLOBAL_AREA_WIDTH_MM (NEO_GLOBAL_AREA_WIDTH_HP * 5.08f)

// Width reserved for the per-row name/toggle/page controls, starting right after the global area
// above (not from the panel's own left edge anymore) - everything measured from
// NEO_CONTROLS_WIDTH_MM onward (the step-cell columns) automatically shifts with it.
#define NEO_ROW_CONTROLS_WIDTH_MM 40.f
#define NEO_CONTROLS_WIDTH_MM (NEO_GLOBAL_AREA_WIDTH_MM + NEO_ROW_CONTROLS_WIDTH_MM)

#define NEO_COLUMN_WIDTH_MM   4.f   // width of one step-column cell
#define NEO_ROW_HEIGHT_MM     6.5f
// Vertical gap trimmed off the bottom of every row's own controls/cells, so adjacent rows never
// visually touch - subtracted from NEO_ROW_HEIGHT_MM wherever a row control's own height is set.
#define NEO_ROW_VPAD_MM       0.5f
#define NEO_FIRST_ROW_Y_MM    12.f

// Per-row control x-offsets, measured from the global area's own right edge (added to
// NEO_GLOBAL_AREA_WIDTH_MM at each use site) - and their own widget sizes.
#define NEO_ROW_NAME_X_MM        1.f
#define NEO_ROW_NAME_WIDTH_MM    16.f
#define NEO_ROW_MEMTAPE_X_MM     19.f
#define NEO_ROW_FOLLOW_X_MM      26.f
#define NEO_ROW_LEFT_X_MM        33.f
#define NEO_ROW_RIGHT_X_MM       38.f
#define NEO_ROW_TOGGLE_WIDTH_MM  6.f  // MEM/TAPE and FOLLOW toggle buttons
#define NEO_ROW_TOGGLE_HEIGHT_MM 4.f
#define NEO_ROW_PAGEBTN_SIZE_MM  4.f  // LEFT/RIGHT paging buttons (square)

// Smallest number of step columns the resize handle must always leave visible - Dieter's own
// call, 2026-07-20, replacing the earlier fixed NEO_MIN_WIDTH_HP guess with an actual usability
// floor.
#define NEO_MIN_VISIBLE_COLS 4

// Content-frame geometry, straight from PanelDesignGuide.md's "Content frame" and ""ORANGE LINE"
// brand accent stripe" sections - identical for every OrangeLine module regardless of width, so
// these apply to NEO's own frame at whatever box.size.x currently is, exactly like every static
// panel applies them at its own fixed width. NEO_FRAME_GAP_MM is the one NEO-specific addition:
// the guide's own "Nested frames" padding value (6 grid units), reused here to separate NEO's own
// two peer frames (global area / row area) instead of a frame nested inside another.
#define NEO_FRAME_MARGIN_MM   0.762f
// Background-only inset (NeoPanelWidget's own fill, not the frame stroke) - half the frame's own
// side margin, Dieter's own call, 2026-07-20.
#define NEO_BACKGROUND_INSET_MM (NEO_FRAME_MARGIN_MM / 2.f)
#define NEO_FRAME_TOP_MM      7.620f
#define NEO_FRAME_BOTTOM_MM   7.620f
#define NEO_FRAME_RADIUS_MM   2.5f
#define NEO_FRAME_STROKE_MM   0.3f
#define NEO_FRAME_GAP_MM      1.524f
#define NEO_ACCENT_Y_MM       124.71525f
#define NEO_TITLE_CENTER_Y_MM 3.810f
#define NEO_TITLE_BASELINE_Y_MM 5.873f // see NeoWork.svg's own text-title for how this was measured
#define NEO_TITLE_SIZE_MM     5.64444f // 16pt, standard multi-character module-name logo size
#define NEO_WORDMARK_SIZE_MM  3.52777f // 10pt, standard "ORANGE"/"LINE" wordmark size
#define NEO_WORDMARK_ORANGE_Y_MM 124.1728f
#define NEO_WORDMARK_LINE_Y_MM   127.7288f
// Rack's own PanelBorder (drawn automatically for every SvgPanel-based module, right at the
// panel's true edges) is what every other OrangeLine module's "natural seam" bevel actually
// comes from - NEO doesn't get it for free since it never calls setPanel(), so it's reproduced
// by hand: a stroke the same width as NEO_BACKGROUND_INSET_MM, traced around the full box.size.
#define NEO_PANEL_BORDER_COLOR nvgRGB(0x5a, 0x5a, 0x5a)

// Computed minimum resizable width, in HP - global area + row controls + at least
// NEO_MIN_VISIBLE_COLS columns, rounded UP to the next whole HP (Rack's own resize-snap grain)
// so rounding can never leave fewer than that many columns visible. A plain function (not a
// #define) since it depends on RACK_GRID_WIDTH/mm2px().
inline float neoMinWidthHp()
{
	float minWidthMm = NEO_CONTROLS_WIDTH_MM + NEO_MIN_VISIBLE_COLS * NEO_COLUMN_WIDTH_MM;
	float minWidthPx = mm2px(Vec(minWidthMm, 0.f)).x;
	return std::ceil(minWidthPx / RACK_GRID_WIDTH);
}

// Computed maximum resizable width, in HP - global area + row controls + enough columns to show
// the connected Host's own largest-possible channel content (getMaxLoopLen()) all at once, since
// no channel can ever have more steps than that regardless of how much wider NEO gets. Rounded
// DOWN to the next whole HP so the snap can never overshoot past what the Host can actually show.
// Returns a generously large (effectively unbounded) fallback while disconnected - nothing to
// size against yet, and the resize handle already re-clamps continuously as the user drags, so a
// too-large value here is harmless (never actually reachable once a real Host is attached and its
// own true ceiling takes over on the very next tick).
inline float neoMaxWidthHp(NeoHostInterface *host)
{
	int maxSteps = host ? host->getMaxLoopLen() : 100000;
	float maxWidthMm = NEO_CONTROLS_WIDTH_MM + (float) maxSteps * NEO_COLUMN_WIDTH_MM;
	float maxWidthPx = mm2px(Vec(maxWidthMm, 0.f)).x;
	return std::floor(maxWidthPx / RACK_GRID_WIDTH);
}

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
	// (neoConnectedHostId, Neo.cpp), persisted via moduleExtraDataToJson/FromJson instead.

	// Which of the 16 real Morpheus channels each row currently displays - "each row has exactly
	// one selector," not the other way around, so two rows can (harmlessly) show the same
	// channel but no row can ever end up ambiguous about which channel it shows.
	ROW_CHANNEL_JSON,
	ROW_CHANNEL_JSON_LAST = ROW_CHANNEL_JSON + NEO_NUM_ROWS - 1,

	// Channel name/color used to live here as per-channel, per-instance state (CHANNEL_COLOR_JSON)
	// - moved 2026-07-20 to genuinely channel-owned, Host-shared storage instead (Neo.cpp's own
	// channelName[]/channelColor[] + refreshChannelTable(), stored on Morpheus itself via the
	// generic ExpanderBridgeInterface::writeExpanderData()/readExpanderData() every Host offers -
	// see ExpanderBridge.hpp). A channel's identity shouldn't depend on which NEO instance/row is
	// currently looking at it, and every attached NEO instance now automatically agrees.

	// Per-row MEM/TAPE toggle (0 = TAPE, 1 = MEM) and FOLLOW toggle (0 = off/manual page, 1 = on/
	// auto-follow play cursor) - real button Params below, these mirror their current value.
	ROW_MEMTAPE_JSON,
	ROW_MEMTAPE_JSON_LAST = ROW_MEMTAPE_JSON + NEO_NUM_ROWS - 1,
	ROW_FOLLOW_JSON,
	ROW_FOLLOW_JSON_LAST = ROW_FOLLOW_JSON + NEO_NUM_ROWS - 1,

	// Per-row manual page (only meaningful while that row's own FOLLOW is off) - which page of
	// this row's own step content is currently visible.
	ROW_PAGE_JSON,
	ROW_PAGE_JSON_LAST = ROW_PAGE_JSON + NEO_NUM_ROWS - 1,

	// Per-row cell display/editor type (0 = Gate, 1 = Value in v1 - Pitch/Curve are follow-up
	// work, see the plan) - user-configurable via right-click, not auto-detected from Morpheus's
	// own data (a step is just a raw float either way, see NeoShared.hpp's own comment).
	ROW_CELLTYPE_JSON,
	ROW_CELLTYPE_JSON_LAST = ROW_CELLTYPE_JSON + NEO_NUM_ROWS - 1,

	NUM_JSONS
};

//
// Parameter Ids - per-row toggle buttons only; the step cells themselves are not Params (they
// write directly through NeoHostInterface to Morpheus, not to Neo's own state)
//
enum ParamIds {
	ROW_MEMTAPE_PARAM,
	ROW_MEMTAPE_PARAM_LAST = ROW_MEMTAPE_PARAM + NEO_NUM_ROWS - 1,
	ROW_FOLLOW_PARAM,
	ROW_FOLLOW_PARAM_LAST = ROW_FOLLOW_PARAM + NEO_NUM_ROWS - 1,
	ROW_LEFT_PARAM,  // manual page-back, active only while that row's FOLLOW is off
	ROW_LEFT_PARAM_LAST = ROW_LEFT_PARAM + NEO_NUM_ROWS - 1,
	ROW_RIGHT_PARAM, // manual page-forward, same condition
	ROW_RIGHT_PARAM_LAST = ROW_RIGHT_PARAM + NEO_NUM_ROWS - 1,

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
