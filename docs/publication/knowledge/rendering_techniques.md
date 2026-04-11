# Rendering Techniques Reference (from 8 research docs)

## Bloom
- **Karis Anti-Firefly**: Weight 2x2 blocks by `1/(1+luma)` on first downsample to prevent bright pixel dominance
- **Jimenez Pipeline**: 13-tap downsample + 9-tap tent upsample, per-mip additive blend = industry standard
- **Dual Kawase**: 4-tap down + 8-tap up at progressive resolutions, ~1.33x pixel throughput for infinite blur radius
- **SOLARIS Depth-Masked Bloom** (Gilcher): `saturate(exp2(tap.w * wmadd.x + wmadd.y))` depth weight prevents bleeding across depth discontinuities
- **Cone Overlap SDR-HDR** (Gilcher): `cone_overlap()` simulates cone cell response for perceptual bloom
- **Per-Mip Spectral Tinting**: Warm near-field, cool far-field (Rayleigh motivation)
- **Soft Threshold**: `soft = clamp(brightness - threshold + knee, 0, 2*knee); contribution = soft*soft/(4*knee)` eliminates hard bloom edges

## Volumetrics
- **Cascaded Radial Blur** (Crytek): 3 passes x 8 samples = 24 fetches but equivalent to 512 effective samples
- **Energy-Conservative Integration** (Hillaire): Accumulate inscatter BEFORE updating transmittance (wrong order loses energy)
- **Epipolar Optimization**: Render god rays at 1/4 horizontal resolution, bilateral upsample (scattering smooth along radial lines)
- **IGN Temporal Jitter**: `frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))))` + 4-frame offset rotation

## AO
- **Bitfield Horizon Occlusion** (Gilcher MXAO): 32-bit uint, each bit = horizon sector, `countbits()` for visibility, AND-based faster than OR
- **Multi-Bounce AO** (Jimenez): `max(ao, ((ao*a + b)*ao + c)*ao)` where a,b,c derived from albedo prevents unrealistic dark cavities
- **Bent Normal**: Accumulate unoccluded sample directions during AO -> used for directional ambient lookup

## Tonemapping
- **UC2 Filmic Adaptation Hook**: `W = WS * (1.0 + adapt_max)` - white point scales with scene brightness
- **SOLARIS Invertible Tonemap** (Gilcher): `pow(1+pow(w,-k), 1/k) * x * pow(1+pow(x,k), -1/k)` allows HDR<->SDR roundtrip
- **ARRI LogC3** (TreyM/Pilgrim): Log encoding for perceptually uniform LUT grading (professional cinema workflow)

## DOF
- **Thin Lens CoC**: `coc = coc_inf * (s-d)/s` where `coc_inf = f^2/(N*d)` (Gilcher iMMERSE)
- **Tile-Based CoC Dilation**: Compute max/min CoC per tile, dilate for foreground blur coverage, only process tiles that need blur
- **Scatter-Gather Hybrid**: Switch technique based on CoC size (small CoC = gather, large = scatter)

## Color Grading
- **Tetrahedral LUT Interpolation** (Gilcher): 4 samples vs 8 for trilinear, eliminates hue shifts at color transitions
- **ReGrade Compositable Pipeline** (Gilcher): 9 ops baked to LUT atlas at init, runtime = single texture lookup
- **LUT Atlas Baking**: Draw neutral LUT -> apply all grading ops -> sample result = O(1) at runtime regardless of op count
- **Split Toning in OKLCH**: Preserves perceptual lightness and avoids hue shifts (vs RGB)

## Lens Effects
- **John Chapman SS Lens Flare**: Sample at screen-center-mirrored positions from bloom buffer for ghost generation
- **Cyberpunk Optimization**: Entire lens flare in single pass at half resolution, reusing bloom's downsampled buffer
- **Starburst Modulation**: `pow(abs(cos(angle * bladeCount)), 16.0)` per pixel against each bright source
- **Brown-Conrady Distortion**: `distortion = 1 + k1*r^2 + k2*r^4` (barrel/pincushion)

## Material Effects
- **SSS Depth Rejection**: `depthWeight = saturate(1 - depthDiff * thickness)` prevents bleeding across silhouettes
- **Fake Translucency**: `pow(saturate(dot(viewDir, -(lightDir + normal*0.5))), power)` for back-lit foliage glow
- **Wetness Detection**: Upward-facing normal (`viewNormal.y > 0.85`) + dual-octave noise for puddle mask

## Temporal
- **AABB Clipping** (Salvi): Compute neighborhood min/max, clamp history to local bounds -> eliminates ghosting
- **Asymmetric EMA**: `alpha = (current > history) ? 0.15 : 0.05` (brighten fast, darken slow - matches human eye)
- **Exponential Step Distribution**: `t = pow(i/N, 1.6)` concentrates samples near camera for contact shadows/ray marching

## Architecture Patterns
- **Weather-Indexed Struct Arrays** (Pilgrim): Per-weather parameter sets with `WeatherLerp()` smooth transitions
- **State Hashing** (Gilcher FFT Bloom): Only regenerate kernel when parameters change (lazy evaluation)
- **Spatial Deinterleaving** (Gilcher MXAO): Process spatially distant pixels together to reduce cache pressure
- **LUT-Baked Color Pipeline**: Separate color ops (bake to LUT) from spatial ops (per-pixel), ~O(1) for color chain
- **ParmLink Cross-Shader Communication** (Kitsuune): CPU expression eval bridging shader files -> SB replaces with C++

## DOF (additional)
- **Nathan Reed 2-ALU CoC**: CPU precomputes `cocScale = f^2/(N*(d-f))`, `cocBias = -cocScale*(d/(d-f))`. GPU: `abs(scale*depth + bias)`
- **Golden Spiral Sampling**: `r = sqrt(i/N)`, `theta = i * golden_angle` — excellent disc coverage for any sample count
- **Hexagonal Bokeh** (Sousa 2011): 3x 1D blur (vertical + 2 diagonal at 60deg each) = hex shape at 1/3 cost of round gather
- **Chromatic Aberration in Bokeh Gather**: Weight R/G/B by radial position in disc — zero extra bandwidth

## Tonemapping (additional)
- **AgX**: Polynomial in log domain, handles supersaturated inputs without hue shifts (ACES shifts blues to purple)
- **Log-Domain Contrast**: `exp2((log2(color) - log2(0.18)) * contrast + log2(0.18))` preserves photographic midpoint

## Adaptation (additional)
- **Geometric Mean**: `exp(mean(log(luma)))` instead of arithmetic mean — matches logarithmic human perception
- **Sky Exclusion via Depth**: Mask `depth > 0.999` from adaptation to prevent bright sky from over-darkening landscape
- **f32tof16 Packing**: Pack 2 half-floats in 1 R32F pixel: `asfloat((f32tof16(a)<<16)|f32tof16(b))` — doubles 1x1 RT capacity

## Film Emulation
- **Film Halation**: Warm red backscatter `float3(color.r, color.r*0.4, color.r*0.1)` blur around bright highlights
- **Density-Dependent Grain**: `intensity * (1 - saturate(luma / rolloff))` — grain reduces in brighter areas (more dye molecules)

## Performance
- **Gather()**: `TextureDepth.Gather()` fetches 4 depth values in 1 instruction (vs 4 SampleLevel)
- **min16float**: Real FP16 packing on AMD RDNA saves 20-35% VGPRs (no effect on NVIDIA)
- **Timer.w Clamping**: `dt = min(Timer.w, 0.05)` prevents wild jumps during cell load spikes (0.5-2.0s frame times)
- **Quality Tier via Defines**: Compile-time quality selection eliminates dead code entirely (vs runtime branches wasting VGPRs)

## Safety Patterns
- **KeepAlive Anti-DCE**: `(Timer.w < 0.0) ? param : 0.0` prevents FXC from stripping conditionally-used SB params
- **NaN Sanitization**: `if(any(isnan(x))||any(isinf(x))) return magenta;` — one NaN cascades through all weighted averaging
- **RT Format Detection**: Write HDR value (>1.0), read back. If <=1.0, RT is UNORM not float (RGBA64 ≠ RGBA64F!)

## ENB-Specific
- **Depth Linearization**: `depth *= rcp(mad(depth, -2999.0, 3000.0))` (Skyrim near=1, far=3000)
- **View-Space Pos**: `ndc * aspect * tanHFov * linearZ` (ENB FieldOfView is degrees, not radians - verify per preset)
- **timeweight() normalization**: `TimeOfDay1.xyzw + TimeOfDay2.xy` sum for correct TOD interpolation
- **AGCC Night Vision**: `_c3.w > 1.51` detects Khajiit NV/Predator Vision, bypass IS bounds
- **Local Tonemap from Bloom**: Lowest bloom mip = local avg luminance, use as key for Reinhard local operator (zero additional passes)
- **Prepass Composite Order**: AO (mul) -> GI (add) -> Contact shadows (mul) -> God rays (add) -> Fog (over) -> SSR (Fresnel add) -> SSS (alpha) -> Clarity (HP add)
- **Mip-Level Scene Average**: `SampleLevel(tex, uv, 4)` = 16x16 avg, 5 samples = free scene average without reduction passes

## Implementation Status (2026-03-07)
Techniques integrated into EotE pipeline:

### Batch 1 (2026-03-05)
- **Geometric Mean Adaptation**: enbadaptation.fx PS_Downsample — log-domain accumulation
- **Sky Exclusion (luminance)**: enbadaptation.fx PS_Downsample — bright pixel rejection (no depth in adaptation)
- **Cell-Load Spike Guard**: enbadaptation.fx PS_Histogram — `min(blendSpeed, 0.3)` cap
- **Adaptation Hysteresis**: enbadaptation.fx PS_Histogram — deadzone near target reduces blend
- **Per-Mip Spectral Tinting**: enbbloom.fx PS_BloomMix — warm near-field, cool far-field (Rayleigh)
- **Log-Domain Contrast**: enbeffect.fx Stage 8 — `exp2((log2(x) - log2(0.18)) * c + log2(0.18))`
- **Local Tone Map from Bloom**: enbeffect.fx Stage 4b — Reinhard local operator from bloom mip
- **NaN Sanitization**: enbeffect.fx output — `isnan()/isinf()` guard before saturate

### Already present in codebase (confirmed 2026-03-07)
- **Gran Turismo Tonemap**: EotE_Tonemappers.fxh — Uchimura piecewise (operator #7)
- **Hunt Effect**: enbeffect.fx Stage 8c — pre-tonemap saturation boost by luminance
- **UC2 Filmic Adaptation**: enbeffect.fx Stage 8b — white point follows adaptation
- **Asymmetric EMA**: enbadaptation.fx PS_Histogram — separate brighten/darken speeds
- **Depth-Masked Bloom**: enbbloom.fx PS_Threshold — smoothstep(start, end, depth)
- **Karis Anti-Firefly**: enbbloom.fx PS_Threshold — 2x2 weighted by 1/(1+luma)
- **Soft-Knee Threshold**: enbbloom.fx PS_Threshold — quadratic soft contribution
- **Dual Kawase Mode**: enbbloom.fx PS_GaussBlur — mode 1, 5-tap diagonal
- **Chromatic Bloom Dispersion**: enbbloom.fx PS_BloomMix — per-mip R/B offset
- **Density-Dependent Grain**: enbeffectpostpass.fx Pass 9 — pow(1-luma, 1.5)
- **Chromatic Grain**: enbeffectpostpass.fx Pass 9 — per-channel with blue coarseness
- **Pipeline Mode Selector**: enbeffect.fx — 5 modes (Digital/Film/Hybrid/Full/Custom)
- **Theme System**: enbglobals.fxh + EotE_ThemeSystem.fxh — 8 presets across 9 shaders
- **Film Pipeline**: enbeffect.fx Stages 5-7 — neg/print curves, density, interimage
- **Grade Pipeline**: enbeffect.fx Stages 11-16 — highlight desat, printer lights, temp, split, CDL, bleach
- **Atmospheric Optics**: Sunsprite_AtmosphericOptics.fxh — aureole, corona, ice halos, parhelia

### Batch 2: Perceptual Color Science (2026-03-07)
- **Helmholtz-Kohlrausch**: enbeffect.fx Stage 10b — Fairchild-Pirrotta hue-dependent chroma→brightness
- **Stevens Effect**: enbeffect.fx Stage 3b — contrast modulated by adaptation level
- **Purkinje Shift**: enbeffect.fx Stage 4a — scotopic rod sensitivity at low adaptation

## Vol 7 Research (2026-03-07) — 5-Category Plan
See `memory/vol7_rendering_plan.md` for full plan across 13 research docs.

### Category 1: Lens Emulation (SB_LensCore.fxh → enblens.fx)
- Polynomial ghost UV (Hullin deg-3) + ABCD matrix (Lee-Eisemann) — 4 lens presets
- Coating quality parameter (Schlick Fresnel, exp interp R=0.04→0.003)
- 6-band spectral CA (Wyman-Sloan-Shirley CIE, Cauchy dispersion, crown/flint glass)
- Brown-Conrady distortion (k1/k2/k3 radial + p1/p2 tangential + anamorphic)
- Veiling glare (bloom-mip proxy for Spencer-Shirley 1/r² PSF)
- Combined Seidel kernel (coma+astigmatism+field curvature in 16-sample gather)
- Starburst upgrade (sinc², per-blade variation, spectral color gradient)
- FUSED composite pass: distortion+CA+ghosts+veil = single technique, 0.18ms
- Total budget: 0.65-0.88ms, 13 passes, 3 named RTs

### Category 2: Atmospheric/Celestial (SB_AtmosphericCore.fxh → enbsunsprite.fx)
- Hestroffer power-law limb darkening: `pow(cosθ, float3(0.429, 0.530, 0.621))`
- Atmospheric extinction: Rayleigh β_R=(5.802,13.558,33.1)e-6 + Mie + Kasten-Young airmass
- Multi-component corona: Cornette-Shanks Mie + Rayleigh brightening + Bishop's Ring
- Buie aureole (CSR-parameterized from SB fog density)
- Moon: Hapke opposition surge `B₀/(1+tan(α/2)/h)` — limb brightening at full phase
- Hillaire LUT pipeline (4 pixel-shader passes into fixed RTs, 0.19ms)
- Draine phase function (pre-fitted for 5 weather conditions)
- Total budget: 0.30ms + optional 0.19ms LUTs

### Category 3: Bloom/DoF (SB_BloomCore.fxh)
- GMM PSF bloom (4-component Gaussian mixture per lens character)
- Depth-masked downsampling (sky full, near geometry 20%)
- Anamorphic streak (KinoStreak horizontal Kawase)
- Tile-classified DoF (16×16 pixel-shader tiles, hierarchical min/max CoC)
- Mip-chain gather acceleration (bloom pyramid as DoF source)
- Optical bokeh: SA, cat's-eye SDF, onion-ring, polygonal aperture

### Category 4: TAA (SB_TemporalAA.fx addon + SB_TemporalCore.fxh)
- k-DOP 14-DOP clipping (7 axes, ray-clip method, +0.02ms over AABB)
- 5-tap bicubic Catmull-Rom history (preserves grass/terrain detail)
- Motion-adaptive α (velocity + depth + accel + SB weather/combat/time-dilation)
- 3-prong MV workaround (velocity dilation + luma-change ghost detect + material α)
- R2 jitter (φ₂ quasirandom, replaces Halton, 5-min change)
- RCAS sharpening (5-tap cross, noise-aware, motion-adaptive strength)
- Sky rotation-only reprojection (depth > 0.9999)
- Total budget: 0.46ms, 5 passes, 2 TextureCustom slots

### Category 5: Denoising (SB_DenoiseCore.fxh)
- Joint bilateral filter (depth + normal guides from TextureDepth)
- À-trous wavelet (3-level, 7×7 footprint, 75 fetches, variance-guided σ)
- SSAO: temporal-first-then-spatial ordering
- SSR: hit-point reprojection + roughness-scaled filter + luminance guide
- Contact shadows: binary bilateral + temporal hysteresis
- Volumetric fog: 2D temporal + depth-aware bilateral upsample
- Sampling: animated IGN, R2, STBN textures, triangular-PDF dithering for R10G10B10A2
