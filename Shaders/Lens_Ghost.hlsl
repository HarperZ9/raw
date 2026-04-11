// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 2: Lens Ghost Generation (ABCD ray-transfer matrix optics)
//
// Each ghost image is produced by light reflecting between lens elements.
// The ABCD ray-transfer matrix describes how a ray's position and angle
// transform through a cascade of surfaces. For ghost generation:
//   - GhostParams[i].xy = scale factor (ABCD 'A' diagonal = magnification)
//     Negative scale means the ghost is inverted (even number of reflections).
//   - GhostParams[i].zw = offset (ABCD 'B' translation)
//
// Per-ghost attenuation uses the Fresnel-Schlick approximation:
//   R = R0 + (1 - R0) * (1 - cos(theta))^5
// where R0 depends on the lens coating refractive index.
//
// Ghost tints model thin-film interference colors from multi-coated optics.
//
// Reference: ABCD matrix optics (Hecht, "Optics", Ch. 6);
//            Fresnel-Schlick approximation (Schlick 1994).

cbuffer GhostCB : register(b0)
{
    uint2  OutputDims;    // Quarter-res output dimensions
    uint2  Mip0Dims;      // Half-res mip dimensions
    float  GhostIntensity; // Overall ghost strength
    int    GhostCount;    // Number of active ghosts (2-8)
    float  AspectRatio;   // Screen aspect ratio
    float  pad0;

    // Per-ghost parameters packed as float4 rows:
    // .xy = scale (ABCD A diagonal), .zw = offset (ABCD B translation)
    float4 GhostParams[8];

    // Per-ghost tint (from thin-film interference model)
    float4 GhostTint[8];
}

Texture2D<float4> Mip0Tex : register(t0); // Half-res bright
Texture2D<float4> Mip1Tex : register(t1); // Quarter-res
Texture2D<float4> Mip2Tex : register(t2); // Eighth-res
Texture2D<float4> Mip3Tex : register(t3); // Sixteenth-res
SamplerState      LinSamp : register(s0);
RWTexture2D<float4> GhostOutput : register(u0);

// Fresnel-Schlick approximation for dielectric coating reflectance.
// R0 for typical multi-coated glass: ((1.38 - 1.0) / (1.38 + 1.0))^2 ~ 0.025
float FresnelSchlick(float cosTheta, float R0)
{
    return R0 + (1.0 - R0) * pow(saturate(1.0 - cosTheta), 5.0);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    // Normalized UV in [0,1] for this output pixel
    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);

    float4 ghostAccum = 0.0;

    // Coating reflectance R0: multi-coated MgF2 on glass, n_coating ~ 1.38
    // R0 = ((n_coating - 1) / (n_coating + 1))^2
    static const float R0 = 0.025;

    for (int i = 0; i < GhostCount; i++)
    {
        float2 scale  = GhostParams[i].xy;
        float2 offset = GhostParams[i].zw;

        // Mirror the UV around screen center and apply per-ghost scale.
        // This simulates the ABCD matrix transformation:
        //   ghostUV = A * (uv - 0.5) + B + 0.5
        // where A (scale) encodes magnification and inversion,
        // and B (offset) encodes lateral displacement from misaligned elements.
        float2 ghostUV = (uv - 0.5) * scale + offset + 0.5;

        // Discard ghosts that fall outside the frame
        if (any(ghostUV < 0.0) || any(ghostUV > 1.0))
            continue;

        // Choose mip based on ghost magnification -- larger ghosts use
        // lower-res mips for softer appearance (as in real optics, larger
        // ghost images have more aberration blur).
        float absMag = max(abs(scale.x), abs(scale.y));
        float4 sample;
        if (absMag > 1.5)
            sample = Mip3Tex.SampleLevel(LinSamp, ghostUV, 0);  // Sixteenth-res for large ghosts (most aberration blur)
        else if (absMag > 0.8)
            sample = Mip2Tex.SampleLevel(LinSamp, ghostUV, 0);  // Eighth-res for medium
        else if (absMag > 0.3)
            sample = Mip1Tex.SampleLevel(LinSamp, ghostUV, 0);  // Quarter-res for small
        else
            sample = Mip0Tex.SampleLevel(LinSamp, ghostUV, 0);  // Half-res for tiny (sharpest)

        // Distance from center: ghosts at screen edges are dimmer due to
        // the cos^4 illumination falloff of optical systems.
        float2 centered = ghostUV - 0.5;
        float dist = length(centered);
        float edgeFalloff = 1.0 - smoothstep(0.3, 0.7, dist);

        // Fresnel attenuation: the angle of incidence increases with distance
        // from the optical axis. cosTheta approximated from distance.
        float cosTheta = saturate(1.0 - dist * 2.0);
        float fresnel = FresnelSchlick(cosTheta, R0);

        // Apply per-ghost thin-film interference tint
        float3 tinted = sample.rgb * GhostTint[i].rgb;

        // Weight by Fresnel reflectance and edge falloff
        ghostAccum.rgb += tinted * fresnel * edgeFalloff;
    }

    GhostOutput[DTid.xy] = float4(ghostAccum.rgb * GhostIntensity, 1.0);
}
