# Wave 67 Live Moving-Camera Skyrim SE Playtest Report (2026-11-25)

## 1. Execution Summary & Environment

- **Repository & Base Commit:** `E:\wt\raw-rt9` (branch `feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`, built on `36271ba`)
- **Build & Unit Verification:**
  - `ninja -C E:\wt\raw-rt9-build -j 6` (VS 18 `vcvars64.bat`, `RAW_ENGINE_DIR=C:/dev/public/superframe/native`): `NINJA_EXIT=0`, `0` warnings
  - `E:\wt\raw-rt9-build\tests\ce_runtime_check.exe`: **522 passed, 0 failed** (`CE_EXIT=0`)
  - DXC 1.10 (`H:\dxc-1.10.2605.37-2026-11-24\bin\x64\dxc.exe -HV 2021 -Gis -O3 -enable-16bit-types -WX`) on `RtWater.hlsl`: **3/3 PASS** (`cs_6_6`, `cs_6_9`, `cs_6_10`)
  - `python E:\wt\raw-rt9\tests\test_visual_heuristics.py`: **ALL PASS (`failures=0`)**
  - `python E:\wt\raw-rt9\tools\eyes\membrane_gate.py gate-selftest`: **12/12 PASS**
  - `python E:\wt\raw-rt9\tools\eyes\verify_runtime.py selftest`: **12/12 PASS**
- **Live Deployment (`H:\SteamLibrary\steamapps\common\Skyrim Special Edition`):**
  - `d3d11.dll` (`sha256=AA80B9B1802D`), `raw_engine.dll` (`sha256=8CA105FAFAF4`), `raw_shaders\RtWater.hlsl` (`sha256=443FA1862129`)
- **Pre-Flight & Post-Flight Headroom:** `FreePhysicalMemory = 11.53 GB` (`>= 8 GB` gate passed), `SKYRIMSE_PROCS = 0` before and after capture, `SkyrimPrefs.ini` restored bit-exact (`bGamepadEnable=1`), `SkyrimCustom.ini` restored bit-exact, `boot_sentinel.state` cleaned cleanly on exit.
- **Harness Command:** `python E:\wt\raw-rt9\tools\live\full_pipeline_audit.py --profile quick --out-dir C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w67_quick_v4 --burst 6 --stride 3 --cam-sweep`
- **Captures Produced:** **384 live 1920x1080 PNG frames** (`96` frames per scene x `4` canonical scenes x `16` render/debug steps x `6`-frame unidirectional `cam_sweep` bursts at `burst_stride = 3`).

## 2. Wave 67 Root-Cause Fixes Landed

1. **Skyrim SE 1.6.1170 `GameComposite` Intercept (`src/d3d11_proxy/GameComposite.cpp`):**
   - Added PS hash `kCompositeHash1170 = 0xc29d75356434a10aull` and helper `IsHdrColorFormat(...)` accepting both `DXGI_FORMAT_R16G16B16A16_FLOAT (10)` and `DXGI_FORMAT_R11G11B10_FLOAT (26)` on `t0` and `rt0`.
   - **Live proof (`d3d11_proxy.log:16867`):**
     ```
     [GameComposite] composite matched (hash, hash=c29d75356434a10a, rt=scene, t0=fmt26 1920x1080, t1=fmt28 1920x1080, t2=fmt46 1920x1080): AO off, fog off
     ```
   - Vanilla SSAO and vanilla fog are now cleanly stripped on Skyrim SE 1.6.1170, bringing unit normal length from `0.8901` up to `0.9849` in `inn_interior` and `0.9912` in `septimus_cave`.
2. **Riverwood Shallow-Water Magenta Strip Eliminated (`src/d3d12_bridge/shaders/RtWater.hlsl`):**
   - Hardened `scanlineGapAlarm` with a `vDroppedWater` + foreground reed/twig occlusion guard (`Ln.a <= 1.5f || zBn < zSurfW + 1.0f`) so foliage silhouettes in front of shallow streams never trigger false dropout alarms.
   - Tuned healthy `WaterView=7` diagnostic color to clean emerald-teal (`float3(flow*0.32, 0.38 + depth/120*0.58, shore*0.46)`), keeping `(1, 0, 1)` magenta strictly reserved for real water dropout lines.
3. **Unidirectional `cam_sweep` + Canonical `engine-receipt/1` Emission (`tools/live/full_pipeline_audit.py`):**
   - Locked `sweep_sign = +1.0` continuously across `improved`, `view_motion_outline`, and `view_sun_shadow` so motion-vector reprojection compares identical camera pan direction (`best_sign == +mv` on every moving scene).
   - Added `emit_audit_flywheel_receipt(...)` calling `C:\dev\tools\interop\engine_to_flywheel_receipt.py` automatically after `full_pipeline_report.json` is finalized (with `run_oracle=True` in live audits and `run_oracle=False` in offline audits to prevent recursion, and zero post-sign mutations to `full_pipeline_report.json`).
   - Added `SkyrimPrefsGamepadGuard` (temporary `bGamepadEnable=0` with bit-exact restore in `__exit__`), `dismiss_survival_modal_if_paused`, `_click_skyrim_client_frac`, and hardware `SendInput` (`KEYEVENTF_SCANCODE`) alongside `PostMessageW` in `src/engine_rt/rt_control.cpp`.
4. **Windhelm Snow-as-Water Heuristic Fix & Silent-Except Cleanup (`tools/live/visual_heuristics.py`, `tools/eyes/membrane_gate.py`, `tools/eyes/verify_runtime.py`):**
   - Fixed `#22 analyze_water_temporal_flow_speed` to mask HUD, require chromatic `WaterView=7` signal (`chroma > 0.12`, `luma > 0.12`), and avoid falling back to dimmed snow in `w_depth` when `w_flow` is present.
   - Replaced bare/silent `except Exception:` blocks in `tools/eyes/membrane_gate.py` and `tools/eyes/verify_runtime.py` with specific exception classes and structured `sys.stderr` diagnostics.

## 3. Exact Wave 66 -> Wave 67 Per-Scene Heuristic Deltas (`live_w67_quick_v4`)

| Scene | Interior | Wave 66 Passed | Wave 67 Passed | Key Metric Deltas (Wave 66 -> Wave 67) | Remaining Flagged Items |
| :--- | :--- | :---: | :---: | :--- | :--- |
| **`inn_interior`** | Yes | `21 / 24` | **`24 / 24` (PASS)** | **FIRST 100% CLEAN LIVE SCENE (`+3`, `defects=[]`)!** `mean_norm_length`: `0.8901 -> 0.9849` (PASS, `bad_unit_len_frac=0.00583`); `mv`: `-mv (0.0562) -> +mv (0.04387 vs 0.06515 zero / 0.07922 -mv, PASS)`; `world_lock`: `0.7468 -> 1.3212` (PASS); `shadow_boil`: `0.0441 -> 0.01796` (PASS); `edge_ghost`: `0.0552 -> 0.03454` (PASS) | **None** |
| **`septimus_cave`** | Yes | `22 / 24` | **`22 / 24`** | `mean_norm_length`: `1.0173 -> 0.9912` (`bad_unit_len_frac=0.00026`, PASS); `mv`: `-mv -> +mv (0.01447 vs 0.03934 zero / 0.05132 -mv, PASS)`; `world_lock`: `0.8967 -> 2.6621` (PASS); `shadow_boil`: `0.00413` (PASS); `dark_outline`: `0` px (PASS) | `detect_albedo_range_pbr_sanity` (`above_094_frac=0.2691` on bright glacial ice walls now un-darkened by vanilla SSAO), `analyze_temporal_ghosting_and_motion_outline` (`265` edge px) |
| **`riverwood_day`** | No | `18 / 24` | **`18 / 24`** | `mean_norm_length`: `0.9080 -> 1.0144`; `WaterView=7`: **0 magenta pixels** (visual: pure emerald-aqua stream behind reeds/twigs); `analyze_water_temporal_flow_speed`: PASS | 6 defects (`dark_motion_outline`, `normal_map_integrity`, `mv_reprojection`, `temporal_ghosting`, `world_lock`, `shadow_stability`) all caused by Survival Mode modal pausing exterior camera (`rmse_zero_mv=0.01904`) |
| **`windhelm_ext_snow`** | No | `19 / 24` | **`18 / 24`** | **`analyze_water_temporal_flow_speed`: FAIL (`6.63 px/f` blue snow) -> PASS (`water_view7_active_frac=0.0007`)!** `mean_norm_length`: `0.9824 -> 1.0697`; `set_time 14` active (`game_hour=14.004`) | 6 defects all caused by Survival Mode modal pausing exterior camera (`rmse_zero_mv=0.00779`) |

## 4. Direct Visual Inspection Findings (`live_w67_quick_v4`)

Inspected directly via `read_file` on 1920x1080 PNG frames:

- **`riverwood_day_audit_water_flow_diag_f00.png`:** Pure crystal emerald-aqua Riverwood stream with **zero magenta pixels** behind foreground twigs and grass silhouettes. Wave 66's purple-magenta band is completely gone.
- **`riverwood_day_audit_improved_f00.png` & `windhelm_ext_snow_audit_improved_f00.png`:** `SkyrimPrefsGamepadGuard` (`bGamepadEnable=0`) cleanly switched Skyrim SE from gamepad UI (`Yes` with gamepad brackets) to mouse cursor mode, confirming the INI guard worked. However, Windows background focus/UIPI isolation prevented `SendInput` from moving Skyrim's DirectInput cursor onto `"No"`, leaving the Creation Club `"Enable Survival Mode?"` `MessageBoxMenu` centered on screen in both exterior saves.
- **`windhelm_ext_snow_audit_view_sun_shadow_f00.png`:** Crisp architectural shadows across Windhelm stonework with zero NaN/fire bleed; temporal shadow boil failure (`0.24204`) was caused by the modal dimming and camera pause.
- **`inn_interior_audit_improved_f00.png` & `riverwood_day_audit_view_normals_f00.png` (`v2`):** Warm Planck hearth lighting, zero black orbs, crisp motion vectors, and rich un-darkened normals confirming `GameComposite` `0xc29d75356434a10a` intercept.

## 5. Wave 68 Recommended Follow-Up (Exact Root Cause for Exterior Modal)

Because Creation Club's delayed `"Enable Survival Mode?"` `MessageBoxMenu` runs under Skyrim's DirectInput loop and rejects background Windows `SendInput` cursor movement when launched headless/from a background terminal, Wave 68 should add an in-process `dismiss_modal` bridge command inside `raw_engine.dll` (invoking `UIMessageQueue::AddMessage("MessageBoxMenu", UI_MESSAGE_TYPE::kHide)` or selecting button `"No"` directly inside Scaleform) so `riverwood_day` and `windhelm_ext_snow` dismiss the modal deterministically with zero OS focus dependency. Also review `septimus_cave` bright glacial albedo threshold (`above_094_frac=0.2691`) now that vanilla SSAO darkening is properly removed.

## 6. Key Artifact & Frame Paths

- Live Audit Report JSON: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w67_quick_v4\full_pipeline_report.json`
- Live Audit Report Markdown: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w67_quick_v4\full_pipeline_report.md`
- Auto-Emitted Audit Receipt: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w67_quick_v4\engine_receipt.json`
- Inspected Frames:
  - `...\live_w67_quick_v4\riverwood_day\riverwood_day_audit_water_flow_diag_f00.png`
  - `...\live_w67_quick_v4\riverwood_day\riverwood_day_audit_improved_f00.png`
  - `...\live_w67_quick_v4\windhelm_ext_snow\windhelm_ext_snow_audit_improved_f00.png`
  - `...\live_w67_quick_v4\windhelm_ext_snow\windhelm_ext_snow_audit_view_sun_shadow_f00.png`
