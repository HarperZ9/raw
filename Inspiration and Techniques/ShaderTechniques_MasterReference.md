# Shader Techniques Master Reference
## A Comprehensive Research Compendium for Building World-Class ENB Shaders

*Compiled February 2026 for SkyrimBridge / Silent Horizons ENB Development*

---

## Table of Contents

1. [Ambient Occlusion](#1-ambient-occlusion)
2. [Screen-Space Reflections](#2-screen-space-reflections)
3. [Subsurface Scattering](#3-subsurface-scattering)
4. [Temporal Anti-Aliasing](#4-temporal-anti-aliasing)
5. [HDR Bloom](#5-hdr-bloom)
6. [Tonemapping & Color Science](#6-tonemapping--color-science)
7. [Color Grading & Film Emulation](#7-color-grading--film-emulation)
8. [Volumetric Fog & Atmospheric Rendering](#8-volumetric-fog--atmospheric-rendering)
9. [Depth of Field](#9-depth-of-field)
10. [Contact Shadows](#10-contact-shadows)
11. [Motion Blur](#11-motion-blur)
12. [Lens Effects](#12-lens-effects)
13. [Dithering & Banding Reduction](#13-dithering--banding-reduction)
14. [Material & PBR Enhancement](#14-material--pbr-enhancement)
15. [Post-Process Pipeline Architecture](#15-post-process-pipeline-architecture)
16. [ENB-Specific Techniques from Community Authors](#16-enb-specific-techniques-from-community-authors)
17. [Master Bibliography](#17-master-bibliography)

---

## 1. Ambient Occlusion

### 1.1 XeGTAO (Ground Truth Ambient Occlusion)
**Authors:** Filip Strugar, Steve McCalla (Intel); based on Jorge Jiménez et al. (Activision)  
**Paper:** "Practical Real-Time Strategies for Accurate Indirect Occlusion" (SIGGRAPH 2016)  
**Source:** https://github.com/GameTechDev/XeGTAO (MIT License)

**Key Innovations:**
- Uses a radiometrically-correct AO equation (Monte Carlo integration of the visibility function over the hemisphere)
- Horizon-based approach: for each pixel, finds the elevation angle of the occluding horizon in multiple directions
- Integrates the cosine-weighted visibility analytically between the horizon angles and the surface normal
- Pre-filters depth buffer into a MIP hierarchy (from Scalable Ambient Obscurance, McGuire et al. 2012) for bandwidth optimization
- Depth MIP filter uses weighted average with threshold-based selection (most distant sample introduces thin occluder bias)
- Relies on spatial denoising + TAA for temporal stability rather than multi-frame accumulation

**Critical Parameters:**
```
RADIUS_MULTIPLIER      = 1.457   // Counters screen-space biases
FALLOFF_RANGE          = 0.615   // Distant samples contribute less
SAMPLE_DISTRIBUTION_POWER = 2.0  // Small crevices prioritized over large surfaces
THIN_OCCLUDER_COMPENSATION = 0.0 // Thickness heuristic
FINAL_VALUE_POWER      = 2.2     // Post-process power curve
DEPTH_MIP_SAMPLING_OFFSET = 3.30 // Bandwidth vs quality tradeoff
```

**Performance:**
- ~1.4ms at 4K on RTX 3070 (high quality, full resolution)
- ~0.56ms at 1080p on RTX 2060
- Outperforms HBAO+ in quality at comparable cost

**Integration Notes for ENB:**
- Bent normals output (optional, +25% cost) provides directional component for indirect lighting
- Multi-bounce diffuse approximation recovers energy lost by single-bounce AO
- Micro-shadowing approximation (Chan, Material Advances in Call of Duty: WWII, 2018) attenuates unshadowed direct light
- GTSO (Ground Truth Specular Occlusion) from the same paper handles specular AO

**Technique: SpatioTemporal Noise**
```hlsl
// Rotates sampling pattern per-frame for temporal stability
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex) {
    uint index = (pixCoord.x + pixCoord.y * 5) + temporalIndex * 7;
    return frac(0.5 + index * float2(0.75487766624669276, 0.5698402909980532));
}
```

### 1.2 HBAO+ (Horizon-Based Ambient Occlusion Plus)
**Author:** Louis Bavoil (NVIDIA)  
**Paper:** "Image-Space Horizon-Based Ambient Occlusion" (SIGGRAPH 2008)

**Key Concepts:**
- Per-pixel, march rays in screen space along multiple directions
- For each direction, find the horizon angle (maximum elevation of occluders)
- Compare horizon angle against surface normal to determine occlusion
- HBAO+ adds: random rotation per pixel, bilateral blur, half-res with interleaved rendering

**Limitations vs GTAO:**
- Not radiometrically correct (uses ad-hoc attenuation instead of proper visibility integral)
- More prone to over-darkening at depth discontinuities
- Less stable temporally without TAA

### 1.3 SSAO Variants Quick Reference

| Technique | Author | Year | Approach | Quality | Cost |
|-----------|--------|------|----------|---------|------|
| Crysis SSAO | Mittring | 2007 | Random hemisphere sampling | Low | Low |
| SAO | McGuire et al. | 2012 | Scalable, depth MIP hierarchy | Medium | Medium |
| HBAO | Bavoil & Sainz | 2008 | Horizon-based, per-direction | High | Medium |
| HBAO+ | Bavoil | 2012 | Interleaved, half-res | High | Medium |
| ASSAO | Strugar (Intel) | 2016 | Adaptive quality levels | High | Low-High |
| GTAO | Jiménez et al. | 2016 | Radiometrically correct | Highest | Medium |
| XeGTAO | Intel | 2021 | GTAO with bent normals | Highest | Medium |
| SDAO | Vermeer et al. | 2021 | Stochastic depth multi-view | Highest | High |

---

## 2. Screen-Space Reflections

### 2.1 Linear Ray Marching (McGuire & Mara)
**Authors:** Morgan McGuire, Michael Mara  
**Paper:** "Efficient GPU Screen-Space Ray Tracing" (JCGT, Vol. 3, No. 4, 2014)  
**Source:** http://casual-effects.blogspot.com/2014/08/screen-space-ray-tracing.html

**Core Algorithm:**
1. Reconstruct view-space position and normal per pixel
2. Reflect view direction across normal
3. Project reflection ray endpoint to screen space
4. March in screen-space coordinates (not world space!) using DDA-like stepping
5. At each step, compare ray depth against depth buffer
6. On intersection, refine with binary search

**Key Insight:** Stepping in screen space distributes samples uniformly across pixels, avoiding the perspective-induced bunching that world-space stepping causes. Device-Z can be linearly interpolated in screen space since it's already perspective-correct.

**Binary Search Refinement:**
```hlsl
// After linear march finds approximate hit:
float tLo = hitT - stepSize;
float tHi = hitT;
[loop] for (int r = 0; r < REFINE_STEPS; r++) {
    float tMid = (tLo + tHi) * 0.5;
    float2 midUV = startUV + deltaUV * tMid;
    float midRayZ = 1.0 / lerp(startInvZ, endInvZ, tMid);
    float sceneZ = SampleDepth(midUV);
    float diff = midRayZ - sceneZ;
    if (diff > 0.0 && diff < thickness)
        tHi = tMid;  // Still behind surface, tighten
    else
        tLo = tMid;  // Passed through, go back
}
```

### 2.2 Hi-Z Tracing (Hierarchical)
**Authors:** Yasin Uludag (GPU Pro 5, 2014)  
**Reference:** "Hi-Z Screen-Space Cone-Traced Reflections"

**Algorithm:**
1. Build a hierarchical depth buffer (Hi-Z) — each MIP level stores the maximum depth of the 2×2 block below
2. Start ray march at the coarsest MIP level
3. If the ray is above the depth at current MIP, advance along the ray
4. If below, drop down one MIP level for finer detail
5. Continue until reaching MIP 0 or finding intersection

**Advantages over Linear:**
- O(log n) traversal instead of O(n) — dramatically faster for long rays
- Natural LOD: distant reflections use coarse tracing, nearby use fine
- Fewer wasted samples

**Hi-Z Generation (Single Pass Downsampler):**
```hlsl
// Use single compute dispatch with groupshared memory
// Avoids pipeline stalls between MIP levels
// AMD's FidelityFX SPD is the reference implementation
groupshared float gs_depth[64];  // 8x8 thread group
// Write MIP 0 from scene depth, compute subsequent MIPs in shared memory
```

### 2.3 Stochastic SSR (AMD FidelityFX SSSR)
**Author:** AMD  
**Source:** https://gpuopen.com/fidelityfx-sssr/

**Key Features:**
- Jitters reflection rays based on surface roughness for glossy reflections
- Blue noise-driven sampling (Eric Heitz) for perceptually uniform noise
- Tile-based classification: skip tiles that don't need tracing
- Confidence-based hit validation with thickness test
- Dedicated temporal+spatial denoiser optimized for reflection noise

**Denoiser Pipeline:**
1. Spatial pre-filter (edge-aware bilateral)
2. Temporal accumulation with reprojection
3. Spatial post-filter for remaining noise
4. Confidence-weighted blend with environment map fallback

### 2.4 SSR Confidence Masking (Best Practices)
**From production implementations (Killing Floor 2, Frostbite, etc.):**

```hlsl
// Screen-edge fade: squared smoothstep avoids hard lines
float2 edgeDist = min(hitUV, 1.0 - hitUV);
float edgeMask = smoothstep(0.0, edgeFade, edgeDist.x) *
                 smoothstep(0.0, edgeFade, edgeDist.y);

// Distance fade: farther hits are less reliable
float distMask = 1.0 - saturate(hitT);

// Facing fade: reflections pointing into screen are reliable,
// sideways ones are wrong due to depth buffer limitations
float facingMask = smoothstep(-0.1, 0.3, reflDir.z);

// Fresnel: grazing angles reflect more (Schlick approximation)
float fresnel = pow(1.0 - saturate(dot(viewDir, normal)), fresnelPower);

// Depth fade: fade out at large world distances
float depthFade = 1.0 - smoothstep(maxDist * 0.5, maxDist, worldZ);

float confidence = fresnel * edgeMask * distMask * facingMask * depthFade;
```

**Killing Floor 2 Trick:** Return screen-space UVs instead of colors from the SSR pass, sample scene color later. This allows: (a) kicking SSR early in the frame, (b) getting free reflections from translucency/particles that render after SSR.

---

## 3. Subsurface Scattering

### 3.1 Separable Subsurface Scattering (S4)
**Authors:** Jorge Jiménez, Károly Zsolnai, Adrian Jarabo, Christian Freude, Thomas Auzinger, et al.  
**Paper:** "Separable Subsurface Scattering" (Computer Graphics Forum, 2015)  
**Source:** https://github.com/iryoku/separable-sss (BSD License)

**Core Principle:** Approximate the radially symmetric 2D diffusion kernel with a single separable (horizontal+vertical) kernel pair, requiring only 2 passes instead of up to 12.

**Algorithm:**
1. Render scene normally, marking skin pixels in stencil buffer
2. Horizontal blur pass with importance-sampled kernel (7-13 taps)
3. Vertical blur pass with same kernel
4. Kernel weights derived from Christensen-Burley or dipole diffusion profiles

**Kernel Generation (Christensen-Burley Normalized Diffusion):**
```hlsl
// Per-channel scatter distances define the kernel shape
// R scatters furthest (red light penetrates skin deepest)
// B scatters least
float3 scatterDistance = float3(0.55, 0.25, 0.10); // mm

// Burley profile: S(r) = A * exp(-r/d) / (2*PI*d*r) + A * exp(-r/(3*d)) / (6*PI*d*r)
// where d = scatterDistance, r = radial distance
// Weight function normalized over the full profile
```

**Depth-Aware Sampling:**
```hlsl
// Prevent light bleeding across depth discontinuities
float depthDiff = abs(centerDepth - sampleDepth);
float depthWeight = saturate(1.0 - depthDiff / maxScatterDepthDiff);
sampleWeight *= depthWeight;
```

**Importance Sampling:** Only 7 samples per pixel needed when positions are importance-sampled from the diffusion profile + spatiotemporal jittering.

### 3.2 Pre-Integrated Skin Shading
**Authors:** Eric Penner, George Borshukov  
**Reference:** "Pre-Integrated Skin Shading" (GPU Pro 2, 2011)

**Concept:** Pre-compute a lookup texture indexed by (NdotL, curvature) that encodes the integral of diffusion over the lit hemisphere. At runtime, just sample this texture instead of computing SSS.

**Curvature Estimation:**
```hlsl
float curvature = length(fwidth(worldNormal)) / length(fwidth(worldPos));
```

---

## 4. Temporal Anti-Aliasing

### 4.1 Karis TAA (UE4 Reference)
**Author:** Brian Karis (Epic Games)  
**Presentation:** "High Quality Temporal Supersampling" (SIGGRAPH 2014, Advances in Real-Time Rendering)

**Essential Components:**

**A) Jitter Pattern:**
```hlsl
// Halton(2,3) sequence provides low-discrepancy jitter
float2 jitterOffset = Halton23[frameIndex % 8]; // Sub-pixel offset
// Apply to projection matrix before rendering
projMatrix._31 += jitterOffset.x * 2.0 / viewportWidth;
projMatrix._32 += jitterOffset.y * 2.0 / viewportHeight;
```

**B) Motion Vector Reprojection:**
- Use per-pixel motion vectors for dynamic objects
- Camera-only reprojection for static geometry (from depth + inverse VP matrices)
- Sample longest velocity vector in 3×3 neighborhood for better edge AA

**C) Neighborhood Clipping (YCoCg):**
```hlsl
// Convert to YCoCg for better AABB precision
float3 ToYCoCg(float3 rgb) {
    return float3(
        0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,  // Y (luma)
        0.5  * rgb.r                 - 0.5  * rgb.b,  // Co
       -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b   // Cg
    );
}

// Build AABB from 3x3 neighborhood
float3 neighborMin = min9(samples);
float3 neighborMax = max9(samples);

// Clip history to AABB (not clamp — clip gives tighter results)
float3 clippedHistory = ClipToAABB(history, neighborMin, neighborMax);
```

**D) Variance Clipping (Salvi):**
```hlsl
// Tighter bounds than min/max AABB
float3 moment1 = 0, moment2 = 0;
for (int i = 0; i < 9; i++) {
    moment1 += samples[i];
    moment2 += samples[i] * samples[i];
}
moment1 /= 9.0;
moment2 /= 9.0;

float3 stddev = sqrt(abs(moment2 - moment1 * moment1));
float3 mu = moment1;

float gamma = lerp(MIN_GAMMA, MAX_GAMMA, confidenceFactor);
float3 aabbMin = mu - gamma * stddev;
float3 aabbMax = mu + gamma * stddev;
```

**E) Blend Factor:**
```hlsl
float blendFactor = lerp(0.05, 0.2, velocityConfidence);
// 0.05 = 20 frame equivalent accumulation (static)
// 0.2 = 5 frame equivalent (fast motion)
float3 result = lerp(clippedHistory, currentSample, blendFactor);
```

### 4.2 Luminance-Weighted Blending (Lottes)
**Author:** Timothy Lottes (NVIDIA)

```hlsl
// Weight history by inverse luminance to prevent fireflies
float historyLuma = Luminance(history);
float currentLuma = Luminance(current);
float historyWeight = 1.0 / (1.0 + historyLuma);
float currentWeight = 1.0 / (1.0 + currentLuma);
float3 result = (history * historyWeight + current * currentWeight)
              / (historyWeight + currentWeight);
```

### 4.3 Bicubic History Sampling
**Author:** Multiple (Intel TAA, Playdead INSIDE)

```hlsl
// 5-tap Catmull-Rom bicubic for history buffer sampling
// Prevents bilinear blurring that compounds over frames
// Critical for maintaining sharpness in TAA
float4 SampleHistoryBicubic(Texture2D tex, float2 uv, float2 texSize) {
    float2 position = uv * texSize;
    float2 center = floor(position - 0.5) + 0.5;
    float2 f = position - center;
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    // Catmull-Rom weights...
}
```

### 4.4 k-DOP Clipping (SIGGRAPH Asia 2024)
**Authors:** Recent advancement replacing AABB clipping with k-DOP (discrete oriented polytope) for tighter bounds and less ghosting.

---

## 5. HDR Bloom

### 5.1 Physically-Based Dual Kawase / Sledgehammer Bloom
**Reference:** Call of Duty: Advanced Warfare (Sledgehammer Games, SIGGRAPH 2014)

**Pipeline:**
1. **Threshold + Karis Average** (first downsample only)
2. **Progressive Downsampling** (13-tap weighted kernel per level)
3. **Progressive Upsampling** (tent filter per level)
4. **Additive Composite** with intensity control

**Karis Average (Anti-Firefly Weighting):**
```hlsl
// Prevents extremely bright subpixels from dominating bloom
// Applied ONLY to the first downsample level
float KarisWeight(float3 color) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

// For each group of 4 samples:
float3 group = (a + b + c + d) * 0.25;
float weight = KarisWeight(group);
group *= weight;
```

**13-Tap Downsample Kernel (Sledgehammer):**
```hlsl
// 5 groups with specific weights that sum to 1.0
// Center cross: 0.5 weight (4 samples, 0.125 each)
// Corner groups: 0.125 weight each (4 groups × 4 samples)
// This kernel has zero contribution from outside the source texel footprint
float3 downsample = e * 0.125;                    // center
downsample += (a + c + g + i) * 0.03125;          // corners
downsample += (b + d + f + h) * 0.0625;           // edges
downsample += (j + k + l + m) * 0.125;            // inner cross
```

**Tent Filter Upsample:**
```hlsl
// 9-tap tent filter for upsampling (weighted box filter)
float3 upsample  = d * 4.0;  // center
upsample += (b + f + h + d) * 2.0;  // cardinal neighbors
upsample += (a + c + g + i) * 1.0;  // diagonal neighbors
upsample *= 1.0 / 16.0;
```

### 5.2 Bloom Pipeline Variations

**Dual Kawase Blur (Marius Bjørge, ARM):**
- Uses downsample and upsample shaders that are faster than Gaussian
- Each pass reads 4 bilinear-filtered taps at specific offsets
- Comparable quality to multi-pass Gaussian at lower cost

**Progressive Approach (UE4/5):**
```
Scene → Threshold → Down0 → Down1 → Down2 → ... → DownN
                                                      ↓
Scene ← Composite ← Up0 ← Up1 ← Up2 ← ... ← UpN
```
Each upsample stage adds the upsampled result to the next level up, creating a natural multi-scale falloff.

---

## 6. Tonemapping & Color Science

### 6.1 Tonemapper Comparison

| Operator | Author(s) | Year | Characteristics |
|----------|-----------|------|-----------------|
| Reinhard | Reinhard et al. | 2002 | Simple, desaturates highlights, soft rolloff |
| Reinhard Extended | Reinhard | 2002 | Adds white point control |
| Hejl-Burgess | Hejl & Burgess | 2010 | Filmic, single MAD per channel |
| Uncharted 2 / Hable | John Hable | 2010 | 6-param filmic curve, industry standard for years |
| VDR | Zink | 2016 | Variable Dynamic Range, middle grey + toe + shoulder |
| ACES (Narkowicz fit) | Krzysztof Narkowicz | 2016 | Simple approximation: `(v*(2.51*v+0.03))/(v*(2.43*v+0.59)+0.14)` |
| ACES (Hill fit) | Stephen Hill | 2016 | More accurate RRT+ODT with input/output matrices |
| Lottes | Timothy Lottes | 2016 | Advanced VDR color pipeline |
| Uchimura / GT | Gran Turismo | 2017 | Maximum contrast, toe strength, linear section |
| AgX | Troy Sobotka | 2022 | Formation-based, preserves hue through highlight compression |
| Tony McMapface | Tomasz Stachowiak | 2022 | Per-channel with perceptual gamut mapping |

### 6.2 ACES Pipeline (Full)
**Reference:** Academy Color Encoding System

```hlsl
// Full ACES: sRGB → AP1 (ACEScg) → RRT → ODT → sRGB
static const float3x3 ACESInputMat = {
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};
static const float3x3 ACESOutputMat = {
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 ACESFitted(float3 color) {
    color = mul(ACESInputMat, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);
    return saturate(color);
}
```

**Known Issues with ACES:**
- Hue shifts in saturated highlights (blues→purple, reds→orange)
- The desaturation in the RRT is intentional but controversial
- Not ideal for all art styles; designed for cinema interchange

### 6.3 AgX (Modern Alternative)
**Author:** Troy Sobotka  
**Philosophy:** Treat the display transform as a "formation" — map scene-referred values to display-referred through a perceptually uniform path that preserves hue identity.

**Key Differences from ACES:**
- No hue rotation in highlights (blue stays blue, not purple)
- More "filmic" shoulder with natural desaturation
- Better behavior with extreme dynamic range inputs
- Designed for real-time with simpler implementation

### 6.4 Per-Channel vs Luminance-Based Tonemapping
**The Great Debate (John Hable's insight):**

- **Per-channel:** Apply curve independently to R, G, B. Desaturates highlights (bright blue → white). More stable, no hue shifts in mid-tones. Industry standard for games.
- **Luminance-based:** Compute luminance, apply curve to luminance, restore chrominance. Preserves hue better but can produce oversaturated highlights and clipping artifacts. Better for cinema.
- **Hybrid approaches:** Tony McMapface and notorious6 from Tomasz Stachowiak use per-channel with perceptual gamut mapping to get best of both worlds.

### 6.5 Reversible Tonemapping for Temporal Effects
**Author:** Brian Karis (Epic Games)  
**Reference:** "Tone Mapping" (Graphic Rants blog, 2013)

```hlsl
// Apply before storing to history buffer (reduces fireflies in TAA/bloom)
float3 Tonemap(float3 color) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return color / (1.0 + luma / range);
}

float3 InverseTonemap(float3 color) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return color / (1.0 - luma / range);
}
```

Also see AMD GPUOpen's optimized reversible tonemapper for resolve operations.

---

## 7. Color Grading & Film Emulation

### 7.1 HDR Color Grading Pipeline
**Reference:** Alex Fry, "HDR Color Grading and Display in Frostbite" (GDC 2017)

**Pipeline Order Matters:**
```
Option A (Standard): Bloom → Tonemap → Color Grade → Output
Option B (Pre-Grade): Bloom → Color Grade (HDR) → Tonemap → Output
```
Pre-grading in HDR gives more range to work with but requires careful handling of values that exceed display range.

### 7.2 Per-Hue Operations
**Technique:** Isolate hue bands and apply independent adjustments

```hlsl
// Hue isolation using smooth masks
float HueMask(float3 hsv, float targetHue, float width) {
    float dist = abs(hsv.x - targetHue);
    dist = min(dist, 1.0 - dist); // Wrap around
    return smoothstep(width, 0.0, dist) * hsv.y; // Weight by saturation
}

// 7-band system: Red, Orange, Yellow, Green, Cyan, Blue, Magenta
// Apply independent saturation, luminance, hue shift per band
```

### 7.3 LUT-Based Color Grading
**Approaches:**
- **Bilinear LUT sampling:** Standard, 2D texture unwrapped from 3D
- **Tetrahedral interpolation:** Higher quality, fewer artifacts at color boundaries
- **Multi-LUT blending:** Weight multiple LUTs by time-of-day, weather, etc.

```hlsl
// Tetrahedral interpolation (higher quality than trilinear)
float3 SampleLUT_Tetrahedral(Texture2D lut, float3 color, float size) {
    float3 scaled = color * (size - 1.0);
    float3 base = floor(scaled);
    float3 frac = scaled - base;
    // Determine which tetrahedron the point is in
    // 6 possible tetrahedra per cube (based on frac ordering)
    // Interpolate using 4 vertices of the selected tetrahedron
}
```

### 7.4 Film Emulation
**Gamut Transform Pipeline:**
```
Linear sRGB → XYZ → Target Gamut (AP1, ARRI, etc.)
→ Log encoding (optional) → 3D LUT → Output Gamut → sRGB
```

**Camera Response Functions:** University of Columbia database provides measured response curves from real cameras, allowing exact reproduction of how specific cameras map irradiance to pixel values.

---

## 8. Volumetric Fog & Atmospheric Rendering

### 8.1 Frostbite Volumetric Fog (Hillaire)
**Author:** Sébastien Hillaire (EA/DICE)  
**Papers:** 
- "Physically-Based & Unified Volumetric Rendering in Frostbite" (SIGGRAPH 2015)
- "Physically Based Sky, Atmosphere & Cloud Rendering in Frostbite" (SIGGRAPH 2016)

**Architecture:**
1. Allocate a frustum-aligned 3D texture ("froxel grid") — e.g., 160×90×64
2. For each froxel, compute scattering and extinction from participating media
3. Accumulate in-scattered light per froxel (single scattering from all lights)
4. Ray-march through the froxel grid along view rays to composite
5. Apply temporal reprojection for stability

**Critical Implementation Details:**

**Froxel Depth Distribution (Exponential):**
```hlsl
// Exponential slice distribution puts more resolution near camera
float linearDepth = near * pow(far / near, (sliceIndex + 0.5) / numSlices);
```

**Analytical Integration (Energy-Conserving):**
```hlsl
// Instead of: transmittance *= exp(-sigmaE * stepSize)
// Use analytical integration per step:
float3 S = scatteredLight;  // In-scattered luminance
float sigmaE = extinction;
float stepSize = dd;

// Analytical integral of transmittance * source over step
float3 Sint = (S - S * exp(-sigmaE * stepSize)) / sigmaE;
luminance += transmittance * Sint;
transmittance *= exp(-sigmaE * stepSize);
```
This analytical form is critical for correctness with strong extinction values.

**Temporal Reprojection:**
- Store extinction (linear) instead of transmittance (non-linear) for better temporal blending
- Reproject froxel positions using previous frame's VP matrix
- Blend factor ~0.05 for temporal stability

### 8.2 Phase Functions

**Henyey-Greenstein:**
```hlsl
float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}
```

**Cornette-Shanks (improved HG):**
```hlsl
float CornetteShanks(float cosTheta, float g) {
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float den = 2.0 * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / (4.0 * PI * den);
}
```

**Dual-lobe (Frostbite):** Blend two HG lobes (forward + back scatter) for more realistic participating media.

### 8.3 Rayleigh & Mie Scattering
**For atmospheric sky rendering:**

```hlsl
// Rayleigh (small particles, wavelength-dependent)
float3 BetaRayleigh = float3(5.8e-6, 13.5e-6, 33.1e-6); // at sea level

// Mie (larger particles, forward-dominated)
float3 BetaMie = float3(21e-6, 21e-6, 21e-6);
float MieG = 0.76; // Forward scattering asymmetry
```

### 8.4 God Rays (Volumetric Light Scattering as Post-Process)
**Author:** Kenny Mitchell (Crytek, 2007) — GPU Gems 3

**Algorithm:**
1. Render depth-based occlusion mask (sky = 1, objects = 0)
2. Project sun position to screen space
3. For each pixel, march toward sun position, accumulating unoccluded samples
4. Apply decay, weight, and exposure controls
5. Additively blend with scene

---

## 9. Depth of Field

### 9.1 Separable Hexagonal Bokeh
**Authors:** John White, Colin Barré-Brisebois (DICE/EA, SIGGRAPH 2011)  
**Revisited by:** Colin Barré-Brisebois (2017)

**Rhomboid Decomposition:** A hexagon can be decomposed into two overlapping rhomboids. Each rhomboid is a skewed box blur that can be computed as two separable passes along different axes (0°, 60°, 120°).

**3-Pass Pipeline:**
1. Vertical blur (0°)
2. Diagonal blur A (60° from vertical output)
3. Diagonal blur B (120° from vertical output)
4. Combine: max(pass2, pass3) for hexagonal shape

**CoC (Circle of Confusion) Calculation:**
```hlsl
float CoC(float depth, float focalDist, float focalLength, float aperture) {
    float coc = abs(aperture * focalLength * (depth - focalDist)
              / (depth * (focalDist - focalLength)));
    return coc * viewportHeight * 0.5; // Convert to pixels
}
```

### 9.2 Circular Separable DoF (Frostbite/Kleber Garcia)
**Author:** Kleber Garcia (EA Frostbite)  
**Used in:** FIFA 17, Mass Effect Andromeda, Anthem, NFS Heat

Uses complex-valued convolutions to achieve circular bokeh with only 2 separable passes, based on the mathematical insight that certain circular kernels can be decomposed into sums of separable complex exponentials.

### 9.3 Scatter-Based Bokeh (Matt Pettineo / The Order: 1886)
**Key Innovation:** Extract bright pixels that exceed a threshold, emit them as point sprites with the aperture shape, then composite. More physically correct but requires geometry shader or compute dispatch.

### 9.4 Practical DoF Tips
- **Always separate near and far fields** to prevent far blur bleeding onto near sharp objects
- **Use premultiplied alpha** for the CoC to prevent color leaking
- **Half-resolution rendering** is standard; upscale with bilateral filter aware of depth discontinuities
- **Adaptive ring count** based on CoC size prevents undersampling large bokeh

---

## 10. Contact Shadows

### 10.1 Screen-Space Contact Shadows
**Used by:** UE4/5, Frostbite, most modern engines

**Algorithm:**
1. For each pixel, ray-march from the surface toward the light in screen space
2. At each step, compare ray depth against depth buffer
3. If ray passes behind geometry, mark as shadowed
4. Binary search refinement for precise boundary
5. Apply thickness check to prevent over-occlusion

**Critical Enhancements:**
```hlsl
// Adaptive thickness based on distance (farther = thicker threshold)
float adaptiveThickness = baseThickness * (1.0 + worldZ * 0.005);

// Power-based step distribution (more samples near surface)
float t_world = pow(t_linear, 1.6); // Concentrate samples near start

// Screen-edge fade
float2 edgeDist = min(uv, 1.0 - uv);
float edgeFade = saturate(min(edgeDist.x, edgeDist.y) / 0.04);

// Depth fade (don't shadow extremely distant objects)
float depthFade = 1.0 - smoothstep(maxDist * 0.5, maxDist, worldZ);
```

---

## 11. Motion Blur

### 11.1 Per-Object Motion Blur (McGuire Reconstruction Filter)
**Author:** Morgan McGuire  
**Paper:** "A Reconstruction Filter for Plausible Motion Blur" (I3D 2012)

**Key Insight:** Motion blur is not just a directional blur along the velocity vector — it must account for objects moving in front of/behind the current pixel. The reconstruction filter handles foreground/background separation correctly.

### 11.2 Camera Motion Blur
**Simple post-process approach:**
```hlsl
// Compute per-pixel velocity from current/previous VP matrices
float2 velocity = (currentNDC - previousNDC) * 0.5;

// Gather samples along velocity direction
float3 result = currentColor;
float totalWeight = 1.0;
for (int i = 1; i <= SAMPLES; i++) {
    float t = float(i) / float(SAMPLES);
    float2 offset = velocity * t;
    float3 sampleColor = tex2D(sceneColor, uv + offset);
    float sampleWeight = 1.0; // Can be depth-weighted
    result += sampleColor * sampleWeight;
    totalWeight += sampleWeight;
}
result /= totalWeight;
```

---

## 12. Lens Effects

### 12.1 Screen-Space Lens Flare (John Chapman)
**Author:** John Chapman  
**Reference:** https://john-chapman.github.io/2017/11/05/pseudo-lens-flare.html

**Pipeline:**
1. **Threshold** bright areas from scene (HDR threshold)
2. **Downsample** to low resolution
3. **Ghost generation:** Sample along vector through image center with multiple offsets and scales
4. **Halo:** Fixed-length vector from pixel to center, creating a ring
5. **Chromatic aberration** on ghosts: offset R/G/B channels slightly
6. **Gaussian blur** to soften
7. **Composite:** Multiply by lens dirt texture + diffraction starburst, blend with scene

**Ghost Sampling:**
```hlsl
// Flip texture coordinates (ghosts mirror around center)
float2 texcoord = -uv + float2(1.0);
float2 ghostVec = (float2(0.5) - texcoord) * ghostDispersal;

for (int i = 0; i < NUM_GHOSTS; i++) {
    float2 offset = frac(texcoord + ghostVec * float(i));
    float weight = length(float2(0.5) - offset) / length(float2(0.5));
    weight = pow(1.0 - weight, 10.0);
    result += tex2D(thresholdTex, offset) * weight;
}
```

### 12.2 Diffraction Starburst
```hlsl
// Procedural starburst based on aperture blade count
float Starburst(float2 uv, float rotation, int blades) {
    float2 centered = uv - 0.5;
    float angle = atan2(centered.y, centered.x) + rotation;
    float ray = cos(angle * float(blades)) * 0.5 + 0.5;
    return pow(ray, 4.0);
}
```

### 12.3 Anamorphic Lens Effects
**Author:** Bart Wronski  
**Key Insight:** Anamorphic lenses squeeze the image horizontally during capture. Lens flares and bloom happen in this squeezed space, then get stretched during projection — producing characteristic horizontal streaks.

**Implementation:** Simply process bloom/flare in a 2:1 squeezed buffer. The stretch happens naturally during upsample.

### 12.4 Physically-Based Lens Flare (Bitsquid)
**Advanced approach using:**
- Ray-tracing through a lens system model
- Fraunhofer diffraction approximation for starburst
- Ghost generation from inter-element reflections
- Aperture SDF for procedural diaphragm shapes

---

## 13. Dithering & Banding Reduction

### 13.1 Blue Noise Dithering
**Key Property:** Blue noise has no low-frequency content, making it perceptually invisible.

```hlsl
// Gauss-filtered blue noise dither (reduces banding in gradients)
void GaussBlueDither(inout float3 color, float2 screenPos, float bitDepth) {
    float noise = BlueNoiseTex.Sample(wrapSampler, screenPos / 128.0).r;
    noise = noise * 2.0 - 1.0; // [-1, 1]
    float quantStep = 1.0 / (pow(2.0, bitDepth) - 1.0);
    color += noise * quantStep;
}
```

### 13.2 Temporal Dithering
Rotate the dither pattern each frame; TAA will accumulate and smooth it.

### 13.3 Ordered (Bayer) Dithering
Useful for specific aesthetic effects; produces structured pattern.

---

## 14. Material & PBR Enhancement

### 14.1 G-Buffer Reading for Material Classification
**From depth + normals + albedo, classify materials:**
```hlsl
// Skin detection heuristic (from G-buffer albedo)
float3 albedo = GBuffer_Albedo.Sample(uv);
float skinLikelihood = SkinDetection(albedo);

// Material roughness from specular response
float roughness = EstimateRoughness(specular, normal, viewDir);
```

### 14.2 Micro-Shadowing (Chan, Call of Duty: WWII)
```hlsl
// Attenuate unshadowed direct light using AO
float microShadow = saturate(abs(dot(normal, lightDir)) + 2.0 * ao * ao - 1.0);
directLight *= microShadow;
```

### 14.3 Multi-Bounce AO Approximation
**From Jiménez et al. GTAO paper:**
```hlsl
// Recover energy lost by single-bounce AO approximation
float3 MultiBounceAO(float ao, float3 albedo) {
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
    return max(ao, ((ao * a + b) * ao + c) * ao);
}
```

---

## 15. Post-Process Pipeline Architecture

### 15.1 Recommended Pipeline Order
```
1.  Eye Adaptation / Auto-Exposure (1×1 texture from histogram)
2.  Exposure normalization (divide scene by adapted luminance)
3.  Bloom extraction + downsample chain + upsample chain
4.  Bloom composite (additive or screen blend)
5.  Color grading (HDR) [if pre-grade pipeline]
6.  Tonemapping
7.  Color grading (LDR) [if post-grade pipeline]
8.  Film emulation / LUT application
9.  Dithering
10. Output (sRGB conversion if needed)
```

### 15.2 Linear Light vs sRGB Considerations
- **All operations before tonemapping** should be in linear light
- **Bloom must be computed in linear** — doing it in sRGB causes incorrect energy distribution
- **TAA neighborhood operations** in YCoCg for precision
- **Color grading** can be in either space depending on artistic intent
- **Dithering** should be the very last operation before display

### 15.3 ENB-Specific Pipeline Constraints
- Pixel shaders only (no compute)
- Fixed render target formats (RGBA32, RGBA64, RGBA64F, R16F)
- Limited temporal persistence (must store in available render targets between techniques)
- Multiple technique passes within a single .fx file
- Day/Night/Interior parameter separation for time-of-day adaptation

---

## 16. ENB-Specific Techniques from Community Authors

### 16.1 Notable ENB Authors & Their Contributions

| Author | ENB(s) | Notable Techniques |
|--------|--------|-------------------|
| Boris Vorontsov | ENBSeries core | The entire framework — all shader hooks, parameter system, render targets |
| KitsuuneNivis | Silent Horizons 2 | Custom shader core, universal compatibility, extensive UI system |
| rudy102 | Rudy ENB | NAT.III integration, photorealistic color grading |
| Confidence Man | NLA (Natural Lighting & Atmospherics) | Pioneered realistic color correction and lighting in ENB |
| Tapioks | Enhanced Shaders | One of the first ENB adaptations, SSS tweaking |
| Kynias | Kauz ENB | Dramatic lighting, atmospheric color schemes on SH2 core |
| MechanicalPanda | Dawnfire ENB | Grounded cinematic look |
| The Sandvich Maker | Re-Engaged/NVT/Intrigued/Semirealis | Multiple weather-specific ENBs, gloomy aesthetics |

### 16.2 Community Shaders (Open Source Alternative)
**Authors:** Doodlum, Nukem, Jonahex, ProfJack, alandtse, FlayaN, others  
**Source:** https://github.com/doodlum/skyrim-community-shaders

**Features (not compatible with ENB):**
- TruePBR: Full physically-based rendering with BRDF replacement
- Dynamic cubemaps for environment reflections
- Screen-space shadows
- Grass collision
- Light limit fix
- SKSE-based, hooks into game shader pipeline directly

### 16.3 haasn's ENB Shader Framework
**Author:** haasn  
**Source:** https://github.com/haasn/ENB-Shaders (MIT-like)

Multi-technique framework compatible with all ENBSeries versions. Good reference for clean ENB architecture patterns.

### 16.4 AcerolaFX (ReShade, but Excellent Reference)
**Author:** Garrett Gunnell (Acerola)  
**Source:** https://github.com/GarrettGunnell/AcerolaFX

Comprehensive post-processing suite with excellent documentation. Pipeline includes: AO, fog, bloom, blend layers, HDR color correction, auto-exposure, tonemapping, LDR color correction, adaptive sharpness, DoF, dithering, and dual sharpening passes.

---

## 17. Master Bibliography

### Foundational Papers
1. Reinhard et al., "Photographic Tone Reproduction for Digital Images," SIGGRAPH 2002
2. Jensen & Buhler, "A Rapid Hierarchical Rendering Technique for Translucent Materials," ACM TOG 2002
3. Hable, "Filmic Tonemapping Operators," GDC 2010
4. Hejl & Burgess, "Filmic Tonemapping for Real-Time Rendering," SIGGRAPH 2010
5. McGuire et al., "Scalable Ambient Obscurance," I3D 2012
6. McGuire & Mara, "Efficient GPU Screen-Space Ray Tracing," JCGT 3(4), 2014
7. Karis, "High Quality Temporal Supersampling," SIGGRAPH 2014 Advances course
8. Jimenez et al., "Separable Subsurface Scattering," CGF 34(6), 2015
9. Narkowicz, "ACES Filmic Tone Mapping Curve," 2016
10. Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion," SIGGRAPH 2016

### Volumetric & Atmosphere
11. Mitchell, "Volumetric Light Scattering as a Post-Process," GPU Gems 3, 2007
12. Wronski, "Volumetric Fog: Unified Compute Shader-based Solution," SIGGRAPH 2014
13. Hillaire, "Physically-Based & Unified Volumetric Rendering in Frostbite," SIGGRAPH 2015
14. Hillaire, "Physically Based Sky, Atmosphere & Cloud Rendering in Frostbite," SIGGRAPH 2016
15. Bruneton & Neyret, "Precomputed Atmospheric Scattering," EGSR 2008

### Reflections
16. Uludag, "Hi-Z Screen-Space Cone-Traced Reflections," GPU Pro 5, 2014
17. Stachowiak, "Stochastic Screen-Space Reflections," SIGGRAPH 2015
18. AMD FidelityFX SSSR documentation, GPUOpen

### Depth of Field
19. White & Barré-Brisebois, "More Performance! Five Rendering Ideas," SIGGRAPH 2011
20. McIntosh et al., "Efficiently Simulating the Bokeh of Polygonal Apertures," CGF 2012
21. Wronski, "Bokeh depth of field — going insane!" (blog series, 2014)
22. Garcia, "Circular Separable DoF" (Frostbite, 2017)

### TAA & Anti-Aliasing
23. Lottes, "TSSAA (Temporal Super-Sampling AA)," 2011
24. Sousa, "Anti-Aliasing Methods in CryEngine 3," SIGGRAPH 2011
25. Salvi, "An Excursion in Temporal Supersampling," GDC 2016
26. Yang, Liu, Salvi, "A Survey of Temporal Antialiasing Techniques," CGF 2020
27. Intel TAA implementation, GameTechDev/TAA

### Color Science & Grading
28. Fry, "HDR Color Grading and Display in Frostbite," GDC 2017
29. Lagarde & de Rousiers, "Moving Frostbite to PBR," SIGGRAPH 2014
30. Lottes, "Advanced Techniques and Optimization of VDR Color Pipelines," GDC 2016
31. Hill, "sRGB Approximation for ACES Output Transform," 2016
32. Sobotka, AgX formation-based display mapping, 2022
33. Stachowiak, "Tony McMapface" and "notorious6" display mapping demos

### Lens Effects
34. Chapman, "Pseudo Lens Flare" (screen-space technique), 2013/2017
35. Wronski, "Anamorphic lens flares and visual effects," 2015
36. Bitsquid, "Physically Based Lens Flare," 2017

### ENB & Skyrim Specific
37. Vorontsov, ENBSeries documentation, enbdev.com
38. KitsuuneNivis, Silent Horizons 2 shader architecture
39. Community Shaders project, https://github.com/doodlum/skyrim-community-shaders

### Additional Resources
40. Advances in Real-Time Rendering course (SIGGRAPH, annually since 2006): https://www.advances.realtimerendering.com/
41. Physically Based Shading course (SIGGRAPH, annually): https://blog.selfshadow.com/
42. ShaderToy community implementations
43. Karis, "Real Shading in Unreal Engine 4," SIGGRAPH 2013
44. Tardif, "Reframing Tonemapping in Games" + "TAA Starter Pack" (blog posts)
45. Marty's Mods / ImmersePro DoF documentation

---

## Appendix A: Technique Priority Matrix for SkyrimBridge

Based on visual impact, performance cost, and ENB compatibility:

| Priority | Technique | Status | Impact | Cost | Difficulty |
|----------|-----------|--------|--------|------|------------|
| ★★★★★ | XeGTAO | Implemented | Very High | Medium | High |
| ★★★★★ | HDR Bloom (Karis) | Implemented | Very High | Low | Medium |
| ★★★★★ | Tonemapping Suite | Implemented | Very High | Negligible | Low |
| ★★★★★ | Color Grading Pipeline | Implemented | Very High | Low | Medium |
| ★★★★☆ | SSR (Linear + Binary) | Implemented | High | Medium | High |
| ★★★★☆ | Contact Shadows | Implemented | High | Medium | Medium |
| ★★★★☆ | TAA (Variance Clip) | Implemented | High | Low | High |
| ★★★★☆ | Volumetric Fog | Implemented | High | Medium-High | Very High |
| ★★★★☆ | Separable SSS | Implemented | High | Low | Medium |
| ★★★★☆ | Film Emulation / LUTs | Implemented | High | Low | Medium |
| ★★★☆☆ | God Rays | Implemented | Medium | Low | Low |
| ★★★☆☆ | Depth of Field | Implemented | Medium | Medium | Medium |
| ★★★☆☆ | Lens Flare (Procedural) | Partial | Medium | Low | Medium |
| ★★★☆☆ | Motion Blur | Partial | Medium | Low | Medium |
| ★★☆☆☆ | Painterly Filter | Implemented | Niche | Medium | Medium |
| ★★☆☆☆ | CRT Shader | Implemented | Niche | Low | Low |

## Appendix B: Techniques to Investigate Further

1. **AgX / Tony McMapface tonemapping** — Modern alternatives to ACES with better hue preservation
2. **Stochastic SSR** — Blue noise-driven with temporal denoiser for glossy reflections
3. **Circular Separable DoF** (Kleber Garcia) — Complex-valued convolution for perfect circular bokeh
4. **Adaptive Sharpening** (CAS / Contrast Adaptive Sharpening from AMD) — Post-TAA sharpening
5. **Screen-Space Global Illumination** (SSGI) — One-bounce diffuse GI from depth buffer
6. **Bent Normal Ambient Occlusion** — Directional AO for specular occlusion
7. **Procedural Weather Effects** — Rain on lens, frost, condensation (SkyrimBridge-driven)
8. **Multi-bounce Volumetric Scattering** — Beyond single-scatter for richer fog
9. **Chromatic Aberration** (physically motivated) — Wavelength-dependent lateral shift
10. **Film Grain** (perceptual, temporal) — Blue-noise-driven with photographic characteristics

---

*This document is a living reference. Update as new techniques are discovered and integrated.*
