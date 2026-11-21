// Lighting.hlsl: the engine's deferred lighting of a Skyrim frame (LightingPass.cpp,
// docs/LIGHTING.md). One full-screen pass over the game's G-buffer and RAW's patched targets.
//
// Match mode reproduces the game's own lit shader, read from its disassembly (docs/LIGHTING.md,
// "The game's shading"): Lambert diffuse and an unnormalised Blinn-Phong lobe from the sun and
// up to seven point lights with the window 1 - (d / r)^2, the directional ambient, albedo times
// the diffuse sum, the specular term times the mask, and the game's soft knee twice.
// Improved mode keeps the light list and adds, each behind its own flag: GGX with
// height-correlated Smith masking and Schlick Fresnel (energy conserving, Lambert scaled by
// 1 - F), screen-space soft shadows for every point light, contact shadows for the sun, and
// the engine's real-time AO on the ambient term. The fog runs after (LightFog.hlsl).
// Original work, MIT.
#include "LightingCommon.hlsli"
#include "Weather.hlsli"
#include "CloudCommon.hlsli"

Texture2D<float>  Depth   : register(t0);
Texture2D<float4> Normal  : register(t1);
Texture2D<float4> Albedo  : register(t2);
Texture2D<float4> Spec    : register(t3);
Texture2D<float4> Mask    : register(t4);
Texture2D<float4> AoHist  : register(t6);
Texture2D<float4> GameLit : register(t7);
Texture2D<float4> SoftMap : register(t8);
Texture2D<float4> RtVis   : register(t9);    // RtShadow.hlsl: sun and three lights' ray-traced visibility
Texture2D<float4> RtBounce : register(t11);  // RtSky.cpp: one bounce of indirect light (exteriors)
Texture2D<float4> RtRefl   : register(t12);  // RtShadow.hlsl: reflected radiance, confidence
Texture2D<float4> Clouds  : register(t13);  // Clouds.hlsl: the clouds' light, what is left of the sky (quarter size)
SamplerState      Lin     : register(s0);
Texture2D<float4> RtIdx   : register(t10);   // which lights, index + 1 over 255   // RAW's emissive target: for soft-lit draws the game's soft-lighting colour

uint clusterOf(float2 px, float z) {
    const uint tx = min(uint(px.x * dims.z * TILES_X), TILES_X - 1u);
    const uint ty = min(uint(px.y * dims.w * TILES_Y), TILES_Y - 1u);
    const float sl = log(max(z, cluster.x) / cluster.x) * cluster.z;
    return (min(uint(max(sl, 0.0f)), SLICES - 1u) * TILES_Y + ty) * TILES_X + tx;
}

// Visibility along a segment from p, by marching the depth buffer: 0 where the ray passes
// behind a surface by less than a thickness, a soft ratio of clearance to distance otherwise
// (the penumbra of a source of angular size `cone`).
float screenShadow(float3 p, float3 dir, float len, float cone, float jitter, uint steps) {
    float vis = 1.0f;
    for (uint i = 0u; i < steps; i++) {
        const float t = (float(i) + jitter) / float(steps) * len;
        const float4 s = screenOf(p + dir * t);
        if (s.w <= 1.0f || any(s.xy < 0.0f) || s.x >= dims.x || s.y >= dims.y) { break; }
        const float zs = viewZOf(Depth.Load(int3(s.xy, 0)));
        const float dz = s.w - zs;   // > 0: the ray point lies behind the surface seen there
        const float bias = 0.004f * s.w + 0.5f;
        if (dz > bias && dz < max(12.0f, 0.04f * s.w)) {
            // Foliage does not occlude here: twigs and leaves are a few pixels thick in the depth
            // buffer, read as solid walls and drew hard streaks on the ground and rocks under them.
            // The game's own shadow mask already carries their shadow.
            const uint hitClass = uint(Spec.Load(int3(s.xy, 0)).z * 255.0f + 0.5f) & 127u;
            if (hitClass != 5u) { return 0.0f; }
        }
        if (dz < 0.0f) { vis = min(vis, saturate(-dz / max(t * cone, 1.0e-3f))); }
    }
    return vis;
}

float knee(float x) { return min(x, (1.0f - resScale.z) * x + 1.0f); }
float3 knee3(float3 x) { return float3(knee(x.r), knee(x.g), knee(x.b)); }

struct Surface { float3 n, v; float shin, mask, f0, alpha; float3 soft; float3 wrap; };

float smooth01(float x) { return x * x * (3.0f - 2.0f * x); }

// One light of colour c (windowed and shadowed; cFull windowed only) from direction l. Soft-lit
// surfaces (foliage) add the game's wrap term, smoothstep((n.l + r) / (1 + r)) - smoothstep(n.l),
// from the unshadowed colour; the game scales it by a texture the G-buffer does not carry, so a
// fitted constant (s.soft) stands in (docs/LIGHTING.md).
// Jimenez 2016 multi-bounce colored AO approximation: replaces muddy grey crevices with physical colored inter-reflection.
float3 gtaoMultiBounce(float vis, float3 alb) {
    const float3 a =  2.0404f * alb - 0.3324f;
    const float3 b = -4.7951f * alb + 0.6417f;
    const float3 c =  2.7552f * alb + 0.6903f;
    return max(vis.xxx, ((vis * a + b) * vis + c) * vis);
}

void addLight(Surface s, float3 l, float3 c, float3 cFull, inout float3 diff, inout float3 spec) {
    const float x = dot(s.n, l), nl = saturate(x);
    // Skin: light that enters the skin comes out past the terminator, red farthest (a per-channel wrap,
    // normalised so the lit side keeps its energy).
    const float3 nlD = any(s.wrap > 0.0f) ? saturate((x + s.wrap) / (1.0f + s.wrap)) / (1.0f + s.wrap) * (1.0f + s.wrap * 0.5f) : nl.xxx;
    if (any(s.soft > 0.0f)) { diff += s.soft * saturate(smooth01(saturate((x + 1.0f) * 0.5f)) - smooth01(nl)) * cFull; }
    const float3 h = normalize(s.v + l);
    const float nh = saturate(dot(s.n, h));
    if ((flags.x & FLAG_GGX) == 0u) {
        diff += nlD * c;
        spec += pow(nh, s.shin) * c;   // the game's lobe: no cosine, no normalisation
        return;
    }
    const float nv = max(dot(s.n, s.v), 1.0e-4f), a2 = s.alpha * s.alpha;
    const float dd = nh * nh * (a2 - 1.0f) + 1.0f;
    const float D = a2 / (PI * dd * dd);
    const float V = 0.5f / (nl * sqrt(nv * nv * (1.0f - a2) + a2) + nv * sqrt(nl * nl * (1.0f - a2) + a2) + 1.0e-6f);
    const float F = s.f0 + (1.0f - s.f0) * pow(1.0f - saturate(dot(s.v, h)), 5.0f);
    diff += nlD * c * (1.0f - F);
    spec += PI * D * V * F * nl * c;   // the game's light colour is irradiance over pi
}

#include "DistantLod.hlsli"   // distantLit: distant LOD lit in improved mode (FOL-04)

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    const float4 game = GameLit.Load(ip);
    const float d = Depth.Load(ip);
    if ((flags.x & FLAG_SURFACE) == 0u) { return game; }
    if (d >= 1.0f) {
        // The sky: physically grounded Perez/Preetham atmosphere, dynamic sun-elevation horizon glow,
        // limb-darkened solar corona attenuated by live cloud transmittance, and curved volumetric clouds.
        if (flags.w == 10u) { return float4(1.0f, 0.0f, 1.0f, game.a); }
        float3 baseSky = game.rgb;
        const float3 sDir = normalize(worldAt(floor(at.xy), 1.0f));
        if ((flags.x & FLAG_MATCH) == 0u) {
            baseSky = baseSky / (1.0f + max(max(baseSky.r, max(baseSky.g, baseSky.b)) - 0.72f, 0.0f) * 0.55f);
            const float dayW = saturate(sunDir.z * 3.5f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            const float sunsetW = saturate(1.0f - abs(sunDir.z - 0.14f) / 0.22f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            if (dayW > 0.02f && sDir.z > -0.02f) {
                const float turb = lerp(2.5f, 5.5f, saturate(clouds.x));
                const float3 pSky = preethamSky(sDir, sunDir.xyz, turb);
                const float skyLum = max(dot(baseSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.08f);
                const float pLum = max(dot(pSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.08f);
                baseSky = lerp(baseSky, pSky * (skyLum / pLum), dayW * (1.0f - saturate(clouds.x * 0.75f)) * 0.28f);
            }
            // Dynamic sun-elevation horizon scatter (warm amber/rose twilight band at dawn/dusk).
            if (sunsetW > 0.01f && sDir.z > -0.05f) {
                const float hBand = pow(1.0f - saturate(sDir.z), 3.8f);
                const float sunAz = pow(saturate(dot(normalize(float3(sDir.xy, 0.0f)), normalize(float3(sunDir.xy, 0.001f)))), 2.5f);
                const float3 warmBand = float3(0.85f, 0.42f, 0.20f) * (0.25f + 0.75f * sunAz) * hBand * sunsetW * (1.0f - saturate(clouds.x * 0.5f)) * 0.32f;
                baseSky += warmBand;
            }
        }
        const float4 c = (flags.x & FLAG_CLOUDS) != 0u ? Clouds.SampleLevel(Lin, (at.xy) * dims.zw, 0.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);
        float3 skyOut = baseSky * c.a + c.rgb;
        if ((flags.x & FLAG_MATCH) == 0u && sunDir.w > 0.5f && sunDir.z > 0.0f) {
            // Dynamic solar aureole attenuated by cloud transmittance c.a (silver-lined corona through thin clouds).
            const float cosSun = saturate(dot(sDir, sunDir.xyz));
            const float corona = pow(cosSun, 96.0f) * 0.42f + pow(cosSun, 14.0f) * 0.08f;
            skyOut += sunColour.rgb * corona * (0.25f + 0.75f * c.a) * saturate(sunDir.z * 6.0f);
        }
        return float4(skyOut, game.a);
    }
    // View 9, a test control: the pass reaches only the top-left quarter, as a half-size viewport
    // would. The coverage check in the capture must fail on it.
    if (flags.w == 9u && (at.x >= dims.x * 0.5f || at.y >= dims.y * 0.5f)) { return game; }
    const float4 sp = Spec.Load(ip);
    const uint cls = uint(sp.z * 255.0f + 0.5f), base = cls & 127u;
    // Effects, water, first person and distant LOD (lit per vertex by the game): the game's own.
    if (flags.w == 12u) {   // view 12: the material class (skin red, foliage green, landscape brown, parallax blue, the rest grey)
        const float3 cc = base == 10u ? float3(1, 0.1, 0.1) : base == 5u ? float3(0.1, 0.8, 0.1) : base == 4u ? float3(0.5, 0.35, 0.1) :
                          base == 6u ? float3(0.1, 0.3, 1) : base == 7u ? float3(0.1, 0.9, 0.9) : base == 0u ? 0.0f.xxx : 0.5f.xxx;
        return float4(cc * (0.35f + 0.65f * dot(GameLit.Load(ip).rgb, 0.333f)), game.a);
    }
    // Distant LOD (class 7: lit per vertex by the game, so it read flat beside the trees and ground RAW lights).
    // FOL-04: in improved mode it gets the same sun, shadows, ambient and sky the near surfaces get. Views 14 and 15
    // show its G-buffer normal and base colour, the rest of the frame dimmed.
    if (base == 7u && (cls & 128u) == 0u) {
        if (flags.w == 14u) { return float4(worldNormal(Normal.Load(ip)) * 0.5f + 0.5f, game.a); }
        if (flags.w == 15u) { return float4(Albedo.Load(ip).rgb, game.a); }
        // Views 2, 4 and 5 on distant LOD: the sun, sky and bounce of the march through the depth (RtShadow.hlsl, RtFar).
        if (flags.w == 2u) { return float4(RtVis.Load(ip).xxx, game.a); }
        if (flags.w == 4u) { return float4(RtIdx.Load(ip).www, game.a); }
        if (flags.w == 5u) { return float4(RtBounce.Load(ip).rgb * proj.w, game.a); }
        if ((flags.x & FLAG_MATCH) != 0u || (flags.x & FLAG_LOD) == 0u || sunDir.w < 0.5f) { return game; }
        return float4(distantLit(ip, at.xy, d), game.a);
    }
    if (flags.w == 14u || flags.w == 15u) { return float4(game.rgb * 0.2f, game.a); }
    const bool skin = base == 10u;
    if (base == 0u || (base >= 7u && !skin) || (cls & 128u) != 0u) { return game; }

    const bool match = (flags.x & FLAG_MATCH) != 0u;
    const float2 px = floor(at.xy);
    const float3 p = worldAt(px, d);
    float3 albedo = Albedo.Load(ip).rgb;
    const float z = viewZOf(d), jit = ign(px);
    Surface s;
    s.n = worldNormal(Normal.Load(ip));
    s.v = normalize(eyeAt() - p);
    s.shin = exp2(sp.y * 13.0f);
    s.mask = sp.x;
    bool lobe = sp.y > 0.002f && base != 4u && base != 6u;   // the game's own shader had a lobe here
    s.f0 = lobe ? lerp(0.02f, 0.08f, saturate(sp.x * resScale.w)) : 0.04f;
    s.alpha = clamp(sqrt(2.0f / (s.shin + 2.0f)), 0.03f, 1.0f);   // Blinn-Phong exponent to GGX width
    // Soft-lit (foliage): the game multiplies its wrap term by the soft-lighting map, which the
    // patched shader writes to RAW's emissive target. Before that channel existed a grey constant
    // fitted on one frame (0.15, tools/lighting/fitsoft.py) stood in; it lost the warm back light.
    // Skin too: the body and hands shaders read the same soft-lighting map at t12 (the patch writes it to the emissive
    // target), and without it the arms read darker than the game's (rt6-tm-1).
    s.soft = base == 5u || skin ? max(SoftMap.Load(ip).rgb, 0.0f.xxx) : 0.0f.xxx;   // negative: the character light (below)
    // Softer since the hands and bodies joined the class (rt6-wat-3): at 0.55 / 0.22 / 0.12 the arms read orange
    // against the game's brown.
    s.wrap = skin && !match ? float3(0.30f, 0.12f, 0.06f) : 0.0f.xxx;
    if (skin && !match) { s.f0 = 0.028f; s.alpha = max(s.alpha, 0.38f); }   // skin's own specular: f0 of 0.028, a soft sheen
    if (base == 5u && !any(s.soft > 0.0f)) { s.soft = 0.15f.xxx; }   // a soft-lit shader the patch found no map in
    // Rain (Weather.hlsli), exteriors in improved mode: darker, glossier, puddles flat and mirror-like.
    Wet wet;
    wet.wet = 0.0f; wet.puddle = 0.0f; wet.snow = 0.0f;
    float2 rings = 0.0f;   // rain on the puddles: the water's slope, which also bends the reflection
    if (!match && sunColour.w > 0.5f && base != 5u) {
        const float sv = (flags.x & FLAG_RT) != 0u && (flags.x & FLAG_SKY) != 0u ? RtIdx.Load(ip).w : 1.0f;
        // The geometric normal from the depth buffer's neighbours (shorter step on each axis).
        const float2 lim = dims.xy - 1.0f;
        const float2 pr = float2(min(px.x + 1.0f, lim.x), px.y), pl = float2(max(px.x - 1.0f, 0.0f), px.y);
        const float2 pd = float2(px.x, min(px.y + 1.0f, lim.y)), pu = float2(px.x, max(px.y - 1.0f, 0.0f));
        const float3 er = worldAt(pr, Depth.Load(int3(pr, 0))) - p, el = p - worldAt(pl, Depth.Load(int3(pl, 0)));
        const float3 ed = worldAt(pd, Depth.Load(int3(pd, 0))) - p, eu = p - worldAt(pu, Depth.Load(int3(pu, 0)));
        float3 ng = cross(dot(ed, ed) < dot(eu, eu) ? ed : eu, dot(er, er) < dot(el, el) ? er : el);
        ng = length(ng) > 1.0e-6f ? normalize(ng) : s.n;
        if (dot(ng, s.n) < 0.0f) { ng = -ng; }
        wet = wetAt(p, s.n, ng, sv);
        // Water fills the pores and standing water hides the ground's own light. At 0.6 and 0.18 the rain frame's
        // lower half read 23 of 255 against the game's 50 (rt5-wx-5, Riverwood under 10a241): too dark for an overcast
        // scene lit mostly by its ambient.
        albedo *= lerp(1.0f, 0.75f, wet.wet) * lerp(1.0f, 0.4f, wet.puddle);
        s.alpha = lerp(lerp(s.alpha, 0.12f, wet.wet), 0.03f, wet.puddle);
        s.shin = max(s.shin, 2.0f / max(s.alpha * s.alpha, 1.0e-4f) - 2.0f);
        s.n = normalize(lerp(s.n, float3(0.0f, 0.0f, 1.0f), wet.puddle));
        if (wet.puddle > 0.0f && weather.w > 0.0f && z < 2500.0f) {
            rings = rainRings((p + camPosW.xyz).xy, camPosW.w) * (0.35f * weather.w * wet.puddle * saturate(2.0f - z / 1250.0f));
            s.n = normalize(s.n + float3(-rings, 0.0f));
        }
        s.f0 = lerp(s.f0, 0.02f, max(wet.wet, wet.puddle));
        s.mask = max(s.mask, max(wet.wet * 0.6f, wet.puddle));
        lobe = lobe || max(wet.wet, wet.puddle) > 0.2f;
        if (wet.snow > 0.0f) {
            // Snow lies over everything below it: bright, rough, its own smooth shape over the ground's bumps.
            // The game lights in its own (gamma) space, where its ground textures sit near 0.3. Full sun on 0.8
            // met the knee and read flat white; 0.68 still clipped in the game's tone map (Riverwood 13:00,
            // 2026-11-19). About 0.56 keeps the drifts' shading; RAW's filmic tone map rolls the rest off.
            albedo = lerp(albedo, float3(0.53f, 0.56f, 0.60f), wet.snow);
            s.alpha = lerp(s.alpha, 0.55f, wet.snow);
            s.shin = lerp(s.shin, 2.0f / (0.55f * 0.55f) - 2.0f, wet.snow);
            s.n = normalize(lerp(s.n, ng, wet.snow * 0.4f));   // the ground's bumps show through as mounds
            s.f0 = lerp(s.f0, 0.02f, wet.snow);
            s.mask = lerp(s.mask, 0.15f, wet.snow);
            wet.puddle *= 1.0f - wet.snow;
            wet.wet *= 1.0f - wet.snow;
        }
    }
    const float3 nSmooth = s.n;
    // Zero-mod procedural PBR micro-normal & roughness synthesis on vanilla stone/wood (attenuated on class-2 armor so reflections stay clean).
    if (!match && !skin && base != 5u && z < 2800.0f) {
        const float distFade = saturate(1.0f - z / 2800.0f) * (base == 2u ? 0.22f : 1.0f);
        const int2 dimMax = max(int2(dims.xy) - int2(1, 1), int2(0, 0));
        const int3 ipL = int3(clamp(int2(px) + int2(-1, 0), int2(0, 0), dimMax), 0);
        const int3 ipR = int3(clamp(int2(px) + int2( 1, 0), int2(0, 0), dimMax), 0);
        const int3 ipU = int3(clamp(int2(px) + int2( 0,-1), int2(0, 0), dimMax), 0);
        const int3 ipD = int3(clamp(int2(px) + int2( 0, 1), int2(0, 0), dimMax), 0);
        const float3 lw = float3(0.2126f, 0.7152f, 0.0722f);
        const float hc = dot(albedo, lw);
        const float hL = abs(Depth.Load(ipL) - d) < 0.0008f ? dot(Albedo.Load(ipL).rgb, lw) : hc;
        const float hR = abs(Depth.Load(ipR) - d) < 0.0008f ? dot(Albedo.Load(ipR).rgb, lw) : hc;
        const float hU = abs(Depth.Load(ipU) - d) < 0.0008f ? dot(Albedo.Load(ipU).rgb, lw) : hc;
        const float hD = abs(Depth.Load(ipD) - d) < 0.0008f ? dot(Albedo.Load(ipD).rgb, lw) : hc;
        const float gx = clamp((hR - hL) * 2.2f, -0.45f, 0.45f) * distFade;
        const float gy = clamp((hD - hU) * 2.2f, -0.45f, 0.45f) * distFade;
        const float3 upRef = abs(s.n.z) < 0.92f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
        const float3 tVec = normalize(cross(upRef, s.n));
        const float3 bVec = cross(s.n, tVec);
        s.n = normalize(s.n + (-gx * tVec - gy * bVec) * 0.42f);
        const float microSlope = saturate(length(float2(gx, gy)) * 1.4f);
        s.alpha = clamp(s.alpha + (microSlope - 0.15f) * 0.12f * distFade, 0.05f, 0.95f);
    }
    // Tokuyoshi-Kaplanyan geometric specular anti-aliasing: filters sub-pixel normal variance to kill GGX sparkle in motion.
    if (!match) {
        const float3 dnx = ddx(s.n), dny = ddy(s.n);
        const float specVar = min(0.45f * (dot(dnx, dnx) + dot(dny, dny)), 0.16f);
        s.alpha = sqrt(saturate(s.alpha * s.alpha + specVar));
    }
    const float3 lift = s.n * (0.3f + 0.002f * z);                 // ray origins off the surface
    const float4 m4 = Mask.Load(ip);

    const bool rt = !match && (flags.x & FLAG_RT) != 0u;
    const float4 rv = rt ? RtVis.Load(ip) : 1.0f.xxxx;
    const uint3 rIdx = rt ? uint3(RtIdx.Load(ip).xyz * 255.0f + 0.5f) : uint3(0u, 0u, 0u);
    // Every light past the three traced ones: the share of their light that reaches the pixel, from one ray a
    // pixel to a light picked by its share, filtered with the sky rays (RtShadow.hlsl, RtSkyFilter.hlsl).
    const float restVis = rt && (flags.x & FLAG_REST) != 0u ? RtBounce.Load(ip).a : 1.0f;
    float3 diff = 0.0f, spec = 0.0f;
    float sunShadow = 1.0f;
    if (sunDir.w > 0.5f) {
        // Interiors have no cascades; channel 0 of their mask belongs to a shadowed point light, so the
        // directional light there is unshadowed, as the game lights it.
        sunShadow = (flags.x & FLAG_SHADOWS) == 0u ? 1.0f : (flags.x & FLAG_CASCADES) != 0u ? cascadeShadow(p, d, true) : m4.x;
        // Not on soft-lit foliage: thin leaves and twigs read as self-occluding in the depth buffer
        // and went black (riverwood_day bush 0.267 -> 0.232 in luminance).
        if (!match && (flags.x & FLAG_CONTACT) != 0u && base != 5u && sunShadow > 0.0f && dot(s.n, sunDir.xyz) > 0.0f)
            sunShadow *= screenShadow(p + lift, sunDir.xyz, tune.w, 0.02f, jit, 12u);
        // The game's mask keeps what the scene lacks (skinned actors). Interiors (no cascades): the
        // directional light is the cell's fill, unshadowed as the game lights it; rays would cut it
        // into window shafts and darken the room.
        // Not on skin: the skinned actors' own triangles still cast blocky self-shadows in the rays (the open actor
        // sheet), so skin keeps the game's mask, which shadows actors correctly.
        if (rt && sunColour.w > 0.5f && !skin) { sunShadow = min(sunShadow, base == 5u ? lerp(1.0f, rv.x, 0.6f) : rv.x); }
        if (!match && sunColour.w > 0.5f && (flags.x & FLAG_CLOUDS) != 0u) { sunShadow *= cloudShadow(p + camPosW.xyz); }   // SKY-02
        // Skin: the shadow's edge turns red where light scattered under the surface reaches past it.
        const float3 sunSh = skin && !match ? pow(saturate(sunShadow).xxx, float3(0.75f, 0.95f, 1.05f)) : sunShadow.xxx;
        addLight(s, sunDir.xyz, sunColour.rgb * sunSh, sunColour.rgb, diff, spec);
        if (wet.snow > 0.0f && sunShadow > 0.0f)
            spec += sunColour.rgb * (sunShadow * wet.snow * 3.0f * snowGlint(p + camPosW.xyz, s.n, s.v, sunDir.xyz, max(1.2f, z * 0.0025f)));
    }

    const uint4 bits = clusters[clusterOf(px, z)];
    uint softLeft = 4u;
    [loop] for (uint w = 0u; w < 4u; w++) {
        uint b = bits[w];
        [loop] while (b != 0u) {
            const uint li = w * 32u + firstbitlow(b);
            b &= b - 1u;
            if (li >= flags.y) { continue; }
            const float3 toL = lightPosR[li].xyz - p;
            const float dist = length(toL);
            const float att = 1.0f - saturate(dist / lightPosR[li].w) * saturate(dist / lightPosR[li].w);
            if (att <= 0.0f) { continue; }
            const float3 l = toL / max(dist, 1.0e-4f);
            float sh = 1.0f;
            const int ch = int(lightColS[li].w);
            if (ch >= 0 && ch <= 3) { sh = m4[ch]; }   // interiors without sun shadows use channel 0 too
            float3 c = lightColS[li].rgb * att;
            const uint slot = li + 1u == rIdx.x ? 1u : li + 1u == rIdx.y ? 2u : li + 1u == rIdx.z ? 3u : 0u;
            if (slot != 0u) { sh = min(sh, rv[slot]); } else { sh = min(sh, restVis); }
            if (!match && slot == 0u && (flags.x & FLAG_SOFT) != 0u && base != 5u && softLeft > 0u && dot(s.n, l) > 0.0f && max(c.r, max(c.g, c.b)) > 0.02f) {
                sh *= screenShadow(p + lift, l, min(dist, tune.z), 4.0f / max(dist, 1.0f), jit, 10u);
                --softLeft;
            }
            addLight(s, l, c * sh, c, diff, spec);
        }
    }

    float3 amb = float3(dot(ambient[0].xyz, s.n) + ambient[0].w, dot(ambient[1].xyz, s.n) + ambient[1].w,
                        dot(ambient[2].xyz, s.n) + ambient[2].w);
    // Foliage gets a third of the AO: screen-space AO over-occludes thin geometry.
    const float aoTerm = (flags.x & FLAG_AO) != 0u ? lerp(1.0f, AoHist.Load(ip).x, tune2.x * (base == 5u ? 0.35f : 1.0f)) : 1.0f;
    // Exteriors: ray-traced sky visibility (RtSky.cpp). Interiors (!match): local SM6.5 RayQuery RTAO from RtShadow.hlsl.
    const bool isExterior = sunColour.w > 0.5f;
    const float skyVis = rt && (flags.x & FLAG_SKY) != 0u && (isExterior || !match) ? RtIdx.Load(ip).w : 1.0f;
    // One occlusion, not two: the ray-traced sky/RTAO visibility already holds near occluders.
    const float rtOcc = isExterior ? lerp(1.0f, skyVis, cluster.w) : lerp(0.42f, 1.0f, skyVis);
    const float combinedOcc = min(aoTerm, rtOcc);
    // Directional hemisphere skylighting: upward-facing ledges catch sky light while overhangs stay grounded.
    const float hemiSky = (!match && isExterior) ? lerp(0.72f, 1.12f, saturate(s.n.z * 0.5f + 0.5f)) : 1.0f;
    amb *= match ? combinedOcc.xxx : gtaoMultiBounce(combinedOcc, saturate(albedo)) * hemiSky;
    // Where the sky is blocked, the blocking surfaces send their own light: the bounce the sky rays found.
    // Interiors too (FLAG_BOUNCE_IN, at RtBounceInterior): lamp-lit walls send their light on.
    const bool bounceHere = sunColour.w > 0.5f || (flags.x & FLAG_BOUNCE_IN) != 0u;
    const float3 bounce = rt && (flags.x & FLAG_BOUNCE) != 0u && bounceHere ? RtBounce.Load(ip).rgb * proj.w * (match ? 1.0f : 1.45f) : 0.0f.xxx;
    // Elder-inspired Nordic chromatic separation (!match): cool slate-blue shadow ambient vs warm direct sun & bounce.
    if (!match && isExterior) {
        const float inShadow = 1.0f - saturate(sunShadow);
        amb *= lerp(float3(1.0f, 1.0f, 1.0f), float3(0.84f, 0.94f, 1.10f), inShadow * 0.65f);
    }
    amb += bounce;

    if (flags.w == 1u) { return float4(s.n * 0.5f + 0.5f, game.a); }
    if (flags.w == 2u) { return float4(sunShadow.xxx, game.a); }
    if (flags.w == 11u) { return float4(cloudShadow(p + camPosW.xyz).xxx, game.a); }   // the clouds' shadow alone
    if (flags.w == 3u) { return float4(amb, game.a); }
    if (flags.w == 4u) { return float4(skyVis.xxx, game.a); }
    if (flags.w == 14u) { return float4(restVis.xxx, game.a); }   // the untraced lights' ray-traced share
    if (flags.w == 5u) { return float4(bounce, game.a); }
    if (flags.w == 13u) { return float4(diff + amb, game.a); }   // the light the surface receives (compare RtDebug=6)
    if (flags.w == 7u) { return float4(RtRefl.Load(ip).rgb, game.a); }   // RtDebug=1: the primary-ray view
    if (flags.w == 8u) { return float4(wet.wet, wet.puddle, wet.snow, game.a); }   // weather: wet red, puddles green, snow blue

    // Environment-mapped (class 2: armour, leather, metal): the game adds its cube map's colour times the env mask
    // times the light the surface receives, before the knee (ps 955d1a31636cbbe4). The patched shader writes that
    // colour times mask to RAW's emissive target, and RAW lights it with its own received light, in both modes.
    const float3 envRefl = base == 2u && (flags.x & FLAG_ENV) != 0u ? max(SoftMap.Load(ip).rgb, 0.0f.xxx) : 0.0f.xxx;
    if (flags.w == 18u) { return float4(envRefl, game.a); }   // view 18: the environment map's reflection (class 2)
    // The character light: a white fill the game's actor (and many object) shaders add to the light sum, from two fixed
    // directions and a scale read from slot 11 (GBufferAnalyse.cpp, FindCharLight). Outside classes 2 and 5 the patch
    // writes it to the emissive target as -(light) - 1/1024; emitted light is never negative.
    const float ea = base != 2u && base != 5u ? SoftMap.Load(ip).r : 0.0f;
    const float charLight = (flags.x & FLAG_CHAR) != 0u && ea < 0.0f ? max(-ea - 1.0f / 1024.0f, 0.0f) : 0.0f;
    if (flags.w == 19u) { return float4(charLight.xxx, game.a); }   // view 19: the character light
    // Attenuate vanilla's flat white character fill light on skin in direct sunlight so shoulders/faces never blow out.
    const float effCharLight = (!match && skin) ? charLight * lerp(0.85f, 0.12f, saturate(dot(diff, float3(0.33f, 0.33f, 0.34f)))) : charLight;
    float3 lit = knee3((albedo + envRefl * (match ? 1.0f : 0.45f)) * (diff + amb + effCharLight));
    if (!match && skin) {
        // Gentle dermal highlight roll-off preserves natural skin tone under harsh direct midday sunlight.
        lit = lit / (1.0f + max(max(lit.r, max(lit.g, lit.b)) - 0.78f, 0.0f) * 0.42f);
    }
    if (flags.w == 16u) { return float4(albedo, game.a); }   // view 16: the G-buffer base colour the pass lights
    if (flags.w == 17u) { return float4(saturate(game.rgb / max(knee3(lit), 1.0e-3f) * 0.25f), game.a); }   // view 17: the game's frame over RAW's diffuse (0.25 = equal)
    // The game's lobe exists only in shaders with a shininess exponent (gloss > 0).
    // Landscape shaders carry no lobe (their slot 1 is a colour layer); the exponent found in parallax
    // shaders belongs to another term (a lobe there doubles the error, tools/lighting/fitsoft.py).
    // GGX follows the game's specular map where the game had a lobe, as the game's own term does (mask x scale x lobe):
    // a sharp lobe at f0 0.02 on a surface the map leaves dark (the inn's beams, rt7-glint-1) read as white streaks the
    // game never draws, and a weight full from a mask of 0.25 still left them on the planks (rt7-glint-5).
    const float specMap = lobe && !(skin && !match) ? saturate(s.mask * resScale.w) : 1.0f;
    lit += (flags.x & FLAG_GGX) != 0u ? spec * specMap : spec * (lobe ? s.mask * resScale.w : 0.0f);
    // Ray-traced reflection on glossy surfaces with live dynamic-cubemap IBL fallback on RT misses,
    // plus dynamic cubemap enhancement for class-2 metal/armor and clearcoat GGX on wet surfaces.
    if (!match) {
        const float nv = saturate(dot(nSmooth, s.v));
        const float fr = s.f0 + (1.0f - s.f0) * pow(1.0f - nv, 5.0f);
        const float3 rDir = reflect(-s.v, nSmooth);
        const float3 dynEnv = evalDynamicEnvCube(rDir, max(s.alpha, base == 2u ? 0.28f : 0.0f));
        const float reflMask = max(saturate(s.mask * resScale.w), max(wet.puddle, 0.5f * wet.wet));
        if (rt && (flags.x & FLAG_REFLECT) != 0u && lobe) {
            const float4 rf = RtRefl.Load(int3(clamp(int2(px + rings * 40.0f), int2(0, 0), int2(dims.xy) - 1), 0));
            const float3 rtCol = min(rf.rgb, 3.0f.xxx);
            const float3 envFinal = lerp(dynEnv * (0.35f + 0.65f * skyVis), rtCol, rf.a);
            lit += envFinal * fr * reflMask * (1.0f - s.alpha) * 0.85f;
            if (flags.w == 6u) { return float4(envFinal, game.a); }
        } else if (base == 2u) {
            // Class-2 metal/armor dynamic cubemap IBL (live sun/sky/ground response).
            lit += dynEnv * fr * max(reflMask, 0.25f) * (1.0f - s.alpha * 0.65f) * (0.35f + 0.65f * skyVis) * 0.55f;
        }
        // Physical clearcoat GGX lobe on wet surfaces (water film over stone/wood/armor).
        if (max(wet.wet, wet.puddle) > 0.08f && isExterior && sunShadow > 0.0f) {
            const float3 ch = normalize(s.v + sunDir.xyz);
            const float cnh = saturate(dot(s.n, ch)), ca2 = 0.0036f;
            const float cdd = cnh * cnh * (ca2 - 1.0f) + 1.0f;
            const float cD = ca2 / (PI * cdd * cdd);
            const float cF = 0.04f + 0.96f * pow(1.0f - saturate(dot(s.v, ch)), 5.0f);
            lit += min(sunColour.rgb * (cD * cF * saturate(dot(s.n, sunDir.xyz)) * sunShadow * 0.25f), 2.5f.xxx) * max(wet.wet * 0.6f, wet.puddle);
        }
    }
    if (!match) {
        // Character shading: subtle oily-pore skin specular (addLight's s.wrap + Sss.hlsl handle dermal scatter without red over-saturation).
        if (skin) {
            if (isExterior) {
                const float nlRaw = dot(s.n, sunDir.xyz);
                const float3 shv = normalize(s.v + sunDir.xyz);
                const float snh = saturate(dot(s.n, shv)), sa2 = 0.055f;
                const float sdd = snh * snh * (sa2 - 1.0f) + 1.0f;
                const float sD = sa2 / (PI * sdd * sdd);
                lit += min(sunColour.rgb * (sD * 0.022f * saturate(nlRaw) * sunShadow * 0.16f), 0.5f.xxx);
            }
        } else if (base == 11u) {
            // Class 11 (Hair): Kajiya-Kay shifted anisotropic hair sheen along vertical strand tangent.
            const float3 tHair = normalize(cross(s.n, float3(1.0f, 0.0f, 0.0f)) + float3(0.0f, 0.0f, 0.35f));
            const float3 hDir = normalize(s.v + (isExterior ? sunDir.xyz : float3(0.0f, 0.0f, 1.0f)));
            const float tdoth = dot(tHair, hDir);
            const float sinTH = sqrt(max(1.0f - tdoth * tdoth, 0.0f));
            const float kajiya = pow(sinTH, 48.0f) * 0.35f + pow(sinTH, 12.0f) * 0.12f;
            lit += (albedo * 0.65f + 0.35f.xxx) * (isExterior ? sunColour.rgb * (0.3f + 0.7f * sunShadow) : amb) * kajiya;
        } else if (base == 12u) {
            // Class 12 (Eyes): wet corneal clearcoat + live dynamic cubemap catchlight.
            const float3 eyeEnv = evalDynamicEnvCube(reflect(-s.v, s.n), 0.04f);
            const float eyeFr = 0.035f + 0.965f * pow(1.0f - saturate(dot(s.n, s.v)), 5.0f);
            lit += eyeEnv * eyeFr * 1.15f;
        } else if (base == 5u && isExterior) {
            const float back = pow(saturate(dot(-s.v, sunDir.xyz)), 3.0f);
            lit += albedo * sunColour.rgb * float3(1.05f, 1.15f, 0.72f) * (0.18f + 0.82f * back) * (0.20f + 0.80f * sunShadow) * 0.42f;
        }
        // Grazing micro-Fresnel environmental wrap so vanilla surfaces gain physical material separation.
        const float nvWrap = saturate(dot(s.n, s.v));
        const float graze = pow(1.0f - nvWrap, 4.5f) * (skin ? 0.14f : base == 5u ? 0.08f : 0.18f);
        lit += (amb + bounce * 0.5f) * graze * (0.35f + 0.65f * skyVis);
        // Exteriors: height-stratified valley mist + Henyey-Greenstein sun-aligned Mie forward scattering.
        if (isExterior && z > 160.0f) {
            const float3 viewDir = normalize(p);
            const float cosTheta = dot(viewDir, sunDir.xyz);
            const float g = 0.72f;
            const float hg = (1.0f - g * g) / (4.0f * 3.14159f * pow(max(1.0f + g * g - 2.0f * g * cosTheta, 0.04f), 1.5f));
            const float heightDensity = exp(-max(p.z + 180.0f, 0.0f) * 0.00085f);
            const float distFog = 1.0f - exp(-max(z - 160.0f, 0.0f) * (0.000030f + 0.000028f * heightDensity));
            const float3 rayleighCol = float3(0.42f, 0.58f, 0.86f) * max(dot(amb, float3(0.3f, 0.5f, 0.2f)), 0.06f);
            const float3 mieCol = sunColour.rgb * min(hg, 1.5f) * (0.20f + 0.80f * sunShadow) * (0.45f + 0.55f * skyVis) * 0.045f;
            lit = lerp(lit, lit + rayleighCol * 0.24f + mieCol, saturate(distFog * 0.55f));
        }
    }
    const float3 finalLitRaw = knee3(lit);
    // Preserve warm emissive tavern windows, lanterns, and runes where the game's glow exceeds RAW's surface lighting.
    const float gLuma = dot(game.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float rLuma = dot(finalLitRaw, float3(0.2126f, 0.7152f, 0.0722f));
    const float emissiveW = (!match && !skin && base != 5u && gLuma > rLuma * 1.65f && gLuma > 0.18f) ? saturate((gLuma - rLuma * 1.65f) * 2.5f) : 0.0f;
    const float3 finalLit = lerp(finalLitRaw, max(finalLitRaw, game.rgb * 0.92f), emissiveW);
    if (flags.w == 20u || flags.w == 21u) {
        const float luma = dot(finalLit, float3(0.2126f, 0.7152f, 0.0722f));
        // Crushed black (<0.015) -> cyan, blown highlight (>0.95) -> magenta, midtones -> calibrated thermal ramp
        const float3 heat = luma < 0.015f ? float3(0.0f, 0.85f, 1.0f)
            : luma > 0.95f ? float3(1.0f, 0.05f, 0.85f)
            : lerp(float3(0.08f, 0.12f, 0.30f), float3(0.95f, 0.82f, 0.28f), saturate(pow(luma, 0.45f)));
        if (flags.w == 21u) { return float4(heat, game.a); }
        // View 20: 4-quadrant live diagnostic (TL final lit | TR normal+env | BL RT shadow/RTAO+bounce | BR luminance/clipping heatmap)
        const float2 q = float2(px) / max(dims.xy, float2(1.0f, 1.0f));
        if (abs(q.x - 0.5f) < 0.0012f || abs(q.y - 0.5f) < 0.0018f) { return float4(1.0f, 1.0f, 1.0f, game.a); }
        if (q.y < 0.5f) {
            return q.x < 0.5f ? float4(finalLit, game.a)
                              : float4(saturate((s.n * 0.5f + 0.5f) * 0.75f + envRefl * 0.65f), game.a);
        }
        const float bounceL = saturate(dot(bounce, float3(0.33f, 0.33f, 0.34f)) * 4.0f);
        return q.x < 0.5f ? float4(isExterior ? sunShadow : skyVis, skyVis, bounceL, game.a)
                          : float4(heat, game.a);
    }
    return float4(finalLit, game.a);
}
