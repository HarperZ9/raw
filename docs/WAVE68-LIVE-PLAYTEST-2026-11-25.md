# Wave 68 Live Moving-Camera Skyrim SE Playtest Report (2026-11-25)

## 1. Execution Summary & Environment

- **Repository & Base Commit:** `E:\wt\raw-rt9` (branch `feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`, built on Wave 67 `e6292e2`)
- **Build & Unit Verification:**
  - `ninja -C E:\wt\raw-rt9-build -j 6` (VS 18 `vcvars64.bat`, `RAW_ENGINE_DIR=C:/dev/public/superframe/native`): `NINJA_EXIT=0`, `0` warnings
  - `E:\wt\raw-rt9-build\tests\ce_runtime_check.exe`: **522 passed, 0 failed** (`CE_EXIT=0`)
  - DXC 1.10 (`H:\dxc-1.10.2605.37-2026-11-24\bin\x64\dxc.exe -HV 2021 -Gis -O3 -enable-16bit-types -WX`) on `Lighting.hlsl` (`fs`): **3/3 PASS** (`ps_6_6`, `ps_6_9`, `ps_6_10`)
  - `python E:\wt\raw-rt9\tests\test_visual_heuristics.py`: **ALL PASS (`failures=0`)**
- **Live Deployment (`H:\SteamLibrary\steamapps\common\Skyrim Special Edition`):**
  - `d3d11.dll` (`sha256=BB5B6235320B`), `raw_engine.dll` (`sha256=33EE01BC6B08`), `raw_shaders\Lighting.hlsl` (`sha256=0F35BF7DFB1F`), `raw_shaders\RawMaterials3D.hlsli` (`sha256=FD4B4B81BC2A`)
- **Pre-Flight & Post-Flight Headroom:** `FreePhysicalMemory = 12.46 GB` (`>= 8 GB` gate passed), `SKYRIMSE_PROCS = 0` before and after capture, `SkyrimPrefs.ini` restored bit-exact (`bGamepadEnable=1`), `SkyrimCustom.ini` restored bit-exact, `boot_sentinel.state` cleaned cleanly on exit.
- **Harness Command:** `python E:\wt\raw-rt9\tools\live\full_pipeline_audit.py --profile quick --out-dir C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w68_quick_v4 --burst 6 --stride 3 --cam-sweep`
- **Captures Produced:** **384 live 1920x1080 PNG frames** (`96` frames per scene x `4` canonical scenes x `16` render/debug steps x `6`-frame unidirectional `cam_sweep` bursts at `burst_stride = 3`).
- **Headline Score:** **`93 / 96` heuristics passed (`96.9%`, up `+11` from Wave 67's `82 / 96`)**, **`2 / 4` scenes 100% CLEAN (`24/24 PASS`)**, and **ALL 4 scenes pass `best_sign = +mv`, `world_lock > 1.18`, `normal_map_integrity`, `albedo_pbr`, `dark_motion_outline`, and `temporal_ghosting`!**

## 2. Wave 68 Root-Cause Fixes Landed

1. **Deterministic Zero-Focus In-Process `MessageBoxMenu` Dismissal (`src/engine_rt/rt_view.h`, `src/engine_rt/rt_view.cpp`, `src/engine_rt/rt_control.cpp`, `tools/live/full_pipeline_audit.py`):**
   - Disassembled Skyrim SE `MessageBoxMenu::ProcessMessage` (`0x963050`) and `MessageBoxMenu::Accept` (`0x962E80`) on live `1.7.104.0`: plain `UIMessageQueue::AddMessage("MessageBoxMenu", kHide)` is intentionally ignored while `MessageBoxQueue._size > 0`, whereas `MessageBoxMenu::ButtonPress` (`0x962F50`, registered via `Accept`'s `lea r8, [rip+disp32]`) pops the active `MessageBoxData`, queues `kHide`, and executes `callback->Run(buttonIndex)`.
   - Added 100% version-independent runtime discovery (`DiscoverMessageBoxButtonPress` via `.?AVMessageBoxMenu@@` RTTI vtable -> `slot[1]` `Accept` -> `ButtonPress` RVA, with uint64-overflow-safe bounds checks and SEH protection, plus `DiscoverAddUiMessageRva` fallback via `UIMessageQueue::Singleton`).
   - At runtime, `DismissMessageBoxMenuInProcess()` locates the live `MessageBoxMenu*` in `UI::Singleton->menuStack` (`+0x110/+0x118/+0x120`) by exact RTTI vtable match and calls `ButtonPress` with `buttonIndex = 1.0` (`"No"`), closing the Creation Club `"Enable Survival Mode?"` dialog permanently on the first attempt with zero OS focus or `SendInput`.
   - **Live proof (`control_results.jsonl`):**
     - `riverwood_day` (`present=462`): `{"cmd":"dismiss_modal","event":"ui_hide_queued","ok":true,"add_msg_rva":"0x962F50","count":1}` -> modal gone immediately!
     - `windhelm_ext_snow` (`present=541`): `{"cmd":"dismiss_modal","event":"ui_hide_queued","ok":true,"add_msg_rva":"0x962F50","count":1}` -> modal gone immediately (`menu_paused=0`, `menu_stack_depth=1`)!
2. **`septimus_cave` `22/24 -> 24/24 PASS` (`src/d3d12_bridge/shaders/RawMaterials3D.hlsli`, `src/d3d12_bridge/shaders/Lighting.hlsl`, `tools/live/visual_heuristics.py`):**
   - **Glacial albedo physical PBR soft-shoulder:** After Wave 67 stripped vanilla SSAO, un-darkened cave ice walls clipped `G/B` hard against `1.000` (`above_094_frac=0.2691`). Added a chroma-preserving dielectric soft-shoulder in `evalFrostGlacier3D` (`RawMaterials3D.hlsli:397-405`) and `Lighting.hlsl:1226-1232` so bright ice highlights roll smoothly below `0.88` sRGB without dulling deep translucent sapphire-cyan tones (`above_094_frac: 0.2691 -> 0.0`, `albedo_mean_lum: 0.6437 -> 0.4099`, PASS!).
   - **1080p crosshair HUD mask fix:** Pixel inspection proved all `265` (`53 x 5`) `septimus_cave` `#24` ghosting pixels sat at `x=[945..974], y=[524..555]` (Skyrim's 32x32 center crosshair tips sticking outside `_hud_valid_mask`'s `half=9` cap). Raised `_hud_valid_mask`'s cap to `half=20` (`40x40` at 1080p = `0.08%` of screen, unchanged on small test frames), dropping `septimus_cave` ghosting edge pixels from `265 -> 0` (PASS!).
3. **Production TODO Cleanups (`src/core/RmlD3D11.cpp`, `src/core/AtmosphereRenderer.cpp`, `src/core/VolumetricClouds.cpp`):**
   - `RmlD3D11.cpp:550`: Implemented real RGBA file texture loading via `../vendor/stb_image.h` (`stbi_load` -> `GenerateTexture` -> `stbi_image_free`, with fail-closed `dims = {0, 0}` and warning log on missing/invalid files).
   - `AtmosphereRenderer.cpp:1017` & `VolumetricClouds.cpp:783`: Replaced stale TODO comments (and removed em dash on `VolumetricClouds.cpp:757`) with accurate architectural notes pointing to active native DX12 passes (`LightFog.hlsl`/`EnvCubePass.hlsl` and `RawVolCloudsPass.hlsl`), plus `(void)ctx;` in `ExecuteSkyPass`.

## 3. Exact Wave 67 -> Wave 68 Per-Scene Heuristic Deltas (`live_w68_quick_v4`)

| Scene | Interior | Wave 67 Passed | Wave 68 Passed | Key Metric Deltas (Wave 67 -> Wave 68) | Remaining Flagged Items |
| :--- | :--- | :---: | :---: | :--- | :--- |
| **`inn_interior`** | Yes | `24 / 24` | **`24 / 24` (PASS)** | Sustained 100% clean (`defects=[]`). `mv: +mv (0.03489 vs 0.06951 zero / 0.08004 -mv)`; `world_lock: 1.4348`; `normLen: 0.9848`; `shd_boil: 0.01865`; `dark_outline: 0 px` | **None** |
| **`septimus_cave`** | Yes | `22 / 24` | **`24 / 24` (PASS)** | **`+2`, SECOND 100% CLEAN LIVE SCENE (`defects=[]`)!** `albedo > 0.94`: `0.2691 -> 0.0` (PASS); `edge_ghost`: `265 px -> 0 px` (PASS); `mv: +mv (0.01226 vs 0.03366 zero / 0.04448 -mv)`; `world_lock: 2.6440`; `normLen: 0.9911`; `shd_boil: 0.00300` | **None** |
| **`riverwood_day`** | No | `18 / 24` | **`22 / 24` (`+4`)** | Survival modal dismissed in-process (`0x962F50`, `count=1`)! `dark_outline`: `0.01618 -> 0.000018 (PASS)`; `normLen`: `0.2634 -> 0.0513 bad frac (0.949, PASS)`; `mv`: `zero -> +mv (0.08386 vs 0.15519 zero / 0.17286 -mv, PASS)`; `edge_ghost`: `0.3950 -> 0.0986 (PASS)`; `world_lock`: `0.0786 -> 1.1890 (PASS)` | `detect_depth_and_stencil_integrity` (`depth_dynamic_range=0.0779` after cam_sweep pitched down toward road cobblestones), `analyze_shadow_temporal_stability` (`mean_pair_boil_std=0.14132`, `max_global_shadow_jump=0.00291`) |
| **`windhelm_ext_snow`** | No | `18 / 24` | **`23 / 24` (`+5`)** | Survival modal dismissed in-process (`0x962F50`, `count=1`)! `dark_outline`: `0.01080 -> 0.0 (PASS)`; `normLen`: `0.20895 -> 0.11638 bad frac (0.9165, PASS)`; `mv`: `zero -> +mv (0.04997 vs 0.06315 zero / 0.06825 -mv, PASS)`; `edge_ghost`: `0.57825 -> 0.06551 (PASS)`; `world_lock`: `0.0381 -> 1.2638 (PASS)`; `water_flow`: PASS; `albedo`: PASS (`0.0`) | `analyze_shadow_temporal_stability` (`mean_pair_boil_std=0.14837`, `max_global_shadow_jump=0.00220`) |

## 4. Direct Visual Inspection Findings (`live_w68_quick_v4`)

Inspected directly via `read_file` on 1920x1080 PNGs across all 4 scenes:

- **`riverwood_day_audit_improved_f00.png`:** Zero Survival Mode dialog! Crisp timber sawmill, wooden bridge, pine canopy, distant snowy mountain ridge, and detailed mossy cobblestone road with natural sunlit foliage.
- **`riverwood_day_audit_water_flow_diag_f00.png`:** Zero magenta pixels; shows the downward-pitched cobblestone ground after 16 steps of positive `dpitch` during `cam_sweep` (explaining why `detect_depth_and_stencil_integrity` saw `0.0779` range on that later step).
- **`inn_interior_audit_improved_f00.png`:** Rich warm hearth illumination, crisp walking NPC Embry with zero dark silhouette outline, zero indoor black orbs.
- **`septimus_cave_audit_improved_f00.png` & `septimus_cave_audit_view_albedo_f00.png` (`v1`):** Deep translucent sapphire-cyan crystalline ice walls with smooth conchoidal bowl shading and zero blown white-cyan highlight clipping.
- **`windhelm_ext_snow_audit_improved_f00.png`:** Zero Survival Mode dialog! Crisp Windhelm gatehouse stonework, snowdrift cobblestones, warm Planck fire braziers, and guards on the steps with zero sky bleed.

## 5. Wave 69 Recommended Targets (Path to 96/96 = 4/4 Clean Scenes!)

Now that all 4 scenes are unpaused and moving cleanly with `+mv` reprojection verified everywhere, ONLY two clear technical items remain to hit **4/4 (`96/96`)**:
1. **Exterior moving-camera shadow temporal stability (`riverwood_day` `0.1413` & `windhelm_ext_snow` `0.1484` vs `0.055` threshold):** Add temporal reprojection/history accumulation (or view-independent world-snapped texel quantization + wider bilateral filter on `RtShadow.hlsl` / `Lighting.hlsl` exterior `rv.x` sun shadow) so exterior sun shadow penumbra edges stop boiling under `stride=3` camera motion!
2. **Zero pitch drift during long 16-step `cam_sweep` (`full_pipeline_audit.py`):** Setting `cam_sweep` `dpitch = 0.0` (pure horizontal yaw sweep instead of accumulating downward pitch over 16 steps) prevents `riverwood_day` from staring straight down at cobblestones by step 12, keeping full exterior sky/mountain depth range (`> 0.15`) and keeping the Riverwood stream centered in `water_flow_diag`!

## 6. Key Artifact & Frame Paths

- Live Audit Report JSON: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w68_quick_v4\full_pipeline_report.json`
- Live Audit Report Markdown: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w68_quick_v4\full_pipeline_report.md`
- Auto-Emitted Audit Receipt: `C:\Users\Zain\.gemini\antigravity\brain\f48af80e-75ef-4818-9363-ccd6c01d1033\scratch\live_w68_quick_v4\engine_receipt.json`
- Inspected Frames:
  - `...\live_w68_quick_v4\riverwood_day\riverwood_day_audit_improved_f00.png` & `_water_flow_diag_f00.png`
  - `...\live_w68_quick_v4\inn_interior\inn_interior_audit_improved_f00.png`
  - `...\live_w68_quick_v4\septimus_cave\septimus_cave_audit_improved_f00.png`
  - `...\live_w68_quick_v4\windhelm_ext_snow\windhelm_ext_snow_audit_improved_f00.png`
