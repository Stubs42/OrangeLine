# OrangeLine Panel Design Guide

Conventions for authoring new panel SVGs (`res/<Name>Work.svg` + the exported
`<Name>Orange/Bright/Dark.svg`). Companion to `Colors.txt` (theme colors) and `CLAUDE.md`
(code/module architecture) - this file is specifically about panel *dimensions and structure*.

**Base template for slim (3HP, single-column-of-controls) modules**: `res/JWork.svg` (built
2026-07-15 while working through every convention in this guide for the first time) is the
reference to start a new slim module from - background/frame/accent stripe/header+footer text
already correctly positioned, plus (if the bottom-most socket is an output, which it usually
is) the output-marking box already placed per the rules below. Copy it rather than starting a
new slim panel from scratch.

## Workflow: building a new panel step by step

This is the actual sequence followed building `res/JWork.svg` from an empty file - repeat it
for the next new module rather than re-deriving the approach from scratch. Each step links to
the section with the full rule/rationale.

1. **Pick dimensions**: height is always 128.5mm; width is `N x 5.08mm` for whatever HP count
   the module needs. See *Panel dimensions*.
2. **Set the Inkscape document grid** to the fixed spacing/origin values. See *Inkscape
   document grid* - and keep its "grid alignment wins over exact spacing" rule in mind for
   every step from here on.
3. **Create the 11-layer skeleton** (empty layers, correct names/order/visibility - `Controls`
   and `TextWork` visible, one theme's Background+Panel visible, everything else `display:none`
   - see *SVG layer structure*). If a same-width panel already exists, its layer skeleton can be
   copied directly instead of retyped.
4. **Backgrounds**: one plain rect-as-`<path>` per theme (`M 0,0 H <w> V <h> H 0 Z`), filled
   with that theme's `Colors.txt` Background color. One per `Background<Theme>` layer.
5. **Frame**: the rounded content-frame path (fixed margins/corner-radius, theme Frame color) in
   each `Panel<Theme>` layer. See *Content frame*.
6. **Accent stripe**: the "ORANGE LINE" brand line, same geometry/color in every `Panel<Theme>`
   layer regardless of theme. See *"ORANGE LINE" brand accent stripe*.
7. **Header/footer brand text** in `TextWork`: the module-name/logo title (placement rule in
   *Title/header text*) plus the "ORANGE LINE" wordmark, which can usually be copied verbatim
   from any other panel of the *same width* (identical x/y/font-size, confirmed on CC14 -> J).
8. **Place controls** (jacks, knobs, buttons) as guide copies of real component art in
   `Controls` (never rendered at runtime - see *SVG layer structure*'s note on that layer).
   Reuse an existing jack/knob guide group as a template and reposition via `translate()` - see
   *Positioning controls* for the corner-diagonal/single-column-formula/repeat-pitch rules that
   determine where each one goes.
9. **Output-marking box** behind any output jack(s), in every `Panel<Theme>` layer. See
   *Output-marking box*.
10. **Ring around parameter controls** (knobs/buttons), in every `Panel<Theme>` layer, where
    there's room. See *Positioning controls*'s ring rule.
11. **Labels** for whichever controls need them, in `TextWork` - standard 9pt size, positioned
    per the grid-snapped-center + measure-from-nearest-visible-object + minimum-spacing-vs.-
    neighboring-controls rules, all in *Positioning controls*/*Panel labels*. Not every control
    needs a label - a run of identical, self-evident controls (e.g. J's 8 plain inputs) can
    stay unlabeled if a label would only clutter the panel (Dieter's call).
12. **Bake `TextWork` into `Text<Theme>`**: converting each real `<text>` element into a
    theme-colored `<path>` - **scriptable, no Inkscape needed**, see *Baking text to paths
    (fontTools)* below. Do this once per theme (Orange/Dark/Bright), reusing the exact same
    geometry with only the fill color changed.
13. **Export the three per-theme files**: still manual for now - save `JWork.svg` as
    `<Name>Orange/Bright/Dark.svg` with only that theme's `Background`/`Panel`/`Text` layers set
    to `display:inline` and everything else (including `TextWork`/`Controls`) `display:none`,
    per the visibility matrix in *SVG layer structure*.

## Baking text to paths (fontTools, no Inkscape needed)

Dieter's original workflow used Inkscape's Object-to-Path + Union (the Union step specifically
to clean up self-intersection artifacts Inkscape's own conversion sometimes introduces).
**Discovered 2026-07-15: this can be done directly from the font file with the Python
`fontTools` library, with no artifacts and no Inkscape dependency at all** - each glyph's
outline comes straight from the font's own TrueType contours (correct nonzero winding for
counters like "O"/"D" automatically), so glyphs never overlap or need a union in the first
place.

- **Install once**: `pip install fonttools` (already done in this environment).
- **Font file**: `res/RobotoCondensed-Bold.ttf` (Roboto Condensed Bold is the only font used
  anywhere in OrangeLine).
- **Method**: load the font with `fontTools.ttLib.TTFont`, get `glyph_set = font.getGlyphSet()`
  and `cmap = font.getBestCmap()`. For each character, look up its glyph via
  `glyph_set[cmap[ord(ch)]]`, draw it through an `SVGPathPen` wrapped in a `TransformPen` with
  matrix `(scale, 0, 0, -scale, x_offset, baseline_y)` where `scale = font_size_mm /
  font['head'].unitsPerEm` (Roboto Condensed's `unitsPerEm` is 2048) - the `-scale` on the y
  axis flips the font's y-up glyph space into SVG's y-down space. Advance each subsequent glyph
  by `glyph.width * scale` along x. For centered text (`text-anchor:middle`), compute the total
  advance width first and start `total_width_mm/2` to the left of the target x; for left-aligned
  text, start exactly at the target x.
- **Verified against real measurements**: the font's *true* cap-height this way (e.g. 2.257mm
  for a 3.175mm/9pt label) is the ground truth - more accurate than the earlier ~0.731
  cap-height/font-size ratio estimate elsewhere in this doc, which was itself a reasonable
  approximation but not exact. Prefer computing exact glyph metrics via fontTools over that
  ratio when precision matters.
- **Output style**: `style="fill:<theme Text color>;fill-rule:nonzero;stroke:none"` - no need to
  carry over the leftover `font-size`/`font-family` metadata Inkscape's own baked paths keep
  (that's just inert history in Inkscape's output, not required for correct rendering).
- Confirmed on all 5 of J's `TextWork` texts (title "J", "ORANGE", "LINE", "READY", "LEN"),
  baked into `TextOrange`/`TextDark`/`TextBright` with the same geometry and each theme's own
  Text color - no Inkscape session was opened for any of it.

## Panel dimensions

- **Height**: fixed at **128.5mm** for every module (`PANELHEIGHT` in `src/OrangeLine.hpp`) -
  standard 3U Eurorack height. Never varies module to module.
- **Width unit (HP)**: **5.08mm** (standard Eurorack horizontal pitch, 0.2"). A panel's width
  must always be an exact integer multiple of 5.08mm. Confirmed existing widths: 15.24mm = 3HP
  (K2C/CC14/D2D/Resc "slim" class), 50.8mm = 10HP (MidiLanes/LanesMidi), 86.36mm = 17HP
  (CVLanes/LanesCV).

## Inkscape document grid

Set these exact values in Inkscape's document properties (File > Document Properties > Grids)
for every new panel, so snapping behaves identically across all panel files:

- **Spacing**: `0.254mm` (= exactly `0.01"` - ties cleanly to the HP unit: `5.08mm / 0.254mm =
  20` exactly, i.e. 1HP = 20 grid steps).
- **Origin X**: `0`.
- **Origin Y**: `-0.252750`.

The Y origin isn't a mathematically "clean" number (it's off from a whole grid step of
`-0.254` by `0.00125mm`, likely old Inkscape px/mm rounding drift rather than a deliberate
choice) - **but it's the value every existing panel was authored against, so it must not be
"corrected" retroactively** - doing so would desync every already-placed element from the grid
across all shipped panels. Keep using exactly `-0.252750` for consistency. A from-scratch grid
realignment across all panels would only make sense as part of a deliberate, one-time full SVG
consolidation pass (see below), never as an incidental fix.

**Grid alignment always wins over hitting an exact stated distance/spacing value.** Whenever a
convention in this guide gives a target spacing (e.g. "3 grid units above the control", "4-5
grid units from the frame") that value is approximate - what actually matters is that the
**center point of every object** lands exactly **on** a grid intersection (a multiple of
`0.254mm` from the origin above), never in between two grid lines. For a control, "center"
means its own geometric center (what you'd pass as its x/y position). **For text, "center"
means the visual bounding-box center - not the baseline/anchor y you set on the `<text>`
element.** Since the bounding box sits *above* the baseline (baseline = bbox bottom for all-caps
text with no descenders), snapping requires converting: `center_y = baseline_y - capheight/2`,
snap `center_y` to the grid, then convert back: `baseline_y = snapped_center_y + capheight/2`.

When a computed position doesn't land on the grid, round it to the nearest grid intersection and
accept the resulting spacing being "approximately" rather than exactly the stated target.

**General principle: any stated spacing is measured against the nearest *visible* object in the
layout, not necessarily the underlying logical/technical one.** A label's distance is from
whatever the eye actually sees as the neighboring edge - if a control has a bigger visible
graphic around it (an output-marking box, a display frame, etc.), measure from *that* edge, not
from the smaller component technically underneath it. This isn't specific to jacks/boxes - apply
it to any pair of neighboring visible elements.

Worked example (output box case): J's output jack sits inside an output-marking box (see below)
that extends beyond the jack itself, so the "OUT" label's spacing is measured from the **box's**
edge (`y=109.447`), not the jack's own smaller edge. 9pt label, cap-height ≈2.321mm, target
baseline `109.447 - 3 grid units = 108.685` -> convert to bbox-center `108.685 -
capheight/2 = 107.5245` -> snap to nearest grid point `107.44325` -> convert back to baseline
`108.603713` (actual gap ~3.32 grid units, not exactly 3 - accepted per the priority rule
above).

## "ORANGE LINE" brand accent stripe

Every panel, regardless of width or theme, has the same horizontal accent line near the
bottom - part of the `Panel<Theme>` layer. Verified identical (same y, thickness, color, and
inset-from-edge) across MidiBus (24HP), CVLanes (17HP), and the whole 3HP class (K2C/CC14/D2D/
Resc) - i.e. it does not scale with panel width, only its length does:

- **Y position**: `124.71525mm` from the panel top (fixed, all panels/themes).
- **X span**: from `0.762mm` to `panelWidthMm - 0.762mm` - i.e. a constant **0.762mm inset**
  from each side edge, regardless of panel width.
- **Stroke**: color `#ff6600` (=`ORANGE` in `src/OrangeLine.hpp`, = the "Text" color in
  `Colors.txt` for the Orange theme), width `0.3mm`, `stroke-linecap:round`.
- Same SVG path convention: `<path d="M 0.762,124.71525 H <panelWidthMm - 0.762>"
  style="fill:none;stroke:#ff6600;stroke-width:0.3;stroke-linecap:round;..."/>`.
- Identical across all three themes (Orange/Bright/Dark) - this line is always `#ff6600`, not a
  per-theme color.

## Content frame (padding / header / footer height)

Reference: CV2CC's `PanelOrange` layer, path `id="path8-2"` (`stroke:#803300`) - a rounded
rectangle framing the panel's usable control area. **These margins are fixed and identical
for every module**, regardless of panel width.

- **Side margin (left and right): `0.762mm`**, geometric path node position - same inset as the
  ORANGE LINE accent stripe, so the frame's sides and the accent stripe's ends line up exactly.
- **Header height (top margin) and footer height (bottom margin): `7.620mm` each**, symmetric.
- Both values are clean multiples of the 0.254mm grid unit: `0.762mm = 3 grid steps = 0.03"`,
  `7.620mm = 30 grid steps = 0.3"` (also exactly `10x` the side margin).
- **Corner radius: `2.5mm`.**
- So for a panel of width `W` (height is always 128.5mm): frame `x=0.762mm, y=7.620mm,
  width=W-1.524mm, height=128.5-15.24=113.26mm`, corners rounded `2.5mm`.
- **Stroke**: width **0.3mm** (same as the ORANGE LINE accent), color = that theme's
  `Colors.txt` **"Frame"** color by default (Orange `803300`, Dark `606060fa`, Bright
  `606080fa`) - unlike the accent stripe, this *is* theme-colored, not a fixed orange.
- **Nested frames**: a frame can contain another frame (e.g. an inner frame around a sub-
  section of controls). Padding between an outer frame and a nested inner frame is **6 grid
  units (`1.524mm`)**, measured **path-node to path-node** (the stroke centerline), same
  measurement convention as everything else in this section - not the stroke-inclusive visual
  bbox (see the toolbar gotcha below).
- **Nested frame corner radius**: `inner_radius = outer_radius - padding` (keeps the ring of
  space between the two frames a constant width all the way around the corner, the standard
  nested-rounded-rect principle). For the standard case (outer `2.5mm`, nested-frame padding
  `1.524mm`): `inner_radius = 2.5 - 1.524 = 0.976mm ≈ 1mm`. If a frame is nested more than one
  level deep, keep applying the same subtraction at each level.

**Gotcha - Inkscape's X/Y/W/H toolbar reads the *visual* (stroke-inclusive) bounding box, not
the geometric path.** For a 0.3mm stroke, the visual bbox is inset by half the stroke width
(0.15mm) from the true path node on every side. This is exactly why CV2CC's toolbar reading
(`x=0.612, y=7.470, w=49.588, h=113.585`) doesn't match the clean values above - `0.612 +
0.15 = 0.762` and `7.470 + 0.15 = 7.620` recover the real geometric values. **When reading any
stroked shape's position off Inkscape's toolbar, add strokewidth/2 to the reported top/left and
subtract it from the reported bottom/right to get the true geometric coordinates** - don't use
the raw toolbar numbers directly as design values.

**Parsing caveat**: reconstructing this element's bounding box from its raw `d` path data (even
with a proper SVG path tokenizer handling multi-segment `c` curves) produced a wildly wrong
result (~y 66-179mm, exceeding the 128.5mm panel entirely) - likely an artifact of Inkscape's
Fillet/Chamfer live path effect encoding the corner-rounding in a way that doesn't parse as a
plain geometric bounding box. **Get exact frame geometry from Inkscape's own X/Y/W/H toolbar
(then apply the stroke-width correction above) rather than computing it from the `d`
attribute**, for this specific kind of rounded-rectangle element.

## SVG layer structure

Every `res/<Name>Work.svg` and its three exported `res/<Name>Orange/Bright/Dark.svg` share the
same 11 top-level layers (`inkscape:groupmode="layer"`), in this exact **XML document order**
(verified across CC14/K2C/D2D/Resc):

```
BackgroundBright, PanelBright, TextBright,
BackgroundDark,   PanelDark,   TextDark,
BackgroundOrange, PanelOrange, TextOrange,
TextWork,
Controls
```

**Careful: Inkscape's Layers panel lists layers top-to-bottom in the *reverse* of XML document
order** (the panel's top entry = drawn last = frontmost; SVG always draws later XML elements on
top of earlier ones). So the list above, read top-to-bottom in Inkscape, actually appears as:

```
Controls, TextWork,
TextOrange, PanelOrange, BackgroundOrange,
TextDark,   PanelDark,   BackgroundDark,
TextBright, PanelBright, BackgroundBright
```

i.e. `Controls` frontmost, then `TextWork`, then each theme as a **Text/Panel/Background**
trio (text in front, background at the back) - confirmed as the intended convention.

- **`Background<Theme>`**: one `<rect>`, full panel size, fill = that theme's `Colors.txt`
  Background color.
- **`Panel<Theme>`**: baked (path-only) decorative panel art for that theme - the ORANGE LINE
  accent stripe, plus any theme-colored decoration (e.g. display-window frames). Colors here
  follow `Colors.txt` per theme (e.g. `Frame`/`DisplayFill`), except the accent stripe which is
  always `#ff6600` regardless of theme.
- **`Text<Theme>`**: the baked (Object to Path + Union'd) text for that theme - module name +
  labels, filled with that theme's `Colors.txt` "Text" color (Orange `ff6600`, Dark `c4bac4`,
  Bright `15152b` - confirmed against CC14). Only the ORANGE LINE accent stripe itself is
  theme-invariant `#ff6600` regardless of theme; the text color does follow the theme.
- **`TextWork`**: the one shared, *editable* real-`<text>`-element layer (not path-baked) used
  for authoring/layout - source for all three `Text<Theme>` layers after Inkscape's Object to
  Path + Union.
- **`Controls`**: guide-only layer with copies of the real jack/knob component art, positioned
  at final coordinates, purely for visual orientation while laying out in Inkscape - never
  rendered at runtime (Rack draws real ports/knobs itself from the widget C++ code).

**Visibility (`style="display:inline"` vs `"none"`) is how the single shared file structure
becomes theme-specific**, verified identical across CC14's four files:

| File | visible (`display:inline`) | everything else |
|---|---|---|
| `<Name>Work.svg` | `BackgroundOrange`, `PanelOrange`, `TextWork`, `Controls` | `none` |
| `<Name>Orange.svg` | `BackgroundOrange`, `PanelOrange`, `TextOrange` | `none` |
| `<Name>Bright.svg` | `BackgroundBright`, `PanelBright`, `TextBright` | `none` |
| `<Name>Dark.svg` | `BackgroundDark`, `PanelDark`, `TextDark` | `none` |

The Work file defaults to showing the Orange theme + the editable `TextWork` + the `Controls`
guide layer while authoring (never `TextOrange`, since that's the redundant baked duplicate of
what `TextWork` already shows). Each exported file shows exactly one theme's three baked layers
and hides everything else, including `TextWork`/`Controls`.

## Title/header text

- Font is always **Roboto Condensed, Bold** (matches every other panel and UI text in
  OrangeLine).
- Module-name/logo text in `TextWork`: **font-size 5.64444** (stored SVG value, see pt/mm
  conversion below - this equals **16pt** in Inkscape's own font-size toolbar) is the
  established size for multi-character logos (confirmed on CC14 "CC14", K2C "K2C", Resc
  "RESC"). A single-character logo (like J's "J") can go larger - Dieter's call per module.
- **Solved: Inkscape's font-size toolbar shows points (pt), not document mm**, regardless of
  the document's own mm-based unit setting used everywhere else (path coordinates, positions,
  etc). Conversion: `stored_mm_value = pt_value x 0.3527778` (`1pt = 25.4/72mm`). Verified two
  ways: CC14's small labels ("ORANGE"/"LINE" etc) store `font-size:3.52777px` = confirmed by
  Dieter to read as `9.99998` in Inkscape = 10pt (`3.52777 / 0.3527778 = 9.99998`); CC14's own
  big module-name logo (`5.64444`) converts to exactly `16pt`. J's title (target 14pt) ->
  `14 x 0.3527778 = 4.938889` stored value.
- **Correction (2026-07-15, superseded by the pt/mm conversion above)**: an earlier version of
  this doc derived a "cap-height ≈ 0.731 x font-size" ratio by comparing a baked path's
  leftover `font-size:Npx` style value directly against its measured path geometry - the ratio
  happened to look plausible but was the wrong explanation (it's really a pt-vs-mm unit
  mismatch, not a font cap-height ratio). Don't reuse the 0.731 ratio for anything.
- **Placement rule (replaces the removed formula above, now the fixed convention for every
  module)**: the header text is **horizontally centered** (`text-anchor:middle` at
  `x = panelWidthMm / 2`) and **vertically centered between the panel's top edge (y=0) and the
  top edge of the outer content frame (y=7.620mm)** - i.e. its bounding-box center sits at a
  fixed **`y = 3.810mm`** (the midpoint of the 7.62mm header zone), regardless of font-size or
  panel width. This sidesteps needing to know the glyph's exact rendered mm size at all - center
  the text's own bounding box on `y=3.810mm` using Inkscape's align tools, rather than computing
  a baseline y by hand.
- **Cross-checked against CC14's existing title**: its real (geometric, not font-size-derived)
  bounding box was measured at y=1.7455mm to y=5.8713mm, center = `3.8084mm` - matches the
  `3.810mm` rule to within 0.0016mm, confirming this is consistent with the panels already
  shipped rather than a newly-invented number.

## Panel labels (jack/control captions, not the header/footer/module-name text)

- **Standard size: 9pt (`3.175mm` stored value)**. Confirmed dominant across the whole panel
  library via a direct audit of every `TextOrange` layer (`res/*Orange.svg`): **444 occurrences**
  of `font-size:3.175px`, by far the most common label size found. Other sizes present are
  separate special-purpose text, not "labels" and not exceptions to this rule: the "ORANGE
  LINE" brand mark is 10pt (`3.52777mm`, 158x), module-name logos are 16pt (`5.64444mm`, 37x).
- **Exceptions**: smaller sizes are fine when a label genuinely doesn't fit (space-constrained),
  confirmed in the wild as 6pt (`2.11667mm`, 11x) and 3pt (`1.05833mm`, 2x, Buckets only) - but
  these are deliberately rare, fine-tuned by Dieter visually case by case, not something to
  default to.
- **Label-to-frame spacing**: the distance between a label's bounding box and the nearest frame
  should only go below **1.25mm** in exceptional (space-constrained) cases - same philosophy as
  the size exception above, not a hard minimum.
- Two not-yet-explained sizes found during the audit, flagged for awareness rather than treated
  as part of either rule above: **14pt** (`4.9389mm`, 5x across Dejavu/Gator/Morph/Phrase/Resc)
  and **24pt** (`8.46667mm`, 6x, source file not yet identified) - ask Dieter what these are for
  before assuming they follow the standard-label or exception conventions.

## Positioning controls (jacks, buttons, knobs) relative to the frame

Guidance, not a rigid formula - applied "where reasonable" (Dieter: "falls möglich"):

- **Corner controls**: a control that logically belongs in a frame's corner (e.g. a connection-
  status light, like the LANES family's `LEFT_CONN_LIGHT`/`RIGHT_CONN_LIGHT`) should sit *on the
  diagonal running through that corner* - i.e. through the center of the frame's rounded-corner
  arc - rather than at an arbitrary offset picked independently for x and y. This keeps corner
  elements visually anchored to the frame's corner consistently across panels of different
  widths, since the diagonal direction (45 deg from the corner) doesn't depend on panel width.
  (Note: the existing LANES corner-light placeholder positions, e.g.
  `calculateCoordinates(3.5f, 4.f, 0.f)`, predate this rule and don't yet follow it - revisit
  during the SVG consolidation pass.)
- **Single-column special case**: when there's only one column (so a control near the bottom
  has no left/right corner to pick between - both bottom corners are equally relevant), the
  control sits at the point where **both bottom corners' 45 deg diagonals cross** - equivalent
  to requiring equal distance to the left, right, *and* bottom frame edges simultaneously. For a
  control horizontally centered at the frame's own center (the usual single-column case), this
  reduces to a simple formula using the frame's geometric (path-node) coordinates:
  ```
  y = frameBottom - (frameCenterX - frameLeft)
  ```
  (the same result whether measured from the frame's sharp mitre corner or its rounded-corner
  arc center - shifting by the corner radius moves both diagonals' start points along the same
  45 deg line, so the crossing point is identical either way). Confirmed on J's output jack:
  frame `left=0.762, bottom=120.88, centerX=7.62` -> `y = 120.88 - (7.62-0.762) = 114.022mm`.
  This calculation is entirely about the frame's own geometry - it doesn't depend on the
  control's physical size at all, only where its *center* should sit.
- **Spacing from the frame**: aim for roughly **4-5 grid units** (`4-5 x 0.254mm ≈ 1.0-1.27mm`)
  between a control's own bounding box and the frame edge, where the layout allows it. A target
  to design toward, not a hard constraint - real component sizes/spacing needs can override it.
- **Label side vs. nearby panel edge**: to spread labels out and avoid them clustering against
  a frame edge, put a control's label on the side facing *away* from the nearest edge - a
  control near the **bottom** of the panel gets its label placed **above** it, a control near
  the **top** gets its label placed **below** it (i.e. always toward the open/interior side, not
  toward the closer edge). Example: J's output jack (near the bottom) should get an "OUT" label
  above it, not below it.
- **Repeat pitch for a series of controls**: when laying out a row/column of sockets, buttons,
  or knobs with no label sitting between consecutive ones, aim for a center-to-center spacing of
  roughly **40 grid units (~10mm)** - a target/base grid to design toward where practical, not a
  hard requirement (real component sizes or a tight panel can force a different pitch).
- **Ring/frame around a parameter control**: knobs, buttons, and other *parameter* controls
  (not jacks - jacks get the output-marking box treatment above instead, where applicable) get a
  thin circular ring around them by default, whenever there's room and it looks right visually -
  skip it in exceptional/tight cases, not a hard requirement. **Radius 18 grid units (`4.572mm`)
  , stroke 0.3mm, color = that theme's Frame color** (same stroke convention as the outer content
  frame and the output-marking box), no fill, centered on the control. Confirmed on J's output-
  gate-length knob.
- **Minimum spacing between neighboring controls, relative to a label's own spacing.** A
  control's visible boundary (including its ring, if it has one) must never sit closer to a
  *different* neighboring control/label than that neighboring label sits from the object it
  labels - otherwise the label reads as belonging to the wrong thing. Target/minimum hierarchy,
  measured as a multiple of the relevant label-to-object gap:
  - **Target: 2x** that gap.
  - **Acceptable minimum: 1.5x.**
  - **Exception minimum: 1x**, only when space is genuinely tight - never less.
  Worked example: J's gate-length knob ring sits above the "OUT" label. The "OUT" label's own
  gap to its output box is ~3.32 grid units, so the ring's gap to the label should target
  `2 x 3.32 ≈ 6.64` grid units - snapped to grid, the knob ended up at `y=100.07725` giving an
  actual gap of ~6.43 grid units (close enough to the 2x target, well above the 1.5x/1x floors).

## Output-marking box

Rack convention: visually distinguish output jacks from inputs with a color-filled box behind
the connector(s) - either one box per output, or one box spanning several outputs together
(e.g. Hold's whole right-hand column of output sockets shares one background box).

- **Color**: `Colors.txt`'s **"OutputFill"** field, per theme (Orange `101020`, Dark `171717`,
  Bright `808094fa` - confirmed by Dieter directly, don't substitute a different color).
- **Stroke**: also has a **0.3mm** stroke in that theme's **"Frame"** color (Orange `803300`,
  Dark `606060` @ 0.98 opacity, Bright `606080` @ 0.98 opacity) - same stroke convention as the
  outer content frame.
- **Geometry** (confirmed from CC14's `CV_OUTPUT` box, Dieter pointed to this as the reference;
  this specific path parsed cleanly with the standard tokenizer, unlike the frame - it's a plain
  hand-drawn rounded box, not an LPE-encoded one): **~9.15mm x 9.15mm square, corner radius
  ~1.27mm (5 grid units), centered exactly on the jack** (box center matched the jack's own
  center to within 0.03mm). For a single output jack at position `(jx, jy)`: box
  `x=jx-4.575, y=jy-4.575, width=9.15, height=9.15, r=1.27`.

## Known issues to fix during the SVG consolidation pass

- **Hold**: the corner radii of its inner (output-marking?) boxes are wrong - noticed by Dieter
  2026-07-15, not yet fixed.
