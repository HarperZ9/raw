# RAW DX12 Wave 77: Multi-Bounce World-Space Froxel GI + Gust-Advected Water Foam/Ripples + Energy-Conserving Micro-Flake Snow Glint (2026-11-26)

## Summary

Wave 77 extends the live Skyrim SE DX12 rendering pipeline in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`) directly on top of Wave 74 (`RawWorldDynamics`), Wave 75 (`L=8` world-space hash radiance cache in `RawRestirHashCache` / `RtCacheCommon`), and Wave 76 (`RawPrecipThermal` + two-order HG crepuscular shafts) with zero duplicate code:

1. **World-Space Multi-Bounce GI Injection into Froxel Volumetrics (`RawVolumetricGiGlint.hlsli::rawEvalFroxelWorldGiBounce`, `FroxelPass.hlsl`, `LightingPass.cpp`)**
   - Binds the resolved DXR + Wave 75 `L=8` hash-radiance-cache multi-bounce GI texture (`RtGiBounce` at `t5`, extending `kFroxelInjectLayout` to 8 slots in `LightingPass.cpp`) into `FroxelPass.hlsl::froxel_inject`.
   - Computes the Wave 75 `L=8` world-space cascade LOD and deterministic hash key (`rawHashCacheKeyMultiRes`) and applies a strict 3D world-space depth-proximity gate (`depthGate = 0` for sky, occluded voxels behind walls, and foreground air far from distant surfaces), eliminating screen-space glow bleed completely.
   - Couples depth-gated `RtGiBounce` with pure 3D world-space clustered fire/torch bounce and directional ambient bounce under strict finite clamping and albedo-bounded bounce conservation (`scatterAlbedo <= 0.85`, `0` NaN/Inf).

2. **Gust-Advected Water Foam & Capillary Ripple Coupling (`RawVolumetricGiGlint.hlsli::rawEvalGustWaterFoamRipple`, `RtWater.hlsl`)**
   - Couples Wave 74's coherent world-space wind gust vector directly into incommensurate downwind-advected capillary normal slope perturbations, coherent downwind foam lace advection, and shoreline/rapids foam accumulation (while leaving calm deep inland water foam-free).
   - Modulates water Fresnel and roughness under foam micro-bubbles so `diffuseFoam + fresnelEff <= 1.0` across all view angles and gust intensities.

3. **Micro-Flake Snow Glint Energy Conservation (`RawVolumetricGiGlint.hlsli::rawEvalMicroFlakeSnowGlint`, `Lighting.hlsl`)**
   - Replaces legacy duplicate unbounded snow sparkle lobes and camera-distance-quantized `floor(pa / grain(z))` cell swimming in `Lighting.hlsl` with a single unified IOR 1.31 micro-facet glint evaluator anchored strictly to fixed dyadic 3D world-space grids (`2u` and `4u` cells -> `0.0` screen-UV drift on camera pan).
   - Enforces algebraic white-furnace energy conservation (`totalReflectance = alb + (1 - alb) * (r / (1 + r)) <= 1.0`, `0` NaN/Inf).

## Files Added / Modified
- `src/d3d12_bridge/shaders/RawVolumetricGiGlint.hlsli` (new)
- `src/d3d12_bridge/RawVolumetricGiGlint.h` (new C++23 mirror)
- `src/d3d12_bridge/LightingPass.cpp` (`kFroxelInjectLayout[8]` + `froxGiSrc` binding at `t5`)
- `src/d3d12_bridge/shaders/FroxelPass.hlsl` (world-space multi-bounce GI injection)
- `src/d3d12_bridge/shaders/RtWater.hlsl` (gust-advected capillary ripples + foam advection/Fresnel/roughness coupling)
- `src/d3d12_bridge/shaders/Lighting.hlsl` (unified white-furnace-bounded world-space micro-flake snow glint)
- `tests/wave77_volumetric_gi_glint_check.cpp` (8-test C++23 unit + 4 planted-failure control suite)
- `tests/test_wave77_volumetric_gi_glint.py` (standalone canonical oracle)

## Verification Receipts

- **DXC 1.10 Matrix (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `35/35 PASS`, `0` warnings.
- **Ninja + MSVC C++23 `/W4 /WX` (Agility SDK `1.722.1-preview`):** `d3d11.dll` clean (`0` warnings); `wave77_volumetric_gi_glint_check.exe` **`8/8 PASS`** (`1,728`-state froxel GI sweep, `4/4` planted controls, white-furnace `<= 1.0`, `0.0` camera-pan UV drift, bit-identical `100 Hz` replay across `60/100/144/240 FPS`); `wave76` **`8/8 PASS`**; `wave75` **`9/9 PASS`**; `wave74` **`10/10 PASS`**; `wave73` **`12/12 PASS`**; `wave72` **`14/14 PASS`**.
- **GPU & Runtime Suites (RTX 4090, `D3D12 DebugLayer=ON`, `0` errors):**
  - `ce_runtime_check.exe`: **`522 passed, 0 failed`**
  - `test_tsr_upscale_gpu.exe`: **`18 passed, 0 failed`** (`0` D3D12 Debug Layer errors on RTX 4090 + WARP)
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors=0`, `NaN=0` across all 21 passes: Fluid `0.257 ms`, RC-GI `0.108 ms`, XPBD `0.0579 ms`, Froxels `0.0446 ms`, Clouds `0.0832 ms`, RAW-TSR+RCAS `0.0506 ms`, Water Caustics+Rays `0.0464 ms`, Snow/Footprint `0.0237 ms`)
  - Python suites: **`7/7 PASS`** (`test_wave77_volumetric_gi_glint.py` `3/3` + waves 76/75/74/73/72 + `test_visual_heuristics.py`).
- **Live 4-Scene Moving-Camera Skyrim SE Audit (`E:/wt/raw-rt9-build/live_w77_quick_v3`, `--profile quick --burst 6 --stride 3 --cam-sweep --cam-sweep-frames 600`, RTX 4090):** **`96/96 PASS`** (`4/4` clean scenes, `0` defects, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released, `Sentinel Cleaned: True`):
  - `riverwood_day`: `24/24 PASS` (`shadow=0.052032`, `ghost=0.048175`, `dark_mo=0.002051`, `mv=0.044653`, `nrm=0.065757`)
  - `inn_interior`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.024268`, `dark_mo=0.000045`, `mv=0.020333`, `nrm=0.004814`)
  - `septimus_cave`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.000000`, `dark_mo=0.000000`, `mv=0.003592`, `nrm=0.000462`)
  - `windhelm_ext_snow`: `24/24 PASS` (`shadow=0.018549`, `ghost=0.000000`, `dark_mo=0.000004`, `mv=0.022374`, `nrm=0.012063`)
