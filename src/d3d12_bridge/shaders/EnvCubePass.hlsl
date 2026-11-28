// EnvCubePass.hlsl: Real-Time Dual-Band Prefiltered Dynamic Environment Cubemap (SM 6.6 / 6.10).
// Runs every frame into a 256x256 RGBA16F equirectangular atlas (< 0.08 ms):
//   Top half    (y = 0..127):   Sharp glossy environment (roughness ~ 0.05) — live Hillaire 2020 Chappuis-Ozone
//                               AtmoSkyView, live 3D Volumetric Curtain Auroras & Milky Way / Aetherius galaxy +
//                               O/B/A/F/G/K/M stars (RawCosmosSky.hlsli), live 64^3 Perlin-Worley Clouds, crisp
//                               sun/moon specular lobes, ground/snow bounce, screen-space scene projection, and
//                               up to 64 live dynamic point lights (hearths, torches, braziers, spells).
//   Bottom half (y = 128..255): Prefiltered rough/convolved environment (roughness ~ 0.75) — 5-tap hemisphere
//                               convolution carrying live aurora, galaxy, and twilight ozone bounce smoothly onto
//                               rough stone, glacial snow, brushed metal, leather, and interior vaults.
// Lighting.hlsl & RtWater.hlsl sample this cubemap so water, ice, wet stone & armor reflect live aurora & galaxy!
// Original work, MIT.
#include "LightingCommon.hlsli"
#include "RawCosmosSky.hlsli"

Texture2D<float4> AtmoSkyView : register(t0);
Texture2D<float4> CloudNoise  : register(t1);
Texture2D<float4> GameLit     : register(t2);
Texture2D<float>  Depth       : register(t3);
SamplerState      Lin         : register(s0);

// Wave 61: Strict NaN/Inf sanitization & anisotropic mountain-silhouette horizon contour helpers.
float3 sanitizeEnv3(float3 c) {
    return all(isfinite(c)) ? clamp(c, 0.0f.xxx, 3.5f.xxx) : 0.0f.xxx;
}

// Evaluates a world-locked 360-deg alpine mountain-ridge silhouette elevation angle (radians) around the horizon.
// Higher jagged ridges to the north/east & lower open coastal valleys to the south/west, receding smoothly at summit altitude.
float evalHorizonMountainRidgeSin(float2 rdXY, float3 camAbs) {
    const float hLen = length(rdXY);
    const float az = (hLen > 1.0e-5f) ? atan2(rdXY.y, rdXY.x) : 0.0f;
    const float shift = (camAbs.x * 0.000018f - camAbs.y * 0.000014f);
    // Multi-harmonic alpine ridge silhouette (smoothly periodic in 2*PI around the horizon):
    const float r1 = 0.50f + 0.50f * sin(az * 2.0f + 0.65f + shift);
    const float r2 = 0.50f + 0.50f * sin(az * 5.0f - 1.35f - shift * 1.4f);
    const float r3 = 0.50f + 0.50f * cos(az * 9.0f + 2.10f + shift * 0.7f);
    const float ridgeProfile = r1 * 0.48f + r2 * 0.34f + r3 * 0.18f;
    // Taller mountain walls toward northern/eastern alpine ranges (+Y/+X); lower when camera stands on high peaks:
    const float northEastBias = 0.72f + 0.28f * saturate((rdXY.y + rdXY.x * 0.5f) / max(hLen, 1.0e-4f) * 0.5f + 0.5f);
    const float altRecede = 1.0f - smoothstep(4500.0f, 18000.0f, max(camAbs.z, 0.0f)) * 0.70f;
    return lerp(0.012f, 0.092f, ridgeProfile) * northEastBias * altRecede;
}

float3 sampleAtmo(float3 rd) {
    const float az = (abs(rd.x) + abs(rd.y) > 1.0e-5f) ? atan2(rd.y, rd.x) : 0.0f;
    const float elev = asin(clamp(rd.z, -1.0f, 1.0f));
    const float vNonLin = 0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI)));
    return sanitizeEnv3(AtmoSkyView.SampleLevel(Lin, float2(az / (2.0f * PI) + 0.5f, saturate(vNonLin)), 0.0f).rgb);
}

float3 evalEnvDir(float3 rd, bool roughBand) {
    const bool isExt = sunColour.w > 0.5f;
    const float3 ambDir = max(float3(dot(ambient[0].xyz, rd) + ambient[0].w,
                                     dot(ambient[1].xyz, rd) + ambient[1].w,
                                     dot(ambient[2].xyz, rd) + ambient[2].w), 0.015f.xxx);
    float3 col = 0.0f.xxx;
    if (isExt) {
        const float3 aSun = astroSunDir();
        const float aElev = astroSunElev();
        const float simTime = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
        const float gameHour = timeEnv.x;

        // Smooth transition across day, Chappuis-ozone twilight (Belt of Venus + Earth Shadow), and starry aurora night:
        const float dayW     = smoothstep(-0.08f, 0.14f, aElev);
        const float atmoDayW = smoothstep(-0.11f, 0.05f, aElev);
        const float nightW   = smoothstep(0.05f, -0.08f, aElev);
        const float twilightW = smoothstep(-0.12f, -0.01f, aElev) * (1.0f - smoothstep(0.04f, 0.14f, aElev));

        const float3 upRd = normalize(float3(rd.xy, max(rd.z, 0.005f)));
        const float3 hSky = sampleAtmo(upRd);

        // Wave 43 Orbital Dual-Moon Phase Illumination (Masser 24-day rose-silver & Secunda 16-day pale-cyan-silver):
        // Shifts night IBL reflections smoothly between luminous moonlit snowscapes & dark new-moon aurora/Milky-Way nights!
        const float seasonProg = rawCosmosSeasonProgress01(phenology0, phenology1, physWorld[1].w, waterB.w, simTime);
        const DualMoonLight dualMoon = evalDualMoonPhaseIllum(seasonProg, gameHour);
        const float moonAmbScale = lerp(0.44f, 1.38f, dualMoon.totalPhase01);
        const float moonSpecScale = lerp(0.14f, 1.35f, dualMoon.totalPhase01);

        // Deep nocturnal sky floor + phase-coupled Milky Way / Aetherius galaxy, warm bulge scatter,
        // dark dust rifts, ecliptic zodiacal light, H-II nebulae, 3D volumetric O/N2+ curtain auroras,
        // Kolmogorov-scintillating spectral stars & 22-deg lunar/solar ice halos + sundogs directly in the cubemap!
        // Sharp glossy band includes crisp stars & Eddington limb-darkened sun so calm water/ice/steel mirrors the sky;
        // rough band disables pinpoint stars so rough stone/snow gets smooth, flicker-free aurora, halo & corona IBL.
        const bool hasTherm = (seasonThermal0.y > 0.05f || abs(seasonThermal0.w) > 0.01f);
        const float surfaceTempC = hasTherm ? seasonThermal0.w : lerp(4.0f, -9.0f, saturate(weather.z));

        const float3 nightBase = max(
            lerp(ambDir * dualMoon.moonTintRGB * (float3(0.46f, 0.58f, 0.86f) * moonAmbScale),
                 float3(0.006f, 0.012f, 0.030f) * lerp(0.70f, 1.55f, dualMoon.totalPhase01),
                 smoothstep(0.05f, 0.65f, upRd.z)),
            float3(0.005f, 0.009f, 0.024f));
        float3 liveCosmos = 0.0f.xxx;
        if (rd.z > -0.02f && (nightW > 0.01f || twilightW > 0.01f)) {
            liveCosmos = evalCosmosAndAuroraThermal(
                upRd, aSun, gameHour, simTime, clouds.x, weather.z, camPosW.y, seasonProg, surfaceTempC,
                !roughBand,  // pinpoint Kolmogorov stars & meteors in sharp band only; smooth aurora+galaxy in rough band
                !roughBand,  // Eddington solar disk in sharp band, smooth corona in both
                true         // apply Kasten-Young airmass extinction near horizon
            );
        }
        // Avoid double-counting aurora already baked into AtmoSkyView during deep night:
        // At day/twilight, AtmoSkyView carries full Chappuis ozone sky + Belt of Venus + Earth Shadow;
        // at night, AtmoSkyView yields seamlessly to higher-angular-res nightBase + liveCosmos!
        const float reflCosmGain = roughBand ? 1.05f : 1.32f;
        const float3 nightSkyFull = nightBase + liveCosmos * reflCosmGain;
        float3 upper = lerp(nightSkyFull, hSky, atmoDayW);
        // During twilight transition, preserve sharp stars/zodiacal light/aurora emerging cleanly above the ozone arch:
        upper += liveCosmos * (twilightW * (1.0f - dayW) * (roughBand ? 0.25f : 0.42f));

        // Wave 61: Twilight Belt-of-Venus & Earth-Shadow Reflection Coupling (synchronized with AtmosphereLut.hlsl):
        // Ensures puddles, wet stone, glacial ice, and forged armor reflect the true rose-magenta anti-solar arch!
        if (twilightW > 0.005f && rd.z > -0.04f) {
            const float clearTwE = twilightW * (1.0f - saturate(clouds.x - 0.38f) * 1.15f);
            const float2 rdHorE  = rd.xy * rsqrt(max(dot(rd.xy, rd.xy), 1.0e-6f));
            const float2 sunHorE = aSun.xy * rsqrt(max(dot(aSun.xy, aSun.xy), 1.0e-6f));
            const float antiAzE  = saturate(dot(rdHorE, -sunHorE));
            const float archCurv = (1.0f - antiAzE) * 0.032f;
            const float shElevE  = clamp(0.026f - aElev * 1.28f - archCurv, 0.006f, 0.25f);
            const float beltCtrE = shElevE + lerp(0.040f, 0.056f, saturate(-aElev * 5.5f + 0.40f));
            const float beltDyE  = rd.z - beltCtrE;
            const float beltSigE = (beltDyE < 0.0f) ? 0.045f : 0.082f;
            const float beltEnv  = exp(-min((beltDyE * beltDyE) / (beltSigE * beltSigE), 36.0f))
                                 * pow(max(antiAzE, 1.0e-4f), 1.40f) * smoothstep(-0.02f, 0.03f, rd.z);
            const float earthShE = (1.0f - smoothstep(shElevE - 0.030f, shElevE + 0.040f, rd.z))
                                 * pow(max(antiAzE, 1.0e-4f), 1.35f) * smoothstep(-0.04f, 0.02f, rd.z);
            const float3 venusColE = lerp(float3(0.58f, 0.21f, 0.34f), float3(0.46f, 0.18f, 0.40f), smoothstep(-0.015f, 0.085f, beltDyE));
            upper += venusColE * (beltEnv * clearTwE * (roughBand ? 0.28f : 0.36f));
            upper = lerp(upper, upper * float3(0.44f, 0.54f, 0.86f) + float3(0.008f, 0.014f, 0.036f), saturate(earthShE * clearTwE * 0.45f));
        }

        // Wave 61: Subtle Aurora Borealis Zenith Sky-Reflection Tint at Northern High Latitudes:
        // Gives clear northern nights an authentic emerald-cyan/violet zenith aurora reflection across puddles, ice & steel.
        if (nightW > 0.02f && rd.z > 0.08f) {
            const float latNorth = smoothstep(-25000.0f, 85000.0f, camPosW.y);
            const float clearAur = pow(1.0f - saturate(clouds.x * 0.95f), 1.35f);
            const float zenArch  = smoothstep(0.08f, 0.72f, rd.z) * (0.58f + 0.42f * saturate(rd.y * 0.5f + 0.5f));
            const float curtainWave = 0.65f + 0.35f * sin(rd.x * 3.8f + rd.y * 2.4f + simTime * 0.16f);
            const float3 aurZenCol = lerp(float3(0.014f, 0.068f, 0.050f), float3(0.024f, 0.044f, 0.072f), smoothstep(0.45f, 0.92f, rd.z));
            const float aurW = nightW * clearAur * lerp(0.28f, 1.0f, latNorth) * zenArch * curtainWave * (roughBand ? 0.55f : 0.78f);
            upper += min(aurZenCol * aurW, float3(0.035f, 0.085f, 0.065f));
        }

        // Daytime & twilight live 22-deg glacial ice halo + horizontal parhelia (sundogs) + Eddington solar corona
        // reflected faithfully into glacial ice, rivers, puddles, and brushed steel armor (zero night double-count!):
        if (rd.z > -0.01f && atmoDayW > 0.01f) {
            const float3 dayOptics = rawCosmosSolarLunarHaloThermal(
                upRd, aSun, gameHour, simTime, clouds.x, weather.z, surfaceTempC, dualMoon, !roughBand);
            upper += dayOptics * (atmoDayW * (roughBand ? 0.58f : 1.10f));
        }

        // Project live upgraded 64^3 Perlin-Worley convective cloud-deck onto upper hemisphere:
        if (rd.z > 0.03f && clouds.x > 0.05f) {
            const float2 cUv = frac((camPosW.xy + rd.xy * (18000.0f / max(rd.z, 0.12f)) + cloudWind.xy * (simTime * 0.28f)) / 36000.0f);
            const float2 tileUv = clamp(cUv, (0.5f / 64.0f).xx, (63.5f / 64.0f).xx) * 0.125f;
            const float cSample = CloudNoise.SampleLevel(Lin, tileUv, 0.0f).r;
            const float cCov = saturate((cSample - lerp(0.64f, 0.34f, saturate(clouds.x))) * 3.1f) * smoothstep(0.03f, 0.24f, rd.z);
            const float3 dayCloud = max(sunColour.rgb, 0.55f.xxx) * 0.90f;
            const float3 twCloud  = float3(0.95f, 0.48f, 0.32f) * (0.35f + 0.65f * saturate(dot(upRd, aSun)));
            // Nocturnal cloud reflections respond directly to waxing/full/waning dual-moon phase + northern aurora under-bounce:
            const float3 nightCloud = ambDir * dualMoon.moonTintRGB * (float3(0.55f, 0.68f, 0.88f) * moonAmbScale)
                                    + float3(0.012f, 0.042f, 0.032f) * (smoothstep(-20000.0f, 80000.0f, camPosW.y) * lerp(1.15f, 0.72f, dualMoon.totalPhase01));
            float3 cCol = lerp(nightCloud, dayCloud, dayW * 0.72f);
            cCol = lerp(cCol, twCloud, twilightW * 0.58f);
            upper = lerp(upper, cCol, cCov * (roughBand ? 0.48f : 0.74f));
        }

        // Lower hemisphere + Wave 61 Anisotropic Horizon Mountain-Silhouette Bounce Occlusion:
        // Replaces the flat horizon line with an undulating 360-deg alpine ridge silhouette & terrain/snow bounce!
        const float3 groundBounce = ambDir * lerp(float3(0.36f, 0.34f, 0.30f), float3(0.60f, 0.66f, 0.76f), saturate(weather.z))
                                  * lerp(1.0f.xxx, dualMoon.moonTintRGB * moonAmbScale, nightW * 0.65f);
        const float ridgeSin = evalHorizonMountainRidgeSin(rd.xy, camPosW.xyz);
        const float ridgeSoft = roughBand ? 0.065f : 0.028f;
        const float skyClearOfRidge = smoothstep(ridgeSin - ridgeSoft, ridgeSin + ridgeSoft, rd.z);
        const float3 ridgeSilCol = lerp(groundBounce * 0.80f, upper * float3(0.38f, 0.46f, 0.64f), 0.36f * (1.0f - nightW * 0.45f));
        const float lowerBlend = smoothstep(-0.18f, 0.01f, rd.z);
        const float3 lowerAndRidge = lerp(groundBounce, ridgeSilCol, lowerBlend);
        col = lerp(lowerAndRidge, upper, skyClearOfRidge);

        // Sun/dual-moon specular lobe (tight in sharp band, wide GGX convolution in rough band; modulated by lunar phase at night!):
        // Daytime sharp-band gain yields cleanly to the Eddington limb-darkened solar disk + Mie corona already in `upper`.
        const float cosS = saturate(dot(rd, sunDir.xyz));
        const float expS = roughBand ? 10.0f : 180.0f;
        const float gainS = roughBand ? 0.22f : lerp(2.10f, 1.15f, dayW);
        const float cAtt = 1.0f - saturate(clouds.x * 0.72f);
        const float3 specLightCol = lerp(sunColour.rgb, sunColour.rgb * dualMoon.moonTintRGB * moonSpecScale, nightW);
        col += specLightCol * (pow(cosS, expS) * gainS * cAtt * step(0.0f, rd.z));
    } else {
        // Interiors, taverns, dungeons, Dwemer ruins: high-contrast directional stone-vault gradient
        // (cool-neutral high vault ceiling vs rich dark floor bounce so metallic jugs, goblets & armor read crisp, not muddy brown!).
        const float3 ceilCol = ambDir * float3(0.88f, 0.94f, 1.06f) * lerp(0.45f, 1.35f, pow(saturate(rd.z * 0.5f + 0.5f), 1.4f));
        const float3 floorCol = ambDir * float3(0.22f, 0.19f, 0.16f);
        col = lerp(floorCol, ceilCol, smoothstep(-0.22f, 0.38f, rd.z));
    }

    // Feature 1: Live Screen-Space Scene Radiance Projection into the 256x256 Dynamic Cubemap:
    // Reflects real nearby buildings, trees, firepits, and tavern walls onto metallic armor, wet floors, and eyes.
    const float4 scEnv = screenOf(eyeAt() + rd * 2400.0f);
    if (scEnv.w > 10.0f) {
        const float2 eUv = scEnv.xy * dims.zw;
        if (all(eUv > 0.03f) && all(eUv < 0.97f)) {
            const float eEdge = smoothstep(0.03f, 0.24f, min(min(eUv.x, 1.0f - eUv.x), min(eUv.y, 1.0f - eUv.y)));
            const float eDepth = Depth.SampleLevel(Lin, eUv, 0.0f);
            if (eDepth < 0.9995f) {
                const float3 scCol = sanitizeEnv3(GameLit.SampleLevel(Lin, eUv, 0.0f).rgb);
                col = lerp(col, scCol, eEdge * (roughBand ? 0.38f : 0.62f));
            }
        }
    }

    // Feature 2: 64-Light Dynamic Point-Light Cubemap Injection (hearths, torches, braziers, forge fires, spells)
    // with boosted interior specular reflection energy so metal armor, wet floors & eyes reflect real nearby fires.
    const uint nLights = min(flags.y, 64u);
    const float lExp = roughBand ? 5.0f : 42.0f;
    const float lGain = (isExt ? 1.0f : 1.55f) * (roughBand ? 0.20f : 0.92f);
    [loop] for (uint li = 0u; li < nLights; li++) {
        const float3 toL = lightPosR[li].xyz; // camera-relative light position
        const float dist = length(toL);
        const float rad = max(lightPosR[li].w * 2.0f, 1.0f);
        if (dist >= rad || dist < 1.0e-3f) { continue; }
        const float att = pow(saturate(1.0f - dist / rad), 1.55f);
        const float cosL = saturate(dot(rd, toL / dist));
        col += sanitizeEnv3(lightColS[li].rgb) * (pow(cosL, lExp) * att * lGain);
    }

    // Project real live scene geometry (pine trees, mountains, stone arches, tavern beams, hearths)
    // onto the dynamic environment cubemap wherever the ray falls inside the camera frustum.
    const float4 sc = screenOf(rd * 4000.0f);
    if (sc.w > 1.0f && all(sc.xy >= 2.0f.xx) && all(sc.xy <= dims.xy - 3.0f)) {
        const float2 suv = sc.xy * dims.zw;
        const float sd = Depth.SampleLevel(Lin, suv, 0.0f);
        if (sd < 0.9999f) {
            const float2 edge = min(suv, 1.0f - suv);
            const float edgeW = smoothstep(0.0f, 0.08f, min(edge.x, edge.y));
            const float3 sceneCol = min(sanitizeEnv3(GameLit.SampleLevel(Lin, suv, 0.0f).rgb), 3.0f.xxx);
            col = lerp(col, sceneCol, edgeW * (roughBand ? 0.62f : 0.85f));
        }
    }
    return sanitizeEnv3(col);
}

float4 bake(float4 at : SV_Position) : SV_Target0 {
    const float2 px = at.xy;
    const bool roughBand = px.y >= 128.0f;
    const float u = px.x / 256.0f;
    const float v = (roughBand ? (px.y - 128.0f) : px.y) / 128.0f;
    const float az = (u - 0.5f) * 2.0f * PI;
    const float elev = (0.5f - v) * PI;
    const float cosE = cos(elev), sinE = sin(elev);
    const float3 rd = float3(cosE * cos(az), cosE * sin(az), sinE);

    if (!roughBand) {
        return float4(evalEnvDir(rd, false), 1.0f);
    }
    // Prefiltered rough band: 5-tap tangent-space hemisphere convolution for clean, noise-free rough IBL.
    const float3 upRef = abs(rd.z) < 0.92f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 t = normalize(cross(upRef, rd));
    const float3 b = cross(rd, t);
    float3 sum = evalEnvDir(rd, true) * 0.36f;
    sum += evalEnvDir(normalize(rd + t * 0.42f), true) * 0.16f;
    sum += evalEnvDir(normalize(rd - t * 0.42f), true) * 0.16f;
    sum += evalEnvDir(normalize(rd + b * 0.42f), true) * 0.16f;
    sum += evalEnvDir(normalize(rd - b * 0.42f), true) * 0.16f;
    return float4(sum, 1.0f);
}
