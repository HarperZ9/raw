// RawGtaoSim.hlsl: SM 6.6 / 6.10 GPU Compute Jimenez (SIGGRAPH 2016) Ground-Truth Ambient Occlusion
// (GTAO) + Unoccluded Bent-Normal & Depth-Aware 3x3 Cross-Bilateral Spatial Denoise Solver.
//
// Standard-Z depth convention (0.0 = near, 1.0 = far; sky >= 0.99998).
// Left-handed DX12 view space (+X right, +Y up, +Z forward; view vector to camera V = normalize(-viewPos)).
//
// Entry points:
//   1. cs_gtao_main    ([numthreads(8, 8, 1)]) - 2-slice x 6-step horizon search, analytical cosine integral & bent normal.
//   2. cs_gtao_denoise ([numthreads(8, 8, 1)]) - 3x3 depth-aware cross-bilateral filter & bent-normal renormalization.
//
// Original clean-room implementation, MIT License.

static const float kPi     = 3.14159265359f;
static const float kHalfPi = 1.57079632679f;
static const float kSkyZ   = 0.99998f;

cbuffer GtaoCB : register(b0)
{
    uint4  gDimsAndSteps;    // x = width, y = height, z = numSlices (default 2), w = numSteps (default 6)
    float4 gInvDimsAndProj;  // x = 1/w, y = 1/h, z = tanHalfFovX, w = tanHalfFovY
    float4 gDepthParams;     // x = nearZ, y = farZ, z = effectRadius, w = falloffStart (0..0.95)
    float4 gFilterParams;    // x = aoPower, y = maxScreenRadiusPx, z = depthEdgeScale, w = temporalJitter
};

Texture2D<float>    gSceneDepth : register(t0);
Texture2D<float4>   gRawInput   : register(t1); // Used by cs_gtao_denoise (bound to dummy/raw during main)
RWTexture2D<float4> gOutTex     : register(u0); // RGBA16F: xyz = bentNormal (view space), w = visibility [0..1]

float LinearizeDepth(float d)
{
    const float nearZ = gDepthParams.x;
    const float farZ  = gDepthParams.y;
    const float denom = max(farZ - d * (farZ - nearZ), 1.0e-5f);
    return (nearZ * farZ) / denom;
}

float3 ReconstructViewPos(int2 px, out float outRawDepth)
{
    const int2 clamped = clamp(px, int2(0, 0), int2((int)gDimsAndSteps.x - 1, (int)gDimsAndSteps.y - 1));
    outRawDepth = gSceneDepth.Load(int3(clamped, 0));
    const float viewZ = LinearizeDepth(outRawDepth);
    const float2 uv   = (float2(clamped) + 0.5f) * gInvDimsAndProj.xy;
    const float ndcX  = uv.x * 2.0f - 1.0f;
    const float ndcY  = 1.0f - uv.y * 2.0f;
    return float3(ndcX * gInvDimsAndProj.z * viewZ,
                  ndcY * gInvDimsAndProj.w * viewZ,
                  viewZ);
}

float InterleavedGradientNoise(float2 pos)
{
    return frac(52.9829189f * frac(dot(pos, float2(0.06711056f, 0.00583715f))));
}

// Jimenez (2016) Eq. 7 analytical cosine-weighted arc integral:
//   F(h, n) = 0.25 * (-cos(2*h - n) + cos(n) + 2*h*sin(n))
float IntegrateArc(float h, float n)
{
    return 0.25f * (-cos(2.0f * h - n) + cos(n) + 2.0f * h * sin(n));
}

// ============================================================================
// Entry 1: Jimenez (2016) 2-Slice x 6-Step GTAO + Unoccluded Bent Normal
// ============================================================================
[numthreads(8, 8, 1)]
void cs_gtao_main(uint3 dtid : SV_DispatchThreadID)
{
    const uint width  = gDimsAndSteps.x;
    const uint height = gDimsAndSteps.y;
    if (dtid.x >= width || dtid.y >= height)
        return;

    const int2 px = int2(dtid.xy);
    float dC = 1.0f;
    const float3 P0 = ReconstructViewPos(px, dC);

    // Sky pixels pass through cleanly: bentNormal = (0, 0, -1), visibility = 1.0
    if (dC >= kSkyZ || !isfinite(dC))
    {
        gOutTex[dtid.xy] = float4(0.0f, 0.0f, -1.0f, 1.0f);
        return;
    }

    // Silhouette-aware view-space normal reconstruction using minimum-depth-gradient finite differences
    float dL, dR, dU, dD;
    const float3 PL = ReconstructViewPos(px + int2(-1,  0), dL);
    const float3 PR = ReconstructViewPos(px + int2( 1,  0), dR);
    const float3 PU = ReconstructViewPos(px + int2( 0, -1), dU);
    const float3 PD = ReconstructViewPos(px + int2( 0,  1), dD);

    const float diffL = (dL < kSkyZ) ? abs(P0.z - PL.z) : 1.0e9f;
    const float diffR = (dR < kSkyZ) ? abs(PR.z - P0.z) : 1.0e9f;
    const float diffU = (dU < kSkyZ) ? abs(PU.z - P0.z) : 1.0e9f;
    const float diffD = (dD < kSkyZ) ? abs(P0.z - PD.z) : 1.0e9f;

    const float3 dPdx = (diffL < diffR) ? (P0 - PL) : (PR - P0);
    const float3 dPdy = (diffD < diffU) ? (P0 - PD) : (PU - P0);

    const float3 V = normalize(-P0); // Surface -> Camera (near (0, 0, -1))
    float3 N = cross(dPdy, dPdx);    // LH view space: +Y up x +X right = -Z toward camera
    const float nLen = length(N);
    N = (nLen > 1.0e-7f) ? (N / nLen) : V;
    if (dot(N, V) < 0.0f)
        N = -N;

    const float effectRadius = max(gDepthParams.z, 1.0e-3f);
    const float falloffStart = clamp(gDepthParams.w, 0.05f, 0.95f);
    const float falloffRange = max(effectRadius * (1.0f - falloffStart), 1.0e-4f);

    // Project world/view effect radius into screen pixels
    const float focalPxX = 0.5f * (float)width / max(gInvDimsAndProj.z, 1.0e-4f);
    const float screenRadiusPx = clamp((effectRadius / max(P0.z, 0.05f)) * focalPxX, 2.0f, gFilterParams.y);

    const uint  numSlices = clamp(gDimsAndSteps.z, 1u, 4u);
    const uint  numSteps  = clamp(gDimsAndSteps.w, 2u, 12u);
    const float ignAngle  = InterleavedGradientNoise(float2(dtid.xy) + gFilterParams.w * 17.0f);
    const float ignStep   = InterleavedGradientNoise(float2(dtid.xy) * 1.73f + float2(13.1f, 7.7f) + gFilterParams.w * 11.0f);

    float  visAccum  = 0.0f;
    float3 bentAccum = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for (uint s = 0u; s < numSlices; ++s)
    {
        const float phi = ((float)s + ignAngle) * (kPi / (float)numSlices);
        const float2 omega = float2(cos(phi), sin(phi)); // View-space (+X right, +Y up)

        // Orthonormal basis inside slice plane: V (angle 0), T (angle +pi/2), axisVec (plane normal)
        const float3 dirVec   = float3(omega.x, omega.y, 0.0f);
        const float3 orthoDir = dirVec - dot(dirVec, V) * V;
        const float  orthoLen = length(orthoDir);
        const float3 T        = (orthoLen > 1.0e-5f) ? (orthoDir / orthoLen) : float3(1.0f, 0.0f, 0.0f);
        const float3 axisVec  = normalize(cross(T, V));

        const float  nDotAxis = dot(N, axisVec);
        const float3 projN    = N - axisVec * nDotAxis;
        const float  projNLen = max(length(projN), 1.0e-5f);
        const float3 projNUnit = projN / projNLen;

        const float cosN = clamp(dot(projNUnit, V), -1.0f, 1.0f);
        const float sinN = dot(projNUnit, T);
        const float n    = atan2(sinN, cosN); // Signed angle in [-pi/2, +pi/2]

        float c1 = -1.0f; // Max horizon cos along -omega
        float c2 = -1.0f; // Max horizon cos along +omega

        // Screen-space step vector (+omega.x right, -omega.y down in screen pixel coordinates)
        const float2 stepDirPx = float2(omega.x, -omega.y);

        [loop]
        for (uint st = 0u; st < numSteps; ++st)
        {
            const float uStep  = ((float)st + 0.25f + 0.5f * ignStep) / (float)numSteps;
            const float rPx    = max(1.0f, uStep * uStep * screenRadiusPx);
            const int2  offPx  = int2(round(stepDirPx * rPx));

            // Negative side (-omega)
            {
                float dS = 1.0f;
                const float3 PS = ReconstructViewPos(px - offPx, dS);
                if (dS < kSkyZ)
                {
                    const float3 delta = PS - P0;
                    const float  dist  = length(delta);
                    if (dist > 1.0e-4f)
                    {
                        const float hCos    = dot(delta, V) / dist;
                        const float falloff = saturate((effectRadius - dist) / falloffRange);
                        c1 = max(c1, lerp(-1.0f, hCos, falloff));
                    }
                }
            }

            // Positive side (+omega)
            {
                float dS = 1.0f;
                const float3 PS = ReconstructViewPos(px + offPx, dS);
                if (dS < kSkyZ)
                {
                    const float3 delta = PS - P0;
                    const float  dist  = length(delta);
                    if (dist > 1.0e-4f)
                    {
                        const float hCos    = dot(delta, V) / dist;
                        const float falloff = saturate((effectRadius - dist) / falloffRange);
                        c2 = max(c2, lerp(-1.0f, hCos, falloff));
                    }
                }
            }
        }

        // Convert horizon cosines to signed angles relative to V, clamped to the surface hemisphere [n - pi/2, n + pi/2]
        float h1 = -acos(clamp(c1, -1.0f, 1.0f));
        float h2 =  acos(clamp(c2, -1.0f, 1.0f));
        h1 = n + max(h1 - n, -kHalfPi);
        h2 = n + min(h2 - n,  kHalfPi);
        h1 = min(h1, 0.0f);
        h2 = max(h2, 0.0f);

        const float sliceVis = projNLen * (IntegrateArc(h1, n) + IntegrateArc(h2, n));
        visAccum += sliceVis;

        // Average unoccluded bent direction inside this slice plane, lifted back to 3D view space
        const float  bentAngle = 0.5f * (h1 + h2);
        const float3 sliceBent = (cos(bentAngle) * V + sin(bentAngle) * T) * projNLen + axisVec * nDotAxis;
        bentAccum += sliceBent;
    }

    float visibility = saturate(visAccum / (float)numSlices);
    visibility = pow(max(visibility, 0.0f), max(gFilterParams.x, 0.1f));

    const float bentLen = length(bentAccum);
    const float3 bentNormal = (bentLen > 1.0e-5f) ? (bentAccum / bentLen) : N;

    gOutTex[dtid.xy] = float4(bentNormal, visibility);
}

// ============================================================================
// Entry 2: 3x3 Depth-Aware Cross-Bilateral Denoise + Bent-Normal Renormalize
// ============================================================================
[numthreads(8, 8, 1)]
void cs_gtao_denoise(uint3 dtid : SV_DispatchThreadID)
{
    const uint width  = gDimsAndSteps.x;
    const uint height = gDimsAndSteps.y;
    if (dtid.x >= width || dtid.y >= height)
        return;

    const int2 px = int2(dtid.xy);
    const float dC = gSceneDepth.Load(int3(px, 0));
    if (dC >= kSkyZ || !isfinite(dC))
    {
        gOutTex[dtid.xy] = float4(0.0f, 0.0f, -1.0f, 1.0f);
        return;
    }

    const float zC        = LinearizeDepth(dC);
    const float edgeScale = max(zC * max(gFilterParams.z, 1.0e-4f), 0.04f);

    static const float kGauss[3] = { 1.0f, 2.0f, 1.0f };

    float3 bentSum = float3(0.0f, 0.0f, 0.0f);
    float  visSum  = 0.0f;
    float  wSum    = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 sp = clamp(px + int2(dx, dy), int2(0, 0), int2((int)width - 1, (int)height - 1));
            const float dN = gSceneDepth.Load(int3(sp, 0));
            if (dN >= kSkyZ || !isfinite(dN))
                continue;

            const float  zN = LinearizeDepth(dN);
            const float  wSpatial = kGauss[dx + 1] * kGauss[dy + 1];
            const float  wDepth   = exp(-abs(zN - zC) / edgeScale);
            const float  w        = wSpatial * wDepth;

            const float4 rawSample = gRawInput.Load(int3(sp, 0));
            if (all(isfinite(rawSample)))
            {
                bentSum += rawSample.xyz * w;
                visSum  += rawSample.w * w;
                wSum    += w;
            }
        }
    }

    if (wSum > 1.0e-5f)
    {
        const float3 bn = bentSum / wSum;
        const float  bl = length(bn);
        const float3 outBent = (bl > 1.0e-5f) ? (bn / bl) : float3(0.0f, 0.0f, -1.0f);
        gOutTex[dtid.xy] = float4(outBent, saturate(visSum / wSum));
    }
    else
    {
        gOutTex[dtid.xy] = gRawInput.Load(int3(px, 0));
    }
}
