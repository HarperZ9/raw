# Engine map

A function-level map of SkyrimSE.exe 1.6.1170, built from the executable's data sections, the Address Library database and CommonLibSSE-NG's names. It tells RAW where each engine system lives: functions, vtables, singletons, settings and the fields worth streaming live. It is read-only static analysis: the tool never launches the game and never changes a game file.

## Run it

```
python -I tools/enginemap/build_map.py \
  --exe "<Skyrim Special Edition>/SkyrimSE.exe" \
  --addrlib "<.../SKSE/Plugins>/versionlib-1-6-1170-0.bin" \
  --commonlib <CommonLibSSE-NG checkout> \
  --bridge <skyrimbridge checkout> \
  --out docs/enginemap --public
python -I tools/enginemap/test_enginemap.py
```

Needs Python 3.10+ and `pefile`. A full run takes about 8 seconds on the CPU. `--public` leaves out values copied from the game (setting defaults and string values), so the output holds addresses, names and notes only. `--addrlib-alt` checks a second database against the first.

## What it reads

| source | gives |
|---|---|
| `.pdata` | every function's start and end (120,080) |
| `.rdata` RTTI | 8,544 vtables, class names and class hierarchies |
| `.data` | 5,344 Setting objects: Skyrim.ini, SkyrimPrefs.ini and game settings, with their compiled defaults |
| Address Library | ID to address |
| CommonLibSSE-NG (MIT) | names for IDs, virtual slot names, member offsets |

The executable's code section is encrypted on disk by Steam's DRM wrapper. The tool reports that and leaves it alone, so callers and callees are not in the map. The `refs` column shows which classes' vtables dispatch to each function.

## Output (`docs/enginemap/`)

- one TSV per subsystem: renderer, shaders, lighting and shadows, scene graph, LOD, sky and weather, water, streaming, Papyrus
- `settings.tsv`, `singletons.tsv`, `live_state.tsv`, `control_points.tsv`, `lighting_techniques.tsv`
- `stats.json`: counts and cross-check results

Confidence per row: **named** (CommonLib names it), **inferred** (exact class and slot from RTTI, method unnamed), **unknown** (exact address, stub shared by many classes).

## Checks the tool runs every time

- Address Library IDs for vtables resolve to vtable starts found by the RTTI scanner (8,017 of 8,017 on 1.6.1170).
- IDs for RTTI type descriptors resolve to type descriptors (7,319 of 7,321; the 2 misses exceed the reader's string limit).
- Named functions start a `.pdata` function (536 of 589; the rest are leaf functions with no unwind entry).

## Debt

Written in Python for now; the BuildLang port is owed.

## Runtime pass (`raw_engine.dll`)

The proxy loads `raw_engine.dll` from `[Modules] Load=raw_engine.dll` in `d3d11_proxy.ini`. The module reads the game's code as it sits in memory. It writes addresses and counts only, never instruction bytes.

| `[EngineRT]` key | effect |
|---|---|
| `CallGraph=1` | decodes every function once on a background thread: `cg_edges.tsv`, `cg_vcalls.tsv`, `cg_refs.tsv`, `cg_clamps.tsv`, `cg_jumptables.tsv` |
| `VTrace=1` | counts calls through each slot of the classes in `engine_rt_vtrace.tsv`, with callers: `vt_trace.tsv` |
| `PapyrusTiming=1` | times `VirtualMachine::Update` and `UpdateTasklets`; `PapyrusBudgetMs` replaces the budget they receive |
| `ShadowAtlas=<size>` | resizes the shadow-map entries of the render-target table before the renderer creates them |
| `GpuTiming=1` (default) | GPU timestamps on every pass change; each `measure` reports GPU ms and draws per pass |
| `OpcodeLoopTiming=1` | times the Papyrus opcode loop (`OpcodeLoop`, default `0x1462650`) at every direct call to it |

The proxy reads `[Timing] PresentInterval` (default -1, the game's own). The game keeps vsync on even with `iVSyncPresentInterval=0` set in memory, so frame-time runs set `PresentInterval=0` in the run's ini.

`[EngineSet]` holds `setting=value` pairs that are applied once, before the renderer creates its targets. Live commands go in `raw_engine_go.txt` (`set`, `wait`, `measure`, `capture`, `atlas`, `fit`, `viewports`, `papyrus`, `probe`, `peek`, `dump`); the harness `engine` command writes them.

Inputs come from the map: `python -I tools/enginemap/rt_inputs.py --map docs/enginemap --out <folder>`. Join the output with names: `python -I tools/enginemap/cg_analyze.py --map docs/enginemap --rt <raw_engine_out> --out docs/enginemap/runtime --public`.

### Results on 1.6.1170

- **Call graph:** 120,080 functions plus 7,234 leaf functions, 5.44M instructions in 861 ms. It found:
  - 345,405 direct edges
  - 74,407 vtable calls
  - 64,879 references to named settings and globals
  - 787 jump tables
- **Papyrus opcode loop:** most likely `sub_1462650`, a 36-entry jump table, called from `VirtualMachine::UpdateTasklets`.
- **Shadows:** the cascade split setup is `BSShadowDirectionalLight` slot 0x10. It runs once per outdoor frame and reads `fShadowDistance`, `fFirstSliceDistance` and `fSplitOverlap`.
- **Batching:** the pass dispatch is `BSBatchRenderer` slot 02, about 6,700 calls per frame.
- **LOD:** terrain LOD distances are read once, at worldspace setup (`sub_511DB0`).
- **Grids:** `uGridsToLoad` is clamped at `sub_19BF30`.

### Engine control results (1.6.1170)

- **Shadow atlas at startup:** `ShadowAtlas=8192` makes the game create its cascade atlas at 8192 x 8192 (it was 4096). The module writes `iShadowMapResolution` and the renderer's size global, which `sub_14F00D0` fills at startup.
- **Live atlas rebuild (`atlas 2 <size>`):** fixed. The shadow passes keep the viewports of the startup size after a rebuild: the module recorded 4096 x 4096 viewports on 8192 and 2048 atlases. Through the proxy's viewport hook (API version 2), the module scales them to the atlas while the atlas is bound. The scaling turns on by itself after a rebuild; `fit 0` turns it off. Riverwood 1080p, frame mean brightness: base 98.0; live 8192 is 104.5 without the fit (self-shadowing lost) and 97.9 with it; live 2048 is 82.9 without the fit and 98.0 with it. Tree self-shadowing returns at both sizes.
- **Shadow distance:** live; the decoded cascade extents follow it and return to baseline when it is set back.
- **LOD and grids:** set at startup, the same aerial view draws full tree models where the default draws LOD.
- **Papyrus:** the budget passed to `VirtualMachine::Update` follows `fUpdateBudgetMS` live.
- **GPU time per pass** (Riverwood 1080p, vsync off, 600 frames each, RTX 4090): frame 9.4 to 9.6 ms. Depth prepass 1.8 ms and geometry 4.7 ms. The shadow atlas pass takes about 2,050 draws and 0.96 ms at 2048, 0.97 ms at 4096 and 1.07 ms at 8192 (fitted). It is bound by draws more than by fill. Shadow distance moves the atlas pass between 0.90 and 1.05 ms (3000, 10000, 20000), and frame time stays within its noise. Depth entry 4 (4096 x 4096 x 8 slices) is never bound outdoors.
- **Papyrus:**
  - The opcode loop has one direct call site, `0x14449BA`, and is timed there.
  - In this scene (Riverwood, AI off) it ran zero times in 600 frames: the VM took 0.015 ms a frame. A script-heavy scene is still owed.
  - The adaptive budget (`papyrus adaptive 16.667 0.5 8`) gave the VM 5.4 ms with no frame-time cost: 9.44 ms against 9.38 ms with the budget off. The VM had no work, so this does not yet show what the budget does under load.
- **Grids (peek):**
  - `TES+0x78` is the GridCellArray: worldX 4, worldY -12, length 5 at +0x10, the cell pointer array at +0x18.
  - `sub_19BF30` reads `uGridsToLoad`, writes back its clamped value, and calls `GridArray::SetCenter`. It also sets up a function-local static under a thread-safe init guard. A live rebuild has to go through it or replace it. Not done yet.
