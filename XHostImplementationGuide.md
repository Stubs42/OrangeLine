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

## Pitfalls already found and fixed (Morpheus, 2026-07-18)

- **The virtual input buffer must have exactly ONE unit convention, enforced at the single read
  point a knob-takeover uses - never read it directly from multiple call sites that each assume
  a different unit.** Morpheus's `OL_statePoly` held raw 0..1 knob units when written by the
  idle-branch fallback (`computeTakeoverRaw()`) but real cable units when written by the
  active-bound branch (`scaleXParamValue()`) - same array, two conventions depending on binding
  state. `getXParamTakeoverValue()` returned it directly either way, so it was only correct by
  accident while idle. Rack's raw `params[i].setValue()` doesn't clamp to the configured range,
  so a real value misread as raw doesn't just look wrong once - each further active-branch read
  re-applies the real-units scale/offset to an already-real number, diverging by a factor of
  `rawScale` every round trip (a bound SCL/OFS candidate's takeover value hit -210 on the very
  first bind in one real test). Fixed by making `getXParamTakeoverValue()` always call
  `computeTakeoverRaw()` instead of reading the buffer directly - that function is the one place
  that already knows, per candidate, how to convert the Host's actual current value into raw
  units, regardless of binding state. See CLAUDE.md's own Pitfalls section for the full
  before/after walkthrough.
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
  value for the candidate it's currently displaying, and which the Host must check (alongside its
  existing adjacency check) before ever reading `getXKnobValue()`. See CLAUDE.md's own Pitfalls
  section and `XShared.hpp`'s own comment on `isXKnobReady()` for the full reasoning.
