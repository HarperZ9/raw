# Third-Party ENB Shader Analysis
## Techniques, Patterns, and Architectural Insights for SkyrimBridge

**Scope:** ~108 files, ~42,600 lines of HLSL across multiple ENB presets and shared libraries.

---

## 1. Preset Identification

The uploaded files represent shader code from **four distinct ENB preset ecosystems** plus two major shared library frameworks and the iMMERSE (MartysMods) ReShade suite:

### 1A. NAT ENB (Natural & Atmospheric Tamriel) — by L00
- **Target:** Skyrim SE (DX11, Shader Model 5.0)
- **Files:** enbeffectpostpass.fx (2461 lines), enbadaptation.fx (364 lines), enbeffectprepass.fx (463 lines), enbunderwater.fx (394 lines)
- **Key collaborators:** JawZ (MSL code), Roxahris (MartinGrain/Overlay ports), LonelyKitsuune (Snow code)
- **Signature patterns:** 7-TOD interpolation via `CAV`/`TODA` macros, massive LUT atlas (61 color grades + 33 finish LUTs), VHS/CRT simulation, extensive overlay/frame system (25 frame textures, 4 dirt textures)

### 1B. Snapdragon ENB — by Prod80 (edited)
- **Target:** Skyrim LE (DX9, Shader Model 3.0 — `vs_3_0`/`ps_3_0`)
- **Files:** enbeffect.fx (2255 lines), enbbloom.fx (1066 lines)
- **Key collaborators:** kingeric1992 (LUT code), CeeJay.dk (Levels), Martinsh/MTichenor (Film Grain)
- **Signature patterns:** AGCC (Automatic Game Color Correction) pipeline reading `_c1`–`_c5` registers, Uncharted 2 filmic tonemapper with adaptation-hooked Linear White, multi-layer bloom with threshold/intensity per layer, extensive prod80 color correction suite

### 1C. Silent Horizons ENB — by LonelyKitsuune/Skratzer
- **Target:** Skyrim SE (DX11, Shader Model 5.0)
- **Files:** enbsunsprite.fx (816 lines — project version), enbUI_*.fxh suite (Primer, Bloom, Effect, PostPass, PostPassA, PostPassLutA, PrePass, Lens, SunSprite, DepthOfField, DepthOfFieldMcfly)
- **Signature patterns:** `_UI_PRIMER_` guard system, `TODIE`/`TOD` macros for 6-phase TOD + Interior separation, `SEPARATE_VAR` for auto-generating DNI parameter variants, comprehensive UI architecture with category separators

### 1D. kingeric1992 Shader Suite
- **Target:** Mixed LE/SE
- **Files:** enbdepthoffield.fx (773 lines), enbeffectprepass.fx (875 lines), enblens.fx (834 lines), enbsunsprite.fx (911 lines), enbsmaa.fx (328 lines)
- **Key techniques:** Parametric bokeh shapes (Circle, Pentagon, Hexagon, Octagon), SMAA integration for ENB, anamorphic lens flares, sprite-based sun rendering with chromatic distortion, depth-driven CoC with gaussian post-blur

### 1E. Reforged Framework — by Sandvich Maker (modified by Sevenence)
- **Files:** ReforgedGlobals.fxh (24K), ReforgedMacros.fxh (18K), ReforgedUI.fxh (36K)
- **Purpose:** Shared utility framework providing shader model compatibility layer, macro-driven technique/texture/parameter declaration, comprehensive colorspace conversions, depth linearization, luma calculation, and dithering

### 1F. Reactor ENB (Fallout 4)
- **Files:** Colorspace.fxh, AdaptTool.fxh, enbeffect_AdaptTool.fxh
- **Purpose:** Colorspace transformation matrices (Vanilla, ACES AP0/AP1, Rec.709, DCI-P3, Rec.2020, ProPhoto), adaptation level visualizer

### 1G. iMMERSE Suite (MartysMods) — by Pascal Gilcher
- **Files:** 16 MartysMods_*.fx files + 15 mmx_*.fxh support headers
- **Purpose:** Production-grade ReShade effects (RTGI, MXAO, FFT Bloom, SMAA, Depth of Field, Regrade, Solaris, Relight, etc.)
- **Note:** Proprietary/copyrighted — analyzed for architectural patterns only

---

## 2. Key Techniques Catalog

### 2A. AGCC — Automatic Game Color Correction (Snapdragon/Prod80)

The AGCC pipeline intercepts Skyrim's native ImageSpace color processing by reading the game's constant buffer registers (`_c1` through `_c5`) and applying them in a controlled, bounded manner:

**Register mapping (reverse-engineered):**
- `_c2.y` — White point
- `_c3.x` — Saturation
- `_c3.z` — Contrast  
- `_c3.w` — Brightness (also drives Khajiit Night Vision / Predator Vision)
- `_c4.xyz` — Tint color (RGB)
- `_c4.w` — Tint amount
- `_c5` — Fade color/amount
- `_c7` — Luma coefficients (Rec.709: 0.2125, 0.7154, 0.0721)

**Bounding system (SKIS — Skyrim IS Limits):**
Each game IS parameter is clamped to user-defined maximums before application, with DNI separation:
- `SKIS_SatE` / `SKIS_SatI` — Max saturation (exterior/interior)
- `SKIS_Con` — Max contrast
- `SKIS_Brightness` — Max brightness (with special handling: if `_c3.w > 1.51`, bypass limit to preserve Night Vision)
- `SKIS_TintD` / `SKIS_TintN` / `SKIS_TintI` — Max tint per DNI

**Processing order:**
1. Eye adaptation (read `_s4`, disabled in interiors with fixed 0.5 value)
2. Bloom mixing (3-layer with per-layer threshold/intensity, tinting, contrast)
3. Game CC application with SKIS bounds (Reinhard-style tone mapping using `_c2.y` white point)
4. Prod80 color corrections (color balance, saturation, levels, RGB balance)
5. Uncharted 2 filmic tonemapper (adaptation-hooked)
6. LUT application (kingeric1992 atlas sampling)
7. Split-tone / sepia / B&W filter

**Key insight for SkyrimBridge:** The AGCC pipeline demonstrates how to read and bound Skyrim's native IS parameters. SkyrimBridge's `SB_IS_Cinematic` tracker domain already captures these values via SKSE, but the bounding approach (allowing game CC while preventing extreme values) is a sophisticated pattern worth preserving. The Night Vision detection (`_c3.w > 1.51`) is particularly clever.

### 2B. 7-TOD Interpolation System (Multiple Presets)

Three distinct implementations of time-of-day interpolation were found:

**NAT/L00 — `CAV` macro (7-phase weighted):**
```hlsl
#define TODA(a,b) \
  ((TimeOfDay1.x * a + TimeOfDay1.y * a + TimeOfDay1.z * a + \
    TimeOfDay1.w * a + TimeOfDay2.x * a + TimeOfDay2.y * b) / timeweight())
```
Uses ENB's `TimeOfDay1` (Dawn/Sunrise/Day/Sunset) and `TimeOfDay2` (Dusk/Night) with 7 phases (Dawn, Sunrise, Day, Sunset, Dusk, Night, Interior). The `CAV` macro handles all 7 exterior phases plus interior.

**Silent Horizons — `TOD`/`TODIE` macros (6-phase + interior):**
```hlsl
#define TOD(a) \
  ((TimeOfDay1.x * a##_Dawn + TimeOfDay1.y * a##_Sunrise + \
    TimeOfDay1.z * a##_Day + TimeOfDay1.w * a##_Sunset + \
    TimeOfDay2.x * a##_Dusk + TimeOfDay2.y * a##_Night) / timeweight())
#define TODIE(a) lerp(TOD(a), a##_Interior, EInteriorFactor)
```
Uses token pasting (`##`) to auto-resolve `_Dawn`, `_Sunrise`, etc. suffixes. Interior handled via lerp with `EInteriorFactor`.

**Reforged — Multiple expansion macros:**
```hlsl
#define LERP_DN(var) var = lerp(var##Night, var##Day, ENightDayFactor);
#define LERP_DNI(var) var = (EInteriorFactor == 1.0 ? var##Interior : lerp(var##Night, var##Day, ENightDayFactor));
#define SELECT_DNI(var) var = (EInteriorFactor == 1.0 ? var##Interior : (ENightDayFactor > 0.5 ? var##Day : var##Night));
#define LERP_TODI(var) // Full 6-phase weighted interpolation
```
Provides multiple strategies: simple DN lerp, DNI with interior branch, hard select, or full 6-phase TODI.

**Key insight for SkyrimBridge:** SkyrimBridge already provides `SB_TOD_Dawn` through `SB_TOD_Night` as Float4 parameters and `SB_Interior` flag. The `timeweight()` normalization pattern (sum of all TOD weights as divisor) ensures correct blending when ENB's weights don't sum to 1.0.

### 2C. LUT Atlas Systems

**NAT Postpass — Mega-atlas approach:**
- 61 color grade LUTs in a single 4096×64 atlas (`PRColorgrades.png`)
- 33 finish LUTs in separate atlas
- Additional special LUTs: DropBlue, LimitedRange
- Standard atlas sampling with floor/frac interpolation for trilinear LUT lookup
- DNI-aware LUT selection (separate exterior/interior LUT indices)

**Silent Horizons Postpass (Postpass_Lut.fxh) — Multi-atlas weighted blending:**
- 80+ named LUT presets organized into groups (SH Base, Kitsune, Summer, Spring, Autumn, CalmMoor, Reinforced, Sacrifice, Winter, Eccentric series, Lofi series, Stroll, etc.)
- `MixLutEnable()` functions for each group check boolean enable flags
- `MIXintensity()` / `Mixedintensity()` — DNI-aware intensity blending using TODA macro
- Dual LUT system: Default LUT set ("DELU") vs named LUT set switchable at runtime
- Texture declarations in Texture.fxh (MiiuAB BMP LUTs) vs TextureAlter.fxh (SnapDragon PNG LUTs) — same variable names, different resource paths

**Snapdragon — Single LUT with film selection:**
- 10 LUT slots (`lutname0` through `lutname9`) selectable via `bFilmList` UI
- Each technique pass binds a different LUT sampler as uniform parameter
- 16×16 tile atlas format with bilinear interpolation

**Key insight for SkyrimBridge:** The multi-atlas weighted blending approach with DNI-aware intensity per LUT is more sophisticated than a simple single-LUT system. SkyrimBridge's Postpass already implements 39-LUT weighted blending with tetrahedral interpolation, which goes beyond all three approaches analyzed here.

### 2D. Kawase Bloom (NAT/L00)

**KawaseBloom.h** implements a Gaussian-approximation bloom using Masaki Kawase's iterative blur technique:

**Core algorithm:**
- 4-tap filter sampling at `±(iteration * pixelSize + halfPixelSize)` diagonals
- Two quality modes: 5-pass (simple) and 25-pass (multi-resolution)

**25-pass multi-resolution pipeline:**
1. Initial downsample to RT1024 with levels adjustment (InBlack/InWhite/Contrast/OutBlack/OutWhite via `CAV`)
2. Progressive downscale: 1024 → 512 → 256 → 128 → 64 → 32
3. Per-resolution Kawase iterations (3 passes each at iteration 1, 2, 3)
4. Final mix: Weighted combination of all 7 resolution levels with per-pass select toggles

**Mix system (`PS_KawaseMix2`):**
- 7 resolution contributions with DNI-aware select intensities (`Pass_select1_int` through `Pass_select7_int`)
- Energy-conserving normalization: `multiplier = divider / int_sum`
- Resolution falloff weights: 0.5, 0.6, 0.45, 0.32, 0.23, 0.23 (1024→32)
- `ENABLE_KawaseMIXR` flag forces all selects to 1.0

**First-pass levels correction** applies pre-bloom color grading per DNI:
```hlsl
res.xyz = max(res.xyz - KRECCInBlack, 0.0) / max(KRECCInWhite - KRECCInBlack, 0.0001);
if (KRfContrast != 1.0) res.xyz = pow(res.xyz, KRfContrast);
res.xyz = res.xyz * (KRECCOutWhite - KRECCOutBlack) + KRECCOutBlack;
```

**Key insight for SkyrimBridge:** SB_HDRBloom already uses a superior approach (13-tap Gaussian + progressive downsample), but the per-resolution selective mixing with DNI-aware weights is a pattern that could enhance bloom character customization.

### 2E. Prod80 Bloom (Snapdragon enbbloom.fx)

**Architecture:** GEN7 bloom based on Ken Turkowski / GPU Gems 3 Ch.40:
- `PS_BloomPrePass` — Threshold extraction with depth-based dynamic blur radius
- `PS_BloomTexture1` / `PS_BloomTexture2` — Dual-pass separable Gaussian (run twice for quality)
- `PS_BloomPostPass` — Final composition
- Depth-aware blur radius modulates bloom spread based on scene distance
- Night/Interior separate intensity sliders

### 2F. Kabloom (Kermles)

**Kabloom.fxh** provides an alternative bloom composition system:
- Input levels (InBlack, InWhite, InGamma, OutBlack, OutWhite) as preprocessing
- 9 blend modes for bloom combination: Off, Addition, Opacity, Lighten, Lighten/Opacity hybrid, Luma-based, Screen/Add, Screen/Opacity, Screen/Lighten/Opacity
- Energy-aware composition that prevents washout

### 2G. Uncharted 2 Filmic Tonemapper (Snapdragon)

Full implementation with DNI-separated parameters and adaptation-hooked white point:

**Per-DNI parameters:** ShoulderStrength, LinearStrength, LinearAngle, ToeStrength, ToeNumerator, ToeDenominator, LinearWhite

**Adaptation integration:**
```hlsl
float W = WS * (1.0f + adapt_max);  // White point scales with scene brightness
float3 numerator = ((Q*(A*Q+C*B)+D*E)/(Q*(A*Q+B)+D*F)) - E/F;
float3 denominator = ((W*(A*W+C*B)+D*E)/(W*(A*W+B)+D*F)) - E/F;
color.xyz = numerator / denominator;
```

The `adapt_max` factor dynamically shifts the Linear White point based on scene adaptation, creating a perceptually stable mapping that responds to lighting conditions.

### 2H. Modified Filmic Tonemapper (Tonemapping.fxh)

Simpler 4-parameter curve with GUI-exposed control points:
```hlsl
float3 modifiedFilmic(float3 color, float mid1, float toe, float shoulder, float mid2) {
    return (color * (mid1 * color + toe)) / (color * (mid1 * color + shoulder) + mid2);
}
```
Plus 8 additional tonemapper options (selectable via `Tonemapper` int).

### 2I. Depth of Field Implementations

**kingeric1992 — Parametric Bokeh DoF (enbeffectprepass.fx, 875 lines):**
- Focus reading via `PS_ReadFocus` / `PS_WriteFocus` with temporal smoothing
- CoC calculation with near/far plane separation
- **4 bokeh shapes via `PixelShaderArray`:** Circle, Pentagon, Hexagon, Octagon
- Each shape uses different angular offsets and ring counts for natural-looking bokeh
- Post-blur Gaussian smoothing (horizontal + vertical) with configurable strength
- Final composition with CAS-style sharpening

**kingeric1992 — McFly-style DoF (enbdepthoffield.fx, 773 lines):**
- Based on Marty McFly's Advanced DoF
- CoC-to-alpha encoding for efficient blur masking
- Chromatic aberration pass (separate R/G/B with offset sampling)
- Lens distortion integration
- Luma sharpening (CeeJay.dk) as post-DoF restoration

### 2J. Lens Effects

**kingeric1992 enblens.fx:**
- Anamorphic lens flare generation via horizontal streak sampling
- Multi-pass: Anamorphic preprocess → Main draw → Anamorphic flare → Post-pass composition
- Lens dirt overlay integration
- Ghost generation with chromatic offset

**NAT enblens.fx (from project files):**
- Similar architecture but with Silent Horizons UI integration

### 2K. Sun Sprite System (kingeric1992)

**enbsunsprite.fx (911 lines):**
- Sprite-based sun rendering with parametric positioning
- `VS_Draw` — Custom vertex shader for sprite placement with `posparameter` (offset, scale)
- **Primary sprites:** 9 elements with individual tint, weight, and chromatic parameters
- **Secondary sprites:** Additional elements with distortion parameters
- **Glare pass:** Screen-space light scattering from sun position
- Alpha clearing pass for clean compositing
- Parametric group offset/scale for corona control

### 2L. SMAA Integration (kingeric1992 enbsmaa.fx)

ENB-native SMAA implementation:
- 3 detection modes: Luma edge, Depth edge, Color edge
- Standard SMAA pipeline: Edge detection → Blending weight calculation → Neighborhood blending
- Area/Search texture resources (SMAA_AreaTex.dds, SMAA_SearchTex.dds)
- Configurable max search steps and diagonal search steps
- Chain integration via `PASSNAME0/1/2` defines for technique sequencing

### 2M. CAS — Contrast Adaptive Sharpening (AMD)

**CAS.fxh** — AMD FidelityFX CAS ported to ENB:
- Port by SLSNe, optimized by Marty McFly
- Vectorized math reducing 64 → 43 instructions
- `tex2Doffset` intrinsic for address offset (9 → 8 registers)
- Adaptive sharpening that avoids over-sharpening high-contrast edges

### 2N. Film Grain (MartyMcFly / Pascal Gilcher)

**FilmGrain.fxh:**
- Simplex noise-based grain (Nikita Miropolskiy algorithm)
- 7 grain profiles: Modern 35mm, Vintage 35mm, Coarse, Medium, Fine, Vintage 8mm, Digital ISO
- Film grain structure simulation (grain clustering)
- DNI-aware hue parameter
- Per-channel grain amount/size/curve customization in full controls mode

### 2O. Subsurface Scattering (SSS.fxh)

Screen-space SSS based on Jorge Jimenez's technique:
- 6-sample Gaussian blur with depth-aware weighting
- Depth-dependent step size (closer pixels = stronger scattering)
- Skin detection via correction parameter
- Separable (horizontal + vertical) implementation

### 2P. Reflective Bump Mapping (RBM.fxh)

**RBM 3.0 beta by Marty McFly:**
- Screen-space normal reconstruction from depth buffer
- Fresnel-based reflection with configurable reflectance
- Color mask system (per-hue enable: Red, Orange, Yellow, Green, Cyan, Blue, Magenta)
- Relief height parameter for bump intensity
- Blur-based normal smoothing

### 2Q. Weather Patch System (WeatherPatches.fxh)

Pre-configured adjustment profiles for different weather mods:
- **Supported:** NAT, Dolomite, CoT, Vivid, Obsidian, Cathedral, ELFX
- Per-weather-mod exposure/gamma/saturation adjustments (DNI-separated)
- Applied as multiplicative corrections to normalize visual baseline across weather systems

### 2R. VHS/CRT Simulation (NAT Postpass)

Three modes selectable via `VHS_Index`:
1. **NTSC decoder** — Simulates NTSC color bleeding/artifacting
2. **VHS** — Tape distortion, tracking lines, noise
3. **Dirty CRT** — Scanlines, phosphor glow, screen curvature

Additional VHS passes: Tape noise overlay, Layer noise overlay (both intensity-controlled via `fVHSnoise`)

---

## 3. Shared Library Analysis

### 3A. Reforged Framework

**ReforgedGlobals.fxh (24K, ~650 lines):**
- Shader model compatibility layer (`REFORGED_HLSL_3` vs `REFORGED_HLSL_5`)
- Unified sampling: `rfSample(tex, co)` → `tex2Dlod` (SM3) or `tex.SampleLevel` (SM5)
- Comprehensive math utilities: remap, linearstep, spline1/2, expStep, gain, parabola, almostIdentity, impulse, cubicPulse
- Gamma conversion suite: accurate (`Lin2sRGB`/`sRGB2Lin`), fast (`pow 2.2`), fastest (`sqrt`)
- Full colorspace conversions: RGB↔HSV, RGB↔HSL, RGB↔HCV
- `LumaTint` — Preserves luminance while applying hue/saturation from tint color
- Depth linearization: parametric `linearDepth(nonLin, near, far)` and Skyrim-specific `getLinearizedDepth` using `rcp(mad(depth, -2999.0, 3000.0))`
- Timothy Lottes channel crosstalk algorithm
- Selectable luma calculation (10 modes: Rec.601, 709, 709.5, 2020, sRGB, equal weight, min, max, HSL-L)
- Levels-in function
- CIE illuminant reference whites (D50 through F12)

**ReforgedMacros.fxh (18K, ~500 lines):**
- Macro-driven declarations: `TECHNIQUE`, `TECHNIQUE_TARGETED`, `TECHNIQUE_NAMED`, `TECHNIQUE_NAMED_TARGETED`
- Texture declaration macros: `TEXTURE_PATH`, `TEXTURE_UNIFORM`, `TEXTURE_ENBEFFECT`
- DNI expansion macros: `LERP_DN`, `LERP_DNI`, `SELECT_EI`, `SELECT_DNI`, `LERP_TODI`
- Whitespace collection (WHITESPACE_1 through WHITESPACE_32) for UI alignment
- UI separator macros with category labeling

**ReforgedUI.fxh (36K, ~900 lines):**
- Comprehensive UI parameter declaration system
- Type-specific macros: `UI_FLOAT`, `UI_FLOAT_DNI`, `UI_INT`, `UI_BOOL`, `UI_COLOR`, `UI_COLOR_DNI`
- Range validation, step size, widget type (Spinner, Slider, Color picker)
- Category organization with separator elements
- Whitespace padding for visual alignment in ENB GUI

### 3B. NAT Common Libraries

**common.fxh** — Stripped-down Reforged variant with:
- Same colorspace/math functions but fewer features
- Chroma tri-dither implementation (quality levels 0–2, configurable bit depth)
- Mono tri-dither alternative

**macros.fxh** — Technique/texture declaration macros (Reforged-compatible)

### 3C. LonelyKitsuune/Skratzer Libraries

**Constatns.fxh (Constants):**
- Luma coefficient sets (Rec.709, Rec.601, custom)
- `timeweight()` function for TOD normalization
- `TOD`/`TODIE` macros (identical to HSL.fxh)
- Hue node array for 8-point hue wheel with TOD-interpolated colors

**HSL.fxh:**
- HSL conversion (kingeric1992)
- Same `TOD`/`TODIE` macros
- Hue node system for color equalizer

**msHelpers.fxh (JawZ Modular Shader Helpers, 12K):**
- GUI annotation system
- Photographic tone reproduction (Reinhard/Stark/Shirley/Ferwerda)
- XYZ↔Yxy color space conversion (easyrgb.com reference)
- Texture atlas tile sampling (Matso)
- Split screen utility (CeeJay.dk)

### 3D. Blend Modes Library

**Blendmodes.fxh:**
Complete Photoshop-compatible blend mode collection:
- 22 blend modes as macros: Normal, Lighten, Darken, Multiply, Average, Add, Subtract, Difference, Negation, Exclusion, Screen, Overlay, SoftLight, HardLight, ColorDodge, ColorBurn, LinearDodge, LinearBurn, LinearLight, VividLight, PinLight, HardMix, Reflect, Glow, Phoenix
- Component-wise operation via `Blend(base, blend, funcf)` macro
- Opacity-aware: `BlendOpacity(base, blend, F, O)`

### 3E. Conversions Library

**Conversions.fxh:**
Comprehensive color space transforms (chilliant.com reference):
- HUE→RGB, RGB→HCV, HSV↔RGB, HSL↔RGB, HCY↔RGB, HCL↔RGB
- All using numerically stable formulations with epsilon protection

---

## 4. iMMERSE / MartysMods Architecture (Pattern Analysis)

The 16 MartysMods files + 15 mmx_* headers represent Pascal Gilcher's iMMERSE framework. Key architectural patterns:

### 4A. mmx_* Header Library
- `mmx_global.fxh` — Global state, screen parameters, frame counter
- `mmx_depth.fxh` — Depth buffer access with multiple linearization modes
- `mmx_colorspaces.fxh` — Color space conversions (similar scope to Reforged but different implementation)
- `mmx_math.fxh` — Extended math library (10K, extensive)
- `mmx_fft.fxh` — Fast Fourier Transform utilities (used by FFT Bloom)
- `mmx_bxdf.fxh` — BXDF (Bidirectional Distribution Functions) for physically-based lighting
- `mmx_harmonics.fxh` — Spherical harmonics
- `mmx_qmc.fxh` — Quasi-Monte Carlo sampling
- `mmx_hash.fxh` — Hash functions for noise generation
- `mmx_camera.fxh` — Camera parameter extraction
- `mmx_deferred.fxh` — Deferred rendering utilities (GBuffer reconstruction)
- `mmx_sfc.fxh` — Surface/material functions
- `mmx_texture.fxh` — Texture sampling utilities
- `mmx_input.fxh` — Input handling
- `mmx_debug.fxh` — Debug visualization

### 4B. Key Effect Files
- **LAUNCHPAD (62K)** — Foundation effect: motion vectors, depth processing, normal reconstruction
- **RTGI_DIFFUSE (68K)** — Ray-traced global illumination (diffuse)
- **RTGI_SPECULAR (55K)** — Ray-traced global illumination (specular)
- **RELIGHT (71K)** — Screen-space relighting
- **REGRADE (52K)** — Color grading suite
- **MXAO (34K)** — Ambient occlusion (GTAO variant)
- **FFTBLOOM (29K)** — FFT-based bloom
- **SMAA (43K)** — Anti-aliasing
- **DEPTHOFFIELD (62K)** — Advanced DoF
- **SOLARIS (21K)** — Bloom/glow
- **EXPOSUREFUSION (20K)** — HDR exposure fusion
- **CLARITY (13K)** — Local contrast enhancement
- **SHARPEN (9K)** — Sharpening
- **INSIGHT (29K)** — Debug/analysis tool
- **LUTMANAGER (12K)** — LUT management

### 4C. Architectural Patterns
- **Modular header system** — Each mmx_* header is self-contained with include guards
- **Technique chaining** — Effects designed to chain through intermediate render targets
- **Compute-capable design** — Uses compute shaders where available (not ENB-compatible)
- **Temporal accumulation** — Frame-to-frame data persistence for RTGI, motion vectors
- **Quality tiers** — Multiple quality presets per effect

---

## 5. Depth Linearization Patterns

Three distinct implementations found:

**1. Reforged parametric:**
```hlsl
float linearDepth(float nonLinDepth, float depthNearVar, float depthFarVar) {
    return (2.0 * depthNearVar) / (depthFarVar + depthNearVar - nonLinDepth * (depthFarVar - depthNearVar));
}
```

**2. Reforged/Skyrim hardcoded (near=1, far=3000):**
```hlsl
float getLinearizedDepth(float2 coord) {
    float depth = TextureDepth.Sample(PointSampler, coord);
    depth *= rcp(mad(depth, -2999.0, 3000.0));
    return depth;
}
```

**3. RBM/SSS helper:**
```hlsl
float GetLinearDepth(float2 coords) { return GetLinearizedDepth(coords); }
float3 GetPosition(float2 coords) {
    float EyeDepth = GetLinearDepth(coords.xy) * 1000.0;
    return float3((coords.xy * 2.0 - 1.0) * EyeDepth, EyeDepth);
}
```

**Key insight for SkyrimBridge:** SkyrimBridge's `SB_LinearDepth()` uses the same hardcoded Skyrim near/far approach. The parametric version could be useful if supporting variable clip planes.

---

## 6. tempInfo Usage

`tempInfo1` and `tempInfo2` are ENB-provided `float4` externals:
- `tempInfo1` — Contains weather/time metadata (exact contents vary by ENB version)
- `tempInfo2.xy` — Cursor position of previous left click
- `tempInfo2.zw` — Cursor position of previous right click

Found in: NAT enbeffectpostpass.fx, NAT enbadaptation.fx, Helper.fxh (Reforged), NAT enbunderwater.fx

**Usage pattern:** Read-only external parameters provided by ENBSeries binary. Not directly written by shaders. SkyrimBridge's tracker system provides much richer data through dedicated Float4 parameters.

---

## 7. UI Architecture Patterns

### 7A. Silent Horizons UI System (enbUI_Primer.fxh)

**`_UI_PRIMER_` guard pattern:**
All UI headers require Primer to be included first via `#ifndef _UI_PRIMER_ / #error`. This ensures the macro infrastructure is available.

**`SEPARATE_VAR(x)` pattern:**
```hlsl
#define SEPARATE_VAR(x) COMBINE(COMBINE(TODIE,_),x)
```
Auto-generates unique variable names by combining a TODIE prefix with the parameter name, preventing namespace collisions across UI files.

**Category organization:**
Each UI file covers one shader stage (Bloom, Effect, PostPass, Lens, etc.) with self-contained parameter declarations using `UI_ELEMENT` for section headers.

### 7B. Reforged UI System (ReforgedUI.fxh)

**Macro-driven parameter declaration:**
- `UI_FLOAT(name, label, min, max, default)` — Single float parameter
- `UI_FLOAT_DNI(name, label, min, max, default)` — Auto-generates Day/Night/Interior variants
- `UI_COLOR(name, label, default)` — RGB color picker
- `UI_COLOR_DNI(name, label, default)` — Color with DNI variants
- `UI_BOOL`, `UI_INT`, `UI_WHITESPACE` — Additional types

**Key insight for SkyrimBridge:** SkyrimBridge's UI headers (enbUI_*.fxh) already implement a similar macro-driven approach with `SH_UI_*` macros and DNI separation. The Reforged approach of auto-generating DNI variants from a single macro call is more concise.

---

## 8. Technique Chaining Patterns

### 8A. NAT Postpass — 4-technique linear chain:
1. `PRC` — Close sharpening pass
2. `PRC1` — Main post-processing (levels → tonemap → dirt → vignette → film effects → grain → VHS noise → color grade → saturation → DropBlue → Finish → LimitedRange)
3. `PRC2` — VHS/CRT simulation (NTSC decoder, VHS distortion, or Dirty CRT)
4. `PRC3` — GUI overlay (composition grid, letterbox, frames, CG list, tutorial, final dithering)

### 8B. Kawase Bloom — 25-technique multi-resolution:
Progressive downsample → Per-resolution Kawase iterations → Weighted mix

### 8C. SMAA — 4-technique standard pipeline:
1. Clear alpha
2. Edge detection (luma/depth/color selectable)
3. Blending weight calculation
4. Neighborhood blending

### 8D. kingeric1992 DoF — Multi-technique with shader arrays:
1. Focus read (R16F target)
2. Focus write (temporal smoothing)
3. CoC-to-alpha
4. Bokeh blur (PixelShaderArray: Circle/Pentagon/Hexagon/Octagon)
5. Gaussian H blur
6. Gaussian V blur
7. Final composition with sharpening

---

## 9. Color Grading Techniques

### 9A. NAT Postpass Processing Chain (PS_PostPro):
1. Clean sharpening (unsharp mask variant)
2. Levels (InBlack/OutBlack/InWhite mapped to GUI-exposed DNI parameters)
3. Tonemapping (exposure-based with defog, gamma)
4. Dirt overlay (4 selectable dirt textures)
5. Vignette (curve-based with radius/amount)
6. Filmic SFX suite (film look → vibrance → sepia — gated by single bool)
7. Film grain (intensity-gated)
8. VHS noise layers (tape + layer noise)
9. Color grading (61 LUT atlas selection via integer dropdown)
10. Saturation adjustment
11. DropBlue LUT (special blue-shift LUT for cinematic look)
12. Finish LUT (final color transform)
13. Limited Range LUT (optional broadcast range clamping)

### 9B. Snapdragon Processing Chain (PS_D6EC7DD1):
1. Adaptation-based exposure
2. Bloom addition (3-layer with tinting)
3. Game CC (AGCC pipeline with SKIS bounds)
4. Prod80 color balance (Day/Night/Interior RGB multipliers)
5. Saturation + Levels (InBlack/Gamma/White/OutBlack/OutWhite per DNI)
6. RGB balance application
7. Uncharted 2 filmic tone curve (adaptation-hooked)
8. LUT palette application
9. Split-tone color tinting (Prod80)
10. Sepia/tri-color tinting (NVIDIA modified by Prod80)
11. Advanced B&W filter

### 9C. Color EQ System (ColorEQ.fxh):
- Hue-targeted saturation/brightness adjustment
- Smootherstep interpolation for natural falloff
- Mid-hue + range selection for isolated color manipulation

---

## 10. Cross-Cutting Patterns Summary

### Patterns Relevant to SkyrimBridge Development:

| Pattern | Source | SkyrimBridge Status |
|---------|--------|-------------------|
| AGCC bounded IS processing | Snapdragon | Implemented as AGIS in enbeffect.fx |
| 7-TOD weighted interpolation | NAT/SH/Reforged | Available via SB_TOD_* parameters |
| `timeweight()` normalization | All presets | Could add as helper function |
| Multi-resolution bloom mixing | Kawase/Prod80 | SB_HDRBloom uses progressive downsample |
| Adaptation-hooked tonemapping | Snapdragon UC2 | SB_Adaptation tracker provides data |
| LUT atlas mega-blending | NAT/SH | Postpass implements 39-LUT weighted |
| Parametric bokeh shapes | kingeric1992 | SB DOF uses different approach |
| Weather mod normalization | WeatherPatches | SB_Weather tracker provides indices |
| Depth-driven blur radius | Prod80 bloom | Available via SB_LinearDepth |
| Sprite-based sun rendering | kingeric1992 | SB sunsprite uses procedural approach |
| Channel crosstalk (Lottes) | Reforged | Not yet in SkyrimBridge |
| CAS adaptive sharpening | AMD/McFly | Could integrate into postpass |
| Film grain profiles | McFly | CinematicFX has basic grain |
| SSS screen-space | Jimenez | Not in SkyrimBridge |
| RBM reflective bumpmapping | McFly | Not in SkyrimBridge |
| VHS/CRT simulation | NAT | CRT shader exists in SkyrimBridge |
| Dual LUT set switching | SH Postpass | Not in current implementation |
| Night Vision detection | Snapdragon AGCC | SB could add via IS tracking |

---

## 11. Notable Code Quality Observations

**Strongest implementations:**
- kingeric1992's shader suite demonstrates excellent modular design with clean separation of concerns
- Reforged framework provides the most robust cross-platform compatibility layer
- Prod80's color correction chain shows careful attention to numerical stability (epsilon protection, saturation guards)

**Common issues found:**
- NAT postpass uses extensive `if` branching in pixel shaders (performance concern)
- Multiple presets redefine identical helper functions (Conversions vs ReforgedGlobals vs common.fxh — same HSV/HSL code)
- Kawase bloom's `lerp(0, value, weight)` is equivalent to `value * weight` — unnecessary lerp
- Some LUT sampling lacks trilinear interpolation on the Z axis

**Architecture lessons:**
- Macro-driven UI declaration dramatically reduces boilerplate (Reforged pattern)
- Token pasting for DNI variants (`a##_Dawn`, `a##_Night`) enables single-declaration parameters
- Header guard patterns (`#ifndef REFORGED_COMMON_H`) prevent redefinition conflicts
- Technique chain naming conventions (`PRC`, `PRC1`, `PRC2`) enable predictable sequencing

---

*Document generated from analysis of uploaded third-party ENB shader files for SkyrimBridge reference.*
*Files analyzed: ~108 files, ~42,600 lines of HLSL*
