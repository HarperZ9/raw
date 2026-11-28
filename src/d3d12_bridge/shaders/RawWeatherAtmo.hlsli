// RawWeatherAtmo.hlsli: Dynamic Simulated Mountain Fog, Valley Inversion Mist, Coastal Sea-Fog,
// Volumetric Rain Curtains, Blizzard Spindrift & Thunderstorm Scatter (SM 6.6 / SM 6.10).
// Standalone clean-room MIT HLSL library (zero external textures, zero cbuffer coupling):
//   - Callable from LightFog.hlsl, FroxelPass.hlsl, and RawTerrainWorld.hlsl
//   - Guarantees sky-dome celestial bodies (Masser, Secunda, stars, aurora) are NEVER washed out
//     by ground, valley, or mountain fog!
// Original work, MIT.
#ifndef RAW_WEATHER_ATMO_HLSLI
#define RAW_WEATHER_ATMO_HLSLI

#include "RawProceduralNoise.hlsli"

struct DynamicFogSample {
    float3 inscatter;
    float  transmittance;
    float  mountainFog01;
    float  valleyMist01;
};

// Returns true for unobstructed upper-sky / celestial-dome rays so Masser, Secunda, stars, and
// aurora stay 100% crisp and exempt from ground/valley/mountain fog wash.
bool rawIsCelestialSkyRay(float dist, float3 rayDir) {
    return (dist > 85000.0f) && (rayDir.z > 0.14f);
}

// Smooth horizon-only gate for sky pixels (1.0 on solid scene geometry; fades smoothly to 0.0
// as sky elevation rises from 0.015 to 0.14 so celestial moons/stars above the horizon stay pure).
float rawWeatherSkyExemption(float dist, float3 rayDir) {
    if (dist <= 85000.0f) { return 1.0f; }
    return 1.0f - smoothstep(0.015f, 0.14f, rayDir.z);
}

// Evaluates sunrise solar burn-off & turbulent wind-eddy dispersion breaking nocturnal valley inversions:
// Returns residual inversion persistence in [0.15, 1.0] (high at calm dawn/night, burns off by late morning or high wind).
float evalInversionBurnOffFactor(float gameHour, float sunElev, float windSpeedU, float cloudCover) {
    const float solarHeat = saturate(sunElev * 1.45f) * (1.0f - saturate(cloudCover) * 0.65f);
    const float middayBurn = exp(-pow((gameHour - 14.0f) / 4.2f, 2.0f)) * solarHeat;
    const float windMix = smoothstep(60.0f, 380.0f, windSpeedU) * 0.55f;
    return saturate(1.0f - saturate(middayBurn * 0.82f + windMix));
}

// Evaluates local volumetric weather extinction & component masks at any absolute world point `pAbsW`.
// Outputs:
//   x = orographic mountain fog / ridge cloud band extinction
//   y = valley thermal inversion & river/creek mist extinction
//   z = coastal sea-fog & katabatic glacier drift extinction
//   w = volumetric rain curtain + blizzard spindrift extinction
float4 evalDynamicWeatherExtinction4(
    float3 pAbsW,
    float4 weather,
    float4 timeEnv,
    float4 cloudWind,
    float2 seaLevelAndTemp,
    float  simTimeSec)
{
    const float wetness    = saturate(weather.x);
    const float snowFall   = saturate(weather.z);
    const float rainFall   = saturate(weather.w);
    const float gameHour   = timeEnv.x;
    const float sunElev    = timeEnv.y;
    const float cloudCov   = saturate(cloudWind.z);
    const float seaLevelZ  = seaLevelAndTemp.x;
    const float seaTempC   = seaLevelAndTemp.y;
    const float2 windXY    = cloudWind.xy;
    const float  windSpeed = length(windXY);
    const float2 windDir   = (windSpeed > 1.0e-3f) ? (windXY / windSpeed) : float2(0.866f, 0.5f);
    const float2 windPerp  = float2(-windDir.y, windDir.x);

    // Height above local water / sea datum (Skyrim scale: 70u = 1m).
    const float altU = pAbsW.z - seaLevelZ;
    const float humidity = saturate(0.28f + wetness * 0.42f + rainFall * 0.45f + snowFall * 0.35f + cloudCov * 0.35f);

    // 1) Orographic Mountain Fog & Ridge Cloud Bands (altitude-stratified Lifting Condensation Level):
    // Moist/stormy air lowers the LCL so drifting cloud scarves wrap mountain passes & alpine peaks.
    const float lclBaseU = lerp(14800.0f, 3800.0f, humidity) - max(-seaTempC, 0.0f) * 140.0f;
    const float zLclU = max(lclBaseU, 1800.0f);
    const float bandHalfThick = lerp(3400.0f, 6800.0f, saturate(cloudCov * 0.65f + rainFall * 0.45f + snowFall * 0.35f));
    const float bandCenter = zLclU + bandHalfThick * 0.55f;
    const float bandDist = abs(altU - bandCenter) / max(bandHalfThick, 600.0f);
    const float altBand = exp(-bandDist * bandDist * 1.35f) * smoothstep(zLclU - 2400.0f, zLclU + 1200.0f, altU);

    // Wind-advected 3D curl-warped orographic wisps (never flat horizontal sheet!):
    const float3 windAdvM = float3(windXY * (simTimeSec * 0.22f), simTimeSec * 4.5f);
    const float3 pOro = (pAbsW - windAdvM) * float3(0.00024f, 0.00024f, 0.00062f);
    const float3 curlM = rawCurlNoise3D(pOro * 0.85f + float3(3.1f, 7.7f, 1.9f)) * 0.55f;
    const float oroN1 = rawNoise3Grad(pOro + curlM).x;
    const float oroN2 = rawNoise3Grad(pOro * 2.35f + float3(17.3f, 5.1f, 11.7f)).x;
    const float oroWisp = smoothstep(0.32f, 0.84f, oroN1 * 0.68f + oroN2 * 0.32f);
    const float oroRegime = saturate(0.24f + cloudCov * 0.72f + rainFall * 0.45f + snowFall * 0.45f);
    const float extMountain = altBand * oroWisp * oroRegime * 0.000185f;

    // 2) Valley Thermal Inversion & River/Creek Mist:
    // Cool-air drainage pools in low basins & river valleys at dawn/dusk and humid/wet weather.
    const float dawnFactor = exp(-pow((gameHour - 6.2f) / 2.6f, 2.0f));
    const float duskFactor = exp(-pow((gameHour - 19.6f) / 2.5f, 2.0f));
    const float lowSunInv  = smoothstep(0.34f, 0.02f, sunElev) * smoothstep(-0.28f, -0.04f, sunElev);
    const float thermalInv = saturate(max(max(dawnFactor, duskFactor), lowSunInv * 0.78f));
    const float invDepthU  = lerp(680.0f, 1650.0f, saturate(thermalInv * 0.65f + wetness * 0.45f));
    const float valleyAlt  = smoothstep(-180.0f, 40.0f, altU) * exp(-max(altU, 0.0f) / invDepthU);

    // Slow drifting 3D filaments sculpting valley mist (zero uniform screen wash!):
    const float3 pVal = (pAbsW - float3(windXY * (simTimeSec * 0.12f), 0.0f)) * float3(0.00065f, 0.00065f, 0.0019f);
    const float3 curlV = rawCurlNoise3D(pVal * 0.72f + float3(11.3f, 2.9f, 19.1f)) * 0.42f;
    const float valN1 = rawNoise3Grad(pVal + curlV).x;
    const float valN2 = rawNoise3Grad(pVal * 2.45f + float3(29.1f, 13.7f, 4.3f)).x;
    const float valFilament = smoothstep(0.34f, 0.86f, valN1 * 0.65f + valN2 * 0.35f);
    const float invPersist = evalInversionBurnOffFactor(gameHour, sunElev, windSpeed, cloudCov);
    const float valStrength = saturate((thermalInv * (0.58f + 0.42f * humidity) + wetness * 0.48f + rainFall * 0.30f) * invPersist);
    const float extValley = valleyAlt * valFilament * valStrength * 0.000165f;

    // 3) Coastal Sea-Fog & Katabatic Glacier Drift:
    // Rolls across northern coasts (seaLevelZ near -14000u) and frigid glacial fjords (seaTempC <= 3C).
    const float coastalGate = saturate(smoothstep(-8500.0f, -12500.0f, seaLevelZ) + smoothstep(4.5f, -3.0f, seaTempC) * 0.65f);
    const float seaBankAlt  = smoothstep(-60.0f, 20.0f, altU) * exp(-max(altU, 0.0f) / lerp(520.0f, 1250.0f, humidity));
    const float3 pSea = (pAbsW - float3(windXY * (simTimeSec * 0.28f), 0.0f)) * float3(0.00034f, 0.00034f, 0.0016f);
    const float seaN = rawNoise3Grad(pSea + float3(41.7f, 17.3f, 6.1f)).x * 0.70f
                     + rawNoise3Grad(pSea * 2.20f + float3(7.1f, 33.9f, 15.3f)).x * 0.30f;
    const float seaWisp = smoothstep(0.36f, 0.85f, seaN);
    const float seaRegime = coastalGate * saturate(0.18f + humidity * 0.65f + thermalInv * 0.35f + snowFall * 0.35f);
    const float extCoastal = seaBankAlt * seaWisp * seaRegime * 0.000145f;

    // 4) Volumetric Weather Curtains (wind-slanted 3D rain sheets + blowing blizzard spindrift):
    float extPrecip = 0.0f;
    if (rainFall > 0.02f) {
        const float slant = clamp(windSpeed * 0.0035f, 0.0f, 0.48f);
        const float2 pRainXY = float2(dot(pAbsW.xy, windDir) - pAbsW.z * slant, dot(pAbsW.xy, windPerp));
        const float3 qRain = float3(pRainXY.x * 0.0014f - simTimeSec * 0.32f,
                                    pRainXY.y * 0.0022f,
                                    pAbsW.z * 0.00045f + simTimeSec * 1.15f);
        const float rSheet = rawNoise3Grad(qRain).x * 0.62f + rawNoise3Grad(qRain * 2.3f + float3(9.3f, 14.1f, 5.7f)).x * 0.38f;
        const float curtainMask = smoothstep(0.34f, 0.84f, rSheet);
        extPrecip += rainFall * curtainMask * 0.000195f;
    }
    if (snowFall > 0.02f) {
        const float3 pSpin = (pAbsW - float3(windXY * (simTimeSec * 0.55f), -simTimeSec * 36.0f)) * float3(0.00095f, 0.00095f, 0.0018f);
        const float3 curlS = rawCurlNoise3D(pSpin * 0.90f + float3(5.3f, 23.1f, 11.9f)) * 0.65f;
        const float spinN  = rawNoise3Grad(pSpin + curlS).x * 0.65f + rawNoise3Grad(pSpin * 2.5f + float3(31.7f, 7.9f, 13.3f)).x * 0.35f;
        // Ground-hugging katabatic spindrift intensifies near snow surfaces while upper air carries blowing drifts:
        const float driftMask = smoothstep(0.28f, 0.82f, spinN);
        extPrecip += snowFall * driftMask * 0.000245f;
    }

    return float4(extMountain, extValley, extCoastal, extPrecip);
}

// Single-point volumetric froxel injection helper for FroxelPass.hlsl:
// Returns float4(inscatterRateRGB, totalExtinction).
float4 evalDynamicWeatherFroxelSample(
    float3 pAbsW,
    float3 rayDir,
    float3 sunDir,
    float3 sunCol,
    float3 skyHor,
    float4 weather,
    float4 timeEnv,
    float4 cloudWind,
    float2 seaLevelAndTemp,
    float  simTimeSec)
{
    const float4 ext4 = evalDynamicWeatherExtinction4(pAbsW, weather, timeEnv, cloudWind, seaLevelAndTemp, simTimeSec);
    const float extTot = ext4.x + ext4.y + ext4.z + ext4.w;
    if (extTot <= 1.0e-7f) { return 0.0f.xxxx; }

    const float cosT = dot(normalize(rayDir), normalize(sunDir));
    const float dayW = smoothstep(-0.08f, 0.14f, timeEnv.y);
    const float lightning = saturate(timeEnv.z);
    // Multi-scattering Henyey-Greenstein forward Mie glow + cool sky-horizon bounce + lightning intra-cloud flash:
    const float phSun = rawMultiScatterPhase(cosT, lerp(0.56f, 0.74f, saturate(ext4.x / max(extTot, 1.0e-6f))), 0.35f);
    const float3 sunScat = sunCol * (phSun * 2.45f * dayW);
    const float3 ambScat = skyHor * lerp(0.42f, 0.72f, saturate(ext4.x / max(extTot, 1.0e-6f)));
    const float3 flashScat = float3(0.62f, 0.78f, 1.18f) * (lightning * (0.45f + 0.85f * saturate((ext4.x + ext4.w) / max(extTot, 1.0e-6f))));
    // Subtle physical albedo tinting per medium (crisp snow/cloud white vs cool alpine mist vs slate rain curtain):
    const float3 albedoMix = (float3(0.95f, 0.97f, 1.00f) * ext4.x
                            + float3(0.84f, 0.90f, 0.96f) * ext4.y
                            + float3(0.78f, 0.86f, 0.93f) * ext4.z
                            + lerp(float3(0.66f, 0.72f, 0.79f), float3(0.92f, 0.95f, 1.00f), saturate(weather.z)) * ext4.w)
                           / max(extTot, 1.0e-6f);
    const float3 inscatterRate = (sunScat + ambScat + flashScat) * albedoMix * extTot;
    return float4(inscatterRate, extTot);
}

// Primary Wave 38 Dynamic Mountain Fog, Valley Inversion Mist, Coastal Sea-Fog & Weather Curtain Integrator.
// Designed for direct integration into LightFog.hlsl and RawTerrainWorld.hlsl:
//   - Guarantees `rawIsCelestialSkyRay` (moons, stars, aurora) receives transmittance == 1.0, inscatter == 0.
//   - Energy-conserving 8-step stratified Beer-Lambert integration with multi-scatter HG Mie sun-glow.
DynamicFogSample evalDynamicMountainAndValleyFog(
    float3 camAbsW,
    float3 hitAbsW,
    float3 rayDir,
    float  dist,
    float3 sunDir,
    float3 sunCol,
    float3 skyHor,
    float4 weather,
    float4 timeEnv,
    float4 cloudWind,
    float2 seaLevelAndTemp)
{
    DynamicFogSample res;
    res.inscatter     = 0.0f.xxx;
    res.transmittance = 1.0f;
    res.mountainFog01 = 0.0f;
    res.valleyMist01  = 0.0f;

    // Hard exemption for sky-dome celestial rays (Masser, Secunda, stars, aurora):
    const float skyGate = rawWeatherSkyExemption(dist, rayDir);
    if (skyGate <= 0.001f || dist <= 24.0f) { return res; }

    const float3 rd = normalize(rayDir);
    const float3 sd = (dot(sunDir, sunDir) > 1.0e-4f) ? normalize(sunDir) : float3(0.0f, 0.0f, 1.0f);
    const float maxDist = min(dist, 42000.0f);
    const float minDist = 32.0f;
    if (maxDist <= minDist) { return res; }

    // Stable world-coherent animation clock derived from gameHour (or zero-jitter continuous drift):
    const float simTimeSec = timeEnv.x * 360.0f + cloudWind.w * 24.0f;
    const float cosT = dot(rd, sd);
    const float dayW = smoothstep(-0.08f, 0.14f, timeEnv.y);
    const float lightning = saturate(timeEnv.z);
    const float phMtn = rawMultiScatterPhase(cosT, 0.72f, 0.42f);
    const float phVal = rawMultiScatterPhase(cosT, 0.54f, 0.32f);

    // 8-step quadratic-distributed stratified march (dense near camera for valley mist, broad at mountain peaks):
    static const int kSteps = 8;
    float tr = 1.0f;
    float3 accumCol = 0.0f.xxx;
    float tauMtn = 0.0f;
    float tauVal = 0.0f;
    float prevT = minDist;

    [unroll] for (int i = 0; i < kSteps; ++i) {
        const float u1 = float(i + 1) / float(kSteps);
        // Smooth quadratic clustering: finer sampling in foreground river valleys & distant mountain passes:
        const float curT = lerp(minDist, maxDist, u1 * (0.35f + 0.65f * u1));
        const float dt = max(curT - prevT, 1.0f);
        const float midT = 0.5f * (prevT + curT);
        prevT = curT;

        const float3 pSample = (dist <= 85000.0f)
            ? lerp(camAbsW, hitAbsW, saturate(midT / max(dist, 1.0f)))
            : (camAbsW + rd * midT);

        const float4 ext4 = evalDynamicWeatherExtinction4(pSample, weather, timeEnv, cloudWind, seaLevelAndTemp, simTimeSec) * skyGate;
        const float extTot = ext4.x + ext4.y + ext4.z + ext4.w;
        if (extTot <= 1.0e-7f) { continue; }

        const float mtnShare = ext4.x / extTot;
        const float valShare = (ext4.y + ext4.z) / extTot;
        const float prcShare = ext4.w / extTot;

        // Physically grounded scattering radiance (golden/silver sunlit alpine wisps + cool valley sky fill + lightning):
        const float ph = lerp(phVal, phMtn, mtnShare);
        const float3 sunLit = sunCol * (ph * 2.55f * dayW * lerp(1.0f, 0.52f, saturate(weather.w * 0.75f + weather.z * 0.55f)));
        const float3 skyLit = skyHor * lerp(0.55f, 0.85f, mtnShare);
        const float3 thunderLit = float3(0.62f, 0.78f, 1.22f) * (lightning * (0.55f + 0.95f * saturate(mtnShare + prcShare)));
        const float3 mediumTint = float3(0.96f, 0.98f, 1.00f) * mtnShare
                                + float3(0.84f, 0.90f, 0.96f) * valShare
                                + lerp(float3(0.66f, 0.72f, 0.80f), float3(0.93f, 0.96f, 1.00f), saturate(weather.z)) * prcShare;

        const float stepTau = extTot * dt;
        const float stepT = exp(-stepTau);
        // Exact analytical slab integration: S * (1 - exp(-sigma * dt)) / sigma * extTot = S * (1 - stepT):
        const float3 stepRadiance = (sunLit + skyLit + thunderLit) * mediumTint;
        accumCol += stepRadiance * (tr * (1.0f - stepT));

        tr *= stepT;
        tauMtn += ext4.x * dt;
        tauVal += (ext4.y + ext4.z) * dt;
    }

    // Clamp minimum transmittance so distant mountains retain atmospheric silhouettes instead of flat whiteout:
    const float minTrans = lerp(0.18f, 0.08f, saturate(weather.z * 0.75f + weather.w * 0.45f));
    const float finalTrans = lerp(1.0f, max(tr, minTrans), skyGate);
    const float energyScale = (1.0f - finalTrans) / max(1.0f - tr, 1.0e-4f);

    res.inscatter     = min(accumCol * min(energyScale, 1.0f), 3.5f.xxx);
    res.transmittance = saturate(finalTrans);
    res.mountainFog01 = saturate(1.0f - exp(-tauMtn));
    res.valleyMist01  = saturate(1.0f - exp(-tauVal));
    return res;
}

#endif
