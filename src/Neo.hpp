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
// standalone/X-family-input modes deferred - see the approved plan). Panel width IS resizable
// (see NeoResizeHandle in Neo.cpp). NEO_NUM_ROWS (16) is the structural row-slot count - every
// row's own state (channel/mem-tape/follow/page/celltype) always exists for all 16, persisted
// regardless of how many are currently ON SCREEN; ROWS_DISPLAYED_JSON (below, 4-8, right-click
// "Grid Rows") independently controls how many of those 16 slots are actually shown/active on
// THIS instance - the rest just stay hidden. See NEO_ROWS_MIN/MAX/DEFAULT and neoRowLayout()
// further down for the mechanism that makes fewer displayed rows show up taller, not blank.
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

// Resize handle geometry (2026-07-20, Dieter's own instruction) - Normal mode's handle is a
// small icon confined to the header band (right upper corner, vertically centered on the same
// y as the title text), entirely above the grid content, so it needs no reserved width at all.
// Full Height's own grid has no header band to hide the handle in (it spans the full panel
// height, chrome-free) - so it gets a dedicated, full-height 1HP-wide strip at the right edge
// instead, and that width has to come OUT of the grid's own usable width or the handle would
// visually cut across the (otherwise seamless) cells.
#define NEO_RESIZE_ICON_SIZE_MM     5.f
#define NEO_RESIZE_RESERVED_WIDTH_HP 1
#define NEO_RESIZE_RESERVED_WIDTH_MM (NEO_RESIZE_RESERVED_WIDTH_HP * 5.08f)

// Lock button (global area, see Neo module's own NeoLockData/toggleLock()) - upper left corner
// of the global area's own frame, spaced per PanelDesignGuide.md's own "Positioning controls"
// target (5 grid units, ~1.27mm) from both the left and top frame edges. Same flat-fill square
// style as the row LEFT/RIGHT paging buttons and X-family's own Bind/Free button - grey when
// unlocked, green when locked (matching NEO's own FOLLOW toggle green) - just a plain square for
// now, a locked/unlocked icon is a later styling pass (Dieter's own instruction, 2026-07-20).
#define NEO_LOCK_BUTTON_SIZE_MM    6.f
#define NEO_LOCK_BUTTON_SPACING_MM 1.27f
#define NEO_LOCK_OFF_COLOR nvgRGB(0x30, 0x30, 0x30)
#define NEO_LOCK_ON_COLOR  nvgRGB(0x00, 0xdd, 0x44)

// How much width is "spent" before the step-column grid begins - NEO_CONTROLS_WIDTH_MM always,
// plus Full Height's own reserved resize-handle strip when active. THE one place this decision
// is made; every width/column computation in Neo.cpp threads its result through rather than
// referencing NEO_CONTROLS_WIDTH_MM directly.
inline float neoRowAreaControlsWidthMm(bool fullHeight)
{
	return NEO_CONTROLS_WIDTH_MM + (fullHeight ? NEO_RESIZE_RESERVED_WIDTH_MM : 0.f);
}

// Column width used to live here as a fixed 4mm constant - 2026-07-20, Dieter's own instruction:
// step-columns are now SQUARE, i.e. column width always equals the current row cell height, so
// it varies with Grid Rows/Full Height and is computed via neoColumnWidthMm() below instead.

// The one gap value that drives the ENTIRE vertical rhythm of the row grid: the space between
// two consecutive rows' own cells, AND (per Dieter's own instruction, 2026-07-20) the space
// between the frame and the first/last row in normal mode - "the padding between the grid cells
// and the frame would be the same as the spacing of the grid cells." See neoRowLayout() below
// for how this one value turns into actual row positions/heights.
#define NEO_ROW_VPAD_MM       0.5f

// How many of NEO_NUM_ROWS's 16 row-slots are actually shown at once on a given instance -
// right-click "Grid Rows" (2026-07-20 addition). Two NEO instances each showing NEO_ROWS_MAX (8)
// rows, one placed directly above the other (same HP position, adjacent rack row) with Full
// Height enabled on both, together show all 16 channels as one seamless 16-row grid - see
// FULL_HEIGHT_JSON's own comment.
#define NEO_ROWS_MIN     3
#define NEO_ROWS_MAX     8
#define NEO_ROWS_DEFAULT 8

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

// Step-cell colors (NeoRowCellsWidget). The horizontal padding between cells is NOT a separate
// fixed value - it's derived every draw() call from the actual current gap (pitch minus cell
// width), which always equals the row-gap neoRowLayout() computes, so horizontal spacing matches
// vertical spacing exactly (Dieter's own instruction, 2026-07-20 - "buttons should have the same
// padding horizontally [as rows do vertically]"). NEO_CELL_BG_COLOR is an always-visible per-cell
// backdrop drawn for every column regardless of gate/value content, so individual cell
// boundaries read clearly even at rest - explicitly "for better visual support during future
// testing," not necessarily the final look.
#define NEO_ROW_BG_COLOR  nvgRGB(0x10, 0x10, 0x10)
#define NEO_CELL_BG_COLOR nvgRGB(0x30, 0x30, 0x30)

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

// Computed minimum resizable width, in HP - controls width (including Full Height's own
// reserved resize-handle strip, see neoRowAreaControlsWidthMm()) + at least NEO_MIN_VISIBLE_COLS
// columns, rounded UP to the next whole HP (Rack's own resize-snap grain) so rounding can never
// leave fewer than that many columns visible. A plain function (not a #define) since it depends
// on RACK_GRID_WIDTH/mm2px(). columnWidthMm/controlsWidthMm are the CURRENT values (see
// neoColumnWidthMm()/neoRowAreaControlsWidthMm()) - the caller already has them, recomputing
// fresh from rowsDisplayed/fullHeight here again would just duplicate that same call.
inline float neoMinWidthHp(float columnWidthMm, float controlsWidthMm)
{
	float minWidthMm = controlsWidthMm + NEO_MIN_VISIBLE_COLS * columnWidthMm;
	float minWidthPx = mm2px(Vec(minWidthMm, 0.f)).x;
	return std::ceil(minWidthPx / RACK_GRID_WIDTH);
}

// Computed maximum resizable width, in HP - controls width + enough columns to show the
// connected Host's own largest-possible channel content (getMaxLoopLen()) all at once, since no
// channel can ever have more steps than that regardless of how much wider NEO gets. Rounded DOWN
// to the next whole HP so the snap can never overshoot past what the Host can actually show.
// Returns a generously large (effectively unbounded) fallback while disconnected - nothing to
// size against yet, and the resize handle already re-clamps continuously as the user drags, so a
// too-large value here is harmless (never actually reachable once a real Host is attached and its
// own true ceiling takes over on the very next tick).
inline float neoMaxWidthHp(NeoHostInterface *host, float columnWidthMm, float controlsWidthMm)
{
	int maxSteps = host ? host->getMaxLoopLen() : 100000;
	float maxWidthMm = controlsWidthMm + (float) maxSteps * columnWidthMm;
	float maxWidthPx = mm2px(Vec(maxWidthMm, 0.f)).x;
	return std::floor(maxWidthPx / RACK_GRID_WIDTH);
}

/*
	THE single formula for where every row sits and how tall its own cells are - both
	NeoWidget::step() (positions the actual row widgets) and NeoPanelWidget::draw() (needs to
	know where the row-area's own usable content starts/ends) call this, so the two can never
	disagree about the layout.

	Normal mode: content runs from NEO_FRAME_TOP_MM to (PANELHEIGHT - NEO_FRAME_BOTTOM_MM), and
	the padding from the frame to the first/last row equals NEO_ROW_VPAD_MM - the same gap used
	BETWEEN rows - so the whole grid (frame edges included) reads as one evenly-spaced rhythm.

	Full Height mode: content runs the full 0..PANELHEIGHT (no frame margin at all - see
	FULL_HEIGHT_JSON's own comment), and the top/bottom padding is HALF of NEO_ROW_VPAD_MM. Two
	stacked instances each showing the same row count this way each contribute one half-gap at
	their own touching edge, which together add up to exactly one normal inter-row gap right at
	the seam - the grid reads as continuous across the two panels, not doubled or pinched.
*/
inline void neoRowLayout(bool fullHeight, int rowsDisplayed, float &outFirstRowYMm, float &outCellHeightMm, float &outRowPitchMm)
{
	if (fullHeight)
	{
		// Full Height can't "cheat" the way normal mode can (Dieter's own framing, 2026-07-20) -
		// it touches the panel's true, fixed top/bottom edges, so two stacked NEOs only tile as
		// one continuous grid if the math closes to PANELHEIGHT with zero leftover. Cell size is
		// snapped to the largest whole HP that still leaves a gap of at least NEO_FRAME_MARGIN_MM
		// (never thinner than the panel's own standard frame margin) - whatever's left over IS
		// exactly the gap needed to close the total, so nothing is separately solved for. This
		// always produces a valid, exactly-closing result for any row count (3..NEO_ROWS_MAX) -
		// some (5, 7) just end up with a proportionally bigger gap relative to cell size than
		// others (4, 6, 8), not a broken one, so every row count stays selectable in both modes -
		// deliberately no exclusion list (Dieter's own call, 2026-07-20: "maybe there's no
		// restriction necessary at all").
		float budgetMm = PANELHEIGHT / (float) rowsDisplayed;
		int hp = (int) std::floor((budgetMm - NEO_FRAME_MARGIN_MM) / 5.08f);
		hp = std::max(1, hp);
		outCellHeightMm = (float) hp * 5.08f;
		outRowPitchMm = budgetMm; // cell + gap always sums to exactly PANELHEIGHT/rowsDisplayed
		outFirstRowYMm = (budgetMm - outCellHeightMm) / 2.f; // half-gap edge, same convention as normal mode
	}
	else
	{
		float contentTopMm = NEO_FRAME_TOP_MM;
		float contentBottomMm = PANELHEIGHT - NEO_FRAME_BOTTOM_MM;
		float padMm = NEO_ROW_VPAD_MM;
		float availableMm = (contentBottomMm - contentTopMm) - 2.f * padMm;
		outCellHeightMm = (availableMm - (float) (rowsDisplayed - 1) * NEO_ROW_VPAD_MM) / (float) rowsDisplayed;
		outRowPitchMm = outCellHeightMm + NEO_ROW_VPAD_MM;
		outFirstRowYMm = contentTopMm + padMm;
	}
}

// Step-columns are square (Dieter's own instruction, 2026-07-20) - column width always equals
// the current row cell height, so it moves whenever Grid Rows/Full Height does. Thin wrapper
// around neoRowLayout() for call sites that only need this one output.
inline float neoColumnWidthMm(bool fullHeight, int rowsDisplayed)
{
	float firstRowYMm, cellHeightMm, rowPitchMm;
	neoRowLayout(fullHeight, rowsDisplayed, firstRowYMm, cellHeightMm, rowPitchMm);
	return cellHeightMm;
}

// The horizontal spacing between column STARTS (cell + gap), not just the visible cell size -
// the actual footprint one column occupies. Equals neoRowLayout()'s own rowPitchMm exactly,
// since cells are square and (Dieter's own instruction, 2026-07-20) horizontal inter-column
// padding must match vertical inter-row padding - "buttons should have the same padding
// horizontally [as rows do vertically]." Every width-BUDGET computation (how many columns fit,
// min/max resizable width, the auto-resize reconciliation) must use this, not
// neoColumnWidthMm() - that one is for drawing the cell itself, this one is for laying columns
// out at all.
inline float neoColumnPitchMm(bool fullHeight, int rowsDisplayed)
{
	float firstRowYMm, cellHeightMm, rowPitchMm;
	neoRowLayout(fullHeight, rowsDisplayed, firstRowYMm, cellHeightMm, rowPitchMm);
	return rowPitchMm;
}

//
// Virtual Parameter Ids stored using Json
//
enum jsonIds {
	STYLE_JSON,
	PANEL_WIDTH_HP_JSON, // current panel width in HP - persisted, applied to box.size.x on load
	ROWS_DISPLAYED_JSON, // NEO_ROWS_MIN..NEO_ROWS_MAX - right-click "Grid Rows"
	// Full Height: row-area frame/border stops being drawn and its content spans the FULL panel
	// height (0..PANELHEIGHT, no top/bottom margin at all), while the header/footer brand text
	// (title, accent stripe, "ORANGE LINE" wordmark) retreats to span only the global area's own
	// width instead of the whole panel - so the row grid becomes a chrome-free rectangle that can
	// butt seamlessly against another NEO's row grid directly above/below it (same HP position, a
	// different rack row) with no rounded corner, border stroke, or brand text crossing the seam.
	// See neoRowLayout()'s own comment for the matching row-padding halving.
	FULL_HEIGHT_JSON,

	// Lock - this instance's own membership in the Host-shared "common config" group (2026-07-20
	// design). Right-click-free, a real panel widget (NeoLockButton) in the global area. See
	// Neo.cpp's own NeoLockData/readLockData()/writeLockData() and NeoWidget::step()'s lock-sync
	// block for the full mechanism and its own schema documentation.
	LOCKED_JSON,

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
