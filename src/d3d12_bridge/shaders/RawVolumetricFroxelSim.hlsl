// RawVolumetricFroxelSim.hlsl: SM 6.6 / 6.10 True 3D 160x90x64 Exponential-Z Volumetric Froxel Fog,
// Altitude-Stratified Mountain/Valley Mist, Henyey-Greenstein Mie Scattering, Local Point-Light
// Scattering & Temporal Reprojection GPU Compute Pipeline.
//
// Analytical slice integration follows Sebastien Hillaire (Frostbite 2015) with exact Taylor limit
// as sigma_t -> 0 (zero 0/0 NaNs).
//
// Entry points:
//   1. cs_froxel_inject    ([numthreads(8, 8, 1)]) - Injects mist density, Mie + local lights & temporal reprojection.
//   2. cs_froxel_integrate ([numthreads(8, 8, 1)]) - Marches front-to-back z=0..D-1 producing (accumScatter, transmittance).
//
// Original clean-room implementation, MIT License.

static const float kPi    = 3.14159265359f;
static const float kInv4Pi = 0.07957747155f;

struct FroxelPointLight
{
    float3 posW;
    float  radius;
    float3 color;
    float  intensity;
};

cbuffer FroxelCB : register(b0)
{
    row_major float4x4 gInvViewProj;       // Current clip -> world (row-vector)
    row_major float4x4 gPrevViewProj;      // Previous world -> clip (row-vector)
    float4             gCamPosAndNear;     // xyz = camPosW, w = nearZ
    float4             gCamFwdAndFar;      // xyz = camForwardW (unit), w = farZ
    float4             gPrevCamPosAndTime; // xyz = prevCamPosW, w = simTimeSec
    float4             gPrevCamFwdAndHist; // xyz = prevCamForwardW (unit), w = historyWeight [0..0.95]
    float4             gFogParams;         // x = baseFogDensity, y = valleyFloorZ, z = fogScaleHeight, w = scatterAlbedo
    float4             gWeatherAndNoise;   // x = rainIntensity, y = snowIntensity, z = noiseAmp, w = noiseScale
    float4             gWindAndPhase;      // xyz = windVelW, w = mieAnisotropyG
    float4             gSunDirAndInt;      // xyz = sunDirW (to sun, unit), w = sunIntensity
    float4             gSunColAndAmbInt;   // xyz = sunColor, w = ambientIntensity
    float4             gAmbColAndJitterZ;  // xyz = ambientColor, w = jitterZ [0..1]
    float4             gJitterXYAndLog;    // xy = jitterXY [-0.5..0.5], z = log(farZ/nearZ), w = 1/log(farZ/nearZ)
    uint4              gGridAndLights;     // x = gridW, y = gridH, z = gridD, w = lightCount (0..16)
    FroxelPointLight   gLights[16];        // 16 * 32 = 512 bytes
};

Texture3D<float4>   gInputVol    : register(t0); // Inject: prev frame inject history; Integrate: current inject volume
RWTexture3D<float4> gOutputVol   : register(u0); // Inject: curr inject volume; Integrate: integrated (scatter, T) volume
SamplerState        gLinearClamp : register(s0);

// Compact hash-based 3D gradient/value noise for wind-advected valley mist wisps (zero trig loops)
float Hash31(float3 p)
{
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise3D(float3 x)
{
    const float3 i = floor(x);
    const float3 f = frac(x);
    const float3 u = f * f * (3.0f - 2.0f * f);

    const float n000 = Hash31(i + float3(0.0f, 0.0f, 0.0f));
    const float n100 = Hash31(i + float3(1.0f, 0.0f, 0.0f));
    const float n010 = Hash31(i + float3(0.0f, 1.0f, 0.0f));
    const float n110 = Hash31(i + float3(1.0f, 1.0f, 0.0f));
    const float n001 = Hash31(i + float3(0.0f, 0.0f, 1.0f));
    const float n101 = Hash31(i + float3(1.0f, 0.0f, 1.0f));
    const float n011 = Hash31(i + float3(0.0f, 1.0f, 1.0f));
    const float n111 = Hash31(i + float3(1.0f, 1.0f, 1.0f));

    const float nx00 = lerp(n000, n100, u.x);
    const float nx10 = lerp(n010, n110, u.x);
    const float nx01 = lerp(n001, n101, u.x);
    const float nx11 = lerp(n011, n111, u.x);
    const float nxy0 = lerp(nx00, nx10, u.y);
    const float nxy1 = lerp(nx01, nx11, u.y);
    return lerp(nxy0, nxy1, u.z);
}

float HenyeyGreenstein(float cosTheta, float g)
{
    const float gc = clamp(g, -0.95f, 0.95f);
    const float g2 = gc * gc;
    const float denom = max(1.0f + g2 - 2.0f * gc * cosTheta, 1.0e-4f);
    return (1.0f - g2) / (4.0f * kPi * denom * sqrt(denom));
}

float3 ComputeRayDirFromUv(float2 uv)
{
    const float ndcX = uv.x * 2.0f - 1.0f;
    const float ndcY = 1.0f - uv.y * 2.0f;
    float4 pNear = mul(float4(ndcX, ndcY, 0.0f, 1.0f), gInvViewProj);
    float4 pFar  = mul(float4(ndcX, ndcY, 1.0f, 1.0f), gInvViewProj);
    const float3 wNear = pNear.xyz / max(abs(pNear.w), 1.0e-6f);
    const float3 wFar  = pFar.xyz / max(abs(pFar.w), 1.0e-6f);
    return normalize(wFar - wNear);
}

float SliceToLinearZ(float u)
{
    return gCamPosAndNear.w * exp(gJitterXYAndLog.z * u);
}

// ============================================================================
// Entry 1: Froxel Density, Mie / Local-Light Scattering & Temporal Reprojection
// ============================================================================
[numthreads(8, 8, 1)]
void cs_froxel_inject(uint3 dtid : SV_DispatchThreadID)
{
    const uint gridW = gGridAndLights.x;
    const uint gridH = gGridAndLights.y;
    const uint gridD = gGridAndLights.z;
    if (dtid.x >= gridW || dtid.y >= gridH || dtid.z >= gridD)
        return;

    const float2 invWH = float2(1.0f / (float)gridW, 1.0f / (float)gridH);
    const float  invD  = 1.0f / (float)gridD;

    // Unjittered froxel center (for clean temporal reprojection) & Halton-jittered sample pos
    const float2 uvCenter = (float2(dtid.xy) + 0.5f) * invWH;
    const float2 uvJitter = saturate((float2(dtid.xy) + 0.5f + gJitterXYAndLog.xy) * invWH);

    const float z0 = SliceToLinearZ((float)dtid.z * invD);
    const float z1 = SliceToLinearZ((float)(dtid.z + 1u) * invD);
    const float zCenter = 0.5f * (z0 + z1);
    const float zSample = lerp(z0, z1, gAmbColAndJitterZ.w);

    const float3 camPos = gCamPosAndNear.xyz;
    const float3 camFwd = gCamFwdAndFar.xyz;

    const float3 rayDirJ = ComputeRayDirFromUv(uvJitter);
    const float  cosViewJ = max(dot(rayDirJ, camFwd), 0.15f);
    const float3 worldPos = camPos + rayDirJ * (zSample / cosViewJ);

    // 1. Altitude-stratified exponential mountain/valley mist + wind-advected 3D mist wisps + weather boost
    const float baseDensity    = max(gFogParams.x, 0.0f);
    const float valleyFloorZ   = gFogParams.y;
    const float fogScaleHeight = max(gFogParams.z, 1.0e-2f);
    const float scatterAlbedo  = saturate(gFogParams.w);

    const float hAbove       = max(0.0f, worldPos.z - valleyFloorZ);
    const float heightFactor = exp(-hAbove / fogScaleHeight);
    const float weatherBoost = 1.0f + max(gWeatherAndNoise.x, 0.0f) * 1.4f + max(gWeatherAndNoise.y, 0.0f) * 1.8f;

    float wispFactor = 1.0f;
    const float noiseAmp = gWeatherAndNoise.z;
    if (noiseAmp > 1.0e-4f)
    {
        const float3 advP = worldPos * gWeatherAndNoise.w - gWindAndPhase.xyz * gPrevCamPosAndTime.w;
        const float n0 = ValueNoise3D(advP);
        const float n1 = ValueNoise3D(advP * 2.03f + float3(11.3f, 5.7f, 2.9f));
        const float fbm = 0.65f * n0 + 0.35f * n1;
        wispFactor = max(0.25f, 1.0f + noiseAmp * (fbm * 2.0f - 1.0f));
    }

    const float sigmaT = max(0.0f, baseDensity * heightFactor * weatherBoost * wispFactor);
    const float sigmaS = sigmaT * scatterAlbedo;

    // 2. Directional Sun/Moon Henyey-Greenstein Mie forward scattering + ambient sky scatter
    const float3 sunDir = gSunDirAndInt.xyz;
    const float  cosSun = dot(rayDirJ, sunDir);
    const float  hgSun  = HenyeyGreenstein(cosSun, gWindAndPhase.w);

    float3 lightingIn = gSunColAndAmbInt.xyz * (gSunDirAndInt.w * hgSun)
                      + gAmbColAndJitterZ.xyz * (gSunColAndAmbInt.w * kInv4Pi);

    // 3. Up to 16 local point/torch/campfire lights with physically-attenuated inverse-square scatter
    const uint lightCount = min(gGridAndLights.w, 16u);
    [loop]
    for (uint li = 0u; li < lightCount; ++li)
    {
        const FroxelPointLight L = gLights[li];
        const float3 toL = L.posW - worldPos;
        const float  d2  = dot(toL, toL);
        const float  r   = max(L.radius, 1.0e-3f);
        const float  r2  = r * r;
        if (d2 < r2)
        {
            const float ratio2 = d2 / r2;
            const float w      = saturate(1.0f - ratio2 * ratio2);
            const float atten  = (w * w) / (d2 + 0.25f);
            const float3 lDir  = toL * rsqrt(max(d2, 1.0e-6f));
            const float phaseL = HenyeyGreenstein(dot(rayDirJ, lDir), 0.35f);
            lightingIn += L.color * (L.intensity * atten * phaseL);
        }
    }

    float4 currVal = float4(max(sigmaS * lightingIn, float3(0.0f, 0.0f, 0.0f)), sigmaT);

    // 4. Temporal 3D froxel reprojection (blends previous frame history inside valid frustum)
    const float histWeight = clamp(gPrevCamFwdAndHist.w, 0.0f, 0.95f);
    if (histWeight > 1.0e-4f)
    {
        const float3 rayDirC = ComputeRayDirFromUv(uvCenter);
        const float  cosViewC = max(dot(rayDirC, camFwd), 0.15f);
        const float3 worldCenter = camPos + rayDirC * (zCenter / cosViewC);

        const float4 prevClip = mul(float4(worldCenter, 1.0f), gPrevViewProj);
        if (prevClip.w > 1.0e-4f)
        {
            const float2 prevUv = float2(
                (prevClip.x / prevClip.w) * 0.5f + 0.5f,
                0.5f - (prevClip.y / prevClip.w) * 0.5f);
            const float prevLinZ = dot(worldCenter - gPrevCamPosAndTime.xyz, gPrevCamFwdAndHist.xyz);
            const float nearZ = gCamPosAndNear.w;
            const float farZ  = gCamFwdAndFar.w;
            if (prevUv.x >= 0.0f && prevUv.x <= 1.0f &&
                prevUv.y >= 0.0f && prevUv.y <= 1.0f &&
                prevLinZ >= nearZ && prevLinZ <= farZ)
            {
                const float prevSliceU = log(prevLinZ / nearZ) * gJitterXYAndLog.w;
                if (prevSliceU >= 0.0f && prevSliceU <= 1.0f)
                {
                    const float4 histVal = gInputVol.SampleLevel(gLinearClamp, float3(prevUv, prevSliceU), 0.0f);
                    if (all(isfinite(histVal)))
                    {
                        currVal = lerp(currVal, max(histVal, float4(0.0f, 0.0f, 0.0f, 0.0f)), histWeight);
                    }
                }
            }
        }
    }

    gOutputVol[dtid] = currVal;
}

// ============================================================================
// Entry 2: Analytical Front-to-Back Froxel Slice Integration (Frostbite 2015)
// ============================================================================
[numthreads(8, 8, 1)]
void cs_froxel_integrate(uint3 dtid : SV_DispatchThreadID)
{
    const uint gridW = gGridAndLights.x;
    const uint gridH = gGridAndLights.y;
    const uint gridD = gGridAndLights.z;
    if (dtid.x >= gridW || dtid.y >= gridH)
        return;

    const float2 uvCenter = (float2(dtid.xy) + 0.5f) / float2((float)gridW, (float)gridH);
    const float3 rayDir   = ComputeRayDirFromUv(uvCenter);
    const float  invCos   = 1.0f / max(dot(rayDir, gCamFwdAndFar.xyz), 0.15f);
    const float  invD     = 1.0f / (float)gridD;

    float3 accumScatter = float3(0.0f, 0.0f, 0.0f);
    float  accumT       = 1.0f;

    float prevZ = SliceToLinearZ(0.0f);

    [loop]
    for (uint k = 0u; k < gridD; ++k)
    {
        const float nextZ = SliceToLinearZ((float)(k + 1u) * invD);
        const float ds    = max((nextZ - prevZ) * invCos, 1.0e-5f);
        prevZ = nextZ;

        const float4 voxel  = gInputVol.Load(int4(int2(dtid.xy), (int)k, 0));
        const float3 inScat = max(voxel.rgb, float3(0.0f, 0.0f, 0.0f));
        const float  sigmaT = max(voxel.a, 0.0f);

        // Exact Beer-Lambert slice transmittance + analytical integration with Taylor limit for sigmaT -> 0
        const float  sliceT = exp(-sigmaT * ds);
        const float3 sliceScatter = (sigmaT > 1.0e-5f)
            ? ((inScat - inScat * sliceT) / sigmaT)
            : (inScat * ds);

        accumScatter += accumT * sliceScatter;
        accumT       *= sliceT;

        gOutputVol[uint3(dtid.xy, k)] = float4(accumScatter, accumT);
    }
}
