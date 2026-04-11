//=============================================================================
//  DoFRenderer.cpp --- Compute-First Physical Depth-of-Field
//
//  Dispatch flow (per frame, PreUI stage, priority 30):
//    1. CopyResource scene RT (or backbuffer at Present-time) -> m_backbufferCopy
//    2. Dispatch Autofocus CS (1x1): weighted depth grid -> focus distance
//    3. Dispatch CoC + Tile CS (full-res 8x8): thin-lens CoC + 16x16 tile min/max
//    4. Dispatch Far Gather CS (half-res 8x8): N-gon shaped bokeh gather
//    5. Dispatch Near Gather CS (half-res 8x8): near-field bokeh gather
//    6. Composite PS (fullscreen): bilateral upsample, blend, CA, dither -> scene RT
//
//  Algorithm:
//    Physical thin-lens model.  Autofocus samples a 10x10 grid of depth values
//    around screen center, rejects outliers via median + variance, and temporally
//    smooths the result.  CoC is computed per pixel from the thin-lens equation.
//    Tiles classify regions as near/far/in-focus for early-out.  Bokeh gathering
//    uses an N-gon aperture SDF with cat-eye vignette and spherical aberration.
//    Final composite adds longitudinal CA and optional focus peaking.
//=============================================================================

#include "DoFRenderer.h"
#include "HiZPyramid.h"
#include "D3D11Hook.h"
#include "SceneData.h"
#include "SharedGPUResources.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>


namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL --- Pass 1: Autofocus CS (1x1 dispatch)
//
//  Samples a 10x10 weighted grid of depth values around screen center.
//  Sorts, finds median, rejects samples >2-sigma from median, averages
//  the survivors.  Temporally smooths the result for stable focus.
// ═══════════════════════════════════════════════════════════════════════════

static const char kAutofocusCS[] = R"HLSL(
// Autofocus CS --- 10x10 depth grid, variance rejection, temporal smooth.
// Single thread group (1x1x1), single thread does all the work.

cbuffer AutofocusCB : register(b0)
{
    float4x4 ProjMatrix;       // Projection matrix (row-major)
    float2   ScreenDims;       // Full-res pixel dimensions
    float    NearZ;
    float    FarZ;
    float    FocusSpeed;       // Temporal smoothing speed
    float    DeltaTime;        // Frame delta time
    float    ManualFocusDist;  // >0 means override autofocus
    float    PrevFocusDist;    // Previous frame result
};

Texture2D<float> DepthTex : register(t0);
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth

RWStructuredBuffer<float> FocusOutput : register(u0);  // 1-element: focus distance

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Manual focus override
    if (ManualFocusDist > 0.0)
    {
        float prev = PrevFocusDist;
        float target = ManualFocusDist;
        float speed = saturate(FocusSpeed * DeltaTime);
        FocusOutput[0] = lerp(prev, target, speed);
        return;
    }

    // 10x10 weighted grid centered on screen
    static const int GRID = 10;
    float depths[GRID * GRID];
    float weights[GRID * GRID];
    int   count = 0;

    float2 center = ScreenDims * 0.5;
    float  gridSpacing = min(ScreenDims.x, ScreenDims.y) * 0.04;  // 4% of min dim

    for (int gy = 0; gy < GRID; gy++)
    {
        for (int gx = 0; gx < GRID; gx++)
        {
            float2 offset = float2(gx - (GRID - 1) * 0.5, gy - (GRID - 1) * 0.5);
            int2 coord = int2(center + offset * gridSpacing);
            coord = clamp(coord, int2(0, 0), int2(ScreenDims) - 1);

            float rawZ = DepthTex.Load(int3(coord, 0));
            if (rawZ < 0.0001) continue;  // skip sky

            float linearZ = LinearDepth.Load(int3(coord, 0));
            if (linearZ < NearZ * 1.1 || linearZ > FarZ * 0.9) continue;

            // Gaussian center weight: samples near center matter more
            float dist = length(offset) / (GRID * 0.5);
            float w = exp(-dist * dist * 2.0);

            depths[count]  = linearZ;
            weights[count] = w;
            count++;
        }
    }

    if (count < 3)
    {
        // Not enough valid samples -- keep previous focus
        FocusOutput[0] = PrevFocusDist;
        return;
    }

    // Simple insertion sort for median finding (100 elements max)
    for (int i = 1; i < count; i++)
    {
        float dKey = depths[i];
        float wKey = weights[i];
        int j = i - 1;
        while (j >= 0 && depths[j] > dKey)
        {
            depths[j + 1]  = depths[j];
            weights[j + 1] = weights[j];
            j--;
        }
        depths[j + 1]  = dKey;
        weights[j + 1] = wKey;
    }

    float median = depths[count / 2];

    // Compute variance for rejection
    float sumW = 0.0;
    float sumWD = 0.0;
    float sumWD2 = 0.0;

    for (int k = 0; k < count; k++)
    {
        sumW   += weights[k];
        sumWD  += weights[k] * depths[k];
        sumWD2 += weights[k] * depths[k] * depths[k];
    }

    float mean = sumWD / max(sumW, 0.0001);
    float variance = (sumWD2 / max(sumW, 0.0001)) - mean * mean;
    float sigma = sqrt(max(variance, 0.01));

    // Reject samples >2-sigma from median, then weighted average
    float finalW = 0.0;
    float finalD = 0.0;

    for (int m = 0; m < count; m++)
    {
        float dev = abs(depths[m] - median);
        if (dev > 2.0 * sigma) continue;

        finalW += weights[m];
        finalD += weights[m] * depths[m];
    }

    float targetFocus = (finalW > 0.0001) ? (finalD / finalW) : median;

    // Temporal smoothing
    float prev = PrevFocusDist;
    float speed = saturate(FocusSpeed * DeltaTime);
    float result = lerp(prev, targetFocus, speed);

    FocusOutput[0] = result;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL --- Pass 2: CoC + Tile Classification CS
//
//  Full-res dispatch (8x8 groups).  Computes signed CoC per pixel from
//  the thin-lens equation, then classifies 16x16 tiles via groupshared
//  min/max reduction for gather radius optimization.
// ═══════════════════════════════════════════════════════════════════════════

static const char kCoCTileCS[] = R"HLSL(
// CoC + Tile Classification CS
// Per-pixel thin-lens CoC, 16x16 tile min/max classification.

cbuffer CoCCB : register(b0)
{
    float4x4 ProjMatrix;
    uint2    ScreenDims;
    float    NearZ;
    float    FarZ;
    float    Aperture;        // f-number
    float    FocalLengthMM;   // Focal length in mm
    float    FocusDist;       // Focus distance (world units from autofocus)
    float    MaxBokehRadius;  // Max CoC in pixels
    uint2    TileCount;       // Number of 16x16 tiles (ceil)
    float2   pad0;
};

Texture2D<float> DepthTex : register(t0);
StructuredBuffer<float> FocusBuf : register(t1);  // 1-element from autofocus
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth

RWTexture2D<float2> CoCOutput  : register(u0);  // Full-res R16G16_FLOAT (signed CoC, abs CoC)
RWTexture2D<float2> TileOutput : register(u1);  // Per-tile R16G16_FLOAT (min CoC, max CoC)

float ComputeCoC(float depth, float focusDist, float aperture, float focalLengthM)
{
    // Thin-lens equation (simplified):
    // CoC = |focalLength^2 * (depth - focusDist)| / (fNumber * depth * (focusDist - focalLength))
    // Since focalLength << focusDist, simplify to:
    // CoC ~= apertureDiameter * |depth - focusDist| / depth
    //       = (focalLength / fNumber) * |depth - focusDist| / depth
    // Then convert from world-space size to pixel-space via projection.

    float apertureDiam = focalLengthM / aperture;  // aperture diameter in meters
    float coc = apertureDiam * (depth - focusDist) / max(depth, 0.001);

    // Convert world-space CoC to pixel-space:
    // pixel_coc = coc_world * (focalLength / sensorSize) * screenWidth / depth
    // Simplified via projection matrix: pixel_coc = coc_world * Proj[0][0] * screenWidth * 0.5 / depth
    float pixelCoC = coc * ProjMatrix[0][0] * ScreenDims.x * 0.5 / max(depth, 0.001);

    return clamp(pixelCoC, -MaxBokehRadius, MaxBokehRadius);
}

// Tile reduction via groupshared memory
// We use 16x16 thread groups; each thread contributes its CoC value.
groupshared float gs_coc[16 * 16];

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID,
          uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
    int2 coord = int2(DTid.xy);
    bool inBounds = (uint(coord.x) < ScreenDims.x && uint(coord.y) < ScreenDims.y);

    float signedCoC = 0.0;

    if (inBounds)
    {
        float rawZ = DepthTex.Load(int3(coord, 0));

        if (rawZ < 0.0001)
        {
            // Sky: max far blur
            signedCoC = MaxBokehRadius;
        }
        else
        {
            float linearZ = LinearDepth.Load(int3(coord, 0));
            float focusDist = FocusBuf[0];
            float focalLengthM = FocalLengthMM * 0.001;  // mm -> meters

            signedCoC = ComputeCoC(linearZ, focusDist, Aperture, focalLengthM);
        }

        CoCOutput[coord] = float2(signedCoC, abs(signedCoC));
    }

    // ── Tile classification: min/max CoC per 16x16 tile ────────────────
    gs_coc[GI] = inBounds ? signedCoC : 0.0;
    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction for min and max
    // We store the value and do the reduction manually
    // Min and max across all 256 threads
    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (GI < stride)
        {
            float a = gs_coc[GI];
            float b = gs_coc[GI + stride];
            // For min: store the smaller signed CoC (most negative = strongest near)
            // For max: store the larger signed CoC (most positive = strongest far)
            // We need both, so we do two passes... or pack them.
            // Alternative: use two groupshared arrays.
            // For simplicity, store min in first half reduction, max in second.
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Simpler approach: thread 0 does a scan (256 elements is fast)
    if (GI == 0)
    {
        float minCoC = gs_coc[0];
        float maxCoC = gs_coc[0];
        uint tileSize = min(16u * 16u, ScreenDims.x * ScreenDims.y);  // safety
        for (uint i = 1; i < 256; i++)
        {
            float c = gs_coc[i];
            minCoC = min(minCoC, c);
            maxCoC = max(maxCoC, c);
        }

        uint2 tileCoord = Gid.xy;
        if (tileCoord.x < TileCount.x && tileCoord.y < TileCount.y)
        {
            TileOutput[int2(tileCoord)] = float2(minCoC, maxCoC);
        }
    }
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL --- Pass 3: Far Field Bokeh Gather CS (half-res)
//
//  N-gon shaped gather with configurable blade count, roundness, cat-eye
//  vignette, and spherical aberration (ring bokeh).  Scatter-as-gather:
//  for each output pixel, gather from an area proportional to CoC.
// ═══════════════════════════════════════════════════════════════════════════

static const char kFarGatherCS[] = R"HLSL(
// Far Field Bokeh Gather CS --- N-gon shaped, half-resolution.

cbuffer FarGatherCB : register(b0)
{
    uint2  HalfDims;        // Half-res output dimensions
    uint2  FullDims;        // Full-res source dimensions
    float  MaxBokehRadius;  // Max CoC in pixels (full-res)
    int    BladeCount;      // N-gon blade count (4-9)
    float  Roundness;       // 0=polygon, 1=circle
    float  CatEyeAmount;   // Cat-eye optical vignette (0-1)
    float  AnamorphicRatio; // Horizontal stretch (0=none, 1=full)
    float  SphericalAberr;  // Ring bokeh weight (0=uniform, 1=ring)
    int    SampleCount;     // Samples per pixel (48-128)
    float  BokehRotation;   // Rotation angle (radians)
};

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy (full-res)
Texture2D<float2> CoCMap     : register(t1);  // Full-res CoC (.x=signed, .y=abs)
Texture2D<float2> TileMap    : register(t2);  // Tile min/max CoC

SamplerState LinearSamp : register(s0);

RWTexture2D<float4> FarOutput : register(u0);  // Half-res RGBA16F

static const float PI = 3.14159265359;
static const float GOLDEN_ANGLE = 2.39996322973;  // pi * (3 - sqrt(5))

// N-gon polygon SDF (distance from point to N-sided polygon boundary)
float PolygonSDF(float2 p, int N, float size)
{
    float a = atan2(p.y, p.x);
    float r = 2.0 * PI / float(N);
    float d = cos(floor(0.5 + a / r) * r - a) * length(p) - size;
    return d;
}

// Generate bokeh sample position with N-gon shaping
float2 BokehSample(int index, int totalSamples, int blades, float roundness,
                    float rotation, float anamorphic)
{
    // Fibonacci spiral distribution for even coverage
    float r = sqrt((float(index) + 0.5) / float(totalSamples));
    float theta = GOLDEN_ANGLE * float(index) + rotation;

    float2 p = float2(cos(theta), sin(theta)) * r;

    // Apply N-gon shaping via SDF rejection / remapping
    float polyDist = PolygonSDF(p, blades, 1.0);
    float circleDist = length(p) - 1.0;
    float shapeDist = lerp(polyDist, circleDist, roundness);

    // Remap point to lie within the shape
    // If outside shape, push it back in
    if (shapeDist > 0.0)
    {
        float scale = 1.0 / (1.0 + shapeDist * 2.0);
        p *= scale;
    }

    // Anamorphic horizontal stretch
    p.x *= 1.0 + anamorphic * 0.5;

    return p;
}

// Cat-eye vignette: off-axis optical vignette
float CatEyeWeight(float2 sampleOffset, float2 pixelUV, float amount)
{
    float2 fromCenter = pixelUV - 0.5;
    float distFromCenter = length(fromCenter) * 2.0;
    float catEye = 1.0 - smoothstep(0.5, 1.0, distFromCenter * amount);

    // Direction-aware: samples on the far side from center are cut
    float2 dir = normalize(fromCenter + 0.0001);
    float alignment = dot(normalize(sampleOffset + 0.0001), dir);
    float dirCut = lerp(1.0, saturate(1.0 - alignment * distFromCenter * amount), amount);

    return catEye * dirCut;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 halfCoord = int2(DTid.xy);
    float2 uv = (float2(halfCoord) + 0.5) / float2(HalfDims);

    // Sample CoC at center (from full-res map)
    int2 fullCoord = halfCoord * 2;
    float2 cocData = CoCMap.Load(int3(min(fullCoord, int2(FullDims) - 1), 0));
    float signedCoC = cocData.x;

    // Far field only: skip if CoC is negative (near) or near zero
    if (signedCoC < 1.0)
    {
        // In focus or near field -- pass through scene color
        float4 scene = SceneColor.Load(int3(min(fullCoord, int2(FullDims) - 1), 0));
        FarOutput[halfCoord] = scene;
        return;
    }

    // Radius in half-res pixels
    float radius = signedCoC * 0.5;  // half-res scale
    radius = min(radius, MaxBokehRadius * 0.5);

    if (radius < 0.5)
    {
        FarOutput[halfCoord] = SceneColor.Load(int3(min(fullCoord, int2(FullDims) - 1), 0));
        return;
    }

    float3 totalColor = float3(0, 0, 0);
    float  totalWeight = 0.0;

    for (int i = 0; i < SampleCount; i++)
    {
        float2 sampleOffset = BokehSample(i, SampleCount, BladeCount, Roundness,
                                           BokehRotation, AnamorphicRatio);

        // Scale by radius
        float2 offset = sampleOffset * radius;

        // Cat-eye vignette
        float catWeight = CatEyeWeight(sampleOffset, uv, CatEyeAmount);

        // Spherical aberration: weight samples at edges more (ring bokeh)
        float edgeDist = length(sampleOffset);
        float spherWeight = lerp(1.0, edgeDist, SphericalAberr);

        // Sample position in half-res
        int2 sampleCoord = halfCoord + int2(offset);
        sampleCoord = clamp(sampleCoord, int2(0, 0), int2(HalfDims) - 1);

        // Map back to full-res for color + CoC read
        int2 fullSampleCoord = sampleCoord * 2;
        fullSampleCoord = clamp(fullSampleCoord, int2(0, 0), int2(FullDims) - 1);

        float2 sampleCoCData = CoCMap.Load(int3(fullSampleCoord, 0));
        float sampleCoC = sampleCoCData.x;

        // Only gather from far-field samples (positive CoC)
        // Weight by how much the sample's CoC covers this pixel
        float cocWeight = saturate(sampleCoC / max(signedCoC, 0.01));

        float w = catWeight * spherWeight * cocWeight;
        if (w < 0.001) continue;

        float3 sampleColor = SceneColor.Load(int3(fullSampleCoord, 0)).rgb;

        // Reject NaN/inf
        if (any(isnan(sampleColor)) || any(isinf(sampleColor)))
            sampleColor = float3(0, 0, 0);

        totalColor += sampleColor * w;
        totalWeight += w;
    }

    float3 result = (totalWeight > 0.001) ? totalColor / totalWeight : float3(0, 0, 0);
    FarOutput[halfCoord] = float4(result, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL --- Pass 4: Near Field Bokeh Gather CS (half-res)
//
//  Same N-gon gathering but for negative CoC (near blur).  Near field
//  bleeds INTO focused areas using dilated near CoC from the tile buffer.
//  Alpha stores near-field contribution weight for compositing.
// ═══════════════════════════════════════════════════════════════════════════

static const char kNearGatherCS[] = R"HLSL(
// Near Field Bokeh Gather CS --- N-gon shaped, half-resolution.
// Near field bleeds into focused regions via tile-dilated CoC.

cbuffer NearGatherCB : register(b0)
{
    uint2  HalfDims;
    uint2  FullDims;
    float  MaxBokehRadius;
    int    BladeCount;
    float  Roundness;
    float  CatEyeAmount;
    float  AnamorphicRatio;
    float  SphericalAberr;
    int    SampleCount;
    float  BokehRotation;
};

Texture2D<float4> SceneColor : register(t0);
Texture2D<float2> CoCMap     : register(t1);
Texture2D<float2> TileMap    : register(t2);

SamplerState LinearSamp : register(s0);

RWTexture2D<float4> NearOutput : register(u0);  // Half-res RGBA16F (rgb=color, a=near weight)

static const float PI = 3.14159265359;
static const float GOLDEN_ANGLE = 2.39996322973;

float PolygonSDF(float2 p, int N, float size)
{
    float a = atan2(p.y, p.x);
    float r = 2.0 * PI / float(N);
    return cos(floor(0.5 + a / r) * r - a) * length(p) - size;
}

float2 BokehSample(int index, int totalSamples, int blades, float roundness,
                    float rotation, float anamorphic)
{
    float r = sqrt((float(index) + 0.5) / float(totalSamples));
    float theta = GOLDEN_ANGLE * float(index) + rotation;
    float2 p = float2(cos(theta), sin(theta)) * r;

    float polyDist = PolygonSDF(p, blades, 1.0);
    float circleDist = length(p) - 1.0;
    float shapeDist = lerp(polyDist, circleDist, roundness);

    if (shapeDist > 0.0)
    {
        float scale = 1.0 / (1.0 + shapeDist * 2.0);
        p *= scale;
    }

    p.x *= 1.0 + anamorphic * 0.5;
    return p;
}

float CatEyeWeight(float2 sampleOffset, float2 pixelUV, float amount)
{
    float2 fromCenter = pixelUV - 0.5;
    float distFromCenter = length(fromCenter) * 2.0;
    float catEye = 1.0 - smoothstep(0.5, 1.0, distFromCenter * amount);
    float2 dir = normalize(fromCenter + 0.0001);
    float alignment = dot(normalize(sampleOffset + 0.0001), dir);
    float dirCut = lerp(1.0, saturate(1.0 - alignment * distFromCenter * amount), amount);
    return catEye * dirCut;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 halfCoord = int2(DTid.xy);
    float2 uv = (float2(halfCoord) + 0.5) / float2(HalfDims);

    // Read center CoC
    int2 fullCoord = halfCoord * 2;
    float2 cocData = CoCMap.Load(int3(min(fullCoord, int2(FullDims) - 1), 0));
    float signedCoC = cocData.x;

    // Read tile to get dilated near CoC (minimum CoC in tile = most negative)
    uint2 tileCoord = DTid.xy / 8;  // tiles are 16x16 full-res = 8x8 half-res
    float2 tileData = TileMap.Load(int3(int2(tileCoord), 0));
    float tileMinCoC = tileData.x;  // most negative CoC in tile

    // Near field: we need to process if this pixel OR its tile has near blur
    // The dilated near CoC determines the gather radius for bleeding
    float nearCoC = min(signedCoC, 0.0);       // this pixel's near contribution
    float dilatedNearCoC = min(tileMinCoC, 0.0); // tile's strongest near CoC

    float gatherRadius = abs(dilatedNearCoC) * 0.5;  // half-res scale
    gatherRadius = min(gatherRadius, MaxBokehRadius * 0.5);

    // Skip if no near blur in this tile
    if (gatherRadius < 0.5)
    {
        NearOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    float3 totalColor = float3(0, 0, 0);
    float  totalWeight = 0.0;

    for (int i = 0; i < SampleCount; i++)
    {
        float2 sampleOffset = BokehSample(i, SampleCount, BladeCount, Roundness,
                                           BokehRotation, AnamorphicRatio);

        float2 offset = sampleOffset * gatherRadius;

        float catWeight = CatEyeWeight(sampleOffset, uv, CatEyeAmount);
        float edgeDist = length(sampleOffset);
        float spherWeight = lerp(1.0, edgeDist, SphericalAberr);

        int2 sampleCoord = halfCoord + int2(offset);
        sampleCoord = clamp(sampleCoord, int2(0, 0), int2(HalfDims) - 1);

        int2 fullSampleCoord = sampleCoord * 2;
        fullSampleCoord = clamp(fullSampleCoord, int2(0, 0), int2(FullDims) - 1);

        float2 sampleCoCData = CoCMap.Load(int3(fullSampleCoord, 0));
        float sampleCoC = sampleCoCData.x;

        // Near-field: only gather from near samples (negative CoC)
        // Bleed into focused areas: if the sample is near, it contributes
        float nearSampleWeight = saturate(-sampleCoC / max(abs(dilatedNearCoC), 0.01));

        float w = catWeight * spherWeight * nearSampleWeight;
        if (w < 0.001) continue;

        float3 sampleColor = SceneColor.Load(int3(fullSampleCoord, 0)).rgb;

        if (any(isnan(sampleColor)) || any(isinf(sampleColor)))
            sampleColor = float3(0, 0, 0);

        totalColor += sampleColor * w;
        totalWeight += w;
    }

    float3 result = (totalWeight > 0.001) ? totalColor / totalWeight : float3(0, 0, 0);

    // Alpha = near contribution weight: how much of this pixel should be near-blurred
    // Blend of own near CoC and dilated tile CoC
    float alpha = saturate(abs(nearCoC) / max(MaxBokehRadius * 0.25, 1.0));
    // Also include dilated contribution (bleeding from nearby near objects)
    alpha = max(alpha, saturate(totalWeight / max(float(SampleCount) * 0.3, 1.0)));

    NearOutput[halfCoord] = float4(result, alpha);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL --- Pass 5: Composite + Effects PS (fullscreen)
//
//  Bilateral upsample from half-res bokeh textures, blend far+near by
//  CoC sign, longitudinal chromatic aberration, anamorphic stretch,
//  focus peaking debug overlay, triangular dither.
// ═══════════════════════════════════════════════════════════════════════════

static const char kCompositePS[] = R"HLSL(
// DoF Composite PS --- bilateral upsample + blend + CA + focus peaking + dither.

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy (sharp)
Texture2D<float4> FarField   : register(t1);  // Half-res far bokeh
Texture2D<float4> NearField  : register(t2);  // Half-res near bokeh (a=weight)
Texture2D<float2> CoCMap     : register(t3);  // Full-res CoC (.x=signed, .y=abs)

SamplerState PointSamp  : register(s0);
SamplerState LinearSamp : register(s1);

cbuffer CompositeCB : register(b0)
{
    float2 ScreenDims;
    float2 HalfDims;
    float  CAStrength;       // Longitudinal chromatic aberration
    float  AnamorphicRatio;  // Horizontal stretch on bokeh
    float  MaxBokehRadius;
    uint   FocusPeaking;     // 1 = show focus peaking overlay
    float  FocusPeakThreshold;  // CoC threshold for "in focus"
    float3 pad0;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// Triangular dither to reduce banding in gradients
float TriDither(float2 coord)
{
    // Interleaved gradient noise
    float noise = frac(52.9829189 * frac(0.06711056 * coord.x + 0.00583715 * coord.y));
    // Triangular PDF remap
    float t = noise * 2.0 - 1.0;
    return sign(t) * (1.0 - sqrt(max(1.0 - abs(t), 0.0)));
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.texcoord;
    int2   coord = int2(input.position.xy);

    // Read sharp scene
    float3 sharp = SceneColor.Load(int3(coord, 0)).rgb;
    if (any(isnan(sharp)) || any(isinf(sharp)))
        sharp = float3(0, 0, 0);

    // Read CoC
    float2 cocData = CoCMap.Load(int3(coord, 0));
    float signedCoC = cocData.x;
    float absCoC = cocData.y;

    // Read half-res bokeh with bilinear upsampling
    float2 halfUV = uv;
    float3 farColor  = FarField.SampleLevel(LinearSamp, halfUV, 0).rgb;
    float4 nearData  = NearField.SampleLevel(LinearSamp, halfUV, 0);
    float3 nearColor = nearData.rgb;
    float  nearAlpha = nearData.a;

    // ── Bilateral weighting for upsample ────────────────────────────────
    // Blend between sharp and bokeh based on CoC magnitude
    float farBlend  = saturate((signedCoC - 1.0) / max(MaxBokehRadius * 0.5, 1.0));
    float nearBlend = nearAlpha;

    // Start with sharp scene
    float3 result = sharp;

    // ── Far field blend ─────────────────────────────────────────────────
    // Smooth transition from sharp to far bokeh
    if (signedCoC > 0.5)
    {
        result = lerp(sharp, farColor, farBlend);
    }

    // ── Near field blend (over-composites on top) ───────────────────────
    // Near field always draws on top (front-to-back compositing)
    if (nearBlend > 0.01)
    {
        result = lerp(result, nearColor, nearBlend);
    }

    // ── Longitudinal Chromatic Aberration ───────────────────────────────
    // Shift R and B channels by CoC-proportional offset along radial direction
    if (CAStrength > 0.001 && absCoC > 1.0)
    {
        float2 fromCenter = uv - 0.5;
        float caOffset = signedCoC * CAStrength * 0.001;  // subtle pixel offset

        float2 uvR = uv + fromCenter * caOffset;
        float2 uvB = uv - fromCenter * caOffset;

        // Clamp UVs
        uvR = saturate(uvR);
        uvB = saturate(uvB);

        // Re-read with shifted UVs (from the already-blurred result, via far/near)
        // For far field:
        if (signedCoC > 0.5)
        {
            float rChan = FarField.SampleLevel(LinearSamp, uvR, 0).r;
            float bChan = FarField.SampleLevel(LinearSamp, uvB, 0).b;
            result.r = lerp(result.r, rChan, farBlend * 0.5);
            result.b = lerp(result.b, bChan, farBlend * 0.5);
        }
        // For near field:
        if (nearBlend > 0.01)
        {
            float rChan = NearField.SampleLevel(LinearSamp, uvR, 0).r;
            float bChan = NearField.SampleLevel(LinearSamp, uvB, 0).b;
            result.r = lerp(result.r, rChan, nearBlend * 0.5);
            result.b = lerp(result.b, bChan, nearBlend * 0.5);
        }
    }

    // ── Focus Peaking overlay ───────────────────────────────────────────
    if (FocusPeaking > 0)
    {
        // Detect in-focus edge: where |CoC| transitions across threshold
        float threshold = FocusPeakThreshold;

        // Sample CoC neighbors for edge detection
        float cocL = CoCMap.Load(int3(max(coord.x - 1, 0), coord.y, 0)).y;
        float cocR = CoCMap.Load(int3(min(coord.x + 1, int(ScreenDims.x) - 1), coord.y, 0)).y;
        float cocU = CoCMap.Load(int3(coord.x, max(coord.y - 1, 0), 0)).y;
        float cocD = CoCMap.Load(int3(coord.x, min(coord.y + 1, int(ScreenDims.y) - 1), 0)).y;

        float cocGrad = abs(cocR - cocL) + abs(cocD - cocU);

        // Show peaking where pixel is in focus AND at a sharpness edge
        bool inFocus = absCoC < threshold;
        bool atEdge  = cocGrad > 0.5;

        if (inFocus && atEdge)
        {
            // Bright red overlay
            result = lerp(result, float3(1.0, 0.1, 0.1), 0.7);
        }
    }

    // ── Triangular dither ───────────────────────────────────────────────
    float dither = TriDither(float2(coord)) / 255.0;
    result += dither;

    // Final clamp
    result = max(result, 0.0);

    return float4(result, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures --- must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) AutofocusCBData
{
    float    projMatrix[16];     // 64 bytes --- row-major 4x4
    float    screenW;            // +64
    float    screenH;            // +68
    float    nearZ;              // +72
    float    farZ;               // +76
    float    focusSpeed;         // +80
    float    deltaTime;          // +84
    float    manualFocusDist;    // +88
    float    prevFocusDist;      // +92   --- total 96 bytes (6 x 16)
};
static_assert(sizeof(AutofocusCBData) == 96, "AutofocusCB must be 96 bytes");

struct alignas(16) CoCCBData
{
    float    projMatrix[16];     // 64 bytes
    uint32_t screenW;            // +64
    uint32_t screenH;            // +68
    float    nearZ;              // +72
    float    farZ;               // +76
    float    aperture;           // +80
    float    focalLengthMM;      // +84
    float    focusDist;          // +88 (unused, read from buffer, but needed for init)
    float    maxBokehRadius;     // +92
    uint32_t tileCountX;         // +96
    uint32_t tileCountY;         // +100
    float    pad[2];             // +104..112 --- total 112 bytes (7 x 16)
};
static_assert(sizeof(CoCCBData) == 112, "CoCCB must be 112 bytes");

struct alignas(16) GatherCBData
{
    uint32_t halfW;              // +0
    uint32_t halfH;              // +4
    uint32_t fullW;              // +8
    uint32_t fullH;              // +12
    float    maxBokehRadius;     // +16
    int32_t  bladeCount;         // +20
    float    roundness;          // +24
    float    catEyeAmount;       // +28
    float    anamorphicRatio;    // +32
    float    sphericalAberr;     // +36
    int32_t  sampleCount;        // +40
    float    bokehRotation;      // +44  --- total 48 bytes (3 x 16)
};
static_assert(sizeof(GatherCBData) == 48, "GatherCB must be 48 bytes");

struct alignas(16) CompositePSCBData
{
    float    screenW;            // +0
    float    screenH;            // +4
    float    halfW;              // +8
    float    halfH;              // +12
    float    caStrength;         // +16
    float    anamorphicRatio;    // +20
    float    maxBokehRadius;     // +24
    uint32_t focusPeaking;       // +28
    float    focusPeakThreshold; // +32
    float    pad[3];             // +36..48 --- total 48 bytes (3 x 16)
};
static_assert(sizeof(CompositePSCBData) == 48, "CompositePSCB must be 48 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Sample count by quality preset
// ═══════════════════════════════════════════════════════════════════════════

static int SampleCountForQuality(DoFRenderer::Quality q)
{
    switch (q) {
        case DoFRenderer::Low:    return 48;
        case DoFRenderer::Medium: return 72;
        case DoFRenderer::High:   return 96;
        case DoFRenderer::Ultra:  return 128;
        default:                  return 72;
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool DoFRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                              IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm = ComputeManager::Get();
    auto& pl = RenderPipeline::Get();
    auto& rpm = RenderPassManager::Get();

    if (!cm.IsInitialized() || !pl.IsInitialized() || !rpm.IsInitialized()) {
        SKSE::log::error("DoFRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("DoFRenderer: failed to get backbuffer (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backTex->GetDesc(&bbDesc);
    backTex->Release();

    m_screenW    = bbDesc.Width;
    m_screenH    = bbDesc.Height;
    m_halfW      = (m_screenW + 1) / 2;
    m_halfH      = (m_screenH + 1) / 2;
    m_tileCountX = (m_screenW + 15) / 16;
    m_tileCountY = (m_screenH + 15) / 16;

    if (!CompileShaders()) return false;
    if (!CreateResources()) {
        ReleaseResources();
        return false;
    }

    // Register composite pixel shader pass
    m_compositePass = rpm.RegisterPass({
        .name     = "DoF_Composite",
        .psSource = kCompositePS,
    });

    if (!m_compositePass) {
        SKSE::log::error("DoFRenderer: composite PS compilation failed");
        ReleaseResources();
        return false;
    }

    // Register as PrePresent pipeline pass (priority 30: after Bloom 10, before ColorPipeline 50)
    m_pipelineHandle = pl.AddPass({
        .name     = "DoF",
        .stage    = PipelineStage::PreUI,
        .priority = 30,
        .enabled  = false,
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_frameIndex  = 0;

    SKSE::log::info("DoFRenderer: initialized ({}x{}, half={}x{}, tiles={}x{}, quality={})",
                    m_screenW, m_screenH, m_halfW, m_halfH,
                    m_tileCountX, m_tileCountY, static_cast<int>(m_quality));
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool DoFRenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    auto CompileCS = [&](const char* name, const char* source,
                         ID3D11ComputeShader** outCS) -> bool
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(source, strlen(source), name,
                                nullptr, nullptr, "main", "cs_5_0",
                                flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("DoFRenderer: {} compile failed: {}", name,
                                 static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            return false;
        }
        if (err) err->Release();

        hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("DoFRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("DoF_Autofocus",   kAutofocusCS,  &m_autofocusCS))  return false;
    if (!CompileCS("DoF_CoCTile",     kCoCTileCS,    &m_cocCS))        return false;
    if (!CompileCS("DoF_FarGather",   kFarGatherCS,  &m_farGatherCS))  return false;
    if (!CompileCS("DoF_NearGather",  kNearGatherCS, &m_nearGatherCS)) return false;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool DoFRenderer::CreateResources()
{
    HRESULT hr;

    // ── Helper: create texture + SRV + UAV ──────────────────────────────
    auto CreateTexSRVUAV = [&](const char* name, uint32_t w, uint32_t h,
                                DXGI_FORMAT fmt,
                                ID3D11Texture2D** outTex,
                                ID3D11ShaderResourceView** outSRV,
                                ID3D11UnorderedAccessView** outUAV) -> bool
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = w;
        desc.Height     = h;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = fmt;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, outTex);
        if (FAILED(hr)) {
            SKSE::log::error("DoFRenderer: failed to create {} texture (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = fmt;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(*outTex, &srvDesc, outSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = fmt;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(*outTex, &uavDesc, outUAV);
        if (FAILED(hr)) return false;

        return true;
    };

    // ── Focus buffer (1-element structured buffer) ──────────────────────
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth           = sizeof(float);
        desc.Usage               = D3D11_USAGE_DEFAULT;
        desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(float);

        float initFocus = m_focusDist;
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = &initFocus;

        hr = m_device->CreateBuffer(&desc, &initData, &m_focusBuf);
        if (FAILED(hr)) {
            SKSE::log::error("DoFRenderer: failed to create focus buffer (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format               = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension        = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement  = 0;
        srvDesc.Buffer.NumElements   = 1;
        hr = m_device->CreateShaderResourceView(m_focusBuf, &srvDesc, &m_focusBufSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format               = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension        = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement  = 0;
        uavDesc.Buffer.NumElements   = 1;
        hr = m_device->CreateUnorderedAccessView(m_focusBuf, &uavDesc, &m_focusBufUAV);
        if (FAILED(hr)) return false;
    }

    // ── CoC map (full-res R16G16_FLOAT) ─────────────────────────────────
    if (!CreateTexSRVUAV("cocMap", m_screenW, m_screenH,
                          DXGI_FORMAT_R16G16_FLOAT,
                          &m_cocTex, &m_cocSRV, &m_cocUAV))
        return false;

    // ── Tile buffer (tile-res R16G16_FLOAT) ─────────────────────────────
    if (!CreateTexSRVUAV("tileMap", m_tileCountX, m_tileCountY,
                          DXGI_FORMAT_R16G16_FLOAT,
                          &m_tileTex, &m_tileSRV, &m_tileUAV))
        return false;

    // ── Far field (half-res RGBA16F) ────────────────────────────────────
    if (!CreateTexSRVUAV("farBokeh", m_halfW, m_halfH,
                          DXGI_FORMAT_R16G16B16A16_FLOAT,
                          &m_farTex, &m_farSRV, &m_farUAV))
        return false;

    // ── Near field (half-res RGBA16F) ───────────────────────────────────
    if (!CreateTexSRVUAV("nearBokeh", m_halfW, m_halfH,
                          DXGI_FORMAT_R16G16B16A16_FLOAT,
                          &m_nearTex, &m_nearSRV, &m_nearUAV))
        return false;

    // ── Backbuffer copy (for scene color input) ─────────────────────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_backbufferCopy);
        if (FAILED(hr)) {
            SKSE::log::error("DoFRenderer: failed to create backbuffer copy (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                 &m_backbufferCopySRV);
        if (FAILED(hr)) return false;
    }

    // ── Output RT (full-res, for composite PS target) ───────────────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_outputTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_outputTex, &srvDesc, &m_outputSRV);
        if (FAILED(hr)) return false;

        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = desc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        hr = m_device->CreateRenderTargetView(m_outputTex, &rtvDesc, &m_outputRTV);
        if (FAILED(hr)) return false;
    }

    // ── Constant buffers ────────────────────────────────────────────────
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;

        desc.ByteWidth = sizeof(AutofocusCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_autofocusCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(CoCCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_cocCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(GatherCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_farCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(GatherCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_nearCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(CompositePSCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_compositeCB);
        if (FAILED(hr)) return false;
    }

    // ── Samplers ────────────────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy  = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&desc, &m_linearSampler);
        if (FAILED(hr)) return false;

        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        hr = m_device->CreateSamplerState(&desc, &m_pointSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  GetDoFOutputSRV
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* DoFRenderer::GetDoFOutputSRV() const
{
    if (!m_initialized) return nullptr;
    return m_outputSRV;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PrePresent stage)
// ═══════════════════════════════════════════════════════════════════════════

void DoFRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) return;

    auto& cm = ComputeManager::Get();

    // ── Acquire depth SRV ───────────────────────────────────────────────
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    if (!depthSRV) {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }
    if (!depthSRV) return;

    // Prefer Hi-Z SRV if available
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }

    // ── Copy scene color ────────────────────────────────────────────────
    // During mid-frame dispatch, the backbuffer doesn't contain the scene —
    // the game renders to an internal RT exposed via ctx.gameSceneRTV.
    // Extract the texture from that RTV first; fall back to swapchain only
    // when gameSceneRTV is null (Present-time dispatch).
    {
        ID3D11Texture2D* sceneTex = nullptr;

        if (ctx.gameSceneRTV) {
            // Mid-frame path: read the game's current render target
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                                  reinterpret_cast<void**>(&sceneTex));
                res->Release();
                if (FAILED(hr)) sceneTex = nullptr;
            }
        }

        if (!sceneTex) {
            // Present-time fallback: swapchain backbuffer
            auto* sc = ctx.swapChain ? ctx.swapChain : D3D11Hook::GetSwapChain();
            if (!sc) return;

            if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&sceneTex))))
                return;
        }

        // Ensure copy texture format matches the source.  The game's internal
        // scene RT is often R16G16B16A16_FLOAT while the swapchain backbuffer
        // is R8G8B8A8_UNORM.  CopyResource requires identical format+size,
        // so lazily recreate the copy texture when there is a mismatch.
        {
            D3D11_TEXTURE2D_DESC srcDesc;
            sceneTex->GetDesc(&srcDesc);

            bool needRecreate = false;
            if (m_backbufferCopy) {
                D3D11_TEXTURE2D_DESC copyDesc;
                m_backbufferCopy->GetDesc(&copyDesc);
                needRecreate = (srcDesc.Format != copyDesc.Format ||
                                srcDesc.Width  != copyDesc.Width  ||
                                srcDesc.Height != copyDesc.Height);
            } else {
                needRecreate = true;
            }

            if (needRecreate) {
                if (m_backbufferCopy)    { m_backbufferCopy->Release();    m_backbufferCopy = nullptr; }
                if (m_backbufferCopySRV) { m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr; }

                D3D11_TEXTURE2D_DESC newDesc = {};
                newDesc.Width      = srcDesc.Width;
                newDesc.Height     = srcDesc.Height;
                newDesc.MipLevels  = 1;
                newDesc.ArraySize  = 1;
                newDesc.Format     = srcDesc.Format;
                newDesc.SampleDesc = {1, 0};
                newDesc.Usage      = D3D11_USAGE_DEFAULT;
                newDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

                HRESULT hr = m_device->CreateTexture2D(&newDesc, nullptr, &m_backbufferCopy);
                if (FAILED(hr)) {
                    SKSE::log::error("DoFRenderer: failed to recreate scene copy tex (0x{:X})",
                                     static_cast<uint32_t>(hr));
                    sceneTex->Release();
                    return;
                }

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                    = newDesc.Format;
                srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MostDetailedMip = 0;
                srvDesc.Texture2D.MipLevels       = 1;
                hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                         &m_backbufferCopySRV);
                if (FAILED(hr)) {
                    SKSE::log::error("DoFRenderer: failed to recreate scene copy SRV");
                    m_backbufferCopy->Release();
                    m_backbufferCopy = nullptr;
                    sceneTex->Release();
                    return;
                }

                SKSE::log::info("DoFRenderer: recreated scene copy tex (fmt={}, {}x{})",
                                static_cast<uint32_t>(srcDesc.Format),
                                srcDesc.Width, srcDesc.Height);
            }
        }

        ctx.context->CopyResource(m_backbufferCopy, sceneTex);
        sceneTex->Release();
    }

    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();
    const float dt    = ctx.deltaTime > 0.0f ? ctx.deltaTime : (1.0f / 60.0f);

    cm.SaveCSState();

    // Bind pre-computed linearized depth at t31 for autofocus + CoC passes
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: Autofocus (1x1 dispatch)
    // ═════════════════════════════════════════════════════════════════════
    {
        AutofocusCBData cb = {};
        std::memcpy(cb.projMatrix, scene.ProjMatrix(), sizeof(float) * 16);
        cb.screenW        = static_cast<float>(m_screenW);
        cb.screenH        = static_cast<float>(m_screenH);
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.focusSpeed     = m_focusSpeed;
        cb.deltaTime      = dt;
        cb.manualFocusDist = m_manualFocus;
        cb.prevFocusDist  = m_prevFocusDist;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_autofocusCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_autofocusCB, 0);
        }

        ctx.context->CSSetShaderResources(0, 1, &depthSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_focusBufUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_autofocusCB);
        ctx.context->CSSetShader(m_autofocusCS, nullptr, 0);

        ctx.context->Dispatch(1, 1, 1);

        // Clear bindings
        ID3D11ShaderResourceView*  nullSRV[1] = {};
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: CoC + Tile Classification (full-res, 16x16 groups)
    // ═════════════════════════════════════════════════════════════════════
    {
        CoCCBData cb = {};
        std::memcpy(cb.projMatrix, scene.ProjMatrix(), sizeof(float) * 16);
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.aperture       = m_aperture;
        cb.focalLengthMM  = m_focalLength;
        cb.focusDist      = m_focusDist;  // placeholder, actual read from buffer
        cb.maxBokehRadius = m_maxBokehRadius;
        cb.tileCountX     = m_tileCountX;
        cb.tileCountY     = m_tileCountY;
        cb.pad[0] = cb.pad[1] = 0.0f;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_cocCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_cocCB, 0);
        }

        // t0 = depth, t1 = focus buffer
        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_focusBufSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);

        // u0 = CoC output, u1 = tile output
        ID3D11UnorderedAccessView* uavs[] = { m_cocUAV, m_tileUAV };
        ctx.context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

        ctx.context->CSSetConstantBuffers(0, 1, &m_cocCB);
        ctx.context->CSSetShader(m_cocCS, nullptr, 0);

        // 16x16 thread groups matching tile size
        UINT groupsX = m_tileCountX;
        UINT groupsY = m_tileCountY;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView*  nullSRVs[2] = {};
        ID3D11UnorderedAccessView* nullUAVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 3: Far Field Bokeh Gather (half-res)
    // ═════════════════════════════════════════════════════════════════════
    {
        GatherCBData cb = {};
        cb.halfW          = m_halfW;
        cb.halfH          = m_halfH;
        cb.fullW          = m_screenW;
        cb.fullH          = m_screenH;
        cb.maxBokehRadius = m_maxBokehRadius;
        cb.bladeCount     = m_bladeCount;
        cb.roundness      = m_roundness;
        cb.catEyeAmount   = m_catEye;
        cb.anamorphicRatio = m_anamorphic;
        cb.sphericalAberr = 0.3f;  // subtle ring bokeh
        cb.sampleCount    = SampleCountForQuality(m_quality);
        cb.bokehRotation  = static_cast<float>(m_frameIndex) * 0.01f;  // slowly rotate

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_farCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_farCB, 0);
        }

        // t0 = scene color, t1 = CoC map, t2 = tile map
        ID3D11ShaderResourceView* srvs[] = { m_backbufferCopySRV, m_cocSRV, m_tileSRV };
        ctx.context->CSSetShaderResources(0, 3, srvs);

        // s0 = linear sampler
        ctx.context->CSSetSamplers(0, 1, &m_linearSampler);

        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_farUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_farCB);
        ctx.context->CSSetShader(m_farGatherCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView*  nullSRVs[3] = {};
        ID3D11UnorderedAccessView* nullUAV[1]   = {};
        ctx.context->CSSetShaderResources(0, 3, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 4: Near Field Bokeh Gather (half-res)
    // ═════════════════════════════════════════════════════════════════════
    {
        GatherCBData cb = {};
        cb.halfW          = m_halfW;
        cb.halfH          = m_halfH;
        cb.fullW          = m_screenW;
        cb.fullH          = m_screenH;
        cb.maxBokehRadius = m_maxBokehRadius;
        cb.bladeCount     = m_bladeCount;
        cb.roundness      = m_roundness;
        cb.catEyeAmount   = m_catEye;
        cb.anamorphicRatio = m_anamorphic;
        cb.sphericalAberr = 0.3f;
        cb.sampleCount    = SampleCountForQuality(m_quality);
        cb.bokehRotation  = static_cast<float>(m_frameIndex) * 0.01f;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_nearCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_nearCB, 0);
        }

        // t0 = scene color, t1 = CoC map, t2 = tile map
        ID3D11ShaderResourceView* srvs[] = { m_backbufferCopySRV, m_cocSRV, m_tileSRV };
        ctx.context->CSSetShaderResources(0, 3, srvs);
        ctx.context->CSSetSamplers(0, 1, &m_linearSampler);

        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_nearUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_nearCB);
        ctx.context->CSSetShader(m_nearGatherCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView*  nullSRVs[3] = {};
        ID3D11UnorderedAccessView* nullUAV[1]   = {};
        ctx.context->CSSetShaderResources(0, 3, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // Unbind linearized depth t31
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 5: Composite PS (fullscreen -> scene RT or backbuffer)
    // ═════════════════════════════════════════════════════════════════════
    {
        // Build composite CB
        CompositePSCBData cb = {};
        cb.screenW            = static_cast<float>(m_screenW);
        cb.screenH            = static_cast<float>(m_screenH);
        cb.halfW              = static_cast<float>(m_halfW);
        cb.halfH              = static_cast<float>(m_halfH);
        cb.caStrength         = m_caStrength;
        cb.anamorphicRatio    = m_anamorphic;
        cb.maxBokehRadius     = m_maxBokehRadius;
        cb.focusPeaking       = m_focusPeaking ? 1u : 0u;
        cb.focusPeakThreshold = 2.0f;   // pixels of CoC considered "in focus"
        cb.pad[0] = cb.pad[1] = cb.pad[2] = 0.0f;

        // SRVs: t0=scene, t1=far, t2=near, t3=CoC
        ID3D11ShaderResourceView* srvs[] = {
            m_backbufferCopySRV,   // t0: sharp scene
            m_farSRV,              // t1: far bokeh
            m_nearSRV,             // t2: near bokeh
            m_cocSRV,              // t3: CoC map
        };

        ID3D11SamplerState* samplers[] = {
            m_pointSampler,    // s0
            m_linearSampler,   // s1
        };

        // Get output RTV: mid-frame uses the game's active scene RT,
        // Present-time falls back to the swapchain backbuffer.
        ID3D11RenderTargetView* outputRTV = nullptr;
        bool ownOutputRTV = false;

        if (ctx.gameSceneRTV) {
            // Mid-frame dispatch: composite directly back into the game's scene RT
            outputRTV = ctx.gameSceneRTV;
            // Don't AddRef — D3D11StateBackup keeps it alive during dispatch
        } else {
            // Present-time fallback: create RTV from swapchain backbuffer
            auto* sc = ctx.swapChain ? ctx.swapChain : D3D11Hook::GetSwapChain();
            if (!sc) return;

            ID3D11Texture2D* backTex = nullptr;
            if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&backTex))))
                return;

            D3D11_TEXTURE2D_DESC texDesc;
            backTex->GetDesc(&texDesc);

            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format        = texDesc.Format;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

            m_device->CreateRenderTargetView(backTex, &rtvDesc, &outputRTV);
            backTex->Release();
            ownOutputRTV = true;
        }

        if (!outputRTV) return;

        RenderPassManager::Get().Execute({
            .passID       = m_compositePass,
            .rtv          = outputRTV,
            .srvs         = srvs,
            .srvCount     = 4,
            .samplers     = samplers,
            .samplerCount = 2,
            .cbData       = &cb,
            .cbSize       = sizeof(cb),
        });

        if (ownOutputRTV) outputRTV->Release();
    }

    // ── Update focus state for next frame ────────────────────────────────
    // Read back the autofocus result for the prev frame tracking.
    // We use the value from the structured buffer on the GPU side; on the
    // CPU side we just track the state we sent so the CB is consistent.
    if (m_manualFocus > 0.0f) {
        float speed = (std::min)(m_focusSpeed * dt, 1.0f);
        m_focusDist = m_prevFocusDist + (m_manualFocus - m_prevFocusDist) * speed;
    }
    // GPU autofocus result feeds into next frame via the structured buffer
    // CPU tracking approximates for the CB prev field
    m_prevFocusDist = m_focusDist;

    m_frameIndex++;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void DoFRenderer::Shutdown()
{
    if (!m_initialized) return;

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("DoFRenderer: shut down");
}


void DoFRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    // Compute shaders
    SafeRelease(m_autofocusCS);
    SafeRelease(m_cocCS);
    SafeRelease(m_farGatherCS);
    SafeRelease(m_nearGatherCS);

    // Pass 1: Autofocus
    SafeRelease(m_focusBuf);
    SafeRelease(m_focusBufSRV);
    SafeRelease(m_focusBufUAV);
    SafeRelease(m_autofocusCB);

    // Pass 2: CoC + Tile
    SafeRelease(m_cocTex);
    SafeRelease(m_cocSRV);
    SafeRelease(m_cocUAV);
    SafeRelease(m_tileTex);
    SafeRelease(m_tileSRV);
    SafeRelease(m_tileUAV);
    SafeRelease(m_cocCB);

    // Pass 3: Far gather
    SafeRelease(m_farTex);
    SafeRelease(m_farSRV);
    SafeRelease(m_farUAV);
    SafeRelease(m_farCB);

    // Pass 4: Near gather
    SafeRelease(m_nearTex);
    SafeRelease(m_nearSRV);
    SafeRelease(m_nearUAV);
    SafeRelease(m_nearCB);

    // Pass 5: Composite
    // m_compositePass is managed by RenderPassManager, no release needed
    SafeRelease(m_compositeCB);

    // Shared
    SafeRelease(m_backbufferCopy);
    SafeRelease(m_backbufferCopySRV);
    SafeRelease(m_linearSampler);
    SafeRelease(m_pointSampler);

    // Output
    SafeRelease(m_outputTex);
    SafeRelease(m_outputSRV);
    SafeRelease(m_outputRTV);
}

} // namespace SB
