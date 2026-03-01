# SkyrimBridge — Complete Technique Integration Guide
## From Five Shader Pioneers to Production Implementation

**Authors Researched**: prod80 (TreyM) · Pascal Gilcher (MartyMcModding) · Boris Vorontsov · Kitsuune · Frans Bouma (Otis_Inf)

**Target Shaders**: SB_GTAO · SB_SSR · SB_HDRBloom · SB_VolumetricFog · SB_ContactShadows · SB_MotionBlur · SB_TemporalAA · SB_MaterialPBR · enbeffect.fx · enbeffectpostpass.fx · SkyrimBridge.fxh

---

# Table of Contents

1. [SB_GTAO.fx — Ambient Occlusion Upgrades](#1-sb_gtaofx--ambient-occlusion-upgrades)
2. [SB_SSR.fx — Screen-Space Reflection Overhaul](#2-sb_ssrfx--screen-space-reflection-overhaul)
3. [SB_HDRBloom.fx — Bloom Pipeline Reconstruction](#3-sb_hdrbloomfx--bloom-pipeline-reconstruction)
4. [SB_VolumetricFog.fx — Atmospheric Rendering Upgrades](#4-sb_volumetricfogfx--atmospheric-rendering-upgrades)
5. [SB_ContactShadows.fx — Shadow Quality Improvements](#5-sb_contactshadowsfx--shadow-quality-improvements)
6. [SB_MotionBlur.fx — Optical Flow Motion Blur](#6-sb_motionblurfx--optical-flow-motion-blur)
7. [SB_TemporalAA.fx — TAA Reconstruction Overhaul](#7-sb_temporalaafx--taa-reconstruction-overhaul)
8. [SB_MaterialPBR.fx — Material Enhancement Upgrades](#8-sb_materialpbrfx--material-enhancement-upgrades)
9. [enbeffect.fx / enbeffectpostpass.fx — Color Science Pipeline](#9-enbeffectfx--enbeffectpostpassfx--color-science-pipeline)
10. [SkyrimBridge.fxh — API Extensions](#10-skyrimbridgefxh--api-extensions)
11. [New Shader: SB_Launchpad.fx — Shared Temporal Prepass](#11-new-shader-sb_launchpadfx--shared-temporal-prepass)
12. [Implementation Roadmap](#12-implementation-roadmap)

---

# 1. SB_GTAO.fx — Ambient Occlusion Upgrades

## Current State

Single-pass GTAO with `aoSlices` (1–8) directional slices and `aoSteps` (2–12) steps per slice. Uses horizon-angle cosine integration with distance falloff. Material-aware intensity multipliers for skin, metal, foliage. No temporal accumulation, no visibility bitmask, no deinterleaved rendering.

---

## 1.1 Visibility Bitmask Integration

**Source**: Pascal Gilcher's MXAO — corrects the Therrien/Levesque/Gilet 2023 paper by reintroducing the cosine term in the radiometric integral.

**What It Does**: Instead of tracking only the maximum horizon angle per side, a bitmask tracks which angular sectors of the hemisphere are occluded. This captures thin occluders and overlapping geometry that max-angle tracking misses entirely.

**Why It Matters**: The current `hcos[side] = maxH` in `ComputeGTAO()` only tracks the single highest horizon angle per side. A thin fence post at 30° occludes that sector, but if there's also a wall at 60°, the wall is ignored because 60° > 30° and the fence-post's contribution is lost. Bitmask tracking captures both.

### Step-by-Step Implementation

**Step 1 — Add Bitmask Quality Tier UI**

Insert after the existing `foliageMult` UI parameter (line ~28):

```hlsl
int aoMode < string UIName="AO Mode";  int UIMin=0; int UIMax=2; > = 1;
// 0 = Standard GTAO (current), 1 = Visibility Bitmask, 2 = Bitmask + Solid Angle
```

**Step 2 — Add Bitmask Helper Functions**

Insert before the `ComputeGTAO` function (before line 37):

```hlsl
// Convert horizon angle pair to bitmask sector
// Maps [h_front, h_back] → bits in a 32-bit mask representing angular sectors
// The MXAO correction: multiply by cos(θ) to weight sectors by their radiometric
// contribution (Gilcher's fix to Therrien et al. 2023)
uint HorizonToBitmask(float2 h_frontback) {
    // Map angle range [0, π] to [0, 31] bit positions
    uint startBit = uint(saturate(h_frontback.x / PI) * 31.0);
    uint endBit   = uint(saturate(h_frontback.y / PI) * 31.0);
    // Create mask with bits set from startBit to endBit
    uint mask = 0;
    for (uint b = startBit; b <= endBit; b++) {
        mask |= (1u << b);
    }
    return mask;
}

// Count set bits (occlusion sectors) with cosine weighting
// Each bit position i maps to angle θ = (i + 0.5) / 32 * π
// Weight each sector by cos(θ) for correct radiometric integration
float BitmaskToAO(uint bitmask) {
    float ao = 0.0;
    float totalWeight = 0.0;
    for (uint i = 0; i < 32; i++) {
        float theta = (float(i) + 0.5) / 32.0 * PI;
        float weight = cos(theta); // Gilcher's correction: cosine term
        totalWeight += weight;
        if (bitmask & (1u << i)) {
            ao += weight;
        }
    }
    return ao / max(totalWeight, 0.001);
}
```

**Step 3 — Modify the Inner Loop in `ComputeGTAO`**

Replace the current per-side tracking logic (lines 60–76) with a mode switch:

```hlsl
// Replace: float hcos[2] = { -1.0, -1.0 };
// With:
float hcos[2] = { -1.0, -1.0 };
uint  bitmask = 0;

for (int side = 0; side < 2; side++) {
    float2 sdir = dir * (side == 0 ? 1.0 : -1.0);
    float maxH = -1.0;

    for (int t = 1; t <= aoSteps; t++) {
        float2 off = sdir * (float(t) / float(aoSteps)) * radPx;
        float2 suv = uv + off * ScreenSize.zw;
        if (any(suv < 0) || any(suv > 1)) break;
        float3 sP = ViewPosFromDepth(suv, TextureDepth.SampleLevel(Sampler0, suv, 0).r);
        float3 h = sP - viewPos;
        float hd = length(h);
        float hc = dot(normalize(h), normal);
        float falloff = saturate(1.0 - hd * hd / (aoRadius * aoRadius * aoFalloff));
        hc = lerp(-1.0, hc, falloff);

        if (aoMode >= 1) {
            // Bitmask mode: accumulate all occluding sectors
            float h_angle = acos(clamp(hc, -1, 1));
            float h_prev  = (t == 1) ? PI : acos(clamp(maxH, -1, 1));
            if (hc > maxH) {
                // New horizon found: set bits from old max to new
                uint newBits = HorizonToBitmask(float2(h_angle, h_prev));
                bitmask |= (side == 0 ? newBits : (newBits << 0)); // Shift for side separation
            }
        }
        maxH = max(maxH, hc);
    }
    hcos[side] = maxH;
}
```

**Step 4 — Final AO Calculation with Mode Switch**

Replace lines 78–84 with:

```hlsl
float sliceAO;
if (aoMode == 0) {
    // Original GTAO: horizon cosine integration
    float h0 = acos(clamp(hcos[0], -1, 1));
    float h1 = acos(clamp(hcos[1], -1, 1));
    float cn = dot(normal.xy, dir);
    sliceAO = saturate(
        0.25 * (-cos(2*h0 - cn) + cn + 2*h0*sin(cn)) +
        0.25 * (-cos(2*h1 + cn) - cn + 2*h1*sin(cn))
    );
} else {
    // Bitmask: count cosine-weighted occluded sectors
    sliceAO = BitmaskToAO(bitmask);
}
ao += sliceAO;
```

**Step 5 — Add Temporal Blue Noise Jitter** (from Gilcher's 4096×64 pattern)

Replace the current `IGNoise` single-frame noise with a temporal cycle. In the slice loop, modify the noise offset:

```hlsl
// Replace: float noise = IGNoise(px);
// With:
uint frameIdx = uint(SB_Render_Frame.x) % 64; // 64-frame temporal cycle
float noise = IGNoise(px + float2(frameIdx * 5.0, frameIdx * 7.0)); // Temporal offset
```

This distributes the angular sampling across 64 frames. Combined with TAA's temporal accumulation in SB_TemporalAA.fx, this effectively multiplies the sample count by 64× at zero per-frame cost.

---

## 1.2 Deinterleaved/Tiled Rendering

**Source**: Gilcher's MXAO uses deinterleaved rendering similar to Intel ASSAO but with improved weighting.

**What It Does**: Instead of computing AO at full resolution, split the screen into a 4×4 grid of sub-images (each ¼ × ¼ resolution). Compute AO for each sub-image independently, then interleave the results back to full resolution. Each sub-pixel within a tile is offset by (tile_x, tile_y) / 4.

**Why It Matters**: Current SB_GTAO samples the depth buffer at full resolution for every pixel, hitting the texture cache hard with scattered random-offset reads. Deinterleaving ensures each sub-image's samples are spatially coherent, dramatically improving cache utilization.

### Step-by-Step Implementation

This requires a multi-pass architecture, which means the current single-pass `technique11` must be expanded.

**Step 1 — Declare 4×4 Render Targets**

ENB's Effects framework doesn't support creating arbitrary render targets, so this must use the available prepass render target pool. Within `enbeffectprepass.fx`, declare:

```hlsl
// Quarter-resolution AO tiles — using ENB's RenderTargetRGBA32 pool
// Each tile holds AO for pixels at (x%4 == tile_x, y%4 == tile_y)
Texture2D texAOTile0; // Tile (0,0)
Texture2D texAOTile1; // Tile (1,0)
// ... up to texAOTile15 for full 4×4
```

**Reality check**: ENB's prepass provides up to 8 render targets. A 4×4 deinterleave needs 16 sub-images. **Practical compromise**: Use 2×2 deinterleaving (4 sub-images, 4 passes) instead of 4×4. This still improves cache coherence substantially while fitting within ENB's render target budget.

**Step 2 — Deinterleave Pass** (runs 4 times, once per sub-pixel offset)

```hlsl
float4 PS_DeinterleaveAO(float4 pos : SV_Position, float2 uv : TEXCOORD0, 
                          uniform int2 tileOffset) : SV_Target {
    // Map quarter-res UV to full-res UV with sub-pixel offset
    float2 fullUV = uv * 0.5 + float2(tileOffset) * ScreenSize.zw;
    float2 fullPx = pos.xy * 2.0 + float2(tileOffset);
    
    float ao = ComputeGTAO(fullUV, fullPx);
    return float4(ao, 0, 0, 1);
}
```

**Step 3 — Interleave Pass** (single pass, reads all 4 tiles)

```hlsl
float4 PS_InterleaveAO(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    int2 px = int2(pos.xy);
    int2 tileOffset = px % 2; // Which sub-pixel
    float2 tileUV = floor(pos.xy / 2.0) * 2.0 * ScreenSize.zw;
    
    // Read from the appropriate tile
    float ao;
    if (tileOffset.x == 0 && tileOffset.y == 0) ao = texAOTile0.Sample(Sampler0, tileUV).r;
    else if (tileOffset.x == 1 && tileOffset.y == 0) ao = texAOTile1.Sample(Sampler0, tileUV).r;
    else if (tileOffset.x == 0 && tileOffset.y == 1) ao = texAOTile2.Sample(Sampler0, tileUV).r;
    else ao = texAOTile3.Sample(Sampler0, tileUV).r;
    
    // Bilateral edge-aware blur to smooth tile seams
    // (implemented in next sub-section)
    return float4(ao, 0, 0, 1);
}
```

**Step 4 — Bilateral Blur to Smooth Tile Boundaries**

After interleaving, a 3×3 bilateral blur with depth+normal edge-stopping smooths the visible grid pattern at tile boundaries:

```hlsl
float BilateralBlurAO(float2 uv, float2 px) {
    float centerAO = texInterleaved.Sample(Sampler0, uv).r;
    float centerZ = SB_LinearDepth(TextureDepth.SampleLevel(Sampler0, uv, 0).r,
                                    SB_Camera_Planes.x, SB_Camera_Planes.y);
    float3 centerN = SB_ReadNormal(SB_GBuffer_Normal, int2(px));
    
    float totalAO = centerAO;
    float totalW = 1.0;
    
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            float2 sUV = uv + float2(x, y) * ScreenSize.zw;
            float sAO = texInterleaved.Sample(Sampler0, sUV).r;
            float sZ = SB_LinearDepth(TextureDepth.SampleLevel(Sampler0, sUV, 0).r,
                                       SB_Camera_Planes.x, SB_Camera_Planes.y);
            float3 sN = SB_ReadNormal(SB_GBuffer_Normal, int2(px) + int2(x, y));
            
            float wZ = exp(-abs(centerZ - sZ) / (centerZ * 0.05 + 0.001));
            float wN = pow(max(dot(centerN, sN), 0), 32.0);
            float w = wZ * wN;
            
            totalAO += sAO * w;
            totalW += w;
        }
    }
    return totalAO / totalW;
}
```

---

## 1.3 Specular Occlusion from AO

**Source**: Identified in gap analysis as a missing technique across all surveyed authors. Based on Lagarde & de Rousiers 2014 (Frostbite engine).

**What It Does**: Derives a specular occlusion term from the diffuse AO value, accounting for roughness and view angle. Smooth surfaces at grazing angles lose more specular energy from occluders than rough surfaces.

### Step-by-Step Implementation

Add to `PS_GTAO`, after the AO computation and before the material multiplier application (after line 93):

```hlsl
// Specular Occlusion — Lagarde & de Rousiers 2014
// Approximation: SO = saturate(pow(NdotV + ao, roughness²) - 1 + ao)
// This feeds into SB_SSR and SB_MaterialPBR via the AO render target
float NdotV_ao = saturate(dot(SB_ReadNormal(SB_GBuffer_Normal, int2(pos.xy)),
                               normalize(-float3((uv * 2.0 - 1.0) * float2(1,-1), 1.0))));
SB_PBRSurface surf_ao = SB_GetSurface(SB_GBuffer_Material, SB_GBuffer_Normal, int2(pos.xy));
float roughSq = surf_ao.roughness * surf_ao.roughness;
float specOcc = saturate(pow(NdotV_ao + ao, exp2(-16.0 * roughSq - 1.0)) - 1.0 + ao);

// Store both diffuse AO (R) and specular occlusion (G) in output
// Requires changing the output to use a 2-channel render target
// If single-channel only: encode as color.rgb *= ao; and pass specOcc separately
color.rgb *= ao;
// TODO: Write specOcc to a secondary channel for SB_SSR consumption
```

**Integration with SB_SSR.fx**: In `PS_SSR`, read the specular occlusion from the GTAO output and multiply the reflection result:

```hlsl
// In SB_SSR.fx, after computing the reflection color (line ~118):
float specOcc = texGTAO_Output.Sample(Sampler0, uv).g; // Read specular occlusion channel
alpha *= specOcc; // Attenuate reflections by specular occlusion
```

---

# 2. SB_SSR.fx — Screen-Space Reflection Overhaul

## Current State

Linear view-space ray march with binary refinement. Schlick Fresnel with material-aware F0. No Hi-Z acceleration, no stochastic roughness sampling, no temporal accumulation, no GGX importance sampling.

---

## 2.1 Hierarchical Min-Max (Hi-Z) Tracing

**Source**: Gilcher's MXAO/RTGI uses Hi-Z min-max depth buffers for specular reflections, similar to AMD's Stochastic Screen-Space Reflections (SSSR).

**What It Does**: Build a mipmap chain of the depth buffer where each mip level stores the minimum depth in its footprint. Ray marching starts at a coarse mip level (large steps), then drops to finer levels when an intersection is approached. This converts O(N) linear marching to O(log N) hierarchical traversal.

**Why It Matters**: Current SB_SSR uses 64 fixed-size linear steps with 8 binary refinement steps. Hi-Z can achieve equivalent or better intersection quality with ~10–15 total steps, a 4–6× performance improvement.

### Step-by-Step Implementation

**Step 1 — Generate Hi-Z Depth Buffer**

This requires a mipmap chain of the depth buffer. In `enbeffectprepass.fx`, add passes to downsample depth:

```hlsl
// Generate Hi-Z mip chain
// Mip 0 = full resolution depth
// Mip k = min(mip k-1, 2x2 neighborhood)
Texture2D texHiZ_Mip0; // Full res (copy of depth)
Texture2D texHiZ_Mip1; // Half res
Texture2D texHiZ_Mip2; // Quarter res
Texture2D texHiZ_Mip3; // Eighth res
Texture2D texHiZ_Mip4; // Sixteenth res

float4 PS_HiZ_Downsample(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                           uniform Texture2D srcMip, uniform float2 srcSize) : SV_Target {
    float2 texel = 1.0 / srcSize;
    float d00 = srcMip.SampleLevel(Sampler0, uv + float2(-0.25, -0.25) * texel, 0).r;
    float d10 = srcMip.SampleLevel(Sampler0, uv + float2( 0.25, -0.25) * texel, 0).r;
    float d01 = srcMip.SampleLevel(Sampler0, uv + float2(-0.25,  0.25) * texel, 0).r;
    float d11 = srcMip.SampleLevel(Sampler0, uv + float2( 0.25,  0.25) * texel, 0).r;
    // For reversed-Z (Skyrim): use MAX for conservative test
    // For standard Z: use MIN
    return float4(min(min(d00, d10), min(d01, d11)), 0, 0, 1);
}
```

**Step 2 — Replace Linear March with Hi-Z Traversal**

Replace the ray march loop in `PS_SSR` (lines 71–101):

```hlsl
// Hi-Z ray march
float3 rayOrigin = viewPos + reflDir * 0.5; // Offset to avoid self-intersection
float3 rayDir = reflDir;

// Project ray endpoints to screen space
float2 startUV = ViewToUV(rayOrigin);
float startZ = rayOrigin.z;

// Determine ray step in screen space
float3 rayEnd = rayOrigin + rayDir * ssrMaxDist;
float2 endUV = ViewToUV(rayEnd);
float2 rayDirSS = endUV - startUV;

float2 hitUV = float2(0, 0);
bool hit = false;

int maxMipLevel = 4; // Number of Hi-Z mip levels
int mipLevel = 2;    // Start at a coarse level
float t = 0.0;
float stepScale = 1.0;

for (int i = 0; i < ssrMaxSteps; i++) {
    // Step size doubles with each mip level
    float mipStepSize = exp2(float(mipLevel));
    t += mipStepSize / float(ssrMaxSteps);
    
    float2 sampleUV = startUV + rayDirSS * t;
    if (any(sampleUV < 0) || any(sampleUV > 1)) break;
    
    // Sample Hi-Z at current mip level
    float hiZDepth;
    if (mipLevel == 0)      hiZDepth = texHiZ_Mip0.SampleLevel(Sampler0, sampleUV, 0).r;
    else if (mipLevel == 1) hiZDepth = texHiZ_Mip1.SampleLevel(Sampler0, sampleUV, 0).r;
    else if (mipLevel == 2) hiZDepth = texHiZ_Mip2.SampleLevel(Sampler0, sampleUV, 0).r;
    else if (mipLevel == 3) hiZDepth = texHiZ_Mip3.SampleLevel(Sampler0, sampleUV, 0).r;
    else                    hiZDepth = texHiZ_Mip4.SampleLevel(Sampler0, sampleUV, 0).r;
    
    float3 sampleViewPos = ViewPosFromDepth(sampleUV, hiZDepth);
    float3 rayAtT = rayOrigin + rayDir * (t * ssrMaxDist);
    float diff = rayAtT.z - sampleViewPos.z;
    
    if (diff > 0 && diff < ssrThickness * mipStepSize) {
        // Potential intersection — refine by dropping to finer mip
        if (mipLevel > 0) {
            mipLevel--;
            t -= mipStepSize / float(ssrMaxSteps); // Step back
        } else {
            // At mip 0: confirmed intersection
            hitUV = sampleUV;
            hit = true;
            break;
        }
    } else if (diff < 0) {
        // Ray hasn't reached the surface yet — can try coarser mip for speed
        mipLevel = min(mipLevel + 1, maxMipLevel);
    }
}
```

**Step 3 — Add GGX VNDF Importance Sampling for Rough Reflections**

**Source**: Gilcher's unified VNDF sampler combining Heitz 2018, Dupuy 2023, Tokuyoshi & Eto 2024.

For surfaces with roughness > 0, instead of a single mirror reflection ray, sample the GGX visible normal distribution to generate a stochastic reflection direction:

```hlsl
// GGX VNDF sampling (Heitz 2018 simplified form)
// Returns a half-vector sampled from the visible normal distribution
float3 SampleGGX_VNDF(float3 V, float roughness, float2 rand) {
    float alpha = roughness * roughness;
    
    // Stretch the view vector so it's in the hemisphere configuration
    float3 Vh = normalize(float3(alpha * V.x, alpha * V.y, V.z));
    
    // Orthonormal basis around Vh
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) / sqrt(lensq) : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);
    
    // Sample disc (Dupuy 2023 spherical cap method)
    float r = sqrt(rand.x);
    float phi = TWO_PI * rand.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
    
    // Reproject onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2)) * Vh;
    
    // Unstretch
    return normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));
}
```

Use in the SSR ray direction computation:

```hlsl
// In PS_SSR, replace the simple reflect:
float3 reflDir;
if (surf.roughness < 0.05) {
    // Mirror reflection for very smooth surfaces
    reflDir = reflect(viewDir, normal);
} else {
    // Stochastic GGX VNDF sampling for rough surfaces
    uint frameIdx = uint(SB_Render_Frame.x) % 64;
    float2 rand = float2(
        IGNoise(pos.xy + float2(frameIdx * 5.3, 0)),
        IGNoise(pos.xy + float2(0, frameIdx * 7.1))
    );
    float3 H = SampleGGX_VNDF(-viewDir, surf.roughness, rand);
    reflDir = reflect(viewDir, H);
}
```

This produces noisy single-sample reflections that are denoised by temporal accumulation in SB_TemporalAA.

---

## 2.2 BRDF Demodulation for Specular Denoising

**Source**: Gilcher's RTGI uses BRDF demodulation to separate material response from lighting before denoising.

**What It Does**: Before temporal accumulation, divide the noisy reflection by the expected BRDF response. After temporal filtering, multiply it back. This prevents the denoiser from smoothing out material-dependent reflection intensity variations.

### Step-by-Step Implementation

**Step 1 — Compute Expected BRDF Weight**

In `PS_SSR`, after computing the reflection color:

```hlsl
// Demodulate: remove BRDF weight before temporal accumulation
float3 brdfWeight = F_Schlick(NdotV, float3(f0, f0, f0)) * 
                    max(1.0 - surf.roughness, 0.04); // Simplified split-sum
float3 demodulated = reflColor / max(brdfWeight, 0.001);

// Store demodulated reflection + brdfWeight for remodulation after TAA
// Output: float4(demodulated, encode(brdfWeight))
```

**Step 2 — In SB_TemporalAA.fx, Temporal Filter the Demodulated Signal**

The TAA pass operates on the demodulated reflection texture normally — neighborhood clamping, motion rejection, blending all work on the lighting signal alone.

**Step 3 — Remodulate After TAA**

In a compositing pass after TAA:

```hlsl
float3 filteredRefl = texTAA_SSR.Sample(Sampler0, uv).rgb;
float3 brdfWeight = decode(texBRDFWeight.Sample(Sampler0, uv));
float3 finalRefl = filteredRefl * brdfWeight; // Reintroduce material response
```

---

# 3. SB_HDRBloom.fx — Bloom Pipeline Reconstruction

## Current State

Single-pass 2D Gaussian blur with material-aware threshold adjustment and bloom tint. Uses a nested loop (N² complexity) which is extremely expensive at high sample counts. No multi-kernel decomposition, no energy conservation, no depth awareness.

---

## 3.1 Separable Multi-Kernel Dual-Filter Bloom

**Source**: Gilcher's Solaris uses dual-blur optimized for GPU throughput with energy conservation. Bouma's Adaptive Fog uses bloom as a light scattering proxy.

**What It Does**: Replace the N² single-pass blur with a separable dual-filter downsample/upsample chain. Each level captures a different spatial frequency of bloom, then the levels are composited with energy-conserving weights.

### Step-by-Step Implementation

**Step 1 — Replace Single Pass with Multi-Pass Downsample Chain**

The current nested `for (i) for (j)` loop must become a ping-pong downsample/upsample chain. In ENB's pipeline, this requires using the bloom shader stage (`enbbloom.fx`) which has multiple passes available.

```hlsl
// Pass 1: Threshold + Downsample to 1/2 resolution
float4 PS_BloomThreshold(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 hdr = SB_HDR_Scene.SampleLevel(Sampler0, uv, 0).rgb;
    
    // Inverse Reinhard to reconstruct HDR headroom (Gilcher's Solaris technique)
    // This extracts highlight information lost to tonemapping
    float lum = dot(hdr, float3(0.2126, 0.7152, 0.0722));
    float3 invTonemap = hdr / max(1.0 - lum, 0.001); // Inverse Reinhard
    float lumHDR = dot(invTonemap, float3(0.2126, 0.7152, 0.0722));
    
    // Soft knee threshold (same as current, but on inverse-tonemapped signal)
    float knee = 0.5;
    float soft = lumHDR - bloomThreshold + knee;
    soft = clamp(soft, 0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-6);
    float contribution = max(soft, lumHDR - bloomThreshold) / max(lumHDR, 1e-6);
    
    return float4(invTonemap * max(contribution, 0), 1);
}

// Pass 2-5: Progressive downsample (each halves resolution)
// Uses 13-tap tent filter for high-quality downsampling (Jimenez 2014)
float4 PS_BloomDown(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                     uniform Texture2D src, uniform float2 texelSize) : SV_Target {
    // 13-tap tent filter: center (4x weight) + 4 diamond + 4 corner + 4 adjacent
    float3 result = 0;
    result += src.SampleLevel(Sampler0, uv, 0).rgb * 4.0;
    result += src.SampleLevel(Sampler0, uv + float2(-1, -1) * texelSize, 0).rgb;
    result += src.SampleLevel(Sampler0, uv + float2( 1, -1) * texelSize, 0).rgb;
    result += src.SampleLevel(Sampler0, uv + float2(-1,  1) * texelSize, 0).rgb;
    result += src.SampleLevel(Sampler0, uv + float2( 1,  1) * texelSize, 0).rgb;
    result += src.SampleLevel(Sampler0, uv + float2(-2,  0) * texelSize, 0).rgb * 2.0;
    result += src.SampleLevel(Sampler0, uv + float2( 2,  0) * texelSize, 0).rgb * 2.0;
    result += src.SampleLevel(Sampler0, uv + float2( 0, -2) * texelSize, 0).rgb * 2.0;
    result += src.SampleLevel(Sampler0, uv + float2( 0,  2) * texelSize, 0).rgb * 2.0;
    result /= 20.0;
    return float4(result, 1);
}

// Pass 6-9: Progressive upsample with energy-conserving additive blend
float4 PS_BloomUp(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                    uniform Texture2D srcCoarse, uniform Texture2D srcFine,
                    uniform float2 coarseTexel, uniform float blendWeight) : SV_Target {
    // 3×3 tent filter on coarser mip
    float3 coarse = 0;
    coarse += srcCoarse.SampleLevel(Sampler0, uv + float2(-1,-1) * coarseTexel, 0).rgb;
    coarse += srcCoarse.SampleLevel(Sampler0, uv + float2( 0,-1) * coarseTexel, 0).rgb * 2.0;
    coarse += srcCoarse.SampleLevel(Sampler0, uv + float2( 1,-1) * coarseTexel, 0).rgb;
    coarse += srcCoarse.SampleLevel(Sampler0, uv + float2(-1, 0) * coarseTexel, 0).rgb * 2.0;
    coarse += srcCoarse.SampleLevel(Sampler0, uv, 0).rgb * 4.0;
    coarse += srcCoarse.SampleLevel(Sampler0, uv + float2( 1, 0) * coarseTexel, 0).rgb * 2.0;
    coarse += srcCoarse.SampleLevel(Sampler0, uv + float2(-1, 1) * coarseTexel, 0).rgb;
    coarse += srcCoarse.SampleLevel(Sampler0, uv + float2( 0, 1) * coarseTexel, 0).rgb * 2.0;
    coarse += srcCoarse.SampleLevel(Sampler0, uv + float2( 1, 1) * coarseTexel, 0).rgb;
    coarse /= 16.0;
    
    float3 fine = srcFine.SampleLevel(Sampler0, uv, 0).rgb;
    return float4(fine + coarse * blendWeight, 1);
}
```

**Step 2 — Depth-Aware Bloom (prod80 Technique)**

**Source**: prod80's depth-aware bloom allows background to bloom into foreground but not vice versa, preventing character halos.

After computing the bloom at each mip level, apply a depth mask during the upsample:

```hlsl
// In PS_BloomUp, add depth-aware masking:
float centerDepth = TextureDepth.SampleLevel(Sampler0, uv, 0).r;
float coarseDepth = texDepthMip.SampleLevel(Sampler0, uv, 0).r; // Depth at coarse level

// Allow bloom from behind (far) to bleed into near, but not near into far
float depthMask = saturate((coarseDepth - centerDepth) * 1000.0 + 0.5);
// depthMask = 1 when coarse sample is behind center (background → foreground: allowed)
// depthMask = 0.5 for same depth, ~0 when coarse is in front (foreground → background: blocked)

coarse *= depthMask;
```

**Step 3 — Bloom-Fog Dual Blend (Bouma's Adaptive Fog)**

Add a final compositing option where bloom feeds into the volumetric fog:

```hlsl
// In SB_VolumetricFog.fx compositing, after computing totalInscatter:
// Read the bloom texture generated by SB_HDRBloom
float3 bloom = texBloomResult.SampleLevel(Sampler0, uv, 0).rgb;
float fogFactor = 1.0 - totalTransmittance; // How much fog is present

// Bouma's dual blend: fog regions get bloom-based light scatter
float3 fogScatter = lerp(bloom * 0.3, SB_Vol_Color.rgb, saturate(fogFactor));
totalInscatter += fogScatter * fogFactor * vfDensity * 0.2;
```

---

## 3.2 Forward Reinhard De/Re-Tonemapping for Highlights

**Source**: Bouma's CinematicDOF v1.2.1 `AccentuateWhites()` function, credited to MartyMcFly.

**What It Does**: Before threshold extraction, apply inverse Reinhard to reconstruct approximate HDR values from the LDR post-tonemap buffer. After bloom blurring, apply forward Reinhard to compress back to displayable range. This prevents the common issue where bloom highlights look "washed out" because the tonemapper has already compressed them.

### Step-by-Step Implementation

Add utility functions to `enbHelper_Common.fxh`:

```hlsl
// Reinhard inverse/forward tonemapping pair
// Used for highlight processing in LDR pipelines
float3 InverseReinhard(float3 color) {
    return color / max(1.0 - dot(color, float3(0.2126, 0.7152, 0.0722)), 0.001);
}

float3 ForwardReinhard(float3 color) {
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    return color / (1.0 + lum);
}

// Extended version with configurable whitepoint (Gilcher's Solaris approach)
// logWhitepoint: value 10 means white = 2^10 = 1024× brighter than midtone
float3 InverseReinhardExt(float3 color, float logWhitepoint) {
    float wp = exp2(logWhitepoint);
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    float scale = (lum * (1.0 + lum / (wp * wp))) / max(1.0 - lum, 0.001);
    return color * (scale / max(lum, 0.001));
}
```

Apply in `PS_BloomThreshold`:

```hlsl
// BEFORE threshold evaluation:
float3 hdr = InverseReinhard(TextureColor.Sample(Sampler0, uv).rgb);
// ... threshold and blur on the inverse-tonemapped signal ...

// AFTER final bloom compositing in PS_HDRBloom:
bloom = ForwardReinhard(bloom);
color.rgb += bloom * bloomIntensity * bloomTint;
```

---

# 4. SB_VolumetricFog.fx — Atmospheric Rendering Upgrades

## Current State

Single-pass ray march with Henyey-Greenstein phase, exponential height fog, dithered step offsets, Beer-Lambert transmittance integration. No temporal accumulation, no Y-gradient sky masking, no bloom-fog interaction, no nearby light scattering.

---

## 4.1 Temporal Reprojection for Fog Accumulation

**Source**: Cross-cutting technique from Gilcher (Launchpad optical flow), Boris (adaptation smoothing), and Kitsuune (histogram adaptation). Standard technique in production volumetrics (Frostbite 2014).

**What It Does**: Instead of computing 32 fog samples per pixel per frame, compute 8 samples per frame and accumulate with previous frame's result using motion-vector reprojection. Over 4 frames, you accumulate 32 samples at the cost of 8.

### Step-by-Step Implementation

**Step 1 — Add Previous-Frame Fog Texture**

```hlsl
Texture2D texPrevFog : register(t15); // Previous frame's fog result
// Must be written by the technique's render target and persisted between frames
// ENB provides this via render target ping-pong
```

**Step 2 — Reduce Per-Frame Sample Count and Add Temporal Offset**

```hlsl
// Modify the ray march loop:
int temporalSteps = max(vfSteps / 4, 8); // Quarter the per-frame cost
uint frameIdx = uint(SB_Render_Frame.x) % 4; // 4-frame cycle
float temporalOffset = float(frameIdx) / 4.0; // Phase offset per frame

for (int i = 0; i < temporalSteps; i++) {
    // Interleave sample positions across frames
    float t = (float(i) + dither + temporalOffset) / float(temporalSteps) * rayLength;
    // ... rest of the march ...
}
```

**Step 3 — Reproject and Blend with Previous Frame**

After the ray march, reproject using SkyrimBridge motion vectors:

```hlsl
// After computing totalInscatter and totalTransmittance:
float3 worldPos = SB_WorldPosFromDepth(uv, depth);
float2 motionVec = SB_MotionVector(worldPos, uv);
float2 prevUV = uv - motionVec;

float4 prevFog = float4(0, 0, 0, 1);
if (all(prevUV > 0) && all(prevUV < 1)) {
    prevFog = texPrevFog.SampleLevel(Sampler0, prevUV, 0);
}

// Temporal blend: 75% previous, 25% current (converges in ~4 frames)
float blendFactor = 0.25;

// Increase blend toward current frame during fast motion or weather transitions
float motionLen = length(motionVec * ScreenSize.xy);
blendFactor = lerp(blendFactor, 1.0, saturate(motionLen * 0.1));
blendFactor = lerp(blendFactor, 1.0, SB_Weather_Transition.x); // Reset on weather change

float3 finalInscatter = lerp(prevFog.rgb, totalInscatter, blendFactor);
float finalTransmit = lerp(prevFog.a, totalTransmittance, blendFactor);

color.rgb = color.rgb * finalTransmit + finalInscatter;

// Output fog state for next frame
// Store in render target: float4(totalInscatter, totalTransmittance)
```

---

## 4.2 Y-Gradient Sky Masking (Bouma's DepthHaze)

**Source**: Bouma's DepthHaze uses a screen-Y gradient to reduce fog at the top of the screen (sky) and near the camera (ground).

### Step-by-Step Implementation

Add to the fog compositing after the ray march:

```hlsl
// Screen-Y gradient: fog is strongest at the horizon line, weakest at sky and near ground
// This prevents fog from contaminating the sky dome and reduces ground-level artifacts
float yFactor = 1.0;
if (depth < 0.9999) { // Not sky
    // Reduce fog at very bottom of screen (near camera ground plane)
    float groundFade = saturate(uv.y * 4.0); // Fade in from bottom 25%
    yFactor *= groundFade;
}

totalInscatter *= yFactor;
totalTransmittance = lerp(1.0, totalTransmittance, yFactor);
```

---

## 4.3 Nearby Light Scattering in Fog

**Source**: SkyrimBridge's unique `SB_Light0/1/2_PosRad` and `SB_Light0/1/2_Color` uniforms enable something no other framework can do: real point-light contribution to fog volumes.

### Step-by-Step Implementation

Inside the ray march loop, at each sample position, add point light in-scattering:

```hlsl
// Inside the ray march loop, after computing base inscattered light:
float3 sampleWorldPos = SB_WorldPosFromDepth(
    uv, // approximate — would need proper world-space reconstruction per step
    depth * float(i) / float(vfSteps) // linear interpolation
);

// Accumulate contribution from nearby point lights
float3 pointScatter = SB_EvaluateNearbyLights(sampleWorldPos);
light += pointScatter * inscatterCoeff * 0.5; // Phase function for point lights ≈ isotropic
```

This makes torches, campfires, and magelight create visible light cones in foggy weather — a hallmark visual effect that ENB presets cannot achieve without game state data.

---

# 5. SB_ContactShadows.fx — Shadow Quality Improvements

## Current State

Linear ray march toward sun direction in view space with fixed thickness test and single-sample hit detection. No exponential step sizing, no penumbra estimation, no material-specific shadow behavior beyond skin softening.

---

## 5.1 Exponential Step Sizing with Penumbra Width

**Source**: Cross-cutting technique from Gilcher's RTGI ray intersection improvements (10× efficiency through adaptive step sizing).

### Step-by-Step Implementation

Replace the linear step loop (lines 50–68):

```hlsl
// Replace fixed-step march with exponential step sizing
float shadow = 1.0;
float3 rayPos = startPos;
float stepSize = csMaxDist / float(csMaxSteps);
float penumbraWidth = 0.0;
float minVisibility = 1.0;

for (int i = 1; i <= csMaxSteps; i++) {
    // Exponential step sizing: small steps near the surface, large steps far away
    float expT = float(i) / float(csMaxSteps);
    expT = expT * expT; // Quadratic distribution: more samples near origin
    
    float3 rayPos = startPos + lightDir * (expT * csMaxDist);
    float2 sUV = ViewToUV(rayPos);
    if (any(sUV < 0) || any(sUV > 1)) break;
    
    float sDepth = TextureDepth.SampleLevel(Sampler0, sUV, 0).r;
    float3 sView = ViewPosFromDepth(sUV, sDepth);
    
    float diff = rayPos.z - sView.z;
    if (diff > 0 && diff < csThickness) {
        // Penumbra estimation: closer occluders = sharper shadows
        float occluderDist = expT * csMaxDist;
        float penumbra = saturate(occluderDist / (csMaxDist * 0.5)); // 0 = sharp, 1 = soft
        
        // Soft shadow: track minimum visibility with distance-based softening
        float visibility = lerp(1.0 - csIntensity, 1.0, penumbra * 0.5);
        minVisibility = min(minVisibility, visibility);
    }
}

shadow = minVisibility;
```

---

## 5.2 Multi-Light Contact Shadows

**Source**: SkyrimBridge's nearby light data enables contact shadows from point lights, not just the sun.

### Step-by-Step Implementation

After the sun contact shadow, add a loop over the 3 nearest lights:

```hlsl
// After sun contact shadow computation:
// Point light contact shadows (short range only)
float pointShadow = 1.0;

for (int li = 0; li < 3; li++) {
    float4 lightPosRad;
    float4 lightCol;
    if (li == 0) { lightPosRad = SB_Light0_PosRad; lightCol = SB_Light0_Color; }
    else if (li == 1) { lightPosRad = SB_Light1_PosRad; lightCol = SB_Light1_Color; }
    else { lightPosRad = SB_Light2_PosRad; lightCol = SB_Light2_Color; }
    
    if (lightCol.a < 0.01) continue;
    
    // Light direction from pixel to light (view-space approximation)
    float3 pixelWorld = SB_WorldPosFromDepth(uv, depth);
    float3 toLightWorld = normalize(lightPosRad.xyz - pixelWorld);
    float distToLight = length(lightPosRad.xyz - pixelWorld);
    
    if (distToLight > lightPosRad.w) continue; // Outside light radius
    
    // Short-range march (8 steps, within light radius only)
    float marchDist = min(distToLight * 0.5, csMaxDist * 0.3);
    for (int j = 1; j <= 8; j++) {
        float t = float(j) / 8.0 * marchDist;
        // ... similar intersection test ...
    }
}
```

---

# 6. SB_MotionBlur.fx — Optical Flow Motion Blur

## Current State

Velocity-vector-driven directional blur with depth-weighted sampling. Per-object motion from SB_GBuffer_Motion. Bidirectional sampling with linear falloff weights.

---

## 6.1 Tile-Based Variable-Rate Motion Blur (McGuire et al. 2012)

**Source**: Standard production technique, improved by Gilcher's velocity buffer processing in Launchpad.

**What It Does**: Instead of evaluating every pixel at full sample count, divide the screen into tiles. Each tile stores the maximum velocity. Tiles with small velocity skip the blur entirely. Tiles with large velocity get full sample count. This dramatically reduces cost for scenes where most of the screen is stationary.

### Step-by-Step Implementation

**Step 1 — Velocity Tile Pass**

```hlsl
// Pass 1: Build velocity tiles (e.g., 20×20 pixel tiles)
float4 PS_VelocityTile(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float2 maxVel = float2(0, 0);
    float maxLen = 0;
    
    int2 tileStart = int2(pos.xy) * 20; // 20×20 tile
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 20; x++) {
            float2 vel = SB_ReadMotion(SB_GBuffer_Motion, tileStart + int2(x, y));
            float len = length(vel * ScreenSize.xy);
            if (len > maxLen) {
                maxLen = len;
                maxVel = vel;
            }
        }
    }
    return float4(maxVel, maxLen, 1);
}

// Pass 2: NeighborMax — expand tiles to account for fast objects bleeding into slow tiles
float4 PS_NeighborMax(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 maxVel = texVelocityTile.Sample(Sampler0, uv).xyz;
    
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float3 neighbor = texVelocityTile.Sample(Sampler0, 
                uv + float2(x, y) * (20.0 * ScreenSize.zw)).xyz;
            if (neighbor.z > maxVel.z) maxVel = neighbor;
        }
    }
    return float4(maxVel, 1);
}
```

**Step 2 — Variable-Rate Blur**

In `PS_MotionBlur`, check the tile velocity before committing to the full sample loop:

```hlsl
// Early-out for stationary tiles
float2 tileUV = floor(pos.xy / 20.0) * 20.0 * ScreenSize.zw;
float tileMaxVel = texNeighborMax.Sample(Sampler0, tileUV).z;

if (tileMaxVel < mbThreshold) return color; // Skip entire tile
```

---

# 7. SB_TemporalAA.fx — TAA Reconstruction Overhaul

## Current State

Standard variance-clip TAA with YCoCg color space, 3×3 neighborhood AABB, motion-based rejection, material-aware blend factors, and optional sharpening. Missing: catmull-rom history sampling, luminance-weighted clipping, anti-flicker weighting.

---

## 7.1 Catmull-Rom History Sampling (Mitchell TAA)

**Source**: Standard production TAA improvement used in UE4/UE5 temporal upsampling.

**What It Does**: Instead of bilinear sampling the history buffer, use a 4×4 Catmull-Rom filter. This prevents the history from degrading to bilinear sharpness over time, maintaining sub-pixel detail.

### Step-by-Step Implementation

Replace `TextureOriginal.SampleLevel(Sampler0, prevUV, 0).rgb` (line 74) with:

```hlsl
// Catmull-Rom 4×4 history sampling (sharper than bilinear, prevents detail loss)
float3 SampleHistoryCatmullRom(Texture2D tex, SamplerState samp, float2 uv, float2 texSize) {
    float2 pos = uv * texSize - 0.5;
    float2 f = frac(pos);
    float2 pos0 = (floor(pos) + 0.5) / texSize;
    
    // Catmull-Rom weights
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    
    // Collapse to 4 bilinear fetches (instead of 16 point samples)
    float2 w12 = w1 + w2;
    float2 tc12 = (pos0 + w2 / w12) / texSize;
    float2 tc0 = (pos0 - 1.0) / texSize;
    float2 tc3 = (pos0 + 2.0) / texSize;
    
    float3 result = 0;
    result += tex.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y);
    result += tex.SampleLevel(samp, float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y);
    result += tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rgb * (w12.x * w12.y);
    result += tex.SampleLevel(samp, float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y);
    result += tex.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);
    
    return max(result, 0);
}

// Usage:
float3 history = SampleHistoryCatmullRom(TextureOriginal, Sampler0, prevUV, ScreenSize.xy);
```

---

## 7.2 Luminance-Weighted Variance Clipping

**Source**: Standard production improvement — prevents flickering on high-contrast edges by weighting the variance calculation by luminance.

### Step-by-Step Implementation

Replace the variance clip computation (lines 77–97):

```hlsl
// Luminance-weighted neighborhood statistics
float3 boxMin = float3(9999, 9999, 9999);
float3 boxMax = float3(-9999, -9999, -9999);
float3 m1 = 0, m2 = 0;
float totalW = 0;

for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
        float2 sUV = uv + float2(x, y) * ScreenSize.zw;
        float3 s = RGBToYCoCg(TextureColor.SampleLevel(Sampler0, sUV, 0).rgb);
        
        // Luminance weight: high-luminance samples contribute more to the AABB
        // This prevents dark noise from excessively expanding the clamp box
        float w = 1.0 / (1.0 + s.x); // Inverse luminance weight
        
        boxMin = min(boxMin, s);
        boxMax = max(boxMax, s);
        m1 += s * w;
        m2 += s * s * w;
        totalW += w;
    }
}

m1 /= totalW;
m2 /= totalW;
float3 sigma = sqrt(abs(m2 - m1 * m1));

// Tighter clipping: use smaller gamma for luminance, larger for chrominance
float3 clipGamma = float3(taaClampGamma * 0.75, taaClampGamma, taaClampGamma);
boxMin = m1 - sigma * clipGamma;
boxMax = m1 + sigma * clipGamma;
```

---

# 8. SB_MaterialPBR.fx — Material Enhancement Upgrades

## Current State

Per-material GGX specular, screen-space SSS for skin, wet surface darkening, snow coverage. Missing: separable SSS profiles, cloth sheen, proper energy conservation.

---

## 8.1 Separable Christensen-Burley SSS (Replacing Disc SSS)

**Source**: `enbHelper_Common.fxh` already contains Christensen-Burley diffusion profiles (Section 8). This technique replaces the current 8-tap disc SSS with a proper separable (horizontal + vertical) diffusion blur using physically-based kernels.

### Step-by-Step Implementation

**Step 1 — Generate SSS Kernel from Diffusion Profile**

The kernel weights are computed from the Christensen-Burley profile already in `enbHelper_Common.fxh`:

```hlsl
// Kernel generation: precompute for up to 25 taps
// Use the CB_DiffusionProfile(r, A, s) function from enbHelper_Common.fxh
// A = scatter color (e.g., float3(0.48, 0.21, 0.11) for Caucasian skin)
// s = scatter distance (typically 1.0–3.0mm, scaled to screen pixels)
static const int SSS_KERNEL_SIZE = 25;
static const int SSS_HALF_SIZE = 12;

struct SSSKernel {
    float3 weights[SSS_KERNEL_SIZE];
};

SSSKernel BuildSSSKernel(float3 scatterColor, float scatterScale) {
    SSSKernel kernel;
    float3 totalWeight = 0;
    
    for (int i = 0; i < SSS_KERNEL_SIZE; i++) {
        float r = abs(float(i - SSS_HALF_SIZE)) * scatterScale;
        kernel.weights[i] = CB_DiffusionProfile(r, scatterColor, 1.0);
        totalWeight += kernel.weights[i];
    }
    
    // Normalize
    for (int i = 0; i < SSS_KERNEL_SIZE; i++) {
        kernel.weights[i] /= totalWeight;
    }
    
    return kernel;
}
```

**Step 2 — Horizontal SSS Blur Pass**

Replace the current `ScreenSSS` 8-tap disc with a separable two-pass blur:

```hlsl
float4 PS_SSS_Horizontal(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    int mat = SB_ReadMaterialID(SB_GBuffer_Material, int2(pos.xy));
    float4 center = TextureColor.Sample(Sampler0, uv);
    
    if (mat != SB_MAT_SKIN) return center; // Only blur skin
    
    float centerDepth = TextureDepth.SampleLevel(Sampler0, uv, 0).r;
    float3 result = center.rgb * sssKernelWeights[SSS_HALF_SIZE];
    
    for (int i = 1; i <= SSS_HALF_SIZE; i++) {
        float2 offset = float2(float(i) * pbrSSSRadius, 0) * ScreenSize.zw;
        
        // Positive direction
        float2 sUV_pos = uv + offset;
        float sDepth_pos = TextureDepth.SampleLevel(Sampler0, sUV_pos, 0).r;
        int sMat_pos = SB_ReadMaterialID(SB_GBuffer_Material, int2(pos.xy + int2(i, 0)));
        float depthW_pos = exp(-abs(centerDepth - sDepth_pos) * 5000.0);
        float matW_pos = (sMat_pos == SB_MAT_SKIN) ? 1.0 : 0.1;
        float3 sColor_pos = TextureColor.SampleLevel(Sampler0, sUV_pos, 0).rgb;
        result += sColor_pos * sssKernelWeights[SSS_HALF_SIZE + i] * depthW_pos * matW_pos;
        
        // Negative direction
        float2 sUV_neg = uv - offset;
        float sDepth_neg = TextureDepth.SampleLevel(Sampler0, sUV_neg, 0).r;
        int sMat_neg = SB_ReadMaterialID(SB_GBuffer_Material, int2(pos.xy - int2(i, 0)));
        float depthW_neg = exp(-abs(centerDepth - sDepth_neg) * 5000.0);
        float matW_neg = (sMat_neg == SB_MAT_SKIN) ? 1.0 : 0.1;
        float3 sColor_neg = TextureColor.SampleLevel(Sampler0, sUV_neg, 0).rgb;
        result += sColor_neg * sssKernelWeights[SSS_HALF_SIZE - i] * depthW_neg * matW_neg;
    }
    
    // Tint with subsurface color
    result = lerp(center.rgb, result * sssColor, pbrSSSIntensity);
    return float4(result, center.a);
}
// PS_SSS_Vertical: identical but offset = float2(0, float(i) * pbrSSSRadius)
```

---

## 8.2 Combat-Reactive Focus Isolation (Bouma's Emphasize)

**Source**: Bouma's Emphasize shader desaturates and darkens out-of-focus regions based on depth. Combined with SkyrimBridge's combat state.

### Step-by-Step Implementation

Add a new section to `PS_MaterialPBR` at the end, before the return:

```hlsl
// Combat-reactive focus isolation
// When in combat, subtly desaturate and darken distant background
float combatFocus = SB_Player_Combat.x; // 0 = peace, 1 = combat
if (combatFocus > 0.5 && depth < 0.9999) {
    float focusDist = SB_GetAutoFocusDistance(10.0); // Focus on crosshair target
    float linearZ = SB_LinearizeDepth(depth);
    float focusZ = focusDist;
    
    // Soft falloff from focus point
    float distFromFocus = abs(linearZ - focusZ) / max(focusZ, 1.0);
    float isolationMask = saturate(distFromFocus * 2.0 - 0.5); // 0 = in focus, 1 = out
    
    // Subtle desaturation of out-of-focus combat background
    float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 desat = lerp(color.rgb, float3(luma, luma, luma), isolationMask * 0.15);
    
    // Slight darkening
    desat *= lerp(1.0, 0.92, isolationMask);
    
    color.rgb = desat;
}
```

---

# 9. enbeffect.fx / enbeffectpostpass.fx — Color Science Pipeline

## Current State

These are the main compositing shaders in the Silent Horizons ENB integration. They handle tonemapping, color grading, LUT application, and final output.

---

## 9.1 CIE Lab Decomposition for Independent Luma/Chroma Control (prod80)

**Source**: prod80's `PD80_00_Color_Spaces.fxh` with Bruce Lindbloom formulations, D65 illuminant, exact rational precision for K = 24389/27 and E = 216/24389.

### Step-by-Step Implementation

**Step 1 — Add CIE Lab Conversion Functions to `enbHelper_Common.fxh`**

```hlsl
// CIE Lab conversion (prod80 formulation with D65 illuminant)
static const float3 D65_WP = float3(0.95047, 1.0, 1.08883);
static const float CIE_K = 24389.0 / 27.0;   // 903.2963...
static const float CIE_E = 216.0 / 24389.0;   // 0.008856...

float3 LinearToXYZ(float3 c) {
    // sRGB to CIE XYZ (D65) matrix
    return float3(
        dot(c, float3(0.4124564, 0.3575761, 0.1804375)),
        dot(c, float3(0.2126729, 0.7151522, 0.0721750)),
        dot(c, float3(0.0193339, 0.1191920, 0.9503041))
    );
}

float3 XYZToLinear(float3 xyz) {
    return float3(
        dot(xyz, float3( 3.2404542, -1.5371385, -0.4985314)),
        dot(xyz, float3(-0.9692660,  1.8760108,  0.0415560)),
        dot(xyz, float3( 0.0556434, -0.2040259,  1.0572252))
    );
}

float LabF(float t) {
    return t > CIE_E ? pow(t, 1.0 / 3.0) : (CIE_K * t + 16.0) / 116.0;
}

float LabFInv(float t) {
    float t3 = t * t * t;
    return t3 > CIE_E ? t3 : (116.0 * t - 16.0) / CIE_K;
}

float3 XYZToLab(float3 xyz) {
    float3 f = float3(LabF(xyz.x / D65_WP.x), LabF(xyz.y / D65_WP.y), LabF(xyz.z / D65_WP.z));
    return float3(116.0 * f.y - 16.0, 500.0 * (f.x - f.y), 200.0 * (f.y - f.z));
}

float3 LabToXYZ(float3 lab) {
    float fy = (lab.x + 16.0) / 116.0;
    float fx = lab.y / 500.0 + fy;
    float fz = fy - lab.z / 200.0;
    return float3(LabFInv(fx) * D65_WP.x, LabFInv(fy) * D65_WP.y, LabFInv(fz) * D65_WP.z);
}

float3 SRGBToLab(float3 c) { return XYZToLab(LinearToXYZ(c)); }
float3 LabToSRGB(float3 lab) { return XYZToLinear(LabToXYZ(lab)); }
```

**Step 2 — Lab-Space LUT Application (prod80 Technique)**

In the LUT application stage of `enbeffectpostpass.fx`:

```hlsl
// Standard LUT application produces: float3 lutColor = SampleLUT(color);
// prod80's innovation: independent luma and chroma mixing
float3 labOriginal = SRGBToLab(color.rgb);
float3 labLUT = SRGBToLab(lutColor.rgb);

// Mix luminance (L*) and chrominance (a*, b*) independently
float newL = lerp(labOriginal.x, labLUT.x, lutLumaMix);  // 0 = original luma, 1 = LUT luma
float2 newAB = lerp(labOriginal.yz, labLUT.yz, lutChromaMix); // Independent chroma control

float3 result = LabToSRGB(float3(newL, newAB));
```

This is impossible with standard RGB-space LUT blending and gives the user separate control over how the LUT affects brightness vs. color.

---

## 9.2 Perceptual Black-Point Detection (prod80)

**Source**: prod80's vector-based black-point detection that considers all three channels together rather than finding per-channel minimums.

### Step-by-Step Implementation

```hlsl
// Vector black-point detection
// Find the darkest color in the scene while preserving its hue
// This prevents the per-channel minimum from creating false color shifts
float3 DetectBlackPoint(Texture2D src, SamplerState samp) {
    float3 minColor = float3(1, 1, 1);
    float minLum = 1.0;
    
    // Sample scene at various mipmap levels for stability (prod80 technique)
    for (int mip = 2; mip <= 4; mip++) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                float2 sUV = (float2(x, y) + 0.5) / 4.0;
                float3 s = src.SampleLevel(samp, sUV, mip).rgb;
                float lum = dot(s, float3(0.2126, 0.7152, 0.0722));
                if (lum < minLum) {
                    minLum = lum;
                    minColor = s; // Keep the full color vector, not per-channel min
                }
            }
        }
    }
    return minColor;
}

// Apply black-point correction to lift shadows
float3 CorrectBlackPoint(float3 color, float3 blackPoint, float strength) {
    return (color - blackPoint * strength) / max(1.0 - blackPoint * strength, 0.001);
}
```

---

## 9.3 Saturation-Weighted Selective Color (prod80)

**Source**: prod80's selective color adjustments weighted by `smooth(curr_sat)` to prevent neutral-color contamination.

### Step-by-Step Implementation

```hlsl
// Selective color adjustment with saturation weighting
float3 SelectiveColor(float3 color, float3 redShift, float3 greenShift, float3 blueShift) {
    float saturation = max3(color) - min3(color);
    float smoothSat = smoothstep(0.0, 0.3, saturation); // Only affect saturated pixels
    
    // Determine dominant channel weights
    float3 channelWeights;
    channelWeights.r = smoothstep(0.0, 0.5, color.r - max(color.g, color.b));
    channelWeights.g = smoothstep(0.0, 0.5, color.g - max(color.r, color.b));
    channelWeights.b = smoothstep(0.0, 0.5, color.b - max(color.r, color.g));
    
    // Apply shifts weighted by saturation and channel dominance
    float3 shift = redShift * channelWeights.r + greenShift * channelWeights.g + blueShift * channelWeights.b;
    return color + shift * smoothSat;
}
```

---

# 10. SkyrimBridge.fxh — API Extensions

Based on the cross-cutting analysis of all five authors, the following additions to `SkyrimBridge.fxh` would unlock new technique categories:

---

## 10.1 Camera Decomposition (from Bouma's IGCS Connector)

Add individual Euler angle accessors and basis vectors for convenience:

```hlsl
float SB_GetCameraPitch() { return SB_Camera_Angles.x; } // radians
float SB_GetCameraYaw()   { return SB_Camera_Angles.y; } // radians
float SB_GetCameraFOV()   { return SB_Camera_Info.x; }   // degrees

float3 SB_GetCameraUp()      { return float3(SB_View_Row0.y, SB_View_Row1.y, SB_View_Row2.y); }
float3 SB_GetCameraRight()   { return float3(SB_View_Row0.x, SB_View_Row1.x, SB_View_Row2.x); }
float3 SB_GetCameraForward() { return float3(SB_View_Row0.z, SB_View_Row1.z, SB_View_Row2.z); }
```

---

## 10.2 Volumetric Scattering Parameters

Add missing SkyrimBridge parameters for fog (currently referenced but may not be fully declared in all builds):

```hlsl
float4 SB_Vol_Scatter;  // .x = inscattering coeff, .y = extinction coeff, .z = phase g, .w = albedo
float4 SB_Vol_Color;    // .rgb = scattering color, .a = emission
float4 SB_Camera_Planes;// .x = near clip, .y = far clip (alias for SB_Camera_Info.yz)
float4 SB_Camera_Position; // alias for SB_Camera_WorldPos
float4 SB_Shadow_SunDir;// .xyz = shadow caster direction (alias for SB_Shadow_Direction)
```

---

## 10.3 Convenience: Weather-Reactive Parameter Interpolation

Inspired by Kitsuune's per-weather shader annotation system:

```hlsl
// Interpolate a value between weather states using SB_Weather_Transition
float SB_WeatherLerp(float pleasant, float cloudy, float rainy, float snowy) {
    float t = SB_Weather_Transition.x; // Transition progress [0, 1]
    float val = pleasant; // Default
    
    if (SB_Weather_Flags.z > 0.5) val = lerp(val, rainy, t);
    else if (SB_Weather_Flags.w > 0.5) val = lerp(val, snowy, t);
    else if (SB_Weather_Flags.y > 0.5) val = lerp(val, cloudy, t);
    
    return val;
}

float3 SB_WeatherLerp3(float3 pleasant, float3 cloudy, float3 rainy, float3 snowy) {
    float t = SB_Weather_Transition.x;
    float3 val = pleasant;
    
    if (SB_Weather_Flags.z > 0.5) val = lerp(val, rainy, t);
    else if (SB_Weather_Flags.w > 0.5) val = lerp(val, snowy, t);
    else if (SB_Weather_Flags.y > 0.5) val = lerp(val, cloudy, t);
    
    return val;
}
```

---

## 10.4 Scene Wetness / Snow Coverage Helpers

Ensure these are available (referenced in SB_MaterialPBR.fx but may need formal declaration):

```hlsl
float SB_SceneWetness()   { return SB_Precip_Surface.x; } // [0, 1]
float SB_PuddleDepth()    { return SB_Precip_Surface.y; }
float SB_SnowCoverage()   { return SB_Precip_Surface.z; } // [0, 1]

// Linear depth helper that works with raw ENB depth
float SB_LinearDepth(float rawDepth, float nearClip, float farClip) {
    return nearClip * farClip / (farClip - rawDepth * (farClip - nearClip));
}
```

---

# 11. New Shader: SB_Launchpad.fx — Shared Temporal Prepass

**Source**: Gilcher's Launchpad precomputes smooth normals, optical flow, and depth preprocessing into shared textures consumed by all subsequent effects.

**What It Does**: Runs first in the shader chain and generates:
1. **Smooth normals** from depth buffer (for effects without G-buffer normal access)
2. **Optical flow** motion vectors (for effects needing temporal reprojection when SB_GBuffer_Motion is unavailable)
3. **Linear depth** at multiple mip levels (Hi-Z pyramid for SSR)
4. **Previous-frame textures** for temporal accumulation

### Step-by-Step Implementation

**This should be a new file: `SB_Launchpad.fx`** placed as the first technique in `enbeffectprepass.fx`.

```hlsl
// SB_Launchpad — Shared temporal prepass
// Run FIRST in the effect chain. Outputs shared textures consumed by:
//   SB_GTAO (smooth normals), SB_SSR (Hi-Z depth, optical flow),
//   SB_VolumetricFog (temporal reprojection), SB_TemporalAA (motion vectors)

// Pass 1: Linear depth + smooth normals
// Pass 2: Hi-Z depth pyramid (4 mip levels)
// Pass 3: Optical flow estimation (if SB_GBuffer_Motion unavailable)
// Pass 4: Copy current frame to history buffer
```

The optical flow estimation should use Gilcher's coarse-to-fine pyramid approach. The SophiaG optimizer integration (from LLM training research, adapted by Gilcher for optical flow) is the state-of-the-art approach but is complex to implement. For initial implementation, use a simplified Lucas-Kanade approach at 3 pyramid levels.

---

# 12. Implementation Roadmap

## Session Priority Matrix

| Priority | Target | Technique | Source Author | Estimated Complexity |
|----------|--------|-----------|---------------|---------------------|
| **P0** | SB_HDRBloom | Separable multi-pass bloom | Gilcher (Solaris) | Medium — requires multi-pass restructure |
| **P0** | SB_HDRBloom | Inverse Reinhard tonemapping | Bouma / Gilcher | Low — utility function addition |
| **P0** | enbHelper_Common | CIE Lab conversion functions | prod80 | Low — pure math functions |
| **P0** | SB_VolumetricFog | Bloom-fog dual blend | Bouma (AdaptiveFog) | Low — compositing addition |
| **P0** | SB_VolumetricFog | Y-gradient sky mask | Bouma (DepthHaze) | Low — single multiply |
| **P1** | SB_GTAO | Visibility bitmask mode | Gilcher (MXAO) | Medium — inner loop restructure |
| **P1** | SB_GTAO | Temporal blue noise jitter | Gilcher (Launchpad) | Low — noise offset change |
| **P1** | SB_SSR | GGX VNDF sampling | Gilcher (RTGI) | Medium — new sampling function |
| **P1** | SB_SSR | Hi-Z ray marching | Gilcher / AMD SSSR | High — requires depth mipchain |
| **P1** | SB_ContactShadows | Exponential step sizing | Gilcher (RTGI) | Low — loop restructure |
| **P1** | SB_MaterialPBR | Separable SSS blur | enbHelper_Common CB profiles | Medium — 2-pass restructure |
| **P1** | SkyrimBridge.fxh | Weather interpolation helpers | Kitsuune (Extender) | Low — utility functions |
| **P2** | SB_TemporalAA | Catmull-Rom history sampling | Production TAA (UE4/5) | Medium — new sampling function |
| **P2** | SB_TemporalAA | Luminance-weighted variance clip | Production TAA | Low — weight computation change |
| **P2** | SB_MotionBlur | Tile-based variable rate | McGuire 2012 / Gilcher | Medium — requires tile passes |
| **P2** | SB_GTAO | Specular occlusion output | Lagarde & de Rousiers 2014 | Low — math addition |
| **P2** | SB_SSR | BRDF demodulation | Gilcher (RTGI) | Medium — requires extra RT |
| **P2** | SB_VolumetricFog | Temporal reprojection | Frostbite 2014 / Gilcher | Medium — requires history buffer |
| **P2** | SB_VolumetricFog | Nearby light scattering | SkyrimBridge unique | Low — loop addition in march |
| **P2** | SB_ContactShadows | Multi-light contact shadows | SkyrimBridge unique | Medium — new light loop |
| **P2** | SB_MaterialPBR | Combat-reactive focus isolation | Bouma (Emphasize) + SB | Low — compositing addition |
| **P2** | enbeffectpostpass | Lab-space LUT application | prod80 | Low — post-LUT processing |
| **P2** | enbeffectpostpass | Perceptual black-point correction | prod80 | Low — scene analysis pass |
| **P2** | enbeffectpostpass | Saturation-weighted selective color | prod80 | Low — per-pixel math |
| **P3** | SB_HDRBloom | Depth-aware bloom masking | prod80 | Medium — requires depth mip |
| **P3** | SB_GTAO | 2×2 deinterleaved rendering | Gilcher (MXAO / ASSAO) | High — multi-pass architecture |
| **P3** | New: SB_Launchpad | Shared temporal prepass | Gilcher (Launchpad) | High — new shader file |
| **P3** | SB_SSR | Irradiance probe grid (SH) | Gilcher (RTGI) | Very High — new data structure |

## Recommended Session Order

**Session 1**: P0 items — HDR bloom separable chain + inverse Reinhard + CIE Lab functions + fog compositing improvements. These are the highest visual-impact changes with lowest risk.

**Session 2**: P1 GTAO — Visibility bitmask + temporal noise. This is the single highest quality improvement to the most visible effect.

**Session 3**: P1 SSR — VNDF sampling + Hi-Z march. Requires Launchpad depth mip chain, so consider building that first.

**Session 4**: P1 Materials — Separable SSS + contact shadow improvements. Skin rendering quality jump.

**Session 5**: P2 TAA + Motion Blur — Catmull-Rom + variance clip + tile-based MB. Temporal stability improvements compound everything else.

**Session 6**: P2 Color Science — Lab LUT, black-point, selective color in postpass. Artistic control layer.

**Session 7**: P2/P3 Advanced — Volumetric temporal, deinterleaved AO, Launchpad prepass. Architecture-level improvements.
