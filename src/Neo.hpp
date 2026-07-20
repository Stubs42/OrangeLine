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

// Content-frame geometry, straight from PanelDesignGuide.md's "Content frame" and ""ORANGE LINE"
// brand accent stripe" sections - identical for every OrangeLine module regardless of width, so
// these apply to NEO's own frame at whatever box.size.x currently is, exactly like every static
// panel applies them at its own fixed width. NEO_FRAME_GAP_MM is the one NEO-specific addition:
// the guide's own "Nested frames" padding value (6 grid units), reused here to separate NEO's own
// two peer frames (global area / row area) instead of a frame nested inside another. Placed here,
// ahead of everything else in this file, because neoRowAreaControlsWidthMm() below (and by
// extension NEO_DEFAULT_WIDTH_HP's own hand derivation) now needs NEO_FRAME_GAP_MM for its own
// right-hand trailing-margin term - a plain #define, so definition order matters.
#define NEO_FRAME_MARGIN_MM   0.762f
// Background-only inset (NeoPanelWidget's own fill, not the frame stroke) - half the frame's own
// side margin, Dieter's own call, 2026-07-20.
#define NEO_BACKGROUND_INSET_MM (NEO_FRAME_MARGIN_MM / 2.f)
#define NEO_FRAME_TOP_MM      7.620f
#define NEO_FRAME_BOTTOM_MM   7.620f
#define NEO_FRAME_RADIUS_MM   2.5f
#define NEO_FRAME_STROKE_MM   0.3f
#define NEO_FRAME_GAP_MM      1.524f

// The module's initial AND minimum width, in HP - "initial width == min width" (Dieter's own
// instruction, 2026-07-20: "fixate the algorithm"). The smallest whole-HP grid point wide enough
// for the row header's own target width (Tw) plus NEO_MIN_VISIBLE_COLS columns at the module's
// default row config (NEO_ROWS_DEFAULT rows, Normal/non-Full-Height) - so a freshly-placed
// module always starts already showing at least that many columns, and can never be dragged
// narrower than that either (NeoResizeHandle::onDragMove()'s own neoMinWidthHp() clamp, which
// stays dynamic for OTHER row configs the user might switch to later - this constant is only
// about the fixed default/initial state). Calculated once by hand rather than at runtime -
// whatever whole-HP width doesn't divide the target evenly just becomes leftover that gets
// absorbed into the header at drag-end (Neo::absorbLeftoverIntoHeader()), so rounding UP here
// can only ever grow the header past Tw, never shrink it below - the "header width after
// settling >= targetHeaderWidth" guarantee holds by construction, not by a runtime check.
//   rightPaddingMm (Normal) = NEO_FRAME_GAP_MM/2 + NEO_FRAME_GAP_MM = 1.5 * 1.524 = 2.286
//   controlsWidthMm = NEO_GLOBAL_AREA_WIDTH_MM (30.48) + NEO_ROW_HEADER_TARGET_WIDTH_MM (162.56)
//                    + rightPaddingMm (2.286) = 195.326
//   columnPitchMm at 8 rows, Normal height = (PANELHEIGHT - 2*NEO_FRAME_TOP_MM) / 8
//                    = (128.5 - 15.24) / 8 = 113.26 / 8 = 14.1575
//   minWidthMm = 195.326 + 4 * 14.1575 = 251.956
//   minWidthHp = ceil(251.956 / 5.08) = 50 (see neoMinWidthHp() below - this hand derivation
//                must always match what that function would compute for this same config, so the
//                two never silently diverge)
// Recompute by hand and update this constant if NEO_ROW_HEADER_TARGET_WIDTH_HP,
// NEO_GLOBAL_AREA_WIDTH_HP, NEO_MIN_VISIBLE_COLS, NEO_ROWS_DEFAULT, neoRowAreaControlsWidthMm()'s
// own right-padding formula, or the frame-margin constants above ever change. No extra fudge-
// factor HP on top of this raw value anymore (removed again 2026-07-20, same day it was added -
// Dieter's own catch, live-testing: "still too wide" - it was double-counting margin that
// neoRowAreaControlsWidthMm()'s own right-padding term already supplies for real now).
#define NEO_DEFAULT_WIDTH_HP 50

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

// Target width (Dieter's own "Tw") for the per-row name/toggle/page controls, starting right
// after the global area above - about 4x its original 40mm (2026-07-20 - the row header needs
// much more room, e.g. for a fuller Morpheus-style cell-type preview), defined as a whole HP
// count for the same reason NEO_GLOBAL_AREA_WIDTH_HP is. This is only the DEFAULT/snap-to target
// now, not the always-used value - the row header's own ACTUAL current width is a real
// per-instance, persisted quantity (ROW_HEADER_WIDTH_MM_JSON, Neo::getRowHeaderWidthMm()) that
// can grow past Tw (2026-07-20 design, worked out with Dieter): dragging the resize handle snaps
// the header back to exactly Tw at drag-START (with the module's own total width adjusted by the
// same delta, so the visible column count doesn't jump at that instant), then at drag-END,
// whatever leftover space the floor-based column count couldn't use gets absorbed INTO the
// header (grown past Tw) instead of sitting there as an ugly dead gap - keeping the step-cell
// grid itself always exactly, tightly filled. The row NAME field is what actually grows to fill
// that reclaimed width (NeoWidget::step()), keeping the rest of the header's own layout fixed.
#define NEO_ROW_HEADER_TARGET_WIDTH_HP 32
#define NEO_ROW_HEADER_TARGET_WIDTH_MM (NEO_ROW_HEADER_TARGET_WIDTH_HP * 5.08f)

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

// Seam-bridging Ext strips very slightly covered NEO's (and Morpheus's) own panel frame line -
// turned out to be a shared-code issue, not NEO-specific; the actual fix (X_STRIP_FRAME_NUDGE_MM)
// now lives in XShared.hpp's own addXExtStrip()/addXExtStripLeft(), fixing every module that uses
// them at once. NEO's own per-tick right-strip re-derivation (NeoWidget::step(), since its width
// isn't fixed) just matches that same shared formula.

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

// How much width is "spent" on either side of the step-column grid, total - the global area,
// plus the row header's own CURRENT width (a real per-instance quantity now, see
// NEO_ROW_HEADER_TARGET_WIDTH_MM's own comment - callers pass module->getRowHeaderWidthMm(), or
// NEO_ROW_HEADER_TARGET_WIDTH_MM itself where no module instance exists yet), plus Full Height's
// own reserved resize-handle strip when active - all on the LEFT - plus a right-hand trailing
// margin so the grid's own last column doesn't run flush to the panel's right edge (Dieter's own
// catch, 2026-07-20, mirroring the same nested-frame reasoning the row-header-frame's own left
// padding fix just used): Normal mode's row area is a real nested frame that needs the peer-frame
// half-gap separation from the global area's own edge (matching the row area frame's LEFT edge,
// NeoPanelWidget::draw()) PLUS a further full gap so content doesn't run flush against the row
// area's OWN right frame edge either - Full Height has no row-area frame to nest inside of at
// all (chrome-free, see FULL_HEIGHT_JSON's own comment), so it only needs that one half-gap.
// THE one place this decision is made; every width/column computation in Neo.cpp threads its
// result through rather than computing it separately.
inline float neoRowAreaControlsWidthMm(bool fullHeight, float rowHeaderWidthMm)
{
	float rightPaddingMm = NEO_FRAME_GAP_MM / 2.f + (fullHeight ? 0.f : NEO_FRAME_GAP_MM);
	return NEO_GLOBAL_AREA_WIDTH_MM + rowHeaderWidthMm + (fullHeight ? NEO_RESIZE_RESERVED_WIDTH_MM : 0.f) + rightPaddingMm;
}

// Column width used to live here as a fixed 4mm constant - 2026-07-20, Dieter's own instruction:
// step-columns are now SQUARE, i.e. column width always equals the current row cell height, so
// it varies with Grid Rows/Full Height and is computed via neoColumnWidthMm() below instead.

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

// Step-cell color (NeoRowCellsWidget). The horizontal padding between cells is NOT a separate
// fixed value - it's derived every draw() call from the actual current gap (pitch minus cell
// width), which always equals the row-gap neoRowLayout() computes, so horizontal spacing matches
// vertical spacing exactly (Dieter's own instruction, 2026-07-20 - "buttons should have the same
// padding horizontally [as rows do vertically]"). NEO_CELL_BG_COLOR is an always-visible per-cell
// backdrop drawn for every column regardless of gate/value content, so individual cell
// boundaries read clearly even at rest - explicitly "for better visual support during future
// testing," not necessarily the final look. No separate row-background fill behind it anymore
// (removed 2026-07-20, Dieter's own instruction - it read as a solid black box behind each row);
// the gaps between cells and any space past the last visible column just show the row area's
// own panel background straight through.
#define NEO_CELL_BG_COLOR nvgRGB(0x30, 0x30, 0x30)

// Morpheus-style cell (NeoMorpheusStyleCellEditor, cellType 2) - replicates MorpheusDisplayWidget's
// own value-to-color technique (Morpheus.cpp) at NEO's own per-cell scale: a step's raw bipolar
// voltage lerps the cell's fill between these two colors, rather than an on/off square or a bar
// height. Exact same RGB values as Morpheus's own DISPLAY_BIPOLAR_LOW_COLOR/HIGH_COLOR (its
// simplest display mode, not the match/dirty "scaleOnOutput" variant) - first pass, 2026-07-20,
// Dieter's own request: "replicate the display of Morpheus... we will enhance and tune from that
// point on."
#define NEO_MORPHEUS_CELL_LOW_COLOR  nvgRGB(164, 32, 32)
#define NEO_MORPHEUS_CELL_HIGH_COLOR nvgRGB(32, 164, 32)

// Smallest number of step columns the resize handle must always leave visible - Dieter's own
// call, 2026-07-20, replacing the earlier fixed NEO_MIN_WIDTH_HP guess with an actual usability
// floor.
#define NEO_MIN_VISIBLE_COLS 4

// Per-row "header data" frame (name/MEM-TAPE/FOLLOW/paging) - 2026-07-20, Dieter's own spec: own
// outer height matches the current cell height. Left corners follow PanelDesignGuide.md's own
// "Nested frames" corner-radius rule (inner_radius = outer_radius - padding) rather than the
// outer frame's own full radius directly - this frame is NESTED inside the row area's own frame,
// separated from it by NEO_FRAME_GAP_MM, so its own radius must shrink by that same amount to
// keep a constant-width ring of space around the corner (corrected 2026-07-20 after the first,
// too-large attempt used NEO_FRAME_RADIUS_MM directly - Dieter's own catch, "about half"). Right
// corners get an even smaller radius still, deliberately emphasizing that this frame leads into
// (belongs with) the step-cell grid rather than standing apart from it - scaled down from the
// left radius by the same ratio the original (2.5mm outer / 2mm right) pairing used. Horizontal
// padding matches the step-cells' own cell padding (half the current row gap) on the left; on
// the right, matching that exactly runs into the step-cell grid's own separate coordinate space,
// so it just uses NEO_FRAME_GAP_MM instead - close in practice, not worth the complexity of
// reconciling the two exactly (Dieter's own call).
#define NEO_ROW_HEADER_LEFT_RADIUS_MM  (NEO_FRAME_RADIUS_MM - NEO_FRAME_GAP_MM)
#define NEO_ROW_HEADER_RIGHT_RADIUS_MM (NEO_ROW_HEADER_LEFT_RADIUS_MM * 0.8f)
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
// reserved resize-handle strip AND the right-hand trailing margin, both folded into
// neoRowAreaControlsWidthMm() itself) + at least NEO_MIN_VISIBLE_COLS columns, rounded UP to the
// next whole HP (Rack's own resize-snap grain - RACK_GRID_WIDTH is a whole HP, 15px/5.08mm;
// Rack has no half-HP resize granularity). An earlier pass (2026-07-20) added a flat +1 HP here
// as a rough fix before controlsWidthMm actually accounted for any right-side margin at all -
// now that neoRowAreaControlsWidthMm() bakes in a real, principled trailing-margin term of its
// own, that flat +1 HP just double-counted margin on top of it (Dieter's own catch, live-testing:
// "still too wide") and was removed again. This is THE one shared formula every minimum-width
// caller uses - the interactive drag clamp (NeoResizeHandle::onDragMove()), the Lock group's own
// width convergence, and NEO_DEFAULT_WIDTH_HP's own hand computation (Neo.hpp, kept numerically
// in sync with this function by hand) - so fixing it here once covers all of them, rather than
// needing the same margin re-applied at every call site. A plain function (not a #define) since
// it depends on RACK_GRID_WIDTH/mm2px(). columnWidthMm/controlsWidthMm are the CURRENT values
// (see neoColumnWidthMm()/neoRowAreaControlsWidthMm()) - the caller already has them, recomputing
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

	Redefined 2026-07-20 (Dieter's own instruction, after the HP-snap approach let padding blow
	up disproportionately at high row counts - e.g. 8 rows in Full Height left a gap nearly as
	big as the cell itself): padding is now the FIXED, primary quantity - always exactly
	NEO_FRAME_GAP_MM, the same "frame padding" used everywhere else in NEO's own layout - and
	cell size is simply whatever real number is left over dividing the rest evenly among the
	rows. No more HP-snapping cell size at all.

	The two modes do NOT share one padding rule (corrected 2026-07-20, same day - the first pass
	above applied Full Height's own half-edge reasoning to Normal mode too, which was wrong and
	left Normal mode's first/last row visibly under-padded against its own frame):
	  - Normal mode ("padding in normal mode is always frame padding," Dieter's own words):
	    Normal mode always keeps its own full frame/border and never stacks seamlessly against
	    anything, so there's no seam to preserve - top, bottom, AND every gap between rows are
	    all one full NEO_FRAME_GAP_MM (literal N+1 equal paddings for N rows).
	  - Full Height mode: half a gap at the very top edge and half at the very bottom edge, full
	    gaps between rows - so two Full Height instances stacked on top of each other still
	    contribute one half-padding each at their own touching edge, summing back to exactly one
	    ordinary inter-row gap right at the seam (N total gap-units, not N+1 - this is the one
	    place the half-edge convention still applies, and only because Full Height's content
	    range is the real, fixed panel edge another instance's own grid has to tile against - see
	    FULL_HEIGHT_JSON's own comment).
	Normal mode uses the fixed band between NEO_FRAME_TOP_MM and (PANELHEIGHT - NEO_FRAME_BOTTOM_MM);
	Full Height uses the full 0..PANELHEIGHT (no frame margin at all).
*/
inline void neoRowLayout(bool fullHeight, int rowsDisplayed, float &outFirstRowYMm, float &outCellHeightMm, float &outRowPitchMm)
{
	float contentTopMm = fullHeight ? 0.f : NEO_FRAME_TOP_MM;
	float contentBottomMm = fullHeight ? PANELHEIGHT : (PANELHEIGHT - NEO_FRAME_BOTTOM_MM);
	float rangeMm = contentBottomMm - contentTopMm;
	if (fullHeight)
	{
		float budgetMm = rangeMm / (float) rowsDisplayed; // N gap-units total (half top + half bottom + (N-1) full)
		outCellHeightMm = std::max(0.1f, budgetMm - NEO_FRAME_GAP_MM);
		outRowPitchMm = budgetMm;
		outFirstRowYMm = contentTopMm + (budgetMm - outCellHeightMm) / 2.f; // half-gap edge
	}
	else
	{
		// N+1 full gap-units total (full top + (N-1) full between rows + full bottom).
		outCellHeightMm = std::max(0.1f, (rangeMm - (float) (rowsDisplayed + 1) * NEO_FRAME_GAP_MM) / (float) rowsDisplayed);
		outRowPitchMm = outCellHeightMm + NEO_FRAME_GAP_MM;
		outFirstRowYMm = contentTopMm + NEO_FRAME_GAP_MM; // full-gap edge
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

// THE one shared column-fit computation - given the panel's current total width, how much of it
// is already spent on controls (neoRowAreaControlsWidthMm()), and the current column pitch,
// returns how many WHOLE columns fit in whatever's left over and exactly how much fractional
// space remains beyond those whole columns. EVERY call site that needs either number must go
// through this one function - Neo::getVisibleColumns() (live column count, used by
// NeoRowCellsWidget's own draw()/click handling), Neo::absorbLeftoverIntoHeader() (the drag-end/
// lock-sync settling step), and the row-cell widget's own box sizing (NeoWidget::step()).
// Before this (2026-07-20), these were three separately hand-written reimplementations of the
// same arithmetic that could silently drift apart from each other (Dieter's own catch, live-
// testing: dragging showed 5 columns, but the drag-end code's own separate calculation decided
// only 4 actually fit for that same settled width - "different thinking about space... ugly and
// has to be resolved... by better coding," not by hunting down one more one-off numeric
// mismatch). Consolidating here is that fix: there is now exactly one place this arithmetic is
// written, so every caller sees the identical answer for the identical input, by construction.
inline void neoColumnFit(float widthMm, float controlsWidthMm, float columnPitchMm, int &outVisibleCols, float &outLeftoverMm)
{
	float cellsWidthMm = std::max(1.f, widthMm - controlsWidthMm);
	// Tiny epsilon before flooring (Dieter's own catch, 2026-07-20, live-testing: "still
	// dropping a column when releasing," even after the algorithm itself was fixed) - once
	// Neo::absorbLeftoverIntoHeader() settles cellsWidthMm to EXACTLY visibleCols*columnPitchMm
	// (by construction, see its own comment), re-deriving that same cellsWidthMm one frame later
	// via a fresh widthMm-controlsWidthMm subtraction is not guaranteed to reproduce the exact
	// same float bit pattern - it can land a hair BELOW the true boundary, and a bare floor()
	// misreads that hair as one fewer column even though nothing about the actual layout
	// changed. 0.001mm is far larger than any realistic float rounding error at these
	// magnitudes (a few hundred mm) but far smaller than any real leftover, so it can only ever
	// correct a false boundary-crossing, never manufacture a column that doesn't really fit.
	const float NEO_COLUMN_FIT_EPSILON_MM = 0.001f;
	outVisibleCols = std::max(1, (int) ((cellsWidthMm + NEO_COLUMN_FIT_EPSILON_MM) / columnPitchMm));
	outLeftoverMm = cellsWidthMm - (float) outVisibleCols * columnPitchMm;
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

	// The row header's own ACTUAL current width in mm - defaults to NEO_ROW_HEADER_TARGET_WIDTH_MM
	// ("Tw"), grows past it when the resize handle's own drag-end absorbs leftover step-cell
	// space into the header instead of leaving it as a dead gap. See
	// NEO_ROW_HEADER_TARGET_WIDTH_MM's own comment (Neo.hpp) for the full drag-lifecycle mechanism.
	ROW_HEADER_WIDTH_MM_JSON,

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
