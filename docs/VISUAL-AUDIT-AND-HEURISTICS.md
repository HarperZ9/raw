# RAW Live Visual Audit, Custom Debug Layers (0..36 + Water 1..7) & 25-Heuristic Manual

This engineering manual documents:
1. Live Wave 63 in-world `SkyrimSE.exe` GPU backbuffer verification results (real 1920x1080 captures).
2. The complete 33-screenshot Wave 63 baseline playtest inventory (`2026-11-25 09:31:34.90` .. `09:38:46.52`).
3. All **37 Lighting Debug Views (`0..36`)** and **7 Water Debug Views (`1..7`)**.
4. All **25 Automated Full-Pipeline Visual, Temporal Motion-Vector & Telemetry Heuristics** in `tools/live/visual_heuristics.py`.
5. The **10-Scene Headless Multi-Frame Burst Auditor** (`tools/live/full_pipeline_audit.py`, `burst=6`, `stride=3`) and upgraded `tools/live/playtest_tour.py`.

All heuristics run in pure Python + Pillow + NumPy (zero OpenCV/SciPy dependencies) and are
regression-verified by `python tests/test_visual_heuristics.py`.

---

## 1. Live In-World `SkyrimSE.exe` Verification Results (Wave 63 & Wave 64 10-Scene 640-Frame Audit)

Real `1920x1080` GPU backbuffer captures obtained via `sStartingCell=` + `raw_harness_go.txt`:

- **PASS - Wave 63/64 Confirmed Fixed Live across all 10 Scenes (640 frames):**
  - ZERO `NaN`/`Inf`/`HDR > 32.0` overflow pixels across all 10 scenes (`view_nan_hdr` 100% clean).
  - Bleached white pine needles FIXED across exteriors (`view_flora_trees` clean).
  - Indoor pitch-black soot orbs & floating neon fireballs 100% ELIMINATED (`inn_interior` & `septimus_cave` clean).
  - `base == 14u` Skinned Actors / Creatures cleanly isolated (`view_full_classes` orange NPCs, `view_moss_mask` ZERO actor violations).
  - `high_hrothgar` alpine snow & sky verified clean.
- **Wave 65 Live Moving-Camera Audit (`live_w65_quick`, 384 frames) Findings Fixed in Wave 66:**
  1. **`GameComposite` Structural Interception:** Wave 65 proved vanilla 2011 SSAO + vanilla fog ran over RAW frames & debug views -> intercepted structurally in `GameComposite.cpp` & forced off on all Debug Views in `BridgeHookLighting.cpp` (plus skipping `RtWater` on Lighting Debug Views `1..36`).
  2. **Headless Modal Auto-Dismiss (`dismiss_modal`) & Direct `set_time <hour>`:** Creation Club Survival Mode modal popped mid-capture and `bat` did not update `game_hour` -> `full_pipeline_audit.py` & `playtest_tour.py` now emit `set_time <hour>` and `dismiss_modal` immediately after `wait_for_world_ready`, a second `dismiss_modal` right before `raw_harness_go.txt`, and periodic `dismiss_modal` if `menu_paused != 0`.
  3. **`burst_stride` MV Scaling & HUD Masking:** `audit_scene_bundle(..., burst_stride=3)` forwards `burst_stride` into `#21/#22/#23/#24/#25` (multiplying 1-frame GPU `mv` by `burst_stride`), masks static HUD regions (`y < 0.11 * H` compass bar and 18px center crosshair box) in `#14` and `#21/#23/#24/#25`, subtracts `burst_stride * mv` camera pan in `#22` water flow speed (skipping interiors & dry `WaterView=7 < 0.5%`), filters warm stone walls via `view_depth_edge` sky mask in `#2`, and uses `0.025` min depth range for enclosed interiors (`is_interior=True`) in `#19`.

---

## 2. Baseline Playtest Inventory (33 Live Skyrim SE Screenshots)

Source directory: `C:\Users\Zain\Videos\NVIDIA\The Elder Scrolls V Skyrim Special Edition\` (`1920x1080`).

| # | Screenshot Timestamp | Observed Defect | Baseline Heuristic Flags | Owning Track & Root Cause |
| :--- | :--- | :--- | :--- | :--- |
| 01 | `09.31.34.90.png` | Water scanline holes | `detect_water_scanline_holes` (0.0910), `detect_dark_motion_outline` | `RtWater.hlsl` hard binary `isWaterPx` gate on quantized bed depth |
| 02 | `09.31.36.47.png` | Water scanline holes | `detect_water_scanline_holes`, `detect_dark_motion_outline` | `RtWater.hlsl` shallow shoreline binary cutout |
| 03 | `09.31.46.94.png` | Crushed pitch-black tree trunks | `detect_crushed_black_tree_trunks`, `detect_dark_motion_outline` | `Lighting.hlsl` `isWoodBark` ambient floor crush |
| 04 | `09.31.51.89.png` | Crushed conifer bark / silhouette rim | `detect_dark_motion_outline` | `Rawlss5Pass.hlsl` cavity darkening + bark shadow crush |
| 05 | `09.31.53.38.png` | Crushed tree trunks + shoreline scanlines | `detect_water_scanline_holes`, `detect_actor_green_moss_chroma`, `detect_dark_motion_outline` | Water + bark + foliage lighting |
| 06 | `09.31.58.47.png` | Riverwood water scanline cutouts | `detect_water_scanline_holes`, `detect_dark_motion_outline` | `RtWater.hlsl` shoreline feathering |
| 07 | `09.32.17.79.png` | Bleached chalky pine needles | Visual + DebugView `34` target | `Lighting.hlsl` foliage wrap/frost bleach |
| 08 | `09.32.32.11.png` | Dark cel-shading motion outline | `detect_dark_motion_outline` (0.00567), `detect_water_scanline_holes` | `Rawlss5Pass.hlsl` negative `cavityScale` + depth dilation |
| 09 | `09.32.42.68.png` | Floating neon fireball + red sky bleed | `detect_red_sky_fire_bleed` (0.2044) | `RawFluidSimHost` ungrounded emitter + Gaussian ball + 850K cutoff (FIXED W63) |
| 10 | `09.32.47.97.png` | Floating neon fireball + red sky bleed | `detect_red_sky_fire_bleed` (0.2553) | `RawFluidFireComposite.hlsl` sky bleed (FIXED W63) |
| 11 | `09.33.13.72.png` | Overcast sky reference | clean | Baseline reference |
| 12 | `09.33.23.72.png` | Red sky fire halo bleed | `detect_red_sky_fire_bleed` (0.0456) | `RawFluidFireComposite.hlsl` + `LightFog.hlsl` (FIXED W63) |
| 13 | `09.33.34.46.png` | Severe red sky fire halo bleed | `detect_red_sky_fire_bleed` (0.2047) | `RawFluidFireComposite.hlsl` 1180K Planck floor (FIXED W63) |
| 14 | `09.33.40.32.png` | Riverwood hearth approach | clean | Emitter grounding reference |
| 15 | `09.33.46.31.png` | Green moss painted on Stump the dog | `detect_actor_green_moss_chroma` (0.2504), `detect_dark_motion_outline` | Missing `base == 14u` skinned creature class + stacked moss passes (FIXED W63) |
| 16 | `09.33.51.59.png` | Frodnar blotchy skin, mossy gloves, outline | Actor portrait inspection | `base == 14u` + `Sss.hlsl` + `AoComposite.hlsl` (FIXED W63) |
| 17 | `09.33.59.22.png` | Forge fire blob | Hearth fire inspection | Grounded turbulent flame tongues (FIXED W63) |
| 18 | `09.34.31.13.png` | Flat beige cloud polygon | Sky inspection | `LightFog.hlsl` / `Clouds.hlsl` sky-cloud mesh depth fallback |
| 19 | `09.34.42.78.png` | Shoreline scanlines + mossy foreground rock | `detect_water_scanline_holes`, `detect_actor_green_moss_chroma`, `detect_dark_motion_outline` | Water + moss balance |
| 20 | `09.34.46.64.png` | Shoreline transition | clean | Optical-depth feathering |
| 21 | `09.34.56.50.png` | Jagged horizontal water scanline holes | `detect_water_scanline_holes` (0.0939), `detect_dark_motion_outline` | `RtWater.hlsl` binary depth step |
| 22 | `09.35.44.84.png` | Periodic zebra-stripe water ripples | `detect_water_zebra_stripes` (peak=24.82, aniso=1.636) | `RtWater.hlsl` stretched `pStr` / `vWake` / `footTrail` sin rings |
| 23 | `09.36.08.92.png` | Giant indoor pitch-black concentric orbs | `detect_indoor_black_orbs` (1.0000) | Trapped cold indoor soot `trans -> 0.015` (FIXED W63 LIVE VERIFIED) |
| 24 | `09.36.10.05.png` | Flat angular blue ice-cave triangles | Glacial shader inspection | `evalFrostGlacier3D` multi-layer refraction (FIXED W63 LIVE VERIFIED) |
| 25 | `09.36.11.18.png` | Giant indoor pitch-black concentric orbs | `detect_indoor_black_orbs` (1.0000) | Indoor soot `trans >= 0.78f` clamp (FIXED W63 LIVE VERIFIED) |
| 26 | `09.36.24.26.png` | Glacial ice cave facets | Glacial shader inspection | Conchoidal fracture normals (FIXED W63 LIVE VERIFIED) |
| 27 | `09.36.29.23.png` | Indoor soot orb + DOF concentric circle | DOF/fire inspection | `DofPass.hlsl` indoor near-field bokeh gather |
| 28 | `09.36.50.34.png` | Periodic wave banding on lake surface | `detect_water_zebra_stripes` | `RtWater.hlsl` isotropic C2 wave spectra |
| 29 | `09.37.13.57.png` | Diagonal blue sky strip + L-shaped cutout | `detect_sky_diagonal_cutout` (ratio=0.822, -12.0 deg) | `LightFog.hlsl` sky-mesh branch + `Clouds.hlsl` reprojection seam |
| 30 | `09.37.21.24.png` | Shadow flicker pair A + flat brown hillside | Pair-flagged with `09.38.46.52.png` (shift=0.280, p95=0.300) | World-space normal bias, RT slot smoothing & hillside shading |
| 31 | `09.37.24.73.png` | Distant river scanline cutout | Shoreline inspection | Continuous optical-depth coverage alpha |
| 32 | `09.37.27.54.png` | Exterior landscape transition | clean | Live world-space snow/grass/caustics composite |
| 33 | `09.38.46.52.png` | Shadow flicker pair B (camera turn) | Pair-flagged with `09.37.21.24.png` (shift=0.280, p95=0.300) | `ShadowAtlas.hlsl`, `RtShadow.hlsl`, `RtSkyFilter.hlsl`, `LightingCommon.hlsli` |

---

## 3. Complete Custom Debug Layer Reference (`DebugView 0..36` & `WaterView 1..7`)

### 3.1 Lighting & Full-Pipeline Debug Views (`flags.w = 0..36`)

| ID | Harness Step | Name | Purpose & Alarm Encoding |
| :--- | :--- | :--- | :--- |
| `0` | `improved` | `FinalLit` | Production PBR + RT + volumetric composite |
| `1` | `view_normals` | `WorldNormals` | World-space unit normals `n * 0.5 + 0.5` |
| `2` | `view_sun_shadow` | `SunShadow` | Directional sun shadow visibility `[0..1]` |
| `3` | `view_ambient` | `AmbientLight` | Indirect ambient irradiance |
| `4` | `view_sky_vis` | `SkyVisibility` | Hemispheric RT sky visibility |
| `5` | `view_gi_bounce` | `RtBounceGI` | Ray-traced multi-bounce GI radiance |
| `6` | `view_rt_refl` | `EnvMapFinal` | Environment / RT reflection output |
| `7` | - | `RtPrimaryRefl` | Primary-ray reflection buffer |
| `8` | `view_weather` | `WeatherWetPuddleSnow` | `R=wet, G=puddle, B=snow` |
| `9` | - | `SplitScreenCompare` | Half-screen split comparison vs vanilla |
| `10` | - | `SkyOverlayMask` | Magenta highlight of sky hemisphere pixels |
| `11` | `view_cloud_shadow` | `CloudShadow` | Volumetric cloud shadow projection |
| `12` | `view_classes` | `MaterialClasses` | Legacy G-buffer material class map |
| `13` | - | `DirectPlusAmbient` | Total incoming surface irradiance |
| `14` | - | `UntracedRestVis` | Ray-traced visibility share for local lights |
| `15` | - | `GBufferAlbedoRaw` | Raw G-buffer albedo target |
| `16` | `view_albedo` | `GBufferBaseAlbedo` | Normalized PBR base albedo `[0.02..0.92]` |
| `17` | - | `TonemapRatio` | Game output to knee-compressed radiance ratio |
| `18` | - | `EnvReflection` | Specular cubemap reflection contribution |
| `19` | `view_char_light` | `CharacterLight` | Rim / character fill light |
| `20` | - | `LuminanceHeat` | False-color linear luminance heatmap |
| `21` | - | `ExposureHeat` | False-color auto-exposure heatmap |
| `22` | `view_wireframe` | `WaterDepthMask` | Water / geometry depth diagnostic |
| `23` | - | `WaterNormalMask` | Water surface normal diagnostic |
| `24` | `view_roughness` | `ShadowCascadeId` | Cascade split / roughness diagnostic |
| `25` | `view_metallic` | `MotionVectorMag` | Motion vector / metallic diagnostic |
| `26` | `view_parallax` | `RoughnessMap` | Surface roughness / parallax diagnostic |
| `27` | `view_gi_contrast` | `SpecularEnergy` | Specular / GI contrast diagnostic |
| `28` | `view_full_classes` | `FullClassPlusActor14` | **Wave 63**: Full class ID map with vivid orange **`base == 14u` Skinned Actor / Creature / Fur / Clothing** |
| `29` | `view_depth_edge` | `LinearDepthSilhouette` | **Wave 63**: Linearized world depth + silhouette discontinuity map |
| `30` | `view_shadow_stab` | `ShadowStabilityAndRtSlots` | **Wave 63**: Shadow cascade stability + top-3 RT point-light slots |
| `31` | `view_moss_mask` | `MossSnowGlacierMask` | **Wave 63**: Weathering mask (`G=moss, B=snow, R=glacier`) |
| `32` | `view_skin_sss` | `ActorSkinSssContactAo` | **Wave 63**: Actor skin wrap, SSS diffusion & contact AO |
| `33` | `view_nan_hdr` | `NanInfHdrOverflowHeatmap` | **Wave 63**: Vivid magenta alarm on any `NaN`, `Inf`, or `> 32.0` HDR pixel (ZERO allowed!) |
| `34` | `view_flora_trees` | `flora_trees` | **Wave 64**: Flora/bark audit (`green=base5`, `lime=pine`, `amber=bark`, **RED=crushed bark <0.015**, **MAGENTA=bleached non-snow foliage**) |
| `35` | `view_motion_outline` | `motion_outline` | **Wave 64**: Motion vector, disocclusion & `Rawlss5` cel-outline risk map |
| `36` | `view_fire_froxel` | `fire_froxel` | **Wave 64**: Fire/soot indoor transmittance compliance (`green >= 0.78`, `red < 0.78`), Froxel opacity & volumetric shafts |

### 3.2 Water Debug Views (`WaterView = 1..7`)

| ID | Harness Step | Name | Purpose |
| :--- | :--- | :--- | :--- |
| `1` | `water_refl` | `water_refl` | Planar / RT water reflection radiance |
| `2` | `water_caustics` | `water_caustics` | Submerged riverbed Snell caustic projection |
| `3` | `water_fresnel` | `water_fresnel` | Schlick Fresnel grazing reflectance factor |
| `4` | `water_depth` | `water_depth` | Continuous optical water depth mask |
| `5` | `water_normals` | `water_normals` | Multi-scale gravity + fluid sim surface normals |
| `6` | `water_body` | `water_body` | Beer-Lambert Nordic emerald-slate water body tint |
| `7` | `water_flow_diag` | `water_flow_diag` | **Wave 64**: `R=flowSpeed, G=depth/120, B=shoreAlpha`, **MAGENTA=shallow continuity alarm** |

---

## 4. All 25 Automated Visual, Temporal Motion-Vector & Telemetry Heuristics (`tools/live/visual_heuristics.py`)

| # | Heuristic | Target Input | Threshold / Gate | Planted-Failure Proof |
| :--- | :--- | :--- | :--- | :--- |
| 1 | `detect_indoor_black_orbs` | `improved` (`is_interior`) | `128x128` black density `>= 0.92` in lit interior (skipped when `is_interior=False`) | `09.36.08` = `1.000`, `09.36.11` = `1.000` |
| 2 | `detect_red_sky_fire_bleed` | `improved` | Upper sky red-dominant area `>= 0.025` | `09.33.23` = `0.0456`, `09.33.34` = `0.2047` |
| 3 | `detect_water_scanline_holes` | `improved` + opt `water_flow_diag` (`WaterView=7`) / `water_depth` | `WaterView=7` magenta `> 0.0005` (direct!) or RGB fallback `alt >= 0.060` & `scan >= 0.032` | `09.31.34` = `0.0910`, `09.34.56` = `0.0939`, View 7 magenta flagged |
| 4 | `detect_water_zebra_stripes` | `improved` | FFT mid-band peak `>= 12.0` & `dy/dx >= 1.25` | `09.35.44` peak = `24.82`, aniso = `1.636` |
| 5 | `detect_sky_diagonal_cutout` | `improved` | Radon collinear sky-seam width ratio `>= 0.28` | `09.37.13` ratio = `0.822` at `-12.0 deg` |
| 6 | `detect_actor_green_moss_chroma` | `improved` or DebugView `28`+`31` | RGB `>= 0.16` or DebugView actor/moss overlap `> 0.002` | `09.33.46` (Stump) = `0.2504`; DebugView leak = `0.200` |
| 7 | `detect_dark_motion_outline` | `improved` + opt DebugView `28`/`29` | Dark contour trough fraction `>= 0.0032` | `09.32.32` = `0.00567` |
| 8 | `detect_camera_turn_shadow_flicker` | Frame A vs Frame B | Abrupt shift `>= 0.18` or `p95 >= 0.24` | `09.37.21` vs `09.38.46`: `shift=0.280, p95=0.300` |
| 9 | `detect_nan_inf_hdr_overflow` | DebugView `33` | **0 magenta alarm pixels allowed** (`> 0` fails) | Planted 1px magenta flagged |
| 10 | `detect_crushed_black_tree_trunks` | `improved` or DebugView `34` | View 34 red alarm `>= 0.002` or RGB vertical bark crush `>= 0.035` | `09.31.46` + planted View 34 red alarm flagged |
| 11 | `detect_bleached_white_pine_needles` | `improved` or DebugView `34` | View 34 magenta alarm `>= 0.002` or RGB chalk canopy `>= 0.055` | Planted View 34 magenta alarm flagged |
| 12 | `detect_water_neon_cyan_oversat` | `improved` or `water_body` | Electric-cyan water pixel ratio `>= 0.12` | Planted cyan water flagged |
| 13 | `detect_flat_ice_cave_triangles` | `improved` | Flat `8x8` ice block (`std < 0.0055`) ratio `>= 0.72` | Planted flat blue ice triangles flagged |
| 14 | `detect_normal_map_integrity` | DebugView `1` | Unit-length error `> 0.12`, zero holes `> 0.08`, or degenerate std `< 0.03` | Planted normal black holes flagged |
| 15 | `detect_albedo_range_pbr_sanity` | DebugView `16` | Black `<0.015` `> 0.20`, white `>0.94` `> 0.14`, or mean out of `[0.03..0.80]` | Planted blown white albedo flagged |
| 16 | `detect_shadow_coverage_balance` | DebugView `2` or `30` | Neither 100% black (`mean < 0.02`) nor 100% flat white (`mean > 0.992, std < 0.015`) | Planted flat white shadow flagged |
| 17 | `detect_skin_sss_sanity` | DebugView `32` | Zero NaN magenta & harsh 1px skin step (`> 0.25`) `< 0.20` | Planted binary skin stripe spike flagged |
| 18 | `detect_exposure_and_tonemap_clipping` | `improved` | Crushed black `> 0.38`, blown white `> 0.18`, or mean out of `[0.025..0.88]` | Planted overexposed frame flagged |
| 19 | `detect_depth_and_stencil_integrity` | DebugView `29` | Depth `p95 - p05 >= 0.08` & zero-depth fraction `<= 0.85` | Planted flat depth buffer flagged |
| 20 | `detect_live_world_compositor_activity` | `boot_selfcheck.txt` + `bridge_live_telemetry.json` + `harness_results.jsonl` | `boot_selfcheck >= 11/12`, `d3d12DebugErrors == 0`, finite GPU solver timings `> 0` | Planted missing/corrupt telemetry flagged |
| 21 | `analyze_motion_vector_reprojection` | `frame_t`, `frame_t1`, DebugView `35` (`mv_debug35_t1`) | Stationary (`< 0.25 px`): low static RMSE; Moving (`>= 0.25 px`): `+mv`/`-mv`/`0` sign & RMSE `<= 0.14` | Planted inverted `-mv` flagged (`sign_inverted=True`) |
| 22 | `analyze_water_temporal_flow_speed` | Burst `improved` (`_f00.._f05`) + opt `water_depth` | Regularized GCC-PHAT FFT phase correlation on contiguous water patch: `[0.15 .. 4.50]` px/f | Planted frozen `0.0 px/f` AND frantic `8.0 px/f` flagged |
| 23 | `analyze_shadow_temporal_stability` | Burst `view_sun_shadow` + opt DebugView `35` MV | Motion-compensated boil std `<= 0.055`, global pop `<= 0.14`, flicker `<= 0.08` | Planted shadow boil/pop burst flagged |
| 24 | `analyze_temporal_ghosting_and_motion_outline` | Burst `improved` + DebugView `35` (`B > 0.15` divergence) | Moving-silhouette dark rim `<= 0.14` & trailing ghost RMSE `<= 0.12` | Planted moving dark rim & ghost streak flagged |
| 25 | `analyze_world_lock_anti_swimming` | Burst `improved` + DebugView `35` MV | Stationary (`< 0.25 px`): low static diff; Moving (`>= 0.25 px`): `screen/world RMSE >= 0.75` | Planted camera-swimming screen-locked texture flagged |

---

## 5. Wave 65 Headless Moving-Camera (`cam_sweep`) 10-Scene Auditor (`tools/live/full_pipeline_audit.py`)

Per-scene live protocol (zero Main-Menu focus lock, zero Survival Mode popup, zero loading-screen race):
1. Writes multi-line `raw_scene_init.txt` in the Skyrim SE folder (`set Survival_ModeEnabled to 0`, `stopquest ccQDRSSE001_SurvivalModeQuest`, `fw <weatherId>`, `set gamehour to <H>`) and sets `sStartingConsoleCommand=bat raw_scene_init` in `SkyrimCustom.ini`.
2. Launches `SkyrimSE.exe` with stale `raw_harness_go.txt` / `raw_harness_status.txt` cleared.
3. Polls `raw_harness_status.txt` until `cell != 1 && world >= 220`, queries `raw_engine_go.txt` `view` until `cell_id != 0 && menu_paused == 0`, then waits `4.0s` for black load fade-in completion.
4. Emits `cam_sweep 0.008 0.0012 0.0 240` to `raw_engine_go.txt` (default `--cam-sweep` ON, oscillating gently during long multi-step captures) and triggers `raw_harness_go.txt` (`burst=6`, `stride=3`, writing `*_f00.bmp .. *_f05.bmp` converted to PNGs).

```powershell
# Quick 4-scene live moving-camera burst audit (cam_sweep ON, burst=6, stride=3)
python tools/live/full_pipeline_audit.py --profile quick --burst 6 --stride 3 --out-dir .\w65_audit_quick

# Full 10-scene live moving-camera burst audit (all biomes, weather, TOD & interiors)
python tools/live/full_pipeline_audit.py --profile full --burst 6 --stride 3 --out-dir .\w65_audit_full

# Offline audit of an existing multi-scene capture folder
python tools/live/full_pipeline_audit.py --offline-dir .\captures --out-dir .\audit_out
```

Safety guarantees enforced in `finally:`:
- Bit-exact restoration of `SkyrimCustom.ini` via `SkyrimCustomIniGuard`.
- Removal of temporary `raw_scene_init.txt`, `raw_harness_go.txt`, `raw_harness_done.txt`, and `raw_engine_go.txt`.
- Clean process termination of `SkyrimSE.exe` between scenes.
- Removal of `raw_engine_out\boot_sentinel.state` on exit so automated audits never trigger a false SafeMode crash-loop rollover.
