// GTAO — Ground Truth Ambient Occlusion (Visibility Bitmask)
//
// Reference: Jimenez et al. 2019, "Practical Real-Time Strategies for
//            Accurate Indirect Occlusion"
//
// Standard bitmask approach: 32-bit mask initialized to 0 (all unoccluded).
// For each horizon sample that occludes, OR the corresponding elevation bits.
// Occlusion = popcount(mask) / 32.
//
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer GTAOCB : register(b0)
{
    float4x4 ProjMatrix;
    uint2    ScreenDims;
    float    NearZ;
    float    FarZ;
    float    AORadius;
    float    AOIntensity;
    int      NumDirections;
    int      NumSteps;
    uint     FrameIndex;
    float    BounceIntensity;
    uint     BounceEnabled;
    float    FPDepthThreshold;
};

Texture2D<float>  DepthTex    : register(t0);   // HiZ mip 0 (reversed-Z)
Texture2D<float4> BlueNoise   : register(t30);  // 128x128 R2 quasi-random
Texture2D<float>  LinearDepth : register(t31);  // View-space Z

RWTexture2D<float> Output : register(u0);       // AO visibility (1=lit, 0=occluded)

static const float PI      = 3.14159265359;
static const float HALF_PI = 1.57079632679;
static const uint  BITS    = 32;

float3 UVToView(float2 uv, float z)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    return float3(ndc.x * z / ProjMatrix[0][0],
                  ndc.y * z / ProjMatrix[1][1], z);
}

float3 ReconstructNormal(int2 p, float2 ts)
{
    // 4-neighbor min-difference reconstruction.
    // Picks the closest-depth neighbor on each axis to handle silhouettes.
    float zC = LinearDepth.Load(int3(p, 0));
    float zL = LinearDepth.Load(int3(p + int2(-1, 0), 0));
    float zR = LinearDepth.Load(int3(p + int2( 1, 0), 0));
    float zU = LinearDepth.Load(int3(p + int2( 0,-1), 0));
    float zD = LinearDepth.Load(int3(p + int2( 0, 1), 0));

    // Pick the neighbor with smaller depth difference on each axis
    float2 uvC = (float2(p) + 0.5) * ts;
    float3 pC = UVToView(uvC, zC);

    bool useR = abs(zR - zC) < abs(zL - zC);
    bool useU = abs(zU - zC) < abs(zD - zC);

    float3 ddx = useR
        ? UVToView(uvC + float2(ts.x, 0), zR) - pC
        : pC - UVToView(uvC - float2(ts.x, 0), zL);
    float3 ddy = useU
        ? UVToView(uvC + float2(0, -ts.y), zU) - pC
        : pC - UVToView(uvC + float2(0, ts.y), zD);

    return normalize(cross(ddy, ddx));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y) return;

    int2   coord = int2(DTid.xy);
    float2 ts    = 1.0 / float2(ScreenDims);
    float2 uv    = (float2(coord) + 0.5) * ts;

    // Skip sky/undrawn (reversed-Z: sky=0.0, near=1.0)
    float raw = DepthTex.Load(int3(coord, 0));
    if (raw < 0.0001) { Output[coord] = 1.0; return; }

    float z = LinearDepth.Load(int3(coord, 0));
    if (z < FPDepthThreshold) { Output[coord] = 1.0; return; }

    float3 P = UVToView(uv, z);
    float3 N = ReconstructNormal(coord, ts);

    // World-space radius -> pixel radius
    float pixR = AORadius * ProjMatrix[0][0] / z * float(ScreenDims.x) * 0.5;
    pixR = clamp(pixR, 2.0, 256.0);

    // Blue noise jitter (Roberts 2018 R2 sequence with golden-ratio temporal offset)
    float4 bn = BlueNoise.Load(int3(int2(DTid.xy % 128), 0));
    float jitter = frac(bn.x + float(FrameIndex) * 0.6180339887);

    float totalOcclusion = 0.0;

    for (int d = 0; d < NumDirections; d++)
    {
        float angle = (PI / float(NumDirections)) * (float(d) + 0.5) + jitter * PI;
        float2 dir  = float2(cos(angle), sin(angle));
        float step  = pixR / float(NumSteps);

        // Standard bitmask: start with 0 (nothing occluded)
        uint occMask = 0;

        for (int s = 1; s <= NumSteps; s++)
        {
            int2 sc = coord + int2(dir * (float(s) * step));
            sc = clamp(sc, 0, int2(ScreenDims) - 1);

            float sRaw = DepthTex.Load(int3(sc, 0));
            if (sRaw < 0.0001) continue;

            float  sZ  = LinearDepth.Load(int3(sc, 0));
            float3 sP  = UVToView((float2(sc) + 0.5) * ts, sZ);
            float3 vec = sP - P;
            float  dist = length(vec);

            if (dist > AORadius * 2.0 || dist < 0.01) continue;

            float3 hDir = vec / dist;
            float  elev = asin(saturate(dot(hDir, N)));
            float  fall = saturate(1.0 - dist / (AORadius * 2.0));

            if (fall > 0.2)
            {
                // Set bits [0..elevBit] as occluded
                uint bit  = min(uint(saturate(elev / HALF_PI) * float(BITS)), BITS - 1);
                uint mask = (1u << (bit + 1)) - 1;
                occMask |= mask;
            }
        }

        // Occlusion for this direction = fraction of bits set
        totalOcclusion += float(countbits(occMask)) / float(BITS);
    }

    totalOcclusion /= float(NumDirections);

    // Output: 1.0 = fully lit, 0.0 = fully occluded
    float visibility = saturate(1.0 - totalOcclusion * AOIntensity);
    Output[coord] = visibility;
}
