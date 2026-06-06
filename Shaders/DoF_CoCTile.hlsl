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
