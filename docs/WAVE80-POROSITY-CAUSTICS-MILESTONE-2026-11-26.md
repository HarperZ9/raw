# RAW DX12 Wave 80 Milestone: Porosity-Coupled Wet-Film Smoothing + Three-Band Cauchy Spectral Caustics + Cumulative Wave 72-80 Certificate (2026-11-26)

## Summary

Wave 80 marks the cumulative **Wave 72-80 RAW DX12 Milestone** in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`), closing the physical transport loop across atmosphere, precipitation, fluids, caustics, porous wet surfaces, combustion, character subsurface scattering, and temporal reconstruction via clean-room `RawPorosityCaustics.hlsli` + C++23 mirror `RawPorosityCaustics.h`:

1. **Porosity-Coupled Wet-Stone / Mud / Wood Albedo Darkening & Micro-Normal Water-Film Smoothing (`RawPorosityCaustics.hlsli::rawEvalPorosityWetFilm`, `Lighting.hlsl`):**
   - Reuses Wave 75/76 world-space heightfield concavity (`rawPtLaplacianConcavity`) and active torch/brazier melt wetness for physically-based porous darkening (`albedoWet = pow(albedo, 1 + k_porosity * wEff)`), water-film micro-normal flattening toward the macro surface normal, and clearcoat roughness tightening (`0.04..0.12` in pooled cavities).
   - Preserves exact identity on living green pines (`isGreenPine == true`) and algebraically guarantees white-furnace energy conservation (`max(albedoWet) + fresnelFilm <= 1.0`).

2. **Three-Band Cauchy Spectral Water-Bed Caustic Dispersion (`RawPorosityCaustics.hlsli::rawEvalSpectralCausticDispersion`, `Lighting.hlsl`, `RtWater.hlsl`):**
   - Evaluates Snell refraction at exact Cauchy refractive indices (`n_R = 1.331`, `n_G = 1.333`, `n_B = 1.339`) coupled to Wave 74 coherent wind gusts and Wave 76 volumetric cloud transmittance over a zero-mean world-space focusing field (`white-furnace spatial mean == 1.0`, `0.0` screen-UV camera-pan drift, zero neon clipping).

3. **Cumulative Wave 72-80 Verified Architecture (Skyrim SE / Fallout 4 / Starfield CE2):**
   - Wave 72: Cross-title ABI contract (`SkyrimSE` / `Fallout4` / `Starfield` Native DX12, Agility SDK `1.722.1-preview`, SM `6.6/6.9/6.10`).
   - Wave 73: Deja null-space 10-step temporal anti-drift guard + deterministic `100 Hz` frame pacing.
   - Wave 74: World-space coherent wind-gust field + foliage phase-locked MV compensation + volumetric cloud-shadow coupling.
   - Wave 75: Disocclusion-safe 2-slot ReSTIR-GI/PT shift-mapping Jacobian & MIS + online `L=8` hash radiance cache + heightfield puddle drainage.
   - Wave 76: World-space thermal/wind snow accumulation & torch/brazier melt + two-order HG cloud-crepuscular shafts + sky-water multi-scattering.
   - Wave 77: World-space multi-bounce froxel GI injection (3D depth proximity gate) + gust-advected water foam/ripples + world-space micro-flake snow glint.
   - Wave 78: Specular-diffuse split temporal disocclusion guard + world-space sub-pixel foliage anti-shimmer + analytical aerial height-fog coupling.
   - Wave 79: Physically-bounded Planck blackbody (`1400 K..3200 K`) fire/smoke self-shadowing + Burley skin/fur/cloth SSS energy conservation.
   - Wave 80: Porosity wet-film darkening/smoothing + 3-band Cauchy spectral water-bed caustics.

## Files Added / Modified (Wave 80)
- `src/d3d12_bridge/shaders/RawPorosityCaustics.hlsli` (new)
- `src/d3d12_bridge/RawPorosityCaustics.h` (new C++23 mirror)
- `src/d3d12_bridge/shaders/Lighting.hlsl` (wired `rawEvalPorosityWetFilm` + `rawEvalSpectralCausticDispersion`)
- `src/d3d12_bridge/shaders/RtWater.hlsl` (wired `rawEvalSpectralCausticDispersion`)
- `tests/wave80_porosity_caustics_check.cpp` (8-test C++23 unit + 4 planted controls + 3-game profile replay)
- `tests/test_wave80_porosity_caustics.py` (standalone canonical oracle)

## Verification Receipts (2026-11-26)
- **DXC 1.10 (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `38/38 PASS` (`0` warnings, `0` errors).
- **Ninja `-j 6` (`vcvars64.bat`, Agility SDK `1.722.1-preview`):** `[7/7] Linking CXX shared library d3d11.dll` (`0` warnings; deployed `SHA256: 899ED44517A7DDDFA5207C6BD6D494A9AD433CECA50414D46B936CB7A0697073`).
- **Cumulative C++23 `/DNOMINMAX /std:c++latest /W4 /WX` Suites:**
  - `wave80_porosity_caustics_check.exe`: `8/8 PASS` (`1,440`-state porosity sweep, white-furnace caustic spatial mean `== 1.0`, `4/4` planted fail-closed controls, 3-game `SkyrimSE`/`Fallout4`/`Starfield` `100 Hz` replay).
  - `wave79`: `8/8 PASS`, `wave78`: `8/8 PASS`, `wave77`: `8/8 PASS`, `wave76`: `8/8 PASS`, `wave75`: `9/9 PASS`, `wave74`: `10/10 PASS`, `wave73`: `12/12 PASS`, `wave72`: `14/14 PASS`.
  - `ce_runtime_check.exe`: `522 passed, 0 failed`.
- **Cumulative Python Suites (`10/10 PASS`):** `test_wave80_porosity_caustics.py` (`3/3`) + `wave79..72` + `test_visual_heuristics.py`.
- **RTX 4090 GPU Hardware Suites (`D3D12 DebugLayer=ON`, `0` errors):**
  - `test_tsr_upscale_gpu.exe`: `18/18 PASS` (`0` D3D12 errors).
  - `raw_bench_suite.exe`: `21/21 PASS` (`d3d12DebugErrors=0`, `NaN=0`: Fluid `0.257 ms`, RC-GI `0.118 ms`, XPBD `0.0579 ms`, Froxels `0.0447 ms`, Clouds `0.0833 ms`, RAW-TSR+RCAS `0.0505 ms`, Water Caustics+Rays `0.0461 ms`, Snow/Footprint `0.0237 ms`).
- **Live 4-Scene Moving-Camera Skyrim SE Audit (`E:/wt/raw-rt9-build/live_w80_quick_v2`, `96/96 PASS`, `0` defects):**
  - `riverwood_day`: `24/24 PASS` (`16` steps, `dark_mo=0.001660` new all-time best, `ghost=0.052107`, `mv=0.048054`, `nrm=0.065491`, `shadow=0.054467`, `water_neon_cyan=0.000000`, `crushed_bark=0.000651`).
  - `inn_interior`: `24/24 PASS` (`16` steps, `shadow=0.001197`, `ghost=0.027078`, `dark_mo=0.000023`, `mv=0.022399`, `nrm=0.004571`).
  - `septimus_cave`: `24/24 PASS` (`16` steps, `shadow=0.001197`, `ghost=0.000000`, `dark_mo=0.000000`, `mv=0.003565`, `nrm=0.000462`, `water_neon_cyan=0.000000`).
  - `windhelm_ext_snow`: `24/24 PASS` (`16` steps, `shadow=0.027811`, `ghost=0.054861`, `dark_mo=0.000246`, `mv=0.024538`, `nrm=0.011777`, `water_neon_cyan=0.000000`).
  - Host cleanup: `Sentinel Cleaned: True`, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released (`False`).
