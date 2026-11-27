# RAW DX12 Wave 87: Zero-Unguarded-Offset Sweep (69 -> 0) + C++/HLSL Layout Parity Lock + Live Runtime RawParamRegistry + 17/17 Subsystem Coverage & 6-Scene Live Skyrim SE Playtesting (2026-11-26)

## Summary

Wave 87 executes the complete Total-Surface Parameter Universe hardening queue identified by Wave 40D Atlas v2 (`catalogue_id: 68404`) in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`):

1. **Zero-Unguarded-Offset Sweep (`69 -> 0` Production Unguarded Raw Hex Offsets):**
   - Replaced every raw inline hex pointer offset across all 19 production files (`raw_engine_profile.h`, `ce_rebuild_arch.cpp`, `rt_audit.cpp`, `rt_combat_impact.cpp`, `rt_depth_topology.cpp`, `rt_drawstats.cpp`, `rt_engine_remaster_tests.h`, `rt_grids.cpp`, `rt_livestate.cpp`, `rt_memory_guards.cpp`, `rt_movement_combat.cpp`, `rt_papyrus.cpp`, `rt_stealth_survival.cpp`, `rt_version.cpp`, `rt_vtime.cpp`, `skse_engine.cpp`) with named `constexpr` ABI layout descriptors and compile-time `static_assert` offset/alignment guards, and named the 3 hash/FP16 constants (`RawTextureForge.cpp`, `RawFloraEngine.cpp`, `RawNifLoader.cpp`).
   - Verified live via `scan_raw_engine_atlas.py --v2 --repo E:/wt/raw-rt9`: **`unguarded_offsets: 69 -> 0`**.

2. **C++/HLSL Layout Parity Lock & Un-Asserted GPU/ABI Structs:**
   - Added exact `static_assert(sizeof(...))` + `alignof(...)` guards across `CeFormatReader.h` (`BsaHeader`, `Ba2Header`, `Subrecord`, `PluginRecord`), `AtmosphereRenderer.cpp` (`AtmosphereCB`, `CelestialCB`), `ClusteredLighting.h` (`GPULightData`, `ClusterInfo`, `ClusterBuildCB`), `DebugRenderer.h` (`DebugVertex`), and `DynamicCubemapRenderer.cpp` (`CubemapCaptureCBData`, `MipGenCBData`), and aligned `Shaders/CelestialBodies.hlsl::CelestialCB` field declarations (`48 B == 48 B` exact parity).

3. **Live Runtime Parameter & Buffer Introspection Registry (`src/engine_rt/RawParamRegistry.{h,cpp}`):**
   - Lock-free atomic hot-path reads (`128 B`, `alignas(64)` `RawParamSlot`, `kMaxParams = 256`, zero mutexes / zero heap allocs on `Get*`).
   - Typed fail-closed `min/max/NaN/Inf/read-only/type` validation, deterministic sorted JSON snapshot export (`raw_param_snapshot.json`), and integration with Wave 86 `RawModSdkHost` & Wave 82 `RawDebugViews`.

4. **100% (`17/17`) Subsystem Playtest Coverage & Expanded 6-Scene Live Skyrim SE World-Cell Playtesting (`tools/live/raw_playtest_runner.py`, `tools/live/full_pipeline_audit.py`):**
   - Closed all 5 previously untested source subsystems (`assets`, `common`, `materials`, `pipeline`, `ui`) -> **`untested_subsystems: 5 -> 0`**.
   - Added `--profile extended6` executing all 4 baseline scenes (`riverwood_day`, `inn_interior`, `septimus_cave`, `windhelm_ext_snow`) PLUS 2 new real live Skyrim SE world-cell scenes (**`whiterun_ext`** `WhiterunExterior01` & **`riften_canal`** `RiftenCityNorthGateExterior`) with full 30-metric heuristics and `raw_param_snapshot.json` export.

## Verification Receipts

- **Atlas v2 Live Proof (`scan_raw_engine_atlas.py --v2 --repo E:/wt/raw-rt9`):**
  - `total_files_scanned: 1060`, `parse_errors: 0`
  - **`unguarded_offsets: 69 -> 0`** (100% production raw hex offsets guarded!)
  - **`playtest_rule_subsystems: 17`**, **`untested_subsystems: 5 -> 0`** (100% subsystem coverage!)
  - **`static_asserts: 164 -> 207`** (`+43` compile-time guards), `unasserted_gpu_abi_structs: 95 -> 83`, `unbound_hlsl_resources: 0`.
- **DXC 1.10 (`-O3 -WX`, `Shaders/CelestialBodies.hlsl` `cs_6_6`):** `1/1 PASS` (`CelestialCB` 48B parity locked).
- **Ninja `-j 6` (`raw_engine.dll`, `d3d11.dll`, `ce_runtime_check.exe`):** `[60/60]` clean (`0` warnings):
  - `raw_engine.dll`: `SHA256 = BD0B1662FD3E954ABF06BCBC0A23821875006CC055FF916BB1989C3F27EE926D`
  - `d3d11.dll`: `SHA256 = 2470F63C4D54D135E1810F1CCF260920845835EC691EAEC611C98B9F2BBA5DC7`
  - `ce_runtime_check.exe`: **`522 passed, 0 failed`**
- **Wave 87 C++23 Standalone Suite (`/DNOMINMAX /std:c++latest /W4 /WX`, `tests/wave87_param_registry_offsets_parity_check.cpp`):** **`5/5 PASS`** (`0` warnings; `4/4` planted fail-closed controls `87-A..87-D`, `RawParamSlot = 128 B` / `alignas(64)`, byte-identical JSON snapshot determinism, Mod SDK + DebugViews integration).
- **Cumulative C++23 & Python Gates:**
  - Cumulative C++23 `/W4 /WX`: **`16/16` wave binaries PASS** (Waves 72..87) + `ce_runtime_check.exe` **`522/522`**.
  - Cumulative Python Suites: **`17/17` PASS** (including `tests/test_wave87_param_registry_offsets_parity.py`).
- **RTX 4090 GPU Hardware Suites (`D3D12 DebugLayer=ON`, `0` errors):**
  - `test_tsr_upscale_gpu.exe`: **`18/18 PASS`** (`0.0524 ms`, `0` D3D12 Debug Layer errors).
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors = 0`, `48kHz FDN-8+HRTF 145.9x RT`, `Hi-Z+16k Cull 0.0287 ms`, `RC-GI 0.117 ms`, `Fluid 0.256 ms`, `XPBD 0.0578 ms`, `NaN = 0`).
- **Live 6-Scene Moving-Camera Skyrim SE (`--profile extended6`, `E:/wt/raw-rt9-build/live_w87_ext6_v5`, **`154/154 PASS`**):**
  - Base live audit: **`144/144 PASS`** (`6/6` clean scenes, `0` defects, `16` camera steps each):
    - `riverwood_day`: `24/24 PASS` (`shadow = 0.049752`, `ghost = 0.054559`, `dark_mo = 0.001842`, `mv = 0.047615`, `nrm = 0.065346`, `water_cyan = 0.000000`, `crushed_bark = 0.000671`).
    - `inn_interior`: `24/24 PASS` (`shadow = 0.001197`, `ghost = 0.025382`, `dark_mo = 0.000031`, `mv = 0.020910`, `nrm = 0.004808`).
    - `septimus_cave`: `24/24 PASS` (`shadow = 0.001197`, `ghost = 0.000000`, `dark_mo = 0.000000`, `mv = 0.003469`, `nrm = 0.000489`).
    - `windhelm_ext_snow`: `24/24 PASS` (`shadow = 0.018526`, `ghost = 0.000000`, `dark_mo = 0.000211`, `mv = 0.023979`, `nrm = 0.012810`).
    - `whiterun_ext` (`WhiterunExterior01`): `24/24 PASS` (`shadow = 0.033024`, `ghost = 0.000000`, `dark_mo = 0.000627`, `mv = 0.029893`, `nrm = 0.022760`, `crushed_bark = 0.000101`).
    - `riften_canal` (`RiftenCityNorthGateExterior`): `24/24 PASS` (`shadow = 0.036708`, `ghost = 0.079365`, `dark_mo = 0.003095`, `mv = 0.062564`, `nrm = 0.088859`, `crushed_bark = 0.000012`).
  - Extended 30-metric collector (`wave87_playtest_v2_report.json`): **`10/10 PASS`** (`all_passed = True`, `coverage_ok = True` across **`17/17` subsystems**, `min_ssim = 0.93385`, `foliage_shimmer_fft = 0.02858` over `724,305` canopy px, **`fireflies = 0`**, **`black_crush_pct = 0.982%`**, `highlight_clip = 0.0%`, `out_of_gamut = 0.0%`, `15` ownership passes with `0` double-exec / `0` flicker, `raw_param_snapshot.json` verified, `rtrace_sha256 = b824022e72645e251d7d67c5960650e1986570ec7f6332f5295f6e2241e471b6`).
  - Visually inspected all 6 fresh 1080p PNGs (`riverwood_day`, `inn_interior`, `septimus_cave`, `windhelm_ext_snow`, `whiterun_ext`, `riften_canal`).
  - Host cleanup verified: `SentinelCleaned=True`, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released (`LOCK_EXISTS=False`).
