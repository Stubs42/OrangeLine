# SOURCE Data Model

**Status: living brainstorming document, not an approved design.** Captured while thinking out
loud about what SOURCE actually *is*, before any interface/module architecture is decided. Treat
everything below as a draft subject to change in the next conversation, not a spec to build
against yet - matches SOURCE's own current status (see memory: "no panel, no interface method
list, no module architecture decided").

## How we got here (context for future re-reads of this doc)

1. NEO started as a straightforward display for Morpheus's own tape/memory state - conceived
   first as just a "magnifier" for Morpheus's own tiny built-in display.
2. Realized that if NEO can *read* that data to display it, it can just as easily *write* it back
   - the magnifier became a full editor.
3. Generalized further: NEO doesn't need to know it's editing "Morpheus" specifically - it just
   needs a Host that exposes step/channel/loop-length/play-cursor through an abstract interface
   (`NeoHostInterface`). Morpheus became the first implementer, not the only possible one.
4. That abstraction raised the natural next question: what's the "other" sequencer NEO could be
   editing, if not Morpheus? That's SOURCE.
5. Pushed one level more abstract: SOURCE, stripped of any sequencer-specific framing, is just a
   **database** - a schema with a read/write interface. NEO is one client of that database that
   happens to render it as a step editor; nothing about the schema itself requires that framing.
6. One more generalization: a "sequence" (as in *sequencer*) is, by definition, just an **ordered
   collection of arbitrary data** - nothing inherent to that idea requires the data to be pitches,
   gates, or even audio-related at all.

## First approach (superseded): classic entity-relationship modeling

The first pass modeled this as a proper relational schema: **Track** (1) --< **Channel** (1) --<
**Step**, plus **Head** (an independent playback/read position traveling over a Channel's or
Track's own data). Along the way, "Step" was reconsidered as the more general **CHAIN** (an
ordered collection of **LINK**s), reserving "SEQUENCE" itself for some higher-level concept still
undefined at the time. Explored down to real key/relationship/attribute detail before Dieter
paused it: **"Let's stop trying database technologie, I think I have a better idea."**

Kept here only as history - the sections below describe the model that actually replaced it.

## Second approach (current): SOURCE as a sparse multidimensional array of floats, indexed by floats

The reframing that stuck. Two premises make this work cleanly, both specific to what VCV Rack
actually is:

- **Every piece of data in Rack is fundamentally just a float** - a voltage, a virtual analog
  signal. There is no other primitive.
- **Rack simulates an analog modular system**: every inter-module connection (a cable) normally
  carries a continuous, transient stream of floats - a live signal with no persistent identity per
  value, here one instant and gone the next.

SOURCE, conceptually, is **a recorded, addressable version of that same kind of stream** - it
takes what would otherwise be an ephemeral, real-time flow of floats and turns it into something
with persistent, individually-addressable positions that can be read back, edited, and re-read out
of order. Formally: SOURCE is a sparse mapping

```
(float, float, ..., float) -> float
```

from an **N-dimensional float coordinate** to a single float value - essentially a sparse tensor /
hash map, `map<(float, ..., float), float>`.

### Dimensions

Example dimensions that came up: song, track, channel, parameter (pitch, velocity, cutoff, ...),
position. **No dimension is structurally special or privileged over any other** - critically,
*time itself is just another dimension*, a peer to track/channel/parameter, not something
everything else hangs off of. "Position" is nothing more than Morpheus's own `pos`/step counter -
a plain incrementing index, just represented as a float like everything else for uniformity, not
because it needs to be fractional.

One poly cable already gives up to **16 address dimensions for free** (one poly channel per
dimension) - comfortably covers every example named so far. Channel-to-dimension mapping (which
poly channel means which dimension) is left to the user/patch's own convention, not something the
design rigidly enforces.

### Per-dimension configuration

SOURCE itself stores no built-in semantics for any dimension - it's completely generic and
meaning-agnostic (dimension 0..15 are just anonymous numbered slots). **The end user, placing a
SOURCE instance into their own patch, configures each active dimension themselves** through the
module's own UI:

- **name** - a user-facing label
- **resolution** - a rounding/quantization factor, needed because raw floats are never bit-stable;
  without it, two CV addresses "meant" to be the same coordinate could land on slightly different
  raw values and be treated as two different sparse keys. **Configurable independently per
  dimension**, not a single global factor - e.g. a sample-index dimension and a sample-position
  dimension can legitimately need different granularities from each other. Generalizes CHAIN's
  earlier "sample rate" idea (see the gate/auto-advance section below) - a coarser rounding factor
  is just a lower effective rate.
- **range** - min/max valid values for that dimension

**No built-in presets.** A user sets this up once for their own needs and saves it as a normal
Rack preset (right-click "Save preset") - since this config just lives in SOURCE's own persisted
JSON state (the same `dataToJson()`/`dataFromJson()` every OrangeLine module already implements),
Rack's own generic preset mechanism handles reuse for free. No bespoke preset system needed.

### Sparse, and the NULL-vs-zero problem

The sparse index only contains entries for coordinate-tuples that were actually written - most
theoretically-possible combinations simply have no entry at all, not a zero, not any implicit
default.

This creates a real problem: **0.0 cannot be used as a "value is absent" sentinel**, since 0 is
frequently a legitimate value itself (0V = C4 in V/Oct convention, for example). Resolution:

- **Internally**, the sparse structure can use whatever private representation it wants (a NaN
  sentinel, or a genuine membership-check-separate-from-value approach) - implementation detail,
  doesn't need deciding yet.
- **Externally, on any real port**, absence/validity is always expressed as its own **separate
  gate signal**, never folded into the value float itself - matching how plenty of existing Rack
  modules (sample-and-hold, logic modules) already signal "this output is meaningful right now."
  The same problem and the same fix apply symmetrically to **address inputs** too: since 0 can
  also be a legitimate address coordinate (channel 0 is a real channel), each address dimension
  needs its own value + active-gate pair, not just a bare float.

### Operations

Three operations, each with deliberately different behavior for a **missing dimension** (an
address that doesn't specify every active dimension), reasoned through via concrete use cases
rather than decided in the abstract:

**READ**
- Output is always a **value + validity pair**, updated together, in lockstep.
- Timing: if a **trigger** is connected to the read's trigger input, it behaves like a classic
  sample-and-hold - samples exactly when triggered. This exists specifically so the user can
  guarantee the address is actually *settled* before it's read (avoiding catching it mid-transition
  while something upstream is still stepping through values). If no trigger is connected, it
  samples on the module's own normal internal idle cycle instead (this codebase's existing
  control-rate throttling convention).
- Missing dimension(s): this is a genuine "give me everything matching this partial address"
  query, resolved by a rule computed **statically from that dimension's own configured range and
  resolution** (`possibleValues = (max - min) / resolution + 1`) - never from how much data
  actually currently exists, so the behavior is fully predictable at configuration time, never
  fluctuating live:
  - `possibleValues <= 16`: **enumerate** - a poly output pair, one poly channel giving which
    specific value of the missing dimension matched, a parallel poly channel giving its data value,
    sorted **ascending by the missing dimension's own coordinate value** (a deterministic,
    repeatable order, not insertion-order-dependent).
  - `possibleValues > 16`: **aggregate** - a small, fixed-size statistical summary instead (count,
    min, max, sum, avg, stddev, ...), which scales to any amount of underlying data without ever
    exceeding the poly ceiling.

**WRITE** (acts as an upsert - insert-if-absent / update-if-present are the same operation, since
the sparse map doesn't structurally distinguish the two cases)
- Requires a trigger (no idle-cycle fallback the way read has - discussed but not fully settled
  whether write's trigger could ever have gate/idle-cycle behavior outside the audio-rate case
  below).
- Two cases, same trigger:
  - **Fully-specified/unique address**: mono value input, sets exactly one entry.
  - **Some dimensions missing, but the resulting "virtual" combination space is ≤16**: polyphonic
    value input, one channel per resulting combination, each entry gets its own distinct value -
    the input-side mirror of read's poly output.
- **BULK UPDATE** - a deliberately *separate* trigger, only reachable this way, never triggered
  automatically: for when missing dimensions would produce **more than 16** possible combinations.
  Always applies one shared value (channel 1 of whatever's connected to the value input, or 0 if
  nothing's connected) to every matched entry, regardless of how many that ends up being. Kept
  strictly separate from ordinary WRITE specifically so a forgotten/unconnected address dimension
  can never silently cause a mass overwrite through the normal write path - bulk operations must be
  deliberately invoked.

**DELETE / REMOVE**
- **One single trigger** handles both "delete exactly one fully-addressed entry" and "bulk-delete
  everything matching a partial address" - no individual/bulk split needed, unlike write, because
  delete has no "which value goes where" ambiguity at all. Scaling from removing 1 entry to
  removing many isn't a meaningfully different or riskier operation the way write's
  value-assignment is.
- Example: (track, channel, position) fully specified, "parameter" left unaddressed, DELETE
  triggered -> every parameter entry at that step is removed, i.e. the whole note/step is deleted
  in one shot. Leaving out further dimensions (e.g. also "position") scales the same way, clearing
  progressively larger scopes (a whole channel, etc.) - there's no value ambiguity regardless of
  how many dimensions are left unaddressed, so it generalizes cleanly all the way up.

### Gate-driven auto-advance (recording / playback, at any rate)

Beyond a one-shot trigger, read/write can instead be driven by a held **gate**. If the relevant
address dimension (typically position/time) is configured with its own rate (a division of sample
rate - anywhere from full audio rate down to a slow musical clock), holding the **write gate**
causes the module to auto-increment that dimension's address internally at the configured rate,
for as long as the gate stays high - continuously committing whatever's on the value input at each
successive step. A generalized tape-recorder model: hold record, position advances, capture
continues; release, it stops wherever it got to.

The same mechanism applies symmetrically to **read**: holding a gate auto-advances through
positions at the configured rate, continuously outputting each stored value in turn - playback.
If the configured rate happens to be full audio rate, this literally produces a continuous audio
signal at the output, with zero special-casing required - the exact same mechanism as slow
step-sequencer playback, just running at a different rate. Deliberately decoupling "gate-driven
auto-advance" from "audio rate specifically" is what makes this work for both cases at once
(and anything in between, e.g. classic hardware "step recording": hold record, play notes live,
they get captured into successive steps one at a time) - one mechanism, any rate, rather than
separate audio-recording and step-recording implementations.

Open, not yet decided: whether the gate-driven auto-advance should **re-read the external address
input continuously** (allowing something else to modulate/scrub position mid-recording) or
**capture a starting address once at gate-open and advance a purely-internal counter from there**
(ignoring the external address for the gate's duration) - likely worth making this itself a
configurable mode, since both are genuinely useful for different scenarios.

Confirmed: nothing needs to be explicitly frozen/remembered when the gate closes. Since the
address is just live external CV, if it doesn't change between gate-closes, the next gate-open
naturally resumes at the same address purely because the CV never moved - no dedicated
position-memory logic required on the module's own part.

### Trigger vs. gate, generalized

- **Trigger** = "do exactly one access, right now, at this exact address."
- **Gate** = "keep auto-advancing and accessing, at whatever rate this dimension is configured to
  run at, for as long as I'm held."

Same underlying operations either way (read/write/delete) - just two different ways of invoking
them in time.

## Open questions (carried over or newly raised, not yet resolved)

- Whether write's trigger could ever have gate/idle-cycle fallback behavior the way read's does,
  outside the audio-rate gate-driven-recording case - not fully settled.
- How the earlier entity-relationship model's **Head** concept maps onto this new framing - does
  "a HEAD" now just mean "whatever external module/CV is driving one of the address dimension
  inputs" (no longer a first-class SOURCE-internal concept at all), or does it still deserve to be
  modeled explicitly? Not yet revisited since the pivot away from the first approach.
- Persistence strategy for a dataset that could get arbitrarily large - still just an idea, no
  granularity decided.
- How SOURCE relates to Morpheus - alternative Host, complementary module, or something else -
  still genuinely undecided.
- Whether/how NEO (or any other editor) would actually consume this model in practice, now that
  it's shaped so differently from NEO's own current (track, channel, step) assumptions - **partly
  answered, 2026-07-21 evening**: NEO's existing global area (currently holding LOCK/FOLLOW/UNBIND)
  could gain knobs letting the user **pin a chosen subset of SOURCE's dimensions to fixed values**
  - selecting a lower-dimensional "slice" of the full N-dimensional space for that NEO instance (or
    locked group) to work within, with NEO's existing per-row track/channel knobs and per-column
    position addressing then operating entirely inside that slice (the "cake" metaphor: SOURCE is
    the whole cake, a NEO instance only ever works on one slice of it). This slice-definition would
    be **shared across locked hosts**, the same way rows/fullHeight/width already are today via
    `NeoLockData`.

  **Still open, to continue tomorrow morning:**
  - Is "a defined number of fixed dimensions" something the user explicitly sets (e.g. "I'm fixing
    2 dimensions" -> exactly 2 knobs appear), each knob showing both *which* dimension it pins and
    *what value* it's pinned to?
  - Does fixing some dimensions automatically determine which ones remain for NEO's own row/column
    addressing (whatever's left over becomes "the variable ones"), or are those two things
    configured independently?
