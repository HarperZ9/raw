# RAW DX12 Wave 82: 9 Live GPU Debug Views + 30-Metric Extended Heuristics + Change-Targeted Playtest Runner + 15-Pass Runtime Ownership Ledger + ABI Offset Guards (2026-11-26)

## Summary

Wave 82 delivers the first milestone of the Creation Engine 2027 Architecture Roadmap (`CE-2027-ARCHITECTURE-ROADMAP-2026-11-26.md`, `catalogue_id: 68390`) and resolves the highest-priority Wave 38B Introspection Atlas findings (`RAW-COMPLETE-STRUCT-BUFFER-PARAM-ATLAS-2026-11-26.md`, `catalogue_id: 68391`) cleanly in `E:/wt/raw-rt9`:

1. **9 Live GPU Debug Render Views (`RawDebugViews.hlsli`, `RawDebugViews.h`, `Lighting.hlsl`, `rt_control.cpp`):**
   - Canonical Modes `0..8` mapped cleanly onto `Lighting.hlsl` `flags.w == 0, 41..48` (preserving all legacy Views `1..36`):
     - `0` Final Lit, `1` Albedo + Porosity Wetness, `2` World Normals + Micro-Normal Film (magenta alarm on degenerate/NaN normals), `3` Linear Depth + Hi-Z Contour Bands (red alarm on NaN/Inf depth), `4` Motion Vectors + Disocclusion Heat, `5` ReSTIR-GI / Hash Radiance + Froxel GI, `6` Shadow Cascades `0..3` + Cloud Transmittance, `7` Wireframe / Overdraw / Meshlet LOD Tiers, `8` Navmesh + Actor Spatial Grid + Per-Pass Profiler Band.
   - Click-any-pixel buffer/mesh/pass inspector (`InspectPixelBuffer`) + live `debug_view <0..8>` control command in `rt_control.cpp`.

2. **30-Metric Extended Heuristics Collector & Change-Targeted Full-World Playtest Runner (`tools/live/raw_heuristics_v2.py`, `tools/live/raw_playtest_runner.py`):**
   - Preserves all 20+ existing `visual_heuristics.py` gates with zero weakened thresholds and adds metrics `#21..#30` (frame-time `p50/p95/p99/1%-low`/hitch ms, per-pass GPU breakdown, VRAM residency delta `<= 32 MB`, draw-call/Hi-Z cull ratio, overdraw & LOD pop delta, temporal L1/SSIM, canopy shimmer FFT `<= 0.12`, firefly spike count `<= 4`, photometric integrity bundle, and runtime ownership ledger check).
   - `ChangeTargetedCoverageMap` enforces fail-closed coverage across all 12 core subsystems (`culling`, `lod`, `navmesh`, `actors`, `water`, `iridescence`, `frost`, `fire`, `audio`, `streaming`, `mod_sdk`, `job_system`) and emits deterministic `.rtrace` SHA-256 replay receipts.

3. **15-Pass Runtime Engine Ownership Ledger (`data/ownership/engine_ownership_manifest.json`, `src/engine_rt/rt_ownership_ledger.h`, `rt_control.cpp`):**
   - Exact 64-byte cache-aligned `PassOwnershipRecord` (`static_assert(sizeof == 64)`), atomic Present-boundary 3-frame finite promotion (`HYBRID_INTERCEPTED -> OWNED_BY_RAW`), fail-closed double-execution & flicker alarms, and live `ownership_dump` JSON command.

4. **Compile-Time C++ <-> HLSL Layout Proofs & Raw-Offset ABI Guards (`#1023`):**
   - Compile-time `sizeof` + `offsetof` `static_assert` proofs across all 11 paired GPU/ownership structs (`PassConstants`, `GpuCullInstance`, `ClusterAabb`, `GpuClusterLight`, `MeshletDesc`, `TerrainVertex`, `FroxelPointLight`, `XpbdParticle`, `XpbdCollider`, `RawDebugViewCB`, `PassOwnershipRecord`).
   - Verified `SkyrimSE 1.7.104.0` ABI `static_assert` offset guard blocks added to the top 4 raw-offset hotspot files (`rt_terrain_lod.cpp`, `rt_softbody.h`, `rt_weather.cpp`, `rt_emitters.cpp`).

## Verification Receipts (2026-11-26)
- **DXC 1.10 (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `79/79 PASS` (`base=70`, `0` warnings) + `Lighting.hlsl` `ps_6_6/6_9/6_10` `-WX` PASS.
- **Ninja `-j 6` (`vcvars64.bat`, Agility SDK `1.722.1-preview`):** `[6/6]` `raw_engine.dll` (`SHA256: 9DD6E01BD9D2FE36C26D0025A0FDC0F9C7C3F6148098843208795D9780EAF1F6`) + `d3d11.dll` (`SHA256: CA6262FAB2D41494EBFE025DDA409A7620358C84FF48FD24B58F120F03A51F17`), `0` warnings.
- **Cumulative C++23 `/DNOMINMAX /std:c++latest /W4 /WX` Suites:**
  - `wave82_playtest_debug_ownership_check.exe`: `6/6 PASS` (all 11 paired GPU/ownership struct `sizeof`+`offsetof` `static_assert`s, 9 canonical debug views + pixel inspector, 15-pass atomic 3-frame promotion ledger, planted 82-C & 82-D fail-closed controls, 3-game `100 Hz` replay).
  - `wave81`: `8/8`, `wave80`: `8/8`, `wave79`: `8/8`, `wave78`: `8/8`, `wave77`: `8/8`, `wave76`: `8/8`, `wave75`: `9/9`, `wave74`: `10/10`, `wave73`: `12/12`, `wave72`: `14/14` (`failures=0`), `ce_runtime_check.exe`: `522 passed, 0 failed`.
- **Cumulative Python Suites:** `13/13 PASS` (incl. `test_wave82_playtest_debug_heuristics_ownership.py` `3/3` with Planted Controls 82-A/82-B/82-C/82-D + uncovered-subsystem fail-closed check).
- **Wave 38B `scan_raw_engine_atlas.py` Re-Audit:** `unguarded_offsets` cut from `150 -> 69` (`-81` raw offsets guarded, `-54%`), `static_asserts` raised `110 -> 124`, `unbound_resources = 0`.
- **RTX 4090 GPU Hardware Suites (`D3D12 DebugLayer=ON`, `0` errors):** `test_tsr_upscale_gpu.exe` `18/18 PASS`; `raw_bench_suite.exe` `21/21 PASS` (`d3d12DebugErrors=0`, `NaN=0`).
- **Live 4-Scene Moving-Camera Skyrim SE + Wave 82 30-Metric Extended Collector (`E:/wt/raw-rt9-build/live_w82_quick_v1`):**
  - Base live audit: `96/96 PASS` (`4/4` clean, `0` defects: `riverwood_day mv=0.047500` & `shadow=0.050980` & `dark_mo=0.001645`; `inn_interior dark_mo=0.000021`; `septimus_cave ghost=0.000000`; `windhelm_ext_snow shadow=0.026814`).
  - Extended Wave 82 30-metric collector (`raw_playtest_runner.py`): **`96 + 10/10 PASS` (`106/106`, `all_passed=True`, `coverage_ok=True`)** on live 1080p frames (`min_ssim=0.93639`, `foliage_shimmer_fft hf_ratio=0.02900` over `723,443` canopy px, `fireflies=0`, `black_crush_pct=1.934%`, `highlight_clip_pct=0.0%`, `out_of_gamut_pct=0.0%`, `15` ownership passes with `0` double_exec / `0` flicker, `rtrace_sha256: b0c22c146932f0abdc452477f684a2c13339e9d16880701ca75e92bb52fd5e88`).
  - Host cleanup: `Sentinel Cleaned: True`, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released (`False`).
