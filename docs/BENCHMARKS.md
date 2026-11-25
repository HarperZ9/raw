# RAW Engine Automated GPU/CPU Benchmarks and Visual Invariants

This document records live, reproducible measurements produced by [`tools/raw_bench_suite.cpp`](../tools/raw_bench_suite.cpp) across RAW Engine's ten core GPU and CPU subsystems. Every GPU timing is measured directly on the Direct3D 12 command queue via `ID3D12QueryHeap` (`D3D12_QUERY_HEAP_TYPE_TIMESTAMP`) normalized by `GetTimestampFrequency()`, with the D3D12 Debug Layer enabled (`0` validation errors required). Every CPU timing is measured with `std::chrono::steady_clock` and summarized with `mean`, `p50`, `p95`, and `p99` percentiles.

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
| 1 | GPU Fluid (`64^3` NS Fire + `256^2` SWE Water) | PASS | Fire `0.222 ms` + Water `0.019 ms` = **`0.241 ms`** | Combined `p50=0.241`, `p95=0.244`, `p99=0.246 ms`; `Tmax=2550 K`; `NaN=0` | `< 0.50 ms` HW GPU (`2.07x` headroom) |
| 2 | GPU 4-Cascade Radiance Cascades GI | PASS | **`0.113 ms`** mean GPU time | `p50=0.113`, `p95=0.114`, `p99=0.114 ms`; `2xRatio=2.000`; `Umbra=0.0%`; `NaN=0` | `< 0.35 ms` HW GPU (`3.11x` headroom) |
| 3 | Procedural PBR Texture Forge (`RawTextureForge`) | PASS | **`8.30 MPix/s`** (`189.5 ms` total) | 8 seamless sets @ `256x256` (3 PBR maps/set); per-set `p50=23.41`, `p95=33.04 ms`; max edge diff `11.61` | 8/8 sets valid |
| 4 | 48 kHz FDN-8 Hadamard + HRTF Audio DSP | PASS | Ratio **`0.0075`** (**`132.7x`** real-time) | `15.12 ms` CPU to render `2,005.3 ms` stereo (188x512 blocks); `p95=0.094 ms`; `RMS=0.027`, `Peak=0.212`, `NaN=0` | Ratio `< 0.15` (`> 6.5x` RT) |
| 5 | 3D Voronoi CSG Destruction Physics | PASS | Fracture **`0.40 ms`**; Rigid step **`4.5 us/step`** | 5 archetypes, 90 shards, 180 60 Hz steps (`p95=10.5 us`); volume error **`0.000000%`** | Vol err `< 1e-4%`, finite |
| 6 | Biomechanical 3D Flora & Hydraulic Hydrology | PASS | **`520.4 trees/s`**; **`142.8 cells/s`** | 10 trees (`904` meshlets, `0` errors); 4 eroded cells (`368` meshlets, `0` errors) | Strict validator `0` errors |
| 7 | Multi-Cell Tamriel ESM Streamer | PASS | Riverwood 3x3 **`230.6 ms`**; Warm LRU **`72.1%`** | `11,186` cells + `12,990` models indexed (`358.6 ms`); `5,401` meshlets; Whiterun `148.9 ms` (`604` hits / `234` misses) | Clean ring load & LRU reuse |
| 8 | GPU XPBD Soft-Body / Hair / Bush-Bones (`4,096p`) | PASS | **`0.0578 ms`** mean GPU time | `256` chains / `4,096` particles, 4 substeps; `p50=0.0573`, `p95=0.0584`, `p99=0.0588 ms`; `stretch=1.0003`, `minSD=+0.0050`, `drift=0.0`, `NaN=0` | `< 0.25 ms` HW GPU (`4.33x` headroom) |
| 9 | GPU Hi-Z Pyramid (`512^2`) + 16k Instance Cull | PASS | Hi-Z `0.0212 ms` + Cull `0.0080 ms` = **`0.0292 ms`** | Combined `p50=0.0297`, `p95=0.0297`, `p99=0.0297 ms`; `vis=5120`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` | `< 0.15 ms` HW GPU (`5.14x` headroom) |
| 10 | GPU 16x9x24 3D Clustered 1,024-Light Grid | PASS | Build `0.0035 ms` + Assign `0.0860 ms` = **`0.0977 ms`** | Combined `p50=0.0973`, `p95=0.0983`, `p99=0.0983 ms`; `lit=3410/3456`, `max=64`, `links=116168`, `ovf=0`, `nanAabb=0` | `< 0.15 ms` HW GPU (`1.54x` headroom) |

### 2. Quick CI Run Comparison: RTX 4090 vs. DX12 WARP (`--quick`)

Commands:
- Hardware GPU: `raw_bench_suite.exe --quick --json bench_rtx4090_quick.json` (`6` warmup + `10` measured GPU frames)
- Software WARP: `raw_bench_suite.exe --quick --warp --json bench_warp_quick.json` (`2` warmup + `4` measured GPU frames)

Both runs exited with code `0`, `allPassed: true`, and `d3d12DebugErrors: 0`.

| # | Subsystem | RTX 4090 (`--quick`) | DX12 WARP (`--quick --warp`) | Key Invariant Match |
| --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` + `256^2`) | Fire `0.223 ms` + Water `0.019 ms` (`0.242 ms` comb, `p95=0.245 ms`) | Fire `134.815 ms` + Water `3.455 ms` (`138.270 ms` comb, `p95=139.540 ms`) | `0` NaNs on both; `Tmax=2362 K` (HW) / `1968 K` (WARP, fewer warmup steps) |
| 2 | GPU Radiance Cascades GI | `0.113 ms` (`p50=0.113`, `p95=0.114 ms`) | `22.977 ms` (`p50=22.708`, `p95=23.668 ms`) | `openIrrad=0.5737`, `umbraIrrad=0.0000`, `2xRatio=2.0000` (bit-identical) |
| 3 | Procedural PBR Forge (8 sets @ `128x128`) | `8.34 MPix/s` (`47.1 ms`, `p95=8.30 ms`) | `8.62 MPix/s` (`45.6 ms`, `p95=7.97 ms`) | `8/8` sets, max edge diff `23.46` |
| 4 | 48 kHz FDN-8 + HRTF Audio (`1,003 ms`) | Ratio `0.0069` (`145.0x` RT, `6.91 ms`) | Ratio `0.0073` (`137.4x` RT, `7.30 ms`) | `RMS=0.0265`, `Peak=0.2116`, `0` NaNs |
| 5 | 3D Voronoi CSG Destruction (60 steps) | Frac `0.40 ms`, step `6.8 us` (`p95=11.6 us`) | Frac `0.46 ms`, step `7.5 us` (`p95=11.8 us`) | `90` shards, `volErr=0.000000%`, all finite |
| 6 | Flora (5 trees) & Hydrology (2 cells) | `611.5 trees/s`, `174.2 cells/s` | `499.7 trees/s`, `168.5 cells/s` | `454` flora + `184` hydro meshlets, `0` errors |
| 7 | Tamriel ESM Streamer | RW `219.9 ms`, WR `149.2 ms`, LRU `72.1%` | RW `229.0 ms`, WR `162.5 ms`, LRU `72.1%` | `11,186` cells, `604` hits / `234` misses |
| 8 | GPU XPBD Soft-Body (`256` chains / `4,096p`) | `0.0574 ms` (`p50=0.0573`, `p95=0.0584 ms`) | `0.9133 ms` (`p50=0.9325`, `p95=0.9432 ms`) | `minSD=+0.0050`, `drift=0.0`, `stretch<=1.0013`, `0` NaNs on both |
| 9 | GPU Hi-Z (`512^2`) + 16k Instance Cull | Hi-Z `0.0213` + Cull `0.0080` (`0.0293 ms` comb, `p95=0.0297 ms`) | Hi-Z `0.6338` + Cull `0.2499` (`0.8838 ms` comb, `p95=0.9450 ms`) | Bit-identical `vis=5120`, `meshlets=20477`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` |
| 10 | GPU 16x9x24 3D Clustered 1,024-Light Grid | Build `0.0035` + Assign `0.0860` (`0.0978 ms` comb, `p95=0.0983 ms`) | `1.8903 ms` comb (`p50=1.8149`, `p95=2.0893 ms`) | Bit-identical `lit=3410/3456`, `max=64`, `links=116168`, `ovf=0`, `nanAabb=0` |

### 3. Graceful Missing-Data Verification

Running `raw_bench_suite.exe --quick --esm "C:\nonexistent\Skyrim.esm" --json bench_skip_esm.json` passes all 9 engine-native benchmarks, marks `tamrielStreamer.skipped = true` (`SKIP`), reports `d3d12DebugErrors = 0`, and exits with code `0` so headless CI runners without Skyrim data stay green.

## Detailed Subsystem Breakdown and Invariants

### 1. GPU Navier-Stokes Fire/Smoke (`64^3`) + Shallow-Water Equations (`256^2`)
- **Implementation**: `RawFluidSimHost` (`src/d3d12_bridge/RawFluidSimHost.h/.cpp`) driving SM 6.6/6.10 compute shaders (`raw_fluid_*_cs.h`).
- **Measured Workload**: Per frame, injects a buoyant `2,600 K` torch flame emitter into the `64^3` MAC grid, executes vorticity confinement, pressure projection (8 Jacobi iterations), MacCormack advection, blackbody radiation bake, and steps the `256^2` shallow-water heightfield with a moving disturbance splash and normal/foam update.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Fire GPU pass: `mean=0.2220 ms`, `p50=0.2217 ms`, `p95=0.2253 ms`, `p99=0.2274 ms`
  - Water GPU pass: `mean=0.0190 ms`, `p50=0.0195 ms`, `p95=0.0195 ms`, `p99=0.0195 ms`
  - Combined GPU pass: `mean=0.2411 ms`, `p50=0.2406 ms`, `p95=0.2438 ms`, `p99=0.2464 ms`
  - CPU command recording overhead: `mean=0.0661 ms`, `p50=0.0561 ms`, `p95=0.1120 ms`
- **Invariants**: Zero NaN/Inf across all readback fire voxels and water texels (`fireNanCount == 0`, `waterNanCount == 0`), active combustion temperature (`peakTempK > 450 K`), and combined HW GPU mean `< 0.50 ms`.

### 2. GPU 4-Cascade Radiance Cascades Global Illumination
- **Implementation**: `RawRadianceHost` (`src/d3d12_bridge/RawRadianceHost.h/.cpp`) running `256x256` RGBA16F 4-cascade raymarch, angular merge, and irradiance resolve passes.
- **Measured Workload**: Emissive light disc occluded by an opaque wall separating an illuminated open probe `(88, 128)` from a deep-umbra probe `(176, 128)`, followed by a 2x emitter brightness pass to verify exact linear radiance transport.
- **Full RTX 4090 Breakdown (60 frames)**:
  - GI GPU pass: `mean=0.1126 ms`, `p50=0.1126 ms`, `p95=0.1137 ms`, `p99=0.1137 ms`
  - CPU command recording overhead: `mean=0.0313 ms`, `p50=0.0257 ms`, `p95=0.0576 ms`
  - Measured probes (identical on RTX 4090 and WARP): `openIrrad=0.5737`, `umbraIrrad=0.0000`, `linearScaleRatio=2.0000`
- **Invariants**: `nanCount == 0`, `openIrrad > 0.05`, `umbraIrrad < 0.25 * openIrrad`, `|linearScaleRatio - 2.0| < 0.05`, and HW GPU mean `< 0.35 ms`.

### 3. Procedural PBR Texture Forge
- **Implementation**: `RawTextureForge` (`src/materials/RawTextureForge.h/.cpp`) generating all 8 tileable PBR material families (Albedo, Normal, ORMH + mip chain) on CPU with zero external image assets.
- **Throughput**: `8.30 MPix/s` (`189.5 ms` total for 8 sets at `256x256`, `p50=23.41 ms`, `p95=33.04 ms` per set); `8.34 MPix/s` at `128x128` (`47.1 ms` total in `--quick` mode).

### 4. 48 kHz FDN-8 Hadamard Reverb + Woodworth HRTF Spatial Audio DSP
- **Implementation**: `RawAudioDsp` (`src/audio/RawAudioDsp.h/.cpp`), combining `SoundscapeSynth` (Nordic stone crypt FDN-8 Hadamard reverb, procedural wind, crackling fire, flowing water) and `SpatialVoice3D` (orbiting Doppler + Woodworth ITD/ILD HRTF source) in 512-frame stereo blocks at `48,000 Hz`.
- **Full Run**: `15.12 ms` CPU to render `2,005.3 ms` of stereo audio (`headroomRatio=0.0075`, **`132.7x`** faster than real-time; per-block `p50=0.0746 ms`, `p95=0.0935 ms`, `p99=0.2389 ms`).
- **Invariants**: `headroomRatio < 0.15` (`> 6.5x` real-time), `nanCount == 0`, `rmsLevel > 1e-4`, `peakAbs <= 2.0`.

### 5. 3D Voronoi CSG Destruction Physics
- **Implementation**: `RawDestructionEngine` (`src/worldgen/RawDestructionEngine.h/.cpp`), fracturing all 5 built-in convex destructible archetypes into 18 Voronoi CSG polyhedral shards each (`90` shards total) and stepping a 60 Hz rigid-body impulse simulation (`180` steps in full mode, `60` in quick mode).
- **Full Run**: Mean fracture latency `0.4015 ms` (`p50=0.3596 ms`, `p95=0.4998 ms`), 60 Hz rigid-body step `4.50 us` mean (`p50=4.05 us`, `p95=10.51 us`, `p99=11.60 us`).
- **Invariants**: Signed-tetrahedron volume conservation error `< 1e-4%` (measured **`0.000000%`** across all 5 archetypes) and 100% finite rigid-body transforms.

### 6. Biomechanical 3D Flora and Hydraulic Terrain Erosion
- **Implementation**: `RawFloraEngine` + `RawHydrologyGen` + `RawTerrainGen` + `RawMeshPipeline`, verified against `ValidateProceduralMeshStrict` (`src/pipeline/RawAssetValidator.h`).
- **Full Run**:
  - Flora (all 5 species x 2 reps = 10 trees): `520.4 trees/s` (`treeMs mean=1.348 ms`, `p95=2.138 ms`), `904` DX12 AS+MS meshlets, `0` validation errors.
  - Hydrology (4 `65x65` cells with 4,096 hydraulic droplets, 24 thermal talus passes, D8 watershed flow, and 64v/126t meshlet build): `142.8 cells/s` (`cellMs mean=6.408 ms`, `p95=6.694 ms`), `368` meshlets, `0` validation errors.

### 7. Multi-Cell Tamriel Streaming (`Skyrim.esm` + `Skyrim - Meshes0.bsa`)
- **Implementation**: `RawTamrielWorldStreamer` (`src/worldgen/RawTamrielWorldStreamer.h/.cpp`) backed by `RawEsmWorldLoader`, `RawEsmModelResolver`, `RawBsaArchive`, and `RawNifLoader`.
- **Measured Run**: Indexed `11,186` Tamriel exterior cells and `12,990` STAT/TREE/ACTI/FURN/CONT/DOOR/MSTT/LIGH models in `358.6 ms`. Cold 3x3 ring stream centered on Riverwood `(4, -12)` completed in `230.6 ms` (`9` cells, `5,401` meshlets). Subsequent jump to Whiterun `(5, -3)` completed in `148.9 ms`, followed by warm revisit verifying **`72.1%`** LRU mesh cache hit rate (`604` hits, `234` misses).

### 8. GPU XPBD Soft-Body, Hair, Cloth and Articulated Bush-Bone Solver
- **Implementation**: `RawXpbdSimHost` (`src/d3d12_bridge/RawXpbdSimHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawXpbdClothSim.hlsl` (`raw_xpbd_cloth_cs.h`) in a single groupshared compute dispatch (`[numthreads(64, 1, 1)]`, 4 chains per threadgroup, 16 nodes per chain).
- **Measured Workload**: Simulates 256 chains (`96` hair strands, `80` cloth cape ribbons, `80` articulated bush branches = `4,096` active particles) with 4 XPBD substeps per frame under `8.5 m/s` Beaufort wind gusts, `0.25` winter snow load, distance and 2-hop bending compliance, and 3 analytic colliders (head sphere, torso capsule, bush wake sphere).
- **Full RTX 4090 Breakdown (60 frames)**:
  - XPBD GPU pass: `mean=0.0578 ms`, `p50=0.0573 ms`, `p95=0.0584 ms`, `p99=0.0588 ms` (`hostStepMs=0.0543 ms`)
  - CPU command recording overhead: `mean=0.0236 ms`, `p50=0.0175 ms`, `p95=0.0414 ms`
  - Readback invariants: `maxRootDrift=0.0000000`, `maxStretchRatio=1.0003`, `minColliderSD=+0.0050`, `nanCount=0`
- **Invariants**: `nanCount == 0`, `maxRootDrift < 1e-5`, `maxStretchRatio < 1.03`, `minColliderSD >= -0.01`, and HW GPU mean `< 0.25 ms`.

### 9. GPU Conservative Hi-Z Occlusion Pyramid and 16,384-Instance Culler
- **Implementation**: `RawGpuCullHost` (`src/d3d12_bridge/RawGpuCullHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawGpuCull.hlsl` (`raw_hiz_reduce_cs.h` + `raw_cull_instances_cs.h`), producing `ExecuteIndirect`-ready `DrawIndexed` and `DispatchMesh` arguments with wave-compacted atomics.
- **Measured Workload**: Builds a full 10-mip Standard-Z conservative `max()` Hi-Z pyramid from a `512x512` depth buffer containing a giant `z=0.25` occluder wall, then culls `16,384` deterministic instances (`4,096` frustum-outside, `1,024` distance-LOD, `6,144` completely behind wall, `2,560` in front of wall, `1,536` peeking past wall edge, `1,024` straddling the near plane).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Hi-Z pyramid build: `mean=0.0212 ms`, `p50=0.0215 ms`, `p95=0.0215 ms`, `p99=0.0215 ms` (`hostHiZMs=0.0174 ms`)
  - 16,384-instance wave-compacted cull: `mean=0.0080 ms`, `p50=0.0082 ms`, `p95=0.0082 ms`, `p99=0.0082 ms` (`hostCullMs=0.0041 ms`)
  - Combined GPU pass: `mean=0.0292 ms`, `p50=0.0297 ms`, `p95=0.0297 ms`, `p99=0.0297 ms`
  - CPU command recording overhead: `mean=0.0412 ms`, `p50=0.0384 ms`, `p95=0.0564 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `visibleCount=5120`, `visibleMeshlets=20477`, `frustumCulled=4096`, `distanceCulled=1024`, `hizCulled=6144`, `falseOcclusionCount=0`
- **Invariants**: Exact bucket counts (`5120/4096/1024/6144`), zero duplicates, zero false survivors, zero false occlusion (`falseOcclusionCount == 0`), exact `IndirectArgsBuffer` match, and HW GPU combined mean `< 0.15 ms`.

### 10. GPU 16x9x24 3D Clustered 1,024-Light Grid Assignment
- **Implementation**: `RawLightClusterHost` (`src/d3d12_bridge/RawLightClusterHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawLightClusterSim.hlsl` (`raw_cluster_build_aabb_cs.h` + `raw_cluster_cull_lights_cs.h`), bypassing Creation Engine's legacy 4-light-per-mesh limit with a 3,456-cluster logarithmic-Z 3D froxel grid and cooperative LDS light batching.
- **Measured Workload**: Uploads and assigns `1,024` dynamic point, fire, and spotlight emitters across all `16x9x24 = 3,456` view-space cluster AABBs (`nearZ=0.5`, `farZ=500.0`, 60 deg FOV, 16:9) with exact sphere-vs-AABB and conservative bounding-sphere-vs-cone rejection (up to 64 lights per cluster, zero global atomic contention).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Cluster AABB build: `mean=0.0035 ms`, `p50=0.0036 ms`, `p95=0.0039 ms`, `p99=0.0040 ms` (`hostBuildMs=0.0032 ms`)
  - 1,024-light cluster assignment: `mean=0.0860 ms`, `p50=0.0860 ms`, `p95=0.0860 ms`, `p99=0.0860 ms` (`hostAssignMs=0.0860 ms`)
  - Combined GPU pass (including 49 KB ring light upload): `mean=0.0977 ms`, `p50=0.0973 ms`, `p95=0.0983 ms`, `p99=0.0983 ms`
  - CPU command recording overhead: `mean=0.0292 ms`, `p50=0.0245 ms`, `p95=0.0512 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `nonEmptyClusters=3410/3456`, `maxLightsCluster=64`, `totalLightLinks=116168`, `overflowErrors=0`, `nanAabbCount=0`
- **Invariants**: Zero per-cluster overflow (`all counts <= 64`), zero invalid light indices, zero NaN/inverted AABBs, exact CPU-GPU parity, and HW GPU combined mean `< 0.15 ms`.

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
- **JSON Schema**: Top-level fields include `suite`, `mode`, `adapter`, `isWarp`, `vramMB`, `debugLayer`, `d3d12DebugErrors`, `timestampFreqHz`, `gpuWarmupFrames`, `gpuMeasuredFrames`, `allPassed`, and `subsystems.{fluidSim, radianceCascades, textureForge, audioDsp, destruction, floraAndHydrology, tamrielStreamer, xpbdSim, gpuCull, lightClusters}` with nested `{mean, p50, p95, p99, min, max}` latency objects for every timed stage.
