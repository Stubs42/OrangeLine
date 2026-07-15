# Param-Access Expander Family (X8 / X16) — Design Spec

Status: **conceptually specified, 2026-07-15, not yet implemented.** First planned use case is
Morpheus (many poly params, too many to give each one a front-panel jack/knob without
overcrowding the panel), but the mechanism is designed to be **generic** - any future module
with poly param-inputs can adopt it the same way any Hub can join the LANES family.

## Motivation

A module can have many *polyphonic* inputs that each pair with a knob/button (CV-modulation of
a parameter). A poly CV input is only really valuable when it lets each channel take a
*different* value - something the knob alone can never do (a knob is inherently one value for
all channels). But giving every such param its own always-present poly jack on the main panel
doesn't scale once a module has more than a handful of them (Morpheus has ~15).

**Candidate criterion**: an input qualifies for this mechanism if and only if it (a) is
polyphonic and (b) pairs with a param (knob/button). Plain "signal" inputs with no param
counterpart (e.g. Morpheus's `CLK_INPUT`) are never candidates.

## Roles

- **Host module** (e.g. Morpheus): for every candidate param, owns
  - the real per-channel value buffer (always present, initialized to default values, fully
    persisted in the host's own JSON - not lazily created when an expander first touches it),
  - a per-param **engaged flag** (see "Red/Green" below) controlling whether the host's actual
    output for that param comes from the buffer or from its own single front-panel knob,
  - the right-click Reset/Initialize submenu (see below).
- **Expander module** (X8 / X16): a generic, reusable "window" onto a host's per-channel
  buffers - no CV sockets at all (deliberately - a socket here would be over-engineering), just
  a parameter-select mechanism, an engage toggle, and physical per-channel value controls.

## Architecture: left-only attachment, single unidirectional chain

An Expander is only ever recognized attached to a Host's **left** side - because these are
*inputs*, and the panel's visual signal flow runs left to right, so control-from-outside
belongs upstream/left of the module it feeds. Dieter: "solche Expander werden ausschliesslich
links vom Modul akzeptiert... damit gibt es immer max 1 Host."

This is a deliberate simplification versus the LANES family's bidirectional Hub+Expander
resolution: because a Host is only ever reachable by looking **right** from an Expander (never
left), the whole graph is a strict singly-linked chain - there is no possible fork, so **no
conflict/ambiguity detection is needed at all** (no `LANES_NEIGHBOR_CONFLICT`-style case can
ever arise here). An Expander resolves its Host by checking `rightExpander.module`: if it's an
`XHostInterface`, that's the Host; if it's another `XExpanderInterface`, relay through its
already-resolved Host pointer (so several Expanders can still chain further left of one Host).
The Host itself only ever needs to check `leftExpander.module` to know whether at least one
Expander is currently present at all.

Consequence for the connection-status indicator: **no dedicated light needed at all** (unlike
LANES' two bi-color corner lights). Normally a header light would show connection state, but
here it's already conveyed for free by the panel's own controls: LEFT/RIGHT/ENGAGE all render
grey together when no Host is resolved, and the name display itself can also show a
"not connected" state (e.g. blank/dashes) - that's the "not connected" indicator, so a separate
light would be redundant.

## Architecture: Expander = a virtual poly cable on the Host's own input (decided 2026-07-15)

Dieter's framing, and the deciding principle for everything below: "aus meiner Sicht ideal wäre
wenn die Xander einfach das wären was sie in der realen Welt auch sind - additional interfaces
des Hosts" -> "aus Sicht des Hosts sind Expander connected cables auf dem In-Port mit 8 oder 16
Channels." An Expander is not an independent module with its own decision logic - it's
conceptually a **virtual poly cable**, plugged into the same input a real cable would use, just
carrying 8 (X8/X8D) or 16 (X16/X16D) channels instead of whatever a real cable happens to carry.
This is not a new concept bolted on - it's the existing "Real-cable override" and "Channels"
behavior (both already speced further below) taken as the *primary* model for the whole
mechanism, not just for the channel-limit feature.

- **The Expander manages itself completely**: its own knob positions, its own button
  debounce/edge-detection (a real hardware control surface's firmware would already do this
  locally - it reports a clean press event upward, not a raw level for someone else to
  edge-detect). It exposes this self-managed state as one flat 8/16-channel array plus two small
  extras (which param it's currently pointed at, and a one-shot "I was just pressed" flag) - see
  Expander Interface below. It never calls anything on the Host.
- **Engaging = plugging the virtual cable in. The Host's Reset = unplugging it.** These are the
  only two moments anything about the binding changes. In between, the Host just reads the
  currently-plugged-in Expander's array every tick, exactly like it would read a real poly
  cable's per-channel voltages.
- **The one piece of state this actually requires the Host to remember** (Dieter was right to
  push on this, see the design discussion this section replaces): a small per-param binding,
  *which* Expander instance currently holds the plug - `Module* xBoundExpander[N]` in the Host's
  own implementation (`N` = `getXParamCount()`), set the moment a param goes Red -> Green, held
  until Reset (or until that pointer stops resolving as an `XExpanderInterface`, checked the same
  defensive way LANES already re-resolves its own neighbors every tick). This is deliberately
  *not* a general "directory of all connected X modules" - just one pointer per candidate param.
- **Consequence: an Expander physically can't be re-wired while its own plug is inserted.** It
  only has one set of 8/16 knobs; if it browsed away (LEFT/RIGHT) to a different param while still
  bound, those same knobs would suddenly mean something else and the "cable" would be silently
  carrying garbage. So while an Expander is the bound provider for a param, its own LEFT/RIGHT is
  locked - exactly like not being able to rewire a real patch cable without unplugging it first.
  This is really just the existing "engage button is inactive while already Green" rule (see
  Red/Green below) extended to LEFT/RIGHT as well, not a new rule.
- **This is also why no separate tie-break/"who's touched" mechanism is needed** for two
  Expanders both simply *browsing* to the same param: browsing alone never binds anything, and a
  second Expander's engage-press on an already-Green param is a no-op (per the existing
  Red-only-goes-one-way rule) - there is only ever at most one bound provider per param, decided
  once, at the moment its plug went in, full stop.
- **No motorized-knob sync.** Even the bound Expander's knob position is never moved by the Host
  - a real knob can't move itself. It simply always shows/holds its own last physical position; the
  Host starts reading whatever position it finds the instant the plug goes in. Same "value jump
  on first touch" tradeoff real (non-motorized) hardware controllers make - accepted here
  deliberately, KISS. A *non-bound* Expander that's merely browsing to an already-engaged param
  should mirror the Host's actual live buffer value on its display instead of its own dangling
  knob position, so it visibly reads as "not the source right now."
- Consequence: **an Expander's own `moduleProcess()` stays almost a no-op** - resolve the Host
  pointer, update the seamless-strip/connection state, done. All decision logic (binding,
  Red/Green, buffer writes) lives on the Host side only.
- This also **fully resolves the earlier "engage must initialize every channel" concern**: there
  is no snapshot/transfer moment at all - the Host just reads the bound Expander's live array
  fresh every tick from the moment the plug goes in, nothing to half-initialize.

## Expander Interface (new, pull-side - the Host reads these, the Expander never calls out)

```cpp
struct XExpanderInterface
{
    virtual XHostInterface* getXHost() = 0;
    virtual float getXStyle() = 0;

    // Fully self-managed by the Expander (own debounce/edge-detection etc) -
    // the Host only ever reads these, during its OWN process(). The Expander
    // never calls back into the Host with them.
    virtual int getXKnobCount() = 0;              // 8 (X8/X8D) or 16 (X16/X16D)
    virtual float getXKnobValue(int channel) = 0; // raw current physical position
    virtual int getXBrowseIndex() = 0;             // which host param this Expander's
                                                    // controls currently point at (local
                                                    // UI state owned by the Expander, see
                                                    // "Expander panel" below) - locked while
                                                    // this instance holds the plug, see above
    virtual bool consumeEngagePress() = 0;         // one-shot: true exactly once per
                                                    // physical click, debounced locally

    virtual ~XExpanderInterface() {}
};
```

## Host Interface (dynamic_cast-based, same style as the LANES family's HubInterface)

```cpp
enum XParamType {
    X_PARAM_CONTINUOUS,
    X_PARAM_TOGGLE,      // click flips state, stays until clicked again
    X_PARAM_CLICK,       // single fixed-length pulse fired on click, independent of hold duration
    X_PARAM_MOMENTARY    // buffer value is high only while the control is actively held down
};

struct XHostInterface {
    virtual int getXParamCount() = 0;
    virtual const char* getXParamName(int index) = 0;
    virtual XParamType getXParamType(int index) = 0;
    virtual NVGcolor getXParamColor(int index) = 0;

    // Red/Green: does the host's actual output for this param come from the
    // per-channel buffer (true) or from the host's own single knob (false)?
    virtual bool isXParamEngaged(int index) = 0;
    virtual void setXParamEngaged(int index, bool engaged) = 0;

    // True while a real cable is patched into the host's own poly CV jack for
    // this param - overrides everything else, see "Real-cable override" below.
    virtual bool isXParamCableConnected(int index) = 0;

    // 1-16, default 16 - see "Host right-click menu" below.
    virtual int getXParamChannelLimit(int index) = 0;
    virtual void setXParamChannelLimit(int index, int limit) = 0;

    virtual float getXParamChannelValue(int index, int channel) = 0;
    virtual void setXParamChannelValue(int index, int channel, float value) = 0;

    // Host-side right-click actions (see "Host right-click menu" below).
    virtual void resetXParam(int index) = 0;       // -> disengage (Red), buffer untouched
    virtual void initializeXParam(int index) = 0;  // buffer -> its own default values

    virtual std::string formatXParamValue(int index, float value) = 0; // continuous only

    virtual ~XHostInterface() {}
};
```

Persistence: the host's own `dataToJson()`/`dataFromJson()` (via the existing
`moduleExtraDataToJson`/`FromJson` optional-hook pattern) must save/restore, per candidate
param, the per-channel buffer values, the engaged flag, **and** the channel limit - this is all
real host state, none of it lives on the expander at all. The `xBoundExpander[N]` pointer array
(see the virtual-cable architecture section above) is **not** persisted - it's a live-session-only
binding to a specific `Module*`, meaningless across a save/reload; on load, every param simply
starts unbound (even if still engaged/Green - it just has no live source until some Expander
re-engages it, exactly like a patch missing a cable that used to be there).

## Red/Green - meaning lives on the host, not the expander

This is **not** "is an expander currently browsing this param" - it's whether the **host**
currently drives its output for that param from the per-channel buffer or from its own single
knob:

- **Red**: `isXParamEngaged() == false` - the host ignores the per-channel buffer entirely
  and drives all channels uniformly from its own front-panel knob, exactly as it does today
  with no expander ever attached.
- **Green**: `isXParamEngaged() == true` - the host drives each channel's output from that
  channel's slot in the buffer.

An Expander's engage button is **one-way**: it can only drive Red -> Green (the Host, on seeing
that Expander's `consumeEngagePress()` fire while browsing an unbound, non-Red-locked param,
calls its own `setXParamEngaged(index, true)` and records that Expander as `xBoundExpander[index]`
- see the virtual-cable architecture section above), never the reverse. Disengaging (Green ->
Red) is only possible via the host's own right-click "Reset" action (see the nested "Expanders"
submenu below), which also clears the binding - deliberately KISS, not a two-way toggle. Because
the flag (and the binding) are owned by the host, a second Expander merely browsing to an
already-engaged param sees the same Green state and its own engage-press is simply a no-op -
there is nothing to keep in sync, because there is only ever one bound source. While engaged,
the *bound* Expander's engage button and its LEFT/RIGHT browsing are both inactive/locked (see
above) until the host's Reset brings the param back to Red and clears the binding.

Because the buffer is always present and persisted (not created/populated only at the moment of
engaging), a param retains its last real values across a disengage/re-engage or a save/reload -
no separate "snap to current values" step is needed.

## Real-cable override

If a real cable is patched into the host's own poly CV jack for a given param, that jack wins
outright: the param's entry on every Expander shows its name **greyed out / disabled**
regardless of the engaged flag or anything else, since the real cable already provides live
per-channel CV directly and letting the buffer fight it would be ambiguous. Query via
`isXParamCableConnected()`.

## Host right-click menu: nested "Expanders" submenu

The host's `appendContextMenu()` gets one generic **"Expanders"** top-level submenu (built by
shared Expander infrastructure iterating `getXParamCount()`), one level per step:

```
Expanders
  └─ <Param Name>            (one entry per candidate param, e.g. "Lock", "Balance", ...)
       ├─ Channels           (submenu, radio-style: "1".."16", default 16 - see below)
       ├─ Reset
       └─ Initialize
```

- **Channels**: `setXParamChannelLimit(index, n)`, 1-16, default 16. Semantics are
  deliberately identical to **a real patched cable that only delivers a defined number of poly
  channels** - channels above the limit simply aren't there, exactly as if a real cable with
  that many channels were plugged into the host's own poly jack for this param. Since every
  candidate param is already polyphonic, the host already has to handle "a cable with fewer
  than 16 channels" as a matter of course - this feature reuses that exact same handling for
  the buffer, no new special-case logic needed. (Same idea, made concrete: **X8 itself** can
  only ever supply channels 1-8 for whatever param it's engaged on - a hardcoded instance of
  this exact same "fewer than 16 channels" behavior, just fixed by the Expander's own physical
  knob count instead of user-configured.)
- **Reset**: `resetXParam(index)` - sets the engaged flag back to Red (host goes back to
  driving all channels from its own knob). The buffer's contents are left untouched, so
  re-engaging later picks up right where it left off.
- **Initialize**: `initializeXParam(index)` - resets the buffer's own values back to their
  defaults ("back to default"), independent of the current engaged state.

On any Expander, knobs/controls for channels above the current limit are simply shown
disabled/greyed - no separate display widget needed, just the control's own visual state.

## Expander panel (X8 = 1 column of 8, X16 = 2 columns of 8, channels 1-8 left / 9-16 right)

- **Name display**: shows the currently browsed param's name, mirroring that param's host-side
  Red/Green engaged state (see above). Greyed out instead of red/green if
  `isXParamCableConnected()` is true for that param (see "Real-cable override"). When no Host
  is resolved at all, shows a grey placeholder (e.g. dashes) instead of a name - there's simply
  nothing to browse, and this doubles as the "not connected" indicator (see above).
- **Two step buttons** (prev/next, not a knob - simpler and more precise for pure list
  navigation than trying to land a rotary knob on an exact index). **Circular**: `next` at the
  last index wraps to `0`; `prev` at index `0` wraps to the last index. Needs its own small
  custom SVG component (not an existing component-library part). Browsing alone never changes
  any engaged state or buffer value - it's read-only navigation, **except** it's locked/disabled
  while this Expander instance is the bound provider (`xBoundExpander[index] == this`) for
  whatever param it's currently on - see the virtual-cable architecture section above.
- **One engage button**: a plain momentary click, self-debounced locally, exposed to the Host as
  `consumeEngagePress()` - the Expander itself has no idea whether this will actually do
  anything (the Host decides: no-op if the browsed param is already bound elsewhere or real-cable
  overridden). Disabled/no-op-looking while that param is grey (real cable connected) or while
  this instance already holds the plug (nothing left for another click to do).
- **8 or 16 value controls**, purely local Rack Params owned by the Expander itself - they hold
  whatever physical position the user last left them at, nothing more. The Host reads them
  (`getXKnobValue`) only while this Expander instance is the bound provider for the browsed
  param; while merely browsing an already-engaged, not-bound-here param, the control should
  instead mirror the Host's actual live buffer value on its display (see architecture section
  above) so it visibly reads as "not the source right now" rather than showing a stale dangling
  position.
- **Numeric display only for continuous-type params** - toggle/click/momentary controls show
  their state through their own visual appearance instead (see below), they need no separate
  digit readout.
- Each continuous-type value control needs a **custom `ParamQuantity`** (not a plain static
  `configParam()`) so its native VCV hover tooltip calls `formatXParamValue()` live, using
  whichever param is currently browsed - a static tooltip can't work here since the same
  physical control represents different params over time.
- **The physical control itself must morph appearance/behavior by type** (Dieter: "der
  Expander muss dann auch beim Umschalten sein Aussehen ändern können, aus Buttons werden dann
  Knobs und umgekehrt"). When the browsed param is **continuous**, each of the 8/16 controls
  renders and behaves as a knob (rotary drag). For the three non-continuous types they render
  and behave as buttons instead, differing only in click semantics:
  - **Toggle**: click flips between two states, stays until clicked again.
  - **Click**: fires a single fixed-length pulse into the buffer on click, regardless of how
    long the mouse is held down (this is what earlier drafts of this spec called "Trigger").
  - **Momentary**: buffer value is high only for as long as the mouse button is actually held
    down on the control, and drops the instant it's released - a live gate-follow, not a pulse.

  Button state is always shown via the button's own appearance (no numeric readout, see above).
  This is a custom composite `ParamWidget` per channel - not several overlaid widgets swapped by
  visibility, but one widget whose `draw()` and interaction handlers branch on the
  currently-browsed param's declared type.
- **Button visual design** (established while building X8's panel): a squarish button shape
  with rounded corners (small ~0.53mm radius), fill = that theme's DisplayFill color, stroke =
  `#ff6600` (the fixed orange accent, not the theme Frame color) - see `res/X8Work.svg`'s
  LEFT/RIGHT/ENGAGE groups for the concrete pattern. Same shape reused for the per-channel
  controls when they're in button mode (toggle/click/momentary).

## Lifecycle / state rules

- **No expander ever attached, or a param never engaged**: baseline behavior, unchanged from
  today - the host's own knob drives all channels uniformly (Red).
- **Engaging a param** (the currently-browsing Expander's engage click, Red -> Green): the host
  sets its own flag and records `xBoundExpander[index] = this Expander` - the virtual cable is
  now plugged in; that Expander's own LEFT/RIGHT locks (see above).
- **Re-targeting (browsing) an Expander that is *not* currently bound**: has no effect on any
  param's engaged flag, binding, or buffer at all - browsing alone is read-only navigation.
- **A second Expander browsing an already-engaged param it doesn't hold the plug for**: sees the
  same Green state and mirrors the Host's live buffer value on its own display; its own
  engage-press is a no-op, since there is only ever one bound provider per param.
- **Manual reset/initialize**: see the Host right-click submenu above - clears the binding
  (Reset) or resets the buffer's own default values (Initialize); the only way state changes
  without the bound Expander itself being touched.
- **Patch save/reload**: buffer values, engaged flags and channel limits are real host state,
  persisted in the host's own JSON (see Host Interface above) and survive a save/reload. The
  `xBoundExpander` binding itself is **not** persisted (see Host Interface above) - every param
  loads unbound, picked back up the next time some Expander engages it.

## Four fixed panel variants (no resizing - deliberately KISS)

An earlier draft of this spec explored a drag-to-resize mechanism (studied via David
O'Rourke's SubmarineFree plugin's `SizeableModuleWidget`/`ResizeHandle`) where per-channel
numeric displays would appear/scale once the panel was widened past a threshold. **Dieter
explicitly scrapped this**: "kein resize window firlefanz" - instead there are simply **four
separate, fixed-width modules**, built collaboratively panel-first (see
[[project-x-expander-family]] for the build history):

- **X8** (3HP, 15.24mm): 8 channel knobs, single column, no per-channel numeric displays -
  value only visible via hover tooltip.
- **X8D** (6HP, 30.48mm): same as X8, but each knob gets its own per-channel numeric display to
  its right (green/orange LCD-style box + a short connector line from the knob ring to the
  display box). Built by doubling X8's width: the shared header (name display, LEFT/RIGHT step
  buttons, ENGAGE, both frames) grows to fill the new width - name display and ENGAGE span the
  full width, LEFT becomes the left half and RIGHT the right half (mirrored margins) - while
  the knob column itself stays put and the newly available right-hand space is used for the
  displays.
- **X16** (6HP, 30.48mm): two columns of 8 knobs (channels 1-8 left, 9-16 right) under one
  shared header, no per-channel displays - built by taking X8's shared header (already
  widened the same way as X8D) and placing a second copy of X8's knob column, shifted right by
  half the panel width, next to the first.
- **X16D** (12HP, 60.96mm): the "same way" applied one more time - **double X8D's width**, which
  gives enough room for a second knob+display column right next to the first (Dieter: "da
  hättest du aber selber drauf kommen können" - this follow-on doubling was the obvious move
  once X8D existed, no new design needed).

Common thread across all four: **only the shared header and outer frames actually scale with
panel width** - the per-channel knob/display units themselves are copy-pasted at a fixed size
into whichever column layout the variant needs, never stretched or reflowed individually.

## Confirmed candidate list: Morpheus (verified against live code, 2026-07-15)

Cross-checked `moduleInitStateTypes()` (poly flags) and `moduleParamConfig()` (names/ranges) in
`src/Morpheus.cpp`/`.hpp` directly rather than pattern-matching enum names - this caught one
real trap (see `EXT_INPUT` below).

| Input | Type | Range | Label |
|---|---|---|---|
| `LOCK_INPUT` | continuous | 0-100% | Lock |
| `BALANCE_INPUT` | continuous | 0-100% | Source(0%) to Random(100%) Balance |
| `LOOP_LEN_INPUT` | continuous (int, snap) | 1-128 | Loop Length |
| `HLD_INPUT` | toggle | 0/1 | Hold |
| `RND_INPUT` | click | 0-10 | Randomize |
| `SHIFT_LEFT_INPUT` | click | 0-10 | Shift Left One Step |
| `SHIFT_RIGHT_INPUT` | click | 0-10 | Shift Right One Step |
| `CLR_INPUT` | click | 0-10 | Clear Loop (CV -> 0V) |
| `REC_INPUT` | click | 0-10 | Record from External Source |
| `GTP_INPUT` | continuous | 0-100% | Random Gate Probability |
| `SCL_INPUT` | continuous | -10 to 10 | Random CV Scale |
| `OFS_INPUT` | continuous | -10 to 10 | Random CV Offset |

**Trap caught by checking the code instead of the enum names**: `EXT_INPUT` is polyphonic and
its name pattern-matches the others, but it is **not** a candidate. `EXT_ON_PARAM` is a plain
on/off switch selecting the CV source ("MEM" vs "EXT"), and `EXT_INPUT` is the actual external
signal itself being switched in - a real signal path like `CLK_INPUT`, not a CV-modulates-a-
knob input. `MEM_INPUT`/`STO_INPUT`/`RCL_INPUT`/`RST_INPUT`/`CLK_INPUT` are excluded too, but
for a simpler reason - they're mono, so criterion (a) alone rules them out already.

None of Morpheus's own candidates need `X_PARAM_MOMENTARY` - it's supported in the interface
for generality (future host modules), not exercised by this first candidate list.

## Open items before implementation

- Resizable-panel threshold/max width values (see "Resizable panels" above) - deferred to that
  final step anyway.
