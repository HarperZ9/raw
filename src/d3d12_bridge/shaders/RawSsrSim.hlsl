// RawSsrSim.hlsl: SM 6.6 / 6.10 GPU Compute GGX Stochastic Screen-Space Reflections (SSR) Solver.
// Implements Heitz (2018) GGX VNDF stochastic importance sampling (pure mirror below roughness 0.05)
// with IGN-jittered adaptive linear view-space raymarching (up to 40 steps) + 5-step binary refinement,
// depth-thickness tolerance, smooth screen-edge vignette, and backface/sky rejection.
// Original clean-room implementation, MIT License.
//
// Standard-Z depth convention (0 = near, 1 = far; >= 0.99998 treated as sky -> (0, 0, 0, 0)).
// View-space convention: Left-Handed DX12 (+X right, +Y up, +Z forward).

Texture2D<float4>   gSceneColor   : register(t0);
Texture2D<float>    gSceneDepth   : register(t1);
Texture2D<float>    gRoughnessMap : register(t2);
RWTexture2D<float4> gSsrOut       : register(u0); // rgb = reflectedRadiance, a = hitConfidence [0..1]

SamplerState        gPointClamp   : register(s0);
SamplerState        gLinearClamp  : register(s1);

cbuffer SsrCB : register(b0)
{
    row_major float4x4 gProj;         // View -> Clip (row-vector mul(float4(v, 1), gProj))
    row_major float4x4 gInvProj;      // Clip -> View (row-vector mul(float4(ndc, 1), gInvProj))
    float4             gRayParams;    // x = maxRayDist, y = thicknessM, z = nearZ, w = startBias
    float4             gRoughParams;  // x = fallbackRoughness, y = maxRoughness, z = edgeFadeWidth, w = frameJitter
    uint4              gDimsAndFlags; // x = width, y = height, z = maxSteps (8..40), w = flags (bit0=hasRoughMap)
};

static const float kTwoPi = 6.28318530718f;

float3 SanitizeRgb(float3 c)
{
    const float3 clean = float3(
        isfinite(c.x) ? c.x : 0.0f,
        isfinite(c.y) ? c.y : 0.0f,
        isfinite(c.z) ? c.z : 0.0f);
    return max(clean, 0.0f.xxx);
}

float InterleavedGradientNoise(float2 px, float frameJitter)
{
    const float2 p = px + float2(frameJitter * 5.588238f, frameJitter * 9.042254f);
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

float3 ReconstructViewPos(float2 uv, float depth)
{
    const float  ndcX = uv.x * 2.0f - 1.0f;
    const float  ndcY = 1.0f - uv.y * 2.0f;
    const float4 vp4  = mul(float4(ndcX, ndcY, depth, 1.0f), gInvProj);
    const float  invW = (abs(vp4.w) > 1.0e-7f) ? (1.0f / vp4.w) : 1.0f;
    return vp4.xyz * invW;
}

float ReconstructViewZ(float2 uv, float depth)
{
    const float  ndcX = uv.x * 2.0f - 1.0f;
    const float  ndcY = 1.0f - uv.y * 2.0f;
    const float4 vp4  = mul(float4(ndcX, ndcY, depth, 1.0f), gInvProj);
    const float  invW = (abs(vp4.w) > 1.0e-7f) ? (1.0f / vp4.w) : 1.0f;
    return vp4.z * invW;
}

float3 ProjectViewToScreen(float3 viewP)
{
    const float4 c4   = mul(float4(viewP, 1.0f), gProj);
    const float  invW = (abs(c4.w) > 1.0e-7f) ? (1.0f / c4.w) : 1.0f;
    const float3 ndc  = c4.xyz * invW;
    return float3(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, ndc.z);
}

float3 ReconstructViewNormal(uint2 px, float depthC, float3 posC, uint2 dims, float2 invDims)
{
    const uint2 pxR = uint2(min(px.x + 1u, dims.x - 1u), px.y);
    const uint2 pxL = uint2((px.x > 0u) ? (px.x - 1u) : 0u, px.y);
    const uint2 pxD = uint2(px.x, min(px.y + 1u, dims.y - 1u));
    const uint2 pxU = uint2(px.x, (px.y > 0u) ? (px.y - 1u) : 0u);

    const float dR = gSceneDepth.Load(int3(pxR, 0));
    const float dL = gSceneDepth.Load(int3(pxL, 0));
    const float dD = gSceneDepth.Load(int3(pxD, 0));
    const float dU = gSceneDepth.Load(int3(pxU, 0));

    float3 dPdx;
    if (abs(dR - depthC) <= abs(dL - depthC) && dR < 0.99998f)
        dPdx = ReconstructViewPos((float2(pxR) + 0.5f) * invDims, dR) - posC;
    else if (dL < 0.99998f)
        dPdx = posC - ReconstructViewPos((float2(pxL) + 0.5f) * invDims, dL);
    else
        dPdx = float3(1.0f, 0.0f, 0.0f);

    float3 dPdy;
    if (abs(dD - depthC) <= abs(dU - depthC) && dD < 0.99998f)
        dPdy = ReconstructViewPos((float2(pxD) + 0.5f) * invDims, dD) - posC;
    else if (dU < 0.99998f)
        dPdy = posC - ReconstructViewPos((float2(pxU) + 0.5f) * invDims, dU);
    else
        dPdy = float3(0.0f, -1.0f, 0.0f);

    float3 n = cross(dPdy, dPdx);
    const float lenSq = dot(n, n);
    if (lenSq < 1.0e-12f)
        return float3(0.0f, 0.0f, -1.0f);
    n *= rsqrt(lenSq);

    // Ensure normal strictly faces camera (camera sits at view-space origin)
    if (dot(n, -posC) < 0.0f)
        n = -n;
    return n;
}

// Heitz (2018) GGX Visible Normal Distribution Function (VNDF) importance sampling
float3 SampleGgxVndfHalfVector(float3 V, float3 N, float roughness, float u1, float u2)
{
    const float3 upRef = (abs(N.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 T     = normalize(cross(upRef, N));
    const float3 B     = cross(N, T);

    const float3 Ve = float3(dot(V, T), dot(V, B), max(dot(V, N), 1.0e-4f));
    const float  a  = clamp(roughness * roughness, 0.0025f, 1.0f);

    // Stretch view direction into hemisphere configuration
    const float3 Vh = normalize(float3(a * Ve.x, a * Ve.y, Ve.z));
    const float  lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    const float3 T1 = (lensq > 1.0e-8f) ? (float3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq)) : float3(1.0f, 0.0f, 0.0f);
    const float3 T2 = cross(Vh, T1);

    // Parameterization of the projected area
    const float r   = sqrt(clamp(u1, 1.0e-4f, 0.9999f));
    const float phi = kTwoPi * u2;
    float sPhi, cPhi;
    sincos(phi, sPhi, cPhi);
    const float t1 = r * cPhi;
    float       t2 = r * sPhi;
    const float s  = 0.5f * (1.0f + Vh.z);
    t2 = (1.0f - s) * sqrt(max(0.0f, 1.0f - t1 * t1)) + s * t2;

    const float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;
    const float3 Ne = normalize(float3(a * Nh.x, a * Nh.y, max(0.0f, Nh.z)));
    return normalize(Ne.x * T + Ne.y * B + Ne.z * N);
}

[numthreads(8, 8, 1)]
void cs_ssr_trace(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 dims = gDimsAndFlags.xy;
    if (dtid.x >= dims.x || dtid.y >= dims.y)
        return;

    const float depthC = gSceneDepth.Load(int3(dtid.xy, 0));
    // Sky pixels (>= 0.99998) or invalid depth return zero reflection & confidence
    if (depthC >= 0.99998f || depthC <= 0.0f || !isfinite(depthC))
    {
        gSsrOut[dtid.xy] = 0.0f.xxxx;
        return;
    }

    const float2 invDims = 1.0f / max(float2(dims), 1.0f.xx);
    const float2 uv      = (float2(dtid.xy) + 0.5f) * invDims;

    const bool  hasRoughMap = (gDimsAndFlags.w & 1u) != 0u;
    const float roughness   = saturate(hasRoughMap
        ? gRoughnessMap.SampleLevel(gPointClamp, uv, 0.0f)
        : gRoughParams.x);
    const float maxRough    = max(gRoughParams.y, 0.05f);
    if (roughness >= maxRough)
    {
        gSsrOut[dtid.xy] = 0.0f.xxxx;
        return;
    }

    const float3 viewPos = ReconstructViewPos(uv, depthC);
    const float3 N       = ReconstructViewNormal(dtid.xy, depthC, viewPos, dims, invDims);
    const float3 I       = normalize(viewPos); // Camera -> surface
    const float3 V       = -I;                 // Surface -> camera

    const float ign0 = InterleavedGradientNoise(float2(dtid.xy), gRoughParams.w);
    const float ign1 = InterleavedGradientNoise(float2(dtid.xy) + float2(17.0f, 31.0f), gRoughParams.w + 1.0f);

    float3 R;
    if (roughness < 0.05f)
    {
        // Pure mirror reflection
        R = normalize(reflect(I, N));
    }
    else
    {
        const float3 H = SampleGgxVndfHalfVector(V, N, roughness, ign0, ign1);
        R = normalize(reflect(I, H));
        if (dot(R, N) <= 0.001f)
        {
            gSsrOut[dtid.xy] = 0.0f.xxxx;
            return;
        }
    }

    // Reject rays pointing strictly back into the camera lens
    if (R.z < -0.92f)
    {
        gSsrOut[dtid.xy] = 0.0f.xxxx;
        return;
    }

    const float maxRayDist = max(gRayParams.x, 1.0f);
    const float thicknessM = max(gRayParams.y, 0.05f);
    const float nearZ      = max(gRayParams.z, 0.01f);
    const float startBias  = max(gRayParams.w, 0.002f);

    const float3 rayOrigin = viewPos + N * startBias;
    float raySpan = maxRayDist;
    if (rayOrigin.z + R.z * raySpan < nearZ + 0.02f && R.z < -1.0e-5f)
    {
        raySpan = max(0.0f, (nearZ + 0.02f - rayOrigin.z) / R.z);
    }
    if (raySpan < 0.05f)
    {
        gSsrOut[dtid.xy] = 0.0f.xxxx;
        return;
    }

    const uint  maxSteps = clamp(gDimsAndFlags.z, 8u, 40u);
    const float invSteps = 1.0f / (float)maxSteps;

    bool   hitFound   = false;
    float  prevDist   = 0.0f;
    float  hitLo      = 0.0f;
    float  hitHi      = 0.0f;
    float2 bestUv     = uv;
    float  bestDiff   = thicknessM;
    float  bestSceneD = 1.0f;

    // Adaptive quadratic step distribution: fine steps near contact origin, long reach to horizon
    [loop]
    for (uint step = 0u; step < maxSteps; ++step)
    {
        const float tNorm = ((float)step + 0.15f + 0.70f * ign0) * invSteps;
        const float curDist = raySpan * (0.28f * tNorm + 0.72f * tNorm * tNorm);
        const float3 pV = rayOrigin + R * curDist;
        if (pV.z <= nearZ)
            break;

        const float3 scr = ProjectViewToScreen(pV);
        if (scr.x < 0.0f || scr.x > 1.0f || scr.y < 0.0f || scr.y > 1.0f || scr.z <= 0.0f || scr.z >= 1.0f)
            break;

        const float sceneD = gSceneDepth.SampleLevel(gPointClamp, scr.xy, 0.0f);
        if (sceneD < 0.99998f && sceneD > 0.0f)
        {
            const float sampleZ = ReconstructViewZ(scr.xy, sceneD);
            const float depthDiff = pV.z - sampleZ;
            if (depthDiff > 0.0f && depthDiff < thicknessM)
            {
                hitFound   = true;
                hitLo      = prevDist;
                hitHi      = curDist;
                bestUv     = scr.xy;
                bestDiff   = depthDiff;
                bestSceneD = sceneD;
                break;
            }
        }
        prevDist = curDist;
    }

    if (!hitFound)
    {
        gSsrOut[dtid.xy] = 0.0f.xxxx;
        return;
    }

    // 5-step binary refinement to pin exact contact intersection
    [unroll]
    for (uint b = 0u; b < 5u; ++b)
    {
        const float midDist = 0.5f * (hitLo + hitHi);
        const float3 midV = rayOrigin + R * midDist;
        const float3 scr  = ProjectViewToScreen(midV);
        if (scr.x < 0.0f || scr.x > 1.0f || scr.y < 0.0f || scr.y > 1.0f)
            break;
        const float sceneD = gSceneDepth.SampleLevel(gPointClamp, scr.xy, 0.0f);
        if (sceneD < 0.99998f && sceneD > 0.0f)
        {
            const float sampleZ = ReconstructViewZ(scr.xy, sceneD);
            const float diff = midV.z - sampleZ;
            if (diff > 0.0f)
            {
                hitHi = midDist;
                if (diff < thicknessM)
                {
                    bestUv     = scr.xy;
                    bestDiff   = diff;
                    bestSceneD = sceneD;
                }
            }
            else
            {
                hitLo = midDist;
            }
        }
        else
        {
            hitLo = midDist;
        }
    }

    if (bestSceneD >= 0.99998f || bestDiff < 0.0f || bestDiff >= thicknessM)
    {
        gSsrOut[dtid.xy] = 0.0f.xxxx;
        return;
    }

    // Smooth screen-edge vignette, thickness fade, roughness fade, and backward-ray fade
    const float edgeW      = max(gRoughParams.z, 0.01f);
    const float edgeDist   = min(min(bestUv.x, 1.0f - bestUv.x), min(bestUv.y, 1.0f - bestUv.y));
    const float edgeFade   = smoothstep(0.0f, edgeW, edgeDist);
    const float thickFade  = 1.0f - smoothstep(thicknessM * 0.65f, thicknessM, bestDiff);
    const float roughFade  = 1.0f - smoothstep(maxRough * 0.65f, maxRough, roughness);
    const float rayDirFade = saturate(1.0f + R.z * 1.15f);
    const float confidence = saturate(edgeFade * thickFade * roughFade * rayDirFade);

    const float3 reflRgb = SanitizeRgb(gSceneColor.SampleLevel(gLinearClamp, bestUv, 0.0f).rgb);
    gSsrOut[dtid.xy] = float4(reflRgb, confidence);
}
