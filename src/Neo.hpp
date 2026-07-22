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

// Recommended (NOT enforced) inset a NeoCellEditor's own drawCell() leaves free on each side, and
// also what NEO itself now leaves as the ONE remaining half-unit of trailing margin wherever the
// grid meets a frame/boundary it doesn't own outright (header-to-first-cell, last-cell-to-row-
// frame in Normal mode, last-cell-to-panel-edge in Full Height) - defined this early (rather than
// down near neoColumnPitchMm(), where the fuller rationale lives) specifically so
// neoRowAreaControlsWidthMm() below can use it too; #define order matters here. See
// neoColumnPitchMm()'s own comment, further down, for the full "cell editor owns its own padding
// now" reasoning this value is part of.
#define NEO_CELL_RECOMMENDED_PADDING_MM (NEO_FRAME_GAP_MM / 2.f)

// The module's initial AND minimum width, in HP - "initial width == min width" (Dieter's own
// instruction, 2026-07-20: "fixate the algorithm"). KISS-simplified 2026-07-22 (Dieter's own
// call): NEO no longer guarantees any minimum number of visible columns at all - the module's own
// panel width must NEVER change just from toggling Full Height/Normal or Grid Rows (only an actual
// user drag - or a locked instance converging toward one - may ever change PANEL_WIDTH_HP_JSON),
// and the earlier "always show >=4 columns" guarantee was the thing FORCING a resize across a mode
// switch whenever the two modes needed different widths for that same column count. The minimum
// is now simply "wide enough for the global area plus the row header at its own minimum width,
// with zero columns required" - showing zero columns at the absolute floor is fine; a future
// collapse/expand mechanism (queued, not yet built) is what will let a user reclaim columns
// without ever resizing, not a column-count floor here.
//
// Still MUST be the max across BOTH Normal and Full Height - same reasoning as before, just
// without the column term: switching modes must never require MORE width than the module already
// has, so the floor has to cover whichever mode's own controlsWidthMm is larger. rightPaddingMm
// is now the SAME value in both modes (2026-07-22 simplification, see
// neoRowAreaControlsWidthMm()'s own comment) - NEO_CELL_RECOMMENDED_PADDING_MM/2 = 0.762/2 =
// 0.381. NEO_ROW_HEADER_MIN_WIDTH_MM enlarged from 32HP to 38HP the same day (Dieter's own
// instruction, "enlarge the row header in width by 6HP to make some space") - recomputed below.
//   Normal:      controlsWidthMm = NEO_GLOBAL_AREA_WIDTH_MM (30.48) + NEO_ROW_HEADER_MIN_WIDTH_MM
//                                 (193.04) + rightPaddingMm (0.381) = 223.901
//                minWidthHp = ceil(223.901 / 5.08) = 45
//   Full Height: controlsWidthMm = 30.48 + 193.04 + NEO_RESIZE_RESERVED_WIDTH_MM (5.08)
//                                 + rightPaddingMm (0.381) = 228.981
//                minWidthHp = ceil(228.981 / 5.08) = 46
//   NEO_DEFAULT_WIDTH_HP = max(45, 46) = 46 (see neoMinWidthHpAnyMode() below - this hand
//                          derivation must always match what that function would compute for this
//                          same config, so the two never silently diverge)
// Recompute by hand and update this constant if NEO_ROW_HEADER_MIN_WIDTH_MM,
// NEO_GLOBAL_AREA_WIDTH_HP, NEO_RESIZE_RESERVED_WIDTH_HP, neoRowAreaControlsWidthMm()'s own
// right-padding formula, or the frame-margin constants above ever change.
#define NEO_DEFAULT_WIDTH_HP 46

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

// Minimum width for the per-row name/toggle/page controls, starting right after the global area
// above - about 4x the row header's original 40mm (2026-07-20 - it needs much more room, e.g.
// for a fuller Morpheus-style cell-type preview), enlarged a further 6HP on top of that
// (2026-07-22, Dieter's own instruction: "enlarge the row header in width by 6HP to make some
// space" - was 32HP, now 38HP). This is a pure FLOOR, never a snap-target: the row header's
// actual current width is not stored anywhere, it's recomputed fresh every time via
// neoComputeLayout() (below) as NEO_ROW_HEADER_MIN_WIDTH_MM + whatever leftover space the
// floor-based column count couldn't use (2026-07-21 redesign, replacing an earlier "snap to a
// target width at drag-start, absorb leftover past it at drag-end" model that persisted an
// incrementally-grown header width and caused a series of lock-sync regressions - see
// XHostImplementationGuide.md-adjacent session notes / neoComputeLayout()'s own comment).
#define NEO_ROW_HEADER_MIN_WIDTH_MM (38 * 5.08f)

// Resize handle geometry - the same small icon (right upper corner, vertically centered on the
// same y as the title text) is used in BOTH Normal and Full Height mode as of 2026-07-22 (see
// NeoResizeHandle::draw()'s own comment - the earlier Full-Height-specific full-height grip-strip
// look turned out effectively invisible in practice). NEO_RESIZE_RESERVED_WIDTH_MM below is still
// reserved out of Full Height's own grid width regardless - originally so the OLD full-height
// strip wouldn't visually cut across the (otherwise seamless) cells; kept unchanged since
// live-testing confirmed the reservation itself was already invisible/harmless, and removing it
// would mean re-deriving NEO_DEFAULT_WIDTH_HP for a purely cosmetic change.
#define NEO_RESIZE_ICON_SIZE_MM     5.f
#define NEO_RESIZE_RESERVED_WIDTH_HP 1
#define NEO_RESIZE_RESERVED_WIDTH_MM (NEO_RESIZE_RESERVED_WIDTH_HP * 5.08f)

// Seam-bridging Ext strips very slightly covered NEO's (and Morpheus's) own panel frame line -
// turned out to be a shared-code issue, not NEO-specific; the actual fix (X_STRIP_FRAME_NUDGE_MM)
// now lives in XShared.hpp's own addXExtStrip()/addXExtStripLeft(), fixing every module that uses
// them at once. NEO's own per-tick right-strip re-derivation (NeoWidget::step(), since its width
// isn't fixed) just matches that same shared formula.

// Lock button (global area, see Neo module's own NeoLockData/toggleLock()) - restyled 2026-07-20
// (Dieter's own instruction) to match X-family's own BIND/FREE button convention
// (X8BindButtonBase, X8Common.hpp): a full-width labeled bar, not a small square icon. Text reads
// "LOCK" while unlocked, "FREE" while locked (mirrors X-family's own Free rename). Color is
// three-state, same shape as X8ButtonBase's own accent color: grey (X_COLOR_INACTIVE_GREY) with
// no Host connected, the normal per-theme text color (xThemedTextColor()) while unlocked/showing
// "LOCK", green (NEO_LOCK_ON_COLOR) while locked/showing "FREE". Spans the global area's own
// frame width, inset by one frame-padding unit (NEO_FRAME_GAP_MM) on the left/top/right from that
// frame's own edges - same "nested element gets one frame-gap from its own containing frame"
// rule as every other framed element in NEO (see CLAUDE.md's "Code-drawn digital displays and
// knob rings" section) - height is a fixed 5mm, not tied to the frame's own bottom edge.
#define NEO_LOCK_BUTTON_X_MM      (NEO_FRAME_MARGIN_MM + NEO_FRAME_GAP_MM)
#define NEO_LOCK_BUTTON_Y_MM      (NEO_FRAME_TOP_MM + NEO_FRAME_GAP_MM)
#define NEO_LOCK_BUTTON_WIDTH_MM  (NEO_GLOBAL_AREA_WIDTH_MM - 1.5f * NEO_FRAME_GAP_MM - NEO_LOCK_BUTTON_X_MM)
#define NEO_LOCK_BUTTON_HEIGHT_MM 5.f

// Global FOLLOW button (2026-07-21) - directly below LOCK/UNLOCK, same X/width/height, one frame-
// gap below it (same "nested element gets one frame-gap from its own containing frame" rule the
// LOCK button itself already uses). Applies to every row at once - per-row FOLLOW is deferred for
// now (Dieter's own call, 2026-07-21; the per-row toggle button is removed, see Neo.cpp). Static
// "FOLLOW" label (not a toggling word like LOCK/UNLOCK), off = the theme's own plain text color,
// on = NEO_LOCK_ON_COLOR (green) - same accent green already established for "this is active" in
// this global area, reused rather than inventing a second green.
#define NEO_FOLLOW_BUTTON_X_MM      NEO_LOCK_BUTTON_X_MM
#define NEO_FOLLOW_BUTTON_Y_MM      (NEO_LOCK_BUTTON_Y_MM + NEO_LOCK_BUTTON_HEIGHT_MM + NEO_FRAME_GAP_MM)
#define NEO_FOLLOW_BUTTON_WIDTH_MM  NEO_LOCK_BUTTON_WIDTH_MM
#define NEO_FOLLOW_BUTTON_HEIGHT_MM NEO_LOCK_BUTTON_HEIGHT_MM

// Global FULL HEIGHT button (2026-07-22) - moved from the right-click "Full Height" menu item to
// a global-area button, directly below FOLLOW (Dieter's own instruction - button ordering in this
// column may get revisited later, this is just where it lands for now). Label reads the ACTION a
// click performs, same convention as LOCK/UNLOCK: "FULL" while in Normal mode (click enters Full
// Height), "NORMAL" while in Full Height mode (click returns to Normal). Deliberately no distinct
// on/off accent color for now (Dieter's own call: "no special color just orange for on and off")
// - NEO_FULLHEIGHT_ON_COLOR/OFF_COLOR are still defined as two separate constants regardless, so
// either can be given its own color later without restructuring anything.
#define NEO_FULLHEIGHT_BUTTON_X_MM      NEO_LOCK_BUTTON_X_MM
#define NEO_FULLHEIGHT_BUTTON_Y_MM      (NEO_FOLLOW_BUTTON_Y_MM + NEO_FOLLOW_BUTTON_HEIGHT_MM + NEO_FRAME_GAP_MM)
#define NEO_FULLHEIGHT_BUTTON_WIDTH_MM  NEO_LOCK_BUTTON_WIDTH_MM
#define NEO_FULLHEIGHT_BUTTON_HEIGHT_MM NEO_LOCK_BUTTON_HEIGHT_MM

// Global UNBIND button (2026-07-21) - a placeholder for now ("we do not yet know whether we will
// keep this button", Dieter's own words) that just calls the existing disconnectNeoHost() the
// right-click "Disconnect" menu item already uses - that menu item stays too, deliberately not
// replaced, until it's settled whether this button sticks around. Anchored to the BOTTOM of the
// global frame instead of stacking with LOCK/FOLLOW at the top - same one-frame-gap inset from the
// frame's own edge, mirrored from the top (NEO_LOCK_BUTTON_Y_MM's own "FRAME_TOP + GAP" shape).
// Text reads "FREE" (X-family's own established term for a Host-disconnect action, see CLAUDE.md's
// "Unbind" -> "Free" rename - LOCK/UNLOCK was deliberately renamed off "FREE" specifically so the
// two buttons never share a label for two unrelated actions).
#define NEO_UNBIND_BUTTON_X_MM      NEO_LOCK_BUTTON_X_MM
#define NEO_UNBIND_BUTTON_WIDTH_MM  NEO_LOCK_BUTTON_WIDTH_MM
#define NEO_UNBIND_BUTTON_HEIGHT_MM NEO_LOCK_BUTTON_HEIGHT_MM
#define NEO_UNBIND_BUTTON_Y_MM      (PANELHEIGHT - NEO_FRAME_BOTTOM_MM - NEO_FRAME_GAP_MM - NEO_UNBIND_BUTTON_HEIGHT_MM)
// Shared by EVERY button in the global area, not just the lock button (Dieter's own instruction,
// 2026-07-20: "all of those buttons in the global area will have the same fontsize, so one
// fontsize for all") - named generically for that reason, not after any one specific button.
// Was an inline literal (X8ButtonBase's own "2.82222f" copied verbatim) until Dieter asked
// whether it was configurable at all.
#define NEO_GLOBAL_AREA_BUTTON_FONT_SIZE_MM 4.f
#define NEO_LOCK_ON_COLOR  nvgRGB(0x00, 0xdd, 0x44)
// Both plain orange for now (Dieter's own call, 2026-07-22) - kept as two separate constants
// anyway, per this codebase's own convention, so either state can get its own distinct color
// later without needing to touch the button's own code, only these two values.
#define NEO_FULLHEIGHT_ON_COLOR  ORANGE
#define NEO_FULLHEIGHT_OFF_COLOR ORANGE

// How much width is "spent" on either side of the step-column grid, total - the global area,
// plus the row header's own CURRENT width (a computed-fresh-every-time value now, see
// neoComputeLayout()'s own comment below - callers either already have a NeoLayoutResult in hand,
// or pass NEO_ROW_HEADER_MIN_WIDTH_MM itself where no module instance/layout exists yet), plus Full Height's
// own reserved resize-handle strip when active - all on the LEFT - plus a right-hand trailing
// margin so the grid's own last column doesn't run flush to the panel's right edge OR row-area
// frame. Simplified 2026-07-22 (Dieter's own instruction, directly following the padding-removal
// work the same day - "the padding of the grid to frame on the right (normal mode) or module
// boundary (full mode) should be just 1/2 padding because the cell is doing the other half"):
// both modes now use the SAME half-recommended-padding margin, exactly mirroring how the grid's
// own LEFT start (header-to-first-cell) already works - NEO itself only ever reserves HALF of
// NEO_CELL_RECOMMENDED_PADDING_MM at any boundary it doesn't fully own, and a conforming last
// cell's own drawCell() self-inset supplies the other half, summing back to one full padding unit
// total, same as any ordinary cell-to-cell gap. Replaces the older, asymmetric Normal-vs-Full-
// Height formula (a peer-frame half-gap plus, in Normal mode only, a further full gap) that
// predates the cell-owns-its-own-padding redesign. THE one place this decision is made; every
// width/column computation in Neo.cpp threads its result through rather than computing it
// separately.
inline float neoRowAreaControlsWidthMm(bool fullHeight, float rowHeaderWidthMm)
{
	float rightPaddingMm = NEO_CELL_RECOMMENDED_PADDING_MM / 2.f; // same margin in both modes now
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

// Track select knob's own fixed Param range - real Params need a fixed range at configParam()
// time, but a Host's actual track count is only known once connected (Morpheus: 18) and is
// generically allowed to differ per Host. A generous fixed ceiling with the actually-used track
// id clamped to min(paramValue, host->getTrackCount()-1) at read time (Neo.cpp) - turning the
// knob past a connected Host's own real count just holds at its last real track, same spirit as
// X-family's own dimming-beyond-channel-limit convention - avoids ever needing to reconfigure a
// live Param's own range/ParamQuantity when the connected Host changes.
#define NEO_MAX_TRACKS 32

// Per-row control x-offsets, measured from the global area's own right edge (added to
// NEO_GLOBAL_AREA_WIDTH_MM at each use site) - and their own widget sizes.
// Track/channel select knobs + their own numeric displays (2026-07-20 addition) - sequentially
// first in the header, ahead of the name field, per Dieter's own spec: "for the beginning we
// just put them sequentially in the header." Track display is 4 characters ("TAPE"/"MSEL"/
// "M-01".."M-16"), channel display is 2 ("1".."16") - widths sized for those exact character
// counts at NEO_ROW_NUMBER_DISPLAY_FONT_SIZE_MM plus the display-background frame's own inset.
// Knob positions are CENTER x (matches createParamCentered()'s own convention), everything else
// here is a left edge. Sizing/spacing enlarged 2026-07-20 (Dieter's own follow-up, after seeing
// the first pass live: bigger font, a real display-background+frame around each number, vertical
// centering, frame height trimmed down ~0.5mm) - the actual per-widget gap between any two
// header controls is always NEO_FRAME_GAP_MM, the same one universal padding unit every other
// gap in NEO already uses (Dieter's own instruction: "the spacing between the displays and
// buttons should be as always frame padding") - no separate hand-picked gap constant, so "the
// padding stays the same" automatically even if a display's own width changes again later,
// rather than independent gap values that could silently drift out of sync with each other.
// Exact values below are lifted from Dieter's own reference SVG (res/DisplaysWithKnobsInFrame.svg,
// 2026-07-20) rather than hand-picked - see its own extraction notes: every gap is exactly
// NEO_FRAME_GAP_MM (confirmed 3 times, the SVG even has literal "h 1.524" tick marks spanning
// each one), the knob gets a themed drawn ring (NeoRowKnobRingWidget) matching the display's own
// frame styling - Dieter's own instruction: build it as its own separable widget/option here,
// don't retrofit it onto any OTHER module's knobs yet ("will use the drawn version in future
// because it makes layout much easier").
// NEO_ROW_TRACK_DISPLAY_X_MM (2026-07-20, Dieter's own correction): the first display needs the
// SAME padding from the row-header-frame's own left edge that the row-header-frame itself has
// from ITS surrounding (row area) frame in Normal mode - that inset is exactly NEO_FRAME_GAP_MM
// (see headerFrameLeftMm's own comment, NeoWidget::step()). headerFrameLeftMm itself, relative
// to the global area edge, is (NEO_FRAME_GAP_MM / 2 + NEO_FRAME_GAP_MM) = 1.5x the gap - add one
// more full gap for the display's own inset: 2.5x total.
#define NEO_ROW_TRACK_DISPLAY_X_MM      (2.5f * NEO_FRAME_GAP_MM)
#define NEO_ROW_TRACK_DISPLAY_WIDTH_MM  olDisplayWidthMm(4, NEO_ROW_NUMBER_DISPLAY_FONT_SIZE_MM, NEO_ROW_DISPLAY_TEXT_INSET_MM)
#define NEO_ROW_CHANNEL_DISPLAY_WIDTH_MM olDisplayWidthMm(2, NEO_ROW_NUMBER_DISPLAY_FONT_SIZE_MM, NEO_ROW_DISPLAY_TEXT_INSET_MM)
#define NEO_ROW_NUMBER_DISPLAY_HEIGHT_MM 6.1f
// Text sits 0.25mm below the box's own vertical center (Dieter's own correction, 2026-07-20) -
// shared by every one of NEO's row displays, not just track/channel.
#define NEO_ROW_DISPLAY_TEXT_Y_OFFSET_MM 0.25f
#define NEO_ROW_SELECT_KNOB_SIZE_MM     (6.f * NEO_FRAME_GAP_MM) // = 9.144mm ring diameter, matches
                                                                  // the reference SVG's own knob
                                                                  // ring exactly (radius = 3x gap)
#define NEO_ROW_NUMBER_DISPLAY_FONT_SIZE_MM 6.f

// Derived, not hand-picked - see NEO_ROW_TRACK_DISPLAY_X_MM's own comment. Kept as real
// constant-expressions (not runtime code) so they're still usable as compile-time #defines
// everywhere the older hand-picked ones were.
#define NEO_ROW_TRACK_KNOB_X_MM    (NEO_ROW_TRACK_DISPLAY_X_MM + NEO_ROW_TRACK_DISPLAY_WIDTH_MM + NEO_FRAME_GAP_MM + NEO_ROW_SELECT_KNOB_SIZE_MM / 2.f)
#define NEO_ROW_CHANNEL_DISPLAY_X_MM (NEO_ROW_TRACK_KNOB_X_MM + NEO_ROW_SELECT_KNOB_SIZE_MM / 2.f + NEO_FRAME_GAP_MM)
#define NEO_ROW_CHANNEL_KNOB_X_MM    (NEO_ROW_CHANNEL_DISPLAY_X_MM + NEO_ROW_CHANNEL_DISPLAY_WIDTH_MM + NEO_FRAME_GAP_MM + NEO_ROW_SELECT_KNOB_SIZE_MM / 2.f)
#define NEO_ROW_NAME_X_MM            (NEO_ROW_CHANNEL_KNOB_X_MM + NEO_ROW_SELECT_KNOB_SIZE_MM / 2.f + NEO_FRAME_GAP_MM)

// On-panel editable channel-identity field (2026-07-21, replacing the old flat 16mm placeholder
// now that the right-click "Channels" menu is gone - see CLAUDE.md's own note on this being
// OrangeLine's first inline-editable ui::TextField). A small colored dot (fill =
// channelColor[track][channel]) precedes an 8-character editable name field, same shared width
// formula every other NEO display uses (olDisplayWidthMm() - no hand-picked field width). The dot
// is deliberately NOT a button/menu/popup (Dieter's own explicit direction, after trying and
// rejecting both a floating swatch menu and an in-panel picker panel idea) - it's a hidden-knob-
// style click-drag control (see NeoRowColorDotWidget, Neo.cpp) that cycles NEO_COLOR_SWATCHES
// directly. Diameter is half NEO_ROW_NUMBER_DISPLAY_HEIGHT_MM (Dieter's own catch, 2026-07-21:
// the first pass at "medium" was much too large/present for a small color control). Dot-to-field
// gap uses the standard frame-padding unit (NEO_FRAME_GAP_MM) - Dieter's own catch, 2026-07-21:
// the display text inset (0.81mm) used at first was too narrow between two separate controls.
// This fits comfortably inside the row header's own minimum-width envelope
// (NEO_ROW_HEADER_MIN_WIDTH_MM, 162.56mm) with room to spare before the right-aligned position
// display. The field is a FIXED width, same as every other row display (Dieter's own reversal,
// 2026-07-21, of an earlier "grow to fill leftover header width" idea - that made padding/spacing
// after the field inconsistent whenever the header's own current width - now recomputed fresh via
// neoComputeLayout() rather than persisted/incrementally grown - exceeded this minimum; only the
// right-aligned position display still tracks the header's own current width). NEO_DEFAULT_
// WIDTH_HP/neoMinWidthHp()/neoMaxWidthHp() are derived from NEO_ROW_HEADER_MIN_WIDTH_MM as one
// opaque envelope value, not from this field's own internal breakdown, so none of them need
// recomputing for this change.
#define NEO_ROW_NAME_DOT_DIAMETER_MM  (NEO_ROW_NUMBER_DISPLAY_HEIGHT_MM / 2.f)
#define NEO_ROW_NAME_DOT_GAP_MM       NEO_FRAME_GAP_MM
// mm of vertical drag per color step (NeoRowColorDotWidget's own click-drag cycling) - first-pass
// value, live-tune once built.
#define NEO_ROW_NAME_DOT_DRAG_STEP_MM 4.f
// Below this total accumulated mouse movement (px, both axes summed), a press+release on the
// color dot counts as a plain CLICK rather than a color-cycling drag (2026-07-22, Dieter's own
// request - "give it a click action executed when clicked and not dragged"). First-pass value,
// live-tune once built.
#define NEO_ROW_NAME_DOT_CLICK_THRESHOLD_PX 3.f
#define NEO_ROW_NAME_FONT_SIZE_MM    NEO_ROW_NUMBER_DISPLAY_FONT_SIZE_MM
// Hard cap on typed name length (Dieter's own catch, 2026-07-21: without an enforced limit the
// user could keep typing arbitrarily far past the field's own drawn width) - single source of
// truth for both the width formula below and NeoRowNameField's own input-length enforcement
// (Neo.cpp), so they can never silently drift apart.
#define NEO_ROW_NAME_MAX_CHARS 8
// Plain, standard display sizing - the same olDisplayWidthMm() call with the same padding every
// other NEO display uses, no per-field nudge (Dieter's own correction, 2026-07-21: this is an
// ordinary display field, it shouldn't need one at all - an earlier pass here added a nudgeMm
// correction, which was itself the wrong move, not just the wrong shape of fix).
#define NEO_ROW_NAME_TEXT_WIDTH_MM   olDisplayWidthMm(NEO_ROW_NAME_MAX_CHARS, NEO_ROW_NAME_FONT_SIZE_MM, NEO_ROW_DISPLAY_TEXT_INSET_MM)

// On-panel cell-editor-selection display (2026-07-22) - shows the row's currently selected
// NeoCellEditor's own name() ("Gate"/"Value"/"Morpheus"/"Knob"), 8-character budget same as the
// name field above (Dieter's own spec: "an 8char display field for the cell editor selection"),
// same shared width formula/font size as every other NEO display - no per-field nudge. Sits right
// after the name field, in the space LEFT/RIGHT paging used to occupy before they moved to sit
// beside the position display (see NeoWidget::step()'s own comment for the reordering).
#define NEO_ROW_CELLTYPE_MAX_CHARS 8
#define NEO_ROW_CELLTYPE_DISPLAY_WIDTH_MM olDisplayWidthMm(NEO_ROW_CELLTYPE_MAX_CHARS, NEO_ROW_NAME_FONT_SIZE_MM, NEO_ROW_DISPLAY_TEXT_INSET_MM)

// Header-widget pool (2026-07-22 "multi-value cell editor" extension) - a fixed, always-existing
// set of 4 generic knob+display+ring triples in the row header (Range Min, Range Max, Secondary
// Track, Secondary Channel), whichever cell editor is currently selected for a row just relabels/
// shows-hides these same pre-built widgets (NeoCellEditor::configureHeaderWidgets(), Neo.cpp) -
// same "always exists, toggle ->visible" convention as trackKnobs[]/channelKnobs[]/cellTypeKnobs[].
// Option C layout (Dieter's own choice, confirmed against a live visual mockup comparing it to one
// wide row of full-size knobs): TWO STACKED ROWS of SMALL knob+display pairs - Range Min/Max on
// top, Secondary Track/Channel below - rather than one long horizontal chain of 4 full-size pairs.
#define NEO_ROW_RANGE_DISPLAY_MAX_CHARS 6
#define NEO_ROW_RANGE_DISPLAY_WIDTH_MM olDisplayWidthMm(NEO_ROW_RANGE_DISPLAY_MAX_CHARS, NEO_POOL_TINY_FONT_SIZE_MM, NEO_ROW_DISPLAY_TEXT_INSET_MM)
// ROW_RANGE_MIN_PARAM/ROW_RANGE_MAX_PARAM's own fixed configParam() ceiling - same reasoning as
// NEO_MAX_TRACKS: a real Param needs a fixed range at configParam() time, but the actually-
// meaningful range is only known once connected (the live source's own current
// NeoChannelProperties.rangeMin/rangeMax). A generous fixed ceiling here, with the real,
// shrink-only EFFECTIVE range computed at read time (NeoValueCellEditorBase::
// computeEffectiveRange(), Neo.cpp) by clamping the raw knob value inside whatever the live
// source currently reports - never the other way around.
#define NEO_ROW_RANGE_PARAM_MIN_LIMIT -10.f
#define NEO_ROW_RANGE_PARAM_MAX_LIMIT  10.f

// "Tiny" pool-widget sizing (Option C) - uses Trimpot (Rack's own smaller standard knob - already
// the idiomatic "compact/secondary control" choice in VCV modules generally), NOT a scaled-down
// RoundSmallBlackKnob: this codebase's own established pitfall (CLAUDE.md, "Self-drawn widget
// backgrounds", point 4) - a widget that loads a fixed-size SVG asset must be sized to that
// asset's OWN natural size and never scaled to fit a differently-sized panel cell, or its stroke
// widths distort. NEO_POOL_TINY_KNOB_RING_SIZE_MM is sized AROUND Trimpot's own natural size the
// same way NEO_ROW_SELECT_KNOB_SIZE_MM is sized around the full knobs' own natural size - both
// values below are first-pass PLACEHOLDERS, not yet measured against Trimpot's real on-screen
// size, flagged exactly like every other "first-pass, live-tune once built" constant already
// added this session - measure and correct once actually placed in Rack.
#define NEO_POOL_TINY_KNOB_RING_SIZE_MM   5.5f   // PLACEHOLDER - measure Trimpot's real size before trusting this
#define NEO_POOL_TINY_FONT_SIZE_MM        3.5f   // PLACEHOLDER
#define NEO_POOL_ROW_GAP_MM (NEO_FRAME_GAP_MM / 2.f) // vertical gap between the two stacked pool rows
#define NEO_POOL_TRACK_DISPLAY_MAX_CHARS 4
#define NEO_POOL_TRACK_DISPLAY_WIDTH_MM olDisplayWidthMm(NEO_POOL_TRACK_DISPLAY_MAX_CHARS, NEO_POOL_TINY_FONT_SIZE_MM, NEO_ROW_DISPLAY_TEXT_INSET_MM)
#define NEO_POOL_CHANNEL_DISPLAY_MAX_CHARS 2
#define NEO_POOL_CHANNEL_DISPLAY_WIDTH_MM olDisplayWidthMm(NEO_POOL_CHANNEL_DISPLAY_MAX_CHARS, NEO_POOL_TINY_FONT_SIZE_MM, NEO_ROW_DISPLAY_TEXT_INSET_MM)
// Header floor: NEO_ROW_HEADER_MIN_WIDTH_MM/NEO_DEFAULT_WIDTH_HP deliberately NOT changed yet -
// Option C's real footprint depends on Trimpot's actual measured size, unknown until built. Build
// the pool with the placeholder constants above, measure the real resulting width live in Rack,
// THEN do the same hand-derivation NEO_DEFAULT_WIDTH_HP's own comment already documents
// (recompute neoRowAreaControlsWidthMm() for both modes, ceil() each to HP, take the max) - do not
// skip this step once the real numbers are in.

// NEO_ROW_NAME_WIDTH_MM/FOLLOW_X_MM/LEFT_X_MM/RIGHT_X_MM used to be static #defines here, chained
// off the field's own BASE width - removed 2026-07-21 (Dieter's own catch: FOLLOW/LEFT/RIGHT kept
// overlapping the name field) because the header's own current width - and everything to the
// right of the fixed-width name field - moves whenever the row header's actual width (now
// neoComputeLayout()'s own headerWidthMm, recomputed fresh, never persisted) exceeds its minimum
// - a static macro structurally can't know about that runtime growth, so it silently drifted out
// of sync with the field's real edge. LEFT/RIGHT's own x-positions are now computed directly in
// NeoWidget::step() from the field's own real current right edge every frame instead - see that
// function for the actual math.
// NEO_ROW_TOGGLE_WIDTH_MM/HEIGHT_MM sized the old per-row FOLLOW button, removed 2026-07-21 in
// favor of one global FOLLOW button (see NEO_FOLLOW_BUTTON_*_MM above) - kept here, unused, for
// the same reason ROW_FOLLOW_JSON/PARAM are kept: re-introducing a per-row toggle later shouldn't
// need to re-derive its size from scratch.
#define NEO_ROW_TOGGLE_WIDTH_MM  6.f  // FOLLOW toggle button
#define NEO_ROW_TOGGLE_HEIGHT_MM 4.f
#define NEO_ROW_PAGEBTN_SIZE_MM  4.f  // LEFT/RIGHT paging buttons (square)

// Right-aligned page/position display (2026-07-20 addition, Dieter's own spec: "pp/PP:sss/SSS
// with pp current page, PP number of pages and sss current step pos and SSS LEN", later split
// into two stacked lines - see getText/getText2's own comment, Neo.cpp). Anchored to the header's
// own actual current right edge (NeoWidget::step(), since header width isn't fixed) rather than
// a fixed left-edge offset like every other row control above - shrinking this width alone keeps
// the right margin fixed and just eats into the left side, by construction of that same
// right-edge-derived formula. Width sized for the longer of the two lines now ("sss/SSS",
// 7 characters, space-padded per-field like "%3d/%3d" rather than zero-padded, monospace font so
// this reads as right-aligned within each field) - a first estimate, not yet visually confirmed.
#define NEO_ROW_POSITION_DISPLAY_WIDTH_MM olDisplayWidthMm(7, NEO_ROW_POSITION_DISPLAY_FONT_SIZE_MM, NEO_ROW_DISPLAY_TEXT_INSET_MM)
// Experiment, 2026-07-20 (Dieter's own request, "just to see how it looks"): height matched to
// the knob ring's own diameter instead of the near-zero-padding text-height ratio every other
// display uses - not yet a confirmed final choice.
#define NEO_ROW_POSITION_DISPLAY_HEIGHT_MM NEO_ROW_SELECT_KNOB_SIZE_MM
#define NEO_ROW_POSITION_DISPLAY_FONT_SIZE_MM 4.7f
#define NEO_ROW_POSITION_DISPLAY_MARGIN_MM NEO_FRAME_GAP_MM // gap from the header's own right edge
// Two-line stacked experiment (2026-07-20) - the second line nudged up a hair from where the
// plain half-height centering would otherwise put it.
#define NEO_ROW_DISPLAY_LINE2_Y_NUDGE_MM -0.2f

// Muted "not applicable" text color (2026-07-22) - the position display's own page/pages line
// (line1) uses this instead of its usual row/theme color when zero columns are visible at all
// (Dieter's own instruction: "if no columns is visible we do not have a mean of a page... it
// should just show a grey -/-" - replacing the previous divide-by-zero-avoiding freeze at
// whatever numPages happened to be before hitting zero columns). A neutral grey, theme- and
// row-color-independent by design - it's specifically meant to read as "this reading doesn't
// apply right now," same spirit as a disabled UI control staying grey regardless of theme.
#define NEO_ROW_MUTED_TEXT_COLOR nvgRGB(0x80, 0x80, 0x80)

// Display background + frame (2026-07-20, Dieter's own follow-up spec, exact values from
// res/DisplaysWithKnobsInFrame.svg) - shared by every one of NEO's own row displays (track/
// channel/position). Background color moved to OrangeLine.hpp's own OL_DISPLAY_BG_* (2026-07-21
// extraction, see CLAUDE.md's "Code-drawn digital displays and knob rings" section) since it's a
// genuinely reusable convention, not NEO-specific. Frame stroke uses the same theme lookup
// (X_FRAME_ORANGE/DARK/BRIGHT, XShared.hpp) every other framed element in NEO already uses.
// Corner radius/stroke width both reuse existing established constants (NEO_FRAME_MARGIN_MM/
// NEO_FRAME_STROKE_MM) rather than new ones - the reference SVG's own measured radius (0.762mm)
// and stroke (0.3mm) turned out to already exactly match those.
#define NEO_ROW_DISPLAY_STROKE_MM NEO_FRAME_STROKE_MM
#define NEO_ROW_DISPLAY_RADIUS_MM NEO_FRAME_MARGIN_MM
#define NEO_ROW_DISPLAY_TEXT_INSET_MM 0.81f // left inset of the text within its own display background

// Row-color-following mode - originally two independent compile-time #defines
// (NEO_ROW_COLOR_FRAME/NEO_ROW_COLOR_TEXT) for live A/B testing whether frame/text should follow
// the row's own identity color; superseded 2026-07-22 (Dieter's own request, once the combination
// looked good, then further split into 3 independent axes once he clarified the header frame
// needs its own NONE choice) by a real, persisted, user-facing choice, cycled by a click on any
// row's color dot - see ROW_COLOR_MODE_JSON (jsonIds below) and Neo.cpp's own NEO_ROW_COLOR_MODES
// list + Neo::rowColorHeaderFrameMode()/rowColorDisplayFrameEnabled()/rowColorTextEnabled(). Does
// NOT affect NeoRowNameField's own text - that one already unconditionally follows the row color
// (2026-07-21, confirmed working) regardless of this mode. Also synced across a LOCK group
// (Dieter's own instruction: "and of course the locked NEOs have to follow") - see NeoLockData's
// own "colorMode" field, Neo.cpp.

// Knob ring (2026-07-20, Dieter's own follow-up spec, exact values from the same reference SVG) -
// a themed circle drawn around each track/channel knob, same stroke/color convention as the
// display frame above. Diameter is NEO_ROW_SELECT_KNOB_SIZE_MM (already the reference SVG's own
// exact ring size) - built as its own separate, self-contained widget (NeoRowKnobRingWidget,
// Neo.cpp) specifically so it stays optional per knob rather than baked into the knob itself;
// Dieter's own instruction: only these two NEO knobs get one for now, not retrofitted onto any
// other module's own knobs yet.
#define NEO_ROW_KNOB_RING_STROKE_MM NEO_FRAME_STROKE_MM

// Step-cell color (NeoRowCellsWidget). The horizontal padding between cells is NOT a separate
// fixed value - it's derived every draw() call from the actual current gap (pitch minus cell
// width), which always equals the row-gap neoRowLayout() computes, so horizontal spacing matches
// vertical spacing exactly (Dieter's own instruction, 2026-07-20 - "buttons should have the same
// padding horizontally [as rows do vertically]"). NEO_CELL_BG_COLOR_* is an always-visible
// per-cell backdrop drawn for every column regardless of gate/value content. Per-theme, not a
// single fixed gray (2026-07-22, Dieter's own instruction: "the cell editors have to use different
// colors for different parts of their rendering which is not defined by row color. cell background
// color is always one of those" - i.e. every cell editor shares this one backdrop, and it must
// follow the active theme like every other non-row-colored fixed color does). Now aliases
// X_STRIP_BG_ORANGE/DARK/BRIGHT directly (2026-07-22 follow-up, Dieter's own instruction: "the
// default background color of the cells should match the background color of the theme") - the
// same plain panel/strip background NeoPanelWidget's own fill already uses, so an empty cell now
// blends into the panel at rest instead of standing out as its own separate gray box (the earlier
// "boundaries read clearly even at rest" reasoning for a distinct gray no longer applies now that
// each cell editor's own frame/content already provides that separation).
#define NEO_CELL_BG_COLOR_ORANGE X_STRIP_BG_ORANGE
#define NEO_CELL_BG_COLOR_BRIGHT X_STRIP_BG_BRIGHT
#define NEO_CELL_BG_COLOR_DARK   X_STRIP_BG_DARK

// Ctrl/Cmd fine-tuning drag divisor (2026-07-22, Dieter's own request: "real knobs allow for
// holding left ctrl... to get a fine tuning possibility which reduces the sensitivity" - same
// convention every real Rack Knob/ParamWidget already offers, replicated here since NEO's own
// step-cell dragging is a custom widget, not a real ParamWidget). Applies generically to EVERY
// NeoCellEditor's own dragValue() via NeoRowCellsWidget::onDragMove() - not cell-type-specific,
// so it lives here rather than in the fallback editor's own section below. Divides the effective
// mouseDelta.y BEFORE it reaches dragValue() while RACK_MOD_CTRL (Ctrl on Windows/Linux, Cmd on
// Mac - see widget/event.hpp) is held, so every editor's own drag automatically gets slower/
// finer control with no per-editor changes needed. First-pass value, not yet feel-tuned live.
#define NEO_FINE_TUNE_DRAG_DIVISOR 4.f

// Default head-position marker (2026-07-22) - NeoCellEditor::drawHeadFrame()'s own default body
// draws this small frame; NEO calls drawHeadFrame() directly whenever a visible cell is the
// channel's current play cursor, no separate query beforehand (Dieter's own correction of an
// earlier isHeadAware()-query design he called out as not good: "the cell editor class should
// already have a method to draw frame for active head position... a cell editor which does not
// want this can override this method with its own"). None of the current concrete editors
// override drawHeadFrame() yet - every one of them gets this default. Per-theme, not a single
// fixed color (Dieter's own follow-up: "all fixed colors not defined by the row color should use
// a correct color for the active theme... we have to define a color for each theme which is used
// for NEOs position cursor frame") - three independent constants, same "kept separate so either
// can get its own distinct color later" precedent as NEO_FULLHEIGHT_ON_COLOR/OFF_COLOR, even
// though all three start at the same first-pass white (not yet visually tuned per theme).
#define NEO_HEAD_FRAME_COLOR_ORANGE nvgRGB(0xff, 0xff, 0xff)
#define NEO_HEAD_FRAME_COLOR_BRIGHT nvgRGB(0xff, 0xff, 0xff)
#define NEO_HEAD_FRAME_COLOR_DARK   nvgRGB(0xff, 0xff, 0xff)
// Tuned 2026-07-22 (Dieter's own instruction) - the default head frame was sitting close enough
// to a conforming cell editor's own frame (inset NEO_CELL_RECOMMENDED_PADDING_MM, 0.762mm) that
// the two visibly touched/overlapped once a real framed editor (NeoFallbackCellEditor/"Knob") was
// actually tested live. Both inset and stroke width are now NEO_FRAME_GAP_MM/5 (the standard
// frame-padding unit divided by 5, ~0.3048mm, not the ~0.25mm Dieter estimated from memory -
// checked against the actual constant per his own request to "adapt for the exact number") -
// equal to each other by design, so the head frame reads as a clean, evenly-weighted ring. This
// leaves the head frame spanning roughly 0.15-0.46mm from the cell's own raw edge, comfortably
// clear of a conforming editor's own frame (which starts around 0.61mm), with a real visible gap
// between the two instead of the previous overlap (old inset/stroke 0.5/0.4mm spanned 0.3-0.7mm,
// crossing into the editor's own frame at 0.61mm).
#define NEO_HEAD_FRAME_STROKE_MM (NEO_FRAME_GAP_MM / 5.f)
#define NEO_HEAD_FRAME_INSET_MM  (NEO_FRAME_GAP_MM / 5.f)

// Morpheus-style cell (NeoMorpheusStyleCellEditor, cellType 2) - replicates MorpheusDisplayWidget's
// own value-to-color technique (Morpheus.cpp) at NEO's own per-cell scale: a step's raw bipolar
// voltage lerps the cell's fill between these two colors, rather than an on/off square or a bar
// height. Exact same RGB values as Morpheus's own DISPLAY_BIPOLAR_LOW_COLOR/HIGH_COLOR (its
// simplest display mode, not the match/dirty "scaleOnOutput" variant) - first pass, 2026-07-20,
// Dieter's own request: "replicate the display of Morpheus... we will enhance and tune from that
// point on."
#define NEO_MORPHEUS_CELL_LOW_COLOR  nvgRGB(164, 32, 32)
#define NEO_MORPHEUS_CELL_HIGH_COLOR nvgRGB(32, 164, 32)

// NeoPitchGateCellEditor (Neo.cpp) own layout - share of the cell's own height reserved for the
// bottom gate strip; the rest (minus one NEO_CELL_RECOMMENDED_PADDING_MM gap between the two
// zones) goes to the pitch-value bar above it. Same "share of cellHeightPx" shape as
// NEO_FALLBACK_DISPLAY_HEIGHT_RATIO - first-pass value, not yet visually tuned live.
#define NEO_PITCHGATE_GATE_STRIP_HEIGHT_RATIO 0.22f

// Fallback cell (NeoFallbackCellEditor/"Knob") own layout/style constants - pulled out of
// NeoRowCellsWidget's own drawCell() (2026-07-22, Dieter's own instruction: "pull that into
// constants as always, there will always be some fine tuning necessary later on"), matching this
// codebase's own "config #defines belong in <Name>.hpp, never in <Name>.cpp" convention (CLAUDE.md).
//
// ABSOLUTE POSITION/SIZE CONVENTION (2026-07-22, Dieter's own instruction - "let's define a
// convention here to reduce misunderstanding"), applies to every CURRENT and FUTURE constant in
// this section that represents a raw offset or size (not a plain ratio of something that already
// scales, like NEO_FALLBACK_DISPLAY_HEIGHT_RATIO above):
//   - Origin is the CELL's own CENTER, not its top-left corner - (0,0) = cellWidthPx/2,
//     cellHeightPx/2. Positive X = right, positive Y = down (same direction NanoVG's own Y axis
//     already uses).
//   - Every such constant is authored as an ABSOLUTE mm value, tuned BY EYE while looking at
//     NEO_ROWS_MAX (8) displayed rows specifically - the smallest, most cramped cell size Normal
//     mode ever produces, so it's the natural "hardest case" reference to tune against.
//   - At any OTHER row count (fewer rows -> bigger cells), the same constant is multiplied by
//     NEO_FALLBACK_REFERENCE_SCALE(cellHeightPx) below before use, so it scales UP proportionally
//     with the cell rather than staying visually tiny in a much bigger cell (or, conversely,
//     staying fixed-size regardless of how little room an 8-row cell actually has).
// This does NOT apply to NEO_CELL_RECOMMENDED_PADDING_MM/NEO_FRAME_GAP_MM themselves - those are a
// separate, already-established, genuinely fixed-mm convention shared across every cell editor
// and the row header, not something this section redefines.
#define NEO_FALLBACK_REFERENCE_CELL_HEIGHT_MM ((PANELHEIGHT - NEO_FRAME_TOP_MM - NEO_FRAME_BOTTOM_MM) / (float) NEO_ROWS_MAX)
#define NEO_FALLBACK_REFERENCE_SCALE(cellHeightPx) ((cellHeightPx) / mm2px(NEO_FALLBACK_REFERENCE_CELL_HEIGHT_MM))
//
// Vertical stack, top to bottom, every gap exactly NEO_CELL_RECOMMENDED_PADDING_MM (2026-07-22,
// Dieter's own spec): frame's own top inner edge, one gap, the knob (sized to fill whatever's
// left, not scaled down), one gap, the display strip, one gap, frame's own bottom inner edge.
// NEO_FALLBACK_DISPLAY_HEIGHT_RATIO is the display strip's own share of the full cell height;
// the knob's diameter is simply whatever remains after that and the three padding gaps.
#define NEO_FALLBACK_DISPLAY_HEIGHT_RATIO    0.28f
#define NEO_FALLBACK_DISPLAY_FONT_SIZE_RATIO 0.825f // of the display strip's own height - 0.75 was the original, 0.9 tried and found too big, settled halfway back
// Knob body fill - deliberately theme-INDEPENDENT (mirrors every real Rack knob/SvgKnob's own
// fixed dark/black physical-control look elsewhere in this codebase, not a themed surface like a
// digital display's own background) - see CLAUDE.md's project_neo_module memory note for why this
// was deliberately left out of the per-theme NEO_CELL_BG_COLOR_*/NEO_HEAD_FRAME_COLOR_* treatment.
#define NEO_FALLBACK_KNOB_BODY_COLOR nvgRGB(0x20, 0x20, 0x20)
// Ratios of the knob's own DRAWN radius (already fully proportional by construction, so these
// don't need the absolute-mm/reference-scale treatment above) - ring stroke, pointer stroke, and
// the pointer's own start/end radii (2026-07-22: the pointer is a segment floating between two
// radii from the knob's own center, not always anchored at the exact center - start=0 keeps
// today's look, a positive start pulls the pointer's own near end away from dead-center).
#define NEO_FALLBACK_KNOB_RING_STROKE_RATIO      0.12f
#define NEO_FALLBACK_KNOB_POINTER_STROKE_RATIO   0.15f
#define NEO_FALLBACK_KNOB_POINTER_START_RATIO    0.f
#define NEO_FALLBACK_KNOB_POINTER_END_RATIO      0.8f
// Floor under both the ring and pointer stroke widths above, in raw px (not mm - a sub-1px stroke
// would simply vanish at typical zoom, regardless of how small the knob itself has scaled down).
#define NEO_FALLBACK_KNOB_MIN_STROKE_PX 1.f
// Knob size/position fine-tuning (2026-07-22, Dieter's own follow-up: "give constants to move and
// size it too") - layered ON TOP of the stack-derived slot above, not a replacement for it: the
// slot's own center (cy) and diameter (knobDiameterPx) still come from the padding-stack math, so
// gaps/alignment with the frame and display strip stay correct; these three just additionally
// scale/nudge what's actually DRAWN within that same slot. NEO_FALLBACK_KNOB_SIZE_RATIO (1 = fill
// the whole slot, matching today's look) shrinks/grows the drawn circle (and, since the pointer's
// own length/stroke are themselves ratios of the knob's radius, everything scales together) while
// keeping it centered on the SAME slot center - never touches the slot's own reserved space, so
// the surrounding gaps to the frame/display never change even if this knob visually shrinks. The
// X/Y nudges follow the ABSOLUTE POSITION/SIZE CONVENTION above - plain mm offsets from the cell's
// own center, tuned by eye at 8 rows, scaled by NEO_FALLBACK_REFERENCE_SCALE() at other row
// counts - same "small residual tunable nudge" pattern as OLLabelButton's own textYNudgePx (see
// CLAUDE.md) - a formula gets you close, this is for whatever correction still looks needed after.
#define NEO_FALLBACK_KNOB_SIZE_RATIO 1.f
#define NEO_FALLBACK_KNOB_X_NUDGE_MM 0.f
#define NEO_FALLBACK_KNOB_Y_NUDGE_MM 0.f

// Drag sensitivity (2026-07-22, Dieter's own catch: "the react a bit too hard on the mouse...
// too little mouse move between min and max") - NeoFallbackCellEditor::dragValue() originally
// mapped exactly cellHeightPx of vertical travel to the full rangeMin..rangeMax span; this
// multiplies that required travel distance, so LARGER = LESS sensitive (more mouse movement
// needed to cover the same range), same "divide by this" role NEO_ROW_NAME_DOT_DRAG_STEP_MM
// plays for the color dot's own drag-cycling. First-pass value, not yet visually/feel-tuned live.
#define NEO_FALLBACK_DRAG_SENSITIVITY_RATIO 3.f

// NEO_MIN_VISIBLE_COLS is gone (2026-07-22, Dieter's own KISS simplification) - NEO no longer
// guarantees any minimum visible column count at all; see NEO_DEFAULT_WIDTH_HP's own comment for
// why (a future collapse/expand mechanism, not a column-count floor, is what reclaims columns
// without ever resizing the panel).

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

// Head-position frame's own corner radius (2026-07-22, Dieter's own catch: "the radius has to add
// for additional size to sneek gently around the radius of the inner cells frame" - using
// NEO_ROW_HEADER_RIGHT_RADIUS_MM directly, unchanged, was wrong once actually seen live, since the
// head frame sits OUTSIDE a conforming cell editor's own frame, not flush against it). Same
// "Nested frames" corner-radius rule as NEO_ROW_HEADER_LEFT_RADIUS_MM just above, applied in the
// opposite direction: going from an INNER radius out to an OUTER one (rather than outer to inner)
// means ADDING the gap between the two rings to the inner radius, not just reusing it unchanged -
// otherwise the outer ring's corner cuts in tighter than the inner ring's, instead of sweeping
// smoothly around it at a constant width. The gap between the two rings' own insets is
// NEO_CELL_RECOMMENDED_PADDING_MM (the conforming cell frame's own inset) minus
// NEO_HEAD_FRAME_INSET_MM (the head frame's own, smaller inset).
#define NEO_HEAD_FRAME_RADIUS_MM (NEO_ROW_HEADER_RIGHT_RADIUS_MM + (NEO_CELL_RECOMMENDED_PADDING_MM - NEO_HEAD_FRAME_INSET_MM))
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

// Computed minimum resizable width, in HP - just controlsWidthMm (including Full Height's own
// reserved resize-handle strip AND the right-hand trailing margin, both folded into
// neoRowAreaControlsWidthMm() itself), rounded UP to the next whole HP (Rack's own resize-snap
// grain - RACK_GRID_WIDTH is a whole HP, 15px/5.08mm; Rack has no half-HP resize granularity). No
// column term at all (2026-07-22, Dieter's own KISS simplification - see NEO_DEFAULT_WIDTH_HP's
// own comment for why zero required columns at the floor is correct now). This is THE one shared
// formula every minimum-width caller uses - the interactive drag clamp (NeoResizeHandle::
// onDragMove()), the Lock group's own width convergence, and NEO_DEFAULT_WIDTH_HP's own hand
// computation (Neo.hpp, kept numerically in sync with this function by hand) - so fixing it here
// once covers all of them, rather than needing the same margin re-applied at every call site. A
// plain function (not a #define) since it depends on RACK_GRID_WIDTH/mm2px().
inline float neoMinWidthHp(float controlsWidthMm)
{
	float minWidthPx = mm2px(Vec(controlsWidthMm, 0.f)).x;
	return std::ceil(minWidthPx / RACK_GRID_WIDTH);
}

// Computed maximum resizable width, in HP - controls width + enough columns to show the
// connected Host's own STRUCTURAL step-count ceiling (getMaxLoopLen(), e.g. Morpheus's fixed 128),
// simplified 2026-07-22 (Dieter's own call: "the user should decide, if columns are unused it's
// his decision") - this deliberately REVERSES an earlier 2026-07-20 catch that capped at the live
// per-channel getLoopLen() max instead (how much of that 128 a channel's content actually
// currently uses), specifically to stop NEO from ever showing a wider grid than any channel could
// possibly use. That reasoning no longer applies: the max is now purely the Host's own fixed
// capacity, and a user resizing wider than their current content need is a valid, deliberate
// choice, not something the module should second-guess or prevent - some trailing columns simply
// showing nothing yet (beyond the current LEN) is fine.
//
// Rounded UP to the next whole HP (Dieter's own catch, 2026-07-20, live-testing: "LEN 8 results in
// max size allowing for 7 columns") - the exact mm needed for maxSteps columns almost never lands
// on a whole-HP grid line, and rounding DOWN (the original, wrong assumption here - "so the snap
// can never overshoot") can land the clamp a hair short of the width maxSteps columns actually
// need, which getVisibleColumns()'s own floor() then reads as one fewer column than the Host can
// really show. ceil() can only ever grant a hair MORE room than exactly maxSteps*columnWidthMm -
// less than one further column's worth by construction, so it can never manufacture a column
// that isn't real either, matching the same reasoning neoColumnFit()'s own epsilon uses.
// Returns a generously large (effectively unbounded) fallback while disconnected - nothing to
// size against yet, and the resize handle already re-clamps continuously as the user drags, so a
// too-large value here is harmless (never actually reachable once a real Host is attached and its
// own true ceiling takes over on the very next tick).
inline float neoMaxWidthHp(NeoHostInterface *host, float columnWidthMm, float controlsWidthMm)
{
	int maxSteps = host ? host->getMaxLoopLen() : 100000;
	float maxWidthMm = controlsWidthMm + (float) maxSteps * columnWidthMm;
	float maxWidthPx = mm2px(Vec(maxWidthMm, 0.f)).x;
	return std::ceil(maxWidthPx / RACK_GRID_WIDTH);
}

/*
	THE single formula for where every row sits and how tall its own cells are - both
	NeoWidget::step() (positions the actual row widgets) and NeoPanelWidget::draw() (needs to
	know where the row-area's own usable content starts/ends) call this, so the two can never
	disagree about the layout.

	KISS-simplified 2026-07-22 (Dieter's own instruction, directly following the horizontal
	inter-cell-padding removal the same day - see neoColumnPitchMm()'s own comment): NEO no longer
	reserves ANY padding between rows either - no gap at the very top, none between consecutive
	rows, none at the very bottom, in EITHER mode. Rows now pack perfectly edge-to-edge vertically,
	exactly mirroring the horizontal removal - "the cell is now responsible to draw its visual
	appearance (frame) accordingly to look good," same NEO_CELL_RECOMMENDED_PADDING_MM convention
	every NeoCellEditor's own drawCell() already applies on all four sides, not just left/right.
	This replaces the entire previous two-mode padding scheme (Normal: N+1 full NEO_FRAME_GAP_MM
	units; Full Height: a half-edge convention at top/bottom so two stacked instances' half-gaps
	summed to one ordinary inter-row gap at the seam) - both modes now use the exact same trivial
	formula, and the seam-stacking guarantee still holds trivially, since a half of a now-zero gap
	is still zero, exactly matching the (also now zero) gap between any two ordinary rows.
	Normal mode still uses the fixed band between NEO_FRAME_TOP_MM and (PANELHEIGHT -
	NEO_FRAME_BOTTOM_MM) - that's the panel's own OUTER frame margin, a completely different,
	untouched concept from the inter-row padding removed here; Full Height still uses the full
	0..PANELHEIGHT (no frame margin at all), now truly edge-to-edge with zero inset from either.
*/
inline void neoRowLayout(bool fullHeight, int rowsDisplayed, float &outFirstRowYMm, float &outCellHeightMm, float &outRowPitchMm)
{
	float contentTopMm = fullHeight ? 0.f : NEO_FRAME_TOP_MM;
	float contentBottomMm = fullHeight ? PANELHEIGHT : (PANELHEIGHT - NEO_FRAME_BOTTOM_MM);
	float rangeMm = contentBottomMm - contentTopMm;
	outCellHeightMm = std::max(0.1f, rangeMm / (float) rowsDisplayed);
	outRowPitchMm = outCellHeightMm;
	outFirstRowYMm = contentTopMm;
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

// The horizontal spacing between column STARTS - the actual footprint one column occupies.
// KISS-simplified 2026-07-22 (Dieter's own instruction, after the earlier "horizontal padding
// must equal vertical row padding" convention - a NEO-reserved gap between every pair of
// cells - turned out to be exactly the wrong shape once a future SOURCE-driven, variable-
// resolution, possibly-continuous (wave/sample editor) row was considered): NEO no longer
// reserves ANY inter-cell gap of its own at all - columns are now packed edge-to-edge,
// columnPitchMm simply equals columnWidthMm. Whether (and how much) visual breathing room shows
// between two cells is now entirely each NeoCellEditor's own choice, made INSIDE its own
// drawCell() - see NEO_CELL_RECOMMENDED_PADDING_MM's own comment for the suggested convention
// (not a requirement) most existing editors follow. Vertical row-to-row padding (neoRowLayout()'s
// own NEO_FRAME_GAP_MM-based spacing) is completely unaffected - this only ever changes the
// horizontal, cell-to-cell spacing within one row. Every width-BUDGET computation (how many
// columns fit, min/max resizable width, the auto-resize reconciliation) must use this, not
// neoColumnWidthMm() directly - they happen to be numerically identical now, but call sites
// should still go through their own semantically-correct accessor rather than assuming that stays
// true forever.
inline float neoColumnPitchMm(bool fullHeight, int rowsDisplayed)
{
	return neoColumnWidthMm(fullHeight, rowsDisplayed);
}

// NEO_CELL_RECOMMENDED_PADDING_MM itself is defined early, right after NEO_FRAME_GAP_MM above
// (needed by neoRowAreaControlsWidthMm(), which comes before this point in the file) - this is
// the fuller rationale for what it means and why it exists. It's the recommended (NOT enforced)
// inset a NeoCellEditor's own drawCell() leaves free on each side, now that NEO itself reserves
// zero inter-cell gap of its own. Matters MOST for an editor that does NOT override
// NeoCellEditor::drawHeadFrame() and relies on its default body - that default draws a small
// stroke frame inset by NEO_HEAD_FRAME_INSET_MM from the cell's own edges, which needs this same
// margin free of the editor's own content to read clearly and stay visually separated from
// whatever the NEXT cell draws, now that there's no NEO-reserved gap between them to fall back
// on. An editor that fills its own cell edge-to-edge on purpose (e.g. a future continuous/
// waveform-style row editor) is free to ignore this entirely - it just needs to override
// drawHeadFrame() itself too in that case, since the default frame assumes this padding is being
// honored.

// Worst-case minimum width across BOTH Full Height and Normal mode - switching modes must never
// require more width than the module already has (only an actual drag may ever change
// PANEL_WIDTH_HP_JSON, see NEO_DEFAULT_WIDTH_HP's own comment), so the floor has to cover
// whichever mode's own controlsWidthMm is larger. Full Height needs more room than Normal at the
// same header width for one reason now (2026-07-22: the column-pitch difference between modes no
// longer matters here, since there's no column-count term left to multiply it against) - its own
// resize-handle strip (NEO_RESIZE_RESERVED_WIDTH_MM) that Normal doesn't reserve at all. Every
// MINIMUM-width floor (NEO_DEFAULT_WIDTH_HP's own hand derivation, the interactive drag clamp, the
// Lock group's own width-convergence clamp) must use this, never the single-mode neoMinWidthHp(),
// so the module can never end up too narrow for whichever mode needs more room. (neoMaxWidthHp()
// deliberately stays single-mode/whichever mode is currently active - a maximum-width ceiling
// shrinking slightly across a mode change isn't the same problem: it only ever caps how much
// FURTHER the module could usefully grow, never forces it narrower than content already needs.)
inline float neoMinWidthHpAnyMode(float rowHeaderWidthMm)
{
	float normalControls = neoRowAreaControlsWidthMm(false, rowHeaderWidthMm);
	float fullHeightControls = neoRowAreaControlsWidthMm(true, rowHeaderWidthMm);
	return std::max(neoMinWidthHp(normalControls), neoMinWidthHp(fullHeightControls));
}

// THE one shared column-fit computation - given the panel's current total width, how much of it
// is already spent on controls (neoRowAreaControlsWidthMm()), and the current column pitch,
// returns how many WHOLE columns fit in whatever's left over and exactly how much fractional
// space remains beyond those whole columns. EVERY call site that needs either number must go
// through this one function - Neo::getVisibleColumns() (live column count, used by
// NeoRowCellsWidget's own draw()/click handling), neoComputeLayout() below (the pure header-width/
// column-fit computation every settling step goes through), and the row-cell widget's own box
// sizing (NeoWidget::step()).
// Before this (2026-07-20), these were three separately hand-written reimplementations of the
// same arithmetic that could silently drift apart from each other (Dieter's own catch, live-
// testing: dragging showed 5 columns, but the drag-end code's own separate calculation decided
// only 4 actually fit for that same settled width - "different thinking about space... ugly and
// has to be resolved... by better coding," not by hunting down one more one-off numeric
// mismatch). Consolidating here is that fix: there is now exactly one place this arithmetic is
// written, so every caller sees the identical answer for the identical input, by construction.
inline void neoColumnFit(float widthMm, float controlsWidthMm, float columnPitchMm, int &outVisibleCols, float &outLeftoverMm)
{
	// Floored at 0, not 1 (2026-07-22, Dieter's own KISS simplification - NEO no longer
	// guarantees any minimum visible column count, see NEO_DEFAULT_WIDTH_HP's own comment). A
	// width narrower than controlsWidthMm itself should never actually happen in practice
	// (neoMinWidthHp() is the floor everywhere a module's own width gets clamped), but flooring at
	// 0 rather than letting this go negative is still cheap, harmless insurance regardless.
	float cellsWidthMm = std::max(0.f, widthMm - controlsWidthMm);
	// Tiny epsilon before flooring (Dieter's own catch, 2026-07-20, live-testing: "still
	// dropping a column when releasing," even after the algorithm itself was fixed) - once
	// neoComputeLayout() settles cellsWidthMm to EXACTLY visibleCols*columnPitchMm
	// (by construction, see its own comment), re-deriving that same cellsWidthMm one frame later
	// via a fresh widthMm-controlsWidthMm subtraction is not guaranteed to reproduce the exact
	// same float bit pattern - it can land a hair BELOW the true boundary, and a bare floor()
	// misreads that hair as one fewer column even though nothing about the actual layout
	// changed. 0.001mm is far larger than any realistic float rounding error at these
	// magnitudes (a few hundred mm) but far smaller than any real leftover, so it can only ever
	// correct a false boundary-crossing, never manufacture a column that doesn't really fit.
	const float NEO_COLUMN_FIT_EPSILON_MM = 0.001f;
	outVisibleCols = std::max(0, (int) ((cellsWidthMm + NEO_COLUMN_FIT_EPSILON_MM) / columnPitchMm));
	outLeftoverMm = cellsWidthMm - (float) outVisibleCols * columnPitchMm;
}

// NEO_HEADER_LIVE_REFLOW (a flat "recompute everything live" vs. "freeze everything during drag"
// toggle) is gone (2026-07-22) - neither extreme was actually right. The real source of jitter
// was never the column count changing, it was the HEADER's own width shifting mid-drag (moving
// every column's x-position with it) - so NeoWidget::step() now hybridizes the two: the header
// stays frozen for the whole drag (no leftover-absorption "beautifying" until the drag actually
// ends), while the visible column count still updates live throughout, computed fresh each tick
// against that same frozen header. See its own comment for the exact split.

// THE single pure function that turns "current total module width" into every derived layout
// quantity - header width, controls width, visible column count, leftover space, cell-grid width.
// Replaces the older incrementally-grown, persisted row-header-width model (Neo::
// getRowHeaderWidthMm()/absorbLeftoverIntoHeader(), removed 2026-07-21): under that model the
// header snapped to a "target width" (Tw) at drag-start and grew past it at drag-end, requiring a
// stored per-instance width (ROW_HEADER_WIDTH_MM_JSON) that had to stay correctly synchronized
// across locked instances - the source of a whole series of lock-sync regressions this session
// (a follower's own blocked width corrupting the shared group target, a "stuck ceiling" after a
// block, columns lost across a Full Height toggle). Dieter's own redesign, confirmed step by
// step: any whole-HP module width is valid, and the header is simply NEO_ROW_HEADER_MIN_WIDTH_MM
// plus whatever leftover space neoColumnFit() finds once the widest possible whole-column count
// is subtracted - nothing persisted, nothing to keep in sync, recomputed fresh from
// (totalWidthMm, fullHeight, rowsDisplayed) alone every time it's needed.
struct NeoLayoutResult
{
	float headerWidthMm;
	float controlsWidthMm; // globalArea + headerWidthMm + rightPadding + (Full Height's) reserved strip
	int visibleCols;
	float leftoverMm;
	float cellsWidthMm;    // visibleCols * columnPitchMm
};

inline NeoLayoutResult neoComputeLayout(float totalWidthMm, bool fullHeight, int rowsDisplayed)
{
	float columnPitchMm = neoColumnPitchMm(fullHeight, rowsDisplayed);
	float controlsWidthAtMinMm = neoRowAreaControlsWidthMm(fullHeight, NEO_ROW_HEADER_MIN_WIDTH_MM);
	int visibleCols;
	float leftoverMm;
	neoColumnFit(totalWidthMm, controlsWidthAtMinMm, columnPitchMm, visibleCols, leftoverMm);
	NeoLayoutResult r;
	r.headerWidthMm = NEO_ROW_HEADER_MIN_WIDTH_MM + leftoverMm;
	r.controlsWidthMm = controlsWidthAtMinMm + leftoverMm;
	r.visibleCols = visibleCols;
	r.leftoverMm = leftoverMm;
	r.cellsWidthMm = (float) visibleCols * columnPitchMm;
	return r;
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
	// See neoRowLayout()'s own comment - both modes pack rows with zero inter-row padding at all
	// now (2026-07-22 KISS simplification), so the seam between two stacked instances is
	// automatically as tight as the gap between any two ordinary rows, with no special-case
	// halving needed to keep the two consistent.
	FULL_HEIGHT_JSON,

	// Lock - this instance's own membership in the Host-shared "common config" group (2026-07-20
	// design). Right-click-free, a real panel widget (NeoLockButton) in the global area. See
	// Neo.cpp's own NeoLockData/readLockData()/writeLockData() and NeoWidget::step()'s lock-sync
	// block for the full mechanism and its own schema documentation.
	LOCKED_JSON,

	// UNUSED as of the 2026-07-21 resize redesign - the row header's width is no longer persisted
	// at all, it's recomputed fresh every time via neoComputeLayout() (Neo.hpp). Slot kept (not
	// renumbered) so nothing downstream shifts - moduleInitJsonConfig() (Neo.cpp) still gives it
	// a real jsonLabel even though OL_state here is never written: dataToJson() indexes
	// OL_jsonLabel[] unconditionally for every slot with no null-check, so an UNLABELED slot is
	// live, uninitialized memory, not a safe no-op - confirmed the hard way, 2026-07-21 (crashed
	// Rack on module-browser creation the one time this slot was left unlabeled). See
	// moduleInitJsonConfig()'s own comment for the full explanation.
	ROW_HEADER_WIDTH_MM_JSON,

	// The non-adjacent stay-connected target host id used to live here as a float
	// (CONNECTED_HOST_ID_JSON) - that assumption ("Rack module ids are small sequential
	// integers") turned out to be wrong in practice (observed ids run into the quadrillions,
	// which silently corrupts when stored in a float) - moved to a real int64_t member
	// (neoConnectedHostId, Neo.cpp), persisted via moduleExtraDataToJson/FromJson instead.

	// Which channel/track each row currently displays used to live here as OL_state
	// (ROW_CHANNEL_JSON) - moved 2026-07-20 to the new ROW_TRACK_PARAM/ROW_CHANNEL_PARAM real
	// knobs (ParamIds below) once they stopped being a right-click-menu-only choice and became
	// on-panel controls, for the same reason ROW_MEMTAPE_JSON did (see its own note below) - a
	// real Param is Rack-natively persisted, so a separate JSON mirror would just be a second,
	// possibly-disagreeing source of truth for the exact same value.

	// Channel name/color used to live here as per-channel, per-instance state (CHANNEL_COLOR_JSON)
	// - moved 2026-07-20 to genuinely channel-owned, Host-shared storage instead (Neo.cpp's own
	// channelName[]/channelColor[] + refreshChannelTable(), stored on Morpheus itself via the
	// generic ExpanderBridgeInterface::writeExpanderData()/readExpanderData() every Host offers -
	// see ExpanderBridge.hpp). A channel's identity shouldn't depend on which NEO instance/row is
	// currently looking at it, and every attached NEO instance now automatically agrees.

	// Per-row MEM/TAPE toggle used to live here (ROW_MEMTAPE_JSON) - removed 2026-07-20, replaced
	// entirely by the new ROW_TRACK_PARAM knob (TAPE/MSEL are now just two of its own choices).
	// FOLLOW toggle (0 = off/manual page, 1 = on/auto-follow play cursor) - real button Param
	// below, this mirrors its current value (a momentary button's own raw param value is
	// meaningless on its own, unlike a knob's).
	ROW_FOLLOW_JSON,
	ROW_FOLLOW_JSON_LAST = ROW_FOLLOW_JSON + NEO_NUM_ROWS - 1,

	// Per-row manual page (only meaningful while that row's own FOLLOW is off) - which page of
	// this row's own step content is currently visible.
	ROW_PAGE_JSON,
	ROW_PAGE_JSON_LAST = ROW_PAGE_JSON + NEO_NUM_ROWS - 1,

	// UNUSED as of the 2026-07-22 on-panel celltype knob (ROW_CELLTYPE_PARAM, ParamIds below) -
	// same "slot kept, not renumbered, still needs a real jsonLabel" precedent as
	// ROW_HEADER_WIDTH_MM_JSON above (an unlabeled slot is live uninitialized memory, not a safe
	// no-op - moduleInitJsonConfig() still calls setJsonLabel() for every row here). Used to be the
	// right-click-menu-only selector (0 = Gate, 1 = Value, 2 = Morpheus) before the real knob
	// replaced it entirely, same reasoning as ROW_TRACK_PARAM/ROW_CHANNEL_PARAM superseding their
	// own predecessor JSON fields.
	ROW_CELLTYPE_JSON,
	ROW_CELLTYPE_JSON_LAST = ROW_CELLTYPE_JSON + NEO_NUM_ROWS - 1,

	// Global FOLLOW (2026-07-21) - one toggle for every row at once, replacing the deferred
	// per-row ROW_FOLLOW_JSON/PARAM above (left in place, unused, rather than renumbering every
	// Param after it - see Neo.cpp's own moduleProcess() for where this is actually read).
	GLOBAL_FOLLOW_JSON,

	// Row-header coloring mode (2026-07-22) - a plain index into Neo.cpp's own
	// NEO_ROW_COLOR_MODES list, module-wide (not per-row, like GLOBAL_FOLLOW_JSON above), cycled
	// by a CLICK (not drag) on any row's own color dot (NeoRowColorDotWidget) - replaces the
	// earlier compile-time-only NEO_ROW_COLOR_FRAME/NEO_ROW_COLOR_TEXT #defines with a real,
	// persisted, user-facing choice.
	ROW_COLOR_MODE_JSON,

	NUM_JSONS
};

//
// Parameter Ids - per-row toggle buttons, plus (2026-07-20) the track/channel select knobs.
// The step cells themselves are not Params (they write directly through NeoHostInterface to
// Morpheus, not to Neo's own state). ROW_MEMTAPE_PARAM is gone (Dieter's own instruction,
// 2026-07-20: the new ROW_TRACK_PARAM knob fully replaces it - TAPE/MSEL are now just two of a
// track knob's own choices, so a separate toggle would be redundant and could disagree).
// ROW_TRACK_PARAM/ROW_CHANNEL_PARAM are real, Rack-native discrete knobs (matching MidiBus's own
// RX/TX_CHANNEL_PARAM precedent) rather than virtual OL_state - real Params are persisted by
// Rack's own base Module::toJson()/fromJson() independent of OrangeLineCommon's dataToJson()/
// dataFromJson() override (which only ever handles the OL_state/jsonLabel "data" blob), so no
// extra JSON mirror is needed for them to survive a save/reload, unlike the momentary buttons
// below (whose own raw param value is meaningless - only the OL_state toggle result they drive
// needs to persist).
//
enum ParamIds {
	ROW_TRACK_PARAM,   // which of the Host's own tracks (TAPE/MSEL/M-01..M-16 for Morpheus)
	ROW_TRACK_PARAM_LAST = ROW_TRACK_PARAM + NEO_NUM_ROWS - 1,
	ROW_CHANNEL_PARAM, // which of the Host's own channels (1..16 for Morpheus)
	ROW_CHANNEL_PARAM_LAST = ROW_CHANNEL_PARAM + NEO_NUM_ROWS - 1,
	// Per-row cell editor selection (2026-07-22) - a real on-panel knob replacing the old
	// right-click-only "Rows -> Row N -> Cell Type" submenu entirely (same reasoning as
	// ROW_TRACK_PARAM/ROW_CHANNEL_PARAM replacing their own predecessors: a separate menu for the
	// exact same setting would be redundant and could disagree with the knob). Range is
	// neoCellEditorRegistry()'s own current size, not a fixed ceiling like NEO_MAX_TRACKS - the
	// registry only ever grows by adding a new NeoCellEditor subclass in source, never at runtime,
	// so there's no "beyond what's currently registered" case to guard against the way a Host's
	// live track count needs guarding for ROW_TRACK_PARAM.
	ROW_CELLTYPE_PARAM,
	ROW_CELLTYPE_PARAM_LAST = ROW_CELLTYPE_PARAM + NEO_NUM_ROWS - 1,
	ROW_FOLLOW_PARAM,
	ROW_FOLLOW_PARAM_LAST = ROW_FOLLOW_PARAM + NEO_NUM_ROWS - 1,
	ROW_LEFT_PARAM,  // manual page-back, active only while that row's FOLLOW is off
	ROW_LEFT_PARAM_LAST = ROW_LEFT_PARAM + NEO_NUM_ROWS - 1,
	ROW_RIGHT_PARAM, // manual page-forward, same condition
	ROW_RIGHT_PARAM_LAST = ROW_RIGHT_PARAM + NEO_NUM_ROWS - 1,

	// Multi-value cell editor infrastructure (2026-07-22) - generic per-ROW real Params, NOT
	// per-cell-type, same reasoning ROW_CELLTYPE_PARAM itself already established: only one cell
	// type is ever active on a row at once, so a per-cell-type copy of these would be redundant
	// and could disagree. Real Params -> no jsonIds mirror needed (same precedent as
	// ROW_TRACK_PARAM/ROW_CHANNEL_PARAM/ROW_CELLTYPE_PARAM - Rack's own base Module persistence
	// already covers these). Not synced across a LOCK group either - confirmed by checking
	// Neo::NeoLockData: ROW_TRACK_PARAM/ROW_CHANNEL_PARAM/ROW_CELLTYPE_PARAM aren't lock-synced
	// today, so these per-row settings shouldn't be either, for consistency.
	ROW_RANGE_MIN_PARAM,     // user-configured effective-range floor (NeoValueCellEditorBase subclasses only)
	ROW_RANGE_MIN_PARAM_LAST = ROW_RANGE_MIN_PARAM + NEO_NUM_ROWS - 1,
	ROW_RANGE_MAX_PARAM,     // user-configured effective-range ceiling, same scope as above
	ROW_RANGE_MAX_PARAM_LAST = ROW_RANGE_MAX_PARAM + NEO_NUM_ROWS - 1,
	ROW_SECONDARY_TRACK_PARAM,   // a compound editor's own second (track,channel) binding - track half
	ROW_SECONDARY_TRACK_PARAM_LAST = ROW_SECONDARY_TRACK_PARAM + NEO_NUM_ROWS - 1,
	ROW_SECONDARY_CHANNEL_PARAM, // ...channel half
	ROW_SECONDARY_CHANNEL_PARAM_LAST = ROW_SECONDARY_CHANNEL_PARAM + NEO_NUM_ROWS - 1,

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
