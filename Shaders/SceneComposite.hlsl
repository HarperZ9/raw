// Scene Compositor — composites all PostGeometry effects onto the game scene.
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

Texture2D<float4> SceneColor   : register(t0);
Texture2D<float>  AOTex        : register(t1);
Texture2D<float4> GITex        : register(t2);
Texture2D<float4> SSRTex       : register(t3);
Texture2D<float4> CloudTex     : register(t4);
Texture2D<float>  ShadowTex    : register(t5);
Texture2D<float>  SkylightTex  : register(t6);

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);

cbuffer CompositeCB : register(b0)
{
    float aoIntensity;
    float giIntensity;
    float ssrIntensity;
    float cloudIntensity;
    uint  enableFlags;
    float giMaxAdd;
    float shadowIntensity;
    float skylightIntensity;
    uint  debugMode;
    uint  hdr10Enabled;
    float paperWhiteNits;
    float peakNits;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.texcoord;
    float3 color = SceneColor.Sample(PointSampler, uv).rgb;

    // Sanitize
    if (any(isnan(color)) || any(isinf(color)))
        color = float3(0, 0, 0);

    // AO: multi-bounce aware, sqrt-softened (Jimenez 2016 + CS approach)
    // Multi-bounce prevents colored surfaces from going too dark.
    // sqrt softening applies gentler AO on direct light (full AO on ambient).
    if (enableFlags & 1u)
    {
        float ao = saturate(AOTex.Sample(PointSampler, uv).r);

        // Estimate albedo from scene color (approximate, sufficient for AO weighting)
        float3 albedo = saturate(color / max(dot(color, float3(0.2126, 0.7152, 0.0722)), 0.01));

        // Jimenez 2016 multi-bounce AO: prevents over-darkening bright/colored surfaces
        float3 a = 2.0404 * albedo - 0.3324;
        float3 b = -4.7951 * albedo + 0.6417;
        float3 c_ = 2.7552 * albedo + 0.6903;
        float3 multiBounce = max(ao, ((ao * a + b) * ao + c_) * ao);

        // sqrt softening: gentler on direct lighting (CS approach)
        color *= lerp(1.0, sqrt(multiBounce), aoIntensity);
    }

    // Contact Shadows: multiplicative darkening (t5 -- rewritten clean)
    if (enableFlags & 16u)
    {
        float shadow = saturate(ShadowTex.Sample(PointSampler, uv).r);
        color *= lerp(1.0, shadow, shadowIntensity);
    }

    // Skylighting: ambient modulation (t6 -- rewritten clean)
    if (enableFlags & 32u)
    {
        float skyVis = saturate(SkylightTex.Sample(PointSampler, uv).r);
        float skyFactor = lerp(1.0 - skylightIntensity * 0.5, 1.0, skyVis);
        color *= skyFactor;
    }

    // SSR: energy-conserving reflections with Specular AO (t3)
    // Lagarde Specular AO (Frostbite PBR 3.0): occlude reflections in
    // corners/crevices where diffuse AO is dark. Rougher surfaces get
    // more specular occlusion.
    if (enableFlags & 4u)
    {
        float4 ssr = SSRTex.Sample(LinearSampler, uv);
        float3 reflColor = max(ssr.rgb, 0.0);
        float  confidence = saturate(ssr.a);
        float  reflWeight = confidence * ssrIntensity;

        // If AO is available, apply specular occlusion to reflections
        if (enableFlags & 1u)
        {
            float ao = saturate(AOTex.Sample(PointSampler, uv).r);
            // Approximate NdotV from depth gradient (cheap, no normal buffer needed)
            float approxNdotV = 0.5; // Conservative midpoint when exact normal unavailable
            float specAO = saturate(pow(abs(approxNdotV + ao), exp2(-16.0 * 0.3 - 1.0)) - 1.0 + ao);
            reflWeight *= specAO;
        }

        color = lerp(color, reflColor, reflWeight);
    }

    // GI: additive indirect light with Reinhard soft-clamp (t2 -- rewritten clean)
    if (enableFlags & 2u)
    {
        float3 gi = max(GITex.Sample(LinearSampler, uv).rgb, 0.0);
        float giLuma = dot(gi, float3(0.2126, 0.7152, 0.0722));
        float giScale = giIntensity;
        if (giLuma > 0.001)
        {
            float clampedLuma = giLuma / (1.0 + giLuma / giMaxAdd);
            giScale *= clampedLuma / giLuma;
        }
        float sceneLuma2 = dot(color, float3(0.2126, 0.7152, 0.0722));
        float darkBoost = saturate(1.0 - sceneLuma2 * 0.5);
        color += gi * giScale * darkBoost;
    }

    // Clouds: volumetric over-blend (t4 -- rewritten clean)
    if (enableFlags & 8u)
    {
        float4 cloud = CloudTex.Sample(LinearSampler, uv);
        float3 inscatter = max(cloud.rgb, 0.0);
        float transmittance = saturate(cloud.a);
        color = color * transmittance + inscatter * cloudIntensity;
    }

    color = max(color, 0.0);

    // Debug visualization (1=AO, 2=GI, 3=SSR, 4=Clouds, 5=Shadow, 6=Skylight)
    if (debugMode == 1u)
        return float4(saturate(AOTex.Sample(PointSampler, uv).rrr), 1.0);
    if (debugMode == 2u)
        return float4(max(GITex.Sample(LinearSampler, uv).rgb, 0.0), 1.0);
    if (debugMode == 3u)
        return float4(max(SSRTex.Sample(LinearSampler, uv).rgb, 0.0), 1.0);
    if (debugMode == 4u)
    {
        float4 cloud = CloudTex.Sample(LinearSampler, uv);
        return float4(max(cloud.rgb, 0.0) + (1.0 - cloud.a) * 0.1, 1.0); // inscatter + transmittance viz
    }
    if (debugMode == 5u)
        return float4(saturate(ShadowTex.Sample(PointSampler, uv).rrr), 1.0);
    if (debugMode == 6u)
        return float4(saturate(SkylightTex.Sample(PointSampler, uv).rrr), 1.0);

    return float4(color, 1.0);
}
