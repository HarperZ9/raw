# Wave 91: OPT-052 BuildLang-NTC610 SM 6.10 Integration & Wave 90 Visual Eye-Audit Refinements (2026-11-27)

## Pre-Flight Keyword-Table & Author Vision Grounding (#1154 / Gate 6)
- **Preflight (`subagent_discovery_hook.py preflight`):** `ok: true`, `elapsed_ms: 377.51`, `silo_exit_code: 0`, flagship owner `HarperZ9/superframe`.
- **Top Salient/Rare Terms:** `aerial` (`salience 102.215`, `idf 4.751`), `neural` (`45.235`), `vector` (`42.62`), `texture` (`34.862`), `specular` (`26.475`), `glacier` (`18.743`), `mist` (`17.003`), `facets` (`16.453`), `cooperative` (`10.19`, `idf 5.156`).
- **Author Vision Anchors:** `E3` (Superframe/RAW DX12 live photoreal world overhaul), `E2` (compressed PBR asset pipeline), `OPT-052` (`BuildLang-NTC610` `-83.33%` 4K PBR VRAM).

## Deliverables Implemented
1. **Septimus Cave Broad Smooth Carved Sapphire Glacier Planes (`RawTotalMaterialOverhaul.hlsli` + `RawTotalRebirth.h`):**
   - Cut medium-scale mottled albedo amplitude by **`69%`** (`fineAmp: 2.65 -> 0.82` on strictly silky 15:1 unidirectional cleavage sheen, `minMul: 0.60 -> 0.80`, `maxMul: 1.62 -> 1.32`).
   - Replaced busy camouflage mottling with organic flow-warped broad smooth translucent sapphire crystal planes (`rawGlacierCarvedVoronoi`), delicate razor-thin luminous conchoidal hairline fracture seams (`seamRim` + subtle `groove`), silky directional cleavage polish, and 2-layer Snell (`OPT-001`, `IOR = 1.31`) + Beer-Lambert (`OPT-033`) internal depth.
2. **Riverwood & Riften Near-Mid `< 60 m` Aerial Mist Clearing (`FroxelPass.hlsl` + `LightFog.hlsl`):**
   - Pushed clear-weather volumetric fog start past `~60 m` (`smoothstep(1800, 5200)` in `FroxelPass.hlsl`, `smoothstep(1600, 4800)` in `LightFog.hlsl`) and gated `groundBank`, `valleyStratMist`, `forestCanopyBelt`, and `valleyDeck` cleanly behind `nearMidClear60m` so mid-ground Riverwood pines and Riften golden autumn birches stay crisp and sunlit while distant mountains retain full 3D volumetric depth.
3. **Riverwood Dry Granite Roughness Floor (`Lighting.hlsl`):**
   - Enforced a matte dry-granite roughness floor (`dryGraniteFloor = lerp(0.78, 0.24, wetRock01)`, `pebbleFloor = lerp(0.74, 0.28, wet)`, `dryRockMinAlpha = lerp(0.76, 0.22, wet)`) and damped dry rock micro-normal sparkle (`0.42 -> 0.24`), eliminating white micro-specular flecking on non-wet foreground granite boulders while keeping rain/shoreline rocks authentically wet.
4. **Wired `OPT-052` `RawNtc610ShaderSpec.hlsli` (SM 6.10 Cooperative Vectors + SM 6.6/6.9 Fallback):**
   - Added pure math/struct spec `src/d3d12_bridge/shaders/RawNtc610ShaderSpec.hlsli` (exact 256-byte `RawNtc610MaterialCB`, `RawNtc610PbrSample`, zero unbound global registers).
   - Wired `rawNtc610DecodeInlineMlp(...)` into `RawNeuralTexDecode.hlsli` with compile-time `RAW_ENABLE_SM610_COOPVEC` SM 6.10 cooperative-vector switch and identical SM 6.6/6.9 unrolled FMA fallback.
   - Wired exact C++23 ABI mirror (`static_assert(sizeof(RawNtc610MaterialCB) == 256)`, `EstimateNtc6104kBudget`, `DecodeNtc610InlineMlp`) into `RawNeuralTextureCache.h` (`10.6675 MiB`/4K PBR archetype, `5.9995x` / `-83.33%` vs `64 MiB` BC7, `170.6801 MiB` for all 16 Skyrim/Playground world archetypes, `4/4` planted fail-closed controls caught).

## Verification Receipts
- **DXC 1.10 (`-O3 -WX`, `0` warnings):** **`10/10 PASS`** (`RawLiveWorldComposite.hlsl` `cs_6_6/6_9/6_10`, `RawNeuralTexDecode.hlsli` `lib_6_6/6_9/6_10` + `lib_6_10 -DRAW_ENABLE_SM610_COOPVEC`, `Lighting.hlsl ps_6_6`, `LightFog.hlsl ps_6_6`, `FroxelPass.hlsl ps_6_6`).
- **VS 18 Ninja Build (`raw_engine d3d11.dll -j 6`):** `0` warnings (`d3d11.dll` SHA-256 prefix `3F205026937C`, `raw_engine.dll` `BD0B1662FD3E`).
- **C++23 `/W4 /WX` & Runtime Suites:**
  - `wave91_ntc610_and_visual_audit_check.exe`: **`10/10 PASS`** (`10.6675 MiB/class`, `5.9995x`, `-83.33%` vs BC7, `16` classes = `170.6801 MiB`, `|N| = 1.000000`, `4/4` planted controls rejected).
  - `wave90_total_rebirth_part3_check.exe`: **`12/12 PASS`** (`0` regressions).
  - `ce_runtime_check.exe`: **`522/522 PASS`**.
- **Python Verification Suites:** **`22/22 PASS`** (including `tests/test_wave91_ntc610_and_visual_audit.py`).
- **Standalone RTX 4090 GPU Suites (`d3d12DebugErrors = 0`):**
  - `test_tsr_upscale_gpu.exe`: **`18/18 PASS`** (`D3D12 Debug Layer reported 0 errors/corruptions`).
  - `raw_bench_suite.exe`: **`PASS (d3d12DebugErrors=0)`**.
  - `test_texture_atlas_gpu.exe`: **`22/22 PASS`** (`HW: 0 D3D12 Debug Layer errors`).
- **Live 6-Scene Skyrim SE Playtest (`E:/wt/raw-rt9-build/live_w91_ext6_v4`):**
  - **`raw.heuristics-v2/37`:** **`all_passed: true`**, **`160/160` (`143` base + `17/17` extended `#21..#37`) PASS**, **`5/6` clean scenes**, `failure_codes: []`, `coverage_ok: true` (`17/17` subsystems), `rtrace_sha256: b824022e72645e251d7d67c5960650e1986570ec7f6332f5295f6e2241e471b6`.
  - **Key Gates:** `Gate #27 foliage_shimmer_fft = 0.03341 <= 0.12`, `Gate #28 fireflies = 3 <= 4`, `Gate #29 photometric_integrity` (`0` NaN, `2.44%` black crush, `0.0%` highlight clip), `Gate #31 vanilla_passthrough = 0.0%`, `Gate #32 tri_amp = 4.0x`, `Gate #33 detail_uplift = 2.2598x >= 1.65x`, `Gate #34 tiling_drop = 58.937% >= 40.0%`, `Gate #35 animal_no_green_bleed = 0.0208 / 0.0088`, `Gate #36 snow_foliage_no_spring_green = -0.0007 / 0.0022`, `Gate #37 ice_wood_anisotropic_no_blotch` (`ice_anisotropy = 1.4391`, `ice_blotch = 0.3124`, `wood_anisotropy = 1.2520`, `wood_blotch = 0.4408`).
  - **All 6 Matched-Pose `3840x1080` Proof Plates Visually Eye-Audited (`live_w91_ext6_v4`):**
    1. `E:/wt/raw-rt9-build/live_w91_ext6_v4/septimus_cave_rebirth_compare_3840x1080.png`
    2. `E:/wt/raw-rt9-build/live_w91_ext6_v4/riverwood_day_rebirth_compare_3840x1080.png`
    3. `E:/wt/raw-rt9-build/live_w91_ext6_v4/riften_canal_rebirth_compare_3840x1080.png`
    4. `E:/wt/raw-rt9-build/live_w91_ext6_v4/windhelm_ext_snow_rebirth_compare_3840x1080.png`
    5. `E:/wt/raw-rt9-build/live_w91_ext6_v4/whiterun_ext_rebirth_compare_3840x1080.png`
    6. `E:/wt/raw-rt9-build/live_w91_ext6_v4/inn_interior_rebirth_compare_3840x1080.png`
- **Host & Lock Hygiene:** `POST_LOCK_EXISTS = False`, `POST_SKYRIMSE_PROCS = 0`, `Mode=1` untouched.
