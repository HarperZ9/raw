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
    const float2 full = (lo + 0.5f) * 4.0f;   // the full-resolution pixel this one stands for
    const float3 far = worldAt(full - 0.5f, 1.0f);
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
        // Dual-lobe Henyey-Greenstein: crisp forward silver lining (g=0.74) + soft warm back-scatter (g=-0.28).
        const float phase = min(lerp(hg(cosT, -0.28f), hg(cosT, 0.74f), 0.52f), 0.44f);
        const float3 sunC = sunColour.rgb * (sunDir.w > 0.5f ? 1.0f : 0.0f);
        const float3 skyC = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.0f);
        const float sigma = kCloudSigma;
        float T = 1.0f;
        float3 S = 0.0f;
        // Exponential sun-march offsets capture both crisp local cauliflower self-shadowing and deep storm underbellies.
        static const float kSunD[6] = { 180.0f, 440.0f, 920.0f, 1750.0f, 3200.0f, 5600.0f };
        static const float kSunW[6] = { 180.0f, 260.0f, 480.0f,  830.0f, 1450.0f, 2400.0f };
        [loop] for (uint i = 0u; i < steps && T > 0.01f; i++) {
            const float t = t0 + (float(i) + j) * dt;
            const float3 pa = o + dir * t;
            const float dens = cloudDensity(pa, true);
            if (dens <= 0.0f) { continue; }
            float od = 0.0f;
            [unroll] for (uint k = 0u; k < 6u; k++) { od += cloudDensity(pa + sunDir.xyz * kSunD[k], false) * kSunW[k]; }
            // Multi-octave scattering (Hillaire 2020) + deep Beer-Powder crease contrast.
            const float beer = (exp(-od * sigma) + 0.52f * exp(-od * sigma * 0.25f) + 0.26f * exp(-od * sigma * 0.0625f)) / 1.78f;
            const float powder = 1.0f - exp(-od * sigma * 2.2f - dens * 0.65f);
            const float h = saturate((pa.z - clouds.y) / clouds.z);
            // Ambient occlusion inside cloud undersides gives dramatic blue-slate storm depth vs crisp lit ridges.
            const float cloudAo = lerp(0.24f, 0.82f, h) * exp(-dens * 0.38f);
            const float lowSun = saturate(1.0f - sunDir.z * 3.0f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            // Keep cool zenith-blue cloud undersides even at sunset so clouds have dramatic gold-rim vs slate-belly contrast (no muddy peach wash).
            const float3 coolBellies = skyC * float3(0.88f, 0.94f, 1.12f) * cloudAo;
            const float3 baseTint = lerp(coolBellies, coolBellies * 0.72f + sunC * lerp(0.16f, 0.06f, h), lowSun * 0.45f);
            const float3 lum = min(sunC * (beer * lerp(1.0f, powder, 0.58f) * phase * 4.0f * PI * 0.32f) + baseTint, 1.18f.xxx);
            const float ext = dens * sigma;
            const float stepT = exp(-ext * dt);
            S += T * lum * (1.0f - stepT);   // the step's in-scatter, integrated over its length
            T *= stepT;
        }
        // Soft filmic shoulder + seamless atmospheric horizon aerial perspective blend (no hard cloud cutoff near mountains).
        const float3 softS = S / (1.0f + S * 0.52f);
        const float fade = exp(-max(t0, 0.0f) / 185000.0f);
        const float3 horizonHaze = skyC * 0.58f + sunC * pow(saturate(cosT), 6.0f) * 0.10f;
        const float3 aerialS = lerp(horizonHaze * (1.0f - T), softS, fade);
        res = float4(aerialS, lerp(1.0f, T, saturate(fade * 1.15f)));
    }
    // Last frame's value along the same direction.
    const float4 c = float4(far, 1.0f);
    const float4 pc = float4(dot(prevWorldToClip[0], c), dot(prevWorldToClip[1], c), dot(prevWorldToClip[2], c), dot(prevWorldToClip[3], c));
    if (pc.w > 0.0f && proj.z > 0.5f) {
        const float2 uv = float2(pc.x / pc.w * 0.5f + 0.5f, 0.5f - pc.y / pc.w * 0.5f) / resScale.xy;
        if (all(uv > 0.0f) && all(uv < 1.0f)) {
            const float4 h = History.SampleLevel(Lin, uv, 0.0f);
            res = lerp(res, h, cloudWind.w);
        }
    }
    return res;
}
