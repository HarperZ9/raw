// Clouds.hlsl: volumetric clouds for the sky (SKY-03), at a quarter of the screen's width and height
// (LightingPass.cpp). Each pixel marches its view ray through the layer (CloudCommon.hlsli) in 40 jittered
// steps. At each step the sun's light reaches the sample through 4 steps toward the sun (Beer's law with
// the powder term for the dark edges facing it), scattered by two Henyey-Greenstein lobes (the silver
// lining toward the sun and a softer back lobe); the sky's light comes from the game's ambient for an
// upward face, darker low in the cloud. Far clouds fade into the haze. Out: rgb the light the clouds send
// to the eye, a what is left of the sky behind them. The result blends with last frame's, found by the
// ray's direction (camera turns keep it; the clouds are far). Lighting.hlsl composites it on sky pixels.
// Original work, MIT.
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"
#include "RawProceduralNoise.hlsli"
#include "RawCosmosSky.hlsli"

Texture2D<float4> History    : register(t0);
Texture2D<float4> TransLut   : register(t1);
Texture2D<float4> CloudNoise : register(t2);
SamplerState      Lin        : register(s0);

// Hardware-trilinear lookup into the precomputed 64x64x64 (512x512 8x8 slice atlas) Periodic 3D Perlin-Worley Volume.
float4 sampleCloudVol(float3 uvw) {
    const float3 p = frac(uvw);
    const float zF = p.z * 64.0f;
    const uint z0 = uint(floor(zF)) & 63u;
    const uint z1 = (z0 + 1u) & 63u;
    const float zT = frac(zF);
    // Clamp inside tile [0.5/64 .. 63.5/64] so adjacent Z slices never bleed across 64x64 tile edges.
    const float2 xy = clamp(p.xy, (0.5f / 64.0f).xx, (63.5f / 64.0f).xx);
    const float2 uv0 = (float2(float(z0 & 7u), float(z0 >> 3u)) + xy) * 0.125f;
    const float2 uv1 = (float2(float(z1 & 7u), float(z1 >> 3u)) + xy) * 0.125f;
    return lerp(CloudNoise.SampleLevel(Lin, uv0, 0.0f), CloudNoise.SampleLevel(Lin, uv1, 0.0f), zT);
}

// Physical Ekman-Spiral Atmospheric Wind Profile: wind speed accelerates & veers clockwise with altitude
// (surface friction at base -> +32 deg mid-troposphere shear -> +54 deg upper jet stream),
// keeping v'(t) == 0 so every altitude layer glides smoothly without back-and-forth oscillation!
float2 ekmanWind(float altNorm) {
    const float ang = altNorm * 0.94f;   // 0..+54 deg clockwise Ekman veer with altitude
    const float ca = cos(ang), sa = sin(ang);
    const float2 w = cloudWind.xy;
    const float2 rotW = float2(w.x * ca - w.y * sa, w.x * sa + w.y * ca);
    return rotW * lerp(0.72f, 2.65f, altNorm);
}

// Fast 3-tap forward-difference divergence-free 2D curl for fluid cloud marbling & metamorphosis (25% fewer ALU ops).
float2 cloudCurl2D(float2 p, float t) {
    const float e = 0.08f;
    const float n0 = cNoise(float3(p.x, p.y, t));
    const float nX = cNoise(float3(p.x + e, p.y, t));
    const float nY = cNoise(float3(p.x, p.y + e, t));
    return float2((nY - n0) / e, -(nX - n0) / e);
}

// Nubis Evolved (Schneider 2022, SKY-02) True 3D Volumetric Cumulus Field with Ekman-Spiral Altitude Shear
// & Curl-Turbulence Fluid Marbling: clouds billow, shear downwind at anvil tops, marble & metamorphose organically.
float cloudDensityVol(float3 pa, bool detail) {
    // Exact 3D spherical planetary shell altitude matching CloudCommon.hlsli::cloudSpan (R = 3,600,000):
    // wraps clouds organically all the way down to the horizon with ZERO ceiling slice or horizon cutoff!
    const float R = 3600000.0f;
    const float2 dxy = pa.xy - camPosW.xy;
    const float alt = length(float3(dxy.x, dxy.y, R + pa.z)) - R;
    const float h = (alt - clouds.y) / clouds.z;
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    // Wave 46 Pause-Aware Bulk Wind Advection + Shared Convective Weather Field (1:1 synced with LightingCommon::cloudShadowN!):
    const float simTimeV = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
    const float2 wBulk = cloudWind.xy;
    const float2 wUnit = normalize(wBulk + float2(1.0e-3f, 0.0f));
    const float3 wind = float3(wBulk * (simTimeV * 0.28f), 0.0f);
    const float3 sheared = pa + wind + float3(wUnit, 0.0f) * (h * h * 650.0f);
    const float cover = saturate(clouds.x);
    const float stormType = saturate(weather.w * 1.4f + max(cover - 0.78f, 0.0f) * 2.5f);

    // Unified 2D continental convective weather map (matches cloudShadowN 1:1!) + fine detail curl-vortex modulation:
    const float tMorph = simTimeV * 0.0022f;
    const float2 wUv = sheared.xy / 46000.0f;
    const float2 curl1 = detail ? (cloudCurl2D(wUv * 1.35f, tMorph * 0.65f) * 0.14f) : 0.0f.xx;
    const float2 curl2 = detail ? (cloudCurl2D(wUv * 2.85f + curl1 * 0.55f, tMorph * 1.15f) * 0.06f) : 0.0f.xx;
    const float2 curlW = clamp(curl1 + curl2, -0.22f.xx, 0.22f.xx);
    const float wCov = rawCloudConvectiveField(pa.xy + curlW * 12000.0f, simTimeV);
    const float wConv = saturate(cNoise(float3(wUv * 1.55f + 19.3f + curlW * 0.5f, 5.1f + tMorph * 0.5f)) * 0.72f + stormType * 0.45f);
    const float topCap = lerp(0.46f, 0.97f, wConv);
    // Flat Lifting Condensation Level (LCL) base (0..0.062) + billowing convective thermal towers + cumulonimbus anvil spread:
    const float baseGrad = smoothstep(0.0f, 0.062f, h);
    const float towerBulge = 1.0f + smoothstep(0.14f, 0.52f, h) * (1.0f - smoothstep(0.58f, topCap, h)) * (0.22f * wConv);
    const float topGrad  = 1.0f - smoothstep(topCap * 0.44f, topCap, h);
    const float anvilSpread = stormType * smoothstep(0.68f, 0.88f, h) * (1.0f - smoothstep(0.89f, 0.99f, h)) * 0.28f;
    const float virgaCurtain = stormType * (1.0f - smoothstep(0.015f, 0.16f, h)) * smoothstep(0.54f, 0.82f, wCov) * 0.24f;
    const float hGrad = saturate(baseGrad * topGrad * towerBulge + anvilSpread + virgaCurtain);
    if (hGrad <= 0.001f) { return 0.0f; }

    // Sculptural 64^3 Periodic 3D Perlin-Worley Cumulus (convective 26k towers + wind-sheared tops):
    const float tEvo = camPosW.w * 0.0030f;
    const float3 baseUvw = float3(sheared.x / 26000.0f, sheared.y / 26000.0f, (alt - clouds.y) / 16000.0f - tEvo * 0.12f);
    const float4 vol0 = sampleCloudVol(baseUvw);
    const float lowWorley = vol0.g * 0.625f + vol0.b * 0.25f + vol0.a * 0.125f;
    const float baseShape = cRemap(vol0.r, -(1.0f - lowWorley) * 0.62f, 1.0f, 0.0f, 1.0f);
    const float localCov = saturate(cover * lerp(0.40f, 1.32f, smoothstep(0.24f, 0.78f, wCov)));
    const float thresh = lerp(0.62f, lerp(0.40f, 0.32f, stormType), localCov);
    float dens = saturate((baseShape * hGrad - thresh) / max(1.0f - thresh, 0.20f));
    dens *= lerp(0.70f, 1.68f, smoothstep(0.035f, 0.38f, h));
    if (detail && dens > 0.002f) {
        const float3 detUvw = float3((sheared.x + wind.x * 0.25f) / 5200.0f, (sheared.y + wind.y * 0.25f) / 5200.0f, (alt - clouds.y) / 4800.0f + tEvo * 0.22f);
        const float4 volD = sampleCloudVol(detUvw);
        const float hiWorley = volD.g * 0.625f + volD.b * 0.25f + volD.a * 0.125f;
        const float detailMod = lerp(hiWorley, 1.0f - hiWorley, saturate(h * 1.45f));
        // Wave 38 Meticulous 3-Octave Analytical 3D Curl-Warped Billow/Worley Erosion via rawNoise3Grad:
        // Carves photographic cauliflower turrets & crisp turbulent wisps well beyond 64^3 LUT resolution!
        const float3 aP1 = float3((sheared.x + wind.x * 0.22f) / 2200.0f,
                                  (sheared.y + wind.y * 0.22f) / 2200.0f,
                                  (alt - clouds.y) / 1950.0f + tEvo * 0.32f);
        const float4 ag1 = rawNoise3Grad(aP1);
        const float3 curlE = float3(ag1.z - ag1.w, ag1.w - ag1.y, ag1.y - ag1.z) * 0.30f;
        const float4 ag2 = rawNoise3Grad(aP1 * 2.35f + curlE + float3(7.3f, -5.1f, 3.7f));
        const float4 ag3 = rawNoise3Grad(aP1 * 5.10f - curlE * 0.65f + float3(-11.4f, 9.2f, 6.1f));
        const float b1 = 1.0f - abs(ag1.x * 2.0f - 1.0f);
        const float b2 = 1.0f - abs(ag2.x * 2.0f - 1.0f);
        const float b3 = 1.0f - abs(ag3.x * 2.0f - 1.0f);
        const float analBillow = saturate(b1 * b1 * 0.52f + b2 * b2 * 0.32f + b3 * b3 * 0.16f);
        const float combErode = lerp(detailMod, 1.0f - analBillow, 0.52f);
        const float erode = combErode * lerp(0.54f, 0.86f, h) * (1.0f - dens * 0.22f);
        dens = cRemap(dens, erode, 1.0f, 0.0f, 1.0f);
        // Sculpted cauliflower cumulus wisp edge sharpen (eliminates blurry 64^3 blobs!):
        dens = saturate(smoothstep(0.045f, 0.76f, dens) * 1.08f);
    }
    return dens * cloudWind.z;
}

float3 sampleCloudSunTrans(float mu) {
    const float r = 6360.0f + 4.5f;   // 4.5 km Nordic cumulus deck altitude
    const float H = sqrt(6460.0f * 6460.0f - 6360.0f * 6360.0f);
    const float rho = sqrt(r * r - 6360.0f * 6360.0f);
    const float d = max(0.0f, -r * mu + sqrt(max(r * r * (mu * mu - 1.0f) + 6460.0f * 6460.0f, 0.0f)));
    const float2 uv = float2(saturate((d - (6460.0f - r)) / max(rho + H - (6460.0f - r), 1.0e-4f)), saturate(rho / H));
    return TransLut.SampleLevel(Lin, uv, 0.0f).rgb;
}

float hg(float c, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * c, 1.0e-4f), 1.5f));
}

// Wave 38: High-Frequency Interleaved Gradient Noise + Golden-Ratio Temporal Offset.
// Eliminates the diagonal polka-dot halftone grid created by static linear grating dot(lo, (0.7548, 0.5698))!
float cloudIGN(float2 px, uint frameIdx) {
    const float baseIgn = frac(52.9829189f * frac(dot(px, float2(0.06711056f, 0.00583715f))));
    return frac(baseIgn + 0.61803399f * float(frameIdx & 63u));
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const float2 lo = floor(at.xy);
    const float2 full = lo + 0.5f;   // Full native-resolution volumetric clouds (4x sharper than 1/2-res!)
    const float3 far = worldAt(full - 0.5f, 0.995f) - eyeAt();
    const float3 dir = normalize(far);
    // March clouds all the way to the physical horizon (eliminates the 10-deg empty cutoff band above mountains!):
    if (WaveActiveAllTrue(dir.z <= -0.005f)) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }
    float4 res = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float t0, t1;
    const float3 o = camPosW.xyz;
    const float maxDist = 650000.0f;
    if (cloudsOn() && cloudSpan(o, dir, maxDist, t0, t1)) {
        // Wave 38: Meticulous 64+ adaptive volumetric steps with empty-air coarse skip, backstep, and fine in-cloud rim sub-stepping:
        const float maxSpan = lerp(165000.0f, 105000.0f, smoothstep(0.02f, 0.32f, dir.z));
        t1 = min(t1, t0 + maxSpan);
        const uint steps = max(lpCloudSteps(), 64u);
        const float dtFine = (t1 - t0) / float(steps);
        const float dtCoarse = dtFine * 2.0f;
        const float j = cloudIGN(lo, uint(proj.z));
        const float3 aSun = astroSunDir();
        const float aElev = astroSunElev();
        const float nightW = smoothstep(0.04f, -0.07f, aElev);
        // Use true astronomical sun by day/twilight (under-lighting clouds cleanly at sunrise/sunset!) and Moon (sunDir) at night.
        const float3 cloudLightDir = normalize(lerp(aSun, sunDir.xyz, nightW));
        const float cosT = dot(dir, cloudLightDir);
        // Dual-lobe Henyey-Greenstein Mie forward/back scatter (g1 = 0.78 silver lining, g2 = -0.28 back-scatter):
        const float phase = min(lerp(hg(cosT, -0.28f), hg(cosT, 0.78f), 0.56f), 0.32f);
        const float3 rawSunC = sunColour.rgb * (sunDir.w > 0.5f ? 1.0f : 0.0f);
        const float sLum = dot(rawSunC, float3(0.2126f, 0.7152f, 0.0722f));
        // Couple daytime/twilight cloud sunlight directly to Hillaire 2020 256x64 Transmittance LUT using TRUE aElev:
        // crisp silver-white at noon (own-43/44), fiery coral-peach-gold underlit cloud bellies at sunrise/sunset (own-13..15/27).
        const float3 hTrans = sampleCloudSunTrans(clamp(aElev, -0.10f, 1.0f));
        const float htLum = max(dot(hTrans, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float twiCloud = smoothstep(-0.10f, 0.00f, aElev) * (1.0f - smoothstep(0.06f, 0.26f, aElev)) * (1.0f - saturate(clouds.x * 0.85f - 0.25f));
        const float3 twiCoral = float3(1.25f, 0.54f, 0.28f) * max(sLum, 0.45f);
        const float3 physDaySun = lerp(lerp(rawSunC, hTrans * (max(sLum, 0.14f) / htLum), 0.65f), twiCoral, twiCloud * 0.48f);
        // Unified World-Space Atmosphere sync + Wave 43 Orbital Dual-Moon Phase Illumination (Masser 24d & Secunda 16d):
        // Dynamically shifts night clouds between luminous rose/silver-cyan full-moon silver linings, half-moon
        // alpine clarity, and deep brooding new-moon silhouettes against blazing auroras & Milky Way!
        float3 uSunC, uSkyZ, uSkyH, uAmbUp, uAmbDn;
        evalWorldAtmosphere(uSunC, uSkyZ, uSkyH, uAmbUp, uAmbDn);
        const float simTimeC = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
        const float seasonProgC = rawCosmosSeasonProgress01(phenology0, phenology1, physWorld[1].w, waterB.w, simTimeC);
        const DualMoonLight dualMoon = evalDualMoonPhaseIllum(seasonProgC, timeEnv.x);
        const float moonPhaseGain = lerp(0.18f, 1.42f, pow(dualMoon.totalPhase01, 1.15f));
        const float3 nightMoonSun = uSunC * dualMoon.moonTintRGB * moonPhaseGain;
        const float3 sunC = lerp(lerp(uSunC, physDaySun, 0.25f), nightMoonSun, nightW);
        const float3 skyC = lerp(uSkyH, uSkyZ, saturate(dir.z * 1.4f)) * lerp(1.0f.xxx, dualMoon.moonTintRGB * lerp(0.52f, 1.28f, dualMoon.totalPhase01), nightW);
        const float sigma = kCloudSigma;
        float T = 1.0f;
        float3 S = 0.0f;
        // Wave 30 Malfatti-Goldberg Asymmetric Corner-Packed Sun Probes (Goldberg 1967 / Sandberg):
        // Packs tight high-importance probe energy near the outer sunlit cumulus shell (where 70% of silver-lining
        // powder contrast forms) while spanning the full 44,000u anvil core in 4 steps!
        static const float kSunD[4] = { 260.0f, 1180.0f, 4450.0f, 13200.0f };
        static const float kSunW[4] = { 260.0f,  920.0f, 3270.0f,  8750.0f };
        // Wave 30 Nocturnal Settlement & Hearth Upward Skyglow Dome (AB704 GTA VI / AB304 sodium-amber night law):
        // Aggregates live nearby warm point lights (town braziers, torches, dragon fire) so clouds over cities glow
        // softly amber from BELOW while wild northern tundra stays dark Kiruna-calibrated indigo!
        float3 nightSkyglow = 0.0f.xxx;
        if (nightW > 0.05f && flags.y > 0u) {
            const uint nSkyL = min(flags.y, 12u);
            [loop] for (uint sli = 0u; sli < nSkyL; ++sli) {
                const float3 lcS = lightColS[sli].rgb;
                const float rS = lightPosR[sli].w;
                if (rS < 120.0f || dot(lcS, float3(0.2126f, 0.7152f, 0.0722f)) < 0.08f) { continue; }
                nightSkyglow += min(lcS, 1.5f.xxx) * saturate(rS / 650.0f);
            }
            nightSkyglow = min(nightSkyglow * 0.022f, float3(0.18f, 0.11f, 0.055f)) * nightW;
        }
        // Adaptive 64+-step raymarch: coarse step in empty air, automatic backstep on cloud entry, and fine 0.65*dtFine rim sub-step!
        float t = t0 + j * dtFine;
        uint emptyStreak = 0u;
        bool inCoarse = false;
        const uint maxIters = max(steps + 24u, 88u);
        [loop] for (uint i = 0u; i < maxIters && t < t1 && T > 0.01f; i++) {
            float3 pa = o + dir * t;
            float coarse = cloudDensityVol(pa, false);
            if (coarse <= 0.01f) {
                emptyStreak++;
                if (emptyStreak >= 2u) {
                    t += dtCoarse;
                    inCoarse = true;
                } else {
                    t += dtFine;
                    inCoarse = false;
                }
                continue;
            }
            if (inCoarse) {
                // Step back half a coarse interval on cloud entry so we never clip a sunlit cauliflower edge:
                t = max(t0 + j * dtFine, t - dtFine);
                pa = o + dir * t;
                inCoarse = false;
            }
            emptyStreak = 0u;
            const float dtStep = (coarse < 0.24f) ? (dtFine * 0.72f) : dtFine;
            t += dtStep;
            const float dens = cloudDensityVol(pa, true);
            if (dens <= 0.001f) { continue; }
            float od = 0.0f;
            [unroll] for (uint k = 0u; k < 4u; k++) { od += cloudDensityVol(pa + cloudLightDir * kSunD[k], false) * kSunW[k]; }
            // 6-Octave Physical Multiple-Scattering Integral with Dual-Lobe Mie HG (g1=0.78 forward, g2=-0.28 back)
            // + Physical Beer-Powder Sugar-Edge Law (2 * exp(-od) * (1 - exp(-2 * od))) for crisp dark convective undersides & silver linings!
            const float odS = od * (sigma * 0.64f);
            const float3 odRGB = odS * float3(0.97f, 1.00f, 1.04f);
            float3 multiScat = 0.0f.xxx;
            float aOct = 1.0f, bOct = 1.0f, cOct = 1.0f;
            [unroll] for (int oct = 0; oct < 6; oct++) {
                const float phOct = lerp(hg(cosT, -0.28f * cOct), hg(cosT, 0.78f * cOct), 0.56f);
                multiScat += exp(-odRGB * bOct) * (aOct * min(phOct, 0.42f));
                aOct *= 0.56f; bOct *= 0.48f; cOct *= 0.54f;
            }
            multiScat = min(multiScat, 1.25f.xxx);
            const float beerPowder = 2.0f * exp(-odS * 0.85f) * (1.0f - exp(-2.0f * (odS + dens * 0.42f)));
            const float powder = saturate(lerp(1.0f - exp(-od * sigma * 1.45f - dens * 0.65f), beerPowder, 0.58f));
            const float h = saturate((pa.z - clouds.y) / clouds.z);
            const float stormW = saturate(weather.w * 1.25f + max(clouds.x - 0.72f, 0.0f) * 2.2f);
            // Deep 3D ambient occlusion inside convective LCL cloud bellies (crisp dark undersides vs luminous tops):
            const float bellyFloor = lerp(lerp(0.24f, 0.13f, stormW), 0.12f, nightW);
            const float cloudAo = lerp(bellyFloor, 1.00f, pow(h, lerp(0.88f, 1.05f, nightW))) * exp(-dens * 0.26f);
            const float lowSun = saturate(1.0f - max(aElev, 0.0f) * 3.4f) * (1.0f - nightW);
            const float overcastW = saturate((clouds.x - 0.50f) * 2.2f);
            // Cluster B (AA425 / AA255 thunderheads lit from inside): continuous gridless 3D volumetric sheet-lightning
            // (single radial bolt origin per flash slot = 100% organic radial cloud illumination with ZERO square cell edges!).
            float3 stormFlash = 0.0f.xxx;
            if (clouds.x > 0.66f && (weather.w > 0.18f || timeEnv.z > 0.05f)) {
                const float stormInt = max(timeEnv.z * 1.45f, saturate((weather.w - 0.18f) * 1.35f) * saturate((clouds.x - 0.62f) * 2.8f));
                const float tSlot = floor(camPosW.w * 0.62f);
                const float tFrac = frac(camPosW.w * 0.62f);
                const float dblPulse = exp(-tFrac * 16.0f) + 0.55f * exp(-abs(tFrac - 0.14f) * 28.0f);
                const float trigger = max(step(0.84f, cHash(float3(tSlot, 3.7f, 11.3f))), smoothstep(0.15f, 0.65f, timeEnv.z));
                const float pulse = max(dblPulse * trigger, timeEnv.z * 0.85f);
                if (pulse > 0.01f) {
                    const float boltAng = cHash(float3(tSlot, 17.1f, 5.3f)) * 6.2831853f;
                    const float boltDist = 24000.0f + cHash(float3(tSlot, 29.7f, 8.9f)) * 75000.0f;
                    const float2 boltPos = camPosW.xy + float2(cos(boltAng), sin(boltAng)) * boltDist;
                    const float distBolt = length(pa.xy - boltPos) / 28000.0f;
                    const float glowVol = exp(-distBolt * distBolt * 2.2f);
                    const float3 boltCol = lerp(float3(0.55f, 0.72f, 1.35f), float3(0.95f, 0.96f, 1.45f), exp(-distBolt * 3.2f));
                    stormFlash = boltCol * (pulse * glowVol * (0.32f + dens * 2.2f) * stormInt * 2.35f);
                }
            }
            const float3 whiteSun = lerp(sunC, dot(sunC, float3(0.2126f, 0.7152f, 0.0722f)).xxx * float3(1.00f, 1.00f, 1.00f), 0.74f * (1.0f - lowSun) * (1.0f - nightW));
            // Upper-to-lower cloud deck radiative coupling: high stratus/altocumulus modulates sunlight reaching lower cumulus
            // so lower cloud puffs sit in deep cool indigo shadow while sunlit rims blaze warm gold (photos 5/7/13/14/15/18)!
            const float upperDeckOcc = (clouds.x > 0.25f)
                ? (1.0f - smoothstep(0.54f, 0.86f, cNoise(float3((pa.xy + cloudLightDir.xy * 18000.0f) * 0.000022f, 4.3f + camPosW.w * 0.002f))) * saturate(clouds.x) * 0.42f)
                : 1.0f;
            // Wave 44 Golden-Hour Alpenglow vs Cool Slate-Violet Cumulus Bellies (sunElev in [-0.06..0.22]):
            // Sunlit rims & cloud undersides catch rich golden-amber -> rose-crimson alpenglow forward scatter
            // while deep optically-thick cumulus bases remain authentically cool slate-violet!
            const float goldenW = smoothstep(-0.06f, 0.02f, aElev) * (1.0f - smoothstep(0.14f, 0.22f, aElev)) * (1.0f - nightW);
            const float deepSunsetW = smoothstep(-0.08f, -0.01f, aElev) * (1.0f - smoothstep(0.05f, 0.14f, aElev));
            const float3 baseSlateViolet = float3(0.16f, 0.17f, 0.30f) * lerp(0.55f, 1.15f, cloudAo);
            float3 coolBase = lerp(lerp(uSkyZ * 0.54f, float3(0.18f, 0.26f, 0.40f), overcastW * 0.52f),
                                   lerp(uSkyH * 0.68f, float3(0.32f, 0.40f, 0.52f), overcastW * 0.52f),
                                   overcastW * 0.58f + (1.0f - h) * 0.42f) * cloudAo;
            const float bellyCoolMask = saturate((1.0f - h) * 1.15f) * (1.0f - exp(-od * sigma * 0.65f - dens * 0.35f));
            coolBase = lerp(coolBase, baseSlateViolet, goldenW * bellyCoolMask * (1.0f - overcastW * 0.55f) * 0.62f);
            coolBase *= lerp(1.0f.xxx, dualMoon.moonTintRGB * lerp(0.42f, 1.32f, dualMoon.totalPhase01), nightW);

            // Golden-amber -> rose-crimson alpenglow rim & underside forward scattering at dawn/sunset:
            const float3 alpenglowCol = lerp(float3(1.56f, 0.78f, 0.24f), float3(1.52f, 0.40f, 0.32f), deepSunsetW);
            const float sunSideFwd = pow(saturate(dot(dir, aSun) * 0.5f + 0.5f), 2.4f);
            const float rimTrans = exp(-od * sigma * 0.32f) * lerp(0.45f, 1.0f, powder);
            const float undersideMask = lerp(1.0f, 0.42f, smoothstep(0.18f, 0.85f, h));
            const float alpenglowEnv = max(twiCloud, goldenW * 0.85f) * (1.0f - overcastW * 0.72f);
            const float3 underLit = alpenglowCol * (alpenglowEnv * rimTrans * (sunSideFwd * 0.52f + hg(cosT, 0.72f) * 0.38f) * undersideMask);
            // Orbital dual-moon phase-coupled silver-lining along thin cloud edges & crowns (warm rose Masser / cool cyan Secunda / dark new-moon):
            const float moonEdge = exp(-od * sigma * 0.40f) * lerp(0.45f, 1.0f, powder);
            const float3 moonRimCol = dualMoon.moonTintRGB * float3(0.56f, 0.62f, 0.84f) * moonPhaseGain;
            const float3 moonRim = moonRimCol * (nightW * (hg(cosT, 0.76f) * 0.62f + 0.08f * pow(h, 1.25f)) * moonEdge);
            const float3 townUnderGlow = nightSkyglow * ((1.0f - h) * (1.0f - h) * exp(-t / 95000.0f));
            // Crisp sunlit cumulus crowns (0.40x) against deep convective LCL bellies (kills muddy brown-grey flat clouds!):
            const float3 rawLum = whiteSun * (multiScat * lerp(0.68f, powder, 0.88f) * 4.0f * PI * 0.40f * upperDeckOcc) + coolBase + underLit + moonRim + townUnderGlow + stormFlash;
            const float3 lum = select(rawLum > 1.25f.xxx, 1.25f.xxx + 1.15f * (1.0f.xxx - exp(-(rawLum - 1.25f.xxx) / 1.15f)), rawLum);
            const float ext = dens * (sigma * 1.15f);
            const float stepT = exp(-ext * dtStep);
            S += T * lum * (1.0f - stepT);   // the step's in-scatter, integrated over its length
            T *= stepT;
        }
        // Wave 44 Seamless Far-Horizon Atmospheric Perspective Extinction (dir.z < 0.06):
        // Smooth C1 cubic horizon dissolve eliminates ANY hard horizon clip line as clouds recede into distant haze!
        const float3 softS = min(S, 1.18f.xxx);
        const float fade = exp(-max(t0, 0.0f) / 420000.0f);
        const float horLin = smoothstep(0.000f, 0.062f, dir.z);
        const float horFade = horLin * horLin * (3.0f - 2.0f * horLin);
        const float distCut = 1.0f - smoothstep(maxDist * 0.62f, maxDist * 0.96f, t0);
        const float effFade = saturate(fade) * horFade * distCut;
        const float3 aerialS = softS * effFade;
        // 3-Layer Physical Atmospheric Cloud Architecture:
        //   Layer 2: Mid-Altitude Altocumulus / Orographic Wave Deck (96k spherical shell, +34 deg Ekman shear, directional self-shadowing)
        //   Layer 3: High-Altitude Ice-Crystal Cirrus Wisps (130k spherical shell, +54 deg jet-stream curl streaks, warm rose/gold alpenglow!)
        float3 cirrusAdd = 0.0f.xxx;
        const float highAlpenglow = smoothstep(-0.12f, -0.01f, aElev) * (1.0f - smoothstep(0.05f, 0.24f, aElev));
        if (dir.z > 0.06f && T > 0.06f) {
            const float rP = 3600000.0f;
            const float r0 = rP + o.z;
            const float bP = r0 * dir.z;
            // Layer 2: Mid-altitude altocumulus / orographic wave bands at 96,000u on spherical shell
            const float rAlto = rP + 96000.0f;
            const float tAlto = max(sqrt(max(bP * bP + rAlto * rAlto - r0 * r0, 0.0f)) - bP, 0.0f);
            if (tAlto > 0.0f && tAlto < 420000.0f) {
                const float2 aWind = ekmanWind(0.65f);
                const float2 aDir = normalize(aWind + float2(1.0e-3f, 0.0f));
                const float2 aPerp = float2(-aDir.y, aDir.x);
                const float2 aPosW = (o.xy + dir.xy * tAlto + aWind * camPosW.w) * 0.000018f;
                const float2 aCurl = cloudCurl2D(aPosW * 1.15f, camPosW.w * 0.0025f) * 0.14f;
                // Anisotropic wind-sheared orographic lee-wave coordinates (elongated across wind, undulating along wind):
                const float2 aWaveUv = float2(dot(aPosW + aCurl, aDir) * 3.1f, dot(aPosW + aCurl, aPerp) * 1.45f);
                const float4 agW1 = rawNoise3Grad(float3(aWaveUv, 4.3f + camPosW.w * 0.0022f));
                const float4 agW2 = rawNoise3Grad(float3(aWaveUv * 2.4f + agW1.yz * 0.35f, 7.1f));
                const float aWave = agW1.x * 0.64f + (1.0f - abs(agW2.x * 2.0f - 1.0f)) * 0.36f;
                const float aMask = smoothstep(0.54f, 0.84f, aWave) * smoothstep(0.06f, 0.28f, dir.z) * exp(-tAlto / 290000.0f);
                // Directional wave-ridge self-shadowing from analytical gradient:
                const float2 lightWave = float2(dot(cloudLightDir.xy, aDir), dot(cloudLightDir.xy, aPerp));
                const float aRidgeLit = saturate(dot(normalize(float3(agW1.yz * 0.8f, 1.0f)), normalize(float3(lightWave, 0.45f))) * 0.65f + 0.45f);
                const float3 altoTwi = lerp(sunC, float3(1.62f, 0.46f, 0.38f), highAlpenglow * 0.90f);
                const float3 altoCol = (altoTwi * aRidgeLit * (0.42f + 0.58f * hg(cosT, 0.72f) * 2.2f) + skyC * 0.24f) * lerp(0.34f, 0.12f, nightW);
                cirrusAdd += altoCol * (aMask * T * horFade * (1.0f - saturate(clouds.x * 0.55f)));
            }
            // Layer 3: High-altitude jet-stream ice-crystal cirrus wisps at 130,000u on spherical shell
            const float rCir = rP + 130000.0f;
            const float tCir = max(sqrt(max(bP * bP + rCir * rCir - r0 * r0, 0.0f)) - bP, 0.0f);
            if (tCir > 0.0f && tCir < 480000.0f) {
                const float2 cWind = ekmanWind(1.00f);
                const float2 cDir = normalize(cWind + float2(1.0e-3f, 0.0f));
                const float2 cPerp = float2(-cDir.y, cDir.x);
                const float2 cPosW = (o.xy + dir.xy * tCir + cWind * camPosW.w) * 0.000012f;
                // Domain-warped jet-stream curl streaks (delicate mares'-tail ice filaments):
                const float4 cg1 = rawNoise3Grad(float3(cPosW * 1.6f, 5.1f + camPosW.w * 0.0018f));
                const float2 cWarp = float2(cg1.z, -cg1.y) * 0.28f;
                const float2 cJetUv = float2(dot(cPosW + cWarp, cDir) * 1.15f, dot(cPosW + cWarp, cPerp) * 3.45f);
                const float4 cg2 = rawNoise3Grad(float3(cJetUv * 1.85f, 8.3f + camPosW.w * 0.0022f));
                const float4 cg3 = rawNoise3Grad(float3(cJetUv * 5.20f + float2(cg2.z, -cg2.y) * 0.45f, 11.7f));
                const float filament = saturate(1.0f - abs(cg2.x * 2.0f - 1.0f) * 1.65f);
                const float wisp = cg1.x * 0.35f + filament * 0.42f + cg3.x * 0.23f;
                const float cMask = smoothstep(0.54f, 0.86f, wisp) * smoothstep(0.08f, 0.32f, dir.z);
                const float cFade = exp(-tCir / 340000.0f);
                const float cirAng = acos(clamp(dot(dir, aSun), -1.0f, 1.0f));
                const float iridBand = exp(-pow((cirAng - 0.19f) / 0.10f, 2.0f)) * (1.0f - nightW);
                const float3 iridTint = float3(0.55f + 0.45f * cos(cirAng * 26.0f), 0.55f + 0.45f * cos(cirAng * 26.0f + 2.094f), 0.55f + 0.45f * cos(cirAng * 26.0f + 4.188f));
                // High-altitude rose/gold twilight alpenglow (stays luminous at low sun angles after ground enters dusk!):
                const float3 roseGold = lerp(float3(1.65f, 0.56f, 0.42f), float3(1.75f, 0.82f, 0.36f), saturate(cosT * 0.5f + 0.5f));
                const float3 cirTwi = lerp(sunC, roseGold, highAlpenglow * 0.92f);
                const float cirNightScale = lerp(0.42f, 0.12f, nightW * (1.0f - highAlpenglow * 0.65f));
                const float3 cirCol = (cirTwi * (0.48f + 0.52f * hg(cosT, 0.70f) * 2.4f) + skyC * 0.26f + iridTint * (iridBand * 0.07f)) * cirNightScale;
                cirrusAdd += cirCol * (cMask * cFade * T * horFade * (1.0f - saturate(clouds.x * 0.60f)));
            }
        }
        res = float4(aerialS + cirrusAdd, lerp(1.0f, T, effFade));
    }
    // Last frame's value along the same direction with motion-adaptive spatiotemporal rejection (zero ghosting on fast turns).
    const float4 c = float4(far, 1.0f);
    const float4 pc = float4(dot(prevWorldToClip[0], c), dot(prevWorldToClip[1], c), dot(prevWorldToClip[2], c), dot(prevWorldToClip[3], c));
    if (pc.w > 0.0f && proj.z > 0.5f) {
        const float2 uv = float2(pc.x / pc.w * 0.5f + 0.5f, 0.5f - pc.y / pc.w * 0.5f) / resScale.xy;
        if (all(uv > 0.0f) && all(uv < 1.0f)) {
            const float edgeFade = smoothstep(0.0f, 0.045f, min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y)));
            const float2 curUv = full * dims.zw;
            const float turnReject = 1.0f - saturate(length(uv - curUv) * 16.0f) * 0.78f;
            const float4 h = History.SampleLevel(Lin, uv, 0.0f);
            // Smooth golden-ratio IGN temporal convergence (silky noise-free night/day clouds, instant rejection on fast camera turns):
            res = lerp(res, h, clamp(max(cloudWind.w, 0.52f), 0.35f, 0.66f) * edgeFade * turnReject);
        }
    }
    return res;
}
