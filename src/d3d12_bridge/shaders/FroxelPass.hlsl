// FroxelPass.hlsl: True 160x90x64 3D Froxel Volumetric Participating Media Pipeline (SM 6.6+).
// Implements Wronski (SIGGRAPH 2014) / Hillaire (SIGGRAPH 2015) 3D Frustum-Aligned Voxel (Froxel) Grid
// packed into a 1280x720 RGBA16F 8x8 slice atlas (64 exponential-Z depth slices from kFroxNear=24u to kFroxFar=42,000u):
//   froxel_inject    : evaluates true 3D heterogeneous world-space alpine mist, valley/river ground fog banks,
//                      cascade + 3D cloud shadowed Hillaire sunlight, dynamically lit ambient sky multi-scattering,
//                      and all 128 clustered local lights into each (x, y, z) froxel.
//   froxel_integrate : front-to-back Beer-Lambert analytical scattering & transmittance integration along z = 0..63.
// Original work, MIT.
#ifdef FROXEL_INT
#define ATLAS_SLOT t2
#include "LightingCommon.hlsli"
Texture2D<float4> FroxelIn     : register(t0);
Texture2D<float4> FroxelBlocks : register(t1);
#elif defined(FROXEL_BLOCK)
#define ATLAS_SLOT t1
#include "LightingCommon.hlsli"
Texture2D<float4> FroxelIn     : register(t0);
#else
#define ATLAS_SLOT t0
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"
#include "RawProceduralNoise.hlsli"
#include "RawWeatherAtmo.hlsli"
Texture2D<float4> TransLut    : register(t1);
Texture2D<float4> AtmoSkyView : register(t2);
Texture2D<float4> FroxelPrev  : register(t3);
Texture2D<float>  Depth       : register(t4);
SamplerState      Lin         : register(s0);
#endif

// Wave 62: Strict finite sanitization helpers shared across all 3 froxel passes (zero NaN/Inf propagation).
float3 sanitizeFroxel3(float3 c) {
    return all(isfinite(c)) ? clamp(c, 0.0f.xxx, 16.0f.xxx) : 0.0f.xxx;
}
float4 sanitizeFroxel4(float4 v) {
    return float4(sanitizeFroxel3(v.rgb), isfinite(v.a) ? max(v.a, 0.0f) : 0.0f);
}

#if !defined(FROXEL_INT) && !defined(FROXEL_BLOCK)
float4 samplePrevFroxel(float2 uv, float z) {
    const float sF = froxelZToSlice(z);
    const uint s0 = min(uint(floor(sF)), 63u);
    const uint s1 = min(s0 + 1u, 63u);
    const float sT = frac(sF);
    const float2 tileUv = clamp(uv, float2(0.5f / 160.0f, 0.5f / 90.0f), float2(159.5f / 160.0f, 89.5f / 90.0f));
    const float2 uv0 = (float2(float(s0 & 7u), float(s0 >> 3u)) + tileUv) * 0.125f;
    const float2 uv1 = (float2(float(s1 & 7u), float(s1 >> 3u)) + tileUv) * 0.125f;
    return sanitizeFroxel4(lerp(FroxelPrev.SampleLevel(Lin, uv0, 0.0f), FroxelPrev.SampleLevel(Lin, uv1, 0.0f), sT));
}

float3 froxelTransAt(float mu) {
    const float r = 6360.0f + 0.5f;
    const float H = sqrt(6460.0f * 6460.0f - 6360.0f * 6360.0f);
    const float rho = sqrt(r * r - 6360.0f * 6360.0f);
    const float muCl = clamp(isfinite(mu) ? mu : 0.0f, -1.0f, 1.0f);
    const float d = max(0.0f, -r * muCl + sqrt(max(r * r * (muCl * muCl - 1.0f) + 6460.0f * 6460.0f, 0.0f)));
    return sanitizeFroxel3(TransLut.SampleLevel(Lin, float2(saturate((d - (6460.0f - r)) / max(rho + H - (6460.0f - r), 1.0e-4f)), saturate(rho / H)), 0.0f).rgb);
}

float3 froxelSkyAt(float3 rd) {
    const float az = (abs(rd.x) + abs(rd.y) > 1.0e-5f) ? atan2(rd.y, rd.x) : 0.0f;
    const float elev = asin(clamp(rd.z, -1.0f, 1.0f));
    const float vNonLin = 0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI)));
    const float3 c0 = sanitizeFroxel3(AtmoSkyView.SampleLevel(Lin, float2(az / (2.0f * PI) + 0.5f, saturate(vNonLin)), 0.0f).rgb);
    const float poleW = smoothstep(0.94f, 0.995f, rd.z);
    if (poleW > 0.001f) {
        const float3 zAvg = (sanitizeFroxel3(AtmoSkyView.SampleLevel(Lin, float2(0.25f, 0.99f), 0.0f).rgb) +
                             sanitizeFroxel3(AtmoSkyView.SampleLevel(Lin, float2(0.75f, 0.99f), 0.0f).rgb)) * 0.5f;
        return lerp(c0, zAvg, poleW);
    }
    return c0;
}

float froxelHG(float cosT, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f), 1.5f));
}

// Jendersie & d'Eon (SIGGRAPH 2023) Unified HG + Draine Mie Phase Function:
// Captures the sharp physical forward diffraction peak and atmospheric fog glory that single-lobe HG misses!
float froxelDrainePhase(float cosT, float g, float alpha) {
    const float g2 = g * g;
    const float denom = max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f);
    const float hg = (1.0f - g2) / (4.0f * PI * pow(denom, 1.5f));
    const float draine = (1.0f + alpha * cosT * cosT) / (1.0f + alpha * (1.0f + 2.0f * g2) / 3.0f);
    return hg * draine;
}

float4 froxel_inject(float4 at : SV_Position) : SV_Target0 {
    const bool isWorldMap = (timeEnv.w > 0.5f) && (camPosW.z > 42000.0f);
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u || isWorldMap) { return float4(0.0f, 0.0f, 0.0f, 0.0f); }
    const uint2 px = uint2(at.xy);
    const uint tileX = px.x / 160u;
    const uint tileY = px.y / 90u;
    const uint slice = tileY * 8u + tileX;   // 0..63
    const float2 uv = (float2(px.x % 160u, px.y % 90u) + 0.5f) / float2(160.0f, 90.0f);
    const float2 fullPx = uv * dims.xy;

    // Reconstruct exact 3D world-space ray & froxel center position from true camera eyeAt() (1st & 3rd person exact).
    // Convert view-space depth zMid -> Euclidean ray distance via dot(farDir, camFwd) so every 3D froxel light glow
    // and sunbeam aligns pixel-for-pixel with its 3D light source across the entire screen FOV!
    const float3 eye = eyeAt();
    const float3 camFwd = normalize(worldAt(dims.xy * 0.5f, 0.995f) - eye);
    const float3 farDir = normalize(worldAt(fullPx, 0.995f) - eye);
    const float z0 = froxelSliceZ(float(slice));
    const float z1 = froxelSliceZ(float(slice + 1u));
    // Wave 30 Plastic-Constant R2 Low-Discrepancy Slice Jitter (phi_2 = 1.32471795724, minimal 2D star-discrepancy):
    const float jit = frac(dot(float2(px.x % 160u, px.y % 90u), float2(0.7548776662f, 0.5698402910f))
                         + float(slice) * 0.3819660113f + proj.z * 0.6180339887f);
    const float zMid = lerp(z0, z1, 0.35f + 0.30f * jit);
    const float rayDist = zMid / max(dot(farDir, camFwd), 0.25f);
    const float3 pRel = eye + farDir * rayDist;
    const float3 wp = pRel + camPosW.xyz;

    // 3D heterogeneous world-space participating media density across 24u..42,000u:
    // Physically calibrated Beer-Lambert extinction (sigma) with Wave 51 altitude-stratified mountain/valley mist,
    // pause-aware 3D curl wisps, and unified cloudShadowN volumetric light-and-shadow shafts (suppressed indoors!).
    const bool isInt = (sunColour.w < 0.5f);
    const float simTime = (physWorld[0].x > 1.0e-4f) ? physWorld[0].x : camPosW.w;
    const float weatherDens = 1.0f + weather.w * 1.65f + weather.z * 1.95f + saturate(clouds.x - 0.30f) * 0.85f;
    const float3 drift = wp * 0.00032f + float3(cloudWind.xy * simTime * 0.00012f, 0.0f);
    const float curlWarp = (cNoise(drift * 1.35f + 3.1f) - 0.5f) * 0.48f;
    const float mist3D = 0.42f + 0.92f * (cNoise(drift + curlWarp) * 0.56f + cNoise(drift * 2.4f + 7.1f) * 0.29f + cNoise(drift * 5.0f - 4.3f) * 0.15f);
    // Low-lying rolling 3D ground & river-basin fog banks anchored to LIVE engine tidal water height
    // (Wave 35 fix: replaces waterA.w glint-strength bug with exact rawLiveWaterZ(engineWorld, physWorld[0])!)
    // + Daylight Video Rule VW-1: interactive actor-wake fog clearing that parts around moving bodies and heals!
    const float liveWaterZ = rawLiveWaterZ(engineWorld, physWorld[0]);
    const float aElevFog = astroSunElev();
    const float dawnDuskFog = exp(-pow((aElevFog - 0.03f) / 0.16f, 2.0f));
    const float basinH = max(wp.z - (liveWaterZ + 45.0f), 0.0f);
    float fogWakeClear = 1.0f;
    if (!isInt && rayDist < 1200.0f) {
        [unroll] for (uint fi = 0u; fi < 6u; ++fi) {
            const float4 ft = footTrail[fi];
            if (ft.w <= 0.05f) { continue; }
            const float3 fd = wp - ft.xyz;
            const float d2 = dot(fd.xy, fd.xy) + fd.z * fd.z * 0.25f;
            fogWakeClear *= (1.0f - exp(-d2 / 5600.0f) * ft.w * 0.62f);
        }
    }
    const float groundBank = isInt ? 0.0f : exp(-basinH * 0.0018f) * (0.42f + 0.58f * cNoise(wp * 0.0011f + float3(cloudWind.xy * simTime * 0.00008f, 0.0f))) * (0.52f + 0.90f * dawnDuskFog + weather.w * 0.85f) * fogWakeClear;
    // Wave 35 Lush Forest & Valley Humidity Layer (NordicCanopy / Scionti / JustHoj cool misty forest depth):
    // Pools softly 120..1,400u above local terrain/water level in wooded valleys while keeping snow peaks crisp!
    const float forestCanopyBelt = isInt ? 0.0f : exp(-max(wp.z - (camPosW.z - 220.0f), 0.0f) * 0.0014f) * smoothstep(550.0f, 3400.0f, rayDist) * (0.45f + 0.55f * cNoise(wp * 0.00062f - float3(cloudWind.xy * simTime * 0.00009f, 0.0f))) * (0.38f + 0.62f * dawnDuskFog + weather.w * 0.55f + saturate(clouds.x - 0.25f) * 0.35f) * fogWakeClear;
    // True 3D alpine ridge collar clouds hugging mountain flanks (3,800u..7,800u world altitude):
    const float peakCollar = isInt ? 0.0f : exp(-pow((wp.z - 5000.0f) / 2600.0f, 2.0f)) * smoothstep(3600.0f, 11000.0f, zMid) * (0.55f + 0.65f * cNoise(wp * 0.00022f - float3(cloudWind.xy * simTime * 0.00007f, 0.0f)));
    // Wave 51 + Wave 62 Altitude-Stratified Valley Mist & Mountain Cloud-Deck Temperature-Inversion Mist Bands:
    // Traps a crisp horizontal sea-of-clouds inversion deck in mountain valleys & below cloud-deck altitude (outdoor only!).
    float valleyStratMist = 0.0f;
    float inversionBandMist = 0.0f;
    if (!isInt) {
        const float valleyZ = liveWaterZ;
        const float invDrive = saturate(phenology1.z * 0.85f + dawnDuskFog * 0.45f + saturate(clouds.x - 0.20f) * 0.35f);
        const float scaleH = lerp(680.0f, 1180.0f, saturate(clouds.x * 0.50f + weather.w * 0.65f + dawnDuskFog * 0.45f));
        const float altExp = exp(-min(max(0.0f, wp.z - valleyZ) / max(scaleH, 100.0f), 24.0f));
        const float3 curlP = (wp - float3(cloudWind.xy * (simTime * 0.18f), simTime * 2.6f)) * float3(0.00072f, 0.00072f, 0.00145f);
        const float4 cg0 = rawNoise3Grad(curlP);
        const float3 curlV = float3(cg0.z - cg0.w, cg0.w - cg0.y, cg0.y - cg0.z);
        const float4 cg1 = rawNoise3Grad(curlP * 2.15f + curlV * 0.42f + float3(7.3f, -5.1f, 3.7f));
        const float curlWisp = lerp(0.55f, 1.48f, saturate(cg0.x * 0.60f + cg1.x * 0.40f));
        valleyStratMist = altExp * curlWisp * (0.48f + 0.72f * dawnDuskFog + 0.55f * weather.w + 0.35f * saturate(clouds.x)) * fogWakeClear;

        // Sharp temperature-inversion mist deck pooling cleanly at valley-pass altitude & mountain cloud-base collar:
        const float invCapZ = valleyZ + lerp(920.0f, 2350.0f, saturate(phenology1.z * 0.55f + clouds.x * 0.45f));
        const float invDz = wp.z - invCapZ;
        const float invSigmaZ = (invDz > 0.0f) ? 260.0f : 620.0f; // crisp capped top, softer misty underside
        const float valleyDeck = exp(-min((invDz * invDz) / (invSigmaZ * invSigmaZ), 24.0f)) * smoothstep(600.0f, 3200.0f, rayDist);
        const float cloudDeckZ = (clouds.y > 1000.0f) ? (clouds.y - 480.0f) : 5600.0f;
        const float hiDz = (wp.z - cloudDeckZ) / 950.0f;
        const float mountainDeck = exp(-min(hiDz * hiDz, 24.0f)) * saturate(clouds.x * 1.15f) * smoothstep(2200.0f, 8500.0f, rayDist);
        inversionBandMist = saturate(valleyDeck * invDrive * 0.85f + mountainDeck * 0.65f) * curlWisp * fogWakeClear;
    }
    // True world-space altitude falloff anchored to live sea/river level (100% world-locked, zero camera-height drift!):
    const float worldElevF = max(wp.z - liveWaterZ, 0.0f);
    const float valleyFogF = exp(-min(worldElevF * 0.00034f, 24.0f)) * (1.0f + 0.35f * saturate(clouds.x + weather.w));
    // Smooth near-camera exterior ramp so foreground (<650u) stays crystal crisp while valleys/forests/mountains gain full 3D depth:
    const float extNearRamp = isInt ? 1.0f : lerp(smoothstep(220.0f, 1500.0f, rayDist), 1.0f, saturate(weather.w * 0.65f + weather.z * 0.75f));
    const float heightFalloff = isInt ? 1.0f : (0.22f + 0.52f * valleyFogF + valleyStratMist * 0.68f + inversionBandMist * 0.92f + peakCollar * 1.48f + groundBank * 1.15f + forestCanopyBelt * 0.88f);
    const float baseExt = isInt ? clamp(tune.x * 0.14f, 0.000006f, 0.000016f)
                                : clamp(tune.x * 0.145f, 0.000011f, 0.000024f);
    float sigma = baseExt * weatherDens * mist3D * heightFalloff * extNearRamp;

    // Radiative-transfer equilibrium in-scattered radiance (units of linear HDR radiance; multiplied by sigma on output
    // so froxel_integrate computes exact L_scat = integral(T * sigma * inScat * ds) matching physical Beer-Lambert transport!):
    float3 inScat = 0.0f.xxx;
    if (!isInt && sunDir.w > 0.5f && tune2.z > 0.0f) {
        const float4 sc = screenOf(pRel);
        // Wave 51 Unified Cloud-Shadow Volumetric God-Ray Shafts:
        // Couples 2-tap convective cloudShadowN(wp, 2u) directly into sun volumetric in-scattering so moving 3D
        // clouds carve crisp traveling light-and-shadow shafts through mountain valley mist!
        const float csRaw = (clouds.x > 0.005f) ? cloudShadowN(wp, 2u) : 1.0f;
        const float cs = ((flags.x & FLAG_CLOUDS) != 0u) ? pow(saturate(csRaw), 1.35f) : saturate(csRaw);
        const float sh = (zMid < 12000.0f ? cascadeShadow(pRel, sc.z, false) : 1.0f) * cs;
        const float3 aSunF = astroSunDir();
        const float aElevF = astroSunElev();
        const float nightW = smoothstep(0.03f, -0.07f, aElevF);
        const float twilightW = smoothstep(-0.08f, 0.02f, aElevF) * (1.0f - smoothstep(0.08f, 0.26f, aElevF));
        const float3 beamDir = normalize(lerp(aSunF, sunDir.xyz, nightW));
        const float cosT = dot(farDir, beamDir);
        const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        // Jendersie & d'Eon (2023) Unified HG + Draine Mie phase blended with 3-octave Wrenninge/Hillaire
        // multi-scattering phase (gives creamy sunlit forest godrays and soft aerial volume without single-scatter harshness):
        const float localOd = saturate(sigma * rayDist * 0.65f);
        const float phSingle = 0.72f * froxelDrainePhase(cosT, lerp(0.74f, 0.78f, nightW), 1.45f) + 0.28f * froxelHG(cosT, -0.22f);
        const float phMulti  = rawMultiScatterPhase(cosT, lerp(0.72f, 0.76f, nightW), localOd);
        const float ph = min(lerp(phSingle, phMulti * 1.85f, 0.45f), 3.2f);
        const float3 hTrans = froxelTransAt(clamp(aElevF, -0.10f, 1.0f));
        const float htLum = max(dot(hTrans, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float3 physSun = lerp(lerp(sunColour.rgb, hTrans * (max(sunLum, 0.10f) / htLum), 0.58f), float3(0.24f, 0.36f, 0.66f) * 0.55f, nightW);
        const float3 hSky = froxelSkyAt(float3(farDir.xy, max(farDir.z, 0.01f)));
        // RDR2-grade suspended 3D golden dust motes & alpine diamond-dust ice crystals catching direct sunlit beams:
        float moteBoost = 1.0f;
        if (zMid < 1800.0f && sh > 0.25f) {
            const float3 mCoord = wp * 0.075f - float3(cloudWind.xy * simTime * 0.002f, simTime * 0.35f);
            const float mCell = cHash(floor(mCoord));
            if (mCell > 0.965f) {
                const float mCore = exp(-dot(frac(mCoord) - 0.5f, frac(mCoord) - 0.5f) * 28.0f);
                moteBoost += mCore * (1.0f - zMid / 1800.0f) * (weather.z > 0.15f ? 2.8f : 1.6f);
            }
        }
        float3 uSunFr, uSkyZFr, uSkyHFr, uAmbUpFr, uAmbDnFr;
        evalWorldAtmosphere(uSunFr, uSkyZFr, uSkyHFr, uAmbUpFr, uAmbDnFr);
        const float3 syncSunFr = lerp(physSun, uSunFr, 0.65f);
        const float overcastFr = saturate((clouds.x - 0.55f) * 1.95f + weather.w * 0.52f + weather.z * 0.45f);
        const float hLumFr = max(dot(hSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float3 skyDeSat = lerp(hSky, hLumFr.xxx * float3(0.88f, 0.94f, 1.04f), overcastFr * 0.72f);
        const float3 syncSkyFr = lerp(skyDeSat, lerp(uSkyHFr, uSkyZFr, saturate(max(farDir.z, 0.0f) * 2.2f)), 0.28f) * 0.88f;
        // Exact night horizon match so night mountain fogs stay deep indigo instead of bright grey-blue:
        const float3 ambSkyFr = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.025f.xxx);
        const float3 nightFogCol = lerp(float3(0.026f, 0.042f, 0.074f), ambSkyFr * float3(0.62f, 0.68f, 0.78f), saturate(clouds.x * 0.85f + weather.z * 0.50f) * 0.80f);
        // Disciplined twilight luminance envelope (prevents bright hazy mountain fogs during sunrise/sunset shifts!):
        const float todAmbGain = lerp(1.0f, 0.62f, twilightW) * lerp(0.46f, 0.94f, cs);
        const float3 ambInScat = lerp(syncSkyFr * todAmbGain, nightFogCol, nightW);
        // Dynamically lit 3D solar in-scattering: sunlit cloud gaps glow warm while shadowed ridges stay deep cool slate!
        const float sunInGain = tune2.z * lerp(0.38f, 0.18f, twilightW) * (1.0f - nightW * 0.78f) * (1.0f - overcastFr * 0.55f);
        inScat += ambInScat + syncSunFr * (sh * ph * sunInGain * moteBoost);

        // Wave 62: Moonlight Rayleigh + Mie Night-Halo Scattering (moonlit mist corona + 22-deg lunar ice-mist ring):
        // Strictly active at night (nightW > 0.02); carves silver-indigo lunar shafts through mountain passes & canopies!
        if (nightW > 0.02f) {
            const float3 moonDir = (dot(sunDir.xyz, sunDir.xyz) > 1.0e-6f) ? normalize(sunDir.xyz) : float3(0.0f, 0.0f, 1.0f);
            const float cosMoon = clamp(dot(farDir, moonDir), -1.0f, 1.0f);
            const float moonRayleigh = (3.0f / (16.0f * PI)) * (1.0f + cosMoon * cosMoon);
            const float moonMieHalo = min(froxelHG(cosMoon, 0.76f), 2.4f);
            const float moonAng = acos(cosMoon);
            const float moonRing22 = exp(-min(pow((moonAng - 0.384f) / 0.090f, 2.0f), 24.0f)) * saturate(0.32f + weather.z * 0.68f);
            const float moonClear = (1.0f - overcastFr * 0.68f) * smoothstep(-0.02f, 0.12f, moonDir.z);
            const float3 moonTint = float3(0.30f, 0.42f, 0.70f);
            inScat += moonTint * (nightW * moonClear * sh * tune2.z * (moonRayleigh * 0.24f + moonMieHalo * 0.14f + moonRing22 * 0.10f));
        }

        // Wave 38 Dynamic Mountain Fog, Valley Inversion Mist, Coastal Sea-Fog & Weather Curtains (RawWeatherAtmo.hlsli):
        // Energy-conserving optical-depth-weighted injection into the 160x90x64 3D froxel grid!
        const float wxTempC = lerp(8.0f, -6.0f, saturate(weather.z + smoothstep(55000.0f, 125000.0f, wp.y) * 0.65f));
        const float4 dynWx = evalDynamicWeatherFroxelSample(
            wp, farDir, beamDir, syncSunFr, ambInScat,
            weather, float4(timeEnv.x, aElevF, timeEnv.z, timeEnv.w),
            float4(cloudWind.xy, saturate(clouds.x), cloudWind.w),
            float2(liveWaterZ, wxTempC),
            simTime);
        const float dynSig = dynWx.w * (0.38f * extNearRamp);
        if (dynSig > 1.0e-7f) {
            const float3 dynInScat = dynWx.rgb / max(dynWx.w, 1.0e-6f);
            const float newSig = sigma + dynSig;
            inScat = (inScat * sigma + dynInScat * (dynSig * lerp(0.48f, 1.0f, sh))) / max(newSig, 1.0e-7f);
            sigma = newSig;
        }
        // Wave 62: Dynamic Snow-Squall Visibility Extinction (gust-driven whiteout extinction sheets during heavy snowstorms):
        // Keeps foreground <140u readable while gusting snow-squall walls organically cut mid/far alpine visibility!
        if (weather.z > 0.16f && rayDist > 110.0f) {
            const float squallAmt = smoothstep(0.16f, 0.82f, weather.z);
            const float3 sqCoord = (wp - float3(cloudWind.xy * (simTime * 0.42f), -simTime * 28.0f)) * float3(0.00095f, 0.00095f, 0.00185f);
            const float4 sqN = rawNoise3Grad(sqCoord);
            const float sqGust = smoothstep(0.34f, 0.86f, sqN.x) * smoothstep(110.0f, 750.0f, rayDist) * fogWakeClear;
            const float sqExt = squallAmt * sqGust * 0.00042f;
            if (sqExt > 1.0e-7f) {
                const float3 sqCol = lerp(ambInScat * float3(0.92f, 0.98f, 1.08f), max(ambInScat * 1.15f, syncSunFr * 0.42f), sh * (1.0f - nightW * 0.7f));
                const float totSig = sigma + sqExt;
                inScat = (inScat * sigma + sqCol * sqExt) / max(totSig, 1.0e-7f);
                sigma = totSig;
            }
        }
        // Blown Alpine Snow Spindrift Plumes (Daylight 2026-11-24 snow-smoke reference):
        // Wind-sheared ground-hugging snow wisps that curl off ridges in blizzards/high peaks and part around the player.
        const float snowForce = saturate(weather.z * 1.15f + smoothstep(3200.0f, 9500.0f, wp.z) * 0.45f);
        if (snowForce > 0.18f && zMid > 60.0f && zMid < 2600.0f) {
            const float2 sWind = cloudWind.xy * 0.042f + float2(12.0f, 5.5f);
            const float3 spCoord = float3(wp.xy - sWind * simTime, wp.z * 1.65f - simTime * 1.8f) * 0.0048f;
            const float spCurl = cNoise(spCoord) * 0.65f + cNoise(spCoord * 2.35f + 7.1f) * 0.35f;
            const float spBelt = smoothstep(-280.0f, -20.0f, pRel.z) * (1.0f - smoothstep(110.0f, 380.0f, pRel.z));
            const float spWake = smoothstep(55.0f, 220.0f, rayDist);
            const float spindrift = smoothstep(0.56f, 0.88f, spCurl) * spBelt * spWake * snowForce * (1.0f - zMid / 2600.0f);
            if (spindrift > 0.01f) {
                sigma += spindrift * 0.00058f;
                inScat += lerp(ambInScat * 1.15f, max(syncSunFr * 0.72f, ambInScat * 1.30f), sh) * (spindrift * 0.42f);
            }
        }
        // Disabled 3.81s rhythmic cold-weather breathPulse (sin(camPosW.w * 1.65)):
        // caused a periodic timed pulse across snow/glacier/ice scenes whenever coldAmt > 0.25!
    }
    // Clustered local lights in 3D froxel space (hearths, torches, braziers, 3D window portal lights, spells).
    if (tune2.y > 0.0f && flags.y > 0u) {
        const uint tx = min(uint(uv.x * TILES_X), TILES_X - 1u);
        const uint ty = min(uint(uv.y * TILES_Y), TILES_Y - 1u);
        const float sl = log(max(zMid, cluster.x) / cluster.x) * cluster.z;
        const uint cIdx = (min(uint(max(sl, 0.0f)), SLICES - 1u) * TILES_Y + ty) * TILES_X + tx;
        const uint4 bits = clusters[cIdx];
        [loop] for (uint w = 0u; w < 4u; w++) {
            uint b = bits[w];
            [loop] while (b != 0u) {
                const uint li = w * 32u + firstbitlow(b);
                b &= b - 1u;
                if (li >= flags.y) { continue; }
                const float3 toL = lightPosR[li].xyz - pRel;
                const float dL = length(toL);
                const float rL = lightPosR[li].w;
                if (dL >= rL) { continue; }
                // Soft, wide atmospheric local-light scatter (zero sharp sphere/donut halo around empty room-fill lights!):
                const float rNorm = saturate(dL / max(rL, 1.0f));
                const float softCore = 1.0f / (1.0f + pow(dL / max(rL * 0.32f, 52.0f), 2.0f));
                const float nearRamp = isInt ? smoothstep(140.0f, 420.0f, rayDist) : smoothstep(75.0f, 260.0f, rayDist);
                const float att = (1.0f - rNorm) * (1.0f - rNorm) * lerp(0.55f, 1.0f, softCore) * nearRamp;
                const float cosL = dot(farDir, toL / max(dL, 1.0e-3f));
                const float phL = min(froxelHG(cosL, isInt ? 0.34f : 0.42f) * (isInt ? 1.15f : 1.45f) + 0.22f, 1.25f);
                const float intSmoke = isInt ? (0.82f + 0.36f * cNoise(wp * 0.0085f - float3(0.0f, 0.0f, simTime * 0.14f))) : 1.0f;
                // Daylight Video Rule VL-1 ("light the weather at the lamp"): wind-slanted rain/snow flakes resolve
                // inside warm lamp cones + Quake-style subtle procedural firelight flicker (25:Z7)!
                float lampWeather = 1.0f;
                if (!isInt && (weather.w > 0.08f || weather.z > 0.08f)) {
                    const float3 wFlow = float3(cloudWind.xy * 0.025f, weather.w > weather.z ? -42.0f : -14.0f);
                    const float3 streakP = (wp - wFlow * simTime) * float3(0.065f, 0.065f, weather.w > weather.z ? 0.016f : 0.048f);
                    const float flake = smoothstep(0.58f, 0.90f, cNoise(streakP));
                    lampWeather += flake * max(weather.w, weather.z) * 0.95f;
                }
                const bool isWarmFire = (lightColS[li].r > lightColS[li].b * 1.35f);
                const float fireFlicker = isWarmFire
                    ? (0.94f + 0.06f * sin(simTime * 7.3f + float(li) * 2.1f) * cos(simTime * 11.7f + float(li) * 1.3f))
                    : 1.0f;
                // Wave 30 Thermal Hearth-Melt Rising Steam Plumes: when cold snow/alpine air meets a warm campfire or
                // brazier, drifting condensation wisps curl upward inside the light cone (AB124 + VL-1 weather-light coupling):
                float steamBoost = 1.0f;
                if (!isInt && isWarmFire && (weather.z > 0.12f || wp.z > 2400.0f) && dL < rL * 0.52f && pRel.z > lightPosR[li].z - 12.0f) {
                    const float hSteam = saturate((pRel.z - lightPosR[li].z + 12.0f) / 140.0f);
                    const float3 sCoord = wp * 0.022f - float3(cloudWind.xy * simTime * 0.002f, simTime * 0.42f);
                    const float sWisp = smoothstep(0.45f, 0.88f, cNoise(sCoord) * 0.65f + cNoise(sCoord * 2.2f + 5.1f) * 0.35f);
                    const float coldS = saturate(weather.z * 1.25f + smoothstep(2400.0f, 4800.0f, wp.z) * 0.65f);
                    steamBoost += sWisp * (1.0f - hSteam) * smoothstep(0.0f, 0.18f, hSteam) * coldS * 1.15f;
                }
                // Wave 35 GTA VI (@redorav) 4-Step Shadowed 3D Local-Light Froxels:
                // Marches 4 depth-buffer shadow probes from froxel center toward the light so indoor torches/braziers
                // carve crisp volumetric shafts through doorways & pillars instead of bleeding milky fog through stone walls!
                float localVis = 1.0f;
                if (dL > 32.0f && rayDist < 4800.0f) {
                    float occF = 1.0f;
                    [unroll] for (int si = 1; si <= 4; ++si) {
                        const float tS = (float(si) - 0.35f) * 0.22f;
                        const float4 sm = screenOf(pRel + toL * tS);
                        if (sm.w > 1.0f && all(sm.xy >= 0.0f) && sm.x < dims.x && sm.y < dims.y) {
                            const float zHit = viewZOf(Depth.Load(int3(sm.xy, 0)));
                            if (zHit < sm.w * 0.94f && (sm.w - zHit) < max(sm.w * 0.38f, 180.0f)) {
                                occF *= 0.28f;
                            }
                        }
                    }
                    localVis = max(occF, 0.08f);
                }
                // Never conjure artificial spherical fog blobs around empty interior room-fill lights;
                // only boost exterior atmospheric density gently when weather/steam is actually present.
                if (!isInt && (lampWeather > 1.02f || steamBoost > 1.02f)) {
                    sigma = max(sigma, 0.000014f * att * lampWeather * steamBoost * lerp(0.35f, 1.0f, localVis));
                }
                const float fillRestraint = isInt ? (isWarmFire ? 0.022f : 0.011f) : (isWarmFire ? 0.16f : 0.10f);
                inScat += min(lightColS[li].rgb * (att * phL * localVis * tune2.y * intSmoke * lampWeather * steamBoost * fireFlicker * fillRestraint), (isInt ? 0.018f : 0.20f).xxx);
            }
        }
    }
    float4 curVoxel = sanitizeFroxel4(float4(inScat * sigma, sigma));
    // Hillaire (SIGGRAPH 2015) Temporal 3D Froxel Reprojection: reprojects 3D froxel center through prevWorldToClip
    // into last frame's 160x90x64 volume for silky, flicker-free 3D participating media (5x effective sample density).
    const float4 pClip = float4(dot(prevWorldToClip[0], float4(pRel, 1.0f)),
                                dot(prevWorldToClip[1], float4(pRel, 1.0f)),
                                dot(prevWorldToClip[2], float4(pRel, 1.0f)),
                                dot(prevWorldToClip[3], float4(pRel, 1.0f)));
    if (pClip.w > 1.0f && proj.z > 0.5f && resScale.x > 1.0e-4f && resScale.y > 1.0e-4f) {
        const float2 pUv = float2(pClip.x / pClip.w * 0.5f + 0.5f, 0.5f - pClip.y / pClip.w * 0.5f) / resScale.xy;
        if (all(pUv > 0.005f) && all(pUv < 0.995f)) {
            const float4 prevVoxel = samplePrevFroxel(pUv, pClip.w);
            // Motion- and depth-adaptive spatiotemporal blend: silky 0.22 when still, responsive 0.72 during fast pans/sprinting.
            const float motMag = saturate(length(pUv - uv) * 12.0f + abs(pClip.w - zMid) / max(zMid, 1.0f) * 3.0f);
            curVoxel = lerp(prevVoxel, curVoxel, lerp(0.22f, 0.72f, motMag));
        }
    }
    return sanitizeFroxel4(curVoxel);
}
#elif defined(FROXEL_BLOCK)
// Pass 1 of Blelloch Associative Monoid Scan (640x180 = 4x2 tiles of 160x90, 8 blocks x 8 slices = 64 slices):
// Integrates 8 consecutive slices per block into (S_block, T_block) using exact Beer-Lambert transport.
float4 froxel_block_scan(float4 at : SV_Position) : SV_Target0 {
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }
    const uint2 px = uint2(at.xy);
    const uint blk = (px.y / 90u) * 4u + (px.x / 160u);   // 0..7
    const uint lx = px.x % 160u;
    const uint ly = px.y % 90u;
    const uint sStart = blk * 8u;

    float3 accScat = 0.0f.xxx;
    float  accTrans = 1.0f;
    [unroll] for (uint r = 0u; r < 8u; r++) {
        const uint s = sStart + r;
        const uint sx = (s & 7u) * 160u + lx;
        const uint sy = (s >> 3u) * 90u + ly;
        const float4 voxelRaw = FroxelIn.Load(int3(sx, sy, 0));
        const float4 voxel = (any(isnan(voxelRaw)) || any(isinf(voxelRaw))) ? 0.0f.xxxx : max(voxelRaw, 0.0f.xxxx);
        const float dz = max(froxelSliceZ(float(s + 1u)) - froxelSliceZ(float(s)), 1.0f);
        const float ext = max(voxel.a, 0.0f);
        const float stepT = exp(-ext * dz);
        const float3 stepS = ext > 1.0e-6f ? (voxel.rgb * ((1.0f - stepT) / ext)) : (voxel.rgb * dz);
        accScat += accTrans * stepS;
        accTrans *= stepT;
    }
    return float4(min(accScat, 3.5f.xxx), accTrans);
}
#else
// Pass 2 of Blelloch Associative Monoid Scan (1280x720):
// Composes preceding 8-slice blocks (0..blk-1) via exact Beer-Lambert monoid (S_a + T_a * S_b, T_a * T_b)
// and integrates only the remaining 1..8 slices inside the target block (4.33x fewer texture loads than O(N^2)!).
float4 froxel_integrate(float4 at : SV_Position) : SV_Target0 {
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }
    const uint2 px = uint2(at.xy);
    const uint targetSlice = (px.y / 90u) * 8u + (px.x / 160u);
    const uint lx = px.x % 160u;
    const uint ly = px.y % 90u;
    const uint targetBlk = targetSlice >> 3u;
    const uint sStart = targetBlk * 8u;

    float3 accScat = 0.0f.xxx;
    float  accTrans = 1.0f;
    [loop] for (uint b = 0u; b < targetBlk; b++) {
        const uint bx = (b & 3u) * 160u + lx;
        const uint by = (b >> 2u) * 90u + ly;
        const float4 blk = FroxelBlocks.Load(int3(bx, by, 0));
        accScat += accTrans * blk.rgb;
        accTrans *= blk.a;
        if (accTrans < 0.02f) { return float4(min(accScat, 3.5f.xxx), accTrans); }
    }
    [loop] for (uint s = sStart; s <= targetSlice; s++) {
        const uint sx = (s & 7u) * 160u + lx;
        const uint sy = (s >> 3u) * 90u + ly;
        const float4 voxelRaw = FroxelIn.Load(int3(sx, sy, 0));
        const float4 voxel = (any(isnan(voxelRaw)) || any(isinf(voxelRaw))) ? 0.0f.xxxx : max(voxelRaw, 0.0f.xxxx);
        const float dz = max(froxelSliceZ(float(s + 1u)) - froxelSliceZ(float(s)), 1.0f);
        const float ext = max(voxel.a, 0.0f);
        const float stepT = exp(-ext * dz);
        const float3 stepS = ext > 1.0e-6f ? (voxel.rgb * ((1.0f - stepT) / ext)) : (voxel.rgb * dz);
        accScat += accTrans * stepS;
        accTrans *= stepT;
        if (accTrans < 0.02f) { break; }
    }
    return float4(min(accScat, 3.5f.xxx), accTrans);
}
#endif
