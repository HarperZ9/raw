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

`[EngineSet]` holds `setting=value` pairs that are applied once, before the renderer creates its targets. Live commands go in `raw_engine_go.txt` (`set`, `wait`, `measure`, `capture`); the harness `engine` command writes them.

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
- **Live atlas rebuild (`atlas 2 <size>`):** runs and the game stays stable. Self-shadowing is wrong at any size other than the startup size. Open.
- **Shadow distance:** live; the decoded cascade extents follow it and return to baseline when it is set back.
- **LOD and grids:** set at startup, the same aerial view draws full tree models where the default draws LOD.
- **Papyrus:** the budget passed to `VirtualMachine::Update` follows `fUpdateBudgetMS` live.
