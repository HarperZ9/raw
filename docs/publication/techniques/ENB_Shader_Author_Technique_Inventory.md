# ENB Shader Author × Technique Inventory
## SkyrimBridge Research — Incremental Improvement Mapping

*Compiled February 2026 — for identifying overlap density and unexplored gaps*

---

## 1. Author Profiles

### Kitsuune
**Platform:** ENB (Skyrim SE) + KiLoader/KiENBExtender framework
**Primary Works:** Silent Horizons 2 (Core & New Dawn), ENB Extender, KreatE, EVLaS successor
**Philosophy:** Full custom shader stack; modern rendering techniques adapted to ENB constraints; emphasis on user-accessible customization without requiring HLSL knowledge.
**Unique Infrastructure:** KiENBExtender adds capabilities ENB doesn't natively support — external shader caching, extended UIName lengths, technique bindings, per-weather parameter separation, preset overlays, and D3D compiler selection. This is middleware between the shader author and ENB itself, effectively expanding what's possible within Boris's framework.

### MartyMcModding (Pascal Gilcher)
**Platform:** ReShade (universal) — not ENB-native, but techniques are widely ported
**Primary Works:** iMMERSE suite (MXAO, Launchpad, Sharpen, Anti-Aliasing), iMMERSE Pro (RTGI, ReGrade, Solaris, Physical DOF, Clarity, Exposure Fusion), iMMERSE Ultimate (ReLight, Convolution Bloom)
**Philosophy:** Research-grade rendering in post-process constraints; publishes papers-level implementations. Explicitly aims for "the most advanced SSAO on the internet" and can prove it. Formal mathematical rigor applied to shader optimization.
**Unique Infrastructure:** Launchpad prepass system — single shared pass computing depth-derived normals and dense optical flow for temporal reprojection, amortized across all downstream effects. This architectural pattern is directly relevant to SkyrimBridge's multi-effect coordination challenge.

### TreyM
**Platform:** ENB (Skyrim SE, Fallout 4)
**Primary Works:** Cinematic Film Looks (CFL) ENB, Film Workshop, Screenarcher's Suite, Eight Zero (80s VHS aesthetic)
**Philosophy:** Cinematic and film-stock emulation through physically-grounded tonemapping. Emphasis on replacing the game's tonemapping entirely to create a realistic lighting foundation before applying stylistic color grading. Analyzed real 35mm film grain scans to match shader output to physical film properties.
**Unique Infrastructure:** Film-stock presets as a parameterization strategy — rather than exposing raw shader variables, provides curated combinations that replicate specific real-world film stocks. Also pioneered image overlay shaders and VHS distortion effects (coded by kingeric1992) for stylistic applications.

### JawZ
**Platform:** ENB (Skyrim LE/SE)
**Primary Works:** Modular Shader Library (MSL), extensive helper function libraries
**Philosophy:** Framework-first approach — build reusable, well-documented shader libraries that other authors can include and configure. Pioneered the modular .fxh include pattern that became standard for ENB shader distribution. Focused on making ENB shader development accessible to newcomers.
**Unique Infrastructure:** MSL library architecture — elepHelpers.fxh (time/location splitters, Yxy/HSL conversions, luminance functions), modular AGCC, tonemapping method collection, procedural color correction, night eye effects. Initial Day-Night-Interior (DNI) separation code and GUI implementation that became the foundation used by virtually every subsequent ENB preset.

### l00ping (L00)
**Platform:** ENB + ESP weather plugin (Skyrim SE)
**Primary Works:** NAT (Natural and Atmospheric Tamriel) — weather + ENB as unified system, NAT.ENB III
**Philosophy:** Weather and rendering as an inseparable system. Each weather gets dedicated tonemapping and effects injected into the renderer. Atmospheric authenticity through tight coupling between weather state and visual processing. "Complete, unique, cleanest All-in-One visual overhaul."
**Unique Infrastructure:** Per-weather tonemapping and SFX injection — each of 70+ weather types has individually tuned rendering parameters. NATFX system that applies visual effects without ENB (fps-lossless). Faked subsurface scattering through tonemapping/HDR manipulation. Dynamic interior settings that adapt to current conditions. Cloud shadowing for cloudy weathers, wetness shaders, atmospheric perspective. Ported ReShade effects into ENB enbeffect shader for other authors.

### Adyss
**Platform:** ENB (Skyrim SE)
**Primary Works:** Ady's Shader Addon, Ady's Shader Setup
**Philosophy:** Modular addon approach — create effects that plug into any existing ENB preset without changing its look, only adding capabilities. Bridge between ReShade's shader ecosystem and ENB's rendering pipeline.
**Unique Infrastructure:** God rays shader for SSE that replaces the missing Oldrim ENB built-in rays (Boris never ported them). Experimental SSAO in prepass. Custom bloom with lens dirt support. Extensive porting of ReShade/SweetFX effects into ENB-compatible format. Shader toolbox philosophy — release individual effects that preset authors can cherry-pick.

### Additional Key Contributors (frequently credited, important technique sources)

| Author | Primary Contribution |
|--------|---------------------|
| **kingeric1992** | 3D LUT shader, SMAA 1X, SunSpritesFX11 procedural sun, VHS distortion shader, tetrahedral LUT interpolation |
| **Prod80** | Technicolor2, shadows shaders, RGB color correction, ported Oldrim ENB shaders to SSE |
| **Matso** | Original DOF shader (Skyrim), rain textures, letterbox code |
| **MaxG3D** | Custom bloom shaders used across many presets |
| **Tapioks** | DNI (Day-Night-Interior) separation code for enbeffect.fx |
| **The Sandvich Maker** | Dithering shader, tools, optimization advice |
| **CeeJay.dk** | Original SweetFX author — LumaSharpen, contrast, vibrance, dithering, borders |
| **Crosire** | ReShade framework author — foundation for all ReShade-based techniques |

---

## 2. Technique × Author Matrix

### Legend
- **●** = Primary author / original implementation
- **◐** = Significant custom implementation or major modification
- **○** = Uses/includes (may be ported or configured, not necessarily authored)
- **—** = Not implemented / not applicable
- **★** = Uniquely novel approach or breakthrough workaround

| Technique Domain | Kitsuune | MartyMcModding | TreyM | JawZ | l00ping | Adyss |
|-----------------|----------|----------------|-------|------|---------|-------|
| **TONEMAPPING & HDR** | | | | | | |
| Custom tonemapping curves | ● ★ | ◐ | ● ★ | ● | ● ★ | — |
| ACES (Hill / Narkowicz) | ○ | ○ | — | — | — | — |
| Filmic (Hable/UC2) | ○ | — | ● | ○ | ● | — |
| Hejl-Burgess | ○ | — | — | ○ | — | — |
| Per-weather tonemapping | ● ★ | — | — | — | ● ★ | — |
| Fake HDR / HDR simulation | — | — | — | — | ● | ● |
| LOG space tonemapping | — | — | ● ★ | — | ○ | — |
| | | | | | | |
| **COLOR GRADING** | | | | | | |
| LUT system (3D) | ● ★ | ○ | ○ | — | ○ | — |
| Tetrahedral LUT interpolation | ● ★ | — | — | — | — | — |
| Per-weather LUTs | ● ★ | — | — | — | — | — |
| Film stock emulation | — | — | ● ★ | — | — | — |
| Per-hue manipulation (7-band) | ● ★ | — | — | — | — | — |
| HSV/HSL color correction | ◐ | — | ◐ | ● | ○ | — |
| Procedural color correction | — | — | — | ● ★ | — | — |
| ReGrade (industry formulas) | — | ● ★ | — | — | — | — |
| AGCC (Apply Game ImageSpace) | ● ★ | — | — | ● | ○ | — |
| Split-toning / tint system | ● | — | ◐ | ◐ | ○ | — |
| | | | | | | |
| **AMBIENT OCCLUSION** | | | | | | |
| GTAO (Jimenez 2016) | ● | ● ★ | — | — | — | — |
| MXAO (advanced GTAO+SSIL) | — | ● ★ | — | — | — | — |
| Visibility bitmask AO | — | ● ★ | — | — | — | — |
| Multi-bounce AO approx | ● | ● | — | — | — | — |
| Material-aware AO intensity | ● ★ | — | — | — | — | — |
| Half-res / checkerboard AO | — | ● ★ | — | — | — | — |
| ENB built-in SSAO/IL tuning | ● | — | ○ | — | ● | — |
| Experimental prepass SSAO | — | — | — | — | — | ● |
| | | | | | | |
| **GLOBAL ILLUMINATION** | | | | | | |
| RTGI (ray-traced screen-space) | — | ● ★ | — | — | — | — |
| SSGI (indirect bounce) | ● | — | — | — | — | — |
| ENB SSIL tuning | ● | — | ○ | — | ● | — |
| ReLight (path-traced studio) | — | ● ★ | — | — | — | — |
| | | | | | | |
| **SCREEN-SPACE REFLECTIONS** | | | | | | |
| Hi-Z ray marching SSR | ● | — | — | — | — | — |
| Binary refinement SSR | ● | — | — | — | — | — |
| Temporal jittered SSR | ● | — | — | — | — | — |
| Bilateral denoised SSR | ● | — | — | — | — | — |
| Fresnel-weighted SSR | ● | — | — | — | — | — |
| | | | | | | |
| **DEPTH OF FIELD** | | | | | | |
| Physical aperture DOF | ● ★ | ● ★ | — | — | — | — |
| Bokeh shape simulation | ● | ● | — | — | — | — |
| Spherical aberration | ● ★ | ● | — | — | — | — |
| Chromatic aberration (longitudinal) | ● ★ | ● | — | — | — | — |
| Chromatic aberration (lateral) | ● | ● | ● ★ | — | — | — |
| Diffraction simulation | ● ★ | — | — | — | — | — |
| Focus breathing | ● | ● | — | — | — | — |
| Circle of confusion bleeding | ● ★ | ● | — | — | — | — |
| Archery/1st person focus | — | — | — | — | ● | — |
| Tilt-shift | — | ● | — | — | — | — |
| | | | | | | |
| **BLOOM** | | | | | | |
| HDR Karis anti-firefly | ● | — | — | — | — | — |
| Multi-pass gaussian bloom | ● | — | — | — | — | ● |
| Convolution bloom | — | ● ★ | — | — | — | — |
| Lens dirt overlay | — | — | — | — | — | ● |
| Atmospheric bloom (weather) | — | — | — | — | ● ★ | — |
| Custom bloom (MaxG3D-derived) | ○ | — | ○ | — | ○ | ○ |
| Solaris (advanced bloom/flare) | — | ● ★ | — | — | — | — |
| | | | | | | |
| **ANTI-ALIASING** | | | | | | |
| SMAA (optimized) | ● | ● ★ | — | — | — | — |
| FXAA (configurable) | ● | — | — | — | — | — |
| CREAA | ● | — | — | — | — | — |
| Temporal AA | ● | — | — | — | — | — |
| | | | | | | |
| **GOD RAYS / VOLUMETRICS** | | | | | | |
| Screen-space god rays | ● | — | — | — | ● | ● ★ |
| DSRS (Dual Sun Ray System) | ● ★ | — | — | — | — | — |
| Volumetric fog (Frostbite) | ● | — | — | — | — | — |
| Atmospheric fog (height/dist) | ● | — | — | — | ● ★ | — |
| Volumetric lighting sync | ● | — | — | — | ● | — |
| Cloud shadows | — | — | — | — | ● ★ | — |
| | | | | | | |
| **CONTACT SHADOWS** | | | | | | |
| Ray-marched contact shadows | ● | — | — | — | — | — |
| Sun-direction aware | ● | — | — | — | — | — |
| | | | | | | |
| **MOTION / TEMPORAL** | | | | | | |
| Optical flow estimation | — | ● ★ | — | — | — | — |
| Temporal reprojection | — | ● ★ | — | — | — | — |
| Motion blur | ● | — | — | — | — | — |
| | | | | | | |
| **SHARPENING / DETAIL** | | | | | | |
| Unsharp mask (iterative) | ● ★ | — | — | — | — | — |
| Edge-aware sharpening | ● ★ | ● | — | — | — | — |
| Local contrast (Clarity) | ● | ● | — | — | — | — |
| LumaSharpen | ○ | — | — | — | — | ○ |
| Exposure fusion | — | ● ★ | — | — | — | — |
| | | | | | | |
| **SKIN / SUBSURFACE** | | | | | | |
| SSS diffusion profiles | ● | — | — | — | — | — |
| Skin color grading | ● | — | — | — | — | — |
| Skin micro-detail | ● | — | — | — | — | — |
| Faked SSS via tonemapping | — | — | — | — | ● ★ | — |
| ENB SSS tuning | ● | — | — | — | ● | — |
| | | | | | | |
| **LENS / CINEMATIC** | | | | | | |
| Lens distortion (barrel/pin) | ● | — | ● | — | — | — |
| Anamorphic lens emulation | — | — | ● ★ | — | — | — |
| Film grain (real scan match) | ● | — | ● ★ | — | — | — |
| Vignette (optical model) | ● ★ | — | ○ | — | — | — |
| Sun sprite / lens flare | ● | — | — | — | ● | — |
| Rain droplet shader (raytraced) | ● ★ | — | — | — | — | — |
| Weather FX (rain/snow lens) | ● | — | — | — | ● | — |
| Procedural lens dirt | ● | — | — | — | — | — |
| Image overlays | — | — | ● | — | ○ | — |
| Letterbox / borders | ● | — | ○ | — | — | — |
| VHS distortion | — | — | ● | — | ○ | — |
| | | | | | | |
| **ADAPTATION / EXPOSURE** | | | | | | |
| Custom adaptation shader | ● ★ | — | — | — | ● | — |
| Adaptation min/max control | ● | — | — | ● | ● | — |
| | | | | | | |
| **DITHERING** | | | | | | |
| Gaussian blue noise dither | ● ★ | — | — | — | — | — |
| Generic dithering | ○ | — | ○ | — | — | ○ |
| | | | | | | |
| **INFRASTRUCTURE / FRAMEWORK** | | | | | | |
| DNI separation system | ◐ | — | — | ● ★ | ○ | — |
| Weather separation (advanced) | ● ★ | — | — | — | ● ★ | — |
| Shader caching (external) | ● ★ | — | — | — | — | — |
| Extended UI system | ● ★ | — | — | ● | — | — |
| Preset overlay system | ● ★ | — | — | — | — | — |
| Optical flow prepass | — | ● ★ | — | — | — | — |
| Helper function libraries | ● | — | — | ● ★ | — | — |
| Technique bindings | ● ★ | — | — | — | — | — |
| Plugin loading framework | ● ★ | — | — | — | — | — |
| ReShade→ENB effect porting | — | — | — | — | ● | ● ★ |
| D3D compiler selection | ● | — | — | — | — | — |

---

## 3. Overlap Density Analysis

### Highest Overlap (3+ authors with significant implementations)
These are the richest areas for comparative analysis and incremental improvement:

1. **Tonemapping** — Kitsuune, TreyM, JawZ, l00ping all have deep custom implementations. Each takes a fundamentally different approach: Kitsuune uses parameterized S-curves, TreyM uses LOG-space film emulation, JawZ provides a library of standard methods, l00ping does per-weather dedicated curves. Cross-pollinating these approaches could yield a hybrid system with the best qualities of each.

2. **Color Grading** — Everyone touches this but from different angles. Kitsuune's per-hue 7-band system and tetrahedral LUT interpolation are technically the most advanced. MartyMcModding's ReGrade uses industry-standard formulas. TreyM's film-stock presets provide artistically curated starting points. JawZ's procedural color correction is the most parametric. A unified system could offer all approaches.

3. **Ambient Occlusion** — MartyMcModding's MXAO represents the technical ceiling (visibility bitmask, better horizon falloff than baseline GTAO, radiometrically correct cosine term). Kitsuune's implementation adds material-aware intensity which Marty's doesn't have. Combining Marty's algorithmic improvements with Kitsuune's material awareness would be the next step.

4. **Depth of Field** — Kitsuune and MartyMcModding have the most sophisticated implementations, each with unique features the other lacks. Kitsuune has diffraction simulation and DSRS-informed ray-traced rain droplets on the lens; Marty has tilt-shift and the deepest optical flow integration for motion-aware blur.

5. **God Rays / Volumetrics** — Three distinct approaches: Adyss's screen-space rays (filling the gap Boris left), Kitsuune's DSRS dual system, and l00ping's weather-integrated volumetric lighting. All work around the same Creation Engine limitation differently.

### Moderate Overlap (2 authors with distinct approaches)
Good candidates for focused comparative study:

6. **Anti-Aliasing** — Kitsuune (SMAA + FXAA + CREAA combined) vs MartyMcModding (compute-optimized SMAA with 15% perf boost). Both have TAA-related concerns specific to ENB pipeline positioning.

7. **Sharpening / Detail Enhancement** — Kitsuune's iterative unsharp mask with edge awareness vs MartyMcModding's Clarity and exposure fusion. Different goals (crisp detail recovery vs local contrast enhancement) that could be unified.

8. **Bloom** — MartyMcModding's convolution bloom (physically correct frequency-domain approach) vs everyone else's multi-pass gaussian. Convolution bloom hasn't been ported to ENB — a significant potential improvement.

9. **Film Grain** — TreyM's real 35mm scan-matched grain vs Kitsuune's Gaussian blue noise dither. Different goals (cinematic authenticity vs banding prevention) that serve complementary purposes.

### Low / No Overlap (single author territory)
These represent unique capabilities and potential gaps:

10. **RTGI / Global Illumination** — MartyMcModding exclusively. No ENB-native equivalent exists. Porting concepts (not code) from RTGI's screen-space ray tracing approach into ENB addon shaders is the single biggest potential visual improvement available.

11. **Optical Flow / Temporal Reprojection** — MartyMcModding's Launchpad is unique. ENB addon shaders currently have no equivalent for motion estimation. If temporal persistence workarounds exist, this could enable motion-aware effects.

12. **SSR** — Kitsuune exclusively within ENB. No other ENB author has shipped a comparable implementation. Improvements would come from academic literature rather than community comparison.

13. **Contact Shadows** — Kitsuune exclusively. Same situation as SSR.

14. **Material-Aware Processing** — SkyrimBridge exclusive via GBuffer reading. No other system has this capability. Every technique in the matrix could potentially benefit from material awareness.

---

## 4. Gap Analysis — Unexplored Territory

### Techniques that exist in production engines but not in any Skyrim ENB implementation:

| Technique | Why It's Missing | Feasibility in ENB |
|-----------|-----------------|-------------------|
| Stochastic SSR | Requires temporal accumulation | Medium — with render target persistence tricks |
| Bent normals for AO | Requires multi-pass data persistence | Medium — could store in alpha channel |
| Screen-space caustics | Requires refraction ray marching | High — purely pixel shader based |
| Specular occlusion from AO | Requires AO data in SSR pass | Medium — pass coordination needed |
| Temporal super-resolution | Requires motion vectors + history buffer | Low without Launchpad-style prepass |
| Volumetric cloud shadows | Requires cloud density estimation | Medium — l00ping's cloud shadow work is a starting point |
| Indirect specular from SSGI | Requires roughness-aware bounce | Medium — combines existing SSR + SSGI knowledge |
| Micro-shadow from normal maps | Requires normal map access in post | Medium — GBuffer normals available via SkyrimBridge |
| Subsurface scattering (proper screen-space) | Requires diffusion kernel passes | High — purely pixel shader, material ID available |
| Eye caustics | Requires per-eye refraction model | High — material classification already identifies eyes |

### SkyrimBridge-Specific Advantages No Other Author Has:

These are areas where SkyrimBridge's game state awareness enables improvements impossible for any other shader system:

1. **Combat-reactive AO/shadow intensity** — Tighten AO during combat for visual tension, soften during exploration
2. **Weather-transition aware bloom** — Smoothly morph bloom characteristics during weather crossfades using actual transition progress, not approximation
3. **Health/stamina reactive post-processing** — Subtle vignette, desaturation, or blur tied to actual player state rather than scripted triggers
4. **Time-of-day precise adaptation** — Use exact sun angle rather than binary day/night for tonemapping curve interpolation
5. **Location-aware volumetrics** — Different fog behavior for Riften (misty) vs Whiterun (clear) vs Solstheim (ashy) using actual worldspace data
6. **Interior type classification** — Different rendering profiles for caves, dwemer ruins, nordic ruins, and homes using cell data

---

## 5. Recommended Research Priorities

### Tier 1 — Highest Impact, Most Feasible
1. **MXAO algorithmic improvements → SB_GTAO** — Port MartyMcModding's better horizon falloff term and visibility bitmask concepts into your existing GTAO. Your material-awareness already exceeds his; combine the best of both.
2. **Convolution bloom investigation** — Determine if MartyMcModding's frequency-domain bloom approach can work within ENB's render target constraints. Would dramatically improve bloom quality.
3. **l00ping's per-weather tonemapping → SkyrimBridge weather awareness** — Your weather domain data makes this even more powerful than l00ping's approach. You can interpolate between tonemapping curves using actual weather transition percentages.

### Tier 2 — High Impact, Moderate Effort
4. **Optical flow estimation for motion blur / TAA** — Study Launchpad's approach. Even a simplified version would massively improve your TAA variance clipping and motion blur quality.
5. **Proper screen-space SSS** — Combine Kitsuune's diffusion profiles with your material ID classification for physically-based subsurface scattering that knows exactly which pixels are skin.
6. **ReGrade color science → SkyrimBridge color pipeline** — MartyMcModding's industry-standard color grading formulas applied with your per-hue and DNI separation system.

### Tier 3 — Exploratory / Long-term
7. **Screen-space GI concepts from RTGI** — Not a full port (too expensive) but extracting the core insight of short-range screen-space ray casting for indirect bounce lighting within ENB's per-pixel constraints.
8. **Temporal render target persistence mapping** — Systematically document which ENB render targets survive frame boundaries, enabling temporal effects currently impossible.
9. **Specular occlusion from AO data** — Feed GTAO results into SSR pass to properly occlude reflections in corners/crevices. Requires coordinating data between two addon shader passes.

---

## 6. Cross-Reference: Your Existing Implementations vs Community State-of-Art

| Your Implementation | Current State | Community Best | Gap |
|-------------------|---------------|---------------|-----|
| SB_GTAO | XeGTAO based, material-aware | MXAO (better horizon, bitmask) | Algorithmic refinement |
| SB_SSR | Hi-Z march, bilateral denoise | Kitsuune SSR (your own, most advanced in ENB) | Self-improving; look to academic lit |
| SB_HDRBloom | Karis anti-firefly, multi-pass | Convolution bloom (Marty) | Fundamentally different approach |
| SB_TemporalAA | Variance clipping | Marty's compute SMAA | Motion vectors would help |
| SB_VolumetricFog | Frostbite-derived | l00ping's atmospheric system | Weather integration depth |
| SB_ContactShadows | Sun-dir ray march | Yours is the only ENB implementation | Academic lit for improvement |
| SB_MotionBlur | Per-pixel velocity | Marty's optical-flow based | Temporal data availability |
| SB_MaterialPBR | GBuffer-aware PBR | No equivalent exists anywhere | Unique — expand classification |
| Color Pipeline | Kitsuune-derived mega shader | ReGrade + per-weather (combined) | Color science refinement |
| Adaptation | Custom Kitsuune-derived | l00ping's per-weather adaptation | Weather awareness via SkyrimBridge |
| Dithering | Gaussian blue noise | Yours (Kitsuune-derived) is SOTA for ENB | Minimal gap |

---

*This document should be treated as a living reference. As individual techniques are investigated and compared in depth, findings should be appended to each section with specific implementation notes and workaround discoveries.*
