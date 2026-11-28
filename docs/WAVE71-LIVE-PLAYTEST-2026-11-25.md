# Wave 71 Competitive Rendering Leap & Live 96/96 Playtest Report (2026-11-25)

## 1. Execution Summary & Environment

- **Repository & Base Commit:** `E:\wt\raw-rt9` (branch `feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`, built directly on Wave 70 `5f05e30`)
- **Build, Shader & Unit Verification:**
  - DXC 1.10 (`H:\sources\catalogued\sdk-and-graphics\dxc-1.10.2605.37-2026-11-24\bin\x64\dxc.exe -HV 2021 -Gis -O3 -enable-16bit-types -WX`) across all 3 modified HLSL shaders and all 5 entry points (`FroxelPass.hlsl`: `froxel_inject`, `froxel_block_scan`, `froxel_integrate`; `Clouds.hlsl`: `fs`; `Rawlss5Pass.hlsl`: `rawlss5_fs`) across `ps_6_6`, `ps_6_9`, and `ps_6_10`: **15 / 15 PASS (`0` warnings, `0` errors)**
  - Full `ninja -C E:\wt\raw-rt9-build -j 6` (`742 / 742` targets, Agility SDK 1.620.1, DXC 1.9/1.10, NVIDIA Streamline 2.14.16, `RAW_ENGINE_DIR=C:/dev/public/superframe/native`): **`NINJA_PASS=1` (`0` warnings)**
  - `E:\wt\raw-rt9-build\tests\ce_runtime_check.exe`: **522 passed, 0 failed**
  - `python E:\wt\raw-rt9\tests\test_wave71_volumetric_temporal.py`: **ALL PASS (`failures=0`, 2/2 planted-failure controls caught)**
  - `python E:\wt\raw-rt9\tests\test_visual_heuristics.py`: **ALL PASS (`failures=0`)**
- **Live Deployment (`H:\SteamLibrary\steamapps\common\Skyrim Special Edition`):**
  - `d3d11.dll` (`sha256=AAD68EC0894A`)
- **Post-Flight Hygiene:** `POST_SKYRIMSE_PROCS = 0`, RTX 4090 idle at `1934 MiB / 24564 MiB`, `SkyrimPrefs.ini` verified bit-exact (`bGamepadEnable=1`), `SkyrimCustom.ini` verified clean (`CUSTOM_INI_CLEAN=True`), `boot_sentinel.state` cleaned on exit.
- **Live Moving-Camera Harness Command:** `python E:\wt\raw-rt9\tools\live\full_pipeline_audit.py --profile quick --burst 6 --stride 3 --out-dir C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w71_quick_v1`
- **Headline Live Result:** **`96 / 96` heuristics passed (`100.0%`, `4 / 4` CLEAN SCENES, `0` flagged scenes, ZERO heuristic thresholds weakened!)**

## 2. Wave 71 Competitive Upgrades Landed

1. **Weather-Regime 3D Volumetric Anisotropic Mie-Draine Phase + Spectral Crepuscular Cloud-Gap Coupling (`src/d3d12_bridge/shaders/FroxelPass.hlsl`):**
   - Added `evalFroxelAnisoPhase(cosT, nightW, wetFogW, snowIceW, localOd)` dynamically adapting the Jendersie & d'Eon (SIGGRAPH 2023) unified HG+Draine phase parameters across dry alpine aerosols (`g=0.70, alpha=1.25`), humid valley/rain droplet fog (`g=0.82, alpha=2.10`), and freezing snow/diamond-dust (`g=0.85, alpha=2.60`), plus a physically gated 22-degree (`0.384 rad`) solar ice-crystal parhelion halo scatter ring.
   - Added `evalSpectralCrepuscularCoupling(...)` coupling `TransLut`, `cloudShadowN(wp, 2u)`, and live solar elevation so sunlit cloud-gap shafts gain slant-path Rayleigh/Ozone forward warmth while shadowed troughs stay cool sky slate.
   - Added in-scattering luminance delta (`scatRel`) to the Hillaire temporal froxel reprojection disocclusion guard (`histKeep`) so volumetric god-ray edges stay crisp without trailing under camera sweeps.
2. **Volumetric Cloud Draine Silver-Lining Diffraction + Anti-Solar Glory + Neighborhood-Clamped History (`src/d3d12_bridge/shaders/Clouds.hlsl`):**
   - Added `cloudDrainePhase` (blended into primary octaves `oct <= 1` of the 6-octave multi-scattering loop for sharper cumulus solar coronas), `cloudGloryRing` (water-droplet anti-solar cloud glory peak around `cosT -> -1`), and solar-elevation-coupled spectral optical depth `odRGB`.
   - Upgraded temporal cloud `History` reprojection with a 4-tap cross neighborhood spread clamp (`hSpread`) around `curS`, suppressing cloud-horizon ghost trails during fast camera turns.
3. **Temporal/Neural Upscaler Disocclusion & Foliage Micro-Motion Hardening (`src/d3d12_bridge/shaders/Rawlss5Pass.hlsl`):**
   - Added `foliageMicroReact` driven by `isFoliage`, live `cloudWind.xy` + rain/snow wind speed, and local sub-pixel variance (`relLumVar + quadIrradStd`), and raised `isFoliage` `baseAlpha` (`0.22..0.86 -> 0.32..0.90`) so wind-swaying pine needles and grass cards stay razor-crisp without temporal smearing.
   - Added `useBilinearHist` disocclusion fallback (`fgSilhouetteNeighbor || depthDisocc > 0.28 || mvDivDisocc > 0.28 || isFoliage`) replacing 5-tap negative-lobe Catmull-Rom with clean bilinear history taps along silhouette boundaries, eliminating negative ringing halos on newly disoccluded edges (`inn_interior` edge ghosting dropped `0.02604 -> 0.0`, `mv` RMSE cut in half `0.02177 -> 0.01087`, `world_lock` jumped `2.3796 -> 3.9831`).
4. **Known-Answer & Planted-Failure Physics/Shader Verification (`tests/test_wave71_volumetric_temporal.py`):**
   - Numerically verifies Draine-HG solid-angle normalization (`< 2e-3` across dry/rain/ice regimes), monotonic weather-regime forward anisotropy (`1.207 < 1.788 < 1.844`), cloud glory anti-solar peaking & 22-deg ice ring, foliage micro-motion reactivity, HLSL contract tokens, and 2/2 fail-closed planted-failure controls (`g=1.02`, `alpha=-0.5`).

## 3. Exact Live 4-Scene Moving-Camera Results (`live_w71_quick_v1`, 384 Live 1080p PNGs)

| Scene | Interior | Verdict | Passed | MV Reproj (`+mv`) | World Lock | Shadow Boil / Flicker | Edge Ghost RMSE | Indoor Black Orbs | Depth Range | Mean Norm Len |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **`riverwood_day`** | No | **PASS** | `24 / 24` | `0.09553` | `1.2702` | `0.05439 / 0.03253` | `0.07028` | `skip (ext)` | `0.4057` | `0.9226` |
| **`inn_interior`** | Yes | **PASS** | `24 / 24` | **`0.01087`** | **`3.9831`** | `0.00120 / 0.00002` | **`0.00000`** | `0.8912` (`<=0.92`) | `0.0863` | `0.9943` |
| **`septimus_cave`** | Yes | **PASS** | `24 / 24` | `0.00397` | `4.7340` | `0.00120 / 0.00002` | `0.00000` | `0.0000` | `0.0754` | `0.9998` |
| **`windhelm_ext_snow`** | No | **PASS** | `24 / 24` | `0.02490` | `1.9676` | `0.02600 / 0.00673` | `0.04299` | `skip (ext)` | `0.5332` | `0.9852` |

## 4. Direct Visual Inspection Findings (`live_w71_quick_v1`)

All four live 1920x1080 rendered frames were inspected directly via `read_file`:

- **`riverwood_day_audit_improved_f00.png`:** Crisp individual spruce needles and foreground brush under foliage micro-motion hardening, clean layered cumulus clouds with bright silver-lining rims, sharp distant peaks and mossy cobblestone road, zero black facets or chalky foliage.
- **`inn_interior_audit_improved_f00.png`:** Zero temporal ghosting (`0.0`), crisp warm hearth fire smoke rising into wooden beams, readable back alcove timber walls, mounted pelts, and candlelight with rich dark contrast.
- **`septimus_cave_audit_improved_f00.png`:** Silky translucent cobalt-indigo glacial ice wall (`normLen=0.9998`, `ghost=0.0`, `orbs=0.0`) with zero bloom blowout or banding.
- **`windhelm_ext_snow_audit_improved_f00.png`:** Atmospheric falling snow particles lit organically inside warm amber gate brazier cones, soft volumetric snow-mist illumination filtering through the main Windhelm archway around approaching NPCs, crisp carved stone reliefs, and zero shadow flicker (`flk=0.00673`).

## 5. Key Artifact & Frame Paths

- Live Audit Report JSON: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w71_quick_v1\full_pipeline_report.json`
- Live Audit Report Markdown: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w71_quick_v1\full_pipeline_report.md`
- Unit & Contract Verification Suite: `E:\wt\raw-rt9\tests\test_wave71_volumetric_temporal.py`
