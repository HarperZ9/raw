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

Texture2D<float4> History : register(t0);
SamplerState      Lin     : register(s0);

float hg(float c, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * c, 1.0e-4f), 1.5f));
}

float rand(float2 p, float s) { return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f)) + s * 0.61803399f)); }

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const float2 lo = floor(at.xy);
    const float2 full = (lo + 0.5f) * 2.0f;   // 1/2-res volumetric clouds (4x sharper than 1/4-res)
    const float3 far = worldAt(full - 0.5f, 0.995f) - eyeAt();
    const float3 dir = normalize(far);
    float4 res = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float t0, t1;
    const float3 o = camPosW.xyz;
    const float maxDist = 400000.0f;
    if (cloudsOn() && cloudSpan(o, dir, maxDist, t0, t1)) {
        const uint steps = 64u;
        const float dt = (t1 - t0) / float(steps);
        const float j = rand(lo, proj.z);
        const float cosT = dot(dir, sunDir.xyz);
        // Balanced dual-lobe Henyey-Greenstein: crisp silver lining without blowing into clipped white blobs.
        const float phase = min(lerp(hg(cosT, -0.25f), hg(cosT, 0.65f), 0.48f), 0.24f);
        const float3 rawSunC = sunColour.rgb * (sunDir.w > 0.5f ? 1.0f : 0.0f);
        const float sLum = dot(rawSunC, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightW = saturate((0.28f - sLum) * 4.5f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
        // Silver-blue lunar illumination on night clouds so clouds passing Masser & Secunda catch crisp moonlight rims.
        const float3 sunC = lerp(rawSunC, max(rawSunC, float3(0.24f, 0.34f, 0.54f)) * 1.65f, nightW * 0.78f);
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
        [loop] for (uint i = 0u; i < steps && T > 0.01f; i++) {
            const float t = t0 + (float(i) + j) * dt;
            const float3 pa = o + dir * t;
            const float dens = cloudDensity(pa, true);
            if (dens <= 0.0f) { continue; }
            float od = 0.0f;
            [unroll] for (uint k = 0u; k < 6u; k++) { od += cloudDensity(pa + sunDir.xyz * kSunD[k], false) * kSunW[k]; }
            // Multi-octave scattering (Hillaire 2020) + balanced Beer-Powder relief.
            const float beer = (exp(-od * sigma) + 0.48f * exp(-od * sigma * 0.25f) + 0.22f * exp(-od * sigma * 0.0625f)) / 1.70f;
            const float powder = 1.0f - exp(-od * sigma * 1.8f - dens * 0.55f);
            const float h = saturate((pa.z - clouds.y) / clouds.z);
            const float cloudAo = lerp(0.36f, 0.92f, h) * exp(-dens * 0.28f);
            const float lowSun = saturate(1.0f - sunDir.z * 3.0f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            const float3 baseTint = lerp(skyC * cloudAo, skyC * (cloudAo * 0.78f) + sunC * lerp(0.14f, 0.05f, h), lowSun * 0.40f);
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
            const float3 lum = min(sunC * (beer * lerp(1.0f, powder, 0.52f) * phase * 4.0f * PI * 0.34f) + baseTint + stormFlash, 0.96f.xxx);
            const float ext = dens * sigma;
            const float stepT = exp(-ext * dt);
            S += T * lum * (1.0f - stepT);   // the step's in-scatter, integrated over its length
            T *= stepT;
        }
        // Soft filmic shoulder + seamless atmospheric horizon aerial perspective blend.
        const float3 softS = S / (1.0f + S * 0.72f);
        const float fade = exp(-max(t0, 0.0f) / 185000.0f);
        const float3 horizonHaze = skyC * 0.52f + sunC * pow(saturate(cosT), 8.0f) * 0.06f;
        const float3 aerialS = lerp(horizonHaze * (1.0f - T), softS, fade);
        res = float4(aerialS, lerp(1.0f, T, saturate(fade * 1.15f)));
    }
    // Last frame's value along the same direction.
    const float4 c = float4(far, 1.0f);
    const float4 pc = float4(dot(prevWorldToClip[0], c), dot(prevWorldToClip[1], c), dot(prevWorldToClip[2], c), dot(prevWorldToClip[3], c));
    if (pc.w > 0.0f && proj.z > 0.5f) {
        const float2 uv = float2(pc.x / pc.w * 0.5f + 0.5f, 0.5f - pc.y / pc.w * 0.5f) / resScale.xy;
        if (all(uv > 0.0f) && all(uv < 1.0f)) {
            const float edgeFade = smoothstep(0.0f, 0.045f, min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y)));
            const float4 h = History.SampleLevel(Lin, uv, 0.0f);
            res = lerp(res, h, cloudWind.w * edgeFade);
        }
    }
    return res;
}
