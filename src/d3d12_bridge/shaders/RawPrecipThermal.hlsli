// RawPrecipThermal.hlsli: Wave 76 World-Space Thermal/Wind Precipitation Accumulation
// (Concave/Leeward Snow Drift + Torch/Brazier Melt) and Energy-Conserving Cloud-Crepuscular
// Shafts + Anisotropic Sky-to-Water Multiple-Scattering Coupling.
// Self-contained clean-room MIT HLSL library (SM 6.6 / 6.9 / 6.10, 0 screen-UV dependence, 0 NaN/Inf).
// Original work, MIT.
#ifndef RAW_PRECIP_THERMAL_HLSLI
#define RAW_PRECIP_THERMAL_HLSLI

struct RawThermalPrecipResult {
    float snowAccum01;     // Effective world-space snow accumulation after concave/leeward drift & thermal melt [0, 1]
    float meltCore01;      // Bare thawed warm core right beside torch/brazier/campfire [0, 1]
    float wetSlush01;      // Glistening wet dark-stone / slush transition collar [0, 1]
    float meltRoughness;   // Energy-conserving surface roughness (matte powder -> glossy wet melt)
    float albedoScale;     // Wet darkening factor strictly in [0.58, 1.0]
};

float rawPtSan1(float v, float fb) {
    return isfinite(v) ? v : fb;
}

float3 rawPtSan3(float3 v) {
    return all(isfinite(v)) ? max(v, 0.0f.xxx) : 0.0f.xxx;
}

// Pure absolute-world micro-topography heightfield (matches Weather.hlsli::rawWorldMicroHeight, zero screen UV!).
float rawPtWorldHeight(float2 wpXY) {
    const float2 q = float2(rawPtSan1(wpXY.x, 0.0f), rawPtSan1(wpXY.y, 0.0f)) * 0.014f;
    return 0.55f * sin(q.x) * cos(q.y)
         + 0.30f * cos(q.x * 2.13f + 1.7f) * sin(q.y * 1.91f - 0.8f)
         + 0.15f * sin((q.x + q.y) * 3.17f + 0.5f);
}

// Discrete Laplacian concavity nabla^2 H > 0 (sheltered ruts/depressions trap drifting snow & snowmelt).
float rawPtLaplacianConcavity(float2 wpXY) {
    const float h = 18.0f;
    const float hc = rawPtWorldHeight(wpXY);
    const float lap = (rawPtWorldHeight(wpXY + float2(h, 0.0f))
                     + rawPtWorldHeight(wpXY - float2(h, 0.0f))
                     + rawPtWorldHeight(wpXY + float2(0.0f, h))
                     + rawPtWorldHeight(wpXY - float2(0.0f, h))
                     - 4.0f * hc) / (h * h * 0.0004f);
    return saturate(max(rawPtSan1(lap, 0.0f), 0.0f) * 0.65f);
}

// World-space thermal & wind-driven precipitation accumulation (100% screen-edge invariant):
// Couples Laplacian concavity + coherent wind leeward shelter + local torch/brazier thermal flux.
RawThermalPrecipResult rawEvalWorldThermalSnowAccum(
    float3 wpAbs,
    float3 normalW,
    float2 windXY,
    float  gust01,
    float  baseSnow01,
    float  tempC,
    float  fireHeat01,
    float  skyVis)
{
    RawThermalPrecipResult o;
    const float3 nSafe = all(isfinite(normalW)) ? normalize(normalW + float3(0.0f, 0.0f, 1.0e-5f)) : float3(0.0f, 0.0f, 1.0f);
    const float2 wSafe = float2(rawPtSan1(windXY.x, 0.0f), rawPtSan1(windXY.y, 0.0f));
    const float  wLen = length(wSafe);
    const float2 wDir = (wLen > 1.0e-3f) ? (wSafe / wLen) : float2(0.8660254f, 0.5f);

    const float g01 = saturate(rawPtSan1(gust01, 0.0f));
    const float sIn = saturate(rawPtSan1(baseSnow01, 0.0f));
    const float tC  = clamp(rawPtSan1(tempC, -2.0f), -45.0f, 45.0f);
    const float fH  = clamp(rawPtSan1(fireHeat01, 0.0f), 0.0f, 1.5f);
    const float sv  = saturate(rawPtSan1(skyVis, 1.0f));

    // 1) Concave shelter & leeward wind-drift accumulation (windward crests scour, leeward hollows drift):
    const float concav = rawPtLaplacianConcavity(wpAbs.xy);
    const float leeward = saturate(dot(nSafe.xy, wDir) * 0.5f + 0.5f);
    const float upSlope = smoothstep(0.32f, 0.80f, nSafe.z);
    const float windScour = saturate((1.0f - leeward) * g01 * 0.35f);
    const float coldFactor = smoothstep(3.0f, -4.0f, tC);
    const float driftBoost = (0.76f + 0.28f * concav + 0.16f * leeward * g01 - windScour);
    const float snowPreMelt = saturate(sIn * upSlope * driftBoost * lerp(0.45f, 1.0f, coldFactor) * lerp(0.35f, 1.0f, sv));

    // 2) Active local thermal melt from torches, braziers, campfires (monotonically melts snow -> dark wet stone/slush):
    const float meltCore = smoothstep(0.14f, 0.68f, fH);
    const float slushCollar = saturate(smoothstep(0.04f, 0.24f, fH) * (1.0f - smoothstep(0.46f, 0.86f, fH)));
    const float wetMelt = saturate(max(meltCore * 0.85f, slushCollar * 0.78f) * max(snowPreMelt, sIn * 0.55f));

    o.snowAccum01   = saturate(snowPreMelt * (1.0f - meltCore));
    o.meltCore01    = meltCore;
    o.wetSlush01    = wetMelt;
    o.meltRoughness = clamp(lerp(0.84f, 0.14f, wetMelt), 0.12f, 0.92f);
    o.albedoScale   = clamp(lerp(1.0f, 0.64f, wetMelt), 0.58f, 1.0f);
    return o;
}

// Energy-conserving two-order Henyey-Greenstein phase function for cloud-crepuscular shafts:
// Blends forward Mie shaft lobe (g=0.72) with isotropic multi-scattering fill (g=0.22) as optical depth grows,
// strictly bounded by single-scattering albedo <= 0.96 so shafts never blow out highlights or crush shadows.
float rawEvalCrepuscularPhase(float cosTheta, float opticalDepth, float cloudTrans) {
    const float mu = clamp(rawPtSan1(cosTheta, 0.0f), -1.0f, 1.0f);
    const float tau = clamp(rawPtSan1(opticalDepth, 0.0f), 0.0f, 12.0f);
    const float ct = saturate(rawPtSan1(cloudTrans, 1.0f));

    const float g1 = 0.72f;
    const float g2 = 0.22f;
    const float d1 = max(1.0f + g1 * g1 - 2.0f * g1 * mu, 0.04f);
    const float d2 = max(1.0f + g2 * g2 - 2.0f * g2 * mu, 0.04f);
    const float ph1 = (1.0f - g1 * g1) / (4.0f * 3.14159265f * pow(d1, 1.5f));
    const float ph2 = (1.0f - g2 * g2) / (4.0f * 3.14159265f * pow(d2, 1.5f));
    const float multiW = saturate(1.0f - exp(-tau * 0.65f));
    // Cloud-gap transmittance weights forward crepuscular beam while preserving soft multi-scattered ambient fill:
    const float phase = lerp(ph1 * lerp(0.35f, 1.0f, ct), ph2, multiW * 0.55f);
    return clamp(phase * 0.96f, 0.0f, 2.8f);
}

// Anisotropic sky-to-water multiple-scattering coupling:
// Accounts for grazing micro-facet wave bounce without exceeding max(skyRefl, waterBody) (white-furnace safe!).
float3 rawEvalSkyWaterMultiScatter(
    float3 skyRefl,
    float3 waterBody,
    float  fresnelF,
    float  cloudTrans,
    float  ndotv,
    float  windGust01)
{
    const float3 skySafe = clamp(rawPtSan3(skyRefl), 0.0f.xxx, 12.0f.xxx);
    const float3 bodSafe = clamp(rawPtSan3(waterBody), 0.0f.xxx, 12.0f.xxx);
    const float  f0 = saturate(rawPtSan1(fresnelF, 0.02f));
    const float  ct = saturate(rawPtSan1(cloudTrans, 1.0f));
    const float  nv = saturate(rawPtSan1(ndotv, 0.5f));
    const float  g01 = saturate(rawPtSan1(windGust01, 0.0f));

    // Multiple-bounce wave-trough sky-to-water transfer strengthens gently at grazing angles under gusts/overcast:
    const float graze = (1.0f - nv) * (1.0f - nv);
    const float multiBounce = saturate(graze * (0.06f + 0.10f * g01) * lerp(1.0f, 0.72f, ct));
    const float fEff = clamp(f0 + (1.0f - f0) * multiBounce, 0.0f, 0.98f);
    return lerp(bodSafe, skySafe, fEff);
}

#endif
