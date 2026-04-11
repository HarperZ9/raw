#ifndef EFFECT_CAS_FXH
#define EFFECT_CAS_FXH
//=============================================================================
//  Effect_CAS.fxh — AMD Contrast Adaptive Sharpening
//
//  Single-pass sharpening that adapts to local contrast to avoid
//  ringing/haloing artifacts. Based on AMD FidelityFX CAS.
//
//  Usage:
//    color.rgb = SB_ApplyCAS(TextureColor, LinearSampler, uv,
//                            PixelSize.xy, sharpness);
//
//  Call after all color grading, before dithering.
//
//  Author: Zain Dana Harper
//  Reference: AMD FidelityFX CAS, adapted from QuantV
//=============================================================================


// AMD Contrast Adaptive Sharpening — 5-tap cross pattern
// tex       = source color texture
// smp       = point or linear sampler
// uv        = current pixel UV
// pixelSize = float2(1/width, 1/height)
// sharpness = 0.0 (none) to 1.0 (maximum)
float3 SB_ApplyCAS(Texture2D tex, SamplerState smp, float2 uv,
                   float2 pixelSize, float sharpness)
{
    // 5-tap cross pattern (center + NSEW neighbors)
    float3 c = tex.SampleLevel(smp, uv, 0).rgb;
    float3 n = tex.SampleLevel(smp, uv + float2(0, -pixelSize.y), 0).rgb;
    float3 s = tex.SampleLevel(smp, uv + float2(0,  pixelSize.y), 0).rgb;
    float3 e = tex.SampleLevel(smp, uv + float2( pixelSize.x, 0), 0).rgb;
    float3 w = tex.SampleLevel(smp, uv + float2(-pixelSize.x, 0), 0).rgb;

    // Per-channel min/max of the neighborhood
    float3 mnRGB = min(c, min(min(n, s), min(e, w)));
    float3 mxRGB = max(c, max(max(n, s), max(e, w)));

    // Adaptive sharpening weight: sharpen more in flat areas, less at edges
    float3 ampRGB = saturate(min(mnRGB, 1.0 - mxRGB) / max(mxRGB, 1e-5));
    ampRGB = sqrt(ampRGB);

    // Sharpness control: 0 = none, 1 = maximum
    // Negative reciprocal per AMD FidelityFX CAS spec: peak ∈ [-0.2, -0.125]
    float peak = -1.0 / lerp(8.0, 5.0, saturate(sharpness));
    float3 wRGB = ampRGB * peak;

    // Weighted sum: sharpen center, subtract neighbors
    float3 rcpW = 1.0 / (1.0 + 4.0 * wRGB);
    float3 result = saturate((c + (n + s + e + w) * wRGB) * rcpW);

    return result;
}


#endif // EFFECT_CAS_FXH
