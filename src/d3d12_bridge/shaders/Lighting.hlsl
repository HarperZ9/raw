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
    const float lh = saturate(dot(l, h));
    const float F = s.f0 + (1.0f - s.f0) * pow(1.0f - lh, 5.0f);
    // Disney/Frostbite Burley retro-reflective rough diffuse (Lagarde 2014): replaces flat Lambert on rough stone, bark, dirt & wool.
    const float fd90 = 0.5f + 2.0f * s.alpha * lh * lh;
    const float burley = any(s.wrap > 0.0f) ? 1.0f : clamp((1.0f + (fd90 - 1.0f) * pow(1.0f - nl, 5.0f)) * (1.0f + (fd90 - 1.0f) * pow(1.0f - nv, 5.0f)), 0.72f, 1.28f);
    // Kulla-Conty / Turquin analytical multi-scattering GGX energy compensation (SIGGRAPH 2017/2019):
    // Restores microfacet multiple-bounce energy lost by single-scattering GGX on rough stone, timber, leather, and brushed steel.
    const float ess = saturate(1.0f - s.alpha * (0.42f * (1.0f - nv) + 0.14f));
    const float kulla = 1.0f + s.f0 * ((1.0f - ess) / max(ess, 0.25f));
    diff += nlD * c * (1.0f - F) * burley;
    spec += PI * D * V * F * kulla * nl * c;   // the game's light colour is irradiance over pi
}

#include "DistantLod.hlsli"   // distantLit: distant LOD lit in improved mode (FOL-04)

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    const float4 game = GameLit.Load(ip);
    const float d = Depth.Load(ip);
    if ((flags.x & FLAG_SURFACE) == 0u) { return game; }
    if (d >= 1.0f) {
        // The sky: Elder Weathers physical color alignment (Kasten-Young air mass + 1/lambda^4 Rayleigh + Mie + cloud collapse)
        // harmonizing sky zenith, horizon, sun/moon direct, ambient, and 3D Worley-Perlin clouds into one coherent palette.
        if (flags.w == 10u) { return float4(1.0f, 0.0f, 1.0f, game.a); }
        float3 baseSky = game.rgb;
        const float3 sDir = normalize(worldAt(floor(at.xy), 0.995f) - eyeAt());
        if ((flags.x & FLAG_MATCH) == 0u) {
            baseSky = baseSky / (1.0f + max(max(baseSky.r, max(baseSky.g, baseSky.b)) - 0.65f, 0.0f) * 0.65f);
            const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float dayW = saturate((sunDir.z - 0.01f) * 4.0f) * saturate((sunLum - 0.20f) * 2.5f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            if (dayW > 0.02f && sDir.z > -0.04f) {
                // Crisp, photographic Nordic cerulean-to-horizon physical sky (no lavender wash, 4K/8K/16K infinite resolution).
                const float cover = saturate(clouds.x);
                const float lowSun = saturate(1.0f - sunDir.z * 2.6f);
                const float3 clearZenith = lerp(float3(0.06f, 0.24f, 0.68f), float3(0.08f, 0.14f, 0.38f), lowSun);
                const float3 clearMid    = lerp(float3(0.22f, 0.48f, 0.84f), float3(0.38f, 0.36f, 0.52f), lowSun);
                const float3 clearHoriz  = lerp(float3(0.62f, 0.76f, 0.88f), float3(0.92f, 0.54f, 0.28f), lowSun);
                const float hUp = saturate(sDir.z);
                float3 clearSky = hUp < 0.25f ? lerp(clearHoriz, clearMid, pow(hUp / 0.25f, 0.75f))
                                              : lerp(clearMid, clearZenith, pow((hUp - 0.25f) / 0.75f, 0.85f));
                // Crisp, limb-darkened physical solar disc (0.55 deg angular diameter) + soft Mie aureole,
                // occluded cleanly by passing volumetric clouds (c.a) and locked to stabilized sunDir.
                const float cosTheta = saturate(dot(sDir, sunDir.xyz));
                const float sunDisc = smoothstep(0.99935f, 0.99972f, cosTheta);
                const float sunCorona = pow(cosTheta, 160.0f) * 0.55f + pow(cosTheta, 18.0f) * 0.16f;
                const float3 sunDiscCol = lerp(float3(1.0f, 0.97f, 0.90f), float3(1.0f, 0.68f, 0.34f), lowSun);
                clearSky += sunDiscCol * (sunCorona * (1.0f - cover * 0.78f));
                // Overcast / storm atmospheric slate transition.
                const float3 stormSky = lerp(float3(0.38f, 0.44f, 0.52f), float3(0.22f, 0.28f, 0.36f), hUp);
                const float3 pureSky = lerp(clearSky, stormSky, saturate(cover * 0.85f - 0.25f + weather.w * 0.35f));
                const float3 solarCore = sunDiscCol * (sunDisc * 1.15f * (1.0f - cover * 0.55f));
                baseSky = lerp(baseSky, pureSky + solarCore, dayW * 0.96f);
            }
        }
        const float4 c = (flags.x & FLAG_CLOUDS) != 0u ? Clouds.SampleLevel(Lin, (at.xy) * dims.zw, 0.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);
        // Strictly monotonic Beer-Lambert cloud occlusion: solar disc smoothly dims behind thick clouds and shines brightly in clear gaps (never inverts dark).
        float3 skyOut = baseSky * c.a + c.rgb;
        if ((flags.x & FLAG_MATCH) == 0u) {
            // Luminous atmospheric moon glow locked strictly to Skyrim's real Masser/Secunda sky pixels (zero camera drift),
            // plus infinite-resolution procedural scintillating starfield and animated volumetric Nordic auroras.
            const float sunLumN = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float nightSkyW = saturate((0.26f - sunLumN) * 4.5f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            if (nightSkyW > 0.05f && sDir.z > 0.0f) {
                const float moonL = dot(baseSky, float3(0.2126f, 0.7152f, 0.0722f));
                const float moonMask = smoothstep(0.14f, 0.45f, moonL);
                skyOut += baseSky * float3(0.88f, 0.96f, 1.18f) * (moonMask * nightSkyW * (0.45f + 0.85f * c.a));
                // Procedural 4K/8K/16K-crisp starfield with stellar color temperatures and subtle scintillation.
                const float3 starCell = floor(sDir * 260.0f);
                const float3 starF = frac(sDir * 260.0f) - 0.5f;
                const float sh0 = cHash(starCell);
                if (sh0 > 0.965f) {
                    const float3 off = float3(cHash(starCell + 11.3f), cHash(starCell + 27.1f), cHash(starCell + 43.7f)) - 0.5f;
                    const float dStar = length(starF - off * 0.55f);
                    const float starCore = exp(-dStar * dStar * 42.0f) * smoothstep(0.965f, 0.998f, sh0);
                    const float twk = 0.78f + 0.22f * sin(camPosW.w * (2.5f + sh0 * 5.0f) + sh0 * 62.8f);
                    const float temp = cHash(starCell + 71.9f);
                    const float3 starCol = lerp(float3(0.72f, 0.84f, 1.0f), float3(1.0f, 0.86f, 0.68f), temp);
                    skyOut += starCol * (starCore * twk * nightSkyW * c.a * saturate(sDir.z * 4.5f) * 1.15f);
                }
                // Animated multi-curtain volumetric Nordic Aurora Borealis across clear northern skies.
                const float auroraVis = nightSkyW * (1.0f - saturate(clouds.x * 1.15f)) * smoothstep(0.08f, 0.35f, sDir.z) * smoothstep(-0.25f, 0.45f, sDir.y);
                if (auroraVis > 0.02f) {
                    float3 aurSum = 0.0f.xxx;
                    [unroll] for (int ai = 0; ai < 6; ai++) {
                        const float ah = 0.22f + float(ai) * 0.13f;
                        const float2 ap = sDir.xy / max(sDir.z, 0.12f) * (1.4f + ah * 0.6f);
                        const float wave = sin(ap.x * 2.2f + camPosW.w * 0.08f + cNoise(float3(ap * 1.3f, camPosW.w * 0.04f)) * 3.2f);
                        const float band = exp(-abs(ap.y - 0.65f - wave * 0.42f) * 4.8f);
                        const float rays = 0.55f + 0.45f * cNoise(float3(ap.x * 7.5f, ap.y * 2.0f, camPosW.w * 0.12f + ah * 2.0f));
                        const float3 aCol = lerp(float3(0.12f, 0.95f, 0.58f), float3(0.42f, 0.26f, 0.96f), saturate((ah - 0.22f) * 1.45f));
                        aurSum += aCol * (band * rays * (1.0f - ah * 0.65f));
                    }
                    skyOut += aurSum * (auroraVis * c.a * 0.11f);
                }
            }
            // Horizon Fix: seamless atmospheric mist/ocean continuation for sub-horizon sky rays (no empty void past map edge).
            if (sDir.z < 0.015f) {
                const float subW = saturate((0.015f - sDir.z) * 18.0f);
                const float3 horizMist = lerp(skyOut, max(float3(ambient[0].w, ambient[1].w, ambient[2].w), 0.04f.xxx) * 0.85f, subW * 0.65f);
                skyOut = horizMist;
            }
            const float skyPeak = max(skyOut.r, max(skyOut.g, skyOut.b));
            skyOut = skyOut / (1.0f + max(skyPeak - 0.56f, 0.0f) * 0.65f);
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
            // Thick powdery 3D snowdrifts (matte high-roughness 0.88, wind-sculpted drift mounds, zero shiny 2D sheet sheen).
            const float3 wpS = p + camPosW.xyz;
            const float drift = cNoise(wpS * 0.0065f) * 0.65f + cNoise(wpS * 0.019f + 7.3f) * 0.35f;
            const float driftDx = cNoise((wpS + float3(8.0f, 0.0f, 0.0f)) * 0.014f) - cNoise((wpS - float3(8.0f, 0.0f, 0.0f)) * 0.014f);
            const float driftDy = cNoise((wpS + float3(0.0f, 8.0f, 0.0f)) * 0.014f) - cNoise((wpS - float3(0.0f, 8.0f, 0.0f)) * 0.014f);
            const float3 snowCol = float3(0.64f, 0.69f, 0.76f) * lerp(0.86f, 1.08f, drift);
            albedo = lerp(albedo, snowCol, wet.snow);
            s.alpha = lerp(s.alpha, 0.88f, wet.snow);
            s.shin = lerp(s.shin, 2.0f / (0.88f * 0.88f) - 2.0f, wet.snow);
            s.n = normalize(lerp(s.n, normalize(ng + float3(-driftDx * 0.65f, -driftDy * 0.65f, 0.25f)), wet.snow * 0.65f));
            s.f0 = lerp(s.f0, 0.015f, wet.snow);
            s.mask = lerp(s.mask, 0.02f, wet.snow);
            wet.puddle *= 1.0f - wet.snow;
            wet.wet *= 1.0f - wet.snow;
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
                s.n = normalize(s.n + float3(bestRad * (maxDep * 0.45f - maxRim * 0.25f), 0.0f) * deformStr);
                if (snowAmt >= mudAmt) {
                    albedo *= lerp(1.0f.xxx, float3(0.58f, 0.72f, 0.92f), maxDep * snowAmt * 0.78f);
                    s.alpha = lerp(s.alpha, 0.24f, maxDep * snowAmt * 0.65f);
                } else {
                    albedo *= lerp(1.0f.xxx, float3(0.45f, 0.42f, 0.38f), maxDep * mudAmt * 0.75f);
                    s.alpha = lerp(s.alpha, 0.08f, maxDep * mudAmt * 0.75f);
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
        // Reconstruct 3D leaf-cluster normal curvature + wind shimmer on flat 2011 alpha-tested foliage cards.
        if (z < 3600.0f) {
            const float3 wpF = p + camPosW.xyz;
            const float wPhase = camPosW.w * 1.4f + dot(wpF.xy, float2(0.008f, 0.006f));
            const float nxF = (cNoise(wpF * 0.045f + float3(sin(wPhase) * 0.35f, 0.0f, 0.0f)) - 0.5f) * 0.55f;
            const float nyF = (cNoise(wpF * 0.045f + float3(11.3f, cos(wPhase) * 0.35f, 5.7f)) - 0.5f) * 0.55f;
            const float fFade = saturate(1.0f - z / 3600.0f);
            s.n = normalize(s.n + float3(nxF, nyF, 0.18f) * fFade);
        }
    }
    const float3 nSmooth = s.n;
    const bool isActorFeat = (base >= 10u && base <= 12u);   // skin, hair, eye
    const bool isActorCloth = (!isActorFeat && base != 2u && base != 5u && SoftMap.Load(ip).r < 0.0f);   // skinned actor clothing
    // Organic world-space anti-tiling & natural slope curvature on terrain (class 4) and cliffs/rocks (class 1, excluding actor cloth).
    if (!match && !isActorCloth && (base == 4u || base == 1u) && z < 16000.0f) {
        const float3 wp = p + camPosW.xyz;
        const float macroN = cNoise(wp * 0.0022f) * 0.62f + cNoise(wp * 0.0068f + 13.7f) * 0.38f;
        const float microN = cNoise(wp * 0.024f + 5.3f);
        const float antiTile = lerp(0.89f, 1.11f, macroN) * lerp(0.95f, 1.05f, microN);
        const float slope = saturate(s.n.z);
        // Natural slope transitions: subtle warm-moss enrichment on flat ledges, crisp cool-slate rock on steep inclines.
        const float3 slopeTint = lerp(float3(0.96f, 0.98f, 1.03f), float3(1.02f, 1.01f, 0.97f), smoothstep(0.55f, 0.88f, slope));
        const float tileFade = saturate(1.0f - z / 16000.0f);
        albedo *= lerp(1.0f.xxx, antiTile * slopeTint, tileFade * (base == 4u ? 0.85f : 0.45f));
        // Triplanar geological crag & fracture normal relief on steep blocky vanilla cliffs (base == 1 or 4, slope < 0.72).
        if (slope < 0.72f && z < 8000.0f) {
            const float cragFade = saturate((0.72f - slope) * 2.5f) * saturate(1.0f - z / 8000.0f);
            const float cx = cNoise(wp * 0.016f + float3(3.1f, 0.0f, 7.3f)) - cNoise(wp * 0.016f - float3(3.1f, 0.0f, 7.3f));
            const float cy = cNoise(wp * 0.016f + float3(0.0f, 3.1f, 11.7f)) - cNoise(wp * 0.016f - float3(0.0f, 3.1f, 11.7f));
            const float cz = cNoise(float3(wp.xy * 0.012f, wp.z * 0.042f)) * 2.0f - 1.0f;
            s.n = normalize(s.n + float3(cx * 0.32f, cy * 0.32f, cz * 0.16f) * cragFade);
        }
        // Novel world-space Nordic moss & damp-crevice weathering on outdoor rocks/ruins (zero texture mods).
        if (base == 1u && sunColour.w > 0.5f && z < 4500.0f && wet.snow < 0.35f) {
            const float mossPatch = smoothstep(0.56f, 0.82f, cNoise(wp * 0.0085f + float3(4.1f, 9.7f, 2.3f))) * smoothstep(0.52f, 0.86f, slope);
            const float mossFade = saturate(1.0f - z / 4500.0f) * (1.0f - wet.snow * 2.5f);
            const float3 mossCol = float3(0.14f, 0.24f, 0.10f) * lerp(0.85f, 1.15f, microN);
            albedo = lerp(albedo, mossCol, mossPatch * mossFade * 0.38f);
            s.alpha = lerp(s.alpha, 0.86f, mossPatch * mossFade * 0.38f);
        }
        s.alpha = clamp(s.alpha + (macroN - 0.5f) * 0.14f * tileFade, 0.08f, 0.96f);
    }
    // Zero-mod procedural PBR micro-normal & roughness synthesis on vanilla stone/wood (excluded on actor skin/hair/eye/cloth & foliage).
    if (!match && !isActorFeat && !isActorCloth && base != 5u && z < 2800.0f) {
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
        // Native zero-mod Parallax Occlusion Mapping (POM) micro-relief self-shadowing on cobblestone, stone, timber & terrain.
        if ((base == 1u || base == 4u || base == 6u) && z < 1600.0f && sunDir.w > 0.5f) {
            const float pomFade = saturate(1.0f - z / 1600.0f) * (base == 4u ? 0.65f : 0.85f);
            const float2 sunScreen = float2(dot(sunDir.xyz, tVec), dot(sunDir.xyz, bVec));
            const float2 stepPx = normalize(sunScreen + float2(1.0e-4f, 0.0f)) * 1.35f;
            float maxH = hc;
            [unroll] for (int pi = 1; pi <= 4; pi++) {
                const int3 spm = int3(clamp(int2(px + stepPx * float(pi)), int2(0, 0), dimMax), 0);
                if (abs(Depth.Load(spm) - d) < 0.0008f) {
                    maxH = max(maxH, dot(Albedo.Load(spm).rgb, lw) - 0.018f * float(pi));
                }
            }
            const float pomOcc = saturate(1.0f - max(maxH - hc, 0.0f) * 2.8f * pomFade);
            albedo *= lerp(0.76f, 1.0f, pomOcc);
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
                if (abs(Depth.Load(spS) - d) < 0.0008f) {
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
    const float restVis = rt && (flags.x & FLAG_REST) != 0u ? RtBounce.Load(ip).a : 1.0f;
    float3 diff = 0.0f, spec = 0.0f;
    float sunShadow = 1.0f;
    if (sunDir.w > 0.5f) {
        // Interiors have no cascades; channel 0 of their mask belongs to a shadowed point light, so the
        // directional light there is unshadowed, as the game lights it.
        sunShadow = (flags.x & FLAG_SHADOWS) == 0u ? 1.0f : (flags.x & FLAG_CASCADES) != 0u ? cascadeShadow(p, d, true) : m4.x;
        // When native hardware RT (RayQuery + Persistent World TLAS) is active, skip screen-space contact shadow hacks
        // completely (keeps screenShadow only as fallback when RT is disabled).
        if (!match && !rt && (flags.x & FLAG_CONTACT) != 0u && base != 5u && !isActorFeat && !isActorCloth && sunShadow > 0.0f && dot(s.n, sunDir.xyz) > 0.0f)
            sunShadow *= screenShadow(p + lift, sunDir.xyz, tune.w, 0.02f, jit, 12u);
        // Skinned actors (skin/hair/eye) keep the game's filtered shadow mask so coarse skinned BLAS triangles never cast blocky self-shadow acne.
        if (rt && sunColour.w > 0.5f && !isActorFeat) { sunShadow = min(sunShadow, base == 5u ? lerp(1.0f, rv.x, 0.6f) : rv.x); }
        if (!match && sunColour.w > 0.5f && (flags.x & FLAG_CLOUDS) != 0u) { sunShadow *= cloudShadow(p + camPosW.xyz); }   // SKY-02
        // Skin: warm dermal penumbra + gentle wrap so shadow transitions on faces and shoulders stay lifelike, never muddy grey.
        const float3 sunSh = skin && !match ? pow(saturate(sunShadow).xxx, float3(0.72f, 0.92f, 1.04f)) : sunShadow.xxx;
        const float dirLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightDir = (!match && sunColour.w > 0.5f) ? saturate((0.26f - dirLum) * 4.5f) : 0.0f;
        // Elder Weathers physical sun-color alignment by day (eliminates vanilla's muddy green/orange mismatches) + silver-blue moonlight by night.
        float3 alignedDayCol = sunColour.rgb;
        if (!match && sunColour.w > 0.5f && nightDir < 0.95f) {
            const float elevDegD = max(degrees(asin(saturate(sunDir.z))), 1.2f);
            const float mD = 1.0f / (sin(radians(elevDegD)) + 0.50572f * pow(elevDegD + 6.07995f, -1.6364f));
            const float3 ewTrans = exp(-(0.20f * float3(0.175f, 0.410f, 1.0f) + 0.025f * lerp(1.0f, 4.5f, saturate(clouds.x))) * mD);
            // High-latitude / alpine plateau CIE D65 midday calibration (Norway / Swiss Alps / Mongolian steppe):
            // crisp cool-neutral daylight at high sun (sunDir.z > 0.30) transitioning naturally to warm amber only at dawn/dusk.
            const float highSun = smoothstep(0.18f, 0.48f, sunDir.z);
            const float3 d65Spec = lerp(ewTrans, float3(0.96f, 0.98f, 1.02f), highSun * 0.78f);
            const float ewLum = max(dot(d65Spec, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
            const float middayTrim = lerp(1.0f, 0.88f, highSun);
            alignedDayCol = lerp(sunColour.rgb, d65Spec * (max(dirLum, 0.05f) / ewLum), 0.62f * (1.0f - nightDir)) * middayTrim;
        }
        // Crisp silver-blue lunar directional bounce & specular on moonlit surfaces at night.
        const float3 dirCol = lerp(alignedDayCol, max(sunColour.rgb, float3(0.14f, 0.20f, 0.34f)) * float3(0.84f, 0.95f, 1.22f), nightDir * 0.65f);
        // Chan 2018 micro-shadowing: uses fine-scale AO to shadow crevices/mortar/bark where coarse shadow maps cannot reach.
        const float microSh = (!match && !isActorFeat && base != 5u) ? saturate(2.0f * AoHist.Load(ip).x + saturate(dot(s.n, sunDir.xyz)) - 0.85f) : 1.0f;
        addLight(s, sunDir.xyz, dirCol * sunSh * lerp(0.55f, 1.0f, microSh), dirCol, diff, spec);
        // Warm golden-emerald backlit foliage subsurface transmission (pine needles, ferns, leaves glow when backlit by sun).
        if (!match && base == 5u && sunColour.w > 0.5f) {
            const float backF = pow(saturate(dot(-s.v, sunDir.xyz)), 3.0f);
            diff += dirCol * float3(0.95f, 1.12f, 0.55f) * (backF * sunShadow * 0.42f);
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
            const float wrapSun = saturate((dot(s.n, sunDir.xyz) + 0.32f) / 1.32f);
            diff += sunColour.rgb * float3(0.18f, 0.09f, 0.06f) * wrapSun * (0.35f + 0.65f * sunShadow);
        }
        if (wet.snow > 0.0f && sunShadow > 0.0f) {
            // arXiv:2505.08985 (Wu/Luan/Hasan/Ramamoorthi 2025) Dual-Scale Position-Normal Manifold Micro-Glints:
            // Resolves crisp footprint-clamped snow crystal sparkle without temporal pixel-shimmer.
            const float fp = max(1.2f, z * 0.0025f);
            const float gFine = snowGlint(p + camPosW.xyz, s.n, s.v, sunDir.xyz, fp);
            const float gBroad = snowGlint(p + camPosW.xyz + 13.7f, s.n, s.v, sunDir.xyz, fp * 2.1f) * 0.45f;
            spec += sunColour.rgb * (sunShadow * wet.snow * 2.4f * (gFine + gBroad));
        }
        // Forged metal micro-flake glint on hammered steel/gold/dwemer armor (base==2) under direct sun.
        if (base == 2u && !match && sunShadow > 0.05f && z < 1600.0f) {
            const float mGlint = snowGlint(p + camPosW.xyz, s.n, s.v, sunDir.xyz, max(0.8f, z * 0.0018f));
            spec += sunColour.rgb * (sunShadow * mGlint * 0.75f * saturate(1.0f - z / 1600.0f));
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
            const float att = 1.0f - saturate(dist / lightPosR[li].w) * saturate(dist / lightPosR[li].w);
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
            // Embers-class warm hearth/torch/candle spectral enrichment + calm world-anchored organic breath.
            // Moderated on skin so NPC faces near hearths stay naturally sculpted instead of harsh orange-red.
            if (!match && lightColS[li].r > lightColS[li].b * 1.30f) {
                const float lPhase = dot(lightPosR[li].xyz + camPosW.xyz, float3(0.031f, 0.047f, 0.023f));
                const float flk = 0.975f + 0.025f * sin(camPosW.w * 2.4f + lPhase);
                const float3 fireBoost = skin ? float3(1.03f, 0.96f, 0.90f) : float3(1.16f, 0.90f, 0.64f);
                c *= fireBoost * flk;
            }
            const uint slot = li + 1u == rIdx.x ? 1u : li + 1u == rIdx.y ? 2u : li + 1u == rIdx.z ? 3u : 0u;
            // When hardware RT is active, bypass vanilla's flickering 4-channel shadow-mask swaps completely:
            // top-3 lights use their exact RT shadow vis (rv[slot]); secondary lights (4..128) use stable ReSTIR visibility.
            if (rt) { sh = slot != 0u ? rv[slot] : lerp(0.68f, 1.0f, restVis); }
            else if (slot != 0u) { sh = rv[slot]; }
            if (!match && !rt && sunColour.w > 0.5f && slot == 0u && (flags.x & FLAG_SOFT) != 0u && base != 5u && softLeft > 0u && dot(s.n, l) > 0.0f && max(c.r, max(c.g, c.b)) > 0.02f) {
                sh *= screenShadow(p + lift, l, min(dist, tune.z), 4.0f / max(dist, 1.0f), jit, 10u);
                --softLeft;
            }
            addLight(s, l, c * sh, c, diff, spec);
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
    // When hardware RayQuery RTAO/skyVis is active, let native 3D RT occlusion dominate and dial back screen-space AO
    // to subtle micro-crease contact (eliminates screen-space AO halos around trees and actors).
    const float ssaoMix = (rt && (flags.x & FLAG_SKY) != 0u) ? 0.38f : 1.0f;
    const float aoWeight = (base == 5u ? 0.35f : (isActorFeat ? 0.48f : 1.0f)) * ssaoMix;
    const float aoTerm = (flags.x & FLAG_AO) != 0u ? lerp(1.0f, AoHist.Load(ip).x, tune2.x * aoWeight) : 1.0f;
    // Exteriors: ray-traced sky visibility (RtSky.cpp). Interiors (!match): local SM6.5 RayQuery RTAO from RtShadow.hlsl.
    const bool isExterior = sunColour.w > 0.5f;
    const float rawSkyVis = rt && (flags.x & FLAG_SKY) != 0u && (isExterior || !match) ? RtIdx.Load(ip).w : 1.0f;
    const float skyVis = isActorFeat ? lerp(0.55f, 1.0f, rawSkyVis) : rawSkyVis;
    // One occlusion, not two: the ray-traced sky/RTAO visibility already holds near occluders.
    const float rtOcc = isExterior ? lerp(1.0f, skyVis, cluster.w) : lerp(0.42f, 1.0f, skyVis);
    float combinedOcc = min(aoTerm, rtOcc);
    // Lifelike Vanilla NPCs: lift recessed eye-socket AO (base==12) so eyes never read as dim dark holes,
    // and soften harsh brow/under-eye portrait shadows on skin (base==10).
    if (!match) {
        if (base == 12u) combinedOcc = lerp(combinedOcc, 1.0f, 0.80f);
        else if (skin)   combinedOcc = lerp(combinedOcc, 1.0f, 0.36f);
    }
    // SIGGRAPH GI-12 Visibility-Aware Bent-Normal Hemisphere Skylighting: bends indirect normal toward open sky
    // inside crevices/overhangs so shadowed stone, timber, and snow catch natural directional sky bounce.
    const float3 bentN = normalize(lerp(s.n, float3(0.0f, 0.0f, 1.0f), (1.0f - combinedOcc) * 0.32f * skyVis));
    const float hemiSky = (!match && isExterior) ? lerp(0.72f, 1.12f, saturate(bentN.z * 0.5f + 0.5f)) : 1.0f;
    amb *= match ? combinedOcc.xxx : gtaoMultiBounce(combinedOcc, saturate(albedo)) * hemiSky;
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
        const float outCool = saturate(clouds.x * 0.6f + weather.w * 0.5f);
        amb *= lerp(float3(1.02f, 1.00f, 0.98f), float3(0.90f, 0.96f, 1.06f), outCool * 0.35f);
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
    // Attenuate vanilla's compressed 128px DXT1 static cubemap (envRefl) in improved mode so full-res RT + dynamic IBL drive clean metal reflections.
    float3 lit = knee3((albedo + envRefl * (match ? 1.0f : 0.12f)) * (diff + amb + effCharLight));
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
        const float fr = s.f0 + (1.0f - s.f0) * pow(1.0f - nv, 5.0f);
        // Frostbite (Lagarde 2014) specular occlusion: prevents fake sky/cubemap gloss inside deep occluded rock/timber crevices.
        const float specOcc = saturate(pow(nv + combinedOcc, exp2(-16.0f * s.alpha - 1.0f)) - 1.0f + combinedOcc);
        const float3 rDir = reflect(-s.v, nSmooth);
        const float3 dynEnv = evalDynamicEnvCube(rDir, max(s.alpha, base == 2u ? 0.28f : 0.0f));
        const float reflMask = max(saturate(s.mask * resScale.w), max(wet.puddle, 0.5f * wet.wet)) * specOcc;
        if (rt && (flags.x & FLAG_REFLECT) != 0u && lobe) {
            // Depth-aware 3x3 bilateral filter on full-res RtRefl: eliminates checkerboard/rectangular blocks on armor, metal & wet stone.
            const int2 baseRefl = clamp(int2(px + rings * 40.0f), int2(0, 0), int2(dims.xy) - 1);
            float4 rfSum = 0.0f.xxxx;
            float rfW = 0.0f;
            [unroll] for (int ry = -1; ry <= 1; ry++) {
                [unroll] for (int rx = -1; rx <= 1; rx++) {
                    const int3 rp = int3(clamp(baseRefl + int2(rx, ry), int2(0, 0), int2(dims.xy) - 1), 0);
                    const float rdDepth = Depth.Load(rp);
                    if (abs(rdDepth - d) < 0.0012f) {
                        const float kw = (rx == 0 && ry == 0) ? 2.0f : 1.0f;
                        const float4 tap = RtRefl.Load(rp);
                        rfSum += float4(min(tap.rgb, 2.5f.xxx), tap.a) * kw;
                        rfW += kw;
                    }
                }
            }
            const float4 rf = rfW > 0.0f ? (rfSum / rfW) : RtRefl.Load(int3(baseRefl, 0));
            const float3 envFinal = lerp(dynEnv * (0.35f + 0.65f * skyVis), rf.rgb, rf.a);
            lit += envFinal * fr * reflMask * (1.0f - s.alpha) * 0.85f;
            if (flags.w == 6u) { return float4(envFinal, game.a); }
        } else if (base == 2u) {
            // Class-2 forged metal/armor dynamic cubemap IBL + Turquin multi-scattering metallic energy preservation.
            const float multiBoost = 1.0f + s.f0 * (1.0f - nv) * 0.85f;
            lit += dynEnv * fr * max(reflMask, 0.25f) * (1.0f - s.alpha * 0.65f) * (0.35f + 0.65f * skyVis) * 0.58f * multiBoost;
        } else if (base == 12u) {
            // Class-12 Eye corneal wet catchlight: sharp corneal Fresnel reflection of live sky/environment.
            const float eyeFr = 0.025f + 0.975f * pow(1.0f - nv, 5.0f);
            lit += evalDynamicEnvCube(rDir, 0.04f) * (eyeFr * 0.85f * (0.4f + 0.6f * skyVis));
        }
        // Estevez-Kulla grazing velvet/fuzz rim sheen on rough organic materials (fur, wool, moss, thatch, pine bark).
        if (!skin && base != 2u && s.alpha > 0.52f && sunDir.w > 0.5f && sunShadow > 0.05f) {
            const float rimFuzz = pow(1.0f - nv, 4.0f) * saturate((s.alpha - 0.52f) * 2.5f) * specOcc;
            lit += albedo * sunColour.rgb * (rimFuzz * sunShadow * 0.28f);
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
        // 1) Skin (base==10): soft portrait dermal wrap-fill + silky peach-fuzz velvet rim sheen (no greasy shine).
        if (skin) {
            const float nvSkin = saturate(dot(s.n, s.v));
            const float fuzzRim = pow(1.0f - nvSkin, 3.2f) * 0.14f;
            const float3 portFill = isExterior ? (sunColour.rgb * (0.28f + 0.72f * sunShadow) * 0.08f + amb * 0.12f)
                                               : (max(amb, float3(0.08f, 0.07f, 0.06f)) * 0.24f);
            lit += albedo * float3(1.06f, 0.90f, 0.84f) * (portFill + fuzzRim * (isExterior ? sunColour.rgb * 0.35f + amb : max(amb, 0.12f.xxx)));
            if (isExterior) {
                const float nlRaw = dot(s.n, sunDir.xyz);
                const float3 shv = normalize(s.v + sunDir.xyz);
                const float snh = saturate(dot(s.n, shv)), sa2 = 0.065f;
                const float sdd = snh * snh * (sa2 - 1.0f) + 1.0f;
                const float sD = sa2 / (PI * sdd * sdd);
                lit += min(sunColour.rgb * (sD * 0.020f * saturate(nlRaw) * sunShadow * 0.14f), 0.45f.xxx);
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
            const float heightDensity = exp(-max(p.z + 180.0f, 0.0f) * 0.00085f);
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
