// Sss.hlsl: light scattered under the skin, in screen space (SKN-08; RtWater.cpp records it after the water).
// On every pixel of material class 10 (skin): a disc of 16 taps on a golden-angle spiral, turned per pixel and
// frame, sized in world units at the pixel's depth so the blur keeps its width as the face moves away. Each
// channel has its own Gaussian width (red scatters furthest, blue least), so shadow edges and pores soften to a
// red glow while the blue detail stays. A tap counts only on skin at the same depth (no bleeding onto the hair
// or the wall behind). The taps' spread is the game's own lit colour, so the result is energy preserving.
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float>  Depth  : register(t0);
Texture2D<float4> Spec   : register(t1);
Texture2D<float4> Colour : register(t2);
RWTexture2D<float4> Out  : register(u0);
cbuffer S : register(b1) { float width, strength, frameNo, sPad; };

bool isSkin(int2 q) { return (uint(Spec.Load(int3(q, 0)).z * 255.0f + 0.5f) & 127u) == 10u; }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int2 p = int2(id.xy);
    const float4 c = Colour.Load(int3(p, 0));
    const float d = Depth.Load(int3(p, 0));
    if (d >= 1.0f || strength <= 0.0f || !isSkin(p)) { Out[p] = c; return; }
    const float z = viewZOf(d);
    const float3 wp = worldAt(float2(p), d);
    // Pixels a world unit spans here.
    const float4 s1 = screenOf(wp + float3(0.0f, 0.0f, 1.0f));
    const float pxPerUnit = max(length(s1.xy - (float2(p) + 0.5f)), 1.0e-3f);
    const float3 sigma = float3(1.0f, 0.42f, 0.22f) * width;      // world units: red scatters furthest
    const float rMax = min(sigma.r * 2.5f * pxPerUnit, 48.0f);   // pixels
    if (rMax < 0.75f) { Out[p] = c; return; }
    const float turn = frac(sin(dot(float2(p), float2(12.9898f, 78.233f)) + frameNo * 0.618f) * 43758.5453f) * 6.2831853f;
    float3 sum = c.rgb, wsum = 1.0f.xxx;
    [unroll] for (uint i = 0u; i < 16u; i++) {
        const float r = sqrt((float(i) + 0.5f) / 16.0f) * rMax;
        const float a = float(i) * 2.3999632f + turn;
        const int2 q = clamp(p + int2(round(float2(cos(a), sin(a)) * r)), int2(0, 0), int2(dims.xy) - 1);
        const float dq = Depth.Load(int3(q, 0));
        if (dq >= 1.0f || !isSkin(q) || abs(viewZOf(dq) - z) > 0.01f * z + 2.0f) { continue; }
        const float ru = r / pxPerUnit;
        // Each tap stands for an annulus of equal area, so the Gaussian weight alone shapes the profile.
        const float3 w = exp(-0.5f * ru * ru / (sigma * sigma));
        sum += w * Colour.Load(int3(q, 0)).rgb;
        wsum += w;
    }
    Out[p] = float4(lerp(c.rgb, sum / wsum, strength), c.a);
}
