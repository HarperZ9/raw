# SkyrimBridge Shader Improvement Blueprint
## Bridging Every Gap — From Community State-of-Art to Production-Engine Quality

**Scope:** Concrete algorithmic improvements to every SB_ shader, drawn from research into
MartyMcModding/MXAO, XeGTAO, GT-VBAO (Mirko Salm), Jimenez SSSS, Therrien SSILVB,
AMD FidelityFX Optical Flow, Frostbite/UE5 production techniques, and 6 ENB shader authors.

Each section: **Current state → Gap → Improvement → HLSL pseudocode → SkyrimBridge integration.**

---

## Table of Contents

1. [SB_GTAO: GT-VBAO with Cosine-Weighted Bitmask + Material Thickness](#1-sb_gtao)
2. [SB_SSR: Stochastic Roughness Reflections + Specular Occlusion](#2-sb_ssr)
3. [SB_HDRBloom: Dual-Kernel Pseudo-Convolution Bloom](#3-sb_hdrbloom)
4. [SB_TemporalAA: Dense Optical Flow + Mitchell Resolve Filter](#4-sb_temporalaa)
5. [SB_MotionBlur: Optical Flow Enhancement + Tile-Based Scattering](#5-sb_motionblur)
6. [SB_MaterialPBR: Separable SSS Diffusion Profiles + Pre-Integrated Skin](#6-sb_materialpbr)
7. [SB_ContactShadows: Penumbra Softening + Multi-Light Tracing](#7-sb_contactshadows)
8. [SB_VolumetricFog: Temporal Reprojected Fog + Cloud Shadows](#8-sb_volumetricfog)
9. [NEW: SB_SpecularOcclusion — AO-to-SO Pipeline + Bent Normal Estimation](#9-sb_specularocclusion)
10. [NEW: SB_ScreenSpaceCaustics — Refraction Ray Marching for Water](#10-sb_screenspacecaustics)
11. [NEW: SB_Launchpad — Temporal Data Collection Prepass](#11-sb_launchpad)
12. [SkyrimBridge.fxh: Temporal Buffer API Extensions](#12-skyrimbridge-temporal-api)
13. [Cross-Shader Data Flow Architecture](#13-cross-shader-data-flow)

---

## 1. SB_GTAO: GT-VBAO with Cosine-Weighted Bitmask + Material Thickness
<a name="1-sb_gtao"></a>

### Current State
- XeGTAO-based: horizon-based with analytic inner integral (`-cos(2h-n)+n+2h*sin(n)`)
- Material-aware multipliers (skin/metal/foliage)
- Basic distance-based falloff: `lerp(-1, hc, saturate(1 - hd²/(R²*falloff)))`

### Gap Analysis

**Gap 1: Falloff towards -1 causes haloing**
Your current falloff interpolates the horizon cosine toward -1 when a sample is far away.
XeGTAO showed this is suboptimal — they interpolate toward the hemisphere horizon
`cos(normal_angle ± PI/2)` instead, making falloff independent of the projected normal and
reducing haloing and detail loss under steep viewing angles.

**Gap 2: No visibility bitmask — can't handle thin occluders**
Standard GTAO treats the depth buffer as a heightfield with infinite thickness. Thin objects
(fences, tree branches, grass) cause over-darkening and halos. The visibility bitmask
(Therrien et al. 2023) replaces the two horizon angles with a 32-bit field representing
N sectors of the hemisphere slice, allowing light to pass *behind* thin surfaces.

**Gap 3: No cosine-weighted hemisphere integration**
Marty's MXAO claims a better horizon falloff AND cosine term that the original VBAO
paper lacks. GT-VBAO (Mirko Salm, 2024) solves this: it remaps the min/max horizon angles
by the cosine-weighted CDF, producing radiometrically correct results that match a
cosine-weighted ray tracer ground truth.

**Gap 4: No material-aware thickness heuristic**
All existing implementations use a fixed thickness. SkyrimBridge knows material types —
skin should have thinner thickness (it's a thin shell), foliage should be very thin (leaves),
stone/terrain should be thicker.

### Improvement: GT-VBAO with Material-Aware Thickness

```hlsl
// ═══ NEW CORE: Visibility Bitmask AO with cosine weighting ═══

static const uint SECTOR_COUNT = 32u;

// Bitmask sector update — sets bits between front and back horizon angles
uint UpdateSectors(float minH, float maxH, uint existingMask) {
    uint startBit = uint(minH * float(SECTOR_COUNT));
    uint arcBits  = uint(ceil((maxH - minH) * float(SECTOR_COUNT)));
    uint mask     = arcBits > 0u ? (0xFFFFFFFFu >> (SECTOR_COUNT - arcBits)) : 0u;
    return existingMask | (mask << startBit);
}

// Cosine-weighted CDF remap for GT-VBAO correctness
float CosineWeightedRemap(float angle, float normalAngle) {
    // Remap from uniform sector space to cosine-weighted space
    // This accounts for the cos(theta) falloff in the diffuse BRDF
    float cosAngle = cos(angle - normalAngle);
    return saturate(0.5 * (1.0 + cosAngle));
}

float ComputeGTVBAO(float2 uv, float2 px) {
    float depth = TextureDepth.SampleLevel(Sampler0, uv, 0).r;
    if (depth >= 0.9999) return 1.0;

    float3 viewPos = ViewPosFromDepth(uv, depth);
    float3 viewDir = normalize(-viewPos);
    float3 normal  = SB_ReadNormal(SB_GBuffer_Normal, int2(px));
    // ... fallback to depth-derived normals if needed ...

    // ─── Material-aware thickness ───
    int mat = SB_ReadMaterialID(SB_GBuffer_Material, int2(px));
    float thickness = aoThickness; // base UI parameter
    if (mat == SB_MAT_SKIN || mat == SB_MAT_HAIR) thickness *= 0.4;
    else if (mat == SB_MAT_FOLIAGE) thickness *= 0.25;
    else if (mat == SB_MAT_TERRAIN || mat == SB_MAT_STONE) thickness *= 2.0;

    float noise = IGNoise(px + SB_Render_Frame.z * 5.0); // temporal rotation
    float totalVisibility = 0.0;
    float sliceAngle = 3.14159265 / float(aoSlices);

    for (int s = 0; s < aoSlices; s++) {
        float phi = sliceAngle * (float(s) + noise);
        float2 dir = float2(cos(phi), sin(phi));

        // Project normal onto slice plane
        float3 sliceDir    = float3(dir, 0.0);
        float3 orthoDir    = sliceDir - dot(sliceDir, viewDir) * viewDir;
        float3 axis        = cross(sliceDir, viewDir);
        float3 projNormal  = normal - axis * dot(normal, axis);
        float  projLen     = length(projNormal);
        float  signN       = sign(dot(orthoDir, projNormal));
        float  cosN        = clamp(dot(projNormal, viewDir) / max(projLen, 1e-6), 0.0, 1.0);
        float  normalAngle = signN * acos(cosN);

        uint occludedBitmask = 0u;
        float radPx = clamp(aoRadius / max(viewPos.z, 0.1) * ScreenSize.x * 0.5, 3.0, 128.0);

        for (int t = 1; t <= aoSteps; t++) {
            float stepFrac = float(t) / float(aoSteps);
            // Squared distribution for better near-field sampling (from XeGTAO)
            stepFrac = stepFrac * stepFrac;

            for (int side = 0; side < 2; side++) {
                float sideSign = (side == 0) ? 1.0 : -1.0;
                float2 offset = dir * sideSign * stepFrac * radPx;
                float2 sUV = uv + offset * ScreenSize.zw;
                if (any(sUV < 0) || any(sUV > 1)) continue;

                float3 sPos = ViewPosFromDepth(sUV,
                    TextureDepth.SampleLevel(Sampler0, sUV, 0).r);
                float3 delta = sPos - viewPos;
                float  dLen  = length(delta);

                // Front-face horizon angle
                float frontCos = dot(normalize(delta), viewDir);
                float frontH   = acos(clamp(frontCos, -1.0, 1.0));

                // Back-face horizon angle (finite thickness assumption)
                float3 backDelta = delta - viewDir * thickness;
                float  backCos   = dot(normalize(backDelta), viewDir);
                float  backH     = acos(clamp(backCos, -1.0, 1.0));

                // Shift by projected normal angle and normalize to [0,1]
                float2 hFrontBack;
                if (side == 0) {
                    hFrontBack = float2(frontH, backH);
                } else {
                    hFrontBack = float2(-frontH, -backH);
                    hFrontBack = hFrontBack.yx; // flip and sort
                }
                hFrontBack = saturate((hFrontBack + normalAngle + 1.5707963) / 3.14159265);

                // GT-VBAO cosine weighting: remap by CDF
                hFrontBack.x = CosineWeightedRemap(hFrontBack.x * 3.14159265, normalAngle);
                hFrontBack.y = CosineWeightedRemap(hFrontBack.y * 3.14159265, normalAngle);

                occludedBitmask = UpdateSectors(
                    min(hFrontBack.x, hFrontBack.y),
                    max(hFrontBack.x, hFrontBack.y),
                    occludedBitmask);
            }
        }

        // Visibility = fraction of unoccluded sectors
        totalVisibility += 1.0 - float(countbits(occludedBitmask)) / float(SECTOR_COUNT);
    }

    totalVisibility /= float(aoSlices);
    return totalVisibility;
}
```

### XeGTAO Horizon Falloff Fix (simpler, can be used without full bitmask)
```hlsl
// BEFORE (your current code - falls off toward -1):
hc = lerp(-1.0, hc, saturate(1.0 - hd*hd / (R*R*falloff)));

// AFTER (XeGTAO's fix - falls off toward hemisphere horizon):
float hemisphereHorizon = cos(normalAngle + (side==0 ? PI/2 : -PI/2));
hc = lerp(hemisphereHorizon, hc, saturate(1.0 - hd*hd / (R*R*falloff)));
// This makes falloff independent of projected normal, eliminates haloing
```

### Multi-Bounce AO Approximation (from UE5)
```hlsl
// After computing AO, apply multi-bounce color correction
// Without this, AO over-darkens colored surfaces (Jimenez 2016)
float3 MultiBounceAO(float ao, float3 albedo) {
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c =  2.7552 * albedo + 0.6903;
    return max(ao, ((ao * a + b) * ao + c) * ao);
}
// Apply: color.rgb *= MultiBounceAO(ao, color.rgb); instead of color.rgb *= ao;
```

### SkyrimBridge-Exclusive Enhancement
```hlsl
// Combat-reactive AO: intensify in combat for dramatic shadows
float combatMult = lerp(1.0, 1.3, SB_Player_Combat.x);
// Weather-aware radius: foggy weather = shorter AO radius (fog occludes)
float weatherScale = lerp(1.0, 0.5, SB_Fog_Density.y);
ao = 1.0 - saturate((1.0 - ao) * combatMult);
```

---

## 2. SB_SSR: Stochastic Roughness Reflections + Specular Occlusion
<a name="2-sb_ssr"></a>

### Current State
- Hi-Z ray march with binary refinement
- Material-aware: skip skin/hair, boost metals, custom water intensity
- Schlick Fresnel with roughness fade
- Single ray per pixel (mirror reflection only)

### Gap Analysis

**Gap 1: No roughness-driven ray jittering (stochastic SSR)**
Current SSR only traces a single mirror-reflection ray. Rough surfaces should scatter
reflection rays across a cone proportional to roughness. Without this, rough metals and
wet surfaces appear unrealistically sharp or simply fade out.

**Gap 2: No specular occlusion from AO data**
SSR should be attenuated in areas where AO indicates heavy occlusion. Light leaking in
corners (where SSR catches distant bright surfaces through occluded geometry) is a
well-known artifact. UE5 intersects the specular lobe with the AO bent cone.

**Gap 3: No temporal accumulation of reflection samples**
A single ray per pixel with roughness jitter is noisy. Accumulating samples over frames
(using SkyrimBridge's motion vectors for reprojection) converges to ground truth.

### Improvement: Stochastic SSR with Importance-Sampled GGX

```hlsl
// ═══ GGX importance sampling for rough reflections ═══

float2 Hammersley(uint i, uint N) {
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float2(float(i) / float(N), float(bits) * 2.3283064365386963e-10);
}

float3 ImportanceSampleGGX(float2 Xi, float roughness, float3 N) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;

    // Tangent space to world space
    float3 up = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    return normalize(T * H.x + B * H.y + N * H.z);
}

float4 PS_SSR_Stochastic(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    // ... existing setup ...
    SB_PBRSurface surf = SB_GetSurface(/*...*/);

    // Temporal sample index: different ray direction each frame
    uint frameIdx = uint(SB_Render_Frame.x) % 16u;
    float2 Xi = Hammersley(frameIdx, 16u);
    // Add spatial noise for variation between pixels
    Xi.x = frac(Xi.x + IGNoise(pos.xy));

    float3 halfVec;
    if (surf.roughness < 0.05) {
        // Mirror-like: use exact reflection
        halfVec = normal;
    } else {
        // Importance sample GGX distribution
        halfVec = ImportanceSampleGGX(Xi, surf.roughness, normal);
    }

    float3 reflDir = reflect(viewDir, halfVec);

    // ... existing ray march with reflDir instead of mirror reflection ...

    // Specular occlusion from AO (if AO data available from SB_GTAO)
    // Read AO from previous pass (stored in alpha of render target)
    float aoValue = /* SB_GTAO output, e.g. from texture */;
    float specOcc = SpecularOcclusionFromAO(NdotV, aoValue, surf.roughness);
    alpha *= specOcc;

    // ... rest of compositing ...
}

// Specular Occlusion approximation (Lagarde 2014, used in UE4/5)
float SpecularOcclusionFromAO(float NdotV, float ao, float roughness) {
    return saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}
```

---

## 3. SB_HDRBloom: Dual-Kernel Pseudo-Convolution Bloom
<a name="3-sb_hdrbloom"></a>

### Current State
- Single Gaussian blur with Karis anti-firefly weighting
- Material-aware thresholds (skin glow, metal spec, emissive boost, eye catchlight)
- 13-tap Gaussian weights, 2D loop (expensive: N² samples)

### Gap Analysis

**Gap 1: True convolution bloom requires FFT (compute shaders, not available in ENB)**
Marty's Convolution Bloom uses FFT in compute shaders to produce physically-accurate
diffraction spikes. ENB can't do compute shaders, but we can approximate the visual
result through a multi-kernel approach.

**Gap 2: Single Gaussian = uniform circular bloom, no diffraction character**
Real camera lenses produce star patterns, anamorphic streaks, and non-uniform halos.
The current single Gaussian just makes things glow uniformly.

**Gap 3: 2D Gaussian loop is O(N²) — separable passes not exploited**
The current implementation loops N×N, but Gaussians are separable into two 1D passes.
ENB supports multiple techniques within a single .fx file — use them for H and V passes.

### Improvement: Multi-Kernel Separable Bloom with Diffraction Approximation

```hlsl
// ═══ Strategy: Multiple separable blur passes at different scales ═══
// Pass 0: Threshold + Karis downscale
// Pass 1: Horizontal Gaussian (wide) — stored in RenderTarget
// Pass 2: Vertical Gaussian (wide) — completes main bloom
// Pass 3: Horizontal streak (narrow, stretched) — anamorphic character
// Pass 4: Vertical streak — star/diffraction cross
// Pass 5: Composite

// ENB supports texture targets within .fx files:
texture2D texBloomH { Width = BUFFER_WIDTH/2; Height = BUFFER_HEIGHT/2; Format = R11G11B10F; };
// Note: If ENB doesn't support custom render targets in addon shaders,
// we encode intermediate data in the alpha channel or use multi-technique ping-pong

// ─── Karis average for threshold (prevents fireflies) ───
float3 KarisAverage(float3 c) {
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    return c / (1.0 + luma);
}

float3 KarisInverse(float3 c) {
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    return c / max(1.0 - luma, 1e-4);
}

// ─── Diffraction spike kernel (approximates physical lens) ───
// Instead of uniform Gaussian, use oriented 1D kernels at 0°, 60°, 120°
// to create 6-pointed star pattern (hexagonal aperture approximation)
float3 DirectionalBloom(Texture2D src, float2 uv, float2 direction, int samples, float radius) {
    float3 result = 0;
    float totalW = 0;
    for (int i = -samples; i <= samples; i++) {
        float t = float(i) / float(samples);
        float w = exp(-3.0 * t * t); // Gaussian falloff along streak
        float2 sUV = uv + direction * t * radius * ScreenSize.zw;
        result += src.SampleLevel(Sampler0, sUV, 0).rgb * w;
        totalW += w;
    }
    return result / totalW;
}

// Composite: blend multiple streak directions for star pattern
float3 StarBloom(Texture2D src, float2 uv, int samples, float radius) {
    float3 bloom = 0;
    // 6-pointed star (3 orientations, each contributes both directions)
    float2 dirs[3] = {
        float2(1, 0),                          // horizontal
        float2(0.5, 0.866),                    // 60 degrees
        float2(-0.5, 0.866)                    // 120 degrees
    };

    for (int d = 0; d < 3; d++) {
        bloom += DirectionalBloom(src, uv, dirs[d], samples, radius);
    }
    return bloom / 3.0;
}

// ─── Weather-aware bloom ───
// SkyrimBridge knows exact weather transition progress
float GetWeatherBloomScale() {
    float fogInfluence = SB_Fog_Density.y * 0.3; // foggy = more scatter = more bloom
    float rainDampening = SB_Precipitation.y * 0.2; // rain droplets diffuse light
    return 1.0 + fogInfluence + rainDampening;
}
```

---

## 4. SB_TemporalAA: Dense Optical Flow + Mitchell Resolve Filter
<a name="4-sb_temporalaa"></a>

### Current State
- Variance clipping in YCoCg space (good foundation)
- Motion vector reprojection from SB_GBuffer_Motion
- Material-aware blend factors (skin stronger, effects weaker)
- Basic sharpening via negative lobe

### Gap Analysis

**Gap 1: No resolve filter (jitter is not un-jittered)**
Best TAA implementations (Karis 2014, Pedersen 2016) use a Mitchell-Netravali or
Blackman-Harris filter in the resolve to reconstruct the current frame at pixel center,
treating it as a set of sub-samples. This eliminates jitter and stabilizes the image.

**Gap 2: Motion vectors may be imprecise for non-rigid motion**
SkyrimBridge provides camera-derived motion vectors via previous ViewProj. This handles
camera motion perfectly but misses per-object motion (NPC walking, swinging weapon).
Supplementing with optical flow estimation fills this gap.

**Gap 3: No Catmull-Rom history sampling**
Bilinear sampling of the history buffer introduces blur. Catmull-Rom (bicubic) interpolation
gives sharper history at the cost of 4 extra texture fetches — a worthwhile trade.

**Gap 4: No luminance-weighted blend**
Bright pixels should favor the current frame more (they're more likely to be new information
like specular highlights). Luminance weighting prevents ghosting on bright objects.

### Improvement: Production-Quality TAA Resolve

```hlsl
// ═══ Mitchell-Netravali filter for current frame reconstruction ═══

// Mitchell filter: B=1/3, C=1/3 — good balance of blur/ringing
float MitchellWeight(float x) {
    float ax = abs(x);
    if (ax < 1.0) {
        return (7.0 * ax*ax*ax - 12.0 * ax*ax + 5.333333) / 6.0;
    } else if (ax < 2.0) {
        return (-2.333333 * ax*ax*ax + 12.0 * ax*ax - 20.0 * ax + 10.666667) / 6.0;
    }
    return 0.0;
}

// 3x3 Mitchell filter to un-jitter the current frame
float3 FilterCurrentFrame(Texture2D src, float2 uv, float2 jitter) {
    float3 result = 0;
    float totalW = 0;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float2 sUV = uv + float2(x, y) * ScreenSize.zw;
            // Distance from sample to unjittered pixel center
            float2 d = float2(x, y) - jitter;
            float w = MitchellWeight(d.x) * MitchellWeight(d.y);
            result += src.SampleLevel(Sampler0, sUV, 0).rgb * w;
            totalW += w;
        }
    }
    return result / totalW;
}

// ═══ Catmull-Rom bicubic history sampling ═══
float3 SampleHistoryCatmullRom(Texture2D tex, float2 uv) {
    float2 texSize = float2(ScreenSize.x, ScreenSize.y);
    float2 samplePos = uv * texSize;
    float2 tc = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - tc;
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    // Catmull-Rom weights
    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    float2 w3 = 0.5 * (f3 - f2);

    // Collapse to 2 bilinear fetches per axis (4 total instead of 16)
    float2 s0 = w0 + w1;
    float2 s1 = w2 + w3;
    float2 f0 = w1 / s0;
    float2 f1 = w3 / s1;

    float2 t0 = (tc - 1.0 + f0) / texSize;
    float2 t1 = (tc + 1.0 + f1) / texSize;

    return (tex.SampleLevel(Sampler0, float2(t0.x, t0.y), 0).rgb * s0.x +
            tex.SampleLevel(Sampler0, float2(t1.x, t0.y), 0).rgb * s1.x) * s0.y +
           (tex.SampleLevel(Sampler0, float2(t0.x, t1.y), 0).rgb * s0.x +
            tex.SampleLevel(Sampler0, float2(t1.x, t1.y), 0).rgb * s1.x) * s1.y;
}

// ═══ Luminance-weighted blend factor ═══
float LuminanceWeight(float3 color) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma); // bright pixels → lower weight → more current frame
}

// ═══ In the resolve ═══
float3 current  = FilterCurrentFrame(TextureColor, uv, SB_Render_Jitter.xy);
float3 history  = SampleHistoryCatmullRom(TextureOriginal, prevUV);
// ... variance clip in YCoCg as before ...

// Luminance-weighted blend
float wCurr = LuminanceWeight(current);
float wHist = LuminanceWeight(history);
float blendAlpha = max(blend, wCurr / (wCurr + wHist));
float3 result = lerp(history, current, blendAlpha);
```

---

## 5. SB_MotionBlur: Optical Flow Enhancement + Tile-Based Scattering
<a name="5-sb_motionblur"></a>

### Current State
- Per-pixel camera-derived motion vectors
- Bidirectional sampling (forward + backward)
- Material-aware: soft on skin, none on sky, boosted on particles
- Depth-aware weighting to prevent bleeding

### Gap Analysis

**Gap 1: Camera-only motion misses per-object movement**
A sword swing creates no motion blur if the camera is stationary. True per-object motion
vectors require engine hooks (Layer 2 SKSE plugin), but we can supplement with
simplified optical flow estimation between frames.

**Gap 2: No tile-based max-velocity scattering (McGuire 2012)**
Current implementation gathers samples along the center pixel's velocity. This misses
cases where a fast-moving object *should* blur over a slow-moving background pixel.
Tile-based approaches (used in every AAA engine) find the maximum velocity in each
tile and scatter it to neighbors.

### Improvement: Hybrid Motion Vectors + Simplified Optical Flow

```hlsl
// ═══ Simplified optical flow for per-object motion ═══
// Compare luminance blocks between current and previous frame
// This is a lightweight version of AMD FidelityFX Optical Flow

float2 EstimateOpticalFlow(float2 uv, float2 px) {
    // Camera-derived motion vector as initial estimate
    float3 worldPos = SB_WorldPosFromDepth(uv,
        TextureDepth.SampleLevel(Sampler0, uv, 0).r);
    float2 cameraMotion = SB_MotionVector(worldPos, uv);

    // Search a small window around the camera-predicted position
    float2 prevUV = uv - cameraMotion;
    float  currLuma = dot(TextureColor.SampleLevel(Sampler0, uv, 0).rgb,
                          float3(0.299, 0.587, 0.114));

    float bestSAD = 9999.0;
    float2 bestOffset = 0;
    int searchRadius = 2; // ±2 pixel search (lightweight)

    for (int sy = -searchRadius; sy <= searchRadius; sy++) {
        for (int sx = -searchRadius; sx <= searchRadius; sx++) {
            float2 testUV = prevUV + float2(sx, sy) * ScreenSize.zw;
            if (any(testUV < 0) || any(testUV > 1)) continue;

            // Sum of absolute differences over a 3x3 block
            float sad = 0;
            for (int by = -1; by <= 1; by++) {
                for (int bx = -1; bx <= 1; bx++) {
                    float2 currSample = uv + float2(bx, by) * ScreenSize.zw;
                    float2 prevSample = testUV + float2(bx, by) * ScreenSize.zw;
                    float cL = dot(TextureColor.SampleLevel(Sampler0, currSample, 0).rgb,
                                   float3(0.299, 0.587, 0.114));
                    float pL = dot(TextureOriginal.SampleLevel(Sampler0, prevSample, 0).rgb,
                                   float3(0.299, 0.587, 0.114));
                    sad += abs(cL - pL);
                }
            }

            if (sad < bestSAD) {
                bestSAD = sad;
                bestOffset = float2(sx, sy) * ScreenSize.zw;
            }
        }
    }

    // Combine: camera motion + optical flow refinement
    return cameraMotion + bestOffset;
}
```

### SkyrimBridge Combat-Reactive Motion Blur
```hlsl
// Killcam: extreme motion blur for cinematic effect
float killcamBoost = SB_Player_Combat.z * 3.0;
// Sprint blur: subtle radial blur when sprinting
float sprintBlur = SB_Player_Movement.y * 0.3;
// Weapon swing: boost blur during combat
float combatBlur = SB_Player_Combat.x * SB_Equip_Flags.x * 0.5;

mbIntensity *= (1.0 + killcamBoost + sprintBlur + combatBlur);
```

---

## 6. SB_MaterialPBR: Separable SSS Diffusion Profiles + Pre-Integrated Skin
<a name="6-sb_materialpbr"></a>

### Current State
- Basic 8-sample disc SSS with depth-aware weighting
- GGX specular for metals, sharp catchlights for eyes
- Wet surface darkening + gloss boost
- Single SSS color tint

### Gap Analysis

**Gap 1: Not using Jimenez's separable SSS profiles**
The current SSS uses a uniform blur with a single color tint. Real skin has different
diffusion distances per RGB channel — red scatters the farthest (blood), green medium
(tissue), blue least (epidermis). Jimenez 2015 reduces a sum-of-6-Gaussians to a single
separable kernel needing only 2 passes (H+V) with 7 samples each.

**Gap 2: No per-channel diffusion variance**
The diffusion profile should have different widths per color channel. This creates the
characteristic warm glow at shadow boundaries on skin.

**Gap 3: No transmission/translucency for ears and thin features**
Backlit ears glow red because light transmits through thin skin. This can be approximated
using depth difference between front and back faces (shadow map trick) or just
depth-gradient heuristic.

### Improvement: Separable SSS with Material-Classified Diffusion

```hlsl
// ═══ Jimenez Separable SSS ═══
// Skin diffusion profile: sum of Gaussians fitted to measured data
// Reduced to 3 Gaussians (the 4th has negligible variance → just add center pixel)

struct DiffusionProfile {
    float3 weight;    // RGB weight
    float  variance;  // spatial extent (mm, converted to pixels)
};

static const DiffusionProfile SkinProfile[3] = {
    { float3(0.233, 0.455, 0.649), 0.0064 },  // narrow (epidermis)
    { float3(0.100, 0.336, 0.344), 0.0484 },  // medium (dermis)
    { float3(0.118, 0.198, 0.0  ), 0.187  }   // wide (deep blood scatter)
};

// Scale factor: converts real-world mm to screen pixels
float SSSScaleToPixels(float worldRadius, float linearDepth) {
    float fovFactor = SB_Camera_Info.x; // FOV in degrees
    return worldRadius * ScreenSize.x / (linearDepth * tan(fovFactor * 0.5 * 0.01745));
}

// Separable 1D SSS blur (run twice: horizontal then vertical)
float3 SeparableSSS(Texture2D src, float2 uv, float2 direction, float2 px) {
    float depth = TextureDepth.SampleLevel(Sampler0, uv, 0).r;
    float linearZ = SB_LinearizeDepth(depth);
    float3 center = src.SampleLevel(Sampler0, uv, 0).rgb;

    // Material check: only blur skin pixels
    int mat = SB_ReadMaterialID(SB_GBuffer_Material, int2(px));
    if (mat != SB_MAT_SKIN && mat != SB_MAT_HAIR) return center;

    float3 result = center * float3(0.549, 0.011, 0.007); // center weight from profile

    for (int g = 0; g < 3; g++) {
        float pixelRadius = SSSScaleToPixels(
            sqrt(SkinProfile[g].variance) * pbrSSSRadius, linearZ);

        // 7 samples per Gaussian (Jimenez: 7 is enough with importance sampling)
        static const float offsets[3] = { 1.0, 2.5, 4.5 };
        static const float weights[3] = { 0.44, 0.35, 0.21 }; // importance-sampled

        for (int s = 0; s < 3; s++) {
            float2 off = direction * offsets[s] * pixelRadius * ScreenSize.zw;

            float2 uvP = uv + off;
            float2 uvN = uv - off;

            // Depth-aware edge stopping
            float dP = TextureDepth.SampleLevel(Sampler0, uvP, 0).r;
            float dN = TextureDepth.SampleLevel(Sampler0, uvN, 0).r;
            float wP = exp(-abs(SB_LinearizeDepth(dP) - linearZ) * 200.0);
            float wN = exp(-abs(SB_LinearizeDepth(dN) - linearZ) * 200.0);

            result += src.SampleLevel(Sampler0, uvP, 0).rgb
                      * SkinProfile[g].weight * weights[s] * wP;
            result += src.SampleLevel(Sampler0, uvN, 0).rgb
                      * SkinProfile[g].weight * weights[s] * wN;
        }
    }

    return result;
}

// ═══ Transmission for thin features (ears, nostrils) ═══
float3 TransmissionApprox(float2 uv, float3 normal, float3 lightDir) {
    // Heuristic: thin areas have steep depth gradients
    float depth = TextureDepth.SampleLevel(Sampler0, uv, 0).r;
    float depthL = TextureDepth.SampleLevel(Sampler0, uv + float2(-2,0)*ScreenSize.zw, 0).r;
    float depthR = TextureDepth.SampleLevel(Sampler0, uv + float2( 2,0)*ScreenSize.zw, 0).r;
    float gradient = abs(SB_LinearizeDepth(depthL) - SB_LinearizeDepth(depthR));

    // Thinner areas transmit more light
    float thinness = saturate(1.0 - gradient * 50.0);
    float backLit  = saturate(dot(-normal, lightDir)); // back-facing to light

    float3 transmitColor = float3(1.0, 0.3, 0.1); // blood red transmission
    return transmitColor * thinness * backLit * pbrSSSIntensity * 0.5;
}
```

---

## 7. SB_ContactShadows: Penumbra Softening + Multi-Light Tracing
<a name="7-sb_contactshadows"></a>

### Current State
- Single ray toward sun direction
- Material-aware skin softening
- Hard shadow with distance-based fade

### Gap Analysis

**Gap 1: No penumbra (soft shadow edges)**
Real shadows have softer edges the farther the occluder is from the receiver.
Current implementation produces hard on/off shadows.

**Gap 2: Only traces toward sun — misses nearby point lights**
SkyrimBridge provides 3 nearest point light positions. Tracing toward those gives
contact shadows from torches, campfires, and magelight.

### Improvement: Penumbra + Multi-Light Contact Shadows

```hlsl
// ═══ Penumbra approximation from occlusion distance ═══
float ContactShadowWithPenumbra(float2 uv, float2 px, float3 lightDir) {
    // ... standard ray march setup ...
    float closestHitDist = csMaxDist;
    float shadow = 1.0;

    for (int i = 1; i <= csMaxSteps; i++) {
        rayPos += rayStep;
        // ... standard hit test ...

        if (hit) {
            closestHitDist = float(i) / float(csMaxSteps) * csMaxDist;
            // Penumbra: shadow is softer when occluder is far from receiver
            float penumbra = saturate(closestHitDist / csPenumbraScale);
            shadow = min(shadow, lerp(1.0 - csIntensity, 1.0, penumbra));
            break; // don't break — continue searching for closer occluders
        }
    }
    return shadow;
}

// ═══ Multi-light shadow tracing ═══
float4 PS_MultiLightShadows(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 color = TextureColor.Sample(Sampler0, uv);
    float3 worldPos = SB_WorldPosFromDepth(uv, /*depth*/);

    // Sun shadow (existing)
    float sunShadow = ContactShadowWithPenumbra(uv, pos.xy,
        normalize(SB_Shadow_Direction.xyz));

    // Point light shadows (NEW — SkyrimBridge exclusive)
    float3 lightShadow = 1.0;
    float atten0 = SB_LightAttenuation(worldPos, SB_Light0_PosRad, SB_Light0_Color);
    if (atten0 > 0.01 && csTraceLights) {
        float3 toLightDir = normalize(SB_Light0_PosRad.xyz - worldPos);
        float ls = ContactShadowWithPenumbra(uv, pos.xy, toLightDir);
        lightShadow *= lerp(1.0, ls, atten0); // only shadow within light radius
    }
    // Repeat for Light1, Light2...

    color.rgb *= sunShadow;
    color.rgb *= lightShadow;
    return color;
}
```

---

## 8. SB_VolumetricFog: Temporal Reprojected Fog + Cloud Shadows
<a name="8-sb_volumetricfog"></a>

### Current State
- Henyey-Greenstein phase function
- Height-based density + weather-driven scattering
- Basic god rays via radial sampling toward sun

### Gap Analysis

**Gap 1: No temporal reprojection — fog flickers at low step counts**
At 32 steps, volumetric fog is noisy. Reprojecting the previous frame's fog result
and blending with the current frame smooths this dramatically (Frostbite 2014).
SkyrimBridge has PrevViewProj matrix — temporal reprojection is ready to use.

**Gap 2: No cloud shadow estimation**
l00ping pioneered cloud shadowing in NAT. With SkyrimBridge's weather data we can
modulate sunlight intensity based on cloud coverage + sun position to create ground shadows.

### Improvement: Temporal Fog + Cloud Shadow Modulation

```hlsl
// ═══ Temporal reprojection for fog ═══
// Store fog inscatter + transmittance in a render target that persists between frames
// ENB's TextureOriginal often contains the previous frame — use it

float4 PS_VolumetricFog_Temporal(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    // Current frame volumetric calculation (existing code with dithered offset)
    float dither = frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715)))
                   + SB_Render_Frame.x * 0.618033988); // golden ratio temporal offset

    float3 currentInscatter = 0;
    float  currentTransmittance = 1.0;
    // ... existing march loop with dither ...

    // Reproject previous frame's fog result
    float3 worldPos = SB_WorldPosFromDepth(uv, depth);
    float2 prevUV = uv - SB_MotionVector(worldPos, uv);

    float3 prevFog = 0;
    if (all(prevUV > 0) && all(prevUV < 1)) {
        prevFog = TextureOriginal.SampleLevel(Sampler0, prevUV, 0).rgb;
    }

    // Temporal blend: 90% history, 10% current — very stable
    float temporalBlend = 0.1;
    // Increase current frame weight during fast camera movement
    float camSpeed = length(SB_MotionVector(worldPos, uv) * ScreenSize.xy);
    temporalBlend = lerp(0.1, 0.5, saturate(camSpeed * 2.0));

    float3 fog = lerp(prevFog, currentInscatter, temporalBlend);

    // Apply
    float4 color = TextureColor.Sample(Sampler0, uv);
    color.rgb = color.rgb * currentTransmittance + fog;
    return color;
}

// ═══ Cloud shadow estimation ═══
float EstimateCloudShadow(float3 worldPos) {
    // Use weather flags + cloud coverage to darken sun-facing areas
    float cloudDensity = SB_Weather_Flags.y; // isCloudy [0,1]
    if (cloudDensity < 0.01) return 1.0;

    // Procedural cloud pattern based on world position + time
    float2 cloudUV = worldPos.xy * 0.0002 + SB_Wind.y * Timer.x * 0.01;
    // Simple hash-based cloud pattern (no texture needed)
    float cloud = frac(sin(dot(cloudUV, float2(12.9898, 78.233))) * 43758.5453);
    cloud = smoothstep(0.3, 0.7, cloud);

    return lerp(1.0, 1.0 - cloudDensity * 0.4, cloud);
}
```

---

## 9. NEW: SB_SpecularOcclusion — AO-to-SO Pipeline + Bent Normal
<a name="9-sb_specularocclusion"></a>

### Rationale
This effect doesn't exist in any ENB shader. Specular occlusion prevents reflections
from appearing in heavily occluded areas (corners, crevices). UE5 uses it extensively.
SkyrimBridge's material classification makes this uniquely effective — we can skip
specular occlusion on matte materials and apply it strongly to metals.

```hlsl
// ═══ Read AO from GTAO pass (requires cross-shader communication) ═══
// Option A: GTAO writes AO to alpha channel of its render target
// Option B: Dedicated AO texture bound to t15

float ApplySpecularOcclusion(float NdotV, float ao, float roughness, int materialID) {
    // Lagarde 2014 specular occlusion approximation
    float so = saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);

    // Material-aware: metals need strong SO, matte materials don't care
    if (materialID == SB_MAT_METAL)   return so;
    if (materialID == SB_MAT_WATER)   return lerp(1.0, so, 0.5);
    if (materialID == SB_MAT_SKIN)    return 1.0; // skin isn't specular enough to need this
    return lerp(1.0, so, 0.5); // default: subtle
}

// ═══ Screen-Space Bent Normal estimation ═══
// Byproduct of GTAO: average unoccluded direction per pixel
// Can be computed alongside AO with minimal extra cost
float3 EstimateBentNormal(float3 normal, float2 uv, float2 px) {
    // During GTAO, accumulate unoccluded sample directions
    // This is the "average visible direction" — useful for:
    //   1. More accurate ambient lighting lookup
    //   2. Specular occlusion cone intersection
    //   3. Better SSR starting direction

    float3 bentNormal = normal; // default: surface normal
    float3 accumDir = 0;
    float  accumW = 0;

    // ... integrated into GTAO loop ...
    // For each unoccluded sample:
    //   accumDir += normalize(samplePos - viewPos) * sampleWeight;
    //   accumW += sampleWeight;

    if (accumW > 0.001) {
        bentNormal = normalize(normal + normalize(accumDir / accumW) * 0.5);
    }
    return bentNormal;
}
```

---

## 10. NEW: SB_ScreenSpaceCaustics — Refraction Marching for Water
<a name="10-sb_screenspacecaustics"></a>

### Rationale
No ENB shader implements screen-space caustics. SkyrimBridge's material classification
can identify water pixels, and the GBuffer normal provides the water surface orientation.
Caustics dramatically improve underwater and near-water visuals.

```hlsl
// ═══ Screen-space water caustics ═══
// For each underwater/near-water pixel, trace a refraction ray through the water surface
// The convergence/divergence of refracted rays creates caustic patterns

float CausticPattern(float3 worldPos) {
    // Procedural caustics via displaced Voronoi
    float2 p = worldPos.xz * 0.05 + Timer.x * 0.5;
    float2 i = floor(p);
    float2 f = frac(p);

    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float2 neighbor = float2(x, y);
            float2 point = frac(sin(dot(i + neighbor, float2(127.1, 311.7))) * 43758.5453);
            point = 0.5 + 0.5 * sin(Timer.x * 0.8 + 6.2831 * point);
            float d = length(neighbor + point - f);
            minDist = min(minDist, d);
        }
    }

    return smoothstep(0.0, 0.4, minDist);
}

float4 PS_WaterCaustics(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 color = TextureColor.Sample(Sampler0, uv);
    float depth = TextureDepth.SampleLevel(Sampler0, uv, 0).r;
    if (depth >= 0.9999) return color;

    float3 worldPos = SB_WorldPosFromDepth(uv, depth);
    float waterLevel = SB_Player_Water.y; // water surface Z

    // Only apply caustics below water or on surfaces near water
    float belowWater = waterLevel - worldPos.z;
    if (belowWater < 0 || belowWater > 500) return color;

    // Caustic intensity falls off with depth below surface
    float depthFade = exp(-belowWater * 0.005);

    // Sun must be shining for caustics
    float sunIntensity = saturate(SB_Sun_Direction.w); // elevation
    if (sunIntensity < 0.01) return color;

    float caustic = CausticPattern(worldPos);
    float3 causticColor = SB_Sun_Color.rgb * caustic * depthFade * sunIntensity * 0.3;

    // Only on terrain/stone below water, not on other materials
    int mat = SB_ReadMaterialID(SB_GBuffer_Material, int2(pos.xy));
    if (mat == SB_MAT_TERRAIN || mat == SB_MAT_STONE) {
        color.rgb += causticColor;
    }

    return color;
}
```

---

## 11. NEW: SB_Launchpad — Temporal Data Collection Prepass
<a name="11-sb_launchpad"></a>

### Rationale
MartyMcModding's Launchpad is the foundational insight: a single prepass shader that
computes shared data (depth normals, optical flow, motion vectors) that ALL downstream
effects consume. This avoids redundant computation and enables temporal effects.

**ENB Constraint:** We can't create arbitrary render targets. BUT we can use:
1. Alpha channels of existing render targets (4th channel often unused)
2. ENB's RenderTargetRGBA64 / RenderTargetRGBA128 when available
3. TextureOriginal (previous frame data — always available)
4. Multi-technique within a single .fx file sharing statics

### Architecture

```hlsl
// ═══ SB_Launchpad.fx — SKSE-side temporal data ═══
// This runs in enbeffectprepass.fx as the FIRST technique

// SkyrimBridge already pushes PrevViewProj matrix — this is the key enabler
// The SKSE plugin stores the previous frame's ViewProj and pushes it each frame

// What we collect per-frame and store for downstream:
// 1. Per-pixel motion vectors (camera + optical flow refinement)
// 2. Previous frame depth (for disocclusion detection)
// 3. Temporal frame index for noise rotation

// ─── Technique 0: Compute motion vectors ───
float4 PS_MotionVectors(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float depth = TextureDepth.SampleLevel(Sampler0, uv, 0).r;
    if (depth >= 0.9999) return 0;

    float3 worldPos = SB_WorldPosFromDepth(uv, depth);
    float2 motion = SB_MotionVector(worldPos, uv);

    // Optical flow refinement (if previous frame available)
    // Compare luminance between predicted and actual previous frame
    float2 prevUV = uv - motion;
    if (all(prevUV > 0) && all(prevUV < 1)) {
        float currLuma = dot(TextureColor.SampleLevel(Sampler0, uv, 0).rgb,
                             float3(0.299, 0.587, 0.114));
        float prevLuma = dot(TextureOriginal.SampleLevel(Sampler0, prevUV, 0).rgb,
                             float3(0.299, 0.587, 0.114));
        float error = abs(currLuma - prevLuma);

        // If large error, the camera motion vector is wrong (per-object motion)
        // Do a small local search to refine
        if (error > 0.1) {
            float bestError = error;
            float2 bestOffset = 0;
            for (int sy = -1; sy <= 1; sy++) {
                for (int sx = -1; sx <= 1; sx++) {
                    if (sx == 0 && sy == 0) continue;
                    float2 testUV = prevUV + float2(sx, sy) * ScreenSize.zw * 2.0;
                    float testLuma = dot(TextureOriginal.SampleLevel(Sampler0, testUV, 0).rgb,
                                        float3(0.299, 0.587, 0.114));
                    float testErr = abs(currLuma - testLuma);
                    if (testErr < bestError) {
                        bestError = testErr;
                        bestOffset = float2(sx, sy) * ScreenSize.zw * 2.0;
                    }
                }
            }
            motion -= bestOffset;
        }
    }

    // Pack into render target:
    // .rg = motion vector (screen space)
    // .b = linear depth (for temporal depth comparison)
    // .a = disocclusion flag
    float linZ = SB_LinearizeDepth(depth) / SB_Camera_Info.z; // normalized
    return float4(motion * 100.0, linZ, 1.0); // scale for precision
}

// ─── Technique 1: Temporal noise seed ───
// Provides deterministic but temporally varying noise for all downstream effects
// Uses Hilbert curve + R2 sequence (from XeGTAO)
float4 PS_TemporalSeed(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    uint frameIdx = uint(SB_Render_Frame.x);
    float spatial = IGNoise(pos.xy);
    float temporal = frac(float(frameIdx) * 0.618033988); // golden ratio
    float combined = frac(spatial + temporal);

    // Second noise channel: Roberts R2 sequence
    float r2_x = frac(0.7548776662 * float(frameIdx));
    float r2_y = frac(0.5698402909 * float(frameIdx));

    return float4(combined, frac(spatial + r2_x), frac(spatial + r2_y), 1.0);
}
```

### SKSE Plugin Temporal Data Additions
```cpp
// In the SkyrimBridge SKSE plugin, add to the per-frame update:

// Store previous frame's ViewProjection matrix
static XMFLOAT4X4 s_prevViewProj;

void OnFrameBegin() {
    // Push previous VP (already done — SB_PrevVP_Row0..3)
    // NEW: Push frame timing data
    float deltaTime = GetFrameDeltaTime();
    float frameCount = GetFrameCount();

    ENBSetParameter("SB_Render_Frame", frameCount, deltaTime,
                    GetScreenWidth(), GetScreenHeight());

    // NEW: Push TAA jitter offset
    // If game has TAA enabled, read its jitter. If not, provide our own.
    float2 jitter = GetTAAJitter(); // from game's camera projection
    ENBSetParameter("SB_Render_Jitter", jitter.x, jitter.y,
                    frameCount % 16, 0);

    // Store current VP for next frame
    s_prevViewProj = GetCurrentViewProjection();
}
```

---

## 12. SkyrimBridge.fxh: Temporal Buffer API Extensions
<a name="12-skyrimbridge-temporal-api"></a>

### New Helper Functions to Add

```hlsl
//=============================================================================
//  TEMPORAL REPROJECTION HELPERS
//=============================================================================

// Get motion vector at a pixel (from Launchpad or SkyrimBridge data)
float2 SB_GetMotionVector(float2 uv, float rawDepth) {
    float3 worldPos = SB_WorldPosFromDepth(uv, rawDepth);
    return SB_MotionVector(worldPos, uv);
}

// Check if a reprojected pixel is valid (not disoccluded)
bool SB_IsReprojectionValid(float2 prevUV, float currentLinearDepth) {
    if (any(prevUV < 0.001) || any(prevUV > 0.999)) return false;
    // Would need previous depth — store in Launchpad's .b channel
    return true;
}

// Temporal noise that rotates each frame (for dithering, AO, etc.)
float SB_TemporalNoise(float2 px) {
    float spatial = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
    float temporal = frac(float(uint(SB_Render_Frame.x) % 64u) * 0.618033988);
    return frac(spatial + temporal);
}

float2 SB_TemporalNoise2D(float2 px) {
    float base = SB_TemporalNoise(px);
    return float2(base, frac(base * 1.618033988 + 0.5));
}

// Temporal accumulation helper: blend with exponential moving average
float3 SB_TemporalBlend(float3 current, float3 history, float alpha,
                         float2 motion, float2 screenSize) {
    // Increase current weight when motion is fast
    float velocity = length(motion * screenSize);
    float adaptiveAlpha = lerp(alpha, 1.0, saturate(velocity * 0.1));
    return lerp(history, current, adaptiveAlpha);
}

//=============================================================================
//  INTER-PASS DATA ENCODING
//=============================================================================

// Pack AO + bent normal into RGBA8
float4 SB_PackAOBentNormal(float ao, float3 bentNormal) {
    return float4(bentNormal * 0.5 + 0.5, ao);
}

void SB_UnpackAOBentNormal(float4 packed, out float ao, out float3 bentNormal) {
    bentNormal = packed.rgb * 2.0 - 1.0;
    ao = packed.a;
}

// Pack 2D motion vector into RG16F (or RG8 with reduced precision)
float2 SB_PackMotionVector(float2 mv) {
    return mv * 100.0 + 0.5; // bias for unsigned storage
}

float2 SB_UnpackMotionVector(float2 packed) {
    return (packed - 0.5) / 100.0;
}

//=============================================================================
//  VOLUMETRIC / ATMOSPHERIC HELPERS
//=============================================================================

// Cloud shadow factor based on weather state
float SB_CloudShadow(float3 worldPos) {
    float cloudy = SB_Weather_Flags.y;
    if (cloudy < 0.01) return 1.0;

    float2 uv = worldPos.xz * 0.0003 + float2(SB_Wind.y, 0) * Timer.x * 0.005;
    float noise = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
    return lerp(1.0, smoothstep(0.3, 0.7, noise), cloudy * 0.35);
}

// Per-weather tonemapping curve interpolation
// Uses SB_Weather_Transition.x for smooth blending between weather states
float3 SB_WeatherLerpTonemapping(float3 color, float3 curveA, float3 curveB) {
    float t = SB_Weather_Transition.x;
    return lerp(
        color * curveA,
        color * curveB,
        t
    );
}
```

---

## 13. Cross-Shader Data Flow Architecture
<a name="13-cross-shader-data-flow"></a>

### Render Order + Data Dependencies

```
Frame N:
┌─────────────────────────────────────────────────────────────┐
│ enbeffectprepass.fx (runs first)                            │
│   Technique 0: SB_Launchpad — motion vectors + temporal seed│
│   Technique 1: SB_GTAO — AO + bent normal (writes to alpha)│
│   Technique 2: SB_ContactShadows — sun + point light shadows│
│                                                             │
│   Output: color.rgb = scene with AO + shadows               │
│           color.a   = AO value (for specular occlusion)     │
│           RenderTarget = packed AO + bent normal             │
├─────────────────────────────────────────────────────────────┤
│ enbeffect.fx (main pass)                                    │
│   Technique 0: SB_SSR — reads AO alpha for specular occ    │
│   Technique 1: SB_MaterialPBR — separable SSS, specular    │
│   Technique 2: SB_VolumetricFog — temporal reprojected      │
│   Technique 3: SB_ScreenSpaceCaustics — water caustics      │
│                                                             │
│   Output: fully lit, material-enhanced scene                 │
├─────────────────────────────────────────────────────────────┤
│ enbbloom.fx                                                  │
│   SB_HDRBloom — multi-kernel pseudo-convolution              │
├─────────────────────────────────────────────────────────────┤
│ enbeffectpostpass.fx (runs last)                             │
│   Technique 0: SB_MotionBlur — optical flow enhanced         │
│   Technique 1: SB_TemporalAA — Mitchell resolve + Catmull-Rom│
│                                                              │
│   Output: final anti-aliased frame → display                 │
│           Also stored as TextureOriginal for Frame N+1       │
└──────────────────────────────────────────────────────────────┘
```

### Temporal Data Persistence Strategy

ENB provides limited temporal persistence. Here's what survives between frames:

| Data                  | Storage Location              | Persistence |
|----------------------|-------------------------------|-------------|
| Previous frame color  | `TextureOriginal` (t2)       | ✅ Always    |
| Previous ViewProj     | `SB_PrevVP_Row0..3`          | ✅ Pushed by SKSE |
| Frame count           | `SB_Render_Frame.x`          | ✅ Pushed by SKSE |
| Delta time            | `SB_Render_Frame.y`          | ✅ Pushed by SKSE |
| TAA jitter            | `SB_Render_Jitter.xy`        | ✅ Pushed by SKSE |
| AO from prev frame    | Encoded in TextureOriginal.a | ⚠️ If not overwritten |
| Motion vectors        | Must recompute each frame    | ❌ Not persistent |
| Accumulated fog       | TextureOriginal              | ⚠️ Mixed with scene |

**Key Insight:** The biggest temporal data win comes from `TextureOriginal` + `SB_PrevVP`.
These two together enable:
- TAA with proper reprojection ✅
- Temporal fog accumulation ✅
- Temporal AO accumulation (if AO is embedded in output) ✅
- Motion vector computation (camera-derived) ✅
- Optical flow estimation (luminance comparison) ✅

**What we can't do without additional render targets:**
- ❌ Store separate AO history texture
- ❌ Store per-pixel velocity field independently
- ❌ Multi-frame specular accumulation

**Workaround strategies:**
1. **Alpha channel smuggling:** Encode AO/metadata in alpha when the main pass doesn't need it
2. **Temporal amortization:** Compute expensive effects over multiple frames (GTAO rotates
   slices per frame instead of doing all slices every frame)
3. **Recomputation:** Motion vectors are cheap enough to recompute rather than store
4. **SKSE-side accumulation:** The plugin can maintain temporal buffers in system memory
   and push smoothed/accumulated values as parameters

---

## Implementation Priority Matrix

| Improvement | Visual Impact | Performance Cost | Complexity | Priority |
|---|---|---|---|---|
| XeGTAO horizon falloff fix | ⭐⭐⭐ | 0 (free) | ⭐ | **P0** |
| Multi-bounce AO | ⭐⭐⭐ | 0 (free) | ⭐ | **P0** |
| Separable SSS profiles | ⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐ | **P1** |
| GTAO → GT-VBAO bitmask | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ | **P1** |
| Stochastic SSR | ⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐ | **P1** |
| TAA Mitchell + Catmull-Rom | ⭐⭐⭐⭐ | ⭐ | ⭐⭐ | **P1** |
| Temporal volumetric fog | ⭐⭐⭐⭐ | ⭐ | ⭐⭐ | **P1** |
| Multi-kernel bloom | ⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐ | **P2** |
| Specular occlusion | ⭐⭐⭐ | ⭐ | ⭐⭐ | **P2** |
| Optical flow refinement | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ | **P2** |
| Contact shadow penumbra | ⭐⭐ | ⭐ | ⭐⭐ | **P2** |
| Multi-light shadows | ⭐⭐⭐ | ⭐⭐ | ⭐⭐ | **P2** |
| Screen-space caustics | ⭐⭐⭐ | ⭐⭐ | ⭐⭐ | **P3** |
| Skin transmission | ⭐⭐ | ⭐ | ⭐⭐ | **P3** |
| Cloud shadows | ⭐⭐ | ⭐ | ⭐ | **P3** |
| SB_Launchpad prepass | ⭐⭐⭐⭐⭐ | ⭐ | ⭐⭐⭐ | **P1** |
| Temporal noise system | ⭐⭐⭐ | 0 (free) | ⭐ | **P0** |

### Recommended Session Order

**Session 1: Free Wins (P0)**
- XeGTAO horizon falloff fix in SB_GTAO
- Multi-bounce AO function
- Temporal noise system in SkyrimBridge.fxh
- SB_Render_Jitter integration

**Session 2: Temporal Foundation (P1 infrastructure)**
- SB_Launchpad prepass
- Motion vector computation
- TextureOriginal temporal reprojection pattern

**Session 3: GTAO → GT-VBAO (P1 highest visual impact)**
- Full visibility bitmask implementation
- Material-aware thickness
- Cosine-weighted integration
- Bent normal extraction

**Session 4: TAA Overhaul (P1)**
- Mitchell resolve filter
- Catmull-Rom history sampling
- Luminance-weighted blend
- Optical flow integration

**Session 5: Material PBR Revolution (P1)**
- Separable SSS diffusion profiles
- Per-channel variance
- Skin transmission approximation
- Specular occlusion pipeline

**Session 6: SSR + Bloom Polish (P2)**
- Stochastic GGX importance sampling
- Specular occlusion from AO
- Multi-kernel directional bloom
- Star/streak patterns

**Session 7: Atmosphere + New Effects (P2-P3)**
- Temporal volumetric fog
- Cloud shadow modulation
- Water caustics
- Multi-light contact shadows
- Combat-reactive everything

---

*This document represents the complete gap analysis between SkyrimBridge's current
shader suite and the combined state-of-art across all researched authors and production
engines. Every improvement listed is feasible within ENB's pixel shader constraints,
and most leverage SkyrimBridge data that no other shader system can access.*
