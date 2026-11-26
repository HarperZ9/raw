# RAW DX12 Wave 74: World-Space Coherent Wind/Foliage MV Compensation and Volumetric Cloud-Shadow Coupling (2026-11-26)

## Summary

Wave 74 delivers genuine world-space physical dynamics and radiative cloud-shadow consistency across foliage, wet surfaces, water, and temporal sun-shadow reconstruction:

1. **Shared World-Space Coherent Gust Field & Radiative Coupling Library (`src/d3d12_bridge/shaders/RawWorldDynamics.hlsli` + C++23 mirror `src/d3d12_bridge/RawWorldDynamics.h`):**
   - `rawEvalCoherentWindGust4(pAbsW, cloudWind, weather, simTimeSec)`: Evaluates a finite, NaN/Inf-sanitized multi-octave traveling wind-gust wavefield aligned to `cloudWind.xy`, returning coherent horizontal sway velocity, dimensionless gust intensity `gust01 in [0, 1]`, and the exact analytical phase-locked temporal sway derivative `swayDeriv`.
   - `rawEvalWetLeafRadiativeBudget(baseAlbedo, wetness, ndotv, cloudTrans, gust01)`: Enforces strict white-furnace energy conservation (`max(albedoEff) + fresnelWet + directTransWeight <= 1.0`) across all viewing angles, wetness levels, cloud transmittances, and gust amplitudes, attenuating only direct backlit transmission under volumetric cloud shadows while preserving indirect sky ambient.
   - `rawEvalWaterCloudRoughnessCoupling(cloudTrans, gust01, rainFall)`: Radiatively couples volumetric cloud-shadow transmittance and wind-gust chop to water GGX specular roughness broadening (`roughScale in [1.0, 1.85]`), direct sun glint attenuation, riverbed caustics, and backlit wave-crest subsurface scattering while keeping sky reflection un-darkened.
2. **Foliage Phase-Locked Temporal MV Compensation (`Rawlss5Pass.hlsl`) & Thin-Foliage Shadow Stability (`RtSkyFilter.hlsl`):**
   - `Rawlss5Pass.hlsl`: Applies bounded (`<= 1.25 px`) phase-locked gust MV compensation and gust-front reactivity to exterior foliage while keeping Wave 73's Deja 10-step anti-drift guard intact.
   - `RtSkyFilter.hlsl`: Removes misplaced `sunCloudBoost` flushing on geometric DXR `Vis.x` sun visibility (volumetric `cloudTransL` is applied analytically with zero temporal lag in `Lighting.hlsl` and `RtWater.hlsl`), and adds a rigid-pan read-only `SunHistIn` depth-safe fallback for thin swaying foliage/grass blades plus silhouette disocclusion snapping.
3. **Volumetric Cloud-Shadow Coupling Across Terrain, Wet Surfaces, Bark, Foliage, and Water (`Lighting.hlsl`, `RtWater.hlsl`):**
   - `Lighting.hlsl`: Unifies `cloudTransL = pow(saturate(cloudShadow(p + camPosW.xyz)), 1.25f)` across direct sun shadow, wet clearcoat roughness broadening, energy-conserving wet-leaf subsurface transmission, and radiatively consistent overcast sky-bounce preservation on vertical exterior tree bark (preventing crushed black bark under moving cloud shadows).
   - `RtWater.hlsl`: Evaluates spatial `cloudTransW = pow(saturate(cloudShadow(wpW)), 1.25f)` and couples it to triple-lobe GGX sun glint roughness & attenuation, riverbed caustics, and wave-crest SSS while leaving sky reflection physically intact.
4. **Unit & Planted-Failure Verification Suites (`tests/wave74_dynamics_coupling_check.cpp`, `tests/test_wave74_dynamics_coupling.py`):**
   - Exhaustive `1,800`-state white-furnace sweep (`albedo + Fresnel + transmission <= 1.0`), extreme `NaN/Inf` sanitization (`0` NaN/Inf propagated), deterministic `100 Hz` fixed-step replay bit-identical across `60 / 100 / 144 / 240 FPS`, and `4/4` planted-failure negative controls (injected energy leak `> 1.0`, unphysical cloud-shadow sky-ambient darkening, clock desync drift, and Wave 73 Deja bias check).

## Verification Receipts

- **DXC 1.10 Matrix (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `29/29 PASS`, `0` warnings (`FroxelPass.hlsl` x 9, `Rawlss5Pass.hlsl` x 3, `RawTsrUpscaleSim.hlsl` x 6, `Lighting.hlsl` x 3, `RtWater.hlsl` x 3, `RtSkyFilter.hlsl` x 3, `Tonemap.hlsl` x 2).
- **Ninja + MSVC C++23 `/W4 /WX` (Agility SDK `1.722.1-preview`):** `d3d11.dll` clean (`0` warnings); `wave74_dynamics_coupling_check.exe` **`10/10 PASS`** (`4/4` planted controls); `wave73_antidrift_pacing_check.exe` **`12/12 PASS`**; `wave72_profile_abi_check.exe` **`14/14 PASS`**.
- **GPU & Runtime Suites (RTX 4090, `D3D12 DebugLayer=ON`, `0` errors):**
  - `ce_runtime_check.exe`: **`522/522 PASS`**
  - `test_tsr_upscale_gpu.exe`: **`18/18 PASS`** (`0.0504-0.0531 ms`, `0` D3D12 Debug Layer errors on RTX 4090 + WARP)
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors=0`, `NaN=0` across all 21 subsystems: Fluid `0.257 ms`, RC-GI `0.111 ms`, XPBD `0.0580 ms`, Froxels `0.0446 ms`, Clouds `0.0832 ms`, RAW-TSR+RCAS `0.0504 ms`, Water Caustics+Rays `0.0463 ms`, Snow/Footprint `0.0236 ms`)
  - Python suites: `test_wave74_dynamics_coupling.py` **`3/3 PASS`**, `test_wave73_antidrift_pacing.py` **`3/3 PASS`**, `test_wave72_multigame_surpass.py` **PASS**, `test_visual_heuristics.py` **PASS**.
- **Live 4-Scene Moving-Camera Skyrim SE Audit (`--profile quick --burst 6 --stride 3`, RTX 4090):** **`96/96 PASS`** (`4/4` clean scenes, `0` defects, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`):
  - `riverwood_day`: `24/24 PASS` (`defects: none`, shadow `0.052817`, ghost `0.068485` [`-14.0%` vs Wave 73], bark `0.000697`)
  - `inn_interior`: `24/24 PASS` (`defects: none`, shadow `0.001197`, ghost `0.028430`, bark `0.0`)
  - `septimus_cave`: `24/24 PASS` (`defects: none`, shadow `0.001197`, ghost `0.000000`, bark `0.0`)
  - `windhelm_ext_snow`: `24/24 PASS` (`defects: none`, shadow `0.018374` [`-32.3%` vs Wave 73], ghost `0.000000` [`-100%` vs Wave 73], bark `0.0`)
