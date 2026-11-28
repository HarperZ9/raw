# RAW Engine 2027 Master Technical Architecture and Capabilities Specification (Wave 60 Milestone)

This document is the authoritative, evidence-bound engineering specification for **RAW Engine** at the **Wave 60 Milestone** (Waves 38 through 60). Every subsystem, file, GPU compute solver, HLSL SM 6.6/6.10 shader, BuildLang 1.4.0 kernel module, binary-verified 1.7.104 runtime hook, Studio CLI audit mode, and benchmark number documented below is built, compile-verified, and backed by automated test receipts in this repository.

## 1. Standalone Native Direct3D 12 Engine

RAW Engine includes a clean-room, zero-proprietary-SDK Direct3D 12 renderer and GPU compute simulation runtime supporting Shader Model 6.6 through 6.10 (`DXC 1.10`, `-HV 2021 -Gis -O3 -enable-16bit-types -WX`, `93/93` SM 6.10 passes) with strict `ID3D12Debug` + `ID3D12InfoQueue` zero-error validation on both hardware GPUs (NVIDIA GeForce RTX 4090) and software DX12 WARP.

### 1.1 Hardware Meshlet Pipeline and Procedural PBR Atlas
- **AS+MS+PS Meshlet Renderer (`src/d3d12_bridge/RawMeshHost.h/.cpp`, `RawTerrainWorld.hlsl`, `RawMeshShader.hlsl`, `RawSubdiv.hlsl`)**:
  - Packs all procedural terrain chunks, biomechanical flora, Voronoi destruction shards, procedural 3D dungeons, and reconstructed `.NIF` world geometry into 64-vertex / 126-triangle GPU meshlets (`MeshletDesc`, 48 bytes) with exact bounding spheres and backface normal cones.
  - Supports Amplification Shader frustum/cone culling, hardware Mesh Shader rasterization, and automatic VS/PS fallback paths.
- **GPU Procedural PBR `Texture2DArray` Atlas (`RawTextureForge` -> `RawMeshHost` -> `RawTerrainWorld.hlsl`)**:
  - Generates all 8 tileable PBR material families (Rock, Snow/Ice, Crystal/Metal, Bark, Basalt, Magma, Moss/Foliage, Cobble) at `128x128` x 8 mip levels (`192` subresources across Albedo, Normal, and swizzled ORMH `Texture2DArray`s) on startup with zero external image textures.
  - Samples via `SampleGrad` tri-planar projection with Ben Golus Whiteout world-normal blending, slope-aware strata snow-drift blending, and smooth distance fade (`2,600..11,000u`).

### 1.2 Real-Time GPU Compute Solvers (16 Live Compute Solvers + GPU Telemetry HUD)
1. **GPU Navier-Stokes Fire/Smoke (`64^3`) + Shallow-Water Equations (`256^2`) (`RawFluidSimHost`)**: `64^3` MAC-grid buoyant combustion with vorticity confinement, adaptive Jacobi pressure projection (`4..8` iters), MacCormack advection, Planck blackbody radiation bake, and coupled `256^2` shallow-water wakes/normals/foam. RTX 4090: **`0.242 ms`** combined (`< 0.50 ms` budget, `0` NaNs).
2. **GPU 4-Cascade Radiance Cascades Global Illumination (`RawRadianceHost`)**: `256x256` RGBA16F 4-cascade raymarch, angular interval merge, and irradiance resolve. RTX 4090: **`0.107 ms`** (`< 0.35 ms` budget), exact **`2.000x`** linear energy scaling, `0.0%` umbra leak.
3. **GPU XPBD Soft-Body, Hair, Cloth and Articulated Bush-Bone Solver (`RawXpbdSimHost`, `RawXpbdClothSim.hlsl`)**: Single groupshared compute dispatch (`[numthreads(64, 1, 1)]`) simulating `256` chains (`4,096` particles) across 4 XPBD substeps with live Beaufort wind gusts, snow load, and live 24-bone character Foot-IK anchors. RTX 4090: **`0.0579 ms`** (`< 0.25 ms` budget, `0.0` root drift, `stretch <= 1.0003`).
4. **GPU Conservative Hi-Z Occlusion Pyramid (`512^2`) and 16k-Instance Culler (`RawGpuCullHost`, `RawGpuCull.hlsl`)**: 10-mip conservative `max()` Standard-Z Hi-Z pyramid + wave-compacted frustum/distance/Hi-Z culling emitting `ExecuteIndirect` `DrawIndexed`/`DispatchMesh` args. RTX 4090: **`0.0290 ms`** (`< 0.15 ms` budget, `0` false occlusions).
5. **GPU `16x9x24` 3D Clustered 1,024-Light Grid (`RawLightClusterHost`, `RawLightClusterSim.hlsl`)**: Builds `3,456` logarithmic-Z view-space AABBs and assigns `1,024` dynamic point/spot lights with zero atomic overflows (`116,168` links). RTX 4090: **`0.0979 ms`** (`< 0.15 ms` budget).
6. **GPU `160x90x64` (`921.6k`-Voxel) 3D Volumetric Froxels (`RawVolumetricFroxelHost`, `RawVolumetricFroxelSim.hlsl`)**: Exponential-Z valley/mountain mist, 2-octave wind wisps, Henyey-Greenstein Mie forward scatter (`74.5x` anisotropy), `0.85` Halton-jittered temporal reprojection, and Frostbite analytical slice integration. RTX 4090: **`0.0447 ms`** (`< 0.25 ms` budget).
7. **GPU Variable-Penumbra PCSS Contact-Hardening Shadows (`RawShadowPcssHost`, `RawShadowPcssSim.hlsl`)**: Fernando 3-stage PCSS (16-tap Vogel blocker search + physical penumbra estimator + 32-tap IGN bilinear PCF) over texel-snapped cascades (`32.0x` contact-hardening penumbra ratio). RTX 4090: **`0.0281 ms`** (`< 0.15 ms` budget).
8. **GPU Jimenez GTAO + Unoccluded Bent-Normal & Bilateral Denoise (`RawGtaoHost`, `RawGtaoSim.hlsl`)**: Horizon-based cosine-weighted ambient occlusion with analytical bent normals and 3x3 bilateral denoise (`960x540`). RTX 4090: **`0.0484 ms`** (`< 0.15 ms` budget).
9. **GPU Stochastic GGX Screen-Space Reflections (`RawSsrHost`, `RawSsrSim.hlsl`)**: Heitz GGX VNDF stochastic lobe raymarch with binary refinement and roughness-adaptive cone spread (`960x540`). RTX 4090: **`0.0343 ms`** (`< 0.15 ms` budget).
10. **GPU 256-Bin Log-Luminance Histogram & Eye-Adaptation Auto-Exposure (`RawAutoExposureHost`, `RawAutoExposureSim.hlsl`)**: Groupshared 256-bin log2 histogram, Hillis-Steele percentile trimming (`15%..95%`), firefly rejection (`0.81%` err), and asymmetric scotopic/photopic EMA (`3.12x` ratio). RTX 4090: **`0.0127 ms`** (`< 0.08 ms` budget).
11. **GPU 3D Volumetric Cloud Raymarcher & Temporal Reprojection (`RawCloudRaymarchHost`, `RawCloudRaymarchSim.hlsl`)**: Earth-curved spherical cloud shell (`1.5..5.5 km`), 32 IGN-jittered steps, 4-tap directional sun march, Beer-Powder + dual-lobe HG silver-lining (`4.73x` anisotropy). RTX 4090: **`0.0833 ms`** (`< 0.15 ms` budget).
12. **GPU Cinematic Tile-Max Motion Blur & Hexagonal Bokeh DoF (`RawPostCinematicHost`, `RawPostCinematicSim.hlsl`)**: 16x16 tile-max + 3x3 neighbor-max dilation, McGuire 12-tap motion blur, 18-tap thin-lens hexagonal bokeh DoF with zero foreground bleed, and `cos^4` optical vignette. RTX 4090: **`0.0459 ms`** (`< 0.08 ms` budget).
13. **GPU 16,384-Particle 3D Curl-Noise Physics & Additive Splat (`RawGpuParticlesHost`, `RawGpuParticlesSim.hlsl`)**: Persistent 16K 3D Embers/Snow/Rain/WaterfallMist simulation with divergence-free curl turbulence, elastic bounce, and lock-free Q22.10 HDR splatting. RTX 4090: **`0.0162 ms`** (`< 0.08 ms` budget).
14. **GPU RAW-TSR 2x Temporal Super-Resolution & RCAS Sharpening (`RawTsrUpscaleHost`, `RawTsrUpscaleSim.hlsl`)**: Halton `(2,3)` subpixel jitter, YCoCg variance-box anti-ghosting history clamp, reactive mask handling, and ringing-safe FidelityFX RCAS sharpening (Benchmark #19).
15. **GPU Spectral Water Caustics & Refraction Field (`RawWaterCausticsHost`, `RawWaterCausticsSim.hlsl`)**: Snell water-air refraction, chromatic RGB Beer-Lambert underwater attenuation, and Jacobian-clamped caustic light sheets.
16. **Zero-Dependency GPU Compute Telemetry HUD (`RawOverlayHost`, `RawOverlayPass.hlsl`)**: Procedural 5x7 bitmap HUD & sparklines (`~0.006 ms` GPU, opt-in `F9`/`H`, default OFF in live Skyrim bridge).

Combined 13-solver benchmarked GPU compute footprint on RTX 4090 (Benchmarks #1, #2, #8-#18 in `docs/BENCHMARKS.md`): **`0.848 ms`** total per frame (`5.1%` of a `16.67 ms` 60 FPS frame budget), leaving massive headroom for RAW-TSR (#19) and rasterization!

### 1.3 Film-Grade Shader Pipeline (Sky, BRDF, Weathering, Water, Clouds and Display)
- **Observatory Sky & Atmosphere (`RawCosmosSky.hlsli`, `AtmosphereLut.hlsl`, `EnvCubePass.hlsl`, `DistantLod.hlsli`)**: Hillaire 2020 LUTs with Chappuis ozone twilight (indigo zenith, Belt of Venus anti-solar arch, Earth Shadow), tilted Milky Way with carbon-dust rifts, H-II/O-III nebulae, ecliptic zodiacal light, 10-stratum 3D auroras (`N2+` 427.8 / `O` 557.7 / `O` 630.0 nm), orbital 24d Masser / 16d Secunda phases, Kolmogorov star scintillation, Eddington solar limb darkening + Mie corona, `22.0 deg` ice halos/sundogs, and unified `cloudShadowN` + seasonal snowline horizon cohesion.
- **Material Transport, Weathering, Shadows and Post-Processing (`StrataBRDF.hlsli`, `RawWeathering.hlsli`, `RawFloraWind.hlsli`, `ShadowAtlas.hlsl`, `NormalUnpack.hlsl`, `Lighting.hlsl`, `AoComposite.hlsl`, `Sss.hlsl`, `FroxelPass.hlsl`, `DepthFog.hlsl`, `DofPass.hlsl`, `Rawlss5Pass.hlsl`, `BloomPass.hlsl`, `RtReflFilter.hlsl`, `GiAtrousPass.hlsl`, `Tonemap.hlsl`, `Clouds.hlsl`, `VolumetricPass.hlsl`, `RtWater.hlsl`, `RawMaterials3D.hlsli`, `RawSpells3D.hlsli`, `LightFog.hlsl`)**: Kulla-Conty/Turquin multi-scatter GGX, Charlie cloth sheen, Airy iridescence, dual-lobe wetness clearcoat, rainwater puddle porosity & hoarfrost rime sparkle, Toksvig normal-variance specular anti-aliasing, texel-snapped Vogel-disk PCF cascades, Beer-Lambert RGB water caustics, Jimenez multi-bounce AO, Christensen-Burley SSS, gust-envelope root-pinned flora wind, and AgX tonemapping with Purkinje scotopic rod-vision shift.

### 1.4 Native Executables
- **`tools/raw_engine_viewer.cpp` (`raw_engine_viewer.exe`)**: Interactive Win32/DX12 viewer uniting Frontier 6-biome showcase, live multi-cell Tamriel streaming (`--esm`), landmark teleports (`L`), playable grounded Walk Mode (`P`) + 24-bone Foot-IK -> XPBD cloth coupling, physics raycast (`C`), `F5`/`F8` CRC-32 Quicksave/Quickload, `F9`/`H` GPU HUD, `N` NavMesh + squad AI, `I` wildfire/snowmelt ecosystem, `V` wildlife boids, `E` 9-hold caravan economy, `O` radiant world events, `D` procedural 3D dungeon explorer, `X` real-time combat skirmish, `Q` radiant quest journal, `F12` `.bmp` capture, and `16` live GPU compute solvers with headless WARP CI verification.
- **`tools/raw_sandbox_render.cpp` (`raw_sandbox_render.exe`)**: Headless/interactive DX12 sandbox renderer (12/12 HW & WARP self-tests).
- **`tools/raw_bench_suite.cpp` (`raw_bench_suite.exe`)**: 19-subsystem automated GPU/CPU benchmark and visual invariant scorecard with `ID3D12QueryHeap` timestamps and JSON output.
- **`tools/raw_forge_cli.cpp` (`raw_forge_cli.exe`)**: 18-mode studio batch baking, Bethesda format inspection, physics, NavMesh, ecosystem, wildlife, settlement, radiant events, diplomacy, CRC-32 world snapshot, procedural dungeon, 24-bone skeleton Foot-IK, 8v8 combat sandbox, and quest story DAG audit CLI.

## 2. Clean-Room Bethesda Format Stack

All parsers are 100% original clean-room MIT C++20/23 implementations with zero Bethesda SDK, Windows, or third-party compression dependencies (built-in RFC 1950/1951 zlib inflate and LZ4 block decode):

| Format | Module | Verified Capabilities |
| --- | --- | --- |
| `.ESM` / `.ESP` Terrain & Placement | `RawEsmWorldLoader`, `RawEsmModelResolver` | Indexes all `11,186` Tamriel exterior cells & `12,990` models in `~360 ms`; decodes `LAND` (`VHGT`/`VNML`/`VCLR`/`VTEX`) and 6-DOF `REFR` placements. |
| `.ESM` World Metadata | `RawEsmNpcWeatherLoader` | Streams `WTHR` (`84`), `CLMT` (`6`), `NPC_` (`5,118`), `REGN` (`317`), `QUST` (`1,811`) from `Skyrim.esm` in **`~44 ms`**, mapping canonical `WTHR` records into physical `RawWeatherSim` regimes. |
| `.BSA` Archives (v104/v105) | `RawBsaArchive` | Hash-indexed v104 (LE/zlib) & v105 (SE/LZ4) reader with sibling `Meshes0/1.bsa` auto-discovery. |
| `.NIF` Meshes (v20.2.0.7) | `RawNifLoader` | Parses SSE `BSTriShape`/`NiTriShape` geometry and shader textures directly into validated 64v/126t meshlets and `.glb`/`.obj`. |
| `.DDS` Textures (BC1..BC7) | `RawDdsDecoder` | Decodes BC1..BC7 + uncompressed mip chains with Toksvig specular anti-aliasing roughness analysis. |
| `.ESS` Save Files (v12 LZ4) & `plugins.txt` | `rt_save_papyrus_guard`, `rt_mod_load_audit` | Audits v12 LZ4 `.ess` headers, 65,535 string-table cap, Papyrus VM hitches, `TES4` master order, and ESL slot integrity. |
| `RAWWORLD` v1 Binary + JSON World Snapshots | `RawWorldSerializer` | Bit-exact 5-pillar persistent world state container (`RAWWORLD` v1 + IEEE 802.3 CRC-32 1-bit tamper rejection + deterministic JSON report). |

## 3. World Simulation, Ecology, Physics, Dungeons, Animation, Combat, Quests and Audio

- **Biomechanical Flora & Hydraulic Terrain (`RawFloraEngine`, `RawHydrologyGen`, `RawTerrainGen`, `RawBiomeWorld`)**: 5 species x 4 seasons x 3 LODs (`616.9 trees/s`, `0` validator errors), hydraulic + thermal + D8 watershed erosion (`144.5 cells/s`, `0` errors).
- **Orbital Meteorology (`RawWeatherSim`)**: 365-day axial-tilt insolation, thermal balance ODE, Stefan's Law ice thickness, metamorphic snowpack compaction/albedo aging, botanical phenology, and live `Skyrim.esm` `WTHR` bridge.
- **3D Voronoi CSG Destruction (`RawDestructionEngine`)**: Exact convex polyhedral fracture across 5 archetypes (`90` shards) with **`0.000000%`** volume error (`< 1e-4%`) and **`4.3 us/step`** 60 Hz rigid-body simulation.
- **3D World Collision & NavMesh/Squad AI (`RawWorldPhysics`, `RawNavMeshAi`)**: Live Riverwood `(4,-12)` SAH BVH (`87,313` tris / `59,267` nodes, `64/64` rays hit, `47/60` grounded walk steps) + 3D NavMesh (`64x64`, `4.6 ms` bake, `22,470` links, Octile A* funnel path `45 -> 4` waypoints in `0.035 ms`, 4-agent tactical squad).
- **Coupled Living World Pillars (`RawWorldEcosystem`, `RawWildlifeBoids`, `RawSettlementSim`, `RawWorldEventDirector`, `RawFactionDiplomacy`, `RawWorldSerializer`)**:
  - Wildfire spread (`eastReach=35 > westReach=1`), rain suppression (`37 -> 0`), D8 snowmelt (`massErr=1.94e-06`), `-8 C` black-ice (`216` cells), 90-day regrowth (`fuel 0.05 -> 0.66`).
  - `512`-agent O(N) 3D spatial-hash wildlife (`AlpineHawk`, `PineCrowFlock`, `TundraDeerHerd`, `RiverSalmonSchool`) stepping in **`0.19 ms/frame`**.
  - 9-Hold living settlement & trade caravan economy (`maxCommErr=6.90e-09`, `goldErr=4.31e-08`, blizzard blockade `peakSnowbound=12`, Winterhold grain `20.66g -> 34.20g`, 30-day `226` deliveries).
  - Emergent radiant world-event director (`45` spawned over 30 days, exact accounting, `0` cooldown spam) coupled with 9-hold & 6-faction diplomacy/bounty/amnesty (`RawFactionDiplomacy`) and bit-exact IEEE 802.3 CRC-32 binary + JSON persistence (`RawWorldSerializer`).
- **Procedural 3D Dungeons, 24-Bone Character IK, RPG Combat Sandbox & Radiant Quest Story Engine (`RawDungeonGen`, `RawSkeletonAnimEngine`, `RawCombatSandboxEngine`, `RawQuestStoryEngine`)**:
  - 3 watertight vaulted 3D dungeon archetypes (`NordicBarrow`, `GlacialIceCavern`, `BasaltDwemerRuin`) with 100% Prim MST + loop BFS connectivity, strict 64v/126t GPU meshlets (`0` errors), torch/brazier/altar anchors, and live Entrance -> Boss NavMesh A* pathing (`6,398u`, `0` wall clips).
  - 24-bone humanoid procedural FK/SLERP blend tree, exact law-of-cosines 2-bone IK + FABRIK fallback, live `RawWorldPhysics` 22-deg slope Terrain Foot-IK (`0.000u` foot error, zero hyperextension), XPBD cloth/hair anchors, and **`0.16 ms/frame`** 64-rig throughput.
  - 8v8 RPG combat sandbox (`220 ms` timed parry, poise break, wet/cold elemental & stamina synergy, swept ballistic arrow wall-occlusion raycasts, `180`-tick battle in `0.15 ms`).
  - Multi-stage DAG quest state machine with strict cycle/invalid-jump rejection, branching Persuade/Bribe/Intimidate dialogue checks, 4-system radiant quest generation, and deterministic `RAW_QUEST_JOURNAL` JSON export.
- **48 kHz FDN-8 Hadamard Reverb + Woodworth HRTF Audio (`RawAudioDsp`, `RawAudioOutput`)**: **`145.3x`** real-time headroom (`0.0069` ratio, `0` NaNs) with live WASAPI output.

## 4. BuildLang 1.4.0 Core Kernel Library (14 Modules, 84 Verified Kernels)

All **14 BuildLang reference modules (`84` verified kernels)** under `native/bld/` pass both `buildc check` and `buildc run` (`buildc 1.4.0`, `rc=0`, `0` errors) with exact equation parity against RAW Engine's C++23 and HLSL kernels (see [`docs/BUILDLANG-KERNEL-MAP.md`](BUILDLANG-KERNEL-MAP.md)):
1. `RawPhysicsKernels.bld` (6), 2. `RawAtmoWeather.bld` (7), 3. `RawBiomeFlora.bld` (5), 4. `RawRenderOptics.bld` (6), 5. `RawFluidDynamics.bld` (6), 6. `RawCombatAi.bld` (6), 7. `RawEconomySim.bld` (6), 8. `RawGpuPipeline.bld` (6), 9. `RawWorldSystems.bld` (6), 10. `RawCloudsDungeon.bld` (6), 11. `RawAnimKinematics.bld` (6), 12. `RawParticlesCombat.bld` (6), 13. `RawTsrQuests.bld` (6), 14. `RawCausticsCrafting.bld` (6).

## 5. Creation Engine 1.7.104 First-Principles Runtime Rebuild (370+ Binary-Verified GMSTs)

`raw_engine.dll` + `d3d11.dll` DX12 translation bridge target **Skyrim Special Edition `1.7.104.0`** with Zydis instruction verification, fail-closed signature validation, SEH guards, **370+ binary-verified zero-overlap GMSTs across 15 modules**, and **489+ passing `ce_runtime_check` assertions**:
- **Forensics, Stability, Hot-Reload & Telemetry**: 7,376-class RTTI VEH crash symbolizer (`rt_crash_forensics`), 0-100 engine health scoreboard (`rt_engine_health`), 300-frame zero-alloc flight recorder (`rt_telemetry_recorder`), `.ESS` save & Papyrus hitch watchdog (`rt_save_papyrus_guard`), `plugins.txt` / ESL auditor (`rt_mod_load_audit`), PI frame-pacing governor (`rt_frame_governor`), live file-triggered diagnostic console (`rt_console_diag`), 4-tier RAM/VRAM auto-fallback presets (`rt_profile_presets`), live atomic shader hot-reload watcher (`rt_shader_hotreload`), self-contained live HTML telemetry dashboard (`rt_live_dashboard`), slab allocator & work-stealing job pool.
- **Combat, Magic, Stealth, Economy, Shouts, Crime, Crafting & Spellcraft GMSTs**: `220 ms` parry / dodge / hitstop (`rt_movement_combat`), skeleton/IK & soft-body coupling (`rt_skeleton_ik`, `rt_softbody`), adaptive acoustic soundscape director (`rt_adaptive_audio_director`), weather-driven NPC daily behavior (`rt_npc_behavior`), 9-hold economy ledger (`rt_economy_ledger`), and 15 binary-verified 1.7.104 GMST modules (`rt_progression_economy`, `rt_ai_packages`, `rt_world_rules_gmst`, `rt_physics_camera_gmst`, `rt_stealth_social_gmst`, `rt_locomotion_melee_gmst`, `rt_magic_physics`, `rt_archery_mount_gmst`, `rt_shouts_survival_gmst`, `rt_crime_mount_gmst`, `rt_crafting_inventory_gmst`, `rt_spellcraft_gmst`, plus core combat/stealth/AI modules).

## 6. Verified Benchmark Scorecard (Live RTX 4090 & DX12 WARP Receipts)

All measured figures below are copied verbatim from [`docs/BENCHMARKS.md`](BENCHMARKS.md) on **Intel Core i7-13700KF (16C/24T) / 32 GB RAM / NVIDIA GeForce RTX 4090 (24,138 MB VRAM, Driver 32.0.16.1088) / Windows 11 Build 26320** with D3D12 Debug Layer **ON** (`d3d12DebugErrors == 0`, exit code `0`). Benchmark #19 (`GPU RAW-TSR 2x Upscaler + RCAS`) extends this suite to 19 live benchmarks in Wave 60.

| # | Subsystem | Status | RTX 4090 Full Run | Quick CI: HW vs. WARP | Budget / Headroom |
| --- | --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` NS Fire + `256^2` SWE Water) | PASS | **`0.242 ms`** (`p95=0.244 ms`) | `0.242 ms` vs `137.836 ms` | `< 0.50 ms` (`2.06x`) |
| 2 | GPU 4-Cascade Radiance Cascades GI | PASS | **`0.107 ms`** (`2xRatio=2.000`) | `0.108 ms` vs `23.520 ms` | `< 0.35 ms` (`3.26x`) |
| 3 | Procedural PBR Texture Forge (`RawTextureForge`) | PASS | **`8.96 MPix/s`** (`175.6 ms`) | `8.95` vs `9.19 MPix/s` | 8/8 seamless sets |
| 4 | 48 kHz FDN-8 Hadamard + HRTF Audio DSP | PASS | **`145.3x` RT** (`0.0069` ratio) | `149.6x` vs `151.5x` RT | `< 0.15` (`> 6.5x` RT) |
| 5 | 3D Voronoi CSG Destruction Physics | PASS | Frac `0.41 ms`, **`4.3 us/step`** | `0.43 ms` vs `0.39 ms` | `0.000000%` volErr |
| 6 | Biomechanical 3D Flora & Hydraulic Hydrology | PASS | **`616.9 trees/s`**, **`144.5 cells/s`** | `628.4` vs `573.1 trees/s` | `0` validator errors |
| 7 | Multi-Cell Tamriel ESM Streamer | PASS | RW **`217.9 ms`**, LRU **`72.1%`** | `212.6 ms` vs `220.1 ms` | `11,186` cells indexed |
| 8 | GPU XPBD Soft-Body / Hair / Bush-Bones (`4,096p`) | PASS | **`0.0579 ms`** (`drift=0.0`) | `0.0571 ms` vs `0.9109 ms` | `< 0.25 ms` (`4.32x`) |
| 9 | GPU Hi-Z Pyramid (`512^2`) + 16k Instance Cull | PASS | **`0.0290 ms`** (`falseOcc=0`) | `0.0295 ms` vs `0.8185 ms` | `< 0.15 ms` (`5.17x`) |
| 10 | GPU `16x9x24` 3D Clustered 1,024-Light Grid | PASS | **`0.0979 ms`** (`ovf=0`) | `0.0981 ms` vs `2.2669 ms` | `< 0.15 ms` (`1.53x`) |
| 11 | GPU `160x90x64` 3D Volumetric Froxels (`921.6k`) | PASS | **`0.0447 ms`** (`Mie=74.5x`) | `0.0443 ms` vs `12.9160 ms` | `< 0.25 ms` (`5.59x`) |
| 12 | GPU Variable-Penumbra PCSS Shadows (`960x540`) | PASS | **`0.0281 ms`** (`ratio=32.0x`) | `0.0282 ms` vs `6.3992 ms` | `< 0.15 ms` (`5.34x`) |
| 13 | GPU Jimenez GTAO + Bent Normals (`960x540`) | PASS | **`0.0484 ms`** (`crv=0.615`) | `0.0485 ms` vs `10.3436 ms` | `< 0.15 ms` (`3.10x`) |
| 14 | GPU GGX Stochastic SSR (`960x540`) | PASS | **`0.0343 ms`** (`R/B=15.0x`) | `0.0351 ms` vs `11.1690 ms` | `< 0.15 ms` (`4.37x`) |
| 15 | GPU 256-Bin Auto-Exposure (`960x540`) | PASS | **`0.0127 ms`** (`asym=3.12x`) | `0.0131 ms` vs `0.9115 ms` | `< 0.08 ms` (`6.30x`) |
| 16 | GPU 3D Volumetric Clouds (`480x270`) | PASS | **`0.0833 ms`** (`silver=4.73x`) | `0.0837 ms` vs `11.0680 ms` | `< 0.15 ms` (`1.80x`) |
| 17 | GPU Cinematic MB + Bokeh DoF (`960x540`) | PASS | **`0.0459 ms`** (`bld=0.0000`) | `0.0463 ms` vs `14.0214 ms` | `< 0.08 ms` (`1.74x`) |
| 18 | GPU 16K 3D Particles + Splat (`480x270`) | PASS | **`0.0162 ms`** (`act=16384`) | `0.0169 ms` vs `0.7625 ms` | `< 0.08 ms` (`4.94x`) |
| 19 | GPU RAW-TSR 2x Upscaler + RCAS (`RawTsrUpscaleHost`) | Wave 60 | Live in `raw_bench_suite` (#19) | HW & WARP verified in `docs/BENCHMARKS.md` | `< 0.15 ms` HW GPU |

## 7. Build, Test, CLI and Launch Commands

```powershell
# Configure & Build (MSVC + Ninja + DXC 1.10)
cmake -S E:\wt\raw-rt9 -B E:\wt\raw-rt9-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build E:\wt\raw-rt9-build

# Core Runtime, DX12 Translation, GPU Compute & 19-Benchmark Studio Suite
E:\wt\raw-rt9-build\tests\ce_runtime_check.exe
E:\wt\raw-rt9-build\tests\d3d11_translate_suite.exe --no-debug
E:\wt\raw-rt9-build\tools\raw_bench_suite.exe --json bench_rtx4090_full.json
E:\wt\raw-rt9-build\tools\raw_bench_suite.exe --quick --warp --json bench_warp_quick.json

# Studio CLI 18-Mode Audits (raw_forge_cli.exe)
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --validate-all out_val --no-write
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --esm-meta
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --physics-probe
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --tamriel-stream
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --navmesh-audit
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --ecosystem-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --wildlife-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --settlement-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --events-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --diplomacy-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --world-snapshot
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --dungeon-gen
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --skeleton-ik
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --combat-sim
E:\wt\raw-rt9-build\tools\raw_forge_cli.exe --quest-sim

# Verify All 14 BuildLang 1.4.0 Kernel Modules (84 Kernels)
foreach ($m in @('RawPhysicsKernels','RawAtmoWeather','RawBiomeFlora','RawRenderOptics','RawFluidDynamics','RawCombatAi','RawEconomySim','RawGpuPipeline','RawWorldSystems','RawCloudsDungeon','RawAnimKinematics','RawParticlesCombat','RawTsrQuests','RawCausticsCrafting')) {
    buildc check "E:\wt\raw-rt9\native\bld\$m.bld"; buildc run "E:\wt\raw-rt9\native\bld\$m.bld"
}

# Interactive DX12 Viewer (Frontier Showcase or Live Tamriel Streamer)
E:\wt\raw-rt9-build\tools\raw_engine_viewer.exe --esm "H:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data\Skyrim.esm"
```
