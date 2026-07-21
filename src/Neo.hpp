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
// default row config (NEO_ROWS_DEFAULT rows) - so a freshly-placed module always starts already
// showing at least that many columns, and can never be dragged narrower than that either
// (NeoResizeHandle::onDragMove()'s own neoMinWidthHpAnyMode() clamp, which stays dynamic for
// OTHER row configs the user might switch to later - this constant is only about the fixed
// default/initial state). Calculated once by hand rather than at runtime - whatever whole-HP
// width doesn't divide the target evenly just becomes leftover that gets absorbed into the
// header at drag-end (Neo::absorbLeftoverIntoHeader()), so rounding UP here can only ever grow
// the header past Tw, never shrink it below - the "header width after settling >=
// targetHeaderWidth" guarantee holds by construction, not by a runtime check.
//
// MUST be the max across BOTH Normal and Full Height (Dieter's own catch, 2026-07-21: switching
// modes must never change the visible column count, so the floor has to cover whichever mode
// needs more room, not just whichever one this constant happened to be derived from) - an
// earlier version of this derivation only computed the Normal branch, and separately used a
// stale, pre-neoRowLayout()-redesign approximation for columnPitchMm (plain rangeMm/rows, instead
// of the real N+1-gap-unit formula) that happened to still round up to the same HP by luck rather
// than by being correct - both mistakes are fixed below.
//   Normal:      rightPaddingMm = NEO_FRAME_GAP_MM/2 + NEO_FRAME_GAP_MM = 1.5 * 1.524 = 2.286
//                controlsWidthMm = NEO_GLOBAL_AREA_WIDTH_MM (30.48) + NEO_ROW_HEADER_TARGET_WIDTH_MM
//                                 (162.56) + rightPaddingMm (2.286) = 195.326
//                columnPitchMm at 8 rows (neoRowLayout()'s own N+1-gap-unit formula):
//                                 cellHeightMm = ((128.5 - 2*7.620) - 9*1.524) / 8 = 12.4430
//                                 columnPitchMm = 12.4430 + 1.524 = 13.9670
//                minWidthMm = 195.326 + 4 * 13.9670 = 251.194
//                minWidthHp = ceil(251.194 / 5.08) = 50
//   Full Height: rightPaddingMm = NEO_FRAME_GAP_MM/2 + 0 = 0.762
//                controlsWidthMm = 30.48 + 162.56 + NEO_RESIZE_RESERVED_WIDTH_MM (5.08)
//                                 + rightPaddingMm (0.762) = 198.882
//                columnPitchMm at 8 rows (half-edge convention, N gap-units total):
//                                 columnPitchMm = 128.5 / 8 = 16.0625
//                minWidthMm = 198.882 + 4 * 16.0625 = 263.132
//                minWidthHp = ceil(263.132 / 5.08) = 52
//   NEO_DEFAULT_WIDTH_HP = max(50, 52) = 52 (see neoMinWidthHpAnyMode() below - this hand
//                          derivation must always match what that function would compute for this
//                          same config, so the two never silently diverge)
// Recompute by hand and update this constant if NEO_ROW_HEADER_TARGET_WIDTH_HP,
// NEO_GLOBAL_AREA_WIDTH_HP, NEO_MIN_VISIBLE_COLS, NEO_ROWS_DEFAULT, NEO_RESIZE_RESERVED_WIDTH_HP,
// neoRowAreaControlsWidthMm()'s own right-padding formula, or the frame-margin constants above
// ever change. No extra fudge-factor HP on top of this raw value anymore (removed again
// 2026-07-20, same day it was added - Dieter's own catch, live-testing: "still too wide" - it was
// double-counting margin that neoRowAreaControlsWidthMm()'s own right-padding term already
// supplies for real now).
#define NEO_DEFAULT_WIDTH_HP 52

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
// This fits comfortably inside the row header's own fixed Tw envelope (NEO_ROW_HEADER_TARGET_
// WIDTH_MM, 162.56mm) with room to spare before the right-aligned position display. The field is a
// FIXED width, same as every other row display (Dieter's own reversal, 2026-07-21, of an earlier
// "grow to fill leftover header width" idea - that made padding/spacing after the field
// inconsistent whenever the header itself had grown past its own default target; only the
// right-aligned position display still tracks the header's own current width now). NEO_DEFAULT_
// WIDTH_HP/neoMinWidthHp()/neoMaxWidthHp() are derived from NEO_ROW_HEADER_TARGET_WIDTH_MM as one
// opaque envelope value, not from this field's own internal breakdown, so none of them need
// recomputing for this change.
#define NEO_ROW_NAME_DOT_DIAMETER_MM  (NEO_ROW_NUMBER_DISPLAY_HEIGHT_MM / 2.f)
#define NEO_ROW_NAME_DOT_GAP_MM       NEO_FRAME_GAP_MM
// mm of vertical drag per color step (NeoRowColorDotWidget's own click-drag cycling) - first-pass
// value, live-tune once built.
#define NEO_ROW_NAME_DOT_DRAG_STEP_MM 4.f
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
// NEO_ROW_NAME_WIDTH_MM/FOLLOW_X_MM/LEFT_X_MM/RIGHT_X_MM used to be static #defines here, chained
// off the field's own BASE width - removed 2026-07-21 (Dieter's own catch: FOLLOW/LEFT/RIGHT kept
// overlapping the name field) because the field's own ACTUAL width grows past this base value at
// runtime whenever the header itself has grown past NEO_ROW_HEADER_TARGET_WIDTH_MM (leftover-
// absorption, see nameFieldWidthMm's own comment in NeoWidget::step(), Neo.cpp) - a static macro
// structurally can't know about that runtime growth, so it silently drifted out of sync with the
// field's real edge. LEFT/RIGHT's own x-positions are now computed directly in NeoWidget::step()
// from the field's own real current right edge every frame instead - see that function for the
// actual math.
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
// connected Host's own longest CURRENTLY CONFIGURED channel content all at once, since no
// visible column beyond that could ever show anything real regardless of how much wider NEO
// gets. Deliberately the live per-channel getLoopLen() max across every one of the Host's
// POLY_CHANNELS channels, not the fixed structural getMaxLoopLen() ceiling (Dieter's own catch,
// 2026-07-20 - "should not extend the maximum size of a channel," meaning the actual live LEN of
// all 16 channels, not the theoretical 128-step capacity) - ALL channels, not just the ones this
// particular NEO instance happens to have its own rows currently showing, since any row could be
// repointed to any channel at any time via its own channel selector. Rounded UP to the next
// whole HP (Dieter's own catch, 2026-07-20, live-testing: "LEN 8 results in max size allowing
// for 7 columns") - the exact mm needed for maxSteps columns almost never lands on a whole-HP
// grid line, and rounding DOWN (the original, wrong assumption here - "so the snap can never
// overshoot") can land the clamp a hair short of the width maxSteps columns actually need,
// which getVisibleColumns()'s own floor() then reads as one fewer column than the Host can
// really show. ceil() can only ever grant a hair MORE room than exactly maxSteps*columnWidthMm -
// less than one further column's worth by construction, so it can never manufacture a column
// that isn't real either, matching the same reasoning neoColumnFit()'s own epsilon uses.
// Returns a generously large (effectively unbounded) fallback while disconnected - nothing to
// size against yet, and the resize handle already re-clamps continuously as the user drags, so a
// too-large value here is harmless (never actually reachable once a real Host is attached and its
// own true ceiling takes over on the very next tick).
inline float neoMaxWidthHp(NeoHostInterface *host, float columnWidthMm, float controlsWidthMm)
{
	int maxSteps = 100000;
	if (host)
	{
		maxSteps = 1;
		for (int c = 0; c < POLY_CHANNELS; c++)
			maxSteps = std::max(maxSteps, host->getLoopLen(c));
	}
	float maxWidthMm = controlsWidthMm + (float) maxSteps * columnWidthMm;
	float maxWidthPx = mm2px(Vec(maxWidthMm, 0.f)).x;
	return std::ceil(maxWidthPx / RACK_GRID_WIDTH);
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

// Worst-case minimum width across BOTH Full Height and Normal mode (2026-07-21, Dieter's own
// catch: switching modes must never change the visible column count, so the module's own
// minimum/default width has to guarantee NEO_MIN_VISIBLE_COLS columns in whichever mode needs
// MORE room, not just whichever mode happens to be active right now). Full Height needs more
// room than Normal at the same row count/header width for two independent reasons: its own
// resize-handle strip (NEO_RESIZE_RESERVED_WIDTH_MM) that Normal doesn't reserve at all, AND its
// own row-padding rule making each column's own pitch wider than Normal's N+1-gap-unit rule does
// (see neoRowLayout()'s own comment). Every MINIMUM-width floor (NEO_DEFAULT_WIDTH_HP's own hand
// derivation, the interactive drag clamp, the Lock group's own width-convergence clamp) must use
// this, never the single-mode neoMinWidthHp(), so the module can never end up too narrow for 4
// columns the instant Full Height is toggled. (neoMaxWidthHp() deliberately stays single-mode/
// whichever mode is currently active - a maximum-width ceiling shrinking slightly across a mode
// change isn't the same problem: it only ever caps how much FURTHER the module could usefully
// grow, never forces it narrower than content already needs.)
inline float neoMinWidthHpAnyMode(int rowsDisplayed, float rowHeaderWidthMm)
{
	float normalPitch = neoColumnPitchMm(false, rowsDisplayed);
	float normalControls = neoRowAreaControlsWidthMm(false, rowHeaderWidthMm);
	float fullHeightPitch = neoColumnPitchMm(true, rowsDisplayed);
	float fullHeightControls = neoRowAreaControlsWidthMm(true, rowHeaderWidthMm);
	return std::max(neoMinWidthHp(normalPitch, normalControls), neoMinWidthHp(fullHeightPitch, fullHeightControls));
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

	// Per-row cell display/editor type (0 = Gate, 1 = Value in v1 - Pitch/Curve are follow-up
	// work, see the plan) - user-configurable via right-click, not auto-detected from Morpheus's
	// own data (a step is just a raw float either way, see NeoShared.hpp's own comment).
	ROW_CELLTYPE_JSON,
	ROW_CELLTYPE_JSON_LAST = ROW_CELLTYPE_JSON + NEO_NUM_ROWS - 1,

	// Global FOLLOW (2026-07-21) - one toggle for every row at once, replacing the deferred
	// per-row ROW_FOLLOW_JSON/PARAM above (left in place, unused, rather than renumbering every
	// Param after it - see Neo.cpp's own moduleProcess() for where this is actually read).
	GLOBAL_FOLLOW_JSON,

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
