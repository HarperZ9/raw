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
    const float3 sigmaN = float3(0.24f, 0.18f, 0.13f) * width;    // narrow epidermal lobe (preserves crisp pores)
    const float3 sigmaW = float3(0.72f, 0.26f, 0.14f) * width;    // tight dermal hemoglobin lobe (zero firelight smear!)
    const float rMax = min(sigmaW.r * 1.8f * pxPerUnit, 10.0f);   // capped at 10px so pores, eyebrows & creases stay sharp
    if (rMax < 0.75f) { Out[p] = c; return; }
    const float cLum0 = max(dot(c.rgb, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-4f);
    // Deterministic Interleaved Gradient Noise rotation (eliminates per-frame skin crawl shimmer).
    const float turn = ign(float2(p)) * 6.2831853f;
    float3 sum = c.rgb, wsum = 1.0f.xxx;
    float thinEdgeCount = 0.0f;
    // Edge- and luminance-preserving 24-tap Golden-Angle Biophysical Epidermal/Dermal SSS:
    [unroll] for (uint i = 0u; i < 24u; i++) {
        const float r = sqrt((float(i) + 0.5f) / 24.0f) * rMax;
        const float a = float(i) * 2.3999632f + turn;
        const int2 q = clamp(p + int2(round(float2(cos(a), sin(a)) * r)), int2(0, 0), int2(dims.xy) - 1);
        const float dq = Depth.Load(int3(q, 0));
        if (dq >= 1.0f || !isSkin(q) || abs(viewZOf(dq) - z) > 0.008f * z + 1.4f) {
            thinEdgeCount += 1.0f;
            continue;
        }
        const float3 cq = Colour.Load(int3(q, 0)).rgb;
        const float lq = max(dot(cq, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-4f);
        // Bilateral luminance edge stop: prevents bright firelight/specular peaks from bleeding over nostrils, pores & brows!
        const float relL = abs(lq - cLum0) / max(max(lq, cLum0), 0.08f);
        const float lStop = exp(-relL * relL * 4.5f);
        const float ru = r / pxPerUnit;
        const float ru2 = -0.5f * ru * ru;
        const float3 w = (0.45f * exp(ru2 / (sigmaN * sigmaN)) + 0.55f * exp(ru2 / (sigmaW * sigmaW))) * lStop;
        sum += w * cq;
        wsum += w;
    }
    const float3 scattered = sum / wsum;
    const float thinFrac = saturate((thinEdgeCount - 5.0f) / 16.0f);
    const float localThick = lerp(3.6f * width, 0.45f * width, thinFrac);
    const float3 tau = localThick / max(sigmaW, 0.05f.xxx);
    const float3 reflDeficit = exp(-2.0f * tau);
    const float3 thinTransmit = exp(-tau) * float3(1.12f, 0.38f, 0.16f) * (thinFrac * 0.16f);
    const float cLum = cLum0;
    const float sLum = max(dot(scattered, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-4f);
    const float melanin = saturate(1.0f - pow(saturate(cLum * 1.35f), 0.55f));
    const float hemoglobin = saturate((c.r - c.g * 0.86f) / max(cLum, 0.05f));
    const float3 fairTint = float3(1.05f, 0.96f, 0.92f);
    const float3 richTint = float3(1.03f, 0.98f, 0.94f);
    const float3 bioTint = lerp(fairTint, richTint, melanin * 0.75f);
    const float bioWeight = saturate(0.28f + hemoglobin * 0.38f) * (1.0f - melanin * 0.30f);
    const float3 dermalGlow = scattered * (1.0f.xxx - reflDeficit * 0.08f) * lerp(1.0f.xxx, bioTint, bioWeight * saturate(sLum * 0.75f))
                            + c.rgb * thinTransmit;
    // Preserve 92% of original high-frequency luminance detail (pores, freckles, stubble, wrinkles) and damp SSS
    // on bright firelight/specular highlights so torches & hearths NEVER blur or damage skin detail!
    const float3 poreKept = dermalGlow * lerp(1.0f, cLum / sLum, 0.92f);
    const float hlDamp = 1.0f - smoothstep(0.42f, 1.25f, cLum) * 0.72f;
    const float effStr = min(strength, 0.45f) * hlDamp;
    Out[p] = float4(lerp(c.rgb, poreKept, effStr), c.a);
}
