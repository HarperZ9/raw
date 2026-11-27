# RAW DX12 Wave 83: Lock-Free Chase-Lev Work-Stealing Job Graph + Async SHA-256 PSO Pre-Warm Cache + Two-Pass Hi-Z GPU Occlusion & Continuous Meshlet LOD Distance Budgeter (2026-11-26)

## Summary

Wave 83 delivers the second major scalability milestone of the Creation Engine 2027 Architecture Roadmap (`CE-2027-ARCHITECTURE-ROADMAP-2026-11-26.md`, `catalogue_id: 68390`) in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`):

1. **Lock-Free Chase-Lev Work-Stealing Job Graph (`src/engine_rt/raw_job_system.h`, `raw_job_system.cpp`):**
   - Exact 64-byte single-cache-line `RawJobSlot` (`static_assert(sizeof == 64)`), eliminating `std::function` heap allocations on the hot job dispatch path (`SubmitInline`).
   - Lock-free `ChaseLevDeque<4096>` (separate 64B cache lines for `top` & `bottom`, LIFO local pop + FIFO cross-worker steal) and lock-free `LockFreeInlineRing<4096>` (Vyukov MPMC protocol) drained before legacy lanes with zero mutex contention on the hot path.
   - Deterministic multi-input fan-in DAG barrier (`SubmitAfterAll`) using strict `acq_rel` atomic countdowns.

2. **Async SHA-256 Keyed PSO Pre-Warm & Binary Disk Cache (`src/d3d12_bridge/RawPsoCache.h`, `RawPsoCache.cpp`):**
   - Clean-room FIPS 180-4 SHA-256 (`ComputeSha256`) keying every PSO by shader bytecode + pipeline state + driver tag (`RawPsoBlobHeader`, `80 B` exact).
   - Atomic temp-write/rename disk persistence + multi-worker background `PrewarmBatchAsync` eliminating shader-compilation stutter (`0` miss on warm cache hit) with fail-closed eviction and clean rebuild on any 1-byte tampered, truncated, or driver-mismatched PSO blob.

3. **Two-Pass Hi-Z GPU Occlusion & Continuous Meshlet LOD Distance Budgeter (`RawGpuCull.hlsl`, `RawTwoPassHiZ.h`, `DistantLod.hlsli`):**
   - Adds `GpuMeshletDesc` (`48 B`, exact C++ <-> HLSL layout parity), `TestMeshletBackfaceCone`, `EvalMeshletLodCut`, `cs_cull_pass1_prev_hiz`, and `cs_cull_pass2_disoccluded` (retesting Pass-1 Hi-Z-culled candidates against the current-frame conservative `max()` Hi-Z pyramid so newly disoccluded geometry renders on the exact same frame with zero 1-frame pop-in!).
   - Adds `FrameBudgetLodGovernor` coupling `p99` / `1%`-low frame time smoothly into `targetPixelError` (`[0.85..3.50 px]`, deadband hysteresis) + `rawEvalContinuousLodFade` in `DistantLod.hlsli`.

## Verification Receipts (2026-11-26)
- **DXC 1.10 (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `15/15 PASS` across all 4 `RawGpuCull.hlsl` entries (`cs_hiz_reduce`, `cs_cull_instances`, `cs_cull_pass1_prev_hiz`, `cs_cull_pass2_disoccluded`) + `Lighting.hlsl` (`ps_6_6/6_9/6_10`), `0` warnings.
- **Ninja `-j 6` (`vcvars64.bat`, Agility SDK `1.722.1-preview`):** `[6/6]` `raw_engine.dll` (`SHA256: 3B072CC9758B03D23D8830CE3DED2556BF895F1D27B3AF3FA76793033E2D47F0`), `raw_bench_suite.exe`, and `d3d11.dll` (`SHA256: B31D9D861EAECD2CD0DE8BF9616AF96873DE7DF1ECEA0DC32C40932C9D146CD4`), `0` warnings.
- **Cumulative C++23 `/DNOMINMAX /std:c++latest /W4 /WX` Suites:**
  - `wave83_jobs_pso_hiz_lod_check.exe`: `6/6 PASS` (`ChaseLevDeque` 2,048-slot multi-thief linearizability `0` lost/`0` dup, `SubmitInline` 2,048 zero-alloc tasks + 64-way `SubmitAfterAll` fan-in DAG, `RawPsoCache` 12/12 cold disk hits + Planted 83-A corrupt PSO fail-closed rejection, Two-Pass Hi-Z disocclusion recovery + Planted 83-B/83-C, continuous LOD watertight cut + 1%-low governor + Planted 83-D, 3-game `100 Hz` replay).
  - `wave82`: `6/6`, `wave81`: `8/8`, `wave80`: `8/8`, `wave79`: `8/8`, `wave78`: `8/8`, `wave77`: `8/8`, `wave76`: `8/8`, `wave75`: `9/9`, `wave74`: `10/10`, `wave73`: `12/12`, `wave72`: `14/14` (`failures=0`), `ce_runtime_check.exe`: `522 passed, 0 failed`.
- **Cumulative Python Suites:** `14/14 PASS`.
- **Wave 38B `scan_raw_engine_atlas.py` Re-Audit:** `static_asserts: 129`, `unguarded_offsets: 69`, `hlsl_resources: 295`, `unbound_resources: 0`.
- **RTX 4090 GPU Hardware Suites (`D3D12 DebugLayer=ON`, `0` errors):** `test_tsr_upscale_gpu.exe` `18/18 PASS`; `raw_bench_suite.exe` `21/21 PASS` (`d3d12DebugErrors=0`, `NaN=0`: Hi-Z+16k Cull `0.0287 ms`, Fluid `0.256 ms`, RC-GI `0.120 ms`, XPBD `0.0580 ms`, Froxels `0.0448 ms`, RAW-TSR+RCAS `0.0506 ms`, Water Caustics+Rays `0.0463 ms`, Snow/Footprint `0.0237 ms`).
- **Live 4-Scene Moving-Camera Skyrim SE + Wave 82/83 30-Metric Playtest Runner (`E:/wt/raw-rt9-build/live_w83_quick_v2`, `106/106 PASS`):**
  - Base live audit: `96/96 PASS` (`4/4` clean, `0` defects: `riverwood_day shadow=0.053171 / mv=0.048946 / dark_mo=0.001888`; `inn_interior mv=0.019929 / dark_mo=0.000024`; `septimus_cave ghost=0.000000`; `windhelm_ext_snow shadow=0.016533 / ghost=0.000000 / dark_mo=0.000002`).
  - Extended 30-metric collector (`wave83_playtest_v2_report.json`): **`10/10 PASS`** (`all_passed=True`, `coverage_ok=True` across `culling`, `lod`, `job_system`, `streaming`, `min_ssim=0.93571`, `foliage_shimmer_fft=0.02757` across `715,194` canopy px, `fireflies=2`, `black_crush=1.669%`, `highlight_clip=0.0%`, `out_of_gamut=0.0%`, `15` ownership passes with `0` double_exec / `0` flicker, `rtrace_sha256: 830a93539a2a9fbc10b9284d22f12bfc64c89d98e1a2c93dea0fa77feb8b0c73`).
  - Host cleanup: `Sentinel Cleaned: True`, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released (`False`).
