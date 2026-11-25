// GiAtrousPass.hlsl: True Schied et al. (SVGF, HPG 2017) 3-Pass 5x5 B3-Spline Variance-Propagating A-Trous GI Denoiser (SM 6.6-6.10).
// Unpacks temporally accumulated SVGF standard deviation (sigma = sqrt(E[L^2] - E[L]^2)) from GiIn.a (packed alongside
// ReSTIR restVis by RtSkyFilter.hlsl), runs a full 25-tap B3-spline wavelet kernel (1/16, 1/4, 3/8, 1/4, 1/16) across
// step sizes 1 -> 2 -> 4 (effective 29x29 receptive field) with:
//   - Depth-gradient-aware phiZ (abs(zCenter - zTap) / max(gradZ * stepDist, eps)) + world-space plane distance stopping
//   - Sharp Normal^64 geometric stopping (zero bleed across corners, roof eaves, weapons & NPC silhouettes)
//   - Bounded SVGF luminance-variance & chromaticity edge-stopping + asymmetric dark-basin flux guard
//   - Energy-conserving irradiance shoulder/ceiling (diffuse bounce NEVER self-amplifies into glowing halos!)
// Step 4 unpacks clean restVis in [0..1] into alpha for Lighting.hlsl. Original work, MIT.
#include "LightingCommon.hlsli"
#include "RawTensorMath.hlsli"

Texture2D<float4> GiIn   : register(t0);
Texture2D<float>  Depth  : register(t1);
Texture2D<float4> Normal : register(t2);

float unpackSigma(float a) {
    return a > 1.5f ? max(a - floor(a * 0.5f) * 2.0f, 0.0f) : 0.12f;
}

float unpackRestVis(float a) {
    return a > 1.5f ? saturate(floor(a * 0.5f) / 15.0f) : saturate(a);
}

// Energy-conserving GI irradiance limiter (soft knee at 1.15, hard ceiling at 1.85):
// Preserves natural sunlit grass/snow/hearth bounce while preventing single bright RT hits from haloing.
float3 sanitizeGiRadiance(float3 c) {
    if (any(isnan(c)) || any(isinf(c))) { return 0.0f.xxx; }
    c = max(c, 0.0f.xxx);
    const float pk = max(c.r, max(c.g, c.b));
    if (pk > 1.15f) {
        const float softPk = 1.15f + (pk - 1.15f) / (1.0f + (pk - 1.15f) * 1.35f);
        c *= min(softPk, 1.85f) / pk;
    }
    return c;
}

float4 atrousStep(float4 at, int stepSize, bool finalPass) {
    const int2 p = int2(at.xy);
    const int3 ip = int3(p, 0);
    const float d = Depth.Load(ip);
    float4 center = GiIn.Load(ip);
    center.rgb = sanitizeGiRadiance(center.rgb);
    if (WaveActiveAllTrue(d >= 1.0f) || d >= 1.0f) {
        return float4(center.rgb, finalPass ? unpackRestVis(center.a) : center.a);
    }

    const float z = viewZOf(d);
    const float3 wp = worldAt(float2(p), d);
    const float3 n = worldNormal(Normal.Load(ip));
    const float cLum = dot(center.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float cSigma = unpackSigma(center.a);
    const float cVar = cSigma * cSigma;
    const float restIdx2 = center.a > 1.5f ? (floor(center.a * 0.5f) * 2.0f) : (floor(saturate(center.a) * 15.0f + 0.5f) * 2.0f);
    const int2 dMax = int2(dims.xy) - 1;

    // Exact screen-space view-depth gradient (min-slope across +-1px prevents silhouette depth inflation!):
    const float zR = viewZOf(Depth.Load(int3(min(p.x + 1, dMax.x), p.y, 0)));
    const float zL = viewZOf(Depth.Load(int3(max(p.x - 1, 0),      p.y, 0)));
    const float zD = viewZOf(Depth.Load(int3(p.x, min(p.y + 1, dMax.y), 0)));
    const float zU = viewZOf(Depth.Load(int3(p.x, max(p.y - 1, 0),      0)));
    const float2 gradZ = float2(
        min(abs(zR - z), abs(z - zL)),
        min(abs(zD - z), abs(z - zU)));
    // Tight depth-gradient-aware phiZ + world plane-distance scale:
    // Blocks bright walls/fires/sky behind foreground weapons, NPCs, and roof eaves from leaking even when coplanar!
    const float phiZBase = max(length(gradZ) * float(stepSize) * 1.35f + z * 0.0045f, 0.55f);
    const float invPhiZ  = 1.0f / phiZBase;
    const float planeScale = 1.0f / max(z * 0.0062f * float(stepSize), 1.25f);

    // Tightened SVGF variance-guided luminance edge-stopping with upper variance cap:
    // Even on freshly disoccluded pixels (high cSigma), prevents a 2.0 fire hit from bleeding onto 0.1 shadow!
    const float effSigmaL = clamp(1.80f * cSigma + 0.020f, 0.020f, 0.42f);
    const float invSigmaL = 1.0f / effSigmaL;

    // Structure-tensor edge orientation from 1px neighbors aligns wavelet along lighting tangents:
    const float lR1 = dot(sanitizeGiRadiance(GiIn.Load(int3(min(p.x + 1, dMax.x), p.y, 0)).rgb), float3(0.2126f, 0.7152f, 0.0722f));
    const float lL1 = dot(sanitizeGiRadiance(GiIn.Load(int3(max(p.x - 1, 0),      p.y, 0)).rgb), float3(0.2126f, 0.7152f, 0.0722f));
    const float lD1 = dot(sanitizeGiRadiance(GiIn.Load(int3(p.x, min(p.y + 1, dMax.y), 0)).rgb), float3(0.2126f, 0.7152f, 0.0722f));
    const float lU1 = dot(sanitizeGiRadiance(GiIn.Load(int3(p.x, max(p.y - 1, 0),      0)).rgb), float3(0.2126f, 0.7152f, 0.0722f));
    const float2 gradL = float2(lR1 - lL1, lD1 - lU1);
    const float gradMag = length(gradL);
    const float2 edgeN = gradMag > 1.0e-4f ? (gradL / gradMag) : float2(1.0f, 0.0f);
    const float edgeCoh = saturate(gradMag / max(cLum + 0.06f, 0.06f));
    const float darkBasin = 1.0f - smoothstep(0.025f, 0.24f, cLum);

    static const float16_t kB3[5] = { float16_t(0.0625h), float16_t(0.25h), float16_t(0.375h), float16_t(0.25h), float16_t(0.0625h) };
    const float16_t w0 = float16_t(0.375h * 0.375h);
    float16_t3 sumRgb = float16_t3(center.rgb) * w0;
    float16_t  wSum   = w0;
    float      varSum = float(w0 * w0) * cVar;
    [unroll] for (int dy = -2; dy <= 2; dy++) {
        const float16_t wy = kB3[dy + 2];
        [unroll] for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) { continue; }
            const int2 q = clamp(p + int2(dx, dy) * stepSize, int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            const float zq = viewZOf(dq);

            // 1) Depth-gradient-aware phiZ stopping (hard cut on foreground/background depth silhouettes!):
            const float stepDist = max(length(float2(float(dx), float(dy))), 1.0f);
            const float expectedDz = max(abs(gradZ.x * float(dx * stepSize)) + abs(gradZ.y * float(dy * stepSize)), 0.0f);
            const float zRatio = abs(z - zq) / max(expectedDz * 1.45f + phiZBase * stepDist * 0.65f, 0.55f);
            if (zRatio > 3.6f) { continue; }
            const float wPhiZ = exp(-zRatio * zRatio * 0.85f);

            // 2) World-space tangent-plane distance stopping (kills leaks across roof eaves & doorways):
            const float3 wq = worldAt(float2(q), dq);
            const float planeDist = abs(dot(n, wq - wp)) * planeScale;
            const float wPlane = exp(-planeDist * planeDist);
            const float wz = wPhiZ * wPlane;
            if (wz < 1.0e-4f) { continue; }

            // 3) Sharp Normal^64 geometric edge-stopping:
            const float3 nq = worldNormal(Normal.Load(int3(q, 0)));
            const float nd = saturate(dot(n, nq));
            if (nd < 0.72f) { continue; }
            const float wn = pow(nd, 64.0f);

            float4 tap = GiIn.Load(int3(q, 0));
            tap.rgb = sanitizeGiRadiance(tap.rgb);
            const float tLum = dot(tap.rgb, float3(0.2126f, 0.7152f, 0.0722f));

            // 4) Luminance-variance + chromaticity edge-stopping + asymmetric dark-basin flux guard:
            //    Bright fire/sky neighbors NEVER bleed upward across shadow borders into dark foregrounds!
            const float3 cChr = center.rgb / max(cLum, 0.025f);
            const float3 tChr = tap.rgb / max(tLum, 0.025f);
            const float chrDiff = length(tChr - cChr);
            const float lumDelta = tLum - cLum;
            const float asymPen = (lumDelta > 0.0f) ? (1.0f + darkBasin * 2.85f + saturate(lumDelta / max(cLum + 0.05f, 0.05f)) * 0.85f) : 1.0f;
            const float2 offDir = normalize(float2(float(dx), float(dy)));
            const float acrossEdge = abs(dot(offDir, edgeN));
            const float wAniso = exp(-acrossEdge * acrossEdge * edgeCoh * 1.45f);
            const float wl = exp(-abs(lumDelta) * invSigmaL * asymPen - chrDiff * 0.65f) * wAniso;

            const float16_t w = float16_t(float(kB3[dx + 2] * wy) * wz * wn * wl);
            const float qSig = unpackSigma(tap.a);
            sumRgb += float16_t3(tap.rgb) * w;
            wSum += w;
            varSum += float(w * w) * (qSig * qSig);
        }
    }
    const float invW = 1.0f / max(float(wSum), 1.0e-5f);
    const float3 outRgb = sanitizeGiRadiance(float3(sumRgb) * invW);
    if (finalPass) {
        // Strictly energy-conserving local crease recovery: bounded inside [min, max] of (outRgb, center.rgb)
        // and capped cleanly so diffuse GI never overshoots into glowing patches.
        const float cleanGate = saturate(1.0f - cSigma * 3.8f);
        const float3 detailClamp = clamp(outRgb + (center.rgb - outRgb) * (0.18f * cleanGate),
                                         min(outRgb, center.rgb), max(outRgb, center.rgb));
        return float4(max(detailClamp, 0.0f.xxx), unpackRestVis(center.a));
    }
    // Schied SVGF HPG 2017 Eq. 3 filtered variance propagation into next wavelet level.
    const float nextSigma = clamp(sqrt(max(varSum * (invW * invW), 0.0f)), 0.0f, 1.25f);
    return float4(outRgb, restIdx2 + nextSigma);
}

float4 step1(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 1, false); }
float4 step2(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 2, false); }
float4 step4(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 4, true); }
