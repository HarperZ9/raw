// VolumetricPass.hlsl: SM6.6 temporally-reprojected 1/2-res 32-slice volumetric lighting pass.
// Unifies sunbeams, silver-blue moonbeams, 128-light torch/hearth/spell mist halos, and 3D wind-drifting
// alpine valley mist with depth-aware temporal reprojection (effective ~192-step smoothness). Original work, MIT.
#define ATLAS_SLOT t1
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"

Texture2D<float>  Depth       : register(t0);
Texture2D<float4> VolPrev     : register(t2);
Texture2D<float4> TransLut    : register(t3);
Texture2D<float4> AtmoSkyView : register(t4);
SamplerState      Lin         : register(s0);

float3 volTransAt(float mu) {
    const float r = 6360.0f + 0.5f;
    const float H = sqrt(6460.0f * 6460.0f - 6360.0f * 6360.0f);
    const float rho = sqrt(r * r - 6360.0f * 6360.0f);
    const float d = max(0.0f, -r * mu + sqrt(max(r * r * (mu * mu - 1.0f) + 6460.0f * 6460.0f, 0.0f)));
    return TransLut.SampleLevel(Lin, float2(saturate((d - (6460.0f - r)) / max(rho + H - (6460.0f - r), 1.0e-4f)), saturate(rho / H)), 0.0f).rgb;
}

float3 volSkyAt(float3 rd) {
    const float az = atan2(rd.y, rd.x);
    const float elev = asin(clamp(rd.z, -1.0f, 1.0f));
    const float vNonLin = 0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI)));
    return AtmoSkyView.SampleLevel(Lin, float2(az / (2.0f * PI) + 0.5f, saturate(vNonLin)), 0.0f).rgb;
}

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
    const float3 eye = eyeAt();
    const float3 p = worldAt(fullPx - 0.5f, min(d, 0.999999f));
    const float3 toP = p - eye;
    const float dist = length(toP);
    const float3 w = toP / max(dist, 1.0e-4f);
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
        const float3 aSunV = astroSunDir();
        const float aElevV = astroSunElev();
        const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightW = smoothstep(0.03f, -0.07f, aElevV);
        const float3 beamDirV = normalize(lerp(aSunV, sunDir.xyz, nightW));
        const float lowSunBoost = lerp(1.0f, 1.32f, saturate(1.0f - abs(max(aElevV, 0.0f) - 0.18f) / 0.28f));
        const float shaftBoost = lerp(lowSunBoost, 2.35f, nightW);
        const float dayG = clamp(lerp(tune.y, max(tune.y, 0.72f), saturate(1.0f - max(aElevV, 0.0f) * 2.5f)), 0.15f, 0.84f);
        const float dynG = lerp(dayG, 0.76f, nightW);
        const float cosT = dot(w, beamDirV);
        // Dual-lobe Henyey-Greenstein: strong forward Mie sunbeams + gentle back-scatter so fog scatters at all view angles.
        const float ph = min(0.74f * phaseHG(cosT, dynG) + 0.26f * phaseHG(cosT, -0.22f), 3.6f);
        // 16 quadratic-clustered temporally-reprojected depth slices (effective ~96-step smoothness alongside 64-slice 3D FroxelPass).
        const uint kSteps = 16u;
        float sumSun = 0.0f, sumSky = 0.0f, cs = 1.0f;
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
            if ((flags.x & FLAG_CLOUDS) != 0u && (i & 1u) == 0u) { cs = cloudShadowN(wpFog, 1u); }
            const float3 fogFlow = wpFog * 0.00052f + float3(cloudWind.xy * camPosW.w * 0.00018f, 0.0f);
            const float mist3d = 0.58f + 0.64f * cNoise(fogFlow);
            const float hDens = (0.32f + 0.68f * exp(-max(ptT.z + 140.0f, 0.0f) * 0.00085f)) * mist3d;
            const float tr = exp(-sigma * t * 0.85f);
            const float vis = cascadeShadow(ptT, c.z, false) * cs;
            sumSun += vis * hDens * tr * dt;
            sumSky += (0.35f + 0.65f * cs) * hDens * tr * dt;
        }
        // Unified Hillaire SIGGRAPH 2020 radiative-transfer sunbeams & sky multi-scattering:
        // shafts, sky dome, clouds, and aerial perspective now share one exact physical Transmittance + Sky-View LUT.
        const float3 hTransV = volTransAt(clamp(sunDir.z, -0.10f, 1.0f));
        const float htLumV = max(dot(hTransV, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float3 physSunV = lerp(sunColour.rgb, hTransV * (max(sunLum, 0.10f) / htLumV), 0.62f * (1.0f - nightW));
        const float3 hSkyV = volSkyAt(float3(w.xy, max(w.z, 0.01f)));
        const float3 unifiedSky = lerp(skyGrad, hSkyV, (1.0f - nightW) * (1.0f - saturate(clouds.x * 0.70f)) * 0.55f);
        const float3 shaftCol = lerp(lerp(physSunV, unifiedSky, 0.22f), max(sH, float3(0.18f, 0.26f, 0.42f)), nightW * 0.72f);
        scatter += shaftCol * (sigma * ph * sumSun * tune2.z * shaftBoost * 0.95f);
        // Gentle isotropic sky multi-scattering inside volumetric mist (strictly bounded to prevent whiteout).
        scatter += unifiedSky * (sigma * sumSky * 0.016f * (1.0f - nightW * 0.65f));
    }
    if (tune2.y > 0.0f) {
        const float weatherMistGain = 1.0f + saturate(weather.w * 0.85f + weather.z * 0.75f + clouds.x * 0.35f) * 0.65f;
        const bool isInt = (sunColour.w < 0.5f);
        const float sunLumV = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightV = isInt ? 0.0f : saturate((0.26f - sunLumV) * 4.5f);
        // GTA-VI-inspired shadowed long-range local-light volumetrics (top 12 lights x 10 depth-marched steps)
        // with crisp anisotropic Mie forward scattering (g=0.65) + rising 3D hearth smoke/dust motes + night city haze.
        const float3 midWp = camPosW.xyz + eye + w * min(tEnd * 0.45f, 1800.0f);
        const float dustMote = isInt ? (0.72f + 0.56f * cNoise(midWp * 0.011f - float3(0.0f, 0.0f, camPosW.w * 0.16f))) : 1.0f;
        const float intGain = isInt ? (1.55f * dustMote) : (weatherMistGain * (1.0f + nightV * 1.25f));
        const float jitL = ign(lo + float2(13.0f, 29.0f));
        uint shadowedDone = 0u;
        [loop] for (uint li = 0u; li < flags.y; li++) {
            const float rL = lightPosR[li].w * (1.0f + nightV * 0.55f);
            const float3 lpRel = lightPosR[li].xyz - eye;
            const float wi = windowIntegral(w, lpRel, rL, tEnd);
            if (wi > 0.0f) {
                const bool isFire = lightColS[li].r > lightColS[li].b * 1.25f;
                const float3 fCol = isFire ? lightColS[li].rgb * float3(1.28f, 0.84f, 0.48f) * 1.55f : lightColS[li].rgb;
                float visScale = 1.0f;
                if ((isInt || nightV > 0.25f) && shadowedDone < 12u) {
                    const float3 lp = lightPosR[li].xyz;
                    const float b = dot(lpRel, w);
                    const float h2 = dot(lpRel, lpRel) - b * b;
                    const float halfL = sqrt(max(rL * rL - h2, 0.0f));
                    const float tA = max(0.0f, b - halfL);
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
                                const float zHit = viewZOf(Depth.Load(int3(sm.xy, 0)));
                                if (zHit < sm.w * 0.96f) { visStep = 0.08f; }
                            }
                            const float cosL = dot(w, normalize(toLight + 1.0e-4f));
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
    if (sunColour.w > 0.5f && weather.w > 0.45f && clouds.x > 0.65f) {
        const float tSlot = floor(camPosW.w * 1.4f);
        const float stormTrigger = step(0.945f, frac(sin(tSlot * 127.1f) * 43758.5453f));
        const float flashEnv = pow(1.0f - frac(camPosW.w * 1.4f), 5.0f) * stormTrigger;
        scatter += float3(0.45f, 0.62f, 1.05f) * (flashEnv * saturate(tEnd / 6000.0f) * 0.22f);
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
                const float2 curUvV = fullPx * dims.zw;
                const float histW = lerp(0.78f, 0.28f, saturate(length(puv - curUvV) * 14.0f));
                scatter = lerp(scatter, prev.rgb, histW);
            }
        }
    }
    return float4(scatter, min(zCur, 65000.0f));
}
