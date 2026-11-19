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
| `LiveState=1` (default) | lights, the view (`RAW_EngineView`: camera, cell, worldspace, game hour, screen) and the shader manager's state each present |
| `Emitters=1` (default), `EmitterLights=0` | glowing effect and particle draws are collected; with `EmitterLights=1` (or live `emitters export 1`) they join `RAW_EngineLights` |
| `[Cascades] Auto`, `Exterior`, `Interior`, `Vista`, `VistaZ` | per-scene cascade presets, each `dist,first,overlap,atlas` (live `cascades`) |

The proxy reads `[Timing] PresentInterval` (default -1, the game's own). The game keeps vsync on even with `iVSyncPresentInterval=0` set in memory, so frame-time runs set `PresentInterval=0` in the run's ini.

`[EngineSet]` holds `setting=value` pairs that are applied once, before the renderer creates its targets. Live commands go in `raw_engine_go.txt` (`set`, `wait`, `measure`, `capture`, `atlas`, `fit`, `viewports`, `papyrus`, `probe`, `peek`, `dump`, `cascades`, `emitters`, `view`, `grids`); the harness `engine` command writes them.

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

### Grids live, the view, cascades, fire and emitter lights, distant LOD, Papyrus under load (engine control 7)

Riverwood at 13:00 and 22:00, the vista 12,000 units up, the Bannered Mare at 22:00, 1080p, the full renderer
(raw::tx with `Wrap=1`, the bridge, `[Lighting] Mode=2`, `RtShadows=1`). Every keyframe reported ray-traced draws above
zero: Riverwood 2,352, the vista 869 and 1,118, the inn 235. ENB checked 232 of 232 after every session.

**Live grids. Working, both ways.** `grids <n>` waits for the game to empty the grid array and writes the length right
after. The game empties it with `GridArray::DetachAll` (0x221380) when the player changes worldspace. The vtable hooks
of engine control 6 never saw it, because the game calls it directly. An inline detour (`rt_detour.cpp`, Zydis, the
prologue moved whole) catches it. `GridCellArray::KillAll` (0x221990) is refused, since its prologue holds a relative
operand; it was not needed. Riverwood (Tamriel) with 7 at startup:

| Step | DetachAll calls | Length | Loaded cells |
|---|---|---|---|
| start | 0 | 7 | 49 |
| `grids 5`, `coc whiterun` (its own worldspace) | 4 | 5 | 25 |
| `coc riverwood` | 8 | 5 | 25 |
| `grids 7`, `coc whiterun`, `coc riverwood` | 16 | 7 | 49 |

- The vista measured 9.14 ms at grids 5 and 6.05 ms at grids 7. The two views are not the same frame, so the gap is not
  a grid cost.
- The builder `0x2216C0` never ran on these worldspace changes: the game keeps the array and refills it. A length above
  the startup size is still out of reach.
- One fault, found and fixed. The first version wrote `uGridsToLoad` when the request arrived. A job thread then indexed
  the 5-wide array as 7 wide and faulted at `SkyrimSE.exe+0x1A2E7E`. The setting now changes with the length and never
  before it.

**The view** (`rt_view.cpp`, `RAW_EngineView`, `view`):
- The `BSGraphics::State` viewport is two uint32 values. Engine control 6 read them as floats and saw zeros; they read
  1920 and 1080 now, beside the screen size at +0x24.
- The camera comes from the PlayerCamera root's world transform, with the FOV (80).
- The cell and the worldspace come from the player's parent cell: Tamriel reads 0x3C. The worldspace sits at +0x128 on
  1.6.1170; CommonLib's +0x120 held zero in a dump of the cell.
- The game hour comes from Sky. The `ok` bits say which parts were read.

**Cascades driven by RAW** (`rt_cascades.cpp`). `cascades set`, `preset` and `auto` work: the exterior preset applied on
the scene change. Measured:
- `fShadowDistance` moves the decoded cascade half-widths, as in engine control 2.
- `fFirstSliceDistance` set live (4000, 600, 4000) did not, and neither did `fSplitOverlap`. The half-widths drifted
  with the sun, from about 2,926 to 3,038. The split cannot be reached through these settings at run time. Driving it
  means replacing the cascade setup (`BSShadowDirectionalLight` slot 0x10) itself.
- Frame time across the settings: 9.0 to 9.9 ms in two sessions, inside run-to-run noise.
- GPU time per pass cannot be read with the full renderer. `GpuTiming=1` stops the bridge at its query creation under
  raw::tx, and the bridge now names the failing step. Runs with the bridge keep `GpuTiming=0`.

**Fire, torch and magic lights** (`rt_livestate.cpp`, `rt_emitters.cpp`). Every placed light's node is named `ptlight`,
so names alone told nothing apart. The flags come from the owning reference (the node's userData). A light form that
flickers, pulses or can be carried is fire (8). A light under an actor that is not a light form is magic (16).
- The inn at 22:00: 16 active lights, and the fires carry flag 8.
- Glowing draws come from the effect and particle shaders' SetupGeometry. Only additive blending counts, and draws
  within 48 units merge. Riverwood at night gave 14 emitters, the warm one beside the forge's light.
- Emitter lights carry flag 32 and stay out of the export unless `emitters export 1`. With them on, the light pass took
  0.17 ms in the inn against 0.16 ms. 1.7 % of the inn's pixels and 8.8 % of Riverwood's changed by more than 6 of 255.
- Their radius and intensity are estimates from the glow's size and colour. The first run took in mist planes over
  water (radius 480 and more); glows over 400 units are now left out. With that filter Riverwood at night
  gives 7 emitters and the inn none: its one glow was a 1,540-unit effect.
- A full repeat of the session ran clean: grids 7 to 5 to 7, no fault, 232 of 232.

**Distant LOD lit by RAW (FOL-04)** (`DistantLod.hlsli`). The tree LOD is class 7: 8 % of the vista's pixels and 2 % of
Riverwood's. Distant terrain is lit like the other lit classes. In improved mode class 7 now gets:
- the sun, with the shadows that reach that far (the game's mask, ray-traced sun visibility, cloud shadow)
- the directional ambient times sky visibility

Its G-buffer normal points one way across the whole card (view 14), so the card stayed flat. A canopy normal stands
in: the pixel's place across the card's silhouette bends the normal from the camera toward screen right and up.
Luminance of the class-7 pixels, game / improved / control without it:

| View | Mean | Standard deviation |
|---|---|---|
| Vista | 167.8 / 169.3 / 167.4 | 37.1 / 36.3 / 36.4 |
| Vista, turned | 133.9 / 137.6 / 134.1 | 23.5 / 24.2 / 22.6 |
| Riverwood | 153.3 / 153.0 / 156.0 | 49.0 / 50.9 / 46.2 |

- The mean stays within 4 of the game's, so the LOD does not pop.
- The shading spread rises 7 to 10 % where fog leaves the trees visible. In the fogged vista it does not.
- It costs 0.03 ms in the light pass (0.265 to 0.295 ms).
- After the merge with master (#33), the turned vista read 139.6 against the game's 134.0, with a spread of 22.5 against
  22.7 without it. The spread gain did not repeat there, and the LOD came out 5.6 brighter. Tuning is open.
- The effect is small. The side-by-side crop is `ingame-ec7-2026-11-19-tree-lod-nolod-vs-raw-crop.png` in the plan
  folder.

**Papyrus under load** (`tools/papyrus/`, `tests/engine_papyrus_heavy.txt`). A light plugin written by our own tool runs
the load: 32 quests. Half run an integer loop and half call a native each iteration; each OnUpdate ends by registering
the next. `pex.py` reads and writes compiled scripts, and it rewrote all 62 compiled scripts in the install byte for
byte. The plugin goes in for one run, and Plugins.txt comes back by hash. Riverwood, 600 frames a measure:

| Run | Frame | Script jobs a frame | CPU in the job a frame | Papyrus samples (6 s) |
|---|---|---|---|---|
| no plugin | 8.59 ms | 0.9 | 0.007 ms | 4 |
| 32 quests, 0 iterations | 8.55 ms | 12.3 | 0.037 ms | 12 |
| 32 quests, 500 iterations | 10.67 ms | 42.9 | 0.208 ms | 31 |
| 32 quests, 5,000 iterations | 8.74 ms | 49.1 | 0.238 ms | 71 |

- The script code runs on the job threads. The render thread held no Papyrus sample in any run.
- The VM throttles what it runs: ten times the loop work raised its CPU by 14 %.
- The budget (1.2, 4 and 8 ms) changed nothing.
- The 10.67 ms frame did not come back at ten times the work, so it is not a script cost.
- Under this load Papyrus costs no frame time, so native dispatch for the opcodes is not built.
- Not measured: how many updates complete each second, where a faster VM would show. That needs a completion counter
  in the plugin, read back by the module.

Still to do (engine control 7; items 1 to 3 are answered in engine control 8):
- Drive the cascade split by replacing the setup at slot 0x10.
- Grow the grid past its startup size, through the builder.
- Count script completions.
- Check the emitter lights against the RT worker's ReSTIR.

### GPU time with the full renderer, cascades, Papyrus throughput, grid growth (engine control 8)

Measured on 1.7.104, the runtime Steam installed on 2026-11-19, at 1080p, through the version layer (`docs/VERSIONS.md`).
ENB checked 232 of 232 after every session.

**GPU time per pass under raw::tx. Working.** `GpuTiming=1` stopped the bridge in engine control 7 because the layer's
timestamp heap held 4,096 slots. RAW's pass timing alone holds 5 x 1,024, so the bridge, created after it, failed at
its queries. The heap now holds 16,384, a full heap is logged once, and a partial set of timing queries is released.
Every measure reports `gpu_timing`. GPU ms per frame, improved mode:

| View | Frame | Geometry | RAW's passes and the rest (`unknown`) | UI | Depth prepass | Shadow atlas |
|---|---|---|---|---|---|---|
| Riverwood, 13:00 | 10.46 | 4.99 | 3.74 | 0.87 | 0.58 | 0.19 |
| Bannered Mare, 22:00 | 3.31 | 1.94 | 0.35 | 0.71 | 0.05 | 0 (depth4 0.19) |

The GPU frame matches the CPU frame (10.46 against 10.46 ms), so Riverwood is GPU-bound at this setting. The bridge,
RAW's lighting and the RT passes land in `unknown`, because they run between the game's phases. Labelling them is open.

**Cascades. RAW drives the split through the setting; no replacement is needed.** The setup (`fn:BSShadowDirectionalLight::SetupCascades`,
slot 0x10) was read for local analysis on 1.7.104. With D = fShadowDistance, N = iNumSplits and F = fFirstSliceDistance:
- The first split, as a fraction of D, is min(D / N, max(F, 500)) / D. The later splits are spaced linearly from it to 1.
- Each split fraction becomes a distance by the practical split scheme: the mean of the logarithmic and the uniform
  split between the near and far planes (weight 0.5), plus fSplitOverlap.

F moves the inner cascade live. Riverwood, D 10000, N 2, half-widths decoded by the proxy:

| fFirstSliceDistance | Inner half-width | Outer | Shadow atlas GPU |
|---|---|---|---|
| 300 (clamped to 500) | 483 | 13,302 | 0.189 ms |
| 1,250 | 994 | 13,218 | 0.192 ms |
| 4,000 (the default here) | about 2,900 | about 12,500 | 0.192 ms |
| 6,000 (clamped to D / N) | 3,725 | 12,443 | 0.191 ms |

- The split costs nothing to move. At 300, near shadows on the bush and the rocks are visibly sharper than at 6,000:
  22 % of pixels differ by more than 6 of 255.
- Crop: `ingame-ec8-2026-11-19-cascade-first300-vs-first6000-1.7.104.png` in the plan folder.
- RAW's per-scene presets (`cascades preset`, `auto`) set F and D, so they drive the split as they stand.
- Engine control 7 saw no movement on 1.6.1170. That run waited 30 frames after the change, against a decode logged
  every 600 frames, so the reading was most likely taken before the change. This is inferred: 1.6.1170 is no longer
  installed to recheck.

**Papyrus throughput. Counted, and the budget is not what limits it.** Each test quest writes its count of completed
updates to its own global (`tools/papyrus/make_load_plugin.py`), and the module reads them at every measure
(`rt_pcount.cpp`). 32 quests: 16 run an integer loop, 16 call `GetFormID` each iteration. Every update ends with two
natives (SetValue, RegisterForSingleUpdate 0.01 s). Updates per second across 16 quests, 1,200 frames per measure:

| Iterations per update | Game's budget | Fixed 4 ms | Fixed 8 ms | Adaptive, 60 fps target | Adaptive, 120 fps target | Native-call quests |
|---|---|---|---|---|---|---|
| 0 | 330 | 343 | 338 | 329 | 340 | the same as arithmetic |
| 500 | 74.6 | 75.7 | 75.0 | 75.9 | 74.4 | 2.4 to 3.7 |
| 5,000 | 9.7 | 9.8 | 9.8 | 9.7 | 9.8 | 0 to 1.2 |

- RAW's budget control changes nothing: every arm is within 5 % of the game's own. The VM used 0.05 to 0.07 ms of CPU
  a frame against a 1.2 ms budget, so the budget never binds.
- The floor is the update cycle itself: 20.6 updates a second per quest at 0 iterations, about 48 ms or 4 to 5 frames.
  That is two frame-synchronised natives plus the 0.01 s timer.
- Arithmetic runs at about 200 opcodes per stack per frame, at both 500 and 5,000 iterations (3,000 opcodes take
  165 ms; 30,000 take 1.59 s). The VM's CPU over those opcodes works out to about 21 ns each. The interpreter is not
  slow per opcode: it is scheduled to a fixed quota per frame.
- A native call costs one frame. 500 `GetFormID` calls take about 6.4 s per update.

**Decision on native dispatch.** Compiling Papyrus to native code would remove the 21 ns per opcode. The numbers say
that is not the limit:
- Arithmetic is held by the per-frame opcode quota.
- Native-heavy scripts are held by one native per frame.
- A native path would help only by also leaving the quota behind.

The first steps, in order:
1. Find and lift the per-frame quota (a data patch if it is a constant). Expected: 500-iteration updates 4x faster and
   5,000-iteration updates 34x faster, down to the 48 ms floor. To be measured.
2. Run read-only natives such as `GetFormID` without the frame wait.
3. Build native dispatch only where the VM's CPU then shows up in the frame.

The opcode-loop hook installs at one call site on 1.7.104 but counts no calls. The loop is reached another way, which is
open.

**Grid growth past the startup size. Working.** The game has its own resize, `fn:TES::SetupGrid` (0x1A1340 on
1.7.104, found through its listing). It:
- writes uGridsToLoad
- allocates a new GridCellArray on the game's heap, sized by the constructor (odd, at least 5)
- centres it where the old one was and moves every cell both arrays hold
- destroys the old array through its vtable

`grids <n>` past the capacity calls it only while a worldspace change has emptied the grid, so no cell moves and no job
thread walks a loaded grid. Startup grids 5 with uExterior Cell Buffer 144; each change went through WhiterunWorld:

| Step | Length | Loaded cells | Riverwood frame | Vista (12,000 up) frame |
|---|---|---|---|---|
| startup | 5 | 25 | | |
| grow to 7 | 7 | 49 | 11.7 ms | |
| grow to 9 | 9 | 81 | 13.0 ms | 8.5 ms |
| grow to 11 | 11 | 121 | 14.6 ms | 10.7 ms |
| back to 5 | 5 (capacity 11) | 25 | 10.3 ms | |

The session ran stable and the game quit cleanly with the grown array. The vista captures are mostly cloud
(`ingame-ec8-2026-11-19-grids-5-9-11-vista-1.7.104.png`). Larger grids need the cell buffer at startup; 144 covers 11.

Still to do:
- Label RAW's passes in the GPU timing.
- Find the Papyrus per-frame opcode quota, and lift it.
- Find how the opcode loop is reached on 1.7.104.
- Give the 1.7.104 table its own values for `fn:TES::SetupGrid` and the other Address Library fallbacks.
