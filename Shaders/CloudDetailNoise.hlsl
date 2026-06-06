// Procedural 3D detail noise for volumetric cloud edge erosion (32^3)
// Reference: Schneider & Vos 2015, "The Real-time Volumetric Cloudscapes
//            of Horizon: Zero Dawn"
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

RWTexture3D<float> DetailNoise : register(u0);

// ---- Hash helpers (same as shape noise) -------------------------------------
uint hash3(uint3 v)
{
    v = v * uint3(1597334677u, 3812015801u, 2798796415u);
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v.x ^ v.y ^ v.z;
}

float hashToFloat(uint h)
{
    return float(h & 0x7FFFFFFFu) / float(0x7FFFFFFF);
}

// ---- 3D Worley noise --------------------------------------------------------
float worleyNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = p - i;

    float minDist = 1.0;

    [unroll]
    for (int z = -1; z <= 1; z++)
    [unroll]
    for (int y = -1; y <= 1; y++)
    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        int3 offset = int3(x, y, z);
        uint h = hash3(uint3(int3(i) + offset));
        float3 fp = float3(offset) + float3(
            hashToFloat(h),
            hashToFloat(h * 2654435761u),
            hashToFloat(h * 2246822519u)
        ) - f;
        float d = dot(fp, fp);
        minDist = min(minDist, d);
    }

    return sqrt(minDist);
}

// ---- FBM (Worley, 3 octaves) -----------------------------------------------
// Inverted Worley layered at increasing frequencies for curly, high-frequency
// detail suitable for eroding cloud edges.
float worleyFBM(float3 p)
{
    float v = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        v += amp * (1.0 - worleyNoise(p * freq));
        freq *= 2.0;
        amp  *= 0.5;
    }
    return v;
}

// ---- Main -------------------------------------------------------------------
// Dispatch: (32/8, 32/8, 1) = (4, 4, 1) thread groups.
// Each thread writes one column of 32 Z slices.
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    [loop]
    for (uint z = 0; z < 32; z++)
    {
        float3 uvw = float3(DTid.xy, z) / 32.0;
        float3 p = uvw * 4.0; // 4 noise periods across the volume

        float detail = worleyFBM(p);

        DetailNoise[uint3(DTid.xy, z)] = saturate(detail);
    }
}
