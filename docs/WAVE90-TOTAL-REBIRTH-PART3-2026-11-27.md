# Wave 90: Project Total Rebirth Part 3 (2026-11-27)

## Pre-Flight Keyword-Table & Formula Grounding (#1154)
- **12-Month Corpus Cat 4 (`Cat4_Graphics_Rendering_Audio_Optics_Games`, `26,945` items):** `calibrated` (`salience 136.757`), `upscale` (`salience 100.437`).
- **Commons Intake (`NEW-COMMONS-INTAKE-INVENTORY-2026-11-27.md` Item #8):** `Soraku` Neural Texture Compression (`64 MiB -> 10.7 MiB`, `-83%` VRAM on 4K PBR material stacks).
- **Verified `FORMULAS.tsv` Kernels:** `OPT-001` (Snell vector refraction), `OPT-002`/`OPT-004` (Fresnel/Schlick reflectance), `OPT-032` (Henyey-Greenstein phase function), `OPT-033` (Beer-Lambert optical absorption), `OPT-034` (GGX distribution), `COL-037` (PSNR).
- **Author Vision Anchors:** `E3` (Superframe/RAW DX12 live photoreal world overhaul), `E2` (compressed PBR asset pipeline).

## Deliverables Implemented
1. **Wave 89 Eye-Audit Visual Refinements:**
   - **Windhelm Crisp White Hoarfrost Twigs (`Lighting.hlsl` + `RawLiveWorldComposite.hlsl`):** Removed the legacy evergreen `min(albedo, float3(0.22, 0.34, 0.19))` dark-olive clamp from non-green snow-biome twigs, lifted frosted twig `skyVis`, and added crystalline white hoarfrost rime + cold Fresnel rim scatter so winter brambles read unmistakably frost-encrusted white/silver in the blizzard (`Gate #36 green_dominance_delta = -0.0007`, `spring_green_flip_frac = 0.0042`).
   - **Septimus Cave Translucent Sapphire Glacier Facets (`RawTotalMaterialOverhaul.hlsli` + `RawTotalRebirth.h`):** Replaced dark zebra striations with calibrated oblique 3:1 translucent sapphire crystal cleavage facets (`minMul = 0.60`, `maxMul = 1.62`, `grainAmp = 0.08`, `fineAmp = 2.65`, `macroAmp = 0.03`), 2-layer Snell-refracted (`OPT-001`, `IOR = 1.31`) internal parallax caustics modulated by Beer-Lambert (`OPT-033`) sapphire absorption, and bounded wet specular micro-glints (`fireflies = 0`).
2. **Part 3: 3D Layered Foliage, Backlit Subsurface Leaf Translucency & Wind-Driven Volumetric Grass:**
   - Added Henyey-Greenstein (`OPT-032`, `g = 0.62` canopy / `g = 0.58` grass) view-aligned backlit subsurface transmission on `TreeFoliageCanopy` and 4-shell volumetric grass, plus coherent travelling wind-gust micro-shadowing (`gustShadow`) and damped gust sheen (`0.24`), locking `foliage_shimmer_fft = 0.02449 <= 0.025`.
3. **`Soraku`-Inspired Compressed 4K PBR Neural/Latent Texture Cache (`RawNeuralTextureCache.h` + `RawNeuralTexDecode.hlsli`):**
   - Clean-room MIT half-res `uint8` latent grid + `int8`-quantized `9 -> 12 -> 9` SiLU residual sharpening MLP with 64-bit FNV-1a checksum sealing and fail-closed validation:
     - `NordicStone`: `5.28x` compression, `59.74 dB` PSNR, `0` NaNs
     - `TimberWood`: `5.28x` compression, `38.52 dB` PSNR, `0` NaNs
     - `GlacierIce`: `5.28x` compression, `35.25 dB` PSNR, `0` NaNs
     - `CobbleRoad`: `5.28x` compression, `56.75 dB` PSNR, `0` NaNs
     - `16x` 4K PBR material archetypes: `3072.0 MiB -> 576.0 MiB` (**`5.33x` / `-81.2%` VRAM**)
     - `3/3` planted fail-closed controls caught (corrupt latent bitflip, NaN weight scale, bad magic header).

## Verification Receipts
- **DXC 1.10 (`-O3 -WX`):** `7/7 PASS` (`Lighting.hlsl ps_6_6`, `RawLiveWorldComposite.hlsl cs_6_6/6_9/6_10`, `RawNeuralTexDecode.hlsli lib_6_6/6_9/6_10`, `0` warnings).
- **VS 18 Ninja Build (`raw_engine d3d11.dll -j 6`):** `0` warnings (`d3d11.dll` SHA-256 prefix `84FB35B4FDEA`, `raw_engine.dll` `BD0B1662FD3E`).
- **C++23 `/W4 /WX` & Runtime Suites:**
  - `wave90_total_rebirth_part3_check.exe`: **`12/12 PASS`**
  - `ce_runtime_check.exe`: **`522/522 PASS`**
- **Python Verification Suites:** **`21/21 PASS`** (including `tests/test_wave90_total_rebirth_part3.py`).
- **Atlas v2 & RTX 4090 GPU Suites (`d3d12DebugErrors = 0`):**
  - Atlas v2: `unguarded_offsets = 0`, `unbound_hlsl_resources = 0`, `untested_subsystems = 0` (`17/17`).
  - `test_tsr_upscale_gpu.exe` (`18/18`), `raw_bench_suite.exe` (`21/21`), `test_texture_atlas_gpu.exe` (`22/22`).
- **Live 6-Scene Skyrim SE Playtest (`E:/wt/raw-rt9-build/live_w90_ext6_v8`):**
  - **`raw.heuristics-v2/37`:** **`all_passed: true`**, **`17/17` extended gates (`#21..#37`) + `141` base = `158` PASS**, `failure_codes: []`, `coverage_ok: true` (`17/17` subsystems), `rtrace_sha256: b824022e72645e251d7d67c5960650e1986570ec7f6332f5295f6e2241e471b6`.
  - **Key Gates:** `Gate #26 temporal_flip_ssim` (`mean_l1 = 0.02105`, `min_ssim = 0.94363`), `Gate #27 foliage_shimmer_fft = 0.02449 <= 0.025`, `Gate #28 fireflies = 0 <= 4`, `Gate #29 photometric_integrity` (`0` NaN, `0.603%` black crush, `0.0%` highlight clip), `Gate #31 vanilla_passthrough = 0.0%`, `Gate #32 tri_amp = 4.0x`, `Gate #33 detail_uplift = 1.9626x >= 1.65x`, `Gate #34 tiling_drop = 55.149% >= 40.0%`, `Gate #35 animal_no_green_bleed = 0.0182 / 0.0094`, `Gate #36 snow_foliage_no_spring_green = -0.0007 / 0.0042`, `Gate #37 ice_wood_anisotropic_no_blotch` (`ice_anisotropy = 1.7531`, `ice_blotch = 0.0985`, `wood_anisotropy = 1.2776`, `wood_blotch = 0.4015`).
  - **All 6 Matched-Pose `3840x1080` Proof Plates Visually Eye-Audited (`live_w90_ext6_v8`):**
    1. `E:/wt/raw-rt9-build/live_w90_ext6_v8/septimus_cave_rebirth_compare_3840x1080.png`
    2. `E:/wt/raw-rt9-build/live_w90_ext6_v8/windhelm_ext_snow_rebirth_compare_3840x1080.png`
    3. `E:/wt/raw-rt9-build/live_w90_ext6_v8/whiterun_ext_rebirth_compare_3840x1080.png`
    4. `E:/wt/raw-rt9-build/live_w90_ext6_v8/riverwood_day_rebirth_compare_3840x1080.png`
    5. `E:/wt/raw-rt9-build/live_w90_ext6_v8/riften_canal_rebirth_compare_3840x1080.png`
    6. `E:/wt/raw-rt9-build/live_w90_ext6_v8/inn_interior_rebirth_compare_3840x1080.png`
- **Host & Lock Hygiene:** `POST_LOCK_EXISTS = False`, `POST_SKYRIMSE_PROCS = 0`, `Mode=1` untouched.
