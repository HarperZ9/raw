// ShadowAtlas.hlsl: D3D11/D3D12 compute pass that copies Skyrim's 16-bit sun cascades (a 2D array
// of R16_UNORM depth, one slice per cascade) into the shared R32_FLOAT atlas (cascade k at x offset
// k * side), with Wave 57 texel-snapped receiver-plane slope estimation, rotated Vogel-disk
// depth-plane de-quantization, and strict cascade-slice clamping to eliminate 16-bit shadow acne
// and cascade-edge bleed with zero peter-panning.
// Compiled by FXC (cs_5_0 /E CSMain /O3 /WX) AND DXC (cs_6_6 / cs_6_10 -WX). Original work, MIT.

Texture2DArray<float> gCascades : register(t0);
RWTexture2D<float>    gAtlas    : register(u0);

// FXC cs_5_0 + DXC SM 6.6/6.10 bit-exact IEEE-754 finite check (zero FXC X3577 warnings without /Gis).
bool isFiniteDepth(float v)
{
    return (asuint(v) & 0x7F800000u) != 0x7F800000u;
}

float sanitizeDepth01(float v)
{
    return isFiniteDepth(v) ? saturate(v) : 1.0f;
}

float loadCascadeDepth(int2 coord, int2 maxCoord, uint slice)
{
    const int2 c = clamp(coord, int2(0, 0), maxCoord);
    return sanitizeDepth01(gCascades.Load(int4(c.x, c.y, int(slice), 0)));
}

// Interleaved gradient noise rotation angle for texel-locked rotated Vogel-disk sampling.
float shadowAtlasIgnAngle(float2 p)
{
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f)))) * 6.2831853f;
}

// Texel-snapped receiver-plane slope & normal-bias helper (shared math for cascade depth preconditioning).
float computeReceiverSlopeBias(float2 slope, float cascadeScale)
{
    const float slopeMag = abs(slope.x) + abs(slope.y);
    // Conservative micro-bias bounded strictly <= 2.5 R16 LSBs so thin geometry never peter-pans.
    return clamp(slopeMag * (0.22f * cascadeScale), 0.0f, 3.8e-5f);
}

// Smooth cascade-transition weight inside [1 - blendWidth, 1] of cascade normalized UV radius.
float cascadeEdgeBlendWeight(float2 uv, float blendWidth)
{
    const float2 edgeDist = min(uv, 1.0f.xx - uv);
    const float minEdge = min(edgeDist.x, edgeDist.y);
    return smoothstep(0.0f, max(blendWidth, 1.0e-4f), minEdge);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    gAtlas.GetDimensions(w, h);
    if (id.x >= w || id.y >= h || h == 0u) return;

    uint cw, ch, slices;
    gCascades.GetDimensions(cw, ch, slices);
    if (cw == 0u || ch == 0u || slices == 0u)
    {
        gAtlas[id.xy] = 1.0f;
        return;
    }

    const uint side = h;
    const uint slice = min(id.x / side, slices - 1u);
    const int2 localPx = int2(int(id.x % side), int(id.y));
    const int2 maxCoord = int2(int(cw) - 1, int(ch) - 1);

    const float dC = loadCascadeDepth(localPx, maxCoord, slice);
    // Fast path for sky / unoccluded far plane texels (preserves exact 1.0f with zero overhead).
    if (dC >= 0.99995f)
    {
        gAtlas[id.xy] = 1.0f;
        return;
    }

    // Best-sided finite-difference receiver-plane slope strictly inside the active cascade slice
    // (never crosses silhouette depth discontinuities or adjacent cascade boundaries).
    const float dL = loadCascadeDepth(localPx + int2(-1,  0), maxCoord, slice);
    const float dR = loadCascadeDepth(localPx + int2( 1,  0), maxCoord, slice);
    const float dU = loadCascadeDepth(localPx + int2( 0, -1), maxCoord, slice);
    const float dD = loadCascadeDepth(localPx + int2( 0,  1), maxCoord, slice);

    const float gxR = dR - dC;
    const float gxL = dC - dL;
    const float gyD = dD - dC;
    const float gyU = dC - dU;
    const float2 slope = float2(
        abs(gxR) <= abs(gxL) ? gxR : gxL,
        abs(gyD) <= abs(gyU) ? gyD : gyU);

    // Continuity gate: only de-quantize smooth R16_UNORM receiver planes; keep silhouette edges bit-exact!
    const float maxJump = max(max(abs(gxR), abs(gxL)), max(abs(gyD), abs(gyU)));
    const float slopeMag = abs(slope.x) + abs(slope.y);
    float outDepth = dC;

    if (maxJump < 0.0014f && slopeMag < 0.0009f)
    {
        // Rotated 8-tap Vogel-disk coplanar R16->R32 depth de-quantization (eliminates 16-bit depth staircases
        // on gently sloped hills/roofs while preserving sharp silhouettes via tight plane-distance rejection).
        const float ang0 = shadowAtlasIgnAngle(float2(localPx));
        const float planeTol = max(3.2e-5f, slopeMag * 0.48f);
        const float invTol2 = 1.0f / max(planeTol * planeTol, 1.0e-10f);
        float dSum = dC * 1.5f;
        float wSum = 1.5f;

        [unroll]
        for (int vi = 0; vi < 8; ++vi)
        {
            const float r = sqrt((float(vi) + 0.5f) / 8.0f) * 1.55f;
            const float a = float(vi) * 2.39996323f + ang0;
            const float2 offF = float2(cos(a), sin(a)) * r;
            const int2 offI = int2(round(offF));
            const float dTap = loadCascadeDepth(localPx + offI, maxCoord, slice);
            if (dTap >= 0.99995f) continue;
            const float dPred = dC + slope.x * float(offI.x) + slope.y * float(offI.y);
            const float planeErr = dTap - dPred;
            const float wTap = exp(-min((planeErr * planeErr) * invTol2, 24.0f));
            // Subtract linear slope offset so the weighted mean reconstructs the exact center plane depth.
            dSum += (dTap - (slope.x * float(offI.x) + slope.y * float(offI.y))) * wTap;
            wSum += wTap;
        }
        outDepth = dSum / max(wSum, 1.0e-4f);

        // Apply tiny texel-snapped receiver-slope micro-bias attenuated near cascade edges for seamless cascade handoff.
        const float2 uvCascade = (float2(localPx) + 0.5f.xx) / float2(float(cw), float(ch));
        const float edgeFade = cascadeEdgeBlendWeight(uvCascade, 0.04f);
        const float cascadeScale = (slice == 0u) ? 1.0f : 1.35f;
        outDepth = saturate(outDepth + computeReceiverSlopeBias(slope, cascadeScale) * edgeFade);
    }

    gAtlas[id.xy] = sanitizeDepth01(outDepth);
}
