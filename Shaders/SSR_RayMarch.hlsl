// Screen-space ray march for reflections.
// Reference: McGuire & Mara 2014, "Efficient GPU Screen-Space Ray Tracing"
// Uses Hi-Z pyramid for coarse-to-fine acceleration.
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Output: float4(hitUV.xy, hitViewZ, confidence)

cbuffer RayMarchCB : register(b0)
{
    float4x4 ProjMatrix;       // Projection matrix (row-major)
    float4x4 InvProjMatrix;    // Inverse projection (for unproject)
    uint2    HalfDims;         // Half-res output dimensions
    uint2    ScreenDims;       // Full-res dimensions
    float    NearZ;
    float    FarZ;
    float    MaxDistance;       // World-space max ray distance
    float    Thickness;        // Depth comparison thickness
    int      MaxSteps;         // Max march iterations
    int      MipCount;         // Hi-Z pyramid mip count
    uint     FrameIndex;       // Temporal jitter
    float    FPDepthThreshold; // First-person depth mask (view-space units)
};

Texture2D<float> HiZTex : register(t0);       // Hi-Z pyramid (all mip levels)
Texture2D<float4> BlueNoise : register(t30); // 128x128 R2 quasi-random blue noise
Texture2D<float> LinearDepth : register(t31); // Pre-computed linearized depth
SamplerState PointSamp   : register(s0);     // Point sampler

RWTexture2D<float4> HitOutput : register(u0); // UV.xy + viewZ + confidence

static const float PI = 3.14159265359;

float3 UVToViewPos(float2 uv, float linearZ)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float3 viewPos;
    viewPos.x = ndc.x * linearZ / ProjMatrix[0][0];
    viewPos.y = ndc.y * linearZ / ProjMatrix[1][1];
    viewPos.z = linearZ;
    return viewPos;
}

float3 ViewPosToUV(float3 viewPos)
{
    // Project view-space position to UV
    float2 ndc;
    ndc.x = viewPos.x * ProjMatrix[0][0] / viewPos.z;
    ndc.y = viewPos.y * ProjMatrix[1][1] / viewPos.z;
    float2 uv;
    uv.x = ndc.x * 0.5 + 0.5;
    uv.y = 1.0 - (ndc.y * 0.5 + 0.5);
    return float3(uv, viewPos.z);
}

float3 ReconstructNormal(int2 fullCoord, float2 texelSize)
{
    float zC = LinearDepth.Load(int3(fullCoord, 0));
    float zL = LinearDepth.Load(int3(fullCoord + int2(-1, 0), 0));
    float zR = LinearDepth.Load(int3(fullCoord + int2( 1, 0), 0));
    float zU = LinearDepth.Load(int3(fullCoord + int2( 0,-1), 0));
    float zD = LinearDepth.Load(int3(fullCoord + int2( 0, 1), 0));

    float2 uvC = (float2(fullCoord) + 0.5) * texelSize;
    float3 pC = UVToViewPos(uvC, zC);

    bool useR = abs(zR - zC) < abs(zL - zC);
    bool useU = abs(zU - zC) < abs(zD - zC);

    float3 ddx = useR
        ? UVToViewPos(uvC + float2(texelSize.x, 0), zR) - pC
        : pC - UVToViewPos(uvC - float2(texelSize.x, 0), zL);
    float3 ddy = useU
        ? UVToViewPos(uvC + float2(0, -texelSize.y), zU) - pC
        : pC - UVToViewPos(uvC + float2(0, texelSize.y), zD);

    return normalize(cross(ddy, ddx));
}

// ── Blue noise temporal jitter (R2 quasi-random, Roberts 2018) ───────────
static const float GOLDEN_RATIO = 0.6180339887;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 halfCoord = int2(DTid.xy);
    int2 fullCoord = halfCoord * 2;
    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv = (float2(fullCoord) + 0.5) * texelSize;

    float rawDepth = HiZTex.Load(int3(fullCoord, 0));

    // Skip sky (reversed-Z: sky=0)
    if (rawDepth < 0.0001)
    {
        HitOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Skip first-person geometry — no reflections on close-range FP model
    float linearZ = LinearDepth.Load(int3(fullCoord, 0));
    {
        if (linearZ < FPDepthThreshold)
        {
            HitOutput[halfCoord] = float4(0, 0, 0, 0);
            return;
        }
    }

    float3 viewPos = UVToViewPos(uv, linearZ);
    float3 viewNormal = ReconstructNormal(fullCoord, texelSize);

    // View direction (from camera to pixel in view space)
    float3 viewDir = normalize(viewPos);

    // Reflect
    float3 reflDir = reflect(viewDir, viewNormal);

    // Skip reflections pointing toward camera
    if (reflDir.z < 0.01)
    {
        HitOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Project reflected ray endpoint for screen-space direction
    float3 rayEnd = viewPos + reflDir * MaxDistance;
    float3 rayEndUV = ViewPosToUV(rayEnd);

    // Screen-space ray direction
    float2 rayDirSS = rayEndUV.xy - uv;
    float rayLenSS = length(rayDirSS);

    if (rayLenSS < 0.001)
    {
        HitOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Normalize and determine step count based on screen-space length
    float2 rayStepSS = rayDirSS / rayLenSS;

    // Blue noise temporal jitter — R2 quasi-random for better convergence
    float4 bn = BlueNoise.Load(int3(int2(DTid.xy % 128), 0));
    float jitter = frac(bn.x + float(FrameIndex) * GOLDEN_RATIO) * 0.5;

    // Hi-Z ray march: start at a reasonable mip and step coarsely
    float confidence = 0.0;
    float2 hitUV = float2(0, 0);
    float hitViewZ = 0;

    // Step size in UV space — at mip 0 this is ~1 pixel
    float baseStep = max(texelSize.x, texelSize.y);

    // Start ray well clear of the starting surface to prevent self-intersection.
    // The initial offset must be large enough that the ray clears the surface's
    // depth tolerance, but small enough to not miss thin nearby reflectors.
    float startOffset = max(baseStep * viewPos.z * 32.0, Thickness * 2.0);
    float3 currentPos = viewPos + reflDir * startOffset * (1.0 + jitter);
    float traveled = startOffset;

    // Minimum travel distance before accepting hits (anti-self-intersection)
    float minTravel = Thickness * 4.0;

    int currentMip = min(MipCount - 1, 3);  // Start at mip 3 for coarse march

    for (int i = 0; i < MaxSteps; i++)
    {
        float3 currentUV = ViewPosToUV(currentPos);

        // Out of screen?
        if (currentUV.x < 0.0 || currentUV.x > 1.0 ||
            currentUV.y < 0.0 || currentUV.y > 1.0 ||
            currentUV.z < 0.0)
        {
            break;
        }

        // Sample depth — use Hi-Z at coarse mips, linear depth at mip 0
        int2 sampleCoord = int2(currentUV.xy * float2(ScreenDims));
        float sceneZ;
        if (currentMip > 0) {
            // Coarse march: read Hi-Z at reduced resolution (reversed-Z)
            // Convert reversed-Z to linear for comparison
            int2 mipCoord = sampleCoord >> currentMip;
            float rawZ = HiZTex.Load(int3(mipCoord, currentMip));
            // reversed-Z linearization: N*F / (N + z*(F-N))
            sceneZ = (rawZ < 0.0001) ? FarZ : NearZ * FarZ / (NearZ + rawZ * (FarZ - NearZ));
        } else {
            // Fine march: use pre-computed linear depth for precision
            sceneZ = LinearDepth.Load(int3(sampleCoord, 0));
        }
        float rayZ = currentUV.z;

        // Hit test: ray is behind the surface within thickness tolerance
        float depthDiff = rayZ - sceneZ;
        float thicknessScale = Thickness * (1.0 + traveled * 0.005);

        if (depthDiff > 0 && depthDiff < thicknessScale && traveled > minTravel)
        {
            // Refine: step down mip levels
            if (currentMip > 0)
            {
                currentMip--;
                continue;
            }

            // Hit confirmed at mip 0
            hitUV = currentUV.xy;
            hitViewZ = sceneZ;

            // Confidence: fade at screen edges, distance, and hit quality
            float2 edgeFade = smoothstep(0.0, 0.1, currentUV.xy) *
                              smoothstep(0.0, 0.1, 1.0 - currentUV.xy);
            float edgeConf = edgeFade.x * edgeFade.y;
            float distConf = saturate(1.0 - traveled / MaxDistance);
            float hitConf = saturate(1.0 - depthDiff / thicknessScale); // Closer hits = higher confidence
            confidence = edgeConf * distConf * hitConf;
            break;
        }

        // Only step mip up if clearly in front (not just at a boundary)
        // Use a small threshold to prevent oscillating between mip levels
        if (depthDiff < -thicknessScale * 0.5 && currentMip < min(MipCount - 1, 2))
        {
            currentMip++;
        }

        // Advance ray — scale step by mip level and view distance
        float stepLen = baseStep * float(1 << currentMip) * currentPos.z;
        currentPos += reflDir * stepLen;
        traveled += stepLen;

        if (traveled > MaxDistance)
            break;
    }

    HitOutput[halfCoord] = float4(hitUV, hitViewZ, confidence);
}
