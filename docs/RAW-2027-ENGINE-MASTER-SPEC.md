# RAW Engine 2027 Master Technical Architecture and Capabilities Specification

This document is the authoritative, evidence-bound engineering specification for **RAW Engine** across Waves 38 through 54. Every subsystem, file, GPU compute solver, HLSL SM 6.6/6.10 shader, BuildLang 1.4.0 kernel module, binary-verified runtime hook, CLI audit mode, and benchmark number documented below is built, compile-verified, and backed by automated test receipts in this repository.

## 1. Standalone Native Direct3D 12 Engine

RAW Engine includes a clean-room, zero-proprietary-SDK Direct3D 12 renderer and GPU compute simulation runtime supporting Shader Model 6.6 through 6.10 (`DXC 1.10`, `-HV 2021 -Gis -O3 -enable-16bit-types -WX`) with strict `ID3D12Debug` + `ID3D12InfoQueue` zero-error validation on both hardware GPUs (NVIDIA GeForce RTX 4090) and software DX12 WARP.

### 1.1 Hardware Meshlet Pipeline and Procedural PBR Atlas
- **AS+MS+PS Meshlet Renderer (`src/d3d12_bridge/RawMeshHost.h/.cpp`, `RawTerrainWorld.hlsl`, `RawMeshShader.hlsl`, `RawSubdiv.hlsl`)**:
  - Packs all procedural terrain chunks, biomechanical flora, Voronoi destruction shards, and reconstructed `.NIF` world geometry into 64-vertex / 126-triangle GPU meshlets (`MeshletDesc`, 48 bytes) with exact bounding spheres and backface normal cones.
  - Supports Amplification Shader frustum/cone culling, hardware Mesh Shader rasterization, and automatic VS/PS fallback paths.
- **GPU Procedural PBR `Texture2DArray` Atlas (`RawTextureForge` -> `RawMeshHost` -> `RawTerrainWorld.hlsl`)**:
  - Generates all 8 tileable PBR material families (Rock, Snow/Ice, Crystal/Metal, Bark, Basalt, Magma, Moss/Foliage, Cobble) at `128x128` x 8 mip levels (`192` subresources across Albedo, Normal, and swizzled ORMH `Texture2DArray`s) on startup with zero external image textures.
  - Samples via `SampleGrad` tri-planar projection with Ben Golus Whiteout world-normal blending and smooth distance fade (`2,600..11,000u`).

### 1.2 Real-Time GPU Compute Physics, Visibility and Lighting Solvers (9+ Live Solvers)
1. **GPU Navier-Stokes Fire/Smoke (`64^3`) + Shallow-Water Equations (`256^2`) (`RawFluidSimHost`)**: `64^3` MAC-grid buoyant combustion with vorticity confinement, adaptive Jacobi pressure projection (`4..8` iters), MacCormack advection, Planck blackbody radiation bake, and coupled `256^2` shallow-water wakes/normals/foam. RTX 4090: **`0.244 ms`** combined (`< 0.50 ms` budget, `0` NaNs).
2. **GPU 4-Cascade Radiance Cascades Global Illumination (`RawRadianceHost`)**: `256x256` RGBA16F 4-cascade raymarch, angular interval merge, and irradiance resolve. RTX 4090: **`0.120 ms`** (`< 0.35 ms` budget), exact **`2.000x`** linear energy scaling, `0.0%` umbra leak.
3. **GPU XPBD Soft-Body, Hair, Cloth and Articulated Bush-Bone Solver (`RawXpbdSimHost`, `RawXpbdClothSim.hlsl`)**: Single groupshared compute dispatch (`[numthreads(64, 1, 1)]`) simulating `256` chains (`4,096` particles) across 4 XPBD substeps with live Beaufort wind gusts, snow load, and analytic sphere/capsule/wake colliders. RTX 4090: **`0.058 ms`** (`< 0.25 ms` budget, `0.0` root drift, `stretch <= 1.0003`).
4. **GPU Conservative Hi-Z Occlusion Pyramid (`512^2`) and 16k-Instance Culler (`RawGpuCullHost`, `RawGpuCull.hlsl`)**: 10-mip conservative `max()` Standard-Z Hi-Z pyramid + wave-compacted frustum/distance/Hi-Z culling emitting `ExecuteIndirect` `DrawIndexed`/`DispatchMesh` args. RTX 4090: **`0.029 ms`** (`< 0.15 ms` budget, `0` false occlusions).
5. **GPU `16x9x24` 3D Clustered 1,024-Light Grid (`RawLightClusterHost`, `RawLightClusterSim.hlsl`)**: Builds `3,456` logarithmic-Z view-space AABBs and assigns `1,024` dynamic point/spot lights with zero atomic overflows (`116,168` links). RTX 4090: **`0.098 ms`** (`< 0.15 ms` budget).
6. **GPU `160x90x64` (`921.6k`-Voxel) 3D Volumetric Froxels (`RawVolumetricFroxelHost`, `RawVolumetricFroxelSim.hlsl`)**: Exponential-Z valley/mountain mist, 2-octave wind wisps, Henyey-Greenstein Mie forward scatter (`74.5x` anisotropy), `0.85` Halton-jittered temporal reprojection, and Frostbite analytical slice integration. RTX 4090: **`0.045 ms`** (`< 0.25 ms` budget).
7. **GPU Variable-Penumbra PCSS Contact-Hardening Shadows (`RawShadowPcssHost`, `RawShadowPcssSim.hlsl`)**: Fernando 3-stage PCSS (16-tap Vogel blocker search + physical penumbra estimator + 32-tap IGN bilinear PCF) over texel-snapped cascades (`32.0x` contact-hardening penumbra ratio). RTX 4090: **`0.028 ms`** (`< 0.15 ms` budget).
8. **GPU Jimenez GTAO + Unoccluded Bent-Normal & Bilateral Denoise (`RawGtaoHost`, `RawGtaoSim.hlsl`)**: Horizon-based cosine-weighted ambient occlusion with bent-normal extraction and depth-aware bilateral filtering.
9. **GPU Stochastic GGX Screen-Space Reflections (`RawSsrHost`, `RawSsrSim.hlsl`)** + **Zero-Dependency GPU Compute Telemetry HUD (`RawOverlayHost`, `RawOverlayPass.hlsl`)**: Procedural 5x7 bitmap HUD & sparklines (`~0.006 ms` GPU, opt-in `F9`/`H`, default OFF in live Skyrim bridge).

Combined 7-solver benchmarked GPU compute footprint on RTX 4090 (Fluid + Radiance GI + 4,096p XPBD + Hi-Z/16k Cull + 1,024-Light Clusters + `921.6k` Volumetric Froxels + PCSS Shadows): **`0.622 ms`** total per frame (`3.7%` of a `16.67 ms` 60 FPS frame budget).

### 1.3 Film-Grade Shader Pipeline (Sky, BRDF, Volumetrics, Denoisers and Display)
- **Observatory Sky & Atmosphere (`RawCosmosSky.hlsli`, `AtmosphereLut.hlsl`, `EnvCubePass.hlsl`)**: Hillaire 2020 LUTs with Chappuis ozone twilight (indigo zenith, Belt of Venus, Earth Shadow), tilted Milky Way with carbon-dust rifts and warm bulge scatter, H-II/O-III nebulae, ecliptic zodiacal light, 10-stratum 3D auroras (`N2+` 427.8 / `O` 557.7 / `O` 630.0 nm), orbital 24d Masser / 16d Secunda phases, Kolmogorov atmospheric star scintillation, Eddington solar limb darkening + Mie corona, and `22.0 deg` (`cos=0.92718`) solar/lunar ice halos + sundogs synced live into `EnvCubePass.hlsl`.
- **Material Transport, Denosing and Post-Processing (`StrataBRDF.hlsli`, `AoComposite.hlsl`, `Sss.hlsl`, `FroxelPass.hlsl`, `DepthFog.hlsl`, `DofPass.hlsl`, `Rawlss5Pass.hlsl`, `BloomPass.hlsl`, `RtReflFilter.hlsl`, `GiAtrousPass.hlsl`, `Tonemap.hlsl`, `Clouds.hlsl`, `VolumetricPass.hlsl`, `RtWater.hlsl`, `RawMaterials3D.hlsli`, `RawSpells3D.hlsli`, `LightFog.hlsl`)**: Kulla-Conty/Turquin multi-scatter GGX, Charlie grazing cloth sheen, Airy thin-film iridescence, Jimenez colored multi-bounce AO, Christensen-Burley RGB subsurface diffusion, unified `cloudShadowN` volumetric shafts, golden-angle hexagonal bokeh DoF, reactive-mask YCoCg anti-ghosting, Karis chromatic bloom, roughness-adaptive reflection filtering, plane-distance edge-stopping A-Trous GI, and AgX perceptual tonemapping with TPDF IGN dithering.

### 1.4 Native Executables
- **`tools/raw_engine_viewer.cpp` (`raw_engine_viewer.exe`)**: Interactive Win32/DX12 viewer uniting Frontier 6-biome showcase, live multi-cell Tamriel streaming (`--esm`), landmark teleports (`L`), playable grounded Walk Mode (`P`) + physics raycast (`C`), `F9`/`H` GPU HUD, `N` NavMesh + squad AI, `I` wildfire/snowmelt ecosystem, `V` wildlife boids, `E` 9-hold caravan economy, `O` world-event director, `F12` `.bmp` capture, and `9+` live GPU solvers with headless WARP CI verification.
- **`tools/raw_sandbox_render.cpp` (`raw_sandbox_render.exe`)**: Headless/interactive DX12 sandbox renderer.
- **`tools/raw_bench_suite.cpp` (`raw_bench_suite.exe`)**: Automated GPU/CPU benchmark and visual invariant scorecard with `ID3D12QueryHeap` timestamps and JSON output.
- **`tools/raw_forge_cli.cpp` (`raw_forge_cli.exe`)**: 12-command studio batch baking, format inspection, physics, NavMesh, ecosystem, wildlife, settlement, and radiant event audit CLI.

## 2. Clean-Room Bethesda Format Stack

All parsers are 100% original clean-room MIT C++20/23 implementations with zero Bethesda SDK, Windows, or third-party compression dependencies (built-in RFC 1950/1951 zlib inflate and LZ4 block decode):

| Format | Module | Verified Capabilities |
| --- | --- | --- |
| `.ESM` / `.ESP` Terrain & Placement | `RawEsmWorldLoader`, `RawEsmModelResolver` | Indexes all `11,186` Tamriel exterior cells & `12,990` models in `~355 ms`; decodes `LAND` (`VHGT`/`VNML`/`VCLR`/`VTEX`) and 6-DOF `REFR` placements. |
| `.ESM` World Metadata | `RawEsmNpcWeatherLoader` | Streams `WTHR` (`84`), `CLMT` (`6`), `NPC_` (`5,118`), `REGN` (`317`), `QUST` (`1,811`) from `Skyrim.esm` in **`46.9 ms`**, mapping canonical `WTHR` records into physical `RawWeatherSim` regimes. |
| `.BSA` Archives (v104/v105) | `RawBsaArchive` | Hash-indexed v104 (LE/zlib) & v105 (SE/LZ4) reader with sibling `Meshes0/1.bsa` auto-discovery. |
| `.NIF` Meshes (v20.2.0.7) | `RawNifLoader` | Parses SSE `BSTriShape`/`NiTriShape` geometry and shader textures directly into validated 64v/126t meshlets and `.glb`/`.obj`. |
| `.DDS` Textures (BC1..BC7) | `RawDdsDecoder` | Decodes BC1..BC7 + uncompressed mip chains with Toksvig specular anti-aliasing roughness analysis. |
| `.ESS` Save Files (v12 LZ4) & `plugins.txt` | `rt_save_papyrus_guard`, `rt_mod_load_audit` | Audits v12 LZ4 `.ess` headers, 65,535 string-table cap, Papyrus VM hitches, `TES4` master order, and ESL slot integrity. |

## 3. World Simulation, Ecology, Physics and Spatial Audio

- **Biomechanical Flora & Hydraulic Terrain (`RawFloraEngine`, `RawHydrologyGen`, `RawTerrainGen`, `RawBiomeWorld`)**: 5 species x 4 seasons x 3 LODs (`560.3 trees/s`, `0` validator errors), hydraulic + thermal + D8 watershed erosion (`144.8 cells/s`, `0` errors).
- **Orbital Meteorology (`RawWeatherSim`)**: 365-day axial-tilt insolation, thermal balance ODE, Stefan's Law ice thickness, metamorphic snowpack compaction/albedo aging, botanical phenology, and live `Skyrim.esm` `WTHR` bridge.
- **3D Voronoi CSG Destruction (`RawDestructionEngine`)**: Exact convex polyhedral fracture across 5 archetypes (`90` shards) with **`0.000000%`** volume error (`< 1e-4%`) and **`4.5 us/step`** 60 Hz rigid-body simulation.
- **3D World Collision & NavMesh/Squad AI (`RawWorldPhysics`, `RawNavMeshAi`)**: Live Riverwood `(4,-12)` SAH BVH (`87,313` tris / `59,267` nodes, `64/64` rays hit, `47/60` grounded walk steps) + 3D NavMesh (`64x64`, `4.55 ms` bake, `22,470` links, Octile A* funnel path `45 -> 4` waypoints in `0.035 ms`, 4-agent tactical squad).
- **Coupled Wildfire, Snowmelt, Wildlife, 9-Hold Economy & Radiant Event Director (`RawWorldEcosystem`, `RawWildlifeBoids`, `RawSettlementSim`, `RawWorldEventDirector`)**:
  - Wildfire spread (`eastReach=35 > westReach=1`), rain suppression (`37 -> 0`), D8 snowmelt (`massErr=1.94e-06`), `-8 C` black-ice (`216` cells), 90-day regrowth (`fuel 0.05 -> 0.66`).
  - `512`-agent O(N) 3D spatial-hash wildlife (`AlpineHawk`, `PineCrowFlock`, `TundraDeerHerd`, `RiverSalmonSchool`) stepping in **`0.195 ms/frame`**.
  - 9-Hold living settlement & trade caravan economy (`maxCommErr=6.90e-09`, `goldErr=4.31e-08`, blizzard blockade `peakSnowbound=12`, Winterhold grain `20.66g -> 34.20g`, 30-day `226` deliveries).
  - Emergent radiant world-event director (`BlizzardCaravanRescue`, `WildfireContainment`, `CaravanBanditAmbush`, `AuroraCelestialGathering`, `SeasonalHarvestMarket`, `PredatorMigrationAlert`) with exact lifecycle accounting (`45` spawned = `0 + 0 + 45` over 30 days, `5/6` kinds, `0` cooldown spam).
- **48 kHz FDN-8 Hadamard Reverb + Woodworth HRTF Audio (`RawAudioDsp`, `RawAudioOutput`)**: **`142.6x`** real-time headroom (`0.0070` ratio, `0` NaNs) with live WASAPI output.

## 4. BuildLang 1.4.0 Core Kernel Library (8 Modules, 48 Verified Kernels)

All 8 BuildLang reference modules under `native/bld/` pass both `buildc check` and `buildc run` (`buildc 1.4.0`, `rc=0`, `0` errors) with exact equation parity against RAW Engine's C++23 and HLSL kernels (see [`docs/BUILDLANG-KERNEL-MAP.md`](BUILDLANG-KERNEL-MAP.md)):
- `RawPhysicsKernels.bld` (6 kernels), `RawAtmoWeather.bld` (7 kernels), `RawBiomeFlora.bld` (5 kernels), `RawRenderOptics.bld` (6 kernels), `RawFluidDynamics.bld` (6 kernels), `RawCombatAi.bld` (6 kernels), `RawEconomySim.bld` (6 kernels), `RawGpuPipeline.bld` (6 kernels).

## 5. Creation Engine 1.7.104 First-Principles Runtime Rebuild

`raw_engine.dll` + `d3d11.dll` DX12 translation bridge target **Skyrim Special Edition `1.7.104.0`** with Zydis instruction verification, fail-closed signature validation, SEH guards, **140+ binary-verified GMSTs**, and **431+ passing `ce_runtime_check` assertions**:
- **Forensics, Stability & Telemetry**: 7,376-class RTTI VEH crash symbolizer (`rt_crash_forensics`), 0-100 engine health scoreboard (`rt_engine_health`), 300-frame zero-alloc flight recorder (`rt_telemetry_recorder`), `.ESS` save & Papyrus hitch watchdog (`rt_save_papyrus_guard`), `plugins.txt` / ESL auditor (`rt_mod_load_audit`), PI frame-pacing governor (`rt_frame_governor`), slab allocator & work-stealing job pool.
- **Combat, Magic, Stealth, Economy & AI Systems**: `220 ms` parry / dodge / hitstop (`rt_movement_combat`), skeleton/IK & soft-body coupling (`rt_skeleton_ik`, `rt_softbody`), adaptive acoustic soundscape director (`rt_adaptive_audio_director`), weather-driven NPC daily behavior (`rt_npc_behavior`), 9-hold economy ledger (`rt_economy_ledger`), and binary-verified 1.7.104 GMST modules (`rt_progression_economy`, `rt_ai_packages`, `rt_world_rules_gmst`, `rt_physics_camera_gmst`, `rt_stealth_social_gmst`, `rt_locomotion_melee_gmst`, `rt_magic_physics`).

## 6. Verified Benchmark Scorecard (Wave 53 Live Receipts)

All figures below are copied verbatim from [`docs/BENCHMARKS.md`](BENCHMARKS.md), measured on **Intel Core i7-13700KF (16C/24T) / 32 GB RAM / NVIDIA GeForce RTX 4090 (24,138 MB VRAM, Driver 32.0.16.1088) / Windows 11 Build 26320** with D3D12 Debug Layer **ON** (`d3d12DebugErrors == 0`, exit code `0`).

### 6.1 Full Hardware GPU Run (RTX 4090, 30 Warmup + 60 Measured GPU Frames)

| # | Subsystem | Status | Primary Measured Result | Percentiles / Verified Invariants | Budget / Headroom |
| --- | --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` NS Fire + `256^2` SWE Water) | PASS | Fire `0.225 ms` + Water `0.019 ms` = **`0.244 ms`** | Combined `p50=0.244`, `p95=0.248`, `p99=0.251 ms`; `Tmax=2550 K`; `NaN=0` | `< 0.50 ms` (`2.05x`) |
| 2 | GPU 4-Cascade Radiance Cascades GI | PASS | **`0.120 ms`** mean GPU time | `p50=0.120`, `p95=0.120`, `p99=0.121 ms`; `2xRatio=2.000`; `Umbra=0.0%`; `NaN=0` | `< 0.35 ms` (`2.93x`) |
| 3 | Procedural PBR Texture Forge (`RawTextureForge`) | PASS | **`8.42 MPix/s`** (`186.8 ms` total) | 8 seamless sets @ `256x256`; per-set `p50=23.18`, `p95=32.27 ms`; seam `11.61` | 8/8 sets valid |
| 4 | 48 kHz FDN-8 Hadamard + HRTF Audio DSP | PASS | Ratio **`0.0070`** (**`142.6x`** real-time) | `14.06 ms` / `2,005.3 ms` stereo; `p95=0.080 ms`; `RMS=0.027`, `Peak=0.212`, `NaN=0` | `< 0.15` (`> 6.5x` RT) |
| 5 | 3D Voronoi CSG Destruction Physics | PASS | Fracture **`0.42 ms`**; Rigid step **`4.5 us/step`** | 5 archetypes, 90 shards, 180 60 Hz steps (`p95=10.6 us`); volErr **`0.000000%`** | `< 1e-4%` volErr |
| 6 | Biomechanical 3D Flora & Hydraulic Hydrology | PASS | **`560.3 trees/s`**; **`144.8 cells/s`** | 10 trees (`904` meshlets, `0` err); 4 cells (`368` meshlets, `0` err) | `0` validator errors |
| 7 | Multi-Cell Tamriel ESM Streamer | PASS | Riverwood 3x3 **`229.5 ms`**; Warm LRU **`72.1%`** | `11,186` cells + `12,990` models (`361.7 ms`); `5,401` meshlets; Whiterun `157.8 ms` | Clean ring + LRU |
| 8 | GPU XPBD Soft-Body / Hair / Bush-Bones (`4,096p`) | PASS | **`0.0579 ms`** mean GPU time | `256` chains, 4 substeps; `p50=0.0584`, `p95=0.0584 ms`; `stretch=1.0003`, `drift=0.0` | `< 0.25 ms` (`4.32x`) |
| 9 | GPU Hi-Z Pyramid (`512^2`) + 16k Instance Cull | PASS | Hi-Z `0.0212` + Cull `0.0082` = **`0.0294 ms`** | `p50=0.0297`, `p95=0.0297 ms`; `vis=5120`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` | `< 0.15 ms` (`5.10x`) |
| 10 | GPU `16x9x24` 3D Clustered 1,024-Light Grid | PASS | **`0.0978 ms`** (`Bld 0.0035 + Asn 0.0859 ms`) | `p50=0.0983`, `p95=0.0983 ms`; `lit=3410/3456`, `links=116168`, `ovf=0`, `NaN=0` | `< 0.15 ms` (`1.53x`) |
| 11 | GPU `160x90x64` 3D Volumetric Froxels (`921.6k`) | PASS | **`0.0449 ms`** (`Inj 0.0297 + Int 0.0113 ms`) | `p50=0.0451`, `p95=0.0461 ms`; `Mie=74.5x`, `tau=5.46/0.31`, `nonMono=0`, `NaN=0` | `< 0.25 ms` (`5.57x`) |
| 12 | GPU Variable-Penumbra PCSS Shadows (`960x540`) | PASS | **`0.0280 ms`** mean GPU time | `p50=0.0276`, `p95=0.0287 ms`; `ratio=32.0x`, `open=1.0`, `umb=0.0`, `sky=1.0`, `NaN=0` | `< 0.15 ms` (`5.36x`) |

### 6.2 Quick CI Comparison: RTX 4090 vs. DX12 WARP (`--quick`)

| # | Subsystem | RTX 4090 (`--quick`) | DX12 WARP (`--quick --warp`) | Verified Parity |
| --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` + `256^2`) | `0.237 ms` (`p95=0.243 ms`) | `138.923 ms` (`p95=139.969 ms`) | `0` NaNs on both |
| 2 | GPU Radiance Cascades GI | `0.108 ms` (`p95=0.108 ms`) | `24.525 ms` (`p95=25.988 ms`) | `open=0.5737`, `umbra=0.0000`, `2xRatio=2.0000` |
| 3 | Procedural PBR Forge (`128x128`) | `8.56 MPix/s` (`45.9 ms`) | `8.58 MPix/s` (`45.8 ms`) | `8/8` sets, seam `23.46` |
| 4 | 48 kHz FDN-8 + HRTF Audio | `146.1x` RT (`6.86 ms`) | `146.4x` RT (`6.85 ms`) | `RMS=0.0265`, `Peak=0.2116`, `0` NaNs |
| 5 | 3D Voronoi CSG Destruction | Frac `0.40 ms`, `6.8 us/step` | Frac `0.41 ms`, `6.9 us/step` | `volErr=0.000000%`, finite |
| 6 | Flora & Hydrology Gen | `587.8 trees/s`, `182.5 cells/s` | `516.8 trees/s`, `172.0 cells/s` | `454` + `184` meshlets, `0` errors |
| 7 | Tamriel ESM Streamer | RW `221.3 ms`, WR `156.0 ms` | RW `236.6 ms`, WR `160.3 ms` | `72.1%` LRU (`604/234`) |
| 8 | GPU XPBD Soft-Body (`4,096p`) | `0.0574 ms` (`p95=0.0584 ms`) | `0.6664 ms` (`p95=0.6952 ms`) | `minSD=+0.0050`, `drift=0.0`, `0` NaNs |
| 9 | GPU Hi-Z (`512^2`) + 16k Cull | `0.0297 ms` (`p95=0.0303 ms`) | `0.8101 ms` (`p95=0.8953 ms`) | Bit-identical `vis=5120`, `hiz=6144`, `falseOcc=0` |
| 10 | GPU 3D Clustered 1,024-Lights | `0.0979 ms` (`p95=0.0983 ms`) | `1.9212 ms` (`p95=2.0168 ms`) | Bit-identical `lit=3410/3456`, `links=116168`, `ovf=0` |
| 11 | GPU `160x90x64` Froxels (`921.6k`) | `0.0450 ms` (`p95=0.0451 ms`) | `13.3166 ms` (`p95=13.7587 ms`) | Bit-identical `Mie=74.45x`, `tau=5.4645/0.3127` |
| 12 | GPU PCSS Shadows (`960x540`) | `0.0286 ms` (`p95=0.0297 ms`) | `6.4828 ms` (`p95=6.6007 ms`) | Bit-identical `ratio=32.00x`, `open=1.0`, `umb=0.0` |

## 7. Build, Test, CLI and Launch Commands

```powershell
# Configure & Build (MSVC + Ninja + DXC 1.10)
cmake -S E:\wt\raw-rt9 -B E:\wt\raw-rt9-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build E:\wt\raw-rt9-build

# Core Runtime, DX12 Translation, GPU Compute & Studio Benchmark Suites
E:\wt\raw-rt9-build\tests\ce_runtime_check.exe
E:\wt\raw-rt9-build\tests\d3d11_translate_suite.exe --no-debug
E:\wt\raw-rt9-build\tools\raw_bench_suite.exe --json bench_rtx4090_full.json
E:\wt\raw-rt9-build\tools\raw_bench_suite.exe --quick --warp --json bench_warp_quick.json

# Studio CLI 12-Mode Audits (raw_forge_cli.exe)
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --validate-all out_val --no-write
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --esm-meta
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --physics-probe
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --tamriel-stream
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --navmesh-audit
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --ecosystem-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --wildlife-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --settlement-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --events-sim

# Verify All 8 BuildLang 1.4.0 Kernel Modules (48 Kernels)
foreach ($m in @('RawPhysicsKernels','RawAtmoWeather','RawBiomeFlora','RawRenderOptics','RawFluidDynamics','RawCombatAi','RawEconomySim','RawGpuPipeline')) {
    buildc check "E:\wt\raw-rt9\native\bld\$m.bld"; buildc run "E:\wt\raw-rt9\native\bld\$m.bld"
}

# Interactive DX12 Viewer (Frontier Showcase or Live Tamriel Streamer)
E:\wt\raw-rt9-build\tools\raw_engine_viewer.exe --esm "H:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data\Skyrim.esm"
```
