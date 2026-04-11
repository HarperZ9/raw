# SkyrimBridge & ENB of the Elders — Documentation Index

**Author:** Zain Dana Harper
**Date:** March 2026
**Version:** SkyrimBridge v3.0.0 / ENB of the Elders (Truth ENB)

---

## Overview

This collection contains all technical documentation, research, and reference material produced during the development of SkyrimBridge (an SKSE64 plugin bridging Skyrim game data to ENB shaders) and ENB of the Elders (a physically-based ENB post-processing suite).

**Total: 65 documents, ~31,000+ lines of technical writing, 11 source .docx research papers**

---

## Table of Contents

### 1. Research — Advanced Rendering Techniques (8,721 lines)

Comprehensive research documentation synthesized from 13 research papers covering 5 categories of rendering techniques for ENB pixel shaders (DX11 SM5.0).

| File | Lines | Description |
|------|-------|-------------|
| [01_LensEmulation.md](research/01_LensEmulation.md) | 1,566 | Polynomial optics ghosts (Hullin/Bodonyi), ABCD matrix transforms (Lee-Eisemann), 6-band spectral CA, Brown-Conrady distortion, Seidel aberrations, procedural starburst, fused composite pass. 4 lens presets with full HLSL. |
| [02_AtmosphericCelestial.md](research/02_AtmosphericCelestial.md) | 2,307 | Hestroffer limb darkening, Rayleigh/Mie extinction, Kasten-Young airmass, Cornette-Shanks phase function, Buie aureole, Draine weather-reactive phase, Hapke moon opposition surge, Hillaire atmosphere LUTs, god rays. |
| [03_BloomDoF.md](research/03_BloomDoF.md) | 1,569 | GMM PSF bloom, depth-masked downsampling, anamorphic streak, tile-classified DoF, Nathan Reed CoC optimization, hexagonal bokeh (Sousa 2011), golden spiral sampling, optical bokeh shapes (SA, cat's-eye, onion-ring). |
| [04_TemporalStability.md](research/04_TemporalStability.md) | 1,836 | k-DOP 14-DOP neighborhood clipping, 5-tap bicubic Catmull-Rom history, motion-adaptive blend with SkyrimBridge signals, 3-prong MV workaround, R2 jitter, RCAS sharpening, SVGF-Lite, sky rotation-only reprojection. |
| [05_Denoising.md](research/05_Denoising.md) | 1,443 | Joint bilateral filtering, a-trous wavelet (3-level, 75 fetches), SVGF variance-guided edge stopping, per-effect strategies (SSAO, SSR, contact shadows, volumetric fog), sampling strategies (IGN, R2, STBN), triangular-PDF dithering. |

### 2. Architecture — System Design (1,162 lines)

| File | Lines | Description |
|------|-------|-------------|
| [SkyrimBridge_Overview.md](architecture/SkyrimBridge_Overview.md) | 29 | High-level system overview |
| [ARCHITECTURE.md](architecture/ARCHITECTURE.md) | 158 | C++ backend architecture: 24 data domains, tracker system, ENBSetParameter pipeline |
| [SHADER_INTEGRATION.md](architecture/SHADER_INTEGRATION.md) | 248 | How SB data flows from C++ to HLSL shaders via extern float4 params |
| [SHARED_MEMORY.md](architecture/SHARED_MEMORY.md) | 125 | SharedMemoryBridge for cross-process communication |
| [EXTENDER_COMPAT.md](architecture/EXTENDER_COMPAT.md) | 226 | ENB Extender replacement: ShaderPreProcessor, ExternBindingProcessor, WeatherSeparationEngine, ParameterBindingEngine, ENBGuiIntegration |

### 3. API Reference (1,988 lines)

| File | Lines | Description |
|------|-------|-------------|
| [PARAMETER_REFERENCE.md](api/PARAMETER_REFERENCE.md) | 292 | All ~150 float4 parameters across 24 domains with UIName annotations |
| [CONFIGURATION.md](api/CONFIGURATION.md) | 205 | FeedbackConfig.ini, WeatherParams.ini, WriteBackConfig.ini specification |
| [WEATHERPARAMS_REFERENCE.md](api/WEATHERPARAMS_REFERENCE.md) | 305 | Weather parameter system: per-weather configs, WeatherLerp(), interpolation |
| [BORIS_INTEGRATION.md](api/BORIS_INTEGRATION.md) | 417 | Technical proposal for ENB author Boris Vorontsov |
| [GETTING_STARTED.md](api/GETTING_STARTED.md) | 359 | Installation, first shader integration, parameter binding walkthrough |
| [PROPOSAL_FOR_BORIS.md](api/PROPOSAL_FOR_BORIS.md) | 238 | Architecture proposal for ENB SDK integration |
| [CREDITS.md](api/CREDITS.md) | 126 | Attribution and third-party credits |

### 4. Technique Library (7,796 lines)

Prior-art analysis and technique inventories drawn from ENB community research.

| File | Lines | Description |
|------|-------|-------------|
| [ShaderTechniques_MasterReference.md](techniques/ShaderTechniques_MasterReference.md) | 1,054 | 93-technique catalog across bloom, DoF, AO, tonemapping, color grading, lens effects, volumetrics, temporal, material effects |
| [shader_technique_catalog.md](techniques/shader_technique_catalog.md) | 1,422 | Extended technique catalog with implementation notes |
| [FilmColorScience_ENB_Research.md](techniques/FilmColorScience_ENB_Research.md) | 620 | Film stock emulation: characteristic curves, subtractive density, interimage effect, print emulation |
| [FilmColorGrading_Techniques_Research.md](techniques/FilmColorGrading_Techniques_Research.md) | 843 | Professional grading: ASC-CDL, printer lights, split-toning, LUT workflows, bleach bypass |
| [ThirdParty_Shader_Analysis.md](techniques/ThirdParty_Shader_Analysis.md) | 623 | Analysis of 3rd-party ENB shader techniques (iMMERSE, SOLARIS, etc.) |
| [FransBouma_Technique_Analysis.md](techniques/FransBouma_Technique_Analysis.md) | 290 | FransBouma/ReShade technique deep-dive |
| [ENB_Shader_Author_Technique_Inventory.md](techniques/ENB_Shader_Author_Technique_Inventory.md) | 318 | Per-author technique inventory (Gilcher, TreyM, Pilgrim, Kitsuune, etc.) |
| [Compass_Rendering_Research.md](techniques/Compass_Rendering_Research.md) | 150 | Compass AI research artifact on ENB rendering optimization |
| [Physics_That_Games_Forgot.md](techniques/Physics_That_Games_Forgot.md) | 177 | 50+ real-world phenomena never rendered in real time: H-K effect, Stevens, Purkinje, heiligenschein, corona diffraction, fluorescence, wave optics |

### 5. Planning & Roadmap (3,397 lines)

| File | Lines | Description |
|------|-------|-------------|
| [ROADMAP.md](planning/ROADMAP.md) | 217 | 6-phase development roadmap: v3 release through Fallout 4 port and RenderBridge platform |
| [COMPREHENSIVE_PLAN_OF_ACTION.md](planning/COMPREHENSIVE_PLAN_OF_ACTION.md) | 528 | Detailed implementation plan with priorities and dependencies |
| [IMPROVEMENT_PLAN.md](planning/IMPROVEMENT_PLAN.md) | 939 | Systematic improvement plan across all subsystems |
| [plan_output.md](planning/plan_output.md) | 1,386 | Detailed plan output with multi-pipeline, theme system, and new technique integration |
| [TEST_CHECKLIST.md](planning/TEST_CHECKLIST.md) | 142 | Verification checklist for shader compilation, visual regression, performance |
| [PHASE1_ANALYSIS.md](planning/PHASE1_ANALYSIS.md) | 185 | Phase 1 completion analysis |

### 6. Knowledge Base (2,464 lines)

Curated technical knowledge accumulated across development sessions.

| File | Lines | Description |
|------|-------|-------------|
| [rendering_techniques.md](knowledge/rendering_techniques.md) | 186 | Implementation status tracker: 30+ techniques with shader locations and formulas |
| [rendering_research.md](knowledge/rendering_research.md) | 310 | ENB pipeline research: Vol 6 reference, render target formats, execution order |
| [backend_systems.md](knowledge/backend_systems.md) | 66 | ShaderCache, SceneObserver, ENB Extender replacement, SB_ShaderDebug |
| [enb_shader_details.md](knowledge/enb_shader_details.md) | 60 | Shader architecture notes: addon wiring, technique naming, ScreenSize convention |
| [theme_pipeline.md](knowledge/theme_pipeline.md) | 147 | Theme system design: ThemeParams struct, 8 presets, TF/TB/TI helpers |
| [enb_weather_system.md](knowledge/enb_weather_system.md) | 65 | KreatE weather configs, ENB native weather, preset overlay system |
| [advanced_crt_vhs_bloom_research.md](knowledge/advanced_crt_vhs_bloom_research.md) | 1,440 | CRT/VHS/retro effect research: phosphor decay, scanlines, bloom characteristics |
| [shader_audit.md](knowledge/shader_audit.md) | 114 | Shader file audit results |
| [extender_replacement_plan.md](knowledge/extender_replacement_plan.md) | 76 | ENB Extender replacement architecture |

### 7. Source Research (5,115 lines text + 11 .docx)

Raw research documents from which the Category 1-5 research docs were synthesized.

#### Extracted Text (from .docx)
| File | Lines | Description |
|------|-------|-------------|
| [_agent1_lens.txt](sources/_agent1_lens.txt) | 774 | Lens emulation ENB compatibility analysis with production HLSL |
| [_agent2_sunsprite.txt](sources/_agent2_sunsprite.txt) | 2,015 | Solar disc physics, limb darkening, atmospheric extinction, corona, moon rendering |
| [_agent2_compat.txt](sources/_agent2_compat.txt) | 510 | ENB compatibility verdicts: NATIVE/WORKAROUND/BLOCKED for all techniques |
| [_taa_compat.txt](sources/_taa_compat.txt) | 522 | TAA k-DOP clipping, bicubic history, motion-adaptive blend, RCAS |
| [_denoising.txt](sources/_denoising.txt) | 1,117 | Bilateral filtering, a-trous wavelet, SVGF, per-effect denoising strategies |

#### Original .docx Research Papers
| File | Size | Description |
|------|------|-------------|
| Gilcher_Rendering_Techniques_Analysis.docx | 25 KB | Pascal Gilcher (iMMERSE/SOLARIS) technique deep-dive |
| Rendering_Algorithm_Improvements_Phase2.docx | 20 KB | Phase 2 algorithm improvements |
| Artistic_Shader_Techniques_ENB_Research_Compilation.docx | 51 KB | Artistic shader technique compilation |
| SkyrimBridge_MultiPipeline_EffectTemplates_Architecture.docx | 36 KB | Multi-pipeline + effect template architecture design |
| SkyrimBridge_MultiPipeline_EffectTemplates_Architecture_Full.docx | 44 KB | Extended multi-pipeline architecture (full version) |
| RenderBridge_Strategic_Vision.docx | 26 KB | RenderBridge platform strategic vision document |
| Agent1_LensEmulation_ENB_Analysis.docx | 38 KB | Agent 1: Physically-based lens emulation for ENB |
| Agent2_Sunsprite_Corona_AtmosphericRendering_Research.docx | 63 KB | Agent 2: Solar disc, corona, atmospheric light source rendering |
| Agent2_ENB_Compatibility_Analysis_Workarounds.docx | 31 KB | Agent 2: ENB compatibility analysis and workaround engineering |
| TAA_Temporal_Stability_ENB_Compatibility_Analysis.docx | 33 KB | Agent 4: TAA temporal stability techniques for ENB |
| SkyrimBridge_Vol7_Denoising_Research.docx | 48 KB | Agent 5: Noise reduction and signal quality for ENB pipelines |

---

## Implementation Status Summary

### ENB of the Elders Shader Pipeline (9 shaders)
```
enbeffectprepass  ->  enbdepthoffield  ->  enbbloom  ->  enbadaptation
       |                    |                  |              |
   AO, GI, fog,        Thin-lens DoF      Karis threshold  Histogram
   contact shadows     golden spiral       Gaussian/Kawase  geometric mean
   multi-bounce AO     chromatic bokeh     spectral tint    asymmetric EMA
   exp. steps                              depth mask       sky exclusion
       |                                       |
  enblens  ->  enbeffect  ->  enbeffectpostpass  ->  enbsunsprite  ->  enbunderwater
     |              |                |                    |
  6 ghosts     18-stage HDR      10-pass post:         Atmospheric:
  ALF streaks  compositor:       diffusion,            aureole,
  halo ring    5 pipelines,      halation,             corona,
               film/grade,       CA, vignette,         ice halos,
               8 tonemappers,    grain (density-dep),  parhelia
               Hunt/H-K/Stevens/ CAS sharpening
               Purkinje effects
```

### Techniques Implemented (30+)
- Geometric mean adaptation, sky exclusion, spike guard, hysteresis
- Per-mip spectral tinting, log-domain contrast, local tone map, NaN sanitization
- Gran Turismo tonemap, Hunt Effect, UC2 filmic adaptation, asymmetric EMA
- Depth-masked bloom, Karis anti-firefly, soft-knee threshold, dual Kawase
- Chromatic bloom dispersion, density-dependent grain, chromatic grain
- Multi-bounce AO (Jimenez), exponential step distribution
- Pipeline mode selector, theme system (8 presets), ARRI LogC3
- Film pipeline (neg/print curves, density, interimage)
- Grade pipeline (highlight desat, printer lights, temp, split-tone, CDL, bleach)
- Helmholtz-Kohlrausch luminance correction, Stevens Effect, Purkinje Shift

### Techniques Documented but Not Yet Implemented
- **Category 1:** Polynomial ghost optics, ABCD matrix, spectral CA, Brown-Conrady, Seidel
- **Category 2:** Hestroffer limb darkening upgrade, Cornette-Shanks, Buie aureole, Hillaire LUTs
- **Category 3:** GMM PSF bloom, tile-classified DoF, optical bokeh shapes
- **Category 4:** k-DOP TAA, bicubic history, motion-adaptive blend, R2 jitter, RCAS
- **Category 5:** Joint bilateral, a-trous wavelet, SVGF, per-effect denoisers

---

## Directory Structure
```
publication/
  INDEX.md                    <- This file
  research/                   <- 5 category research docs (8,721 lines)
    01_LensEmulation.md
    02_AtmosphericCelestial.md
    03_BloomDoF.md
    04_TemporalStability.md
    05_Denoising.md
  architecture/               <- System design docs (1,162 lines)
    SkyrimBridge_Overview.md
    ARCHITECTURE.md
    SHADER_INTEGRATION.md
    SHARED_MEMORY.md
    EXTENDER_COMPAT.md
  api/                        <- API reference & guides (1,988 lines)
    PARAMETER_REFERENCE.md
    CONFIGURATION.md
    WEATHERPARAMS_REFERENCE.md
    BORIS_INTEGRATION.md
    GETTING_STARTED.md
    PROPOSAL_FOR_BORIS.md
    CREDITS.md
  techniques/                 <- Technique library (7,796 lines)
    ShaderTechniques_MasterReference.md
    shader_technique_catalog.md
    FilmColorScience_ENB_Research.md
    FilmColorGrading_Techniques_Research.md
    ThirdParty_Shader_Analysis.md
    FransBouma_Technique_Analysis.md
    ENB_Shader_Author_Technique_Inventory.md
    Compass_Rendering_Research.md
    Physics_That_Games_Forgot.md
  planning/                   <- Roadmap & plans (3,397 lines)
    ROADMAP.md
    COMPREHENSIVE_PLAN_OF_ACTION.md
    IMPROVEMENT_PLAN.md
    plan_output.md
    TEST_CHECKLIST.md
    PHASE1_ANALYSIS.md
  knowledge/                  <- Curated technical knowledge (2,464 lines)
    rendering_techniques.md
    rendering_research.md
    backend_systems.md
    enb_shader_details.md
    theme_pipeline.md
    enb_weather_system.md
    advanced_crt_vhs_bloom_research.md
    shader_audit.md
    extender_replacement_plan.md
  sources/                    <- Raw research inputs (5,115 lines + 11 .docx)
    _agent1_lens.txt
    _agent2_sunsprite.txt
    _agent2_compat.txt
    _taa_compat.txt
    _denoising.txt
    *.docx (11 research papers)
```
