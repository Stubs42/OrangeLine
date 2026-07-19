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
  - a per-param **binding** to at most one currently-engaged Expander (see "Architecture:
    Expander = a virtual poly cable" below) - this is the *only* thing genuinely stored across
    ticks, and it is session-only, never persisted,
  - a per-param **channel limit** (1-16, default 16) - the only thing about a candidate param
    that actually needs to be persisted in the host's own JSON,
  - the right-click Channels/Reset submenu (see below).
  There is deliberately **no separate per-channel value buffer stored on the Host at all** - see
  below for why that turned out to be unnecessary.
- **Expander module** (X8 / X16): a generic, reusable "remote control" for a host's candidate
  params - no CV sockets at all (deliberately - a socket here would be over-engineering), just a
  parameter-select mechanism (LEFT/RIGHT step buttons), an engage button, and physical
  per-channel value controls. It manages all of its own physical state itself (see below) and
  stores nothing about the Host at all beyond which one it currently resolves.

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

**Non-adjacent, persistent connection** (added 2026-07-18, corrected the same day): physical
adjacency is only how a connection is *first* established, not a requirement for it to keep
working - Dieter's own framing is an NFC handshake, touching two phones once exchanges a contact
that then persists regardless of physical position. The first implementation tried to make this
literal - remembering the Host's own module ID and falling back to it when nothing was adjacent -
but live testing found that remembered ID genuinely goes stale within a single session (no reload
involved at all), since nothing ever forced it to refresh. Retired the same day in favor of
something structurally robust: a bound Expander is found via a **live full-rack scan** for
whichever Host currently reports it as bound (`findXBoundHost()`, `XShared.hpp`) - the same
authoritative state a Host already maintains for its own purposes, so there is nothing to remember
and nothing that can go stale. Only while *not yet bound anywhere* does an Expander fall back to
plain physical adjacency, purely so there's something to browse before ever engaging - once
engaged, the scan takes over and physical position stops mattering entirely. A bound Expander is
read **fully live** regardless of where it physically sits - this is what forced the
single-binding-per-Expander rule below, since there is no longer an adjacency signal available to
arbitrate between several simultaneously-live bindings. Releasing a binding is also fully
position-independent now (see "four ways to disconnect" below) - no separate "Disconnect" action
exists, since there's nothing remembered to disconnect from; unbinding IS the disconnect.

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
  edge-detect), its own currently-browsed param index. It exposes this self-managed state as one
  flat 8/16-channel array plus two small extras (which param it's currently pointed at, and a
  one-shot "I was just clicked" event) - see Expander Interface below. It never calls anything on
  the Host, and it needs **no extra code at all** to keep the channel array live: the knobs are
  plain `ParamIds` like on any other module, and the shared framework's own
  `processParamsAndInputs()` already copies every param's value into `OL_state` every relevant
  tick, for free, exactly as it does for every other OrangeLine module. `getXKnobValue(channel)`
  is therefore just a one-line passthrough into state the framework was already maintaining -
  not a new mechanism.
- **Engaging = plugging a virtual cable in.** Disengaging = unplugging it - and, taking the
  cable metaphor all the way, **a real cable can be unplugged from either end**, so this needs to
  work the same way here (see "four ways to disconnect" below), not just from the Host's side.
- **One Expander instance may hold at most one binding, globally, at any time** (revised
  2026-07-18, retiring an earlier 2026-07-15 "any number of simultaneous bindings, to different
  params" rule - that rule enabled a deliberate "multi-host relay" trick, one Expander bound to
  different candidates on different Hosts, with only the currently-adjacent one read live; it had
  to be retired once bindings became readable live regardless of adjacency, see "Non-adjacent,
  persistent connection" below - reading *every* stale binding live at once, with no adjacency
  arbitration left to pick one, made the old multi-binding model incoherent). The 1:1 rule *per
  param* still holds unchanged (never two Expanders bound to the same param at once). Engaging a
  new, available param first clears this Expander's own existing binding everywhere in the rack
  (any Host, any param, via a full-rack scan - `xUnbindExpanderEverywhere()` in `XShared.hpp`),
  then grants the new one - so binding is now a strict *move*, never an *add*.
- **The one piece of state this actually requires the Host to remember**: a small per-param
  binding, *which* Expander instance currently holds the plug. Concretely a stable Rack module
  ID, not a raw pointer:
  ```cpp
  int64_t boundExpanderId = -1;   // -1 = unbound; session-only, never persisted
  ```
  one per candidate param. This is deliberately *not* a general "directory of all connected X
  modules" - just one ID per candidate param, and it is looked up fresh through Rack's own
  engine whenever it's needed (`Module *m = APP->engine->getModule(boundExpanderId); auto *exp =
  m ? dynamic_cast<XExpanderInterface*>(m) : nullptr;`), never cached as a raw pointer across
  ticks.
- **Why a stable ID instead of a raw `Module*` or a live chain-walk, and why no grace-period
  timer is needed** (an earlier draft of this section proposed exactly such a timer - rejected,
  see below): dragging a module around in the Rack UI never destroys and recreates its C++
  object, it only changes its `leftExpander`/`rightExpander` adjacency - so a cached pointer (or
  ID) stays completely valid the entire time a module is being rearranged in the patch. The
  *only* way `APP->engine->getModule(id)` can ever return `nullptr` for a previously-bound ID is
  if that specific module instance was **actually deleted** from the patch - a real, deliberate
  event, not a "temporary network interruption". A timer-based grace period was solving the
  wrong problem: there is no transient-disconnect problem to begin with once the binding doesn't
  depend on current chain adjacency at all.
- **An Expander recognizing itself as the bound one**: the Host exposes `getXParamBoundId(index)`
  (returns the bound module's ID, or -1) - an Expander compares this against its own Rack-native
  `this->id` to know whether *it* is the bound provider for a given param, no new identity
  mechanism needed since every Rack `Module` already has a stable public `id`.
- **Four, equally legitimate ways to end one specific param's binding**:
  1. **Host side**: the right-click "Reset" action (see below) - explicit and immediate.
  2. **Expander side**: clicking ENGAGE again while browsing the *one* param this instance
     already holds - also explicit and immediate (the engage button ends up being a real toggle,
     but only ever for a param this same instance currently holds - see Red/Green below). Works
     regardless of physical adjacency (2026-07-18) - the Host consumes this via the same id-based
     lookup it already uses to read the value, not only via its adjacency chain-walk. Discovering
     a *brand new* binding still genuinely requires adjacency (see "virtual poly cable" above) -
     only *releasing an existing one* doesn't.
  3. **The bound module is actually deleted from the patch**: `getModule(id)` starts returning
     `nullptr`. The binding is **not** implicitly cleared by this - it stays "virtually plugged
     in" (an Expander somewhere might still believe it holds this plug forever, if it was deleted
     without ever clicking disengage) until an explicit Reset (path 1) frees it up again. In the
     meantime, no data transfers for that param - see the Track & Hold behavior below.
  4. **A real cable gets patched into the same jack**: takes over immediately and actively clears
     any existing binding for that param (rather than leaving a dormant, superseded binding
     sitting there) - see "Real-cable override" below.
- **Browsing is never locked or filtered, on any Expander, ever** (supersedes both the earlier
  "LEFT/RIGHT locked while bound" rule and a briefly-considered "hide taken params from the
  browse list entirely" rule - Dieter: "wir sind kein Hochsicherheitstrakt"). The browse list is
  simply **every** candidate param, always, full stop - LEFT/RIGHT just steps through all of
  them unconditionally, no dynamic filtering logic needed anywhere. What changes per param is
  only its **display color/state**: this instance's own bound params (e.g. green), available
  ones (e.g. another color), and params taken by a *different* Expander or by a real cable (e.g.
  grey/disabled) - three visual states, all read straight off `isXParamEngaged()` +
  `getXParamBoundId()` + `isXParamCableConnected()`, nothing new to store. Clicking ENGAGE while
  browsing a grey (taken-elsewhere or cable-connected) entry is simply a no-op - see Red/Green
  below. Clicking it on an available one *moves* this instance's one binding here, first clearing
  whatever it held elsewhere (see "one binding, globally" above).
- **Track & Hold, not "locked knobs"** (Dieter's own analogy, replacing the earlier "can't
  re-wire while bound" reasoning): an Expander only has one physical set of 8/16 knobs, so it can
  only ever *actively* represent whichever param it's currently browsing - but that's fine,
  because it doesn't need to represent all of its bound params at once. A bound param's value is
  **live only while this Expander's own `getXBrowseIndex()` currently equals that param's index**;
  the moment it browses away to something else (available, or another param it also holds),
  the previous one simply stops receiving fresh values and holds at whatever it last was -
  exactly like a Track & Hold module losing its gate. This needs **no new storage anywhere**: it
  falls straight out of the Host's own already-established "a disconnected input's
  `OL_statePoly` just isn't refreshed, stays at its last value" convention (see "No separate
  per-channel value buffer" below) - the Host simply only treats a bound Expander as "providing
  live data this tick" for the one param it's currently browsing, nothing new to build.
- **No tie-break/"who's touched" heuristic is ever needed** for two Expanders both looking at the
  same taken param - browsing to it and staring at it is completely harmless (it's read-only,
  shown grey, see above), and the only thing that could ever act on it - clicking ENGAGE - is
  simply defined as a no-op whenever the param is already taken by someone else. "Stealing"
  another Expander's active binding is therefore impossible by definition of what an engage-click
  does, not because the param was ever hidden from view.
- **No motorized-knob sync while a control is actually interactive (engaged by this instance) -
  but read-only controls are free to be "motorized" (decided 2026-07-15, retiring the earlier
  "value jump on touch" tradeoff entirely, not just softening it).** The original "a real knob
  can't move itself" reasoning only applies while a control is actually accepting user input. A
  VCV knob is just a drawn graphic, not real hardware - while a value control is showing a param
  this instance *isn't* currently engaged for (available, taken elsewhere, cable-connected), it
  isn't accepting input anyway, so nothing stops it from visually live-tracking
  `getXParamLiveValue()` continuously (see below), exactly like a motorized fader would. Because
  LEFT/RIGHT stepping *always* sets `browseIndex` to the newly-selected param immediately, the
  control has already been live-tracking the real value for as long as it's been sitting there
  browsed by the time ENGAGE is actually clicked. So the value taken over at the moment of
  engaging is, by construction, exactly the value the control was already showing - **no jump at
  all**, not merely an accepted one. The control only stops tracking and becomes a normal,
  position-holding, user-draggable knob once *this instance* actually becomes the bound provider.
- Consequence: **an Expander's own `moduleProcess()` stays almost a no-op** - resolve the Host
  pointer, update the seamless-strip/connection state, handle its own local button
  debounce/browse-index bookkeeping, done. All *decision* logic (binding, Red/Green) lives on the
  Host side only.
- This also **fully resolves the earlier "engage must initialize every channel" concern**: there
  is no snapshot/transfer moment, and no separate buffer to initialize at all (see next section) -
  the Host just reads whichever bound Expander is currently browsing a given param, fresh, every
  tick that it's the live one.

## No separate per-channel value buffer on the Host at all

An earlier draft of this spec gave the Host its own persisted `buffer[16]` per candidate param,
mirroring whatever an engaged Expander provided. Dieter's own question ("warum machen wir das
nicht so, dass der Expander seine Werte einfach im Memory ablegt wie es das Framework ja
sowieso macht, und der Host sie liest wenn eine Connection besteht") cuts that out entirely:

- A candidate param already has a real poly CV jack on the Host, and the shared framework
  already has an established, well-understood convention for "this input isn't connected right
  now" (see the pitfalls list in `CLAUDE.md`: a disconnected input's `OL_statePoly` simply isn't
  refreshed, so the module already has to tolerate "no new data this tick" gracefully). This is
  exactly the mechanism that gives Track & Hold above for free.
- Rather than wrapping every individual read call site, the Host does this **once per tick, in
  one place, right after the framework's own `processParamsAndInputs()`** - for each candidate
  param, in priority order:
  1. **A real cable is connected** (`getInputConnected(inputId)`) - already handled, completely
     unchanged, by the framework itself. If a binding also happens to exist for this param, it is
     actively cleared right here (see "four ways to disconnect" above) rather than left dormant.
  2. **Else, a param is currently bound to an Expander whose own `getXBrowseIndex()` equals this
     param's index right now** (i.e. it's the one *actively being looked at*) - overwrite
     `OL_statePoly` for this input with that Expander's live array, clamped to
     `getXParamChannelLimit()`.
  3. **Else** (bound but not currently browsed there, or not bound at all, or a real cable) -
     write nothing this tick. Either the existing "no cable, use the front-panel knob" behavior
     applies unchanged (never bound), or the Track & Hold behavior applies (bound, but this isn't
     the moment it's live) - both already exactly how the framework already treats an unrefreshed
     `OL_statePoly` slot, no special-casing needed.
  ```cpp
  // sketch, lives in XShared.hpp, called once per candidate param right after
  // processParamsAndInputs() inside the Host's own moduleProcess()
  inline void refreshXAwarePolyInput(Module *host, int inputId, int64_t &boundExpanderId,
                                      int channelLimit)
  {
      if (host->getInputConnected(inputId)) {
          boundExpanderId = -1;   // real cable actively displaces any existing virtual binding
          return;                 // framework already wrote OL_statePoly from the real cable
      }
      Module *m = boundExpanderId >= 0 ? APP->engine->getModule(boundExpanderId) : nullptr;
      auto *exp = m ? dynamic_cast<XExpanderInterface*>(m) : nullptr;
      if (!exp)
          return;                 // not bound, or bound module gone - leave stale value as-is
      if (exp->getXBrowseIndex() != /* this param's index */ -1)
          return;                 // bound, but not the one it's currently looking at - hold
      int channels = std::min(exp->getXKnobCount(), channelLimit);
      for (int c = 0; c < channels; c++)
          host->OL_statePoly[inputId * POLY_CHANNELS + c] = exp->getXKnobValue(c);
  }
  ```
- Consequence: **`isXParamEngaged(index)` is a derived quantity, not separately stored or
  persisted** - it is simply `boundExpanderId[index] != -1`. There is nothing to keep in sync
  between "the flag" and "the binding" because they're the same fact.
- The **only** thing about a candidate param that still needs real persistence is the channel
  limit (a single int, 1-16) - see Host Interface below.

## Expander Interface (new, pull-side - the Host reads these, the Expander never calls out)

```cpp
struct XExpanderInterface
{
    virtual XHostInterface* getXHost() = 0;
    virtual float getXStyle() = 0;

    // Fully self-managed by the Expander (own debounce/edge-detection, own
    // browse-index bookkeeping) - the Host only ever reads these, during its
    // OWN process(). The Expander never calls back into the Host with them.
    virtual int getXKnobCount() = 0;              // 8 (X8/X8D) or 16 (X16/X16D)
    virtual float getXKnobValue(int channel) = 0; // one-line passthrough into OL_state,
                                                   // see architecture section above -
                                                   // no separate storage needed
    virtual int getXBrowseIndex() = 0;             // which host param this Expander's
                                                    // controls currently point at (local
                                                    // UI state owned by the Expander) -
                                                    // never locked, freely navigable, see
                                                    // "Browsing is never locked" above
    virtual bool consumeEngagePress() = 0;         // one-shot: true exactly once per
                                                    // physical click, debounced locally -
                                                    // the Host decides what it means
                                                    // (bind if unbound, unbind if this
                                                    // instance is the one currently bound)

    virtual ~XExpanderInterface() {}
};
```

## Host Interface (dynamic_cast-based, same style as the LANES family's HubInterface)

```cpp
enum XParamType {
    X_PARAM_CONTINUOUS,
    X_PARAM_TOGGLE,      // click flips state, stays until clicked again
    X_PARAM_CLICK,       // single fixed-length pulse fired on click, independent of hold duration
    X_PARAM_PUSH         // value is high only while the control is actively held down - named
                         // "Push" (not "Momentary") for consistency with Dieter's YATOF project
};

struct XHostInterface {
    virtual int getXParamCount() = 0;
    virtual const char* getXParamName(int index) = 0;
    virtual XParamType getXParamType(int index) = 0;
    virtual NVGcolor getXParamColor(int index) = 0;

    // Red/Green - derived, not separately stored: true iff this param currently
    // has a binding (see "No separate per-channel value buffer" above). Also
    // read directly by Expanders to color their own (unfiltered) browse list -
    // see "Expander panel" below.
    virtual bool isXParamEngaged(int index) = 0;

    // Which module currently holds the binding for this param, or -1. An
    // Expander compares this against its own Rack-native `id` to know whether
    // *it* is the bound provider - see architecture section above.
    virtual int64_t getXParamBoundId(int index) = 0;

    // True while a real cable is patched into the host's own poly CV jack for
    // this param - overrides everything else, see "Real-cable override" below.
    virtual bool isXParamCableConnected(int index) = 0;

    // 1-16, default 16 - the only thing about a candidate param that is
    // actually persisted - see "Host right-click menu" below.
    virtual int getXParamChannelLimit(int index) = 0;
    virtual void setXParamChannelLimit(int index, int limit) = 0;

    // Host-side right-click action (see "Host right-click menu" below).
    virtual void resetXParam(int index) = 0;       // clears the binding -> Red

    // Read-only, available regardless of engagement or who (if anyone) is bound -
    // the actual current value sitting in the Host's own poly input state for this
    // param/channel (from a real cable, a bound Expander, or just whatever was last
    // there). Lets ANY Expander browsing this param live-monitor it even when it's
    // not the one driving it - see "Expander panel" below.
    virtual float getXParamLiveValue(int index, int channel) = 0;

    virtual std::string formatXParamValue(int index, float value) = 0; // continuous only

    virtual ~XHostInterface() {}
};
```

Note what's deliberately **not** part of this abstract interface: there is no
`setXParamEngaged()`/bind method callable from outside the Host at all. Binding only ever
happens from *inside* the Host's own `process()`, which walks its own left-side chain using
Rack's already-public `leftExpander.module`/`... .module->leftExpander.module` pointers (no
bespoke chain-walk helper needed - that's already how any Module can traverse expanders) and,
for each `XExpanderInterface` it finds, checks `consumeEngagePress()` + `getXBrowseIndex()`
itself, updating its own private `boundExpanderId[]` directly. Nothing external ever needs to
call into the Host to make this happen.

Persistence: the host's own `dataToJson()`/`dataFromJson()` (via the existing
`moduleExtraDataToJson`/`FromJson` optional-hook pattern) only needs to save/restore, per
candidate param, the **channel limit** - nothing else about the X mechanism is real persisted
state. `boundExpanderId[]` is never persisted - every param loads unbound (Red), picked back up
the next time some Expander engages it, exactly like a patch missing a cable that used to be
there.

## Red/Green - derived from the binding, not a separately stored flag

This is **not** "is an expander currently browsing this param" - it's whether the **host**
currently drives its output for that param from a bound Expander's live array or from its own
single knob:

- **Red**: `isXParamEngaged() == false` (`boundExpanderId == -1`) - the host drives all channels
  uniformly from its own front-panel knob, exactly as it does today with no expander ever
  attached.
- **Green**: `isXParamEngaged() == true` - the host drives each channel's output from the bound
  Expander's live array (see "No separate per-channel value buffer" above).

Getting to Green: the Host, on seeing any Expander's `consumeEngagePress()` fire while it's
browsing an available (untaken) param, first clears that Expander's binding everywhere else in
the rack (see "one binding, globally" above), then records its module ID as the new binding for
that param. Getting back to Red for one specific
param: any of the **four equally legitimate disconnect paths** above - Host Reset, the *bound*
Expander's own engage button clicked again while browsing that specific param (a real toggle, but
only ever effective for a param this same instance already holds - any other Expander's
engage-press on an already-engaged param is simply defined as a no-op, see "Expander panel"
below), the bound module being deleted outright (binding then just sits stale, providing no
data, until an explicit Reset clears it), or a real cable being patched into that param's jack
(clears the binding immediately, see below).

## Real-cable override

If a real cable is patched into the host's own poly CV jack for a given param, that jack wins
outright - see priority order in "No separate per-channel value buffer" above - and it actively
clears any existing virtual binding for that param rather than leaving it dormant (see "four ways
to disconnect" above). The param's entry on every Expander shows its name **greyed out /
disabled** regardless of any binding, since the real cable already provides live per-channel CV
directly. Query via `isXParamCableConnected()`.

## Host right-click menu: nested "Expanders" submenu

The host's `appendContextMenu()` gets one generic **"Expanders"** top-level submenu (built by
shared Expander infrastructure iterating `getXParamCount()`), one level per step:

```
Expanders
  └─ <Param Name>            (one entry per candidate param, e.g. "Lock", "Balance", ...)
       ├─ Channels           (submenu, radio-style: "1".."16", default 16 - see below)
       └─ Reset
```

- **Channels**: `setXParamChannelLimit(index, n)`, 1-16, default 16. Semantics are
  deliberately identical to **a real patched cable that only delivers a defined number of poly
  channels** - channels above the limit simply aren't there, exactly as if a real cable with
  that many channels were plugged into the host's own poly jack for this param. Since every
  candidate param is already polyphonic, the host already has to handle "a cable with fewer
  than 16 channels" as a matter of course - this feature reuses that exact same handling, no new
  special-case logic needed. (Same idea, made concrete: **X8 itself** can only ever supply
  channels 1-8 for whatever param it's engaged on - a hardcoded instance of this exact same
  "fewer than 16 channels" behavior, just fixed by the Expander's own physical knob count instead
  of user-configured.)
- **Reset**: `resetXParam(index)` - clears the binding (`boundExpanderId = -1`), so the host goes
  back to driving all channels from its own knob (Red) and the param becomes available again for
  any Expander to browse to and engage.
- **A Host's own right-click "Initialize" (Rack's built-in module reset) should also release every
  candidate binding it holds** (added 2026-07-18) - easy to overlook since bindings live outside
  the usual `OL_state`/JSON reset path, but a freshly-initialized Host silently still controlling
  some Expander's knobs would be a confusing surprise, not a feature.

**"Initialize" dropped (open question, flagging rather than deciding silently)**: an earlier
draft of this spec had a separate "Initialize" action ("reset the buffer's own values back to
defaults"), which made sense when the Host owned a persisted buffer. Now that there is no such
buffer (see "No separate per-channel value buffer" above - values only ever come live from a
real cable or a bound Expander), there's nothing left for "Initialize" to actually reset. Left
out of the menu above pending Dieter's confirmation this action is really gone, not just moved
somewhere else.

On any Expander, knobs/controls for channels above the current limit are simply shown
disabled/greyed - no separate display widget needed, just the control's own visual state.

## Expander panel (X8 = 1 column of 8, X16 = 2 columns of 8, channels 1-8 left / 9-16 right)

**Selector reverted to LEFT/RIGHT + ENGAGE (2026-07-16)**: a 16-cell color-coded button grid was
tried instead (see git history around commit `2ef6a11`/`06693d1`) and then explicitly dropped by
Dieter the next day - "zu bunt... zu verspielt und zu aufwendig, ausserdem sehen die Panels nicht
wirklich gut aus" (too colorful, too fussy/elaborate, and the panels just didn't look good). Back
to the original mechanism below. The live-monitoring/motorized-control ideas that came up
alongside the grid (see "No motorized-knob sync" above) are **not** part of what got dropped -
Dieter confirmed those stay, just re-expressed in terms of LEFT/RIGHT/ENGAGE instead of grid
press/long-press (see point 2 under "8 or 16 value controls" below).

- **Name display**: shows the currently browsed param's name, in one of three visual states read
  straight off the Host (Dieter: "wir sind kein Hochsicherheitstrakt" - nothing is ever hidden,
  only colored): **available** (untaken by anyone - one color), **mine** (`isXParamEngaged() &&
  getXParamBoundId() == this->id` - e.g. green), or **taken/unavailable** (engaged by a *different*
  Expander, or `isXParamCableConnected()` - both shown the same way, e.g. grey/disabled, since
  neither is anything this Expander can act on). When no Host is resolved at all, shows a grey
  placeholder (e.g. dashes) instead of a name - there's simply nothing to browse, and this doubles
  as the "not connected" indicator (see above).
- **Two step buttons** (prev/next, not a knob - simpler and more precise for pure list
  navigation than trying to land a rotary knob on an exact index). **Circular, over the complete,
  unfiltered list of every candidate param, always** - nothing is ever skipped or hidden, see
  above. Needs its own small custom SVG component (not an existing component-library part).
  Browsing is **never locked** (see architecture section above) - since an Expander now holds at
  most one binding, stepping away from it simply freezes its value (Track & Hold, see above) until
  browsed back to; stepping onto a grey (taken-elsewhere/cable-connected) one is harmless
  read-only viewing, no restriction of any kind.
- **One engage button**: a plain click, self-debounced locally, exposed to the Host as
  `consumeEngagePress()` - the Expander itself has no idea whether this will actually do anything
  (the Host decides: *add* a new binding if the browsed param is currently available, *remove*
  the existing one if this instance already holds the binding for the param it's currently
  browsing, or **do nothing at all** if the browsed param is grey - taken by a different Expander
  or cable-connected - see Red/Green above).
- **8 or 16 value controls**, one per channel, each a custom composite `ParamWidget` with **two
  orthogonal render/behavior axes**:
  1. **Type-based appearance** (Dieter: "der Expander muss dann auch beim Umschalten sein
     Aussehen ändern können, aus Buttons werden dann Knobs und umgekehrt") - continuous ->
     rotary knob; toggle/click/push -> button, differing only in click semantics:
     - **Toggle**: click flips the control's own param value between two states, stays until
       clicked again.
     - **Click**: fires a single fixed-length pulse on click, regardless of how long the mouse
       is held down (what earlier drafts called "Trigger").
     - **Push**: value is high only while the mouse button is actually held down, drops the
       instant it's released - a live gate-follow, not a pulse (named "Push", not "Momentary",
       for consistency with Dieter's YATOF project terminology).
     Button state is always shown via the button's own appearance - no numeric readout needed
     for these three types (see below).
  2. **Engagement-based interactivity** (new, 2026-07-15, see "No motorized-knob sync" above):
     while this instance is **not** the bound provider for the currently-browsed param, the
     control is **read-only and "motorized"** - it continuously renders `getXParamLiveValue()`
     (knob rotates to match the real value in real time; a button-type control's lit state
     mirrors the real boolean/pulse state) and does not accept drag/click-to-change input at all.
     The instant this instance *becomes* the bound provider, the control switches to a normal,
     interactive, position-holding control (accepting drag same as any ordinary knob/button) -
     starting from exactly the value it was already showing, so no jump occurs.
  These two axes are independent - a continuous param can be either interactive-knob or
  read-only-motorized-knob; same for each button type.
- **Numeric display only for continuous-type params, on X8D/X16D** - X8/X16 (no per-channel
  display) rely on the knob's own rotation (read-only/motorized case) or hover tooltip
  (interactive case) instead; toggle/click/push never need a digit readout regardless of
  variant, their own appearance already shows the state.
- Each continuous-type control needs a **custom `ParamQuantity`** (not a plain static
  `configParam()`) so its native VCV hover tooltip calls `formatXParamValue()` live, using
  whichever param is currently browsed - a static tooltip can't work here since the same
  physical control represents different params over time.
  This is a custom composite `ParamWidget` per channel - not several overlaid widgets swapped by
  visibility, but one widget whose `draw()` and interaction handlers branch on the
  currently-browsed param's declared type.
- **Button visual design** (established while building X8's panel): a squarish button shape
  with rounded corners (small ~0.53mm radius), fill = that theme's DisplayFill color, stroke =
  `#ff6600` (the fixed orange accent, not the theme Frame color) - see `res/X8Work.svg`'s
  LEFT/RIGHT/ENGAGE groups for the concrete pattern. Same shape reused for the per-channel
  controls when they're in button mode (toggle/click/push).

## Lifecycle / state rules

- **No expander ever attached, or a param never engaged**: baseline behavior, unchanged from
  today - the host's own knob drives all channels uniformly (Red).
- **Engaging a param** (any Expander's engage click while browsing an available param, Red ->
  Green): the Host, during its own `process()`, sees that Expander's `consumeEngagePress()` fire
  first clears that Expander's binding everywhere else in the rack, then records its module ID as
  `boundExpanderId[index]` - this Expander now holds exactly one binding, globally; the param now
  shows grey (taken) on every other Expander's browse list, but is still visible there, just not
  actionable.
- **Browsing, on any Expander, at any time**: never locked, never filtered, always the complete
  list - has no effect on any param's state by itself, whether landing on an available, own, or
  grey (taken-elsewhere/cable-connected) entry. Stepping onto a param this instance already holds
  elsewhere hands it live control (Track & Hold - the one just browsed away from simply freezes
  at its last value, see architecture section above).
- **Disengaging one specific param** (Red <- Green): any of the **four** equally legitimate paths
  under "Architecture: Expander = a virtual poly cable" above - Host Reset, the *bound* Expander's
  own engage button clicked again while browsing that param, that module being deleted outright
  (binding then sits stale, providing no data, until an explicit Reset clears it), or a real
  cable being patched into that param's own jack (clears it immediately).
- **Patch save/reload**: only the channel limit is real persisted host state (see Host Interface
  above) and survives a save/reload. The binding itself is **not** persisted - every param loads
  unbound (Red), picked back up the next time some Expander engages it.

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
| `RND_INPUT` | push | 0-10 | Randomize |
| `SHIFT_LEFT_INPUT` | click | 0-10 | Shift Left One Step |
| `SHIFT_RIGHT_INPUT` | click | 0-10 | Shift Right One Step |
| `CLR_INPUT` | push | 0-10 | Clear Loop (CV -> 0V) |
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

`RND_INPUT`/`CLR_INPUT` are `X_PARAM_PUSH` (real pushbuttons, effect while held), not
`X_PARAM_CLICK` - confirmed directly by Dieter, correcting an earlier draft of this table that
had them as click-type. `SHIFT_LEFT_INPUT`/`SHIFT_RIGHT_INPUT`/`REC_INPUT` really are
`X_PARAM_CLICK` (fixed one-shot pulse regardless of hold duration).

## Open items before implementation

- Resizable-panel threshold/max width values (see "Resizable panels" above) - deferred to that
  final step anyway.
