// Procedural 3D Perlin-Worley shape noise for volumetric clouds (128^3)
// Reference: Schneider & Vos 2015, "The Real-time Volumetric Cloudscapes
//            of Horizon: Zero Dawn"
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

RWTexture3D<float> ShapeNoise : register(u0);

// ---- Hash / gradient helpers ------------------------------------------------
// Based on Inigo Quilez's integer hash (no sin, GPU-friendly)
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

// ---- 3D value noise ---------------------------------------------------------
// Trilinear interpolation of hashed lattice values.
float valueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = p - i;
    // Smoothstep (Hermite) interpolation weights
    float3 u = f * f * (3.0 - 2.0 * f);

    int3 ip = int3(i);

    // Eight corner hashes
    float c000 = hashToFloat(hash3(uint3(ip + int3(0,0,0))));
    float c100 = hashToFloat(hash3(uint3(ip + int3(1,0,0))));
    float c010 = hashToFloat(hash3(uint3(ip + int3(0,1,0))));
    float c110 = hashToFloat(hash3(uint3(ip + int3(1,1,0))));
    float c001 = hashToFloat(hash3(uint3(ip + int3(0,0,1))));
    float c101 = hashToFloat(hash3(uint3(ip + int3(1,0,1))));
    float c011 = hashToFloat(hash3(uint3(ip + int3(0,1,1))));
    float c111 = hashToFloat(hash3(uint3(ip + int3(1,1,1))));

    // Trilinear blend
    float x00 = lerp(c000, c100, u.x);
    float x10 = lerp(c010, c110, u.x);
    float x01 = lerp(c001, c101, u.x);
    float x11 = lerp(c011, c111, u.x);
    float y0  = lerp(x00,  x10,  u.y);
    float y1  = lerp(x01,  x11,  u.y);
    return lerp(y0, y1, u.z);
}

// ---- 3D Worley noise --------------------------------------------------------
// Single-octave cellular noise: returns distance to nearest random feature point
// in a 3x3x3 neighbourhood search.
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
        // Deterministic random feature point inside each cell
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

// ---- FBM (value noise, 4 octaves) ------------------------------------------
float valueNoiseFBM(float3 p)
{
    float v = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        v += amp * valueNoise(p * freq);
        freq *= 2.0;
        amp  *= 0.5;
    }
    return v;
}

// ---- Main -------------------------------------------------------------------
// Dispatch: (128/8, 128/8, 1) = (16, 16, 1) thread groups.
// Each thread writes one column of 128 Z slices.
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    [loop]
    for (uint z = 0; z < 128; z++)
    {
        float3 uvw = float3(DTid.xy, z) / 128.0;
        float3 p = uvw * 8.0; // 8 noise periods across the volume

        // Perlin-like base: 4-octave value noise FBM
        float perlin = valueNoiseFBM(p);

        // Worley erosion: invert so ridges become valleys
        float worley = 1.0 - worleyNoise(p * 2.0);

        // Perlin-Worley blend (Schneider & Vos eq. 1):
        // Remap Perlin using Worley to add billowy structure
        float shape = saturate(lerp(perlin, worley, 0.3));

        ShapeNoise[uint3(DTid.xy, z)] = shape;
    }
}
