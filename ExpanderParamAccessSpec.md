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
real host state, none of it lives on the expander at all.

## Red/Green - meaning lives on the host, not the expander

This is **not** "is an expander currently browsing this param" - it's whether the **host**
currently drives its output for that param from the per-channel buffer or from its own single
knob:

- **Red**: `isXParamEngaged() == false` - the host ignores the per-channel buffer entirely
  and drives all channels uniformly from its own front-panel knob, exactly as it does today
  with no expander ever attached.
- **Green**: `isXParamEngaged() == true` - the host drives each channel's output from that
  channel's slot in the buffer.

An Expander's engage button is **one-way**: it can only drive Red -> Green
(`setXParamEngaged(index, true)`), never the reverse. Disengaging (Green -> Red) is only
possible via the host's own right-click "Reset" action (see the nested "Expanders" submenu
below) - deliberately KISS, not a two-way toggle. Because the flag is owned by the host, any
number of Expanders (or repeated re-targets of one Expander) pointed at the same param all see
the same state - there's no per-Expander copy to keep in sync. While engaged, the engage button
itself is inactive (nothing left for it to do) until the host's Reset brings the param back to
Red.

**Open concern raised by Dieter (2026-07-15), not yet resolved**: every host module interprets
"missing" channels differently (some hold the last value, some fall back to a default, some
treat channel N as 0 - there's no single convention today). At the moment of engaging, the host
must not just paper over this with a single default value used only for the channels it
happens to already have - it needs to actually **write real values into every channel slot of
the buffer** the Expander reads from, so the buffer is always fully initialized regardless of
how many channels were actually driving that param at the moment of engaging. This may need
small changes to individual host modules (e.g. Morpheus) or possibly to the shared framework
(`OrangeLineCommon.hpp`) itself - not yet designed, to be worked out when implementation
actually starts ("wir werden sehen, eins nach dem anderen").

Because the buffer is always present and always up to date (not created/populated only at the
moment of engaging), an Expander's value controls show the correct per-channel values the
instant it browses to any param, engaged or not - no separate "snap to current values" step is
needed the way an earlier draft of this spec assumed.

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
  any engaged state or buffer value - it's read-only navigation.
- **One engage/toggle button**: flips `setXParamEngaged()` for the currently-browsed param
  (Red <-> Green). Disabled/no-op while that param is grey (real cable connected).
- **8 or 16 value controls**, reading/writing the browsed param's buffer directly via
  `getXParamChannelValue`/`setXParamChannelValue` - always rendering the buffer's real
  current values, whether or not the param is currently engaged (so turning a knob while Red
  pre-stages a value that takes effect the moment the param goes Green).
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
- **Engaging a param** (any Expander's toggle button, Red -> Green): `setXParamEngaged(index,
  true)` on the host - a single flag, so every Expander currently browsing that param reflects
  the change immediately.
- **Re-targeting one Expander to a different param**: has no effect on any other param's
  engaged flag or buffer at all - those are host state, entirely independent of which param any
  particular Expander happens to be looking at right now.
- **Multiple Expanders (or multiple re-targets) pointed at the same engaged param**: no
  peer-to-peer sync needed - there's only **one** buffer and **one** engaged flag, owned by the
  host, and every control anywhere simply reads/writes that same state.
- **Manual reset/initialize**: see the Host right-click submenu above - the only way state
  changes without an Expander physically present.
- **Patch save/reload**: buffer values and engaged flags are real host state, persisted in the
  host's own JSON (see Host Interface above) - they survive a save/reload the same as any other
  module parameter, regardless of whether any Expander is connected at load time.

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
