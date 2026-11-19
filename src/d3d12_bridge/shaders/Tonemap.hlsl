// Tonemap.hlsl: RAW's tone map, in place of the game's (src/d3d11_proxy/Tonemap.cpp). It takes the game's tone map's
// bindings as they are: t1 the HDR scene, t0 the bloom, t2 the eye-adapted luminance (x the adapted average, y the
// game's target), pixel buffer 2 the imagespace values (bloom limit, white, curve choice, saturation, brightness,
// contrast, tint) and per-frame buffer 12 (the output gamma, the dynamic resolution scale). RAW's own values come in
// buffer 5. The build embeds this file (TonemapPs.h); with [HotReload] on, a copy in ShaderDir replaces it live.
// Compiled as ps_5_0 by d3dcompiler_47. Original work, MIT.
cbuffer PerFrame : register(b12) { float4 F[45]; };
cbuffer Image : register(b2) { float4 P[5]; };
cbuffer Raw : register(b5) { float4 RA; float4 RB; float4 RC; float4 RX; };   // RA: mode, bias (stops), min lum, max lum;
// RB: curve, key, white, view; RC: adapt, reference average, ToneX0, ToneX1; RX: ToneX2..5
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
    // Mode 1: a camera's exposure brings the metered average to the key. Adapt (RC.x) is the share of a change of
    // average the exposure follows: 1 is a camera's full compensation, below 1 a dark room stays darker than a bright
    // street, as the eye's partial adaptation leaves it. The reference average (RC.y) is where both agree.
    const float metered = clamp(avg, RA.z, RA.w);
    const float camera = RB.y / (pow(RC.y, 1.0f - RC.x) * pow(metered, RC.x));
    const float exposure = (RA.x < 0.5f ? lum.y / avg : camera) * exp2(RA.y);
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
    if (RB.w > 1.5f) { return float4(lum.x * 0.25f, lum.y * 0.25f, exposure * 0.25f, 1.0f); }   // view 2: adapted average, the game's target, exposure (over 4, no gamma)
    if (RB.w > 0.5f) { return float4(exposure.xxx * 0.25f, 1.0f); }   // view 1: the exposure (4 = white, no gamma)
    // The game's grade, lane for lane as the game computes it (alpha included; its anti-aliasing reads it).
    const float g = dot(mapped, W);
    float4 c = float4(mapped, 1.0f);
    c = P[3].x * (c - g) + g;
    c = P[4].w * (g * P[4] - c) + c;
    c = P[3].z * (P[3].w * c - pivot) + pivot;
    c.rgb = pow(saturate(c.rgb), F[42].x);
    return c;
}
