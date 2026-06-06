// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Contact Shadow Denoise CS — 5x5 bilateral spatial filter.
//
// Smooths the ray-marched shadow mask while preserving sharp edges at depth
// discontinuities.  Uses separable Gaussian spatial weights with an
// exponential depth edge-stopping function (same pattern as GTAO denoise).

cbuffer DenoiseCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthThreshold;     // Edge-stopping sensitivity (relative depth diff)
    float3 pad0;
};

// ── Resources ────────────────────────────────────────────────────────────
Texture2D<float>   ShadowInput  : register(t0);   // Raw contact shadow mask
Texture2D<float>   DepthTex     : register(t1);   // Depth buffer (unused, kept for binding compat)
Texture2D<float>   LinearDepth  : register(t31);  // Pre-computed view-space Z

RWTexture2D<float> ShadowOutput : register(u0);   // Denoised shadow mask

// ── Gaussian kernel (5x5 separable) ─────────────────────────────────────
// Offsets: 0 -> center, ±1, ±2.  Weights: sigma ~ 1.0
static const float kGauss[3] = { 0.375, 0.25, 0.0625 };

float SpatialWeight(int offset)
{
    return kGauss[abs(offset)];
}

// ── Main ─────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float centerZ      = LinearDepth.Load(int3(coord, 0));
    float centerShadow = ShadowInput.Load(int3(coord, 0));

    float accumShadow = 0.0;
    float accumWeight = 0.0;

    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 tap = clamp(coord + int2(dx, dy),
                             int2(0, 0),
                             int2(ScreenDims) - 1);

            float tapZ      = LinearDepth.Load(int3(tap, 0));
            float tapShadow = ShadowInput.Load(int3(tap, 0));

            // Spatial: separable Gaussian
            float ws = SpatialWeight(dx) * SpatialWeight(dy);

            // Depth edge stopping: relative depth difference → exponential falloff.
            // Large depth jumps (object silhouettes) get near-zero weight so the
            // filter does not blur shadow across depth discontinuities.
            float relDiff = abs(tapZ - centerZ) / max(centerZ, 0.01);
            float wd      = exp(-relDiff / max(DepthThreshold, 0.001));

            float w = ws * wd;
            accumShadow += tapShadow * w;
            accumWeight += w;
        }
    }

    ShadowOutput[coord] = accumShadow / max(accumWeight, 1e-6);
}
