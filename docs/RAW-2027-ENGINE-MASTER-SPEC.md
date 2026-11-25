# RAW Engine 2027 Master Technical Architecture and Capabilities Specification

This document is the authoritative, evidence-bound engineering specification for **RAW Engine** across Waves 38 through 49. Every subsystem, file, shader pipeline, runtime hook, and benchmark number documented below is built, compile-verified, and backed by automated test and benchmark receipts in this repository.

## 1. Standalone Native Direct3D 12 Engine

RAW Engine includes a clean-room, zero-proprietary-SDK Direct3D 12 renderer and GPU compute simulation runtime supporting Shader Model 6.6 through 6.10 (`DXC 1.10`, `-HV 2021 -Gis -O3 -enable-16bit-types -WX`) with strict `ID3D12Debug` + `ID3D12InfoQueue` zero-error validation on both hardware GPUs (NVIDIA GeForce RTX 4090) and software DX12 WARP.

### 1.1 Hardware Meshlet Pipeline and Procedural PBR Atlas
- **AS+MS+PS Meshlet Renderer (`src/d3d12_bridge/RawMeshHost.h/.cpp`, `RawTerrainWorld.hlsl`, `RawMeshShader.hlsl`, `RawSubdiv.hlsl`)**:
  - Packs all procedural terrain chunks, biomechanical flora, Voronoi destruction shards, and reconstructed `.NIF` world geometry into 64-vertex / 126-triangle GPU meshlets (`MeshletDesc`, 48 bytes) with exact bounding spheres and backface normal cones.
  - Supports Amplification Shader frustum/cone culling, hardware Mesh Shader rasterization, and automatic VS/PS fallback paths.
- **GPU Procedural PBR `Texture2DArray` Atlas (`RawTextureForge` -> `RawMeshHost` -> `RawTerrainWorld.hlsl`)**:
  - Generates all 8 tileable PBR material families (Rock, Snow/Ice, Crystal/Metal, Bark, Basalt, Magma, Moss/Foliage, Cobble) at `128x128` x 8 mip levels (`192` subresources across Albedo, Normal, and swizzled ORMH `Texture2DArray`s) on startup with zero external image textures.
  - Samples via `SampleGrad` tri-planar projection with Ben Golus Whiteout world-normal blending and smooth distance fade (`2,600..11,000u`).

### 1.2 Real-Time GPU Compute Physics and Lighting Solvers
- **GPU Navier-Stokes Fire/Smoke (`64^3`) + Shallow-Water Equations (`256^2`) (`src/d3d12_bridge/RawFluidSimHost.h/.cpp`)**:
  - `64^3` MAC-grid buoyant fire and smoke simulation with vorticity confinement, adaptive Jacobi pressure projection (`4..8` iterations), MacCormack advection, and physical Planck blackbody radiation bake.
  - Coupled `256^2` shallow-water heightfield with dynamic wakes, wave normals, and shoreline foam.
  - Verified RTX 4090 GPU cost: **`0.237 ms`** combined (`0.218 ms` fire + `0.019 ms` water; `< 0.50 ms` budget, `0` NaNs).
- **GPU 4-Cascade Radiance Cascades Global Illumination (`src/d3d12_bridge/RawRadianceHost.h/.cpp`)**:
  - `256x256` RGBA16F 4-cascade raymarch, angular interval merge, and irradiance resolve.
  - Verified RTX 4090 GPU cost: **`0.111 ms`** (`< 0.35 ms` budget), exact **`2.000x`** linear energy scaling, `0.0%` umbra leak, bit-identical probes between RTX 4090 and WARP.
- **GPU XPBD Soft-Body, Hair, Cloth and Articulated Bush-Bone Solver (`src/d3d12_bridge/RawXpbdSimHost.h/.cpp`, `RawXpbdClothSim.hlsl`)**:
  - Single groupshared compute dispatch (`[numthreads(64, 1, 1)]`, 4 chains per threadgroup, 16 nodes per chain) simulating `256` chains (`4,096` particles) across 4 XPBD substeps per frame with distance/bending compliance, live Beaufort wind gusts, winter snow load, and analytic sphere/capsule/wake colliders.
  - Verified RTX 4090 GPU cost: **`0.058 ms`** (`< 0.25 ms` budget, `0.0` root drift, `stretch <= 1.0003`, `0` NaNs).
- **Zero-Dependency GPU Compute Telemetry HUD (`src/d3d12_bridge/RawOverlayHost.h/.cpp`, `RawOverlayPass.hlsl`)**:
  - Procedural 5x7 bitmap font renderer and real-time latency sparklines drawn directly on the GPU compute queue in `< 0.02 ms` (`~0.006 ms` on RTX 4090), strictly opt-in (`F9`/`H`, default OFF in live Skyrim bridge).

### 1.3 Observatory-Grade Sky, Atmosphere, Weather and Post-Processing Shaders
- **Celestial and Atmospheric Optics (`RawCosmosSky.hlsli`, `AtmosphereLut.hlsl`, `EnvCubePass.hlsl`)**:
  - Hillaire 2020 transmittance, multi-scattering, and C1-continuous sky-view LUTs with Chappuis ozone twilight absorption (indigo zenith, rose Belt of Venus, blue-slate Earth Shadow).
  - Tilted sidereal Milky Way / Aetherius plane with domain-warped carbon-dust rifts, warm-amber galactic bulge scatter, H-II/O-III emission nebulae, ecliptic zodiacal dust light on dark low-moon nights, 10-stratum 3D volumetric curtain auroras (95..260 km `N2+` 427.8 nm / `O` 557.7 nm / `O` 630.0 nm), and orbital 24-day Masser / 16-day Secunda phase illumination.
  - Equal-area cube-sphere O/B/A/F/G/K/M starfield with Kolmogorov atmospheric scintillation (rock-steady zenith `dir.z >= 0.45`, airmass-scaled R/G/B chromatic twinkling near horizon), Eddington solar limb darkening (`1 - 0.6*(1 - mu)`) + inverse-power Mie corona, and exact `22.0 deg` (`cos(22 deg) = 0.92718`) hexagonal ice-crystal solar/lunar halos + horizontal parhelia (sundogs/moondogs) reflected live into `EnvCubePass.hlsl`.
- **Volumetrics, Clouds, Water, Materials and Display (`Clouds.hlsl`, `VolumetricPass.hlsl`, `RtWater.hlsl`, `RawMaterials3D.hlsli`, `RawSpells3D.hlsli`, `LightFog.hlsl`, `BloomPass.hlsl`, `Tonemap.hlsl`)**:
  - Wind-advected convective cloud shadows unified 1:1 between `LightingCommon.hlsli` and `Clouds.hlsl`, golden-hour alpenglow, mountain god-rays with 42/51-deg Mie rainbows, depth-occluded torch-lit parallax precipitation, weather-coupled elemental spell optics, 3-layer refracted glacial ice stress fractures, capillary shoreline meniscus water, Karis-prefiltered bloom, and AgX perceptual tonemapping with post-gamma TPDF IGN dithering.

### 1.4 Native Executables
- **`tools/raw_engine_viewer.cpp` (`raw_engine_viewer.exe`)**: Interactive Win32/DX12 viewer with Frontier 6-biome showcase, live multi-cell Tamriel streaming (`--esm`), landmark teleports (`L`: Riverwood, Whiterun, High Hrothgar, Solitude), playable grounded Walk Mode (`P`) + physics raycast (`C`), live GPU Fire/Water/GI/XPBD solvers, and headless CI verification (`--headless-frames`, `--warp`).
- **`tools/raw_sandbox_render.cpp` (`raw_sandbox_render.exe`)**: Headless/interactive DX12 sandbox renderer for rapid shader and multi-pass validation.
- **`tools/raw_bench_suite.cpp` (`raw_bench_suite.exe`)**: 8-subsystem automated GPU/CPU benchmark and invariant suite with `ID3D12QueryHeap` timestamps, percentile latencies, ASCII scorecard, and JSON export.
- **`tools/raw_forge_cli.cpp` (`raw_forge_cli.exe`)**: Studio batch baking, asset validation, Bethesda format inspection, and live Tamriel physics/streaming audit CLI.

## 2. Clean-Room Bethesda Format Stack

All parsers are 100% original clean-room MIT C++20/23 implementations with zero Bethesda SDK, Windows, or third-party compression library dependencies (including built-in RFC 1950/1951 zlib inflate and LZ4 block decode).

| Format | Module | Verified Capabilities |
| --- | --- | --- |
| `.ESM` / `.ESP` Terrain & Placement | `src/worldgen/RawEsmWorldLoader.h/.cpp`, `RawEsmModelResolver.h/.cpp` | Indexes all `11,186` Tamriel exterior cells and `12,990` `STAT`/`TREE`/`ACTI`/`FURN`/`CONT`/`DOOR`/`MSTT`/`LIGH` models in `~360 ms`; decodes `LAND` (`VHGT`/`VNML`/`VCLR`/`VTEX`) and 6-DOF `REFR` placements. |
| `.ESM` World Metadata | `src/worldgen/RawEsmNpcWeatherLoader.h/.cpp` | Streams `WTHR` (`84`), `CLMT` (`6`), `NPC_` (`5,118`), `REGN` (`317`), and `QUST` (`1,811`) directly from `Skyrim.esm` in **`44.6 ms`** (`5,118` compressed records inflated), mapping canonical `WTHR` records into physical `RawWeatherSim` parameters. |
| `.BSA` Archives (v104/v105) | `src/worldgen/RawBsaArchive.h/.cpp` | Zero-dependency hash-indexed reader for v104 (LE/zlib) and v105 (SE/LZ4) archives with automatic sibling `Meshes0/1.bsa` discovery and extraction. |
| `.NIF` Meshes (v20.2.0.7) | `src/worldgen/RawNifLoader.h/.cpp` | Parses SSE `BSTriShape` / `NiTriShape` vertex streams, skin/shader metadata, and diffuse texture paths, packing directly into validated 64v/126t GPU meshlets and `.glb`/`.obj` exports. |
| `.DDS` Textures (BC1..BC7) | `src/worldgen/RawDdsDecoder.h/.cpp` | Decodes BC1, BC2, BC3, BC4, BC5, BC6H, BC7, and uncompressed formats with full mip-chain parsing and Toksvig normal-map variance roughness analysis. |
| `.ESS` Save Files (v12 LZ4) | `src/engine_rt/rt_save_papyrus_guard.h/.cpp` | Audits Skyrim SE v12 LZ4-compressed `.ess` save headers, form-version integrity, string-table counts (65,535 limit watchdog), and Papyrus VM hitch telemetry. |

## 3. World Simulation, Physics and Spatial Audio

- **Biomechanical 3D Flora (`src/worldgen/RawFloraEngine.h/.cpp`)**: Generates 5 distinct Nordic species across 4 phenological seasons and 3 LODs (`60` catalog assets, `2,240` meshlets, `183,310` triangles, `0` strict validator errors) at **`626.6 trees/s`**.
- **Hydraulic Terrain Erosion and 6-Biome WorldGen (`RawTerrainGen`, `RawHydrologyGen`, `RawBiomeWorld`, `RawMeshPipeline`)**: Analytical tectonic plates, hydraulic droplet erosion, thermal talus relaxation, D8 watershed drainage, and watertight skirtless LOD stitching at **`147.4 cells/s`** (`0` strict validator errors).
- **Orbital Meteorology and Surface Thermodynamics (`src/worldgen/RawWeatherSim.h/.cpp`)**: 365-day axial-tilt insolation, surface thermal energy balance ODE, Stefan's Law lake/river ice thickness, snowpack metamorphic compaction and albedo aging (`0.88` powder -> `0.55` wet granular), fire-heat melt rings, and botanical phenology (`365`-day almanac verified).
- **3D Voronoi CSG Destruction Physics (`src/worldgen/RawDestructionEngine.h/.cpp`)**: Exact convex half-space clipping across 5 structural archetypes (`90` shards) with **`0.000000%`** signed-tetrahedron volume conservation error (`< 1e-4%` invariant), **`0.37 ms`** mean fracture latency, and **`4.2 us/step`** 60 Hz rigid-body simulation.
- **3D World Collision, SAH AABB BVH Raycaster and Character Controller (`src/worldgen/RawWorldPhysics.h/.cpp`)**:
  - Combines O(1) cell heightfields with per-cell 8-bin SAH AABB BVHs over streamed `.NIF` triangles, Moller-Trumbore raycasting, and a sub-stepped capsule character controller (`<= 48 deg` walkable slope slide, `<= 42u` curb step-up, Archimedes water buoyancy).
  - Live Riverwood `(4, -12)` CLI probe receipt (`raw_forge_cli --physics-probe`): **`87,313`** triangles in **`59,267`** SAH BVH nodes, **`64/64`** downward rays hit (`33` prop + `31` terrain, 100% valid unit normals), **`47/60`** grounded walk steps (`194.1u` walked, zero tunneling).
- **Multi-Cell Tamriel World Streamer (`src/worldgen/RawTamrielWorldStreamer.h/.cpp`)**:
  - Concentric 3x3/5x5 ring streaming over all `11,186` Tamriel cells with strict VRAM (`128 MB`) and decoded `.NIF` LRU cache (`64 MB`) caps.
  - Live 3-landmark CLI audit (`raw_forge_cli --tamriel-stream`): Riverwood `(4,-12)` `38.47 MB` (`76.0%` LRU hit) -> Whiterun `(5,-3)` `28.29 MB` (`73.4%` LRU hit) -> High Hrothgar `(14,-14)` `41.61 MB` (`7,591` prop meshlets, **`74.0%`** cumulative LRU hit rate across `996` hits / `350` misses).
- **48 kHz FDN-8 Hadamard Reverb + Woodworth HRTF Audio (`src/audio/RawAudioDsp.h/.cpp`, `RawAudioOutput.h/.cpp`)**:
  - Procedural wind, fire crackle, water flow, 8-channel Hadamard FDN reverberator, Woodworth ITD/ILD binaural HRTF, Doppler shift, and real-time WASAPI audio output running at **`145.7x`** real-time (`0.0069` CPU headroom ratio, `0` NaNs).

## 4. Creation Engine 1.7.104 First-Principles Runtime Rebuild

`raw_engine.dll` + `d3d11.dll` DX12 translation bridge target **Skyrim Special Edition `1.7.104.0`** with fail-closed binary verification, SEH protection, and zero unverified offsets:

- **Binary-Verified Detour and Offset Infrastructure (`rt_detour`, `rt_zydis_offsets`, `rt_version`, `data/versions/skyrimse-1.7.104.tsv`)**: Uses Zydis instruction-length decoding and live binary signature/prologue verification before patching any `.text` hook or `.data` GMST.
- **VEH Crash Forensics and 7,376-Class RTTI Symbolizer (`rt_crash_forensics`)**: Vectored Exception Handler capturing full register state, stack frames, module map, and Creation Engine RTTI class resolution across `7,376` recovered engine classes.
- **Skeleton/IK and Soft-Body Physics (`rt_skeleton_ik`, `rt_softbody`, `rt_terrain_lod`)**: Foot-IK terrain grounding, weapon/skeleton stability, hair/cape soft-body coupling, and terrain LOD/horizon continuity fixes.
- **Modern Combat, Movement and Elemental Magic (`rt_movement_combat`, `rt_combat_ai`, `rt_combat_impact`, `rt_magic_physics`)**: `220 ms` timed parry window, directional dodge with I-frames, melee hitstop, poise/stagger mechanics, persistent impact decals (blood/chip/scorch/frost/shock), and binary-verified spell projectile/aoe physics.
- **9-Hold Living Economy and Binary-Verified Progression GMSTs (`rt_economy_ledger`, `rt_progression_economy`, `rt_settings`)**:
  - Dynamic 9-hold supply/demand trade ledger reacting to regional events, weather, and player commerce.
  - `19` binary-verified `1.7.104.0` GMSTs audited (`17` live-read patched + `2` dead legacy 0-reader GMSTs skipped), including smooth hyperbolic-tangent crafting anti-exploit caps (`<= 2.25x` asymptote eliminating infinite Fortify Alchemy/Enchanting loops), realistic barter spread, and exact fall-damage energy scaling.
- **Core Stability, Memory and Save/Papyrus Watchdogs (`rt_core_fixes`, `rt_memory_guards`, `rt_save_papyrus_guard`, `raw_job_system`, `raw_slab_allocator`)**: Handle-cap guard, lock-free slab allocator and work-stealing job pool, `.ESS` save integrity audit, and Papyrus VM frame-hitch watchdog.

## 5. Verified Benchmark Scorecard (Wave 48 Live Receipts)

All figures below are copied verbatim from [`docs/BENCHMARKS.md`](BENCHMARKS.md), measured on **Intel Core i7-13700KF (16C/24T) / 32 GB RAM / NVIDIA GeForce RTX 4090 (24,138 MB VRAM, Driver 32.0.16.1088) / Windows 11 Build 26320** with D3D12 Debug Layer **ON** (`d3d12DebugErrors == 0`, exit code `0`).

### 5.1 Full Hardware GPU Run (RTX 4090, 30 Warmup + 60 Measured GPU Frames)

| # | Subsystem | Status | Primary Measured Result | Percentiles / Verified Invariants | Target / Headroom |
| --- | --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` NS Fire + `256^2` SWE Water) | PASS | Fire `0.218 ms` + Water `0.019 ms` = **`0.237 ms`** | Combined `p50=0.237`, `p95=0.243`, `p99=0.244 ms`; `Tmax=2550 K`; `NaN=0` | `< 0.50 ms` (`2.11x`) |
| 2 | GPU 4-Cascade Radiance Cascades GI | PASS | **`0.111 ms`** mean GPU time | `p50=0.112`, `p95=0.112`, `p99=0.113 ms`; `2xRatio=2.000`; `Umbra=0.0%`; `NaN=0` | `< 0.35 ms` (`3.14x`) |
| 3 | Procedural PBR Texture Forge (`RawTextureForge`) | PASS | **`9.00 MPix/s`** (`174.8 ms` total) | 8 seamless sets @ `256x256`; per-set `p50=21.45`, `p95=30.34 ms`; seam `11.61` | 8/8 sets valid |
| 4 | 48 kHz FDN-8 Hadamard + HRTF Audio DSP | PASS | Ratio **`0.0069`** (**`145.7x`** real-time) | `13.76 ms` / `2,005.3 ms` stereo; `p95=0.078 ms`; `RMS=0.027`, `Peak=0.212`, `NaN=0` | `< 0.15` (`> 6.5x` RT) |
| 5 | 3D Voronoi CSG Destruction Physics | PASS | Fracture **`0.37 ms`**; Rigid step **`4.2 us/step`** | 5 archetypes, 90 shards, 180 60 Hz steps (`p95=10.0 us`); volErr **`0.000000%`** | `< 1e-4%` volErr |
| 6 | Biomechanical 3D Flora & Hydraulic Hydrology | PASS | **`626.6 trees/s`**; **`147.4 cells/s`** | 10 trees (`904` meshlets, `0` err); 4 cells (`368` meshlets, `0` err) | `0` validator errors |
| 7 | Multi-Cell Tamriel ESM Streamer | PASS | Riverwood 3x3 **`207.6 ms`**; Warm LRU **`72.1%`** | `11,186` cells + `12,990` models (`360.7 ms`); `5,401` meshlets; Whiterun `144.5 ms` | Clean ring + LRU |
| 8 | GPU XPBD Soft-Body / Hair / Bush-Bones (`4,096p`) | PASS | **`0.0579 ms`** mean GPU time | `256` chains, 4 substeps; `p50=0.0584`, `p95=0.0584 ms`; `stretch=1.0003`, `drift=0.0`, `NaN=0` | `< 0.25 ms` (`4.32x`) |

Combined 4-solver real-time GPU compute footprint (Fire + Water + 4-Cascade GI + 4,096-particle XPBD) on RTX 4090: **`0.406 ms`** total per frame (`2.4%` of a `16.67 ms` 60 FPS frame budget).

### 5.2 Quick CI Comparison: RTX 4090 vs. DX12 WARP (`--quick`)

| # | Subsystem | RTX 4090 (`--quick`) | DX12 WARP (`--quick --warp`) | Verified Parity |
| --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` + `256^2`) | `0.242 ms` (`p95=0.248 ms`) | `155.321 ms` (`p95=172.085 ms`) | `0` NaNs on both |
| 2 | GPU Radiance Cascades GI | `0.112 ms` (`p95=0.113 ms`) | `31.345 ms` (`p95=33.302 ms`) | `open=0.5737`, `umbra=0.0000`, `2xRatio=2.0000` |
| 3 | Procedural PBR Forge (`128x128`) | `9.03 MPix/s` (`43.5 ms`) | `9.13 MPix/s` (`43.1 ms`) | `8/8` sets, seam `23.46` |
| 4 | 48 kHz FDN-8 + HRTF Audio | `149.6x` RT (`6.70 ms`) | `148.5x` RT (`6.75 ms`) | `RMS=0.0265`, `Peak=0.2116`, `0` NaNs |
| 5 | 3D Voronoi CSG Destruction | Frac `0.40 ms`, `6.7 us/step` | Frac `0.39 ms`, `6.7 us/step` | `volErr=0.000000%`, finite |
| 6 | Flora & Hydrology Gen | `625.2 trees/s`, `183.3 cells/s` | `572.4 trees/s`, `177.8 cells/s` | `454` + `184` meshlets, `0` errors |
| 7 | Tamriel ESM Streamer | RW `210.7 ms`, WR `143.0 ms` | RW `215.2 ms`, WR `144.4 ms` | `72.1%` LRU (`604/234`) |
| 8 | GPU XPBD Soft-Body (`4,096p`) | `0.0570 ms` (`p95=0.0579 ms`) | `1.0311 ms` (`p95=1.1485 ms`) | `minSD=+0.0050`, `drift=0.0`, `0` NaNs |

## 6. Build, Test, CLI and Launch Commands

### 6.1 Configure and Build (MSVC + Ninja + DXC 1.10)

```powershell
# Configure (from Visual Studio x64 Developer Prompt)
cmake -S E:\wt\raw-rt9 -B E:\wt\raw-rt9-build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build all libraries, shaders, tests, and studio tools
cmake --build E:\wt\raw-rt9-build
```

### 6.2 Automated Verification and Benchmark Suites

```powershell
# Creation Engine 1.7.104 runtime, worldgen, physics, economy, save & forensics test suite
E:\wt\raw-rt9-build\tests\ce_runtime_check.exe

# Direct3D 11 -> Direct3D 12 translation suite
E:\wt\raw-rt9-build\tests\d3d11_translate_suite.exe --no-debug

# GPU compute & rendering tests (hardware GPU + headless WARP)
E:\wt\raw-rt9-build\tests\test_fluid_sim.exe
E:\wt\raw-rt9-build\tests\test_radiance_cascades.exe
E:\wt\raw-rt9-build\tests\test_xpbd_gpu.exe
E:\wt\raw-rt9-build\tests\test_texture_atlas_gpu.exe
E:\wt\raw-rt9-build\tests\test_overlay_gpu.exe
E:\wt\raw-rt9-build\tests\test_full_frame_warp.exe

# Studio 8-subsystem GPU/CPU benchmark suite (full HW GPU + quick CI WARP)
E:\wt\raw-rt9-build\tools\raw_bench_suite.exe --json bench_rtx4090_full.json
E:\wt\raw-rt9-build\tools\raw_bench_suite.exe --quick --warp --json bench_warp_quick.json
```

### 6.3 Studio CLI Audit Commands (`raw_forge_cli.exe`)

```powershell
# Procedural flora, 6-biome diorama, 365-day almanac & 32-material quality gates
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --validate-all out_val --no-write

# Procedural seamless PBR texture library (8 sets)
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --textures out_tex --tex-size 256

# Real Skyrim.esm WTHR/CLMT/NPC_/REGN/QUST metadata & RawWeatherSim mapping audit
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --esm-meta

# Live Tamriel SAH BVH collision, 64-ray scan & 60-step capsule walk probe (Riverwood 4,-12)
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --physics-probe

# Live 3-landmark Tamriel multi-cell streamer & LRU cache audit (Riverwood -> Whiterun -> High Hrothgar)
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --tamriel-stream
```

### 6.4 Interactive Engine Viewer Launch

```powershell
# Launch interactive Frontier 6-biome DX12 viewer (P: Walk Mode, C: Raycast, F9/H: GPU HUD, T: Time, W: Weather)
E:\wt\raw-rt9-build\tools\raw_engine_viewer.exe

# Launch live multi-cell Tamriel streamer (L: teleport Riverwood / Whiterun / High Hrothgar / Solitude)
E:\wt\raw-rt9-build\tools\raw_engine_viewer.exe --esm "H:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data\Skyrim.esm"

# Headless CI verification on WARP (zero physical GPU required)
E:\wt\raw-rt9-build\tools\raw_engine_viewer.exe --headless-frames 4 --warp
```
