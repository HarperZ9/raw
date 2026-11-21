// LightFog.hlsl: volumetric fog lit by every light, after the lighting pass (LightingPass.cpp).
// A homogeneous medium of extinction sigma along each camera ray, up to the surface or a
// maximum distance:
//   sun     marched in 12 jittered steps, each shadowed by the game's cascades, with the
//           Henyey-Greenstein phase
//   lights  closed form, unshadowed: along the ray x(t) = t w the game's window 1 - |x - c|^2 / r^2
//           is a quadratic in t, so its integral over the chord inside the sphere is exact
// The result: lit * T(t_end) + in-scatter, T(t) = exp(-sigma t). Sky pixels keep the game's
// atmosphere. With FLAG_FOG off the pass copies the lit frame. Original work, MIT.
#define ATLAS_SLOT t2
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"

Texture2D<float4> Lit   : register(t0);
Texture2D<float>  Depth : register(t1);

float phaseHG(float cosT, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f), 1.5f));
}

// The integral of 1 - |x(t) - c|^2 / r^2 over t in [0, tEnd] where it is positive.
float windowIntegral(float3 w, float3 c, float r, float tEnd) {
    const float tc = dot(w, c);
    const float h2 = dot(c, c) - tc * tc;
    const float r2 = r * r;
    if (h2 >= r2) { return 0.0f; }
    const float half = sqrt(r2 - h2);
    const float sa = max(0.0f, tc - half) - tc, sb = min(tEnd, tc + half) - tc;   // s = t - tc
    if (sb <= sa) { return 0.0f; }
    return (1.0f - h2 / r2) * (sb - sa) - (sb * sb * sb - sa * sa * sa) / (3.0f * r2);
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    const float4 lit = Lit.Load(ip);
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u) { return lit; }
    const float d = Depth.Load(ip);
    if (d >= 1.0f) { return lit; }   // the sky keeps the game's own atmosphere
    const float2 px = floor(at.xy);
    const float3 p = worldAt(px, min(d, 0.999999f));
    const float dist = length(p);
    const float3 w = p / max(dist, 1.0e-4f);
    const float tEnd = min(dist, tune2.w);
    const float sigma = tune.x;

    float3 scatter = 0.0f;
    if (sunDir.w > 0.5f && tune2.z > 0.0f) {
        const float jit = ign(px);
        const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightW = saturate((0.28f - sunLum) * 4.5f);
        // Sun-position-driven volumetric shafts by day, and crisp silver-blue lunar moonbeams at night.
        const float lowSunBoost = lerp(1.0f, 1.18f, saturate(1.0f - abs(sunDir.z - 0.18f) / 0.28f));
        const float shaftBoost = lerp(lowSunBoost, 3.10f, nightW);
        const float dayG = clamp(lerp(tune.y, max(tune.y, 0.70f), saturate(1.0f - sunDir.z * 2.5f)), 0.1f, 0.82f);
        const float dynG = lerp(dayG, 0.76f, nightW);
        const float ph = min(phaseHG(dot(w, sunDir.xyz), dynG), 3.4f);
        // 16 quadratic-clustered steps with height-stratified valley density for crisp forest sunbeams & moonbeams.
        const uint kSteps = 16u;
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
            if ((flags.x & FLAG_CLOUDS) != 0u && (i & 3u) == 0u) { cs = cloudShadowN(wpFog, 1u); }
            // 3D world-space wind-drifting volumetric valley mist density (living rolling fog banks).
            const float3 fogFlow = wpFog * 0.00055f + float3(cloudWind.xy * camPosW.w * 0.00018f, 0.0f);
            const float mist3d = 0.62f + 0.58f * cNoise(fogFlow);
            const float hDens = (0.35f + 0.65f * exp(-max(w.z * t + 120.0f, 0.0f) * 0.0009f)) * mist3d;
            sum += cascadeShadow(w * t, c.z, false) * cs * hDens * exp(-sigma * t) * dt;
        }
        const float3 shaftCol = lerp(sunColour.rgb, max(sunColour.rgb, float3(0.22f, 0.31f, 0.48f)), nightW * 0.72f);
        scatter += shaftCol * (sigma * ph * sum * tune2.z * shaftBoost);
    }
    if (tune2.y > 0.0f) {
        // Enhanced volumetric point-light mist halos around torches, campfires, braziers, and spells (stronger in rain/snow/fog).
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
    // Soft highlight shoulder on volumetric in-scatter so bright sunbeams stay sculpted and never clip.
    const float scMax = max(scatter.r, max(scatter.g, scatter.b));
    scatter /= (1.0f + scMax * 0.65f);
    // Unified sky- and weather-coupled exponential height fog & alpine mountain mist (replaces Skyrim's flat 2011 metric fog).
    if ((flags.x & FLAG_FOG_REPLACES) != 0u && sunColour.w > 0.5f) {
        const float weatherDens = 1.0f + weather.w * 2.2f + weather.z * 2.6f + saturate(clouds.x - 0.35f) * 1.1f;
        const float baseSigma = 0.0000135f * weatherDens;
        // Smooth C2 elevation weight across w.z = 0 (eliminates the horizontal midpoint fog band at eye horizon).
        const float hStart = max(camPosW.z * 0.00025f, 0.0f);
        const float elevW = lerp(1.12f, 0.44f, smoothstep(-0.15f, 0.30f, w.z));
        // 3D rolling alpine mist & high-altitude mountain-collar cloud band (replaces static billboard fog planes).
        const float3 midPt = camPosW.xyz + w * min(dist * 0.55f, 45000.0f);
        const float3 windShift = float3(cloudWind.xy * camPosW.w * 0.00006f, 0.0f);
        const float alpineNoise = cNoise(midPt * 0.00014f + windShift) * 0.65f + cNoise(midPt * 0.00042f - windShift * 1.6f + 7.1f) * 0.35f;
        const float peakCollar = exp(-pow((midPt.z - 5200.0f) / 3600.0f, 2.0f)) * smoothstep(8000.0f, 28000.0f, dist);
        const float alpineMist = (0.76f + 0.48f * alpineNoise) * (1.0f + peakCollar * (0.45f + 0.55f * alpineNoise));
        const float od = baseSigma * min(dist, 240000.0f) * elevW * alpineMist * exp(-hStart * 0.15f);
        const float T = exp(-od);
        // Atmospheric in-scatter color locked 100% to live skyPalette() + Mie forward sun glow + night indigo.
        float3 sZ, sM, sH;
        skyPalette(sZ, sM, sH);
        const float sunLumF = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightF = saturate((0.26f - sunLumF) * 4.5f);
        const float lowSunF = saturate(1.0f - sunDir.z * 2.6f);
        const float cosSunF = saturate(dot(w, sunDir.xyz));
        float3 atmoCol = lerp(sH * 0.78f, sM * 0.85f, smoothstep(-0.08f, 0.35f, w.z));
        const float3 mieCol = lerp(float3(1.0f, 0.94f, 0.84f), float3(1.0f, 0.60f, 0.30f), lowSunF);
        atmoCol += mieCol * (pow(cosSunF, 8.0f) * 0.22f * (1.0f - saturate(clouds.x * 0.75f)));
        // Snowstorm frosted whiteout vs night indigo-blue alpine haze.
        atmoCol = lerp(atmoCol, float3(0.56f, 0.62f, 0.70f), weather.z * 0.65f);
        atmoCol = lerp(atmoCol, float3(0.05f, 0.09f, 0.19f), nightF);
        return float4(lit.rgb * T + atmoCol * (1.0f - T) + scatter, lit.a);
    }
    const float T = (flags.x & FLAG_FOG_REPLACES) != 0u ? exp(-sigma * tEnd) : 1.0f;
    return float4(lit.rgb * T + scatter, lit.a);
}
