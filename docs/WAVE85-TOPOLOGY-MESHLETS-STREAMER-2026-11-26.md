# RAW DX12 Wave 85: NIF Topology Repair + Watertight 64v/64t Meshlet Clusterizer & Monotonic QEM LOD DAG + Priority Async VRAM Residency Streamer (2026-11-26)

## Summary

Wave 85 delivers the fourth major Creation Engine 2027 milestone (`CE-2027-ARCHITECTURE-ROADMAP-2026-11-26.md`, `catalogue_id: 68390`) in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`):

1. **NIF Geometry Topology Repair & Plain-English Corrupt-NIF Diagnostics (`src/ce_runtime/CeTopologyRepair.{h,cpp}`):**
   - Quantized spatial-hash seam welding (`posWeldEps = 1e-4`, `uvWeldEps = 1e-4`, `normalDotMin = 0.996`) sealing floating vertex cracks while preserving UV seams and hard normal creases.
   - Out-of-range index, NaN/Inf vertex, duplicate-index, and zero-area degenerate triangle stripping (`minTriangleArea = 1e-7`).
   - Exact MikkTSpace UV-gradient + two-pass Gram-Schmidt tangent-frame orthonormalization (`|N| = 1`, `|T| = 1`, `|dot(N, T)| <= 1e-5`, handedness `w in {-1, +1}`).
   - Deterministic plain-English `TopologyDiagnostic` reporting exact corrupt mesh name, vertex index, and index span instead of silent CTD.

2. **Watertight `64v/64t` Meshlet Clusterizer & Monotonic QEM LOD DAG (`src/ce_runtime/CeMeshletBuilder.{h,cpp}`):**
   - Greedy adjacency-first spatial clusterizer enforcing strict `<= 64` vertices and `<= 64` triangles per meshlet.
   - Emits exact 48-byte `raw::bridge::wave83::GpuMeshletDesc` descriptors (minimum bounding sphere + snorm8x4 packed normal bounding cone) ready for Wave 83 Two-Pass Hi-Z GPU occlusion & backface cone culling.
   - Builds a Garland-Heckbert Quadric Error Metric (QEM) cluster LOD DAG with locked boundary vertices and strictly enforced `parentErrorWorld >= lodErrorWorld` monotonicity so continuous screen-space LOD cuts never crack or pop.

3. **Priority Async VRAM Residency & Horizon Streaming Budgeter (`src/d3d12_bridge/RawAssetStream.{h,cpp}`):**
   - Distance, screen-coverage, and camera-velocity lookahead priority queue with epoch-based stale request cancellation.
   - Hard `80%` VRAM LRU residency cap (`VramResidencyBudgeter`, `O(1)` touch & tail eviction, fail-closed breach prevention) + fixed `256`-slot async upload ring + zero-leak residency telemetry feeding `raw_heuristics_v2.py` Metric `#23`.

## Verification Receipts

- **Build (`Ninja -j 6`, VS 18 `vcvars64.bat`):** `[3/3]` clean build (`0` warnings):
  - `d3d11.dll`: `SHA256 = 265E36383797ECE69BE4277EE33BF79640A3AD81237F5BCDE72F57BE68EBDE5A`
  - `raw_engine.dll`: `SHA256 = 60C90B688A1570C4D8C9A3C7D7FDA16E0CD952C4C4124D71A186D08B28F02959`
  - `ce_runtime_check.exe`: **`522 passed, 0 failed`**
- **Wave 85 C++23 Standalone Suite (`/DNOMINMAX /std:c++latest /W4 /WX`, `tests/wave85_topology_meshlet_stream_check.cpp`):** **`7/7 PASS`** (`0` warnings):
  - `T1`: Spatial-hash seam welding (`2560 -> 693` verts, `1,803` welded, `24` degenerates stripped, hard-edge crease preserved at `6` verts, max Gram-Schmidt tangent orthogonality error `|dot(N,T)| = 6.17e-08 <= 1e-5`).
  - `T2` (**Planted Control 85-A**): Corrupt NaN vertex + out-of-range index NIF fails closed (`ok=false`) with exact plain-English diagnostics naming `corrupt_pauldron.nif` and exact vertex/index spans.
  - `T3`: Watertight `64v/64t` meshlet clusterizer & monotonic 4-level QEM LOD DAG (`66` meshlets, `lod0=31`, `maxV=64`, `maxT=64`, `avgV/T=0.77`, `47` tight normal cones, crack-free Wave 83 continuous screen-space LOD cut `nearSel=31 -> farSel=16`).
  - `T4` (**Planted Control 85-B**): Non-monotonic QEM LOD DAG (`lodErrorWorld > parentErrorWorld`) rejected fail-closed.
  - `T5` (**Planted Control 85-C**): Over-limit `68v` meshlet and NaN cone cutoff rejected fail-closed.
  - `T6` (**Planted Control 85-D**): Camera-velocity priority ordering, stale epoch cancellation, `256`-slot async upload ring, strict `80%` VRAM hard cap (`peak = 408.0 / 409.6 MB`, `23` O(1) LRU evictions across 9-cell traversal), LRU-disabled budget breach blocked fail-closed, and `0` VRAM leak after release.
  - `T7`: Multi-title `100 Hz` deterministic replay across `SkyrimSE`, `Fallout4`, and `Starfield` bit-identical across runs.
- **Cumulative C++23, Python & Atlas Gates:**
  - Cumulative C++23 `/W4 /WX`: **`14/14` wave binaries PASS** (Waves 72..85) + `ce_runtime_check.exe` **`522/522`**.
  - Cumulative Python Suites: **`15/15` PASS** (including `tests/test_wave85_topology_meshlet_stream.py`).
  - Wave 38B Atlas Re-Audit (`scan_raw_engine_atlas.py --src E:/wt/raw-rt9/src`): `cpp_files: 669`, `hlsl_files: 90`, `cpp_structs: 1597`, `static_asserts: 130`, `unguarded_offsets: 69`, `hlsl_resources: 295`, `unbound_resources: 0`.
  - Hygiene scan: `badEm = 0`, `badForb = 0`.
- **RTX 4090 GPU Hardware Suites (`D3D12 DebugLayer=ON`, `0` errors):**
  - `test_tsr_upscale_gpu.exe`: **`18/18 PASS`** (`0.0526 ms`, `0` D3D12 Debug Layer errors).
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors = 0`, `Hi-Z+16k Cull 0.0287 ms`, `RC-GI 0.107 ms`, `Fluid 0.251 ms`, `XPBD 0.0579 ms`, `48kHz FDN-8+HRTF 139.9x RT`, `Froxel 0.0447 ms`, `RAW-TSR+RCAS 0.0505 ms`, `Caustics 0.0473 ms`, `Snow 0.0238 ms`, `NaN = 0`).
- **Live 4-Scene Moving-Camera Skyrim SE + 30-Metric Playtest Runner (`E:/wt/raw-rt9-build/live_w85_quick_v1`, `106/106 PASS`):**
  - Base live audit: **`96/96 PASS`** (`4/4` clean, `0` defects, `16` camera steps per scene):
    - `riverwood_day`: `24/24 PASS` (`ghost = 0.066107`, `dark_mo = 0.001946`, `mv = 0.049174`, `nrm = 0.068496`, `water_neon_cyan = 0.000000`, `crushed_bark = 0.000701`).
    - `inn_interior`: `24/24 PASS` (`ghost = 0.027525`, `dark_mo = 0.000026`, `mv = 0.023353`, `nrm = 0.004628`).
    - `septimus_cave`: `24/24 PASS` (`ghost = 0.000000`, `dark_mo = 0.000000`, `mv = 0.003614`, `nrm = 0.000462`).
    - `windhelm_ext_snow`: `24/24 PASS` (`ghost = 0.000000`, `dark_mo = 0.000176`, `mv = 0.022240`, `nrm = 0.012742`).
  - Extended 30-metric collector (`wave85_playtest_v2_report.json`): **`10/10 PASS`** (`all_passed = True`, `coverage_ok = True` across `culling`, `lod`, `streaming`, `min_ssim = 0.93606`, `foliage_shimmer_fft = 0.02807` over `717,423` canopy px, `fireflies = 3`, `black_crush_pct = 1.673%`, `highlight_clip = 0.0%`, `out_of_gamut = 0.0%`, `15` ownership passes with `0` double-exec / `0` flicker, `rtrace_sha256 = c69ea37dc1d90e5b8ee821b1b384ce1b64e8ddf665a31289c648a671498d194f`).
  - Visually inspected all 4 fresh 1080p PNGs (`riverwood_day`, `inn_interior`, `septimus_cave`, `windhelm_ext_snow`).
  - Host cleanup verified: `SentinelCleaned=True`, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released (`LOCK_EXISTS=False`).
