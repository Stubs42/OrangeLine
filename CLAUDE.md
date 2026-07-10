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

- **`src/OrangeLine.hpp`** — global macros/constants shared by all modules: `POLY_CHANNELS=16`, `STYLE_ORANGE/BRIGHT/DARK`, `quantize(cv)` (round to nearest semitone), `note()`/`octave()`, panel-layout helpers (`calculateCoordinates`, `OFFSET_*` constants, `PANELHEIGHT=128.5mm`), and the shared `NumberWidget`/`TextWidget` display widgets.
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

## Adding a new module — file checklist

1. `src/<Name>.hpp` — enums + any `#define`s for repeated-I/O counts.
2. `src/<Name>JsonLabels.hpp` — `char *jsonLabel[NUM_JSONS] = { ... };` matching the `jsonIds` enum order exactly.
3. `src/<Name>.cpp` — the module struct (see architecture above) + `<Name>Widget : ModuleWidget` + `Model *model<Name> = createModel<<Name>, <Name>Widget>("<Name>");` at the end.
4. `src/plugin.hpp` — add `extern Model *model<Name>;`.
5. `src/plugin.cpp` — add `p->addModel(model<Name>);`.
6. `plugin.json` — add a `{ "slug", "name", "description", "tags" }` entry to the `modules` array (watch the comma after the previous entry — this repo has hit a missing-comma JSON bug here before).
7. `res/<Name>Orange/Bright/Dark.svg` (+ optionally `<Name>Work.svg`) — panel art; can start as a plain placeholder and be replaced later without touching the C++.

No Makefile changes are ever needed — `src/*.cpp` is globbed automatically.
