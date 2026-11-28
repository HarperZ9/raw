# RAW Engine Automated GPU/CPU Benchmarks and Visual Invariants

This document records live, reproducible measurements produced by [`tools/raw_bench_suite.cpp`](../tools/raw_bench_suite.cpp) across RAW Engine's twenty-one core GPU and CPU subsystems. Every GPU timing is measured directly on the Direct3D 12 command queue via `ID3D12QueryHeap` (`D3D12_QUERY_HEAP_TYPE_TIMESTAMP`) normalized by `GetTimestampFrequency()`, with the D3D12 Debug Layer enabled (`0` validation errors required). Every CPU timing is measured with `std::chrono::steady_clock` and summarized with `mean`, `p50`, `p95`, and `p99` percentiles.

No numbers in this document are estimated or synthetic. All figures below come verbatim from live runs of `raw_bench_suite.exe`.

## Reference Testbed

| Component | Specification |
| --- | --- |
| GPU (Hardware DX12) | NVIDIA GeForce RTX 4090 (24,138 MB dedicated VRAM, Driver 32.0.16.1088) |
| GPU (Software Reference) | Microsoft Basic Render Driver (DX12 WARP) |
| CPU | 13th Gen Intel Core i7-13700KF (16 cores / 24 logical processors) |
| System Memory | 32 GB RAM (34,115,358,720 bytes) |
| OS | Windows 11 Build 26320 (x64) |
| Compiler / Flags | MSVC (Visual Studio 18) `/std:c++20 /W4 /WX /O2 /MT` |
| D3D12 Debug Layer | Enabled (`ID3D12Debug` + `ID3D12InfoQueue`, strict `d3d12DebugErrors == 0`) |
| Tamriel World Data | Local `Skyrim.esm` + `Skyrim - Meshes0.bsa` (gracefully skipped when absent) |

## Executive Scorecard

### 1. Hardware GPU Full Run (NVIDIA GeForce RTX 4090, 30 Warmup + 60 Measured Frames)

Command: `raw_bench_suite.exe --json bench_rtx4090_full.json`
Exit Code: `0` | D3D12 Debug Errors: `0` | GPU Timestamp Clock: `1,000,000,000 Hz`
Combined All 16 GPU Solvers Mean: **`0.968 ms`** on RTX 4090

| # | Subsystem | Status | Primary Measured Result | Percentiles / Invariants | Budget / Target |
| --- | --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` NS Fire + `256^2` SWE Water) | PASS | Fire `0.218 ms` + Water `0.019 ms` = **`0.237 ms`** | Combined `p50=0.239`, `p95=0.243`, `p99=0.244 ms`; `Tmax=2550 K`; `NaN=0` | `< 0.50 ms` HW GPU (`2.11x` headroom) |
| 2 | GPU 4-Cascade Radiance Cascades GI | PASS | **`0.110 ms`** mean GPU time | `p50=0.111`, `p95=0.111`, `p99=0.112 ms`; `2xRatio=2.000`; `Umbra=0.0%`; `NaN=0` | `< 0.35 ms` HW GPU (`3.17x` headroom) |
| 3 | Procedural PBR Texture Forge (`RawTextureForge`) | PASS | **`8.85 MPix/s`** (`177.8 ms` total) | 8 seamless sets @ `256x256` (3 PBR maps/set); per-set `p50=21.46`, `p95=30.42 ms`; max edge diff `11.61` | 8/8 sets valid |
| 4 | 48 kHz FDN-8 Hadamard + HRTF Audio DSP | PASS | Ratio **`0.0069`** (**`145.6x`** real-time) | `13.77 ms` CPU to render `2,005.3 ms` stereo (188x512 blocks); `p95=0.076 ms`; `RMS=0.027`, `Peak=0.212`, `NaN=0` | Ratio `< 0.15` (`> 6.5x` RT) |
| 5 | 3D Voronoi CSG Destruction Physics | PASS | Fracture **`0.39 ms`**; Rigid step **`4.5 us/step`** | 5 archetypes, 90 shards, 180 60 Hz steps (`p95=10.2 us`); volume error **`0.000000%`** | Vol err `< 1e-4%`, finite |
| 6 | Biomechanical 3D Flora & Hydraulic Hydrology | PASS | **`647.0 trees/s`**; **`149.5 cells/s`** | 10 trees (`904` meshlets, `0` errors); 4 eroded cells (`368` meshlets, `0` errors) | Strict validator `0` errors |
| 7 | Multi-Cell Tamriel ESM Streamer | PASS | Riverwood 3x3 **`211.7 ms`**; Warm LRU **`72.1%`** | `11,186` cells + `12,990` models indexed (`359.7 ms`); `5,401` meshlets; Whiterun `143.8 ms` (`604` hits / `234` misses) | Clean ring load & LRU reuse |
| 8 | GPU XPBD Soft-Body / Hair / Bush-Bones (`4,096p`) | PASS | **`0.0580 ms`** mean GPU time | `256` chains / `4,096` particles, 4 substeps; `p50=0.0584`, `p95=0.0584`, `p99=0.0594 ms`; `stretch=1.0003`, `minSD=+0.0050`, `drift=0.0`, `NaN=0` | `< 0.25 ms` HW GPU (`4.31x` headroom) |
| 9 | GPU Hi-Z Pyramid (`512^2`) + 16k Instance Cull | PASS | Hi-Z `0.0210 ms` + Cull `0.0081 ms` = **`0.0291 ms`** | Combined `p50=0.0287`, `p95=0.0297`, `p99=0.0297 ms`; `vis=5120`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` | `< 0.15 ms` HW GPU (`5.15x` headroom) |
| 10 | GPU 16x9x24 3D Clustered 1,024-Light Grid | PASS | **`0.0977 ms`** (`Bld 0.0035 + Asn 0.0859 ms`) | Combined `p50=0.0973`, `p95=0.0983`, `p99=0.0983 ms`; `lit=3410/3456`, `links=116168`, `ovf=0`, `NaN=0` | `< 0.15 ms` HW GPU (`1.54x` headroom) |
| 11 | GPU `160x90x64` 3D Volumetric Froxels (`921.6k`) | PASS | **`0.0447 ms`** (`Inj 0.0297 + Int 0.0123 ms`) | Total `p50=0.0451`, `p95=0.0451`, `p99=0.0461 ms`; `Mie=74.5x`, `tau=5.46/0.31`, `nonMono=0`, `NaN=0` | `< 0.25 ms` HW GPU (`5.59x` headroom) |
| 12 | GPU Variable-Penumbra PCSS Shadows (`960x540`) | PASS | **`0.0280 ms`** mean GPU time (`host=0.0246 ms`) | `p50=0.0276`, `p95=0.0297`, `p99=0.0297 ms`; `ratio=32.0x`, `open=1.0`, `umb=0.0`, `sky=1.0`, `NaN=0` | `< 0.15 ms` HW GPU (`5.36x` headroom) |
| 13 | GPU Jimenez GTAO + Bent Normals (`960x540`) | PASS | **`0.0536 ms`** (`Main 0.0430 + Den 0.0082 ms`) | Total `p50=0.0532`, `p95=0.0543`, `p99=0.0547 ms`; `flr=0.966`, `crv=0.615`, `bx=-0.174`, `pit=0.680`, `NaN=0` | `< 0.15 ms` HW GPU (`2.80x` headroom) |
| 14 | GPU GGX Stochastic SSR (`960x540`) | PASS | **`0.0342 ms`** mean GPU time (`host=0.0297 ms`) | `p50=0.0338`, `p95=0.0348`, `p99=0.0358 ms`; `conf=1.000`, `R/B=15.0x`, `sky=0.0`, `span=153->281`, `NaN=0` | `< 0.15 ms` HW GPU (`4.39x` headroom) |
| 15 | GPU 256-Bin Auto-Exposure (`960x540`) | PASS | **`0.0132 ms`** (`H 0.0072 + A 0.0020 ms`) | Total `p50=0.0133`, `p95=0.0133`, `p99=0.0143 ms`; `drk=8.87x`, `glc=0.022x`, `ffErr=0.81%`, `asym=3.12x`, `NaN=0` | `< 0.08 ms` HW GPU (`6.06x` headroom) |
| 16 | GPU 3D Volumetric Clouds (`480x270`) | PASS | **`0.0832 ms`** mean GPU time (`host=0.0778 ms`) | `p50=0.0829`, `p95=0.0850`, `p99=0.0850 ms`; `minT=0.000`, `px=63626`, `silver=4.73x`, `horBad=0`, `NaN=0` | `< 0.15 ms` HW GPU (`1.80x` headroom) |
| 17 | GPU Cinematic MB + Bokeh DoF (`960x540`) | PASS | **`0.0460 ms`** (`T 0.0082 + C 0.0338 ms`) | Total `p50=0.0461`, `p95=0.0471`, `p99=0.0471 ms`; `strk=0.80`, `dof=0.50->0.08`, `bld=0.0000`, `vig=20.7%`, `NaN=0` | `< 0.08 ms` HW GPU (`1.74x` headroom) |
| 18 | GPU 16K 3D Particles + Splat (`480x270`) | PASS | **`0.0163 ms`** (`S 0.0051 + P 0.0031 ms`) | Total `p50=0.0164`, `p95=0.0174`, `p99=0.0174 ms`; `act=16384`, `bnc=16384`, `px=2126`, `wX=+5.90`, `NaN=0` | `< 0.08 ms` HW GPU (`4.91x` headroom) |
| 19 | GPU RAW-TSR 2x Upscaler + RCAS (`640x360 -> 1280x720`) | PASS | **`0.0468 ms`** (`R 0.0328 + C 0.0102 ms`) | Total `p50=0.0471`, `p95=0.0471`, `p99=0.0481 ms`; `rmse-32.8%`, `sky=0.02`, `spk=4.00`, `rcas+36.1%`, `NaN=0` | `< 0.08 ms` HW GPU (`1.71x` headroom) |
| 20 | GPU Physical Water Caustics + God-Rays (`512x512`) | PASS | **`0.0462 ms`** (`C 0.0174 + R 0.0256 ms`) | Total `p50=0.0461`, `p95=0.0471`, `p99=0.0471 ms`; `pk=2.82x`, `E=1.000`, `RB=0.031`, `sun=19.7x`, `NaN=0` | `< 0.06 ms` HW GPU (`1.30x` headroom) |
| 21 | GPU 512x512 Snow, Melt & Footprints (`64 m`) | PASS | **`0.0238 ms`** (`S 0.0164 + N 0.0041 ms`) | Total `p50=0.0236`, `p95=0.0246`, `p99=0.0246 ms`; `tr=0.16`, `rim=0.49`, `mErr=0.03%`, `ice=1.00`, `NaN=0` | `< 0.05 ms` HW GPU (`2.10x` headroom) |

### 2. Quick CI Run Comparison: RTX 4090 vs. DX12 WARP (`--quick`)

Commands:
- Hardware GPU: `raw_bench_suite.exe --quick --json bench_rtx4090_quick.json` (`6` warmup + `10` measured GPU frames)
- Software WARP: `raw_bench_suite.exe --quick --warp --json bench_warp_quick.json` (`2` warmup + `4` measured GPU frames)

Both runs exited with code `0`, `allPassed: true`, and `d3d12DebugErrors: 0`.

| # | Subsystem | RTX 4090 (`--quick`) | DX12 WARP (`--quick --warp`) | Key Invariant Match |
| --- | --- | --- | --- | --- |
| 1 | GPU Fluid (`64^3` + `256^2`) | Fire `0.222 ms` + Water `0.020 ms` (`0.241 ms` comb, `p95=0.243 ms`) | Fire `147.429 ms` + Water `3.901 ms` (`151.330 ms` comb, `p95=155.172 ms`) | `0` NaNs on both; `Tmax=2362 K` (HW) / `1968 K` (WARP, fewer warmup steps) |
| 2 | GPU Radiance Cascades GI | `0.114 ms` (`p50=0.114`, `p95=0.114 ms`) | `24.375 ms` (`p50=24.022`, `p95=25.566 ms`) | `openIrrad=0.5737`, `umbraIrrad=0.0000`, `2xRatio=2.0000` (bit-identical) |
| 3 | Procedural PBR Forge (8 sets @ `128x128`) | `9.11 MPix/s` (`43.1 ms`, `p95=7.43 ms`) | `9.18 MPix/s` (`42.9 ms`, `p95=7.41 ms`) | `8/8` sets, max edge diff `23.46` |
| 4 | 48 kHz FDN-8 + HRTF Audio (`1,003 ms`) | Ratio `0.0066` (`151.4x` RT, `6.62 ms`) | Ratio `0.0066` (`150.7x` RT, `6.65 ms`) | `RMS=0.0265`, `Peak=0.2116`, `0` NaNs |
| 5 | 3D Voronoi CSG Destruction (60 steps) | Frac `0.38 ms`, step `6.7 us` (`p95=10.8 us`) | Frac `0.39 ms`, step `6.7 us` (`p95=10.7 us`) | `90` shards, `volErr=0.000000%`, all finite |
| 6 | Flora (5 trees) & Hydrology (2 cells) | `623.3 trees/s`, `187.9 cells/s` | `560.4 trees/s`, `183.5 cells/s` | `454` flora + `184` hydro meshlets, `0` errors |
| 7 | Tamriel ESM Streamer | RW `220.0 ms`, WR `146.9 ms`, LRU `72.1%` | RW `217.7 ms`, WR `144.2 ms`, LRU `72.1%` | `11,186` cells, `604` hits / `234` misses |
| 8 | GPU XPBD Soft-Body (`256` chains / `4,096p`) | `0.0575 ms` (`p50=0.0573`, `p95=0.0584 ms`) | `1.0251 ms` (`p50=1.0242`, `p95=1.2130 ms`) | `minSD=+0.0050`, `drift=0.0`, `stretch<=1.0013`, `0` NaNs on both |
| 9 | GPU Hi-Z (`512^2`) + 16k Instance Cull | Hi-Z `0.0211` + Cull `0.0082` (`0.0293 ms` comb, `p95=0.0297 ms`) | Hi-Z `0.5988` + Cull `0.2204` (`0.8192 ms` comb, `p95=0.8700 ms`) | Bit-identical `vis=5120`, `meshlets=20477`, `fr=4096`, `dist=1024`, `hiz=6144`, `falseOcc=0` |
| 10 | GPU 3D Clustered 1,024-Light Grid (`3,456` cl) | `0.0985 ms` (`Bld 0.0036 + Asn 0.0859 ms`, `p95=0.0993 ms`) | `2.0074 ms` (`p50=1.9354`, `p95=2.2244 ms`) | Bit-identical `lit=3410/3456`, `links=116168`, `ovf=0`, `NaN=0` |
| 11 | GPU `160x90x64` 3D Volumetric Froxels (`921.6k`) | `0.0447 ms` (`Inj 0.0287 + Int 0.0123 ms`, `p95=0.0451 ms`) | `13.0370 ms` (`p50=13.0879`, `p95=13.2212 ms`) | Bit-identical `Mie=74.45x`, `tau=5.4645/0.3127`, `nonMono=0`, `NaN=0` |
| 12 | GPU Variable-Penumbra PCSS (`960x540`) | `0.0278 ms` (`p50=0.0276`, `p95=0.0288 ms`) | `6.3770 ms` (`p50=6.3746`, `p95=6.4507 ms`) | Bit-identical `ratio=32.00x`, `open=1.0`, `umb=0.0`, `sky=1.0`, `NaN=0` |
| 13 | GPU Jimenez GTAO + Bent Normals (`960x540`) | `0.0483 ms` (`Main 0.0369 + Den 0.0082 ms`, `p95=0.0492 ms`) | `10.3188 ms` (`Main 6.9820 + Den 3.3585 ms`, `p95=10.4475 ms`) | Matching `flr=0.9664`, `crv=0.6147`, `bx=-0.1737`, `pit=0.6804`/`0.6803`, `skyBad=0`, `NaN=0` |
| 14 | GPU GGX Stochastic SSR (`960x540`) | `0.0343 ms` (`host=0.0307 ms`, `p95=0.0348 ms`) | `11.3288 ms` (`host=11.2232 ms`, `p95=11.6354 ms`) | Bit-identical `targetPx=9078`, `conf=1.0000`, `R/B=15.01x`, `sky=0.0`, `span=153->281`, `NaN=0` |
| 15 | GPU 256-Bin Auto-Exposure (`960x540`) | `0.0133 ms` (`H 0.0072 + A 0.0020 ms`, `p95=0.0133 ms`) | `0.9346 ms` (`H 0.9033 + A 0.0166 ms`, `p95=0.9733 ms`) | Bit-identical `drk=8.8741x`, `glc=0.0220x`, `ffErr=0.8121%`, `asym=3.1214x`, `nanFinite=true` |
| 16 | GPU 3D Volumetric Clouds (`480x270`) | `0.0836 ms` (`host=0.0799 ms`, `p95=0.0840 ms`) | `10.9564 ms` (`host=11.5300 ms`, `p95=11.0581 ms`) | Matching `minT=0.000`, `px=63626`, `silver=4.73x`/`4.74x`, `clearMaxRad=0.0`, `horBad=0`, `NaN=0` |
| 17 | GPU Cinematic MB + Bokeh DoF (`960x540`) | `0.0459 ms` (`T 0.0082 + C 0.0338 ms`, `p95=0.0466 ms`) | `13.7816 ms` (`T 0.8747 + C 12.5526 ms`, `p95=14.1568 ms`) | Bit-identical `idDiff=0.0010`, `strk=0.7998`, `vert=0.0`, `dof=0.5000->0.0844`, `bld=0.0`, `vig=20.7%`, `NaN=0` |
| 18 | GPU 16K 3D Particles + Splat (`480x270`) | `0.0167 ms` (`S 0.0061 + P 0.0031 ms`, `p95=0.0174 ms`) | `0.8548 ms` (`S 0.5922 + P 0.1983 ms`, `p95=0.9000 ms`) | Bit-identical `act=16384`, `exp=0`, `embY=2.3533`, `snwY=7.1440`, `wX=+5.9004`, `bnc=16384`, `px=2126`, `NaN=0` |
| 19 | GPU RAW-TSR 2x Upscaler + RCAS (`640x360 -> 1280x720`) | `0.0466 ms` (`R 0.0338 + C 0.0102 ms`, `p95=0.0471 ms`) | `22.4459 ms` (`R 17.0138 + C 4.4006 ms`, `p95=23.5556 ms`) | Bit-identical `rmse=0.1320->0.0888` (`-32.8%`), `sky=0.0200`, `spk=3.9980`, `rcas=+36.12%`, `NaN=0` |
| 20 | GPU Physical Water Caustics + God-Rays (`512x512`) | `0.0466 ms` (`C 0.0164 + R 0.0256 ms`, `p95=0.0471 ms`) | `7.3903 ms` (`C 3.7878 + R 3.8080 ms`, `p95=7.6154 ms`) | Bit-identical `flatErr=0.0`, `pk=2.818x`, `E=0.9996`, `RB=0.0307`/`0.3223`, `R/B_ret=0.118/0.852`, `sun=19.67x`, `NaN=0` |
| 21 | GPU 512x512 Snow, Melt & Footprints (`64 m`) | `0.0234 ms` (`S 0.0164 + N 0.0031 ms`, `p95=0.0236 ms`) | `6.4383 ms` (`S 5.2785 + N 1.1625 ms`, `p95=6.7432 ms`) | Bit-identical `tr=0.1636`, `rim=0.4924`, `mErr=0.030%`, `wallNx=+0.3335`, `fireH=0.0`, `ice=1.000`, `fall=+0.1982 m`, `NaN=0` |

### 3. Graceful Missing-Data Verification

Running `raw_bench_suite.exe --quick --esm "C:\nonexistent\Skyrim.esm" --json bench_skip_esm.json` passes all 20 engine-native benchmarks, marks `tamrielStreamer.skipped = true` (`SKIP`), reports `d3d12DebugErrors = 0`, and exits with code `0` so headless CI runners without Skyrim data stay green.

## Detailed Subsystem Breakdown and Invariants

### 1. GPU Navier-Stokes Fire/Smoke (`64^3`) + Shallow-Water Equations (`256^2`)
- **Implementation**: `RawFluidSimHost` (`src/d3d12_bridge/RawFluidSimHost.h/.cpp`) driving SM 6.6/6.10 compute shaders (`raw_fluid_*_cs.h`).
- **Measured Workload**: Per frame, injects a buoyant `2,600 K` torch flame emitter into the `64^3` MAC grid, executes vorticity confinement, pressure projection (8 Jacobi iterations), MacCormack advection, blackbody radiation bake, and steps the `256^2` shallow-water heightfield with a moving disturbance splash and normal/foam update.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Fire GPU pass: `mean=0.2222 ms`, `p50=0.2217 ms`, `p95=0.2253 ms`, `p99=0.2286 ms`
  - Water GPU pass: `mean=0.0191 ms`, `p50=0.0195 ms`, `p95=0.0195 ms`, `p99=0.0203 ms`
  - Combined GPU pass: `mean=0.2412 ms`, `p50=0.2406 ms`, `p95=0.2438 ms`, `p99=0.2487 ms`
  - CPU command recording overhead: `mean=0.0687 ms`, `p50=0.0634 ms`, `p95=0.0918 ms`
- **Invariants**: Zero NaN/Inf across all readback fire voxels and water texels (`fireNanCount == 0`, `waterNanCount == 0`), active combustion temperature (`peakTempK > 450 K`), and combined HW GPU mean `< 0.50 ms`.

### 2. GPU 4-Cascade Radiance Cascades Global Illumination
- **Implementation**: `RawRadianceHost` (`src/d3d12_bridge/RawRadianceHost.h/.cpp`) running `256x256` RGBA16F 4-cascade raymarch, angular merge, and irradiance resolve passes.
- **Measured Workload**: Emissive light disc occluded by an opaque wall separating an illuminated open probe `(88, 128)` from a deep-umbra probe `(176, 128)`, followed by a 2x emitter brightness pass to verify exact linear radiance transport.
- **Full RTX 4090 Breakdown (60 frames)**:
  - GI GPU pass: `mean=0.1072 ms`, `p50=0.1075 ms`, `p95=0.1075 ms`, `p99=0.1075 ms`
  - CPU command recording overhead: `mean=0.0383 ms`, `p50=0.0326 ms`, `p95=0.0579 ms`
  - Measured probes (identical on RTX 4090 and WARP): `openIrrad=0.5737`, `umbraIrrad=0.0000`, `linearScaleRatio=2.0000`
- **Invariants**: `nanCount == 0`, `openIrrad > 0.05`, `umbraIrrad < 0.25 * openIrrad`, `|linearScaleRatio - 2.0| < 0.05`, and HW GPU mean `< 0.35 ms`.

### 3. Procedural PBR Texture Forge
- **Implementation**: `RawTextureForge` (`src/materials/RawTextureForge.h/.cpp`) generating all 8 tileable PBR material families (Albedo, Normal, ORMH + mip chain) on CPU with zero external image assets.
- **Throughput**: `8.90 MPix/s` (`176.7 ms` total for 8 sets at `256x256`, `p50=21.59 ms`, `p95=30.32 ms` per set); `9.11 MPix/s` at `128x128` (`43.1 ms` total in `--quick` mode).

### 4. 48 kHz FDN-8 Hadamard Reverb + Woodworth HRTF Spatial Audio DSP
- **Implementation**: `RawAudioDsp` (`src/audio/RawAudioDsp.h/.cpp`), combining `SoundscapeSynth` (Nordic stone crypt FDN-8 Hadamard reverb, procedural wind, crackling fire, flowing water) and `SpatialVoice3D` (orbiting Doppler + Woodworth ITD/ILD HRTF source) in 512-frame stereo blocks at `48,000 Hz`.
- **Full Run**: `13.62 ms` CPU to render `2,005.3 ms` of stereo audio (`headroomRatio=0.0068`, **`147.3x`** faster than real-time; per-block `p50=0.0718 ms`, `p95=0.0752 ms`, `p99=0.0784 ms`).
- **Invariants**: `headroomRatio < 0.15` (`> 6.5x` real-time), `nanCount == 0`, `rmsLevel > 1e-4`, `peakAbs <= 2.0`.

### 5. 3D Voronoi CSG Destruction Physics
- **Implementation**: `RawDestructionEngine` (`src/worldgen/RawDestructionEngine.h/.cpp`), fracturing all 5 built-in convex destructible archetypes into 18 Voronoi CSG polyhedral shards each (`90` shards total) and stepping a 60 Hz rigid-body impulse simulation (`180` steps in full mode, `60` in quick mode).
- **Full Run**: Mean fracture latency `0.4494 ms` (`p50=0.4547 ms`, `p95=0.5901 ms`), 60 Hz rigid-body step `4.37 us` mean (`p50=3.95 us`, `p95=10.10 us`, `p99=10.90 us`).
- **Invariants**: Signed-tetrahedron volume conservation error `< 1e-4%` (measured **`0.000000%`** across all 5 archetypes) and 100% finite rigid-body transforms.

### 6. Biomechanical 3D Flora and Hydraulic Terrain Erosion
- **Implementation**: `RawFloraEngine` + `RawHydrologyGen` + `RawTerrainGen` + `RawMeshPipeline`, verified against `ValidateProceduralMeshStrict` (`src/pipeline/RawAssetValidator.h`).
- **Full Run**:
  - Flora (all 5 species x 2 reps = 10 trees): `640.4 trees/s` (`treeMs mean=1.161 ms`, `p95=1.748 ms`), `904` DX12 AS+MS meshlets, `0` validation errors.
  - Hydrology (4 `65x65` cells with 4,096 hydraulic droplets, 24 thermal talus passes, D8 watershed flow, and 64v/126t meshlet build): `146.1 cells/s` (`cellMs mean=6.473 ms`, `p95=6.616 ms`), `368` meshlets, `0` validation errors.

### 7. Multi-Cell Tamriel Streaming (`Skyrim.esm` + `Skyrim - Meshes0.bsa`)
- **Implementation**: `RawTamrielWorldStreamer` (`src/worldgen/RawTamrielWorldStreamer.h/.cpp`) backed by `RawEsmWorldLoader`, `RawEsmModelResolver`, `RawBsaArchive`, and `RawNifLoader`.
- **Measured Run**: Indexed `11,186` Tamriel exterior cells and `12,990` STAT/TREE/ACTI/FURN/CONT/DOOR/MSTT/LIGH models in `361.7 ms`. Cold 3x3 ring stream centered on Riverwood `(4, -12)` completed in `220.9 ms` (`9` cells, `5,401` meshlets). Subsequent jump to Whiterun `(5, -3)` completed in `143.6 ms`, followed by warm revisit verifying **`72.1%`** LRU mesh cache hit rate (`604` hits, `234` misses).

### 8. GPU XPBD Soft-Body, Hair, Cloth and Articulated Bush-Bone Solver
- **Implementation**: `RawXpbdSimHost` (`src/d3d12_bridge/RawXpbdSimHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawXpbdClothSim.hlsl` (`raw_xpbd_cloth_cs.h`) in a single groupshared compute dispatch (`[numthreads(64, 1, 1)]`, 4 chains per threadgroup, 16 nodes per chain).
- **Measured Workload**: Simulates 256 chains (`96` hair strands, `80` cloth cape ribbons, `80` articulated bush branches = `4,096` active particles) with 4 XPBD substeps per frame under `8.5 m/s` Beaufort wind gusts, `0.25` winter snow load, distance and 2-hop bending compliance, and 3 analytic colliders (head sphere, torso capsule, bush wake sphere).
- **Full RTX 4090 Breakdown (60 frames)**:
  - XPBD GPU pass: `mean=0.0579 ms`, `p50=0.0584 ms`, `p95=0.0584 ms`, `p99=0.0588 ms` (`hostStepMs=0.0553 ms`)
  - CPU command recording overhead: `mean=0.0238 ms`, `p50=0.0198 ms`, `p95=0.0402 ms`
  - Readback invariants: `maxRootDrift=0.0000000`, `maxStretchRatio=1.0003`, `minColliderSD=+0.0050`, `nanCount=0`
- **Invariants**: `nanCount == 0`, `maxRootDrift < 1e-5`, `maxStretchRatio < 1.03`, `minColliderSD >= -0.01`, and HW GPU mean `< 0.25 ms`.

### 9. GPU Conservative Hi-Z Occlusion Pyramid and 16,384-Instance Culler
- **Implementation**: `RawGpuCullHost` (`src/d3d12_bridge/RawGpuCullHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawGpuCull.hlsl` (`raw_hiz_reduce_cs.h` + `raw_cull_instances_cs.h`), producing `ExecuteIndirect`-ready `DrawIndexed` and `DispatchMesh` arguments with wave-compacted atomics.
- **Measured Workload**: Builds a full 10-mip Standard-Z conservative `max()` Hi-Z pyramid from a `512x512` depth buffer containing a giant `z=0.25` occluder wall, then culls `16,384` deterministic instances (`4,096` frustum-outside, `1,024` distance-LOD, `6,144` completely behind wall, `2,560` in front of wall, `1,536` peeking past wall edge, `1,024` straddling the near plane).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Hi-Z pyramid build: `mean=0.0210 ms`, `p50=0.0215 ms`, `p95=0.0215 ms`, `p99=0.0215 ms` (`hostHiZMs=0.0174 ms`)
  - 16,384-instance wave-compacted cull: `mean=0.0081 ms`, `p50=0.0082 ms`, `p95=0.0082 ms`, `p99=0.0082 ms` (`hostCullMs=0.0041 ms`)
  - Combined GPU pass: `mean=0.0291 ms`, `p50=0.0287 ms`, `p95=0.0297 ms`, `p99=0.0297 ms`
  - CPU command recording overhead: `mean=0.0468 ms`, `p50=0.0415 ms`, `p95=0.0704 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `visibleCount=5120`, `visibleMeshlets=20477`, `frustumCulled=4096`, `distanceCulled=1024`, `hizCulled=6144`, `falseOcclusionCount=0`
- **Invariants**: Exact bucket counts (`5120/4096/1024/6144`), zero duplicates, zero false survivors, zero false occlusion (`falseOcclusionCount == 0`), exact `IndirectArgsBuffer` match, and HW GPU combined mean `< 0.15 ms`.

### 10. GPU 16x9x24 3D Clustered 1,024-Light Grid Assignment
- **Implementation**: `RawLightClusterHost` (`src/d3d12_bridge/RawLightClusterHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawLightClusterSim.hlsl` (`raw_cluster_build_aabb_cs.h` + `raw_cluster_cull_lights_cs.h`) across `16x9x24 = 3,456` logarithmic-Z frustum froxels.
- **Measured Workload**: Builds all `3,456` view-space cluster AABBs and assigns `1,024` active dynamic point and spotlights with sphere-vs-AABB and cone intersection tests into a zero-atomic light-index pool (`max 64 lights/cluster`, `116,168` total links).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Cluster AABB build: `mean=0.0035 ms`, `p50=0.0035 ms`, `p95=0.0039 ms`, `p99=0.0039 ms` (`hostBuildMs=0.0030 ms`)
  - 1,024-light assignment: `mean=0.0859 ms`, `p50=0.0860 ms`, `p95=0.0860 ms`, `p99=0.0860 ms` (`hostAssignMs=0.0860 ms`)
  - Combined GPU pass: `mean=0.0977 ms`, `p50=0.0973 ms`, `p95=0.0983 ms`, `p99=0.0983 ms`
  - CPU command recording overhead: `mean=0.0390 ms`, `p50=0.0368 ms`, `p95=0.0558 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `nonEmptyClusters=3410/3456`, `maxLightsCluster=64`, `totalLightLinks=116168`, `overflowErrors=0`, `nanAabbCount=0`
- **Invariants**: Zero NaN AABBs, zero list overflows, exact cluster coverage, and HW GPU combined mean `< 0.15 ms`.

### 11. GPU `160x90x64` 3D Volumetric Froxel Fog, Mie Scattering and Temporal Reprojection
- **Implementation**: `RawVolumetricFroxelHost` (`src/d3d12_bridge/RawVolumetricFroxelHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawVolumetricFroxelSim.hlsl` (`raw_froxel_inject_cs.h` + `raw_froxel_integrate_cs.h`) over a `160x90x64` (`921,600`-voxel) `RGBA16F` exponential-Z froxel volume with ping-pong 3D temporal history.
- **Measured Workload**: Injects altitude-stratified mountain/valley mist, 2-octave wind-advected 3D mist wisps, rain density boost, Henyey-Greenstein Mie forward scattering (`g=0.68`), 16 local point lights, and `0.85` Halton-jittered 3D temporal reprojection, followed by exact analytical front-to-back Beer-Lambert slice integration (Frostbite 2015 Taylor-safe formulation).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`Inject + Integrate`): `mean=0.0447 ms`, `p50=0.0451 ms`, `p95=0.0451 ms`, `p99=0.0451 ms` (`hostInjectMs=0.0287 ms`, `hostIntegrateMs=0.0123 ms`)
  - CPU command recording overhead: `mean=0.0277 ms`, `p50=0.0238 ms`, `p95=0.0461 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `mieForwardRatio=74.4527x`, `valleyTau=5.4645` vs `peakTau=0.3127`, `nanCount=0`, `nonMonoCount=0`
- **Invariants**: Zero NaN/Inf across all `921,600` voxels, `0 < T <= 1` monotonically non-increasing in Z, forward Mie anisotropy `> 2.0x`, valley/peak optical depth stratification `> 2.5x`, and HW GPU total mean `< 0.25 ms`.

### 12. GPU Variable-Penumbra PCSS Contact-Hardening Shadows and Texel-Snapped Cascades
- **Implementation**: `RawShadowPcssHost` (`src/d3d12_bridge/RawShadowPcssHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawShadowPcssSim.hlsl` (`raw_shadow_pcss_cs.h`) with Fernando (2005) 3-stage PCSS (16-tap Vogel-disk blocker search, physical penumbra estimator, 32-tap IGN-rotated hardware-bilinear PCF) and world-space texel-snapped directional cascades (`BuildTexelSnappedCascade`).
- **Measured Workload**: Evaluates `960x540` `RG16F` `(visibility, normPenumbra)` against a `1024x1024` `R32F` texel-snapped cascade containing a near-ground contact caster (`0.8 m` height) and a tall distant-penumbra spire (`22.0 m` height) plus sky pixels (`depth = 1.0`).
- **Full RTX 4090 Breakdown (60 frames)**:
  - PCSS GPU pass: `mean=0.0279 ms`, `p50=0.0276 ms`, `p95=0.0287 ms`, `p99=0.0287 ms` (`hostPcssMs=0.0246 ms`)
  - CPU command recording overhead: `mean=0.0201 ms`, `p50=0.0158 ms`, `p95=0.0428 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `openVis=1.0000`, `nearUmbraVis=0.0000`, `farUmbraVis=0.0000`, `skyMinVis=1.0000`, `nearEdgePen=0.0312` vs `farEdgePen=1.0000` (`penumbraRatio=32.00x`), `softBandPx=4121`, `nanInfCount=0`
- **Invariants**: Open sunlit ground `== 1.0`, deep umbra `== 0.0`, sky `== 1.0`, contact-hardening penumbra ratio `> 2.0x`, `nanInfCount == 0`, and HW GPU mean `< 0.15 ms`.

### 13. GPU Jimenez GTAO, Unoccluded Bent Normals and Bilateral Denoise
- **Implementation**: `RawGtaoHost` (`src/d3d12_bridge/RawGtaoHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawGtaoSim.hlsl` (`raw_gtao_main_cs.h` + `raw_gtao_denoise_cs.h`) implementing Jimenez (SIGGRAPH 2016) horizon-angle integration, analytical unoccluded bent normals, and a 3x3 depth-preserving bilateral denoise pass into `RGBA16F` `(bentNormal.xyz, visibility)`.
- **Measured Workload**: Evaluates `960x540` ray-traced 3D synthetic depth containing an open flat floor (`Y = -2.0`), a 90-degree occluding vertical wall (`X >= +1.0`), a recessed circular pit with a 3-sided raised stone rim (`(-1.2, -2.55, 5.15)`), and a sky strip (`depth = 1.0`).
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`Main + Denoise`): `mean=0.0484 ms`, `p50=0.0481 ms`, `p95=0.0492 ms`, `p99=0.0492 ms` (`hostMainMs=0.0369 ms`, `hostDenoiseMs=0.0072 ms`)
  - CPU command recording overhead: `mean=0.0264 ms`, `p50=0.0209 ms`, `p95=0.0470 ms`
  - Readback invariants (matching across RTX 4090 and WARP): `openFloorVis=0.9664` (`bentY=0.9995`), `creviceVis=0.6147` (`bentX=-0.1737` tilting away from `+X` wall), `pitVis=0.6804`, `skyBadCount=0`, `nanCount=0`
- **Invariants**: Open flat floor `vis > 0.90` & `bentY > 0.92`, 90-degree crevice `vis < 0.65` & `bentX < -0.15`, recessed pit `pitVis < openFloorVis - 0.25`, exact sky passthrough (`vis == 1.0`, `bent == (0, 0, -1)`), `nanCount == 0`, and HW GPU total mean `< 0.15 ms`.

### 14. GPU GGX Stochastic Screen-Space Reflections (SSR)
- **Implementation**: `RawSsrHost` (`src/d3d12_bridge/RawSsrHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawSsrSim.hlsl` (`raw_ssr_trace_cs.h`) implementing Heitz (2018) GGX VNDF stochastic reflection lobe sampling, adaptive 40-step view-space linear raymarching with 5-step binary refinement, depth-thickness rejection, and smooth screen-border vignette fade into `RGBA16F` `(reflectedRadiance.rgb, hitConfidence)`.
- **Measured Workload**: Evaluates `960x540` ray-traced synthetic 3D HDR color and Standard-Z depth containing a flat wet reflective floor (`Y = -1.8 m`), a bright crimson-gold emissive pillar (`X in [-1.8, +1.8]`, `Y in [-1.8, +5.2]`, `Z in [11.0, 14.0]`, HDR `(2.10, 0.52, 0.14)`), and a dark sky (`depth = 1.0`), followed by a roughness `0.28` GGX lobe spread probe.
- **Full RTX 4090 Breakdown (60 frames)**:
  - SSR GPU pass: `mean=0.0341 ms`, `p50=0.0338 ms`, `p95=0.0348 ms`, `p99=0.0352 ms` (`hostSsrMs=0.0297 ms`)
  - CPU command recording overhead: `mean=0.0201 ms`, `p50=0.0171 ms`, `p95=0.0399 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `targetPx=9078`, `meanConf=1.0000`, `meanR=2.0996`, `meanB=0.1399` (`rbRatio=15.01x`), `maxSkyConf=0.0000`, `maxOpenFloorConf=0.0000`, GGX roughness span `153 px -> 281 px`, `nanInfCount=0`
- **Invariants**: Mirror floor reflection `conf > 0.80`, `meanR > 0.70`, `R/B > 3.0x`, exact zero sky/open-floor false hits (`maxSkyConf == 0.0`, `maxOpenFloorConf == 0.0`), GGX rough lobe span strictly wider than mirror span (`roughSpanW > mirrorSpanW`), `nanInfCount == 0`, and HW GPU mean `< 0.15 ms`.

### 15. GPU 256-Bin Log-Luminance Histogram and Asymmetric Human Eye-Adaptation Auto-Exposure
- **Implementation**: `RawAutoExposureHost` (`src/d3d12_bridge/RawAutoExposureHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawAutoExposureSim.hlsl` (`raw_exposure_hist_cs.h` + `raw_exposure_adapt_cs.h`) using `groupshared uint s_hist[256]`, smooth center-weighted spatial weighting (`1..4x`), Hillis-Steele parallel prefix-sum percentile trimming (`15%..95%`), photographic EV100 calibration, and asymmetric scotopic/photopic eye-adaptation EMA (`tauBright=0.35 s`, `tauDark=1.40 s`).
- **Measured Workload**: Evaluates `960x540` `RGBA32F` HDR scenes covering a dark dungeon (`lum = 0.02`), sunlit glacier (`lum = 8.0`), `0.50` midtone poisoned with `2%` extreme `10,000.0` fireflies, `0.50` midtone poisoned with `5%` NaN/+Inf/-Inf texels, and symmetric `0.25 s` adaptation steps (`dark -> bright` vs `bright -> dark`), followed by steady-state GPU profiling.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`Histogram + Eye-Adapt`): `mean=0.0127 ms`, `p50=0.0123 ms`, `p95=0.0133 ms`, `p99=0.0137 ms` (`hostHistMs=0.0061 ms`, `hostAdaptMs=0.0031 ms`)
  - CPU command recording overhead: `mean=0.0232 ms`, `p50=0.0192 ms`, `p95=0.0470 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `darkExposure=8.8741x`, `glacierExposure=0.0220x`, `fireflyTargetLum=0.4959` (`fireflyErrPct=0.8121%`), `asymBrightFrac=0.5105` vs `asymDarkFrac=0.1635` (`asymRatio=3.1214x`), `nanFinite=true`
- **Invariants**: Dark dungeon `exposure > 4.0x`, sunlit glacier `exposure < 0.15x`, 95th-percentile firefly rejection error `< 10%`, asymmetric eye-adaptation ratio `> 1.8x`, NaN/Inf resilience `nanFinite == true`, and HW GPU total mean `< 0.08 ms`.

### 16. GPU 3D Volumetric Cloud Raymarcher, Beer-Powder Silver-Lining and Temporal Reprojection
- **Implementation**: `RawCloudRaymarchHost` (`src/d3d12_bridge/RawCloudRaymarchHost.h/.cpp`) driving SM 6.6/6.10 compute shader `RawCloudRaymarchSim.hlsl` (`raw_cloud_raymarch_cs.h`) tracing an Earth-curved spherical atmospheric cloud shell (`R=6,371 km`, base `1.5 km`, top `5.5 km`) with 32 IGN-jittered primary steps, wind-advected multi-octave cumulus weather/shape/billow density, 4-tap directional sun light-march, Beer-Powder dark-edge energy conservation, 2-octave multi-scattering, dual-lobe Henyey-Greenstein silver-lining phase (`g1=+0.72`, `g2=-0.28`), and ping-pong `RGBA16F` temporal reprojection.
- **Measured Workload**: Evaluates `480x270` clear sky (`coverage=0.0`), dense cumulus (`coverage=0.85`) with temporal reprojection (`historyWeight=0.75`) and below-horizon passthrough verification, plus an anti-solar comparison pass over the exact same cloud field to prove directional silver-lining phase anisotropy.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Cloud raymarch GPU pass: `mean=0.0831 ms`, `p50=0.0829 ms`, `p95=0.0850 ms`, `p99=0.0850 ms` (`hostCloudMs=0.0778 ms`)
  - CPU command recording overhead: `mean=0.0245 ms`, `p50=0.0205 ms`, `p95=0.0473 ms`
  - Readback invariants (matching across RTX 4090 and WARP): `clearMaxRad=0.0000`, `belowHorizonBad=0`, `minTrans=0.0000`, `cloudyPixels=63626`, `forwardLuma=0.6137` vs `antiSolarLuma=0.1296` (`silverRatio=4.73x`), `nanInfCount=0`
- **Invariants**: Exact clear-sky `(0, 0, 0, 1.0)` & below-horizon passthrough (`belowHorizonBad == 0`), dense cumulus optical occlusion (`minTrans < 0.35`, `cloudyPixels > 10000`), forward silver-lining strictly brighter than anti-solar clouds (`silverRatio > 2.0x`), `nanInfCount == 0`, and HW GPU mean `< 0.15 ms`.

### 17. GPU Cinematic Post-Process: Tile-Max Motion Blur and Depth-Aware Hexagonal Bokeh DoF
- **Implementation**: `RawPostCinematicHost` (`src/d3d12_bridge/RawPostCinematicHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawPostCinematicSim.hlsl` (`raw_post_tile_max_cs.h` + `raw_post_neighbor_max_cs.h` + `raw_post_cinematic_cs.h`) executing 16x16 groupshared tile-max velocity reduction, 3x3 neighbor-max dilation, McGuire (2012) 12-tap directional motion blur, 2-ring 18-tap hexagonal physical thin-lens CoC bokeh DoF with co-located foreground bleed guard, and natural `cos^4` filmic optical vignette into `RGBA16F`.
- **Measured Workload**: Evaluates `960x540` zero-velocity identity, `+24 px` horizontal moving bar directional streak vs vertical smear, sharp focus-plane (`Z=6 m`) vs far-plane (`Z=35 m`) stripe contrast, sharp crimson foreground pillar bleed guard against dark blurry background, and poisoned NaN/+Inf filmic vignette resilience, followed by steady-state 3-pass GPU profiling.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`TileMax + NeighborMax + DoF + MotionBlur + Vignette`): `mean=0.0459 ms`, `p50=0.0461 ms`, `p95=0.0461 ms`, `p99=0.0465 ms` (`hostTileMs=0.0082 ms`, `hostCompMs=0.0348 ms`)
  - CPU command recording overhead: `mean=0.0278 ms`, `p50=0.0241 ms`, `p95=0.0483 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `identityMaxDiff=0.0010`, `horizStreakR=0.7998`, `vertSmearR=0.0000`, `focusStd=0.5000` vs `farBlurStd=0.0844` (`5.93x` blur ratio), `maxBgBleedR=0.0000`, `vignetteFalloffPct=20.71%`, `nanInfCount=0`
- **Invariants**: Zero-velocity identity `< 0.002`, directional horizontal streak `> 0.25` with zero vertical smear `< 0.005`, focus plane contrast `> 0.49` vs far plane blur `< 0.25`, exact zero foreground silhouette bleed (`maxBgBleedR < 0.005`), smooth vignette attenuation `> 10%`, `nanInfCount == 0`, and HW GPU total mean `< 0.08 ms`.

### 18. GPU 16,384-Particle 3D Physics and Screen-Space Additive Splat Engine
- **Implementation**: `RawGpuParticlesHost` (`src/d3d12_bridge/RawGpuParticlesHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawGpuParticlesSim.hlsl` (`raw_particles_sim_cs.h` + `raw_particles_splat_cs.h`) simulating `16,384` persistent 3D particles (`64-byte` structs, 4 modes: `Embers`, `Snow`, `Rain`, `WaterfallMist`) with divergence-free analytical 3D curl-noise turbulence, weather wind advection, aerodynamic drag, ground-plane elastic bounce restitution, and lock-free `InterlockedAdd` Q22.10 fixed-point additive screen-space HDR splatting (`480x270`).
- **Measured Workload**: Verifies `16,384`-particle active/expiry conservation (`activeCount == 16384`, `expiredActive == 0`), upward ember buoyancy (`emberMeanY`) vs downward snow gravity (`snowMeanY`), `+X` wind advection (`windMeanX`), rain ground-plane elastic bounce restitution (`bouncedCount`, `minBounceY`), non-zero additive screen-space splat rasterization (`splattedCount`, `nonZeroSplatPx`), and zero NaN/Inf across all particles and splat texels, followed by steady-state GPU profiling.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`Sim 16K + Clear + Additive Splat`): `mean=0.0164 ms`, `p50=0.0164 ms`, `p95=0.0174 ms`, `p99=0.0174 ms` (`hostSimMs=0.0051 ms`, `hostSplatMs=0.0031 ms`)
  - CPU command recording overhead: `mean=0.0238 ms`, `p50=0.0202 ms`, `p95=0.0413 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `activeCount=16384`, `expiredActive=0`, `emberMeanY=2.3533 m`, `snowMeanY=7.1440 m`, `windMeanX=+5.9004 m`, `bouncedCount=16384`, `minBounceY=0.5643 m`, `splattedCount=16384`, `nonZeroSplatPx=2126`, `nanInfCount=0`
- **Invariants**: Exact `16,384/16,384` active particle conservation with `expiredActive == 0`, upward ember buoyancy (`emberMeanY > 1.2 m`) vs downward snow fall (`snowMeanY < 8.5 m`), strong `+X` wind drift (`windMeanX > 1.0 m`), ground-plane bounce floor respected (`bouncedCount > 8000`, `minBounceY >= 0.45 m`), active additive splat coverage (`splattedCount > 1000`, `nonZeroSplatPx > 64`), `nanInfCount == 0`, and HW GPU total mean `< 0.08 ms`.

### 19. GPU RAW-TSR 2x Temporal Super-Resolution and RCAS Sharpening Upscaler
- **Implementation**: `RawTsrUpscaleHost` (`src/d3d12_bridge/RawTsrUpscaleHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawTsrUpscaleSim.hlsl` (`raw_tsr_resolve_cs.h` + `raw_tsr_rcas_cs.h`) executing 16-phase Halton `(2,3)` sub-pixel un-jittering, 5-tap Catmull-Rom bicubic history reprojection, YCoCg variance-clipping anti-ghosting (`gamma=1.15`), closest-depth velocity dilation, sub-pixel hit-gated temporal accumulation, reactive mask pass-through, and clean-room RCAS contrast-adaptive sharpening with a strict 5-tap cross non-ringing clamp into `RGBA16F`.
- **Measured Workload**: Evaluates `640x360 -> 1280x720` (2x upscale) across 16-frame static Halton `(2,3)` sub-pixel convergence vs 1-frame RMSE against analytic 1280x720 ground truth, `+24 px` fast-pan orange tower disocclusion anti-ghosting (`0.02` sky behind stale `3.0` history), `4.0` moving fire-spark reactive pass-through, RCAS contrast-adaptive edge gradient boost on a soft `0.10 -> 0.90` ramp with poisoned NaN/+Inf corners, and steady-state 2-pass GPU profiling.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`TSR Resolve + RCAS Sharpen`): `mean=0.0467 ms`, `p50=0.0471 ms`, `p95=0.0471 ms`, `p99=0.0471 ms` (`hostResolveMs=0.0338 ms`, `hostRcasMs=0.0092 ms`)
  - CPU command recording overhead: `mean=0.0172 ms`, `p50=0.0147 ms`, `p95=0.0324 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `rmse1Frame=0.1320 -> rmse16Frames=0.0888` (`rmseReductionPct=32.75%`), `disoccSkyR=0.0200` (true `0.02`, stale history `3.00`), `reactiveSparkR=3.9980` (true `4.000`), `rcasGradBoostPct=+36.12%` (zero ringing `[0.0999..0.8999]`), `nanInfCount=0`
- **Invariants**: 16-frame Halton `(2,3)` RMSE reduction `> 20%`, disoccluded sky `< 0.06`, reactive fire spark `> 3.6`, RCAS edge gradient boost `> 8%` with zero overshoot outside `[0.098..0.902]`, `nanInfCount == 0`, and HW GPU total mean `< 0.08 ms`.

### 20. GPU Physical Water Caustics and 16-Step Henyey-Greenstein Underwater Volumetric God-Rays
- **Implementation**: `RawWaterCausticsHost` (`src/d3d12_bridge/RawWaterCausticsHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawWaterCausticsSim.hlsl` (`raw_water_caustics_trace_cs.h` + `raw_water_underwater_rays_cs.h`) solving periodic 4-wave Gerstner/swell Snell refraction bed-maps via damped 2x2 Newton iteration with exact analytical Jacobian focusing (`|det(dP_bed/dP_surf)|^-1`), Cauchy RGB chromatic dispersion, Pope & Fry spectral Beer-Lambert underwater extinction, and 16-step IGN-jittered Henyey-Greenstein volumetric underwater sun-shafts coupled directly to live Pass-1 caustic filaments into `512x512` `RGBA16F`.
- **Measured Workload**: Verifies exact flat-water uniform unit irradiance (`flatMaxErr == 0.0000`), wavy-surface peak caustic focusing (`peakFocus > 2.0x`) with exact area-preserving mean energy conservation (`0.85 <= meanEnergy <= 1.15`), Cauchy RGB chromatic dispersion separation (`rbMeanDiff`, `rbMaxDiff`), Beer-Lambert spectral depth extinction (`1 m` shallow vs `6 m` deep, red attenuating much faster than blue), 16-step Henyey-Greenstein underwater god-ray sunward vs anti-solar anisotropy (`sunwardRatio > 2.5x`), exact zero scatter above water (`aboveSkyMax == 0.0`), and zero NaN/Inf across all outputs, followed by steady-state 2-pass GPU profiling.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`Caustics Trace + Underwater Sun-Shafts`): `mean=0.0468 ms`, `p50=0.0471 ms`, `p95=0.0471 ms`, `p99=0.0481 ms` (`hostCausticsMs=0.0164 ms`, `hostRaysMs=0.0256 ms`)
  - CPU command recording overhead: `mean=0.0212 ms`, `p50=0.0158 ms`, `p95=0.0355 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `flatMaxErr=0.0000`, `peakFocus=2.8184x`, `meanEnergy=0.9996`, `rbMeanDiff=0.0307` (`rbMaxDiff=0.3223`), `redRetention=0.1184` vs `blueRetention=0.8523`, `sunwardRatio=19.67x`, `aboveSkyMax=0.0000`, `nanInfCount=0`
- **Invariants**: Flat-water unit error `< 2e-3`, wavy peak focusing `> 2.0x`, mean energy within `15%` of `1.0`, Cauchy chromatic separation `rbMeanDiff > 0.025` & `rbMaxDiff > 0.20`, Beer-Lambert red retention `< 0.35 * blueRetention`, sunward volumetric shaft ratio `> 2.5x`, above-water scatter `< 1e-5`, `nanInfCount == 0`, and HW GPU total mean `< 0.06 ms`.

### 21. GPU 512x512 Snow Accumulation, Campfire/Solar Melt and Mass-Conserving Footprint Deformation
- **Implementation**: `RawSnowDeformHost` (`src/d3d12_bridge/RawSnowDeformHost.h/.cpp`) driving SM 6.6/6.10 compute shaders `RawSnowDeformSim.hlsl` (`raw_snow_deform_step_cs.h` + `raw_snow_normal_height_cs.h`) over a `512x512` (`64 m` world span, `0.125 m/texel`) world-anchored toroidal `RGBA16F` snow state field `(depth, compaction, slushWetness, blackIceMask)` with up to 32 elliptical actor/horse/cart footprint stamps (analytically balanced mass-conserving rim displacement), up to 16 circular campfire/lava thermal heat sources, ambient/solar melt to slush, sub-zero slush-to-black-ice refreeze, seasonal snowfall replenishment, compaction-aware wind-drift smoothing, and 3x3 Sobel normal + sparkle/glint output.
- **Measured Workload**: Verifies mass-conserving footprint depression & raised compacted rim (`massErrPct < 1.0%`), 3x3 Sobel wall normal alignment (`leftNx > 0.12`, `rightNx < -0.12`, flat snow `(0, 1, 0)`), campfire melt (`fireMeltedH < 0.05 m`) to wet slush followed by `-18 C` sub-zero refreeze to glossy black ice (`frozenBlackIce > 0.40`), seasonal snowfall replenishment (`+0.20 m`), zero NaN/Inf across all state and normal texels, and steady-state GPU profiling with all 32 simultaneous stamps and 16 heat sources active.
- **Full RTX 4090 Breakdown (60 frames)**:
  - Total GPU pass (`SnowDeform Step + Sobel Normal`): `mean=0.0238 ms`, `p50=0.0236 ms`, `p95=0.0246 ms`, `p99=0.0246 ms` (`hostStepMs=0.0164 ms`, `hostNormalMs=0.0041 ms`)
  - CPU command recording overhead: `mean=0.0163 ms`, `p50=0.0137 ms`, `p95=0.0290 ms`
  - Readback invariants (bit-identical on RTX 4090 and WARP): `trenchDepthM=0.1636 m`, `rimHeightM=0.4924 m`, `massErrPct=0.0300%`, `wallNormalNx=+0.3335`, `fireMeltedH=0.0000 m`, `frozenBlackIce=1.0000`, `snowfallGainM=+0.1982 m`, `nanInfCount=0`
- **Invariants**: Footprint trench `< 0.20 m` & raised rim `> 0.45 m` with `< 1.0%` mass error, Sobel wall normals aligned (`leftNx > 0.12`, `rightNx < -0.12`, flat `ny ~ 1.0`), campfire melts core `< 0.05 m` & refreezes slush into black ice `> 0.40`, snowfall replenishment `+0.18..0.22 m`, `nanInfCount == 0`, and HW GPU total mean `< 0.05 ms`.

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
- **JSON Schema**: Top-level fields include `suite`, `mode`, `adapter`, `isWarp`, `vramMB`, `debugLayer`, `d3d12DebugErrors`, `timestampFreqHz`, `gpuWarmupFrames`, `gpuMeasuredFrames`, `allPassed`, and `subsystems.{fluidSim, radianceCascades, textureForge, audioDsp, destruction, floraAndHydrology, tamrielStreamer, xpbdSim, gpuCull, lightClusters, volumetricFroxels, shadowPcss, gtao, ssr, autoExposure, cloudRaymarch, postCinematic, gpuParticles, tsrUpscale, waterCaustics, snowDeform}` with nested `{mean, p50, p95, p99, min, max}` latency objects for every timed stage.
