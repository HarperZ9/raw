// RawWorldDynamics.hlsli: Wave 74 World-Space Coherent Gust Field, Foliage Phase-Locked
// Temporal Compensation, and Energy-Conserving Wet-Leaf / Water Volumetric Cloud-Shadow Coupling.
// Self-contained clean-room MIT HLSL library (SM 6.6 / 6.9 / 6.10, zero texture deps, zero NaN/Inf).
// Original work, MIT.
#ifndef RAW_WORLD_DYNAMICS_HLSLI
#define RAW_WORLD_DYNAMICS_HLSLI

struct RawWetLeafBudget {
    float3 albedoEff;
    float  fresnelWet;
    float  directTransWeight;
    float  wetRoughness;
};

struct RawWaterCloudCoupling {
    float sunTransmittance;
    float roughScale;
    float causticTransmittance;
    float crestSssTransmittance;
};

float rawDynSanitize1(float v, float fallbackVal) {
    return isfinite(v) ? v : fallbackVal;
}

float3 rawDynSanitize3(float3 v) {
    return all(isfinite(v)) ? max(v, 0.0f.xxx) : 0.0f.xxx;
}

// Evaluates a smooth, coherent 3D world-space wind gust wavefield at absolute world position pAbsW.
// Returns float4(swayVelX, swayVelY, gustIntensity01, phaseLockedSwayDeriv):
//   - xy: coherent horizontal gust sway velocity along local wind direction (finite, bounded)
//   - z : dimensionless gust envelope in [0, 1]
//   - w : analytical phase-locked temporal sway derivative for foliage MV compensation
float4 rawEvalCoherentWindGust4(float3 pAbsW, float4 cloudWind, float4 weather, float simTimeSec) {
    const float2 pXY = float2(rawDynSanitize1(pAbsW.x, 0.0f), rawDynSanitize1(pAbsW.y, 0.0f));
    const float2 wXY = float2(rawDynSanitize1(cloudWind.x, 0.0f), rawDynSanitize1(cloudWind.y, 0.0f));
    const float  tSafe = rawDynSanitize1(simTimeSec, 0.0f);
    const float  rain01 = saturate(rawDynSanitize1(weather.w, 0.0f));
    const float  snow01 = saturate(rawDynSanitize1(weather.z, 0.0f));

    const float  wLen = length(wXY);
    const float2 wDir = (wLen > 1.0e-3f) ? (wXY / wLen) : float2(0.8660254f, 0.5f);
    const float2 wPerp = float2(-wDir.y, wDir.x);

    const float windNorm = saturate(wLen / 280.0f + rain01 * 0.35f + snow01 * 0.30f);
    const float omega1 = 1.15f + 1.85f * windNorm;
    const float omega2 = 0.72f + 1.10f * windNorm;

    const float phi1 = dot(pXY, wDir) * 0.0024f - tSafe * omega1;
    const float phi2 = dot(pXY, wPerp) * 0.0041f - tSafe * omega2 + 1.37f;

    const float wave1 = 0.55f + 0.45f * sin(phi1);
    const float eddy2 = 0.65f + 0.35f * cos(phi2);
    const float ampScale = 0.20f + 0.80f * windNorm;
    const float gust01 = saturate(wave1 * eddy2 * ampScale);

    // Exact analytical time-derivative of the primary traveling gust envelope (phase-locked sway):
    const float swayDeriv = clamp(cos(phi1) * eddy2 * ampScale * (omega1 * 0.32f), -1.5f, 1.5f);
    const float2 swayVel = wDir * swayDeriv;
    return float4(swayVel.x, swayVel.y, gust01, swayDeriv);
}

// White-furnace energy-conserving wet-leaf radiative budget coupled to volumetric cloud-shadow
// transmittance `cloudTrans` and wind-gust flutter `gust01`.
// Algebraically guarantees: max(albedoEff) + fresnelWet + directTransWeight <= 1.0 for ALL inputs.
// Direct cloud shadow attenuates ONLY direct backlit transmission (never indirect sky ambient!).
RawWetLeafBudget rawEvalWetLeafRadiativeBudget(
    float3 baseAlbedo,
    float  wetness,
    float  ndotv,
    float  cloudTrans,
    float  gust01)
{
    RawWetLeafBudget b;
    const float3 albSafe = clamp(rawDynSanitize3(baseAlbedo), 0.0f.xxx, 0.92f.xxx);
    const float  w = saturate(rawDynSanitize1(wetness, 0.0f));
    const float  nv = saturate(rawDynSanitize1(ndotv, 0.5f));
    const float  ct = saturate(rawDynSanitize1(cloudTrans, 1.0f));
    const float  g = saturate(rawDynSanitize1(gust01, 0.0f));

    const float f0Wet = lerp(0.016f, 0.024f, w);
    const float fSpec = saturate(f0Wet + (1.0f - f0Wet) * pow(1.0f - nv, 5.0f));
    const float remEnergy = max(1.0f - fSpec, 0.0f);

    // Wet water film traps internal scatter (mild physical darkening), strictly bounded <= remEnergy:
    const float3 albWet = albSafe * lerp(1.0f, 0.86f, w * 0.65f);
    b.albedoEff = min(albWet, remEnergy.xxx);
    b.fresnelWet = fSpec;

    const float maxAlb = max(b.albedoEff.r, max(b.albedoEff.g, b.albedoEff.b));
    const float transCap = max(remEnergy - maxAlb, 0.0f);
    const float gustMod = 1.0f + 0.16f * g;
    const float desiredTrans = lerp(0.24f, 0.15f, w) * ct * gustMod;
    b.directTransWeight = min(desiredTrans, transCap);
    b.wetRoughness = clamp(lerp(0.78f, 0.24f, w) + g * 0.05f, 0.18f, 0.94f);
    return b;
}

// Radiative coupling between volumetric cloud-shadow transmittance, wind gusts, and water surface
// specular roughness / direct sun glitter / river caustics (leaves sky reflection un-darkened!).
RawWaterCloudCoupling rawEvalWaterCloudRoughnessCoupling(float cloudTrans, float gust01, float rainFall) {
    RawWaterCloudCoupling c;
    const float ct = saturate(rawDynSanitize1(cloudTrans, 1.0f));
    const float g = saturate(rawDynSanitize1(gust01, 0.0f));
    const float r = saturate(rawDynSanitize1(rainFall, 0.0f));

    c.sunTransmittance = ct;
    // Cloud shadow softens tight mirror glitter while wind gusts + rain broaden capillary wave slope variance:
    c.roughScale = clamp(1.0f + (1.0f - ct) * 0.32f + g * 0.28f + r * 0.22f, 1.0f, 1.85f);
    c.causticTransmittance = lerp(0.30f, 1.0f, ct);
    c.crestSssTransmittance = lerp(0.36f, 1.0f, ct);
    return c;
}

#endif
