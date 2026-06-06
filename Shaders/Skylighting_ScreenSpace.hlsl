// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Skylighting CS — Screen-space upper-hemisphere sky visibility.
//
// For each pixel, estimate what fraction of the upper hemisphere is open
// to the sky (not blocked by nearby geometry).  We march outward along N
// evenly-spaced 2D directions on the screen plane.  At each step we
// reconstruct view-space positions and test whether the sample occludes
// the pixel's sky view by comparing elevation angles against the surface
// normal.  Directions that escape the screen or hit a sky pixel count as
// visible.  The output is a single scalar: 0 = fully occluded, 1 = full
// sky exposure.
//
// Depth is read from HiZ (reversed-Z: near=1, far=0).
// Sky check: rawDepth < 0.0001 (reversed-Z: sky=0).
// NDC -> pixel: multiply by ScreenDims.x * 0.5.
// LinearDepth at t31 gives view-space Z in game units.

cbuffer SkylightCB : register(b0)
{
    float4x4 ProjMatrix;       // Row-major projection matrix
    uint2    ScreenDims;       // Full-res screen dimensions
    float    NearZ;            // Near clip plane (game units)
    float    FarZ;             // Far clip plane  (game units)
    float    SampleRadius;     // World-space hemisphere sample radius
    float    Intensity;        // Output multiplier (applied once)
    int      NumDirections;    // Directions to sample (2-12)
    int      NumSteps;         // Steps per direction  (2-16)
    uint     FrameIndex;       // For temporal jitter rotation
    float    FPDepthThreshold; // First-person depth cutoff (~16 units)
    float2   pad0;
};

Texture2D<float>  DepthTex    : register(t0);   // HiZ mip 0 (reversed-Z)
Texture2D<float4> BlueNoise   : register(t30);  // 128x128 R2 quasi-random
Texture2D<float>  LinearDepth : register(t31);  // Pre-computed view-space Z

RWTexture2D<float> SkyOutput  : register(u0);   // Full-res raw sky visibility

// ─── Constants ───────────────────────────────────────────────────────────

static const float PI           = 3.14159265359;
static const float TWO_PI       = 6.28318530718;
static const float GOLDEN_RATIO = 0.6180339887;  // R2 temporal advance

// Horizon angle threshold: ignore elevations below this to avoid
// self-occlusion from coplanar geometry and depth precision noise.
static const float MIN_HORIZON_COS = 0.08;

// Fall-off: occluders near the edge of the radius contribute less.
// This avoids hard cut-off artifacts at the radius boundary.
static const float FALLOFF_POWER = 1.5;

// ─── Helpers ─────────────────────────────────────────────────────────────

// Reconstruct view-space position from UV + linear depth.
float3 UVToViewPos(float2 uv, float linearZ)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    return float3(ndc.x * linearZ / ProjMatrix[0][0],
                  ndc.y * linearZ / ProjMatrix[1][1],
                  linearZ);
}

// Reconstruct view-space normal from depth cross-product.
// Uses the best of two possible triangles (left/right, up/down) to
// minimise edge artifacts at depth discontinuities.
float3 ReconstructNormal(int2 coord, float2 texelSize)
{
    float dC = LinearDepth.Load(int3(coord, 0));
    float dL = LinearDepth.Load(int3(coord + int2(-1,  0), 0));
    float dR = LinearDepth.Load(int3(coord + int2( 1,  0), 0));
    float dU = LinearDepth.Load(int3(coord + int2( 0, -1), 0));
    float dD = LinearDepth.Load(int3(coord + int2( 0,  1), 0));

    // Pick the neighbour pair with the smallest depth discontinuity.
    float2 uvC = (float2(coord) + 0.5) * texelSize;

    float3 posC = UVToViewPos(uvC, dC);

    // Horizontal: prefer whichever neighbour is closer in depth.
    bool useLeft = abs(dL - dC) < abs(dR - dC);
    float2 uvH  = uvC + float2(useLeft ? -texelSize.x : texelSize.x, 0.0);
    float3 posH = UVToViewPos(uvH, useLeft ? dL : dR);

    // Vertical: prefer whichever neighbour is closer in depth.
    bool useUp  = abs(dU - dC) < abs(dD - dC);
    float2 uvV  = uvC + float2(0.0, useUp ? -texelSize.y : texelSize.y);
    float3 posV = UVToViewPos(uvV, useUp ? dU : dD);

    float3 edgeH = posH - posC;
    float3 edgeV = posV - posC;

    // Cross product order depends on which neighbours we picked to keep
    // the normal pointing toward the camera (negative Z in view space).
    float3 n;
    if (useLeft == useUp)
        n = cross(edgeV, edgeH);
    else
        n = cross(edgeH, edgeV);

    return normalize(n);
}

// ─── Main ────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2  coord     = int2(DTid.xy);
    float rawDepth  = DepthTex.Load(int3(coord, 0));

    // Sky pixels (reversed-Z: sky=0) are trivially fully sky-visible.
    if (rawDepth < 0.0001)
    {
        SkyOutput[coord] = 1.0;
        return;
    }

    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv        = (float2(coord) + 0.5) * texelSize;
    float  linearZ   = LinearDepth.Load(int3(coord, 0));

    // Skip first-person geometry (arms, weapons).
    if (linearZ < FPDepthThreshold)
    {
        SkyOutput[coord] = 1.0;
        return;
    }

    float3 viewPos    = UVToViewPos(uv, linearZ);
    float3 viewNormal = ReconstructNormal(coord, texelSize);

    // ── Project world-space radius to pixel-space ────────────────────
    // ProjMatrix[0][0] is focal-length / aspect.  Dividing by viewZ
    // gives NDC radius; multiplying by half-screen-width gives pixels.
    float pixelRadius = SampleRadius * ProjMatrix[0][0] / linearZ
                        * float(ScreenDims.x) * 0.5;
    pixelRadius = clamp(pixelRadius, 3.0, float(ScreenDims.y) * 0.5);

    float stepLen = pixelRadius / float(NumSteps);

    // ── Per-pixel temporal jitter via blue noise ─────────────────────
    float4 bn = BlueNoise.Load(int3(int2(DTid.xy) % 128, 0));
    float  rotAngle = frac(bn.x + float(FrameIndex) * GOLDEN_RATIO) * TWO_PI;
    float  stepJitter = frac(bn.y + float(FrameIndex) * GOLDEN_RATIO);

    // ── Direction loop ───────────────────────────────────────────────
    float visibleCount = 0.0;

    for (int d = 0; d < NumDirections; d++)
    {
        float angle = TWO_PI * (float(d) + 0.5) / float(NumDirections) + rotAngle;
        float2 dir  = float2(cos(angle), sin(angle));

        // Track the maximum horizon angle found along this direction.
        // If any sample subtends a large enough angle above the surface
        // normal, this direction is blocked.
        float maxHorizonCos = MIN_HORIZON_COS;
        bool  blocked       = false;

        for (int s = 1; s <= NumSteps; s++)
        {
            // Jittered step offset — first step starts at (0.5 + jitter)
            // to break banding between pixels.
            float t = (float(s) - 0.5 + stepJitter) * stepLen;
            int2  sampleCoord = coord + int2(dir * t);

            // Off-screen: this direction escapes to sky.
            if (any(sampleCoord < 0) || sampleCoord.x >= int(ScreenDims.x) ||
                sampleCoord.y >= int(ScreenDims.y))
                break;

            float sampleRawDepth = DepthTex.Load(int3(sampleCoord, 0));

            // Sky pixel (reversed-Z: sky=0): this direction reaches the sky — not blocked.
            if (sampleRawDepth < 0.0001)
                break;

            float  sampleLinZ = LinearDepth.Load(int3(sampleCoord, 0));
            float2 sampleUV   = (float2(sampleCoord) + 0.5) * texelSize;
            float3 samplePos  = UVToViewPos(sampleUV, sampleLinZ);

            float3 toSample = samplePos - viewPos;
            float  dist     = length(toSample);

            // Ignore samples outside the world-space radius.
            if (dist > SampleRadius)
                continue;

            // Elevation cosine: how far "above" us (in normal direction)
            // the sample is.  Positive means the sample is in our upper
            // hemisphere and could block sky.
            float elevCos = dot(toSample, viewNormal) / dist;

            // Distance attenuation: near occluders matter more.
            float distAtten = 1.0 - pow(saturate(dist / SampleRadius), FALLOFF_POWER);

            // Effective horizon: combine elevation with distance weight.
            float effectiveHorizon = elevCos * distAtten;

            if (effectiveHorizon > maxHorizonCos)
            {
                maxHorizonCos = effectiveHorizon;

                // If the horizon angle is steep enough, this direction
                // is blocked by geometry (not sky-visible).
                if (maxHorizonCos > 0.3)
                {
                    blocked = true;
                    break;
                }
            }
        }

        if (!blocked)
            visibleCount += 1.0;
    }

    // ── Final sky visibility ─────────────────────────────────────────
    float vis = visibleCount / max(float(NumDirections), 1.0);
    vis = saturate(vis * Intensity);

    SkyOutput[coord] = vis;
}
