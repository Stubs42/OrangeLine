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

**Second attempt (also since retired) - resolve by live scan, not by memory** (Dieter's own
diagnosis: "why is there any code handling a disattach from the host's neighborhood anyway" - a
Host already finds its bound Expander purely by a stable id,
`APP->engine->getModule(boundExpanderId)` in Morpheus.cpp's refresh loop, so the Expander side
should resolve its Host the same structurally-robust way, not by remembering a Host id that can
drift out of sync with reality). `XShared.hpp`'s `findXBoundHostId(expanderId)` scanned every
module in the rack, every tick, for whichever `XHostInterface` reports `getXParamBoundId(i) ==
expanderId` - structurally could never go stale (derived fresh from authoritative state every
time), but Dieter rejected the "search the whole patch" shape itself the moment it was proposed
again for a different problem: "wir müssen immer schauen ob es eine bessere Lösung gibt" (we must
always check whether there's a better solution). Kept only as a last-resort fallback (see below).

**Final design - push a stable id at the moment the relationship actually changes ("attach"),
never scan for it.** Binding changes are rare and Host-initiated (a user's Engage click, not
continuous) - the Host already has the Expander's own resolved pointer in hand at that exact
moment, so it just tells the Expander its own module id directly
(`XExpanderInterface::setXBoundHostId()`). The Expander resolves the actual Host pointer fresh
from that id only when actually needed (every tick, but a single targeted `getModule()` lookup,
never a scan) - never cached across ticks, so a deleted Host can never leave a dangling reference.
Each X-family Expander's own `moduleProcess()` resolves `xHost` as: the Host resolved from
`xBoundHostId` if bound anywhere, else a cached `xAdjacentHost` (updated only via
`Module::onExpanderChange()`, not re-polled every tick either) for browsing purposes only.
`findXBoundHostId()` survives as a one-time-per-module-lifetime fallback, used only on the single
tick right after a patch load (`dataFromJsonCalled`), where a binding can already be restored as
bound without any push ever having fired this session. `CONNECTED_HOST_ID_JSON`, `WAS_ENGAGED_JSON`,
and the auto-reengage-after-reload mechanism are all still gone (superseded, not needed by either
scan-based or push-based resolution).

**A pushed id needs both sides to clean up on deletion, or it just relocates the dangling-pointer
risk instead of fixing it.** `Module::onRemove()` (fires before either side is actually destroyed)
lets each side proactively clear its own reference to the other using only the id it already has -
Morpheus's own `onRemove()` walks its own small `xCandidates[]` releasing every binding it holds;
each Expander's own `onRemove()` resolves its one known Host directly and clears its binding there.
**Critical, easy to get wrong**: `onRemove()` fires *while* Rack's own `removeModule()` still holds
an exclusive lock, so every engine lookup anywhere in that call chain (however many function calls
deep) must use `APP->engine->getModule_NoLock()`, never the ordinary share-locking `getModule()` -
using the wrong one caused a real, reproducible Rack hang on deleting a multi-module selection.
This is why a *second*, separate interior method exists (`resetXParamDuringRemoval()`, alongside
the ordinary `resetXParam()`) - an interface method reachable from `onRemove()` can't silently
reuse whatever "safe" implementation it already had for the ordinary (non-removal) case if that
implementation itself calls the locking `getModule()`. See CLAUDE.md's own Pitfalls section for
the fuller writeup of both this design's history and the lock pitfall specifically.

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

## Duplicate/selection-load recovery (2026-07-19): reconnecting a cloned strip without any Rack "clone" hook

Dieter's own motivating case: duplicating a whole strip (Morpheus + several bound Expanders)
left the duplicated Expanders unconnected to the duplicated Morpheus - each side just carried
its own copy of the OLD relationship's ids, which no longer matched anything real.

**First finding: there is no `Module`-level clone hook to catch this with at all.**
`ModuleWidget::cloneAction()` exists but isn't `virtual` in the SDK header (confirmed directly),
so a plugin subclass overriding it is silently never called by Rack's own generic
duplicate/Ctrl+D code, which necessarily holds a plain `ModuleWidget*`. "Import Selection" is a
wholly separate `RackWidget::loadSelection()` code path from `RackWidget::cloneSelectionAction()`
- it goes through the same ordinary JSON-load mechanism as opening a patch, and `RackWidget`
itself is a single Rack-owned instance a plugin can never subclass regardless. Conclusion: a
clone and an ordinary patch reload are **completely indistinguishable** from a `Module`'s own
point of view - the only usable signal is comparing "what id did I have when I was last saved"
against "what id do I actually have now" (see `OL_selfId`, `OrangeLineCommon.hpp`).

**Design alternatives considered and rejected before landing on the final mechanism** (each
surfaced a real problem, not just a style preference):
- An `origId` "birth certificate" (permanent, set once at genuine construction, shared across
  every clone generation) - ambiguous the moment the same original gets duplicated more than
  once, since every generation shares the identical origId with no way to tell them apart.
- A full rack scan gated on physical adjacency (hoping Rack places a duplicated selection as a
  contiguous block) - works for the common case but isn't provably guaranteed, and doesn't cover
  "only the Expander was duplicated" (lands nowhere near its old Host at all).
- Comparing timestamps to detect "created in the same operation" - explicitly rejected by Dieter
  as not watertight (clock resolution, two genuine operations close together).
- A "whoever's `process()` happens to run first does the active work, the other one waits"
  design, using the OWN `OL_selfId` mismatch state as the signal for "has the other side already
  resolved". This one is subtly broken: if a Host resolves its own mismatch in the very same
  cycle it detects it (which is exactly what "resolve promptly" means), it can go "settled" before
  an Expander that ticks later ever gets a chance to notice it was still fresh - the signal
  needed for discovery evaporates in the same tick it's supposed to enable discovery.

**Final design** - both directions gate on the CANDIDATE being a fresh clone, never on "who
ticked first": see `XShared.hpp`'s own architecture comment (right above `xIsFreshClone()`) for
the complete reasoning, and `X8ModuleCommon.hpp`/`Morpheus.cpp` for where each side's own
one-time, `dataFromJsonCalled`-gated call site lives. In short - a Host only ever repairs its own
slots while it is ITSELF a fresh clone (so a settled, unaffected Host can never reassign a slot
away from an Expander still legitimately bound to it - the "only the Expander was duplicated"
case); an Expander's reverse search only ever considers a Host that is ITSELF a fresh clone as a
valid target, for the identical reason mirrored. Every check uses only data valid the instant
`dataFromJson()` returns (`OL_selfId`, `Module::id`) - never anything that depends on whether the
other side has ticked `process()` yet, so there is no ordering assumption anywhere to get wrong.
Deliberately additive-only: if no matching fresh clone exists, nothing is cleared - genuine
orphan cleanup is already `onRemove()`'s job, not this mechanism's.

**Known, deliberately accepted gap**: duplicating a Host alone (not its Expanders) leaves that
clone's inherited-but-stale candidate slots untouched (there's nothing to repair them with), and
the refresh loop doesn't currently re-verify a still-resolvable occupant actually agrees it's
bound *here* before treating a slot as engaged - harmless in practice (the stale occupant is off
serving its own real Host) unless it coincidentally browses to the exact same candidate index.
Flagged in `XShared.hpp`'s own comment too - a future fix would add a live agreement check to the
refresh loop itself, deliberately not bundled into this pass since it touches already-working,
delicate live-reading code for a narrower case than what was actually being fixed.

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
