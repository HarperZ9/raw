# RAW Engine Automated GPU/CPU Benchmarks and Visual Invariants

This document records live, reproducible measurements produced by [`tools/raw_bench_suite.cpp`](../tools/raw_bench_suite.cpp) across RAW Engine's nine core GPU and CPU subsystems. Every GPU timing is measured directly on the Direct3D 12 command queue via `ID3D12QueryHeap` (`D3D12_QUERY_HEAP_TYPE_TIMESTAMP`) normalized by `GetTimestampFrequency()`, with the D3D12 Debug Layer enabled (`0` validation errors required). Every CPU timing is measured with `std::chrono::steady_clock` and summarized with `mean`, `p50`, `p95`, and `p99` percentiles.

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
| 1 | GPU Fluid (`64^3` NS Fire + `256^2` SWE Water) | PASS | Fire `0.223 ms` + Water `0.019 ms` = **`0.243 ms`** | Combined `p50=0.243`, `p95=0.245`, `p99=0.246 ms`; `Tmax=2550 K`; `NaN=0` | `< 0.50 ms` HW GPU (`2.06x` headroom) |
| 2 | GPU 4-Cascade Radiance Cascades GI | PASS | **`0.108 ms`** mean GPU time | `p50=0.108`, `p95=0.109`, `p99=0.109 ms`; `2xRatio=2.000`; `Umbra=0.0%`; `NaN=0` | `< 0.35 ms` HW GPU (`3.26x` headroom) |
| 3 | Procedural PBR Texture Forge (`RawTextureForge`) | PASS | **`8.90 MPix/s`** (`176.7 ms` total) | 8 seamless sets @ `256x256` (3 PBR maps/set); per-set `p50=21.74`, `p95=30.29 ms`; max edge diff `11.61` | 8/8 sets valid |
| 4 | 48 kHz FDN-8 Hadamard + HRTF Audio DSP | PASS | Ratio **`0.0068`** (**`147.3x`** real-time) | `13.62 ms` CPU to render `2,005.3 ms` stereo (188x512 blocks); `p95=0.075 ms`; `RMS=0.027`, `Peak=0.212`, `NaN=0` | Ratio `< 0.15` (`> 6.5x` RT) |
| 5 | 3D Voronoi CSG Destruction Physics | PASS | Fracture **`0.43 ms`**; Rigid step **`4.3 us/step`** | 5 archetypes, 90 shards, 180 60 Hz steps (`p95=10.2 us`); volume error **`0.000000%`** | Vol err `< 1e-4%`, finite |
| 6 | Biomechanical 3D Flora & Hydraulic Hydrology | PASS | **`625.1 trees/s`**; **`145.6 cells/s`** | 10 trees (`904` meshlets, `0` errors); 4 eroded cells (`368` meshlets, `0` errors) | Strict validator `0` errors |
| 7 | Multi-Cell Tamriel ESM Streamer | PASS | Riverwood 3x3 **`215.5 ms`**; Warm LRU **`72.1%`** | `11,186` cells + `12,990` models indexed (`361.2 ms`); `5,401` meshlets; Whiterun `145.6 ms` (`604` hits / `234` misses) | Clean ring load & LRU reuse |
| 8 | GPU XPBD Soft-Body / Hair / Bush-Bones (`4,096p`) | PASS | **`0.0580 ms`** mean GPU time | `256` chains / `4,096` particles, 4 substeps; `p50=0.0584`, `p95=0.0584`, `p99=0.0594 ms`; `stretch=1.0003`, `minSD=+0.0050`, `drift=0.0`, `NaN=0` | `< 0.25 ms` HW GPU (`4.31x` headroom) |
| 9 | GPU Hi-Z Pyramid (`512^2`) + 16k Instance Cull | PASS | Hi-Z `0.0212 ms` + Cull `0.0081 ms` = **`0.0293 ms`** | Combined `p50=0.0297`, `p95=0.0297`, `p99=0.0297 ms`; `vis=5120`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` | `< 0.15 ms` HW GPU (`5.12x` headroom) |

### 2. Quick CI Run Comparison: RTX 4090 vs. DX12 WARP (`--quick`)

Commands:
- Hardware GPU: `raw_bench_suite.exe --quick --json bench_rtx4090_quick.json` (`6` warmup + `10` measured GPU frames)
- Software WARP: `raw_bench_suite.exe --quick --warp --json bench_warp_quick.json` (`2` warmup + `4` measured GPU frames)

Both runs exited with code `0`, `allPassed: true`, and `d3d12DebugErrors: 0`.

| # | Subsystem | RTX 4090 (`--quick`) | DX12 WARP (`--quick --warp`) | Key Invariant Match |
| --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` + `256^2`) | Fire `0.223 ms` + Water `0.019 ms` (`0.243 ms` comb, `p95=0.246 ms`) | Fire `137.937 ms` + Water `3.487 ms` (`141.424 ms` comb, `p95=144.620 ms`) | `0` NaNs on both; `Tmax=2362 K` (HW) / `1968 K` (WARP, fewer warmup steps) |
| 2 | GPU Radiance Cascades GI | `0.118 ms` (`p50=0.118`, `p95=0.118 ms`) | `22.405 ms` (`p50=22.259`, `p95=22.997 ms`) | `openIrrad=0.5737`, `umbraIrrad=0.0000`, `2xRatio=2.0000` (bit-identical) |
| 3 | Procedural PBR Forge (8 sets @ `128x128`) | `9.17 MPix/s` (`42.9 ms`, `p95=7.40 ms`) | `9.20 MPix/s` (`42.7 ms`, `p95=7.39 ms`) | `8/8` sets, max edge diff `23.46` |
| 4 | 48 kHz FDN-8 + HRTF Audio (`1,003 ms`) | Ratio `0.0066` (`150.8x` RT, `6.65 ms`) | Ratio `0.0068` (`147.7x` RT, `6.79 ms`) | `RMS=0.0265`, `Peak=0.2116`, `0` NaNs |
| 5 | 3D Voronoi CSG Destruction (60 steps) | Frac `0.37 ms`, step `6.7 us` (`p95=10.8 us`) | Frac `0.38 ms`, step `6.8 us` (`p95=10.9 us`) | `90` shards, `volErr=0.000000%`, all finite |
| 6 | Flora (5 trees) & Hydrology (2 cells) | `600.0 trees/s`, `184.6 cells/s` | `558.2 trees/s`, `177.2 cells/s` | `454` flora + `184` hydro meshlets, `0` errors |
| 7 | Tamriel ESM Streamer | RW `219.8 ms`, WR `148.0 ms`, LRU `72.1%` | RW `213.6 ms`, WR `145.9 ms`, LRU `72.1%` | `11,186` cells, `604` hits / `234` misses |
| 8 | GPU XPBD Soft-Body (`256` chains / `4,096p`) | `0.0572 ms` (`p50=0.0573`, `p95=0.0579 ms`) | `1.0417 ms` (`p50=1.0870`, `p95=1.1249 ms`) | `minSD=+0.0050`, `drift=0.0`, `stretch<=1.0013`, `0` NaNs on both |
| 9 | GPU Hi-Z (`512^2`) + 16k Instance Cull | Hi-Z `0.0211` + Cull `0.0081` (`0.0292 ms` comb, `p95=0.0297 ms`) | Hi-Z `0.6319` + Cull `0.2521` (`0.8840 ms` comb, `p95=1.0192 ms`) | Bit-identical `vis=5120`, `meshlets=20477`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` |

### 3. Graceful Missing-Data Verification

Running `raw_bench_suite.exe --quick --esm "C:\nonexistent\Skyrim.esm" --json bench_skip_esm.json` passes all 8 engine-native benchmarks, marks `tamrielStreamer.skipped = true` (`SKIP`), reports `d3d12DebugErrors = 0`, and exits with code `0` so headless CI runners without Skyrim data stay green.

## Detailed Subsystem Breakdown and Invariants

### 1. GPU Navier-Stokes Fire/Smoke (`64^3`) + Shallow-Water Equations (`256^2`)
- **Implementation**: `RawFluidSimHost` (`src/d3d12_bridge/RawFluidSimHost.h/.cpp`) driving SM 6.6/6.10 compute shaders (`raw_fluid_*_cs.h`).
- **Measured Workload**: Per frame, injects a buoyant `2,600 K` torch flame emitter into the `64^3` MAC grid, executes vorticity confinement, pressure projection (8 Jacobi iterations), MacCormack advection, blackbody radiation bake, and steps the `256^2` shallow-water heightfield with a moving disturbance splash and normal/foam update.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Fire GPU pass: `mean=0.2234 ms`, `p50=0.2232 ms`, `p95=0.2253 ms`, `p99=0.2263 ms`
  - Water GPU pass: `mean=0.0193 ms`, `p50=0.0195 ms`, `p95=0.0195 ms`, `p99=0.0195 ms`
  - Combined GPU pass: `mean=0.2427 ms`, `p50=0.2427 ms`, `p95=0.2448 ms`, `p99=0.2458 ms`
  - CPU command recording overhead: `mean=0.0664 ms`, `p50=0.0560 ms`, `p95=0.0974 ms`
- **Invariants**: Zero NaN/Inf across all readback fire voxels and water texels (`fireNanCount == 0`, `waterNanCount == 0`), active combustion temperature (`peakTempK > 450 K`), and combined HW GPU mean `< 0.50 ms`.

### 2. GPU 4-Cascade Radiance Cascades Global Illumination
- **Implementation**: `RawRadianceHost` (`src/d3d12_bridge/RawRadianceHost.h/.cpp`) running `256x256` RGBA16F 4-cascade raymarch, angular merge, and irradiance resolve passes.
- **Measured Workload**: Emissive light disc occluded by an opaque wall separating an illuminated open probe `(88, 128)` from a deep-umbra probe `(176, 128)`, followed by a 2x emitter brightness pass to verify exact linear radiance transport.
- **Full RTX 4090 Breakdown (60 frames)**:
  - GI GPU pass: `mean=0.1075 ms`, `p50=0.1075 ms`, `p95=0.1085 ms`, `p99=0.1085 ms`
  - CPU command recording overhead: `mean=0.0325 ms`, `p50=0.0262 ms`, `p95=0.0557 ms`
  - Measured probes (identical on RTX 4090 and WARP): `openIrrad=0.5737`, `umbraIrrad=0.0000`, `linearScaleRatio=2.0000`
- **Invariants**: `nanCount == 0`, `openIrrad > 0.05`, `umbraIrrad < 0.25 * openIrrad`, `|linearScaleRatio - 2.0| < 0.05`, and HW GPU mean `< 0.35 ms`.

### 3. Procedural PBR Texture Forge
- **Implementation**: `RawTextureForge` (`src/materials/RawTextureForge.h/.cpp`) generating all 8 tileable PBR material families (Albedo, Normal, ORMH + mip chain) on CPU with zero external image assets.
- **Throughput**: `8.90 MPix/s` (`176.7 ms` total for 8 sets at `256x256`, `p50=21.74 ms`, `p95=30.29 ms` per set); `9.17 MPix/s` at `128x128` (`42.9 ms` total in `--quick` mode).

### 4. 48 kHz FDN-8 Hadamard Reverb + Woodworth HRTF Spatial Audio DSP
- **Implementation**: `RawAudioDsp` (`src/audio/RawAudioDsp.h/.cpp`), combining `SoundscapeSynth` (Nordic stone crypt FDN-8 Hadamard reverb, procedural wind, crackling fire, flowing water) and `SpatialVoice3D` (orbiting Doppler + Woodworth ITD/ILD HRTF source) in 512-frame stereo blocks at `48,000 Hz`.
- **Full Run**: `13.62 ms` CPU to render `2,005.3 ms` of stereo audio (`headroomRatio=0.0068`, **`147.3x`** faster than real-time; per-block `p50=0.0723 ms`, `p95=0.0754 ms`, `p99=0.0810 ms`).
- **Invariants**: `headroomRatio < 0.15` (`> 6.5x` real-time), `nanCount == 0`, `rmsLevel > 1e-4`, `peakAbs <= 2.0`.

### 5. 3D Voronoi CSG Destruction Physics
- **Implementation**: `RawDestructionEngine` (`src/worldgen/RawDestructionEngine.h/.cpp`), fracturing all 5 built-in convex destructible archetypes into 18 Voronoi CSG polyhedral shards each (`90` shards total) and stepping a 60 Hz rigid-body impulse simulation (`180` steps in full mode, `60` in quick mode).
- **Full Run**: Mean fracture latency `0.4267 ms` (`p50=0.4285 ms`, `p95=0.5248 ms`), 60 Hz rigid-body step `4.30 us` mean (`p50=3.85 us`, `p95=10.20 us`, `p99=10.92 us`).
- **Invariants**: Signed-tetrahedron volume conservation error `< 1e-4%` (measured **`0.000000%`** across all 5 archetypes) and 100% finite rigid-body transforms.

### 6. Biomechanical 3D Flora and Hydraulic Terrain Erosion
- **Implementation**: `RawFloraEngine` + `RawHydrologyGen` + `RawTerrainGen` + `RawMeshPipeline`, verified against `ValidateProceduralMeshStrict` (`src/pipeline/RawAssetValidator.h`).
- **Full Run**:
  - Flora (all 5 species x 2 reps = 10 trees): `625.1 trees/s` (`treeMs mean=1.203 ms`, `p95=1.873 ms`), `904` DX12 AS+MS meshlets, `0` validation errors.
  - Hydrology (4 `65x65` cells with 4,096 hydraulic droplets, 24 thermal talus passes, D8 watershed flow, and 64v/126t meshlet build): `145.6 cells/s` (`cellMs mean=6.468 ms`, `p95=6.562 ms`), `368` meshlets, `0` validation errors.

### 7. Multi-Cell Tamriel Streaming (`Skyrim.esm` + `Skyrim - Meshes0.bsa`)
- **Implementation**: `RawTamrielWorldStreamer` (`src/worldgen/RawTamrielWorldStreamer.h/.cpp`) backed by `RawEsmWorldLoader`, `RawEsmModelResolver`, `RawBsaArchive`, and `RawNifLoader`.
- **Measured Run**: Indexed `11,186` Tamriel exterior cells and `12,990` STAT/TREE/ACTI/FURN/CONT/DOOR/MSTT/LIGH models in `361.2 ms`. Cold 3x3 ring stream centered on Riverwood `(4, -12)` completed in `215.5 ms` (`9` cells, `5,401` meshlets). Subsequent jump to Whiterun `(5, -3)` completed in `145.6 ms`, followed by warm revisit verifying **`72.1%`** LRU mesh cache hit rate (`604` hits, `234` misses).

### 8. GPU XPBD Soft-Body, Hair, Cloth and Articulated Bush-Bone Solver
- **Implementation**: `RawXpbdSimHost` (`src/d3d12_bridge/RawXpbdSimHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawXpbdClothSim.hlsl` (`raw_xpbd_cloth_cs.h`) in a single groupshared compute dispatch (`[numthreads(64, 1, 1)]`, 4 chains per threadgroup, 16 nodes per chain).
- **Measured Workload**: Simulates 256 chains (`96` hair strands, `80` cloth cape ribbons, `80` articulated bush branches = `4,096` active particles) with 4 XPBD substeps per frame under `8.5 m/s` Beaufort wind gusts, `0.25` winter snow load, distance and 2-hop bending compliance, and 3 analytic colliders (head sphere, torso capsule, bush wake sphere).
- **Full RTX 4090 Breakdown (60 frames)**:
  - XPBD GPU pass: `mean=0.0580 ms`, `p50=0.0584 ms`, `p95=0.0584 ms`, `p99=0.0594 ms` (`hostStepMs=0.0553 ms`)
  - CPU command recording overhead: `mean=0.0185 ms`, `p50=0.0151 ms`, `p95=0.0406 ms`
  - Readback invariants: `maxRootDrift=0.0000000`, `maxStretchRatio=1.0003`, `minColliderSD=+0.0050`, `nanCount=0`
- **Invariants**: `nanCount == 0`, `maxRootDrift < 1e-5`, `maxStretchRatio < 1.03`, `minColliderSD >= -0.01`, and HW GPU mean `< 0.25 ms`.

### 9. GPU Conservative Hi-Z Occlusion Pyramid and 16,384-Instance Culler
- **Implementation**: `RawGpuCullHost` (`src/d3d12_bridge/RawGpuCullHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawGpuCull.hlsl` (`raw_hiz_reduce_cs.h` + `raw_cull_instances_cs.h`), producing `ExecuteIndirect`-ready `DrawIndexed` and `DispatchMesh` arguments with wave-compacted atomics.
- **Measured Workload**: Builds a full 10-mip Standard-Z conservative `max()` Hi-Z pyramid from a `512x512` depth buffer containing a giant `z=0.25` occluder wall, then culls `16,384` deterministic instances (`4,096` frustum-outside, `1,024` distance-LOD, `6,144` completely behind wall, `2,560` in front of wall, `1,536` peeking past wall edge, `1,024` straddling the near plane).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Hi-Z pyramid build: `mean=0.0212 ms`, `p50=0.0215 ms`, `p95=0.0215 ms`, `p99=0.0215 ms` (`hostHiZMs=0.0174 ms`)
  - 16,384-instance wave-compacted cull: `mean=0.0081 ms`, `p50=0.0082 ms`, `p95=0.0082 ms`, `p99=0.0082 ms` (`hostCullMs=0.0041 ms`)
  - Combined GPU pass: `mean=0.0293 ms`, `p50=0.0297 ms`, `p95=0.0297 ms`, `p99=0.0297 ms`
  - CPU command recording overhead: `mean=0.0461 ms`, `p50=0.0387 ms`, `p95=0.0812 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `visibleCount=5120`, `visibleMeshlets=20477`, `frustumCulled=4096`, `distanceCulled=1024`, `hizCulled=6144`, `falseOcclusionCount=0`
- **Invariants**: Exact bucket counts (`5120/4096/1024/6144`), zero duplicates, zero false survivors, zero false occlusion (`falseOcclusionCount == 0`), exact `IndirectArgsBuffer` match, and HW GPU combined mean `< 0.15 ms`.

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
- **JSON Schema**: Top-level fields include `suite`, `mode`, `adapter`, `isWarp`, `vramMB`, `debugLayer`, `d3d12DebugErrors`, `timestampFreqHz`, `gpuWarmupFrames`, `gpuMeasuredFrames`, `allPassed`, and `subsystems.{fluidSim, radianceCascades, textureForge, audioDsp, destruction, floraAndHydrology, tamrielStreamer, xpbdSim, gpuCull}` with nested `{mean, p50, p95, p99, min, max}` latency objects for every timed stage.
