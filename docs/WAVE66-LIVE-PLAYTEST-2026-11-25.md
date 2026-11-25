# Wave 66 Live Moving-Camera Skyrim SE Playtest Report (2026-11-25)

## 1. Execution Summary & Environment

- **Repository & Commit:** `E:\wt\raw-rt9` at `36271ba` (Wave 66)
- **Live Deployment:** `H:\SteamLibrary\steamapps\common\Skyrim Special Edition` (`d3d11.dll` 7,715,840 B, `raw_engine.dll` 1,890,816 B, 83/83 SHA-256 verified)
- **Pre-Flight Headroom:** `FreePhysicalMemory = 10.25 GB` (`>= 8 GB` gate passed), RTX 4090 `22,066 MiB` free VRAM (`1%` GPU util), `SKYRIMSE_PROCS = 0` before and after capture
- **Harness Command:** `python E:\wt\raw-rt9\tools\live\full_pipeline_audit.py --profile quick --out-dir C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w66_quick --burst 6 --stride 3 --cam-sweep`
- **Captures Produced:** **384 live 1920x1080 PNG frames** (`96` frames per scene x `4` canonical scenes x `16` debug/render steps x `6`-frame `cam_sweep` bursts at `burst_stride = 3`)
- **Offline Visual Heuristics Self-Test:** `python E:\wt\raw-rt9\tests\test_visual_heuristics.py` -> **ALL PASS (`failures=0`)**
- **Boot Self-Check & Telemetry:** `boot_selfcheck.txt` `11/12 PASS, 1 WARN (save string count), 0 FAIL (EngineHealthScore 84/100)`; `d3d12_debug_errors = 0`; `boot_sentinel.state` cleaned cleanly on exit

## 2. Live 1920x1080 GPU Solver Telemetry (`bridge_live_telemetry.json`)

All 19 native DX12 GPU compute solvers executed live in `SkyrimSE.exe` at 1920x1080 (`frame 1080`, `rtActive = true`, `radianceCascadesActive = true`, `hydrologyErosionActive = true`, ~`1.57 ms` combined GPU compute):

| Subsystem | Live GPU Time (ms) | Live State |
| :--- | ---: | :--- |
| Radiance Cascades GI | `0.3420` | Active |
| Water Caustics | `0.2570` | Active |
| Fluid Fire Simulation | `0.2529` | 3 emitters, 8 pressure iters |
| Volumetric Clouds Raymarch | `0.1280` | Active |
| TSR Upscale | `0.0901` | Active |
| Live World Composite | `0.0819` | Active |
| PCSS Shadows | `0.0553` | Active |
| XPBD Physics | `0.0532` | 4,096 particles |
| Froxel Volumetric Fog (Inject + Integrate) | `0.0522` | `0.0266 + 0.0256` |
| SSR Trace | `0.0512` | Active |
| Post Cinematic | `0.0451` | Active |
| GPU Cull (Hi-Z + Indirect) | `0.0442` | 178 instances |
| GTAO (Main + Denoise) | `0.0430` | `0.0338 + 0.0092` |
| Auto Exposure (Hist + Adapt) | `0.0358` | `0.0338 + 0.0020` |
| Fluid Water Wakes | `0.0256` | 16 wakes |
| Snow Deformation | `0.0246` | Active |
| Dynamic Grass Wind | `0.0184` | Active |
| Clustered Lights (Build + Assign) | `0.0103` | 75 active lights |
| GPU Particles | `0.0092` | Active |

## 3. Per-Scene 24-Heuristic Audit Summary (`live_w66_quick`)

| Scene | Interior | Heuristics Passed | Key Verified Wins | Flagged Items |
| :--- | :--- | :--- | :--- | :--- |
| `riverwood_day` | No | `18 / 24` | `0` NaN/Inf, `0` red sky, `0` bleached pine, `0.004%` crushed bark (PASS), `0%` neon cyan, `0` actor moss overlap | `detect_dark_motion_outline`, `detect_normal_map_integrity`, `analyze_motion_vector_reprojection`, `analyze_temporal_ghosting_and_motion_outline`, `analyze_world_lock_anti_swimming`, `analyze_shadow_temporal_stability` (Survival Mode modal paused camera + GameComposite miss) |
| `inn_interior` | Yes | `21 / 24` | `0` indoor black orbs, `0` NaN/Inf, `detect_dark_motion_outline` PASS (`0.00001`), `analyze_temporal_ghosting_and_motion_outline` PASS (`0.0552`), `analyze_shadow_temporal_stability` PASS (`0.0441`), crisp walking NPC Embry | `detect_normal_map_integrity` (`mean_norm_len = 0.8901`, GameComposite SSAO), `analyze_motion_vector_reprojection` (`best_sign=-mv`, sweep flip), `analyze_world_lock_anti_swimming` (`0.7468` vs `0.75` threshold) |
| `septimus_cave` | Yes | `22 / 24` | `0` NaN/Inf, `detect_flat_ice_cave_triangles` PASS, `detect_normal_map_integrity` PASS (`mean_norm_len = 1.0173`), `analyze_world_lock_anti_swimming` PASS (`0.8967`), `analyze_shadow_temporal_stability` PASS (`0.0033`) | `analyze_motion_vector_reprojection` (`best_sign=-mv`, sweep flip), `analyze_temporal_ghosting_and_motion_outline` (`265` edge px) |
| `windhelm_ext_snow` | No | `19 / 24` | `0` NaN/Inf, `0` red sky (`0` px), `detect_dark_motion_outline` PASS (`0.00006`), `analyze_temporal_ghosting_and_motion_outline` PASS (`0.0470`), warm Planck guard torches | `detect_normal_map_integrity` (GameComposite SSAO), `analyze_water_temporal_flow_speed` (blue snow mistook for water), `analyze_motion_vector_reprojection`, `analyze_world_lock_anti_swimming` (`0.7477`), `analyze_shadow_temporal_stability` (Survival modal popped mid-capture + right wall shadows) |

## 4. Direct Visual Inspection Findings (1920x1080 Frame Audit)

Inspected directly via `read_file` on captured PNGs under `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w66_quick`:

- **`inn_interior_audit_improved_f00.png` & `_f05.png`:** Warm Planck fireplace glow on the left hearth, natural tavern wood and stonework, zero indoor black orbs, and smooth `cam_sweep` camera pan while NPC Embry walks across the tavern with zero dark silhouette outline.
- **`inn_interior_audit_view_motion_outline_f00.png` & `_view_normals_f00.png`:** Clean signed motion vector field with crisp blue `mvDivPx` silhouette rim around walking Embry; normal map shows rich carved timber and floor detail, slightly darkened by un-intercepted vanilla SSAO (`mean_norm_length = 0.8901`).
- **`septimus_cave_audit_improved_f00.png`:** Deep translucent glacial turquoise walls with internal crystalline fracture veins and zero flat-triangle artifacts.
- **`windhelm_ext_snow_audit_improved_f00.png` & `_f05.png`:** Smooth horizontal `cam_sweep` pan across snowy Windhelm courtyard, guards holding warm Planck torches, zero sky fire bleed, crisp snow & stone relief.
- **`riverwood_day_audit_view_flora_trees_f00.png` & `_view_nan_hdr_f00.png`:** Zero NaN/Inf/HDR > 32 magenta pixels; lush green foliage (`base==5`) and warm amber bark (`0` bleached needles, `85` crushed-bark pixels = `0.004%`, PASS).

## 5. Exact Root Causes Identified for Wave 67

1. **SMOKING GUN: Exact Skyrim SE 1.6.1170 `GameComposite` Hash & Format Logged (`d3d11_proxy.log:17624`):**
   ```
   [GameComposite] diag candidate #1 (frame 300): hash=c29d75356434a10a verts=6 rt=scene t0=fmt26(1920x1080) t1=fmt28(1920x1080) t2=fmt46(1920x1080)
   ```
   `GameComposite.cpp` checked for `t0 == DXGI_FORMAT_R16G16B16A16_FLOAT (10)`, whereas Skyrim 1.6.1170 binds `t0 = DXGI_FORMAT_R11G11B10_FLOAT (26)` with `t1 = R8G8B8A8_UNORM (28)` (vanilla SSAO) and `t2 = R24_UNORM_X8_TYPELESS (46)` (depth) under PS hash `0xc29d75356434a10a`. Adding hash `0xc29d75356434a10a` and accepting `DXGI_FORMAT_R11G11B10_FLOAT` for `t0` will cleanly intercept vanilla SSAO & fog across all normal and debug views, fixing `detect_normal_map_integrity` (`0.89 -> 1.00`).
2. **Creation Club Survival Mode Modal Still Popped Up Over `riverwood_day` & Mid-Capture in `windhelm_ext_snow`:**
   Visual inspection of `riverwood_day_audit_improved_f00.png` and `windhelm_ext_snow_audit_view_sun_shadow_f00.png` showed the `"Enable Survival Mode? Yes / No"` dialog pausing `riverwood_day` (freezing `cam_sweep` so `rmse_zero_mv = 0.0172`) and popping mid-burst in `windhelm_ext_snow` (spiking `analyze_shadow_temporal_stability` to `0.229`). `dismiss_modal` must reliably target `MessageBoxMenu` (`No` button / `Escape` / UI message close) rather than relying solely on `bat raw_scene_init` before the delayed CC quest trigger.
3. **Residual Riverwood Shallow-Water Magenta Patch Behind Twigs (`riverwood_day_audit_water_flow_diag_f00.png`):**
   Visual inspection spotted a visible purple-magenta band on the left Riverwood stream behind twigs/grass. The Survival Mode modal dimmed the frame `0.83x`, pushing those pixels just below the heuristic's magenta threshold (`magenta_pixels: 3`), so the heuristic reported PASS while visual inspection caught the real residual discontinuity in `RtWater.hlsl`.
4. **Mid-Capture `cam_sweep` Direction Flip Between `improved` & `view_motion_outline` (`full_pipeline_audit.py:630-639`):**
   `sweep_sign = -sweep_sign` reversed camera pan every `3.5s`, causing `improved` (`step 0`) and `view_motion_outline` (`step 1`) to run with opposite camera pan directions. As a result, `inn_interior` (`rmse_minus_mv = 0.05615 < rmse_plus_mv = 0.06870`) and `septimus_cave` (`rmse_minus_mv = 0.03639 < rmse_plus_mv = 0.04400`) both reported `best_sign: "-mv"`. Locking `sweep_sign` constant across `improved`, `view_motion_outline`, and `view_sun_shadow` will yield clean apples-to-apples motion-vector reprojection verification.
5. **Windhelm Snow Misclassified as Water in `#22` (`analyze_water_temporal_flow_speed`) & Right Stone Wall Shadows:**
   In `windhelm_ext_snow`, blue shadowed snow (`water_frac = 0.4244`) bypassed the `water_flow_diag` gate and was scored as `6.63 px/f` water flow; and `windhelm_ext_snow_audit_view_sun_shadow_f00.png` still shows blocky shadow edges on the right stone wall (beyond the Survival modal interruption).

## 6. Key Artifact & Frame Paths

- Report JSON: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w66_quick\full_pipeline_report.json`
- Report Markdown: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w66_quick\full_pipeline_report.md`
- Inspected Frames:
  - `...\live_w66_quick\riverwood_day\riverwood_day_audit_improved_f00.png`
  - `...\live_w66_quick\riverwood_day\riverwood_day_audit_water_flow_diag_f00.png`
  - `...\live_w66_quick\riverwood_day\riverwood_day_audit_view_flora_trees_f00.png`
  - `...\live_w66_quick\riverwood_day\riverwood_day_audit_view_nan_hdr_f00.png`
  - `...\live_w66_quick\inn_interior\inn_interior_audit_improved_f00.png` & `_f05.png`
  - `...\live_w66_quick\inn_interior\inn_interior_audit_view_motion_outline_f00.png` & `_view_normals_f00.png`
  - `...\live_w66_quick\septimus_cave\septimus_cave_audit_improved_f00.png`
  - `...\live_w66_quick\windhelm_ext_snow\windhelm_ext_snow_audit_improved_f00.png` & `_f05.png`
  - `...\live_w66_quick\windhelm_ext_snow\windhelm_ext_snow_audit_view_sun_shadow_f00.png`
