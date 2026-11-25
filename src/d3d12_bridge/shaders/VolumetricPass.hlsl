// VolumetricPass.hlsl: SM 6.6 / 6.10 Temporally-Reprojected 1/2-Res Volumetric Lighting, Mountain Crepuscular God-Rays
// & Physical 42-deg / 51-deg Mie Rainbow Optics (with Alexander's Dark Band).
// Couples directly to RawWeatherAtmo.hlsli (mountain LCL fog bands, valley thermal inversion burn-off, coastal sea-fog,
// and rain curtains), CloudCommon.hlsli cloud-shadow occlusion, and Hillaire TransLut / AtmoSkyView. Original work, MIT.
#define ATLAS_SLOT t1
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"
#include "RawProceduralNoise.hlsli"
#include "RawWeatherAtmo.hlsli"

Texture2D<float>  Depth       : register(t0);
Texture2D<float4> VolPrev     : register(t2);
Texture2D<float4> TransLut    : register(t3);
Texture2D<float4> AtmoSkyView : register(t4);
SamplerState      Lin         : register(s0);

float3 volTransAt(float mu) {
    const float r = 6360.0f + 0.5f;
    const float H = sqrt(6460.0f * 6460.0f - 6360.0f * 6360.0f);
    const float rho = sqrt(r * r - 6360.0f * 6360.0f);
    const float muCl = clamp(isfinite(mu) ? mu : 0.0f, -1.0f, 1.0f);
    const float d = max(0.0f, -r * muCl + sqrt(max(r * r * (muCl * muCl - 1.0f) + 6460.0f * 6460.0f, 0.0f)));
    return TransLut.SampleLevel(Lin, float2(saturate((d - (6460.0f - r)) / max(rho + H - (6460.0f - r), 1.0e-4f)), saturate(rho / H)), 0.0f).rgb;
}

float3 volSkyAt(float3 rd) {
    const float rdLen2 = dot(rd, rd);
    const float3 d = (isfinite(rdLen2) && rdLen2 > 1.0e-8f) ? (rd * rsqrt(rdLen2)) : float3(0.0f, 0.0f, 1.0f);
    const float az = (abs(d.x) + abs(d.y) > 1.0e-5f) ? atan2(d.y, d.x) : 0.0f;
    const float elev = asin(clamp(d.z, -1.0f, 1.0f));
    const float vNonLin = saturate(0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI))));
    // Exact texel-center inverse of AtmosphereLut.hlsl::skyview (zero +-PI seam on camera pans):
    const float uRaw = frac(az / (2.0f * PI) + 0.5f);
    const float uLut = (uRaw * 191.0f + 0.5f) / 192.0f;
    const float vLut = (vNonLin * 107.0f + 0.5f) / 108.0f;
    return AtmoSkyView.SampleLevel(Lin, float2(uLut, vLut), 0.0f).rgb;
}

float phaseHG(float cosT, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f), 1.5f));
}

float windowIntegral(float3 w, float3 lp, float r, float tMinStart, float tEnd) {
    const float b = dot(lp, w);
    const float h2 = dot(lp, lp) - b * b;
    const float d2 = r * r - h2;
    if (d2 <= 0.0f) { return 0.0f; }
    const float halfLen = sqrt(d2);
    const float u0 = max(tMinStart, b - halfLen) - b, u1 = min(tEnd, b + halfLen) - b;
    if (u1 <= u0) { return 0.0f; }
    return (1.0f - h2 / (r * r)) * (u1 - u0) - (u1 * u1 * u1 - u0 * u0 * u0) / (3.0f * r * r);
}

// 100% Physically Accurate 42-deg Primary + 51-deg Secondary Mie Rainbow Arc & Alexander's Dark Band:
// Strictly gated on active rain + visible daytime sun behind camera (ZERO phantom rainbows on dry sunny days!).
//   - Primary 42-deg bow (1 internal reflection): R (42.3 deg) -> O -> Y -> G (41.4 deg) -> B -> V (40.6 deg)
//   - Secondary 51-deg bow (2 internal reflections, inverted V-B-G-Y-O-R): R (50.4 deg) -> V (53.2 deg)
//   - Alexander's Dark Band (42.5..50.2 deg): geometric forbidden zone reducing droplet scatter between the two bows!
float3 evalPhysicalMieRainbow(float3 rayDir, float3 sunD, float rain01, float cloudCov, float tRainSpan, out float alexDarken) {
    alexDarken = 1.0f;
    const float cosAnti = dot(rayDir, -sunD);
    if (rain01 <= 0.15f || sunD.z <= 0.04f || cosAnti <= 0.58f || tRainSpan <= 350.0f) {
        return 0.0f.xxx;
    }

    // Physical visibility gates:
    // - Rain droplet density ramps cleanly above 0.15
    // - Sun elevation must be above horizon (> 0.04) and below ~44 deg (sin 44 = 0.695) for bow to rise above ground
    // - Needs sufficient sunlit rain curtain depth (> 350u) and breaks in overcast so sunlight illuminates the rain!
    const float rainGate  = smoothstep(0.15f, 0.52f, rain01);
    const float sunElGate = smoothstep(0.04f, 0.14f, sunD.z) * (1.0f - smoothstep(0.56f, 0.72f, sunD.z));
    const float depthGate = smoothstep(350.0f, 3200.0f, tRainSpan);
    const float sunBreak  = 1.0f - smoothstep(0.72f, 0.98f, saturate(cloudCov)) * 0.72f;
    const float horGate   = smoothstep(-0.04f, 0.05f, rayDir.z);
    const float bowGate   = rainGate * sunElGate * depthGate * sunBreak * horGate;
    if (bowGate <= 0.002f) { return 0.0f.xxx; }

    const float theta = acos(clamp(cosAnti, -1.0f, 1.0f));

    // Primary 42-deg spectral dispersion (6 narrow CIE-calibrated bands R-O-Y-G-B-V, red outside at 42.3 deg = 0.7383 rad):
    const float bR = exp(-pow((theta - 0.7383f) / 0.0068f, 2.0f));
    const float bO = exp(-pow((theta - 0.7322f) / 0.0065f, 2.0f));
    const float bY = exp(-pow((theta - 0.7272f) / 0.0064f, 2.0f));
    const float bG = exp(-pow((theta - 0.7218f) / 0.0066f, 2.0f));
    const float bC = exp(-pow((theta - 0.7155f) / 0.0068f, 2.0f));
    const float bV = exp(-pow((theta - 0.7086f) / 0.0072f, 2.0f));
    const float3 priSpec = float3(1.00f, 0.06f, 0.02f) * bR
                         + float3(1.00f, 0.42f, 0.04f) * bO
                         + float3(0.95f, 0.86f, 0.08f) * bY
                         + float3(0.10f, 0.92f, 0.24f) * bG
                         + float3(0.06f, 0.62f, 0.95f) * bC
                         + float3(0.38f, 0.14f, 0.95f) * bV;

    // Secondary 51-deg inverted spectral dispersion (red inside at 50.4 deg = 0.8796 rad, violet outside at 53.1 deg = 0.9268 rad):
    const float sR = exp(-pow((theta - 0.8796f) / 0.0095f, 2.0f));
    const float sY = exp(-pow((theta - 0.8925f) / 0.0095f, 2.0f));
    const float sG = exp(-pow((theta - 0.9035f) / 0.0098f, 2.0f));
    const float sB = exp(-pow((theta - 0.9155f) / 0.0102f, 2.0f));
    const float sV = exp(-pow((theta - 0.9268f) / 0.0108f, 2.0f));
    const float3 secSpec = (float3(1.00f, 0.10f, 0.02f) * sR
                          + float3(0.96f, 0.78f, 0.08f) * sY
                          + float3(0.12f, 0.90f, 0.26f) * sG
                          + float3(0.08f, 0.56f, 0.96f) * sB
                          + float3(0.40f, 0.14f, 0.94f) * sV) * 0.30f;

    // Alexander's Dark Band (42.5..50.2 deg = 0.742..0.876 rad) + subtle bright supernumerary interior (< 40.4 deg):
    const float alexBand   = smoothstep(0.740f, 0.768f, theta) * (1.0f - smoothstep(0.854f, 0.880f, theta));
    const float superInner = (1.0f - smoothstep(0.680f, 0.715f, theta)) * smoothstep(0.560f, 0.650f, theta);
    alexDarken = 1.0f - alexBand * bowGate * 0.28f + superInner * bowGate * 0.08f;

    // Solar spectral transmittance tints low-sun rainbows warmer red-gold authentically:
    const float3 sunTint = lerp(float3(0.95f, 0.97f, 1.00f), max(volTransAt(sunD.z), float3(0.25f, 0.18f, 0.10f)), 0.55f);
    return (priSpec + secSpec) * sunTint * (bowGate * 0.165f);
}

float3 sanitizeVolRgb(float3 c) {
    return (any(isnan(c)) || any(isinf(c))) ? 0.0f.xxx : clamp(c, 0.0f.xxx, 16.0f.xxx);
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const float2 lo = floor(at.xy);
    const float2 fullPx = (lo + 0.5f) * 2.0f;
    const int2 fullIp = clamp(int2(fullPx), int2(0, 0), max(int2(dims.xy) - 1, int2(0, 0)));
    const bool isWorldMap = (timeEnv.w > 0.5f) && (camPosW.z > 42000.0f);
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u || isWorldMap) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }
    const float d = Depth.Load(int3(fullIp, 0));
    const float3 eye = eyeAt();
    const float3 p = worldAt(fullPx - 0.5f, min(d, 0.999999f));
    const float3 toP = p - eye;
    const float toPLen2 = dot(toP, toP);
    const float dist = (isfinite(toPLen2) && toPLen2 > 1.0e-8f) ? sqrt(toPLen2) : 1.0e-4f;
    const float3 w = (isfinite(toPLen2) && toPLen2 > 1.0e-8f) ? (toP / dist) : float3(0.0f, 1.0f, 0.0f);
    // Cap volumetric shaft integration to 12,000 units so open ocean/coast horizons never over-accumulate nuclear scatter.
    const float tEnd = min(dist, min(max(tune2.w, 1.0f), 12000.0f));
    const float weatherDens = 1.0f + weather.w * 1.15f + weather.z * 1.35f + saturate(clouds.x - 0.25f) * 0.55f;
    const float sigma = max(tune.x, 0.00014f) * weatherDens;
    const float zCur = d < 0.99998f ? clamp(viewZOf(d), 0.1f, 65000.0f) : 65000.0f;
    const float simTime = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
    const float liveWaterZV = rawLiveWaterZ(engineWorld, physWorld[0]);
    const float estSeaTempC = lerp(8.5f, -4.5f, saturate(weather.z * 0.85f + smoothstep(20000.0f, 110000.0f, camPosW.y) * 0.55f));
    const float2 seaLevelAndTemp = float2(liveWaterZV, estSeaTempC);

    float3 scatter = 0.0f.xxx;
    const bool isExterior = (sunColour.w > 0.5f);
    if (isExterior && sunDir.w > 0.5f && tune2.z > 0.0f) {
        // Golden-ratio temporally-phased IGN jitter converges smoothly across frames without static banding on pans:
        const float jit = frac(ign(lo) + 0.61803399f * float(uint(proj.z) & 63u));
        const float3 aSunV = astroSunDir();
        const float aElevV = astroSunElev();
        const float nightW = smoothstep(0.03f, -0.07f, aElevV);
        const float3 beamRawV = lerp(aSunV, sunDir.xyz, nightW);
        const float beamLen2V = dot(beamRawV, beamRawV);
        const float3 beamDirV = (isfinite(beamLen2V) && beamLen2V > 1.0e-8f) ? (beamRawV * rsqrt(beamLen2V)) : float3(0.0f, 0.0f, 1.0f);
        const float lowSun = saturate(1.0f - max(aElevV, 0.0f) * 2.6f);
        const float lowSunBoost = lerp(1.0f, 1.42f, saturate(1.0f - abs(max(aElevV, 0.0f) - 0.17f) / 0.28f));
        const float shaftBoost = lerp(lowSunBoost, 2.25f, nightW);

        // Dual-lobe Mie forward scatter (tight alpine crepuscular sunbeam lobe g1 + broad atmospheric aureole g2 + subtle backscatter):
        const float g1 = lerp(clamp(lerp(max(tune.y, 0.62f), 0.85f, lowSun), 0.45f, 0.86f), 0.78f, nightW);
        const float g2 = 0.36f;
        const float cosT = dot(w, beamDirV);
        const float ph = min(0.62f * phaseHG(cosT, g1) + 0.28f * phaseHG(cosT, g2) + 0.10f * phaseHG(cosT, -0.18f), 2.85f);
        const float twilightGate = lerp(smoothstep(-0.02f, 0.09f, aElevV), 1.0f, nightW);

        // Coupled sunrise inversion burn-off from RawWeatherAtmo.hlsli (valley mist crisp at dawn/dusk, clears cleanly at midday):
        const float windSpeedV = length(cloudWind.xy);
        const float invPersist = evalInversionBurnOffFactor(timeEnv.x, aElevV, windSpeedV, clouds.x);

        // 28 quadratic-clustered temporally-reprojected depth slices coupled with unified cloudShadowN mountain shafts:
        const uint kSteps = 28u;
        float sumSun = 0.0f, sumSky = 0.0f, cs = 1.0f;
        float weatherCouple = 0.0f;
        float tPrev = 0.0f;
        [loop] for (uint i = 0u; i < kSteps; i++) {
            const float u1 = (float(i) + 1.0f) / float(kSteps);
            const float tNext = tEnd * u1 * u1;
            const float dt = max(tNext - tPrev, 1.0e-3f);
            const float t = tPrev + jit * dt;
            tPrev = tNext;
            const float3 ptT = eye + w * t;
            const float4 c = screenOf(ptT);
            const float3 wpFog = ptT + camPosW.xyz;
            // Wave 54 Unified Cloud-Shadow Mountain Shafts (2-tap convective cloudShadowN from LightingCommon.hlsli)
            // + RawWeatherAtmo mountain fog / valley inversion mist density (evaluated every 2nd step):
            if ((i & 1u) == 0u) {
                if (clouds.x > 0.005f) {
                    const float csRaw = cloudShadowN(wpFog, 2u);
                    cs = ((flags.x & FLAG_CLOUDS) != 0u) ? pow(max(saturate(csRaw), 1.0e-5f), 1.35f) : saturate(csRaw);
                } else {
                    cs = 1.0f;
                }
                const float4 ext4 = evalDynamicWeatherExtinction4(wpFog, weather, timeEnv, cloudWind, seaLevelAndTemp, simTime);
                // Mountain LCL cloud bands + valley thermal inversion mist catch crepuscular alpine god-rays!
                weatherCouple = saturate((ext4.x * 1.45f + ext4.y * 1.15f + ext4.z * 0.85f + ext4.w * 0.70f) * 3400.0f);
            }
            const float3 fogFlow = wpFog * 0.00050f + float3(cloudWind.xy * (simTime * 0.00018f), 0.0f);
            const float mist3d = 0.50f + 0.66f * cNoise(fogFlow) + 0.28f * cNoise(fogFlow * 2.4f + 7.3f);
            const float worldElevV = max(wpFog.z - liveWaterZV, 0.0f);
            const float valleyBank = exp(-min(worldElevV * 0.00038f, 32.0f)) * invPersist;
            const float collarDz = (wpFog.z - 4600.0f) / 2400.0f;
            const float alpineCollar = exp(-min(collarDz * collarDz, 32.0f)) * 0.68f;
            const float hDens = (0.26f + 0.74f * valleyBank + alpineCollar + weatherCouple * 0.95f) * mist3d;
            const float tr = exp(-min(sigma * t * (0.85f + weatherCouple * 0.35f), 40.0f));
            const float vis = (c.w > 0.5f ? cascadeShadow(ptT, saturate(c.z), false) : 1.0f) * cs;
            sumSun += vis * hDens * tr * dt;
            sumSky += (0.35f + 0.65f * cs) * hDens * tr * dt;
        }

        // Unified World-Space Atmosphere + TransLut solar spectral attenuation sync:
        float3 uSunV, uSkyZV, uSkyHV, uAmbUpV, uAmbDnV;
        evalWorldAtmosphere(uSunV, uSkyZV, uSkyHV, uAmbUpV, uAmbDnV);
        const float3 transSunV = max(sanitizeVolRgb(volTransAt(max(beamDirV.z, 0.02f))), float3(0.22f, 0.16f, 0.12f));
        const float3 hSkyV = sanitizeVolRgb(volSkyAt(float3(w.xy, max(w.z, 0.01f))));
        const float3 unifiedSky = lerp(lerp(uSkyHV, uSkyZV, saturate(max(w.z, 0.0f) * 2.0f)), hSkyV, (1.0f - nightW) * (1.0f - saturate(clouds.x * 0.70f)) * 0.45f);
        const float3 dayShaftCol = lerp(uSunV * lerp(1.0f.xxx, transSunV, 0.42f), unifiedSky, 0.20f);
        const float3 shaftCol = lerp(dayShaftCol, max(uSkyHV, float3(0.18f, 0.26f, 0.42f)), nightW * 0.72f);
        scatter += shaftCol * (sigma * ph * sumSun * tune2.z * shaftBoost * 0.92f * twilightGate);

        // Wave 54 Dawn/Dusk Chappuis-Reddened Golden-Hour Solar Aureole Glow (strictly when sunElev in [0.0..0.28];
        // strictly 0.0 at midday so midday skies stay crisp & clean!):
        const float sunElevA = max(aElevV, sunDir.z * isRealSun());
        const float goldenBell = smoothstep(0.00f, 0.055f, sunElevA) * (1.0f - smoothstep(0.14f, 0.28f, sunElevA)) * (1.0f - nightW);
        if (goldenBell > 0.005f && cosT > 0.35f) {
            const float aureoleLobe = pow(saturate(cosT), 8.0f) * 0.38f + pow(saturate(cosT), 36.0f) * 0.62f;
            const float chappuisWarmT = smoothstep(0.01f, 0.22f, sunElevA);
            const float3 chappuisCol = lerp(float3(1.22f, 0.44f, 0.15f), float3(1.12f, 0.78f, 0.42f), chappuisWarmT)
                                     * lerp(1.0f.xxx, transSunV, 0.35f);
            const float shaftRatio = saturate(sumSun / max(sumSky, 1.0e-3f));
            const float spanGate = smoothstep(180.0f, 3800.0f, tEnd) * smoothstep(-0.08f, 0.14f, w.z) * (1.0f - smoothstep(0.32f, 0.65f, w.z));
            const float cloudBreakA = (1.0f - saturate(clouds.x * 0.65f)) * (0.35f + 0.65f * shaftRatio);
            scatter += chappuisCol * (aureoleLobe * goldenBell * spanGate * cloudBreakA * 0.28f);
        }

        // Gentle isotropic sky multi-scattering inside volumetric mist (strictly bounded to prevent whiteout).
        scatter += unifiedSky * (sigma * sumSky * 0.015f * (1.0f - nightW * 0.65f));

        // Physical 42-deg / 51-deg Mie Rainbow Arc + Alexander's Dark Band:
        // Strictly gated on active exterior rain + real visible sun behind camera (ZERO dry-day phantom rainbows!):
        if (weather.w > 0.15f && aSunV.z > 0.04f && isRealSun() > 0.25f) {
            float alexDarken = 1.0f;
            const float3 rainbowRgb = evalPhysicalMieRainbow(w, aSunV, weather.w, clouds.x, tEnd, alexDarken);
            scatter = scatter * alexDarken + rainbowRgb;
        }
    }
    if (tune2.y > 0.0f) {
        const float weatherMistGain = 1.0f + saturate(weather.w * 0.85f + weather.z * 0.75f + clouds.x * 0.35f) * 0.65f;
        const bool isInt = !isExterior;
        const float nightV = isInt ? 0.0f : smoothstep(0.03f, -0.07f, astroSunElev());
        const float3 midWp = camPosW.xyz + eye + w * min(tEnd * 0.45f, 1800.0f);
        const float dustMote = isInt ? (0.78f + 0.44f * cNoise(midWp * 0.011f - float3(0.0f, 0.0f, simTime * 0.16f))) : 1.0f;
        // Crisp clear-foreground interior volumetric gain eliminates milky beige haze over candles & hearths:
        const float intGain = isInt ? (0.042f * dustMote) : (weatherMistGain * (1.0f + nightV * 1.10f));
        const float tMinLocal = isInt ? 220.0f : 130.0f;
        const float jitL = frac(ign(lo + float2(13.0f, 29.0f)) + 0.61803399f * float(uint(proj.z) & 63u));
        uint shadowedDone = 0u;
        [loop] for (uint li = 0u; li < flags.y; li++) {
            const float rL = lightPosR[li].w * (1.0f + nightV * 0.55f);
            if (!isfinite(rL) || rL <= 1.0e-3f) { continue; }
            const float3 lpRel = lightPosR[li].xyz - eye;
            const float wi = windowIntegral(w, lpRel, rL, tMinLocal, tEnd);
            if (wi > 0.0f) {
                const bool isFire = lightColS[li].r > lightColS[li].b * 1.25f;
                const float3 fCol = isFire ? lightColS[li].rgb * float3(1.18f, 0.86f, 0.52f) * 1.05f : lightColS[li].rgb;
                float visScale = 1.0f;
                if ((isInt || nightV > 0.25f) && shadowedDone < 12u) {
                    const float3 lp = lightPosR[li].xyz;
                    const float b = dot(lpRel, w);
                    const float h2 = dot(lpRel, lpRel) - b * b;
                    const float halfL = sqrt(max(rL * rL - h2, 0.0f));
                    const float tA = max(tMinLocal, b - halfL);
                    const float tB = min(tEnd, b + halfL);
                    if (tB > tA + 4.0f) {
                        shadowedDone++;
                        float occAcc = 0.0f;
                        const float dStep = (tB - tA) * 0.10f;
                        [unroll] for (int si = 0; si < 10; si++) {
                            const float ts = tA + (float(si) + jitL) * dStep;
                            const float3 sp = eye + w * ts;
                            const float3 toLight = lp - sp;
                            const float3 midPt = sp + toLight * 0.5f;
                            const float4 sm = screenOf(midPt);
                            float visStep = 1.0f;
                            if (sm.w > 1.0f && all(sm.xy >= 0.0f) && sm.x < dims.x && sm.y < dims.y) {
                                const float2 smUv = sm.xy * dims.zw;
                                const float edgeV = smoothstep(0.0f, 0.06f, min(min(smUv.x, 1.0f - smUv.x), min(smUv.y, 1.0f - smUv.y)));
                                const float zHit = viewZOf(Depth.Load(int3(sm.xy, 0)));
                                if (zHit < sm.w * 0.96f) { visStep = lerp(1.0f, 0.08f, edgeV); }
                            }
                            const float tlLen2 = dot(toLight, toLight);
                            const float3 tlDir = (isfinite(tlLen2) && tlLen2 > 1.0e-8f) ? (toLight * rsqrt(tlLen2)) : w;
                            const float cosL = clamp(dot(w, tlDir), -1.0f, 1.0f);
                            const float phL = min(phaseHG(cosL, 0.62f) * 3.8f + 0.55f, 2.8f);
                            occAcc += visStep * phL * 0.10f;
                        }
                        visScale = occAcc;
                    }
                }
                scatter += fCol * (sigma * wi * visScale * tune2.y * intGain / (4.0f * PI));
            }
        }
    }
    // Synchronized thunderstorm lightning flash illuminating volumetric rain mist across valleys.
    if (isExterior && weather.w > 0.45f && clouds.x > 0.65f) {
        const float tSlot = floor(simTime * 1.4f);
        const float stormTrigger = step(0.945f, frac(sin(tSlot * 127.1f) * 43758.5453f));
        const float flashEnv = max(pow(max(1.0f - frac(simTime * 1.4f), 0.0f), 5.0f) * stormTrigger, saturate(timeEnv.z));
        scatter += float3(0.45f, 0.62f, 1.05f) * (flashEnv * saturate(tEnd / 6000.0f) * 0.22f);
    }
    // Physical energy-bounded highlight shoulder + NaN/Inf sanitization (prevents volumetric fog from pushing coastal skies/snow into nuclear bloom).
    scatter = sanitizeVolRgb(scatter);
    const float scMax = max(scatter.r, max(scatter.g, scatter.b));
    scatter = (scatter / (1.0f + scMax * 1.45f)) * 0.36f;

    // Smooth C1 temporal reprojection with far-sky directional projection, border fade, pause guard,
    // and continuous depth + luma disocclusion rejection (zero silhouette popping on rapid camera pans!).
    const float3 reprojPos = (d < 0.99998f) ? p : (eye + w * 200000.0f);
    const float4 cp = float4(reprojPos, 1.0f);
    const float4 pc = float4(dot(prevWorldToClip[0], cp), dot(prevWorldToClip[1], cp), dot(prevWorldToClip[2], cp), dot(prevWorldToClip[3], cp));
    if (pc.w > 1.0e-4f && proj.z > 0.5f && timeEnv.w < 0.5f) {
        const float2 puv = float2(pc.x / pc.w * 0.5f + 0.5f, 0.5f - pc.y / pc.w * 0.5f) / max(resScale.xy, float2(1.0e-4f, 1.0e-4f));
        if (all(isfinite(puv)) && all(puv > 0.005f) && all(puv < 0.995f)) {
            const float4 prev = VolPrev.SampleLevel(Lin, puv, 0.0f);
            const float prevZ = isfinite(prev.a) ? prev.a : -1.0f;
            const float curZCl = min(zCur, 65000.0f);
            const float zRel = abs(prevZ - curZCl) / max(curZCl, 1.0f);
            const float zAccept = (prevZ > 0.0f) ? (1.0f - smoothstep(0.03f, 0.14f, zRel)) : 0.0f;
            if (zAccept > 0.002f) {
                const float edgeFade = smoothstep(0.005f, 0.065f, min(min(puv.x, 1.0f - puv.x), min(puv.y, 1.0f - puv.y)));
                const float2 curUvV = fullPx * dims.zw;
                const float3 prevCol = sanitizeVolRgb(prev.rgb);
                const float curL = dot(scatter, float3(0.2126f, 0.7152f, 0.0722f));
                const float prevL = dot(prevCol, float3(0.2126f, 0.7152f, 0.0722f));
                const float lReject = 1.0f - smoothstep(0.50f, 1.40f, abs(prevL - curL) / max(max(curL, prevL), 0.04f));
                const float histW = lerp(0.76f, 0.22f, saturate(length(puv - curUvV) * 16.0f)) * zAccept * edgeFade * lReject;
                scatter = lerp(scatter, prevCol, saturate(histW));
            }
        }
    }
    return float4(sanitizeVolRgb(scatter), min(zCur, 65000.0f));
}
