// RtSkyFilter.hlsl: one sky ray a pixel, made usable (RtSky.cpp). Per pixel: a depth-aware mean of
// the 3 x 3 raw rays around it, blended into the history found along the game's motion vector
// (bilinear, each tap only at the pixel's own view depth), up to 32 frames. While the history is
// short the mean widens to 5 x 5. Out: the light-index output's alpha (the lighting pass's sky
// visibility), the filtered bounce (RtSky.h) and the next histories. Original work, MIT.
#define ATLAS_SLOT t9
#include "LightingCommon.hlsli"

Texture2D<float>  Depth  : register(t0);
Texture2D<float2> Motion : register(t2);
RWTexture2D<float4>      Raw     : register(u0);   // bounce rgb, sky a
RWTexture2D<float4>      HistIn  : register(u1);   // sky, view depth, frames
RWTexture2D<float4>      HistOut : register(u2);
RWTexture2D<unorm float4> Lights : register(u3);
RWTexture2D<float4>      BIn     : register(u4);   // bounce rgb
RWTexture2D<float4>      BOut    : register(u5);
RWTexture2D<float4>      Bounce  : register(u6);   // the lighting pass's input
cbuffer F : register(b1) { uint hasMotion, reset, maxFrames, fPad; };

float4 meanAround(int2 p, float z, int r) {
    float4 s = 0.0f;
    float w = 0.0f;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            const int2 q = clamp(p + int2(x, y), int2(0, 0), int2(dims.xy) - 1);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            const float rel = (viewZOf(dq) - z) / z;
            const float k = exp2(-rel * rel * 4000.0f);
            s += k * Raw[q]; w += k;
        }
    }
    return w > 1.0e-4f ? s / w : Raw[p];
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int2 p = int2(id.xy);
    const float d = Depth.Load(int3(p, 0));
    const float4 L = Lights[p];
    if (d >= 1.0f) {
        HistOut[p] = float4(1.0f, -1.0f, 0.0f, 0.0f);
        BOut[p] = 0.0f;
        Bounce[p] = 0.0f;
        Lights[p] = float4(L.xyz, 1.0f);
        return;
    }
    const float z = viewZOf(d);
    float n = 0.0f, past = 0.0f;
    float3 pastB = 0.0f;
    bool have = false;
    if (reset == 0u) {
        const float2 mv = hasMotion != 0u ? Motion.Load(int3(p, 0)) : float2(0.0f, 0.0f);
        const float2 prev = float2(p) + mv * dims.xy;
        const int2 pb = int2(floor(prev));
        const float2 pa = prev - float2(pb);
        float hs = 0.0f, hn = 0.0f, hw = 0.0f;
        float3 hb = 0.0f;
        [unroll] for (int k = 0; k < 4; k++) {
            const int2 t = pb + int2(k & 1, k >> 1);
            if (any(t < 0) || t.x >= int(dims.x) || t.y >= int(dims.y)) { continue; }
            const float4 h = HistIn[t];
            if (h.y <= 0.0f || abs(h.y - min(z, 65000.0f)) > 0.04f * z) { continue; }
            const float w = ((k & 1) ? pa.x : 1.0f - pa.x) * ((k >> 1) ? pa.y : 1.0f - pa.y);
            hs += w * h.x; hn += w * h.z; hw += w; hb += w * BIn[t].rgb;
        }
        if (hw > 0.05f) { past = hs / hw; n = hn / hw; pastB = hb / hw; have = true; }
    }
    const float4 cur = meanAround(p, z, n < 6.0f ? 2 : 1);
    n = have ? min(n + 1.0f, float(maxFrames)) : 1.0f;
    const float vis = have ? lerp(past, cur.a, 1.0f / n) : cur.a;
    const float3 b = have ? lerp(pastB, cur.rgb, 1.0f / n) : cur.rgb;
    HistOut[p] = float4(vis, min(z, 65000.0f), n, 0.0f);
    BOut[p] = float4(b, 0.0f);
    Bounce[p] = float4(b, 1.0f);
    Lights[p] = float4(L.xyz, vis);
}
