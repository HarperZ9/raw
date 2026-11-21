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
        // Sun-position-driven volumetric shafts: stronger forward anisotropy & golden-hour gain at low sun elevations.
        const float lowSunBoost = lerp(1.0f, 1.65f, saturate(1.0f - abs(sunDir.z - 0.18f) / 0.28f));
        const float dynG = clamp(lerp(tune.y, max(tune.y, 0.72f), saturate(1.0f - sunDir.z * 2.5f)), 0.1f, 0.85f);
        const float ph = min(phaseHG(dot(w, sunDir.xyz), dynG), 3.4f);
        // 16 quadratic-clustered steps with height-stratified valley density for crisp forest sunbeams.
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
            if ((flags.x & FLAG_CLOUDS) != 0u && (i & 3u) == 0u) { cs = cloudShadowN(w * t + camPosW.xyz, 1u); }
            const float hDens = 0.35f + 0.65f * exp(-max(w.z * t + 120.0f, 0.0f) * 0.0009f);
            sum += cascadeShadow(w * t, c.z, false) * cs * hDens * exp(-sigma * t) * dt;
        }
        scatter += sunColour.rgb * (sigma * ph * sum * tune2.z * lowSunBoost);
    }
    if (tune2.y > 0.0f) {
        [loop] for (uint li = 0u; li < flags.y; li++) {
            const float wi = windowIntegral(w, lightPosR[li].xyz, lightPosR[li].w, tEnd);
            if (wi > 0.0f) { scatter += lightColS[li].rgb * (sigma * wi * tune2.y / (4.0f * PI)); }
        }
    }
    // Soft highlight shoulder on volumetric in-scatter so bright sunbeams stay sculpted and never clip.
    const float scMax = max(scatter.r, max(scatter.g, scatter.b));
    scatter /= (1.0f + scMax * 0.65f);
    // With the game's distance fog still to come, extinction is the game's to apply; ours only adds light.
    const float T = (flags.x & FLAG_FOG_REPLACES) != 0u ? exp(-sigma * tEnd) : 1.0f;
    return float4(lit.rgb * T + scatter, lit.a);
}
