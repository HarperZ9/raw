# RAW Engine Automated GPU/CPU Benchmarks and Visual Invariants

This document records live, reproducible measurements produced by [`tools/raw_bench_suite.cpp`](../tools/raw_bench_suite.cpp) across RAW Engine's seven core GPU and CPU subsystems. Every GPU timing is measured directly on the Direct3D 12 command queue via `ID3D12QueryHeap` (`D3D12_QUERY_HEAP_TYPE_TIMESTAMP`) normalized by `GetTimestampFrequency()`, with the D3D12 Debug Layer enabled (`0` validation errors required). Every CPU timing is measured with `std::chrono::steady_clock` and summarized with `mean`, `p50`, `p95`, and `p99` percentiles.

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
| 1 | GPU Fluid (`64^3` NS Fire + `256^2` SWE Water) | PASS | Fire `0.223 ms` + Water `0.019 ms` = **`0.242 ms`** | Combined `p50=0.242`, `p95=0.245`, `p99=0.246 ms`; `Tmax=2550 K`; `NaN=0` | `< 0.50 ms` HW GPU (`2.07x` headroom) |
| 2 | GPU 4-Cascade Radiance Cascades GI | PASS | **`0.108 ms`** mean GPU time | `p50=0.108`, `p95=0.109`, `p99=0.109 ms`; `2xRatio=2.000`; `Umbra=0.0%`; `NaN=0` | `< 0.35 ms` HW GPU (`3.25x` headroom) |
| 3 | Procedural PBR Texture Forge (`RawTextureForge`) | PASS | **`9.02 MPix/s`** (`174.5 ms` total) | 8 seamless sets @ `256x256` (3 PBR maps/set); per-set `p50=21.51`, `p95=29.88 ms`; max edge diff `11.61` | 8/8 sets valid |
| 4 | 48 kHz FDN-8 Hadamard + HRTF Audio DSP | PASS | Ratio **`0.0068`** (**`146.3x`** real-time) | `13.71 ms` CPU to render `2,005.3 ms` stereo (188x512 blocks); `p95=0.076 ms`; `RMS=0.027`, `Peak=0.212`, `NaN=0` | Ratio `< 0.15` (`> 6.5x` RT) |
| 5 | 3D Voronoi CSG Destruction Physics | PASS | Fracture **`0.38 ms`**; Rigid step **`4.2 us/step`** | 5 archetypes, 90 shards, 180 60 Hz steps (`p95=9.8 us`); volume error **`0.000000%`** | Vol err `< 1e-4%`, finite |
| 6 | Biomechanical 3D Flora & Hydraulic Hydrology | PASS | **`602.9 trees/s`**; **`144.5 cells/s`** | 10 trees (`904` meshlets, `0` errors); 4 eroded cells (`368` meshlets, `0` errors) | Strict validator `0` errors |
| 7 | Multi-Cell Tamriel ESM Streamer | PASS | Riverwood 3x3 **`220.1 ms`**; Warm LRU **`72.1%`** | `11,186` cells + `12,990` models indexed (`359.0 ms`); `5,401` meshlets; Whiterun `160.9 ms` (`604` hits / `234` misses) | Clean ring load & LRU reuse |

### 2. Quick CI Run Comparison: RTX 4090 vs. DX12 WARP (`--quick`)

Commands:
- Hardware GPU: `raw_bench_suite.exe --quick --json bench_rtx4090_quick.json` (`6` warmup + `10` measured GPU frames)
- Software WARP: `raw_bench_suite.exe --quick --warp --json bench_warp_quick.json` (`2` warmup + `4` measured GPU frames)

Both runs exited with code `0`, `allPassed: true`, and `d3d12DebugErrors: 0`.

| # | Subsystem | RTX 4090 (`--quick`) | DX12 WARP (`--quick --warp`) | Key Invariant Match |
| --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` + `256^2`) | Fire `0.223 ms` + Water `0.019 ms` (`0.242 ms` comb, `p95=0.245 ms`) | Fire `142.350 ms` + Water `3.769 ms` (`146.119 ms` comb, `p95=149.168 ms`) | `0` NaNs on both; `Tmax=2362 K` (HW) / `1968 K` (WARP, fewer warmup steps) |
| 2 | GPU Radiance Cascades GI | `0.108 ms` (`p50=0.108`, `p95=0.109 ms`) | `25.763 ms` (`p50=24.710`, `p95=29.126 ms`) | `openIrrad=0.5737`, `umbraIrrad=0.0000`, `2xRatio=2.0000` (bit-identical) |
| 3 | Procedural PBR Forge (8 sets @ `128x128`) | `9.13 MPix/s` (`43.1 ms`, `p95=7.43 ms`) | `9.18 MPix/s` (`42.8 ms`, `p95=7.41 ms`) | `8/8` sets, max edge diff `23.46` |
| 4 | 48 kHz FDN-8 + HRTF Audio (`1,003 ms`) | Ratio `0.0067` (`149.4x` RT, `6.71 ms`) | Ratio `0.0067` (`149.9x` RT, `6.69 ms`) | `RMS=0.0265`, `Peak=0.2116`, `0` NaNs |
| 5 | 3D Voronoi CSG Destruction (60 steps) | Frac `0.39 ms`, step `6.6 us` (`p95=10.7 us`) | Frac `0.40 ms`, step `6.7 us` (`p95=10.7 us`) | `90` shards, `volErr=0.000000%`, all finite |
| 6 | Flora (5 trees) & Hydrology (2 cells) | `563.5 trees/s`, `163.7 cells/s` | `550.3 trees/s`, `182.2 cells/s` | `454` flora + `184` hydro meshlets, `0` errors |
| 7 | Tamriel ESM Streamer | RW `225.2 ms`, WR `159.2 ms`, LRU `72.1%` | RW `229.4 ms`, WR `149.0 ms`, LRU `72.1%` | `11,186` cells, `604` hits / `234` misses |

### 3. Graceful Missing-Data Verification

Running `raw_bench_suite.exe --quick --esm "C:\nonexistent\Skyrim.esm" --json bench_skip_esm.json` passes all 6 engine-native benchmarks, marks `tamrielStreamer.skipped = true` (`SKIP`), reports `d3d12DebugErrors = 0`, and exits with code `0` so headless CI runners without Skyrim data stay green.

## Detailed Subsystem Breakdown and Invariants

### 1. GPU Navier-Stokes Fire/Smoke (`64^3`) + Shallow-Water Equations (`256^2`)
- **Implementation**: `RawFluidSimHost` (`src/d3d12_bridge/RawFluidSimHost.h/.cpp`) driving SM 6.6/6.10 compute shaders (`raw_fluid_*_cs.h`).
- **Measured Workload**: Per frame, injects a buoyant `2,400 K` torch flame emitter into the `64^3` MAC grid, executes vorticity confinement, pressure projection (8 Jacobi iterations), MacCormack advection, blackbody radiation bake, and steps the `256^2` shallow-water heightfield with a moving disturbance splash and normal/foam update.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Fire GPU pass: `mean=0.2229 ms`, `p50=0.2232 ms`, `p95=0.2263 ms`, `p99=0.2267 ms`
  - Water GPU pass: `mean=0.0190 ms`, `p50=0.0195 ms`, `p95=0.0195 ms`, `p99=0.0199 ms`
  - Combined GPU pass: `mean=0.2420 ms`, `p50=0.2417 ms`, `p95=0.2448 ms`, `p99=0.2458 ms`
  - CPU command recording overhead: `mean=0.0682 ms`, `p50=0.0618 ms`, `p95=0.0973 ms`
- **Invariants**: Zero NaN/Inf across all readback fire voxels and water texels (`fireNanCount == 0`, `waterNanCount == 0`), active combustion temperature (`peakTempK > 400 K`), and combined HW GPU mean `< 0.50 ms`.

### 2. GPU 4-Cascade Radiance Cascades Global Illumination
- **Implementation**: `RawRadianceHost` (`src/d3d12_bridge/RawRadianceHost.h/.cpp`) running `256x256` RGBA16F 4-cascade raymarch, angular merge, and irradiance resolve passes.
- **Measured Workload**: Emissive light disc occluded by an opaque wall separating an illuminated open probe `(88, 128)` from a deep-umbra probe `(176, 128)`, followed by a 2x emitter brightness pass to verify exact linear radiance transport.
- **Full RTX 4090 Breakdown (60 frames)**:
  - GI GPU pass: `mean=0.1077 ms`, `p50=0.1075 ms`, `p95=0.1085 ms`, `p99=0.1090 ms`
  - CPU command recording overhead: `mean=0.0342 ms`, `p50=0.0268 ms`, `p95=0.0557 ms`
  - Measured probes (identical on RTX 4090 and WARP): `openIrrad=0.5737`, `umbraIrrad=0.0000`, `linearScaleRatio=2.0000`
- **Invariants**: `nanCount == 0`, `openIrrad > 0.05`, `umbraIrrad < 0.25 * openIrrad`, `|linearScaleRatio - 2.0| < 0.05`, and HW GPU mean `< 0.35 ms`.

### 3. Procedural PBR Texture Forge
- **Implementation**: `RawTextureForge` (`src/materials/RawTextureForge.h/.cpp`) generating all 8 tileable PBR material families (Albedo, Normal, ORMH + mip chain) on CPU with zero external image assets.
- **Throughput**: `9.02 MPix/s` (`174.5 ms` total for 8 sets at `256x256`, `p50=21.51 ms`, `p95=29.88 ms` per set); `9.13 MPix/s` at `128x128` (`43.1 ms` total in `--quick` mode).

### 4. 48 kHz FDN-8 Hadamard Reverb + Woodworth HRTF Spatial Audio DSP
- **Implementation**: `RawAudioDsp` (`src/audio/RawAudioDsp.h/.cpp`), combining `SoundscapeSynth` (Nordic stone crypt FDN-8 Hadamard reverb, procedural wind, crackling fire, flowing water) and `SpatialVoice3D` (orbiting Doppler + Woodworth ITD/ILD HRTF source) in 512-frame stereo blocks at `48,000 Hz`.
- **Full Run**: `13.71 ms` CPU to render `2,005.3 ms` of stereo audio (`headroomRatio=0.0068`, **`146.3x`** faster than real-time; per-block `p50=0.0729 ms`, `p95=0.0763 ms`, `p99=0.0786 ms`).
- **Invariants**: `headroomRatio < 0.15` (`> 6.5x` real-time), `nanCount == 0`, `rmsLevel > 1e-4`, `peakAbs <= 2.0`.

### 5. 3D Voronoi CSG Destruction Physics
- **Implementation**: `RawDestructionEngine` (`src/worldgen/RawDestructionEngine.h/.cpp`), fracturing all 5 built-in convex destructible archetypes into 18 Voronoi CSG polyhedral shards each (`90` shards total) and stepping a 60 Hz rigid-body impulse simulation (`180` steps in full mode, `60` in quick mode).
- **Full Run**: Mean fracture latency `0.3826 ms` (`p50=0.3427 ms`, `p95=0.4810 ms`), 60 Hz rigid-body step `4.23 us` mean (`p50=3.80 us`, `p95=9.81 us`, `p99=10.80 us`).
- **Invariants**: Signed-tetrahedron volume conservation error `< 1e-4%` (measured **`0.000000%`** across all 5 archetypes) and 100% finite rigid-body transforms.

### 6. Biomechanical 3D Flora and Hydraulic Terrain Erosion
- **Implementation**: `RawFloraEngine` + `RawHydrologyGen` + `RawTerrainGen` + `RawMeshPipeline`, verified against `ValidateProceduralMeshStrict` (`src/pipeline/RawAssetValidator.h`).
- **Full Run**:
  - Flora (all 5 species x 2 reps = 10 trees): `602.9 trees/s` (`treeMs mean=1.283 ms`, `p95=2.005 ms`), `904` DX12 AS+MS meshlets, `0` validation errors.
  - Hydrology (4 `65x65` cells with 4,096 hydraulic droplets, 24 thermal talus passes, D8 watershed flow, and 64v/126t meshlet build): `144.5 cells/s` (`cellMs mean=6.396 ms`, `p95=6.650 ms`), `368` meshlets, `0` validation errors.

### 7. Multi-Cell Tamriel Streaming (`Skyrim.esm` + `Skyrim - Meshes0.bsa`)
- **Implementation**: `RawTamrielWorldStreamer` (`src/worldgen/RawTamrielWorldStreamer.h/.cpp`) backed by `RawEsmWorldLoader`, `RawEsmModelResolver`, `RawBsaArchive`, and `RawNifLoader`.
- **Measured Run**: Indexed `11,186` Tamriel exterior cells and `12,990` STAT/TREE/ACTI/FURN/CONT/DOOR/MSTT/LIGH models in `359.0 ms`. Cold 3x3 ring stream centered on Riverwood `(4, -12)` completed in `220.1 ms` (`9` cells, `5,401` meshlets). Subsequent jump to Whiterun `(5, -3)` completed in `160.9 ms`, followed by warm revisit verifying **`72.1%`** LRU mesh cache hit rate (`604` hits, `234` misses).

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
- **JSON Schema**: Top-level fields include `suite`, `mode`, `adapter`, `isWarp`, `vramMB`, `debugLayer`, `d3d12DebugErrors`, `timestampFreqHz`, `gpuWarmupFrames`, `gpuMeasuredFrames`, `allPassed`, and `subsystems.{fluidSim, radianceCascades, textureForge, audioDsp, destruction, floraAndHydrology, tamrielStreamer}` with nested `{mean, p50, p95, p99, min, max}` latency objects for every timed stage.
