# Theme System & Multi-Pipeline (2026-03-06)

## Theme System Architecture

### Two-Layer Design
- **Layer 1 (shader-only):** `enbglobals.fxh` declares `ui_EotE_Theme` (int 0-7), includes `Helper/EotE_ThemeSystem.fxh`
- **Layer 2 (C++ sync):** `SB_Theme_Config` float4 (.x = theme index), read from enbeffect.fx panel via ENBGetParameter, pushed to all 9 shaders

### Files
- `enbglobals.fxh` — Theme UI selector + SB_Theme_Config extern + `#include "Helper/EotE_ThemeSystem.fxh"`
- `Helper/EotE_ThemeSystem.fxh` — ThemeParams struct (34 floats), 8 static const presets, TF/TI/TB helpers
- `Helper/EotE_LogEncoding.fxh` — ARRI LogC3 Lin↔Log conversion (new)
- All 9 .fx files now include `enbglobals.fxh` (added 2026-03-06)

### ThemeParams Struct Fields (all float for static const array compat)
- enbeffect.fx: tonemapMode, curve, whitePoint, brightness, contrast, saturation, filmEnable, filmNegStock, filmNegIntensity, filmPrintIntensity, filmDensity, filmInterimage, gradeEnable, highlightDesatStr, colorTemp, splitToneEnable, splitShadow/HighlightRGB, splitIntensity, bleachBypass, localToneEnable, localToneStr
- enbbloom.fx: bloomIntensity, bloomSpectralTint
- enbadaptation.fx: adaptBias
- enbeffectpostpass.fx: diffusionEnable/Str, halationEnable/Str, vignetteEnable/Str, grainIntensity, sharpenStr
- enbsunsprite.fx: sunspriteIntensity

### 8 Presets
0=Manual (no override), 1=Cinematic (ACES/warm/halation), 2=Fantasy (AgX/vibrant/diffused), 3=Photorealistic (AgX/neutral), 4=Film Noir (Reinhard/high contrast/vignette), 5=Vintage Film (Reinhard/warm/grain), 6=Horror (Lottes/cold/dark), 7=Ethereal (Linear/bloomy/soft)

### Theme Helper API
- `ThemeActive()` — returns true when ui_EotE_Theme > 0
- `TF(manual, themed)` — float: returns themed if active, manual otherwise
- `TI(manual, themed)` — int version (casts float to int)
- `TB(manual, themed)` — bool version (themed > 0.5)
- `GetTheme()` — returns THEME_PRESETS[clamp(ui_EotE_Theme, 0, 7)]

### C++ Integration (BridgeData)
- Domain 24: ThemeData { Float4 Config; } added to AllData
- kParamTable entry: `ENTRY(theme.Config, "SB_Theme_Config")`
- main.cpp: Before PushAllData, reads theme index from enbeffect.fx via ENBGetParameter(int)

## Multi-Pipeline System

### Design
- `ui_PipelineMode` int (0-4) in enbeffect.fx controls which stage groups are active
- Resolves `filmOn` and `gradeOn` bools at top of PS_Draw
- ANDs with existing per-stage bool toggles (no new nesting)

### Pipeline Modes
| Mode | Name | Film On | Grade On |
|------|------|---------|----------|
| 0 | Digital | false | false |
| 1 | Film | true | false |
| 2 | Hybrid | true | true |
| 3 | Full | true | true |
| 4 | Custom | true | true (individual stage bools control) |

## New Techniques Added (Phase 4)
- **Gran Turismo Tonemap (#7)** in EotE_Tonemappers.fxh — Uchimura piecewise curve (toe/linear/shoulder)
- **UC2 Filmic Adaptation** in enbeffect.fx — white point modulated by adaptation value (ui_FilmicAdaptStr)
- **Hunt Effect** in enbeffect.fx — pre-tonemap saturation boost in bright areas (ui_HuntEffect)
- **Depth-Masked Bloom** in enbbloom.fx — smoothstep depth mask in PS_Threshold (ui_BloomDepthMask)
- **Density-Dependent Grain** in enbeffectpostpass.fx — pow(1-luma, 1.5) grain scaling (ui_GrainDensityDep)
- **ARRI LogC3** in Helper/EotE_LogEncoding.fxh — Lin↔Log conversion for future log-domain grading

## New Techniques Added (Phase 5)
- **Dual Kawase Bloom** in enbbloom.fx — `ui_BloomMode` (0=Gaussian, 1=Kawase), 5-tap diagonal per pass vs 30+ for Gaussian, sigma-responsive width via VS pre-scale
- **Exponential Steps** in enbeffectprepass.fx — `ui_Vol_ExpSteps` (1.0=uniform, 1.6=recommended), pow(tLinear, exp) clusters samples near camera, per-step stepSize
- **Bloom-RT Halation** in enbeffectpostpass.fx — `UIFHALO_UseBloomRT` toggle, reads RenderTarget128 (3 taps) instead of 12-tap local Gaussian + TextureDownsampled
- **Quality Tier System** in enbglobals.fxh — `QUALITY_TIER` compile-time (0=Low, 1=Medium, 2=High), defines QT_AO_DIRS/STEPS, QT_VOL_STEPS, QT_DOF_RINGS
- **Quality Tier Wiring** in enbeffectprepass.fx — VB_NUM_DIRS/STEPS and VOL_STEPS now use QT_ macros
- **Tetrahedral LUT** in Helper/Effect_WeatherLUT.fxh — `SB_SampleLUT_Tetrahedral()` replaces trilinear, 6-tetrahedra decomposition eliminates diagonal hue shifts

## Skipped Techniques (not applicable)
- **AABB Temporal Clip** — no temporal accumulation buffer/history in prepass
- **Gather() Optimization** — marginal savings, only applies to debug peaking code
- **Tile CoC Dilation** — existing near CoC blur + bleed pass handles foreground leak adequately

## Preset Swap System (Phase 5+)

### Architecture
- **File-swap approach**: Python script copies focused .fx files from `Presets/<name>/` to active `enbseries/`
- **Fallback**: Missing files in a preset are copied from `Presets/Full/` (all 9 shaders)
- **Script**: `preset_swap.py` — list/current/apply/restore/diff commands
- **State**: `Presets/current_preset.json` tracks active preset + timestamp

### Preset Contents (ALL COMPLETE — 45 shaders total)

| Preset | Custom Shaders | Focus |
|--------|---------------|-------|
| Full | 9/9 (baseline) | All features, all toggles |
| Performance | 9/9 | Stripped: Kawase bloom, HBAO, CAS+grain, passthrough lens, simple DOF/adapt/UW/sun |
| Cinematic | 9/9 | Film: ACES Hill, film pipeline, warm bloom, halation, hexagonal anamorphic DOF, procedural flares, warm UW |
| Photorealistic | 9/9 | Neutral: GTAO, AgX, conservative bloom, circular cat-eye DOF, voronoi UW, minimal lens/sun |
| Fantasy | 9/9 | Vibrant: VB-SSGI, AgX Punchy, spectral bloom, swirly Helios DOF, rainbow lens/sun, magical UW |

### Technique Matrix (fundamentally different algorithms per preset)

| Shader | Full | Performance | Cinematic | Photorealistic | Fantasy |
|--------|------|-------------|-----------|----------------|---------|
| **AO/GI** | VB-SSGI (bitmask) | Simple HBAO (8×4) | VB-SSGI (Full) | **GTAO** (analytical horizon) | **Multi-bounce VB-SSGI** |
| **Volumetrics** | Ray march + godrays | None | From Full | **Haze only** (no march) | **Ethereal fog tufts** (3D noise) |
| **Bloom** | Gaussian/Kawase 7-mip | Kawase 5-mip | Warm Gaussian 7-mip | **Conservative Kawase 5-mip** | **Spectral Kawase 7-mip** |
| **Tonemap** | 8 operators | Reinhard/ACES/AgX | ACES Hill | Full AgX (poly sigmoid) | AgX Punchy (sat output) |
| **PostPass** | 10-pass cinematic | CAS+grain (1 tech) | 5-pass film | **CAS+dither (1 tech)** | **Diffusion+vignette+grain (3 tech)** |
| **Adaptation** | Full (asymmetric+filmic) | 3×3 fast EMA | Asymmetric EMA+filmic shoulder | Symmetric 7×7 uniform | Fast symmetric center-heavy |
| **DOF** | 10-tech N-gon+cat-eye+tilt | 5-tech 3-ring circular | 10-tech hex anamorphic | 10-tech circular cat-eye | 10-tech swirly pentagonal |
| **Lens** | Full (ghosts+star+halo+streak) | Passthrough (zero cost) | Procedural flares (all enabled) | Minimal star only | Rainbow prismatic (all enabled) |
| **Sun** | Full (disc+glare+streak+spikes) | Disc+glare only | Warm film (anamorphic+6-blade) | Clean disc+glare (no spikes) | 8-blade rainbow+anamorphic |
| **Underwater** | Full (8 addons) | Tint+blur+waves only | Warm amber+trochoidal caustics | Voronoi caustics+scattering | Magical domain-warp+biolum+sparkle |

### Key Differences per Preset
- **Performance/enbeffect.fx**: 3 inline tonemappers, QUALITY_TIER 0, no film/grade/pipeline
- **Performance/enbbloom.fx**: Kawase-only (no Gaussian), 5 mips (skip 32/16), no spectral/depth/tint
- **Performance/enbeffectpostpass.fx**: Single technique — CAS + simple grain + dither
- **Performance/enbeffectprepass.fx**: Simple HBAO (8 dirs × 4 steps), no GI/vol/haze/godrays/skylighting
- **Cinematic/enbeffect.fx**: ACES Hill only, always-on film pipeline, Hunt effect, UC2 filmic adapt, Oklab split-toning
- **Cinematic/enbbloom.fx**: Warm defaults (5500K day tint), anamorphic 1.3, spectral tint 0.25, depth mask on
- **Cinematic/enbeffectpostpass.fx**: 5-pass (diffusion, halation, vignette, CA, polish), all enabled by default
- **Photorealistic/enbeffect.fx**: Full AgX (polynomial sigmoid), subtle highlight rolloff, no grading
- **Photorealistic/enbbloom.fx**: High threshold (1.2), tight sigma (3-4), no tint/spectral, 5 mips, sharp mip falloff
- **Photorealistic/enbeffectpostpass.fx**: Single technique — CAS + dither only (no grain, no FX)
- **Photorealistic/enbeffectprepass.fx**: GTAO (Jimenez analytical cosine-weighted horizon integral, 12 dirs × 6 steps), clean haze
- **Fantasy/enbeffect.fx**: AgX Punchy (saturated output transform), Oklab shadow/highlight tinting, vibrance, screen-blend bloom glow
- **Fantasy/enbbloom.fx**: Low threshold (0.7), wide sigma (7-8), spectral tint 0.4, chromatic bloom spread, 7 mips with heavy wide weights
- **Fantasy/enbeffectpostpass.fx**: 3-pass (ethereal diffusion, warm-tinted vignette, chromatic grain), all enabled
- **Fantasy/enbeffectprepass.fx**: VB-SSGI with strong multi-bounce (1.0 intensity, 1.2 saturation), ethereal 3D noise fog tufts
- **Cinematic/enbeffectprepass.fx**: Contact shadows (16-step SS ray march), atmospheric haze (Beer-Lambert + HG inscatter)
- **Cinematic/enbadaptation.fx**: Asymmetric EMA (bright→dark 1.5x, dark→bright 3.0x), filmic shoulder, center-weighted 5×5
- **Cinematic/enblens.fx**: Procedural texture-free: anamorphic streaks, 5-7 ghost chain with CA, 6-blade aperture star, halo ring
- **Cinematic/enbsunsprite.fx**: Film-era warm tint (1.0, 0.92, 0.72), anamorphic streak, 6-blade diffraction spikes
- **Cinematic/enbdepthoffield.fx**: 6-blade hexagonal aperture (curvature 0.25), anamorphic 1.33x, warm-biased fringing default
- **Cinematic/enbunderwater.fx**: Warm amber absorption, trochoidal caustics (copper tint), film diffusion, no addons
- **Photorealistic/enbadaptation.fx**: Symmetric speed 2.5, 7×7 uniform metering, sky exclusion 1.5, no filmic shoulder
- **Photorealistic/enblens.fx**: Minimal — subtle 7-blade aperture star only, disabled by default, threshold 1.8
- **Photorealistic/enbsunsprite.fx**: Clean disc + subtle glare only, physically-tinted, no spikes/anamorphic
- **Photorealistic/enbdepthoffield.fx**: 7-blade circular (curvature 0.85), cat-eye enabled, fringing off, clean neutral
- **Photorealistic/enbunderwater.fx**: Jerlov-typed absorption, voronoi caustics, forward scattering fog, cold blue-green
- **Fantasy/enbadaptation.fx**: Fast speed 4.0, wide min/max range, heavy center weight 0.7, dramatic swings
- **Fantasy/enblens.fx**: Rainbow everything: prismatic ghost chain, 8-blade rainbow star, rainbow halo ring, anamorphic streak
- **Fantasy/enbsunsprite.fx**: Large disc, 8-blade rainbow spikes (chromatic sin dispersion), anamorphic streak
- **Fantasy/enbdepthoffield.fx**: Helios-44 swirly bokeh (rotational field curvature), 5-blade pentagonal, field curvature
- **Fantasy/enbunderwater.fx**: Domain-warped iridescent caustics, bioluminescence blobs, sparkle particles, magical color shift
- **Performance/enbadaptation.fx**: 3×3 grid (9 samples), single speed slider, no filmic shoulder, ~0.01ms
- **Performance/enblens.fx**: Passthrough (outputs black = zero cost with additive blend)
- **Performance/enbsunsprite.fx**: Disc + glare only, 4 UI params, single technique
- **Performance/enbdepthoffield.fx**: 5-tech pipeline, 5×5 focus, 3-ring circular gather (24 samples), 3-tap post-blur
- **Performance/enbunderwater.fx**: 3-tech pipeline, combined mask blur, simple 4-tap blur + tint, basic waves

### Textures
- `generate_textures.py` generates all 9 textures procedurally (numpy/PIL/scipy)
- BlueNoiseAtlas (512×512 RGBA), CharTexture (tileable), BokehShapes (8 analytical shapes), RainDroplets (hemisphere normals), FrostPattern (FFT ice), FilmGrain, LensDirt, ColorGradient, NoiseAtlas
