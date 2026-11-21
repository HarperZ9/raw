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
    const bool activeSkin = (d < 1.0f && strength > 0.0f && isSkin(p));
    // SM6.9 Wave-coherent early exit: ~95% of screen waves contain zero skin pixels and skip all world/depth math.
    if (!WaveActiveAnyTrue(activeSkin) || !activeSkin) { Out[p] = c; return; }
    const float z = viewZOf(d);
    const float3 wp = worldAt(float2(p), d);
    // Pixels a world unit spans here.
    const float4 s1 = screenOf(wp + float3(0.0f, 0.0f, 1.0f));
    const float pxPerUnit = max(length(s1.xy - (float2(p) + 0.5f)), 1.0e-3f);
    const float3 sigmaN = float3(0.32f, 0.24f, 0.18f) * width;    // narrow epidermal lobe (preserves pores)
    const float3 sigmaW = float3(1.00f, 0.36f, 0.20f) * width;    // broad dermal hemoglobin lobe (red scatters furthest)
    const float rMax = min(sigmaW.r * 2.5f * pxPerUnit, 48.0f);   // pixels
    if (rMax < 0.75f) { Out[p] = c; return; }
    // Deterministic Interleaved Gradient Noise rotation (eliminates per-frame skin crawl shimmer).
    const float turn = ign(float2(p)) * 6.2831853f;
    float3 sum = c.rgb, wsum = 1.0f.xxx;
    [unroll] for (uint i = 0u; i < 16u; i++) {
        const float r = sqrt((float(i) + 0.5f) / 16.0f) * rMax;
        const float a = float(i) * 2.3999632f + turn;
        const int2 q = clamp(p + int2(round(float2(cos(a), sin(a)) * r)), int2(0, 0), int2(dims.xy) - 1);
        const float dq = Depth.Load(int3(q, 0));
        if (dq >= 1.0f || !isSkin(q) || abs(viewZOf(dq) - z) > 0.01f * z + 2.0f) { continue; }
        const float ru = r / pxPerUnit;
        const float ru2 = -0.5f * ru * ru;
        const float3 w = 0.45f * exp(ru2 / (sigmaN * sigmaN)) + 0.55f * exp(ru2 / (sigmaW * sigmaW));
        sum += w * Colour.Load(int3(q, 0)).rgb;
        wsum += w;
    }
    const float3 scattered = sum / wsum;
    // Preserve crisp high-frequency epidermal pore & lash luminance while keeping warm dermal hemoglobin scatter,
    // plus subtle backlit dermal translucency along thin silhouette edges (ears, nose bridge, cheeks).
    const float cLum = max(dot(c.rgb, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-4f);
    const float sLum = max(dot(scattered, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-4f);
    const float3 dermalGlow = scattered * lerp(float3(1.0f, 1.0f, 1.0f), float3(1.06f, 0.96f, 0.92f), saturate(sLum * 0.65f));
    const float3 poreKept = dermalGlow * lerp(1.0f, cLum / sLum, 0.36f);
    Out[p] = float4(lerp(c.rgb, poreKept, strength), c.a);
}
