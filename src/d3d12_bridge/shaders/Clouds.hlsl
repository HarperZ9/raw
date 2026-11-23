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

// Nubis Evolved (Schneider 2022, SKY-02) True 3D Volumetric Cumulus Field:
// Flat condensation base, per-cloud variable tower heights (cumulus -> towering congestus -> storm anvils),
// 3D domain-warped cauliflower billows, and infinite-resolution edge wisps inside a tall 44,000u 3D slab.
float cloudDensityVol(float3 pa, bool detail) {
    const float2 dxy = pa.xy - camPosW.xy;
    const float curvedZ = pa.z + dot(dxy, dxy) * 1.15e-8f;
    const float h = (curvedZ - clouds.y) / clouds.z;
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    const float3 wind = float3(cloudWind.xy * camPosW.w, 0.0f);
    const float3 sheared = pa + wind + float3(cloudWind.xy, 0.0f) * (h * 12.0f);
    const float cover = saturate(clouds.x);
    const float stormType = saturate(weather.w * 1.4f + max(cover - 0.78f, 0.0f) * 2.5f);

    // 2D continental weather map: varies BOTH local coverage AND per-cloud 3D tower height (flat stratus vs tall cumulus towers).
    const float2 wUv = sheared.xy / 62000.0f;
    const float wCov = cNoise(float3(wUv, 1.3f)) * 0.62f + cNoise(float3(wUv * 2.35f + 7.1f, 3.7f)) * 0.38f;
    const float wHeight = saturate(cNoise(float3(wUv * 1.45f + 19.3f, 5.1f)) * 0.70f + stormType * 0.45f);
    const float topCap = lerp(0.42f, 0.96f, wHeight);
    // Crisp flat cumulus condensation base (0..0.08) + rounded vertical cauliflower dome tapering to topCap.
    const float baseGrad = smoothstep(0.0f, 0.09f, h);
    const float topGrad  = 1.0f - smoothstep(topCap * 0.48f, topCap, h);
    const float anvilSpread = stormType * smoothstep(0.68f, 0.88f, h) * (1.0f - smoothstep(0.88f, 0.99f, h)) * 0.18f;
    const float hGrad = saturate(baseGrad * topGrad + anvilSpread);
    if (hGrad <= 0.001f) { return 0.0f; }

    // 3D dimensional cumulus structure (isotropic vertical + horizontal scale so clouds read as huge 3D masses, not flat sheets!).
    const float3 p0 = float3(sheared.x / 32000.0f, sheared.y / 32000.0f, (curvedZ - clouds.y) / 22000.0f);
    const float3 w0 = float3(cNoise(p0 * 0.95f + float3(3.1f, 7.4f, 1.9f)) - 0.5f,
                             cNoise(p0 * 0.95f + float3(11.7f, 2.3f, 5.1f)) - 0.5f,
                             cNoise(p0 * 0.95f + float3(6.3f, 14.1f, 2.7f)) - 0.5f) * 0.55f;
    const float3 pw = p0 + w0;
    const float f1 = cNoise(pw);
    const float f2 = cNoise(pw * 2.09f + float3(13.1f, 5.7f, 3.3f));
    const float f3 = cNoise(pw * 4.27f + float3(7.9f, 19.3f, 8.1f));
    const float f4 = cNoise(pw * 8.63f + float3(23.7f, 11.1f, 15.7f));
    // Billowed ridges (1 - |2n - 1|) sculpt real puffy cumulus domes instead of flat Perlin blobs.
    const float billow = (1.0f - abs(f1 * 2.0f - 1.0f)) * 0.50f + (1.0f - abs(f2 * 2.0f - 1.0f)) * 0.30f + f3 * 0.13f + f4 * 0.07f;
    const float localCov = saturate(cover * lerp(0.55f, 1.35f, smoothstep(0.25f, 0.78f, wCov)));
    const float thresh = lerp(0.64f, lerp(0.42f, 0.24f, stormType), localCov);
    float dens = saturate((billow * hGrad - thresh) / max(1.0f - thresh, 0.22f));
    // Vertical density gradient: lighter wisps at base, dense billowing core at mid-altitude, crisp crowns.
    dens *= lerp(0.55f, 1.15f, smoothstep(0.05f, 0.38f, h));
    if (detail && dens > 0.002f) {
        // High-frequency 3D cauliflower turret erosion on cloud boundaries (infinite resolution, zero 64^3 blur).
        const float3 r = float3((sheared.x + wind.x * 0.30f) / 6200.0f, (sheared.y + wind.y * 0.30f) / 6200.0f, (curvedZ - clouds.y) / 5200.0f);
        const float d1 = cNoise(r);
        const float d2 = cNoise(r * 2.19f + float3(5.3f, 17.1f, 9.7f));
        const float d3 = cNoise(r * 4.61f + float3(19.9f, 3.7f, 21.3f));
        const float detailFbm = d1 * 0.52f + d2 * 0.31f + d3 * 0.17f;
        const float erode = detailFbm * lerp(0.32f, 0.54f, h) * (1.0f - dens * 0.50f);
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
        const float twiCloud = smoothstep(-0.10f, 0.00f, aElev) * (1.0f - smoothstep(0.06f, 0.26f, aElev));
        const float3 twiCoral = float3(1.45f, 0.52f, 0.28f) * max(sLum, 0.55f);
        const float3 physDaySun = lerp(lerp(rawSunC, hTrans * (max(sLum, 0.14f) / htLum), 0.65f), twiCoral, twiCloud * 0.72f);
        // Subtle silver-indigo lunar illumination on night clouds (never lit like daytime!).
        const float3 sunC = lerp(physDaySun, float3(0.16f, 0.23f, 0.40f) * max(sLum * 0.65f, 0.35f), nightW);
        const float3 rawSkyC = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.0f);
        const float skyLum = max(dot(rawSkyC, float3(0.2126f, 0.7152f, 0.0722f)), 0.05f);
        // Clean atmospheric blue-silver cloud shadow ambient (eliminates muddy brown undersides).
        const float3 skyC = lerp(rawSkyC, float3(0.62f, 0.76f, 0.96f) * skyLum, 0.72f);
        const float sigma = kCloudSigma;
        float T = 1.0f;
        float3 S = 0.0f;
        // Deep 6-step exponential sun-ray march spanning the full 44,000u 3D cumulus volume:
        // gives real self-shadowed cloud bellies, glowing translucent rims, and true light scattering THROUGH clouds.
        static const float kSunD[6] = { 320.0f, 850.0f, 2100.0f, 4600.0f, 9200.0f, 16500.0f };
        static const float kSunW[6] = { 320.0f, 530.0f, 1250.0f, 2500.0f, 4600.0f,  7300.0f };
        // Adaptive empty-space skipping raymarch across the tall 3D cumulus slab.
        float t = t0 + j * dt;
        [loop] for (uint i = 0u; i < steps && t < t1 && T > 0.01f; i++) {
            const float3 pa = o + dir * t;
            const float coarse = cloudDensityVol(pa, false);
            if (coarse <= 0.001f) { t += dt * 2.0f; continue; }
            const float dens = cloudDensityVol(pa, true);
            if (dens <= 0.001f) { t += dt; continue; }
            t += dt;
            float od = 0.0f;
            [unroll] for (uint k = 0u; k < 6u; k++) { od += cloudDensityVol(pa + cloudLightDir * kSunD[k], false) * kSunW[k]; }
            // Wrenninge 2013 + Schneider Nubis Evolved 4-Octave Multiple-Scattering Energy Integral:
            // strong forward-scattering silver lining (g1=0.82) lets sunlight visibly scatter THROUGH thin cloud edges
            // while deep optical depth carves rich 3D self-shadowed cloud cores!
            const float3 odRGB = od * (sigma * 0.52f) * float3(0.96f, 1.00f, 1.05f);
            float3 multiScat = 0.0f.xxx;
            float aOct = 1.0f, bOct = 1.0f, cOct = 1.0f;
            [unroll] for (int oct = 0; oct < 4; oct++) {
                const float phOct = lerp(hg(cosT, -0.28f * cOct), hg(cosT, 0.82f * cOct), 0.62f);
                multiScat += exp(-odRGB * bOct) * (aOct * min(phOct, 0.85f));
                aOct *= 0.58f; bOct *= 0.38f; cOct *= 0.50f;
            }
            const float powder = 1.0f - exp(-od * sigma * 1.15f - dens * 0.55f);
            const float h = saturate((pa.z - clouds.y) / clouds.z);
            const float stormW = saturate(weather.w * 1.25f + max(clouds.x - 0.72f, 0.0f) * 2.2f);
            // Deep 3D ambient occlusion inside cloud bellies (0.24..1.00) instead of flat grey 0.62 fill!
            const float cloudAo = lerp(lerp(0.32f, 0.18f, stormW), 1.00f, pow(h, 0.82f)) * exp(-dens * 0.22f);
            const float lowSun = saturate(1.0f - max(aElev, 0.0f) * 3.4f) * (1.0f - nightW);
            const float overcastW = saturate((clouds.x - 0.50f) * 2.2f);
            float3 stormFlash = 0.0f.xxx;
            if (clouds.x > 0.78f && weather.w > 0.25f && timeEnv.z > 0.05f) {
                const float tSlot = floor(camPosW.w * 0.55f);
                const float tFrac = frac(camPosW.w * 0.55f);
                const float pulse = exp(-tFrac * 18.0f) * step(0.88f, cHash(float3(tSlot, 3.7f, 11.3f)));
                if (pulse > 0.01f) {
                    const float2 cell = floor(pa.xy / 38000.0f);
                    const float cellHit = step(0.75f, cHash(float3(cell, tSlot)));
                    stormFlash = float3(0.65f, 0.82f, 1.25f) * (pulse * cellHit * dens * 1.8f * timeEnv.z);
                }
            }
            const float3 whiteSun = lerp(sunC, dot(sunC, float3(0.2126f, 0.7152f, 0.0722f)).xxx * float3(1.00f, 0.99f, 0.98f), 0.45f * (1.0f - lowSun) * (1.0f - nightW));
            const float3 dayCoolBase = lerp(float3(0.34f, 0.46f, 0.68f), float3(0.46f, 0.52f, 0.60f), overcastW) * max(dot(skyC, float3(0.2126f, 0.7152f, 0.0722f)), 0.18f) * cloudAo;
            const float3 nightCloudBase = float3(0.014f, 0.022f, 0.042f) * cloudAo;
            const float3 coolBase = lerp(dayCoolBase, nightCloudBase, nightW);
            const float3 underLit = float3(1.35f, 0.46f, 0.22f) * (twiCloud * (1.0f - h) * (0.40f + 0.60f * pow(saturate(dot(dir, aSun) * 0.5f + 0.5f), 2.0f)) * (1.0f - overcastW * 0.45f) * 0.58f);
            const float3 rawLum = whiteSun * (multiScat * lerp(0.85f, powder, 0.72f) * 4.0f * PI * 0.36f) + coolBase + underLit + stormFlash;
            const float3 lum = select(rawLum > 0.95f.xxx, 0.95f.xxx + 1.85f * (1.0f.xxx - exp(-(rawLum - 0.95f.xxx) / 1.85f)), rawLum);
            const float ext = dens * (sigma * 0.65f);
            const float stepT = exp(-ext * dt);
            S += T * lum * (1.0f - stepT);   // the step's in-scatter, integrated over its length
            T *= stepT;
        }
        // True linear HDR cloud radiance matched to Hillaire sky + seamless C2 horizon elevation fade.
        const float3 softS = min(S, 2.40f.xxx);
        const float fade = exp(-max(t0, 0.0f) / 185000.0f);
        const float horFade = smoothstep(0.015f, 0.24f, dir.z);
        const float distCut = 1.0f - smoothstep(maxDist * 0.50f, maxDist * 0.95f, t0);
        const float effFade = saturate(fade * 1.15f) * horFade * distCut;
        const float3 horizonHaze = skyC + sunC * pow(saturate(cosT), 8.0f) * 0.05f;
        const float3 aerialS = lerp(horizonHaze * (1.0f - T), softS, fade) * (horFade * distCut);
        // High-altitude wispy Cirrus cloud deck (h = 68,000u above cumulus): wind-sheared ice-crystal streaks
        // illuminated by Hillaire 2020 solar transmittance, visible through gaps in the lower cumulus deck.
        // 2nd High-Altitude Ice-Crystal Cirrus / Mares'-Tail Layer at 125,000u (above the 46k..90k cumulus slab!):
        // Catches fiery coral-gold sunrise/sunset light minutes before dawn & after dusk (author-01.jpg own-13..15, own-27)
        // plus subtle pearlescent cloud iridescence within 5..22 deg of the sun (Commons Cluster B).
        float3 cirrusAdd = 0.0f.xxx;
        if (dir.z > 0.03f && T > 0.06f) {
            const float tCir = (125000.0f - o.z) / max(dir.z, 0.03f);
            if (tCir > 0.0f && tCir < 650000.0f) {
                const float2 cPos = (o.xy + dir.xy * tCir + cloudWind.xy * camPosW.w * 1.65f) * 0.0000095f;
                const float warp = cNoise(float3(cPos * 1.8f, 2.3f)) * 1.55f;
                const float streak = cNoise(float3(cPos.x * 4.4f + warp, cPos.y * 1.15f - warp * 0.5f, 5.1f));
                const float detail = cNoise(float3(cPos * 10.0f + warp, 8.7f));
                const float cMask = smoothstep(0.54f, 0.84f, streak * 0.68f + detail * 0.32f) * smoothstep(0.03f, 0.20f, dir.z);
                const float cFade = exp(-tCir / 380000.0f);
                const float cirAng = acos(clamp(dot(dir, aSun), -1.0f, 1.0f));
                const float iridBand = exp(-pow((cirAng - 0.19f) / 0.10f, 2.0f)) * (1.0f - nightW);
                const float3 iridTint = float3(0.55f + 0.45f * cos(cirAng * 26.0f), 0.55f + 0.45f * cos(cirAng * 26.0f + 2.094f), 0.55f + 0.45f * cos(cirAng * 26.0f + 4.188f));
                const float3 cirTwi = lerp(sunC, float3(1.45f, 0.50f, 0.26f), twiCloud * 0.85f);
                const float3 cirCol = (cirTwi * (0.48f + 0.52f * hg(cosT, 0.60f) * 2.4f) + skyC * 0.28f + iridTint * (iridBand * 0.22f)) * lerp(0.46f, 0.12f, nightW);
                cirrusAdd = cirCol * (cMask * cFade * T * horFade * (1.0f - saturate(clouds.x * 0.62f)));
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
