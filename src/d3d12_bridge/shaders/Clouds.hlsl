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

Texture2D<float4> History  : register(t0);
Texture2D<float4> TransLut : register(t1);
SamplerState      Lin      : register(s0);

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
    const float2 full = lo + 0.5f;   // full native 1:1 resolution volumetric clouds (zero upscaling blur)
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
        const float cosT = dot(dir, sunDir.xyz);
        // Balanced dual-lobe Henyey-Greenstein: crisp silver lining without blowing into clipped white blobs.
        const float phase = min(lerp(hg(cosT, -0.25f), hg(cosT, 0.65f), 0.48f), 0.24f);
        const float3 rawSunC = sunColour.rgb * (sunDir.w > 0.5f ? 1.0f : 0.0f);
        const float sLum = dot(rawSunC, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightW = saturate((0.28f - sLum) * 4.5f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
        // Couple daytime cloud sunlight directly to Hillaire 2020 256x64 Transmittance LUT at cloud-deck altitude (4.5 km):
        // crisp CIE D65 silver-white at noon, authentic fiery copper-crimson underlit cloud bellies at sunrise/sunset.
        const float3 hTrans = sampleCloudSunTrans(clamp(sunDir.z, -0.15f, 1.0f));
        const float htLum = max(dot(hTrans, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float3 physDaySun = lerp(rawSunC, hTrans * (max(sLum, 0.08f) / htLum), 0.58f * (1.0f - nightW));
        // Silver-blue lunar illumination on night clouds so clouds passing Masser & Secunda catch crisp moonlight rims.
        const float3 sunC = lerp(physDaySun, max(rawSunC, float3(0.24f, 0.34f, 0.54f)) * 1.65f, nightW * 0.78f);
        const float3 rawSkyC = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.0f);
        const float skyLum = max(dot(rawSkyC, float3(0.2126f, 0.7152f, 0.0722f)), 0.05f);
        // Clean atmospheric blue-silver cloud shadow ambient (eliminates muddy brown undersides).
        const float3 skyC = lerp(rawSkyC, float3(0.62f, 0.76f, 0.96f) * skyLum, 0.72f);
        const float sigma = kCloudSigma;
        float T = 1.0f;
        float3 S = 0.0f;
        // Fine-to-deep exponential sun-march offsets capture crisp photographic cumulus tufts and soft undersides.
        static const float kSunD[6] = { 140.0f, 340.0f, 760.0f, 1500.0f, 2800.0f, 4800.0f };
        static const float kSunW[6] = { 140.0f, 200.0f, 420.0f,  740.0f, 1300.0f, 2000.0f };
        // Adaptive empty-space skipping raymarch: 2x stride in clear sky, fine 1x stride with full detail inside clouds.
        float t = t0 + j * dt;
        [loop] for (uint i = 0u; i < steps && t < t1 && T > 0.01f; i++) {
            const float3 pa = o + dir * t;
            const float coarse = cloudDensity(pa, false);
            if (coarse <= 0.001f) { t += dt * 2.0f; continue; }
            const float dens = cloudDensity(pa, true);
            if (dens <= 0.001f) { t += dt; continue; }
            t += dt;
            float od = 0.0f;
            [unroll] for (uint k = 0u; k < 6u; k++) { od += cloudDensity(pa + sunDir.xyz * kSunD[k], false) * kSunW[k]; }
            // Wrenninge 2013 (Oz: The Great and Powerful / Frostbite / UE5) 4-Octave Multiple-Scattering Energy Integral:
            // Couples per-octave extinction attenuation (b^n), energy attenuation (a^n), and phase-eccentricity decay (c^n)
            // for luminous translucent silver-lined cumulus rims and deep sculptural cloud undersides.
            const float3 odRGB = od * sigma * float3(0.97f, 1.00f, 1.04f);
            float3 multiScat = 0.0f.xxx;
            float aOct = 1.0f, bOct = 1.0f, cOct = 1.0f;
            [unroll] for (int oct = 0; oct < 4; oct++) {
                const float phOct = lerp(hg(cosT, -0.22f * cOct), hg(cosT, 0.72f * cOct), 0.56f);
                multiScat += exp(-odRGB * bOct) * (aOct * min(phOct, 0.42f));
                aOct *= 0.56f; bOct *= 0.34f; cOct *= 0.52f;
            }
            const float powder = 1.0f - exp(-od * sigma * 1.6f - dens * 0.45f);
            const float h = saturate((pa.z - clouds.y) / clouds.z);
            // Physical high-albedo water-droplet cloud undersides (never dark charcoal ink blots even directly overhead).
            const float cloudAo = lerp(0.56f, 1.00f, pow(h, 0.55f)) * exp(-dens * 0.10f);
            const float lowSun = saturate(1.0f - sunDir.z * 3.0f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            const float overcastW = saturate((clouds.x - 0.50f) * 2.2f);
            const float3 coolSilver = lerp(float3(0.72f, 0.82f, 0.98f), float3(0.80f, 0.85f, 0.93f), overcastW) * max(dot(skyC, float3(0.2126f, 0.7152f, 0.0722f)), 0.24f) * 1.45f;
            const float3 neutralSun = lerp(sunC, dot(sunC, float3(0.2126f, 0.7152f, 0.0722f)).xxx * float3(0.98f, 0.99f, 1.02f), overcastW * 0.65f);
            const float3 baseTint = lerp(coolSilver * cloudAo, coolSilver * (cloudAo * 0.75f) + neutralSun * lerp(0.14f, 0.05f, h), lowSun * 0.40f * (1.0f - overcastW * 0.5f));
            // Volumetric sheet-lightning inside heavy rainstorms (clouds.x > 0.78 && weather.w > 0.25).
            float3 stormFlash = 0.0f.xxx;
            if (clouds.x > 0.78f && weather.w > 0.25f) {
                const float tSlot = floor(camPosW.w * 0.55f);
                const float tFrac = frac(camPosW.w * 0.55f);
                const float pulse = exp(-tFrac * 18.0f) * step(0.88f, cHash(float3(tSlot, 3.7f, 11.3f)));
                if (pulse > 0.01f) {
                    const float2 cell = floor(pa.xy / 38000.0f);
                    const float cellHit = step(0.75f, cHash(float3(cell, tSlot)));
                    stormFlash = float3(0.65f, 0.82f, 1.25f) * (pulse * cellHit * dens * 1.8f);
                }
            }
            const float sunCalm = lerp(1.0f, 0.80f, pow(saturate(cosT), 12.0f));
            // Balanced photographic cloud-to-sky exposure: crisp snow-white sunlit tops (~1.1-1.6 linear) and cool blue-silver
            // undersides (~0.45-0.75) so clouds float cleanly in front of the cerulean Hillaire sky with zero yellow blowout.
            const float3 whiteSun = lerp(sunC, dot(sunC, float3(0.2126f, 0.7152f, 0.0722f)).xxx * float3(0.98f, 0.99f, 1.02f), 0.45f * (1.0f - lowSun));
            const float3 lum = min(whiteSun * (multiScat * lerp(1.0f, powder, 0.56f) * sunCalm * 4.0f * PI * 0.20f) + baseTint * 0.78f + stormFlash, 1.65f.xxx);
            const float ext = dens * sigma;
            const float stepT = exp(-ext * dt);
            S += T * lum * (1.0f - stepT);   // the step's in-scatter, integrated over its length
            T *= stepT;
        }
        // Clean HDR cloud radiance matched to Hillaire sky + seamless C2 horizon elevation fade.
        const float3 softS = min(S, 1.85f.xxx);
        const float fade = exp(-max(t0, 0.0f) / 185000.0f);
        const float horFade = smoothstep(0.025f, 0.165f, dir.z);
        const float effFade = saturate(fade * 1.15f) * horFade;
        const float3 horizonHaze = skyC + sunC * pow(saturate(cosT), 8.0f) * 0.05f;
        const float3 aerialS = lerp(horizonHaze * (1.0f - T), softS, fade) * horFade;
        // High-altitude wispy Cirrus cloud deck (h = 68,000u above cumulus): wind-sheared ice-crystal streaks
        // illuminated by Hillaire 2020 solar transmittance, visible through gaps in the lower cumulus deck.
        float3 cirrusAdd = 0.0f.xxx;
        if (dir.z > 0.04f && T > 0.08f) {
            const float tCir = (68000.0f - o.z) / max(dir.z, 0.04f);
            if (tCir > 0.0f && tCir < 450000.0f) {
                const float2 cPos = (o.xy + dir.xy * tCir + cloudWind.xy * camPosW.w * 1.35f) * 0.000014f;
                const float warp = cNoise(float3(cPos * 1.8f, 2.3f)) * 1.4f;
                const float streak = cNoise(float3(cPos.x * 4.2f + warp, cPos.y * 1.15f - warp * 0.5f, 5.1f));
                const float detail = cNoise(float3(cPos * 9.5f + warp, 8.7f));
                const float cMask = smoothstep(0.56f, 0.84f, streak * 0.70f + detail * 0.30f) * smoothstep(0.04f, 0.22f, dir.z);
                const float cFade = exp(-tCir / 280000.0f);
                const float3 cirCol = (sunC * (0.45f + 0.55f * hg(cosT, 0.55f) * 2.5f) + skyC * 0.35f) * 0.42f;
                cirrusAdd = cirCol * (cMask * cFade * T * horFade * (1.0f - saturate(clouds.x * 0.65f)));
            }
        }
        res = float4(aerialS + cirrusAdd, lerp(1.0f, T, effFade));
    }
    // Last frame's value along the same direction.
    const float4 c = float4(far, 1.0f);
    const float4 pc = float4(dot(prevWorldToClip[0], c), dot(prevWorldToClip[1], c), dot(prevWorldToClip[2], c), dot(prevWorldToClip[3], c));
    if (pc.w > 0.0f && proj.z > 0.5f) {
        const float2 uv = float2(pc.x / pc.w * 0.5f + 0.5f, 0.5f - pc.y / pc.w * 0.5f) / resScale.xy;
        if (all(uv > 0.0f) && all(uv < 1.0f)) {
            const float edgeFade = smoothstep(0.0f, 0.045f, min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y)));
            const float4 h = History.SampleLevel(Lin, uv, 0.0f);
            res = lerp(res, h, min(cloudWind.w, 0.68f) * edgeFade);
        }
    }
    return res;
}
