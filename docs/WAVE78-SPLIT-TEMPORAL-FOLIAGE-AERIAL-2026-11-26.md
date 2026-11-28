# RAW DX12 Wave 78: Specular-Diffuse Split Temporal Disocclusion Guard + World-Space Sub-Pixel Foliage Anti-Shimmer + Analytical Aerial-Perspective & Height-Fog Multi-Scatter Coupling (2026-11-26)

## Summary

Wave 78 extends the live Skyrim SE DX12 rendering pipeline in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`) with three clean-room physically grounded upgrades sharing `RawSplitTemporalAerial.hlsli` + C++23 mirror `RawSplitTemporalAerial.h` (reusing Wave 76's `rawEvalCrepuscularPhase` with zero duplicate code):

1. **Specular-Diffuse Split Temporal Disocclusion Guard (`RawSplitTemporalAerial.hlsli::rawEvalSplitTemporalGuard`, `Rawlss5Pass.hlsl`):**
   - Decouples roughness-adaptive specular virtual-reflection parallax clamping (`specParallaxFactor = (1 - r)^2`, `gammaSpec <= gammaDiff`, `alphaSpec >= alphaDiff`) from smooth diffuse GI history (`gammaDiff`, `alphaDiff`), eliminating specular-to-diffuse crosstalk so wet stone, snow glint, and water reflections stay sharp under rapid camera sweeps without dragging dark trails while preserving Wave 73's Deja 10-step fixed point.

2. **World-Space Sub-Pixel Foliage Anti-Shimmer & Coverage Preservation (`RawSplitTemporalAerial.hlsli::rawEvalFoliageCoverageAntiShimmer`, `Lighting.hlsl`):**
   - Applies distance-adaptive sub-pixel mip/coverage solidity preservation (`[1.0, 1.18]`) and pine-needle backlit translucency conservation (`totalEnergyRatio <= 1.0`, white-furnace safe) strictly in 3D world space on exterior canopies (`base == 5u || isGreenPineCard`), returning exact `1.0` identity on tree trunks (`isWoodBark == true`) so trunks never darken.

3. **Energy-Conserving Analytical Aerial-Perspective & Height-Fog Multi-Scatter Coupling (`RawSplitTemporalAerial.hlsli::rawEvalAnalyticalAerialHeightFog`, `LightFog.hlsl`):**
   - Integrates altitude-exponential height fog analytically with an exact Taylor limit at flat horizon rays (`|u| < 1e-3`, zero `0/0` NaNs), couples Wave 76's volumetric cloud transmittance and two-order crepuscular phase into distant mountain in-scattering (`T in [0, 1]`, white-furnace bounded `<= 1.0`), and smoothly locks horizon silhouettes to sky luminance with `0` horizon seam pops.

## Files Added / Modified
- `src/d3d12_bridge/shaders/RawSplitTemporalAerial.hlsli` (new)
- `src/d3d12_bridge/RawSplitTemporalAerial.h` (new C++23 mirror)
- `src/d3d12_bridge/shaders/Rawlss5Pass.hlsl` (wired `rawEvalSplitTemporalGuard`)
- `src/d3d12_bridge/shaders/Lighting.hlsl` (wired `rawEvalFoliageCoverageAntiShimmer`)
- `src/d3d12_bridge/shaders/LightFog.hlsl` (wired `rawEvalAnalyticalAerialHeightFog`)
- `tests/wave78_split_temporal_aerial_check.cpp` (8-test C++23 unit + 4 planted-failure control suite)
- `tests/test_wave78_split_temporal_aerial.py` (standalone canonical oracle)

## Verification Receipts

- **DXC 1.10 Matrix (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `38/38 PASS`, `0` warnings.
- **Ninja + MSVC C++23 `/W4 /WX` (Agility SDK `1.722.1-preview`):** `[6/6] d3d11.dll` clean (`0` warnings, including native SM 6.9 DXIL 1.9 RAWLSS Long Vectors); `wave78_split_temporal_aerial_check.exe` **`8/8 PASS`** (`4/4` planted controls: specular->diffuse crosstalk caught, super-unit aerial inscatter caught, flat-horizon 0/0 NaN & negative transmittance caught, extreme NaN/Inf sanitized; bit-identical `100 Hz` replay across `60/100/144/240 FPS`); `wave77` **`8/8 PASS`**; `wave76` **`8/8 PASS`**; `wave75` **`9/9 PASS`**; `wave74` **`10/10 PASS`**; `wave73` **`12/12 PASS`**; `wave72` **`14/14 PASS`**.
- **GPU & Runtime Suites (RTX 4090, `D3D12 DebugLayer=ON`, `0` errors):**
  - `ce_runtime_check.exe`: **`522 passed, 0 failed`**
  - `test_tsr_upscale_gpu.exe`: **`18 passed, 0 failed`** (`0` D3D12 Debug Layer errors on RTX 4090 + WARP)
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors=0`, `NaN=0` across all 21 passes: Fluid `0.256 ms`, RC-GI `0.108 ms`, XPBD `0.0579 ms`, Froxels `0.0447 ms`, Clouds `0.0834 ms`, RAW-TSR+RCAS `0.0505 ms`, Water Caustics+Rays `0.0463 ms`, Snow/Footprint `0.0237 ms`)
  - Python suites: **`8/8 PASS`** (`test_wave78_split_temporal_aerial.py` `3/3` + waves 77..72 + `test_visual_heuristics.py`).
- **Live 4-Scene Moving-Camera Skyrim SE Audit (`E:/wt/raw-rt9-build/live_w78_quick_v2`, `--profile quick --burst 6 --stride 3 --cam-sweep`, RTX 4090):** **`96/96 PASS`** (`4/4` clean scenes, `0` defects, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released, `Sentinel Cleaned: True`):
  - `riverwood_day`: `24/24 PASS` (`shadow=0.050096`, `ghost=0.056047`, `dark_mo=0.001697` all-time best, `mv=0.049380`, `nrm=0.065333`)
  - `inn_interior`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.027121`, `dark_mo=0.000020`, `mv=0.022073`, `nrm=0.004409`)
  - `septimus_cave`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.000000`, `dark_mo=0.000000`, `mv=0.003468`, `nrm=0.000489`)
  - `windhelm_ext_snow`: `24/24 PASS` (`shadow=0.016403` all-time best, `ghost=0.000000`, `dark_mo=0.000008`, `mv=0.017354` all-time best, `nrm=0.011445` all-time best)
