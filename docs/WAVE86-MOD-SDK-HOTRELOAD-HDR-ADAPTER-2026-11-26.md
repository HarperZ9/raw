# RAW DX12 Wave 86: 2027 Mod-Developer SDK ABI v2.0 + Sandboxed BuildLang FFI + Sub-100 ms Atomic Hot-Reload + HDR10/BT.2020/OKLab Calibrated Output + Cross-Title ICreationGameAdapter (2026-11-26)

## Summary

Wave 86 closes the full **2027 Creation Engine Architecture Milestone (Waves 82-86)** from `CE-2027-ARCHITECTURE-ROADMAP-2026-11-26.md` (`catalogue_id: 68390`) in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`):

1. **2027 Mod-Developer SDK ABI v2.0 + Sandboxed BuildLang FFI (`include/raw_mod_sdk_abi.h`, `src/engine_rt/RawModSdkHost.{h,cpp}`, `sdk/raw_mod_sdk.bld`):**
   - Versioned C11/C++23 ABI (`RAW_MOD_SDK_ABI_VERSION = 0x00020000u`) with `static_assert` 64-byte layout parity across `RawModPassDesc`, `RawModDebugViewDesc`, `RawModReadOnlyWorldQuery`, `RawModBudgetRecord`, and `RawModSdkApiV2`.
   - Exposes acyclic frame-graph pass registration (fail-closed DFS cycle rejection), non-colliding live debug view slots (`100..255` preserving core `0..8`), capability-gated read-only G-buffer/actor/navmesh queries, and `RawModBudgetTracker` enforcing per-mod CPU-microsecond + VRAM-byte soft/hard caps and naming the exact offending mod ID.
   - Matching sandboxed BuildLang contract `sdk/raw_mod_sdk.bld` verified clean via `buildc check` (`0` errors).

2. **Sub-100 ms Atomic Live Hot-Reload + Automatic PSO Rollback (`src/d3d12_bridge/RawHotReload.{h,cpp}`):**
   - Integrates with Wave 83's SHA-256 `RawPsoCache` to compile, canary-validate, and stage HLSL/BuildLang/material candidates for atomic Present-boundary commit (`<< 100 ms`).
   - Automatically rolls back to the last-known-good PSO/preset on any compile error, NaN canary, or ABI hash mismatch (`0` crash, `0` black frame).

3. **True HDR10 / BT.2020 / OKLab Calibrated Display Output (`src/d3d12_bridge/shaders/RawHdrDisplayOutput.hlsli`, `src/d3d12_bridge/RawHdrDisplayOutput.h`):**
   - Exact ITU-R BT.709 -> ITU-R BT.2020 matrix (D65 white preserved), Björn Ottosson OKLab perceptual soft-gamut rolloff (`0` negative/out-of-gamut clip), exact SMPTE ST 2084 PQ OETF/EOTF (`100..4000 nits` matching `Calibrate Pro` + `build-color`, round-trip `< 1e-4`), and exact SDR fallback identity.

4. **Shared Cross-Title `ICreationGameAdapter` (`src/ce_runtime/ICreationGameAdapter.h`) + `tools/live/raw_mod_check.py`:**
   - Pure cross-title adapter contract unifying `SkyrimSE`, `Fallout4`, `Starfield`, and `SuperframeNative` with fail-closed RTTI contract verification + deterministic headless mod-regression CLI `raw_mod_check.py`.

## Verification Receipts

- **BuildLang Compiler (`buildc check sdk/raw_mod_sdk.bld`):** Lexing OK (`314` tokens), Parsing OK (`14` items), Type checking OK, **`0` errors**.
- **DXC 1.10 (`-O3 -WX`, `RawHdrDisplayOutput.hlsli`):** **`3/3 PASS`** across `cs_6_6`, `cs_6_9`, `cs_6_10`.
- **Ninja `-j 6` (`raw_engine.dll`, `d3d11.dll`, `ce_runtime_check.exe`):** Clean (`0` warnings); `ce_runtime_check.exe` **`522 passed, 0 failed`**.
- **Wave 86 C++23 Standalone Suite (`/DNOMINMAX /std:c++latest /W4 /WX`, `tests/wave86_mod_sdk_hotreload_hdr_check.cpp`):** **`6/6 PASS`** (`0` warnings):
  - `T1`: SemVer v2.0 ABI (`0x00020000`) negotiation, capability-gated read-only world queries, non-colliding debug view slots `[100..255]`, and `RawModBudgetTracker` identifying exact offending mod ID `202` (`HeavyScriptMod`).
  - `T2` (**Planted Control 86-A**): ABI major `3.0` and truncated `structSize = 16` rejected fail-closed.
  - `T3` (**Planted Control 86-C**): Cyclic render pass dependency (`50 -> 51 -> 50`) rejected fail-closed (`FRAMEGRAPH_CYCLE`).
  - `T4` (**Planted Control 86-B**): Atomic Present-boundary hot-reload (`maxReloadUs = 1 us << 100 ms`), plus `3/3` automatic PSO rollbacks (syntax error, NaN canary, ABI hash mismatch) preserving last-known-good PSO untouched.
  - `T5`: HDR10/BT.2020/OKLab calibrated output (`d65WhiteErr = 0.00e+00`, SMPTE ST 2084 PQ round-trip relative error `2.44e-07 < 1e-4` across `0.5..4000 nits`, `peak1000PQ = 0.7518`, exact SDR fallback identity, `0` NaN).
  - `T6` (**Planted Control 86-D**): 4-title `ICreationGameAdapter` (`SkyrimSE`, `Fallout4`, `Starfield`, `SuperframeNative`) `100 Hz` bit-identical deterministic replay + cross-title Skyrim RTTI tag rejected by `Fallout4Adapter`.
- **Cumulative C++23, Python & Atlas Gates:**
  - Cumulative C++23 `/W4 /WX`: **`15/15` wave binaries PASS** (Waves 72..86) + `ce_runtime_check.exe` **`522/522`**.
  - `raw_mod_check.py --self-test`: **`self_test_ok: true`** (caught all 5 planted codes `ABI_MISMATCH`, `ADAPTER_CONTRACT_MISMATCH`, `FORMID_CONFLICT`, `FRAMEGRAPH_CYCLE`, `MOD_BUDGET_EXCEEDED`).
  - Cumulative Python Suites: **`16/16` PASS**.
  - Wave 38B/40D Atlas Re-Audit (`scan_raw_engine_atlas.py --src E:/wt/raw-rt9/src`): `cpp_files: 675`, `hlsl_files: 91`, `cpp_structs: 1616`, `static_asserts: 130`, `unguarded_offsets: 69`, `hlsl_resources: 295`, `unbound_resources: 0`.
  - Hygiene scan: `badEm = 0`, `badForb = 0`.
- **RTX 4090 GPU Hardware Suites (`D3D12 DebugLayer=ON`, `0` errors):**
  - `test_tsr_upscale_gpu.exe`: **`18/18 PASS`** (`0.0530 ms`, `0` D3D12 Debug Layer errors).
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors = 0`, `48kHz FDN-8+HRTF 143.5x RT`, `Hi-Z+16k Cull 0.0297 ms`, `RC-GI 0.115 ms`, `Fluid 0.257 ms`, `XPBD 0.0579 ms`, `Froxel 0.0448 ms`, `RAW-TSR+RCAS 0.0507 ms`, `Caustics 0.0464 ms`, `Snow 0.0239 ms`, `NaN = 0`).
- **Live 4-Scene Moving-Camera Skyrim SE + 30-Metric Playtest Runner (`E:/wt/raw-rt9-build/live_w86_quick_v1`, `106/106 PASS`):**
  - Base live audit: **`96/96 PASS`** (`4/4` clean, `0` defects, `16` camera steps per scene):
    - `riverwood_day`: `24/24 PASS` (`ghost = 0.065421`, `dark_mo = 0.001705`, `mv = 0.048193`, `nrm = 0.068285`, `water_neon_cyan = 0.000000`, `crushed_bark = 0.000636`).
    - `inn_interior`: `24/24 PASS` (`ghost = 0.026902`, `dark_mo = 0.000040`, `mv = 0.022352`, `nrm = 0.004360`).
    - `septimus_cave`: `24/24 PASS` (`ghost = 0.000000`, `dark_mo = 0.000000`, `mv = 0.003568`, `nrm = 0.000462`).
    - `windhelm_ext_snow`: `24/24 PASS` (`ghost = 0.000000`, `dark_mo = 0.000129`, `mv = 0.028041`, `nrm = 0.012743`).
  - Extended 30-metric collector (`wave86_playtest_v2_report.json`): **`10/10 PASS`** (`all_passed = True`, `coverage_ok = True` across `culling`, `lod`, `mod_sdk`, `streaming`, `min_ssim = 0.93640`, `foliage_shimmer_fft = 0.03067` over `729,028` canopy px, `fireflies = 2`, `black_crush_pct = 1.026%`, `highlight_clip = 0.0%`, `out_of_gamut = 0.0%`, `15` ownership passes with `0` double-exec / `0` flicker, `rtrace_sha256 = c69ea37dc1d90e5b8ee821b1b384ce1b64e8ddf665a31289c648a671498d194f`).
  - Visually inspected all 4 fresh 1080p PNGs (`riverwood_day`, `inn_interior`, `septimus_cave`, `windhelm_ext_snow`).
  - Host cleanup verified: `SentinelCleaned=True`, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released (`LOCK_EXISTS=False`).
