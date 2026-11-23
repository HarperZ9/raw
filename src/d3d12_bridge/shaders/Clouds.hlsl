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
    const float2 dxy = pa.xy - camPosW.xy;
    const float curvedZ = pa.z + dot(dxy, dxy) * 1.15e-8f;
    const float h = (curvedZ - clouds.y) / clouds.z;
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    // Altitude-layered Ekman wind advection (base moves slower, upper turrets & anvils veer & shear faster!).
    const float2 wLayer = ekmanWind(h * 0.55f);
    const float3 wind = float3(wLayer * camPosW.w, 0.0f);
    const float3 sheared = pa + wind + float3(wLayer, 0.0f) * (h * 14.0f);
    const float cover = saturate(clouds.x);
    const float stormType = saturate(weather.w * 1.4f + max(cover - 0.78f, 0.0f) * 2.5f);

    // 2D continental weather map + slow convective metamorphosis wave (curl-marbled on detail pass to save ~1.4 ms GPU time!):
    const float tMorph = camPosW.w * 0.0032f;
    const float2 wUv = sheared.xy / 62000.0f;
    const float2 curlW = detail ? (cloudCurl2D(wUv * 1.35f, tMorph * 0.65f) * 0.28f) : 0.0f.xx;
    const float wCov = cNoise(float3(wUv + curlW, 1.3f + tMorph)) * 0.62f
                     + cNoise(float3((wUv - curlW * 0.6f) * 2.35f + 7.1f, 3.7f - tMorph * 0.8f)) * 0.38f;
    const float wHeight = saturate(cNoise(float3(wUv * 1.45f + 19.3f + curlW * 0.5f, 5.1f + tMorph * 0.5f)) * 0.70f + stormType * 0.45f);
    const float topCap = lerp(0.42f, 0.96f, wHeight);
    // Crisp flat cumulus condensation base (0..0.08) + rounded vertical cauliflower dome tapering to topCap.
    const float baseGrad = smoothstep(0.0f, 0.09f, h);
    const float topGrad  = 1.0f - smoothstep(topCap * 0.48f, topCap, h);
    const float anvilSpread = stormType * smoothstep(0.68f, 0.88f, h) * (1.0f - smoothstep(0.88f, 0.99f, h)) * 0.18f;
    const float hGrad = saturate(baseGrad * topGrad + anvilSpread);
    if (hGrad <= 0.001f) { return 0.0f; }

    // 4D evolving towering 3D cumulus & anvil volumes (tighter 18k/15k aspect ratio replaces flat 2D pancake streaks
    // seen in 12.01.38.14.mp4 with sculptural billowing RDR2/MSFS cauliflower crowns and deep convective turbulence!):
    const float tEvo = camPosW.w * 0.0085f;
    const float3 p0 = float3(sheared.x / 19500.0f, sheared.y / 19500.0f, (curvedZ - clouds.y) / 14500.0f - tEvo * 0.45f);
    const float3 w0 = float3(cNoise(p0 * 1.10f + float3(3.1f + tEvo, 7.4f, 1.9f)) - 0.5f,
                             cNoise(p0 * 1.10f + float3(11.7f, 2.3f - tEvo * 0.90f, 5.1f)) - 0.5f,
                             cNoise(p0 * 1.10f + float3(6.3f, 14.1f, 2.7f + tEvo * 1.25f)) - 0.5f) * 0.85f;
    const float3 pw = p0 + w0;
    // Sculptural 4D cumulus cauliflower domes: blends smooth Perlin base with rounded billowing Worley-like puffs!
    const float f1 = cNoise(pw);
    const float f2 = cNoise(pw * 2.13f + float3(13.1f, 5.7f, 3.3f + tEvo * 0.35f));
    const float f3 = cNoise(pw * 4.37f + float3(7.9f, 19.3f, 8.1f - tEvo * 0.45f));
    const float f4 = cNoise(pw * 8.81f + float3(23.7f, 11.1f, 15.7f));
    const float puff = smoothstep(0.18f, 0.86f, f1 * 0.50f + f2 * 0.28f + f3 * 0.15f + f4 * 0.07f);
    const float billow = pow(puff, 0.78f);
    const float localCov = saturate(cover * lerp(0.50f, 1.40f, smoothstep(0.22f, 0.78f, wCov)));
    const float thresh = lerp(0.58f, lerp(0.36f, 0.22f, stormType), localCov);
    float dens = saturate((billow * hGrad - thresh) / max(1.0f - thresh, 0.22f));
    dens *= lerp(0.65f, 1.35f, smoothstep(0.05f, 0.42f, h));
    if (detail && dens > 0.002f) {
        const float3 r = float3((sheared.x + wind.x * 0.30f) / 4800.0f, (sheared.y + wind.y * 0.30f) / 4800.0f, (curvedZ - clouds.y) / 4200.0f);
        const float d1 = cNoise(r);
        const float d2 = cNoise(r * 2.27f + float3(5.3f, 17.1f, 9.7f));
        const float d3 = cNoise(r * 4.71f + float3(19.9f, 3.7f, 21.3f));
        const float d4 = cNoise(r * 9.53f + float3(11.1f, 29.3f, 7.9f));
        const float detailFbm = d1 * 0.44f + d2 * 0.30f + d3 * 0.16f + d4 * 0.10f;
        const float erode = (1.0f - detailFbm) * lerp(0.32f, 0.58f, h) * (1.0f - dens * 0.45f);
        dens = cRemap(dens, erode, 1.0f, 0.0f, 1.0f);
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

float rand(float2 p, float s) { return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f)) + s * 0.61803399f)); }

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const float2 lo = floor(at.xy);
    const float2 full = (lo + 0.5f) * 2.0f;   // 1/2-res temporally-reprojected clouds (4x faster, full-res depth composite)
    const float3 far = worldAt(full - 0.5f, 0.995f) - eyeAt();
    const float3 dir = normalize(far);
    // SM6.9 Wave-coherent sub-horizon early exit: entire waves looking below the horizon skip cloud raymarching cleanly.
    if (WaveActiveAllTrue(dir.z <= 0.015f)) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }
    float4 res = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float t0, t1;
    const float3 o = camPosW.xyz;
    const float maxDist = 400000.0f;
    if (cloudsOn() && cloudSpan(o, dir, maxDist, t0, t1)) {
        const uint steps = 48u;
        const float dt = (t1 - t0) / float(steps);
        const float j = rand(lo, proj.z);
        const float3 aSun = astroSunDir();
        const float aElev = astroSunElev();
        const float nightW = smoothstep(0.04f, -0.07f, aElev);
        // Use true astronomical sun by day/twilight (under-lighting clouds cleanly at sunrise/sunset!) and Moon (sunDir) at night.
        const float3 cloudLightDir = normalize(lerp(aSun, sunDir.xyz, nightW));
        const float cosT = dot(dir, cloudLightDir);
        // Balanced dual-lobe Henyey-Greenstein: crisp silver lining without blowing into clipped white blobs.
        const float phase = min(lerp(hg(cosT, -0.25f), hg(cosT, 0.65f), 0.48f), 0.24f);
        const float3 rawSunC = sunColour.rgb * (sunDir.w > 0.5f ? 1.0f : 0.0f);
        const float sLum = dot(rawSunC, float3(0.2126f, 0.7152f, 0.0722f));
        // Couple daytime/twilight cloud sunlight directly to Hillaire 2020 256x64 Transmittance LUT using TRUE aElev:
        // crisp silver-white at noon (own-43/44), fiery coral-peach-gold underlit cloud bellies at sunrise/sunset (own-13..15/27).
        const float3 hTrans = sampleCloudSunTrans(clamp(aElev, -0.10f, 1.0f));
        const float htLum = max(dot(hTrans, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float twiCloud = smoothstep(-0.10f, 0.00f, aElev) * (1.0f - smoothstep(0.06f, 0.26f, aElev)) * (1.0f - saturate(clouds.x * 0.85f - 0.25f));
        const float3 twiCoral = float3(1.25f, 0.54f, 0.28f) * max(sLum, 0.45f);
        const float3 physDaySun = lerp(lerp(rawSunC, hTrans * (max(sLum, 0.14f) / htLum), 0.65f), twiCoral, twiCloud * 0.48f);
        // Unified World-Space Atmosphere sync: lock cloud sun, shadow ambient, and horizon haze directly to evalWorldAtmosphere()
        // so volumetric clouds fuse seamlessly into the sky dome, mountain aerial perspective, and terrain lighting!
        float3 uSunC, uSkyZ, uSkyH, uAmbUp, uAmbDn;
        evalWorldAtmosphere(uSunC, uSkyZ, uSkyH, uAmbUp, uAmbDn);
        const float3 sunC = lerp(uSunC, physDaySun, 0.25f * (1.0f - nightW));
        const float3 skyC = lerp(uSkyH, uSkyZ, saturate(dir.z * 1.4f));
        const float sigma = kCloudSigma;
        float T = 1.0f;
        float3 S = 0.0f;
        // Fast 4-step exponential sun-ray march spanning the full 44,000u 3D cumulus volume (recovers ~1.3 ms GPU time!):
        static const float kSunD[4] = { 420.0f, 1650.0f, 5200.0f, 13500.0f };
        static const float kSunW[4] = { 420.0f, 1230.0f, 3550.0f,  8300.0f };
        // Uniform jittered 48-step march (eliminates 2*dt empty-space skip contour-banding stripes seen in 09.57.42.68.png!):
        float t = t0 + j * dt;
        [loop] for (uint i = 0u; i < steps && t < t1 && T > 0.01f; i++) {
            const float3 pa = o + dir * t;
            t += dt;
            const float coarse = cloudDensityVol(pa, false);
            if (coarse <= 0.001f) { continue; }
            const float dens = cloudDensityVol(pa, true);
            if (dens <= 0.001f) { continue; }
            float od = 0.0f;
            [unroll] for (uint k = 0u; k < 4u; k++) { od += cloudDensityVol(pa + cloudLightDir * kSunD[k], false) * kSunW[k]; }
            // 8-Octave Path-Traced-Matched Multiple-Scattering Integral (zydak1939 + CharlesGrassi + Wrenninge):
            // Water droplets have albedo ~0.999: 8 geometric octaves (a=0.66, b=0.46, c=0.56) reproduce pure luminous
            // white sunlit cumulus crowns alongside deep cool cerulean-slate self-shadowed hollows!
            const float3 odRGB = od * (sigma * 0.58f) * float3(0.96f, 1.00f, 1.05f);
            float3 multiScat = 0.0f.xxx;
            float aOct = 1.0f, bOct = 1.0f, cOct = 1.0f;
            [unroll] for (int oct = 0; oct < 8; oct++) {
                const float phOct = lerp(hg(cosT, -0.30f * cOct), hg(cosT, 0.84f * cOct), 0.64f);
                multiScat += exp(-odRGB * bOct) * (aOct * min(phOct, 0.90f));
                aOct *= 0.66f; bOct *= 0.46f; cOct *= 0.56f;
            }
            const float powder = 1.0f - exp(-od * sigma * 1.45f - dens * 0.65f);
            const float h = saturate((pa.z - clouds.y) / clouds.z);
            const float stormW = saturate(weather.w * 1.25f + max(clouds.x - 0.72f, 0.0f) * 2.2f);
            // Deep 3D ambient occlusion inside cloud bellies (0.22..1.00) with cerulean-slate sky-scatter hollows (zydak1939)!
            const float cloudAo = lerp(lerp(0.28f, 0.16f, stormW), 1.00f, pow(h, 0.85f)) * exp(-dens * 0.24f);
            const float lowSun = saturate(1.0f - max(aElev, 0.0f) * 3.4f) * (1.0f - nightW);
            const float overcastW = saturate((clouds.x - 0.50f) * 2.2f);
            // Cluster B (AA425 / AA255 thunderheads lit from inside): continuous gridless 3D volumetric sheet-lightning
            // (single radial bolt origin per flash slot = 100% organic radial cloud illumination with ZERO square cell edges!).
            float3 stormFlash = 0.0f.xxx;
            if (clouds.x > 0.66f && (weather.w > 0.18f || timeEnv.z > 0.05f)) {
                const float stormInt = max(timeEnv.z, saturate((weather.w - 0.18f) * 1.35f) * saturate((clouds.x - 0.62f) * 2.8f));
                const float tSlot = floor(camPosW.w * 0.62f);
                const float tFrac = frac(camPosW.w * 0.62f);
                const float dblPulse = exp(-tFrac * 16.0f) + 0.55f * exp(-abs(tFrac - 0.14f) * 28.0f);
                const float trigger = step(0.84f, cHash(float3(tSlot, 3.7f, 11.3f)));
                const float pulse = dblPulse * trigger;
                if (pulse > 0.01f) {
                    const float boltAng = cHash(float3(tSlot, 17.1f, 5.3f)) * 6.2831853f;
                    const float boltDist = 24000.0f + cHash(float3(tSlot, 29.7f, 8.9f)) * 75000.0f;
                    const float2 boltPos = camPosW.xy + float2(cos(boltAng), sin(boltAng)) * boltDist;
                    const float distBolt = length(pa.xy - boltPos) / 28000.0f;
                    const float glowVol = exp(-distBolt * distBolt * 2.4f);
                    const float3 boltCol = lerp(float3(0.55f, 0.72f, 1.35f), float3(0.95f, 0.96f, 1.40f), exp(-distBolt * 3.5f));
                    stormFlash = boltCol * (pulse * glowVol * (0.30f + dens * 2.1f) * stormInt * 2.2f);
                }
            }
            const float3 whiteSun = lerp(sunC, dot(sunC, float3(0.2126f, 0.7152f, 0.0722f)).xxx * float3(1.00f, 0.995f, 0.99f), 0.45f * (1.0f - lowSun) * (1.0f - nightW));
            const float3 coolBase = lerp(uSkyZ * 0.78f, uSkyH * 0.85f, overcastW * 0.65f + (1.0f - h) * 0.35f) * cloudAo;
            const float3 underLit = float3(1.25f, 0.50f, 0.24f) * (twiCloud * (1.0f - h) * pow(saturate(dot(dir, aSun)), 2.5f) * (1.0f - overcastW * 0.85f) * 0.55f);
            // Night lunar rim glow: clouds near the Moon (sunDir.xyz at night) transmit translucent silver-blue light!
            const float3 moonRim = float3(0.38f, 0.52f, 0.85f) * (nightW * hg(cosT, 0.76f) * exp(-od * sigma * 0.35f) * 0.65f);
            const float3 rawLum = whiteSun * (multiScat * lerp(0.80f, powder, 0.75f) * 4.0f * PI * 0.30f) + coolBase + underLit + moonRim + stormFlash;
            const float3 lum = select(rawLum > 1.05f.xxx, 1.05f.xxx + 2.10f * (1.0f.xxx - exp(-(rawLum - 1.05f.xxx) / 2.10f)), rawLum);
            const float ext = dens * (sigma * 1.15f);
            const float stepT = exp(-ext * dt);
            S += T * lum * (1.0f - stepT);   // the step's in-scatter, integrated over its length
            T *= stepT;
        }
        // Continuous world-space horizon merge: extend clouds smoothly down to ~4 deg above horizon (eliminates 14-deg gap!).
        const float3 softS = min(S, 2.40f.xxx);
        const float fade = exp(-max(t0, 0.0f) / 220000.0f);
        const float horFade = smoothstep(0.002f, 0.085f, dir.z);
        const float distCut = 1.0f - smoothstep(maxDist * 0.55f, maxDist * 0.96f, t0);
        const float effFade = saturate(fade * 1.15f) * horFade * distCut;
        const float3 horizonHaze = skyC + sunC * pow(saturate(cosT), 8.0f) * 0.05f;
        const float3 aerialS = lerp(horizonHaze * (1.0f - T), softS, fade) * (horFade * distCut);
        // High-altitude wispy Cirrus cloud deck (h = 68,000u above cumulus): wind-sheared ice-crystal streaks
        // illuminated by Hillaire 2020 solar transmittance, visible through gaps in the lower cumulus deck.
        // 3-Layer Ekman-Sheared Sky Architecture: Mid-Altitude Altocumulus (96k, +34 deg veer, curl-marbled)
        // + High-Altitude Jet-Stream Cirrus (130k, +54 deg veer, 2.65x speed, mares'-tail stretching & iridescence)!
        float3 cirrusAdd = 0.0f.xxx;
        if (dir.z > 0.03f && T > 0.06f) {
            // Layer 2: Mid-altitude altocumulus wave bands at 96,000u (Ekman +34 deg, curl-noise marbling)
            const float tAlto = (96000.0f - o.z) / max(dir.z, 0.03f);
            if (tAlto > 0.0f && tAlto < 520000.0f) {
                const float2 aWind = ekmanWind(0.65f);
                const float2 aPos = (o.xy + dir.xy * tAlto + aWind * camPosW.w) * 0.000016f;
                const float2 aCurl = cloudCurl2D(aPos * 1.45f, camPosW.w * 0.0035f) * 0.85f;
                const float aWave = cNoise(float3((aPos + aCurl) * float2(2.8f, 2.1f), 4.3f + camPosW.w * 0.0022f));
                const float aMask = smoothstep(0.56f, 0.85f, aWave) * smoothstep(0.03f, 0.18f, dir.z) * exp(-tAlto / 320000.0f);
                const float3 altoTwi = lerp(sunC, float3(1.55f, 0.44f, 0.34f), twiCloud * 0.88f);
                const float3 altoCol = (altoTwi * (0.42f + 0.58f * hg(cosT, 0.68f) * 2.2f) + skyC * 0.24f) * lerp(0.38f, 0.14f, nightW);
                cirrusAdd += altoCol * (aMask * T * horFade * (1.0f - saturate(clouds.x * 0.55f)));
            }
            // Layer 3: High-altitude jet-stream ice-crystal cirrus at 130,000u (Ekman +54 deg, 2.65x speed, curl wisps)
            const float tCir = (130000.0f - o.z) / max(dir.z, 0.03f);
            if (tCir > 0.0f && tCir < 650000.0f) {
                const float2 cWind = ekmanWind(1.00f);
                const float2 cPos = (o.xy + dir.xy * tCir + cWind * camPosW.w) * 0.0000095f;
                const float2 cCurl = cloudCurl2D(cPos * 1.9f, camPosW.w * 0.0042f) * 1.15f;
                const float streak = cNoise(float3((cPos.x + cCurl.x) * 2.4f, (cPos.y + cCurl.y) * 1.25f, 5.1f + camPosW.w * 0.0018f));
                const float detail = cNoise(float3((cPos + cCurl * 0.6f) * 10.0f, 8.7f));
                const float cMask = smoothstep(0.54f, 0.84f, streak * 0.68f + detail * 0.32f) * smoothstep(0.03f, 0.20f, dir.z);
                const float cFade = exp(-tCir / 380000.0f);
                const float cirAng = acos(clamp(dot(dir, aSun), -1.0f, 1.0f));
                const float iridBand = exp(-pow((cirAng - 0.19f) / 0.10f, 2.0f)) * (1.0f - nightW);
                const float3 iridTint = float3(0.55f + 0.45f * cos(cirAng * 26.0f), 0.55f + 0.45f * cos(cirAng * 26.0f + 2.094f), 0.55f + 0.45f * cos(cirAng * 26.0f + 4.188f));
                const float3 cirTwi = lerp(sunC, float3(1.48f, 0.48f, 0.28f), twiCloud * 0.88f);
                const float3 cirCol = (cirTwi * (0.48f + 0.52f * hg(cosT, 0.62f) * 2.4f) + skyC * 0.26f + iridTint * (iridBand * 0.24f)) * lerp(0.46f, 0.14f, nightW);
                cirrusAdd += cirCol * (cMask * cFade * T * horFade * (1.0f - saturate(clouds.x * 0.62f)));
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
            const float turnReject = 1.0f - saturate(length(uv - curUv) * 14.0f) * 0.62f;
            const float4 h = History.SampleLevel(Lin, uv, 0.0f);
            res = lerp(res, h, min(cloudWind.w, 0.68f) * edgeFade * turnReject);
        }
    }
    return res;
}
