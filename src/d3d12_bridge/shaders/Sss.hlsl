// Sss.hlsl: Wave 51 Christensen-Burley (Disney 2015) Normalized RGB Subsurface Diffusion.
// Evaluates exact analytical Christensen-Burley normalized diffusion R(r, d) = (exp(-r/d) + exp(-r/(3d))) / (8*pi*d*r)
// across 24 golden-angle Vogel-disk taps with physically distinct RGB mean-free-path radii d_rgb per material class:
//   - Human/NPC Skin (class 10)   : warm hemoglobin-red scatter   d_rgb = (1.00, 0.37, 0.22)
//   - Snow & Glacial Ice (1/2/4)  : cool sapphire-cyan scatter    d_rgb = (0.45, 0.78, 1.00)
//   - Foliage / Pine Needles (5)  : warm chartreuse-emerald glow  d_rgb = (0.48, 0.92, 0.34)
// Enforces strict 3D plane-normal + depth-discontinuity + class + luminance + chromaticity bilateral guards
// and zero-NaN energy normalization so pores, eyelashes, hairlines, eyes & armor edges stay razor-sharp!
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float>  Depth  : register(t0);
Texture2D<float4> Spec   : register(t1);
Texture2D<float4> Colour : register(t2);
RWTexture2D<float4> Out  : register(u0);
cbuffer S : register(b1) { float width, strength, frameNo, sPad; };

static const uint SSS_KIND_NONE    = 0u;
static const uint SSS_KIND_SKIN    = 1u;
static const uint SSS_KIND_SNOWICE = 2u;
static const uint SSS_KIND_FOLIAGE = 3u;

uint rawMatClassAt(int2 q) {
    return uint(Spec.Load(int3(q, 0)).z * 255.0f + 0.5f) & 127u;
}

float3 sssSanitizeRgb(float3 rgb) {
    return (any(isnan(rgb)) || any(isinf(rgb))) ? 0.0f.xxx : clamp(rgb, 0.0f.xxx, 16.0f.xxx);
}

// Classify pixel subsurface diffusion family from G-buffer material class + linear RGB optics:
uint classifySssKind(int2 q, float3 rgb, float viewZ) {
    const uint m = rawMatClassAt(q);
    // Strictly exclude Skinned Actor / Creature / Fur / Clothing (class 14):
    if (m == 14u) { return SSS_KIND_NONE; }
    if (m == 10u) { return SSS_KIND_SKIN; }
    if (m == 5u && viewZ < 1400.0f) { return SSS_KIND_FOLIAGE; }
    if ((m == 1u || m == 2u || m == 4u) && viewZ < 1400.0f) {
        const float cMin = min(rgb.r, min(rgb.g, rgb.b));
        const float cMax = max(rgb.r, max(rgb.g, rgb.b));
        // Neutral bright snow or blue-cyan glacial ice (excludes stone, bark, metal & dark dirt):
        const bool isSnow = (cMin > 0.24f && (cMax - cMin) < 0.14f);
        const bool isIce  = (rgb.b > rgb.r * 1.12f && rgb.b > 0.16f && rgb.g >= rgb.r * 0.98f);
        if (isSnow || isIce) { return SSS_KIND_SNOWICE; }
    }
    return SSS_KIND_NONE;
}

// Christensen & Burley (2015, Disney) Analytical Normalized Diffusion Profile:
//   R(r, d) = (exp(-r / d) + exp(-r / (3 * d))) / (8 * pi * d * r)
float3 evalChristensenBurleyR(float r, float3 dRgb) {
    const float3 dSafe = max(dRgb, 0.04f.xxx);
    const float rSafe = max(r, 0.06f);
    return (exp(-rSafe / dSafe) + exp(-rSafe / (3.0f * dSafe))) / (8.0f * PI * dSafe * rSafe);
}

// Reconstruct local geometric surface normal directly from Depth (zero extra SRVs; catches sharp 3D corners!):
float3 reconstructDepthNormal(int2 p, float dCenter, float3 wpCenter, int2 dimMax) {
    const int2 pR = min(p + int2(1, 0), dimMax);
    const int2 pL = max(p - int2(1, 0), int2(0, 0));
    const int2 pD = min(p + int2(0, 1), dimMax);
    const int2 pU = max(p - int2(0, 1), int2(0, 0));
    const float dR = Depth.Load(int3(pR, 0));
    const float dL = Depth.Load(int3(pL, 0));
    const float dD = Depth.Load(int3(pD, 0));
    const float dU = Depth.Load(int3(pU, 0));
    const float3 dx = (abs(dR - dCenter) < abs(dCenter - dL) && dR < 0.99998f)
                    ? (worldAt(float2(pR), dR) - wpCenter)
                    : ((dL < 0.99998f) ? (wpCenter - worldAt(float2(pL), dL)) : float3(1.0f, 0.0f, 0.0f));
    const float3 dy = (abs(dD - dCenter) < abs(dCenter - dU) && dD < 0.99998f)
                    ? (worldAt(float2(pD), dD) - wpCenter)
                    : ((dU < 0.99998f) ? (wpCenter - worldAt(float2(pU), dU)) : float3(0.0f, 1.0f, 0.0f));
    const float3 nRaw = cross(dx, dy);
    const float len2 = dot(nRaw, nRaw);
    return (len2 > 1.0e-10f) ? (nRaw * rsqrt(len2)) : float3(0.0f, 0.0f, 1.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int2 p = int2(id.xy);
    const float4 cRaw = Colour.Load(int3(p, 0));
    const float3 cRgb = sssSanitizeRgb(cRaw.rgb);
    const float safeAlpha = isfinite(cRaw.a) ? saturate(cRaw.a) : 1.0f;
    const float safeStr = isfinite(strength) ? saturate(strength) : 0.0f;
    const float d = Depth.Load(int3(p, 0));
    // Standard-Z sky guard (d >= 0.99998 untouched!) + wave-coherent early exit:
    const bool validDepth = (isfinite(d) && d > 0.0f && d < 0.99998f && safeStr > 0.0f);
    const float z = validDepth ? max(viewZOf(d), 0.1f) : 1.0e6f;
    const uint kind = validDepth ? classifySssKind(p, cRgb, z) : SSS_KIND_NONE;
    const bool activeSss = (kind != SSS_KIND_NONE);
    if (!WaveActiveAnyTrue(activeSss) || !activeSss) { Out[p] = float4(cRgb, safeAlpha); return; }

    const int2 dimMax = max(int2(dims.xy) - 1, int2(0, 0));
    const float3 wp = worldAt(float2(p), d);
    const float4 s1 = screenOf(wp + float3(0.0f, 0.0f, 1.0f));
    const float pxPerUnit = max(length(s1.xy - (float2(p) + 0.5f)), 1.0e-3f);
    const float effWidth = isfinite(width) ? clamp(width, 0.08f, 6.0f) : 1.0f;

    // Physically distinct Christensen-Burley RGB mean-free-path radii d_rgb per material family:
    const float3 dRgb = (kind == SSS_KIND_SKIN)    ? (float3(1.00f, 0.37f, 0.22f) * (effWidth * 0.58f))
                      : (kind == SSS_KIND_SNOWICE) ? (float3(0.45f, 0.78f, 1.00f) * (effWidth * 0.52f))
                                                   : (float3(0.48f, 0.92f, 0.34f) * (effWidth * 0.42f));
    const float dMaxCh = max(dRgb.r, max(dRgb.g, dRgb.b));
    const float rMaxPx = clamp(dMaxCh * 2.15f * pxPerUnit, 0.0f, (kind == SSS_KIND_SKIN) ? 9.0f : 6.5f);
    if (rMaxPx < 0.75f) { Out[p] = cRaw; return; }

    const float3 nGeo = reconstructDepthNormal(p, d, wp, dimMax);
    const float3 lumaW = float3(0.2126f, 0.7152f, 0.0722f);
    const float cLum0 = max(dot(cRgb, lumaW), 1.0e-4f);
    const float3 cChroma0 = cRgb / max(cLum0, 0.03f);
    const uint centerClass = rawMatClassAt(p);

    // 4-neighbor micro-variance & class-boundary detector: preserves pores, eyelashes, hairlines, eyes & armor borders!
    float localMinL = cLum0;
    float localMaxL = cLum0;
    float microEdge = 0.0f;
    static const int2 kMicro4[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
    [unroll] for (int mi = 0; mi < 4; ++mi) {
        const int2 mq = clamp(p + kMicro4[mi], int2(0, 0), dimMax);
        const uint mCls = rawMatClassAt(mq);
        if (mCls == centerClass) {
            const float3 mc = sssSanitizeRgb(Colour.Load(int3(mq, 0)).rgb);
            const float ml = max(dot(mc, lumaW), 1.0e-4f);
            localMinL = min(localMinL, ml);
            localMaxL = max(localMaxL, ml);
            const float3 mChroma = mc / max(ml, 0.03f);
            microEdge = max(microEdge, length(mChroma - cChroma0));
        } else {
            microEdge = max(microEdge, 0.72f);
        }
    }
    const float localVar = saturate((localMaxL - localMinL) / max(localMaxL, 0.06f));
    const float detailProtect = saturate(max(smoothstep(0.14f, 0.48f, localVar), smoothstep(0.18f, 0.55f, microEdge)));

    // Deterministic Interleaved Gradient Noise rotation (zero temporal crawl shimmer):
    const float turn = ign(float2(p)) * 6.2831853f;
    // Center self-weight anchored to Christensen-Burley core energy:
    const float3 wCenter = evalChristensenBurleyR(0.06f, dRgb) * 0.38f;
    float3 sum = cRgb * wCenter;
    float3 wsum = wCenter;
    float thinEdgeCount = 0.0f;

    // 24-Tap Golden-Angle Vogel Disk with Christensen-Burley R(r, d_rgb) + Depth/Plane-Normal/Class/Chroma Guards:
    const float depthTol = (kind == SSS_KIND_SKIN) ? (0.0060f * z + 1.10f) : (0.012f * z + 2.20f);
    const float planeScale = (kind == SSS_KIND_SKIN) ? 1.85f : 0.75f;
    [unroll] for (uint i = 0u; i < 24u; i++) {
        const float rPx = sqrt((float(i) + 0.5f) / 24.0f) * rMaxPx;
        const float a = float(i) * 2.3999632f + turn;
        const int2 q = clamp(p + int2(round(float2(cos(a), sin(a)) * rPx)), int2(0, 0), dimMax);
        const float dq = Depth.Load(int3(q, 0));
        if (dq <= 0.0f || dq >= 0.99998f || rawMatClassAt(q) != centerClass) {
            thinEdgeCount += 1.0f;
            continue;
        }
        const float zq = viewZOf(dq);
        if (abs(zq - z) > depthTol) {
            thinEdgeCount += 1.0f;
            continue;
        }
        // 3D geometric tangent-plane & normal-edge bilateral guard (prevents bleed across nostrils/eyelids/armor creases):
        const float3 wpQ = worldAt(float2(q), dq);
        const float planeDist = abs(dot(wpQ - wp, nGeo));
        const float normalStop = exp(-planeDist * planeDist * planeScale);
        if (normalStop < 0.02f) {
            thinEdgeCount += 1.0f;
            continue;
        }

        const float3 cq = sssSanitizeRgb(Colour.Load(int3(q, 0)).rgb);
        const float lq = max(dot(cq, lumaW), 1.0e-4f);
        const float relL = abs(lq - cLum0) / max(max(lq, cLum0), 0.07f);
        // Wave 63: Allow dermal hemoglobin scatter to bridge facial shadow terminators smoothly on SKIN
        // while 3D tangent-plane normalStop + depthTol + class boundary keep nostrils, eyelids & hairlines razor-sharp!
        const float lDecay = (kind == SSS_KIND_SKIN) ? lerp(2.4f, 7.5f, detailProtect) : lerp(5.5f, 11.5f, detailProtect);
        const float cDecay = (kind == SSS_KIND_SKIN) ? lerp(12.0f, 28.0f, detailProtect) : lerp(18.0f, 36.0f, detailProtect);
        const float lStop = exp(-relL * relL * lDecay);
        const float3 qChroma = cq / max(lq, 0.03f);
        const float3 dChroma = qChroma - cChroma0;
        const float cStop = exp(-dot(dChroma, dChroma) * cDecay);

        const float rWorld = max(rPx / pxPerUnit, 0.04f);
        const float3 wCB = evalChristensenBurleyR(rWorld, dRgb) * (normalStop * lStop * cStop);
        sum += wCB * cq;
        wsum += wCB;
    }

    const float3 scattered = sum / max(wsum, 1.0e-5f.xxx);
    const float sLum = max(dot(scattered, lumaW), 1.0e-4f);
    const float lumRatioSafe = clamp(cLum0 / sLum, 0.72f, 1.38f);
    const float distFade = 1.0f - smoothstep(750.0f, 1400.0f, z);

    if (kind == SSS_KIND_SKIN) {
        const float thinFrac = saturate((thinEdgeCount - 5.0f) / 16.0f);
        const float localThick = lerp(3.4f * effWidth, 0.45f * effWidth, thinFrac);
        const float3 tau = localThick / max(dRgb, 0.05f.xxx);
        const float3 reflDeficit = exp(-2.0f * tau);
        const float3 thinTransmit = exp(-tau) * float3(1.12f, 0.36f, 0.14f) * (thinFrac * 0.14f);

        const float melanin = saturate(1.0f - pow(saturate(cLum0 * 1.35f), 0.55f));
        const float hemoglobin = saturate((cRgb.r - cRgb.g * 0.86f) / max(cLum0, 0.05f));
        const float3 bioTint = lerp(float3(1.05f, 0.96f, 0.92f), float3(1.03f, 0.98f, 0.94f), melanin * 0.75f);
        const float bioWeight = saturate(0.26f + hemoglobin * 0.36f) * (1.0f - melanin * 0.30f);
        const float3 dermalGlow = scattered * (1.0f.xxx - reflDeficit * 0.08f) * lerp(1.0f.xxx, bioTint, bioWeight * saturate(sLum * 0.75f))
                                + cRgb * thinTransmit;

        // Preserve high-frequency facial micro-detail (pores, freckles, stubble, eyelashes) while letting
        // bounded subsurface luminance soften blotchy low-poly facial shadow terminators cleanly!
        const float3 poreKept = dermalGlow * lerp(1.0f, lumRatioSafe, lerp(0.84f, 0.96f, detailProtect));
        const float hlDamp = 1.0f - smoothstep(0.34f, 1.05f, cLum0) * 0.80f;
        const float varDamp = 1.0f - detailProtect * 0.38f;
        const float effStr = min(safeStr, 0.48f) * hlDamp * varDamp;
        Out[p] = float4(sssSanitizeRgb(lerp(cRgb, poreKept, effStr)), safeAlpha);
    } else if (kind == SSS_KIND_SNOWICE) {
        // Cool sapphire-cyan subsurface transmission on snowbanks & glacial ice (preserves sharp ridge detail!):
        const float3 iceTint = lerp(1.0f.xxx, float3(0.92f, 0.99f, 1.06f), saturate(sLum * 0.65f));
        const float3 snowKept = (scattered * iceTint) * lerp(1.0f, lumRatioSafe, 0.88f);
        const float effStr = min(safeStr, 0.32f) * (1.0f - detailProtect * 0.55f) * distFade;
        Out[p] = float4(sssSanitizeRgb(lerp(cRgb, snowKept, effStr)), safeAlpha);
    } else {
        // Restrained warm chartreuse-emerald foliage transmission (never bleaches sunlit conifer needles!):
        const float3 leafTint = float3(0.96f, 1.05f, 0.88f);
        const float3 leafKept = (scattered * leafTint) * lerp(1.0f, lumRatioSafe, 0.92f);
        const float hlLeafDamp = 1.0f - smoothstep(0.22f, 0.75f, cLum0) * 0.85f;
        const float effStr = min(safeStr, 0.22f) * hlLeafDamp * (1.0f - detailProtect * 0.50f) * distFade;
        Out[p] = float4(sssSanitizeRgb(lerp(cRgb, leafKept, effStr)), safeAlpha);
    }
}
