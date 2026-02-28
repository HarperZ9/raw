# SkyrimBridge Shader Suite — Complete Technical Analysis

*Comprehensive analysis of the SkyrimBridge / Silent Horizons ENB project*
*55+ files, ~21,500 lines of shader code + C++ integration layer*

---

## Executive Summary

SkyrimBridge is a production-grade ENB shader framework for Skyrim SE that bridges real-time game engine state to HLSL post-processing shaders via an SKSE plugin. The system provides 100+ float4 parameters across 17 tracking domains, enabling shader effects that respond dynamically to weather, combat, lighting, equipment, and player state — capabilities impossible in generic post-processing frameworks like ReShade.

The codebase consists of three tiers:

1. **Infrastructure** — SkyrimBridge.fxh data bridge, helper libraries, D3D11 shader hooks
2. **Addon Shaders** — Eight standalone SB_*.fx modules implementing modern rendering techniques
3. **Silent Horizons ENB** — Full preset with rebuilt core shaders consuming bridge data

---

## I. Infrastructure Layer

### SkyrimBridge.fxh (561 lines)

The foundational data interface. Declares 100+ `float4` parameters populated every frame by the SKSE plugin via `ENBSetParameter()`. Without this header included in a shader, pushed data is silently discarded by ENB.

**17 Tracking Domains:**

| Domain | Key Parameters | Use Cases |
|--------|---------------|-----------|
| **Celestial** | Sun/Masser/Secunda NDC positions, directions, colors, moon phases, time-of-day | Sunsprite positioning, moonlight color, time-dependent effects |
| **Atmosphere** | Sky gradients (upper/lower/horizon), ambient/sunlight colors, cloud tints | Fog color matching, sky-aware bloom, atmospheric scattering |
| **Fog** | Near/far colors, density curves, height falloff, water altitude | Height fog, volumetric scattering, underwater detection |
| **Weather** | Wind direction/speed, precipitation type/intensity, lightning, wetness/puddles/snow, transition state | Rain droplets, frost, wet surface darkening, lightning flash |
| **Player** | World position, health/stamina/magicka, movement state, combat flags, water submersion | Combat-reactive effects, health vignette, underwater shader |
| **Camera** | FOV, clip planes, aspect ratio, pitch/yaw, View/Proj/ViewProj/PrevVP/InvVP matrices (row-major 4×4) | Motion vectors, world position reconstruction, temporal reprojection |
| **Interior Lighting** | Ambient/directional colors, fog parameters | Interior-specific rendering adjustments |
| **Shadow/Directional** | Shadow caster direction, diffuse/ambient colors | Contact shadows, directional light estimation |
| **Active Magic FX** | Night Eye, Detect Life/Dead, Ethereal, time manipulation, elemental damage, status effects | Vision mode rendering, elemental screen effects |
| **Render State** | Frame count, delta time, screen dimensions, TAA jitter | Temporal effects, animation, jitter patterns |
| **ImageSpace** | Game's own HDR/cinematic/DOF/IMOD tint/blur state | AGIS (respecting game's visual intent) |
| **Nearby Lights** | 3 nearest point/spot lights (position, radius, color, intensity), summary stats | Local light bloom, point light god rays |
| **Actor Values** | All 160+ actor values: resistances, combat stats, skills, movement | Gameplay-reactive visual feedback |
| **Crosshair Target** | Target distance, formType, isActor, health/level/hostility | Auto-focus DOF, target highlighting |
| **Equipment** | Weapon types (20 enums), damage, enchantments, armor class, torch state | Weapon-specific effects, torch glow |
| **Quest State** | Main quest stage, completed count, tracked objective | Story-reactive atmosphere |
| **UI/Menu State** | Menu flags, HUD visibility, crafting/book/lockpick/console states | Menu-aware post-processing reduction |

**Key Helper Functions:**
- `SB_WorldPosFromDepth()` — Reconstruct world position from depth + inverse VP matrix
- `SB_MotionVector()` — Per-pixel motion vectors from current/previous VP matrices (exact, no optical flow needed)
- `SB_EvaluateNearbyLights()` — Evaluate 3 nearest lights with inverse-square falloff
- `SB_LightToScreen()` — Project nearby light positions to screen space
- `SB_GetAutoFocusDistance()` — Crosshair-based auto-focus for DOF
- `SB_ShouldReducePostFX()` — Menu/loading screen detection for effect reduction
- `SB_SceneWetness()`, `SB_SnowCoverage()` — Weather state queries
- `SB_IsActive()` — Bridge availability check for graceful fallback

### enbHelper_Common.fxh (634 lines)

Comprehensive utility library with 15 sections. Highlights:

**Christensen-Burley Diffusion Profile (Sections 8-15):**
Production-quality SSS implementation matching Disney/Pixar standards:
- `BurleyDiffusion(r, d)` — R(r) = A·exp(-r/d) + (1-A)·exp(-r/3d)
- `BurleyDiffusion3(r, ScatterDist)` — Per-channel evaluation for skin (R:2.0mm, G:0.8mm, B:0.4mm)
- 13-tap separable kernel at importance-sampled positions [0, ±1, ±2, ±3.25, ±5, ±7.5, ±11]
- Per-channel weights encoding characteristic falloff (blue narrow, red wide)
- `EstimateCurvature()` — Laplacian of normal field for thickness approximation
- `EstimateSpecularFresnel()` — Fresnel-weighted specular isolation to preserve highlights during blur
- `NormalBilateralWeight()` — Cross-boundary rejection for SSS kernel
- `BurleyTransmission()` — Translucency for thin geometry (ears, nostrils)
- `PennerWrapLighting()` — Per-channel wrap diffuse with curvature-adaptive scattering
- `TemporalIGN()` — Frame-varying interleaved gradient noise for kernel jitter

**General Utilities:**
- Color space: Lin↔sRGB (accurate piecewise + fast approx), RGB↔HSL (Ian Taylor)
- Interpolation: LinearStep, smootherstep (6th-order Perlin)
- Noise: InterleavedGradientNoise, Random/RandomGauss
- Filtering: BicubicFilter (Catmull-Rom, 4 bilinear taps)
- DNISep() — Day/Night/Interior interpolation system
- Technique macros: TECH11, TWOPASSTECH11 for clean technique definitions

**Design Pattern:** No global variables declared — uses macros (`_HLP_PixelSize`) to avoid X3003 redefinition conflicts. Each .fx file declares its own `PixelSize`/`ScreenRes` from macros.

### ShaderHooks.h / .cpp (298 + 629 lines)

D3D11 DeviceContext vtable hook system for material classification and pipeline interception.

**Hooked Functions (11 total):**
- `Draw` / `DrawIndexed` — Material classification + pre/post callbacks
- `PSSetShader` / `VSSetShader` / `HSSetShader` / `DSSetShader` / `GSSetShader` — Full shader stage tracking
- `IASetVertexBuffers` — Vertex layout fingerprinting (stride-based mesh type detection)
- `Map` / `Unmap` — Constant buffer write interception (captures CB data at Unmap)
- `OMSetRenderTargets` — Render pass detection

**Material Classification (14 categories):**
Sky, Terrain, Architecture, Foliage, Skin, Hair, Eye, Water, Effect, Metal, Cloth, Snow (tessellated), Decal, Other

**Tessellation Detection:**
Classifies HS/DS combinations into None, Snow, Terrain, Water, Unknown — enabling snow-specific rendering without ESP queries.

**Architecture:**
- Singleton pattern with `ShaderHooks::Get()`
- Per-draw `DrawState` snapshot assembled from tracked pipeline state
- Shader→MaterialID cache with mutex-protected hash map
- Callback system: `onPreDraw`, `onPostDraw`, `onCBCaptured`
- Maximum 16 simultaneous Map/Unmap tracking slots
- 512-byte CB capture buffer per mapping

---

## II. Addon Shaders (SB_*.fx)

Eight standalone shader modules implementing modern rendering techniques, all consuming SkyrimBridge data.

### SB_GTAO.fx — Ground Truth Ambient Occlusion

Multi-slice horizon-based AO (Jimenez 2016 variant):
- Configurable slices (1-8) and steps per slice (2-12)
- IGN spatial noise for slice angle jitter
- Per-side horizon search with distance-weighted falloff
- Analytic cosine-weighted visibility integral
- G-buffer normal readback with depth-derived fallback
- **Material-aware intensity:** Skin/hair AO multiplied by `skinMult` (default 0.5), metal boosted by `metalMult` (1.2), foliage reduced by `foliageMult` (0.8)

### SB_ContactShadows.fx — Screen-Space Contact Shadows

Ray-marched micro-shadows along sun direction:
- Uses `SB_Shadow_SunDir` for light direction (ground truth, not estimated)
- Normal bias to prevent self-shadowing
- Thickness-windowed hit test to avoid false positives
- Distance-dependent penumbra softness (fade based on step distance)
- N·L early-out for back-facing surfaces
- **Material-aware:** Skin receives softened shadows via `csSkinSoft` parameter

### SB_SSR.fx — Screen-Space Reflections

View-space ray marching with binary refinement:
- Linear ray march (up to 128 steps) + binary refinement (up to 16 steps)
- Schlick Fresnel with material-derived F0 (0.04 dielectric, 0.9 metallic)
- Edge fade at screen borders
- Roughness-based fade (configurable max roughness threshold)
- **Material-aware:** Skin/hair excluded entirely, metal gets `ssrMetalBoost` (1.5×), water gets separate `ssrWaterIntens` (1.2×)
- Metallic reflections tinted by surface albedo
- PBR surface readback via `SB_GetSurface()` for roughness/metalness/materialID

### SB_HDRBloom.fx — HDR-Aware Bloom

Pre-tonemap HDR bloom with material-aware thresholding:
- Reads `SB_HDR_Scene` texture (pre-tonemap linear HDR)
- Soft-knee threshold curve (Karis 2014)
- Per-material threshold adjustment: skin raises threshold (reduces bloom), eyes lower it (catchlight emphasis), metals lower it (specular bloom), effects lower it (emissive boost)
- 13-tap Gaussian weights (σ ≈ 4.0), 2D convolution (diagonal sampling)
- Per-material composite multipliers match threshold adjustments
- Configurable bloom tint with slight warm shift default (1.0, 0.95, 0.9)

### SB_VolumetricFog.fx — Volumetric Fog / God Rays

Screen-space volumetric light scattering:
- Henyey-Greenstein phase function with configurable anisotropy (default g=0.7)
- Height fog with exponential falloff relative to player position
- Beer-Lambert transmittance integration with early-out at <1% transmittance
- Inscatter from `SB_Vol_Scatter` and `SB_Vol_Color` (bridge volumetric data)
- Sun contribution using `SB_Sun_Color` and `SB_Sun_Direction`
- Sky pass: simple radial god rays toward `SB_Sun_NDC` position
- IGN dither for temporal noise reduction
- Configurable weather color blending

### SB_MotionBlur.fx — Per-Object Motion Blur

Velocity-based directional blur using engine motion vectors:
- Reads `SB_GBuffer_Motion` for per-pixel velocity (exact, from engine matrices)
- Bidirectional sampling along velocity direction (forward + backward)
- Depth-weighted bilateral rejection to prevent background/foreground bleeding
- Velocity threshold for minimum blur activation
- **Material-aware:** Skin/hair/eye reduced to 15% blur (`mbSkinMult`), effects boosted to 150% (`mbEffectMult`), sky excluded entirely
- Depth-based falloff (distant objects blur less)
- Max velocity clamp in pixels (configurable up to 64px)

### SB_TemporalAA.fx — Temporal Anti-Aliasing

Reprojection-based TAA with variance clipping:
- Per-pixel reprojection via `SB_GBuffer_Motion`
- YCoCg color space for tighter clamping (better than RGB AABB)
- 3×3 neighborhood variance clip (σ-based, tighter than min/max AABB)
- Configurable clamp gamma for clip box expansion
- Motion-based rejection: high velocity → more current frame weight
- Post-TAA sharpening via 4-tap negative lobe (cross pattern)
- **Material-aware:** Skin/hair get stronger history blend (0.05 vs 0.1) for smoother appearance, effects get weaker history (0.3) to avoid smearing
- Falls back to `TextureOriginal` for previous frame (ENB ping-pong)

### SB_MaterialPBR.fx — Material-Aware PBR Enhancement

Per-pixel physically-based rendering enhancement:
- **GGX Specular:** Full Cook-Torrance microfacet model (D_GGX + G_Smith + F_Schlick)
- **Skin SSS:** 8-tap disc-pattern diffusion with depth bilateral rejection, skin-masked, configurable scatter color (default warm: 1.0, 0.4, 0.25)
- **Metal:** Colored Fresnel using albedo as F0, GGX specular with `SB_Sun_Color`
- **Eye:** Ultra-smooth (roughness 0.05) catchlight specular
- **Cloth:** View-angle-dependent softening (Fresnel-based edge darkening)
- **Weather Integration:**
  - Wet surface darkening from `SB_SceneWetness()` (rain-reactive)
  - Wet gloss boost (reduced roughness when wet)
  - Snow coverage shifts terrain albedo toward white via `SB_SnowCoverage()`

---

## III. Silent Horizons ENB Preset

### enbeffectprepass.fx (3,463 lines) — The Rendering Workhorse

11 techniques, 17 passes. The most complex file in the suite:

**Techniques 0-2: Separable SSS**
- Two-pass separable Christensen-Burley diffusion
- Runtime kernel evaluation from user scatter distances
- Depth-scaled radius (world-space consistent scattering)
- Normal-bilateral edge rejection
- Shadow-bleed asymmetry (lit → shadow OK, reverse blocked)
- Temporal IGN jitter on kernel offsets
- Surface-following via depth gradient

**Techniques 3-4: GTAO + SSGI**
- GTAO: Multi-slice horizon search with analytic visibility integral, thickness heuristic, temporal rotation, multi-bounce energy approximation, bent normal output
- SSGI: Single-bounce indirect illumination, golden-angle low-discrepancy sampling, depth rejection + normal weighting, configurable color bleed

**Technique 5: Contact Shadows**
- Ray-march along sun direction in screen space
- Thickness-windowed hit test, distance-dependent penumbra, N·L fade

**Techniques 6-10: Compositing + Enhancement**
- Effects composite (AO + GI + CS + skin detail + clarity)
- Painterly filter (anisotropic Kuwahara, Kyprianidis et al. 2009)
- Atmospheric fog, skin micro-detail, realism enhance

**SSS Final Composite:**
- Fresnel-enhanced specular preservation
- Full color grading (exposure/contrast/saturation/tint/levels)
- Penner pre-integrated wrap lighting
- Backlit translucency with Burley transmission profile
- Subsurface color shift (hemoglobin absorption in shadow)
- Multi-factor skin mask (material + depth + normal variance)

### enbeffect.fx (1,411 lines) — HDR Post-Processing Compositor

Single technique, single pass. Clean pipeline architecture:

**Two Pipeline Modes:**
- Standard: Bloom → Tonemap → Color Grading → AGIS → Film Emu → Dither
- Pre-Grade: Bloom → Color Grading (in HDR) → Tonemap → AGIS → Film Emu → Dither

**7 Tonemappers** (per-TOD parameterized):
0. Linear (bypass)
1. Reinhard Extended (white point)
2. Hejl-Burgess Filmic (white point)
3. Uncharted 2 / Hable (6-param curve)
4. VDR — Variable Dynamic Range (middle grey, toe contrast, shoulder)
5. ACES — Academy Color Encoding System (Hill + Narkowicz fittings)
6. Kitsuune — Custom S-curve

**Two Tonemapping Methods:** Per-channel (preserves hue) vs Luminance-based (compress luminance, reapply chroma)

**Color Grading:** Per 7-TOD (dawn/sunrise/day/sunset/dusk/night/interior): Exposure (EV), Lottes-pivot Contrast, Saturation, 3-channel Balance, Gamma + Correction, RGBA Tint

**AGIS — Automatic Game Image-Space:**
Reads game's own imagespace modifiers from SkyrimBridge (`SB_IS_Cinematic`/`SB_IS_CineTint`) and applies them as bounded adjustments. Prevents the ENB from overriding game-authored effects (vampire lord, hit flash, skooma). Delta mode preserves preset grading while respecting game intent. Per-DNI min/max clamping.

**Film Emulation:** Input/output gamut transforms (sRGB, Rec.709, DCI-P3, ACEScg). Log encoding (ACEScg, LogC, S-Log3, DaVinci Intermediate).

**SkyrimBridge Reactive Effects:** Lightning flash → exposure spike, Rain/snow → desaturation, Night Eye → blue/green shift + brightness boost

### enbbloom.fx (1,012 lines) — Physically-Based Progressive Bloom

Dual-filter Kawase blur (Bjorge/ARM 2015):
- Soft-knee threshold (Karis 2014) with per-pass RGBA balance weights
- Depth separation: Near (tight), Far (wide/soft), Sky (independent)
- SkyrimBridge integration: Lightning → bloom spike, Rain → bloom dampening, Fog → bloom softening, Nearby lights → point bloom, Game IS_HDR.bloomScale → engine respect

### enbadaptation.fx (682 lines) — Eye Adaptation

Multi-mip weighted luminance metering (mips 3-6):
- Center-focus weighting, sky exclusion, emissive outlier rejection
- 7-TOD exposure compensation with min/max EV clamps
- Asymmetric adaptation (scotopic slow / photopic fast)
- Hysteresis dead zone, maximum rate limiter, first-frame snap
- SkyrimBridge: Combat speed, Night Eye, lightning, torch, storm, interior ambient, menu pause, slow-time, killcam, dialogue, sun elevation bias, nearby lights, altitude, IS_HDR speed, interior transition smoothing

### enblens.fx (3,014 lines) — Master Lens Effects

Ghost/hoop/ring lens flares, lens reflections, anamorphic flares, starburst, lens dirt (procedural + texture), WeatherFX integration. SkyrimBridge: game sun color blending (`SKYRIMBRIDGE_SUNCOLOR_BLEND`), lightning flash on all flares.

### enbsunsprite.fx (1,926 lines) — Sun Sprite/God Rays

Custom sun intensity detection with optional SkyrimBridge sun color override. Lightning flashes all flares. Hoop AA, starburst AA. Multi-pass anamorphic + starburst + corona.

### enbdepthoffield.fx (1,657 lines) — Advanced DOF

Based on Marty McFly's ADOF 3.0, extended by Kitsuune. Optical vignette, chromatic aberration, spherical aberration, diffraction, graining, focusing tool, stylized bokeh shapes, bilateral filtering. SkyrimBridge: `SB_GetAutoFocusDistance()` for crosshair-based auto-focus.

### enbunderwater.fx (1,574 lines) — Physically-Based Underwater

Extensive underwater rendering:
- PBR wavelength-dependent absorption (Beer-Lambert per-channel)
- Dual-layer noise caustics or Voronoi cell caustics
- Screen-space god rays from sun direction
- Floating particles, procedural bubbles, bioluminescence
- Snell's window / total internal reflection
- Tyndall / directional particulate scattering
- Wet lens effect near surface
- Wave distortion with barrel lens model

---

## IV. Procedural Effect Libraries

### Effect_ProceduralWeatherFX.fxh (772 lines)

Fully procedural rain and frost — no textures required:
- **Rain:** Contact-angle droplet model, meniscus normals, internal caustic highlights, physics-based drip trails with capillary thinning
- **Frost:** Multi-scale dendritic growth with recursive branching, 6-fold crystal symmetry, analytical gradient Sobel normals, sub-crystal fine detail
- High-quality integer hash functions (xxHash-inspired) replacing sin-based PRNGs
- C2-continuous value noise (quintic Hermite interpolation)

### Effect_CinematicFX.fxh (1,310 lines)

Eight physically-motivated cinema effects:
1. **Lens Diffusion** — Pro-Mist/Glimmerglass simulation, dual-kernel PSF (16-tap local + TextureDownsampled far-field)
2. **Film Halation** — Wavelength-dependent scatter (red 1.5× wider than blue), soft-knee threshold
3. **Light Leaks / Film Burns** — Scene brightness adaptation, hash-based intermittency, 3-layer FBM noise
4. **Gate Weave / Film Jitter** — Frame-quantized weave with analytical velocity for motion blur
5. **Cinematic Letterbox** — Aspect ratio masking with projected-black grain
6. **Anamorphic Lens** — Bilinear-optimized 13-tap horizontal blur, per-channel CA, field curvature, focus breathing
7. **Optical Vignette** — cos⁴(arctan(r)) natural + mechanical plateau, cat-eye aperture, per-channel wavelength-dependent falloff
8. **Film Damage** — Scratches, sprocket burns, fiber dust, gate hair, splice marks, chemical fading

### Effect_ProceduralLensDirt.fxh (289 lines)

Five procedural layers replacing static textures:
1. Dust particles — Voronoi-distributed microscopic specks
2. Smudge streaks — Domain-warped FBM fingerprint patterns
3. Radial wipe marks — Concentric cleaning streaks
4. Water spots — Dried mineral rings from evaporated droplets
5. Film residue — Low-frequency haze from coating degradation

### Effect_AtmosphericFog.fxh (304 lines)

Atmospheric fog with depth-validated sky color sampling, Poisson-disc sky estimation, aerial perspective desaturation, DNI-separated parameters. Fixed legacy FOG semantic collision by using FOGP prefix.

### Effect_CRTShader.fxh (182 lines)

CRT monitor simulation: phosphor mask, scanline structure, barrel distortion screen curvature, overscan, phosphor bloom, corner shadow, brightness/contrast/saturation.

---

## V. UI Configuration System

The enbUI_*.fxh files define ENB GUI parameters using SHADERGROUP and SEPARATE_VAR macros for organized INI sections. Total ~2,500 lines across 10 files, providing DNI separation, tooltips, and spinner widgets for every effect parameter.

**enbUI_Primer.fxh** (295 lines) — Master UI framework: file headers, whitespace, SEPARATE_VAR macro for 3-TOD (Day/Night/Interior) or 7-TOD (Dawn/Sunrise/Day/Sunset/Dusk/Night/Interior) parameter separation via ExtSep7().

---

## VI. Comparison with MartysMods (iMMERSE)

### Where SkyrimBridge Has the Advantage

| Capability | SkyrimBridge | MartysMods |
|-----------|-------------|-----------|
| **Motion Vectors** | Engine-provided via VP matrices (exact) | 8-level pyramid optical flow (approximate, ~2000 lines) |
| **Material Classification** | 14 categories from shader hooks | Must infer from depth/color heuristics |
| **Light Sources** | 3 nearest lights with position/radius/color | No light detection capability |
| **Weather State** | Rain, snow, wind, lightning, wetness, temperature | No game state access |
| **Sun Position** | NDC + world direction + color (ground truth) | Must estimate from brightest pixel |
| **Player/Combat State** | Health, stamina, combat flags, equipment | No game state access |
| **Menu Awareness** | 17+ menu state flags for effect reduction | No menu detection |
| **Performance** | No optical flow pyramid, no albedo estimation | Expensive reconstruction passes |

### Where MartysMods Has the Advantage

| Capability | MartysMods | SkyrimBridge Equivalent |
|-----------|-----------|----------------------|
| **QMC Sampling** | Roberts R₂, Owen-scrambled Sobol, stratification | Simple hash-based RNG + temporal IGN |
| **Bounded VNDF** | State-of-the-art GGX importance sampling for SSR | Linear ray march + binary refinement |
| **FFT Bloom** | Frequency-domain convolution (physically accurate diffraction) | Dual-filter Kawase (efficient but not physically exact) |
| **Tetrahedral LUT** | 4 taps vs 8 for color grading | Standard trilinear LUT |
| **SH Hallucination** | Estimate L2 ZH from L1 for better angular resolution | No spherical harmonics |
| **Albedo Estimation** | 9-level Kuwahara pyramid + exposure fusion | Relies on game G-buffer (when available) |
| **Feature Gates** | DX9/10/11/12/Vulkan/OpenGL fallbacks | DX11 only (ENB constraint) |
| **Cross-Game** | Works in any ReShade-supported game | Skyrim SE only |

### Directly Portable Techniques

1. **Bounded VNDF Sampling** → Upgrade SB_SSR.fx importance sampling
2. **Tetrahedral LUT Interpolation** → Faster color grading in enblut.fxh
3. **B-spline Bicubic** → 25 ALU, 4-tap upsampling (SkyrimBridge has Catmull-Rom already)
4. **FFT Bloom** → Physically accurate diffraction for enbbloom.fx (if ENB render targets allow)
5. **SH Hallucination** → Better angular resolution if RTGI/radiance caching is added
6. **Feature Gate Architecture** → Clean ENB vs Community Shaders backend split

---

## VII. Architecture Patterns

### Data Bridge Pattern
SKSE plugin pushes flat float4 arrays via `ENBSetParameter()`. Shaders pull what they need. No callbacks, no marshaling — just parameter declaration and reading. Enables any shader to opt into any game data by including SkyrimBridge.fxh.

### Material-Aware Processing
Every addon shader queries `SB_ReadMaterialID()` or `SB_GetSurface()` and adjusts behavior per-material. This is the system's signature capability — effects that understand *what* they're rendering.

### Time-of-Day Separation
`DNISep()` and `ExtSep7()` provide automatic interpolation of parameters across 3 or 7 time-of-day periods. Single parameter definition with day/night/interior variants reduces complexity.

### Graceful Degradation
All SkyrimBridge-dependent code is gated behind `SB_IsActive()`. Shaders work standalone (with reduced capability) when the SKSE plugin is absent.

### Helper Library Modularity
`enbHelper_Common.fxh` provides utilities via macros (no global variable pollution). Each .fx file owns its own namespace. Prevents the X3003 redefinition errors that plague ENB shared code.

### Separable Pipeline
The rendering pipeline is strictly ordered: SSS → GTAO → SSGI → Contact Shadows → Composite → Painterly → Bloom → Adaptation → Main Effect → Post Pass. Each stage reads from previous results via ENB render targets.

---

## VIII. Codebase Statistics

| Category | Files | Lines |
|----------|-------|-------|
| Core Infrastructure | 4 | ~2,100 |
| Addon Shaders (SB_*.fx) | 8 | ~620 |
| Main ENB Shaders | 8 | ~14,700 |
| Effect Libraries | 5 | ~2,860 |
| UI Configuration | 10 | ~2,500 |
| C++ Integration | 2 | ~930 |
| Documentation | 3 | ~1,200 |
| Configuration INI | 10 | ~12,000 |
| **Total** | **50+** | **~37,000** |

---

## IX. Key Technical Strengths

1. **Ground Truth Data** — Direct engine access eliminates estimation error and saves GPU cycles
2. **Material Intelligence** — 14-category classification enables physically-correct per-surface treatment
3. **Production-Quality SSS** — Christensen-Burley with all the bells: bilateral rejection, specular preservation, translucency, wrap lighting
4. **AGIS System** — Unique approach to respecting game's own imagespace intent while maintaining ENB aesthetics
5. **Procedural Everything** — Weather effects, lens dirt, and film damage are all procedural (no texture dependencies)
6. **Comprehensive Tonemapping** — 7 operators × 2 methods × 7 time-of-day periods with film emulation
7. **D3D11 Hook Depth** — Full shader stage tracking including tessellation, CB capture, and vertex layout fingerprinting
8. **Robust Architecture** — Clean separation of concerns, graceful fallback, mutex-protected caches

---

## X. Development Opportunities

### Near-Term Enhancements
- Integrate bounded VNDF sampling from MartysMods into SB_SSR.fx for importance-sampled reflections
- Add tetrahedral LUT interpolation to enblut.fxh (4 taps vs 8, measurable savings)
- Implement temporal accumulation for GTAO/SSGI using SB_MotionVector for stable reprojection

### Medium-Term
- FFT bloom option alongside Kawase for physically-accurate diffraction patterns
- SH-based radiance caching leveraging nearby light data for multi-bounce approximation
- Feature gate architecture for Community Shaders backend support

### Long-Term (Layer 5 — PerfGov)
- Frame budget monitoring with dynamic quality scaling
- Scene complexity classification from draw call telemetry
- Dynamic INI governance for engine-level settings
- Runtime occlusion plane injection (research completed, pending BSOcclusionPlane RE)
