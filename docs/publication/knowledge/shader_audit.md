# ENB of the Elders — Shader Suite Audit (Updated 2026-03-06)

## Inventory
- 9 root .fx shaders (~9K lines), ~55 techniques, ~450+ UI params
- 25 Helper .fxh files (~7K lines)
- 22 Addon .fxh files (~14K lines, all active — no stubs remaining)
- Total: ~30K+ lines HLSL

## Addon Files (22 total)
### PrePass Addons (8)
- `PrePass_SkinSSS.fxh` — Burley-profile SSS (2-pass horizontal + vertical)
- `PrePass_StylizationSuite.fxh` — 5-mode NPR (Kuwahara, watercolor, ink wash, cross-hatch, posterize) ~430 lines
- `PrePass_SnowCover.fxh` — Screen-space snow accumulation with depth-reconstructed normals + SB precipitation ~345 lines
- `PrePass_ParticleField.fxh` — 5-mode particle VFX (motes, fireflies, embers, dust, snow) ~472 lines
- `PrePass_Incandescence.fxh` — Thermal glow + heat shimmer for fire/lava/forge ~248 lines
- `PrePass_PhotoStudio.fxh` — Composition guides, focus peaking, zebra stripes, histogram ~601 lines
- `PrePass_SSR.fxh` — Screen-space reflections (view-space ray march + binary refinement + Fresnel) ~339 lines
- Wiring: EotE_PrePass3 (Style), 4 (Snow), 5 (Particles), 6 (Incandescence), 7 (PhotoStudio), 8 (SSR)
- `#if X_LOADED` compile guards for conditional technique inclusion

### PostPass Addons (6)
- `Effect_CRTShader.fxh` — CRT v3.0 + VHS display simulation ~720 lines
- `Effect_CinematicFX.fxh` — 8 cinematic effects (diffusion, halation, leaks, etc.)
- `Effect_AtmosphericFog.fxh` — Screen-space atmospheric fog
- `Effect_BlurSuite.fxh` — Gaussian/Box/Radial/Tilt-Shift blur
- `Effect_AASuite.fxh` — SMAA edge detection + blend
- `Effect_Sharpening.fxh` — CAS/RCAS sharpening

### Lens Addons (3)
- `Lens_SpectralEffects.fxh` — Diffraction starburst (N-blade aperture) + veil glare
- `Effect_ProceduralLensDirt.fxh` — Procedural lens dirt overlay
- `Effect_ProceduralWeatherFX.fxh` — Rain droplets + frost vignette

### Other Addons (3)
- `Effect_ColorGrading.fxh` — Extended color grading (Lift/Gamma/Gain, Channel Mixer, etc.)
- `Sunsprite_AtmosphericOptics.fxh` — Sun atmospheric optics
- `Underwater_SurfaceFX.fxh` + `Underwater_VolumetricFX.fxh` — Underwater effects
- `DOF_Advanced.fxh` — Advanced DOF features

## SSR (PrePass_SSR.fxh) — UPDATED 2026-03-06
- View-space ray march using FOV+ScreenSize reconstruction (no MatrixProjection in prepass)
- Default 64 steps + 10 refinement, roughness mip sampling for noise reduction
- Spatial-only jitter (no Timer.x — temporal jitter causes flicker without accumulation)
- Schlick Fresnel with water/floor boost, screen edge/depth/direction fade
- Hit confidence includes ray direction quality check (suppress screen-parallel hits)

## Chromatic Bloom Dispersion — NEW
- Per-channel UV offset in PS_BloomMix for wider mips (128, 64, 32, 16)
- Red shifts outward (longer wavelength), Blue shifts inward
- Progressive scale: 1x at mip128 → 4x at mip16
- `ui_BloomChromaDisp` parameter (0-1, default 0)

## CRT v3.0 + VHS (Effect_CRTShader.fxh) — COMPLETE
- Anti-aliased phosphor masks, generalized Gaussian beam, convergence errors
- CRT halation, 4 color profiles, signal noise, phosphor bloom, corner shadow
- VHS: YIQ chroma blur/smear, head switching, tracking, tape noise, snow, jitter, SP/LP/EP modes
- Wired as `EotE_Display` technique in enbeffectpostpass.fx

## Bloom (enbbloom.fx) — COMPREHENSIVE
- 16 techniques, 7-mip Gaussian pyramid + Dual Kawase mode
- Karis soft-knee threshold, depth masking, anamorphic stretch
- Per-mip spectral tinting + desaturation, chromatic dispersion (NEW)
- Kelvin temperature tinting, theme system integration

## Adaptation (enbadaptation.fx) — COMPREHENSIVE
- 64-bin histogram percentile anchoring, asymmetric EMA, scene-cut detection
- Hysteresis deadzone, cell-load spike guard, sky exclusion
- 3 metering modes: Matrix, Center-Weighted, Spot

## Effect (enbeffect.fx) — 18-STAGE PIPELINE
- Multi-pipeline selector: Digital/Film/Hybrid/Full/Custom
- 8 tonemappers including Gran Turismo
- Film pipeline: characteristic curves, Beer-Lambert, interimage
- Grade pipeline: highlight desat, printer lights, split-tone, ASC-CDL, bleach bypass
- UC2 Filmic Adaptation, Hunt/Cone Overlap, Local tone mapping

## PostPass (enbeffectpostpass.fx) — 10+5 TECHNIQUES
- Cinematic pipeline: Diffusion, Halation, Light Leaks, CA, Anamorphic, Gate Weave, Vignette, Letterbox, Film Damage, Final Polish
- CRT/VHS Display (EotE_Display technique)

## Prepass (enbeffectprepass.fx) — 8+ TECHNIQUES
- VB-SSGI, Skylighting, Multi-Bounce AO, Volumetric Fog, Atmospheric Haze, God Rays, Water Surface Mask, SSS
- Addons: Stylization, Snow, Particles, Incandescence, PhotoStudio, SSR

## Theme System — COMPLETE
- 8 presets, ~40-field struct, TF/TI/TB helpers

## Lens (enblens.fx) — COMPREHENSIVE
- 6 techniques: threshold → 3 downsamples → ghost/ALF/halo compose → spectral effects
- Diffraction starburst (N-blade) + veil glare (1/r² PSF)
- Procedural lens dirt + weather FX (rain, frost)

## Optimizations Applied
- [branch] guards on VB-SSGI, volumetric fog, atmospheric haze
- Quality tier compile-time sample counts
- memcmp dirty tracking in C++

## Bug Fixes Applied (2026-03-06)
- Underwater particles: `smoothstep(0.8, 0.3, depth)` → `(1.0 - smoothstep(0.3, 0.8, depth))` (well-defined behavior)
- Underwater bubbles: `TextureColor` → `TextureOriginal` (avoid processing-stage feedback)
- Underwater god rays: Hardcoded `float2(0.5, 0.0)` → UI params `ui_GodRaySunX/Y`
- SSS UIName: em-dash `──` → ASCII `-` (ENB editor compatibility)
- DOF post-blur: Added bilateral CoC weighting to prevent sharp edge artifacts at focus transitions
- DOF alpha encoding: `smoothstep(0.0, 0.6, ...)` → `smoothstep(0.0, 1.0, ...)` (wider transition)
- enbeffect.fx UI ordering: Moved GRADE|Bleach Bypass before LOCAL, ADAPT|Filmic before COLOR, TONEMAP|Hunt after AgX
- Water Mask: Added depth-normal heuristic fallback when SB is not active (flat + uniform depth = likely water)

## Bloom Additions (2026-03-06)
- Height-based bloom mask: Screen-Y band with center boost + outside attenuation
- Color-selective bloom: Hue-matching with configurable tolerance and boost multiplier

## Key Remaining Gaps
1. Temporal reprojection for SSGI (needs motion vectors + frame-persistent RT)
2. 4 legacy unused files: Helper/enbUI_CRT.fxh, Helper/enbUI_CinematicFX.fxh, UI/enbUI_CRT.fxh, UI/enbUI_CinematicFX.fxh (safe to delete — merged into enbUI_Lens.fxh)
