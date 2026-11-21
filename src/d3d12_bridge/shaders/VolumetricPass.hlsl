// VolumetricPass.hlsl: SM6.6 temporally-reprojected 1/2-res 32-slice volumetric lighting pass.
// Unifies sunbeams, silver-blue moonbeams, 128-light torch/hearth/spell mist halos, and 3D wind-drifting
// alpine valley mist with depth-aware temporal reprojection (effective ~192-step smoothness). Original work, MIT.
#define ATLAS_SLOT t1
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"

Texture2D<float>  Depth   : register(t0);
Texture2D<float4> VolPrev : register(t2);
SamplerState      Lin     : register(s0);

float phaseHG(float cosT, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f), 1.5f));
}

float windowIntegral(float3 w, float3 lp, float r, float tEnd) {
    const float b = dot(lp, w);
    const float h2 = dot(lp, lp) - b * b;
    const float d2 = r * r - h2;
    if (d2 <= 0.0f) { return 0.0f; }
    const float halfLen = sqrt(d2);
    const float u0 = max(0.0f, b - halfLen) - b, u1 = min(tEnd, b + halfLen) - b;
    if (u1 <= u0) { return 0.0f; }
    return (1.0f - h2 / (r * r)) * (u1 - u0) - (u1 * u1 * u1 - u0 * u0 * u0) / (3.0f * r * r);
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const float2 lo = floor(at.xy);
    const float2 fullPx = (lo + 0.5f) * 2.0f;
    const int2 fullIp = clamp(int2(fullPx), int2(0, 0), int2(dims.xy) - 1);
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }
    const float d = Depth.Load(int3(fullIp, 0));
    const float3 p = worldAt(fullPx - 0.5f, min(d, 0.999999f));
    const float dist = length(p);
    const float3 w = p / max(dist, 1.0e-4f);
    // Cap volumetric shaft integration to 12,000 units so open ocean/coast horizons never over-accumulate nuclear scatter.
    const float tEnd = min(dist, min(tune2.w, 12000.0f));
    const float weatherDens = 1.0f + weather.w * 1.15f + weather.z * 1.35f + saturate(clouds.x - 0.25f) * 0.55f;
    const float sigma = max(tune.x, 0.00014f) * weatherDens;
    const float zCur = d < 1.0f ? viewZOf(d) : 65000.0f;

    // Unified atmospheric palette shared with Sky, Clouds, and Mountain Aerial Perspective.
    float3 sZ, sM, sH;
    skyPalette(sZ, sM, sH);
    const float3 skyGrad = lerp(sH, lerp(sM, sZ, smoothstep(0.18f, 0.72f, w.z)), smoothstep(-0.08f, 0.26f, w.z));
    const float highSun = smoothstep(0.18f, 0.48f, sunDir.z);

    float3 scatter = 0.0f.xxx;
    if (sunDir.w > 0.5f && tune2.z > 0.0f) {
        const float jit = ign(lo);
        const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightW = saturate((0.28f - sunLum) * 4.5f);
        const float lowSunBoost = lerp(1.0f, 1.32f, saturate(1.0f - abs(sunDir.z - 0.18f) / 0.28f));
        const float shaftBoost = lerp(lowSunBoost, 3.10f, nightW);
        const float dayG = clamp(lerp(tune.y, max(tune.y, 0.72f), saturate(1.0f - sunDir.z * 2.5f)), 0.15f, 0.84f);
        const float dynG = lerp(dayG, 0.76f, nightW);
        const float cosT = dot(w, sunDir.xyz);
        // Dual-lobe Henyey-Greenstein: strong forward Mie sunbeams + gentle back-scatter so fog scatters at all view angles.
        const float ph = min(0.74f * phaseHG(cosT, dynG) + 0.26f * phaseHG(cosT, -0.22f), 3.6f);
        // 32 quadratic-clustered depth slices with per-slice 3D mist, cloud shadows, and multi-scatter ambient sky lift.
        const uint kSteps = 32u;
        float sumSun = 0.0f, sumSky = 0.0f, cs = 1.0f;
        float tPrev = 0.0f;
        [loop] for (uint i = 0u; i < kSteps; i++) {
            const float u1 = (float(i) + 1.0f) / float(kSteps);
            const float tNext = tEnd * u1 * u1;
            const float dt = max(tNext - tPrev, 1.0e-3f);
            const float t = tPrev + jit * dt;
            tPrev = tNext;
            const float4 c = screenOf(w * t);
            const float3 wpFog = w * t + camPosW.xyz;
            if ((flags.x & FLAG_CLOUDS) != 0u && (i & 1u) == 0u) { cs = cloudShadowN(wpFog, 1u); }
            const float3 fogFlow = wpFog * 0.00052f + float3(cloudWind.xy * camPosW.w * 0.00018f, 0.0f);
            const float mist3d = 0.58f + 0.64f * cNoise(fogFlow);
            const float hDens = (0.32f + 0.68f * exp(-max(w.z * t + 140.0f, 0.0f) * 0.00085f)) * mist3d;
            const float tr = exp(-sigma * t * 0.85f);
            const float vis = cascadeShadow(w * t, c.z, false) * cs;
            sumSun += vis * hDens * tr * dt;
            sumSky += (0.35f + 0.65f * cs) * hDens * tr * dt;
        }
        // Spectral alignment: CIE D65 high-latitude sun blended with the live atmospheric gradient & cloud tint.
        const float3 d65Sun = lerp(sunColour.rgb, float3(0.96f, 0.98f, 1.02f) * max(sunLum, 0.12f), highSun * 0.58f);
        const float3 shaftCol = lerp(lerp(d65Sun, skyGrad, 0.24f), max(sH, float3(0.18f, 0.26f, 0.42f)), nightW * 0.72f);
        scatter += shaftCol * (sigma * ph * sumSun * tune2.z * shaftBoost * 0.95f);
        // Gentle isotropic sky multi-scattering inside volumetric mist (strictly bounded to prevent whiteout).
        scatter += skyGrad * (sigma * sumSky * 0.016f * (1.0f - nightW * 0.65f));
    }
    if (tune2.y > 0.0f) {
        const float weatherMistGain = 1.0f + saturate(weather.w * 0.85f + weather.z * 0.75f + clouds.x * 0.35f) * 0.65f;
        [loop] for (uint li = 0u; li < flags.y; li++) {
            const float wi = windowIntegral(w, lightPosR[li].xyz, lightPosR[li].w, tEnd);
            if (wi > 0.0f) {
                const bool isFire = lightColS[li].r > lightColS[li].b * 1.30f;
                const float3 fCol = isFire ? lightColS[li].rgb * float3(1.22f, 0.86f, 0.54f) * 1.45f : lightColS[li].rgb;
                scatter += fCol * (sigma * wi * tune2.y * weatherMistGain / (4.0f * PI));
            }
        }
    }
    // Physical energy-bounded highlight shoulder (prevents volumetric fog from pushing coastal skies/snow into nuclear bloom).
    const float scMax = max(scatter.r, max(scatter.g, scatter.b));
    scatter = (scatter / (1.0f + scMax * 1.45f)) * 0.36f;

    // Temporal reprojection with depth-disocclusion rejection (stores viewZ in alpha for depth-aware upsampling).
    const float4 cp = float4(p, 1.0f);
    const float4 pc = float4(dot(prevWorldToClip[0], cp), dot(prevWorldToClip[1], cp), dot(prevWorldToClip[2], cp), dot(prevWorldToClip[3], cp));
    if (pc.w > 0.0f && proj.z > 0.5f) {
        const float2 puv = float2(pc.x / pc.w * 0.5f + 0.5f, 0.5f - pc.y / pc.w * 0.5f) / resScale.xy;
        if (all(puv > 0.01f) && all(puv < 0.99f)) {
            const float4 prev = VolPrev.SampleLevel(Lin, puv, 0.0f);
            const float zRel = abs(prev.a - min(zCur, 65000.0f)) / max(min(zCur, 65000.0f), 1.0f);
            if (zRel < 0.08f) {
                scatter = lerp(scatter, prev.rgb, 0.78f);
            }
        }
    }
    return float4(scatter, min(zCur, 65000.0f));
}
