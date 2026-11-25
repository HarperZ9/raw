// Sss.hlsl: High-Detail Biophysical Dual-Lobe Skin Subsurface Scattering (SKN-08 / Wave 40).
// On every pixel of material class 10 (skin): a 24-tap golden-angle spiral disc with Jimenez dual-lobe
// epidermal/dermal Gaussian weights, strict depth + luminance + chromaticity + local micro-variance edge
// stops so intense campfire/torch light NEVER washes out pores, freckles, wrinkles, eyelashes, or stubble,
// while preserving warm hemoglobin dermal scattering right across shadow terminators.
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
    const float4 s1 = screenOf(wp + float3(0.0f, 0.0f, 1.0f));
    const float pxPerUnit = max(length(s1.xy - (float2(p) + 0.5f)), 1.0e-3f);

    // Refined Jimenez dual-lobe biophysical skin scattering widths:
    //   sigmaN: tight epidermal lobe keeping green/blue pores, stubble & fine wrinkles razor sharp
    //   sigmaW: crimson hemoglobin dermal lobe (red scatters furthest across shadow terminators)
    const float3 sigmaN = float3(0.20f, 0.14f, 0.10f) * width;
    const float3 sigmaW = float3(0.68f, 0.22f, 0.12f) * width;
    const float3 wNarrow = float3(0.32f, 0.56f, 0.74f);
    const float3 wWide   = 1.0f.xxx - wNarrow;
    const float rMax = min(sigmaW.r * 1.75f * pxPerUnit, 9.0f);
    if (rMax < 0.75f) { Out[p] = c; return; }

    const int2 dimMax = int2(dims.xy) - 1;
    const float3 lumaW = float3(0.2126f, 0.7152f, 0.0722f);
    const float cLum0 = max(dot(c.rgb, lumaW), 1.0e-4f);
    const float3 cChroma0 = c.rgb / max(cLum0, 0.03f);

    // 4-neighbor micro-variance estimator: detects pores, freckles, stubble, eyelashes & lip borders
    // so high-frequency facial micro-relief stays crisp even under intense hearth/torch firelight!
    float localMinL = cLum0;
    float localMaxL = cLum0;
    float microEdge = 0.0f;
    static const int2 kMicro4[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
    [unroll] for (int mi = 0; mi < 4; ++mi) {
        const int2 mq = clamp(p + kMicro4[mi], int2(0, 0), dimMax);
        if (isSkin(mq)) {
            const float3 mc = Colour.Load(int3(mq, 0)).rgb;
            const float ml = max(dot(mc, lumaW), 1.0e-4f);
            localMinL = min(localMinL, ml);
            localMaxL = max(localMaxL, ml);
            const float3 mChroma = mc / max(ml, 0.03f);
            microEdge = max(microEdge, length(mChroma - cChroma0));
        } else {
            microEdge = max(microEdge, 0.65f);
        }
    }
    const float localVar = saturate((localMaxL - localMinL) / max(localMaxL, 0.06f));
    const float detailProtect = saturate(max(smoothstep(0.14f, 0.48f, localVar), smoothstep(0.18f, 0.55f, microEdge)));

    // Deterministic Interleaved Gradient Noise rotation (zero temporal skin-crawl shimmer):
    const float turn = ign(float2(p)) * 6.2831853f;
    float3 sum = c.rgb;
    float3 wsum = 1.0f.xxx;
    float thinEdgeCount = 0.0f;

    // Edge-, depth-, luminance-, and chromaticity-stopping 24-tap Jimenez Dual-Lobe SSS:
    [unroll] for (uint i = 0u; i < 24u; i++) {
        const float r = sqrt((float(i) + 0.5f) / 24.0f) * rMax;
        const float a = float(i) * 2.3999632f + turn;
        const int2 q = clamp(p + int2(round(float2(cos(a), sin(a)) * r)), int2(0, 0), dimMax);
        const float dq = Depth.Load(int3(q, 0));
        if (dq >= 1.0f || !isSkin(q) || abs(viewZOf(dq) - z) > 0.0065f * z + 1.15f) {
            thinEdgeCount += 1.0f;
            continue;
        }
        const float3 cq = Colour.Load(int3(q, 0)).rgb;
        const float lq = max(dot(cq, lumaW), 1.0e-4f);
        // Strict bilateral luminance & chromaticity edge stops:
        // prevents intense campfire/torch peaks from bleeding across nostrils, eyebrows, freckles & eyelashes!
        const float relL = abs(lq - cLum0) / max(max(lq, cLum0), 0.07f);
        const float lStop = exp(-relL * relL * lerp(5.5f, 11.0f, detailProtect));
        const float3 qChroma = cq / max(lq, 0.03f);
        const float3 dChroma = qChroma - cChroma0;
        const float cStop = exp(-dot(dChroma, dChroma) * lerp(18.0f, 36.0f, detailProtect));

        const float ru = r / pxPerUnit;
        const float ru2 = -0.5f * ru * ru;
        const float3 w = (wNarrow * exp(ru2 / (sigmaN * sigmaN)) + wWide * exp(ru2 / (sigmaW * sigmaW))) * (lStop * cStop);
        sum += w * cq;
        wsum += w;
    }

    const float3 scattered = sum / wsum;
    const float thinFrac = saturate((thinEdgeCount - 5.0f) / 16.0f);
    const float localThick = lerp(3.6f * width, 0.45f * width, thinFrac);
    const float3 tau = localThick / max(sigmaW, 0.05f.xxx);
    const float3 reflDeficit = exp(-2.0f * tau);
    const float3 thinTransmit = exp(-tau) * float3(1.12f, 0.36f, 0.14f) * (thinFrac * 0.14f);

    const float cLum = cLum0;
    const float sLum = max(dot(scattered, lumaW), 1.0e-4f);
    const float melanin = saturate(1.0f - pow(saturate(cLum * 1.35f), 0.55f));
    const float hemoglobin = saturate((c.r - c.g * 0.86f) / max(cLum, 0.05f));
    const float3 fairTint = float3(1.05f, 0.96f, 0.92f);
    const float3 richTint = float3(1.03f, 0.98f, 0.94f);
    const float3 bioTint = lerp(fairTint, richTint, melanin * 0.75f);
    const float bioWeight = saturate(0.26f + hemoglobin * 0.36f) * (1.0f - melanin * 0.30f);
    const float3 dermalGlow = scattered * (1.0f.xxx - reflDeficit * 0.08f) * lerp(1.0f.xxx, bioTint, bioWeight * saturate(sLum * 0.75f))
                            + c.rgb * thinTransmit;

    // Preserve 95% of high-frequency luminance micro-detail (pores, freckles, wrinkles, stubble, eyelashes)
    // and damp SSS strongly on intense campfire/torch highlights while keeping warm dermal terminator glow!
    const float3 poreKept = dermalGlow * lerp(1.0f, cLum / sLum, lerp(0.94f, 0.985f, detailProtect));
    const float hlDamp = 1.0f - smoothstep(0.34f, 1.05f, cLum) * 0.80f;
    const float varDamp = 1.0f - detailProtect * 0.42f;
    const float effStr = min(strength, 0.45f) * hlDamp * varDamp;
    Out[p] = float4(lerp(c.rgb, poreKept, effStr), c.a);
}
