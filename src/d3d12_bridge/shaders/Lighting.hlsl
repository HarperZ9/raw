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
Texture2D<float4> RtBounce : register(t11);  // RtSky.cpp + GiAtrousPass: 3-pass 25-tap SVGF indirect bounce
Texture2D<float4> RtRefl   : register(t12);  // RtShadow.hlsl + RtReflFilter: temporal GGX VNDF RT reflection
Texture2D<float4> EnvCube  : register(t13);  // EnvCubePass.hlsl: live 256x256 dual-band prefiltered dynamic cubemap
Texture2D<float4> WorldField : register(t14); // WorldFieldSim.hlsl: 512x512 RGBA16F persistent PDE grid (unlocked by kMaxRasterBindings=60!)
SamplerState      Lin      : register(s0);
Texture2D<float4> RtIdx   : register(t10);   // which lights, index + 1 over 255

// Sample our live 256x256 dual-band prefiltered dynamic environment cubemap (top: glossy, bottom: rough convolution).
float3 sampleLiveEnvCube(float3 rd, float roughness) {
    const float3 d = normalize(rd);
    const float u = atan2(d.y, d.x) / (2.0f * PI) + 0.5f;
    const float vLocal = saturate(0.5f - asin(clamp(d.z, -1.0f, 1.0f)) / PI);
    const float vClamp = clamp(vLocal, 0.5f / 128.0f, 127.5f / 128.0f);
    const float3 sharp = EnvCube.SampleLevel(Lin, float2(u, vClamp * 0.5f), 0.0f).rgb;
    const float3 rough = EnvCube.SampleLevel(Lin, float2(u, 0.5f + vClamp * 0.5f), 0.0f).rgb;
    return lerp(sharp, rough, saturate(roughness * roughness));
}

// Karis (SIGGRAPH 2013 UE4) / Lazarov analytical Split-Sum Environment BRDF LUT approximation.
float2 envBRDFScaleBias(float roughness, float NoV) {
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4( 1.0f,  0.0425f,  1.040f, -0.040f);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28f * NoV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// Fdez-Aguera (JCGT 2019) Multi-Scattering Specular IBL energy compensation for both dielectrics and conductors.
float3 evalMultiScatterIBL(float3 F0, float roughness, float NoV, float3 envCol, float specOcc) {
    const float2 ab = envBRDFScaleBias(roughness, NoV);
    const float3 FssEss = F0 * ab.x + ab.y;
    const float Ess = ab.x + ab.y;
    const float Ems = 1.0f - Ess;
    const float3 Favg = F0 + (1.0f - F0) / 21.0f;
    const float3 Fms = FssEss * Favg / max(1.0f - (1.0f - Ess) * Favg, 0.05f.xxx);
    return envCol * (FssEss + Fms * Ems) * specOcc;
}

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
    const float lh = saturate(dot(l, h));
    const float F = s.f0 + (1.0f - s.f0) * pow(1.0f - lh, 5.0f);
    // EON Energy-Preserving Rough Diffuse BRDF (Portsmouth/Kutz/Hill 2025, MAT-03) blended with Burley retro-reflection:
    // gives rough stone masonry, bark, plaster, and snowdrifts authentic micro-rough depth while strictly conserving energy.
    const float rEon = saturate(s.alpha);
    const float cosPhi = dot(l - s.n * nl, s.v - s.n * nv);
    const float eonA = 1.0f / (1.0f + (0.5f - 2.0f / (3.0f * PI)) * rEon);
    const float eonS = cosPhi * (max(nl, nv) > 1.0e-4f ? (1.0f / max(nl, nv)) : 0.0f);
    const float eonDiff = any(s.wrap > 0.0f) ? 1.0f : clamp(eonA * (1.0f + rEon * max(eonS, 0.0f) * 0.32f), 0.78f, 1.24f);
    // Kulla-Conty / Turquin analytical multi-scattering GGX energy compensation (SIGGRAPH 2017/2019):
    // Restores microfacet multiple-bounce energy lost by single-scattering GGX on rough stone, timber, leather, and brushed steel.
    const float ess = saturate(1.0f - s.alpha * (0.42f * (1.0f - nv) + 0.14f));
    const float kulla = 1.0f + s.f0 * ((1.0f - ess) / max(ess, 0.25f));
    diff += nlD * c * (1.0f - F) * eonDiff;
    spec += PI * D * V * F * kulla * nl * c;   // the game's light colour is irradiance over pi
}

#include "DistantLod.hlsli"   // distantLit: distant LOD lit in improved mode (FOL-04)

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    const float4 game = GameLit.Load(ip);
    const float d = Depth.Load(ip);
    if ((flags.x & FLAG_SURFACE) == 0u) { return game; }
    if (d >= 1.0f) {
        // Pass raw sky/celestial buffer (Masser, Secunda, exotic DLC realms) cleanly straight to LightFog.hlsl
        // which owns 100% of the 24h physical Hillaire skybox, single isRealSun() solar disc, lunar halo, stars, aurora & clouds.
        // (Removes duplicate sunDisc in Lighting.hlsl that previously leaked onto the Moon via moonKeep!)
        if (flags.w == 10u) { return float4(1.0f, 0.0f, 1.0f, game.a); }
        return float4(game.rgb, game.a);
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
    // Full-range PBR albedo expansion (!match, non-skin): rescues vanilla Skyrim's narrow washed-out 2011 texture range
    // into crisp deep darks and rich natural reflectance while preserving exact hue.
    if (!match && !skin && base != 12u) {
        const float aLum = max(dot(albedo, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-4f);
        const float expLum = saturate(pow(aLum, 1.12f) * 1.14f);
        albedo = saturate(albedo * (expLum / aLum));
    }
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
        // Native Seasons (waterB.w: 0 Auto, 1 Spring, 2 Summer, 3 Autumn, 4 Winter).
        const uint season = uint(waterB.w + 0.5f);
        if (season == 4u) {
            const float winMask = saturate((s.n.z - 0.42f) * 2.4f) * (0.45f + 0.55f * sv);
            wet.snow = max(wet.snow, winMask * 0.90f);
        } else if (season == 1u) {
            wet.wet = max(wet.wet, 0.25f * saturate(s.n.z));
        }
        // Engine-native capillary wet-shoreline wicking driven directly by TESObjectCELL::waterHeight (engineWorld.x):
        // Rocks, bridge piers, dock pilings & riverbanks within 0..46u above the live cell water plane darken and turn glossy.
        if (engineWorld.z > 0.5f && engineWorld.x > -13000.0f && (base == 1u || base == 4u) && z < 4500.0f) {
            const float aboveW = (p.z + camPosW.z) - engineWorld.x;
            if (aboveW > -6.0f && aboveW < 46.0f) {
                const float wicking = (1.0f - smoothstep(4.0f, 46.0f, aboveW)) * smoothstep(-6.0f, 2.0f, aboveW);
                const float shoreNoise = 0.72f + 0.28f * valueNoise((p.xy + camPosW.xy) * 0.045f);
                wet.wet = max(wet.wet, wicking * shoreNoise * 0.88f);
            }
        }
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
            // SKY-09 Volumetric 3D Sastrugi Snowdrift Relief + Glacial Subsurface Scattering:
            // Wind-sculpted multi-octave 3D snowdrift dunes with deep drift-trough self-shadowing, icy blue-turquoise
            // snow subsurface scattering in shadowed troughs, and matte high-roughness powder crests.
            const float3 wpS = p + camPosW.xyz;
            const float2 wFlowS = normalize(cloudWind.xy + float2(1.0f, 0.35f));
            const float2 sCoord = float2(dot(wpS.xy, wFlowS) * 0.0045f, dot(wpS.xy, float2(-wFlowS.y, wFlowS.x)) * 0.014f);
            const float sastrugi = cNoise(float3(sCoord, wpS.z * 0.008f)) * 0.62f + cNoise(float3(sCoord * 2.4f + 7.3f, wpS.z * 0.018f)) * 0.38f;
            const float driftDx = cNoise((wpS + float3(7.0f, 0.0f, 0.0f)) * 0.015f) - cNoise((wpS - float3(7.0f, 0.0f, 0.0f)) * 0.015f);
            const float driftDy = cNoise((wpS + float3(0.0f, 7.0f, 0.0f)) * 0.015f) - cNoise((wpS - float3(0.0f, 7.0f, 0.0f)) * 0.015f);
            const float3 snowCrest = float3(0.76f, 0.80f, 0.86f);
            const float3 snowTroughSSS = float3(0.42f, 0.56f, 0.72f);   // glacial ice-blue subsurface scatter in drift troughs
            const float3 snowCol = lerp(snowTroughSSS, snowCrest, smoothstep(0.22f, 0.78f, sastrugi));
            albedo = lerp(albedo, snowCol, wet.snow);
            s.alpha = lerp(s.alpha, 0.86f, wet.snow);
            s.shin = lerp(s.shin, 2.0f / (0.86f * 0.86f) - 2.0f, wet.snow);
            s.n = normalize(lerp(s.n, normalize(ng + float3(-driftDx * 0.92f, -driftDy * 0.92f, 0.22f)), wet.snow * 0.78f));
            s.f0 = lerp(s.f0, 0.018f, wet.snow);
            s.mask = lerp(s.mask, 0.03f, wet.snow);
            wet.puddle *= 1.0f - wet.snow;
            wet.wet *= 1.0f - wet.snow;
        }
        // Real-Time Thermal Campfire / Hearth / Torch Snow Melt (COMPETITIVE-2026.md #192402 Dynamic Terrain Deformation):
        // Warm fire lights physically melt surrounding snowdrifts into damp bare earth + glistening icy slush rim!
        if ((wet.snow > 0.05f || base == 4u) && z < 2600.0f && s.n.z > 0.35f && flags.y > 0u) {
            float meltHeat = 0.0f;
            const uint maxMeltL = min(flags.y, 16u);
            [loop] for (uint mi = 0u; mi < maxMeltL; ++mi) {
                const float3 lc = lightColS[mi].rgb;
                if (lc.r > lc.b * 1.25f && lc.r > 0.18f) {
                    const float3 ld = p - lightPosR[mi].xyz;
                    const float rMelt = min(lightPosR[mi].w * 0.42f, 260.0f);
                    const float d2 = dot(ld, ld);
                    if (d2 < rMelt * rMelt) {
                        const float att = 1.0f - sqrt(d2) / max(rMelt, 1.0f);
                        meltHeat = max(meltHeat, saturate(att * att * 1.45f));
                    }
                }
            }
            if (meltHeat > 0.02f) {
                const float meltMask = smoothstep(0.08f, 0.72f, meltHeat);
                const float slushRing = smoothstep(0.02f, 0.25f, meltHeat) * (1.0f - smoothstep(0.45f, 0.85f, meltHeat));
                wet.snow *= (1.0f - meltMask);
                wet.wet = max(wet.wet, max(meltMask * 0.82f, slushRing * 0.75f));
                // Darken thawed snow into wet Nordic loam/stone inside the melt core + glossy blue-grey slush rim:
                const float natSnowMelt = (base == 4u) ? saturate((min(albedo.r, min(albedo.g, albedo.b)) - 0.34f) * 4.5f) : 0.0f;
                albedo = lerp(albedo, float3(0.14f, 0.12f, 0.10f), meltMask * natSnowMelt * 0.78f);
                albedo = lerp(albedo, float3(0.44f, 0.54f, 0.64f), slushRing * natSnowMelt * 0.45f);
                s.alpha = lerp(s.alpha, 0.15f, max(meltMask, slushRing) * max(natSnowMelt, wet.snow) * 0.75f);
            }
        }
        // Persistent 512x512 GPU PDE WorldField snowpack depth, post-rain puddle persistence + carved trench normal relief (t14 via 60-binding RHI!):
        if ((base == 4u || base == 1u) && z < 1000.0f && s.n.z > 0.42f) {
            const float2 simUv = saturate(p.xy / 2048.0f + 0.5f.xx);
            if (all(simUv > 0.02f) && all(simUv < 0.98f)) {
                const float4 wf = WorldField.SampleLevel(Lin, simUv, 0.0f);
                const float simSnow = wf.a;
                const float simWet = wf.b;
                const float sL = WorldField.SampleLevel(Lin, simUv + float2(-1.0f / 512.0f, 0.0f), 0.0f).a;
                const float sR = WorldField.SampleLevel(Lin, simUv + float2( 1.0f / 512.0f, 0.0f), 0.0f).a;
                const float sD = WorldField.SampleLevel(Lin, simUv + float2(0.0f, -1.0f / 512.0f), 0.0f).a;
                const float sU = WorldField.SampleLevel(Lin, simUv + float2(0.0f,  1.0f / 512.0f), 0.0f).a;
                if (base == 4u) {
                    wet.snow = max(wet.snow, simSnow * saturate((s.n.z - 0.42f) * 2.5f));
                    s.n = normalize(s.n + float3((sL - sR) * 1.85f, (sD - sU) * 1.85f, 0.0f));
                }
                wet.wet = max(wet.wet, simWet * (1.0f - wet.snow) * 0.72f);
            }
        }
        // GEO-04 4-Step Ray-Marched 3D Snowdrift & Cobblestone Relief Occlusion + Dual-Frequency Micro-Displacement (< 1,200u):
        // Breaks flat 2011 terrain quads with real view-ray heightfield relief & micro-cavity self-shadowing!
        if ((base == 1u || base == 4u) && z < 1200.0f) {
            const float3 wpM = p + camPosW.xyz;
            const float mFade = saturate(1.0f - z / 1200.0f);
            float3 wpDispl = wpM;
            if (base == 4u && z < 750.0f && s.v.z > 0.12f) {
                const float rScale = lerp(3.2f, 6.5f, wet.snow);
                const float2 stepXY = (-s.v.xy / max(s.v.z, 0.20f)) * (rScale * 0.25f);
                float hCur = 1.0f;
                [unroll] for (int rp = 0; rp < 4; ++rp) {
                    const float hSamp = cNoise(wpDispl * 0.085f) * 0.65f + cNoise(wpDispl * 0.24f + 5.1f) * 0.35f;
                    if (hSamp >= hCur) { break; }
                    wpDispl.xy += stepXY;
                    hCur -= 0.25f;
                }
                const float reliefOcc = lerp(0.80f, 1.08f, saturate(hCur));
                albedo *= lerp(1.0f, reliefOcc, saturate(1.0f - z / 750.0f) * 0.65f);
            }
            const float mx = (cNoise(wpDispl * 0.22f) - 0.5f) * 0.18f + (cNoise(wpDispl * 0.68f + 7.1f) - 0.5f) * 0.10f;
            const float my = (cNoise(wpDispl * 0.22f + 13.4f) - 0.5f) * 0.18f + (cNoise(wpDispl * 0.68f + 19.3f) - 0.5f) * 0.10f;
            s.n = normalize(s.n + float3(mx, my, 0.0f) * mFade);
        }
        // Persistent 32-step world-anchored snow & wet-mud footprint deformation trail (footTrail[0..31]).
        const float natSnow = (base == 4u) ? saturate((min(albedo.r, min(albedo.g, albedo.b)) - 0.36f) * 4.5f) * saturate(s.n.z * 1.4f - 0.3f) : 0.0f;
        const float snowAmt = max(wet.snow, natSnow);
        const float mudAmt = (base == 4u) ? max(wet.wet, wet.puddle) * (1.0f - snowAmt) : 0.0f;
        if ((snowAmt > 0.08f || mudAmt > 0.15f) && z < 2200.0f && s.n.z > 0.45f) {
            const float3 wpAbs = p + camPosW.xyz;
            float maxDep = 0.0f, maxRim = 0.0f;
            float2 bestRad = 0.0f.xx;
            [loop] for (uint fi = 0u; fi < 32u; fi++) {
                const float4 ft = footTrail[fi];
                if (ft.w <= 0.01f) { continue; }
                const float3 fd = wpAbs - ft.xyz;
                const float r2 = dot(fd.xy, fd.xy);
                if (r2 < 576.0f && abs(fd.z) < 160.0f) {
                    const float rr = sqrt(r2);
                    const float dep = exp(-r2 / 145.0f) * ft.w;
                    const float rim = exp(-pow(rr - 15.0f, 2.0f) / 36.0f) * ft.w;
                    if (dep > maxDep) { maxDep = dep; bestRad = fd.xy / max(rr, 0.5f); }
                    maxRim = max(maxRim, rim);
                }
            }
            if (maxDep > 0.02f || maxRim > 0.02f) {
                const float deformStr = max(snowAmt, mudAmt * 0.85f);
                const float stormDepthBoost = 1.0f + saturate(weather.z * 1.4f) * 0.85f;
                // True 3D carved deep snow-trench normals: steep inward-sloping trench walls + piled-up raised snow berms!
                s.n = normalize(s.n + float3(bestRad * (maxDep * 0.88f * stormDepthBoost - maxRim * 0.52f * stormDepthBoost), 0.0f) * deformStr);
                if (snowAmt >= mudAmt) {
                    // Deep recessed trench self-shadowing + glacial blue-indigo subsurface wall scatter + bright raised berm crest:
                    const float trenchAo = lerp(1.0f, 0.44f, saturate(maxDep * snowAmt * stormDepthBoost));
                    const float3 trenchWallSSS = float3(0.36f, 0.58f, 0.88f);
                    albedo = lerp(albedo, trenchWallSSS, saturate(maxDep * snowAmt * 0.72f)) * trenchAo;
                    albedo *= lerp(1.0f, 1.14f, saturate(maxRim * snowAmt * 0.85f));
                    s.alpha = lerp(s.alpha, 0.22f, saturate(maxDep * snowAmt * 0.75f));
                } else {
                    albedo *= lerp(1.0f.xxx, float3(0.38f, 0.35f, 0.31f), maxDep * mudAmt * 0.82f);
                    s.alpha = lerp(s.alpha, 0.07f, maxDep * mudAmt * 0.82f);
                }
            }
            wet.snow = max(wet.snow, natSnow * 0.65f);
        }
    }
    // Seasonal foliage palette transformation (base == 5u: Spring emerald, Autumn golden-amber/crimson, Winter frost).
    if (!match && base == 5u) {
        const uint seasonF = uint(waterB.w + 0.5f);
        if (seasonF == 1u) {
            albedo *= float3(0.94f, 1.12f, 0.92f);
        } else if (seasonF == 3u) {
            const float greenness = saturate((albedo.g - max(albedo.r, albedo.b) * 0.85f) * 3.5f);
            albedo = lerp(albedo, float3(albedo.g * 1.28f, albedo.g * 0.72f, albedo.b * 0.38f), greenness * 0.78f);
        } else if (seasonF == 4u) {
            const float snowLeaf = saturate((s.n.z - 0.25f) * 2.0f);
            albedo = lerp(albedo * float3(0.86f, 0.92f, 1.04f), float3(0.56f, 0.60f, 0.66f), snowLeaf * 0.55f);
        }
        // 2027 Botanical Vegetation & Canopy Upgrade (base == 5u): wind-coupled 3D leaf-cluster normal curvature,
        // fluttering micro-vein relief, waxy leaf-cuticle specular sheen (f0=0.036), and organic canopy hue/value depth!
        if (z < 4200.0f) {
            const float3 wpF = p + camPosW.xyz;
            const float2 windV = normalize(cloudWind.xy + float2(1.0f, 0.35f));
            const float windSpd = saturate(length(cloudWind.xy) / 280.0f + weather.w * 0.45f);
            const float gustWave = sin(dot(wpF.xy, windV) * 0.0055f - camPosW.w * (1.8f + windSpd * 2.4f));
            const float flutter = (0.35f + 0.65f * windSpd) * (0.65f + 0.35f * gustWave);
            const float nxF = (cNoise(wpF * 0.048f + float3(windV * flutter * 0.55f, 0.0f)) - 0.5f) * 0.68f;
            const float nyF = (cNoise(wpF * 0.048f + float3(11.3f, windV.y * flutter * 0.55f, 5.7f)) - 0.5f) * 0.68f;
            const float leafVein = cNoise(wpF * 0.24f + 3.1f) * 0.5f + 0.5f;
            const float fFade = saturate(1.0f - z / 4200.0f);
            s.n = normalize(s.n + float3(nxF, nyF, 0.22f) * fFade);
            // Organic canopy chlorophyll variation (breaks up flat uniform 2011 leaf card brightness):
            const float canopyTone = cNoise(wpF * 0.012f + 19.4f);
            albedo *= lerp(1.0f.xxx, lerp(float3(0.84f, 0.90f, 0.80f), float3(1.10f, 1.12f, 0.96f), canopyTone), fFade * 0.48f);
            s.alpha = lerp(s.alpha, lerp(0.42f, 0.26f, leafVein), fFade * 0.55f);
            s.f0 = lerp(s.f0, 0.036f, fFade * 0.45f);
        }
    }
    const float3 nSmooth = s.n;
    const bool isActorFeat = (base >= 10u && base <= 12u);   // skin, hair, eye
    const bool isActorCloth = (!isActorFeat && base != 2u && base != 5u && SoftMap.Load(ip).r < 0.0f);   // skinned actor clothing
    // GTA VI / RDR2-grade dynamic rain-wetness sheen on characters (skin, armor, cloth) outdoors:
    // soaked cloth darkens realistically while rain-slicked skin & forged steel catch crisp sky/torch highlights!
    if (!match && sunColour.w > 0.5f && weather.w > 0.08f && (isActorFeat || isActorCloth || base == 2u) && z < 2600.0f) {
        const float actorWet = saturate(weather.w * 1.15f) * saturate(s.n.z * 0.65f + 0.45f) * (1.0f - weather.z * 0.75f);
        if (isActorCloth) {
            albedo *= lerp(1.0f, 0.74f, actorWet * 0.75f);
            s.alpha = lerp(s.alpha, 0.28f, actorWet * 0.65f);
        } else {
            s.alpha = lerp(s.alpha, 0.16f, actorWet * 0.72f);
            s.f0 = lerp(s.f0, 0.042f, actorWet * 0.55f);
        }
    }
    // Organic world-space anti-tiling & natural slope curvature on terrain (class 4) and cliffs/rocks (class 1, excluding actor cloth).
    if (!match && !isActorCloth && (base == 4u || base == 1u) && z < 16000.0f) {
        const float3 wp = p + camPosW.xyz;
        const float macroN = cNoise(wp * 0.0022f) * 0.62f + cNoise(wp * 0.0068f + 13.7f) * 0.38f;
        const float microN = cNoise(wp * 0.024f + 5.3f);
        const float antiTile = lerp(0.89f, 1.11f, macroN) * lerp(0.95f, 1.05f, microN);
        const float slope = saturate(s.n.z);
        // Natural slope transitions + Commons Cluster 9 ("Nature retaking structures"): subtle moisture-driven
        // velvet moss & lichen growth on upward-facing stone/ruin crevices & ledges, plus crisp cool-slate cliffs.
        const float3 slopeTint = lerp(float3(0.96f, 0.98f, 1.03f), float3(1.02f, 1.01f, 0.97f), smoothstep(0.55f, 0.88f, slope));
        const float tileFade = saturate(1.0f - z / 16000.0f);
        albedo *= lerp(1.0f.xxx, antiTile * slopeTint, tileFade * (base == 4u ? 0.85f : 0.45f));
        if (base == 1u && sunColour.w > 0.5f && z < 4200.0f && wet.snow < 0.25f && slope > 0.58f) {
            const float mossPatch = smoothstep(0.56f, 0.82f, cNoise(wp * 0.0085f + 4.1f) * 0.65f + microN * 0.35f);
            const float mossW = mossPatch * smoothstep(0.58f, 0.86f, slope) * (0.28f + 0.42f * saturate(weather.x)) * saturate(1.0f - z / 4200.0f) * (1.0f - wet.snow * 3.5f);
            albedo = lerp(albedo, float3(0.10f, 0.17f, 0.07f) * (0.75f + 0.50f * microN), mossW * 0.52f);
            s.alpha = lerp(s.alpha, 0.88f, mossW * 0.45f);
        }
        // Native Ground-Up Terrain System Replacement (replaces Terrain Helper for ENB + 2011 smeared quad blends, MAT-09):
        // 1) Wronski Laplacian Band-Preserving Micro-Contrast: sharpens blurry 6-layer linear terrain quad transitions
        //    into crisp height-blended pebble/gravel/loam/rock boundaries with slope-adaptive tactile roughness!
        if (base == 4u && z < 4800.0f && wet.snow < 0.45f) {
            const float tFade = saturate(1.0f - z / 4800.0f) * (1.0f - wet.snow * 1.8f);
            const float pebbleH = cNoise(wp * 0.085f) * 0.55f + cNoise(wp * 0.24f + 7.1f) * 0.30f + cNoise(wp * 0.62f + 19.3f) * 0.15f;
            const float lapContrast = (pebbleH - 0.5f) * (slope > 0.80f ? 0.34f : 0.24f);
            const float3 loamTint = lerp(float3(0.94f, 0.92f, 0.88f), float3(1.06f, 1.04f, 0.99f), smoothstep(0.28f, 0.72f, pebbleH));
            albedo = saturate(albedo * lerp(1.0f.xxx, loamTint * (1.0f + lapContrast), tFade * 0.85f));
            // Height-driven micro-roughness: raised pebbles/stones catch crisp specular sheen while dirt crevices stay matte!
            const float heightGloss = smoothstep(0.54f, 0.82f, pebbleH) * tFade;
            s.alpha = clamp(lerp(s.alpha, max(s.alpha * 0.68f, 0.28f), heightGloss * 0.55f), 0.06f, 0.96f);
        }
        // 2) Steep-cliff tri-planar geological strata synthesis on terrain (base == 4u, slope < 0.68):
        // breaks up Skyrim's stretched 2011 vertical cliff UV smears with crisp horizontal basalt/granite strata!
        if (base == 4u && slope < 0.68f && z < 8500.0f) {
            const float cliffW = smoothstep(0.68f, 0.35f, slope) * saturate(1.0f - z / 8500.0f);
            const float strata = cNoise(float3(wp.xy * 0.0045f, wp.z * 0.028f)) * 0.65f + cNoise(float3(wp.xy * 0.016f, wp.z * 0.075f)) * 0.35f;
            albedo *= lerp(1.0f, lerp(0.74f, 1.24f, strata), cliffW * 0.78f);
            s.n = normalize(s.n + float3(0.0f, 0.0f, (strata - 0.5f) * 0.42f * cliffW));
        }
        // Procedural sub-centimeter world-space micro-normal & grazing roughness synthesis on vanilla terrain/rock/bark:
        // gives 2011 meshes crisp sunlit micro-grain and tactile roughness variation without requiring 4K texture mods.
        if (z < 1800.0f) {
            const float mgFade = saturate(1.0f - z / 1800.0f) * (1.0f - wet.snow * 0.85f);
            const float mg0 = cNoise(wp * 0.14f);
            const float mgX = cNoise(wp * 0.14f + float3(0.45f, 0.0f, 0.0f)) - mg0;
            const float mgY = cNoise(wp * 0.14f + float3(0.0f, 0.45f, 0.0f)) - mg0;
            const float mgZ = cNoise(wp * 0.14f + float3(0.0f, 0.0f, 0.45f)) - mg0;
            s.n = normalize(s.n + float3(mgX, mgY, mgZ) * (0.42f * mgFade));
            s.alpha = clamp(s.alpha * lerp(1.0f, lerp(0.84f, 1.14f, microN), 0.55f * mgFade), 0.04f, 0.96f);
        }
        // Triplanar geological crag & fracture normal relief on steep blocky vanilla cliffs (base == 1 or 4, slope < 0.72).
        if (slope < 0.72f && z < 8000.0f) {
            const float cragFade = saturate((0.72f - slope) * 2.5f) * saturate(1.0f - z / 8000.0f);
            const float cx = cNoise(wp * 0.016f + float3(3.1f, 0.0f, 7.3f)) - cNoise(wp * 0.016f - float3(3.1f, 0.0f, 7.3f));
            const float cy = cNoise(wp * 0.016f + float3(0.0f, 3.1f, 11.7f)) - cNoise(wp * 0.016f - float3(0.0f, 3.1f, 11.7f));
            const float cz = cNoise(float3(wp.xy * 0.012f, wp.z * 0.042f)) * 2.0f - 1.0f;
            s.n = normalize(s.n + float3(cx * 0.32f, cy * 0.32f, cz * 0.16f) * cragFade);
        }
        // Physical microclimate-driven 3D procedural Nordic moss, alpine crustose lichen & damp water-edge patina
        // (100% world-space wp = p + camPosW.xyz, zero static texture mods, zero camera dependency):
        // moss grows on shaded/damp upward stone ledges; pale lichen colonizes dry exposed granite; damp rot darkens near-ground bases.
        if (base == 1u && sunColour.w > 0.5f && z < 5000.0f && wet.snow < 0.35f) {
            const float mossFade = saturate(1.0f - z / 5000.0f) * (1.0f - wet.snow * 2.5f);
            const float shadeHumid = lerp(0.55f, 1.35f, saturate(wet.wet * 0.85f + (1.0f - saturate(s.n.y * 0.5f + 0.5f)) * 0.45f));
            const float mMacro = cNoise(wp * 0.0075f + float3(4.1f, 9.7f, 2.3f));
            const float mTuft  = cNoise(wp * 0.085f + float3(13.3f, 2.7f, 6.1f));
            const float mossPatch = smoothstep(0.50f, 0.78f, mMacro * shadeHumid) * smoothstep(0.46f, 0.84f, slope);
            const float3 mossCol = lerp(float3(0.08f, 0.16f, 0.05f), float3(0.18f, 0.29f, 0.10f), mTuft);
            const float mossW = mossPatch * mossFade * 0.48f;
            albedo = lerp(albedo, mossCol, mossW);
            s.alpha = lerp(s.alpha, 0.90f, mossW);
            // Tufted 3D moss micro-normal perturbation so moss reads as velvety raised growth, not flat green paint.
            const float mDx = cNoise(wp * 0.14f + float3(0.4f, 0.0f, 0.0f)) - cNoise(wp * 0.14f - float3(0.4f, 0.0f, 0.0f));
            const float mDy = cNoise(wp * 0.14f + float3(0.0f, 0.4f, 0.0f)) - cNoise(wp * 0.14f - float3(0.0f, 0.4f, 0.0f));
            s.n = normalize(s.n + float3(mDx, mDy, 0.0f) * (0.45f * mossW));
            // Dry exposed alpine crustose lichen on steep/mid granite faces.
            const float lichenMask = smoothstep(0.68f, 0.88f, cNoise(wp * 0.022f + float3(21.7f, 5.1f, 17.3f)))
                                   * smoothstep(0.18f, 0.55f, slope) * (1.0f - mossPatch);
            const float3 lichenCol = float3(0.42f, 0.46f, 0.36f) * lerp(0.85f, 1.15f, microN);
            albedo = lerp(albedo, lichenCol, lichenMask * mossFade * 0.24f);
        }
        // Triplanar world-space micro-porosity & pebble/slate relief on close terrain & stone ruins (zero texture mods).
        if (z < 1800.0f && (base == 4u || base == 1u)) {
            const float poreFade = saturate(1.0f - z / 1800.0f) * (base == 4u ? 0.85f : 0.55f);
            const float p0 = cNoise(wp * 0.16f);
            const float pxN = cNoise(wp * 0.16f + float3(0.35f, 0.0f, 0.0f)) - p0;
            const float pyN = cNoise(wp * 0.16f + float3(0.0f, 0.35f, 0.0f)) - p0;
            const float fineP = cNoise(wp * 0.44f + 5.7f);
            s.n = normalize(s.n + float3(pxN, pyN, 0.0f) * (0.28f * poreFade));
            s.alpha = clamp(s.alpha + (fineP - 0.5f) * 0.16f * poreFade, 0.06f, 0.96f);
        }
        s.alpha = clamp(s.alpha + (macroN - 0.5f) * 0.14f * tileFade, 0.08f, 0.96f);
    }
    // True Robust Screen-Space Complex Parallax Occlusion Mapping (Tatarchuk GDC 2006):
    // 16-step view-ray heightfield raymarch + 4-step binary search refinement along the INTO-surface view tangent
    // (-vTan), guarded by linear view-Z plane prediction (works at steep grazing angles on close walls & roads where
    // raw non-linear |Depth - d| < 0.0006 previously aborted on step 0!), displacing Albedo & Normal up to 22px,
    // plus strong heightfield micro-normal reconstruction, crevice AO, and 10-step directional relief self-shadowing.
    if (!match && !isActorFeat && !isActorCloth && base != 5u && z < 2600.0f) {
        const float distFade = saturate(1.0f - z / 2600.0f) * (base == 2u ? 0.25f : 1.0f);
        const int2 dimMax = max(int2(dims.xy) - int2(1, 1), int2(0, 0));
        const float3 lw = float3(0.2126f, 0.7152f, 0.0722f);
        const float3 upRef = abs(s.n.z) < 0.92f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
        const float3 tVec = normalize(cross(upRef, s.n));
        const float3 bVec = cross(s.n, tVec);
        // Linear view-Z plane slope for silhouette-safe grazing-angle plane prediction.
        const int2 pInt = int2(px);
        const float zR = viewZOf(Depth.Load(int3(min(pInt.x + 1, dimMax.x), pInt.y, 0))) - z;
        const float zL = z - viewZOf(Depth.Load(int3(max(pInt.x - 1, 0), pInt.y, 0)));
        const float zD = viewZOf(Depth.Load(int3(pInt.x, min(pInt.y + 1, dimMax.y), 0))) - z;
        const float zU = z - viewZOf(Depth.Load(int3(pInt.x, max(pInt.y - 1, 0), 0)));
        const float2 zSlope = float2(abs(zR) < abs(zL) ? zR : zL, abs(zD) < abs(zU) ? zD : zU);
        const float zPlaneTol = max(z * 0.055f, 4.5f);

        float2 pomPx = px;
        const bool useRealHeight = (base == 6u && sp.y > 0.002f);
        // Smooth 5-tap macro heightfield kernel (eliminates 1-pixel wood-grain / stone-speckle "fuzzy spikes" during POM).
        const float2 invDim = dims.zw;
        float hFinal = useRealHeight ? saturate(sp.y) : saturate((dot(albedo, lw) - 0.05f) * 2.35f);
        if ((base == 1u || base == 4u || base == 6u) && z < 1500.0f) {
            const float pomFade = saturate(1.0f - z / 1500.0f) * (useRealHeight ? 0.95f : (base == 4u ? 0.85f : 0.48f));
            const float nvPom = max(abs(dot(s.n, s.v)), 0.35f);
            // Restrained physical relief depth: 2.6u (max 6px) on general wood/stone (no spiky wood planks or fuzzy hearths),
            // 5.2u (max 11px) on cobblestone terrain (base==4) and 6.5u (max 13px) on authored _p.dds parallax (base==6).
            const float worldDepth = useRealHeight ? 6.5f : (base == 4u ? 5.2f : 2.6f);
            const float maxRayPx   = useRealHeight ? 13.0f : (base == 4u ? 11.0f : 6.0f);
            const float3 vInto = -s.v + s.n * dot(s.v, s.n);
            const float4 sEnd = screenOf(p + vInto * (worldDepth * pomFade / nvPom));
            float2 vRayPx = (sEnd.w > 1.0f) ? (sEnd.xy - px) : float2(0.0f, 0.0f);
            const float rayLen = length(vRayPx);
            if (rayLen > maxRayPx) { vRayPx *= (maxRayPx / rayLen); }
            if (rayLen > 0.45f) {
                // Feature 3: Adaptive 16-step Steep POM + 4-step sub-pixel binary refinement (eliminates grazing stepping).
                float curLayer = 1.0f;
                const float dLayer = 1.0f / 16.0f;
                float2 curOff = 0.0f.xx;
                const float2 dOff = vRayPx * dLayer;
                bool hitPom = false;
                [unroll] for (int st = 0; st < 16; st++) {
                    curOff += dOff;
                    curLayer -= dLayer;
                    const float2 pSample = clamp(px + curOff, float2(1.0f, 1.0f), dims.xy - 2.0f);
                    const int3 spP = int3(int2(round(pSample)), 0);
                    const float zq = viewZOf(Depth.Load(spP));
                    const float zPred = z + zSlope.x * curOff.x + zSlope.y * curOff.y;
                    if (abs(zq - zPred) > zPlaneTol) { break; }
                    const float2 uvP = (pSample + 0.5f) * invDim;
                    float hCur;
                    if (useRealHeight) {
                        hCur = saturate(Spec.SampleLevel(Lin, uvP, 0.0f).y);
                    } else {
                        // 5-tap cross-filtered bilinear albedo luminance suppresses 1-pixel grain spikes cleanly.
                        const float hC = dot(Albedo.SampleLevel(Lin, uvP, 0.0f).rgb, lw);
                        const float hN = dot(Albedo.SampleLevel(Lin, uvP + float2( 1.5f,  0.0f) * invDim, 0.0f).rgb, lw)
                                       + dot(Albedo.SampleLevel(Lin, uvP + float2(-1.5f,  0.0f) * invDim, 0.0f).rgb, lw)
                                       + dot(Albedo.SampleLevel(Lin, uvP + float2( 0.0f,  1.5f) * invDim, 0.0f).rgb, lw)
                                       + dot(Albedo.SampleLevel(Lin, uvP + float2( 0.0f, -1.5f) * invDim, 0.0f).rgb, lw);
                        hCur = saturate(((hC * 0.40f + hN * 0.15f) - 0.05f) * 2.35f);
                    }
                    if (hCur >= curLayer) { hitPom = true; break; }
                }
                if (hitPom) {
                    // 4-step sub-pixel binary search refinement with bilinear texture sampling (zero jagged stair-step slivers).
                    float2 loOff = curOff - dOff, hiOff = curOff;
                    float loL = curLayer + dLayer, hiL = curLayer;
                    [unroll] for (int bi = 0; bi < 4; bi++) {
                        const float2 midOff = 0.5f * (loOff + hiOff);
                        const float midL = 0.5f * (loL + hiL);
                        const float2 pM = clamp(px + midOff, float2(1.0f, 1.0f), dims.xy - 2.0f);
                        const float zM = viewZOf(Depth.Load(int3(int2(round(pM)), 0)));
                        const float zPredM = z + zSlope.x * midOff.x + zSlope.y * midOff.y;
                        const float2 uvM = (pM + 0.5f) * invDim;
                        const float hM = abs(zM - zPredM) < zPlaneTol
                            ? (useRealHeight ? saturate(Spec.SampleLevel(Lin, uvM, 0.0f).y)
                                             : saturate((dot(Albedo.SampleLevel(Lin, uvM, 0.0f).rgb, lw) - 0.05f) * 2.35f))
                            : midL;
                        if (hM >= midL) { hiOff = midOff; hiL = midL; } else { loOff = midOff; loL = midL; }
                    }
                    const float2 pF = clamp(px + hiOff, float2(1.0f, 1.0f), dims.xy - 2.0f);
                    const float zF = viewZOf(Depth.Load(int3(int2(round(pF)), 0)));
                    const float zPredF = z + zSlope.x * hiOff.x + zSlope.y * hiOff.y;
                    if (abs(zF - zPredF) < zPlaneTol) {
                        pomPx = pF;
                        const float2 uvF = (pF + 0.5f) * invDim;
                        const float3 dispAlb = Albedo.SampleLevel(Lin, uvF, 0.0f).rgb;
                        const float aLumD = max(dot(dispAlb, lw), 1.0e-4f);
                        const float3 dispExp = saturate(dispAlb * (saturate(pow(aLumD, 1.12f) * 1.14f) / aLumD));
                        albedo = lerp(albedo, dispExp, pomFade * 0.85f);
                        s.n = normalize(lerp(s.n, worldNormal(Normal.SampleLevel(Lin, uvF, 0.0f)), pomFade * 0.72f));
                        hFinal = useRealHeight ? saturate(Spec.SampleLevel(Lin, uvF, 0.0f).y) : saturate((aLumD - 0.05f) * 2.35f);
                        albedo *= lerp(0.68f, 1.08f, lerp(1.0f, hFinal, pomFade * 0.75f));
                    }
                }
            }
        }
        const int2 pBase = clamp(int2(round(pomPx)), int2(0, 0), dimMax);
        const int3 ipL = int3(clamp(pBase + int2(-1, 0), int2(0, 0), dimMax), 0);
        const int3 ipR = int3(clamp(pBase + int2( 1, 0), int2(0, 0), dimMax), 0);
        const int3 ipU = int3(clamp(pBase + int2( 0,-1), int2(0, 0), dimMax), 0);
        const int3 ipD = int3(clamp(pBase + int2( 0, 1), int2(0, 0), dimMax), 0);
        const float hc = useRealHeight ? hFinal : dot(albedo, lw);
        const float zBase = viewZOf(Depth.Load(int3(pBase, 0)));
        const float hL = abs(viewZOf(Depth.Load(ipL)) - (zBase - zSlope.x)) < zPlaneTol ? (useRealHeight ? Spec.Load(ipL).y : dot(Albedo.Load(ipL).rgb, lw)) : hc;
        const float hR = abs(viewZOf(Depth.Load(ipR)) - (zBase + zSlope.x)) < zPlaneTol ? (useRealHeight ? Spec.Load(ipR).y : dot(Albedo.Load(ipR).rgb, lw)) : hc;
        const float hU = abs(viewZOf(Depth.Load(ipU)) - (zBase - zSlope.y)) < zPlaneTol ? (useRealHeight ? Spec.Load(ipU).y : dot(Albedo.Load(ipU).rgb, lw)) : hc;
        const float hD = abs(viewZOf(Depth.Load(ipD)) - (zBase + zSlope.y)) < zPlaneTol ? (useRealHeight ? Spec.Load(ipD).y : dot(Albedo.Load(ipD).rgb, lw)) : hc;
        const float gx = clamp((hR - hL) * 2.6f, -0.55f, 0.55f) * distFade;
        const float gy = clamp((hD - hU) * 2.6f, -0.55f, 0.55f) * distFade;
        s.n = normalize(s.n + (-gx * tVec - gy * bVec) * 0.62f);
        const float microSlope = saturate(length(float2(gx, gy)) * 1.4f);
        s.alpha = clamp(s.alpha + (microSlope - 0.15f) * 0.12f * distFade, 0.05f, 0.95f);
        // Feature 4: 8-step directional POM relief self-shadowing along BOTH exterior sun AND brightest interior hearth/torch!
        if ((base == 1u || base == 4u || base == 6u) && z < 1800.0f && (sunDir.w > 0.5f || flags.y > 0u)) {
            const float pomFade = saturate(1.0f - z / 1800.0f) * (useRealHeight ? 0.92f : (base == 4u ? 0.85f : 0.65f));
            float3 pomLDir = sunDir.xyz;
            if (sunDir.w <= 0.5f && flags.y > 0u) {
                float3 bestDir = float3(0.0f, 0.0f, 1.0f);
                float bestScore = 0.0f;
                const uint maxPL = min(flags.y, 16u);
                [loop] for (uint pli = 0u; pli < maxPL; ++pli) {
                    const float3 dl = lightPosR[pli].xyz - p;
                    const float d2 = dot(dl, dl);
                    const float r2 = lightPosR[pli].w * lightPosR[pli].w;
                    if (d2 < r2 && d2 > 1.0f) {
                        const float sc = (lightColS[pli].r + lightColS[pli].g + lightColS[pli].b) * (1.0f - d2 / r2);
                        if (sc > bestScore) { bestScore = sc; bestDir = dl * rsqrt(d2); }
                    }
                }
                pomLDir = bestDir;
            }
            const float4 sSun = screenOf(p + (pomLDir - s.n * dot(pomLDir, s.n)) * 5.0f);
            const float2 sunDirPx = (sSun.w > 1.0f && length(sSun.xy - px) > 0.05f) ? normalize(sSun.xy - px) : float2(0.707f, -0.707f);
            const float2 stepPx = sunDirPx * 1.35f;
            float maxH = hFinal;
            [unroll] for (int pi = 1; pi <= 8; pi++) {
                const float2 sOff = stepPx * float(pi);
                const float2 pS = clamp(pomPx + sOff, float2(1.0f, 1.0f), dims.xy - 2.0f);
                const float zS = viewZOf(Depth.Load(int3(int2(round(pS)), 0)));
                const float zPredS = zBase + zSlope.x * sOff.x + zSlope.y * sOff.y;
                if (abs(zS - zPredS) < zPlaneTol) {
                    const float2 uvS = (pS + 0.5f) * invDim;
                    const float hs = useRealHeight ? saturate(Spec.SampleLevel(Lin, uvS, 0.0f).y)
                                                   : saturate((dot(Albedo.SampleLevel(Lin, uvS, 0.0f).rgb, lw) - 0.05f) * 2.35f);
                    maxH = max(maxH, hs - 0.065f * float(pi));
                }
            }
            const float pomOcc = saturate(1.0f - max(maxH - hFinal, 0.0f) * 2.2f * pomFade);
            albedo *= lerp(0.58f, 1.0f, pomOcc);
            s.alpha = clamp(lerp(min(s.alpha + 0.20f, 0.96f), max(s.alpha - 0.12f, 0.10f), pomOcc), 0.08f, 0.96f);
        }
    }
    // 2027 Geometric Curvature Cavity AO & Convex Edge-Wear Micro-Detail Synthesis (upgrades 2011 low-poly meshes & normals):
    // Darkens recessed stone/timber/armor crevices via analytic normal divergence and lifts crisp metallic/stone bevels.
    if (!match && (base == 1u || base == 2u || base == 4u) && z < 2400.0f) {
        const float3 dpx = ddx(p), dpy = ddy(p);
        const float3 dnxC = ddx(s.n), dnyC = ddy(s.n);
        const float pxScale = max(length(dpx) + length(dpy), 0.25f);
        const float curv = clamp((dot(dnxC, dpx) + dot(dnyC, dpy)) / max(pxScale * pxScale * 0.25f, 0.15f), -1.0f, 1.0f);
        const float curvFade = saturate(1.0f - z / 2400.0f);
        const float cavity = saturate(-curv * 0.85f) * curvFade;
        const float bevel  = saturate(curv * 0.65f) * curvFade * (base == 2u ? 1.0f : 0.45f);
        albedo *= (1.0f - cavity * 0.24f + bevel * 0.07f);
        s.alpha = clamp(s.alpha + cavity * 0.10f - bevel * 0.08f, 0.06f, 0.96f);
    }
    // Physical Decal Grounding & Glacial Ice/Glacier Internal Subsurface Scattering + Crystal Fractures:
    if (!match && (base == 1u || base == 4u) && z < 2400.0f) {
        const float bloodMask = saturate((albedo.r - max(albedo.g, albedo.b) * 1.85f) * 6.0f) * smoothstep(0.06f, 0.22f, albedo.r) * (1.0f - smoothstep(0.38f, 0.55f, albedo.r));
        if (bloodMask > 0.05f) {
            s.alpha = lerp(s.alpha, 0.16f, bloodMask * 0.82f);
            s.f0 = lerp(s.f0, 0.042f, bloodMask * 0.75f);
            albedo *= lerp(1.0f.xxx, float3(0.88f, 0.78f, 0.78f), bloodMask * 0.5f);
        }
        // Deep glacial ice & frozen glacier shelves (Cluster C Iceland ice): parallax-shifted internal crystal fractures
        // + glossy clearcoat ice specular + luminous blue-turquoise subsurface transmission!
        const float iceMask = saturate((albedo.b - albedo.r * 1.10f) * 5.5f) * smoothstep(0.24f, 0.48f, albedo.b) * (1.0f - wet.snow * 0.85f);
        if (iceMask > 0.06f) {
            const float3 wpIce = (p + camPosW.xyz) - s.v * 14.0f;
            const float crack = abs(cNoise(wpIce * 0.045f) * 2.0f - 1.0f);
            const float vein = exp(-crack * 14.0f);
            const float3 glacialSSS = float3(0.16f, 0.58f, 0.82f) * (0.35f + 0.65f * vein);
            albedo = lerp(albedo, lerp(albedo * float3(0.82f, 0.96f, 1.12f), glacialSSS, 0.34f), iceMask * 0.78f);
            s.alpha = lerp(s.alpha, 0.14f, iceMask * 0.72f);
            s.f0 = lerp(s.f0, 0.032f, iceMask * 0.65f);
        }
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
    float4 rv = rt ? RtVis.Load(ip) : 1.0f.xxxx;
    const uint3 rIdx = rt ? uint3(RtIdx.Load(ip).xyz * 255.0f + 0.5f) : uint3(0u, 0u, 0u);
    // Light-ID-matched depth-aware 3x3 bilateral filter on RT sun shadow (rv.x) and point-light RT shadows (rv.yzw):
    // prevents cross-light shadow blending when adjacent pixels rank point lights in different slot orders.
    if (rt) {
        float4 pvSum = rv * 2.0f;
        float4 pvW = 2.0f.xxxx;
        const int2 dMaxS = int2(dims.xy) - 1;
        [unroll] for (int sy = -1; sy <= 1; sy++) {
            [unroll] for (int sx = -1; sx <= 1; sx++) {
                if (sx == 0 && sy == 0) continue;
                const int3 spS = int3(clamp(ip.xy + int2(sx, sy), int2(0, 0), dMaxS), 0);
                if (abs(viewZOf(Depth.Load(spS)) - z) < max(z * 0.04f, 3.0f)) {
                    const float4 nRv = RtVis.Load(spS);
                    const uint3 nIdx = uint3(RtIdx.Load(spS).xyz * 255.0f + 0.5f);
                    pvSum.x += nRv.x; pvW.x += 1.0f;
                    if (nIdx.x == rIdx.x) { pvSum.y += nRv.y; pvW.y += 1.0f; }
                    if (nIdx.y == rIdx.y) { pvSum.z += nRv.z; pvW.z += 1.0f; }
                    if (nIdx.z == rIdx.z) { pvSum.w += nRv.w; pvW.w += 1.0f; }
                }
            }
        }
        rv = pvSum / pvW;
    }
    // Every light past the three traced ones: the share of their light that reaches the pixel, from one ray a
    // pixel to a light picked by its share, filtered with the sky rays (RtShadow.hlsl, RtSkyFilter.hlsl).
    const float rawRestA = RtBounce.Load(ip).a;
    const float restVis = rt && (flags.x & FLAG_REST) != 0u ? (rawRestA > 1.5f ? saturate(floor(rawRestA * 0.5f) / 15.0f) : rawRestA) : 1.0f;
    float3 diff = 0.0f, spec = 0.0f;
    float sunShadow = 1.0f;
    if (sunDir.w > 0.5f) {
        // Interiors have no cascades; channel 0 of their mask belongs to a shadowed point light, so the
        // directional light there is unshadowed, as the game lights it.
        sunShadow = (flags.x & FLAG_SHADOWS) == 0u ? 1.0f : (flags.x & FLAG_CASCADES) != 0u ? cascadeShadow(p, d, true) : m4.x;
        // Pure Native Hardware DXR 1.2 Sun Shadows when RT is active (zero screen-space contact shadow hacks!).
        // screenShadow runs ONLY as a fallback when hardware RT is explicitly disabled.
        if (!match && !rt && (flags.x & FLAG_CONTACT) != 0u && base != 5u && !isActorFeat && !isActorCloth && sunShadow > 0.0f && dot(s.n, sunDir.xyz) > 0.0f) {
            sunShadow *= screenShadow(p + lift, sunDir.xyz, tune.w, 0.02f, jit, 12u);
        }
        // Skinned actors (skin/hair/eye AND skinned clothing) keep the game's filtered shadow mask so coarse low-poly
        // skinned BLAS triangles never cast flat triangular self-shadow acne onto NPC tunics/shirts/backs.
        if (rt && sunColour.w > 0.5f && !isActorFeat && !isActorCloth) { sunShadow = min(sunShadow, rv.x); }
        if (!match && sunColour.w > 0.5f && (flags.x & FLAG_CLOUDS) != 0u) { sunShadow *= pow(saturate(cloudShadow(p + camPosW.xyz)), 1.25f); }   // SKY-02 rolling cloud-shadow contrast
        // Skin: warm dermal penumbra + gentle wrap so shadow transitions on faces and shoulders stay lifelike, never muddy grey.
        const float3 sunSh = skin && !match ? pow(saturate(sunShadow).xxx, float3(0.72f, 0.92f, 1.04f)) : sunShadow.xxx;
        // True engine-synchronized Sun vs Moon direct-lighting separation driven by Sky::currentGameHour (+0x1B0, astroSunElev):
        // Prevents ShadowSceneNode->sunLight's night-time Moon flip from ever receiving daytime D65 sunlight gain/color,
        // and drives authentic golden-coral sunrise/sunset direct warmth as astroSunElev crosses 0 deg!
        const float dirLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float aElevL = astroSunElev();
        const float nightDir = (!match && sunColour.w > 0.5f) ? max(smoothstep(0.03f, -0.06f, aElevL), saturate((0.22f - dirLum) * 4.5f)) : 0.0f;
        const float goldenW = (!match && sunColour.w > 0.5f) ? (smoothstep(-0.04f, 0.04f, aElevL) * (1.0f - smoothstep(0.14f, 0.34f, aElevL))) : 0.0f;
        float3 alignedDayCol = sunColour.rgb;
        if (!match && sunColour.w > 0.5f && nightDir < 0.95f) {
            const float elevDegD = max(degrees(asin(saturate(max(aElevL, 0.02f)))), 1.5f);
            const float mD = 1.0f / (sin(radians(elevDegD)) + 0.50572f * pow(elevDegD + 6.07995f, -1.6364f));
            const float3 ewTrans = exp(-(0.20f * float3(0.175f, 0.410f, 1.0f) + 0.025f * lerp(1.0f, 4.5f, saturate(clouds.x))) * mD);
            const float highSun = smoothstep(0.22f, 0.48f, aElevL);
            const float3 d65Spec = lerp(ewTrans, float3(0.96f, 0.98f, 1.02f), highSun * 0.78f);
            const float ewLum = max(dot(d65Spec, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
            const float middayTrim = lerp(1.0f, 0.88f, highSun);
            alignedDayCol = lerp(sunColour.rgb, d65Spec * (max(dirLum, 0.05f) / ewLum), 0.62f * (1.0f - nightDir)) * middayTrim;
            alignedDayCol = lerp(alignedDayCol, alignedDayCol * float3(1.32f, 0.84f, 0.52f), goldenW * 0.68f);
        }
        // Disciplined cool silver-blue moonlight at night (Commons Cluster A: lets warm torches/hearths/windows dominate night scenes).
        const float3 moonDirectCol = float3(0.22f, 0.30f, 0.50f) * clamp(dirLum * 0.75f, 0.14f, 0.48f);
        const float sunHdrGain = (!match && sunColour.w > 0.5f) ? lerp(1.20f, 1.00f, saturate(clouds.x * 1.15f)) * (1.0f - nightDir * 0.42f) : 1.0f;
        const float3 dirCol = lerp(alignedDayCol * sunHdrGain, moonDirectCol, nightDir);
        // Chan 2018 micro-shadowing: uses fine-scale AO to shadow crevices/mortar/bark where coarse shadow maps cannot reach.
        const float microSh = (!match && !isActorFeat && base != 5u) ? saturate(2.0f * AoHist.Load(ip).x + saturate(dot(s.n, sunDir.xyz)) - 0.85f) : 1.0f;
        const float sunPunch = (!match && sunColour.w > 0.5f) ? lerp(1.16f, 0.85f, nightDir) : 1.0f;
        addLight(s, sunDir.xyz, dirCol * (sunSh * lerp(0.50f, 1.0f, microSh) * sunPunch), dirCol * sunPunch, diff, spec);
        // MAT-07 Distributed Microfacet Snow & Hoar-Frost Diamond Glints (Deliot & Belcour 2023):
        // crisp sunlit crystalline sparkle on snowpacks & frost (<1,600u) with zero temporal shimmer.
        if (!match && sunColour.w > 0.5f && z < 1600.0f && sunShadow > 0.25f && nightDir < 0.75f &&
            (wet.snow > 0.25f || (base == 4u && min(albedo.r, min(albedo.g, albedo.b)) > 0.42f && s.n.z > 0.55f))) {
            const float3 wpGl = (p + camPosW.xyz) * 0.42f;
            const float3 gCell = floor(wpGl);
            const float3 gRand = float3(cHash(gCell), cHash(gCell + 19.3f), cHash(gCell + 47.1f)) - 0.5f;
            const float3 microN = normalize(s.n + gRand * 0.65f);
            const float3 hGl = normalize(s.v + sunDir.xyz);
            const float glint = pow(saturate(dot(microN, hGl)), 96.0f) * step(0.82f, cHash(gCell + 83.7f));
            spec += dirCol * (glint * sunShadow * saturate(1.0f - z / 1600.0f) * (1.0f - nightDir) * 1.85f);
        }
        // Rich dual-lobe translucent chlorophyll subsurface scattering + sunlit canopy crown rim + waxy leaf-cuticle sheen (base == 5u).
        if (!match && base == 5u && sunColour.w > 0.5f) {
            const float backF = pow(saturate(dot(-s.v, sunDir.xyz)), 2.8f);
            const float broadF = saturate(dot(-s.v, sunDir.xyz) * 0.5f + 0.5f);
            const float crownUp = saturate(s.n.z * 0.75f + 0.25f);
            diff += dirCol * float3(0.54f, 0.98f, 0.30f) * ((backF * 0.44f + broadF * 0.09f) * sunShadow * (1.0f - nightDir * 0.78f));
            // Direct sunlit crown highlight (breaks up flat 2011 shaded foliage with crisp golden top-canopy direct lighting):
            diff += dirCol * (crownUp * sunShadow * 0.22f * (1.0f - nightDir * 0.70f));
            const float3 hLeaf = normalize(s.v + sunDir.xyz);
            const float leafCuticle = pow(saturate(dot(s.n, hLeaf)), 32.0f) * 0.26f;
            spec += dirCol * (leafCuticle * sunShadow * (0.30f + 0.70f * crownUp));
        }
        // Marschner dual-lobe shifted hair specular (base == 11: primary colorless cuticle band + warm tinted secondary band).
        if (!match && base == 11u && sunShadow > 0.05f) {
            const float3 hHair = normalize(s.v + sunDir.xyz);
            const float3 tHair = normalize(cross(s.n, float3(0.0f, 0.0f, 1.0f) + 1.0e-4f));
            const float3 t1 = normalize(tHair + s.n * 0.10f);
            const float3 t2 = normalize(tHair - s.n * 0.14f);
            const float s1 = pow(sqrt(max(1.0f - pow(dot(t1, hHair), 2.0f), 0.0f)), 48.0f);
            const float s2 = pow(sqrt(max(1.0f - pow(dot(t2, hHair), 2.0f), 0.0f)), 18.0f);
            spec += dirCol * (s1 * 0.22f + albedo * s2 * 0.34f) * sunShadow;
        }
        if (skin && !match) {
            // Dual-lobe character skin shading (Callisto Protocol / SKN-09): warm dermal wrap scatter +
            // subtle secondary oil/pore micro-sheen lobe (alpha=0.18) for lifelike faces & hands (never chalky or plastic).
            const float wrapSun = saturate((dot(s.n, sunDir.xyz) + 0.35f) / 1.35f);
            diff += dirCol * float3(0.20f, 0.095f, 0.062f) * wrapSun * (0.35f + 0.65f * sunShadow);
            const float3 hSkin = normalize(s.v + sunDir.xyz);
            const float nhSk = saturate(dot(s.n, hSkin));
            const float ddSk = nhSk * nhSk * (0.0324f - 1.0f) + 1.0f;
            const float dSk = 0.0324f / (PI * ddSk * ddSk);
            const float fSk = 0.028f + 0.972f * pow(1.0f - saturate(dot(s.v, hSkin)), 5.0f);
            spec += dirCol * (min(dSk * fSk * 0.14f, 1.4f) * sunShadow * saturate(dot(s.n, sunDir.xyz)));
        }
        // Physical Character Eye Cornea Wetness + Iris Caustic Catchlight (base == 12u, SKN-09):
        if (base == 12u && !match) {
            const float3 hEye = normalize(s.v + sunDir.xyz);
            const float nhE = saturate(dot(s.n, hEye));
            const float ddE = nhE * nhE * (0.0012f - 1.0f) + 1.0f;
            const float cornea = min(0.0012f / (PI * ddE * ddE), 18.0f) * (0.04f + 0.96f * pow(1.0f - saturate(dot(s.v, hEye)), 5.0f));
            spec += dirCol * (cornea * 0.28f * (0.35f + 0.65f * sunShadow));
            diff += dirCol * float3(0.14f, 0.08f, 0.07f) * (0.25f * saturate(dot(s.n, sunDir.xyz) + 0.4f));
        }
        if (wet.snow > 0.0f && sunShadow > 0.0f) {
            // arXiv:2505.08985 (Wu/Luan/Hasan/Ramamoorthi 2025) Dual-Scale Position-Normal Manifold Micro-Glints:
            // Resolves crisp footprint-clamped snow crystal sparkle without temporal pixel-shimmer.
            const float fp = max(1.2f, z * 0.0025f);
            const float gFine = snowGlint(p + camPosW.xyz, s.n, s.v, sunDir.xyz, fp);
            const float gBroad = snowGlint(p + camPosW.xyz + 13.7f, s.n, s.v, sunDir.xyz, fp * 2.1f) * 0.45f;
            spec += sunColour.rgb * (sunShadow * wet.snow * 2.4f * (gFine + gBroad));
        }
        // Forged metal micro-flake glint + Belcour-Barla (SIGGRAPH 2017) Airy Thin-Film Spectral Iridescence
        // + MAT-01 Substrate Dual-Lobe Clearcoat/Lacquer on glass, glazed ceramics, polished stone & forged armor!
        if (!match && sunShadow > 0.05f && z < 1800.0f && (base == 2u || wet.wet > 0.35f || s.alpha < 0.22f)) {
            const float nvIr = saturate(dot(s.n, s.v));
            const float filmOpd = (base == 2u ? 480.0f : 360.0f) * sqrt(max(1.0f - (1.0f - nvIr * nvIr) * 0.56f, 0.1f));
            const float3 airy = 0.5f + 0.5f * cos(6.2831853f * filmOpd / float3(615.0f, 540.0f, 465.0f));
            const float irStr = (base == 2u ? 0.24f : 0.14f) * pow(1.0f - nvIr, 2.2f) * saturate(1.0f - z / 1800.0f);
            spec += dirCol * airy * (irStr * sunShadow);
            // Crisp dual-lobe clearcoat highlight (roughness 0.06 mirror coat over underlying material lobe):
            if (!skin && s.alpha < 0.25f) {
                const float3 hCoat = normalize(s.v + sunDir.xyz);
                const float nhC = saturate(dot(s.n, hCoat));
                const float ddC = nhC * nhC * (0.0036f - 1.0f) + 1.0f;
                const float fCoat = 0.04f + 0.96f * pow(1.0f - saturate(dot(s.v, hCoat)), 5.0f);
                const float dCoat = 0.0036f / (PI * ddC * ddC);
                spec += dirCol * (min(dCoat * fCoat * 0.16f, 2.2f) * sunShadow * saturate(dot(s.n, sunDir.xyz)));
            }
            if (base == 2u && z < 1600.0f) {
                const float mGlint = snowGlint(p + camPosW.xyz, s.n, s.v, sunDir.xyz, max(0.8f, z * 0.0018f));
                spec += sunColour.rgb * (sunShadow * mGlint * 0.75f * saturate(1.0f - z / 1600.0f));
            }
        }
        // 2028 Reflective Water-Bounce Caustics (engine TESObjectCELL::waterHeight = engineWorld.x, Commons guycalledfrank / WAT-07):
        // Sunlit water surfaces bounce animated shimmering light ribbons UPWARD onto bridge undersides, dock pilings,
        // sawmill wheels, overhangs & riverbanks within 0..240u above the live cell water plane!
        if (!match && sunColour.w > 0.5f && engineWorld.z > 0.5f && engineWorld.x > -13000.0f && z < 3200.0f && nightDir < 0.75f) {
            const float3 wpC = p + camPosW.xyz;
            const float aboveW = wpC.z - engineWorld.x;
            if (aboveW > -4.0f && aboveW < 240.0f && s.n.z < 0.55f) {
                const float hFade = smoothstep(-4.0f, 12.0f, aboveW) * (1.0f - smoothstep(70.0f, 240.0f, aboveW));
                const float downFace = saturate((0.55f - s.n.z) * 1.45f);
                const float2 cUv = wpC.xy * 0.048f - normalize(cloudWind.xy + float2(1.0f, 0.35f)) * (camPosW.w * 0.85f);
                const float c1 = sin(cUv.x * 2.1f + sin(cUv.y * 1.9f + camPosW.w * 1.6f));
                const float c2 = cos(cUv.y * 2.5f - cos(cUv.x * 2.3f - camPosW.w * 1.3f));
                const float caustNet = pow(saturate(c1 * c2 * 0.5f + 0.5f), 4.0f);
                const float clearSunW = 1.0f - saturate(clouds.x * 0.78f);
                diff += dirCol * float3(0.55f, 0.92f, 1.00f) * (caustNet * hFade * downFace * clearSunW * (1.0f - nightDir) * 0.42f);
            }
        }
    }

    const uint4 bits = clusters[clusterOf(px, z)];
    uint softLeft = 4u;
    float fireMelt = 0.0f;
    [loop] for (uint w = 0u; w < 4u; w++) {
        // SM6.9 Wave-scalarized clustered light loop: WaveActiveBitOr keeps all lanes in lockstep so
        // lightPosR[li] / lightColS[li] compile to uniform SGPR scalar broadcasts with zero warp divergence.
        uint b = WaveActiveBitOr(bits[w]);
        [loop] while (b != 0u) {
            const uint li = w * 32u + firstbitlow(b);
            b &= b - 1u;
            if (li >= flags.y) { continue; }
            const float3 toL = lightPosR[li].xyz - p;
            const float dist = length(toL);
            const float rNorm = saturate(dist / lightPosR[li].w);
            // Full-reach physical point-light throw (restores real torch/hearth/lantern/spell light cast across rooms & streets;
            // previous (win*win)/(1+1.45*rNorm^2) crushed 50-70% of light reach past 40% radius!).
            const float win = saturate(1.0f - rNorm * rNorm);
            const float att = pow(win, 1.35f) * 1.28f;
            if (att <= 0.0f) { continue; }
            // Native campfire/torch heat snow melt: warm firelights melt surrounding snow into glistening damp ground.
            if (!match && wet.snow > 0.01f && lightColS[li].r > lightColS[li].b * 1.25f) {
                fireMelt = max(fireMelt, saturate(att * 1.65f));
            }
            const float3 l = toL / max(dist, 1.0e-4f);
            float sh = 1.0f;
            const int ch = int(lightColS[li].w);
            if (ch >= 0 && ch <= 3) { sh = m4[ch]; }   // interiors without sun shadows use channel 0 too
            float3 c = lightColS[li].rgb * att;
            // Rich 2200K-2600K blackbody firelight spectrum (hearths, torches, candles, braziers, forges):
            // Removed duplicate high-frequency 6.3 rad/s shader flicker (rt_livestate.cpp already drives silky 0.12-EMA breath!).
            if (!match && lightColS[li].r > lightColS[li].b * 1.25f) {
                const bool isIntLight = (sunColour.w < 0.5f);
                const float3 fireBoost = skin ? float3(1.06f, 0.95f, 0.86f)
                                              : (isIntLight ? float3(1.60f, 0.92f, 0.42f) : float3(1.48f, 0.88f, 0.44f));
                c *= fireBoost;
            }
            const uint slot = li + 1u == rIdx.x ? 1u : li + 1u == rIdx.y ? 2u : li + 1u == rIdx.z ? 3u : 0u;
            // When hardware RT is active, bypass vanilla's flickering 4-channel shadow-mask swaps completely:
            // top-3 lights use their exact RT shadow vis (rv[slot]); secondary lights (4..128) use stable ReSTIR visibility.
            // On skinned actor clothing/skin, soften BLAS self-shadow to eliminate coarse low-poly triangle acne on backs/shoulders.
            if (rt) {
                // Real hardware DXR 1.2 point-light shadow cast (0.02 floor for primary top-3 RT lights, 0.06 for secondary ReSTIR;
                // eliminates washed-out shadow leakage while keeping skinned actor self-shadow softening free of BLAS acne):
                const float rtSh = slot != 0u ? lerp(0.02f, 1.0f, rv[slot]) : lerp(0.06f, 1.0f, restVis);
                sh = (isActorCloth || isActorFeat) ? lerp(0.45f, 1.0f, rtSh) : rtSh;
            }
            else if (slot != 0u) { sh = rv[slot]; }
            if (!match && !rt && sunColour.w > 0.5f && slot == 0u && (flags.x & FLAG_SOFT) != 0u && base != 5u && softLeft > 0u && dot(s.n, l) > 0.0f && max(c.r, max(c.g, c.b)) > 0.02f) {
                sh *= screenShadow(p + lift, l, min(dist, tune.z), 4.0f / max(dist, 1.0f), jit, 10u);
                --softLeft;
            }
            // GTA-VI-inspired Representative-Point Sphere Area-Light Specular (Karis 2013):
            // Replaces pinpoint local-light specular dots with broad, physical area-light highlights on tables, stone & armor.
            Surface sArea = s;
            if (!match && (flags.x & FLAG_GGX) != 0u) {
                const float rSrc = (lightColS[li].r > lightColS[li].b * 1.30f) ? 14.0f : 8.0f;
                const float aPrime = clamp(s.alpha + rSrc / max(2.0f * dist, 12.0f), s.alpha, 0.96f);
                const float normE = (s.alpha / max(aPrime, 0.02f));
                sArea.alpha = aPrime;
                float3 specTmp = 0.0f.xxx;
                addLight(sArea, l, c * sh, c, diff, specTmp);
                spec += specTmp * (normE * normE);
            } else {
                addLight(s, l, c * sh, c, diff, spec);
            }
        }
    }
    if (!match && fireMelt > 0.01f) {
        // Darken & gloss melted snow around campfires, braziers, and torches.
        albedo *= lerp(1.0f.xxx, float3(0.68f, 0.70f, 0.72f), fireMelt * wet.snow * 0.65f);
        wet.wet = max(wet.wet, fireMelt * wet.snow * 0.75f);
        wet.snow *= (1.0f - fireMelt * 0.85f);
    }

    float3 amb = float3(dot(ambient[0].xyz, s.n) + ambient[0].w, dot(ambient[1].xyz, s.n) + ambient[1].w,
                        dot(ambient[2].xyz, s.n) + ambient[2].w);
    // Pure Native Hardware DXR 1.2 Directional Skylighting & RTAO when RT is active (zero screen-space GTAO halos!):
    // Screen-space GTAO (AoHist) runs ONLY as a fallback when hardware RT sky/RTAO is disabled.
    const float ssaoMix = (rt && (flags.x & FLAG_SKY) != 0u) ? 0.0f : 1.0f;
    const float aoWeight = (base == 5u ? 0.78f : (isActorFeat ? 0.45f : 1.0f)) * ssaoMix;
    const float aoRaw = (flags.x & FLAG_AO) != 0u ? lerp(1.0f, AoHist.Load(ip).x, tune2.x * aoWeight) : 1.0f;
    const float aoTerm = !match ? pow(saturate(aoRaw), 1.22f) : aoRaw;
    // Exteriors: ray-traced sky visibility (RtSky.cpp). Interiors (!match): local SM6.9 RayQuery RTAO from RtShadow.hlsl.
    const bool isExterior = sunColour.w > 0.5f;
    const float rawSkyVis = rt && (flags.x & FLAG_SKY) != 0u && (isExterior || !match) ? RtIdx.Load(ip).w : 1.0f;
    const float skyVis = isActorFeat ? lerp(0.55f, 1.0f, rawSkyVis) : rawSkyVis;
    // Physical directional skylighting falloff: roofs, covered walkways, arches & dense foliage cores drop to deep rich shade
    // (0.10..1.0 exteriors, 0.16..1.0 velvety interior shadow drama) while open surfaces keep crisp skylighting.
    const float skyVisShaped = !match ? pow(saturate(skyVis), base == 5u ? 1.65f : 1.85f) : skyVis;
    const float rtOcc = isExterior ? lerp(1.0f, lerp(0.06f, 1.0f, skyVisShaped), max(cluster.w, 0.88f)) : lerp(0.11f, 1.0f, skyVisShaped);
    // Feature 18: Contact-hardening near-field micro-AO grounding under foliage, rocks, furniture & NPC feet.
    const float nearGroundAo = !match ? pow(saturate(aoTerm), lerp(1.0f, 1.32f, saturate(1.0f - z / 1600.0f))) : aoTerm;
    float combinedOcc = min(nearGroundAo, rtOcc);
    // Lifelike Vanilla NPCs: lift recessed eye-socket AO (base==12) so eyes never read as dim dark holes,
    // and soften harsh brow/under-eye portrait shadows on skin (base==10).
    if (!match) {
        if (base == 12u) combinedOcc = lerp(combinedOcc, 1.0f, 0.80f);
        else if (skin)   combinedOcc = lerp(combinedOcc, 1.0f, 0.36f);
    }
    // SIGGRAPH GI-12 Visibility-Aware Bent-Normal Hemisphere Skylighting (exteriors) &
    // Directional Vault/Hearth Ambient Shaping (interiors: deepens recessed corners & highlights upper masonry/beams).
    const float3 bentN = normalize(lerp(s.n, float3(0.0f, 0.0f, 1.0f), (1.0f - combinedOcc) * 0.38f * skyVis));
    const float hemiSky = !match ? (isExterior ? lerp(0.54f, 1.18f, saturate(bentN.z * 0.5f + 0.5f))
                                               : lerp(0.52f, 1.20f, saturate(bentN.z * 0.5f + 0.5f))) : 1.0f;
    // Keep multi-bounce AO subtle so deep crevices stay genuinely grounded instead of lifting back to muddy grey.
    const float3 mbOcc = lerp(combinedOcc.xxx, gtaoMultiBounce(combinedOcc, saturate(albedo)), 0.45f);
    const float ambHdrScale = (!match && isExterior) ? lerp(0.76f, 0.92f, saturate(clouds.x)) : 1.0f;
    amb *= match ? combinedOcc.xxx : mbOcc * hemiSky * ambHdrScale;
    // Multi-bounce hardware RayQuery GI: boosted indirect energy + color-bleed preservation + upward overhang bounce
    // so sunlit grass, cobblestone, snow, water, and warm tavern hearths cast tangible colored indirect light.
    const bool bounceHere = sunColour.w > 0.5f || (flags.x & FLAG_BOUNCE_IN) != 0u;
    float3 bounce = 0.0f.xxx;
    if (rt && (flags.x & FLAG_BOUNCE) != 0u && bounceHere) {
        const float3 rawB = RtBounce.Load(ip).rgb * proj.w;
        if (match) {
            bounce = rawB;
        } else {
            const float bGain = isExterior ? 2.15f : 2.35f;
            const float overhangLift = 1.0f + saturate(-s.n.z) * 0.45f;
            const float3 bScaled = rawB * (bGain * overhangLift);
            const float bLum = dot(bScaled, float3(0.2126f, 0.7152f, 0.0722f));
            // Preserve physical indirect chroma bleed (green grass, warm timber, golden firelight) without clipping.
            bounce = max(lerp(bLum.xxx, bScaled, 1.24f), 0.0f.xxx);
            const float bPeak = max(bounce.r, max(bounce.g, bounce.b));
            if (bPeak > 1.25f) { bounce *= (1.25f / bPeak); }
        }
    }
    // Unified sky-coupled hemisphere ambient (!match exteriors): locks world shadow & sky ambient directly
    // to our live skyPalette() zenith/horizon colors so terrain, roofs, and shadows always match the sky dome.
    if (!match && isExterior) {
        const float sunLumA = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float dayAmbW = saturate((sunDir.z - 0.01f) * 4.0f) * saturate((sunLumA - 0.20f) * 2.5f);
        if (dayAmbW > 0.02f) {
            float3 sZ, sM, sH;
            skyPalette(sZ, sM, sH);
            const float3 skyHemi = lerp(sH * 0.62f, lerp(sM, sZ, 0.45f), saturate(s.n.z * 0.5f + 0.5f));
            const float ambL = max(dot(amb, float3(0.2126f, 0.7152f, 0.0722f)), 0.02f);
            const float hemiL = max(dot(skyHemi, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
            const float inShadow = 1.0f - saturate(sunShadow);
            // Weight blue sky-ambient tint by skyVis so open shadows catch cerulean sky while covered porches stay warm neutral.
            amb = lerp(amb, skyHemi * (ambL / hemiL), dayAmbW * skyVis * lerp(0.20f, 0.34f, inShadow));
        }
    } else if (!match && !isExterior) {
        // Exterior Time-of-Day Interior Lighting Coupling (driven by live gameHour / sunElev in timeEnv.xy):
        // interior ambient shifts naturally across golden dawn -> crisp daylight -> amber dusk -> cool nocturnal indigo.
        const float sunE = timeEnv.y;
        const float dayInt = smoothstep(-0.14f, 0.28f, sunE);
        const float goldInt = smoothstep(-0.14f, 0.10f, sunE) * (1.0f - smoothstep(0.18f, 0.52f, sunE));
        const float outCool = saturate(clouds.x * 0.6f + weather.w * 0.5f);
        const float3 todIntTint = lerp(float3(0.76f, 0.86f, 1.14f),
                                       lerp(float3(1.04f, 1.05f, 1.08f), float3(1.18f, 0.92f, 0.72f), goldInt), dayInt);
        const float todIntGain = lerp(0.78f, 1.12f, dayInt);
        amb *= lerp(todIntTint, float3(0.88f, 0.95f, 1.06f), outCool * 0.35f) * todIntGain;
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
    if (flags.w == 22u) {
        // Mesh wireframe / geometric facet diagnostic: highlights crisp triangle & silhouette discontinuities in cyan-green.
        const float3 dnxW = ddx(nSmooth), dnyW = ddy(nSmooth);
        const float edgeN = smoothstep(0.18f, 0.65f, length(dnxW) + length(dnyW));
        const float edgeZ = smoothstep(0.015f, 0.06f, (abs(ddx(z)) + abs(ddy(z))) / max(z, 1.0f));
        const float wire = saturate(max(edgeN, edgeZ));
        return float4(lerp(albedo * 0.14f, float3(0.15f, 0.95f, 0.85f), wire), game.a);
    }
    if (flags.w == 23u) {
        // Volumetric cloud shadow & spectral scattering diagnostic on world surfaces.
        const float csW = (flags.x & FLAG_CLOUDS) != 0u ? cloudShadow(p + camPosW.xyz) : 1.0f;
        return float4(csW, saturate(dot(amb, 0.333f.xxx)), saturate(dot(diff, 0.333f.xxx)), game.a);
    }
    // Material & Texture Author Diagnostic Views (24..27) for ENB Complex Material / CS PBR / Bottled Shaders / Vanilla:
    if (flags.w == 24u) {
        // View 24: PBR Roughness map (dark = mirror/wet/polished metal, bright = matte stone/snow/fabric).
        return float4(s.alpha.xxx, game.a);
    }
    if (flags.w == 25u) {
        // View 25: Material classification & Metallic/Clearcoat map (R = conductor metallic, G = wet/lacquer clearcoat, B = dielectric).
        const float envL25 = dot(base == 2u ? max(SoftMap.Load(ip).rgb, 0.0f.xxx) : 0.0f.xxx, float3(0.2126f, 0.7152f, 0.0722f));
        const float met25 = (base == 2u) ? saturate((max(saturate(s.mask * resScale.w), envL25 * 1.35f) - 0.22f) * 1.95f) : 0.0f;
        const float cc25 = max(saturate(wet.wet * 0.85f + wet.puddle * 1.15f), (base == 2u ? (1.0f - met25) * 0.35f : 0.0f));
        return float4(met25, cc25, saturate(1.0f - max(met25, cc25)), game.a);
    }
    if (flags.w == 26u) {
        // View 26: Parallax Height Map Inspector (Cyan = authored _p.dds height from GBuffer SV_Target5.g on base=6; Gold = macro relief height).
        const bool hasPdds = (base == 6u && sp.y > 0.002f);
        const float h26 = hasPdds ? saturate(sp.y) : saturate((dot(albedo, float3(0.2126f, 0.7152f, 0.0722f)) - 0.05f) * 2.35f);
        return float4(hasPdds ? float3(0.15f, 0.90f, 1.0f) * h26 : float3(1.0f, 0.78f, 0.28f) * h26, game.a);
    }
    if (flags.w == 27u) {
        // View 27: SVGF GI Bounce & Sky-Visibility Occlusion Contrast Inspector (R = bounce luma, G = skyVis, B = AO).
        const float bL27 = saturate(dot(bounce, float3(0.2126f, 0.7152f, 0.0722f)) * 2.5f);
        return float4(bL27, skyVisShaped, aoTerm, game.a);
    }

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
    // True Complex PBR Material Diffuse & Specular IBL:
    // - Class-2 metals (steel, iron, silver, gold, Dwemer brass): physical conductor metallicity attenuates diffuse
    //   energy (1 - 0.72*m) and drives chromatic conductor F0 into Karis Split-Sum + Fdez-Aguera Multi-Scattering IBL
    //   sampled from our live 256x256 dual-band prefiltered Dynamic Environment Cubemap (EnvCube).
    // - Wet surfaces & oiled armor: dual-lobe Clearcoat GGX water/lacquer film sampling the sharp cubemap band.
    const float envLumM = dot(envRefl, float3(0.2126f, 0.7152f, 0.0722f));
    const float metallic = (!match && base == 2u) ? saturate((max(saturate(s.mask * resScale.w), envLumM * 1.35f) - 0.22f) * 1.95f) : 0.0f;
    const float3 diffAlb = albedo * (1.0f - metallic * 0.72f);
    const float3 rawLit = (diffAlb + envRefl * (match ? 1.0f : 0.08f)) * (diff + amb + effCharLight);
    float3 lit = match ? knee3(rawLit) : rawLit;
    // Physical glacial-cyan subsurface wrap transmission on sunlit snowbanks.
    if (!match && wet.snow > 0.05f && sunDir.w > 0.5f) {
        const float snowWrap = saturate((dot(s.n, sunDir.xyz) + 0.35f) / 1.35f);
        lit += sunColour.rgb * float3(0.18f, 0.34f, 0.46f) * (snowWrap * wet.snow * (0.35f + 0.65f * sunShadow) * 0.22f);
    }
    // True Interior Window-Lighting & HDR Window Bloom Driver (coupled to exterior time-of-day timeEnv.xy):
    // bright interior windows (GlowMapped base==3 or high-emissive vertical glass) radiate physical HDR sunlight by day
    // and cool moonlight by night, directly feeding BloomPass.hlsl (glow/bloom) & LightFog.hlsl (volumetric window shafts).
    if (!match && !isExterior && !skin && !isActorFeat && !isActorCloth) {
        const float gLumW = dot(game.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float isVertGlass = smoothstep(0.68f, 0.35f, abs(s.n.z));
        const float winMask = (base == 3u ? smoothstep(0.22f, 0.65f, gLumW) : smoothstep(0.55f, 0.95f, gLumW)) * isVertGlass;
        if (winMask > 0.02f) {
            const float sunE = timeEnv.y;
            const float dayW = smoothstep(-0.12f, 0.26f, sunE);
            const float goldW = smoothstep(-0.12f, 0.12f, sunE) * (1.0f - smoothstep(0.18f, 0.52f, sunE));
            const float3 winCol = lerp(float3(0.28f, 0.46f, 0.92f),
                                       lerp(float3(1.02f, 1.06f, 1.15f), float3(1.35f, 0.82f, 0.36f), goldW), dayW);
            const float winHdr = lerp(0.55f, 2.65f, dayW) * (1.0f - saturate(clouds.x * 0.38f));
            lit = lerp(lit, max(lit, game.rgb * winCol * winHdr), winMask);
        }
    }
    if (!match && skin) {
        // Per-channel dermal highlight roll-off preserves natural skin tone under direct midday sun and close hearth fire.
        lit = lit / (1.0f + max(lit - 0.68f.xxx, 0.0f.xxx) * 0.52f);
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
        // Frostbite (Lagarde 2014) specular occlusion: prevents fake sky/cubemap gloss inside deep occluded rock/timber crevices.
        const float specOcc = saturate(pow(nv + combinedOcc, exp2(-16.0f * s.alpha - 1.0f)) - 1.0f + combinedOcc);
        const float3 rDir = reflect(-s.v, nSmooth);
        const float envRough = max(s.alpha, base == 2u ? 0.14f : 0.08f);
        // Live 256x256 Dual-Band Prefiltered Dynamic Environment Cubemap (live sky + clouds + 32 dynamic lights + ground bounce).
        const float3 dynEnv = sampleLiveEnvCube(rDir, envRough);
        const float3 F0mat = lerp(s.f0.xxx, saturate(albedo * 1.15f + 0.12f), metallic);
        const float reflMask = max(saturate(s.mask * resScale.w), max(wet.puddle, 0.5f * wet.wet));
        // Feature 5: Anisotropic Brushed-Metal GGX + Gulbrandsen F82-Tint Conductor Fresnel on forged steel/armor/Dwemer brass (base==2).
        if (base == 2u && metallic > 0.15f) {
            const float3 tAniso = normalize(cross(nSmooth, abs(nSmooth.z) < 0.95f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f)));
            const float3 bAniso = cross(nSmooth, tAniso);
            const float3 keyL = isExterior && sunDir.w > 0.5f ? sunDir.xyz : normalize(s.v + float3(0.15f, 0.10f, 0.55f));
            const float3 hA = normalize(s.v + keyL);
            const float nhA = saturate(dot(nSmooth, hA));
            const float thA = dot(tAniso, hA), bhA = dot(bAniso, hA);
            const float ax = max(s.alpha * 1.45f, 0.08f), ay = max(s.alpha * 0.65f, 0.04f);
            const float denomA = (thA * thA) / (ax * ax) + (bhA * bhA) / (ay * ay) + nhA * nhA;
            const float dAniso = 1.0f / max(PI * ax * ay * denomA * denomA, 1.0e-4f);
            const float vhA = saturate(dot(s.v, hA));
            const float3 f82 = lerp(F0mat, 1.0f.xxx, pow(1.0f - vhA, 5.0f)) - F0mat * (0.14f * vhA * pow(1.0f - vhA, 6.0f));
            const float3 keyColA = isExterior ? (sunColour.rgb * sunShadow) : max(amb * 1.4f, 0.08f.xxx);
            lit += min(keyColA * f82 * (dAniso * saturate(dot(nSmooth, keyL)) * 0.065f * metallic * specOcc), 2.2f.xxx);
        }
        // Feature 6: Sony Imageworks Charlie Microfiber Sheen BRDF on actor cloaks, wool tunics, banners & fur pelts.
        if (isActorCloth || (base == 1u && !skin && s.alpha > 0.68f && s.mask < 0.10f && z < 1600.0f)) {
            const float rCloth = 0.55f;
            const float invR = 1.0f / rCloth;
            const float cos2h = nv * nv;
            const float sin2h = max(1.0f - cos2h, 0.0078125f);
            const float dCharlie = (2.0f + invR) * pow(sin2h, invR * 0.5f) / (2.0f * PI);
            const float3 sheenTint = lerp(albedo * 1.25f + 0.08f.xxx, 1.0f.xxx, 0.35f);
            const float3 sheenKey = isExterior ? (sunColour.rgb * (0.25f + 0.75f * sunShadow) + amb * 0.65f) : max(amb * 1.35f, 0.06f.xxx);
            lit += sheenTint * sheenKey * (dCharlie * pow(1.0f - nv, 2.2f) * 0.14f * specOcc);
        }
        // Feature 7: Kulla-Conty Clearcoat Absorption on wet stone, puddles & shoreline boulders (energy-conserving base darkening).
        if (max(wet.wet, wet.puddle) > 0.12f) {
            const float fCoat = 0.04f + 0.96f * pow(1.0f - nv, 5.0f);
            lit *= lerp(1.0f, 1.0f - fCoat * 0.42f, max(wet.wet * 0.65f, wet.puddle));
        }
        // Feature 8: Dual-Lobe Thin-Leaf Chlorophyll Subsurface Scattering + Canopy Micro-Dapple (base==5 foliage).
        if (base == 5u && isExterior && sunDir.w > 0.5f) {
            const float backLeaf = pow(saturate(dot(-s.v, sunDir.xyz)), 3.2f);
            const float wrapLeaf = saturate((dot(nSmooth, sunDir.xyz) + 0.45f) / 1.45f);
            const float3 chloro = lerp(albedo, float3(albedo.r * 0.92f, albedo.g * 1.35f, albedo.b * 0.48f), 0.62f);
            lit += chloro * sunColour.rgb * ((backLeaf * 0.38f + wrapLeaf * 0.12f) * sunShadow * (1.0f - saturate(clouds.x * 0.72f)));
        }
        const float3 F0mat_unused_marker = F0mat;
        if (rt && (flags.x & FLAG_REFLECT) != 0u && lobe) {
            const int2 baseRefl = clamp(int2(px + rings * 40.0f), int2(0, 0), int2(dims.xy) - 1);
            float4 rfSum = 0.0f.xxxx;
            float rfW = 0.0f;
            [unroll] for (int ry = -1; ry <= 1; ry++) {
                [unroll] for (int rx = -1; rx <= 1; rx++) {
                    const int3 rp = int3(clamp(baseRefl + int2(rx, ry), int2(0, 0), int2(dims.xy) - 1), 0);
                    const float rdDepth = Depth.Load(rp);
                    if (abs(viewZOf(rdDepth) - z) < max(z * 0.04f, 3.0f)) {
                        const float kw = (rx == 0 && ry == 0) ? 2.0f : 1.0f;
                        const float4 tap = RtRefl.Load(rp);
                        rfSum += float4(min(tap.rgb, 3.0f.xxx), tap.a) * kw;
                        rfW += kw;
                    }
                }
            }
            const float4 rf = rfW > 0.0f ? (rfSum / rfW) : RtRefl.Load(int3(baseRefl, 0));
            const float3 envFinal = lerp(dynEnv * (0.30f + 0.70f * skyVisShaped), rf.rgb, rf.a);
            lit += evalMultiScatterIBL(F0mat, envRough, nv, envFinal, specOcc) * max(reflMask, metallic);
            if (flags.w == 6u) { return float4(envFinal, game.a); }
        } else if (base == 2u) {
            // Class-2 forged metal/armor/gold/brass: exact Karis Split-Sum + Fdez-Aguera Multi-Scattering Conductor IBL.
            const float3 metalEnv = dynEnv * (0.30f + 0.70f * skyVisShaped);
            lit += evalMultiScatterIBL(F0mat, envRough, nv, metalEnv, specOcc) * max(reflMask, max(metallic, 0.35f));
        } else if (base == 12u) {
            // Class-12 Eye corneal wet catchlight: sharp corneal reflection from live dynamic cubemap (reflects nearby fires & sky).
            lit += evalMultiScatterIBL(float3(0.028f, 0.028f, 0.028f), 0.04f, nv, sampleLiveEnvCube(rDir, 0.04f), 0.45f + 0.55f * skyVis);
        } else if (base == 4u || base == 6u || base == 1u) {
            // Full-world physical dielectric Split-Sum IBL on terrain, cobblestones, stone & timber (excluded on foliage).
            const float3 dieF0 = lerp(0.028f, 0.042f, saturate(wet.wet + wet.puddle)).xxx;
            const float dieRough = clamp(s.alpha, 0.16f, 0.82f);
            const float3 dieEnv = sampleLiveEnvCube(rDir, dieRough) * (0.28f + 0.72f * skyVisShaped);
            lit += evalMultiScatterIBL(dieF0, dieRough, nv, dieEnv, specOcc) * lerp(0.55f, 1.0f, saturate(wet.wet + wet.puddle));
        }
        // Dual-Lobe Clearcoat GGX water film (crisp mirror-like reflections on rain-soaked roads/puddles, GTA VI storm study)
        // & oiled lacquer (low-metallic class-2 armor/leather).
        const float clearcoat = max(saturate(wet.wet * 0.85f + wet.puddle * 1.15f), (base == 2u ? (1.0f - metallic) * 0.35f : 0.0f));
        if (clearcoat > 0.05f && !skin && base != 5u) {
            const float ccRough = lerp(0.08f, 0.03f, saturate(wet.puddle));
            const float3 ccEnv = sampleLiveEnvCube(rDir, ccRough) * (0.30f + 0.70f * skyVisShaped);
            lit += evalMultiScatterIBL(float3(0.022f, 0.022f, 0.022f), ccRough, nv, ccEnv, specOcc) * (clearcoat * 0.95f);
        }
        // Estevez-Kulla grazing velvet/fuzz rim sheen on rough organic materials (fur, wool, moss, thatch, pine bark).
        if (!skin && base != 2u && s.alpha > 0.52f && sunDir.w > 0.5f && sunShadow > 0.05f) {
            const float rimFuzz = pow(1.0f - nv, 4.0f) * saturate((s.alpha - 0.52f) * 2.5f) * specOcc;
            lit += albedo * sunColour.rgb * (rimFuzz * sunShadow * 0.28f);
        }
        // Synchronized thunderstorm lightning overhead flash on wet surfaces, puddles & open ground during heavy storms.
        if (isExterior && weather.w > 0.45f && clouds.x > 0.65f) {
            const float tSlot = floor(camPosW.w * 1.4f);
            const float stormTrigger = step(0.945f, frac(sin(tSlot * 127.1f) * 43758.5453f));
            const float flashEnv = pow(1.0f - frac(camPosW.w * 1.4f), 5.0f) * stormTrigger;
            if (flashEnv > 0.005f) {
                const float upFace = saturate(s.n.z * 0.65f + 0.35f) * skyVis;
                const float wetAmp = 1.0f + max(wet.wet, wet.puddle) * 1.6f;
                lit += (albedo * 0.45f + 0.12f * max(wet.wet, wet.puddle)) * float3(0.62f, 0.78f, 1.25f) * (flashEnv * upFace * wetAmp * 0.65f);
            }
        }
        // Subtle emerald-cyan Aurora Borealis overhead sky bounce on clear-night snowfields & frost.
        if (isExterior && wet.snow > 0.15f && sunDir.w > 0.5f) {
            const float sLumAur = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float aurBounce = saturate((0.26f - sLumAur) * 4.5f) * (1.0f - saturate(clouds.x * 1.15f)) * saturate(s.n.z);
            lit += albedo * float3(0.06f, 0.24f, 0.19f) * (aurBounce * wet.snow * skyVis * 0.45f);
        }
        // Physical clearcoat GGX + subtle Belcour-Barla thin-film spectral iridescence on wet water films & glacial ice.
        if (max(wet.wet, wet.puddle) > 0.08f && isExterior && sunShadow > 0.0f) {
            const float3 ch = normalize(s.v + sunDir.xyz);
            const float cnh = saturate(dot(s.n, ch)), ca2 = 0.0036f;
            const float cdd = cnh * cnh * (ca2 - 1.0f) + 1.0f;
            const float cD = ca2 / (PI * cdd * cdd);
            const float vh = saturate(dot(s.v, ch));
            const float cF = 0.04f + 0.96f * pow(1.0f - vh, 5.0f);
            const float3 thinFilm = 0.88f + 0.12f * cos(float3(0.0f, 2.094f, 4.188f) + vh * 9.5f);
            lit += min(sunColour.rgb * thinFilm * (cD * cF * saturate(dot(s.n, sunDir.xyz)) * sunShadow * 0.26f), 2.5f.xxx) * max(wet.wet * 0.6f, wet.puddle);
        }
    }
    if (!match) {
        // Character Portrait & Eye-Candy Shading (Vanilla NPCs & PCs):
        // 1) Skin (base==10): soft portrait dermal wrap-fill + silky peach-fuzz velvet rim sheen + subtle nose/cheek oil specular.
        if (skin) {
            const float nvSkin = saturate(dot(s.n, s.v));
            const float fuzzRim = pow(1.0f - nvSkin, 2.9f) * 0.18f;
            const float3 portFill = isExterior ? (sunColour.rgb * (0.30f + 0.70f * sunShadow) * 0.10f + amb * 0.16f)
                                               : (max(amb, float3(0.10f, 0.08f, 0.07f)) * 0.28f);
            lit += albedo * float3(1.08f, 0.91f, 0.84f) * (portFill + fuzzRim * (isExterior ? sunColour.rgb * 0.40f + amb : max(amb, 0.14f.xxx)));
            if (isExterior) {
                const float nlRaw = dot(s.n, sunDir.xyz);
                const float3 shv = normalize(s.v + sunDir.xyz);
                const float snh = saturate(dot(s.n, shv)), sa2 = 0.048f;
                const float sdd = snh * snh * (sa2 - 1.0f) + 1.0f;
                const float sD = sa2 / (PI * sdd * sdd);
                lit += min(sunColour.rgb * (sD * 0.024f * saturate(nlRaw) * sunShadow * 0.18f), 0.52f.xxx);
            }
        } else if (base == 11u) {
            // 2) Hair (base==11): Dual-lobe Kajiya-Kay / Marschner anisotropic hair sheen (works in both exteriors & interiors).
            const float3 tHair = normalize(cross(s.n, float3(1.0f, 0.0f, 0.0f)) + float3(0.0f, 0.0f, 0.35f));
            const float3 hDir = normalize(s.v + (isExterior ? sunDir.xyz : normalize(s.v + float3(0.20f, 0.15f, 0.65f))));
            const float tdoth = dot(tHair, hDir);
            const float sinTH = sqrt(max(1.0f - tdoth * tdoth, 0.0f));
            const float kajiya1 = pow(sinTH, 52.0f) * 0.40f;
            const float kajiya2 = pow(sinTH, 14.0f) * 0.18f;
            const float3 hKey = isExterior ? (sunColour.rgb * (0.35f + 0.65f * sunShadow) + amb * 0.35f)
                                           : max(amb * 1.45f, float3(0.14f, 0.13f, 0.12f));
            lit += hKey * (float3(0.95f, 0.96f, 1.0f) * kajiya1 + (albedo * 0.80f + 0.20f.xxx) * kajiya2);
        } else if (base == 12u) {
            // 3) Eyes (base==12): Luminous iris subsurface translucency + crisp dual wet corneal catchlights
            // (live sky/sun reflection in exteriors + soft warm window/hearth catchlight in interiors so eyes never look dead).
            const float nvEye = saturate(dot(s.n, s.v));
            const float3 eyeKeyCol = isExterior ? max(sunColour.rgb * 0.55f + amb, float3(0.24f, 0.28f, 0.34f))
                                                : max(amb * 1.85f, float3(0.26f, 0.22f, 0.18f));
            // Soft iris & sclera subsurface lift so eye color stays vivid and readable even in shadow.
            lit += albedo * eyeKeyCol * (0.48f + 0.32f * nvEye);
            // Crisp GGX wet corneal specular catchlight (upper-right studio/sky key glint + subtle secondary glint).
            const float3 keyDir1 = isExterior ? normalize(lerp(sunDir.xyz, normalize(s.v + float3(0.26f, 0.22f, 0.48f)), 0.45f))
                                              : normalize(s.v + float3(0.28f, 0.20f, 0.46f));
            const float3 keyDir2 = normalize(s.v + float3(-0.32f, -0.14f, 0.28f));
            const float nh1 = saturate(dot(s.n, normalize(s.v + keyDir1)));
            const float nh2 = saturate(dot(s.n, normalize(s.v + keyDir2)));
            const float glint1 = pow(nh1, 180.0f) * 1.35f + pow(nh1, 32.0f) * 0.18f;
            const float glint2 = pow(nh2, 120.0f) * 0.42f;
            const float3 eyeEnv = evalDynamicEnvCube(reflect(-s.v, s.n), 0.04f);
            const float eyeFr = 0.04f + 0.96f * pow(1.0f - nvEye, 5.0f);
            lit += eyeEnv * eyeFr * 1.25f + eyeKeyCol * (glint1 + glint2);
        } else if (base == 5u && isExterior) {
            // Deep Nordic Vegetation Lighting (pine needles, ferns, shrubs, ivy):
            // 1) Rich backlit chlorophyll translucency ONLY under direct unclouded sun (no chalky overcast wash)
            const float back = pow(saturate(dot(-s.v, sunDir.xyz)), 3.8f);
            const float cloudClear = 1.0f - saturate(clouds.x * 0.82f);
            lit += albedo * sunColour.rgb * float3(1.06f, 1.20f, 0.52f) * back * sunShadow * cloudClear * 0.42f;
            // 2) Subtle albedo-tinted rain-wet sheen only when genuinely wet (eliminates pale frosted sky-wash on dry pines)
            if (wet.wet > 0.15f) {
                const float nvLeaf = saturate(dot(s.n, s.v));
                const float wetFr = pow(1.0f - nvLeaf, 4.5f) * (wet.wet - 0.15f) * 0.18f;
                lit += evalDynamicEnvCube(reflect(-s.v, s.n), 0.18f) * lerp(albedo, 1.0f.xxx, 0.35f) * (wetFr * (0.3f + 0.7f * skyVis));
            }
            // 3) Rich Nordic evergreen canopy depth (prevents glancing pine cards from reading pale/chalky)
            lit *= lerp(float3(0.74f, 0.82f, 0.70f), 1.0f.xxx, saturate(skyVis * 0.85f + sunShadow * 0.25f));
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
            // True world-space elevation density (replaces camera-relative p.z so height fog never shifts when moving/jumping!):
            const float heightDensity = exp(-max((p.z + camPosW.z) - waterA.w, 0.0f) * 0.00042f);
            const float distFog = 1.0f - exp(-max(z - 160.0f, 0.0f) * (0.000030f + 0.000028f * heightDensity));
            const float3 rayleighCol = float3(0.42f, 0.58f, 0.86f) * max(dot(amb, float3(0.3f, 0.5f, 0.2f)), 0.06f);
            const float3 mieCol = sunColour.rgb * min(hg, 1.5f) * (0.20f + 0.80f * sunShadow) * (0.45f + 0.55f * skyVis) * 0.045f;
            lit = lerp(lit, lit + rayleighCol * 0.24f + mieCol, saturate(distFog * 0.55f));
        }
    }
    const float3 finalLitRaw = knee3(lit);
    // Preserve genuinely chromatic emissive tavern windows, lanterns, braziers, and magic runes
    // while strictly excluding achromatic white/blue snowfields & glaciers (fixes glowing Dawnstar snow/ice patches).
    const float gLuma = dot(game.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float rLuma = dot(finalLitRaw, float3(0.2126f, 0.7152f, 0.0722f));
    const float gMax = max(game.r, max(game.g, game.b));
    const float gMin = min(game.r, min(game.g, game.b));
    const float gSat = (gMax - gMin) / max(gMax, 1.0e-4f);
    const float zPix = viewZOf(d);
    const float emissiveW = (!match && !skin && base != 5u && zPix < 5500.0f && gSat > 0.28f && gLuma > rLuma * 1.65f && gLuma > 0.18f)
                          ? saturate((gLuma - rLuma * 1.65f) * 2.5f) * saturate((5500.0f - zPix) / 1500.0f) : 0.0f;
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
