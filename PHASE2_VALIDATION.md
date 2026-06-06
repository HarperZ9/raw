# RAW Phase 2 -- Effect Validation Checklist

Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

---

## Test Environment

- **Skyrim SE build:** 1.6.1170 (latest AE)
- **RAW build:** _[commit hash]_
- **GPU profiler:** F11 overlay + per-pass timing (Phase 1C)
- **Debug views:** INSERT > Shaders tab > Debug Visualization

### Test Locations

| ID | Location | Purpose |
|----|----------|---------|
| L1 | Whiterun exterior (day, clear) | Bright directional light, grass, architecture |
| L2 | Riverwood exterior (dawn/dusk) | Low sun angle, long shadows, water, trees |
| L3 | Embershard Mine (interior) | Dark environment, torch light, tight spaces |
| L4 | Solitude exterior (overcast) | Diffuse sky light, stone surfaces, large-scale |
| L5 | Blackreach (underground) | Bioluminescence, emissive particles, no sun |
| L6 | Throat of the World (high altitude) | Extreme distance, atmosphere, snow, sparse |
| L7 | Riften canals (water) | Water surfaces, reflections, underwater transitions |
| L8 | Dragonsreach interior | Large interior, mixed lighting, metallic surfaces |

### Validation Protocol

For each effect:

1. Enable **ONLY** this effect (all others disabled via ImGui toggles)
2. Visit each listed test location
3. Toggle the effect on/off (INSERT GUI or per-pass X button from profiler)
4. Capture screenshots with F12 (on and off)
5. Note: visual quality, artifacts, performance cost (GPU profiler ms)
6. Grade: **PASS** / **PASS-WITH-NOTES** / **FAIL**
7. Record notes in the space provided

### Grading Criteria

| Grade | Meaning |
|-------|---------|
| **PASS** | Visually correct, no artifacts, meets perf budget |
| **PASS-WITH-NOTES** | Visible improvement, minor issues acceptable for now |
| **FAIL** | Broken output, severe artifacts, or >2x perf budget |

---

## 1. Scene Compositor

**Source:** Custom pipeline -- AO (multiplicative) -> GI (clamped additive) -> SSR (energy-conserving lerp) -> Clouds (transmittance over)
**Class:** `SceneCompositor` (`SceneCompositor.cpp/h`)
**Shader:** `SceneComposite.hlsl`
**Pipeline stage:** PrePresent, priority 90

### Visual Checklist
- [ ] Copy+blend roundtrip preserves scene color when all inputs are null SRVs
- [ ] No color shift compared to vanilla when all effect intensities set to 0
- [ ] Debug mode 1 (AO) shows correct grayscale occlusion
- [ ] Debug mode 2 (GI) shows indirect bounce correctly
- [ ] Debug mode 3 (SSR) shows reflections only
- [ ] Debug mode 4 (Clouds) shows cloud inscatter only
- [ ] Debug mode 5 (Shadow) shows contact shadow mask
- [ ] Debug mode 6 (Skylight) shows sky visibility
- [ ] Debug mode 7 (HDR heatmap) shows luminance distribution
- [ ] No banding in dark gradients (8-bit precision check)

### Known Artifact Types
- Color shift from incorrect gamma handling in copy/composite
- Sky darkening if sky pixels are not skipped (depth < 0.0001 in reversed-Z)
- Alpha bleeding at screen edges

### Performance Budget
- Target: < 0.3ms at 1080p (single fullscreen PS)

### Test Locations: L1, L3, L5, L8
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 2. GTAO (Ground Truth Ambient Occlusion + Bounce GI)

**Source:** VB-SSGI -- Jimenez 2016 (GTAO) upgraded to Activision 2019 ("Practical Real-Time Strategies for Accurate Indirect Occlusion")
**Class:** `GTAORenderer` (`GTAORenderer.cpp/h`)
**Shaders:** `GTAO_Main.hlsl`, `GTAO_Spatial.hlsl`, `GTAO_Temporal.hlsl`
**Output:** SRV at t20, R16G16B16A16_FLOAT (.rgb = indirect bounce, .a = AO)
**Pipeline:** 3-pass compute (main -> spatial denoise -> temporal accumulation)
**VRAM:** ~32 MB at 1080p

### Visual Checklist
- [ ] Correct darkening in corners and crevices (wall joints, rock overhangs)
- [ ] No halo artifacts around characters or moving objects
- [ ] Proper occlusion falloff distance (not too wide or narrow)
- [ ] No banding in AO gradients (verify spatial filter smoothing)
- [ ] Temporal stability -- no flickering when stationary
- [ ] No ghosting when camera moves quickly (temporal rejection working)
- [ ] Bounce GI: indirect color bleeding from nearby surfaces (red wall -> red bounce)
- [ ] Bounce GI: does not over-brighten dark areas (check m_bounceIntensity)
- [ ] Sky pixels excluded (no AO applied to sky, depth < 0.0001)
- [ ] AO radius feels correct at default 1.5 world-space units

### Parameter Sweep
- [ ] Radius: 0.5 -> 1.0 -> 1.5 -> 3.0 (visual quality vs reach)
- [ ] Directions: 2 -> 4 -> 6 -> 8 (quality vs cost)
- [ ] Steps: 4 -> 8 -> 12 -> 16 (quality vs cost)
- [ ] Intensity: 0.5 -> 1.0 -> 1.5 (find sweet spot)
- [ ] Bounce intensity: 0.0 -> 0.25 -> 0.5 -> 1.0

### Known Artifact Types
- Halo around characters (radius too large relative to depth discontinuity)
- Banding from insufficient directions/steps
- Temporal ghosting from overly aggressive accumulation (alpha too low)
- AO on sky pixels = visible darkened sky band

### Performance Budget
- Target: < 1.0ms at 1080p (4 dirs, 8 steps)
- Perf preset: 2 dirs / 4 steps (< 0.5ms)
- Ultra preset: 6 dirs / 12 steps (< 1.5ms)

### Test Locations: L1, L3, L4, L8
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 3. Contact Shadows

**Source:** Screen-space ray march toward directional light
**Class:** `ContactShadowRenderer` (`ContactShadowRenderer.cpp/h`)
**Shaders:** `ContactShadow_Main.hlsl`, `ContactShadow_Denoise.hlsl`
**Output:** SRV at t28, R8_UNORM shadow mask (1 = lit, 0 = shadowed)
**Pipeline:** 2-pass compute (ray march -> spatial denoise)
**VRAM:** ~6 MB at 1080p

### Visual Checklist
- [ ] Fine shadows at object-ground contact points (feet, furniture legs)
- [ ] No shadow acne (random noise where there should be none)
- [ ] No shadow swimming on static geometry when camera moves
- [ ] Correct sun direction alignment (shadows match the sun)
- [ ] Clean fade at max ray distance (no hard cutoff lines)
- [ ] No self-shadowing artifacts on flat surfaces
- [ ] Proper denoise -- smooth mask without visible noise pattern

### Parameter Sweep
- [ ] Ray length: 0.05 -> 0.10 -> 0.20 (reach vs artifacts)
- [ ] Thickness: 0.01 -> 0.02 -> 0.05 (false occlusion tolerance)
- [ ] Max steps: 8 -> 16 -> 32 (quality vs cost)
- [ ] Intensity: 0.5 -> 1.0 -> 1.5

### Known Artifact Types
- Shadow acne from thickness too low
- Shadow swimming from inconsistent depth reads
- Hard cutoff lines at max ray distance
- Sun direction not updating correctly at dawn/dusk

### Performance Budget
- Target: < 0.5ms at 1080p (16 steps)

### Test Locations: L1, L2, L4, L6
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 4. Skylighting

**Source:** Hemisphere-based sky visibility with 3D voxel probe grid
**Class:** `SkylightingRenderer` (`SkylightingRenderer.cpp/h`)
**Shaders:** `Skylighting_ProbeUpdate.hlsl`, `Skylighting_ProbeQuery.hlsl`, `Skylighting_ScreenSpace.hlsl`, `Skylighting_Spatial.hlsl`, `Skylighting_Temporal.hlsl`
**Output:** SRV at t29, R16_FLOAT (0 = fully occluded, 1 = full sky)
**Pipeline:** 4-pass compute (probe update -> probe query -> spatial -> temporal)
**VRAM:** ~10 MB at 1080p

### Visual Checklist
- [ ] Visible variation between open sky and sheltered areas
- [ ] Under overhangs and porches: noticeably darker ambient than open field
- [ ] Forest canopy reduces sky visibility vs open meadow
- [ ] No light leaking through walls or terrain (probe grid resolution adequate)
- [ ] Temporal stability -- no flickering in static scenes
- [ ] Clean gradients at indoor/outdoor transition points
- [ ] Probe grid covers camera-centered volume without visible boundary

### Parameter Sweep
- [ ] Directions: 2 -> 4 -> 6 -> 8 (hemisphere coverage)
- [ ] Steps: 4 -> 8 -> 12 (depth march quality)
- [ ] Radius: 1.0 -> 3.0 -> 5.0 (sample reach)
- [ ] Intensity: 0.5 -> 0.8 -> 1.0 -> 1.5

### Known Artifact Types
- Light leaking through thin geometry (wall thickness < voxel size)
- Sudden brightness change when crossing probe grid boundary
- Flickering from insufficient temporal alpha
- Incorrect sky visibility underground (Blackreach should be 0)

### Performance Budget
- Target: < 0.8ms at 1080p (6 dirs, 8 steps)
- Probe update: < 0.3ms (128x128x64, amortized)

### Test Locations: L1, L2, L3, L4, L6
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 5. SSR (Screen-Space Reflections)

**Source:** Stachowiak 2015 -- Hi-Z accelerated ray marching
**Class:** `SSRRenderer` (`SSRRenderer.cpp/h`)
**Shaders:** `SSR_RayMarch.hlsl`, `SSR_Resolve.hlsl`, `SSR_Temporal.hlsl`
**Output:** SRV at t27, half-res R16G16B16A16_FLOAT (reflected color + alpha)
**Pipeline:** 3-pass compute (ray march -> resolve -> temporal denoise)
**VRAM:** ~24 MB at 1080p

### Visual Checklist
- [ ] Reflections visible on wet/shiny surfaces (stone, metal, water)
- [ ] No ray march stepping artifacts (visible staircase patterns)
- [ ] Fresnel attenuation correct (reflections stronger at glancing angles)
- [ ] Edge fade at screen borders (no hard cutoff)
- [ ] Distance fade for far reflections
- [ ] No reflection of UI elements or HUD
- [ ] Temporal stability -- no flickering reflections on static surfaces
- [ ] Half-res to full-res upsample: no blocky/pixelated edges
- [ ] Correct behavior on curved surfaces (not just flat floors)

### Parameter Sweep
- [ ] Max distance: 50 -> 100 -> 200 (reach vs missed hits)
- [ ] Thickness: 0.1 -> 0.5 -> 1.0 (false intersection tolerance)
- [ ] Max steps: 32 -> 64 -> 128 (quality vs cost)
- [ ] Intensity: 0.3 -> 0.5 -> 1.0

### Known Artifact Types
- Staircase/stepping artifacts from insufficient steps
- Streaking from incorrect temporal accumulation
- Sky reflected where there should be geometry (thickness too large)
- Blocky reflections from half-res without bilateral upsample

### Performance Budget
- Target: < 1.5ms at 1080p (64 steps, half-res)

### Test Locations: L1, L7, L8, L4
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 6. SSGI (Screen-Space Global Illumination)

**Source:** Voxel cone tracing -- 128^3 YCoCg SH2 voxel grid
**Class:** `SSGIRenderer` (`SSGIRenderer.cpp/h`)
**Shaders:** `SSGI_Voxelize.hlsl`, `SSGI_Trace.hlsl`, `SSGI_Denoise.hlsl`, `SSGI_Composite.hlsl`
**Output:** SRV at t26, half-res R16G16B16A16_FLOAT indirect lighting
**Pipeline:** 4-pass (voxelize -> trace -> denoise -> upsample+composite)
**VRAM:** ~32 MB at 1080p (128^3 + 64^3 voxel grids + half-res intermediates)

### Visual Checklist
- [ ] Visible indirect color bounce (red carpet -> red tint on white wall)
- [ ] No race condition artifacts (voxelization vs trace timing)
- [ ] No visible voxel grid pattern in GI output
- [ ] Proper falloff at voxel grid boundary (m_voxelRange = 2048)
- [ ] No over-brightening of dark scenes (m_giIntensity default 0.25)
- [ ] Temporal stability after convergence (no shimmer on static view)
- [ ] YCoCg SH2 encoding: no color banding or chroma artifacts
- [ ] Half-res upsample: no blocky artifacts at depth edges

### Parameter Sweep
- [ ] GI intensity: 0.1 -> 0.25 -> 0.5 -> 1.0
- [ ] Ray count: 2 -> 4 -> 8 (quality vs cost)
- [ ] Max steps: 16 -> 32 -> 64 (voxel march quality)
- [ ] Voxel range: 1024 -> 2048 -> 4096 (coverage vs resolution)

### Known Artifact Types
- Light leaking through walls (voxel resolution insufficient)
- Color banding from YCoCg chroma subsampling (64^3 CoCg grid)
- Race condition flicker from concurrent voxelize + trace
- Visible grid pattern at voxel boundaries

### Performance Budget
- Target: < 2.0ms at 1080p (8 rays, 32 steps, half-res)
- Voxelize: < 0.5ms (128^3 grid clear + populate)
- Trace: < 1.0ms (half-res, 8 rays)

### Test Locations: L3, L8, L5, L1
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 7. Bloom (Kawase + Karis Anti-Firefly)

**Source:** Jimenez 13-tap downsample, Karis 2013 anti-firefly, 9-tap tent upsample
**Class:** `BloomRenderer` (`BloomRenderer.cpp/h`)
**Shaders:** `Bloom_Extract.hlsl`, `Bloom_Downsample.hlsl`, `Bloom_Upsample.hlsl`, `Bloom_Anamorphic.hlsl`, `Bloom_Composite.hlsl`
**Output:** Bloom SRV for downstream (ColorPipeline, ToneMapManager)
**Pipeline:** 8 compute passes + 1 PS composite (4 mip levels)
**VRAM:** ~20 MB at 1080p

### Visual Checklist
- [ ] Bright light sources produce soft glow (sun, torches, candles)
- [ ] No firefly artifacts on very bright pixels (Karis filter working)
- [ ] Energy conservation -- bloom does not significantly change scene brightness
- [ ] Smooth mip transitions (no visible resolution steps in bloom)
- [ ] Spectral tinting on upsample (optional warm/cool color shift)
- [ ] Anamorphic streak (when enabled) is clean horizontal blur
- [ ] No bloom on dark scene areas (threshold working correctly)
- [ ] Color temperature tinting applied correctly (6500K default neutral)

### Parameter Sweep
- [ ] Threshold: 0.5 -> 1.0 -> 2.0 (extraction sensitivity)
- [ ] Intensity: 0.25 -> 0.5 -> 1.0 (bloom strength)
- [ ] Knee: 0.1 -> 0.5 -> 0.9 (soft-knee transition width)
- [ ] Anamorphic: 0.0 -> 0.3 -> 0.6 (streak strength)

### Known Artifact Types
- Fireflies (bright pixel explosions) if Karis filter fails
- Energy non-conservation causing overall scene brightening
- Visible mip level transitions (resolution stepping in bloom halo)
- Anamorphic streaks bleeding across screen edges

### Performance Budget
- Target: < 0.8ms at 1080p (all 9 passes)

### Test Locations: L1, L2, L3, L5
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 8. Tonemapping (8 Operators via ColorPipeline)

**Source:** Multiple published operators
**Class:** `ColorPipeline` (`ColorPipeline.cpp/h`)
**Shader:** `ColorPipeline.hlsl` (stage 8: CPS_ToneMap)
**Operators:**

| # | Operator | Source / Paper |
|---|----------|---------------|
| 8a | AgX | Troy Sobotka -- open-source filmic tone mapper |
| 8b | ACES | Stephen Hill -- fitted RRT+ODT approximation |
| 8c | Reinhard Extended | Reinhard et al. 2002 -- x(1+x/w^2)/(1+x) |
| 8d | Hejl-Burgess | Hejl 2010 -- single-pass filmic approximation |
| 8e | Hable (Uncharted 2) | John Hable 2010 -- piecewise filmic |
| 8f | Lottes | Timothy Lottes (AMD) -- parametric |
| 8g | Gran Turismo (Uchimura) | Uchimura 2017 -- two-segment curve |
| 8h | None (Linear) | Linear clamp (debug/reference) |

### Per-Operator Validation

For each operator, compare A/B against Linear (None):

#### 8a. AgX (Default)
- [ ] Saturated colors desaturate gracefully into highlight roll-off
- [ ] No color clipping in neon/fire scenes
- [ ] Natural skin tone preservation
- [ ] Clean shadow-to-midtone transition
- Grade: ___

#### 8b. ACES
- [ ] "Hollywood" filmic look with lifted blacks
- [ ] No excessive blue shift in shadows
- [ ] Good dynamic range compression for high-contrast scenes
- Grade: ___

#### 8c. Reinhard Extended
- [ ] Soft highlight compression, preserves relative luminance
- [ ] No excessive desaturation in highlights
- [ ] Works well for evenly-lit scenes
- Grade: ___

#### 8d. Hejl-Burgess
- [ ] Single-pass filmic, slight warm bias
- [ ] No harsh clipping
- [ ] Good for warm/golden hour scenes
- Grade: ___

#### 8e. Hable (Uncharted 2)
- [ ] Classic filmic S-curve with lifted blacks
- [ ] Strong toe/shoulder character
- [ ] Slight desaturation in extremes (intentional)
- Grade: ___

#### 8f. Lottes
- [ ] AMD parametric curve, adjustable contrast
- [ ] Clean midtone response
- [ ] No unexpected color shifts
- Grade: ___

#### 8g. Gran Turismo (Uchimura)
- [ ] Two-segment response, automotive photography feel
- [ ] Smooth transition at segment boundary
- [ ] Good for outdoor/racing scenes
- Grade: ___

#### 8h. Linear (None)
- [ ] Raw linear clamp to [0,1]
- [ ] Reference baseline for A/B comparison
- [ ] Expected: harsh clipping in bright areas
- Grade: ___

### Overall Tonemapping Notes
- Best default operator for Skyrim: ___
- Performance: all operators should be < 0.1ms (arithmetic-only, no texture ops)
- Test Locations: L1 (bright), L2 (dawn/dusk), L3 (dark), L6 (HDR extreme)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 9. Color Pipeline (12-Stage Full-Spectrum)

**Source:** Composite system -- Oklab color science, ASC-CDL, FILM stock emulation
**Class:** `ColorPipeline` (`ColorPipeline.cpp/h`)
**Shader:** `ColorPipeline.hlsl`
**Pipeline stage:** PrePresent, priority 50

### Stage-by-Stage Validation

Each stage can be toggled independently via the stage bitmask.

#### 9a. Exposure + White Balance (CPS_Exposure)
- [ ] Auto-exposure adapts to bright and dark environments
- [ ] White balance (Tanner Helland Kelvin->RGB) produces correct shift
- [ ] Transition speed is smooth, not jarring
- [ ] No exposure pumping (oscillation between frames)
- Test: Walk from L3 (dark) to L1 (bright), observe adaptation

#### 9b. Stevens Effect (CPS_Stevens)
- [ ] Adaptation-scaled contrast increases in bright scenes
- [ ] Reduced contrast in dark scenes (scotopic adaptation)
- [ ] No clipping or saturation artifacts

#### 9c. Purkinje Shift (CPS_Purkinje)
- [ ] Blue-shift visible in very dark scenes (night exterior)
- [ ] No effect in bright daylight (correctly gated by EV)
- [ ] Subtle -- should not dominate color

#### 9d. Local Tone Mapping (CPS_LocalTM)
- [ ] Bloom-driven local contrast enhancement
- [ ] No halo artifacts around bright objects
- [ ] Dark areas in bright scenes get visibility boost

#### 9e. FILM Pipeline (CPS_Film)
- [ ] Neg/print stock emulation with per-channel toe/shoulder/gamma
- [ ] Beer-Lambert density produces film-like saturation
- [ ] Interimage cross-channel inhibition is subtle, not color-shifting
- [ ] Print stock simulation adds correct roll-off character

#### 9f. Log-domain Contrast (CPS_Contrast)
- [ ] Fallback when FILM disabled
- [ ] Clean S-curve without clipping
- [ ] No banding in smooth gradients

#### 9g. Hunt Effect (CPS_Hunt)
- [ ] Brightness-dependent saturation boost
- [ ] Colors appear more vivid in well-lit areas
- [ ] No over-saturation or neon artifacts

#### 9h. AgX Punchy Look (CPS_AgXPunchy)
- [ ] Optional saturation boost in AgX color space
- [ ] Only active when AgX tonemapper selected
- [ ] Adds "punch" without breaking color integrity

#### 9i. GRADE Pipeline (CPS_Grade)
- [ ] Printer lights (R/G/B 1-50): shifts color balance like film lab
- [ ] Split-toning in Oklab: shadow/highlight tinting works independently
- [ ] ASC-CDL (slope/offset/power/saturation): industry-standard grading
- [ ] Bleach bypass: desaturated high-contrast look
- [ ] Highlight desaturation: bright areas gracefully lose color

#### 9j. Extended Grading (CPS_ExtGrade)
- [ ] Lift/Gamma/Gain: 3-way color wheels function correctly
- [ ] Vibrance: boosts desaturated colors, leaves saturated ones alone
- [ ] S-curve contrast: adjustable midtone contrast

#### 9k. Final Output (CPS_Dither)
- [ ] sRGB encoding correct (no double-gamma)
- [ ] Dithering eliminates banding in 8-bit gradients
- [ ] PQ (HDR10) encoding produces correct nit levels

### Performance Budget
- Target: < 0.5ms at 1080p (single fullscreen PS, all stages)

### Test Locations: L1, L2, L3, L5, L6
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 10. Depth of Field (Thin-Lens Physical DoF)

**Source:** Physical thin-lens CoC, N-gon shaped bokeh gather
**Class:** `DoFRenderer` (`DoFRenderer.cpp/h`)
**Shaders:** `DoF_Autofocus.hlsl`, `DoF_CoCTile.hlsl`, `DoF_FarGather.hlsl`, `DoF_NearGather.hlsl`, `DoF_Composite.hlsl`
**Pipeline:** 5-pass (autofocus -> CoC + tile -> far gather -> near gather -> composite)
**VRAM:** ~40 MB at 1080p

### Visual Checklist
- [ ] Autofocus tracks center-screen subject correctly
- [ ] Far field blur: background defocuses with distance
- [ ] Near field blur: foreground defocuses when very close to camera
- [ ] Bokeh shape: N-gon aperture shape visible in specular highlights
- [ ] Cat-eye effect at frame edges (optical vignetting of bokeh)
- [ ] No hard boundaries between focused and defocused regions (CoC gradient)
- [ ] No bright bokeh artifacts from energy non-conservation
- [ ] Focus peaking overlay (debug) highlights in-focus band
- [ ] Chromatic aberration (longitudinal CA) shifts color at bokeh edges
- [ ] Temporal smoothing on autofocus (no snapping between frames)
- [ ] No artifacts on transparent objects (sky, particles)

### Parameter Sweep
- [ ] Aperture: f/1.4 -> f/2.8 -> f/5.6 -> f/11 -> f/22
- [ ] Focal length: 24mm -> 50mm -> 85mm -> 200mm
- [ ] Quality: Low -> Medium -> High -> Ultra (48 -> 72 -> 96 -> 128 samples)
- [ ] Blade count: 4 -> 6 -> 9 (polygon vs circular bokeh)
- [ ] Manual focus vs autofocus mode

### Known Artifact Types
- Bright bokeh explosions from non-conservative gather
- Hard line at CoC tile boundary (16x16 tile artifact)
- Near/far field overlap artifacts
- Autofocus hunting (oscillation on ambiguous depth)

### Performance Budget
- Target: < 2.0ms at 1080p (Medium quality)
- Low: < 1.0ms | High: < 3.0ms | Ultra: < 4.0ms

### Test Locations: L1 (far focus), L8 (near focus), L2 (mixed), L3 (dark bokeh)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 11. Lens Effects (Physically-Based)

**Source:** ABCD matrix ghosts, thin-film MgF2 coating, Brown-Conrady distortion, Cauchy spectral CA
**Class:** `LensRenderer` (`LensRenderer.cpp/h`)
**Shaders:** `Lens_Downsample.hlsl`, `Lens_Ghost.hlsl`, `Lens_Starburst.hlsl`, `Lens_Anamorphic.hlsl`, `Lens_Composite.hlsl`
**Pipeline:** 5-pass (downsample -> ghost eval -> starburst + veiling -> anamorphic -> composite)
**VRAM:** ~28 MB at 1080p

### Visual Checklist
- [ ] Ghost elements visible when looking toward sun (mirrored bright spots)
- [ ] Ghost thin-film coating colors (rainbow/iridescent tint)
- [ ] Starburst diffraction pattern from aperture blades (spike count matches blade count)
- [ ] Anamorphic flare streak (horizontal, with spectral dispersion)
- [ ] Barrel/pincushion distortion (subtle, matches lens preset)
- [ ] 6-band chromatic aberration (color fringing at frame edges)
- [ ] cos^4(theta) vignette (darker corners, physically motivated)
- [ ] Veiling glare (low-frequency bright wash from bright sources)
- [ ] Lens dirt modulated by flare/ghost brightness
- [ ] No lens effects when looking away from all light sources

### Parameter Sweep
- [ ] Ghost intensity: 0.0 -> 0.15 -> 0.30 (visibility)
- [ ] Starburst intensity: 0.0 -> 0.10 -> 0.25
- [ ] Distortion K1: -0.05 -> -0.02 -> 0.0 (barrel to none)
- [ ] CA strength: 0.0 -> 0.003 -> 0.01 (fringing)
- [ ] Lens presets: Cooke / Zeiss / MIR1 / Primo / Custom

### Known Artifact Types
- Ghost elements misaligned with light source (matrix calculation error)
- Starburst aliasing (insufficient angular samples)
- Visible mip level transitions in downsample chain
- Over-aggressive chromatic aberration looks like rendering error

### Performance Budget
- Target: < 1.2ms at 1080p (all sub-passes)

### Test Locations: L1 (look at sun), L2 (dawn/dusk low sun), L3 (torch ghosts)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 12. Volumetric Clouds

**Source:** Worley + Perlin FBM noise, Henyey-Greenstein phase, Beer's law extinction, powder effect
**Class:** `VolumetricClouds` (`VolumetricClouds.cpp/h`)
**Shaders:** `CloudShapeNoise.hlsl`, `CloudDetailNoise.hlsl`, `CloudRaymarch.hlsl`, `CloudComposite.hlsl`
**Pipeline:** 3-pass (noise gen [one-time] -> raymarch [quarter-res] -> composite)
**VRAM:** ~20 MB at 1080p

### Visual Checklist
- [ ] Cloud shapes look natural (not uniform or tiled)
- [ ] Silver lining effect when sun is behind clouds (forward scattering)
- [ ] Cloud density varies with altitude (base to top gradient)
- [ ] Temporal reprojection: no visible ghosting on camera rotation
- [ ] Quarter-res to full-res upsample: no blocky edges against sky
- [ ] Proper transmittance: scene visible through thin clouds
- [ ] Wind offset accumulation: clouds drift over time
- [ ] Fog integration: smooth transition from ground fog to cloud base
- [ ] No visible tiling/repetition in noise pattern
- [ ] No clouds underground (Blackreach should have none)

### Parameter Sweep
- [ ] Coverage: 0.0 -> 0.25 -> 0.5 -> 0.75 -> 1.0
- [ ] Density: 0.01 -> 0.05 -> 0.10 (extinction multiplier)
- [ ] Cloud base: 1000m -> 1500m -> 2000m
- [ ] Cloud top: 3000m -> 4000m -> 5000m
- [ ] Fog density: 0.0 -> 0.0005 -> 0.001

### Known Artifact Types
- Noise texture tiling visible at large scale
- Temporal ghosting on fast camera rotation
- Blocky quarter-res upsample at cloud edges
- Light leaking through dense cloud regions (density too low)
- Banding in transmittance gradients

### Performance Budget
- Target: < 3.0ms at 1080p (quarter-res raymarch, most expensive effect)
- Noise generation: one-time cost, not per-frame

### Test Locations: L1, L4, L6 (avoid L3, L5 -- underground)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 13. Subsurface Scattering (SSS)

**Source:** Separable Burley diffusion profile approximation
**Class:** `SubsurfaceScatteringRenderer` (`SubsurfaceScatteringRenderer.cpp/h`)
**Shader:** `SSS_Blur.hlsl` (single shader for H + V pass)
**Pipeline:** 2-pass compute (horizontal blur -> vertical blur)
**VRAM:** ~40 MB at 1080p

### Visual Checklist
- [ ] Skin appears softer/warmer than without SSS (not plastic-looking)
- [ ] Per-channel Burley widths: red scatters widest, blue narrowest
- [ ] No bleeding across depth discontinuities (ears vs background)
- [ ] Only skin pixels affected (MAT_SKIN from MaterialClassifier)
- [ ] Foliage SSS: leaves appear translucent when backlit
- [ ] No color shift on non-SSS materials (perfect passthrough)
- [ ] Back-lighting translucency visible (light through ears, nose)
- [ ] No visible grid/pattern from compute shader groupshared limits

### Parameter Sweep
- [ ] SSS radius: 0.005 -> 0.012 -> 0.025 (blur extent)
- [ ] SSS strength: 0.0 -> 0.5 -> 1.0 -> 2.0
- [ ] Translucency: 0.0 -> 0.3 -> 0.6 (back-light amount)
- [ ] Skin widths R/G/B: default (0.012/0.008/0.004) -> wider -> narrower

### Known Artifact Types
- Depth bleeding (skin color bleeds onto nearby objects)
- Over-blur making skin look like wax
- MaterialClassifier false positives (non-skin pixels get SSS)
- Visible H/V pass seam (separable filter limitation)

### Performance Budget
- Target: < 0.8ms at 1080p (2 full-res compute passes)

### Test Locations: L8 (NPCs), L1 (outdoor NPCs), L2 (foliage backlit)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 14. Water Blending

**Source:** Screen-space depth-based edge blending + animated caustics
**Class:** `WaterBlendingRenderer` (`WaterBlendingRenderer.cpp/h`)
**Shader:** `WaterBlending_Main.hlsl`
**Pipeline:** Single compute pass
**VRAM:** ~4 MB at 1080p

### Visual Checklist
- [ ] Soft edges at water-terrain boundaries (no hard water line)
- [ ] Caustic patterns visible underwater (animated, tiling)
- [ ] Depth fog: objects deeper underwater become more blue/murky
- [ ] Only water pixels affected (MaterialClassifier MAT_WATER)
- [ ] Caustic scale matches world-space (not screen-space stretched)
- [ ] No artifacts at water surface viewing angle transitions

### Parameter Sweep
- [ ] Edge blend width: 0.1 -> 0.5 -> 1.0 (world-space units)
- [ ] Caustic intensity: 0.0 -> 0.25 -> 0.5
- [ ] Caustic scale: 0.01 -> 0.02 -> 0.05
- [ ] Depth fog strength: 0.0 -> 0.4 -> 0.8

### Known Artifact Types
- Hard water line (edge blend too narrow)
- Caustic tiling visible (repeat pattern too small)
- Incorrect MaterialClassifier water ID (blending on non-water pixels)

### Performance Budget
- Target: < 0.3ms at 1080p (single compute pass)

### Test Locations: L7, L2 (rivers), L4 (Solitude harbor)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 15. Grass Lighting

**Source:** Screen-space multi-light correction for BSGrassShader
**Class:** `GrassLightingRenderer` (`GrassLightingRenderer.cpp/h`)
**Shader:** `GrassLighting_Main.hlsl`
**Pipeline:** Single compute pass
**VRAM:** ~2 MB at 1080p

### Visual Checklist
- [ ] Grass no longer uniformly dark in shadowed areas
- [ ] Multi-light contribution: torches/campfires illuminate nearby grass
- [ ] Subsurface translucency: grass blades glow when backlit by sun
- [ ] Wind normal perturbation: subtle light variation from wind sway
- [ ] Only vegetation pixels affected (MAT_VEGETATION from MaterialClassifier)
- [ ] No over-brightening (ambient boost is subtle)
- [ ] Lighting matches full-detail tree/object lighting in same scene

### Parameter Sweep
- [ ] Ambient boost: 0.0 -> 0.15 -> 0.30
- [ ] Subsurface strength: 0.0 -> 0.35 -> 0.7
- [ ] Multi-light intensity: 0.5 -> 1.0 -> 2.0
- [ ] Wind sway: 0.0 -> 0.5 -> 1.0

### Known Artifact Types
- Over-bright grass near multiple light sources (clamping needed)
- MaterialClassifier misidentifying non-vegetation pixels
- Visible hard boundary between affected and unaffected vegetation

### Performance Budget
- Target: < 0.2ms at 1080p (single compute pass, CB only)

### Test Locations: L1 (fields), L2 (forest, campfire), L4
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 16. Tree LOD Lighting

**Source:** Screen-space correction for BSDistantTreeShader
**Class:** `TreeLODLightingRenderer` (`TreeLODLightingRenderer.cpp/h`)
**Shader:** `TreeLODLighting_Main.hlsl`
**Pipeline:** Single compute pass
**VRAM:** ~2 MB at 1080p

### Visual Checklist
- [ ] LOD trees match lighting of full-detail trees (no brightness pop)
- [ ] Atmosphere ambient color applied correctly from LUT (t23/t24)
- [ ] Sun directional contribution visible (LOD trees not flat-lit)
- [ ] Color matching blend: LOD and full trees transition smoothly
- [ ] No artifacts at LOD transition distance

### Parameter Sweep
- [ ] Ambient match: 0.0 -> 0.4 -> 0.8 -> 1.0
- [ ] Directional strength: 0.0 -> 0.3 -> 0.6 -> 1.0
- [ ] Color match blend: 0.0 -> 0.25 -> 0.5 -> 1.0

### Known Artifact Types
- Brightness pop at LOD transition (color match not calibrated)
- Wrong atmosphere tint if LUT not available
- Over-darkening at dusk/dawn (directional strength too high for low sun)

### Performance Budget
- Target: < 0.2ms at 1080p (single compute pass)

### Test Locations: L1 (look toward distance), L6 (extreme distance), L4
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 17. Particle Lighting

**Source:** Screen-space emissive detection + radial light scatter
**Class:** `ParticleLightingRenderer` (`ParticleLightingRenderer.cpp/h`)
**Shaders:** `ParticleLight_EmissiveDetect.hlsl`, `ParticleLight_Scatter.hlsl`, `ParticleLight_Composite.hlsl`
**Pipeline:** 3-pass compute (detect -> scatter -> composite)
**VRAM:** ~12 MB at 1080p

### Visual Checklist
- [ ] Emissive particles (fire, magic) illuminate nearby geometry
- [ ] Light scatter radius matches visible particle brightness
- [ ] Depth-aware attenuation: scatter does not bleed through walls
- [ ] No false positives (non-emissive bright pixels triggering scatter)
- [ ] Scatter falloff looks natural (inverse-square or configurable exponent)
- [ ] No visible quarter-res artifacts in scatter output
- [ ] MAT_EMISSIVE pixels always detected regardless of luminance threshold

### Parameter Sweep
- [ ] Intensity: 0.5 -> 1.0 -> 2.0 -> 5.0
- [ ] Luminance threshold: 1.0 -> 2.0 -> 5.0
- [ ] Scatter radius: 32 -> 64 -> 128 -> 256 pixels
- [ ] Falloff exponent: 1.0 -> 2.0 -> 4.0

### Known Artifact Types
- False positives from specular highlights (not truly emissive)
- Quarter-res blockiness in scatter composite
- Depth bleed through thin geometry
- Over-brightness from multiple overlapping scatter sources

### Performance Budget
- Target: < 0.8ms at 1080p (quarter-res scatter pass)

### Test Locations: L3 (torches), L5 (bioluminescence), L8 (fire pit)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 18. Screen-Space Decals

**Source:** Deferred OBB projection with procedural patterns
**Class:** `ScreenSpaceDecalRenderer` (`ScreenSpaceDecalRenderer.cpp/h`)
**Shader:** `Decal_Main.hlsl`
**Pipeline:** Single compute pass
**VRAM:** ~4 MB at 1080p

### Visual Checklist
- [ ] Decals project correctly onto geometry surfaces
- [ ] Normal-aware attenuation (decals fade on angled surfaces)
- [ ] Per-decal lifetime management (fade out over time)
- [ ] No z-fighting or depth artifacts
- [ ] Patterns render correctly: solid, circle, splatter, impact
- [ ] Max 64 decals without visible performance degradation
- [ ] No decal projection onto incorrect surfaces (behind the volume)

### Parameter Sweep
- [ ] Global opacity: 0.5 -> 1.0
- [ ] Normal threshold: 0.3 -> 0.5 -> 0.8
- [ ] Decal count stress test: 1 -> 16 -> 32 -> 64

### Known Artifact Types
- Decal stretching on surfaces not aligned with projection axis
- Visible box edges where decal volume intersects geometry
- Z-fighting on coplanar surfaces

### Performance Budget
- Target: < 0.3ms at 1080p (single compute pass, 64 decals max)

### Test Locations: L1, L3, L8
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 19. Indirect Specular (SSR + Cubemap)

**Source:** Schlick Fresnel, roughness-based SSR/cubemap blending
**Class:** `IndirectSpecularRenderer` (`IndirectSpecularRenderer.cpp/h`)
**Shader:** `IndirectSpecular_Main.hlsl`
**Output:** SRV at t32, R16G16B16A16_FLOAT
**Pipeline:** Single compute pass (full-res)
**VRAM:** ~32 MB at 1080p

### Visual Checklist
- [ ] Smooth surfaces: SSR provides screen-accurate reflections
- [ ] Rough surfaces: cubemap provides stable, noise-free reflections
- [ ] Blend transition: no visible seam between SSR and cubemap zones
- [ ] Fresnel: reflections stronger at glancing angles (F0 = 0.04 dielectric)
- [ ] Metals: use albedo as F0, high reflectivity at all angles
- [ ] Roughness threshold: surfaces above 0.7 receive no specular (correct)
- [ ] Cubemap fallback: reflections present even when SSR has no hit

### Parameter Sweep
- [ ] Intensity: 0.3 -> 0.8 -> 1.0 -> 2.0
- [ ] Cubemap fallback: 0.0 -> 0.5 -> 1.0
- [ ] Fresnel bias: 0.02 -> 0.04 -> 0.1
- [ ] Roughness threshold: 0.5 -> 0.7 -> 0.9

### Known Artifact Types
- Visible seam between SSR zone and cubemap fallback
- Cubemap not matching scene lighting (stale capture)
- Over-bright specular on non-metallic surfaces (F0 too high)

### Performance Budget
- Target: < 0.5ms at 1080p (single full-res compute pass)

### Test Locations: L8 (metal/glass), L1 (wet stone), L7 (water surface)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 20. Volumetric Lighting (God Rays)

**Source:** Screen-space ray march, Henyey-Greenstein phase function
**Class:** `VolumetricLightingRenderer` (`VolumetricLightingRenderer.cpp/h`)
**Shaders:** `Volumetric_RayMarch.hlsl`, `Volumetric_Upsample.hlsl`
**Output:** SRV at t33, R16G16B16A16_FLOAT (scatter.rgb, transmittance.a)
**Pipeline:** 2-pass compute (half-res ray march -> bilateral upsample)
**VRAM:** ~20 MB at 1080p

### Visual Checklist
- [ ] Light shafts visible through tree canopy (god rays)
- [ ] Correct directionality (shafts point toward sun)
- [ ] Henyey-Greenstein anisotropy: brighter looking toward the sun
- [ ] Depth occlusion: shafts blocked by geometry correctly
- [ ] No visible stepping artifacts in ray march
- [ ] Half-res to full-res upsample: no blocky edges
- [ ] Transmittance: scene properly dimmed in volumetric shadow
- [ ] No light shafts in fully enclosed interiors (no sun)

### Parameter Sweep
- [ ] Intensity: 0.5 -> 1.0 -> 2.0 -> 5.0
- [ ] Scatter density: 0.005 -> 0.015 -> 0.05
- [ ] Anisotropy: 0.3 -> 0.5 -> 0.7 -> 0.9 (HG asymmetry)
- [ ] Num steps: 16 -> 32 -> 64 -> 128
- [ ] Max distance: 1000 -> 5000 -> 20000

### Known Artifact Types
- Banding from insufficient step count
- Sun direction not updating correctly
- Half-res upsample bleeding at depth edges
- God rays visible in enclosed interiors (phase detection error)

### Performance Budget
- Target: < 1.5ms at 1080p (64 steps, half-res)

### Test Locations: L1 (god rays through Whiterun gate), L2 (forest canopy), L6
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 21. Atmosphere (Physical Sky)

**Source:** Bruneton 2008 / Hillaire 2020 -- Rayleigh+Mie scattering, precomputed LUTs
**Class:** `AtmosphereRenderer` (`AtmosphereRenderer.cpp/h`)
**Shaders:** `AtmoTransmittance.hlsl`, `AtmoMultiScatter.hlsl`, `AtmoAerial.hlsl`, `CelestialBodies.hlsl`
**Output:** Transmittance SRV at t23, Scattering SRV at t24, Celestial SRV at t25
**Pipeline:** LUT precomputation (transmittance -> multi-scatter -> aerial perspective) + celestial pass

### Visual Checklist
- [ ] Sky color matches physically-based Rayleigh blue at midday
- [ ] Sunset/sunrise: correct red-orange gradient at horizon
- [ ] Multi-scattering prevents sky from being too dark
- [ ] Aerial perspective: distant mountains fade with atmospheric haze
- [ ] Sun disk rendered with correct angular size and intensity
- [ ] Moon: correct phase rendering (new/crescent/quarter/gibbous/full)
- [ ] Stars: visible at night, fade near horizon and in moon's vicinity
- [ ] LUT updates correctly when sun angle changes significantly
- [ ] No banding in sky gradient (LUT resolution adequate)
- [ ] Transmittance/scattering LUTs used by other renderers (trees, volumetric)

### Parameter Sweep
- [ ] Sun disk intensity: 0.5 -> 1.0 -> 2.0
- [ ] Star intensity: 0.0 -> 0.5 -> 1.0 -> 2.0
- [ ] Moon phase: cycle through 0.0 -> 0.25 -> 0.5 -> 0.75 -> 1.0

### Known Artifact Types
- Banding in sky gradients (LUT resolution too low)
- Sun disk too bright or wrong size
- Moon/stars visible during daytime
- Aerial perspective too strong or too weak

### Performance Budget
- LUT precompute: < 2.0ms (amortized, only on sun angle change)
- Celestial pass: < 0.3ms at 1080p
- Per-frame sky eval: < 0.2ms

### Test Locations: L1, L2, L6 (all exteriors, different times of day)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 22. Underwater Rendering

**Source:** Beer-Lambert absorption, trochoidal caustics, Gerstner waves, Tyndall scattering
**Class:** `UnderwaterRenderer` (`UnderwaterRenderer.cpp/h`)
**Shaders:** `Underwater_Caustics.hlsl`, `Underwater_GodRays.hlsl`, `Underwater_Waves.hlsl`, `UnderwaterComposite.hlsl`
**Pipeline:** 4-pass (caustics CS -> god rays CS -> wave distortion CS -> composite PS)
**Pipeline stage:** PrePresent, priority 5

### Visual Checklist
- [ ] Only activates when player is submerged (PlayerData::Water check)
- [ ] Beer-Lambert: red light absorbed first (scene goes blue-green with depth)
- [ ] Caustic patterns on underwater surfaces (animated, 3-octave trochoidal)
- [ ] God rays: radial light shafts from sun position (16 depth-tested samples)
- [ ] Wave distortion: Gerstner waves distort view UV (4-octave)
- [ ] Photic zone grading: deeper = darker/bluer, shallow = brighter
- [ ] Snell's window: visible when looking up at water surface
- [ ] Wet lens transition: visual effect when surfacing/submerging
- [ ] Depth fog: exponential fog increases with distance
- [ ] Tyndall scattering: particle-in-water haze effect

### Parameter Sweep
- [ ] Absorption R/G/B: default (0.45/0.07/0.02) -> extreme -> subtle
- [ ] Caustic intensity: 0.0 -> 0.3 -> 0.6 -> 1.0
- [ ] God ray intensity: 0.0 -> 0.2 -> 0.4 -> 0.8
- [ ] Wave intensity: 0.0 -> 0.15 -> 0.3 -> 0.6
- [ ] Fog density: 0.005 -> 0.02 -> 0.05

### Known Artifact Types
- Underwater effect activating above water (state detection error)
- Wave distortion too strong causing nausea-inducing view
- God rays visible without sun (underground water)
- Wet lens effect stuck on screen after surfacing

### Performance Budget
- Target: < 1.5ms at 1080p (all 4 passes, quarter-res caustics/rays)

### Test Locations: L7 (Riften canal submersion), L2 (river submersion)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 23. Dynamic Cubemap

**Source:** Per-frame 6-face cubemap capture with temporal blending
**Class:** `DynamicCubemapRenderer` (`DynamicCubemapRenderer.cpp/h`)
**Shaders:** `Cubemap_Capture.hlsl`, `Cubemap_MipGen.hlsl`
**Output:** SRV at t30, 128x128x6 R11G11B10_FLOAT cubemap with mip chain
**Pipeline:** 1 face per frame (6-frame rotation) + mip generation

### Visual Checklist
- [ ] Metal/glass surfaces reflect actual environment (not baked)
- [ ] Reflections update with time of day (not static)
- [ ] Mip chain: smooth transition from sharp to blurry reflections by roughness
- [ ] Temporal blending: no popping when face updates
- [ ] Correct for current weather conditions (clear vs overcast reflected)
- [ ] No visible cube seams at face boundaries

### Parameter Sweep
- [ ] Face resolution: 64 -> 128 -> 256
- [ ] Update frequency: 0.5 -> 1.0 -> 2.0 faces/frame
- [ ] Blend speed: 0.01 -> 0.1 -> 0.5

### Known Artifact Types
- Stale reflections (update too slow for fast weather change)
- Visible cube seams between face edges
- Mip level pop (roughness-based selection not smooth)
- Performance spike on face capture frame

### Performance Budget
- Target: < 0.3ms per frame (1 face capture + mip gen, amortized)
- Full cycle: 6 frames for complete update

### Test Locations: L8 (Dragonsreach armor/weapons), L1 (metal surfaces)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 24. ToneMap Manager (Auto-Exposure + Legacy Tone Map)

**Source:** Histogram-based auto-exposure with temporal smoothing
**Class:** `ToneMapManager` (`ToneMapManager.cpp/h`)
**Shaders:** `ToneMap_AutoExposure.hlsl`, `ToneMap_Main.hlsl`
**Pipeline:** 2-pass (auto-exposure CS from histogram -> tonemap PS)

### Visual Checklist
- [ ] Auto-exposure correctly brightens dark scenes and dims bright scenes
- [ ] Temporal smoothing: no abrupt exposure jumps between frames
- [ ] Adaptation speed configurable and feels natural (2.0 EV/sec default)
- [ ] AgX/ACES/Reinhard tone curves produce expected output
- [ ] HDR10 PQ encoding produces correct nit levels on HDR displays
- [ ] Vanilla influence blend: respects game's ImageSpace parameters
- [ ] No exposure pumping on scene with mixed bright/dark regions

### Performance Budget
- Target: < 0.3ms at 1080p (CS + PS)

### Test Locations: L1/L3 transition (bright to dark), L2 (dawn/dusk adaptation)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 25. Luminance Histogram

**Source:** 256-bin GPU histogram with compute reduction
**Class:** `LuminanceHistogram` (`LuminanceHistogram.cpp/h`)
**Shaders:** `Histogram_Main.hlsl`, `Histogram_Reduction.hlsl`, `Histogram_Metering.hlsl`
**Output:** SRV at t17 (histogram data for auto-exposure)

### Visual Checklist
- [ ] Histogram bins cover full luminance range (no clipping at extremes)
- [ ] Metering mode: center-weighted works correctly for gameplay
- [ ] Reduction produces correct average luminance for exposure
- [ ] No histogram stalls causing exposure lag

### Performance Budget
- Target: < 0.2ms at 1080p

### Test Locations: L1, L3, L6 (extreme luminance range)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 26. Denoise Manager

**Source:** A-Trous wavelet + bilateral + temporal denoise
**Class:** `DenoiseManager` (`DenoiseManager.cpp/h`)
**Shaders:** `Denoise_ATrous.hlsl`, `Denoise_Bilateral.hlsl`, `Denoise_BilateralRGBA.hlsl`, `Denoise_Temporal.hlsl`, `Denoise_TemporalRGBA.hlsl`

### Visual Checklist
- [ ] A-Trous: progressive spatial filter without visible artifacts
- [ ] Bilateral: preserves edges while smoothing noise
- [ ] Temporal: ping-pong history rejects stale data on camera motion
- [ ] No over-blurring of fine detail
- [ ] Edge preservation on depth discontinuities

### Performance Budget
- Target: varies by consumer (profiled as part of each effect pipeline)

### Test Locations: Validated indirectly through effects that consume it
- Grade: ___
- Notes:


---

## 27. Motion Vector Generation

**Source:** Per-pixel motion vectors from depth + camera matrices
**Class:** `MotionVectorGen` (`MotionVectorGen.cpp/h`)
**Shader:** `MotionVector_Main.hlsl`

### Visual Checklist
- [ ] Motion vectors correct for camera rotation (uniform field)
- [ ] Motion vectors correct for camera translation (depth-dependent parallax)
- [ ] Zero motion on stationary camera
- [ ] No motion vector noise on sky pixels

### Performance Budget
- Target: < 0.2ms at 1080p

### Test Locations: Any exterior while moving/rotating camera
- Grade: ___
- GPU time: ___ms
- Notes:


---

## 28. TAA (Temporal Anti-Aliasing)

**Source:** Variance clipping neighborhood clamping + temporal reprojection
**Class:** `TAAManager` (`TAAManager.cpp/h`)
**Shader:** `TAA_Resolve.hlsl`

### Visual Checklist
- [ ] Edge aliasing reduced on geometry silhouettes
- [ ] No excessive ghosting on moving objects
- [ ] Sharpness preserved (not overly blurry)
- [ ] Correct reprojection using motion vectors
- [ ] No jitter visible on static camera

### Performance Budget
- Target: < 0.3ms at 1080p

### Test Locations: L1, L2 (foliage edges), L8 (architecture edges)
- Grade: ___
- GPU time: ___ms
- Notes:


---

## Integration Tests

After validating each effect individually, test combinations:

### Core Stack (GTAO + Contact Shadows + Skylighting + Compositor)
- [ ] All three produce valid SRVs simultaneously
- [ ] Compositor blends all three correctly
- [ ] No SRV slot conflicts (t20, t28, t29)
- [ ] Total GPU cost < 3.0ms at 1080p
- Grade: ___

### Reflection Stack (SSR + Indirect Specular + Dynamic Cubemap)
- [ ] SSR confidence correctly drives cubemap fallback
- [ ] No double-reflection artifacts
- [ ] SRV slots: t27 (SSR), t32 (IndirectSpecular), t30 (Cubemap)
- [ ] Total GPU cost < 2.5ms at 1080p
- Grade: ___

### Post-Process Stack (Bloom + DoF + Lens + Color Pipeline + ToneMap)
- [ ] Correct execution order: Bloom(10) -> Lens(20) -> DoF(30) -> Color(50) -> ToneMap(100)
- [ ] No double-tonemapping (ColorPipeline CPS_ToneMap vs ToneMapManager)
- [ ] Bloom provides SRV to ColorPipeline correctly
- [ ] Total GPU cost < 5.0ms at 1080p
- Grade: ___

### Full Pipeline (All Effects Enabled)
- [ ] Total frame time < 16.6ms (60 fps at 1080p) on RTX 3070 or equivalent
- [ ] No SRV slot conflicts across all renderers
- [ ] No visual artifacts from effect interaction
- [ ] Default preset looks significantly better than vanilla
- [ ] No crashes during 15-minute play session
- Grade: ___

---

## Edge Case Tests

- [ ] Loading screen transition: no crash, effects resume correctly
- [ ] Cell transition (interior <-> exterior): no stale buffers
- [ ] Fast travel: effects reinitialize correctly
- [ ] Console open/close: no effect timing disruption
- [ ] Alt-tab: effects survive focus loss and recovery
- [ ] Resolution change: textures recreated at new resolution
- [ ] Save/load: no state corruption

---

## Sign-Off

| Milestone | Criteria | Date | Status |
|-----------|----------|------|--------|
| 2A Complete | Core effects (Compositor + GTAO + Shadows + Sky + Bloom) PASS | ___ | ___ |
| 2B Complete | Advanced effects (SSR + SSGI + ToneMap + DoF + Clouds) PASS | ___ | ___ |
| 2C Complete | Default preset looks better than vanilla, no artifacts | ___ | ___ |
| Phase 2 Gate | 1-hour play session with zero visual artifacts | ___ | ___ |
