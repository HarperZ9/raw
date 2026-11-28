// RawFogShadowCache16km.hlsli: CS-040 16 km far-field volumetric fog shadow cache + Kiruna twilight dusk photometry.
// SPDX-FileCopyrightText: 2026 Zain Dana Harper
// SPDX-License-Identifier: MIT
//
// Pure math include (zero register declarations, zero resource bindings). Mirrored bit-for-bit in
// src/d3d12_bridge/RawFogShadowCache16km.h so the C++23 unit test validates the exact shader constants.
//
// 1) Clipmap cache layout: 6 nested camera-snapped levels (1, 2, 4, 8, 16, 32 km sides) of 256 x 256 x 32 RG16F
//    texels (sun-ray optical depth + far-field geometric visibility). Level 5 covers a 16 km radius around the eye.
//    Each level refreshes 1/8 of its slices per frame (8-frame round robin), and the clip origin is snapped to whole
//    texels so camera motion never shimmers the far fog shadow.
// 2) Far-field visibility: the cascade shadow (0..12,000 u) crossfades over 9,000..12,000 u into 4 geometric
//    sun-ray depth probes (stride 0.04, 0.12, 0.36, 1.08 x ray distance, clamped 600 u .. 140,000 u) so distant
//    mountain ridges carve real shadow volumes through valley haze out to 16 km (1,120,000 u at 70 u per metre).
// 3) Kiruna twilight photometry: Kasten-Young (1989) relative airmass per view zenith angle gives the dusk
//    horizon-to-zenith luminance ratio (capped at 4.0), zenith luminance decays 0.41 dex per degree of solar
//    depression through civil and nautical twilight, and a luminance-normalized solar-amber / anti-solar rose
//    tint keeps twilight horizons crisp instead of washing them into flat grey-blue haze.
// Original work, MIT.

#ifndef RAW_FOG_SHADOW_CACHE_16KM_HLSLI
#define RAW_FOG_SHADOW_CACHE_16KM_HLSLI

static const float kRawFsc16UnitsPerMeter   = 70.0f;
static const float kRawFsc16RangeU          = 1120000.0f;   // 16 km far-field shadow radius
static const float kRawFsc16Level0SideU     = 70000.0f;     // 1 km innermost clipmap side
static const uint  kRawFsc16Levels          = 6u;           // 1, 2, 4, 8, 16, 32 km sides
static const uint  kRawFsc16TexelsXY        = 256u;
static const uint  kRawFsc16Slices          = 32u;
static const uint  kRawFsc16BytesPerTexel   = 4u;           // RG16F
static const uint  kRawFsc16UpdateDivisor   = 8u;           // 1/8 of slices refreshed per frame
static const uint  kRawFsc16ProbeCount      = 4u;
static const float kRawFsc16CascadeEndU     = 12000.0f;
static const float kRawFsc16CascadeFadeU    = 3000.0f;
static const float kRawFsc16ProbeMinU       = 600.0f;
static const float kRawFsc16ProbeMaxU       = 140000.0f;
static const float kRawFsc16VisFloor        = 0.14f;
static const float kRawFsc16TwilightMaxRatio = 4.0f;

float rawFsc16SafeDist(float d)
{
    return isfinite(d) ? max(d, 0.0f) : 0.0f;
}

// Smallest clipmap level whose half-side covers the horizontal distance (0..5).
uint rawFsc16LevelForDistance(float distU)
{
    const float ratio = rawFsc16SafeDist(distU) / (kRawFsc16Level0SideU * 0.5f);
    if (ratio <= 1.0f) { return 0u; }
    return min(uint(ceil(log2(ratio))), kRawFsc16Levels - 1u);
}

float rawFsc16LevelSideU(uint level)
{
    return kRawFsc16Level0SideU * float(1u << min(level, kRawFsc16Levels - 1u));
}

float rawFsc16TexelSizeU(uint level)
{
    return rawFsc16LevelSideU(level) / float(kRawFsc16TexelsXY);
}

// Texel-snapped clip origin (prevents far fog shadow shimmer under camera translation).
float2 rawFsc16SnapOrigin(float2 camXY, uint level)
{
    const float t = rawFsc16TexelSizeU(level);
    return floor(camXY / t) * t;
}

// 8-frame round robin: true when this slice is refreshed on this frame.
bool rawFsc16SliceDueThisFrame(uint slice, uint frameIndex)
{
    return (slice % kRawFsc16UpdateDivisor) == (frameIndex % kRawFsc16UpdateDivisor);
}

// Geometric sun-ray probe distance (i = 0..3): 0.04, 0.12, 0.36, 1.08 x ray distance, clamped.
float rawFsc16ProbeStride(uint i, float rayDistU)
{
    const float mul = 0.04f * pow(3.0f, float(min(i, kRawFsc16ProbeCount - 1u)));
    return clamp(rawFsc16SafeDist(rayDistU) * mul, kRawFsc16ProbeMinU, kRawFsc16ProbeMaxU);
}

// Far-field weight: 0 inside the cascade, 1 from 12,000 u out to 85% of range, 0 at 16 km.
float rawFsc16FarFieldWeight(float distU)
{
    const float d = rawFsc16SafeDist(distU);
    return smoothstep(kRawFsc16CascadeEndU - kRawFsc16CascadeFadeU, kRawFsc16CascadeEndU, d)
         * (1.0f - smoothstep(kRawFsc16RangeU * 0.85f, kRawFsc16RangeU, d));
}

// One depth probe: soft occlusion ramp when the scene surface sits in front of the probe (4%..8% relative depth onset,
// fading out past 35%..45% so thin foreground props never shadow far fog). Continuous in depth (zero temporal boiling)
// and edge-faded near the screen border.
float rawFsc16ProbeOcclusion(float zHit, float zProbe, float2 probeUv)
{
    if (!(isfinite(zHit) && isfinite(zProbe)) || zProbe <= 1.0f || zHit < 0.0f) { return 1.0f; }
    const float edgeV = smoothstep(0.0f, 0.06f, min(min(probeUv.x, 1.0f - probeUv.x), min(probeUv.y, 1.0f - probeUv.y)));
    const float rel = (zProbe - zHit) / zProbe;
    const float hitW = smoothstep(0.04f, 0.08f, rel) * (1.0f - smoothstep(0.35f, 0.45f, rel));
    return lerp(1.0f, 0.30f, edgeV * hitW);
}

float rawFsc16ResolveProbeVis(float occProduct)
{
    return clamp(isfinite(occProduct) ? occProduct : 1.0f, kRawFsc16VisFloor, 1.0f);
}

// Cascade (near) to far-field (probe) visibility crossfade. Identical to the cascade below 9,000 u.
float rawFsc16CombineVisibility(float cascadeVis, float farProbeVis, float distU)
{
    const float cv = saturate(isfinite(cascadeVis) ? cascadeVis : 1.0f);
    const float fv = saturate(isfinite(farProbeVis) ? farProbeVis : 1.0f);
    const float d = rawFsc16SafeDist(distU);
    const float cascadeW = 1.0f - smoothstep(kRawFsc16CascadeEndU - kRawFsc16CascadeFadeU, kRawFsc16CascadeEndU, d);
    const float farV = lerp(1.0f, fv, rawFsc16FarFieldWeight(d));
    return lerp(farV, cv, cascadeW);
}

// Kasten and Young (1989) relative optical airmass for zenith angle in degrees.
float rawFsc16KastenYoungAirmass(float zenithDeg)
{
    const float z = clamp(isfinite(zenithDeg) ? zenithDeg : 0.0f, 0.0f, 89.9f);
    return 1.0f / (cos(radians(z)) + 0.50572f * pow(96.07995f - z, -1.6364f));
}

struct RawFsc16Twilight {
    float3 tint;               // luminance-normalized (Rec. 709 Y == 1)
    float  gain;               // photometric luminance gain on twilight fog in-scatter
    float  weight;             // civil + nautical twilight activation 0..1
    float  zenithLumRel;       // zenith luminance relative to sunset (1.0 at 0 deg)
    float  horizonZenithRatio; // dusk horizon / zenith luminance ratio (1..4)
};

RawFsc16Twilight rawFsc16KirunaTwilight(float sunElevRad, float viewElevRad, float cosSunAz)
{
    RawFsc16Twilight o;
    const float hDeg = clamp(degrees(isfinite(sunElevRad) ? sunElevRad : 0.0f), -18.0f, 90.0f);
    o.weight = smoothstep(-12.0f, -6.0f, hDeg) * (1.0f - smoothstep(2.0f, 8.0f, hDeg));
    o.zenithLumRel = pow(10.0f, 0.41f * max(min(hDeg, 0.0f), -12.0f));
    const float viewElevDeg = degrees(max(isfinite(viewElevRad) ? viewElevRad : 0.0f, 0.0f));
    const float zDeg = clamp(90.0f - viewElevDeg, 0.0f, 85.0f);
    const float X = rawFsc16KastenYoungAirmass(zDeg);
    o.horizonZenithRatio = clamp(1.0f + 0.30f * (X - 1.0f), 1.0f, kRawFsc16TwilightMaxRatio);
    const float solar = saturate((isfinite(cosSunAz) ? cosSunAz : 0.0f) * 0.5f + 0.5f);
    const float3 tintRaw = lerp(float3(1.03f, 0.97f, 1.03f), float3(1.07f, 0.98f, 0.86f), solar);
    o.tint = tintRaw / max(dot(tintRaw, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-4f);
    o.gain = lerp(0.90f, 1.06f, saturate((o.horizonZenithRatio - 1.0f) / (kRawFsc16TwilightMaxRatio - 1.0f)));
    return o;
}

// Multiplier applied to fog in-scatter during twilight (1.0 outside twilight; blend strength 0..1).
float3 rawFsc16TwilightFogMul(RawFsc16Twilight tw, float strength)
{
    return lerp(1.0f.xxx, tw.tint * tw.gain, saturate(tw.weight * strength));
}

#endif // RAW_FOG_SHADOW_CACHE_16KM_HLSLI
