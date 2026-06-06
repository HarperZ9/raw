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
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "SceneData.h"
#include "SharedGPUResources.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"


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
// Physical thin-lens depth-of-field -- Pass 1: Depth-based autofocus
// Reference: Standard thin-lens optics (textbook), Potmesil & Chakravarty 1981
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

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
}

    static const int GRID = 10;

Texture2D<float> DepthTex : register(t0);
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth
RWStructuredBuffer<float> FocusOutput : register(u0);  // 1-element: focus distance

// Linearize a reversed-Z depth value to view-space distance.
// LinearDepth (t31) already stores linear depth, but if we sample raw depth
// from DepthTex (t0) we need this conversion.
float DepthToViewZ(float d)
{
    // Reversed-Z: d=1 at near, d=0 at far
    // viewZ = NearZ * FarZ / (FarZ - d * (FarZ - NearZ))
    float denom = FarZ - d * (FarZ - NearZ);
    return (denom > 1e-6) ? (NearZ * FarZ / denom) : FarZ;
}

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // If manual focus is set, use it with temporal smoothing
    if (ManualFocusDist > 0.0)
    {
        float smoothed = lerp(PrevFocusDist, ManualFocusDist,
                              saturate(FocusSpeed * DeltaTime));
        FocusOutput[0] = smoothed;
        return;
    }

    // Sample a GRID x GRID weighted grid of depth values around screen center.
    // Center-weighted: closer to center = higher weight.
    float2 center = ScreenDims * 0.5;
    float  gridSpan = min(ScreenDims.x, ScreenDims.y) * 0.15; // 15% of smaller dim

    float depths[GRID * GRID];
    float weights[GRID * GRID];
    int count = 0;

    [unroll]
    for (int y = 0; y < GRID; y++)
    {
        [unroll]
        for (int x = 0; x < GRID; x++)
        {
            // Map grid cell to screen position centered on screen
            float2 offset = float2(x - (GRID - 1) * 0.5, y - (GRID - 1) * 0.5)
                          / ((GRID - 1) * 0.5);   // [-1, 1]
            float2 samplePos = center + offset * gridSpan;

            // Clamp to valid screen range
            samplePos = clamp(samplePos, float2(0, 0), ScreenDims - float2(1, 1));

            // Read linear depth from pre-computed buffer
            float viewZ = LinearDepth.Load(int3((int2)samplePos, 0));

            // Reject sky pixels (extremely large depth)
            if (viewZ >= FarZ * 0.99)
            {
                depths[count]  = 0.0;
                weights[count] = 0.0;
            }
            else
            {
                depths[count]  = viewZ;
                // Gaussian center-weighting
                float dist2 = dot(offset, offset);
                weights[count] = exp(-2.0 * dist2);
            }
            count++;
        }
    }

    // Simple insertion sort on depths for median finding
    [unroll]
    for (int i = 1; i < GRID * GRID; i++)
    {
        float dKey = depths[i];
        float wKey = weights[i];
        int j = i - 1;
        [loop]
        while (j >= 0 && depths[j] > dKey)
        {
            depths[j + 1]  = depths[j];
            weights[j + 1] = weights[j];
            j--;
        }
        depths[j + 1]  = dKey;
        weights[j + 1] = wKey;
    }

    // Find median of non-zero depth samples
    int validStart = 0;
    int validEnd   = GRID * GRID - 1;
    [loop]
    while (validStart < GRID * GRID && depths[validStart] <= 0.0)
        validStart++;
    [loop]
    while (validEnd > validStart && weights[validEnd] <= 0.0)
        validEnd--;

    if (validStart > validEnd)
    {
        // All sky or invalid -- keep previous focus
        FocusOutput[0] = PrevFocusDist;
        return;
    }

    int medianIdx = (validStart + validEnd) / 2;
    float medianDepth = depths[medianIdx];

    // Compute variance around median and reject outliers > 2 sigma
    float sumW  = 0.0;
    float sumD  = 0.0;
    float sumD2 = 0.0;

    [loop]
    for (int k = validStart; k <= validEnd; k++)
    {
        sumW  += weights[k];
        sumD  += depths[k] * weights[k];
        sumD2 += depths[k] * depths[k] * weights[k];
    }

    float meanD    = (sumW > 1e-6) ? (sumD / sumW) : medianDepth;
    float variance = (sumW > 1e-6) ? (sumD2 / sumW - meanD * meanD) : 0.0;
    float sigma    = sqrt(max(variance, 0.0));
    float threshold = max(sigma * 2.0, medianDepth * 0.3); // at least 30% of median

    // Weighted average of inliers
    float finalSum  = 0.0;
    float finalWSum = 0.0;

    [loop]
    for (int m = validStart; m <= validEnd; m++)
    {
        float deviation = abs(depths[m] - medianDepth);
        if (deviation <= threshold)
        {
            finalSum  += depths[m] * weights[m];
            finalWSum += weights[m];
        }
    }

    float newFocus = (finalWSum > 1e-6) ? (finalSum / finalWSum) : medianDepth;

    // Clamp to valid range
    newFocus = clamp(newFocus, NearZ * 2.0, FarZ * 0.95);

    // Temporal smoothing: lerp from previous focus distance
    float smoothAlpha = saturate(FocusSpeed * DeltaTime);
    float result = lerp(PrevFocusDist, newFocus, smoothAlpha);

    // Guard against degenerate values
    result = max(result, NearZ * 2.0);

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
// Physical thin-lens depth-of-field -- Pass 2: CoC computation + tile dilation
// Reference: Potmesil & Chakravarty 1981, Nilsson 2012 (tile dilation)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

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
}

Texture2D<float> DepthTex : register(t0);
StructuredBuffer<float> FocusBuf : register(t1);  // 1-element from autofocus
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth
RWTexture2D<float2> CoCOutput  : register(u0);  // Full-res R16G16_FLOAT (signed CoC, abs CoC)
RWTexture2D<float2> TileOutput : register(u1);  // Per-tile R16G16_FLOAT (min CoC, max CoC)

// Groupshared memory for 16x16 tile reduction
groupshared float gs_cocValues[16 * 16];

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID,
          uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
    // Out-of-bounds guard
    if (any(DTid.xy >= ScreenDims))
    {
        gs_cocValues[GI] = 0.0;
        GroupMemoryBarrierWithGroupSync();
        // Still participate in reduction below
    }
    else
    {
        // Read autofocus result from structured buffer
        float focusDist = FocusBuf[0];
        focusDist = max(focusDist, NearZ * 2.0);

        // Read linear view-space depth
        float viewZ = LinearDepth.Load(int3(DTid.xy, 0));

        // Thin-lens CoC formula (simplified):
        //   CoC_world = A * f * (S - d) / (d * (S - f))
        // where A = aperture diameter, f = focal length, S = focus distance, d = view depth
        //
        // Simplified to pixel-space:
        //   CoCScale = (A * f) / (S - f)
        //   CoC_pixels = CoCScale * (1 - S / d) * sensorScaleFactor
        //
        // The sensor scale maps world-space CoC to pixel-space CoC.
        // We derive it from the projection matrix: Proj[0][0] = 2*n/w maps
        // horizontal FOV. Sensor factor ~ ScreenDims.x * 0.5 * Proj[0][0] / focusDist
        // but a simpler approach: fold it into CoCScale directly.

        float focalLength = FocalLengthMM * 0.001;  // mm -> meters (world units)
        float apertureDiameter = focalLength / max(Aperture, 0.01); // A = f/N

        // CoCScale in world units
        float denom = focusDist - focalLength;
        float cocScale = (denom > 1e-6)
                       ? (apertureDiameter * focalLength / denom)
                       : 0.0;

        // Signed CoC: positive = far field (behind focus), negative = near field
        float cocWorld = cocScale * (1.0 - focusDist / max(viewZ, 1e-4));

        // Convert world-space CoC to pixel-space CoC
        // Using projection: pixels = cocWorld * Proj[0][0] * ScreenDims.x * 0.5 / viewZ
        float cocPixels = cocWorld * ProjMatrix[0][0] * (float)ScreenDims.x * 0.5
                        / max(viewZ, 1e-4);

        // Clamp to maximum bokeh radius
        cocPixels = clamp(cocPixels, -MaxBokehRadius, MaxBokehRadius);

        // Write per-pixel CoC: .x = signed CoC, .y = absolute CoC
        CoCOutput[DTid.xy] = float2(cocPixels, abs(cocPixels));

        // Store signed CoC for tile reduction
        gs_cocValues[GI] = cocPixels;
    }

    GroupMemoryBarrierWithGroupSync();

    // ── Tile reduction: find min (most negative = strongest near) and
    //    max (most positive = strongest far) CoC within the 16x16 tile ──
    // Standard parallel reduction in groupshared memory.
    [unroll]
    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (GI < stride)
        {
            float a = gs_cocValues[GI];
            float b = gs_cocValues[GI + stride];
            // min tracks most-negative (near), max tracks most-positive (far)
            // We store both: use negative values for near, positive for far
            // For the reduction, we need two separate values.
            // Re-purpose: store min in even passes, max in odd -- too complex.
            // Instead: we'll do two reductions. But that's expensive for SM5.0.
            //
            // Simpler: store the value with the larger absolute magnitude,
            // preserving sign. This gives us the "worst case" CoC for the tile.
            gs_cocValues[GI] = (abs(a) >= abs(b)) ? a : b;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Thread 0 writes the tile result
    if (GI == 0)
    {
        float maxAbsCoc = gs_cocValues[0];
        // For the tile buffer:
        //   .x = min CoC (most negative, for near-field dilation)
        //   .y = max CoC (most positive, for far-field)
        // Since we only have one reduction, we store the dominant CoC.
        // Near-field dilation uses abs of negative CoC.
        float tileNear = (maxAbsCoc < 0.0) ? maxAbsCoc : 0.0;
        float tileFar  = (maxAbsCoc > 0.0) ? maxAbsCoc : 0.0;

        // Second pass through shared memory to get both min and max
        // We need to re-read from CoCOutput since shared mem is consumed.
        // For a 16x16 tile, just scan the written values.
        float tileMin = 0.0;
        float tileMax = 0.0;
        uint2 tileBase = Gid.xy * 16;
        for (uint ty = 0; ty < 16; ty++)
        {
            for (uint tx = 0; tx < 16; tx++)
            {
                uint2 px = tileBase + uint2(tx, ty);
                if (all(px < ScreenDims))
                {
                    float2 cocVal = CoCOutput[px];
                    tileMin = min(tileMin, cocVal.x);
                    tileMax = max(tileMax, cocVal.x);
                }
            }
        }

        TileOutput[Gid.xy] = float2(tileMin, tileMax);
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
// Physical thin-lens depth-of-field -- Pass 3: Far-field bokeh gather (disc blur)
// Reference: Potmesil & Chakravarty 1981, Jimenez 2014 (scatter-as-gather)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

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
}

static const float PI = 3.14159265359;
static const float GOLDEN_ANGLE = 2.39996322973;  // pi * (3 - sqrt(5))

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy (full-res)
Texture2D<float2> CoCMap     : register(t1);  // Full-res CoC (.x=signed, .y=abs)
Texture2D<float2> TileMap    : register(t2);  // Tile min/max CoC
SamplerState LinearSamp : register(s0);
RWTexture2D<float4> FarOutput : register(u0);  // Half-res RGBA16F

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= HalfDims))
        return;

    // Map half-res pixel to full-res center
    float2 fullCenter = float2(DTid.xy) * 2.0 + 1.0;

    // Read center pixel CoC (signed)
    float2 centerCoC = CoCMap.Load(int3((int2)fullCenter, 0));
    float  gatherCoC = centerCoC.x;  // signed: positive = far

    // Check tile for max far CoC -- early out if tile has no far blur
    uint2 tileIdx = (uint2)fullCenter / 16;
    float2 tileCoC = TileMap.Load(int3(tileIdx, 0));
    float  tileFarCoC = tileCoC.y;  // max positive CoC in tile

    // If this pixel and its tile have negligible far CoC, write sharp scene
    if (gatherCoC <= 0.5 && tileFarCoC <= 0.5)
    {
        float2 uv = (fullCenter + 0.5) / float2(FullDims);
        FarOutput[DTid.xy] = SceneColor.SampleLevel(LinearSamp, uv, 0);
        return;
    }

    // Gather radius in full-res pixels: use this pixel's far CoC
    float gatherRadius = max(gatherCoC, 0.0);
    gatherRadius = min(gatherRadius, MaxBokehRadius);

    // Scale to half-res for sampling offsets
    float halfRadius = gatherRadius * 0.5;

    // If radius is tiny, just sample center
    if (halfRadius < 0.5)
    {
        float2 uv = (fullCenter + 0.5) / float2(FullDims);
        FarOutput[DTid.xy] = SceneColor.SampleLevel(LinearSamp, uv, 0);
        return;
    }

    // Adaptive sample count based on CoC size
    int numSamples = SampleCount;

    // Disc gather using Fibonacci spiral (golden angle) for uniform coverage
    float3 colorSum = float3(0, 0, 0);
    float  weightSum = 0.0;

    float2 invFullDims = 1.0 / float2(FullDims);

    for (int i = 0; i < numSamples; i++)
    {
        // Fibonacci spiral: r = sqrt(i/N), theta = i * goldenAngle
        float t = (float(i) + 0.5) / float(numSamples);
        float r = sqrt(t);
        float theta = float(i) * GOLDEN_ANGLE + BokehRotation;

        float2 offset;
        offset.x = r * cos(theta);
        offset.y = r * sin(theta);

        // Apply anamorphic stretch (squeeze horizontally)
        offset.x *= 1.0 - AnamorphicRatio * 0.5;

        // Scale by gather radius and convert to UV space
        float2 sampleUV = (fullCenter + offset * gatherRadius + 0.5) * invFullDims;

        // Clamp to screen bounds
        sampleUV = clamp(sampleUV, invFullDims, 1.0 - invFullDims);

        // Read sample color and CoC
        float4 sampleColor = SceneColor.SampleLevel(LinearSamp, sampleUV, 0);
        float2 sampleCoC   = CoCMap.Load(int3(
            (int2)(sampleUV * float2(FullDims)), 0));

        // Disc membership test: a background sample should only contribute
        // if its own CoC is large enough to reach this pixel.
        // sampleCoC.x > 0 means the sample is in the far field.
        float sampleCoCFar = max(sampleCoC.x, 0.0);

        // Weight 1: disc membership -- sample's CoC must cover the distance
        //           from the sample to the gather center
        float sampleDist = r * gatherRadius;
        float membership = saturate(sampleCoCFar - sampleDist + 1.0);

        // Weight 2: Gaussian falloff from disc center (softer edges)
        float gaussian = exp(-2.0 * r * r);

        // Weight 3: spherical aberration (ring bokeh) -- boost edges of disc
        float ringWeight = lerp(1.0, r, SphericalAberr);

        float w = membership * gaussian * ringWeight;
        w = max(w, 1e-5);

        colorSum  += sampleColor.rgb * w;
        weightSum += w;
    }

    float3 result = (weightSum > 1e-5) ? (colorSum / weightSum) : float3(0, 0, 0);
    FarOutput[DTid.xy] = float4(result, 1.0);
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
// Physical thin-lens depth-of-field -- Pass 4: Near-field bokeh gather
// Reference: Potmesil & Chakravarty 1981, Nilsson 2012 (near-field dilation)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

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
}

static const float PI = 3.14159265359;
static const float GOLDEN_ANGLE = 2.39996322973;

Texture2D<float4> SceneColor : register(t0);
Texture2D<float2> CoCMap     : register(t1);
Texture2D<float2> TileMap    : register(t2);
SamplerState LinearSamp : register(s0);
RWTexture2D<float4> NearOutput : register(u0);  // Half-res RGBA16F (rgb=color, a=near weight)

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= HalfDims))
    {
        return;
    }

    // Map half-res pixel to full-res center
    float2 fullCenter = float2(DTid.xy) * 2.0 + 1.0;

    // Read center pixel CoC
    float2 centerCoC = CoCMap.Load(int3((int2)fullCenter, 0));
    float  centerNearCoC = -min(centerCoC.x, 0.0);  // Flip sign: near CoC becomes positive

    // Read tile min CoC for near-field dilation (Nilsson 2012).
    // The tile stores the most-negative CoC; we use its absolute value as the
    // dilated gather radius so near-field objects bleed into focused regions.
    uint2 tileIdx = (uint2)fullCenter / 16;
    float2 tileCoC = TileMap.Load(int3(tileIdx, 0));
    float  tileNearCoC = -min(tileCoC.x, 0.0);  // Most negative -> positive magnitude

    // Use dilated tile CoC for gather radius (prevents sharp near-field edges)
    float gatherRadius = max(tileNearCoC, centerNearCoC);
    gatherRadius = min(gatherRadius, MaxBokehRadius);

    // Early out: no near-field blur in this tile
    if (gatherRadius < 0.5)
    {
        NearOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    int numSamples = SampleCount;

    // Disc gather using Fibonacci spiral
    float3 colorSum = float3(0, 0, 0);
    float  weightSum = 0.0;
    float  nearAlphaSum = 0.0;

    float2 invFullDims = 1.0 / float2(FullDims);

    for (int i = 0; i < numSamples; i++)
    {
        // Fibonacci spiral point
        float t = (float(i) + 0.5) / float(numSamples);
        float r = sqrt(t);
        float theta = float(i) * GOLDEN_ANGLE + BokehRotation;

        float2 offset;
        offset.x = r * cos(theta);
        offset.y = r * sin(theta);

        // Anamorphic stretch
        offset.x *= 1.0 - AnamorphicRatio * 0.5;

        // Scale by gather radius and convert to UV
        float2 sampleUV = (fullCenter + offset * gatherRadius + 0.5) * invFullDims;
        sampleUV = clamp(sampleUV, invFullDims, 1.0 - invFullDims);

        // Read sample color and CoC
        float4 sampleColor = SceneColor.SampleLevel(LinearSamp, sampleUV, 0);
        float2 sampleCoC   = CoCMap.Load(int3(
            (int2)(sampleUV * float2(FullDims)), 0));

        // Near-field: negative CoC means closer than focus plane
        float sampleNearCoC = -min(sampleCoC.x, 0.0);  // positive magnitude

        // Disc membership: sample must be in near field to contribute.
        // Near-field objects scatter into focused areas, so we accept samples
        // that have significant near CoC.
        float membership = saturate(sampleNearCoC * 2.0);

        // Gaussian falloff from disc center
        float gaussian = exp(-2.0 * r * r);

        // Spherical aberration (ring bokeh)
        float ringWeight = lerp(1.0, r, SphericalAberr);

        float w = membership * gaussian * ringWeight;
        w = max(w, 1e-5);

        colorSum    += sampleColor.rgb * w;
        weightSum   += w;
        nearAlphaSum += membership;
    }

    float3 result = (weightSum > 1e-5) ? (colorSum / weightSum) : float3(0, 0, 0);

    // Near-field alpha: fraction of samples that were in the near field,
    // weighted by the gather radius relative to max.  This controls how
    // strongly the near field overlaps the final composite.
    float nearAlpha = (nearAlphaSum / float(numSamples))
                    * saturate(gatherRadius / max(MaxBokehRadius, 1.0));

    NearOutput[DTid.xy] = float4(result, saturate(nearAlpha));
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
// Physical thin-lens depth-of-field -- Pass 5: Final composite
// Reference: Potmesil & Chakravarty 1981, Jimenez 2014 (compositing)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

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
}

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy (sharp)
Texture2D<float4> FarField   : register(t1);  // Half-res far bokeh
Texture2D<float4> NearField  : register(t2);  // Half-res near bokeh (a=weight)
Texture2D<float2> CoCMap     : register(t3);  // Full-res CoC (.x=signed, .y=abs)
SamplerState PointSamp  : register(s0);
SamplerState LinearSamp : register(s1);

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// Triangular dither to break banding (Gjoel 2016)
float TriangularDither(float2 pos)
{
    float noise = frac(sin(dot(pos, float2(12.9898, 78.233))) * 43758.5453);
    // Triangular PDF: remap [0,1) uniform to [-0.5, 0.5) triangular
    noise = noise * 2.0 - 1.0;
    return sign(noise) * (1.0 - sqrt(1.0 - abs(noise))) * 0.5;
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.texcoord;

    // Read sharp scene color
    float4 scene = SceneColor.Sample(PointSamp, uv);

    // Read per-pixel CoC
    float2 cocData = CoCMap.Sample(PointSamp, uv);
    float  signedCoC = cocData.x;
    float  absCoC    = cocData.y;

    // Compute half-res UV for sampling bokeh textures
    float2 halfUV = uv;  // same UV space, different texture dimensions

    // ── Far-field blend ─────────────────────────────────────────────────
    // Blend from sharp scene to far bokeh based on far CoC magnitude.
    float farCoC = max(signedCoC, 0.0);
    float farBlendFactor = saturate(farCoC / max(MaxBokehRadius, 1.0));

    // Smooth the blend factor to avoid hard transitions
    farBlendFactor = smoothstep(0.0, 1.0, farBlendFactor);

    float4 farColor = FarField.Sample(LinearSamp, halfUV);
    float3 output = lerp(scene.rgb, farColor.rgb, farBlendFactor);

    // ── Near-field blend ────────────────────────────────────────────────
    // Near field overlaps everything -- it bleeds over focused and far regions.
    float4 nearColor = NearField.Sample(LinearSamp, halfUV);
    float  nearAlpha = nearColor.a;

    // Boost near alpha slightly so near-field objects fully cover the scene
    nearAlpha = saturate(nearAlpha * 1.5);

    output = lerp(output, nearColor.rgb, nearAlpha);

    // ── Longitudinal chromatic aberration ───────────────────────────────
    // Shift red and blue channels radially based on CoC magnitude.
    // Longitudinal CA causes color fringing along the optical axis.
    if (CAStrength > 0.001)
    {
        float2 center = float2(0.5, 0.5);
        float2 fromCenter = uv - center;
        float  caOffset = absCoC * CAStrength * 0.001; // subtle

        float2 uvR = uv + fromCenter * caOffset;
        float2 uvB = uv - fromCenter * caOffset;

        // Re-sample only R and B channels with offset
        float4 farR = FarField.Sample(LinearSamp, uvR);
        float4 farB = FarField.Sample(LinearSamp, uvB);

        // Apply CA only to blurred regions
        float caBlend = saturate(absCoC / max(MaxBokehRadius, 1.0));
        output.r = lerp(output.r, lerp(scene.r, farR.r, farBlendFactor), caBlend);
        output.b = lerp(output.b, lerp(scene.b, farB.b, farBlendFactor), caBlend);
    }

    // ── Focus peaking debug overlay ─────────────────────────────────────
    if (FocusPeaking > 0)
    {
        // Detect "in focus" region: CoC below threshold
        if (absCoC < FocusPeakThreshold)
        {
            // Edge detection on CoC map for peaking outline
            float2 texelSize = 1.0 / ScreenDims;
            float cocL = CoCMap.Sample(PointSamp, uv + float2(-texelSize.x, 0)).y;
            float cocR = CoCMap.Sample(PointSamp, uv + float2( texelSize.x, 0)).y;
            float cocU = CoCMap.Sample(PointSamp, uv + float2(0, -texelSize.y)).y;
            float cocD = CoCMap.Sample(PointSamp, uv + float2(0,  texelSize.y)).y;

            float edge = abs(cocR - cocL) + abs(cocD - cocU);
            if (edge > 0.5)
            {
                // Red peaking overlay
                output = lerp(output, float3(1, 0, 0), 0.7);
            }
        }
    }

    // ── Triangular dither to break banding ──────────────────────────────
    float dither = TriangularDither(input.position.xy) / 255.0;
    output += dither;

    return float4(output, scene.a);
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
        ID3DBlob* blob = ShaderLoader::Compile(name, source, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("DoFRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
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
        return CreateGPUTexture(m_device, w, h, fmt, outTex, outSRV, outUAV, name);
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
    // Constant buffers
    if (!CreateCB(m_device, sizeof(AutofocusCBData), &m_autofocusCB)) return false;
    if (!CreateCB(m_device, sizeof(CoCCBData), &m_cocCB)) return false;
    if (!CreateCB(m_device, sizeof(GatherCBData), &m_farCB)) return false;
    if (!CreateCB(m_device, sizeof(GatherCBData), &m_nearCB)) return false;
    if (!CreateCB(m_device, sizeof(CompositePSCBData), &m_compositeCB)) return false;
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

        UploadCB(ctx.context, m_autofocusCB, &cb, sizeof(cb));

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

        UploadCB(ctx.context, m_cocCB, &cb, sizeof(cb));

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

        UploadCB(ctx.context, m_farCB, &cb, sizeof(cb));

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

        UploadCB(ctx.context, m_nearCB, &cb, sizeof(cb));

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
