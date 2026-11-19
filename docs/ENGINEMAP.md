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
| live `sample <s> [lo hi]` | in-process sampling profiler: every thread about once a millisecond; hottest functions overall and in a range, the callers found on their stacks, and the threads |
| live `vtime <0xvtable> <0xslot> <label>`, `ltime <0xsite> <label>` | CPU time of a vtable slot, or of the function a RIP-relative lea hands on (a job), outermost calls per thread; every `measure` reports `cpu_ms` and `cpu_calls` per frame |
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
  - `sub_1462650` has one direct call site, `0x14449BA`, and is timed there.
  - It ran zero times in 4,200 measured frames across three scenes: Riverwood with AI off, the Bannered Mare and Riverwood with AI on.
  - Over the same frames, `VirtualMachine::Update` and `UpdateTasklets` together took 0.015 to 0.021 ms a frame.
  - Read: script code does not run inside those two functions or through that call site. The likely route is the job threads that `TasksToJobs` feeds. So `sub_1462650` is not yet shown to be the opcode loop, and the loop is not located.
  - The adaptive budget (`papyrus adaptive 16.667 0.5 8`) sets 4.8 to 8 ms and costs no frame time. Riverwood with AI off: 9.44 ms against 9.38 ms with it off. The inn: 3.70 against 3.79 ms. With the scripts off the VM's thread, this budget does not govern them, so it has no effect either way yet.
- **Grids (peek):**
  - `TES+0x78` is the GridCellArray: worldX 4, worldY -12, length 5 at +0x10, the cell pointer array at +0x18.
  - `sub_19BF30` reads `uGridsToLoad`, writes back its clamped value, and calls `GridArray::SetCenter`. It also sets up a function-local static under a thread-safe init guard. A live rebuild has to go through it or replace it. Not done yet.

### Where the CPU time goes (engine control 4)

**Sampler.** 109 threads at about 1 kHz, the inn and Riverwood with AI on.
- Of about 3,300 samples a run that landed in game code, 6 were in Papyrus code (0x1420000 to 0x1470000), and none on the render thread.
- The hottest functions overall are `sub_769190`, `sub_14F3DC0`, `sub_E320DD`, `sub_150C2D0` and `BSCullingProcess::Process1`, plus the job threads' wait loop (`sub_CF77D5`, `sub_CF7AF0`).

**Papyrus's path.** `VirtualMachine::TasksToJobs` (slot 0x12) hands `sub_14565F0` to the job system (`sub_CF66B0`, through the lea at `0x144F236`). That job runs the script code. Timed through the lea, in Riverwood, 600 frames:
- 0.2 jobs per frame
- 0.001 to 0.005 ms of CPU a frame
- In vanilla, Papyrus is close to idle. A budget has nothing to govern here, and native dispatch would save time only in script-heavy saves, which are not measured yet.

**CPU per frame.** Riverwood, vsync off, frame 9.6 to 10.0 ms. Thread time of the outermost calls:

| Function | CPU ms a frame | Calls a frame |
|---|---|---|
| `BSShaderAccumulator` slot 0x2A (submits the batches) | 6.1 to 6.3 | 11 |
| `BSCullingProcess::Process1` (scene culling, recursive) | 0.37 to 0.38 | 7,700 |
| `BSCullingProcess::Process2` (cull entry) | 0.08 | 22 |
| `BSBatchRenderer` slot 02 (pass dispatch, outermost) | 0.11 | 6,000 |

- Draw submission takes about two thirds of the frame on the render thread. Culling takes under half a millisecond.
- So GPU-driven batching (one ExecuteIndirect per pass instead of about 6,000 dispatches) attacks the largest cost. A faster culler would not.
- The shadow distance barely moves any of these numbers (3000 and 20000).
- The hooks cost 0.07 ms a frame (inn: 1.81 ms without them, 1.89 ms with them).

### The proxy's own CPU cost, and submission pass by pass (engine control 5)

The sampler's module breakdown of the render thread found RAW's proxy taking 38 % of its samples. Each step below was measured in game (Riverwood, 1080p, vsync off, 600 frames):

| Step | Frame | Batch submission (`BSShaderAccumulator` 0x2A) | Proxy share of the render thread |
|---|---|---|---|
| Diagnostics on (as before) | 9.43 ms | 6.19 ms | 38 % |
| Diagnostics opt-in (`[Diagnostics]` BindingLedger, CBScan, PipelineAsserts) | 5.70 ms | 3.28 ms | 20 % |
| Constant-buffer dirty tracker off (`[Proxy] CBDirtyTracker`) | 4.96 to 5.51 ms | 2.63 to 2.95 ms | 11 % |
| Latest run (same settings) | 4.98 ms | 2.58 ms | 11 % |

- **Inn:** the frame went from 3.72 ms to 2.24 ms (diagnostics), then from 2.12 ms to 1.76 ms (tracker). The bind cache on shader binds showed no change outside noise.
- **Render thread now:** the game 48 %, the system D3D11 runtime 17 %, the proxy 11 %, ntdll 8 % and the NVIDIA driver 7 %.

**Submission pass by pass** (`cpu_order_ms`, the k-th call of slot 0x2A in each frame, Riverwood):
- Three of the 11 calls carry 90 % of the time:
  - call 8: 1.22 ms
  - call 5: 0.56 ms
  - call 6: 0.52 ms
- The draw counts match: about 2,460 geometry draws, about 2,030 depth-prepass draws and about 2,020 shadow-atlas draws a frame.
- On the GPU: geometry 3.16 ms, depth prepass 0.56 ms, shadow atlas 0.25 ms.
- The inn has 9 calls and 0.53 ms in total. Indoors, depth entry 4 (8 slices) is bound with 1,500 draws and takes 0.48 ms of GPU time.

**The render thread's hottest game functions:**
- `sub_14F3DC0`, `sub_E28000`, `sub_150C2D0` and `sub_14DD1BA`
- `BSLightingShaderProperty::Unk_2B`
- `BSLightingShader::SetupMaterial`
- These are the targets for the batching work: the per-draw material setup and the pass loop around it.

### The full renderer's CPU cost, draw statistics, grids, weather and lights (engine control 6)

All numbers: Riverwood at 13:00 and the Bannered Mare, 1080p, vsync off (`[Timing] PresentInterval=0`), 600 frames a
measure, with the full renderer on: raw::tx with `Wrap=1`, the bridge, `[Lighting] Mode=2` and `RtShadows=1`. Every
run's keyframe reported the ray-tracing scene's draws (Riverwood 1,955 to 1,957).

**CPU frame with the full renderer** (`engine_fullcpu.txt`). The 4.98 ms of engine control 5 had the lighting decode
and the ray tracing skipped (the constant-buffer shadows were off), so it does not describe the full renderer.

| Constant-buffer shadows | Riverwood frame | Batch submission (`BSShaderAccumulator` 0x2A) | Uploads skipped a frame |
|---|---|---|---|
| master (raw#27): dedupe of every dynamic buffer | 11.73 to 12.87 ms (4 measures) | 8.26 to 9.22 ms | 7,300 of 16,600 |
| only the buffers the bridge reads, no dedupe | 12.42 to 13.51 ms (4) | 8.93 to 9.78 ms | none, 8,400 shadowed |
| every buffer, no dedupe | 12.12 to 12.72 ms (2) | 8.60 to 9.18 ms | none |
| dedupe with a staging/shadow swap | 13.14 to 13.60 ms (2) | 9.46 to 9.70 ms | 3,200 to 3,400 |

- On raw::tx a skipped upload saves a costly Map, so master's dedupe is the fastest of the four and stays.
- The swap lost half the skips because the game writes its constant buffers in part: the staging block has to keep
  the last write.
- Batch submission costs 8.3 to 9.2 ms here, against 2.6 ms on native D3D11 without the bridge.
- Every inn measure under raw::tx read 8.33 ms, the display's 120 Hz. raw::tx now skips its latency-object wait at
  sync 0 (`tx_swapchain.cpp`). With it, one vista measure read 7.44 ms, but the next run's three vistas read 8.32 to
  8.33 ms again. The cap is not removed, and frames faster than 8.33 ms under raw::tx still cannot be measured.

**Where the render thread goes** (`engine_fullsample.txt`, sampler, Riverwood, 4,776 samples on the render thread):
d3d11.dll (the proxy with raw::tx) 54 %, SkyrimSE.exe 21 %, the NVIDIA driver 13 %, ntdll 6 %. In the proxy:

| Function | Samples |
|---|---|
| `raw::tx::Context::Unmap`, `Map`, and the memcpy | 455 |
| `raw::tx::Tracker::Find`, `Require`, `RequireRange` | about 230 |
| `raw::bridge::RtScene::PlaceInstances`, `SetDraws` | about 300 |
| `raw::tx::Context::ApplyTables`, `ScanGraphics`, `IASet*` | about 170 |

raw::tx counts its Maps every 600 presents (proxy log, `[tx] map stats`): Riverwood sends it 14,100 to 14,700 Maps a
frame, all on renamed buffers (constant and dynamic buffers). None takes the committed-copy path.

**Draw statistics per pass** (`engine drawstats`, `engine_drawstats.txt`; hooks on `SetupTechnique`, `SetupMaterial`
and `SetupGeometry` of every BSShader class):

| Pass (call of 0x2A) | Draws | Meshes | Same key as the previous draw | Material setups | CPU in the three calls |
|---|---|---|---|---|---|
| 5, shadow (Utility) | 1,741 | 478 | 162 | 1,288, none repeated | 0.45 ms |
| 6, shadow (Utility) | 1,837 | 689 | 56 | 1,412, none repeated | 0.46 ms |
| 8, geometry (Lighting) | 2,029 | 771 | 146 | 1,300, 6 repeated | 1.26 ms |
| inn, geometry (Lighting) | 936 | 354 | 59 | 727, 19 repeated | 0.67 ms |

- The engine already skips a repeated material.
- Only 7 to 9 % of draws could be instanced in the order the game submits them.
- Sorting by mesh would bring each pass down to its mesh count, 2.5 to 3.6 times fewer.
- The three Setup calls take about 2.2 ms of the 9 ms submission. The rest is the draw path: the proxy's per-draw
  hooks and raw::tx's translation.

**Weather** (`rt_weather.cpp`, `engine weather`). Each present reads Sky's current and last weather, the transition and
the precipitation. It blends rain, snow, cloud and fog and calls the proxy's `PG_SetWeatherState` when the proxy exports
it. After `fw 10a241` (storm rain) it read rain 1.0, cloud 1.0, fog 0.86 (fog far 25,000) and precipitation density 2.0.
The clear weather 0x81A read fog 0.48 (fog far 80,000). `fw` switched with no outgoing weather, so the blend across a
transition is not yet tested.

**Live state and lights** (`rt_livestate.cpp`). It covers the ShadowSceneNode sun, active lights and shadow casters,
`BSShaderManager::State` (timers, interior, grid centre, technique, ambient specular, character light, water) and
`BSGraphics::State` (viewport, inside frame). `RAW_EngineLights` hands the light list to the ray-tracing worker,
`live <n>` streams the state to `live_state.jsonl`, and `lights` writes the list once. In game (`engine_lights.txt`), the Bannered
Mare at 22:00 had 16 active lights plus the sun, with warm fire colours of 1.46, 1.34, 0.87 and radii of 140 to 280
units, and 10 shadow casters. Riverwood at night had 1 active light (radius 439, near 20,555, -45,354, inside the
grid centred on 18,432, -47,104). Positions come from the NiLight's world transform (+0xA0); BSLight+0x50 read zero
on 1.6.1170. The `BSGraphics::State` viewport read zeros at those offsets, so it is not yet a valid field.

**Grids. Not working yet.** In the Bannered Mare the array still held all 49 cells of the exterior grid, so a change
"while the grid is empty in an interior" never finds it empty. `grids <n>` now waits for `GridCellArray::DetachAll` or
`KillAll` (vtable 0x178B160 slots 1 and 2) and writes the length right after either one. In game, neither slot ran:
- A save reload in the same cell kept all 49 cells (`applied_by_hook` 0).
- The two worldspace changes I tried did not load: `coc RavenRock` and `cow DLC2SolstheimWorld 0 0` are not valid
  IDs in this install.

Still to do: a worldspace change with a valid cell ID, and an inline detour on the two functions in case the game calls
them directly rather than through the vtable.
