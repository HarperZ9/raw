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
    const float tEnd = min(dist, tune2.w);
    const float sigma = tune.x;
    const float zCur = d < 1.0f ? viewZOf(d) : 65000.0f;

    float3 scatter = 0.0f.xxx;
    if (sunDir.w > 0.5f && tune2.z > 0.0f) {
        const float jit = ign(lo);
        const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightW = saturate((0.28f - sunLum) * 4.5f);
        const float lowSunBoost = lerp(1.0f, 1.18f, saturate(1.0f - abs(sunDir.z - 0.18f) / 0.28f));
        const float shaftBoost = lerp(lowSunBoost, 3.10f, nightW);
        const float dayG = clamp(lerp(tune.y, max(tune.y, 0.70f), saturate(1.0f - sunDir.z * 2.5f)), 0.1f, 0.82f);
        const float dynG = lerp(dayG, 0.76f, nightW);
        const float ph = min(phaseHG(dot(w, sunDir.xyz), dynG), 3.4f);
        // 32 quadratic-clustered depth slices with per-slice 3D mist and cloud-shadow shafts.
        const uint kSteps = 32u;
        float sum = 0.0f, cs = 1.0f;
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
            const float3 fogFlow = wpFog * 0.00055f + float3(cloudWind.xy * camPosW.w * 0.00018f, 0.0f);
            const float mist3d = 0.62f + 0.58f * cNoise(fogFlow);
            const float hDens = (0.35f + 0.65f * exp(-max(w.z * t + 120.0f, 0.0f) * 0.0009f)) * mist3d;
            sum += cascadeShadow(w * t, c.z, false) * cs * hDens * exp(-sigma * t) * dt;
        }
        const float3 shaftCol = lerp(sunColour.rgb, max(sunColour.rgb, float3(0.22f, 0.31f, 0.48f)), nightW * 0.72f);
        scatter += shaftCol * (sigma * ph * sum * tune2.z * shaftBoost);
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
    // Soft highlight shoulder + moist Nordic volumetric exposure.
    const float scMax = max(scatter.r, max(scatter.g, scatter.b));
    scatter = (scatter / (1.0f + scMax * 0.85f)) * 0.82f;

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
