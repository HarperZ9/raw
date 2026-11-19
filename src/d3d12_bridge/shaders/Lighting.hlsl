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

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    const float4 game = GameLit.Load(ip);
    const float d = Depth.Load(ip);
    if ((flags.x & FLAG_SURFACE) == 0u) { return game; }
    if (d >= 1.0f) {
        // The sky: the clouds over the game's own sky. View 10 marks the sky pixels this pass owns.
        if (flags.w == 10u) { return float4(1.0f, 0.0f, 1.0f, game.a); }
        if ((flags.x & FLAG_CLOUDS) == 0u) { return game; }
        const float4 c = Clouds.SampleLevel(Lin, (at.xy) * dims.zw, 0.0f);
        return float4(game.rgb * c.a + c.rgb, game.a);
    }
    // View 9, a test control: the pass reaches only the top-left quarter, as a half-size viewport
    // would. The coverage check in the capture must fail on it.
    if (flags.w == 9u && (at.x >= dims.x * 0.5f || at.y >= dims.y * 0.5f)) { return game; }
    const float4 sp = Spec.Load(ip);
    const uint cls = uint(sp.z * 255.0f + 0.5f), base = cls & 127u;
    // Effects, water, first person and distant LOD (lit per vertex by the game): the game's own.
    if (flags.w == 12u) {   // view 12: the material class (skin red, foliage green, landscape brown, parallax blue, the rest grey)
        const float3 cc = base == 10u ? float3(1, 0.1, 0.1) : base == 5u ? float3(0.1, 0.8, 0.1) : base == 4u ? float3(0.5, 0.35, 0.1) :
                          base == 6u ? float3(0.1, 0.3, 1) : base == 0u ? 0.0f.xxx : 0.5f.xxx;
        return float4(cc * (0.35f + 0.65f * dot(GameLit.Load(ip).rgb, 0.333f)), game.a);
    }
    const bool skin = base == 10u;
    if (base == 0u || (base >= 7u && !skin) || (cls & 128u) != 0u) { return game; }

    const bool match = (flags.x & FLAG_MATCH) != 0u;
    const float2 px = floor(at.xy);
    const float3 p = worldAt(px, d);
    float3 albedo = Albedo.Load(ip).rgb;
    const float z = viewZOf(d), jit = ign(px);
    Surface s;
    s.n = worldNormal(Normal.Load(ip));
    s.v = normalize(-p);
    s.shin = exp2(sp.y * 13.0f);
    s.mask = sp.x;
    bool lobe = sp.y > 0.002f && base != 4u && base != 6u;   // the game's own shader had a lobe here
    s.f0 = lobe ? lerp(0.02f, 0.08f, saturate(sp.x * resScale.w)) : 0.04f;
    s.alpha = clamp(sqrt(2.0f / (s.shin + 2.0f)), 0.03f, 1.0f);   // Blinn-Phong exponent to GGX width
    // Soft-lit (foliage): the game multiplies its wrap term by the soft-lighting map, which the
    // patched shader writes to RAW's emissive target. Before that channel existed a grey constant
    // fitted on one frame (0.15, tools/lighting/fitsoft.py) stood in; it lost the warm back light.
    s.soft = base == 5u ? SoftMap.Load(ip).rgb : 0.0f.xxx;
    s.wrap = skin && !match ? float3(0.55f, 0.22f, 0.12f) : 0.0f.xxx;
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
    const float3 lift = s.n * (0.3f + 0.002f * z);                 // ray origins off the surface
    const float4 m4 = Mask.Load(ip);

    const bool rt = !match && (flags.x & FLAG_RT) != 0u;
    const float4 rv = rt ? RtVis.Load(ip) : 1.0f.xxxx;
    const uint3 rIdx = rt ? uint3(RtIdx.Load(ip).xyz * 255.0f + 0.5f) : uint3(0u, 0u, 0u);
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
        if (rt && sunColour.w > 0.5f) { sunShadow = min(sunShadow, base == 5u ? lerp(1.0f, rv.x, 0.6f) : rv.x); }
        if (!match && sunColour.w > 0.5f && (flags.x & FLAG_CLOUDS) != 0u) { sunShadow *= cloudShadow(p + camPosW.xyz); }   // SKY-02
        // Skin: the shadow's edge turns red where light scattered under the surface reaches past it.
        const float3 sunSh = skin && !match ? pow(saturate(sunShadow).xxx, float3(0.55f, 0.9f, 1.1f)) : sunShadow.xxx;
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
            if (slot != 0u) { sh = min(sh, rv[slot]); }
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
    if ((flags.x & FLAG_AO) != 0u) { amb *= lerp(1.0f, AoHist.Load(ip).x, tune2.x * (base == 5u ? 0.35f : 1.0f)); }
    // Exteriors: the sky the pixel sees (RtSky.cpp), in the light-index output's alpha. Interiors keep
    // the game's ambient: their cell ambient is set for an enclosed room.
    const float skyVis = rt && (flags.x & FLAG_SKY) != 0u && sunColour.w > 0.5f ? RtIdx.Load(ip).w : 1.0f;
    amb *= lerp(1.0f, skyVis, cluster.w);
    // Where the sky is blocked, the blocking surfaces send their own light: the bounce the sky rays found.
    // Interiors too (FLAG_BOUNCE_IN, at RtBounceInterior): lamp-lit walls send their light on.
    const bool bounceHere = sunColour.w > 0.5f || (flags.x & FLAG_BOUNCE_IN) != 0u;
    const float3 bounce = rt && (flags.x & FLAG_BOUNCE) != 0u && bounceHere ? RtBounce.Load(ip).rgb * proj.w : 0.0f.xxx;
    amb += bounce;

    if (flags.w == 1u) { return float4(s.n * 0.5f + 0.5f, game.a); }
    if (flags.w == 2u) { return float4(sunShadow.xxx, game.a); }
    if (flags.w == 11u) { return float4(cloudShadow(p + camPosW.xyz).xxx, game.a); }   // the clouds' shadow alone
    if (flags.w == 3u) { return float4(amb, game.a); }
    if (flags.w == 4u) { return float4(skyVis.xxx, game.a); }
    if (flags.w == 5u) { return float4(bounce, game.a); }
    if (flags.w == 13u) { return float4(diff + amb, game.a); }   // the light the surface receives (compare RtDebug=6)
    if (flags.w == 7u) { return float4(RtRefl.Load(ip).rgb, game.a); }   // RtDebug=1: the primary-ray view
    if (flags.w == 8u) { return float4(wet.wet, wet.puddle, wet.snow, game.a); }   // weather: wet red, puddles green, snow blue

    float3 lit = knee3(albedo * (diff + amb));
    // The game's lobe exists only in shaders with a shininess exponent (gloss > 0).
    // Landscape shaders carry no lobe (their slot 1 is a colour layer); the exponent found in parallax
    // shaders belongs to another term (a lobe there doubles the error, tools/lighting/fitsoft.py).
    lit += (flags.x & FLAG_GGX) != 0u ? spec : spec * (lobe ? s.mask * resScale.w : 0.0f);
    // Ray-traced reflection on glossy surfaces, weighted by Schlick Fresnel at the view angle and the
    // game's specular mask; where the ray found nothing the game had on screen, nothing is added.
    if (rt && (flags.x & FLAG_REFLECT) != 0u && lobe) {
        const float4 rf = RtRefl.Load(int3(clamp(int2(px + rings * 40.0f), int2(0, 0), int2(dims.xy) - 1), 0));
        const float nv = saturate(dot(s.n, s.v));
        const float fr = s.f0 + (1.0f - s.f0) * pow(1.0f - nv, 5.0f);
        lit += rf.rgb * rf.a * fr * max(saturate(s.mask * resScale.w), max(wet.puddle, 0.5f * wet.wet)) * (1.0f - s.alpha);
        if (flags.w == 6u) { return float4(rf.rgb * rf.a, game.a); }
    }
    return float4(knee3(lit), game.a);
}
