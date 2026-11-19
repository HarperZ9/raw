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

Texture2D<float>  Depth   : register(t0);
Texture2D<float4> Normal  : register(t1);
Texture2D<float4> Albedo  : register(t2);
Texture2D<float4> Spec    : register(t3);
Texture2D<float4> Mask    : register(t4);
Texture2D<float4> AoHist  : register(t6);
Texture2D<float4> GameLit : register(t7);
Texture2D<float4> SoftMap : register(t8);
Texture2D<float4> RtVis   : register(t9);    // RtShadow.hlsl: sun and three lights' ray-traced visibility
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

struct Surface { float3 n, v; float shin, mask, f0, alpha; float3 soft; };

float smooth01(float x) { return x * x * (3.0f - 2.0f * x); }

// One light of colour c (windowed and shadowed; cFull windowed only) from direction l. Soft-lit
// surfaces (foliage) add the game's wrap term, smoothstep((n.l + r) / (1 + r)) - smoothstep(n.l),
// from the unshadowed colour; the game scales it by a texture the G-buffer does not carry, so a
// fitted constant (s.soft) stands in (docs/LIGHTING.md).
void addLight(Surface s, float3 l, float3 c, float3 cFull, inout float3 diff, inout float3 spec) {
    const float x = dot(s.n, l), nl = saturate(x);
    if (any(s.soft > 0.0f)) { diff += s.soft * saturate(smooth01(saturate((x + 1.0f) * 0.5f)) - smooth01(nl)) * cFull; }
    const float3 h = normalize(s.v + l);
    const float nh = saturate(dot(s.n, h));
    if ((flags.x & FLAG_GGX) == 0u) {
        diff += nl * c;
        spec += pow(nh, s.shin) * c;   // the game's lobe: no cosine, no normalisation
        return;
    }
    const float nv = max(dot(s.n, s.v), 1.0e-4f), a2 = s.alpha * s.alpha;
    const float dd = nh * nh * (a2 - 1.0f) + 1.0f;
    const float D = a2 / (PI * dd * dd);
    const float V = 0.5f / (nl * sqrt(nv * nv * (1.0f - a2) + a2) + nv * sqrt(nl * nl * (1.0f - a2) + a2) + 1.0e-6f);
    const float F = s.f0 + (1.0f - s.f0) * pow(1.0f - saturate(dot(s.v, h)), 5.0f);
    diff += nl * c * (1.0f - F);
    spec += PI * D * V * F * nl * c;   // the game's light colour is irradiance over pi
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    const float4 game = GameLit.Load(ip);
    const float d = Depth.Load(ip);
    if (d >= 1.0f || (flags.x & FLAG_SURFACE) == 0u) { return game; }
    // View 9, a test control: the pass reaches only the top-left quarter, as a half-size viewport
    // would. The coverage check in the capture must fail on it.
    if (flags.w == 9u && (at.x >= dims.x * 0.5f || at.y >= dims.y * 0.5f)) { return game; }
    const float4 sp = Spec.Load(ip);
    const uint cls = uint(sp.z * 255.0f + 0.5f), base = cls & 127u;
    // Effects, water, first person and distant LOD (lit per vertex by the game): the game's own.
    if (base == 0u || base >= 7u || (cls & 128u) != 0u) { return game; }

    const bool match = (flags.x & FLAG_MATCH) != 0u;
    const float2 px = floor(at.xy);
    const float3 p = worldAt(px, d), albedo = Albedo.Load(ip).rgb;
    const float z = viewZOf(d), jit = ign(px);
    Surface s;
    s.n = worldNormal(Normal.Load(ip));
    s.v = normalize(-p);
    s.shin = exp2(sp.y * 13.0f);
    s.mask = sp.x;
    const bool lobe = sp.y > 0.002f && base != 4u && base != 6u;   // the game's own shader had a lobe here
    s.f0 = lobe ? lerp(0.02f, 0.08f, saturate(sp.x * resScale.w)) : 0.04f;
    s.alpha = clamp(sqrt(2.0f / (s.shin + 2.0f)), 0.03f, 1.0f);   // Blinn-Phong exponent to GGX width
    // Soft-lit (foliage): the game multiplies its wrap term by the soft-lighting map, which the
    // patched shader writes to RAW's emissive target. Before that channel existed a grey constant
    // fitted on one frame (0.15, tools/lighting/fitsoft.py) stood in; it lost the warm back light.
    s.soft = base == 5u ? SoftMap.Load(ip).rgb : 0.0f.xxx;
    if (base == 5u && !any(s.soft > 0.0f)) { s.soft = 0.15f.xxx; }   // a soft-lit shader the patch found no map in
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
        if (rt && sunColour.w > 0.5f) { sunShadow = min(sunShadow, rv.x); }
        addLight(s, sunDir.xyz, sunColour.rgb * sunShadow, sunColour.rgb, diff, spec);
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

    if (flags.w == 1u) { return float4(s.n * 0.5f + 0.5f, game.a); }
    if (flags.w == 2u) { return float4(sunShadow.xxx, game.a); }
    if (flags.w == 3u) { return float4(amb, game.a); }
    if (flags.w == 4u) { return float4(skyVis.xxx, game.a); }

    float3 lit = knee3(albedo * (diff + amb));
    // The game's lobe exists only in shaders with a shininess exponent (gloss > 0).
    // Landscape shaders carry no lobe (their slot 1 is a colour layer); the exponent found in parallax
    // shaders belongs to another term (a lobe there doubles the error, tools/lighting/fitsoft.py).
    lit += (flags.x & FLAG_GGX) != 0u ? spec : spec * (lobe ? s.mask * resScale.w : 0.0f);
    return float4(knee3(lit), game.a);
}
