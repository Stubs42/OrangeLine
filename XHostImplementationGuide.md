# X-Family Host Implementation Guide

Rules for any module that implements `XHostInterface` (or `XOHostInterface`) - i.e. exposes
candidate params/outputs that an X8/X16/XO8/etc. Expander can browse, bind to, or read.
Companion to `CLAUDE.md` (general module architecture) and `XShared.hpp`/`XOShared.hpp` (the
interface contracts themselves) - this file is specifically the accumulated, hard-won rules for
*implementing* those interfaces correctly, discovered by working through real bugs.

**This is a living document.** Every rule below came from a real, observed bug - when a new one
is found (in Morpheus or any future Host), it gets added here immediately, not just fixed
silently in the code. Dieter: "ich denke da schreiben wir ab jetzt alles rein worüber wir
fallen" (from now on we write in here everything we trip over).

**Working example**: Morpheus (`Morpheus.cpp`) is the first, most complete `XHostInterface`
implementation, and the module these rules are being actively audited/fixed against as of
2026-07-18. Treat it as the reference to check a new rule against before assuming it's correct
elsewhere too.

## Rule 1: the virtual input buffer must always hold a valid value for all 16 channels

Each candidate's `OL_statePoly[inputId * POLY_CHANNELS + channel]` range is the X-family's own
"virtual cable" - what a bound Expander's `getXParamTakeoverValue()` reads to initialize its
knob the moment it attaches or binds. This buffer must be kept continuously correct for **every
one of the 16 channel slots**, not just the ones the Host happens to be actively using right
now - because an Expander with more capacity than the Host currently needs (e.g. an X16 attaching
while the Host only processes 4 channels) can ask for any of the 16 at any time, and must never
find an unmaintained, stale, or zeroed slot.

This must hold across (at least) three distinct scenarios - confirmed so far:

1. **Nothing connected at all** (no real cable, never virtually bound) - every channel's slot
   must reflect whatever value the Host is *actually using* for that channel right now (its own
   panel/param fallback), not a leftover zero from array initialization.
2. **A partially-populated source** - a real cable or virtual Expander binding with fewer than
   16 channels. Channels beyond what that source actually supplies still need a *correct*
   value in their own slot.
3. **The Host's own configured channel count is less than 16** (e.g. a right-click "Channels"
   menu limiting how many channels the Host itself actually processes) - channels beyond that
   self-imposed limit must *still* be correctly maintained in the buffer, exactly like case 2.

**Open question, not yet resolved (2026-07-18)**: for cases 2 and 3, what exact value should an
out-of-range channel get? Dieter's instinct is "the last actually-supplied/active channel's own
value" (an extend/sample-and-hold convention), *not* a fall back to the Host's global panel
param - but he was explicit this isn't a framework-generic rule to assume, it's context-dependent
per Host, and needs to be checked against what Morpheus's own existing reader functions actually
do today before deciding whether to change it. **Do not implement a fix for this until that's
confirmed** - this entry exists so the question itself isn't lost, not as a green light.

## Rule 2: any loop that maintains the virtual input buffer must iterate over all `POLY_CHANNELS`, never just the Host's own configured channel count

Dieter's own suspicion (2026-07-18), not yet confirmed against Morpheus's actual code: many
per-channel refresh loops probably iterate `for (channel = 0; channel < polyChannels; channel++)`
- where `polyChannels` is the Host's *own*, right-click-configured, currently-processed channel
count - rather than the full, fixed `POLY_CHANNELS` (16). Any loop responsible for keeping Rule
1's buffer correct that does this will never touch channels beyond `polyChannels` at all, leaving
them permanently stale/unmaintained regardless of what a bound Expander might need. This needs to
be audited wherever it appears, not just in the one place found first.

## Superseded by a full architecture change (2026-07-18): the raw/CV/display three-layer system is gone

The "one unit convention" pitfall originally recorded here (kept below for history) turned out to
be a symptom of a bigger structural problem: every candidate's value passed through **three**
representations (a 0..1 raw knob position, a CV/cable value, and a human-readable display value),
with only a partial, one-directional conversion pipeline between them. This was replaced entirely -
see CLAUDE.md's own Pitfalls section and the commit `97e5e8b` message for the full writeup. The
short version: the Expander's own knob now directly holds each candidate's **display value**
(dynamically ranged per browsed candidate via `minValue`/`maxValue`/`defaultValue`/`snapEnabled`,
mirroring `Dejavu.cpp`'s own `reconfigureForState()` pattern), and `OL_statePoly` now consistently
holds CV/cable units everywhere it's written (both the idle-branch fallback and the active-bound
branch apply the same `scaleXParamValue()` display->CV conversion) - the old two-hop raw<->CV<->
display pipeline collapsed to one hop (display<->CV). This is now simply **the** convention, not a
pitfall to keep re-checking - `XCandidate`'s `rawScale`/`rawOffset` no longer exist at all,
replaced by `displayMin`/`displayMax`/`displayDefault`/`snap`/`unit`/`cvScale`.

A nice side effect: Rule 1's own "what should an out-of-range channel's value be" question (below)
now has a clearer frame - whatever gets chosen, it needs to be expressed in CV units and pass
through the same `scaleXParamValue()` hop as everything else, so there's no risk of a repeat of
this exact unit-mismatch class of bug regardless of how that question gets answered.

## Non-adjacent, persistent connection (2026-07-18, corrected same day): a bound Expander is read live regardless of adjacency

**First attempt (retired the same day it shipped)**: every X-family Expander persisted the
resolved Host's module ID (`CONNECTED_HOST_ID_JSON`) and re-resolved it every tick - adjacency
first, then an `APP->engine->getModule()` fallback on the remembered ID if nothing was adjacent.
Live testing found this genuinely goes stale **within a single session, with no reload involved
at all**: engage a fresh X8 to a fresh Morpheus, detach it, and the remembered ID and the Host's
real current ID could already disagree (confirmed directly - a debug readout on X8's own panel
showed one ID while the right-click menu's own independent lookup showed a completely different
one, moments apart, same session). The remembered-id approach is inherently fragile because it's a
**snapshot** - nothing forces it to refresh if it silently becomes wrong.

**The actual fix - resolve by live scan, not by memory** (Dieter's own diagnosis: "why is there
any code handling a disattach from the host's neighborhood anyway" - a Host already finds its
bound Expander purely by a stable id, `APP->engine->getModule(boundExpanderId)` in Morpheus.cpp's
refresh loop, so the Expander side should resolve its Host the same structurally-robust way, not
by remembering a Host id that can drift out of sync with reality). `XShared.hpp`'s
`findXBoundHost(expanderId)` scans every module in the rack for whichever `XHostInterface` reports
`getXParamBoundId(i) == expanderId` for some candidate `i`, and returns that Host - the exact same
scan shape `getXEngagedSummary()`/`xUnbindExpanderEverywhere()` already used. Each X-family
Expander's own `moduleProcess()` now resolves `xHost` as: the Host found by this scan if bound
anywhere, else plain physical adjacency (`resolveXHost()`) purely so there's something to browse
before ever engaging. This can never go stale, because nothing is remembered - it's derived fresh
from the same authoritative state (a Host's own `boundExpanderId`) every single tick.
`CONNECTED_HOST_ID_JSON`, `WAS_ENGAGED_JSON`, and the auto-reengage-after-reload mechanism (now
pointless - reconnection is automatic via the scan, nothing to re-press) are all gone.

**Disengage is id-based too now.** The physical ENGAGE button toggle used to only reach Morpheus
via its adjacency chain-walk (`leftExpander.module`), so releasing a binding stopped working the
moment the Expander was detached (the chain-walk simply never found it). Discovering a *brand
new* binding genuinely still needs adjacency (that's the actual "touch" moment - Morpheus has no
other way to learn which Expander/candidate is being requested) - but *releasing an existing one*
doesn't, since Morpheus already holds that Expander's stable id directly. Morpheus's refresh loop
(the same id-based lookup that reads the value) now also consumes `consumeEngagePress()` for the
bound Expander and clears the binding if it fires while that Expander is browsing the exact
candidate it holds - works regardless of physical position. If the Expander happens to also be
adjacent, the chain-walk may consume the same one-shot press first; either path produces the
correct end result, so there's no double-handling risk.

**A Host's own right-click Initialize should release its own X-family bindings too** - easy to
forget since bindings aren't part of the usual `OL_state`/JSON reset path (Morpheus's own
`boundExpanderId` array lives outside `OL_state`, see Rule area above / `moduleExtraDataToJson`).
Add an explicit clear loop to `moduleReset()`.

**XO-family/XR8/XR16 keep the remembered-id approach** (`resolveXOHostPersistent()`,
`XOShared.hpp`) unchanged - there's no equivalent "who's bound to me" scan possible there, since
watching an XO-family Host's output is never exclusive (no `boundExpanderId`-style state exists to
scan for). The remembered-id fragility risk is real there too in principle, but no live-tested
failure has been found yet - if one shows up, look here first.

## Pitfalls already found and fixed (Morpheus, 2026-07-18)

- **(Historical - see the superseding section above)** The virtual input buffer must have exactly
  ONE unit convention, enforced at the single read point a knob-takeover uses - never read it
  directly from multiple call sites that each assume a different unit. Morpheus's `OL_statePoly`
  held raw 0..1 knob units when written by the idle-branch fallback but real cable units when
  written by the active-bound branch - same array, two conventions depending on binding state,
  diverging by a factor of `rawScale` every round trip (a bound SCL/OFS candidate's takeover value
  hit -210 on the very first bind in one real test). The fix at the time (routing
  `getXParamTakeoverValue()` through a shared conversion function) was correct as far as it went,
  but the deeper fix was removing the raw layer entirely - see above.
- **`channels >= channel` is an off-by-one** when `channels` is a count and `channel` is a
  0-indexed position - must be `channels > channel`. Found identically duplicated across all ten
  of Morpheus's own `getChannelXXX()` reader functions. See CLAUDE.md's own Pitfalls section for
  the full writeup (the self-referential feedback loop this caused via `computeTakeoverRaw()`,
  and why only the exact `channel == channels` boundary channel was affected).
- **A Host must not assume a fixed number of ticks before a newly-arrived-or-bound Expander's
  knob is safe to read.** Rack does not guarantee any particular relative ordering between an
  Expander's and a Host's own, independently-throttled `process()` calls - a fixed "wait one
  tick" grace period is a race, not a fix (tried, reverted the same day). The correct mechanism
  is a plain level-based handshake: `XExpanderInterface::isXKnobReady(int index)`, which the
  Expander holds false for exactly as long as its own knob doesn't yet reflect the Host's held
  value for the candidate it's currently displaying, and which the Host must check before ever
  reading `getXKnobValue()` - it is now the sole gate (an adjacency check is no longer part of
  this, see "Non-adjacent, persistent connection" below - a bound Expander is read live whether or
  not it's physically adjacent). See CLAUDE.md's own Pitfalls section and `XShared.hpp`'s own
  comment on `isXKnobReady()` for the full reasoning.
