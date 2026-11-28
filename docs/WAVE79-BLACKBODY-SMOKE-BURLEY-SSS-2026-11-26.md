# RAW DX12 Wave 79: Physically-Bounded Planck Blackbody Fire/Smoke + Burley Skin/Cloth SSS + Multi-Game Profile Verification (2026-11-26)

## Summary

Wave 79 upgrades combustion emission, volumetric smoke self-shadowing, and character skin/fur/cloth subsurface transport in `E:/wt/raw-rt9` (`feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`) via clean-room shared library `RawBlackbodySss.hlsli` + C++23 mirror `RawBlackbodySss.h`:

1. **Physically-Bounded Planck Blackbody Fire/Torch/Brazier Emission & Volumetric Smoke Self-Shadowing (`RawBlackbodySss.hlsli::rawEvalBoundedBlackbodyFireSmoke`, `LightFog.hlsl`):**
   - Evaluates exact Planck spectral chromaticity across `1400 K..3200 K` at Rec.709/Rec.2020-safe centroids (`615, 540, 465 nm`) with a uniform scalar HDR soft shoulder so channel ratios remain invariant (zero salmon/magenta hue shift under peak hearth/brazier radiance).
   - Couples exact Beer-Lambert smoke self-shadowing (`T_smoke = exp(-max(od, 0))` in `(0, 1]`) and soot underside scattering bounded by `sootAlbedo <= 0.48` directly into `LightFog.hlsl::evalVolumetricFirePlumes`.

2. **Character Skin & Fur/Cloth Burley-Normalized Subsurface Scattering Guard (`RawBlackbodySss.hlsli::rawEvalBurleySkinClothSss`, `Lighting.hlsl`):**
   - Evaluates Christensen-Burley normalized diffusion terminator wrap + roughness-damped Schlick Fresnel (`albedoEff + sssWrap + fresnelSpec <= 1.0` algebraically white-furnace bounded), wired into both `skin` shading and `isActorCloth` guard/NPC fur/tunic strata slabs in `Lighting.hlsl`.

3. **Multi-Game Profile Verification (`tests/wave79_blackbody_sss_check.cpp`, `tests/test_wave79_blackbody_sss.py`):**
   - Verifies bit-identical `100 Hz` deterministic replay across **Skyrim SE (`CE1_DX11`)**, **Fallout 4 (`CE1_5_FO4_DX11`)**, and **Starfield (`CE2_Starfield_DX12`)** at `60 / 100 / 144 / 240 FPS`, plus 4 planted-failure controls.

## Files Added / Modified
- `src/d3d12_bridge/shaders/RawBlackbodySss.hlsli` (new)
- `src/d3d12_bridge/RawBlackbodySss.h` (new C++23 mirror)
- `src/d3d12_bridge/shaders/LightFog.hlsl` (wired `rawEvalBoundedBlackbodyFireSmoke`)
- `src/d3d12_bridge/shaders/Lighting.hlsl` (wired `rawEvalBurleySkinClothSss` on skin + actor cloth/fur)
- `tests/wave79_blackbody_sss_check.cpp` (8-test C++23 unit + 4 planted controls + 3-game profile replay)
- `tests/test_wave79_blackbody_sss.py` (standalone canonical oracle)

## Verification Receipts

- **DXC 1.10 Matrix (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `38/38 PASS`, `0` warnings.
- **Ninja + MSVC C++23 `/W4 /WX` (Agility SDK `1.722.1-preview`):** `[5/5] d3d11.dll` clean (`0` warnings); `wave79_blackbody_sss_check.exe` **`8/8 PASS`** (`1,500`-state Burley skin/cloth white-furnace `<= 1.0`, `1400 K..3200 K` temperature-monotone Planck chromaticity & hue-preserving shoulder, `4/4` planted controls, bit-identical `100 Hz` replay across Skyrim SE / Fallout 4 / Starfield profiles & `60/100/144/240 FPS`); `wave78` **`8/8 PASS`**; `wave77` **`8/8 PASS`**; `wave76` **`8/8 PASS`**; `wave75` **`9/9 PASS`**; `wave74` **`10/10 PASS`**; `wave73` **`12/12 PASS`**; `wave72` **`14/14 PASS`**.
- **GPU & Runtime Suites (RTX 4090, `D3D12 DebugLayer=ON`, `0` errors):**
  - `ce_runtime_check.exe`: **`522 passed, 0 failed`**
  - `test_tsr_upscale_gpu.exe`: **`18 passed, 0 failed`** (`0` D3D12 Debug Layer errors on RTX 4090 + WARP)
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors=0`, `NaN=0` across all 21 passes: Fluid `0.258 ms`, RC-GI `0.109 ms`, XPBD `0.0580 ms`, Froxels `0.0447 ms`, Clouds `0.0836 ms`, RAW-TSR+RCAS `0.0506 ms`, Water Caustics+Rays `0.0463 ms`, Snow/Footprint `0.0237 ms`)
  - Python suites: **`9/9 PASS`** (`test_wave79_blackbody_sss.py` `3/3` + waves 78..72 + `test_visual_heuristics.py`).
- **Live 4-Scene Moving-Camera Skyrim SE Audit (`E:/wt/raw-rt9-build/live_w79_quick_v1`, `--profile quick --burst 6 --stride 3 --cam-sweep`, RTX 4090):** **`96/96 PASS`** (`4/4` clean scenes, `0` defects, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`, `D:/gpu.lock.d` released, `Sentinel Cleaned: True`):
  - `riverwood_day`: `24/24 PASS` (`shadow=0.051870`, `ghost=0.063464`, `dark_mo=0.001895`, `mv=0.049141`, `nrm=0.070776`)
  - `inn_interior`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.030739`, `dark_mo=0.000023`, `mv=0.025369`, `nrm=0.004187`)
  - `septimus_cave`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.000000`, `dark_mo=0.000000`, `mv=0.003921`, `nrm=0.000474`)
  - `windhelm_ext_snow`: `24/24 PASS` (`shadow=0.020072`, `ghost=0.045785`, `dark_mo=0.000002`, `mv=0.018472`, `nrm=0.012788`)
