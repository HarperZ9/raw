# RAW Engine Automated GPU/CPU Benchmarks and Visual Invariants

This document records live, reproducible measurements produced by [`tools/raw_bench_suite.cpp`](../tools/raw_bench_suite.cpp) across RAW Engine's fifteen core GPU and CPU subsystems. Every GPU timing is measured directly on the Direct3D 12 command queue via `ID3D12QueryHeap` (`D3D12_QUERY_HEAP_TYPE_TIMESTAMP`) normalized by `GetTimestampFrequency()`, with the D3D12 Debug Layer enabled (`0` validation errors required). Every CPU timing is measured with `std::chrono::steady_clock` and summarized with `mean`, `p50`, `p95`, and `p99` percentiles.

No numbers in this document are estimated or synthetic. All figures below come verbatim from live runs of `raw_bench_suite.exe`.

## Reference Testbed

| Component | Specification |
| --- | --- |
| GPU (Hardware DX12) | NVIDIA GeForce RTX 4090 (24,138 MB dedicated VRAM, Driver 32.0.16.1088) |
| GPU (Software Reference) | Microsoft Basic Render Driver (DX12 WARP) |
| CPU | 13th Gen Intel Core i7-13700KF (16 cores / 24 logical processors) |
| System Memory | 32 GB RAM (34,115,358,720 bytes) |
| OS | Windows 11 Build 26320 (x64) |
| Compiler / Flags | MSVC (Visual Studio 18) `/std:c++20 /W4 /WX /O2 /MD` |
| D3D12 Debug Layer | Enabled (`ID3D12Debug` + `ID3D12InfoQueue`, strict `d3d12DebugErrors == 0`) |
| Tamriel World Data | Local `Skyrim.esm` + `Skyrim - Meshes0.bsa` (gracefully skipped when absent) |

## Executive Scorecard

### 1. Hardware GPU Full Run (NVIDIA GeForce RTX 4090, 30 Warmup + 60 Measured Frames)

Command: `raw_bench_suite.exe --json bench_rtx4090_full.json`
Exit Code: `0` | D3D12 Debug Errors: `0` | GPU Timestamp Clock: `1,000,000,000 Hz`

| # | Subsystem | Status | Primary Measured Result | Percentiles / Invariants | Budget / Target |
| --- | --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` NS Fire + `256^2` SWE Water) | PASS | Fire `0.224 ms` + Water `0.019 ms` = **`0.243 ms`** | Combined `p50=0.243`, `p95=0.246`, `p99=0.252 ms`; `Tmax=2550 K`; `NaN=0` | `< 0.50 ms` HW GPU (`2.05x` headroom) |
| 2 | GPU 4-Cascade Radiance Cascades GI | PASS | **`0.108 ms`** mean GPU time | `p50=0.109`, `p95=0.109`, `p99=0.110 ms`; `2xRatio=2.000`; `Umbra=0.0%`; `NaN=0` | `< 0.35 ms` HW GPU (`3.23x` headroom) |
| 3 | Procedural PBR Texture Forge (`RawTextureForge`) | PASS | **`8.88 MPix/s`** (`177.2 ms` total) | 8 seamless sets @ `256x256` (3 PBR maps/set); per-set `p50=21.75`, `p95=30.43 ms`; max edge diff `11.61` | 8/8 sets valid |
| 4 | 48 kHz FDN-8 Hadamard + HRTF Audio DSP | PASS | Ratio **`0.0070`** (**`143.8x`** real-time) | `13.95 ms` CPU to render `2,005.3 ms` stereo (188x512 blocks); `p95=0.083 ms`; `RMS=0.027`, `Peak=0.212`, `NaN=0` | Ratio `< 0.15` (`> 6.5x` RT) |
| 5 | 3D Voronoi CSG Destruction Physics | PASS | Fracture **`0.39 ms`**; Rigid step **`5.3 us/step`** | 5 archetypes, 90 shards, 180 60 Hz steps (`p95=12.6 us`); volume error **`0.000000%`** | Vol err `< 1e-4%`, finite |
| 6 | Biomechanical 3D Flora & Hydraulic Hydrology | PASS | **`609.6 trees/s`**; **`145.1 cells/s`** | 10 trees (`904` meshlets, `0` errors); 4 eroded cells (`368` meshlets, `0` errors) | Strict validator `0` errors |
| 7 | Multi-Cell Tamriel ESM Streamer | PASS | Riverwood 3x3 **`219.7 ms`**; Warm LRU **`72.1%`** | `11,186` cells + `12,990` models indexed (`364.6 ms`); `5,401` meshlets; Whiterun `141.3 ms` (`604` hits / `234` misses) | Clean ring load & LRU reuse |
| 8 | GPU XPBD Soft-Body / Hair / Bush-Bones (`4,096p`) | PASS | **`0.0579 ms`** mean GPU time | `256` chains / `4,096` particles, 4 substeps; `p50=0.0584`, `p95=0.0584`, `p99=0.0588 ms`; `stretch=1.0003`, `minSD=+0.0050`, `drift=0.0`, `NaN=0` | `< 0.25 ms` HW GPU (`4.32x` headroom) |
| 9 | GPU Hi-Z Pyramid (`512^2`) + 16k Instance Cull | PASS | Hi-Z `0.0211 ms` + Cull `0.0080 ms` = **`0.0291 ms`** | Combined `p50=0.0287`, `p95=0.0297`, `p99=0.0297 ms`; `vis=5120`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` | `< 0.15 ms` HW GPU (`5.15x` headroom) |
| 10 | GPU 16x9x24 3D Clustered 1,024-Light Grid | PASS | **`0.0977 ms`** (`Bld 0.0035 + Asn 0.0859 ms`) | Combined `p50=0.0973`, `p95=0.0983`, `p99=0.0983 ms`; `lit=3410/3456`, `links=116168`, `ovf=0`, `NaN=0` | `< 0.15 ms` HW GPU (`1.54x` headroom) |
| 11 | GPU `160x90x64` 3D Volumetric Froxels (`921.6k`) | PASS | **`0.0447 ms`** (`Inj 0.0297 + Int 0.0113 ms`) | Total `p50=0.0451`, `p95=0.0451`, `p99=0.0455 ms`; `Mie=74.5x`, `tau=5.46/0.31`, `nonMono=0`, `NaN=0` | `< 0.25 ms` HW GPU (`5.59x` headroom) |
| 12 | GPU Variable-Penumbra PCSS Shadows (`960x540`) | PASS | **`0.0280 ms`** mean GPU time (`host=0.0246 ms`) | `p50=0.0276`, `p95=0.0287`, `p99=0.0297 ms`; `ratio=32.0x`, `open=1.0`, `umb=0.0`, `sky=1.0`, `NaN=0` | `< 0.15 ms` HW GPU (`5.36x` headroom) |
| 13 | GPU Jimenez GTAO + Bent Normals (`960x540`) | PASS | **`0.0541 ms`** (`Main 0.0420 + Den 0.0072 ms`) | Total `p50=0.0543`, `p95=0.0543`, `p99=0.0553 ms`; `flr=0.966`, `crv=0.615`, `bx=-0.174`, `pit=0.680`, `NaN=0` | `< 0.15 ms` HW GPU (`2.77x` headroom) |
| 14 | GPU GGX Stochastic SSR (`960x540`) | PASS | **`0.0342 ms`** mean GPU time (`host=0.0307 ms`) | `p50=0.0338`, `p95=0.0349`, `p99=0.0358 ms`; `conf=1.000`, `R/B=15.0x`, `sky=0.0`, `span=153->281`, `NaN=0` | `< 0.15 ms` HW GPU (`4.39x` headroom) |
| 15 | GPU 256-Bin Auto-Exposure (`960x540`) | PASS | **`0.0132 ms`** (`H 0.0072 + A 0.0020 ms`) | Total `p50=0.0133`, `p95=0.0133`, `p99=0.0137 ms`; `drk=8.87x`, `glc=0.022x`, `ffErr=0.81%`, `asym=3.12x`, `NaN=0` | `< 0.08 ms` HW GPU (`6.06x` headroom) |

### 2. Quick CI Run Comparison: RTX 4090 vs. DX12 WARP (`--quick`)

Commands:
- Hardware GPU: `raw_bench_suite.exe --quick --json bench_rtx4090_quick.json` (`6` warmup + `10` measured GPU frames)
- Software WARP: `raw_bench_suite.exe --quick --warp --json bench_warp_quick.json` (`2` warmup + `4` measured GPU frames)

Both runs exited with code `0`, `allPassed: true`, and `d3d12DebugErrors: 0`.

| # | Subsystem | RTX 4090 (`--quick`) | DX12 WARP (`--quick --warp`) | Key Invariant Match |
| --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` + `256^2`) | Fire `0.222 ms` + Water `0.019 ms` (`0.242 ms` comb, `p95=0.246 ms`) | Fire `139.579 ms` + Water `3.490 ms` (`143.069 ms` comb, `p95=144.165 ms`) | `0` NaNs on both; `Tmax=2362 K` (HW) / `1968 K` (WARP, fewer warmup steps) |
| 2 | GPU Radiance Cascades GI | `0.107 ms` (`p50=0.107`, `p95=0.108 ms`) | `23.796 ms` (`p50=23.645`, `p95=24.477 ms`) | `openIrrad=0.5737`, `umbraIrrad=0.0000`, `2xRatio=2.0000` (bit-identical) |
| 3 | Procedural PBR Forge (8 sets @ `128x128`) | `8.90 MPix/s` (`44.2 ms`, `p95=7.56 ms`) | `9.08 MPix/s` (`43.3 ms`, `p95=7.46 ms`) | `8/8` sets, max edge diff `23.46` |
| 4 | 48 kHz FDN-8 + HRTF Audio (`1,003 ms`) | Ratio `0.0067` (`148.4x` RT, `6.76 ms`) | Ratio `0.0069` (`144.6x` RT, `6.93 ms`) | `RMS=0.0265`, `Peak=0.2116`, `0` NaNs |
| 5 | 3D Voronoi CSG Destruction (60 steps) | Frac `0.40 ms`, step `6.6 us` (`p95=10.7 us`) | Frac `0.39 ms`, step `7.2 us` (`p95=12.7 us`) | `90` shards, `volErr=0.000000%`, all finite |
| 6 | Flora (5 trees) & Hydrology (2 cells) | `591.6 trees/s`, `182.2 cells/s` | `543.7 trees/s`, `174.7 cells/s` | `454` flora + `184` hydro meshlets, `0` errors |
| 7 | Tamriel ESM Streamer | RW `215.3 ms`, WR `148.8 ms`, LRU `72.1%` | RW `231.4 ms`, WR `147.2 ms`, LRU `72.1%` | `11,186` cells, `604` hits / `234` misses |
| 8 | GPU XPBD Soft-Body (`256` chains / `4,096p`) | `0.0573 ms` (`p50=0.0573`, `p95=0.0579 ms`) | `0.9653 ms` (`p50=0.9714`, `p95=1.0899 ms`) | `minSD=+0.0050`, `drift=0.0`, `stretch<=1.0013`, `0` NaNs on both |
| 9 | GPU Hi-Z (`512^2`) + 16k Instance Cull | Hi-Z `0.0207` + Cull `0.0081` (`0.0288 ms` comb, `p95=0.0292 ms`) | Hi-Z `0.6074` + Cull `0.2311` (`0.8386 ms` comb, `p95=0.9283 ms`) | Bit-identical `vis=5120`, `meshlets=20477`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` |
| 10 | GPU 3D Clustered 1,024-Light Grid (`3,456` cl) | `0.0980 ms` (`Bld 0.0035 + Asn 0.0858 ms`, `p95=0.0983 ms`) | `1.9342 ms` (`p50=1.9317`, `p95=2.0347 ms`) | Bit-identical `lit=3410/3456`, `links=116168`, `ovf=0`, `NaN=0` |
| 11 | GPU `160x90x64` 3D Volumetric Froxels (`921.6k`) | `0.0446 ms` (`Inj 0.0287 + Int 0.0123 ms`, `p95=0.0451 ms`) | `13.1031 ms` (`p50=13.0262`, `p95=13.3374 ms`) | Bit-identical `Mie=74.45x`, `tau=5.4645/0.3127`, `nonMono=0`, `NaN=0` |
| 12 | GPU Variable-Penumbra PCSS (`960x540`) | `0.0282 ms` (`p50=0.0282`, `p95=0.0287 ms`) | `6.4509 ms` (`p50=6.4607`, `p95=6.4749 ms`) | Bit-identical `ratio=32.00x`, `open=1.0`, `umb=0.0`, `sky=1.0`, `NaN=0` |
| 13 | GPU Jimenez GTAO + Bent Normals (`960x540`) | `0.0484 ms` (`Main 0.0369 + Den 0.0082 ms`, `p95=0.0492 ms`) | `10.4448 ms` (`Main 6.9673 + Den 3.3761 ms`, `p95=10.5265 ms`) | Matching `flr=0.9664`, `crv=0.6147`, `bx=-0.1737`, `pit=0.6804`/`0.6803`, `skyBad=0`, `NaN=0` |
| 14 | GPU GGX Stochastic SSR (`960x540`) | `0.0344 ms` (`host=0.0317 ms`, `p95=0.0354 ms`) | `11.3486 ms` (`host=11.5022 ms`, `p95=11.4825 ms`) | Bit-identical `targetPx=9078`, `conf=1.0000`, `R/B=15.01x`, `sky=0.0`, `span=153->281`, `NaN=0` |
| 15 | GPU 256-Bin Auto-Exposure (`960x540`) | `0.0128 ms` (`H 0.0072 + A 0.0020 ms`, `p95=0.0133 ms`) | `0.8693 ms` (`H 0.8494 + A 0.0163 ms`, `p95=0.9137 ms`) | Bit-identical `drk=8.8741x`, `glc=0.0220x`, `ffErr=0.8121%`, `asym=3.1214x`, `nanFinite=true` |

### 3. Graceful Missing-Data Verification

Running `raw_bench_suite.exe --quick --esm "C:\nonexistent\Skyrim.esm" --json bench_skip_esm.json` passes all 14 engine-native benchmarks, marks `tamrielStreamer.skipped = true` (`SKIP`), reports `d3d12DebugErrors = 0`, and exits with code `0` so headless CI runners without Skyrim data stay green.

## Detailed Subsystem Breakdown and Invariants

### 1. GPU Navier-Stokes Fire/Smoke (`64^3`) + Shallow-Water Equations (`256^2`)
- **Implementation**: `RawFluidSimHost` (`src/d3d12_bridge/RawFluidSimHost.h/.cpp`) driving SM 6.6/6.10 compute shaders (`raw_fluid_*_cs.h`).
- **Measured Workload**: Per frame, injects a buoyant `2,600 K` torch flame emitter into the `64^3` MAC grid, executes vorticity confinement, pressure projection (8 Jacobi iterations), MacCormack advection, blackbody radiation bake, and steps the `256^2` shallow-water heightfield with a moving disturbance splash and normal/foam update.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Fire GPU pass: `mean=0.2243 ms`, `p50=0.2243 ms`, `p95=0.2263 ms`, `p99=0.2324 ms`
  - Water GPU pass: `mean=0.0191 ms`, `p50=0.0195 ms`, `p95=0.0195 ms`, `p99=0.0195 ms`
  - Combined GPU pass: `mean=0.2434 ms`, `p50=0.2427 ms`, `p95=0.2458 ms`, `p99=0.2518 ms`
  - CPU command recording overhead: `mean=0.0617 ms`, `p50=0.0549 ms`, `p95=0.0903 ms`
- **Invariants**: Zero NaN/Inf across all readback fire voxels and water texels (`fireNanCount == 0`, `waterNanCount == 0`), active combustion temperature (`peakTempK > 450 K`), and combined HW GPU mean `< 0.50 ms`.

### 2. GPU 4-Cascade Radiance Cascades Global Illumination
- **Implementation**: `RawRadianceHost` (`src/d3d12_bridge/RawRadianceHost.h/.cpp`) running `256x256` RGBA16F 4-cascade raymarch, angular merge, and irradiance resolve passes.
- **Measured Workload**: Emissive light disc occluded by an opaque wall separating an illuminated open probe `(88, 128)` from a deep-umbra probe `(176, 128)`, followed by a 2x emitter brightness pass to verify exact linear radiance transport.
- **Full RTX 4090 Breakdown (60 frames)**:
  - GI GPU pass: `mean=0.1082 ms`, `p50=0.1085 ms`, `p95=0.1086 ms`, `p99=0.1096 ms`
  - CPU command recording overhead: `mean=0.0309 ms`, `p50=0.0255 ms`, `p95=0.0587 ms`
  - Measured probes (identical on RTX 4090 and WARP): `openIrrad=0.5737`, `umbraIrrad=0.0000`, `linearScaleRatio=2.0000`
- **Invariants**: `nanCount == 0`, `openIrrad > 0.05`, `umbraIrrad < 0.25 * openIrrad`, `|linearScaleRatio - 2.0| < 0.05`, and HW GPU mean `< 0.35 ms`.

### 3. Procedural PBR Texture Forge
- **Implementation**: `RawTextureForge` (`src/materials/RawTextureForge.h/.cpp`) generating all 8 tileable PBR material families (Albedo, Normal, ORMH + mip chain) on CPU with zero external image assets.
- **Throughput**: `8.88 MPix/s` (`177.2 ms` total for 8 sets at `256x256`, `p50=21.75 ms`, `p95=30.43 ms` per set); `8.90 MPix/s` at `128x128` (`44.2 ms` total in `--quick` mode).

### 4. 48 kHz FDN-8 Hadamard Reverb + Woodworth HRTF Spatial Audio DSP
- **Implementation**: `RawAudioDsp` (`src/audio/RawAudioDsp.h/.cpp`), combining `SoundscapeSynth` (Nordic stone crypt FDN-8 Hadamard reverb, procedural wind, crackling fire, flowing water) and `SpatialVoice3D` (orbiting Doppler + Woodworth ITD/ILD HRTF source) in 512-frame stereo blocks at `48,000 Hz`.
- **Full Run**: `13.95 ms` CPU to render `2,005.3 ms` of stereo audio (`headroomRatio=0.0070`, **`143.8x`** faster than real-time; per-block `p50=0.0725 ms`, `p95=0.0829 ms`, `p99=0.1098 ms`).
- **Invariants**: `headroomRatio < 0.15` (`> 6.5x` real-time), `nanCount == 0`, `rmsLevel > 1e-4`, `peakAbs <= 2.0`.

### 5. 3D Voronoi CSG Destruction Physics
- **Implementation**: `RawDestructionEngine` (`src/worldgen/RawDestructionEngine.h/.cpp`), fracturing all 5 built-in convex destructible archetypes into 18 Voronoi CSG polyhedral shards each (`90` shards total) and stepping a 60 Hz rigid-body impulse simulation (`180` steps in full mode, `60` in quick mode).
- **Full Run**: Mean fracture latency `0.3862 ms` (`p50=0.3842 ms`, `p95=0.4651 ms`), 60 Hz rigid-body step `5.25 us` mean (`p50=4.05 us`, `p95=12.60 us`, `p99=24.96 us`).
- **Invariants**: Signed-tetrahedron volume conservation error `< 1e-4%` (measured **`0.000000%`** across all 5 archetypes) and 100% finite rigid-body transforms.

### 6. Biomechanical 3D Flora and Hydraulic Terrain Erosion
- **Implementation**: `RawFloraEngine` + `RawHydrologyGen` + `RawTerrainGen` + `RawMeshPipeline`, verified against `ValidateProceduralMeshStrict` (`src/pipeline/RawAssetValidator.h`).
- **Full Run**:
  - Flora (all 5 species x 2 reps = 10 trees): `609.6 trees/s` (`treeMs mean=1.271 ms`, `p95=1.986 ms`), `904` DX12 AS+MS meshlets, `0` validation errors.
  - Hydrology (4 `65x65` cells with 4,096 hydraulic droplets, 24 thermal talus passes, D8 watershed flow, and 64v/126t meshlet build): `145.1 cells/s` (`cellMs mean=6.444 ms`, `p95=6.545 ms`), `368` meshlets, `0` validation errors.

### 7. Multi-Cell Tamriel Streaming (`Skyrim.esm` + `Skyrim - Meshes0.bsa`)
- **Implementation**: `RawTamrielWorldStreamer` (`src/worldgen/RawTamrielWorldStreamer.h/.cpp`) backed by `RawEsmWorldLoader`, `RawEsmModelResolver`, `RawBsaArchive`, and `RawNifLoader`.
- **Measured Run**: Indexed `11,186` Tamriel exterior cells and `12,990` STAT/TREE/ACTI/FURN/CONT/DOOR/MSTT/LIGH models in `364.6 ms`. Cold 3x3 ring stream centered on Riverwood `(4, -12)` completed in `219.7 ms` (`9` cells, `5,401` meshlets). Subsequent jump to Whiterun `(5, -3)` completed in `141.3 ms`, followed by warm revisit verifying **`72.1%`** LRU mesh cache hit rate (`604` hits, `234` misses).

### 8. GPU XPBD Soft-Body, Hair, Cloth and Articulated Bush-Bone Solver
- **Implementation**: `RawXpbdSimHost` (`src/d3d12_bridge/RawXpbdSimHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawXpbdClothSim.hlsl` (`raw_xpbd_cloth_cs.h`) in a single groupshared compute dispatch (`[numthreads(64, 1, 1)]`, 4 chains per threadgroup, 16 nodes per chain).
- **Measured Workload**: Simulates 256 chains (`96` hair strands, `80` cloth cape ribbons, `80` articulated bush branches = `4,096` active particles) with 4 XPBD substeps per frame under `8.5 m/s` Beaufort wind gusts, `0.25` winter snow load, distance and 2-hop bending compliance, and 3 analytic colliders (head sphere, torso capsule, bush wake sphere).
- **Full RTX 4090 Breakdown (60 frames)**:
  - XPBD GPU pass: `mean=0.0579 ms`, `p50=0.0584 ms`, `p95=0.0584 ms`, `p99=0.0588 ms` (`hostStepMs=0.0543 ms`)
  - CPU command recording overhead: `mean=0.0199 ms`, `p50=0.0152 ms`, `p95=0.0418 ms`
  - Readback invariants: `maxRootDrift=0.0000000`, `maxStretchRatio=1.0003`, `minColliderSD=+0.0050`, `nanCount=0`
- **Invariants**: `nanCount == 0`, `maxRootDrift < 1e-5`, `maxStretchRatio < 1.03`, `minColliderSD >= -0.01`, and HW GPU mean `< 0.25 ms`.

### 9. GPU Conservative Hi-Z Occlusion Pyramid and 16,384-Instance Culler
- **Implementation**: `RawGpuCullHost` (`src/d3d12_bridge/RawGpuCullHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawGpuCull.hlsl` (`raw_hiz_reduce_cs.h` + `raw_cull_instances_cs.h`), producing `ExecuteIndirect`-ready `DrawIndexed` and `DispatchMesh` arguments with wave-compacted atomics.
- **Measured Workload**: Builds a full 10-mip Standard-Z conservative `max()` Hi-Z pyramid from a `512x512` depth buffer containing a giant `z=0.25` occluder wall, then culls `16,384` deterministic instances (`4,096` frustum-outside, `1,024` distance-LOD, `6,144` completely behind wall, `2,560` in front of wall, `1,536` peeking past wall edge, `1,024` straddling the near plane).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Hi-Z pyramid build: `mean=0.0211 ms`, `p50=0.0215 ms`, `p95=0.0215 ms`, `p99=0.0215 ms` (`hostHiZMs=0.0174 ms`)
  - 16,384-instance wave-compacted cull: `mean=0.0080 ms`, `p50=0.0082 ms`, `p95=0.0082 ms`, `p99=0.0082 ms` (`hostCullMs=0.0041 ms`)
  - Combined GPU pass: `mean=0.0291 ms`, `p50=0.0287 ms`, `p95=0.0297 ms`, `p99=0.0297 ms`
  - CPU command recording overhead: `mean=0.0446 ms`, `p50=0.0386 ms`, `p95=0.0706 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `visibleCount=5120`, `visibleMeshlets=20477`, `frustumCulled=4096`, `distanceCulled=1024`, `hizCulled=6144`, `falseOcclusionCount=0`
- **Invariants**: Exact bucket counts (`5120/4096/1024/6144`), zero duplicates, zero false survivors, zero false occlusion (`falseOcclusionCount == 0`), exact `IndirectArgsBuffer` match, and HW GPU combined mean `< 0.15 ms`.

### 10. GPU 16x9x24 3D Clustered 1,024-Light Grid Assignment
- **Implementation**: `RawLightClusterHost` (`src/d3d12_bridge/RawLightClusterHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawLightClusterSim.hlsl` (`raw_cluster_build_aabb_cs.h` + `raw_cluster_cull_lights_cs.h`) across `16x9x24 = 3,456` logarithmic-Z frustum froxels.
- **Measured Workload**: Builds all `3,456` view-space cluster AABBs and assigns `1,024` active dynamic point and spotlights with sphere-vs-AABB and cone intersection tests into a zero-atomic light-index pool (`max 64 lights/cluster`, `116,168` total links).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Cluster AABB build: `mean=0.0035 ms`, `p50=0.0035 ms`, `p95=0.0040 ms`, `p99=0.0041 ms` (`hostBuildMs=0.0035 ms`)
  - 1,024-light assignment: `mean=0.0859 ms`, `p50=0.0860 ms`, `p95=0.0860 ms`, `p99=0.0860 ms` (`hostAssignMs=0.0860 ms`)
  - Combined GPU pass: `mean=0.0977 ms`, `p50=0.0973 ms`, `p95=0.0983 ms`, `p99=0.0983 ms`
  - CPU command recording overhead: `mean=0.0305 ms`, `p50=0.0244 ms`, `p95=0.0529 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `nonEmptyClusters=3410/3456`, `maxLightsCluster=64`, `totalLightLinks=116168`, `overflowErrors=0`, `nanAabbCount=0`
- **Invariants**: Zero NaN AABBs, zero list overflows, exact cluster coverage, and HW GPU combined mean `< 0.15 ms`.

### 11. GPU `160x90x64` 3D Volumetric Froxel Fog, Mie Scattering and Temporal Reprojection
- **Implementation**: `RawVolumetricFroxelHost` (`src/d3d12_bridge/RawVolumetricFroxelHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawVolumetricFroxelSim.hlsl` (`raw_froxel_inject_cs.h` + `raw_froxel_integrate_cs.h`) over a `160x90x64` (`921,600`-voxel) `RGBA16F` exponential-Z froxel volume with ping-pong 3D temporal history.
- **Measured Workload**: Injects altitude-stratified mountain/valley mist, 2-octave wind-advected 3D mist wisps, rain density boost, Henyey-Greenstein Mie forward scattering (`g=0.68`), 16 local point lights, and `0.85` Halton-jittered 3D temporal reprojection, followed by exact analytical front-to-back Beer-Lambert slice integration (Frostbite 2015 Taylor-safe formulation).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`Inject + Integrate`): `mean=0.0447 ms`, `p50=0.0451 ms`, `p95=0.0451 ms`, `p99=0.0455 ms` (`hostInjectMs=0.0297 ms`, `hostIntegrateMs=0.0113 ms`)
  - CPU command recording overhead: `mean=0.0208 ms`, `p50=0.0179 ms`, `p95=0.0457 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `mieForwardRatio=74.4527x`, `valleyTau=5.4645` vs `peakTau=0.3127`, `nanCount=0`, `nonMonoCount=0`
- **Invariants**: Zero NaN/Inf across all `921,600` voxels, `0 < T <= 1` monotonically non-increasing in Z, forward Mie anisotropy `> 2.0x`, valley/peak optical depth stratification `> 2.5x`, and HW GPU total mean `< 0.25 ms`.

### 12. GPU Variable-Penumbra PCSS Contact-Hardening Shadows and Texel-Snapped Cascades
- **Implementation**: `RawShadowPcssHost` (`src/d3d12_bridge/RawShadowPcssHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawShadowPcssSim.hlsl` (`raw_shadow_pcss_cs.h`) with Fernando (2005) 3-stage PCSS (16-tap Vogel-disk blocker search, physical penumbra estimator, 32-tap IGN-rotated hardware-bilinear PCF) and world-space texel-snapped directional cascades (`BuildTexelSnappedCascade`).
- **Measured Workload**: Evaluates `960x540` `RG16F` `(visibility, normPenumbra)` against a `1024x1024` `R32F` texel-snapped cascade containing a near-ground contact caster (`0.8 m` height) and a tall distant-penumbra spire (`22.0 m` height) plus sky pixels (`depth = 1.0`).
- **Full RTX 4090 Breakdown (60 frames)**:
  - PCSS GPU pass: `mean=0.0280 ms`, `p50=0.0276 ms`, `p95=0.0287 ms`, `p99=0.0297 ms` (`hostPcssMs=0.0246 ms`)
  - CPU command recording overhead: `mean=0.0186 ms`, `p50=0.0158 ms`, `p95=0.0367 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `openVis=1.0000`, `nearUmbraVis=0.0000`, `farUmbraVis=0.0000`, `skyMinVis=1.0000`, `nearEdgePen=0.0312` vs `farEdgePen=1.0000` (`penumbraRatio=32.00x`), `softBandPx=4121`, `nanInfCount=0`
- **Invariants**: Open sunlit ground `== 1.0`, deep umbra `== 0.0`, sky `== 1.0`, contact-hardening penumbra ratio `> 2.0x`, `nanInfCount == 0`, and HW GPU mean `< 0.15 ms`.

### 13. GPU Jimenez GTAO, Unoccluded Bent Normals and Bilateral Denoise
- **Implementation**: `RawGtaoHost` (`src/d3d12_bridge/RawGtaoHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawGtaoSim.hlsl` (`raw_gtao_main_cs.h` + `raw_gtao_denoise_cs.h`) implementing Jimenez (SIGGRAPH 2016) horizon-angle integration, analytical unoccluded bent normals, and a 3x3 depth-preserving bilateral denoise pass into `RGBA16F` `(bentNormal.xyz, visibility)`.
- **Measured Workload**: Evaluates `960x540` ray-traced 3D synthetic depth containing an open flat floor (`Y = -2.0`), a 90-degree occluding vertical wall (`X >= +1.0`), a recessed circular pit with a 3-sided raised stone rim (`(-1.2, -2.55, 5.15)`), and a sky strip (`depth = 1.0`).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`Main + Denoise`): `mean=0.0541 ms`, `p50=0.0543 ms`, `p95=0.0543 ms`, `p99=0.0553 ms` (`hostMainMs=0.0420 ms`, `hostDenoiseMs=0.0072 ms`)
  - CPU command recording overhead: `mean=0.0281 ms`, `p50=0.0199 ms`, `p95=0.0507 ms`
  - Readback invariants (matching across RTX 4090 and WARP): `openFloorVis=0.9664` (`bentY=0.9995`), `creviceVis=0.6147` (`bentX=-0.1737` tilting away from `+X` wall), `pitVis=0.6804`, `skyBadCount=0`, `nanCount=0`
- **Invariants**: Open flat floor `vis > 0.90` & `bentY > 0.92`, 90-degree crevice `vis < 0.65` & `bentX < -0.15`, recessed pit `pitVis < openFloorVis - 0.25`, exact sky passthrough (`vis == 1.0`, `bent == (0, 0, -1)`), `nanCount == 0`, and HW GPU total mean `< 0.15 ms`.

### 14. GPU GGX Stochastic Screen-Space Reflections (SSR)
- **Implementation**: `RawSsrHost` (`src/d3d12_bridge/RawSsrHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawSsrSim.hlsl` (`raw_ssr_trace_cs.h`) implementing Heitz (2018) GGX VNDF stochastic reflection lobe sampling, adaptive 40-step view-space linear raymarching with 5-step binary refinement, depth-thickness rejection, and smooth screen-border vignette fade into `RGBA16F` `(reflectedRadiance.rgb, hitConfidence)`.
- **Measured Workload**: Evaluates `960x540` ray-traced synthetic 3D HDR color and Standard-Z depth containing a flat wet reflective floor (`Y = -1.8 m`), a bright crimson-gold emissive pillar (`X in [-1.8, +1.8]`, `Y in [-1.8, +5.2]`, `Z in [11.0, 14.0]`, HDR `(2.10, 0.52, 0.14)`), and a dark sky (`depth = 1.0`), followed by a roughness `0.28` GGX lobe spread probe.
- **Full RTX 4090 Breakdown (60 frames)**:
  - SSR GPU pass: `mean=0.0342 ms`, `p50=0.0338 ms`, `p95=0.0349 ms`, `p99=0.0358 ms` (`hostSsrMs=0.0307 ms`)
  - CPU command recording overhead: `mean=0.0221 ms`, `p50=0.0180 ms`, `p95=0.0444 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `targetPx=9078`, `meanConf=1.0000`, `meanR=2.0996`, `meanB=0.1399` (`rbRatio=15.01x`), `maxSkyConf=0.0000`, `maxOpenFloorConf=0.0000`, GGX roughness span `153 px -> 281 px`, `nanInfCount=0`
- **Invariants**: Mirror floor reflection `conf > 0.80`, `meanR > 0.70`, `R/B > 3.0x`, exact zero sky/open-floor false hits (`maxSkyConf == 0.0`, `maxOpenFloorConf == 0.0`), GGX rough lobe span strictly wider than mirror span (`roughSpanW > mirrorSpanW`), `nanInfCount == 0`, and HW GPU mean `< 0.15 ms`.

### 15. GPU 256-Bin Log-Luminance Histogram and Asymmetric Human Eye-Adaptation Auto-Exposure
- **Implementation**: `RawAutoExposureHost` (`src/d3d12_bridge/RawAutoExposureHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawAutoExposureSim.hlsl` (`raw_exposure_hist_cs.h` + `raw_exposure_adapt_cs.h`) using `groupshared uint s_hist[256]`, smooth center-weighted spatial weighting (`1..4x`), Hillis-Steele parallel prefix-sum percentile trimming (`15%..95%`), photographic EV100 calibration, and asymmetric scotopic/photopic eye-adaptation EMA (`tauBright=0.35 s`, `tauDark=1.40 s`).
- **Measured Workload**: Evaluates `960x540` `RGBA32F` HDR scenes covering a dark dungeon (`lum = 0.02`), sunlit glacier (`lum = 8.0`), `0.50` midtone poisoned with `2%` extreme `10,000.0` fireflies, `0.50` midtone poisoned with `5%` NaN/+Inf/-Inf texels, and symmetric `0.25 s` adaptation steps (`dark -> bright` vs `bright -> dark`), followed by steady-state GPU profiling.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`Histogram + Eye-Adapt`): `mean=0.0132 ms`, `p50=0.0133 ms`, `p95=0.0133 ms`, `p99=0.0137 ms` (`hostHistMs=0.0072 ms`, `hostAdaptMs=0.0020 ms`)
  - CPU command recording overhead: `mean=0.0281 ms`, `p50=0.0214 ms`, `p95=0.0487 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `darkExposure=8.8741x`, `glacierExposure=0.0220x`, `fireflyTargetLum=0.4959` (`fireflyErrPct=0.8121%`), `asymBrightFrac=0.5105` vs `asymDarkFrac=0.1635` (`asymRatio=3.1214x`), `nanFinite=true`
- **Invariants**: Dark dungeon `exposure > 4.0x`, sunlit glacier `exposure < 0.15x`, 95th-percentile firefly rejection error `< 10%`, asymmetric eye-adaptation ratio `> 1.8x`, NaN/Inf resilience `nanFinite == true`, and HW GPU total mean `< 0.08 ms`.

## Reproducing Benchmarks

### CLI Options

```text
raw_bench_suite.exe [options]
  --warp               Force DX12 WARP software adapter (for headless CI)
  --quick              Fast CI mode (10 measured GPU frames on HW, 4 on WARP; smaller CPU counts)
  --frames <N>         Override measured GPU frame count (default: 60 on HW, 8 on WARP)
  --json <path>        Write structured JSON benchmark report
  --esm <path>         Override Skyrim.esm path
  --bsa <path>         Override Skyrim - Meshes0.bsa path
  --no-debug-layer     Disable D3D12 Debug Layer (enabled by default for strict 0-error validation)
  --help               Print usage and exit
```

### Example Commands

```powershell
# Full studio benchmark on hardware GPU (30 warmup + 60 measured GPU frames)
.\raw_bench_suite.exe --json bench_hw_full.json

# Fast CI regression check on hardware GPU
.\raw_bench_suite.exe --quick --json bench_hw_quick.json

# Headless CI verification on DX12 WARP (zero hardware GPU required)
.\raw_bench_suite.exe --quick --warp --json bench_warp_quick.json
```

### Exit Code and JSON Contract

- **Exit Code `0`**: Every executed subsystem passed its numerical, visual, memory, and performance invariants AND `d3d12DebugErrors == 0`.
- **Exit Code `1`**: Any invariant failed, any hardware GPU budget was exceeded, or any D3D12 Debug Layer error occurred.
- **JSON Schema**: Top-level fields include `suite`, `mode`, `adapter`, `isWarp`, `vramMB`, `debugLayer`, `d3d12DebugErrors`, `timestampFreqHz`, `gpuWarmupFrames`, `gpuMeasuredFrames`, `allPassed`, and `subsystems.{fluidSim, radianceCascades, textureForge, audioDsp, destruction, floraAndHydrology, tamrielStreamer, xpbdSim, gpuCull, lightClusters, volumetricFroxels, shadowPcss, gtao, ssr, autoExposure}` with nested `{mean, p50, p95, p99, min, max}` latency objects for every timed stage.
