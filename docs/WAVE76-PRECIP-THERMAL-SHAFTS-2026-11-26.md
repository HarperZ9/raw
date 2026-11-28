# RAW DX12 Wave 76: World-Space Thermal/Wind Precipitation Accumulation, Torch/Brazier Melt, and Coupled Cloud-Crepuscular & Sky-Water Multi-Scattering (2026-11-26)

## Summary

Wave 76 couples RAW DX12's world-space heightfield concavity (Wave 75) and coherent wind-gust field (Wave 74) with active thermal point-light snow melt and energy-conserving volumetric/sky-water multiple scattering:

1. **World-Space Thermal & Wind-Driven Precipitation Accumulation (`src/d3d12_bridge/shaders/RawPrecipThermal.hlsli` + C++23 mirror `src/d3d12_bridge/RawPrecipThermal.h` + `Lighting.hlsl`):**
   - `rawEvalWorldThermalSnowAccum(...)`: Couples pure absolute-world discrete Laplacian concavity (`nabla^2 H > 0` sheltered hollows), coherent leeward wind drift vs windward gust scour, and local warm point-light thermal flux (torches, braziers, campfires) so snow accumulates naturally in sheltered depressions and melts monotonically into glossy dark wet stone/slush around active thermal sources.
   - Wired cleanly into `Lighting.hlsl` so live braziers and guard torches melt snow even without external seasonal-sim uniforms while remaining `100%` screen-edge invariant (`0.0` screen-UV dependence, `0` NaN/Inf, strictly excluding vertical wood bark).
2. **Energy-Conserving Two-Order Cloud-Crepuscular Shafts (`FroxelPass.hlsl`) & Anisotropic Sky-to-Water Multi-Scattering (`RtWater.hlsl`):**
   - `rawEvalCrepuscularPhase(cosTheta, opticalDepth, cloudTrans)`: Two-order Henyey-Greenstein phase function blending forward Mie crepuscular shafts (`g=0.72`) with isotropic multi-scattering fill (`g=0.22`) weighted by optical depth `1 - exp(-0.65 * tau)` and bounded by single-scattering albedo `<= 0.96` (verified 4-pi solid-angle energy integral `<= 1.0`).
   - `rawEvalSkyWaterMultiScatter(skyRefl, waterBody, fresnelF, cloudTrans, ndotv, windGust01)`: Couples grazing wave-trough multiple-bounce Fresnel transfer with spatial cloud transmittance and wind gusts while staying strictly white-furnace bounded inside `[min(waterBody, skyRefl), max(waterBody, skyRefl)]`.
3. **Unit & Planted-Failure Verification Suites (`tests/wave76_precip_thermal_check.cpp`, `tests/test_wave76_precip_thermal.py`):**
   - Exhaustive `1,728`-state energy-conservation & boundedness sweep across temperature, wind speed, snow cover, torch distance, cloud transmittance, and phase angle.
   - **Planted Control A:** Warm torch proximity (`260 u -> 120 u -> 40 u`) monotonically decreases snow depth & increases wet glossy melt; disabling the thermal term fails closed.
   - **Planted Control B:** Exact `0.0` screen-edge residual from screen center to `2 px` border; injected radial screen-space vignette hack caught (`> 0.20` diff).
   - **Planted Control C:** Extreme `NaN/Inf` inputs sanitize cleanly (`0` propagated).
   - **Planted Control D:** 4-pi crepuscular solid-angle integral conserves energy (`<= 1.0`) while planted super-unit albedo (`1.35`) exceeds bound and fails closed.
   - Deterministic `100 Hz` replay verified bit-identical (`diff <= 1e-9`) across `60 / 100 / 144 / 240 FPS`.

## Verification Receipts

- **DXC 1.10 Matrix (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `35/35 PASS`, `0` warnings.
- **Ninja + MSVC C++23 `/W4 /WX` (Agility SDK `1.722.1-preview`):** `d3d11.dll` clean (`0` warnings); `wave76_precip_thermal_check.exe` **`8/8 PASS`** (`4/4` planted controls); `wave75` **`9/9 PASS`**; `wave74` **`10/10 PASS`**; `wave73` **`12/12 PASS`**; `wave72` **`14/14 PASS`**.
- **GPU & Runtime Suites (RTX 4090, `D3D12 DebugLayer=ON`, `0` errors):**
  - `ce_runtime_check.exe`: **`522/522 PASS`**
  - `test_tsr_upscale_gpu.exe`: **`18/18 PASS`** (`0` D3D12 Debug Layer errors on RTX 4090 + WARP)
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors=0`, `NaN=0` across all 21 passes: Fluid `0.256 ms`, RC-GI `0.109 ms`, XPBD `0.0580 ms`, Froxels `0.0447 ms`, Clouds `0.0834 ms`, RAW-TSR+RCAS `0.0508 ms`, Water Caustics+Rays `0.0465 ms`, Snow/Footprint `0.0239 ms`)
  - Python suites: `test_wave76_precip_thermal.py` **`3/3 PASS`**, `test_wave75` **`3/3 PASS`**, `test_wave74` **`3/3 PASS`**, `test_wave73` **`3/3 PASS`**, `test_wave72` **PASS**, `test_visual_heuristics.py` **PASS**.
- **Live 4-Scene Moving-Camera Skyrim SE Audit (`--profile quick --burst 6 --stride 3`, RTX 4090):** **`96/96 PASS`** (`4/4` clean scenes, `0` defects, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`):
  - `riverwood_day`: `24/24 PASS` (`shadow=0.049742` new all-time best `<0.050`!, `ghost=0.068707`, `bark=0.000692`, `nrm=0.065211`)
  - `inn_interior`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.029454`, `bark=0.0`, `nrm=0.004388`)
  - `septimus_cave`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.000000`, `bark=0.0`, `nrm=0.000484`)
  - `windhelm_ext_snow`: `24/24 PASS` (`shadow=0.023464`, `ghost=0.000000`, `bark=0.0`, `nrm=0.012719`)
