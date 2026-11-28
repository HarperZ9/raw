# RAW DX12 Wave 84: 1,024-Actor SoA Crowd ECS + 4-Tier Adaptive Scheduler + Hierarchical HPA* Navmesh & Dynamic Carving + ORCA Crowd Avoidance + Off-Thread Utility/HTN AI + Geometry-Coupled Estuary-Space Spatial FDN Audio (2026-11-26)

## Summary

Wave 84 delivers the third major Creation Engine 2027 milestone (`CE-2027-ARCHITECTURE-ROADMAP-2026-11-26.md`, `catalogue_id: 68390`) in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`):

1. **1,024-Actor Cache-Aligned SoA Crowd ECS & 4-Tier Adaptive Scheduler (`src/engine_rt/RawActorCrowdEcs.{h,cpp}`):**
   - Contiguous `alignas(64)` Structure-of-Arrays (`kMaxCrowdActors = 1024`) dispatched across Wave 83's lock-free `RawJobSystem`.
   - Deterministic 4-tier distance/visibility budgeter (`Tier 0` `100 Hz` cap 32, `Tier 1` `50 Hz` cap 128, `Tier 2` `25 Hz` cap 512, `Tier 3` `5 Hz` horizon/occluded) stepping `1,024` live actors strictly within `<= 2.2 ms` CPU with fail-closed NaN/radius sanitization.

2. **Hierarchical HPA* Multi-Cell Navmesh + Dynamic Carving + RVO2/ORCA + Off-Thread Utility/HTN AI (`src/ce_runtime/CeNavMesh.{h,cpp}`, `src/engine_rt/RawCrowdNavAi.{h,cpp}`):**
   - `HpaClusterNavGraph`: chunked `64 m` clusters with border portal edges, `O(1)` spatial hash lookup, and runtime dynamic obstacle tile carving (barricades, fallen trees, braziers, spell walls).
   - `SolveOrcaCrowdStep`: deterministic RVO2/ORCA reciprocal half-plane local steering + hard separation preventing capsule interpenetration (`0` overlaps under 64-actor opposing cross-flow stress).
   - `EvaluateUtilityHtnBatch`: pure off-thread Utility -> HTN tactical planner dispatched across Wave 83 worker threads (bit-identical single-thread vs multi-worker decisions).

3. **Geometry-Coupled Estuary-Space 8x8 Orthonormal Hadamard FDN Audio (`src/engine_rt/rt_adaptive_audio_director.{h,cpp}`):**
   - Exact `1/sqrt(8)` Sylvester Hadamard orthogonal feedback matrix (`||H*x||_2 == ||x||_2`), geometry-coupled RT60 (`cavern > inn > snow/open_sky`), early-reflection occlusion, muffled underwater LPF (`<= 650 Hz`), and fail-closed unstable-gain rejection (`0` NaN).

## Verification Receipts

- **Build (`Ninja -j 6`, VS 18 `vcvars64.bat`):** `[30/30]` clean build (`0` warnings):
  - `raw_engine.dll`: `SHA256 = 60C90B688A1570C4D8C9A3C7D7FDA16E0CD952C4C4124D71A186D08B28F02959`
  - `d3d11.dll`: `SHA256 = B31D9D861EAECD2CD0DE8BF9616AF96873DE7DF1ECEA0DC32C40932C9D146CD4`
  - `ce_runtime_check.exe`: **`522 passed, 0 failed`**
- **Wave 84 C++23 Standalone Suite (`/DNOMINMAX /std:c++latest /W4 /WX`, `tests/wave84_crowd_nav_ai_audio_check.cpp`):** **`7/7 PASS`** (`0` warnings):
  - `1,024`-actor SoA crowd ECS avg tick = **`0.0086 ms`** (`255x` faster than `2.20 ms` CPU budget; active tiers `32 / 128 / 436 / 428`, `0` NaN).
  - 64-actor opposing cross-flow RVO2/ORCA avoidance = **`0` capsule overlaps** + Planted Fail-Closed Control `84-A` (disabled ORCA catches interpenetration).
  - Hierarchical HPA* `64 m` cluster multi-cell pathfinding + dynamic barricade carving reroute + Planted Fail-Closed Control `84-B` (carving disabled hits blocked tile).
  - 256-actor off-thread Utility -> HTN tactical AI: bit-identical 1-worker vs 4-worker decisions across Wave 83 `RawJobSystem`.
  - `EstuarySpaceFdn`: exact `8x8` orthonormal Sylvester Hadamard energy preservation (`||H*x||_2 == ||x||_2`), geometry-coupled RT60 (`cavern > inn > open_sky/snow`), muffled underwater LPF (`<= 650 Hz`), and Planted Fail-Closed Control `84-C` (unstable feedback gain rejected).
  - Planted Fail-Closed Control `84-D`: NaN velocity/position & degenerate radius sanitization.
  - Multi-game `100 Hz` deterministic replay across `SkyrimSE`, `Fallout4`, and `Starfield`.
- **Cumulative C++23 & Python Regression Gates:**
  - C++23 `/W4 /WX`: Wave 83 (`6/6`), Wave 82 (`6/6`), Wave 81 (`8/8`), Wave 80 (`8/8`), Wave 79 (`8/8`), Wave 78 (`8/8`), Wave 77 (`8/8`), Wave 76 (`8/8`), Wave 75 (`9/9`), Wave 74 (`10/10`), Wave 73 (`12/12`), Wave 72 (`0` failures), `ce_runtime_check.exe` (`522/522`).
  - Python Suites: **`15/15 PASS`** (including `tests/test_wave84_crowd_nav_ai_audio.py`).
  - Wave 38B Atlas Re-Audit (`scan_raw_engine_atlas.py`): `cpp_files: 665`, `hlsl_files: 90`, `cpp_structs: 1578`, `static_asserts: 129`, `unguarded_offsets: 69`, `hlsl_resources: 295`, `unbound_resources: 0`.
  - Hygiene scan: `badEm = 0`, `badForb = 0`.
- **RTX 4090 GPU Hardware Suites (`D3D12 DebugLayer=ON`, `0` errors):**
  - `test_tsr_upscale_gpu.exe`: **`18/18 PASS`**
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors = 0`, `48kHz FDN-8 + HRTF Audio 141.5x RT`, `RC-GI 0.110 ms`, `Fluid 0.257 ms`, `XPBD 0.0580 ms`, `Hi-Z+16k Cull 0.0297 ms`, `Froxels 0.0449 ms`, `RAW-TSR+RCAS 0.0506 ms`, `Caustics 0.0464 ms`, `Snow 0.0239 ms`, `NaN = 0`).
- **Live 4-Scene Moving-Camera Skyrim SE + 30-Metric Playtest Runner (`E:/wt/raw-rt9-build/live_w84_quick_v1`, `106/106 PASS`):**
  - Base live audit: **`96/96 PASS`** (`4/4` clean, `0` defects, `16` camera steps per scene):
    - `riverwood_day`: `24/24 PASS` (`ghost = 0.045345`, `dark_mo = 0.001343`, `mv = 0.045035` all three new all-time bests; `shadow = 0.050615`, `nrm = 0.065479`, `water_neon_cyan = 0.000000`, `crushed_bark = 0.001418`).
    - `inn_interior`: `24/24 PASS` (`shadow = 0.001197`, `ghost = 0.023832`, `dark_mo = 0.000033`, `mv = 0.020327`, `nrm = 0.004435`).
    - `septimus_cave`: `24/24 PASS` (`shadow = 0.001197`, `ghost = 0.000000`, `dark_mo = 0.000000`, `mv = 0.003743`, `nrm = 0.000484`).
    - `windhelm_ext_snow`: `24/24 PASS` (`ghost = 0.000000`, `dark_mo = 0.000000`, `shadow = 0.018565`, `mv = 0.019751`, `nrm = 0.011516`).
  - Extended 30-metric collector (`wave84_playtest_v2_report.json`): **`10/10 PASS`** (`all_passed = True`, `coverage_ok = True` across `actors`, `audio`, `navmesh`, `min_ssim = 0.93548`, `foliage_shimmer_fft = 0.03961` over `673,526` canopy px, `fireflies = 0`, `black_crush_pct = 0.907%`, `highlight_clip = 0.0%`, `out_of_gamut = 0.0%`, `15` ownership passes with `0` double-exec / `0` flicker, `rtrace_sha256 = 6c302d45dcd7dcbc1d76664b8758dd373320f931df2ca0c382ce51a68845116d`).
  - Visually inspected all 4 fresh 1080p PNGs (`riverwood_day`, `inn_interior`, `septimus_cave`, `windhelm_ext_snow`).
  - Host cleanup verified: `Sentinel Cleaned: True`, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released (`LOCK_EXISTS=False`).
