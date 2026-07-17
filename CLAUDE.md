# OrangeLine

VCV Rack 2 plugin (C++). Repo root is the plugin package consumed by VCV Rack's plugin build system (`plugin.mk` from the Rack SDK).

## Build

```
source setenv.sh   # sets RACK_DIR and RACK_USER_DIR (MSYS2 MinGW64 shell)
make -j$(nproc)
```

`RACK_DIR` must point at a Rack-SDK checkout (`setenv.sh` sets it to `$HOME/RackDevelopment/Rack-SDK`). `Makefile` globs `src/*.cpp` automatically — new modules never need a Makefile change.

**Toolchain location on this machine:** `gcc`/`g++`/`as` live under `C:\msys64\mingw64\bin`, `make` lives under `C:\msys64\usr\bin`. In a real MSYS2 MINGW64 terminal these are already on `PATH`. In a sandboxed/non-MSYS shell (e.g. Git Bash), `/mingw64` may resolve to a *different*, compiler-less bundle — if `make`/`gcc` aren't found, prepend the real paths explicitly: `export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"`.

**Known sandbox quirk:** in a constrained shell tool, `g++` invoked directly works fine, but the same `g++` invoked as a child of `make` (or of a nested `bash script.sh`) can fail with `Cannot create temporary file in C:\WINDOWS\: Permission denied`, even with `TMP`/`TEMP` correctly exported — this is an environment-propagation issue specific to nested child processes in that sandbox, not a real toolchain/config problem. Workaround: run each compiler invocation as a *direct, top-level* command rather than through `make` or a script (use `make -n` to get the exact command lines, then run each one individually). This does not occur in a real interactive MSYS2 terminal — prefer letting the user run `make` there when possible.

## Module architecture (read before adding or editing a module)

Every module is a single `struct <Name> : Module` that composes the shared framework via **`#include`, not inheritance**:

```cpp
struct MyModule : Module
{
#include "OrangeLineCommon.hpp"      // shared state machine, always first
#include "MyModuleJsonLabels.hpp"    // per-module json label array
    // module-specific members + module* hooks
};
```

- **`src/OrangeLine.hpp`** — global macros/constants shared by all modules: `POLY_CHANNELS=16`, `STYLE_ORANGE/BRIGHT/DARK`, `quantize(cv)` (round to nearest semitone), `note()`/`octave()`, panel-layout helpers (`calculateCoordinates`, `OFFSET_*` constants, `PANELHEIGHT=128.5mm`), and shared widgets: `NumberWidget`/`TextWidget` (display-only, `drawLayer(layer==1)`) and `CCGridWidget` (interactive 16×8 clickable grid, plain `draw()` since it's a control not a dimmable readout, supports click-drag "paint" via `onDragHover` — see `CC2CV.cpp`/`CV2CC.cpp` for the pattern of wiring it to a module's own `bool[128]`/`float[128]` via raw pointers instead of per-cell Params). Used by CC2CV, CV2CC, RECALL and MidiBus. `CCGridWidget::create()` takes its visual-state pointers (`enabled`/`activity`/`value`/`locked` plus further optional ones) as trailing parameters defaulting to `nullptr` — adding a new one for a module that needs an extra visual distinction never touches existing callers. The exact color scheme is intentionally not documented here (Dieter iterates on it directly) — see the widget's own comments in `OrangeLine.hpp` for the current mapping.
- **`src/OrangeLineCommon.hpp`** — the actual engine, `#include`d literally into every module struct. Defines the generic `process()`:
  1. `moduleSkipProcess()` — module decides whether to skip this sample (control-rate throttling; see below).
  2. `processParamsAndInputs()` — populates `OL_statePoly[inputId * POLY_CHANNELS + channel]` from real jack voltages (skipped when `moduleSkipProcess()` returns true).
  3. `moduleProcessState()` → `moduleProcess(args)` → `moduleReflectChanges()` → `reflectChanges()` (writes `OL_state`/`OL_statePoly` out to real VCV ports, dirty-flag gated).
  - JSON persistence (`dataToJson`/`dataFromJson`) iterates `NUM_JSONS` via the `char *jsonLabel[NUM_JSONS]` array from `<Name>JsonLabels.hpp`.
  - Trigger outputs: mark with `setStateTypeOutput(id, STATE_TYPE_TRIGGER)`, then just `setStateOutput(id, 10.f)` when it should fire — the framework auto-manages a `dsp::PulseGenerator` for a hardcoded 1ms pulse and resets the state to 0 itself. This pulse keeps advancing in real time even during skipped (throttled) cycles. See `Cron.cpp`'s `CLK_OUTPUT`/`RST_OUTPUT` for the canonical example.
  - Poly I/O: `setInPoly(id, true)` / `setOutPoly(id, true)` in `moduleInitStateTypes()`. Read poly input voltage with `OL_statePoly[inputId * POLY_CHANNELS + channel]`. Write poly output with `setStateOutPoly(outputId, channel, value)` + `setOutPolyChannels(outputId, count)`.

- **Required hooks every module must implement** (called by the shared engine above): `moduleSkipProcess()`, `moduleInitStateTypes()`, `moduleInitJsonConfig()`, `moduleParamConfig()`, `moduleCustomInitialize()`, `moduleInitialize()`, `moduleReset()`, `moduleProcess(args)`, `moduleProcessState()`, `moduleReflectChanges()`.

- **Control-rate throttling**: most modules only need to react to CV/gate changes every ~1ms, not every audio sample. Standard pattern (see `Buckets.cpp`, `Hold.cpp`):
  ```cpp
  bool moduleSkipProcess() { return idleSkipCounter != 0; }
  ```
  Modules with tighter timing needs (clocks, phase-based modules) override this more carefully — see `Gator.cpp`, `Swing.cpp`, `Phrase.cpp` for patterns that avoid skipping across a clock edge.

- **Enum conventions** (`<Name>.hpp`): four enums, `jsonIds` / `ParamIds` / `InputIds` / `OutputIds` (+ `LightIds`). Repeated/indexed I/O uses a `_LAST` sentinel so the count auto-derives:
  ```cpp
  enum InputIds {
      CV_INPUT,
      CV_INPUT_LAST = CV_INPUT + NUM_ROWS - 1,
      NUM_INPUTS
  };
  ```
  `NUM_PARAMS`/`NUM_INPUTS`/`NUM_OUTPUTS`/`NUM_LIGHTS`/`NUM_JSONS` are just the final auto-incremented enumerator — never hardcode them.

- **Widget/panel conventions**: every module widget follows the same shape — `setPanel()` loads `res/<Name>Orange.svg`; a `brightPanel`/`darkPanel` pair (`SvgPanel*`, members already declared in the shared `OrangeLineCommon.hpp` section) load `res/<Name>Bright.svg` / `res/<Name>Dark.svg` and toggle visibility via a `styleChanged` flag + `STYLE_JSON`; a right-click `appendContextMenu()` offers the Orange/Bright/Dark switch (copy the `<Name>StyleItem : MenuItem` pattern verbatim, e.g. from `Buckets.cpp`). `res/<Name>Work.svg` is typically the Inkscape source/authoring file for the shipped panels (not loaded at runtime).
  - `res/Template.svg` is the boilerplate starting point for a brand-new panel with full branding/screws/chamfered corners — only reach for it when actually hand-authoring final artwork; for a quick functional placeholder (no artwork yet), a plain rectangle + a guide layer of jack-position circles is enough to get a module running (see `res/LanesWork.svg` for the pattern: a `controls` layer of plain circles at the exact mm coordinates the widget C++ uses for `addInput`/`addOutput`, so guide and code stay in sync).

## Touch: hidden low-latency force-process infrastructure

Chained throttled modules (see control-rate throttling above) each wake up on their own
independent ~1kHz cycle, so a signal hopping through several modules can pick up up to ~43
samples of avoidable extra latency per hop on top of Rack's own unavoidable one-sample-per-
cable latency. **Touch** is a shared mechanism that collapses this: a hidden mono trigger
**Wakeup** forces that one sample to fully process regardless of the throttle counter, and a
hidden mono trigger **Ready** relays the same pulse onward by default — chaining Ready →
Wakeup across a string of modules keeps the whole chain in near-lockstep. (Right-click menu
label: "Wakeup/Ready Ports" — internal identifiers/comments/filenames still say "Touch"/
`OL_touch*`/`TouchIn.svg`/`TouchOut.svg`, only the user-facing port names changed.)

- **Zero changes to any module's own enums.** Wakeup/Ready live at raw Rack port index
  `NUM_INPUTS` / `NUM_OUTPUTS` (each module's own enum sentinel — "one past the last real
  port"), registered via `config(NUM_PARAMS, NUM_INPUTS + 1, NUM_OUTPUTS + 1, NUM_LIGHTS)` in
  the shared `initializeInstance()`. They're read/written directly as `inputs[NUM_INPUTS]` /
  `outputs[NUM_OUTPUTS]`, entirely bypassing `OL_state`/`stateIdx*` — no `InputIds`/`OutputIds`
  entry needed, so no risk of renumbering existing patch cables.
- **Mechanism** (`OrangeLineCommon.hpp`, all under `#ifndef OL_TOUCH_DISABLED`): `bool touchFired
  = OL_touchInTrigger.process(inputs[NUM_INPUTS].getVoltage()); bool skip =
  moduleSkipProcess() && !touchFired;` — a touch forces the sample through without disturbing
  `idleSkipCounter`'s own ongoing phase. The Ready pulse (`OL_touchOutPulse`, currently
  **0.05s** — deliberately longer than the standard 1ms trigger-output convention so it's
  actually visible on a scope, tune freely in one place if needed) keeps advancing every real
  sample regardless of throttling, same reasoning as `processActiveOutputTriggers()`.
- **`OL_touchOutRequest`** (bool): lets a module that already has its own early-wake trigger
  (a custom `moduleSkipProcess()` forcing `skip = false` on some other condition, e.g. a clock
  edge) request a Ready relay itself, without needing a redundant dedicated Wakeup of its
  own — see Mother/Dejavu/Morph/Morpheus below.
- **Widget helpers** (`OrangeLine.hpp`): `addOrangeLineTouchPorts(w, module, NUM_INPUTS,
  NUM_OUTPUTS, &module->OL_touchInPort, &module->OL_touchOutPort, &module->OL_touchVisible)`
  (full Wakeup+Ready, top-left/bottom-right corners) or `addOrangeLineTouchOutputOnly(...)`
  (Ready only, for modules with their own early-wake trigger and no need for Wakeup) in the
  widget constructor after panel setup; `addOrangeLineTouchMenuItem(menu, inPort, outPort,
  visibleFlag)` in `appendContextMenu()`, placed **above** the Style section with its own
  separator. Both jacks default hidden (`Widget::visible` gates hit-testing too, confirmed via
  the SDK's `recursePositionEvent` — a hidden port is also unpatchable, so hidden really means
  inactive, not just invisible), toggled via the menu item and persisted through an
  unconditional `"touchVisible"` JSON key added directly in the shared `dataToJson()`/
  `dataFromJson()` (not any module's own `jsonIds` — guarded with `json_is_boolean()` first).
- **Per-module opt-out**: `#define OL_TOUCH_DISABLED` in a module's own `<Name>.hpp` **before**
  its first `#include "OrangeLine.hpp"` (or `"<Family>Shared.hpp"`, which itself includes
  `OrangeLine.hpp`). Position overrides for panels with no room in the usual top-left spot:
  `#define OL_TOUCH_IN_Y_MM <value>` (etc., all `#ifndef`-guarded in `OrangeLine.hpp`) before
  that same include.
- **Current per-module state** (Dieter's own call per module, not a blanket rollout):
  - **Full Wakeup+Ready**: Buckets, CVLanes, Cron, D2D\*, Fence (coexists with its own
    pre-existing TRG in/out), Gator, Hold (the pilot), K2C\*, LanesCV, Phrase, Resc\*, Swing,
    CC14\* (\* = slim panel, Wakeup repositioned to the bottom via `OL_TOUCH_IN_Y_MM`, same
    row as Ready mirrored to the left).
  - **Ready only** (already has its own early-wake trigger via `OL_touchOutRequest`, no
    Wakeup needed): Mother, Dejavu, Morph, Morpheus.
  - **Disabled entirely** (`OL_TOUCH_DISABLED`): CC2CV, CV2CC, RECALL, MidiBus, MidiLanes,
    LanesMidi — MIDI-hardware-facing modules, where MIDI's own transport latency dwarfs the
    sub-millisecond timing Touch fixes; would just be one more unused hidden jack on an already
    busy panel.
  - **J (planned, not yet built)**: deliberately gets **no Touch I/O of its own** — it's Touch-
    adjacent infrastructure (syncing branches that used Touch), not a Touch consumer needing
    its own further latency reduction.
- **Deliberately undocumented anywhere user-facing** (README, in-app manual, right-click menu
  wording) — Dieter's explicit call: "the community should still have something to puzzle
  about" ("die community soll noch was zum rätseln haben"). This file (developer-facing) is the
  only place the mechanism is described.

## Self-drawn widget backgrounds vs. static panel decoration

A recurring pattern first worked out on the XO-family (XO8/XD8/XOD8/XO16/XD16/XOD16) displays and
gate indicators, generalizing the "cover the panel's own decoration when a widget's visual state
changes" idea already used by `X8DButtonCover`/`X16DButtonCover`/`X8ValueButton`. Read this before
adding a new per-channel display/indicator cell to any module, or before touching an existing
one's sizing/coloring.

1. **A widget that needs to render more than one mutually-exclusive "look" in the same panel
   cell (e.g. a numeric readout vs. a lit/unlit gate square) needs an opaque background under
   *both* looks, not just under whichever one used to rely on the static SVG art.** If the panel's
   own decorative rect is only masked while one look is showing (the historical X8D pattern —
   mask only appears for the button look, the continuous look still relies on the real SVG art
   underneath), removing that static art later breaks the OTHER look, which never had its own
   cover. Once a cell's decoration is going away entirely, give it an **always-visible** cover
   (never toggled), and let each look draw its own content on top of that shared, guaranteed-
   opaque background.
2. **Order of operations when replacing static decoration with a code-drawn cover**: get the
   cover's geometry/color right and confirmed live *first*, with the static SVG art still present
   as a safety net (any coverage gap just shows old art peeking through, not a hole). Only once
   that's confirmed should the static decoration actually be deleted from the `Work.svg` source —
   and if Dieter is doing that deletion himself, explicitly confirm every coordinate/color you
   still need has already been extracted into the C++ side first, since once it's gone from the
   file it can't be re-measured.
3. **A themed cover/frame's own corner radius must match the *real* underlying artwork's radius
   exactly** if the cover is meant to fully hide it — a bigger rounded corner cuts away more area
   near each corner than a smaller one, so a mismatched (typically larger, "looks about right")
   radius leaves the real artwork's own corners poking out as small visible slivers even when the
   width/height match perfectly. Get the true radius from the SVG itself: an
   `inkscape:path-effect`'s `nodesatellites_param` fillet value, or `0` (sharp corners, no
   rounding at all) for a plain `<rect>` with no `rx`/`ry`. Don't assume every sibling module uses
   the same radius — check each `Work.svg` directly (XD8's own display cell turned out to be a
   plain sharp rect while XOD8/XOD16/XD16 all use a ~0.77258mm fillet).
4. **A control widget that loads its own fixed-size asset (e.g. a shared button-cap SVG,
   `res/SquareButton.svg`) should own a box sized to that asset's *natural* size and be
   positioned by its own center point — never scaled to fit a differently-sized panel cell.**
   Scaling distorts stroke widths and forces any separately-drawn themed frame around it to
   choose between matching the scaled (fragile, cell-geometry-dependent) size or the asset's true
   size (mismatching whatever covers the surrounding cell) — see `X8ValueButton`
   (`X8Common.hpp`) for the reference convention: `box.size = cap->box.size` set once in the
   constructor, never touched afterward, with the frame drawn at its own fixed size independent
   of `box.size`. Position via `box.pos = calculateCoordinates(centerX, centerY,
   0.f).minus(box.size.div(2.f))` (mirrors what `createWidgetCentered` does internally) — never
   top-left-anchor a differently-sized box to a panel rect's own top-left corner.
5. **A "digital display" cell's own background is a genuinely different per-theme color from the
   general panel/strip background** — this codebase's convention is `X_DISPLAY_BG_*` (matching
   `tools/bake_panel_theme.py`'s own `THEME_DISPLAYFILL_COLOR`: `#100600`/`#171717`/`#15152b`)
   vs. `X_STRIP_BG_*` (the plain panel background, used e.g. behind a knob-ring cover). Mixing
   these up is easy to miss in isolation (both are dark, plausible-looking colors) but creates a
   visible mismatch the moment two widgets sharing a cell use different ones — e.g. a themed
   frame drawn on top using the correct color suddenly doesn't blend with a cover drawn
   underneath using the wrong one. Check which semantic field a widget is meant to represent
   before picking a color constant, don't assume all dark backgrounds are interchangeable.
6. **When insetting an opaque cover to avoid painting over an adjacent decorative element (e.g.
   the panel's own outer frame), the inset *shrinks* the covered rectangle on the affected
   side(s) — it never expands it.** Easy to get backwards when adjusting a second axis right
   after correctly inset-ing the first one (a real mistake made live: correctly shrank left/right,
   then wrongly *expanded* top/bottom by the same amount when asked for an analogous vertical
   nudge). If unsure which direction a requested nudge means, match whatever the
   already-confirmed-correct axis did.

## Pitfalls learned the hard way (read before writing `moduleProcess()`)

- **`setOutPolyChannels()` must be (re)asserted every tick inside `moduleProcess()`, never only once in `moduleInitStateTypes()`/the constructor.** `initializeInstance()` does `memset(OL_polyChannels, 0, ...)` immediately *after* `moduleInitStateTypes()` runs, silently wiping a one-time call and leaving the output permanently at 0 channels — with no compile error and no obvious symptom besides "no output at all". This holds even when the channel count is constant (K2C: always `POLY_CHANNELS`) — constant doesn't mean "set once", it means "assert the same value every tick".
- **Disconnected inputs don't get their `OL_statePoly` refreshed.** `processParamsAndInputs()` skips writing state for a disconnected input entirely, so a stale last-known value (e.g. a gate stuck high) lingers unless the module explicitly checks `getInputConnected(id)` and forces a neutral value (0/off) itself. Every module that reacts to gate/CV state needs this check per input it reads — see `Lanes.cpp`/`K2C.cpp` for the pattern.
- **Never compare two "conceptually equal" floats computed via different code paths with `==`.** `quantize(cv)` is `round(cv * 12.f) / 12.f`; if a second value is built by repeatedly adding `1.f/12.f` instead, it drifts from `quantize()`'s result after a few steps (binary float can't represent twelfths exactly) and an exact-equality match silently stops firing with no error. Fix: track the accumulator as an integer semitone count and divide by 12 at the point of comparison, so both sides run the *identical* formula.
- **`NUM_CHANNELS` does not exist anywhere in OrangeLine — use `POLY_CHANNELS`** (defined in `src/OrangeLine.hpp`, always 16). A hallucinated `#define NUM_CHANNELS` in a new module's `.hpp` may compile silently (macro re-definition or shadowing) but is a sign something wasn't cross-checked against the existing codebase convention.
- **If a crash/misbehavior appears right after a module rename or restructure and doesn't reproduce under logical bisection, suspect a stale build artifact before the code.** A cached `build/src/<Name>.cpp.o` not being recompiled by `make`'s dependency check produced a real `Fatal signal 11` in Rack's module browser that took a whole throwaway diagnostic module to chase — the fix was `make clean` + rebuild, zero code changes. Try that early when a crash defies explanation.
- **Renaming a module (e.g. after the user picks a new name) must never let text-substitution touch `.svg` file *contents*.** Only rename the files themselves (`mv`/`cp`, byte-for-byte content unchanged); a broad find/replace across `res/*.svg` has corrupted XML attributes (e.g. `sodipodi:docname="OldOrange.svg"` losing its opening quote) and even reached into unrelated tracked files like `LICENSE.txt`. It is fine for the C++ to keep referencing the old filename after a rename — asset paths are independent of the struct/model name.
- **A `dsp::Timer`/rate-limiter inside `moduleProcess()` must scale elapsed time by `(samplesSkipped + 1)`, not just `args.sampleTime`.** `moduleProcess()` is only called on the one sample in ~43 (at 48kHz) where `moduleSkipProcess()` returns false — feeding it a bare `args.sampleTime` each call undercounts real elapsed time by that same factor, so e.g. a "200Hz max" rate limiter copied verbatim from a non-throttled module (VCV Core's CV-CC does exactly this) would actually fire ~43x slower than intended. `samplesSkipped` holds the correct skipped-sample count at the moment `moduleProcess()` runs (it's reset only after) — use `timer.process(args.sampleTime * (samplesSkipped + 1))`. See `CV2CC.cpp`'s MIDI send rate limiter.
- **Mono trigger *inputs* work like trigger outputs, just less documented**: `setStateTypeInput(id, STATE_TYPE_TRIGGER)` in `moduleInitStateTypes()`, then check `changeInput(id)` in `moduleProcess()` — true exactly on the one tick the input's rising edge was detected (the framework manages the `dsp::SchmittTrigger` and resets the flag every real tick automatically). See `CV2CC.cpp`'s `FORCE_INPUT`.
- **Extending `dataToJson()`/`dataFromJson()` beyond the flat `OL_state` float array** (e.g. persisting a `midi::Port`, which needs a nested `json_t` object, not a float): `OrangeLineCommon.hpp` defines these as the module's own `override`, so a second definition in the module `.cpp` is an ODR violation. Instead use the two optional hooks `moduleExtraDataToJson`/`moduleExtraDataFromJson` (`std::function<void(json_t*)>`, default `nullptr`, called from the shared `dataToJson()`/`dataFromJson()` if set) — assign a lambda in the module's constructor. Every other module leaves these `nullptr` and is completely unaffected. See `CC2CV.cpp`/`CV2CC.cpp`.
- **A module with zero real inputs or zero real outputs** (first occurred with `CC2CV`/`CV2CC`, pure MIDI-port modules with no CV on one side) makes one of `OrangeLineCommon.hpp`'s `memset` calls degenerate to a zero-length array, which triggers a harmless `-Wmemset-elt-size` warning ("length equal to number of elements without multiplication by element size"). It's a false positive (memset of 0 bytes is a correct no-op) — silenced locally around `initializeInstance()`'s memset block rather than being a sign of a real bug.
- **System MIDI messages (status nibble `0xf` — SysEx `0xF0-0xF7`, Clock `0xF8`, Start/Continue/Stop `0xFA-0xFC`, etc.) bypass `midi::Port.channel` filtering entirely**, verified directly in `Rack/src/midi.cpp`'s `InputDevice::onMessage`/`Output::sendMessage` (not just the SDK headers): `if (message.getStatus() != 0xf && input->channel >= 0 && message.getChannel() != input->channel) continue;`. They arrive in `tryPop()` regardless of the selected channel, and `sendMessage()` never overwrites their channel-nibble byte (which, for status `0xf`, is actually the message *sub-type*, not a channel) with the port's configured channel. See `MidiBus.cpp`'s `handleIncomingSystemMessage`/`sendSystemRealtime` for the pattern of manually stamping/reading that sub-type nibble.
- **When a `CCGridWidget` needs a visual state that reflects "did an override actually change something" rather than just "is something patched"**, and that override only fires at a specific event (e.g. a one-shot snapshot) rather than continuously: latch the comparison result *at that same event*, don't recompute it continuously against whatever the live baseline has since drifted to. RECALL's TX grid hit this: comparing a frozen snapshot against the *current* live receive value would keep "diverging" over time even with no override at all, simply because the live side kept moving after the freeze while the snapshot didn't. Compare the override's incoming value against the baseline's value *at the moment of the snapshot*, store the boolean result, and leave it alone until the next such event — a genuinely continuous override (recomputed every tick) is a separate, simpler case and doesn't need latching.
- **`moduleCustomInitialize()` runs on *every* non-skipped `process()` tick, not once at construction** — despite the name, and unlike `moduleInitialize()` (which really is gated to run only when `!OL_initialized`, i.e. genuinely once per fresh instance or patch/preset reload). `OrangeLineCommon.hpp`'s `initialize()` calls `moduleCustomInitialize()` unconditionally as its very first line, every single tick. Using it to set a one-time default for an `OL_state`/JSON-backed value (e.g. a right-click menu's persisted setting) silently re-stamps that default every ~1ms, so any user change made via a menu item appears to do nothing — the checkmark/value snaps right back before it's even noticeable. Fix: set genuine one-time defaults directly in the module's own constructor, *after* `initializeInstance()` — the constructor runs exactly once, before either `dataToJson()` (fresh module) or `dataFromJson()` (loaded module/patch) is ever called, so a saved value still correctly overrides the default afterward. Reserve `moduleCustomInitialize()` for things that genuinely need reasserting every tick (same category as `setOutPolyChannels()` above). First hit building X8's per-Expander "Channels" menu.
- **Rack's own SVG loader (NanoSVG) does not support `<clipPath>` at all** — verified directly in the vendored `nanosvg.h` (zero occurrences of "clip" anywhere in the file). A hand-authored panel/widget SVG that relies on `clipPath` to mask overflowing artwork (e.g. shading that pokes past a rounded-corner silhouette) will render correctly in Inkscape or a browser but silently show the *unclipped* content once loaded through `APP->window->loadSvg()` in Rack itself — there's no error, no warning, the element is just ignored. Don't try to work around this with `nvgScissor()` either unless a plain axis-aligned rectangle genuinely covers the overflow — `nvgScissor()` can't do rounded corners, so it only helps if the overflow reaches the widget's outer bounding box, not just past an inner rounded silhouette. The robust fix for "hide whatever pokes out past a rounded shape" is a same-color mask shape (solid fill with a same-shaped hole, standard `nvgPathWinding(NVG_HOLE)` donut technique) drawn on top in NanoVG, or — simpler still — author the source artwork so nothing overflows its own rounded silhouette in the first place, so no masking is needed at all. First hit building X8's Toggle/Click/Push value-button cap.
- **A candidate input read via a Host/Expander binding (X-family) can carry values in a completely different unit convention than what the Host's own reader function assumes.** Morpheus's `getChannelRnd()`/`getChannelRec()`/`getChannelClr()`/`getChannelHld()` all compared the raw candidate value against `> 5.f`, matching a real cable's 0..10V gate convention — but a bound X8 Expander's own knob/button range is `configParam(KNOB_PARAM+i, 0.f, 1.f, ...)`, i.e. 0..1, whose maximum (1.0) can never cross a 5.0 threshold. The candidate would resolve as "connected" (green, correctly bound) yet never actually register as engaged, with no error anywhere — it just silently never fired. `LOOP_LEN` happened to escape this because its own scale (`raw*100`) already assumed a 0..1-ish raw range, not a full 0..10V swing, so it's easy to test one working candidate and assume the pattern is fine for all of them. Fix candidate-input thresholds to a value that correctly distinguishes on/off for *both* conventions (e.g. `0.5f` instead of `5.f` — a real gate's usual 0V/10V swing clears 0.5 by just as wide a margin as it clears 5), and audit every reader function a new Expander-compatible candidate touches, not just the first one tested.

## Right-click context menu: submenu/radio-style item, step by step

Every module's plain top-level items (e.g. the Style Orange/Bright/Dark switch) already follow
the `<Name>StyleItem : MenuItem` pattern described in "Widget/panel conventions" above. A
**submenu with a radio-style checkmark list** (e.g. Morpheus's own "Poly Channels", X8's
"Channels") is a second, slightly more involved shape that has bitten twice already — follow
this exact sequence, don't reinvent it:

1. **Nest the leaf-item struct inside the submenu-parent struct** (matches Morpheus's
   `PolyChannelsItem`/`PolyChannelItem` and X8's `X8ChannelsItem`/`X8ChannelItem`):
   ```cpp
   struct XChannelsItem : MenuItem {
       X *module;
       struct XChannelItem : MenuItem {
           X *module;
           int channels;
           void onAction(const event::Action &e) override {
               module->OL_setOutState(CHANNEL_LIMIT_JSON, float(channels));
           }
           void step() override {
               if (module)
                   rightText = (module->OL_state[CHANNEL_LIMIT_JSON] == channels) ? "✔" : "";
           }
       };
       Menu *createChildMenu() override {
           Menu *menu = new Menu;
           for (int channel = 1; channel <= N; channel++) {
               XChannelItem *item = new XChannelItem();
               item->module = module;
               item->channels = channel;
               item->text = module->channelNumbers[channel - 1]; // shared "1".."16" array,
                                                                   // already on every module
                                                                   // via OrangeLineCommon.hpp
               item->setSize(Vec(50, 20));   // <-- do not skip, see step 2
               menu->addChild(item);
           }
           return menu;
       }
   };
   ```
2. **Every leaf item's `setSize()` must be called explicitly** - omitting it produced a
   right-click submenu that opened as a completely empty shadow/box (X8's first "Channels"
   attempt): the items exist and their `onAction`/`step()` are wired correctly, they just render
   at zero size, so nothing is visible or clickable. `Vec(50, 20)` (Dejavu) / `Vec(70, 20)`
   (Morpheus) are the established sizes to match - pick one and stay consistent within a module.
3. **In `appendContextMenu()`**, add the parent item with `rightText = RIGHT_ARROW` (not the
   checkmark logic - that only lives on the leaf items) so it visually reads as "opens a
   submenu":
   ```cpp
   XChannelsItem *channelsItem = new XChannelsItem();
   channelsItem->module = module;
   channelsItem->text = "Channels";
   channelsItem->rightText = RIGHT_ARROW;
   menu->addChild(channelsItem);
   ```
4. **The backing value's one-time default belongs in the module's own constructor, never in
   `moduleCustomInitialize()`** - see the Pitfalls entry above (`moduleCustomInitialize()` runs
   every tick, not once; a default set there re-stamps itself before a user's menu click can
   ever be observed, so the checkmark appears permanently stuck).

## Workflow: building a new module step by step

This is the actual sequence used building **J** (2026-07-15, the first module built this way
start to finish) - repeat it for the next new module. Panel art comes *before* the C++ (the
widget's jack/knob positions are measured from the finished panel, not the other way round) -
see `PanelDesignGuide.md`'s own numbered Workflow section for that half of the process first.

1. **Pin down the exact behavior before writing any code.** Simple-sounding specs hide real
   ambiguity - e.g. J's "AND of connected inputs, fixed-length output pulse" needed one crucial
   clarifying question (edge-triggered fixed pulse vs. continuous level-following capped at max
   length - these are different modules) before implementation could start. Ask rather than
   assume whenever a design choice would produce genuinely different behavior.
2. **Find the closest existing module as a structural reference** rather than inventing the
   module shape from scratch. Two different existing modules can each supply a different piece:
   J used `D2D.cpp` for the overall minimal hook shape (a small, modern, throttled module with
   no legacy cruft) and `Gator.hpp`/`Gator.cpp` for the `LEN_PARAM` gate-length-knob pattern
   (`configParam(LEN_PARAM, min, max, default, "Gate Length", " ms")` + `getStateParam
   (LEN_PARAM)`).
3. **Write `<Name>.hpp`**: the four/five enums (`jsonIds`/`ParamIds`/`InputIds`/`OutputIds`/
   `LightIds`), any `#define`s, `#define OL_TOUCH_DISABLED` before the first `#include` if the
   module doesn't need Wakeup/Ready (see the Touch section above for which modules do).
4. **Write `<Name>.cpp`**: the module struct (`#include "OrangeLineCommon.hpp"` first, then the
   required hooks - `moduleSkipProcess`, `moduleInitStateTypes`, `moduleInitJsonConfig`,
   `moduleParamConfig`, `moduleCustomInitialize`, `moduleInitialize`, `moduleReset`,
   `moduleProcess`, `moduleProcessState`, `moduleReflectChanges`), then `<Name>Widget :
   ModuleWidget` using the exact jack/knob coordinates from the finished panel's `Controls`
   layer, then `Model *model<Name> = createModel<<Name>, <Name>Widget>("<Name>");`.
   - **`<Name>JsonLabels.hpp` is optional, not required** - only the older/more complex modules
     (CVLanes, Hold, LanesCV, LanesMidi, MidiBus) use a separate `char *jsonLabel[NUM_JSONS]`
     array file. Simpler modern modules (D2D, K2C, CC14, J) just call `setJsonLabel(id, "name")`
     directly inside `moduleInitJsonConfig()` - no separate file needed. Prefer this simpler
     form for a new module unless it has a genuinely large indexed JSON range.
   - A custom fixed-duration output pulse (not the standard hardcoded-1ms `STATE_TYPE_TRIGGER`
     convention) needs its own `dsp::PulseGenerator` member, triggered manually in
     `moduleProcess()` with whatever duration the module computes (see J's
     `outPulse.trigger(getStateParam(LEN_PARAM) / 1000.f)`) - same pattern the Touch mechanism
     itself uses for `OL_touchOutPulse`.
5. **Register the module**: `src/plugin.hpp` (`extern Model *model<Name>;`), `src/plugin.cpp`
   (`p->addModel(model<Name>);`), `plugin.json` (a `{ "slug", "name", "description", "tags" }`
   entry - validate the file is still well-formed JSON after editing, e.g. `python3 -c "import
   json; json.load(open('plugin.json'))"` - this repo has hit a missing-comma bug here before).
6. **Compile-check each new/changed `.cpp` individually** via the direct-`g++` sandbox
   workaround (see Build section above) before attempting a full link - catches syntax/type
   errors immediately without waiting on a full plugin relink. Leave the actual full link/build
   to Dieter when he wants to do it himself in a real MSYS2 terminal and test-load the result in
   Rack - don't run it unprompted once told "I'll do it".

## Adding a new module — file checklist

1. `src/<Name>.hpp` — enums + any `#define`s for repeated-I/O counts.
2. `src/<Name>JsonLabels.hpp` — optional (see Workflow step 4 above); only needed for large
   indexed JSON ranges, most new modules just call `setJsonLabel()` in `moduleInitJsonConfig()`.
3. `src/<Name>.cpp` — the module struct (see architecture above) + `<Name>Widget : ModuleWidget` + `Model *model<Name> = createModel<<Name>, <Name>Widget>("<Name>");` at the end.
4. `src/plugin.hpp` — add `extern Model *model<Name>;`.
5. `src/plugin.cpp` — add `p->addModel(model<Name>);`.
6. `plugin.json` — add a `{ "slug", "name", "description", "tags" }` entry to the `modules` array (watch the comma after the previous entry — this repo has hit a missing-comma JSON bug here before).
7. `res/<Name>Orange/Bright/Dark.svg` (+ optionally `<Name>Work.svg`) — panel art; can start as a plain placeholder and be replaced later without touching the C++.

No Makefile changes are ever needed — `src/*.cpp` is globbed automatically.

## Infix pattern (RX output / TX input pairs)

Used by RECALL and MidiBus (and worth reusing for any future module that sits inline in a signal path and wants to let a patch tap or override individual signals without being forced to). For every carried signal, expose two ports: an **RX output** (a read tap of what was just received/would otherwise flow through) and a **TX input** (what actually gets sent onward — if left unpatched, falls back to the RX value automatically). At rest (nothing patched into any TX input) the module is a pure 1:1 passthrough; patching something into a TX input turns that one signal into a processing insert, without needing to touch or reconfigure anything else. MidiBus's bus-stop metaphor (Dieter): "wenn keiner wartet fährt der Bus durch, wenn man einsteigen will baut man ein Infix" — the bus drives straight through if nobody's waiting; to get on, you build an infix.

## Expander modules (Hub + Expander pattern)

Use this pattern when several modules need to share state where one module collects/produces it and any number of others consume it in different forms — e.g. the LANES family: **`CVLanes`**/**`MidiLanes`** are Hubs (CV-input and MIDI-input variants, same role), **`LanesCV`**/**`LanesMidi`** are Expanders that read a Hub's state and turn it into CV jacks or MIDI. Treat this family as the canonical reference implementation — copy its file shapes verbatim for a new Hub/Expander family rather than re-deriving the pattern from scratch.

**Core principle**: any merging/voice-stealing/allocation-*capacity* decision belongs to the **consumer** (each Expander), never the Hub. A Hub just collects and normalizes raw state; it must not decide how many simultaneous "things" downstream can represent, because that's inherently a property of the consumer (Rack's own 16-channel poly limit for a CV expander, a MIDI device's own capabilities for a MIDI expander, etc — see `LanesVoiceAllocator.hpp`, a `template <int CAPACITY>` allocator each Expander instantiates independently with its own capacity, run against the Hub's raw per-source state).

- **`src/<Family>Shared.hpp`** — a header **separate from any module's own `<Name>.hpp`**, included by every family member (Hubs and Expanders alike). It holds only:
  - Shared `#define`s used by more than one family member (e.g. `NUM_SOURCES`/`NUM_LANES`).
  - `struct <Family>HubInterface` — pure-virtual, declares only what a Hub exposes (raw, *unmerged* per-source state — e.g. `getSourceGate/Pitch/Velocity/Lane(source, channel)`). Every Hub type implements this.
  - `struct <Family>ExpanderInterface` — pure-virtual, declares `getLanesHub()` (the Hub pointer this expander itself resolved) and `getLanesHubAmbiguous()` (see bidirectional resolution below). Every Expander type implements this.
  - `resolveLanesHub(Module *neighbor)` and `classifyLanesNeighborForHub(Module *neighbor, <Family>HubInterface *self)` helpers (below).

  This must be a **separate header from each module's own `<Name>.hpp`** (which still defines that module's own `jsonIds`/`ParamIds`/`InputIds`/`OutputIds`/`LightIds` enums as usual) — if the shared interfaces lived inside e.g. the Hub's own header, every Expander's `.hpp` including it would pull in the Hub's own enums a second time and collide. Each family member's own `<Name>.hpp` includes `<Family>Shared.hpp` (for the interfaces/shared constants) *and* declares its own enums as normal.

- **Why `dynamic_cast`, not `model == modelX` + `reinterpret_cast`**: a plain type check via `dynamic_cast<X*>(neighbor)` lets any *future* Hub or Expander type join the family automatically just by implementing the right interface — no central registry of "known models" to keep in sync, and no risk of a stale `reinterpret_cast` reading the wrong memory layout if a struct changes shape.

- **Chain-walk / resolution** (`resolveLanesHub`, in the shared header):
  ```cpp
  inline LanesHubInterface* resolveLanesHub(Module *neighbor) {
      if (!neighbor) return nullptr;
      LanesHubInterface *hub = dynamic_cast<LanesHubInterface*>(neighbor);
      if (hub) return hub;
      LanesExpanderInterface *link = dynamic_cast<LanesExpanderInterface*>(neighbor);
      if (link) return link->getLanesHub();
      return nullptr;
  }
  ```
  Every Expander calls this **on both `leftExpander.module` and `rightExpander.module`, every control-rate tick** (the Rack neighborhood can change any time) — a Hub is one instance, but there's no reason an Expander can't sit on either side of it, or be reached through a chain of other Expanders in between:
  ```cpp
  LanesHubInterface *hubLeft  = resolveLanesHub(leftExpander.module);
  LanesHubInterface *hubRight = resolveLanesHub(rightExpander.module);
  lanesHub = hubLeft ? hubLeft : hubRight;   // prefer left, arbitrary but deterministic
  ```

- **Pitfall — comparing "found via multiple paths" must compare *identity*, not just non-null count.** In a plain healthy chain `Hub | ExpanderA | ExpanderB`, `ExpanderA` legitimately resolves the *same* Hub twice: directly via its left side, and indirectly via its right side (because `ExpanderB` resolves back to that same Hub through *its* left side, which is `ExpanderA`). A naive ambiguity check like `hubLeft && hubRight` treats this healthy, ordinary case as a conflict. The correct check compares pointer identity:
  ```cpp
  bool hubConflict = hubLeft && hubRight && hubLeft != hubRight;
  ```
  Only genuinely *different* Hub instances reachable on each side (e.g. `HubA | Expander | HubB`) are an actual conflict. This bit the LANES family once already — reflexive lookups (A resolves through B, B resolves through A) can make the identical answer arrive from more than one direction without anything being wrong.

- **Connection-status corner lights** — every family member (Hub or Expander) gets two tiny bi-color lights, one per side, so a glance at the panel shows whether the whole chain is healthy:
  - Component: `TinyLight<GreenRedLight>` (`GreenRedLight` reads 2 consecutive light IDs: green then red — mix both for yellow). Declare as `LEFT_CONN_LIGHT, LEFT_CONN_LIGHT_LAST = LEFT_CONN_LIGHT + 1, RIGHT_CONN_LIGHT, RIGHT_CONN_LIGHT_LAST = RIGHT_CONN_LIGHT + 1` in `LightIds`. Placeholder position (until real panel art defines one): `calculateCoordinates(3.5f, 4.f, 0.f)` near the top-left corner, mirrored near the top-right (panel-width minus ~3.5-4mm) — small margin from the panel edge, safely above the first control row.
  - **Expander** meaning (uses the `hubLeft`/`hubRight` from the resolution above): off (nothing connected on that specific side) / **green** (exactly one Hub reachable, healthy) / **yellow** (something connected, but no Hub reachable through *either* side) / **red** (`hubConflict`, see above — two different Hubs reachable). The yellow/green/red judgement is shared by both lights (it reflects the whole module's chain health); only the off/lit distinction is actually per-side.
  - **Hub** meaning — deliberately simpler, **no yellow**, since a Hub doesn't need to "find" anything, it's complete by itself: off (nothing recognized on that side) / **green** (a normal Expander unambiguously serving this Hub) / **red** (another Hub directly adjacent, or an Expander that's ambiguous or is actually serving a *different* Hub reachable through its far side). Use `classifyLanesNeighborForHub(Module *neighbor, LanesHubInterface *self)`:
    ```cpp
    inline LanesNeighborKind classifyLanesNeighborForHub(Module *neighbor, LanesHubInterface *self) {
        if (!neighbor) return LANES_NEIGHBOR_NONE;
        if (dynamic_cast<LanesHubInterface*>(neighbor)) return LANES_NEIGHBOR_CONFLICT;
        LanesExpanderInterface *link = dynamic_cast<LanesExpanderInterface*>(neighbor);
        if (!link) return LANES_NEIGHBOR_NONE;
        if (link->getLanesHubAmbiguous()) return LANES_NEIGHBOR_CONFLICT;
        LanesHubInterface *theirHub = link->getLanesHub();
        if (theirHub && theirHub != self) return LANES_NEIGHBOR_CONFLICT;
        return LANES_NEIGHBOR_OK;
    }
    ```
    This is what makes `HubA | Expander | HubB` light up red on the Expander-facing side of *both* Hubs, not just on the Expander itself — a Hub can't tell this just from its immediate neighbor's *type* (the immediate neighbor is a perfectly normal Expander), it has to ask that Expander whether it's `getLanesHubAmbiguous()` or serving someone else.

- **Seam-bridging "Ext" strip** — a thin full-height sliver, drawn flat-color (no SVG asset) in
  `draw()`, matching that theme's panel background plus a continuation of the panel's own accent
  line, drawn right at a module's own left/right edge. Purpose: two touching same-themed family
  members read as one continuous panel instead of two separate ones with a visible gap/border
  between them.
  - **Rack DOES clip a widget's rendering to its own `ModuleWidget`'s bounds** (confirmed live,
    2026-07-16, correcting an earlier wrong assumption in this file that it didn't) — a widget
    positioned or sized past its own module's edge does NOT reach across the seam into the
    neighbor's rendered surface; that portion is simply clipped away and never drawn at all.
  - **Consequence: there is no such thing as a strip "reaching into" or "covering" the neighbor.**
    The illusion of a continuous panel only works because each module fills its own strip flush
    to its own edge, using up the space right up to (never past) its own boundary — when two
    same-themed modules sit flush against each other, both edges independently show matching
    background/accent color right at the boundary, and the seam disappears purely because both
    sides look identical there, not because either one draws over the gap.
  - **Both sides of every seam always need their own strip — this is not optional insurance, it's
    the only way it can work at all**, precisely *because* of the clipping above: neither module
    can compensate for the other's missing strip, since neither can paint outside its own bounds.
    LANES already does this correctly (`LanesShared.hpp`'s `LanesExtStripWidget`/
    `addLanesExtStrips()`/`updateLanesExtStrips()` give every Hub/Expander both a `left` and a
    `right` strip). The X-family's `XShared.hpp` version (`XExtStripWidget`/`addXExtStrip()`/
    `updateXExtStrip()`) originally gave only the Expander a right-edge strip, reasoning that the
    attachment direction is architecturally fixed so only one "owner" edge should be needed — that
    reasoning was wrong given clipping, and Dieter is reworking the X-family strip geometry
    directly as of this writing (2026-07-16) to fix it; check `XShared.hpp`'s current state rather
    than assuming any specific constant/geometry described in an earlier version of this file.

- **New Expander/Hub family checklist**:
  1. `src/<Family>Shared.hpp` — shared constants + the two interfaces + `resolveLanesHub()`/`classifyLanesNeighborForHub()` (rename the `Lanes`-prefixed identifiers to your family's name).
  2. `src/<Family>VoiceAllocator.hpp` (if the family involves merging/polyphony at all) — the reusable `template <int CAPACITY>` allocator, one instance per Expander.
  3. Each Hub: `struct <Name> : Module, <Family>HubInterface`, implements the raw-state getters, plus the two connection lights using `classifyLanesNeighborForHub()`.
  4. Each Expander: `struct <Name> : Module, <Family>ExpanderInterface`, implements `getLanesHub()`/`getLanesHubAmbiguous()`, resolves both sides every tick, plus the two connection lights using the green/yellow/red logic above.
  5. The seam-bridging Ext strip (above) — every member needs its own strip on every edge that
     can ever face a same-family neighbor, full stop; a fixed attachment direction (only one type
     of neighbor possible on a given edge, like X-family's Host-is-always-rightward rule) changes
     which edges need a strip at all, never whether both sides of an actual seam need one.
  6. Usual per-module checklist (above) for each Hub/Expander's own `.hpp`/`.cpp`/`JsonLabels.hpp`/`plugin.hpp`/`plugin.cpp`/`plugin.json`/panel files.

- **Near-identical sibling modules (e.g. X8/X8D, and eventually X16/X16D) — share code via two
  headers, don't copy-paste.** X8D started as a literal copy of X8 (different panel width, extra
  per-channel display) with every identifier renamed to avoid ODR clashes - by the time X8D grew
  its own numeric-display/tooltip work, ~95% of the two files was byte-identical logic under
  different names, which Dieter flagged directly ("we have two modules with about 95% identical
  code, we should not duplicate this much"). Fixed by splitting into:
  - **`<Family>Common.hpp`** — every widget/control class that doesn't hardcode a size/position
    (e.g. `X8Knob`, `X8ValueButton`, `X8NameDisplay`, the custom `ParamQuantity`, any shared free
    function like `x8BrowsedParamTaken()`). These must operate on a plain `Module*` +
    `dynamic_cast<FamilyExpanderInterface*>`, never on the concrete sibling type (`X8*`/`X8D*`) -
    which usually means promoting a few more accessors onto the family's own `ExpanderInterface`
    (in `<Family>Shared.hpp`) so shared code never needs to know which sibling it's attached to.
    Reused verbatim (`#include`'d at file scope) by every sibling's own `.cpp`.
  - **`<Family>ModuleCommon.hpp`** — the module struct's entire body *except the constructor*
    (`#include`'d inside each sibling's own `struct <Name> : Module, <Family>ExpanderInterface {
    ... }`, same composition pattern as `OrangeLineCommon.hpp` itself). The constructor can't be
    shared this way - a C++ constructor's name must exactly match its enclosing class - so it's
    the one deliberate, small, per-file duplication (a few lines, otherwise identical).
  - If the siblings' enums are byte-identical except a macro name (e.g. `NUM_X8_KNOBS` vs.
    `NUM_X8D_KNOBS`), unify the macro name and delete the redundant `.hpp` entirely - X8D.cpp just
    `#include`s X8's own `X8.hpp` directly rather than keeping a near-empty `X8D.hpp` around.
  - What does **NOT** get shared: the constructor (see above); any widget class that hardcodes a
    size/position that genuinely differs per sibling (e.g. X8D's wider `ENGAGE` button) - nest
    these small sized subclasses *inside* each sibling's own `Widget` struct (not file scope), so
    a same-named-but-differently-sized class in the other sibling's `.cpp` can never collide
    across translation units; and anything one sibling has that the other structurally lacks
    (X8D's per-channel numeric display column has no X8 equivalent at all).
