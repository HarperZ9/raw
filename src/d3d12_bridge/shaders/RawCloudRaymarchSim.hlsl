// RawCloudRaymarchSim.hlsl: Clean-Room SM 6.6 / SM 6.10 GPU Compute Volumetric Cloud Raymarching Solver.
// Implements:
//   - Earth-curved spherical atmospheric cloud shell (planetRadius = 6,371 km, base 1.5 km, top 5.5 km)
//   - 32-step IGN-jittered primary raymarch with strict horizon & clear-sky early-out -> (0, 0, 0, 1.0)
//   - Wind-advected 3D multi-octave cumulus weather/shape/detail density with flat base & anvil profile
//   - 4-tap directional sun light-march + Beer-Powder dark-edge energy conservation + 2-octave multi-scattering
//   - Dual-lobe Henyey-Greenstein silver-lining phase (g1 = +0.72, g2 = -0.28) + cool sky ambient bottom-fill
//   - Optional ping-pong temporal history reprojection blend with strict NaN/Inf sanitization
// Original clean-room implementation, MIT License.

cbuffer CloudCB : register(b0)
{
    float4 g_camPosAndTime;      // xyz = camera world pos (m), w = timeSec
    float4 g_camRightAndFov;     // xyz = camera right, w = tanHalfFovX
    float4 g_camUpAndFov;        // xyz = camera up, w = tanHalfFovY
    float4 g_camFwdAndRad;       // xyz = camera forward, w = planetRadiusM
    float4 g_sunDirAndInt;       // xyz = normalized sun dir, w = sunIntensity
    float4 g_sunColAndAmb;       // rgb = sun color, w = ambientIntensity
    float4 g_skyZenAndCov;       // rgb = sky zenith tint, w = cloudCoverage [0..1]
    float4 g_skyHorAndDens;      // rgb = sky horizon tint, w = densityScale
    float4 g_shellParams;        // x = cloudBaseM, y = cloudTopM, z = anvilFactor, w = historyWeight
    float4 g_windAndScale;       // xyz = wind vel (m/s), w = noiseScale
    uint4  g_dimsAndFrame;       // x = width, y = height, z = frameIndex, w = flags (bit0 = hasHistory)
};

Texture2D<float4>   g_prevHistory : register(t0);
RWTexture2D<float4> g_cloudOut    : register(u0);
SamplerState        g_linearClamp : register(s0);

static const float kPi = 3.14159265359f;

float SanitizeFinite(float v, float fallback)
{
    uint bits = asuint(v);
    return ((bits & 0x7F800000u) == 0x7F800000u) ? fallback : v;
}

float4 SanitizeCloud4(float4 c)
{
    float r = clamp(SanitizeFinite(c.r, 0.0f), 0.0f, 64.0f);
    float g = clamp(SanitizeFinite(c.g, 0.0f), 0.0f, 64.0f);
    float b = clamp(SanitizeFinite(c.b, 0.0f), 0.0f, 64.0f);
    float a = clamp(SanitizeFinite(c.a, 1.0f), 0.0f, 1.0f);
    return float4(r, g, b, a);
}

// Interleaved Gradient Noise with golden-ratio temporal cycling (zero diagonal grating)
float EvalIGN(uint2 px, uint frameIdx)
{
    float2 p = float2(px);
    float ign = frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
    return frac(ign + 0.61803399f * float(frameIdx & 63u));
}

// Fast integer hash -> [0, 1)
float Hash31(int3 p)
{
    uint3 q = asuint(p);
    uint h = q.x * 1664525u + q.y * 1013904223u + q.z * 374761393u;
    h ^= h >> 15u;
    h *= 2246822519u;
    h ^= h >> 13u;
    h *= 3266489917u;
    h ^= h >> 16u;
    return float(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// Smooth 3D value noise
float ValueNoise3D(float3 p)
{
    float3 fp = floor(p);
    int3   ip = int3(fp);
    float3 f  = p - fp;
    float3 u  = f * f * (3.0f - 2.0f * f);

    float n000 = Hash31(ip + int3(0, 0, 0));
    float n100 = Hash31(ip + int3(1, 0, 0));
    float n010 = Hash31(ip + int3(0, 1, 0));
    float n110 = Hash31(ip + int3(1, 1, 0));
    float n001 = Hash31(ip + int3(0, 0, 1));
    float n101 = Hash31(ip + int3(1, 0, 1));
    float n011 = Hash31(ip + int3(0, 1, 1));
    float n111 = Hash31(ip + int3(1, 1, 1));

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);

    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    return lerp(nxy0, nxy1, u.z);
}

// Fast 2D integer hash -> [0, 1)
float Hash21(int2 p)
{
    uint2 q = asuint(p);
    uint h = q.x * 1664525u + q.y * 1013904223u;
    h ^= h >> 15u;
    h *= 2246822519u;
    h ^= h >> 13u;
    h *= 3266489917u;
    h ^= h >> 16u;
    return float(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// Smooth 2D macro weather coverage noise (4 corners instead of 3D 8 corners)
float WeatherNoise2D(float2 p)
{
    float2 fp = floor(p);
    int2   ip = int2(fp);
    float2 f  = p - fp;
    float2 u  = f * f * (3.0f - 2.0f * f);

    float n00 = Hash21(ip + int2(0, 0));
    float n10 = Hash21(ip + int2(1, 0));
    float n01 = Hash21(ip + int2(0, 1));
    float n11 = Hash21(ip + int2(1, 1));
    return lerp(lerp(n00, n10, u.x), lerp(n01, n11, u.x), u.y);
}

float Remap01(float v, float lo, float hi)
{
    return saturate((v - lo) / max(hi - lo, 1.0e-5f));
}

// Positive distance from origin ro (inside sphere) to sphere of radius R centered at origin
float IntersectSphereFromInside(float3 ro, float3 rd, float radius)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float disc = max(b * b - c, 0.0f);
    return max(0.0f, -b + sqrt(disc));
}

// Dual-lobe Henyey-Greenstein phase function (forward silver-lining + back-scatter)
float EvalHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0f + g2 - 2.0f * g * cosTheta, 1.0e-4f);
    return (1.0f - g2) / (4.0f * kPi * denom * sqrt(denom));
}

float EvalDualHG(float cosTheta, float g1, float g2, float w1)
{
    return lerp(EvalHG(cosTheta, g2), EvalHG(cosTheta, g1), w1);
}

// Evaluates volumetric cumulus/stratus density at planet-centered position pPlanet (detailed primary sample)
float SampleCloudDensityPrimary(float3 pPlanet, float planetR, float baseM, float topM,
                                float coverage, float densityScale, float anvilFactor,
                                float3 windShift, float noiseScale, out float outWeather, out float outHeight01)
{
    outWeather  = 0.5f;
    outHeight01 = 0.0f;

    float r = length(pPlanet);
    float alt = r - planetR;
    float thickness = max(topM - baseM, 1.0f);
    float h = (alt - baseM) / thickness;
    if (h <= 0.0f || h >= 1.0f) return 0.0f;
    outHeight01 = h;

    // Cumulus vertical height profile: flat condensation base + rounded/anvil upper dome
    float botGrad = smoothstep(0.0f, 0.14f, h);
    float topCutoff = lerp(0.84f, 0.96f, saturate(anvilFactor));
    float topGrad = smoothstep(1.0f, max(topCutoff - 0.24f, 0.45f), h);
    float heightProfile = botGrad * topGrad;
    if (heightProfile <= 1.0e-4f) return 0.0f;

    // World-horizontal coordinates advected by wind (shear slightly with altitude)
    float3 q = float3(pPlanet.x, alt, pPlanet.z) + windShift + float3(windShift.x, 0.0f, windShift.z) * (h * 0.18f);
    float3 sp = q * noiseScale;

    // Weather macro field modulates cloud cluster distribution across the sky
    float weather = WeatherNoise2D(sp.xz * 0.45f + float2(1.3f, 2.7f));
    outWeather = weather;
    float localCov = saturate(coverage * lerp(0.70f, 1.25f, weather) + anvilFactor * smoothstep(0.62f, 0.90f, h) * 0.14f * coverage);

    // 2-octave base shape FBM (smooth billow puffs)
    float n0 = ValueNoise3D(sp * 1.65f);
    float n1 = ValueNoise3D(sp * 3.45f + float3(11.3f, 4.7f, 8.9f));
    float baseShape = n0 * 0.68f + n1 * 0.32f;

    // Remap base shape against coverage threshold
    float threshold = 1.0f - localCov;
    float d = Remap01(baseShape, threshold * 0.85f, 1.0f) * heightProfile;
    if (d <= 1.0e-4f) return 0.0f;

    // Crisp billow erosion detail on primary march samples
    float n2 = ValueNoise3D(sp * 7.80f + float3(-5.2f, 9.1f, 3.3f));
    float billow = 1.0f - abs(n2 * 2.0f - 1.0f);
    float erode = (1.0f - billow) * 0.28f * (1.0f - d * 0.65f);
    d = saturate(d - erode);

    return d * densityScale;
}

// Fast macro-shape density sample for 4-tap directional sun light-march (reuses local macro weather)
float SampleCloudDensityLight(float3 pPlanet, float planetR, float baseM, float topM,
                              float coverage, float densityScale, float anvilFactor,
                              float3 windShift, float noiseScale, float weather)
{
    float alt = length(pPlanet) - planetR;
    float h = (alt - baseM) / max(topM - baseM, 1.0f);
    if (h <= 0.0f || h >= 1.0f) return 0.0f;

    float botGrad = smoothstep(0.0f, 0.14f, h);
    float topCutoff = lerp(0.84f, 0.96f, saturate(anvilFactor));
    float topGrad = smoothstep(1.0f, max(topCutoff - 0.24f, 0.45f), h);
    float heightProfile = botGrad * topGrad;
    if (heightProfile <= 1.0e-4f) return 0.0f;

    float3 q = float3(pPlanet.x, alt, pPlanet.z) + windShift + float3(windShift.x, 0.0f, windShift.z) * (h * 0.18f);
    float3 sp = q * noiseScale;
    float localCov = saturate(coverage * lerp(0.70f, 1.25f, weather) + anvilFactor * smoothstep(0.62f, 0.90f, h) * 0.14f * coverage);
    float baseShape = ValueNoise3D(sp * 1.65f);
    float d = Remap01(baseShape, (1.0f - localCov) * 0.85f, 1.0f) * heightProfile;
    return d * densityScale;
}

[numthreads(8, 8, 1)]
void cs_cloud_raymarch(uint3 tid : SV_DispatchThreadID)
{
    const uint width  = g_dimsAndFrame.x;
    const uint height = g_dimsAndFrame.y;
    if (tid.x >= width || tid.y >= height) return;

    const float2 uv  = (float2(tid.xy) + 0.5f) / float2(float(width), float(height));
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    const float3 camRight = g_camRightAndFov.xyz;
    const float3 camUp    = g_camUpAndFov.xyz;
    const float3 camFwd   = g_camFwdAndRad.xyz;
    const float  tanX     = g_camRightAndFov.w;
    const float  tanY     = g_camUpAndFov.w;

    const float3 rayDir = normalize(camFwd + camRight * (ndc.x * tanX) + camUp * (ndc.y * tanY));

    const float coverage     = saturate(g_skyZenAndCov.w);
    const float densityScale = max(g_skyHorAndDens.w, 0.0f);

    // Strict early-out for below-horizon pixels OR clear sky -> exact (0, 0, 0, 1.0)
    if (rayDir.y <= 0.01f || coverage <= 1.0e-4f || densityScale <= 1.0e-6f) {
        g_cloudOut[tid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const float planetR     = max(g_camFwdAndRad.w, 100000.0f);
    const float baseM       = max(g_shellParams.x, 50.0f);
    const float topM        = max(g_shellParams.y, baseM + 100.0f);
    const float anvilFactor = saturate(g_shellParams.z);
    const float histWeight  = clamp(g_shellParams.w, 0.0f, 0.85f);
    const float noiseScale  = max(g_windAndScale.w, 1.0e-6f);
    const float timeSec     = g_camPosAndTime.w;
    const float3 windShift  = g_windAndScale.xyz * timeSec + float3(g_camPosAndTime.x, 0.0f, g_camPosAndTime.z);

    // Planet-centered ray origin (camera horizontal XZ baked into windShift to preserve float precision)
    const float camAlt = max(g_camPosAndTime.y, 0.0f);
    const float3 ro    = float3(0.0f, planetR + camAlt, 0.0f);
    const float rBot   = planetR + baseM;
    const float rTop   = planetR + topM;

    float tStart = 0.0f;
    float tEnd   = 0.0f;
    if (camAlt < baseM) {
        tStart = IntersectSphereFromInside(ro, rayDir, rBot);
        tEnd   = IntersectSphereFromInside(ro, rayDir, rTop);
    } else if (camAlt <= topM) {
        tStart = 0.0f;
        tEnd   = IntersectSphereFromInside(ro, rayDir, rTop);
    } else {
        g_cloudOut[tid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // Clamp max march span near low horizon so 32 steps maintain crisp cumulus structure
    tEnd = min(tEnd, tStart + 55000.0f);
    const float marchDist = tEnd - tStart;
    if (marchDist <= 1.0f) {
        g_cloudOut[tid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const uint  kSteps = 32u;
    const float dt     = marchDist / float(kSteps);
    const float jitter = EvalIGN(tid.xy, g_dimsAndFrame.z);

    const float3 sunDir       = normalize(g_sunDirAndInt.xyz);
    const float3 sunLight     = max(g_sunColAndAmb.rgb, 0.0f) * max(g_sunDirAndInt.w, 0.0f);
    const float3 skyZenith    = max(g_skyZenAndCov.rgb, 0.0f);
    const float3 skyHorizon   = max(g_skyHorAndDens.rgb, 0.0f);
    const float  ambientInt   = max(g_sunColAndAmb.w, 0.0f);

    const float cosTheta = dot(rayDir, sunDir);
    // Dual-lobe Henyey-Greenstein silver-lining phase (g1 = +0.72, g2 = -0.28)
    const float phaseOct0 = EvalDualHG(cosTheta, 0.72f, -0.28f, 0.72f);
    // Softer second-octave multi-scattering phase
    const float phaseOct1 = EvalDualHG(cosTheta, 0.36f, -0.14f, 0.65f);

    // 4-tap directional sun light-march step offsets across cloud thickness
    const float lightBaseStep = (topM - baseM) * 0.14f;
    static const float kLightTaps[4] = { 0.25f, 0.75f, 1.65f, 3.20f };
    static const float kLightWeights[4] = { 0.50f, 0.65f, 1.10f, 1.75f };

    float3 accumRad = float3(0.0f, 0.0f, 0.0f);
    float  transmittance = 1.0f;

    [loop]
    for (uint s = 0u; s < kSteps; ++s) {
        if (transmittance < 0.008f) break;

        const float  t = tStart + (float(s) + jitter) * dt;
        const float3 p = ro + rayDir * t;

        float weather = 0.5f;
        float h = 0.0f;
        const float sigmaT = SampleCloudDensityPrimary(p, planetR, baseM, topM, coverage, densityScale,
                                                       anvilFactor, windShift, noiseScale, weather, h);
        if (sigmaT <= 1.0e-6f) continue;

        // 4-tap directional sun optical depth light-march
        float sunOptD = 0.0f;
        [unroll]
        for (uint l = 0u; l < 4u; ++l) {
            float3 lp = p + sunDir * (kLightTaps[l] * lightBaseStep);
            float ld = SampleCloudDensityLight(lp, planetR, baseM, topM, coverage, densityScale,
                                               anvilFactor, windShift, noiseScale, weather);
            sunOptD += ld * (kLightWeights[l] * lightBaseStep);
        }

        // Beer-Powder dark-edge energy conservation + 2-octave multi-scattering
        const float localOptD = sigmaT * dt;
        const float beer0     = exp(-sunOptD);
        const float powder    = 1.0f - 0.65f * exp(-2.0f * (sunOptD + localOptD));
        const float oct0      = beer0 * max(powder, 0.35f) * phaseOct0;
        const float oct1      = 0.42f * exp(-0.32f * sunOptD) * phaseOct1;
        const float3 directScatter = sunLight * (oct0 + oct1);

        // Cool sky ambient bottom-fill modulated by cloud height & self-occlusion
        const float ambVis = 0.35f + 0.65f * exp(-0.35f * sunOptD);
        const float3 ambientScatter = lerp(skyHorizon * 0.45f, skyZenith, saturate(0.20f + 0.80f * h))
                                    * (ambientInt * ambVis * (1.0f / (4.0f * kPi)));

        const float3 inScatter = (directScatter + ambientScatter) * sigmaT;

        // Frostbite / Hillaire energy-conserving analytical step integration
        const float stepTrans = exp(-localOptD);
        const float3 stepRad  = (inScatter - inScatter * stepTrans) / max(sigmaT, 1.0e-6f);

        accumRad      += transmittance * stepRad;
        transmittance *= stepTrans;
    }

    if (transmittance < 0.01f) transmittance = 0.0f;
    float4 result = SanitizeCloud4(float4(accumRad, transmittance));

    // Optional temporal history blend (reduces 32-step IGN variance cleanly over frames)
    const bool hasHistory = (g_dimsAndFrame.w & 1u) != 0u;
    if (hasHistory && histWeight > 0.001f) {
        float4 prev = SanitizeCloud4(g_prevHistory.SampleLevel(g_linearClamp, uv, 0.0f));
        result = SanitizeCloud4(lerp(result, prev, histWeight));
    }

    g_cloudOut[tid.xy] = result;
}
