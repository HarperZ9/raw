#pragma once
// TonemapPs.h: RAW's tone map, in place of the game's (Tonemap.cpp). It takes the game's tone map's
// bindings as they are: t1 the HDR scene, t0 the bloom, t2 the eye-adapted luminance (x the adapted
// average, y the game's target), pixel buffer 2 the imagespace values (bloom limit, white, curve choice,
// saturation, brightness, contrast, tint) and per-frame buffer 12 (the output gamma, the dynamic
// resolution scale). RAW's own values come in buffer 5.
//
// Exposure, mode 0: the game's (target / adapted average). Mode 1, a camera's: the metered average,
// held between MinLum and MaxLum, is brought to Key (mid grey), then ExposureBias stops. The curve,
// 0: the game's own; 1: a filmic curve per channel (the fitted ACES curve published by K. Narkowicz,
// 2015) with its input scaled by White, which rolls highlights off to white and desaturates them as film
// does. After the curve the game's imagespace grade applies as the game applies it, so weathers and
// interiors keep their look. Original work, MIT.

namespace SB::Proxy
{
inline constexpr const char* kTonemapHlsl = R"HLSL(
cbuffer PerFrame : register(b12) { float4 F[45]; };
cbuffer Image : register(b2) { float4 P[5]; };
cbuffer Raw : register(b5) { float4 RA; float4 RB; };   // RA: mode, bias (stops), min lum, max lum; RB: curve, key, white, view
Texture2D<float4> Bloom : register(t0);
Texture2D<float4> Scene : register(t1);
Texture2D<float4> Lum   : register(t2);
SamplerState S0 : register(s0);
SamplerState S1 : register(s1);
SamplerState S2 : register(s2);
struct In { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
static const float3 W = float3(0.2125f, 0.7154f, 0.0721f);

float3 filmic(float3 x) { return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f)); }

float gameCurve(float l) {   // the game's own: a Hejl-style curve or a Reinhard with a white point
    if (P[2].z > 0.5f) {
        const float x = max(l - 0.004f, 0.0f);
        return pow((x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f), 2.2f) * P[2].y;
    }
    return l * (1.0f + l * P[2].y) / (1.0f + l);
}

float4 main(In i) : SV_Target {
    const float2 suv = min(max(i.uv * F[43].xy, 0.0f), float2(F[44].z, F[43].y));
    const float3 hdr = Scene.Sample(S1, suv).rgb;
    const float3 bloom = P[0].x > 0.5f ? Bloom.Sample(S0, suv).rgb : Bloom.Sample(S0, i.uv).rgb;
    const float2 lum = Lum.Sample(S2, i.uv).xy;
    const float avg = max(lum.x, 1.0e-5f);
    const float exposure = (RA.x < 0.5f ? lum.y / avg : RB.y / clamp(avg, RA.z, RA.w)) * exp2(RA.y);
    float3 mapped;
    float pivot = lum.x;
    if (RB.x < 0.5f) {
        const float l = max(dot(hdr, W), 1.0e-5f);
        mapped = hdr * (gameCurve(l * exposure) / l);
    } else {
        mapped = filmic(hdr * exposure * RB.z);
        if (RA.x > 0.5f) { pivot = dot(filmic((RB.y * exp2(RA.y) * RB.z).xxx), W); }   // the grade turns about mid grey
    }
    mapped += bloom * saturate(P[2].x - dot(mapped, W));
    if (RB.w > 0.5f) { return float4(exposure.xxx * 0.25f, 1.0f); }   // view: the exposure (4 = white)
    // The game's grade, lane for lane as the game computes it (alpha included; its anti-aliasing reads it).
    const float g = dot(mapped, W);
    float4 c = float4(mapped, 1.0f);
    c = P[3].x * (c - g) + g;
    c = P[4].w * (g * P[4] - c) + c;
    c = P[3].z * (P[3].w * c - pivot) + pivot;
    c.rgb = pow(saturate(c.rgb), F[42].x);
    return c;
}
)HLSL";
} // namespace SB::Proxy
