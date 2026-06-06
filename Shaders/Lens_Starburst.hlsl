// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 3: Diffraction Starburst from Aperture Blades
//
// Bright point sources produce radial streaks due to diffraction at the
// aperture blade edges. An N-blade aperture produces 2*N streaks (for even N)
// or N streaks (for odd N). The streak pattern is:
//   streak = pow(abs(cos(angle * numBlades / 2)), streakPower)
//
// Each output pixel accumulates light from bright source pixels along radial
// directions matching the starburst pattern, weighted by distance falloff.
//
// Reference: Fraunhofer diffraction from polygonal aperture (Hecht, "Optics",
//            Ch. 10); starburst power empirically fit to real lens photographs.

cbuffer StarburstCB : register(b0)
{
    uint2  OutputDims;
    float  StarburstIntensity;
    float  VeilingGlareStrength;
    int    ApertureBlades;     // 4-9
    float  StarburstRotation;  // Rotation angle in radians (linked to camera)
    float  CoatingFactor;      // Multi-coated: 0.01, uncoated: 0.1
    float  pad0;
    uint2  Mip3Dims;           // Sixteenth-res dimensions
    float2 pad1;
}

Texture2D<float4> BrightMip   : register(t0); // Quarter-res bright extract
Texture2D<float4> BloomLowest : register(t1); // Bloom lowest mip (veiling glare proxy)
SamplerState      LinSamp     : register(s0);
RWTexture2D<float4> StarburstOutput : register(u0);

static const float PI = 3.14159265359;

// Number of radial samples per streak direction
static const int kRadialSamples = 16;

// Starburst sharpness exponent (higher = thinner streaks)
static const float kStreakPower = 4.0;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);
    float2 center = float2(0.5, 0.5);
    float2 toPixel = uv - center;
    float dist = length(toPixel);

    // ── Starburst ────────────────────────────────────────────────────
    // Compute the angular streak pattern from the aperture blade count.
    // For N blades, diffraction produces streaks at angles k * PI / N.
    float angle = atan2(toPixel.y, toPixel.x) + StarburstRotation;
    float halfBlades = float(ApertureBlades) * 0.5;

    // Streak intensity: cos^power produces the characteristic star pattern.
    // The abs() ensures we get streaks in both directions for each blade edge.
    float streak = pow(abs(cos(angle * halfBlades)), kStreakPower);

    // Accumulate bright source light along radial direction toward center.
    // This simulates the radial PSF of diffraction streaks.
    float3 starColor = 0.0;
    float2 dir = (dist > 1e-5) ? normalize(toPixel) : float2(0, 0);

    for (int s = 0; s < kRadialSamples; s++)
    {
        // Sample along the radial direction from this pixel toward center
        float t = (float(s) + 0.5) / float(kRadialSamples);
        float2 sampleUV = uv - dir * t * 0.5;  // Walk halfway toward center

        // Clamp to valid range
        sampleUV = clamp(sampleUV, 0.0, 1.0);

        float3 src = BrightMip.SampleLevel(LinSamp, sampleUV, 0).rgb;

        // Distance-based falloff: light from distant bright sources fades
        float weight = exp(-t * 3.0);
        starColor += src * weight;
    }
    starColor /= float(kRadialSamples);

    // Apply streak pattern and coating factor (multi-coated lenses produce
    // weaker starbursts due to reduced surface reflections)
    float3 starburst = starColor * streak * CoatingFactor * 10.0;

    // ── Veiling Glare ────────────────────────────────────────────────
    // Low-frequency haze from internal scatter (approximated by the lowest
    // mip of the bright extract or bloom). Adds a uniform warm glow.
    float3 veil = BloomLowest.SampleLevel(LinSamp, uv, 0).rgb;

    float3 result = starburst * StarburstIntensity + veil * VeilingGlareStrength;

    StarburstOutput[DTid.xy] = float4(result, 1.0);
}
