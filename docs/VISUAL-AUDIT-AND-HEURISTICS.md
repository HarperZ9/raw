# RAW Live Visual Audit, Debug Layers & Automated Heuristics Manual

This document catalogs the 33 Wave 63 live Skyrim SE baseline playtest screenshots
(`2026-11-25 09:31:34.90` .. `09:38:46.52`), maps every observed defect to its exact shader or
runtime root cause, specifies the complete Debug Layer table (`0..33`), defines all 8 quantitative
visual defect heuristics implemented in `tools/live/visual_heuristics.py`, and tracks Wave 63+
remediation across all engineering tracks.

All heuristics are implemented in pure Python + Pillow + NumPy (`tools/live/visual_heuristics.py`),
integrated into the live harness (`tools/live/kinds.py`, `tools/live/playtest_tour.py`), and
regression-verified by `tests/test_visual_heuristics.py`.

---

## 1. Baseline Playtest Inventory (33 Live Skyrim SE Screenshots)

Source capture directory:
`C:\Users\Zain\Videos\NVIDIA\The Elder Scrolls V Skyrim Special Edition\`
All 33 baseline captures were recorded at `1920x1080` on 2026-11-25 (`09:31:34.90` to `09:38:46.52`).

| # | Screenshot Timestamp | Primary Defect Category | Heuristic Flags (Baseline) | Owning Track & Root Cause |
| :--- | :--- | :--- | :--- | :--- |
| 01 | `09.31.34.90.png` | Water scanline holes | `detect_water_scanline_holes` (0.0910), `detect_dark_motion_outline` | Track 2 (`RtWater.hlsl:289` hard binary `isWaterPx` boolean on quantized bed depth) |
| 02 | `09.31.36.47.png` | Water scanline holes | `detect_water_scanline_holes`, `detect_dark_motion_outline` | Track 2 (`RtWater.hlsl` shallow shoreline binary cutout) |
| 03 | `09.31.46.94.png` | Crushed pitch-black tree trunks | `detect_dark_motion_outline` | Track 4 (`DistantLod.hlsli` & conifer bark shadow crush) |
| 04 | `09.31.51.89.png` | Crushed conifer bark / silhouette rim | `detect_dark_motion_outline` | Track 3 & Track 4 (`Rawlss5Pass.hlsl` cavity darkening + bark shadow lift) |
| 05 | `09.31.53.38.png` | Crushed tree trunks + shoreline scanlines | `detect_water_scanline_holes`, `detect_actor_green_moss_chroma`, `detect_dark_motion_outline` | Track 2, Track 3 & Track 4 |
| 06 | `09.31.58.47.png` | Riverwood water scanline cutouts | `detect_water_scanline_holes`, `detect_dark_motion_outline` | Track 2 (`RtWater.hlsl` hard binary water gate) |
| 07 | `09.32.17.79.png` | Bleached pine needles | clean (chroma/exposure visual inspection) | Track 4 (`DistantLod.hlsli` boreal chlorophyll & needle translucency) |
| 08 | `09.32.32.11.png` | Dark cel-shading motion outline | `detect_dark_motion_outline` (0.00567), `detect_water_scanline_holes` | Track 3 (`Rawlss5Pass.hlsl` negative `cavityScale` + `bestVelPx` dilation) |
| 09 | `09.32.42.68.png` | Floating blurry neon fireball + red sky bleed | `detect_red_sky_fire_bleed` (0.2044) | Track 1 & Track 6 (`RawFluidSimHost` ungrounded emitter + Gaussian sphere + 850K cutoff) |
| 10 | `09.32.47.97.png` | Floating neon fireball + red sky bleed | `detect_red_sky_fire_bleed` (0.2553) | Track 1 & Track 6 (`RawFluidFireComposite.hlsl` red halo bleed onto sky) |
| 11 | `09.33.13.72.png` | Overcast sky / distant horizon reference | clean | Baseline exterior reference |
| 12 | `09.33.23.72.png` | Red sky fire halo bleed | `detect_red_sky_fire_bleed` (0.0456) | Track 1 & Track 4 (`RawFluidFireComposite.hlsl` + `LightFog.hlsl` `skyFireVol`) |
| 13 | `09.33.34.46.png` | Severe red sky fire halo bleed | `detect_red_sky_fire_bleed` (0.2047) | Track 1 & Track 4 (`RawFluidFireComposite.hlsl` sky `tMax` clamp + 1180K Planck floor) |
| 14 | `09.33.40.32.png` | Riverwood hearth approach | clean | Track 1 emitter grounding target |
| 15 | `09.33.46.31.png` | Green moss painted on Stump the dog | `detect_actor_green_moss_chroma` (0.2504), `detect_dark_motion_outline` | Track 3 & Track 8 (missing `base == 14u` skinned creature class + 4 stacked moss passes) |
| 16 | `09.33.51.59.png` | Frodnar blotchy skin, mossy gloves, dark outline | visual actor inspection (dark interior) | Track 3 & Track 8 (`base == 14u` actor clothing + `Sss.hlsl` terminator + `AoComposite.hlsl`) |
| 17 | `09.33.59.22.png` | Forge fire blob | visual fire inspection | Track 1 & Track 6 (hearth emitter grounding & turbulent flame tongues) |
| 18 | `09.34.31.13.png` | Flat beige cloud polygon | visual sky inspection | Track 4 (`LightFog.hlsl` & `Clouds.hlsl` vanilla sky-cloud mesh depth fallback) |
| 19 | `09.34.42.78.png` | Shoreline scanlines + mossy foreground rock | `detect_water_scanline_holes`, `detect_actor_green_moss_chroma`, `detect_dark_motion_outline` | Track 2 & Track 3 |
| 20 | `09.34.46.64.png` | Shoreline transition | clean | Track 2 continuous optical-depth feathering |
| 21 | `09.34.56.50.png` | Jagged horizontal water scanline holes | `detect_water_scanline_holes` (0.0939), `detect_dark_motion_outline` | Track 2 (`RtWater.hlsl:289` hard depth step) |
| 22 | `09.35.44.84.png` | Periodic zebra-stripe water ripples | `detect_water_zebra_stripes` (peak=24.82, aniso=1.636) | Track 2 (`RtWater.hlsl` stretched `pStr`, `vWake`, concentric `footTrail` sin rings) |
| 23 | `09.36.08.92.png` | Giant indoor pitch-black concentric orbs | `detect_indoor_black_orbs` (1.0000) | Track 1 & Track 6 (`RawFluidFireComposite.hlsl` cold indoor soot `trans -> 0.015`) |
| 24 | `09.36.10.05.png` | Flat angular blue ice-cave triangles | clean (glacier shading target) | Track 2 (`RawMaterials3D.hlsli::evalFrostGlacier3D` multi-layer glacial depth) |
| 25 | `09.36.11.18.png` | Giant indoor pitch-black concentric orbs | `detect_indoor_black_orbs` (1.0000) | Track 1 & Track 6 (indoor soot dissipation & `trans >= 0.78f` clamp) |
| 26 | `09.36.24.26.png` | Glacial ice cave wall facets | clean (glacier shading target) | Track 2 (`evalFrostGlacier3D` Snell refraction & micro-bubble parallax) |
| 27 | `09.36.29.23.png` | Indoor soot orb + DOF concentric circle | visual DOF/fire inspection | Track 1 (`DofPass.hlsl` indoor near-field bokeh gather + soot clamp) |
| 28 | `09.36.50.34.png` | Periodic wave banding on lake surface | `detect_water_zebra_stripes` | Track 2 (`RtWater.hlsl` isotropic aperiodic C2 wave spectrum) |
| 29 | `09.37.13.57.png` | Diagonal blue sky strip + L-shaped white cutout | `detect_sky_diagonal_cutout` (ratio=0.822, angle=-12.0 deg) | Track 4 (`LightFog.hlsl` sky-mesh branch + `Clouds.hlsl` reprojection seam) |
| 30 | `09.37.21.24.png` | Shadow flicker pair A + flat brown hillside | pair-flagged with `09.38.46.52.png` (shift=0.280, p95=0.300) | Track 3 & Track 4 (world-space normal bias, RT slot smoothing, hillside shading) |
| 31 | `09.37.24.73.png` | Distant river scanline cutout | visual shoreline inspection | Track 2 (`RtWater.hlsl` optical-depth coverage alpha) |
| 32 | `09.37.27.54.png` | Exterior landscape transition | clean | Track 6 live world-space snow/grass/caustics composite |
| 33 | `09.38.46.52.png` | Shadow flicker pair B (camera turn) | pair-flagged with `09.37.21.24.png` (shift=0.280, p95=0.300) | Track 3 & Track 4 (`ShadowAtlas.hlsl`, `RtShadow.hlsl`, `RtSkyFilter.hlsl`, `LightingCommon.hlsli`) |

---

## 2. Complete RAW Debug Layer Reference (`DebugView` / `flags.w = 0..33`)

Hotkeys in live Skyrim SE:
- `F9`: Toggle RAW Telemetry / Performance HUD
- `F10`: Toggle Debug Layer Drawer
- `F11`: Cycle Core Debug Views (`0, 28, 29, 30, 31, 32, 33, 1, 2, 8`)
- Harness command: `debug_view` (`{"view": N, "capture": true}`) via `tools/live/kinds.py`

| ID | Name | Purpose & Interpretation |
| :--- | :--- | :--- |
| `0` | `FinalLit` | Standard production PBR + RT + volumetric composite output |
| `1` | `WorldNormals` | World-space surface normals mapped `n * 0.5 + 0.5` |
| `2` | `SunShadow` | Directional sun shadow visibility factor (0 = occluded, 1 = sunlit) |
| `3` | `AmbientLight` | Indirect ambient irradiance term |
| `4` | `SkyVisibility` | Hemispheric RT sky visibility (`RtIdx.w` / `skyVis`) |
| `5` | `RtBounceGI` | Ray-traced multi-bounce global illumination radiance |
| `6` | `EnvMapFinal` | Cubemap environment reflection lookup |
| `7` | `RtPrimaryRefl` | Primary-ray reflection buffer (`RtRefl`) |
| `8` | `WeatherWetPuddleSnow` | Weather mask (`R=wet`, `G=puddle`, `B=snow`) |
| `9` | `SplitScreenCompare` | Half-screen split comparison against vanilla backbuffer |
| `10` | `SkyOverlayMask` | Magenta highlight of pixels classified as sky hemisphere |
| `11` | `CloudShadow` | Volumetric cloud shadow projection on terrain |
| `12` | `MaterialClasses` | Legacy G-buffer material class map |
| `13` | `DirectPlusAmbient` | Total incoming surface irradiance (`diff + amb`) |
| `14` | `UntracedRestVis` | Ray-traced visibility share for untraced local lights |
| `15` | `GBufferAlbedoRaw` | Raw G-buffer albedo target (`Albedo.Load`) |
| `16` | `GBufferBaseAlbedo` | Post-normalization base albedo consumed by `Lighting.hlsl` |
| `17` | `TonemapRatio` | Ratio of game output to knee-compressed lit radiance |
| `18` | `EnvReflection` | Specular environment reflection contribution |
| `19` | `CharacterLight` | Rim/character fill light factor |
| `20` | `LuminanceHeat` | False-color linear luminance heatmap |
| `21` | `ExposureHeat` | False-color auto-exposure heatmap |
| `22` | `WaterDepthMask` | Water optical depth mask |
| `23` | `WaterNormalMask` | Water surface normal diagnostic |
| `24` | `ShadowCascadeId` | Cascade split index visualization (0..3) |
| `25` | `MotionVectorMag` | Screen-space motion vector magnitude |
| `26` | `RoughnessMap` | Effective surface roughness (including Toksvig normal AA) |
| `27` | `SpecularEnergy` | Specular lobe radiance energy |
| `28` | `FullClassPlusActor14` | **Wave 63**: Vivid per-class map including new **`base == 14u` Skinned Actor / Creature / Fur / Clothing** |
| `29` | `LinearDepthSilhouette` | **Wave 63**: Linearized world depth + silhouette discontinuity edge detector |
| `30` | `ShadowStabilityAndRtSlots` | **Wave 63**: Shadow cascade stability + top-3 RT point-light slot assignment |
| `31` | `MossSnowGlacierMask` | **Wave 63**: Weathering mask (`G=moss`, `B=snow`, `R=glacier`) to catch zero-tolerance leakage onto actors |
| `32` | `ActorSkinSssContactAo` | **Wave 63**: Actor skin wrap, SSS diffusion, specular & contact AO inspector |
| `33` | `NanInfHdrOverflowHeatmap` | **Wave 63**: Vivid magenta alarm on any `NaN`, `Inf`, or `> 32.0` HDR energy pixel |

---

## 3. Automated Visual Heuristics Specification & Planted-Failure Proofs

Implemented in `tools/live/visual_heuristics.py` and verified in `tests/test_visual_heuristics.py`:

| # | Detector Function | Mathematical Feature | Threshold | Baseline Planted-Failure Proof | Clean Control Score |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `detect_indoor_black_orbs` | Integral-image max pitch-black (`lum < 0.015`) `128x128` box density inside central 76%x80% ROI, gated on lit surround (`bright_frac >= 0.35`) | `>= 0.92` | `09.36.08.92.png` = `1.000`, `09.36.11.18.png` = `1.000` (PASS detect) | `0.000` |
| 2 | `detect_red_sky_fire_bleed` | Area fraction of upper sky (`y in [6%..45%]`) with `(R > 0.42) & (R - B > 0.22) & (R > 1.18*G)` | `>= 0.025` | `09.33.23.72.png` = `0.0456`, `09.33.34.46.png` = `0.2047` (PASS detect) | `0.000` |
| 3 | `detect_water_scanline_holes` | Inside blue/cyan water pixels (`y in [35%..92%]`): row-to-row vertical sign-flip ratio (`d1[y]*d1[y-1] < -0.0015`) AND horizontal-band step ratio (`|dy| > 0.07 & |dx| < 0.04`) | `alt >= 0.060` and `scan >= 0.032` | `09.31.34.90.png` = `0.0910`, `09.34.56.50.png` = `0.0939` (PASS detect) | `0.000` |
| 4 | `detect_water_zebra_stripes` | 1D FFT mid-band spectral peak-to-median ratio (periods 6..40 px) along Y/X inside water-dominant lower ROI (`water_frac >= 0.25`), gated by `dy/dx >= 1.25` and `mean(|dy|) >= 0.006` | `peak >= 12.0` | `09.35.44.84.png` = `24.82` (`dy/dx = 1.636`) (PASS detect) | `0.000` |
| 5 | `detect_sky_diagonal_cutout` | Pure-NumPy Radon/Hough line accumulator across `[-36..+36 deg]` (1.5 deg steps) on bright-sky-to-blue-sky chroma jumps (`|grad(B - R)| > 0.085`), measuring max collinear distinct X-column width ratio | `>= 0.28` | `09.37.13.57.png` = `0.822` at `-12.0 deg` (1/33 surgical hit!) | `0.000` |
| 6 | `detect_actor_green_moss_chroma` | Dual-mode: (a) RGB central actor ROI (`y in [35%..88%], x in [25%..75%]`) moss-green fraction; (b) DebugView 28 actor pixels overlapping DebugView 31 green moss mask (`> 0.002`) | RGB `>= 0.16`, Debug `> 0.002` | `09.33.46.31.png` (Stump) = `0.2504`; DebugView leak = `0.2000` vs clean `0.0000` | `0.000` |
| 7 | `detect_dark_motion_outline` | Fraction of interior pixels (`lum < 0.08`) bracketed by brighter pixels (`+0.12`) at `+-3px` along X or Y (1-4px dark cel-shading contour rim) | `>= 0.0032` | `09.32.32.11.png` = `0.00567` (PASS detect) | `0.000` |
| 8 | `detect_camera_turn_shadow_flicker` | Consecutive-frame luminance stability: fraction of pixels swinging `> 0.15` OR 95th-percentile `|lum_a - lum_b|` | `shift >= 0.18` or `p95 >= 0.24` | `09.37.21.24.png` vs `09.38.46.52.png`: `shift = 0.280`, `p95 = 0.300` (PASS detect) | `0.000` |

---

## 4. Playtest Tour Harness Usage & Safety Guarantees

1. **Offline Audit of Any Screenshot Folder:**
   ```powershell
   python tools/live/playtest_tour.py --offline-dir "C:\path\to\shots" --out-dir ".\audit_out"
   ```
   Emits `audit_out\playtest_report.json` and `audit_out\playtest_report.md`.
2. **Live Automated Skyrim SE Tour:**
   ```powershell
   python tools/live/playtest_tour.py --launch --game-dir "H:\SteamLibrary\steamapps\common\Skyrim Special Edition" --out-dir ".\live_tour_out" --views 0,28,29,30,31,32,33
   ```
   - Sets `SteamAppId=489830` and `SteamGameId=489830`.
   - Verifies `raw_engine_out\boot_selfcheck.txt` and `bridge_live_telemetry.json`.
   - Cycles RAW debug views via `[Lighting] DebugView=N` hot-reload and captures frames via GDI.
   - Always removes `raw_engine_out\boot_sentinel.state` in `finally:` on clean harness exit so automated runs never trigger a false SafeMode crash-loop rollover.
3. **Interactive Session Request Handlers (`tools/live/kinds.py`):**
   - `debug_view`: switches `[Lighting] DebugView` (0..33) live and optionally captures a frame.
   - `visual_audit`: captures a live frame (or loads a path), runs all single-frame heuristics, and writes `visual_audit.json`.
   - `camera_sweep_flicker`: captures consecutive frames across a camera turn and writes `camera_sweep_flicker.json`.

---

## 5. Wave 63+ Defect Remediation Tracking Ledger

| Defect ID | Symptom | Owning Track | Automated Gate | Status |
| :--- | :--- | :--- | :--- | :--- |
| `V63-01` | Indoor giant pitch-black concentric orbs | Track 1 (`@fire-sky-fix`) + Track 6 (`@flora-engine`) | `detect_indoor_black_orbs` | In Progress (Wave 63) |
| `V63-02` | Floating neon fireballs & red sky fire bleed | Track 1 (`@fire-sky-fix`) + Track 4 + Track 6 | `detect_red_sky_fire_bleed` | In Progress (Wave 63) |
| `V63-03` | Jagged horizontal water scanline cutouts | Track 2 (`@water-terrain-fix`) | `detect_water_scanline_holes` | In Progress (Wave 63) |
| `V63-04` | Fast water & periodic zebra-stripe ripples | Track 2 (`@water-terrain-fix`) | `detect_water_zebra_stripes` | In Progress (Wave 63) |
| `V63-05` | Diagonal blue sky strip + L-shaped cutout | Track 4 (`@sky-cosmos-atmo`) | `detect_sky_diagonal_cutout` | In Progress (Wave 63) |
| `V63-06` | Green moss on Stump the dog & Frodnar gloves | Track 3 (`@fluid-sim-core`) + Track 8 (`@ce-tfc-lod-fix`, `base == 14u`) | `detect_actor_green_moss_chroma` (RGB + DebugView 28/31) | In Progress (Wave 63) |
| `V63-07` | Dark cel-shading motion outline & blotchy skin | Track 3 (`@fluid-sim-core`) | `detect_dark_motion_outline` + DebugView 32 | In Progress (Wave 63) |
| `V63-08` | Camera-turn shadow flicker & flat hillside | Track 3 (`@fluid-sim-core`) + Track 4 (`@sky-cosmos-atmo`) | `detect_camera_turn_shadow_flicker` + DebugView 30 | In Progress (Wave 63) |
| `V63-09` | Live world-space snow tracks, grass, caustics & UI | Track 5 (`@live-world-render`) + Track 6 (`@flora-engine`) | `test_ui_lib_gpu` + `test_live_world_composite_gpu` | In Progress (Wave 63) |
| `V63-10` | Automated visual heuristics & playtest tour | Track 7 (`@ce-decompiler-rebuild`) | `python tests/test_visual_heuristics.py` (18/18 PASS) | Complete (Wave 63) |
