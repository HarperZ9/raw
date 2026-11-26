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
#include "StrataBRDF.hlsli"
#include "SpectralTransport.hlsli"
#include "RawWorldDynamics.hlsli"
#include "RawPrecipThermal.hlsli"
#include "RawVolumetricGiGlint.hlsli"
#include "RawSplitTemporalAerial.hlsli"
#include "RawBlackbodySss.hlsli"
#include "RawProceduralNoise.hlsli"
#include "RawMaterials3D.hlsli"
#include "RawDecals3D.hlsli"
#include "RawSeasonsThermals.hlsli"
#include "RawWeathering.hlsli"

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

// Wave 58: Strict NaN/Inf sanitization helper for HDR lighting outputs & IBL samples.
float3 sanitizeLit3(float3 c) {
    return all(isfinite(c)) ? max(c, 0.0f.xxx) : 0.0f.xxx;
}

// Sample our live 256x256 dual-band prefiltered dynamic environment cubemap (top: glossy, bottom: rough convolution).
float3 sampleLiveEnvCube(float3 rd, float roughness) {
    const float rd2 = dot(rd, rd);
    const float3 d = (isfinite(rd2) && rd2 > 1.0e-8f) ? (rd * rsqrt(rd2)) : float3(0.0f, 0.0f, 1.0f);
    const float u = (abs(d.x) + abs(d.y) > 1.0e-5f) ? (atan2(d.y, d.x) / (2.0f * PI) + 0.5f) : 0.5f;
    const float vLocal = saturate(0.5f - asin(clamp(d.z, -1.0f, 1.0f)) / PI);
    const float vClamp = clamp(vLocal, 0.5f / 128.0f, 127.5f / 128.0f);
    const float3 sharp = sanitizeLit3(EnvCube.SampleLevel(Lin, float2(u, vClamp * 0.5f), 0.0f).rgb);
    const float3 rough = sanitizeLit3(EnvCube.SampleLevel(Lin, float2(u, 0.5f + vClamp * 0.5f), 0.0f).rgb);
    return lerp(sharp, rough, saturate(roughness * roughness));
}

// Wave 42 Aperiodic Dual-Layer Curl-Convergence Underwater Caustics (zero Voronoi grids, zero periodic stripes):
float evalCurlCausticLayer(float2 xy, float tSec) {
    const float2 q1 = xy * 0.024f + float2(tSec * 0.22f, -tSec * 0.14f);
    const float4 n1 = rawNoise3Grad(float3(q1, tSec * 0.32f));
    const float2 warp1 = float2(n1.z, -n1.y) * 0.65f;
    const float f1 = pow(saturate(1.0f - abs(rawNoise3Grad(float3(q1 + warp1, tSec * 0.36f + 7.1f)).x * 2.0f - 1.0f) * 2.4f), 3.6f);

    // Incommensurately rotated second layer (53 deg) for organic intersecting caustic filaments:
    const float2 q2 = float2(xy.x * 0.6018f - xy.y * 0.7986f, xy.x * 0.7986f + xy.y * 0.6018f) * 0.049f
                    + float2(-tSec * 0.18f, tSec * 0.26f);
    const float4 n2 = rawNoise3Grad(float3(q2, -tSec * 0.44f + 19.3f));
    const float2 warp2 = float2(n2.z, -n2.y) * 0.55f;
    const float f2 = pow(saturate(1.0f - abs(rawNoise3Grad(float3(q2 + warp2, -tSec * 0.48f + 31.7f)).x * 2.0f - 1.0f) * 2.6f), 3.8f);
    return saturate(f1 * 0.55f + f2 * 0.45f + f1 * f2 * 1.15f);
}

// Wave 42 Multi-Cell Hashed Aperiodic Expanding Raindrop Splash Ring Gradient (ddx-safe analytical ring normal):
float2 evalRainSplashCellGrid(float2 pGrid, float tSec, float seedOff) {
    const float2 baseCell = floor(pGrid);
    const float2 fLocal = frac(pGrid);
    float2 grad = 0.0f.xx;
    [unroll] for (int dy = -1; dy <= 1; ++dy) {
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            const float2 cell = baseCell + float2(float(dx), float(dy));
            const float3 h0 = rawHash33(float3(cell, seedOff));
            const float cycle = tSec * (1.15f + 0.65f * h0.z) + h0.x * 7.31f;
            const float dropId = floor(cycle);
            const float age = frac(cycle);
            const float3 hd = rawHash33(float3(cell + dropId * 1.37f, seedOff + 13.7f));
            const float2 center = float2(dx, dy) + (0.16f + 0.68f * hd.xy);
            const float2 d = fLocal - center;
            const float r = length(d);
            const float ringR = age * 0.54f;
            const float dr = r - ringR;
            const float env = exp(-dr * dr * 58.0f) * (1.0f - age) * (1.0f - age) * smoothstep(0.0f, 0.06f, age);
            const float waveSlope = cos(dr * 32.0f) * env;
            if (r > 1.0e-3f) {
                grad += (d / r) * waveSlope;
            }
        }
    }
    return grad;
}

float2 evalRainSplashRippleGrad(float2 worldXY, float tSec) {
    const float2 g1 = evalRainSplashCellGrid(worldXY * 0.088f, tSec, 3.1f);
    const float2 rotXY = float2(worldXY.x * 0.7986f - worldXY.y * 0.6018f, worldXY.x * 0.6018f + worldXY.y * 0.7986f);
    const float2 g2r = evalRainSplashCellGrid(rotXY * 0.142f + float2(11.3f, 7.9f), tSec * 1.18f, 17.7f);
    const float2 g2 = float2(g2r.x * 0.7986f + g2r.y * 0.6018f, -g2r.x * 0.6018f + g2r.y * 0.7986f);
    return g1 * 0.62f + g2 * 0.42f;
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
    const float x = dot(s.n, l), nl = max(x, 0.0f);
    // Skin: light that enters the skin comes out past the terminator, red farthest (a per-channel wrap,
    // normalised so the lit side keeps its energy).
    const float3 nlD = any(s.wrap > 0.0f) ? saturate((x + s.wrap) / (1.0f + s.wrap)) / (1.0f + s.wrap) * (1.0f + s.wrap * 0.5f) : nl.xxx;
    if (any(s.soft > 0.0f)) {
        const float wrapTrans = saturate(smooth01(saturate((x + 1.0f) * 0.5f)) - smooth01(nl));
        if ((flags.x & FLAG_GGX) == 0u) {
            diff += s.soft * wrapTrans * cFull;
        } else if (!any(s.wrap > 0.0f)) {
            // Foliage (base==5u): strictly SHADOWED `c` (never unshadowed cFull!), chlorophyll-tinted wrap
            // & restrained boreal transmission so sunlit pine needles stay lush evergreen instead of chalky grey!
            const float backView = pow(saturate(dot(-s.v, l)), 3.6f);
            const float3 wrapChloro = float3(0.68f, 0.94f, 0.44f);
            const float3 chloroTint = float3(0.46f, 0.86f, 0.24f);
            const float3 softClamped = min(s.soft, 0.42f.xxx);
            diff += softClamped * (wrapChloro * (wrapTrans * 0.14f) + chloroTint * (backView * 0.22f * (1.0f - nl * 0.65f))) * c;
        } else {
            // Skin soft-map wrap: keeps arms/hands matched to shadowed direct light with zero foliage tint/wax!
            diff += min(s.soft, 0.35f.xxx) * (wrapTrans * 0.35f) * c;
        }
    }
    const float3 h = rawSafeHalfVec(s.v, l, s.n);
    const float nh = saturate(dot(s.n, h));
    if ((flags.x & FLAG_GGX) == 0u) {
        diff += nlD * c;
        spec += pow(nh, max(s.shin, 1.0f)) * c;   // the game's lobe: no cosine, no normalisation
        return;
    }
    const float nv = max(dot(s.n, s.v), 1.0e-4f), a2 = max(s.alpha * s.alpha, 1.0e-4f);
    const float dd = max(nh * nh * (a2 - 1.0f) + 1.0f, 1.0e-4f);
    const float D = a2 / (PI * dd * dd);
    const float V = 0.5f / (nl * sqrt(nv * nv * (1.0f - a2) + a2) + nv * sqrt(nl * nl * (1.0f - a2) + a2) + 1.0e-5f);
    const float lh = saturate(dot(l, h));
    const float F = saturate(s.f0 + (1.0f - s.f0) * pow(1.0f - lh, 5.0f));
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
    spec += min(PI * D * V * F * kulla * nl, 8.0f) * c;   // energy-conserving bounded direct GGX
}

#include "DistantLod.hlsli"   // distantLit: distant LOD lit in improved mode (FOL-04)

// Wave 63 Live Visual Debug View 28: Distinct Material, Actor/Creature/Fur/Cloth (14), Foliage & Glacier Class ID Palette
float3 debugClassColor(uint base, bool isActorCloth, float iceMask) {
    float3 c = float3(0.50f, 0.50f, 0.54f);
    if      (base == 0u)  { c = float3(0.08f, 0.08f, 0.11f); }
    else if (base == 14u) { c = float3(1.00f, 0.32f, 0.06f); }   // Skinned Actor / Creature / Fur / Clothing (vivid orange-vermilion)
    else if (isActorCloth){ c = float3(0.95f, 0.44f, 0.14f); }   // Legacy SoftMap<0 actor cloth fallback
    else if (base == 1u)  { c = float3(0.58f, 0.60f, 0.64f); }   // Static Stone / Wood / Architecture
    else if (base == 2u)  { c = float3(0.95f, 0.78f, 0.18f); }   // Metal / EnvMap Armor
    else if (base == 3u)  { c = float3(1.00f, 0.96f, 0.65f); }   // Emissive
    else if (base == 4u)  { c = float3(0.56f, 0.38f, 0.18f); }   // Terrain / Landscape
    else if (base == 5u)  { c = float3(0.12f, 0.82f, 0.24f); }   // Foliage / Flora
    else if (base == 6u)  { c = float3(0.18f, 0.42f, 0.96f); }   // Parallax / Masonry
    else if (base == 7u)  { c = float3(0.12f, 0.88f, 0.92f); }   // Distant LOD
    else if (base == 9u)  { c = float3(0.06f, 0.56f, 0.98f); }   // Water
    else if (base == 10u) { c = float3(1.00f, 0.26f, 0.24f); }   // Actor Skin
    else if (base == 11u) { c = float3(0.76f, 0.28f, 0.96f); }   // Actor Hair
    else if (base == 12u) { c = float3(0.18f, 1.00f, 1.00f); }   // Actor Eye
    else if (base == 13u) { c = float3(0.62f, 0.96f, 0.20f); }   // TruePBR
    if (iceMask > 0.22f && base != 10u && base != 11u && base != 12u && base != 14u && !isActorCloth) {
        c = lerp(c, float3(0.62f, 0.94f, 1.00f), saturate(iceMask * 1.4f));   // Glacier / Ice highlight
    }
    return c;
}

// Wave 63 Live Visual Debug View 33: NaN / Inf / Negative / HDR > 32.0 Overflow Heatmap
float3 debugHdrEnergyHeatmap(float3 rawLit) {
    if (!all(isfinite(rawLit)) || any(rawLit < -1.0e-3f) || max(rawLit.r, max(rawLit.g, rawLit.b)) > 32.0f) {
        return float3(1.0f, 0.0f, 1.0f);   // Electric magenta alarm on NaN / Inf / negative / >32 HDR overflow!
    }
    const float l = dot(max(rawLit, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f));
    const float t = saturate(log2(1.0f + l * 4.0f) / 4.2f);
    if (t < 0.25f) return lerp(float3(0.03f, 0.06f, 0.24f), float3(0.04f, 0.48f, 0.85f), t * 4.0f);
    if (t < 0.50f) return lerp(float3(0.04f, 0.48f, 0.85f), float3(0.10f, 0.84f, 0.36f), (t - 0.25f) * 4.0f);
    if (t < 0.75f) return lerp(float3(0.10f, 0.84f, 0.36f), float3(0.98f, 0.86f, 0.14f), (t - 0.50f) * 4.0f);
    return lerp(float3(0.98f, 0.86f, 0.14f), float3(1.00f, 0.14f, 0.06f), (t - 0.75f) * 4.0f);
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    const float4 game = GameLit.Load(ip);
    const float d = Depth.Load(ip);
    if ((flags.x & FLAG_SURFACE) == 0u) { return game; }
    // Clean 3D World Map isolation: ONLY bypass when MapMenu pauses the game (timeEnv.w > 0.5) AND camera is at
    // overhead map altitude (> 42,000u). Never bypass walkable mountains (High Hrothgar Z~15,800, Throat of the World Z~38,600)!
    const bool isWorldMap = (timeEnv.w > 0.5f) && (camPosW.z > 42000.0f);
    if (isWorldMap) { return game; }
    if (d >= 1.0f) {
        // Pass raw sky/celestial buffer (Masser, Secunda, exotic DLC realms) cleanly straight to LightFog.hlsl
        // which owns 100% of the 24h physical Hillaire skybox, single isRealSun() solar disc, lunar halo, stars, aurora & clouds.
        // (Removes duplicate sunDisc in Lighting.hlsl that previously leaked onto the Moon via moonKeep!)
        if (flags.w == 1u) { return float4(0.5f, 0.5f, 1.0f, game.a); }
        if (flags.w == 2u) { return float4(1.0f, 1.0f, 1.0f, game.a); }
        if (flags.w == 10u) { return float4(1.0f, 0.0f, 1.0f, game.a); }
        if (flags.w == 28u) { return float4(0.04f, 0.06f, 0.12f, game.a); }
        if (flags.w == 29u) { return float4(0.02f, 0.04f, 0.10f, game.a); }
        if (flags.w == 30u || flags.w == 31u || flags.w == 32u || flags.w == 34u || flags.w == 35u || flags.w == 36u) {
            return float4(0.03f, 0.04f, 0.06f, game.a);
        }
        if (flags.w == 33u) { return float4(debugHdrEnergyHeatmap(game.rgb), game.a); }
        return float4(game.rgb, game.a);
    }
    // View 9, a test control: the pass reaches only the top-left quarter, as a half-size viewport
    // would. The coverage check in the capture must fail on it.
    if (flags.w == 9u && (at.x >= dims.x * 0.5f || at.y >= dims.y * 0.5f)) { return game; }
    const float4 sp = Spec.Load(ip);
    const uint cls = uint(sp.z * 255.0f + 0.5f), base = cls & 127u;
    // Effects, water, first person and distant LOD (lit per vertex by the game): the game's own.
    if (flags.w == 12u) {   // view 12: the material class (skin red, foliage green, landscape brown, parallax blue, the rest grey)
        const float3 cc = base == 10u ? float3(1, 0.1, 0.1) : base == 14u ? float3(1.0, 0.35, 0.06) : base == 5u ? float3(0.1, 0.8, 0.1) : base == 4u ? float3(0.5, 0.35, 0.1) :
                          base == 6u ? float3(0.1, 0.3, 1) : base == 7u ? float3(0.1, 0.9, 0.9) : base == 0u ? 0.0f.xxx : 0.5f.xxx;
        return float4(cc * (0.35f + 0.65f * dot(GameLit.Load(ip).rgb, 0.333f)), game.a);
    }
    // Distant LOD (class 7: lit per vertex by the game, so it read flat beside the trees and ground RAW lights).
    // FOL-04: in improved mode it gets the same sun, shadows, ambient and sky the near surfaces get. Views 14 and 15
    // show its G-buffer normal and base colour, the rest of the frame dimmed.
    if (base == 7u && (cls & 128u) == 0u) {
        if (flags.w == 28u) {
            const float lodL = saturate(dot(Albedo.Load(ip).rgb, float3(0.2126f, 0.7152f, 0.0722f)));
            return float4(debugClassColor(7u, false, 0.0f) * (0.45f + 0.55f * lodL), game.a);
        }
        if (flags.w == 29u) {
            const float zLod = max(viewZOf(d), 1.0f);
            const float zRamp = saturate(log2(zLod) / 15.0f);
            const float zDisc = smoothstep(0.03f, 0.12f, (abs(ddx(zLod)) + abs(ddy(zLod))) / max(zLod, 1.0f));
            return float4(lerp(float3(zRamp * 0.85f, zRamp * 0.55f, 1.0f - zRamp * 0.65f), float3(0.15f, 1.0f, 0.95f), zDisc), game.a);
        }
        if (flags.w == 30u) { return float4(saturate(RtVis.Load(ip).x), 0.0f, 1.0f, game.a); }
        if (flags.w == 31u) { return float4(0.08f, 0.0f, 0.08f, game.a); }
        if (flags.w == 32u || flags.w == 34u) { return float4(Albedo.Load(ip).rgb * 0.16f, game.a); }
        if (flags.w == 35u) {
            const float zLod35 = max(viewZOf(d), 1.0f);
            const float zEdge35 = smoothstep(0.025f, 0.10f, (abs(ddx(zLod35)) + abs(ddy(zLod35))) / zLod35);
            return float4(zEdge35 * 0.85f, zEdge35 * 0.45f, 0.08f, game.a);
        }
        if (flags.w == 36u) { return float4(0.02f, 0.02f, 0.04f, game.a); }
        if (flags.w == 1u || flags.w == 14u) { return float4(worldNormal(Normal.Load(ip)) * 0.5f + 0.5f, game.a); }
        if (flags.w == 15u) { return float4(Albedo.Load(ip).rgb, game.a); }
        // Views 2, 4 and 5 on distant LOD: the sun, sky and bounce of the march through the depth (RtShadow.hlsl, RtFar).
        if (flags.w == 2u) { return float4(RtVis.Load(ip).xxx, game.a); }
        if (flags.w == 4u) { return float4(RtIdx.Load(ip).www, game.a); }
        if (flags.w == 5u) { return float4(RtBounce.Load(ip).rgb * proj.w, game.a); }
        if ((flags.x & FLAG_MATCH) != 0u || (flags.x & FLAG_LOD) == 0u || sunDir.w < 0.5f) {
            return flags.w == 33u ? float4(debugHdrEnergyHeatmap(game.rgb), game.a) : game;
        }
        const float3 dLit = distantLit(ip, at.xy, d);
        if (flags.w == 33u) { return float4(debugHdrEnergyHeatmap(dLit), game.a); }
        return float4(dLit, game.a);
    }
    if (flags.w == 14u || flags.w == 15u) { return float4(game.rgb * 0.2f, game.a); }
    const bool skin = base == 10u;
    const bool isHair = base == 11u;
    const bool isEye = base == 12u;
    const bool isSkinnedActor = base == 14u;   // Wave 63 Contract: Skinned Actor / Creature / Fur / Clothing
    // Unlock full physically-based PBR shading for Skin (10), Hair (11), Eyes (12), and Skinned Actor/Creature/Cloth (14)!
    if (base == 0u || (base >= 7u && !skin && !isHair && !isEye && !isSkinnedActor) || (cls & 128u) != 0u) {
        if (flags.w == 1u) {
            const float3 nEff = worldNormal(Normal.Load(ip));
            const float nEffL = dot(nEff, nEff);
            return float4((nEffL > 0.25f ? (nEff * rsqrt(nEffL)) : float3(0.0f, 0.0f, 1.0f)) * 0.5f + 0.5f, game.a);
        }
        if (flags.w == 2u) { return float4(1.0f, 1.0f, 1.0f, game.a); }
        if (flags.w == 28u) { return float4(debugClassColor(base, false, 0.0f) * (0.45f + 0.55f * saturate(dot(game.rgb, 0.333f))), game.a); }
        if (flags.w == 29u) {
            const float zEarly = max(viewZOf(d), 1.0f);
            const float zRamp = saturate(log2(zEarly) / 15.0f);
            const float zDisc = smoothstep(0.03f, 0.12f, (abs(ddx(zEarly)) + abs(ddy(zEarly))) / max(zEarly, 1.0f));
            return float4(lerp(float3(zRamp * 0.85f, zRamp * 0.55f, 1.0f - zRamp * 0.65f), float3(0.15f, 1.0f, 0.95f), zDisc), game.a);
        }
        if (flags.w == 33u) { return float4(debugHdrEnergyHeatmap(game.rgb), game.a); }
        if (flags.w == 34u || flags.w == 35u || flags.w == 36u) { return float4(0.04f, 0.05f, 0.07f, game.a); }
        return game;
    }

    // Wave 63: Define actor/creature flags BEFORE all terrain smoothing, bark, strata, snow & weathering passes
    // so animals (Stump the dog), NPC gloves/hands (Frodnar), and skinned clothes NEVER receive moss/rock/terrain passes!
    const bool isActorFeat  = (skin || isHair || isEye);
    const bool isActorCloth = (isSkinnedActor || (!isActorFeat && base != 2u && base != 5u && base != 13u && SoftMap.Load(ip).r < 0.0f));

    const bool match = (flags.x & FLAG_MATCH) != 0u;
    const float2 px = floor(at.xy);
    const float3 p = worldAt(px, d);
    float3 albedo = Albedo.Load(ip).rgb;
    // Full-range PBR albedo expansion (!match, non-skin): rescues vanilla Skyrim's narrow washed-out 2011 texture range
    // while protecting dark bark/timber albedos (aLum < 0.10) from crushing down into pitch black!
    if (!match && !skin && base != 12u) {
        const float aLum = max(dot(albedo, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-4f);
        const float darkProtect = smoothstep(0.02f, 0.12f, aLum);
        const float expLum = lerp(aLum, saturate(pow(aLum, 1.12f) * 1.14f), darkProtect);
        albedo = saturate(albedo * (expLum / aLum));
    }
    const float z = viewZOf(d), jit = ign(px);
    Surface s;
    s.n = worldNormal(Normal.Load(ip));
    // GPU-Native PN-Triangle / Cross-Bilateral Mesh & Terrain Normal Rounding (strictly excludes skin & actors!):
    if (!match && !isActorCloth && (base == 1u || base == 4u) && z < 3600.0f) {
        const int2 dMaxN = int2(dims.xy) - 1;
        const float zPlaneTol = max(z * 0.015f, 2.5f);
        float3 nAcc = s.n * 1.4f;
        float wAcc = 1.4f;
        static const int2 kPnOff[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
        [unroll] for (int ni = 0; ni < 4; ++ni) {
            const int2 qn = clamp(int2(px) + kPnOff[ni], int2(0, 0), dMaxN);
            const float dq = Depth.Load(int3(qn, 0));
            if (dq >= 1.0f) { continue; }
            const float dz = abs(viewZOf(dq) - z);
            if (dz > zPlaneTol) { continue; }
            const float3 nq = worldNormal(Normal.Load(int3(qn, 0)));
            const float nd = dot(s.n, nq);
            // Smooth sharp 2011 polygon creases (0.35 < dot < 0.99) into continuous curved C1 bevels:
            if (nd > 0.35f) {
                const float wn = (1.0f - dz / zPlaneTol) * smoothstep(0.35f, 0.72f, nd);
                nAcc += nq * wn;
                wAcc += wn;
            }
        }
        s.n = normalize(nAcc / max(wAcc, 1.0e-4f));
    }
    s.v = normalize(eyeAt() - p);
    s.shin = exp2(sp.y * 13.0f);
    const bool isTruePbr = (!match && base == 13u);
    const float3 rmaosPbr = isTruePbr ? saturate(SoftMap.Load(ip).rgb) : 0.0f.xxx;
    s.mask = isTruePbr ? max(saturate(1.0f - rmaosPbr.r), rmaosPbr.g) : sp.x;
    bool lobe = isTruePbr || (sp.y > 0.002f && base != 4u && base != 6u);   // TruePBR always has physical GGX lobe
    s.f0 = isTruePbr ? lerp(0.04f, 0.90f, rmaosPbr.g) : (lobe ? lerp(0.02f, 0.08f, saturate(sp.x * resScale.w)) : 0.04f);
    s.alpha = isTruePbr ? clamp(rmaosPbr.r * rmaosPbr.r, 0.04f, 0.98f) : clamp(sqrt(2.0f / (s.shin + 2.0f)), 0.03f, 1.0f);
    // Soft-lit (foliage): the game multiplies its wrap term by the soft-lighting map, which the
    // patched shader writes to RAW's emissive target. Before that channel existed a grey constant
    // fitted on one frame (0.15, tools/lighting/fitsoft.py) stood in; it lost the warm back light.
    // Skin too: the body and hands shaders read the same soft-lighting map at t12 (the patch writes it to the emissive
    // target), and without it the arms read darker than the game's (rt6-tm-1).
    s.soft = base == 5u || skin ? max(SoftMap.Load(ip).rgb, 0.0f.xxx) : 0.0f.xxx;   // negative: the character light (below)
    // Softer since the hands and bodies joined the class (rt6-wat-3): at 0.55 / 0.22 / 0.12 the arms read orange
    // against the game's brown.
    s.wrap = skin && !match ? float3(0.36f, 0.16f, 0.08f) : (isHair && !match ? float3(0.16f, 0.12f, 0.08f) : 0.0f.xxx);
    // Wave 64: Classify living green pine saplings/shrubs rendered under base==1u AND vertical warm-brown tree bark
    // at fs() scope so BOTH bark shadow-floor protection AND pine chalk-bleach protection run consistently across every pass!
    const bool isGreenPineCard = (!match && base == 1u && !isActorCloth && sunColour.w > 0.5f
        && albedo.g > max(albedo.r, albedo.b) * 1.04f && albedo.g > 0.020f && z < 6500.0f);
    const bool isWoodBark = (!match && base == 1u && !isActorCloth && !isGreenPineCard && sunColour.w > 0.5f
        && abs(s.n.z) < 0.72f && albedo.r > albedo.b * 1.06f && albedo.r >= albedo.g * 0.96f && albedo.r > 0.015f && z < 5500.0f);
    if (skin && !match) {
        // Preserve 100% native facial normal-map pores/creases (no synthetic cNoise breakup) with physiological roughness floor:
        s.f0 = 0.026f;
        s.alpha = max(s.alpha, 0.48f);
    } else if (isEye && !match) {
        s.f0 = 0.045f;
        s.alpha = 0.055f;   // wet physical corneal mirror gloss
    } else if (isHair && !match) {
        s.f0 = 0.046f;
        s.alpha = clamp(s.alpha, 0.28f, 0.55f);
    } else if (isSkinnedActor && !match) {
        // Wave 63 Class-14 Skinned Actor / Creature / Fur / Clothing PBR defaults (strictly excludes moss/strata/bark):
        s.f0 = 0.036f;
        s.alpha = clamp(s.alpha, 0.34f, 0.88f);
    } else if (!match && base == 1u && !isActorCloth && !isGreenPineCard && z < 2600.0f) {
        // Dynamic 2026 Real-Material Micro-Relief on 2011 Tree Bark, Timber, Masonry & Nordic Rocks:
        const float3 wpMat = (p + camPosW.xyz) * 0.032f;
        const float wear = cNoise(wpMat) * 0.65f + cNoise(wpMat * 2.7f + 5.1f) * 0.35f;
        const float wearFade = 1.0f - smoothstep(180.0f, 2600.0f, z);
        s.alpha = clamp(s.alpha * lerp(1.0f, lerp(0.74f, 1.26f, wear), wearFade * 0.68f), 0.06f, 0.96f);
        // 1) Procedural 4K-grade anisotropic tree bark & wood-grain furrows on vertical warm-brown surfaces:
        if (isWoodBark && z < 2000.0f) {
            const float3 bP = float3((p.xy + camPosW.xy) * 0.42f, (p.z + camPosW.z) * 0.075f);
            const float b0 = cNoise(bP);
            const float b1 = cNoise(bP + float3(0.18f, 0.0f, 0.0f));
            const float b2 = cNoise(bP + float3(0.0f, 0.18f, 0.0f));
            const float barkFade = 1.0f - smoothstep(140.0f, 2000.0f, z);
            s.n = normalize(s.n + float3((b0 - b1) * 0.68f, (b0 - b2) * 0.68f, 0.0f) * barkFade);
            // Lift sculpted sunlit/shaded bark ridges gently (0.90..1.16) so dark spruce bark never crushes to #050505!
            albedo *= lerp(1.0f, 0.90f + 0.26f * b0, barkFade * 0.60f);
        }
        // 2) Sculpted Tri-Planar Rock-Cliff Sedimentary Strata, Fracture Fissures & Rain-Runoff Decals
        //    (Wave 63: removed redundant stacked parallax moss pass #1; moss is unified cleanly in RawWeathering.hlsli!):
        if (sunColour.w > 0.5f && z < 2600.0f) {
            const float3 wpAbs = p + camPosW.xyz;
            const bool isRockCliff = (!isWoodBark && abs(s.n.z) < 0.76f && abs(albedo.r - albedo.b) < 0.14f);
            if (isRockCliff) {
                const float cliffFade = 1.0f - smoothstep(220.0f, 2600.0f, z);
                // Sedimentary horizontal strata ledges + sharp vertical basalt fracture cracks:
                const float strataWarp = cNoise(wpAbs * 0.014f) * 28.0f;
                const float strataH = sin((wpAbs.z + strataWarp) * 0.16f) * 0.5f + 0.5f;
                const float crackN = abs(cNoise(wpAbs * 0.065f + float3(7.1f, 3.3f, 11.7f)) * 2.0f - 1.0f);
                const float fissure = 1.0f - smoothstep(0.02f, 0.14f, crackN);
                // Rain-runoff mineral streak decals running vertically down stone walls & cliffs:
                const float streak = smoothstep(0.58f, 0.86f, cNoise(float3(wpAbs.xy * 0.085f, wpAbs.z * 0.009f + 4.7f)));
                const float3 cliffGrad = float3((cNoise(wpAbs * 0.11f + float3(0.4f, 0.0f, 0.0f)) - cNoise(wpAbs * 0.11f - float3(0.4f, 0.0f, 0.0f))),
                                                (cNoise(wpAbs * 0.11f + float3(0.0f, 0.4f, 0.0f)) - cNoise(wpAbs * 0.11f - float3(0.0f, 0.4f, 0.0f))),
                                                (strataH - 0.5f) * 0.55f);
                s.n = normalize(s.n + cliffGrad * (0.42f * cliffFade));
                albedo *= lerp(1.0f, (0.86f + 0.22f * strataH) * (1.0f - fissure * 0.16f) * (1.0f - streak * 0.10f), cliffFade * 0.62f);
                s.alpha = lerp(s.alpha, max(s.alpha * 0.82f, 0.30f), streak * cliffFade * 0.40f);
                // Pale silver-green alpine lichen crust decals on exposed high-altitude rock above 1200u:
                if (wpAbs.z > 1200.0f) {
                    const float lichen = smoothstep(0.68f, 0.88f, cNoise(wpAbs * 0.09f + 29.3f)) * smoothstep(1200.0f, 2600.0f, wpAbs.z);
                    albedo = lerp(albedo, float3(0.48f, 0.52f, 0.44f), lichen * cliffFade * 0.35f);
                }
            }
        }
    }
    if (base == 5u && !any(s.soft > 0.0f)) { s.soft = 0.15f.xxx; }   // a soft-lit shader the patch found no map in
    // Rain (Weather.hlsli), exteriors in improved mode: darker, glossier, puddles flat and mirror-like.
    // Wave 63 + Wave 64: Strictly exclude actors/creatures (isActorFeat || isActorCloth) AND track fireHeatDbg for View 36!
    Wet wet;
    wet.wet = 0.0f; wet.puddle = 0.0f; wet.snow = 0.0f;
    float2 rings = 0.0f;   // rain on the puddles: the water's slope, which also bends the reflection
    float fireHeatDbg = 0.0f;
    if (!match && sunColour.w > 0.5f && base != 5u && !isActorFeat && !isActorCloth) {
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
        // Wave 64: Use unified isGreenPineCard so alpha-tested pine saplings NEVER get snow-painted white on clear days!
        const bool isGreenPine = isGreenPineCard;
        const bool activeSnowWx = (weather.z > 0.08f || seasonThermal0.x > 0.05f || season == 4u);
        if (wet.snow > 0.0f) {
            // Never paint living green pine branches chalky white on clear days; cap winter snow dusting to delicate tips!
            const float snowEff = isGreenPine ? (activeSnowWx ? (wet.snow * 0.18f) : 0.0f) : wet.snow;
            // SKY-09 Volumetric 3D Sastrugi Snowdrift Relief + Glacial Subsurface Scattering:
            const float3 wpS = p + camPosW.xyz;
            const float2 wFlowS = normalize(cloudWind.xy + float2(1.0f, 0.35f));
            const float2 sCoord = float2(dot(wpS.xy, wFlowS) * 0.0045f, dot(wpS.xy, float2(-wFlowS.y, wFlowS.x)) * 0.014f);
            const float sastrugi = cNoise(float3(sCoord, wpS.z * 0.008f)) * 0.62f + cNoise(float3(sCoord * 2.4f + 7.3f, wpS.z * 0.018f)) * 0.38f;
            const float driftDx = cNoise((wpS + float3(7.0f, 0.0f, 0.0f)) * 0.015f) - cNoise((wpS - float3(7.0f, 0.0f, 0.0f)) * 0.015f);
            const float driftDy = cNoise((wpS + float3(0.0f, 7.0f, 0.0f)) * 0.015f) - cNoise((wpS - float3(0.0f, 7.0f, 0.0f)) * 0.015f);
            const float3 snowCrest = float3(0.76f, 0.80f, 0.86f);
            const float3 snowTroughSSS = float3(0.42f, 0.56f, 0.72f);   // glacial ice-blue subsurface scatter in drift troughs
            const float3 snowCol = lerp(snowTroughSSS, snowCrest, smoothstep(0.22f, 0.78f, sastrugi));
            albedo = lerp(albedo, snowCol, snowEff);
            s.alpha = lerp(s.alpha, 0.86f, snowEff);
            s.shin = lerp(s.shin, 2.0f / (0.86f * 0.86f) - 2.0f, snowEff);
            if (!isGreenPine) {
                s.n = normalize(lerp(s.n, normalize(ng + float3(-driftDx * 0.92f, -driftDy * 0.92f, 0.22f)), snowEff * 0.78f));
            }
            s.f0 = lerp(s.f0, 0.018f, snowEff);
            s.mask = lerp(s.mask, 0.03f, snowEff);
            wet.puddle *= 1.0f - snowEff;
            wet.wet *= 1.0f - snowEff;
            wet.snow = snowEff;
        }
        // Wave 39 Unified Live Seasonal & Surface Thermal Energy Balance (RawSeasonsThermals.hlsli):
        // Evaluates real orbital-season snowpack accumulation/compaction/albedo aging, directional slope/aspect melt,
        // active campfire/hearth/forge/torch heat-radius melt rings, static freeze-thaw black ice & windward frost rime,
        // and solar/wind puddle evaporation across all exterior non-actor terrain, architecture, roofs & rocks!
        const bool isNonActorExt = (!skin && !isHair && !isEye && base != 5u);
        if (isNonActorExt && z < 8000.0f) {
            // Compute local fire/hearth/forge thermal heat flux over active cluster warm lights via evalFireThermalMeltMask:
            float fireHeat01 = 0.0f;
            if (z < 2600.0f && flags.y > 0u) {
                const uint4 cBitsT = clusters[clusterOf(px, z)];
                uint warmChecked = 0u;
                [loop] for (uint tw = 0u; tw < 4u && warmChecked < 16u; ++tw) {
                    uint tb = cBitsT[tw];
                    [loop] while (tb != 0u && warmChecked < 16u) {
                        const uint tli = tw * 32u + firstbitlow(tb);
                        tb &= tb - 1u;
                        if (tli >= flags.y) { continue; }
                        const float3 tlc = lightColS[tli].rgb;
                        if (tlc.r > tlc.b * 1.25f && tlc.r > 0.18f) {
                            const float rMelt = min(lightPosR[tli].w * 0.45f, 280.0f);
                            const float hStr  = saturate(tlc.r * 0.85f);
                            fireHeat01 = max(fireHeat01, evalFireThermalMeltMask(p, lightPosR[tli].xyz, rMelt, hStr));
                            ++warmChecked;
                        }
                    }
                }
            }
            fireHeatDbg = max(fireHeatDbg, fireHeat01);
            const bool hasSeasonState = (seasonThermal0.x > 0.005f || seasonThermal1.x > 0.005f
                                      || seasonThermal1.y > 0.01f  || seasonThermal1.z > 0.01f
                                      || phenology1.w > 0.001f);
            if (hasSeasonState) {
                const SurfaceThermalEval st = evalSeasonalSurfaceThermals(
                    p + camPosW.xyz, s.n, sunDir.xyz, cloudWind.xy,
                    seasonThermal0, seasonThermal1, fireHeat01,
                    wet.wet, wet.puddle, phenology1.w);
                // 1) Seasonal snowpack accumulation with metamorphic albedo/roughness aging (0 when seasonThermal0.x <= 0.005!):
                if (seasonThermal0.x > 0.005f && st.snowCov01 > 0.01f && !isGreenPine) {
                    const float sMix = st.snowCov01 * smoothstep(8000.0f, 300.0f, z);
                    albedo  = lerp(albedo, st.snowAlbedo, sMix * 0.88f);
                    s.alpha = lerp(s.alpha, st.snowRoughness, sMix * 0.85f);
                    s.f0    = lerp(s.f0, 0.018f, sMix * 0.80f);
                    wet.snow = max(wet.snow, sMix);
                }
                // 2) Fire/hearth/forge thermal melt rings: melts snowpack & natural snow into wet dark earth + glossy slush rim!
                if (st.fireMeltRing01 > 0.01f || st.slushRim01 > 0.01f) {
                    const float natSnowMelt = (base == 4u || base == 1u)
                        ? saturate((min(albedo.r, min(albedo.g, albedo.b)) - 0.30f) * 4.5f) : 0.0f;
                    const float snowHere = max(natSnowMelt, wet.snow);
                    wet.snow *= (1.0f - st.fireMeltRing01);
                    wet.wet   = max(wet.wet, max(st.fireMeltRing01 * 0.82f, st.slushRim01 * 0.78f) * max(snowHere, 0.35f));
                    albedo    = lerp(albedo, float3(0.13f, 0.11f, 0.095f), st.fireMeltRing01 * snowHere * 0.78f);
                    albedo    = lerp(albedo, float3(0.42f, 0.52f, 0.62f),  st.slushRim01 * snowHere * 0.48f);
                    s.alpha   = lerp(s.alpha, 0.14f, max(st.fireMeltRing01, st.slushRim01) * snowHere * 0.78f);
                    lobe      = lobe || (max(st.fireMeltRing01, st.slushRim01) * snowHere > 0.15f);
                }
                // 3) Static freeze-thaw black ice glaze & windward hoarfrost rime when cold (zero pulsing!):
                if (seasonThermal0.w < 0.5f && (st.iceGlaze01 > 0.02f || st.frostRime01 > 0.02f)) {
                    const float coldFade = smoothstep(5500.0f, 180.0f, z);
                    const float gIce  = st.iceGlaze01 * coldFade;
                    const float gRime = st.frostRime01 * coldFade;
                    if (gIce > 0.02f) {
                        albedo  *= lerp(1.0f, 0.74f, gIce * 0.75f);
                        s.alpha  = lerp(s.alpha, 0.085f, gIce * 0.85f);
                        s.f0     = lerp(s.f0, 0.018f, gIce * 0.85f);
                        s.mask   = max(s.mask, gIce * 0.80f);
                        lobe     = true;
                    }
                    if (gRime > 0.02f && !isGreenPine) {
                        albedo  = lerp(albedo, float3(0.74f, 0.82f, 0.90f), gRime * 0.42f);
                        s.alpha = lerp(s.alpha, 0.52f, gRime * 0.45f);
                    }
                }
                // 4) Puddle evaporation & snowmelt runoff balance:
                wet.puddle = lerp(wet.puddle, st.puddleMask01, 0.60f);
                wet.wet    = max(wet.wet * saturate(1.0f - phenology1.w * 0.45f), st.wetBoost01 * 0.72f);
            } else if (!isWoodBark && !isGreenPine && z < 2600.0f && (fireHeat01 > 0.03f || wet.snow > 0.05f || weather.z > 0.08f)) {
                // Wave 76: World-space thermal/wind precipitation & torch/brazier melt active even without external season uniforms
                // (100% absolute world-space, 0 screen-UV dependence; melts snow around Windhelm braziers & guard torches into wet stone):
                const float3 wpPt = p + camPosW.xyz;
                const float4 wGustPt = rawEvalCoherentWindGust4(wpPt, cloudWind, weather, camPosW.w);
                const float natSnowPt = (base == 4u || base == 1u)
                    ? saturate((min(albedo.r, min(albedo.g, albedo.b)) - 0.32f) * 4.2f) : 0.0f;
                const float baseSnowPt = max(wet.snow, max(natSnowPt * saturate(weather.z * 1.4f + (fireHeat01 > 0.03f ? 0.65f : 0.0f)), weather.z * 0.65f));
                const float tempPt = lerp(4.0f, -6.0f, saturate(weather.z + natSnowPt * 0.55f));
                const RawThermalPrecipResult ptRes = rawEvalWorldThermalSnowAccum(
                    wpPt, s.n, cloudWind.xy, wGustPt.z, baseSnowPt, tempPt, fireHeat01, sv);
                if (ptRes.wetSlush01 > 0.02f && fireHeat01 > 0.03f) {
                    const float meltFade = smoothstep(2600.0f, 180.0f, z) * saturate(ptRes.wetSlush01 * 0.65f);
                    wet.snow *= (1.0f - ptRes.meltCore01 * 0.75f);
                    wet.wet   = max(wet.wet, meltFade * 0.75f);
                    albedo   *= lerp(1.0f, ptRes.albedoScale, meltFade * 0.65f);
                    s.alpha   = lerp(s.alpha, ptRes.meltRoughness, meltFade * 0.65f);
                    lobe      = lobe || (meltFade > 0.12f);
                }
            } else if (fireHeat01 > 0.02f && (wet.snow > 0.05f || base == 4u) && s.n.z > 0.35f) {
                // Fallback when RawWeatherSim uniforms are zeroed (e.g. legacy synthetic WARP tests):
                const float meltMask  = smoothstep(0.10f, 0.70f, fireHeat01);
                const float slushRing = smoothstep(0.02f, 0.25f, fireHeat01) * (1.0f - smoothstep(0.45f, 0.85f, fireHeat01));
                const float natSnowMelt = (base == 4u) ? saturate((min(albedo.r, min(albedo.g, albedo.b)) - 0.34f) * 4.5f) : 0.0f;
                wet.snow *= (1.0f - meltMask);
                wet.wet   = max(wet.wet, max(meltMask * 0.82f, slushRing * 0.75f));
                albedo    = lerp(albedo, float3(0.14f, 0.12f, 0.10f), meltMask * natSnowMelt * 0.78f);
                albedo    = lerp(albedo, float3(0.44f, 0.54f, 0.64f), slushRing * natSnowMelt * 0.45f);
                s.alpha   = lerp(s.alpha, 0.15f, max(meltMask, slushRing) * max(natSnowMelt, wet.snow) * 0.75f);
            }
            // Wave 42 Dynamic Raindrop Splash Ripples on Wet Surfaces & Puddles (strictly rain-gated; 100% calm on dry days!):
            const float effWetRain = max(wet.wet, wet.puddle);
            if (weather.w > 0.08f && s.n.z > 0.72f && effWetRain > 0.15f && wet.snow < 0.25f && z < 1800.0f) {
                const float splashFade = smoothstep(1800.0f, 120.0f, z)
                                       * smoothstep(0.08f, 0.42f, weather.w)
                                       * smoothstep(0.15f, 0.65f, effWetRain)
                                       * smoothstep(0.72f, 0.92f, s.n.z)
                                       * (1.0f - saturate(wet.snow * 4.0f));
                const float wtRain = (physWorld[0].x > 0.001f) ? physWorld[0].x : camPosW.w;
                const float2 splashG = evalRainSplashRippleGrad((p + camPosW.xyz).xy, wtRain);
                const float splashAmp = lerp(0.10f, 0.24f, saturate(wet.puddle * 1.25f)) * splashFade;
                s.n = normalize(s.n + float3(-splashG.x, -splashG.y, 0.0f) * splashAmp);
                rings += splashG * (splashAmp * 0.65f);
            }
        }
        // Persistent 512x512 Toroidal World-Locked GPU PDE WorldField (exact frac(worldXY/2048) matches WorldFieldSim.hlsl!):
        // Wave 31: Detects Skyrim's permanent linear-RGB snow terrain (min > 0.16, neutral chroma) in addition to live snowfall
        // so walking across Bleak Falls Barrow / Windhelm / High Hrothgar ALWAYS carves deep shaded 3D snow trenches & berms!
        const float aMinS = min(albedo.r, min(albedo.g, albedo.b));
        const float aMaxS = max(albedo.r, max(albedo.g, albedo.b));
        const float permSnowGround = (base == 4u || base == 1u)
            ? smoothstep(0.15f, 0.32f, aMinS) * (1.0f - smoothstep(0.08f, 0.22f, aMaxS - aMinS)) * smoothstep(0.32f, 0.70f, s.n.z)
            : 0.0f;
        if ((base == 4u || base == 1u) && z < 1600.0f && s.n.z > 0.38f) {
            const float2 simUv = frac((p.xy + camPosW.xy) / 2048.0f);
            const float4 wf = WorldField.SampleLevel(Lin, simUv, 0.0f);
            const float simSnow = wf.a;
            const float simWet = wf.b;
            const float sL = WorldField.SampleLevel(Lin, frac(simUv + float2(-1.0f / 512.0f, 0.0f)), 0.0f).a;
            const float sR = WorldField.SampleLevel(Lin, frac(simUv + float2( 1.0f / 512.0f, 0.0f)), 0.0f).a;
            const float sD = WorldField.SampleLevel(Lin, frac(simUv + float2(0.0f, -1.0f / 512.0f)), 0.0f).a;
            const float sU = WorldField.SampleLevel(Lin, frac(simUv + float2(0.0f,  1.0f / 512.0f)), 0.0f).a;
            const float activeSnowMask = max(permSnowGround, weather.z > 0.04f ? saturate(simSnow) : 0.0f);
            if (activeSnowMask > 0.06f) {
                const float snowFade = smoothstep(1600.0f, 160.0f, z);
                const float slopeSnow = smoothstep(0.38f, 0.75f, s.n.z);
                const float avgNeighbor = (sL + sR + sD + sU) * 0.25f;
                const float trenchDip = saturate((avgNeighbor - simSnow) * 9.5f);
                const float bermLift  = saturate((simSnow - avgNeighbor) * 7.5f);
                if (weather.z > 0.04f) { wet.snow = max(wet.snow, simSnow * slopeSnow); }
                s.n = normalize(s.n + float3((sL - sR) * 2.45f, (sD - sU) * 2.45f, 0.0f) * (activeSnowMask * slopeSnow * snowFade));
                // Deepen carved snow boot-trenches with unmistakable glacial blue shadow + crisp pushed-up berm crests:
                albedo = lerp(albedo, albedo * float3(0.56f, 0.68f, 0.86f), trenchDip * activeSnowMask * slopeSnow * snowFade * 0.85f);
                albedo = min(albedo * (1.0f + bermLift * activeSnowMask * slopeSnow * snowFade * 0.18f), float3(0.86f, 0.90f, 0.94f));
            }
            if (weather.w > 0.05f) { wet.wet = max(wet.wet, simWet * (1.0f - wet.snow) * 0.55f); }
        }
        // Wave 38 Smooth Analytical World-Space Micro-Normal Relief (ZERO coarse 4-step parallax staircase bands!):
        // Uses analytical 2-octave rawNoise3Grad tangent normals + continuous micro-cavity shading on terrain & stone.
        if ((base == 1u || base == 4u) && z < 1200.0f) {
            const float3 wpM = p + camPosW.xyz;
            const float mFade = smoothstep(1200.0f, 120.0f, z);
            const float4 gLo = rawNoise3Grad(wpM * 0.16f + float3(3.7f, 11.3f, 5.1f));
            const float4 gHi = rawNoise3Grad(wpM * 0.48f + float3(19.1f, 7.3f, 23.9f));
            const float3 microGrad = gLo.yzw * 0.14f + gHi.yzw * 0.075f;
            const float3 tangGrad = microGrad - s.n * dot(microGrad, s.n);
            s.n = normalize(s.n + tangGrad * mFade);
            if (base == 4u && z < 850.0f) {
                const float microVal = gLo.x * 0.65f + gHi.x * 0.35f;
                const float smoothCav = lerp(0.92f, 1.05f, microVal);
                albedo *= lerp(1.0f, smoothCav, smoothstep(850.0f, 120.0f, z) * 0.65f);
            }
        }
        // Wind-swept glacial sastrugi snowdrift relief + Wave 34 alexandre196/karynatrychyk Volumetric Snow Overhang Rim & Blue SSS:
        // Linear-RGB snow detection (permSnowGround) works on ALL Skyrim snow biomes even in clear dry weather!
        const float natSnow = permSnowGround;
        const float snowAmt = max(wet.snow, natSnow);
        const float mudAmt = (base == 4u && weather.w > 0.08f) ? max(wet.wet, wet.puddle) * (1.0f - snowAmt) : 0.0f;
        // Sculpt a puffy rounded snow lip normal + glacial blue subsurface rim wherever snow blankets bare rock/logs!
        if ((base == 1u || base == 4u) && z < 2400.0f) {
            const float sDx = ddx(snowAmt), sDy = ddy(snowAmt);
            const float rimBand = saturate(snowAmt * (1.0f - snowAmt) * 4.0f) * smoothstep(2400.0f, 180.0f, z);
            if (rimBand > 0.04f) {
                const float3 dpxS = ddx(p), dpyS = ddy(p);
                const float3 lipGrad = normalize(dpxS * sDx + dpyS * sDy + float3(0.0f, 0.0f, 1.0e-4f));
                s.n = normalize(lerp(s.n, normalize(s.n - lipGrad * 0.42f + float3(0.0f, 0.0f, 0.24f)), rimBand * 0.55f));
                // Cool ice-blue subsurface glow right inside the thick snow overhang edge (alexandre196 snow sphere!):
                albedo = lerp(albedo, lerp(albedo, float3(0.48f, 0.68f, 0.90f), 0.36f), rimBand * 0.62f);
            }
        }
        if (snowAmt > 0.18f && z < 3200.0f && s.n.z > 0.45f) {
            const float3 wpSn = p + camPosW.xyz;
            const float2 wDirS = normalize(cloudWind.xy + float2(1.0f, 0.35f));
            const float2 wPerpS = float2(-wDirS.y, wDirS.x);
            const float2 sUv = float2(dot(wpSn.xy, wDirS) * 0.012f, dot(wpSn.xy, wPerpS) * 0.036f);
            const float sDx = (cNoise(float3(sUv + float2(0.15f, 0.0f), 2.1f)) - cNoise(float3(sUv - float2(0.15f, 0.0f), 2.1f))) * 0.16f;
            const float sDy = (cNoise(float3(sUv + float2(0.0f, 0.15f), 2.1f)) - cNoise(float3(sUv - float2(0.0f, 0.15f), 2.1f))) * 0.16f;
            const float driftFade = saturate((snowAmt - 0.18f) * 1.6f) * smoothstep(3200.0f, 240.0f, z);
            s.n = normalize(s.n + float3(wDirS * sDx + wPerpS * sDy, 0.0f) * driftFade);
            s.alpha = max(s.alpha, lerp(0.58f, 0.74f, driftFade));
        }
        // Wave 43 Real 3D Volumetric Snowpack Boot-Print & NPC Trail Carving + Mud Impressions (RawSeasonsThermals.hlsli):
        // Carves deep shaded elliptical boot trenches + raised rim snowbanks across player & 16 live ProcessLists NPC trails!
        const float effWetTrail = max(mudAmt, max(wet.wet, wet.puddle) * (1.0f - snowAmt));
        if (!skin && !isHair && !isEye && base != 5u && s.n.z > 0.65f && z < 2600.0f && (snowAmt > 0.15f || effWetTrail > 0.25f)) {
            const float trailFade = smoothstep(2600.0f, 180.0f, z) * smoothstep(0.65f, 0.82f, s.n.z);
            const FootTrailResult ftRes = evalSnowAndMudFootTrail(p + camPosW.xyz, s.n, footTrail, snowAmt, effWetTrail);
            if (ftRes.cavityDepth01 > 0.01f || ftRes.rimHeight01 > 0.01f) {
                s.n = normalize(s.n + ftRes.normalPerturb * trailFade);
                if (snowAmt > 0.15f) {
                    // Compressed icy-blue subsurface snow trench + sunlit raised rim snowbank:
                    const float3 trenchBlue = float3(0.76f, 0.88f, 1.08f) * ftRes.aoFactor;
                    albedo = lerp(albedo, albedo * trenchBlue, saturate(ftRes.cavityDepth01 * trailFade * 0.92f));
                    albedo = min(albedo * (1.0f + ftRes.rimHeight01 * trailFade * 0.14f), 0.92f.xxx);
                } else {
                    // Darkened wet muddy boot depression pooling glossy rainwater:
                    albedo *= lerp(1.0f, ftRes.aoFactor, trailFade);
                    s.mask = max(s.mask, ftRes.cavityDepth01 * trailFade * 0.72f);
                    lobe = lobe || (ftRes.cavityDepth01 * trailFade > 0.15f);
                }
                s.alpha = clamp(s.alpha + ftRes.roughDelta * trailFade, 0.08f, 0.96f);
            }
        }
    }
    // Seasonal & 3D Botanical Canopy Volume Transport for BOTH soft-lit (base==5u) AND alpha-tested pine/shrub cards (isGreenPineCard):
    const bool isBotanicalCard = (!match && (base == 5u || isGreenPineCard));
    if (isBotanicalCard) {
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
        // Wave 36 + Wave 64 Old-Growth 3D Conifer Canopy Volume:
        // Rich deep Nordic spruce/fir chlorophyll albedo (zero chalky bleached white-green pine needles!),
        // 3-step view-tangent bough parallax heightfield, and balanced 3D crown-to-underside shading.
        if (z < 7200.0f) {
            const float3 wpF = p + camPosW.xyz;
            const float fFade = 1.0f - smoothstep(240.0f, 7200.0f, z);
            const float3 vTanF = -s.v / max(abs(dot(s.n, s.v)), 0.35f);
            float boughDepth = 0.0f;
            [unroll] for (int bp = 0; bp < 3; ++bp) {
                const float hBp = (float(bp) + 1.0f) * 0.333f;
                const float3 pBp = (wpF + vTanF * (hBp * 6.5f)) * 0.032f;
                boughDepth += rawNoise3Grad(pBp).x * (0.50f - hBp * 0.16f);
            }
            const float4 bGrad = rawNoise3Grad(wpF * 0.018f + float3(3.1f, 5.7f, 1.7f));
            const float3 canopyMacroN = normalize(float3(s.n.xy * 0.72f + bGrad.yz * 0.42f,
                                                         s.n.z * 0.62f + 0.24f + bGrad.w * 0.24f));
            s.n = normalize(lerp(s.n, canopyMacroN, 0.46f * fFade));
            const float leafVar = saturate(boughDepth * 1.25f);
            s.f0 = 0.016f;
            s.alpha = clamp(lerp(s.alpha, 0.80f, 0.78f * fFade), 0.64f, 0.94f);
            const float speciesId = cNoise(wpF * 0.0045f + float3(17.3f, 9.1f, 3.7f));
            // Deep saturated boreal spruce & old-growth redwood/fir reflectance (Crimson Desert / NordicCanopy palette):
            const float3 speciesTint = speciesId < 0.5f
                ? lerp(float3(0.42f, 0.76f, 0.38f), float3(0.50f, 0.82f, 0.42f), speciesId * 2.0f)
                : lerp(float3(0.50f, 0.82f, 0.42f), float3(0.56f, 0.80f, 0.38f), (speciesId - 0.5f) * 2.0f);
            const float crownDepthAo = lerp(0.50f, 0.98f, saturate(s.n.z * 0.55f + 0.45f)) * lerp(0.70f, 1.08f, leafVar);
            albedo *= lerp(1.0f.xxx, speciesTint * crownDepthAo, fFade * 0.86f);
            // Boost living pine chlorophyll saturation while preventing chalky pale white-green needles:
            const float fLuma = dot(albedo, float3(0.2126f, 0.7152f, 0.0722f));
            albedo = saturate(lerp(fLuma.xxx, albedo, 1.36f));
            // Wave 39 + Wave 64 Live Seasonal Botanical Phenology (RawSeasonsThermals.hlsli):
            // Strictly gate winter bough snow & frost rim on genuine freezing/snowy conditions so clear-day
            // Riverwood pine boughs NEVER pick up pale grey-white frost bleach!
            const bool hasLivePhen = (sunColour.w > 0.5f)
                && ((phenology0.x + phenology0.y + phenology0.z + phenology0.w + phenology1.x + phenology1.y) > 0.01f);
            const bool activeSnowFrost = (weather.z > 0.08f || seasonThermal0.x > 0.05f || seasonF == 4u
                || (seasonThermal0.y > 0.05f && seasonThermal0.w < -1.5f));
            if (hasLivePhen) {
                const bool isEvergreenConifer = (base == 1u) || (albedo.g < 0.26f && albedo.b >= albedo.r * 0.65f);
                const SeasonalFoliageEval sf = evalSeasonalFoliagePhenology(
                    wpF, s.n, albedo, phenology0, phenology1, seasonThermal0, seasonThermal1, isEvergreenConifer);
                albedo = saturate(albedo * sf.albedoTint);
                const float boughSnow = activeSnowFrost ? (sf.canopySnowCov01 * (isEvergreenConifer ? 0.22f : 0.28f) * fFade) : 0.0f;
                const float frostEdge = activeSnowFrost ? (sf.frostRim01 * 0.12f * fFade) : 0.0f;
                if (boughSnow + frostEdge > 0.005f) {
                    albedo = lerp(albedo, float3(0.58f, 0.65f, 0.72f), saturate(boughSnow + frostEdge));
                }
                albedo = isEvergreenConifer
                    ? min(albedo, lerp(float3(0.22f, 0.34f, 0.19f), float3(0.42f, 0.48f, 0.46f), boughSnow))
                    : min(albedo, float3(0.46f, 0.38f, 0.24f));
            } else {
                albedo = min(albedo, float3(0.22f, 0.34f, 0.19f));
            }
        }
    }
    const float3 nSmooth = s.n;
    // Physically grounded rain-wetness on characters (roughness 0.38..0.48 avoids polygonal specular facets along low-poly mesh edges!):
    if (!match && sunColour.w > 0.5f && weather.w > 0.08f && (isActorFeat || isActorCloth || base == 2u) && z < 2600.0f) {
        const float actorWet = saturate(weather.w * 1.15f) * saturate(s.n.z * 0.50f + 0.50f) * (1.0f - weather.z * 0.75f);
        if (isActorCloth) {
            albedo *= lerp(1.0f, 0.80f, actorWet * 0.65f);
            s.alpha = lerp(s.alpha, 0.48f, actorWet * 0.50f);
        } else {
            s.alpha = lerp(s.alpha, 0.38f, actorWet * 0.50f);
            s.f0 = lerp(s.f0, 0.036f, actorWet * 0.40f);
        }
    }
    // Organic world-space anti-tiling & natural slope curvature on terrain (class 4) and cliffs/rocks (class 1, excluding actor cloth).
    if (!match && !isActorCloth && (base == 4u || base == 1u) && z < 16000.0f) {
        const float3 wp = p + camPosW.xyz;
        const float macroN = cNoise(wp * 0.0022f) * 0.62f + cNoise(wp * 0.0068f + 13.7f) * 0.38f;
        const float microN = cNoise(wp * 0.024f + 5.3f);
        const float antiTile = lerp(0.89f, 1.11f, macroN) * lerp(0.95f, 1.05f, microN);
        const float slope = saturate(s.n.z);
        // Natural slope transitions: crisp cool-slate highland cliffs transitioning smoothly into warm valley loam.
        const float3 slopeTint = lerp(float3(0.96f, 0.99f, 1.05f), float3(1.02f, 1.01f, 0.97f), smoothstep(0.55f, 0.88f, slope));
        const float tileFade = saturate(1.0f - z / 16000.0f);
        albedo *= lerp(1.0f.xxx, antiTile * slopeTint, tileFade * (base == 4u ? 0.85f : 0.45f));
        // (Wave 63: removed redundant moss pass #2; exterior stone/terrain moss runs once in RawWeathering.hlsli!)
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
        // 2) Steep-cliff & hillside tri-planar geological strata synthesis on terrain (base == 4u, slope < 0.74):
        //    Wave 63: Prevents flat dark-brown hillsides by injecting cool-granite mineral ledges + 3D ridge normal relief!
        if (base == 4u && slope < 0.74f && z < 11000.0f) {
            const float cliffW = (1.0f - smoothstep(0.32f, 0.74f, slope)) * saturate(1.0f - z / 11000.0f);
            const float strata = cNoise(float3(wp.xy * 0.0045f, wp.z * 0.028f)) * 0.60f
                               + cNoise(float3(wp.xy * 0.016f, wp.z * 0.075f)) * 0.40f;
            const float screeMask = smoothstep(0.45f, 0.82f, macroN * 0.60f + strata * 0.40f) * cliffW;
            const float3 graniteLedge = lerp(float3(0.22f, 0.23f, 0.24f), float3(0.38f, 0.39f, 0.38f), strata);
            albedo = lerp(albedo, lerp(albedo, graniteLedge, 0.36f), screeMask * (1.0f - wet.snow * 0.85f));
            albedo *= lerp(1.0f, lerp(0.84f, 1.24f, strata), cliffW * 0.74f);
            const float sx = cNoise(float3(wp.xy * 0.014f + float2(0.8f, 0.0f), wp.z * 0.045f))
                           - cNoise(float3(wp.xy * 0.014f - float2(0.8f, 0.0f), wp.z * 0.045f));
            const float sy = cNoise(float3(wp.xy * 0.014f + float2(0.0f, 0.8f), wp.z * 0.045f))
                           - cNoise(float3(wp.xy * 0.014f - float2(0.0f, 0.8f), wp.z * 0.045f));
            s.n = normalize(s.n + float3(sx * 0.34f, sy * 0.34f, (strata - 0.5f) * 0.46f) * cliffW);
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
        // (Wave 63: removed redundant stacked moss/lichen pass #3; single exterior moss pass lives in RawWeathering.hlsli!)
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
        // Keep 16-step Steep POM strictly on authored _p.dds heightmaps (base==6) and cobblestone terrain (base==4):
        // never displace smooth ceramic jugs, furniture, or ashlar stone walls (base==1) by painted luminance!
        if ((base == 4u || useRealHeight) && z < 1500.0f) {
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
        const float microNormScale = (base == 1u && !useRealHeight) ? 0.24f : 0.62f;
        s.n = normalize(s.n + (-gx * tVec - gy * bVec) * microNormScale);
        const float microSlope = saturate(length(float2(gx, gy)) * 1.4f);
        s.alpha = clamp(s.alpha + (microSlope - 0.15f) * 0.10f * distFade, 0.05f, 0.95f);
        // Feature 4: 8-step directional POM relief self-shadowing (gentle on base==1 so ashlar stone walls stay clean):
        if ((base == 4u || useRealHeight || (base == 1u && z < 900.0f)) && z < 1800.0f && (sunDir.w > 0.5f || flags.y > 0u)) {
            const float pomFade = saturate(1.0f - z / 1800.0f) * (useRealHeight ? 0.92f : (base == 4u ? 0.80f : 0.35f));
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
            const float pomOcc = saturate(1.0f - max(maxH - hFinal, 0.0f) * 2.0f * pomFade);
            albedo *= lerp(base == 1u ? 0.86f : 0.70f, 1.0f, pomOcc);
            s.alpha = clamp(lerp(min(s.alpha + 0.16f, 0.96f), max(s.alpha - 0.10f, 0.10f), pomOcc), 0.08f, 0.96f);
        }
    }
    // Wave 40 + Wave 58 Unified 3D Procedural Weathering (RawWeathering.hlsli):
    // Curvature-driven edge chipping & mortar erosion, multi-scale rainwater porosity darkening,
    // north-facing moss/lichen moisture accumulation, sub-zero hoarfrost micro-sparkle & thermal soot!
    const float3 wDpx = ddx(p), wDpy = ddy(p);
    const float3 wDnx = ddx(s.n), wDny = ddy(s.n);
    float porosityWetW = 0.0f;
    float hoarfrostGlintW = 0.0f;
    float weatherMossMask = 0.0f;
    float sootHeatDbg = 0.0f;
    if (!match && !isActorFeat && !isActorCloth && !isBotanicalCard && (base == 1u || base == 2u || base == 4u) && z < 3200.0f) {
        const float pxScaleW = max(length(wDpx) + length(wDpy), 0.25f);
        const float curvW = clamp((dot(wDnx, wDpx) + dot(wDny, wDpy)) / max(pxScaleW * pxScaleW * 0.25f, 0.15f), -1.0f, 1.0f);
        const float edgeMagW = saturate((length(wDnx) + length(wDny)) * 1.15f);
        const float curvConvexW  = saturate(curvW * 0.72f + edgeMagW * 0.25f * saturate(curvW * 2.0f));
        const float curvConcaveW = saturate(-curvW * 0.78f);
        // Compute thermal soot column exposure directly above active warm hearths/forges/sconces:
        float sootHeat01 = 0.0f;
        if (z < 2200.0f && flags.y > 0u) {
            const uint4 cBitsS = clusters[clusterOf(px, z)];
            uint sootChk = 0u;
            [loop] for (uint sw = 0u; sw < 4u && sootChk < 12u; ++sw) {
                uint sb = cBitsS[sw];
                [loop] while (sb != 0u && sootChk < 12u) {
                    const uint sli = sw * 32u + firstbitlow(sb);
                    sb &= sb - 1u;
                    if (sli >= flags.y) { continue; }
                    const float3 slc = lightColS[sli].rgb;
                    if (slc.r > slc.b * 1.30f && slc.r > 0.20f) {
                        sootHeat01 = max(sootHeat01, evalThermalSootColumn(p, lightPosR[sli].xyz, lightPosR[sli].w, saturate(slc.r * 0.90f)));
                        ++sootChk;
                    }
                }
            }
        }
        // River/shoreline proximity boost for creeping damp moss & capillary porosity darkening:
        float waterProxW = 0.0f;
        if (sunColour.w > 0.5f && engineWorld.z > 0.5f && engineWorld.x > -13000.0f) {
            const float abW = (p.z + camPosW.z) - engineWorld.x;
            waterProxW = (1.0f - smoothstep(12.0f, 260.0f, max(abW, 0.0f))) * smoothstep(-10.0f, 4.0f, abW);
        }
        const float iceQuick = saturate((albedo.b - albedo.r * 1.04f) * 4.5f) * smoothstep(0.18f, 0.44f, albedo.b);
        const RawWeatheringResult wRes = evalSurfaceWeathering3D(
            p + camPosW.xyz, s.n, albedo, s.alpha,
            curvConvexW, curvConcaveW, z, base,
            wet.wet, wet.snow, iceQuick, waterProxW,
            phenology0, seasonThermal0, sootHeat01, (sunColour.w > 0.5f));
        albedo  = wRes.albedo;
        s.n     = wRes.normal;
        s.alpha = wRes.roughness;
        porosityWetW    = wRes.porosityWet01;
        hoarfrostGlintW = wRes.hoarfrostGlint01;
        weatherMossMask = wRes.mossCov01;
        sootHeatDbg     = max(sootHeat01, wRes.sootMask01);
        if (porosityWetW > 0.15f) {
            s.mask = max(s.mask, porosityWetW * 0.55f);
            lobe = true;
        }
    }
    // Wave 46 Rainstorm Vertical Wall Wet-Streak Sheen (100% inactive on dry days & interiors!):
    // Animated downward-streaming rain rivulets stretched vertically along +Z drifting down at simTime * 3.8
    // modulate vertical wall roughness (-0.24) and add subtle clearcoat water-rivulet glints.
    float rainWallStreak = 0.0f;
    if (!match && sunColour.w > 0.5f && weather.w > 0.20f && !isActorFeat && !isActorCloth && base != 5u && abs(s.n.z) < 0.45f && z < 2400.0f) {
        const float3 wpRain = p + camPosW.xyz;
        const float simTimeR = (physWorld[0].x > 1.0e-4f) ? physWorld[0].x : camPosW.w;
        const float rainAmtW = smoothstep(0.20f, 0.72f, weather.w) * (1.0f - saturate(wet.snow * 2.5f));
        const float vertGate = smoothstep(0.45f, 0.14f, abs(s.n.z)) * smoothstep(2400.0f, 180.0f, z);
        // Vertically stretched meandering wet channels + downward-drifting raindrop rivulet beads (+Z drift = falling):
        const float4 chanN = rawNoise3Grad(float3(wpRain.xy * 0.095f, wpRain.z * 0.016f + simTimeR * 0.12f));
        const float3 rivP  = float3(wpRain.xy * 0.135f + chanN.yz * 0.42f, wpRain.z * 0.022f + simTimeR * 3.8f * 0.18f);
        const float4 rivN  = rawNoise3Grad(rivP);
        const float4 beadN = rawNoise3Grad(float3(wpRain.xy * 0.22f + chanN.yz * 0.35f + 7.3f, wpRain.z * 0.055f + simTimeR * 3.8f * 0.38f));
        const float streakShape = smoothstep(0.48f, 0.84f, rivN.x * 0.65f + chanN.x * 0.35f);
        const float beadPulse   = smoothstep(0.55f, 0.88f, beadN.x);
        rainWallStreak = saturate((streakShape * 0.72f + streakShape * beadPulse * 0.48f) * rainAmtW * vertGate);
        if (rainWallStreak > 0.01f) {
            albedo  *= lerp(1.0f, 0.84f, rainWallStreak);
            s.alpha  = clamp(s.alpha - 0.24f * rainWallStreak, 0.06f, 0.96f);
            s.f0     = lerp(s.f0, 0.024f, rainWallStreak * 0.65f);
            s.mask   = max(s.mask, rainWallStreak * 0.72f);
            lobe     = true;
            const float3 rivGrad = rivN.yzw * 0.16f + beadN.yzw * 0.10f;
            const float3 rivTang = rivGrad - s.n * dot(rivGrad, s.n);
            s.n = normalize(s.n + rivTang * rainWallStreak);
        }
    }
    // True 3D Physical Relief Decals (blood pools, static frost-creep, charred scorches & weapon cracks) +
    // 100% Static, Non-Pulsing Crystalline Glacial Ice Optical Transport (includes base==2u EnvMap ice caves!):
    float iceMask = 0.0f;
    float3 decalEmit = 0.0f.xxx;
    if (!match) {
        if ((base == 1u || base == 2u || base == 4u) && z < 12000.0f) {
            const float blueIce = saturate((albedo.b - albedo.r * 1.04f) * 4.8f) * smoothstep(0.16f, 0.44f, albedo.b);
            const float caveIce = (sunColour.w < 0.5f && (base == 2u || base == 1u))
                ? smoothstep(0.24f, 0.52f, albedo.b) * saturate((albedo.b - albedo.r * 0.88f) * 4.0f) : 0.0f;
            iceMask = max(blueIce, caveIce) * (1.0f - wet.snow * 0.78f) * saturate(1.0f - z / 12000.0f);
        }
        // Uniform-flow 3D relief decal pass (quad-safe ddx/ddy for meniscus rims, frost veins, scorch fissures & impact cracks):
        const float prevDecalMask = s.mask;
        const RawDecalResult dec = evalDecals3D(albedo, s.n, s.v, p, z, s.alpha, s.f0, s.mask, base, iceMask, wet.snow);
        albedo    = dec.albedo;
        s.n       = dec.normal;
        s.alpha   = dec.alpha;
        s.f0      = dec.f0;
        s.mask    = dec.mask;
        decalEmit = dec.emissive;
        if (dec.mask > prevDecalMask + 0.05f) { lobe = true; }
        // Wave 41 Live 3D Dynamic Combat & Elemental Impact Decals (blood splatters, stone chips, cooling scorches,
        // frost ice patches & vitrified shock Lichtenberg burns streamed live from RAW_GetImpactDecals):
        if (!isActorFeat && z < 4500.0f) {
            const RawImpactDecalResult imp = evalDynamicImpactDecals3D(p + camPosW.xyz, s.n, impactDecalPosR, impactDecalMeta);
            const float actorGate = isActorCloth ? (imp.bloodMask * 0.55f) : 1.0f;
            const float impAlbW = saturate(imp.albedoBlend * actorGate);
            const float impRghW = saturate(imp.roughBlend  * actorGate);
            const float impF0W  = saturate(imp.f0Blend     * actorGate);
            if (impAlbW > 0.005f || impRghW > 0.005f || any(imp.emissive > 0.005f)) {
                albedo    = lerp(albedo, imp.albedo, impAlbW);
                s.alpha   = lerp(s.alpha, imp.roughness, impRghW);
                s.f0      = lerp(s.f0, imp.f0Override, impF0W);
                s.n       = normalize(s.n + imp.normalPerturb * actorGate);
                decalEmit += imp.emissive * (isActorCloth ? 0.0f : 1.0f);
                if (impF0W > 0.08f) {
                    s.mask = max(s.mask, impF0W * 0.82f);
                    lobe = true;
                }
            }
        }
        // True 3-Layer Static Parallax Snell-Refracted Crystalline Glacial Ice & Ice-Cave Optical Transport (IOR 1.31):
        // Delegates directly to RawMaterials3D.hlsli::evalFrostGlacier3D (Warren & Brandt 2008 Beer-Lambert absorption +
        // 3-layer Snell Voronoi cleavage fractures + crystal facet normals, 100% static / ZERO time-pulse modulation!).
        if (iceMask > 0.04f) {
            const float iceNear = smoothstep(2200.0f, 110.0f, z);
            const float3 wpAbsI = p + camPosW.xyz;
            const float3 iceLDir = (sunDir.w > 0.5f && sunColour.w > 0.5f) ? sunDir.xyz : normalize(float3(0.25f, 0.20f, 0.94f));
            const float3 iceLCol = (sunColour.w > 0.5f) ? sunColour.rgb : max(float3(ambient[0].w, ambient[1].w, ambient[2].w) * 1.4f, 0.08f.xxx);
            if (iceNear > 0.02f) {
                const RawMaterialEvalResult glac = evalFrostGlacier3D(
                    wpAbsI, s.n, s.v, iceLDir, iceLCol, albedo, lerp(14.0f, 44.0f, iceNear), saturate(wet.snow * 0.65f));
                // Preserve Skyrim's authored glacial ice wall relief and shading while layering static Beer-Lambert depth:
                s.n     = normalize(lerp(s.n, glac.normal, iceMask * iceNear * 0.45f));
                albedo  = lerp(albedo, glac.albedo, iceMask * (0.42f + 0.30f * iceNear));
                s.alpha = lerp(s.alpha, glac.roughness, iceMask * 0.85f);
            } else {
                // Distant glacial ice (z >= 2200u): cheap static Warren & Brandt sapphire-turquoise optical body.
                const float nvIce = saturate(dot(s.n, s.v));
                const float3 farGlac = float3(0.055f, 0.215f, 0.345f) * lerp(0.78f, 1.14f, nvIce);
                albedo  = lerp(albedo, farGlac, iceMask * 0.38f);
                s.alpha = lerp(s.alpha, 0.22f, iceMask * 0.80f);
            }
            // Wave 68: Physical PBR dielectric albedo highlight rolloff for bright glacial ice walls
            // (prevents un-SSAO cave ice albedo from exceeding 0.88 sRGB while preserving translucent cyan hue).
            const float iceMaxC = max(albedo.r, max(albedo.g, albedo.b));
            if (iceMaxC > 0.58f) {
                const float iceSoft = 0.58f + 0.18f * ((iceMaxC - 0.58f) / (iceMaxC - 0.58f + 0.30f));
                albedo *= lerp(1.0f, iceSoft / iceMaxC, saturate(iceMask * 1.25f));
            }
            s.f0 = lerp(s.f0, 0.018f, iceMask * 0.88f);
            s.mask = max(s.mask, iceMask * 0.76f);
            lobe = true;
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
    // Wave 63 + Wave 65 Cross-Slot Light-ID-Matched Depth+Normal Gaussian Bilateral RT Shadow Filter:
    // Matches each center top-3 light ID (rIdx.x/y/z) across ALL 3 neighbor slots (nIdx.x/y/z) AND adds
    // 4 outer cardinal radius-2 taps on sun shadow (rv.x) so multi-tap PCSS sun penumbras are silky-smooth
    // and rock-solid across both stationary frames and cam_sweep camera motion!
    if (rt) {
        rv = all(isfinite(rv)) ? saturate(rv) : 1.0f.xxxx;
        float4 pvSum = rv * 2.25f;
        float4 pvW = 2.25f.xxxx;
        const int2 dMaxS = int2(dims.xy) - 1;
        const float zTolS = max(z * 0.032f, 2.5f);
        [unroll] for (int sy = -1; sy <= 1; sy++) {
            [unroll] for (int sx = -1; sx <= 1; sx++) {
                if (sx == 0 && sy == 0) continue;
                const int3 spS = int3(clamp(ip.xy + int2(sx, sy), int2(0, 0), dMaxS), 0);
                const float dzS = abs(viewZOf(Depth.Load(spS)) - z);
                if (dzS < zTolS) {
                    const float gW = ((sx == 0 || sy == 0) ? 1.15f : 0.78f) * (1.0f - (dzS / zTolS) * 0.55f);
                    const float nDotS = dot(nSmooth, worldNormal(Normal.Load(spS)));
                    const float nwS = smoothstep(0.70f, 0.96f, nDotS) * gW;
                    // Wave 69: Relaxed bump-tolerant normal weight for exterior sunShadow (depth-gated!) so
                    // masonry brick crevices and foliage cards denoise cleanly without blurring across silhouettes.
                    const float sunW = smoothstep(0.30f, 0.88f, nDotS) * gW;
                    if (sunW <= 0.01f && nwS <= 0.01f) continue;
                    const float4 nRvRaw = RtVis.Load(spS);
                    const float4 nRv = all(isfinite(nRvRaw)) ? saturate(nRvRaw) : 1.0f.xxxx;
                    if (sunW > 0.01f) { pvSum.x += nRv.x * sunW; pvW.x += sunW; }
                    if (nwS <= 0.01f) continue;
                    const uint3 nIdx = uint3(RtIdx.Load(spS).xyz * 255.0f + 0.5f);
                    if (rIdx.x != 0u) {
                        if      (nIdx.x == rIdx.x) { pvSum.y += nRv.y * nwS; pvW.y += nwS; }
                        else if (nIdx.y == rIdx.x) { pvSum.y += nRv.z * nwS; pvW.y += nwS; }
                        else if (nIdx.z == rIdx.x) { pvSum.y += nRv.w * nwS; pvW.y += nwS; }
                    }
                    if (rIdx.y != 0u) {
                        if      (nIdx.y == rIdx.y) { pvSum.z += nRv.z * nwS; pvW.z += nwS; }
                        else if (nIdx.x == rIdx.y) { pvSum.z += nRv.y * nwS; pvW.z += nwS; }
                        else if (nIdx.z == rIdx.y) { pvSum.z += nRv.w * nwS; pvW.z += nwS; }
                    }
                    if (rIdx.z != 0u) {
                        if      (nIdx.z == rIdx.z) { pvSum.w += nRv.w * nwS; pvW.w += nwS; }
                        else if (nIdx.x == rIdx.z) { pvSum.w += nRv.y * nwS; pvW.w += nwS; }
                        else if (nIdx.y == rIdx.z) { pvSum.w += nRv.z * nwS; pvW.w += nwS; }
                    }
                }
            }
        }
        // 8 outer radius-2 ring taps strictly for exterior sun-shadow penumbra smoothness (depth-gated, zero light-slot overhead):
        if (sunColour.w > 0.5f) {
            static const int2 kSunR2[8] = {
                int2(2, 0), int2(-2, 0), int2(0, 2), int2(0, -2),
                int2(2, 2), int2(-2, 2), int2(2, -2), int2(-2, -2)
            };
            [unroll] for (int ri = 0; ri < 8; ++ri) {
                const int3 sp2 = int3(clamp(ip.xy + kSunR2[ri], int2(0, 0), dMaxS), 0);
                const float dz2 = abs(viewZOf(Depth.Load(sp2)) - z);
                if (dz2 < zTolS) {
                    const float baseW2 = (ri < 4) ? 0.56f : 0.38f;
                    const float nw2 = smoothstep(0.32f, 0.88f, dot(nSmooth, worldNormal(Normal.Load(sp2)))) * baseW2 * (1.0f - dz2 / zTolS);
                    if (nw2 > 0.01f) {
                        const float nSun2 = RtVis.Load(sp2).x;
                        if (isfinite(nSun2)) { pvSum.x += saturate(nSun2) * nw2; pvW.x += nw2; }
                    }
                }
            }
        }
        rv = saturate(pvSum / max(pvW, 1.0e-4f.xxxx));
    }
    // Every light past the three traced ones: the share of their light that reaches the pixel, from one ray a
    // pixel to a light picked by its share, filtered with the sky rays (RtShadow.hlsl, RtSkyFilter.hlsl).
    const float rawRestA = RtBounce.Load(ip).a;
    const float restVis = rt && (flags.x & FLAG_REST) != 0u ? (rawRestA > 1.5f ? saturate(floor(rawRestA * 0.5f) / 15.0f) : rawRestA) : 1.0f;
    float3 diff = 0.0f, spec = 0.0f;
    float sunShadow = 1.0f;
    // Wave 44 Unified Early Conductor / Brushed-Metal Classification (steel, Nordic iron, Dwemer bronze, silver, gold):
    // Evaluated once before sun + clustered point-light loops so both sunlight and torches/forges drive exact
    // Burley/Heitz anisotropic GGX highlights with strict energy conservation (kD = (1 - F) * (1 - metallic))!
    const float3 envReflEarly = (base == 2u && (flags.x & FLAG_ENV) != 0u) ? max(SoftMap.Load(ip).rgb, 0.0f.xxx) : 0.0f.xxx;
    const float envLumEarly = dot(envReflEarly, float3(0.2126f, 0.7152f, 0.0722f));
    const bool isEnbMetalEarly = (!match && base == 2u && iceMask < 0.08f && (envReflEarly.g > envReflEarly.r * 1.15f || envReflEarly.b > envReflEarly.r * 1.15f));
    const float base1Metal = (!match && base == 1u && !isActorCloth && !isBotanicalCard && iceMask < 0.08f && wet.wet < 0.20f && wet.puddle < 0.10f && wet.snow < 0.15f)
        ? smoothstep(0.58f, 0.88f, saturate(s.mask * resScale.w)) * smoothstep(0.45f, 0.22f, s.alpha) * 0.72f : 0.0f;
    const float rawMetallicEarly = isTruePbr ? saturate(rmaosPbr.g)
                                 : (isEnbMetalEarly ? saturate(envReflEarly.g)
                                 : ((!match && base == 2u) ? saturate((max(saturate(s.mask * resScale.w), envLumEarly * 1.35f) - 0.22f) * 1.95f) : base1Metal));
    const float earlyMetallic = rawMetallicEarly * (1.0f - iceMask);
    const bool isGoldBronzeAlloy = (albedo.r > albedo.b * 1.35f && albedo.g > albedo.b * 1.12f);
    const float albLumMetal = dot(albedo, float3(0.2126f, 0.7152f, 0.0722f));
    const float3 metalF0 = isGoldBronzeAlloy
        ? lerp(float3(0.84f, 0.62f, 0.28f), float3(1.00f, 0.78f, 0.34f), saturate(albLumMetal * 2.2f))
        : (albLumMetal > 0.38f ? float3(0.92f, 0.91f, 0.89f)
                               : lerp(float3(0.56f, 0.58f, 0.61f), saturate(albedo * 1.35f + 0.20f), 0.55f));
    const float metalAniso = isGoldBronzeAlloy ? 0.56f : (albLumMetal > 0.38f ? 0.66f : 0.48f);
    const bool useAnisoMetal = (!match && earlyMetallic > 0.22f && !isActorFeat && base != 5u && iceMask < 0.08f);
    // Wave 45 100%-Analytical Micro-Relief Crevice Self-Shadowing parameters (stone/terrain/cobbles/wood/bark < 2200u):
    const float mrDistFade = (!match && !isActorFeat && !isActorCloth && (base == 1u || base == 4u || base == 6u) && z < 2200.0f)
        ? smoothstep(2200.0f, 160.0f, z) * (1.0f - saturate(wet.snow * 0.72f)) * (1.0f - saturate(wet.puddle * 0.85f)) * (1.0f - saturate(iceMask * 0.85f))
        : 0.0f;
    const float mrStrength = mrDistFade * ((base == 4u || base == 6u) ? 0.85f : 0.72f);
    const float mrScale    = (base == 4u || base == 6u) ? 0.22f : 0.28f;
    // Wave 66: Quad-safe relative depth silhouette jump detector (attenuates contact shadows & AO darkening
    // across sharp depth silhouettes > 0.035 so characters, weapons & pine branches never form dark cel-shading rims!).
    const float zJumpSil = (abs(ddx(z)) + abs(ddy(z))) / max(z, 1.0f);
    const float silEdgeAtten = smoothstep(0.035f, 0.11f, zJumpSil);

    float cloudTransL = 1.0f;
    if (sunDir.w > 0.5f) {
        // Interiors (sunColour.w < 0.5): NEVER read m4.x (that is vanilla's flickering point-light shadow channel 0!).
        // Keep interior directional fill unshadowed and calm so turning the camera never flickers the whole room!
        sunShadow = (sunColour.w < 0.5f || (flags.x & FLAG_SHADOWS) == 0u) ? 1.0f
                  : ((flags.x & FLAG_CASCADES) != 0u ? cascadeShadow(p, d, true) : m4.x);
        // Pure Native Hardware DXR 1.2 Sun Shadows when RT is active (zero screen-space contact shadow hacks!).
        // screenShadow runs ONLY as a fallback when hardware RT is explicitly disabled (attenuated across silhouettes!).
        if (!match && !rt && (flags.x & FLAG_CONTACT) != 0u && base != 5u && !isActorFeat && !isActorCloth && sunShadow > 0.0f && dot(s.n, sunDir.xyz) > 0.0f) {
            const float ssSun = screenShadow(p + lift, sunDir.xyz, tune.w, 0.02f, jit, 12u);
            sunShadow *= lerp(ssSun, 1.0f, silEdgeAtten);
        }
        // Pure Native Hardware DXR 1.2 Sun Shadows on exterior world geometry (and 72% hardware DXR blend on skinned actors!):
        if (rt && sunColour.w > 0.5f) {
            sunShadow = (!isActorFeat && !isActorCloth) ? rv.x : lerp(m4.x, rv.x, 0.72f);
        }
        if (!match && sunColour.w > 0.5f && (flags.x & FLAG_CLOUDS) != 0u) {
            cloudTransL = pow(saturate(cloudShadow(p + camPosW.xyz)), 1.25f);
            sunShadow *= cloudTransL;
        }   // SKY-02 + Wave 74 unified volumetric cloud-shadow transmittance
        // Wave 29 Chromatic Shadow-Boundary Penumbra Splitting (neighbor-reactive quad derivative of sunShadow):
        // Uses hardware ddx/ddy on sunShadow (uniform sunDir.w branch) to detect the active penumbra boundary:
        // outer sunlit edge warms via solar limb darkening (~4200K) while inner shadow edge shifts cobalt Rayleigh (~12000K)!
        // Strictly attenuated under heavy overcast and disabled in interiors.
        const float penumbraBand = saturate(sunShadow * (1.0f - sunShadow) * 4.0f);
        const float shGrad = saturate((abs(ddx(sunShadow)) + abs(ddy(sunShadow))) * 5.5f);
        const float clearPenW = (!match && sunColour.w > 0.5f) ? saturate(1.0f - clouds.x * 1.15f) : 0.0f;
        const float outerLimb = smoothstep(0.45f, 0.88f, sunShadow) * max(penumbraBand, shGrad * 0.65f) * clearPenW;
        const float innerRayleigh = smoothstep(0.55f, 0.08f, sunShadow) * max(penumbraBand, shGrad * 0.65f) * clearPenW;
        const float3 spectralPenShift = float3(0.055f, 0.022f, -0.028f) * outerLimb
                                      + float3(-0.022f, 0.008f, 0.048f) * innerRayleigh;
        const float3 sunSh = skin && !match
            ? pow(saturate(sunShadow).xxx, float3(0.88f, 0.96f, 1.02f))
            : max(sunShadow.xxx + float3(0.022f, 0.012f, -0.008f) * penumbraBand + spectralPenShift, 0.0f.xxx);
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
        // True HDR Sun vs Shadow key-to-fill contrast (crisp sunlit highlights + deep dimensional shadows):
        const float sunHdrGain = (!match && sunColour.w > 0.5f) ? lerp(1.85f, 1.15f, saturate(clouds.x * 1.15f)) * (1.0f - nightDir * 0.52f) : 1.0f;
        // In interiors, keep flat directional vault fill minimal (0.12x) so real hearths, torches & candles sculpt the room!
        const float3 dirCol = (sunColour.w < 0.5f && !match) ? (sunColour.rgb * 0.12f)
                            : lerp(alignedDayCol * sunHdrGain, moonDirectCol, nightDir);
        // Chan 2018 micro-shadowing + Wave 45 100%-analytical directional micro-relief crevice self-shadowing:
        const float microSh = (!match && !isActorFeat && base != 5u) ? saturate(2.0f * AoHist.Load(ip).x + saturate(dot(s.n, sunDir.xyz)) - 0.85f) : 1.0f;
        const float sunMicroRelief = (mrStrength > 0.01f && sunShadow > 0.02f)
            ? evalMicroReliefSelfShadow(p + camPosW.xyz, s.n, sunDir.xyz, mrStrength, mrScale) : 1.0f;
        const float sunPunch = (!match && sunColour.w > 0.5f) ? lerp(1.24f, 0.85f, nightDir) : 1.0f;
        addLight(s, sunDir.xyz, dirCol * (sunSh * lerp(0.50f, 1.0f, microSh) * sunMicroRelief * sunPunch), dirCol * sunPunch, diff, spec);
        // Wave 77: Energy-Conserving World-Space Micro-Flake Snow & Hoar-Frost Glints (IOR 1.31):
        // Anchored strictly to fixed dyadic 3D world-space grids (0.0 screen-UV drift on camera pan,
        // algebraic white-furnace upper bound <= 1.0, 0 NaN/Inf; replaces unbounded stacked sparkle lobes).
        if (!match && sunColour.w > 0.5f && z < 2000.0f && sunShadow > 0.12f && nightDir < 0.75f &&
            (wet.snow > 0.12f || ((base == 4u || base == 1u) && min(albedo.r, min(albedo.g, albedo.b)) > 0.26f && s.n.z > 0.48f))) {
            const float snowCovGl = max(wet.snow, ((base == 4u || base == 1u) && min(albedo.r, min(albedo.g, albedo.b)) > 0.26f) ? 0.65f : 0.0f);
            const float maxSnowAlb = max(albedo.r, max(albedo.g, albedo.b));
            const RawSnowGlintResult glW77 = rawEvalMicroFlakeSnowGlint(
                p + camPosW.xyz, s.n, s.v, sunDir.xyz, z, snowCovGl, maxSnowAlb);
            spec += dirCol * (glW77.glintSpec * sunShadow * (1.0f - nightDir));
        }
        // Foliage (base == 5u) single unified energy-conserving backlit transmission runs once at line ~1491
        // (removed duplicate stacked diff/spec glow lobes that quadruple-counted chlorophyll and made trees glow!).
        // Wave 40 Physical Marschner 3-Lobe Anisotropic Hair Shading (R cuticle + TT backlit transmission + TRT pigment):
        // Replaces plastic isotropic GGX highlights on NPC hair with real shifted strand-tangent R/TT/TRT highlights!
        if (!match && base == 11u) {
            spec = 0.0f.xxx;
            const float3 gDownH = float3(0.0f, 0.0f, -1.0f);
            float3 tStrand = gDownH - s.n * dot(gDownH, s.n);
            tStrand = (dot(tStrand, tStrand) > 1.0e-4f) ? normalize(tStrand) : normalize(cross(s.n, float3(1.0f, 0.0f, 0.0f)));
            const float strandShift = (rawNoise3Grad((p + camPosW.xyz) * 0.85f).x - 0.5f) * 0.14f;
            const float3 tFlow = normalize(tStrand + s.n * strandShift);
            const float3 hair3Lobe = evalMarschnerHair3Lobe(tFlow, s.n, s.v, sunDir.xyz, albedo, clamp(s.alpha, 0.24f, 0.52f));
            spec += dirCol * hair3Lobe * lerp(0.18f, 1.0f, sunShadow);
        }
        if (skin && !match) {
            // Dual-lobe character skin shading (Callisto Protocol / SKN-09 + Wave 63 smooth Gaussian terminator):
            // C-infinity Gaussian terminator SSS eliminates sharp blotchy cheek/nose cusp bands on NPC faces!
            const float nlSk = dot(s.n, sunDir.xyz);
            const float termBand = exp(-nlSk * nlSk * 4.5f) * (0.28f + 0.72f * sunShadow);
            diff += dirCol * float3(0.18f, 0.078f, 0.046f) * termBand;
            const float3 hSkin = rawSafeHalfVec(s.v, sunDir.xyz, s.n);
            const float nhSk = saturate(dot(s.n, hSkin));
            const float ddSk = max(nhSk * nhSk * (0.0484f - 1.0f) + 1.0f, 1.0e-4f);
            const float dSk = 0.0484f / (PI * ddSk * ddSk);
            const float fSk = 0.028f + 0.972f * pow(1.0f - saturate(dot(s.v, hSkin)), 5.0f);
            spec += dirCol * (min(dSk * fSk * 0.16f, 1.3f) * sunShadow * max(nlSk, 0.0f));
        }
        // Physical Character Eye Cornea Wetness + Iris Caustic Catchlight (base == 12u, SKN-09):
        if (base == 12u && !match) {
            const float3 hEye = rawSafeHalfVec(s.v, sunDir.xyz, s.n);
            const float nhE = saturate(dot(s.n, hEye));
            const float ddE = max(nhE * nhE * (0.0012f - 1.0f) + 1.0f, 1.0e-4f);
            const float cornea = min(0.0012f / (PI * ddE * ddE), 18.0f) * (0.04f + 0.96f * pow(1.0f - saturate(dot(s.v, hEye)), 5.0f));
            spec += dirCol * (cornea * 0.28f * (0.35f + 0.65f * sunShadow));
            diff += dirCol * float3(0.14f, 0.08f, 0.07f) * (0.25f * saturate(dot(s.n, sunDir.xyz) + 0.4f));
        }
        // Wave 77 unified micro-flake snow glint already evaluated above with strict white-furnace conservation <= 1.0
        // (eliminates duplicate unbounded snowGlint stacking and camera-distance quantization cell swimming).
        // Wave 44 Physical Brushed-Metal Anisotropic GGX + Complex-IOR Conductor Fresnel (forged steel, iron, Dwemer bronze, silver, gold):
        // Replaces flat circular highlights on armor/weapons with elongated anisotropic brushed-steel curves!
        if (useAnisoMetal && sunShadow > 0.02f && z < 2600.0f) {
            const float3 anisoSun = evalBrushedMetalAniso(p + camPosW.xyz, s.n, s.v, sunDir.xyz, metalF0, s.alpha, metalAniso);
            const float3 sunLightEff = dirCol * (sunSh * lerp(0.50f, 1.0f, microSh) * sunPunch);
            spec = lerp(spec, min(sunLightEff * anisoSun, 4.5f.xxx), earlyMetallic * smoothstep(2600.0f, 180.0f, z));
            if (z < 1600.0f && base == 2u) {
                const float mGlint = snowGlint(p + camPosW.xyz, s.n, s.v, sunDir.xyz, max(0.8f, z * 0.0018f));
                spec += sunColour.rgb * (sunShadow * mGlint * 0.42f * earlyMetallic * saturate(1.0f - z / 1600.0f));
            }
        }
        // Wave 42 Dual-Layer Snell-Refracted RGB Underwater Caustics (strictly gated on truly submerged exterior surfaces):
        // Refracts sunDir through water (IOR 1.333) and evaluates aperiodic curl-convergence RGB filaments attenuated by
        // Beer-Lambert water depth & cloud shadow (ZERO Voronoi grids, ZERO caustics on dry land above liveWaterZ!).
        if (!match && sunColour.w > 0.5f && !isActorFeat && z < 4800.0f && sunDir.z > 0.05f && sunShadow > 0.02f) {
            const bool liveWaterZValid = (engineWorld.z > 0.5f && engineWorld.x > -48000.0f && engineWorld.x < 48000.0f);
            if (liveWaterZValid) {
                const float liveWaterZ = rawLiveWaterZ(engineWorld, physWorld[0]);
                const float3 absPosC = p + camPosW.xyz;
                const float subDepthU = liveWaterZ - absPosC.z;
                if (subDepthU > 1.5f && subDepthU < 900.0f) {
                    const float3 srC = refract(-sunDir.xyz, float3(0.0f, 0.0f, 1.0f), 1.0f / 1.333f);
                    const float2 surfXY = absPosC.xy - srC.xy * (subDepthU / max(-srC.z, 0.22f));
                    const float wtCaust = (physWorld[0].x > 0.001f) ? physWorld[0].x : camPosW.w;
                    const float2 dispDir = (length(srC.xy) > 1.0e-3f) ? normalize(srC.xy) : float2(0.8f, 0.6f);
                    const float dispU = clamp(subDepthU * 0.032f, 0.25f, 2.2f);
                    const float3 causRGB = float3(
                        evalCurlCausticLayer(surfXY + dispDir * dispU, wtCaust),
                        evalCurlCausticLayer(surfXY, wtCaust),
                        evalCurlCausticLayer(surfXY - dispDir * dispU, wtCaust));
                    const float3 beerDepth = exp(-subDepthU * float3(0.0085f, 0.0028f, 0.0018f));
                    const float focusW = smoothstep(1.5f, 18.0f, subDepthU);
                    const float upW = saturate(s.n.z * 0.75f + 0.25f);
                    const float clearSunC = max(1.0f - saturate(clouds.x * 0.78f), 0.18f);
                    const float distFadeC = smoothstep(4800.0f, 320.0f, z);
                    diff += dirCol * causRGB * (beerDepth * (focusW * upW * clearSunC * sunShadow * distFadeC * 0.78f));
                }
            }
        }
    }

    const uint4 bits = clusters[clusterOf(px, z)];
    uint softLeft = 4u;
    bool mrKeyUsed = false;
    float fireMelt = 0.0f;
    float localWarmIrradDbg = 0.0f;
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
            // Physical focused point-light falloff (eliminates 3.8x triple-stacked interior fire blow-out so hearths,
            // torches & candles cast rich, warm, focused pools with deep cozy shadows instead of flat orange wash!):
            const float win = saturate(1.0f - rNorm * rNorm);
            const float att = (win * win) / (1.0f + rNorm * rNorm * 0.85f);
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
            // Rich natural 2400K-2700K warm firelight (hearths, torches, candles, braziers):
            // Soft peak clamp (1.45) prevents close-range torches from blowing out walls, players & roads into neon yellow!
            if (!match && lightColS[li].r > lightColS[li].b * 1.25f) {
                const bool isIntLight = (sunColour.w < 0.5f);
                const float3 fireBoost = skin ? float3(1.00f, 0.90f, 0.80f)
                                              : (isIntLight ? float3(1.16f, 0.86f, 0.56f) : float3(1.12f, 0.84f, 0.54f));
                c *= fireBoost;
            } else if (!match && sunColour.w < 0.5f) {
                c *= 1.08f;
            }
            if (!match) {
                // Smooth physiological firelight knee on skin (0.85) preserves facial pores & form right beside hearths/torches!
                const float cPeak = max(c.r, max(c.g, c.b));
                const float cKnee = skin ? 0.85f : 1.35f;
                if (cPeak > cKnee) { c *= (cKnee + (cPeak - cKnee) / (1.0f + (cPeak - cKnee) * (skin ? 1.55f : 1.25f))) / cPeak; }
            }
            const uint slot = li + 1u == rIdx.x ? 1u : li + 1u == rIdx.y ? 2u : li + 1u == rIdx.z ? 3u : 0u;
            // Wave 63 Flicker-Free RT Point-Light Shadow Handoff:
            // Harmonizes top-3 slot floor (0.22) with ReSTIR secondary floor (0.28) and crossfades smoothly near
            // outer attenuation radius so turning the camera never causes a room-wide brightness pop on slot swap!
            // Lifts actor BLAS self-shadow floor cleanly (0.68 face/skin, 0.50 skinned fur/cloth) to prevent blocky blotches.
            if (rt) {
                const float slotVisVal = slot != 0u ? lerp(0.22f, 1.0f, rv[slot]) : lerp(0.28f, 1.0f, restVis);
                const float restVisVal = lerp(0.28f, 1.0f, restVis);
                const float slotWeight = slot != 0u ? smoothstep(0.015f, 0.14f, att) : 0.0f;
                const float rtSh = lerp(restVisVal, slotVisVal, slotWeight);
                sh = isActorFeat ? lerp(0.68f, 1.0f, rtSh) : (isActorCloth ? lerp(0.50f, 1.0f, rtSh) : rtSh);
            }
            else if (slot != 0u) { sh = rv[slot]; }
            if (lightColS[li].r > lightColS[li].b * 1.18f) {
                localWarmIrradDbg += dot(c * sh, float3(0.2126f, 0.7152f, 0.0722f)) * saturate(dot(s.n, l) * 0.5f + 0.5f);
            }
            if (!match && !rt && sunColour.w > 0.5f && slot == 0u && (flags.x & FLAG_SOFT) != 0u && base != 5u && softLeft > 0u && dot(s.n, l) > 0.0f && max(c.r, max(c.g, c.b)) > 0.02f) {
                const float ssPt = screenShadow(p + lift, l, min(dist, tune.z), 4.0f / max(dist, 1.0f), jit, 10u);
                sh *= lerp(ssPt, 1.0f, silEdgeAtten);
                --softLeft;
            }
            // Wave 45 analytical micro-relief crevice self-shadowing on the nearest/strongest key point light (torches/hearths):
            if (mrStrength > 0.01f && !mrKeyUsed && (slot == 1u || max(c.r, max(c.g, c.b)) > 0.06f) && dot(s.n, l) > 0.01f) {
                sh *= evalMicroReliefSelfShadow(p + camPosW.xyz, s.n, l, mrStrength, mrScale);
                mrKeyUsed = true;
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
                if (isHair) {
                    const float3 gDownP = float3(0.0f, 0.0f, -1.0f);
                    float3 tStrP = gDownP - s.n * dot(gDownP, s.n);
                    tStrP = (dot(tStrP, tStrP) > 1.0e-4f) ? normalize(tStrP) : normalize(cross(s.n, float3(1.0f, 0.0f, 0.0f)));
                    spec += c * evalMarschnerHair3Lobe(tStrP, s.n, s.v, l, albedo, clamp(s.alpha, 0.24f, 0.52f)) * lerp(0.22f, 1.0f, sh) * 0.65f;
                } else if (useAnisoMetal && z < 2600.0f) {
                    // Torches, forges & hearths cast elongated brushed-steel anisotropic GGX reflections on armor & weapons!
                    const float3 anisoPt = evalBrushedMetalAniso(p + camPosW.xyz, s.n, s.v, l, metalF0, sArea.alpha, metalAniso);
                    spec += lerp(specTmp * (normE * normE), min(c * sh * anisoPt * (normE * normE), 3.6f.xxx), earlyMetallic);
                } else {
                    spec += specTmp * (normE * normE);
                }
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
    // Sync exterior hemisphere ambient directly with evalWorldAtmosphere() sky zenith/horizon + ground bounce,
    // and upgrade interior lighting across all location types (Tavern/Home warm hearth contrast, Nordic Crypt/Cave
    // cool mineral-slate vault depth, Dwemer Ruin burnished bronze-gold directional bounce):
    float3 barkGroundBounce = float3(0.085f, 0.072f, 0.054f);
    if (!match) {
        if (sunColour.w > 0.5f) {
            float3 uSunA, uSkyZA, uSkyHA, uAmbUpA, uAmbDnA;
            evalWorldAtmosphere(uSunA, uSkyZA, uSkyHA, uAmbUpA, uAmbDnA);
            barkGroundBounce = max(uAmbDnA, float3(0.085f, 0.072f, 0.054f));
            const float3 skyHemi = lerp(uAmbDnA, uAmbUpA, saturate(s.n.z * 0.5f + 0.5f));
            amb = lerp(amb, skyHemi, 0.48f);
        } else {
            const float ambWarm = saturate((amb.r - amb.b) * 3.5f);
            const float ambCool = saturate((amb.b - amb.r) * 3.5f);
            // Architectural vault shaping: deep dark ceiling corners & recessed wood/stone (0.24..0.44x) so hearths,
            // candles & windows carve rich chiaroscuro contrast instead of flat 2011 beige-grey interiors!
            const float3 intTint = lerp(float3(0.86f, 0.92f, 1.04f), float3(1.06f, 0.92f, 0.78f), ambWarm);
            const float3 cryptTint = lerp(intTint, float3(0.78f, 0.88f, 1.08f), ambCool);
            amb *= cryptTint * lerp(0.24f, 0.44f, saturate(s.n.z * 0.5f + 0.5f));
        }
        // Wave 42 Physical Underwater Ambient & Direct Beer-Lambert Extinction (kUnderwater = FLAG_UNDERWATER):
        // Applies calibrated red-first spectral water absorption when submerged so rivers & Sea of Ghosts feel genuinely aqueous!
        if ((flags.x & FLAG_UNDERWATER) != 0u) {
            const float liveWZU = (engineWorld.z > 0.5f && engineWorld.x > -48000.0f && engineWorld.x < 48000.0f)
                ? rawLiveWaterZ(engineWorld, physWorld[0]) : (camPosW.z + 60.0f);
            const float subColU = max(liveWZU - (p.z + camPosW.z), 0.0f);
            const float3 beerDepth = exp(-min(subColU, 1400.0f) * float3(0.0028f, 0.00075f, 0.00042f));
            const float3 beerView  = exp(-min(z, 3000.0f) * float3(0.0034f, 0.00088f, 0.00050f));
            const float3 uwAbsorb  = max(beerDepth * beerView, float3(0.035f, 0.14f, 0.22f));
            amb  *= uwAbsorb;
            diff *= max(beerDepth, float3(0.08f, 0.26f, 0.36f));
        }
    }
    // 100% Pure Hardware DXR 3D Contact RTAO & Skylighting (ZERO screen-space GTAO/SSAO when RT is active!):
    // Wave 66: Attenuate screen-space AO and micro-contact darkening smoothly across sharp depth silhouette jumps
    // (silEdgeAtten from zJumpSil > 0.035) so moving characters, weapons & pine branches never form dark cel rims!
    const float ssaoMix = (rt && (flags.x & FLAG_SKY) != 0u) ? 0.0f : 1.0f;
    const float aoWeight = (base == 5u ? 0.82f : (isActorFeat ? 0.30f : (isActorCloth ? 0.45f : 1.0f))) * ssaoMix * (1.0f - silEdgeAtten * 0.80f);
    const float aoRaw = (flags.x & FLAG_AO) != 0u ? lerp(1.0f, AoHist.Load(ip).x, tune2.x * aoWeight) : 1.0f;
    const float aoTerm = !match ? lerp(pow(saturate(aoRaw), 1.25f), 1.0f, silEdgeAtten * 0.72f) : aoRaw;
    // Exteriors: ray-traced sky visibility (RtSky.cpp). Interiors (!match): local SM6.9 RayQuery RTAO from RtShadow.hlsl.
    const bool isExterior = sunColour.w > 0.5f;
    const float rawSkyVis = rt && (flags.x & FLAG_SKY) != 0u && (isExterior || !match) ? RtIdx.Load(ip).w : 1.0f;
    const float skyVis = isActorFeat ? lerp(0.28f, 1.0f, rawSkyVis)
                       : (isActorCloth ? lerp(0.26f, 1.0f, rawSkyVis)
                       : (isWoodBark ? lerp(0.44f, 1.0f, rawSkyVis)
                       : (isBotanicalCard ? lerp(0.24f, 1.0f, rawSkyVis) : rawSkyVis)));
    // Physical directional skylighting falloff: roofs, covered walkways, arches & dense foliage cores drop to deep rich shade
    // while vertical pine tree trunks (isWoodBark) enforce a realistic >= 0.36 skyVisShaped floor so ZERO bark pixels crush!
    const float skyVisPow = !match ? pow(saturate(skyVis), isBotanicalCard ? 1.50f : 1.85f) : skyVis;
    const float skyVisShaped = (!match && isWoodBark) ? max(skyVisPow, 0.36f) : skyVisPow;
    const float rtOccRaw = isExterior ? lerp(1.0f, lerp(0.10f, 1.0f, skyVisShaped), max(cluster.w, 0.88f)) : lerp(0.12f, 1.0f, skyVisShaped);
    const float rtOcc = (!match && isWoodBark) ? max(rtOccRaw, 0.40f) : rtOccRaw;
    // Smooth physical irradiance soft-shoulder on close-range point lights (prevents bright plaster walls like
    // Farengar's study 15.00.26 and snow floes under torches from clipping into flat white!):
    if (!match) {
        const float dPeak = max(diff.r, max(diff.g, diff.b));
        if (dPeak > 0.95f) {
            const float dSoft = 0.95f + (dPeak - 0.95f) / (1.0f + (dPeak - 0.95f) * 0.42f);
            diff *= (dSoft / dPeak);
        }
    }
    // Feature 18: Contact-hardening near-field micro-AO grounding under foliage, rocks, furniture & NPC feet.
    // Wave 63 + Wave 65 + Wave 66: Strictly exclude faces/skin/hair/eyes (isActorFeat), keep skinned cloth/fur (0.90)
    // and tree trunks (0.94) gentle, and fade out across depth silhouette edges (silEdgeAtten)!
    const float nearGroundAo = !match ? pow(saturate(aoTerm), lerp(1.0f, 1.45f, saturate(1.0f - z / 900.0f))) : aoTerm;
    if (!match && !isActorFeat && z < 650.0f) {
        const float microFloor = isWoodBark ? 0.94f : (isActorCloth ? 0.90f : 0.70f);
        const float microDiffOcc = lerp(lerp(1.0f, lerp(microFloor, 1.0f, nearGroundAo), saturate(1.0f - z / 650.0f)), 1.0f, silEdgeAtten);
        diff *= microDiffOcc;
    }
    float combinedOcc = min(nearGroundAo, rtOcc);
    // Lifelike Vanilla NPCs, Forest Trunks & Moving Silhouettes: prevent dark 1-2px cel-shading rims on depth silhouettes!
    if (!match) {
        if (base == 12u)       combinedOcc = lerp(combinedOcc, 1.0f, 0.82f);
        else if (skin)         combinedOcc = lerp(combinedOcc, 1.0f, 0.14f);
        else if (isActorCloth) combinedOcc = lerp(combinedOcc, 1.0f, 0.10f);
        else if (isWoodBark)   combinedOcc = max(combinedOcc, 0.38f);
        combinedOcc = lerp(combinedOcc, max(combinedOcc, 0.72f), silEdgeAtten * 0.80f);
    }
    // SIGGRAPH GI-12 Visibility-Aware Bent-Normal Hemisphere Skylighting (exteriors) &
    // Directional Vault/Hearth Ambient Shaping (interiors: deepens recessed corners & highlights upper masonry/beams).
    const float3 bentN = normalize(lerp(s.n, float3(0.0f, 0.0f, 1.0f), (1.0f - combinedOcc) * 0.38f * skyVis));
    const float hemiSky = !match ? (isExterior ? lerp(0.62f, 1.18f, saturate(bentN.z * 0.5f + 0.5f))
                                               : lerp(0.54f, 1.20f, saturate(bentN.z * 0.5f + 0.5f))) : 1.0f;
    // Keep multi-bounce AO subtle so deep crevices stay genuinely grounded instead of lifting back to muddy grey.
    const float3 mbOcc = lerp(combinedOcc.xxx, gtaoMultiBounce(combinedOcc, saturate(albedo)), 0.38f);
    // Wave 65: Rich sunlight-to-shadow dynamic range with >= 0.64 overcast/rain ambient floor so foreground boulders
    // and foliage keep crisp readable micro-detail during Riverwood rainstorms!
    const float overcastStormW = saturate(max(clouds.x * 0.90f, weather.w * 0.95f));
    const float ambHdrScale = (!match && isExterior) ? lerp(0.56f, 0.78f, saturate(overcastStormW * 0.85f + 0.25f * smoothstep(0.25f, 0.70f, overcastStormW))) : 1.0f;
    amb *= match ? combinedOcc.xxx : mbOcc * hemiSky * ambHdrScale;
    // Wave 64 + Wave 65 Forest Ground-Bounce Fill & Deep-Furrow Floor on Vertical Tree Trunks (isWoodBark):
    // Eliminates every remaining crushed-black bark red speckle (luma < 0.015) on shadowed Riverwood pine trunks!
    if (!match && isExterior && isWoodBark) {
        albedo = max(albedo, float3(0.046f, 0.037f, 0.028f));
        const float barkVertW = saturate(1.0f - abs(s.n.z));
        const float dayBarkW = smoothstep(-0.06f, 0.14f, astroSunElev());
        const float3 forestBounce = max(barkGroundBounce, float3(0.11f, 0.095f, 0.072f)) * float3(1.18f, 1.02f, 0.80f);
        amb += forestBounce * (barkVertW * lerp(0.36f, 0.56f, dayBarkW) * lerp(0.75f, 1.0f, sunShadow));
    }
    // Multi-bounce hardware RayQuery GI: physically energy-conserving gain + smooth Reinhard shoulder + contact-AO grounding
    // (eliminates runaway GI glow on walls, snow, ice, and interiors while keeping rich natural color bleed!).
    const bool bounceHere = sunColour.w > 0.5f || (flags.x & FLAG_BOUNCE_IN) != 0u;
    float3 bounce = 0.0f.xxx;
    if (rt && (flags.x & FLAG_BOUNCE) != 0u && bounceHere) {
        const float3 rawB = max(RtBounce.Load(ip).rgb * proj.w, 0.0f.xxx);
        if (match) {
            bounce = rawB;
        } else {
            const float bGain = isExterior ? (skin ? 0.88f : 1.08f) : (skin ? 0.80f : 0.98f);
            const float overhangLift = 1.0f + saturate(-s.n.z) * 0.16f;
            const float contactMod = lerp(0.55f, 1.0f, nearGroundAo);
            const float3 bScaled = rawB * (bGain * overhangLift * contactMod);
            const float bLum = dot(bScaled, float3(0.2126f, 0.7152f, 0.0722f));
            // Natural indirect chroma bleed (1.10x) with smooth energy-conserving Reinhard shoulder (knee 0.52, cap 0.85):
            bounce = max(lerp(bLum.xxx, bScaled, 1.10f), 0.0f.xxx);
            const float bPeak = max(bounce.r, max(bounce.g, bounce.b));
            if (bPeak > 0.52f) {
                const float bSoft = 0.52f + (bPeak - 0.52f) / (1.0f + (bPeak - 0.52f) * 1.65f);
                bounce *= min(bSoft, 0.85f) / bPeak;
            }
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
            // Weight blue sky-ambient tint by skyVis, and cap tightly on skin (0.08x) so shaded human faces
            // never turn corpse blue-purple (fixes Hilde 23.50.29.60.png)!
            const float skinSkyCap = skin ? 0.08f : 1.0f;
            amb = lerp(amb, skyHemi * (ambL / hemiL), dayAmbW * skyVis * lerp(0.20f, 0.34f, inShadow) * skinSkyCap);
        }
    } else if (!match && !isExterior) {
        // Exterior Time-of-Day Interior Lighting Coupling (driven by live gameHour / sunElev in timeEnv.xy):
        // Disciplined interior ambient floor on skin (0.76x) so warm hearth/candle directional light sculpts faces!
        const float sunE = timeEnv.y;
        const float dayInt = smoothstep(-0.14f, 0.28f, sunE);
        const float goldInt = smoothstep(-0.14f, 0.10f, sunE) * (1.0f - smoothstep(0.18f, 0.52f, sunE));
        const float outCool = saturate(clouds.x * 0.6f + weather.w * 0.5f);
        const float3 todIntTint = lerp(float3(0.76f, 0.86f, 1.14f),
                                       lerp(float3(1.04f, 1.05f, 1.08f), float3(1.18f, 0.92f, 0.72f), goldInt), dayInt);
        const float todIntGain = lerp(0.78f, 1.08f, dayInt) * (skin ? 0.76f : 1.0f);
        amb *= lerp(todIntTint, float3(0.88f, 0.95f, 1.06f), outCool * 0.35f) * todIntGain;
        // Wave 70: Physical multi-bounce room-ambient floor in lit interiors (applied AFTER todIntGain)
        // so shadowed tavern alcoves preserve readable wood/stone detail instead of crushing to #000000.
        amb = max(amb, float3(0.042f, 0.036f, 0.030f) * lerp(0.72f, 1.0f, combinedOcc));
    }
    // Wave 29 Reciprocal Cross-Material Contact Empathy + Environmental Asymmetry Breaking:
    // 1) 4-tap neighbor G-buffer inspection (strictly gated by 3D world distance so distant mountains never bleed):
    //    - Skin bordering Hair/Cloth (<8u): warm 620..670nm hemoglobin subsurface seam replacing dead grey SSAO.
    //    - Stone/Terrain/Wood bordering Water (<28u above water plane or neighbor base==9): capillary wet meniscus (VR-1).
    //    - Opaque edges bordering Emissive flames/spells (<28u): directional warm spectral rim scatter.
    // 2) Environmental asymmetry breaking on natural rock/terrain (base==1||4): windward frost/rime vs leeward bare stone,
    //    plus north/shaded-side damp moss growth breaking 2011 UV tiling regularity!
    if (!match && z < 3200.0f && !isEye && base != 3u) {
        const int2 dMaxC = int2(dims.xy) - 1;
        static const int2 kC4[4] = { int2(2, 0), int2(-2, 0), int2(0, 2), int2(0, -2) };
        float skinContact = 0.0f;
        float waterContact = 0.0f;
        float3 emissiveRim = 0.0f.xxx;
        [unroll] for (int ci = 0; ci < 4; ci++) {
            const int2 qC = clamp(ip.xy + kC4[ci], int2(0, 0), dMaxC);
            const float dQ = Depth.Load(int3(qC, 0));
            if (dQ >= 1.0f) { continue; }
            const float3 pQ = worldAt(float2(qC), dQ);
            const float dist3 = length(pQ - p);
            if (dist3 > 32.0f) { continue; }
            const uint bQ = uint(Spec.Load(int3(qC, 0)).z * 255.0f + 0.5f) & 127u;
            if (skin && (bQ == 11u || bQ == 1u || bQ == 2u) && dist3 < 8.5f) {
                skinContact = max(skinContact, saturate(1.0f - dist3 / 8.5f));
            }
            if ((base == 1u || base == 4u) && bQ == 9u && dist3 < 28.0f) {
                waterContact = max(waterContact, saturate(1.0f - dist3 / 28.0f));
            }
            if (bQ == 3u && dist3 < 28.0f) {
                const float3 eCol = max(GameLit.Load(int3(qC, 0)).rgb, 0.0f.xxx);
                const float eW = saturate(1.0f - dist3 / 28.0f) * 0.22f;
                emissiveRim += min(eCol, 2.5f.xxx) * eW;
            }
        }
        // Wave 34/35 Live Tidal Capillary Shoreline Meniscus + GTA VI (@OtarieBambelle) Above-Surface Reflected Water Caustics:
        // At spring high tide the dark glistening wet shoreline climbs higher up coastal rocks & piers;
        // downward/side-facing bridge arches, ship hulls, piers & rock overhangs within 0..260u above sunlit water
        // receive shimmering reflected water caustics dancing upward from the moving surface!
        if ((base == 1u || base == 4u) && engineWorld.z > 0.5f) {
            const float liveTideWaterZ = rawLiveWaterZ(engineWorld, physWorld[0]);
            const float highTideMarkU  = 34.0f + physWorld[1].w * 18.0f;
            const float aboveW = (p.z + camPosW.z) - liveTideWaterZ;
            if (aboveW > -8.0f && aboveW < highTideMarkU) {
                waterContact = max(waterContact, smoothstep(highTideMarkU, 0.0f, max(aboveW, 0.0f)) * smoothstep(-8.0f, 1.0f, aboveW));
            }
            // Strictly exclude glaciers, icebergs, and snowbanks (iceMask / wet.snow) from animated reflected water caustics
            // — eliminates the rhythmic cyan timed pulse across coastal ice!
            const float nonIceCoast = (1.0f - saturate(iceMask * 4.0f)) * (1.0f - saturate(wet.snow * 2.5f));
            if (nonIceCoast > 0.05f && isExterior && sunDir.w > 0.5f && sunDir.z > 0.10f && aboveW > 4.0f && aboveW < 260.0f && s.n.z < 0.45f && z < 2800.0f) {
                const float clearSunC = (1.0f - saturate(clouds.x * 0.80f)) * smoothstep(0.10f, 0.28f, sunDir.z);
                if (clearSunC > 0.05f) {
                    const float downFace = saturate((0.45f - s.n.z) * 1.65f);
                    const float heightAtt = (1.0f - smoothstep(18.0f, 260.0f, aboveW)) * smoothstep(4.0f, 18.0f, aboveW);
                    const float wtC = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
                    const float2 caustXY = (p.xy + camPosW.xy) - sunDir.xy * (aboveW / max(sunDir.z, 0.25f)) * 0.45f;
                    const float cNet = rawReflectedCausticNet(caustXY, wtC, 54.0f);
                    const float distC = smoothstep(2800.0f, 350.0f, z);
                    diff += sunColour.rgb * float3(0.42f, 0.86f, 0.96f) * (cNet * downFace * heightAtt * clearSunC * distC * nonIceCoast * 0.48f);
                }
            }
        }
        // Removed orange-blotch skinContact ambient dump on neck/collar seams (23.50.29.60.png Hilde neck artifact!).
        // Keep subtle warm dermal balance only in neutral shadowed skin without hard seam spikes.
        if (waterContact > 0.02f) {
            // Capillary pore-darkening + wet clearcoat meniscus reflection:
            albedo *= lerp(1.0f, 0.62f, waterContact * 0.85f);
            spec += max(amb, 0.04f.xxx) * (pow(1.0f - saturate(dot(s.n, s.v)), 4.0f) * waterContact * 0.42f);
        }
        if (dot(emissiveRim, float3(0.2126f, 0.7152f, 0.0722f)) > 0.005f) {
            diff += emissiveRim * (0.25f + 0.75f * pow(1.0f - saturate(dot(s.n, s.v)), 2.0f));
        }
        // Environmental Asymmetry Breaking (Windward Rime on exterior stone/terrain, strictly excluding actors & foliage!):
        // (Wave 63 + Wave 64: removed redundant shaded-side moss pass #4 and excluded isBotanicalCard pine saplings!)
        if (isExterior && !isActorFeat && !isActorCloth && !isBotanicalCard && (base == 1u || base == 4u)) {
            const float3 wpAsym = p + camPosW.xyz;
            const float2 windD = normalize(cloudWind.xy + float2(1.0f, 0.35f));
            const float windward = saturate(dot(s.n.xy, -windD));
            const float asymN = cNoise(wpAsym * 0.011f) * 0.62f + cNoise(wpAsym * 0.034f + 5.7f) * 0.38f;
            // Windward hoarfrost/rime sheen in cold/snow conditions (leeward stays darker bare rock):
            const float coldAsym = saturate(weather.z * 1.15f + smoothstep(2600.0f, 5200.0f, wpAsym.z) * 0.65f);
            const float rimeW = windward * smoothstep(0.42f, 0.82f, asymN) * coldAsym * smoothstep(0.15f, 0.75f, s.n.z);
            if (rimeW > 0.02f) {
                albedo = lerp(albedo, float3(0.76f, 0.82f, 0.90f), rimeW * 0.32f);
                spec += max(amb, 0.05f.xxx) * (pow(1.0f - saturate(dot(s.n, s.v)), 3.0f) * rimeW * 0.24f);
            }
        }
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
    const float3 envRefl = envReflEarly;
    if (flags.w == 18u) { return float4(envRefl, game.a); }   // view 18: the environment map's reflection (class 2)
    // The character light: a white fill the game's actor (and many object) shaders add to the light sum, from two fixed
    // directions and a scale read from slot 11 (GBufferAnalyse.cpp, FindCharLight). Outside classes 2 and 5 the patch
    // writes it to the emissive target as -(light) - 1/1024; emitted light is never negative.
    const float ea = (base != 2u && base != 5u && base != 13u) ? SoftMap.Load(ip).r : 0.0f;
    const float charLight = (flags.x & FLAG_CHAR) != 0u && ea < 0.0f ? max(-ea - 1.0f / 1024.0f, 0.0f) : 0.0f;
    if (flags.w == 19u) { return float4(charLight.xxx, game.a); }   // view 19: the character light
    // Wave 33 Physical Skin Lighting (kills flat unlit indoor mannequin look on Mralki 00.09.59.87!):
    // Attenuate vanilla's flat camera-attached character fill light on skin (0.18 indoors, 0.12 in sun) so real
    // tavern hearths, windows, and sunlight carve dimensional brow/cheek/nose shadows.
    const float effCharLight = (!match && skin)
        ? charLight * (isExterior ? lerp(0.32f, 0.10f, saturate(dot(diff, float3(0.33f, 0.33f, 0.34f)))) : 0.18f)
        : charLight;
    // Wave 44 Strict Fresnel Energy Conservation (kD = (1 - F) * (1 - metallic)):
    // Conductors suppress diffuse energy proportionally to metallic weight and channel reflectance into chromatic
    // anisotropic direct GGX + Karis Split-Sum / Fdez-Aguera Multi-Scattering Conductor IBL.
    const float metallic = earlyMetallic;
    const float pbrBakedAo = isTruePbr ? lerp(0.32f, 1.0f, saturate(rmaosPbr.b)) : 1.0f;
    const float nvSafeDiff = max(dot(s.n, s.v), 1.0e-4f);
    const float3 kFView = saturate(lerp(s.f0.xxx, metalF0, metallic) + (1.0f - lerp(s.f0.xxx, metalF0, metallic)) * pow(1.0f - nvSafeDiff, 5.0f));
    const float3 kD = match ? 1.0f.xxx : saturate((1.0f.xxx - kFView * 0.45f) * (1.0f - metallic * (isTruePbr ? 0.96f : 0.86f)));
    const float3 diffAlb = albedo * kD;
    const float3 totalIrrad = diff + amb * pbrBakedAo + effCharLight;
    const float warmRatio = saturate((totalIrrad.r - totalIrrad.b * 1.15f) / max(totalIrrad.r + totalIrrad.b + 0.05f, 0.05f));
    const float effTempK = (!match && warmRatio > 0.12f) ? lerp(5800.0f, 2350.0f, saturate(warmRatio * 1.85f)) : 0.0f;
    const float3 spectralDiff = match ? (diffAlb * totalIrrad)
                                      : spectralSurfaceResponse(diffAlb, totalIrrad, effTempK, 0.72f);
    const float3 rawLit = spectralDiff + envRefl * (match ? 1.0f : 0.08f) * totalIrrad;
    float3 lit = match ? knee3(rawLit) : rawLit;
    // Subtle alpine snowbank subsurface wrap (skipped on 3D World Map camPosW.z > 18000 -> kills cyan map peak glow!):
    if (!match && wet.snow > 0.05f && sunDir.w > 0.5f && timeEnv.w < 0.5f && camPosW.z < 18000.0f) {
        const float snowWrap = saturate((dot(s.n, sunDir.xyz) + 0.30f) / 1.30f);
        lit += sunColour.rgb * float3(0.08f, 0.14f, 0.22f) * (snowWrap * wet.snow * (0.35f + 0.65f * sunShadow) * 0.10f);
    }
    // True Interior Window-Lighting & HDR Window Bloom Driver (coupled to exterior time-of-day timeEnv.xy):
    // STRICTLY base == 3u (GlowMapped emissive windows ONLY!). Previously matched base != 3u vertical surfaces
    // with gLumW > 0.55, which falsely treated Farengar's plaster wall (16.00.49) and Septimus Signus's vertical
    // ice cave walls (16.02.00 / 16.02.04) as 2.65x HDR windows and blasted them into blinding white/cyan bloom!
    if (!match && !isExterior && base == 3u && !skin && !isActorFeat && !isActorCloth) {
        const float gLumW = dot(game.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float isVertGlass = smoothstep(0.68f, 0.35f, abs(s.n.z));
        const float winMask = smoothstep(0.26f, 0.68f, gLumW) * isVertGlass;
        if (winMask > 0.02f) {
            const float sunE = timeEnv.y;
            const float dayW = smoothstep(-0.12f, 0.26f, sunE);
            const float goldW = smoothstep(-0.12f, 0.12f, sunE) * (1.0f - smoothstep(0.18f, 0.52f, sunE));
            const float3 winCol = lerp(float3(0.28f, 0.46f, 0.92f),
                                       lerp(float3(1.02f, 1.06f, 1.15f), float3(1.35f, 0.82f, 0.36f), goldW), dayW);
            const float winHdr = lerp(0.55f, 2.20f, dayW) * (1.0f - saturate(clouds.x * 0.38f));
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
    // Wave 64 + Wave 72: Energy-conserving foliage specular + continuous wet-leaf sheen under rain/mist!
    // Keeps kD + kS <= 1.0 (white-furnace safe): wet leaves gain crisp sky-coupled dielectric sheen while
    // attenuating diffuse energy proportionally, preventing chalky bleaching on dry midday pine needles.
    const float wetLeafW = (isBotanicalCard && isExterior && !isWoodBark) ? saturate(wet.wet * 0.90f + weather.w * 0.35f) : 0.0f;
    const float botSpecScale = lerp(0.08f, 0.22f, wetLeafW);
    const float specMap = lobe && !(skin && !match) ? saturate(s.mask * resScale.w) : (isBotanicalCard ? botSpecScale : 1.0f);
    const float3 foliageSpecTint = isBotanicalCard ? lerp(float3(0.14f, 0.24f, 0.10f), float3(0.42f, 0.50f, 0.46f), wetLeafW) : 1.0f.xxx;
    if (wetLeafW > 0.02f) { lit = max(lit * (1.0f - wetLeafW * 0.14f), min(lit, 0.022f.xxx)); }
    lit += ((flags.x & FLAG_GGX) != 0u ? spec * specMap : spec * (lobe ? s.mask * resScale.w : 0.0f)) * foliageSpecTint;
    // Ray-traced reflection on glossy surfaces with live dynamic-cubemap IBL fallback on RT misses,
    // plus dynamic cubemap enhancement for class-2 metal/armor and clearcoat GGX on wet surfaces.
    if (!match) {
        const float nv = max(dot(nSmooth, s.v), 1.0e-4f);
        // Frostbite (Lagarde 2014) specular occlusion: prevents fake sky/cubemap gloss inside deep occluded rock/timber crevices.
        const float specOcc = saturate(pow(max(nv + combinedOcc, 1.0e-4f), exp2(-16.0f * s.alpha - 1.0f)) - 1.0f + combinedOcc);
        const float3 rDir = reflect(-s.v, nSmooth);
        const float envRough = max(s.alpha, base == 2u ? 0.14f : 0.08f);
        // Live 256x256 Dual-Band Prefiltered Dynamic Environment Cubemap (live sky + clouds + 32 dynamic lights + ground bounce).
        const float3 dynEnv = sampleLiveEnvCube(rDir, envRough);
        const float3 F0mat = lerp(s.f0.xxx, metalF0, metallic);
        const float reflMask = max(saturate(s.mask * resScale.w), max(wet.puddle, 0.5f * wet.wet));
        // Note: Direct brushed-metal anisotropic GGX (evalBrushedMetalAniso) runs cleanly across both sun and clustered
        // torches/forges above (no duplicate unshadowed ad-hoc anisotropy lobe here!).
        // RAW-STRATA Unified 4-Slab Energy-Conserving Optical Transport (excluded on skin, eyes & ALL botanical foliage!):
        if (!skin && !isEye && !isBotanicalCard) {
            StrataSlab st;
            st.baseAlbedo = albedo;
            st.roughness = s.alpha;
            st.metallic = metallic;
            st.coatWeight = max(saturate(wet.wet * 0.75f + wet.puddle * 1.05f), ((base == 2u && iceMask < 0.08f) || isTruePbr ? (1.0f - metallic) * 0.30f : 0.0f));
            st.coatRoughness = lerp(0.14f, 0.04f, saturate(wet.puddle));
            st.thinFilmNm = (base == 2u && metallic > 0.50f && iceMask < 0.08f) ? 460.0f : 0.0f;
            st.fuzzWeight = (isActorCloth ? 0.55f : (wet.snow > 0.40f ? 0.28f * wet.snow : 0.0f));
            st.fuzzTint = wet.snow > 0.40f ? float3(0.88f, 0.94f, 1.00f) : lerp(albedo * 1.15f + 0.04f.xxx, 1.0f.xxx, 0.25f);
            st.fuzzRoughness = wet.snow > 0.40f ? 0.52f : 0.62f;
            if (st.coatWeight > 0.02f || st.fuzzWeight > 0.02f) {
                const float3 stKeyDir = (isExterior && sunDir.w > 0.5f) ? sunDir.xyz : normalize(s.v + float3(0.12f, 0.08f, 0.62f));
                const float3 stKeyCol = isExterior ? (sunColour.rgb * (0.20f + 0.80f * sunShadow) + amb * 0.35f) : max(amb * 1.25f, 0.05f.xxx);
                if (isActorCloth) {
                    const RawBurleySssBudget clothW79 = rawEvalBurleySkinClothSss(albedo, dot(nSmooth, stKeyDir), nv, s.alpha, false);
                    st.fuzzWeight = min(st.fuzzWeight, max(1.0f - clothW79.totalEnergyMax + clothW79.fresnelSpec, 0.25f));
                }
                float3 baseTrans = 1.0f.xxx;
                const float3 upperSlab = evalStrataUpperSlabs(st, nSmooth, s.v, stKeyDir, stKeyCol, specOcc, baseTrans);
                lit = lit * baseTrans + upperSlab;
            }
        }
        const float3 F0mat_unused_marker = F0mat;
        if (rt && (flags.x & FLAG_REFLECT) != 0u && lobe && !isBotanicalCard) {
            const int2 baseRefl = clamp(int2(px + rings * 40.0f), int2(0, 0), int2(dims.xy) - 1);
            float4 rfSum = 0.0f.xxxx;
            float rfW = 0.0f;
            [unroll] for (int ry = -1; ry <= 1; ry++) {
                [unroll] for (int rx = -1; rx <= 1; rx++) {
                    const int3 rp = int3(clamp(baseRefl + int2(rx, ry), int2(0, 0), int2(dims.xy) - 1), 0);
                    const float rdDepth = Depth.Load(rp);
                    if (abs(viewZOf(rdDepth) - z) < max(z * 0.03f, 2.5f)) {
                        const float4 tap = RtRefl.Load(rp);
                        if (!any(isnan(tap)) && !any(isinf(tap))) {
                            const float nw = pow(saturate(dot(nSmooth, worldNormal(Normal.Load(rp)))), 16.0f);
                            const float kw = ((rx == 0 && ry == 0) ? 2.0f : 1.0f) * max(nw, (rx == 0 && ry == 0) ? 1.0f : 0.0f);
                            rfSum += float4(clamp(tap.rgb, 0.0f.xxx, 4.0f.xxx), saturate(tap.a)) * kw;
                            rfW += kw;
                        }
                    }
                }
            }
            const float4 rf = rfW > 0.0f ? (rfSum / rfW) : RtRefl.Load(int3(baseRefl, 0));
            // Smoothly weight RT reflections by roughness AND cleanly zero out noisy RtRefl on glaciers/ice
            // so outdoor glaciers & ice caves reflect the silky static prefiltered IBL with zero RT flicker/pulse:
            const float rtReflW = saturate(rf.a) * lerp(0.82f, 0.22f, saturate(envRough * 2.4f)) * (1.0f - saturate(iceMask * 1.6f));
            const float3 envFinal = lerp(dynEnv * (0.30f + 0.70f * skyVisShaped), rf.rgb, rtReflW);
            lit += evalMultiScatterIBL(F0mat, envRough, nv, envFinal, specOcc) * max(reflMask, metallic);
            if (flags.w == 6u) { return float4(envFinal, game.a); }
        } else if (base == 2u) {
            // Class-2 forged metal/armor/gold/brass: exact Karis Split-Sum + Fdez-Aguera Multi-Scattering Conductor IBL.
            const float3 metalEnv = dynEnv * (0.30f + 0.70f * skyVisShaped);
            lit += evalMultiScatterIBL(F0mat, envRough, nv, metalEnv, specOcc) * max(reflMask, max(metallic, 0.35f));
        } else if (base == 12u) {
            // Class-12 Eye corneal wet catchlight: sharp corneal reflection from live dynamic cubemap (reflects nearby fires & sky).
            lit += evalMultiScatterIBL(float3(0.028f, 0.028f, 0.028f), 0.04f, nv, sampleLiveEnvCube(rDir, 0.04f), 0.45f + 0.55f * skyVis);
        } else if ((base == 4u || base == 6u || base == 1u || isSkinnedActor) && !isBotanicalCard) {
            // Full-world physical dielectric Split-Sum IBL on terrain, cobblestones, stone, timber & skinned actor cloth/fur (excluded on ALL foliage!).
            const float3 dieF0 = lerp(0.028f, 0.042f, saturate(wet.wet + wet.puddle)).xxx;
            const float dieRough = clamp(s.alpha, 0.16f, 0.82f);
            const float3 dieEnv = sampleLiveEnvCube(rDir, dieRough) * (0.28f + 0.72f * skyVisShaped);
            lit += evalMultiScatterIBL(dieF0, dieRough, nv, dieEnv, specOcc) * lerp(isSkinnedActor ? 0.45f : 0.55f, 1.0f, saturate(wet.wet + wet.puddle));
        }
        // Wave 58: Marmoset/Lagarde Horizon Specular Occlusion prevents micro-normal reflection rays pointing
        // behind the macro surface normal nSmooth from leaking bright sky/cubemap glow across grazing creases.
        const float3 rDetail = reflect(-s.v, s.n);
        const float horizonDot = saturate(1.0f + 1.35f * dot(rDetail, nSmooth));
        const float horizonOcc = horizonDot * horizonDot;
        // Energy-Conserving Dual-Lobe Clearcoat / Wetness Specular (sharp primary mirror-water film + broader
        // capillary wet-sheen secondary lobe; strictly attenuates base layer by (1 - clearcoat * F_coat) and
        // excludes glacial ice so ice caves never double-count EnvCube!).
        const float clearcoat = saturate(max(
            max(saturate(wet.wet * 0.85f + wet.puddle * 1.15f + porosityWetW * 0.48f), rainWallStreak * 0.78f),
            ((base == 2u && iceMask < 0.08f) ? (1.0f - metallic) * 0.35f : 0.0f))) * (1.0f - saturate(iceMask * 2.0f));
        if (clearcoat > 0.04f && !skin && !isBotanicalCard) {
            const float fCoatV = saturate(0.0204f + 0.9796f * pow(1.0f - nv, 5.0f));
            // Strictly conserve energy: base layer transmits (1 - coat * F_coat) beneath the dielectric water/lacquer film!
            lit *= saturate(1.0f - clearcoat * fCoatV * 0.85f);
            const float ccRough1 = lerp(lerp(0.075f, 0.050f, rainWallStreak), 0.028f, saturate(wet.puddle));
            const float ccRough2 = lerp(0.24f, 0.14f, saturate(wet.puddle));
            const float skyEnvGate = (0.30f + 0.70f * skyVisShaped) * horizonOcc;
            const float3 ccEnv1 = sampleLiveEnvCube(rDir, ccRough1) * skyEnvGate;
            const float3 ccEnv2 = sampleLiveEnvCube(rDir, ccRough2) * skyEnvGate;
            const float3 ccLobe1 = evalMultiScatterIBL(float3(0.0204f, 0.0204f, 0.0204f), ccRough1, nv, ccEnv1, specOcc);
            const float3 ccLobe2 = evalMultiScatterIBL(float3(0.0204f, 0.0204f, 0.0204f), ccRough2, nv, ccEnv2, specOcc);
            lit += (ccLobe1 * 0.72f + ccLobe2 * 0.28f) * (clearcoat * 0.92f);
        }
        // Estevez-Kulla grazing velvet/fuzz rim sheen on rough organic materials (excluded on ALL foliage to avoid pale chalk halo!).
        if (!skin && base != 2u && !isBotanicalCard && s.alpha > 0.52f && sunDir.w > 0.5f && sunShadow > 0.05f) {
            const float rimFuzz = pow(1.0f - nv, 4.0f) * saturate((s.alpha - 0.52f) * 2.5f) * specOcc;
            lit += albedo * sunColour.rgb * (rimFuzz * sunShadow * 0.28f);
        }
        // Wave 46 Blizzard Alpine Spindrift (strictly gated on active snowstorms; 100% inactive on calm/dry days!):
        // Subtle wind-advected blowing snow-dust grazing highlights whipping along exposed rocky ridges & cliff faces.
        if (isExterior && weather.z > 0.25f && !isActorFeat && !isActorCloth && base != 5u && s.n.z < 0.65f && z < 2600.0f) {
            const float3 wpSpin = p + camPosW.xyz;
            const float simTimeS = (physWorld[0].x > 1.0e-4f) ? physWorld[0].x : camPosW.w;
            const float2 windDirS = normalize(cloudWind.xy + float2(1.0f, 0.35f));
            const float2 windPerpS = float2(-windDirS.y, windDirS.x);
            const float windBoost = clamp(1.0f + physWorld[1].x * 0.14f, 1.0f, 2.4f);
            const float3 spinP = float3(
                dot(wpSpin.xy, windDirS) * 0.0085f - simTimeS * (1.85f * windBoost),
                dot(wpSpin.xy, windPerpS) * 0.030f,
                wpSpin.z * 0.026f - simTimeS * 0.28f);
            const float4 sn0 = rawNoise3Grad(spinP);
            const float4 sn1 = rawNoise3Grad(spinP * 2.15f + float3(sn0.y * 0.75f - simTimeS * 1.1f, sn0.z * 0.75f, 7.1f));
            const float wispMask = smoothstep(0.46f, 0.86f, sn0.x * 0.58f + sn1.x * 0.42f);
            const float stormGate = smoothstep(0.25f, 0.72f, weather.z);
            const float distGate  = smoothstep(2600.0f, 220.0f, z);
            const float ridgeSlope = smoothstep(-0.20f, 0.18f, s.n.z) * smoothstep(0.65f, 0.18f, s.n.z);
            const float grazeRim   = pow(1.0f - nv, 2.8f);
            const float windShear  = 0.45f + 0.55f * (1.0f - abs(dot(s.n.xy, windDirS)));
            const float skyExp     = saturate(0.20f + 0.80f * skyVis);
            const float fwdScatter = pow(saturate(dot(-s.v, sunDir.xyz) * 0.5f + 0.5f), 2.0f);
            const float3 spinLight = amb * float3(1.12f, 1.22f, 1.36f)
                                   + sunColour.rgb * float3(0.86f, 0.92f, 1.00f) * (0.14f + 0.42f * sunShadow * fwdScatter);
            lit += min(spinLight, 1.65f.xxx) * (wispMask * grazeRim * ridgeSlope * windShear * stormGate * distGate * skyExp * 0.48f);
        }
        // Synchronized thunderstorm lightning overhead flash on wet surfaces & puddles during heavy rainstorms
        // (strictly gated off on frozen glaciers/ice/snow so cold regions never pulse!).
        if (isExterior && weather.w > 0.45f && weather.z < 0.15f && clouds.x > 0.65f && iceMask < 0.08f) {
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
        // Wave 58: Energy-Conserving Direct Sun Dual-Lobe Clearcoat GGX on wet water films (full Smith visibility V +
        // Schlick Fresnel IOR 1.333 + sharp primary mirror lobe & soft wet-sheen secondary lobe; zero oil-slick stripes!).
        const float wetFilmGlint = saturate(max(max(wet.wet * 0.62f + porosityWetW * 0.45f, wet.puddle), rainWallStreak * 0.58f));
        if (wetFilmGlint > 0.06f && isExterior && sunShadow > 0.0f && !skin && base != 5u) {
            const float cnl = saturate(dot(s.n, sunDir.xyz));
            if (cnl > 1.0e-4f) {
                const float3 ch = rawSafeHalfVec(s.v, sunDir.xyz, s.n);
                const float cnh = saturate(dot(s.n, ch));
                const float cvh = saturate(dot(s.v, ch));
                const float cnv = max(dot(s.n, s.v), 1.0e-4f);
                const float cF = saturate(0.0204f + 0.9796f * pow(1.0f - cvh, 5.0f));
                // Wave 58 + Wave 74: Primary mirror-film lobe + secondary capillary sheen lobe coupled to
                // volumetric cloud-shadow transmittance & coherent wind-gust ripple broadening (leaves sky ambient intact):
                const float4 wGustWet = rawEvalCoherentWindGust4(p + camPosW.xyz, cloudWind, weather, camPosW.w);
                const float wetRoughBroad = 1.0f + (1.0f - cloudTransL) * 0.25f + wGustWet.z * 0.22f + weather.w * 0.18f;
                const float ca1 = clamp(0.003364f * wetRoughBroad, 0.003364f, 0.0120f);
                const float ca2 = clamp(0.0484f * wetRoughBroad, 0.0484f, 0.1200f);
                const float cd1 = max(cnh * cnh * (ca1 - 1.0f) + 1.0f, 1.0e-4f);
                const float cd2 = max(cnh * cnh * (ca2 - 1.0f) + 1.0f, 1.0e-4f);
                const float cD1 = ca1 / (PI * cd1 * cd1);
                const float cD2 = ca2 / (PI * cd2 * cd2);
                const float cV1 = 0.5f / (cnl * sqrt(cnv * cnv * (1.0f - ca1) + ca1) + cnv * sqrt(cnl * cnl * (1.0f - ca1) + ca1) + 1.0e-5f);
                const float cV2 = 0.5f / (cnl * sqrt(cnv * cnv * (1.0f - ca2) + ca2) + cnv * sqrt(cnl * cnl * (1.0f - ca2) + ca2) + 1.0e-5f);
                const float dualLobe = (cD1 * cV1 * 0.68f + cD2 * cV2 * 0.32f) * PI * cF * cnl;
                lit += min(sunColour.rgb * (dualLobe * sunShadow * lerp(0.84f, 1.0f, cloudTransL) * horizonOcc * 0.65f), 2.2f.xxx) * wetFilmGlint;
            }
        }
        // Wave 58: Sub-Zero Hoarfrost / Rime Crystal Micro-Sparkle (crisp sunlit frost needle glints on sub-zero edges):
        if (hoarfrostGlintW > 0.01f && isExterior && sunShadow > 0.05f) {
            const float3 fh = rawSafeHalfVec(s.v, sunDir.xyz, s.n);
            const float fnh = saturate(dot(s.n, fh));
            const float frostSpec = pow(fnh, 42.0f) * saturate(dot(s.n, sunDir.xyz)) * sunShadow * horizonOcc;
            lit += min(sunColour.rgb * float3(0.88f, 0.95f, 1.08f) * (frostSpec * hoarfrostGlintW * 1.45f), 1.35f.xxx);
        }
    }
    if (!match) {
        // Character Portrait & Eye-Candy Shading (Vanilla NPCs & PCs):
        // 1) Skin (base==10): Wave 33 Physical Dual-Lobe Sebum/Pore Specular + Hemoglobin Terminator SSS
        // Driven by the actual strongest live scene light indoors (hearths/candles/windows) or sun outdoors — never flat ambient fill!
        if (skin) {
            const float nvSkin = saturate(dot(s.n, s.v));
            float3 skKeyDir = isExterior ? sunDir.xyz : normalize(float3(0.25f, 0.20f, 0.85f));
            float3 skKeyCol = isExterior ? (sunColour.rgb * sunShadow) : 0.0f.xxx;
            if (!isExterior && flags.y > 0u) {
                float bestSk = 0.0f;
                const uint nSkL = min(flags.y, 24u);
                [loop] for (uint ski = 0u; ski < nSkL; ++ski) {
                    const float3 dlS = lightPosR[ski].xyz - p;
                    const float dS = length(dlS);
                    const float rS = lightPosR[ski].w;
                    if (dS < rS && dS > 1.0f) {
                        const float winS = saturate(1.0f - (dS / rS) * (dS / rS));
                        const float lumS = dot(lightColS[ski].rgb, float3(0.2126f, 0.7152f, 0.0722f)) * winS * winS;
                        if (lumS > bestSk) {
                            bestSk = lumS;
                            skKeyDir = dlS / dS;
                            skKeyCol = lightColS[ski].rgb * (winS * winS);
                        }
                    }
                }
            }
            const float nlRaw = dot(s.n, skKeyDir);
            // Wave 63 + Wave 79: Burley-normalized hemoglobin terminator SSS + energy-conserving sebum Fresnel (<= 1.0):
            const RawBurleySssBudget skinW79 = rawEvalBurleySkinClothSss(albedo, nlRaw, nvSkin, s.alpha, true);
            lit += skinW79.sssWrap * min(skKeyCol, 0.90f.xxx) * 0.55f;
            const float3 shv = rawSafeHalfVec(s.v, skKeyDir, s.n);
            const float snh = saturate(dot(s.n, shv));
            const float sa1 = 0.048f, sa2 = 0.240f;
            const float sd1 = max(snh * snh * (sa1 - 1.0f) + 1.0f, 1.0e-4f);
            const float sd2 = max(snh * snh * (sa2 - 1.0f) + 1.0f, 1.0e-4f);
            const float sD = (sa1 / (PI * sd1 * sd1)) * 0.30f + (sa2 / (PI * sd2 * sd2)) * 0.70f;
            const float sF = min(skinW79.fresnelSpec, 0.026f + 0.974f * pow(1.0f - saturate(dot(s.v, shv)), 5.0f));
            lit += min(skKeyCol * (sD * sF * saturate(nlRaw) * 0.14f), 0.24f.xxx);
            const float fuzzRim = pow(1.0f - nvSkin, 4.0f) * saturate(nlRaw * 0.5f + 0.5f) * 0.032f;
            lit += skinW79.albedoEff * float3(1.04f, 0.92f, 0.86f) * (min(skKeyCol, 0.90f.xxx) * fuzzRim);
        } else if (base == 11u) {
            // 2) Hair (base==11): Wave 40 Physical Marschner 3-Lobe Anisotropic Hair Shading (R + TT + TRT)
            // driven by actual strongest local hearth/candle/torch key light indoors or sun+sky fill outdoors!
            const float3 gDownH = float3(0.0f, 0.0f, -1.0f);
            float3 tStrand = gDownH - s.n * dot(gDownH, s.n);
            tStrand = (dot(tStrand, tStrand) > 1.0e-4f) ? normalize(tStrand) : normalize(cross(s.n, float3(1.0f, 0.0f, 0.0f)));
            const float strandJit = (rawNoise3Grad((p + camPosW.xyz) * 0.85f).x - 0.5f) * 0.14f;
            const float3 tFlow = normalize(tStrand + s.n * strandJit);
            float3 hKeyDir = isExterior ? sunDir.xyz : normalize(s.v + float3(0.18f, 0.14f, 0.72f));
            float3 hKeyCol = isExterior ? (amb * 0.45f) : max(amb * 0.85f, float3(0.08f, 0.075f, 0.07f));
            if (flags.y > 0u) {
                float bestHL = 0.0f;
                const uint nHairL = min(flags.y, 16u);
                [loop] for (uint hli = 0u; hli < nHairL; ++hli) {
                    const float3 dlH = lightPosR[hli].xyz - p;
                    const float dH = length(dlH);
                    const float rH = lightPosR[hli].w;
                    if (dH < rH && dH > 1.0f) {
                        const float winH = saturate(1.0f - (dH / rH) * (dH / rH));
                        const float lumH = dot(lightColS[hli].rgb, float3(0.2126f, 0.7152f, 0.0722f)) * winH * winH;
                        if (lumH > bestHL) {
                            bestHL = lumH;
                            hKeyDir = dlH / dH;
                            hKeyCol = lightColS[hli].rgb * (winH * winH * 0.85f);
                        }
                    }
                }
            }
            const float3 m3 = evalMarschnerHair3Lobe(tFlow, s.n, s.v, hKeyDir, albedo, clamp(s.alpha, 0.24f, 0.52f));
            lit += min(hKeyCol * m3, 1.6f.xxx);
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
            const float nh1 = saturate(dot(s.n, rawSafeHalfVec(s.v, keyDir1, s.n)));
            const float nh2 = saturate(dot(s.n, rawSafeHalfVec(s.v, keyDir2, s.n)));
            const float glint1 = pow(nh1, 180.0f) * 1.35f + pow(nh1, 32.0f) * 0.18f;
            const float glint2 = pow(nh2, 120.0f) * 0.42f;
            const float3 eyeEnv = evalDynamicEnvCube(reflect(-s.v, s.n), 0.04f);
            const float eyeFr = 0.04f + 0.96f * pow(1.0f - nvEye, 5.0f);
            lit += eyeEnv * eyeFr * 1.25f + eyeKeyCol * (glint1 + glint2);
        } else if ((base == 5u || isGreenPineCard) && isExterior) {
            // Engine-Level 3D Botanical Canopy Volume Transport (Wave 64: unified for base==5u AND base==1u pine saplings!):
            // Combines DXR/cloud-gated backlit chlorophyll subsurface scattering, balanced crown shadow fill,
            // and deep emerald needle warmth so sunlit pines stay lush Nordic spruce green with zero chalk bleach!
            const float3 wpCan = p + camPosW.xyz;
            const float back = pow(saturate(dot(-s.v, sunDir.xyz)), 3.6f);
            const float cloudClear = 1.0f - saturate(clouds.x * 0.82f);
            const float dayGate = smoothstep(0.02f, 0.14f, astroSunElev());
            const float foliageThickness = lerp(0.45f, 1.0f, cNoise(wpCan * 0.024f + 5.1f));
            // Wave 74: World-space coherent gust + white-furnace energy-conserving wet-leaf subsurface budget
            // (albedo_eff + Fresnel + transmission <= 1.0; cloudTransL dims direct backlit glow once, never sky ambient):
            const float4 wGustLeaf = rawEvalCoherentWindGust4(wpCan, cloudWind, weather, camPosW.w);
            const float nvLeaf = saturate(dot(s.n, s.v));
            const RawWetLeafBudget leafBudget = rawEvalWetLeafRadiativeBudget(albedo, wet.wet, nvLeaf, cloudTransL, wGustLeaf.z);
            const float frontSun = saturate(dot(s.n, sunDir.xyz));
            const float backTight = pow(saturate(dot(-s.v, sunDir.xyz)), 5.0f);
            const float backEdgeGate = 1.0f - frontSun * 0.78f;
            const float3 sssCol = leafBudget.albedoEff * float3(0.84f, 1.18f, 0.24f);
            const float transScale = saturate(leafBudget.directTransWeight / 0.24f);
            // Wave 78: World-space sub-pixel foliage anti-shimmer & coverage preservation (strictly !isWoodBark!):
            const float maxLeafAlb = max(leafBudget.albedoEff.r, max(leafBudget.albedoEff.g, leafBudget.albedoEff.b));
            const RawFoliageAntiShimmerResult folW78 = rawEvalFoliageCoverageAntiShimmer(
                wpCan, s.n, s.v, z, maxLeafAlb, leafBudget.directTransWeight, skyVis, isWoodBark);
            lit += sssCol * sunColour.rgb * ((back * 0.09f + backTight * 0.18f) * backEdgeGate * foliageThickness * sunShadow * cloudClear * dayGate * lerp(0.65f, 1.15f, transScale) * folW78.translucencyScale);
            // Balanced 3D vertical crown-to-underside pine self-shadowing (rich green shadow volume, never black or chalk!):
            const float crownUp = saturate(s.n.z * 0.55f + 0.50f);
            const float canopyOcc = saturate(skyVis * 0.62f + sunShadow * 0.24f + crownUp * 0.22f);
            lit *= lerp(float3(0.42f, 0.54f, 0.38f) * max(folW78.canopyFloorScale, 0.92f), 1.0f.xxx, canopyOcc) * folW78.coverageScale;
            // Preserve rich emerald needle saturation under bright midday sunlight (prevents white-grey highlight wash!):
            if (wet.snow < 0.15f) {
                const float pLum = dot(lit, float3(0.2126f, 0.7152f, 0.0722f));
                const float brightSunW = smoothstep(0.28f, 0.75f, pLum);
                const float3 richNeedles = max(lerp(pLum.xxx, lit, 1.24f), 0.0f.xxx) * float3(0.94f, 1.04f, 0.88f);
                lit = lerp(lit, richNeedles, brightSunW * 0.65f);
            }
        }
        // Wave 58: Horizon-Occluded Sky-Bounce Ambient Fill Respecting Grazing Fresnel (energy-conserving):
        // Uses Lagarde geometric horizon occlusion against macro normal nSmooth + physical dielectric grazing Fresnel
        // so exposed stone, roofs, and wet terrain pick up crisp cool-sky grazing fill while occluded overhangs stay grounded!
        const float nvWrap = saturate(dot(s.n, s.v));
        const float3 rWrap = reflect(-s.v, s.n);
        const float horizBounce = saturate(1.0f + 1.25f * dot(rWrap, nSmooth));
        const float horizBounceOcc = horizBounce * horizBounce;
        const float fGraze = saturate(s.f0 + (1.0f - s.f0) * pow(1.0f - nvWrap, 5.0f));
        const float skyBounceScale = (skin ? 0.08f : isBotanicalCard ? 0.030f : 0.14f) * (1.0f - metallic * 0.55f);
        const float3 skyBounceTint = isExterior ? lerp(1.0f.xxx, float3(0.86f, 0.94f, 1.12f), skyVisShaped * (1.0f - (skin ? 0.92f : 0.0f)) * 0.45f) : 1.0f.xxx;
        lit += amb * skyBounceTint * (fGraze * skyBounceScale * horizBounceOcc * (0.25f + 0.75f * skyVisShaped) * combinedOcc * (1.0f - iceMask * 0.55f)) + decalEmit;
        // Wave 74: Radiatively consistent overcast diffuse sky-bounce preservation on vertical exterior tree bark
        // (volumetric cloud shadows attenuate direct sun, NOT indirect sky hemisphere fill on shaded pine trunks!).
        if (isWoodBark && isExterior) {
            lit += max(albedo, float3(0.060f, 0.048f, 0.036f)) * max(amb, float3(0.085f, 0.095f, 0.105f))
                 * (0.26f + 0.20f * (1.0f - cloudTransL)) * (0.48f + 0.52f * skyVisShaped);
        }
        // Note: removed redundant 3rd Mie scatter wash in Lighting.hlsl; FroxelPass.hlsl (0..42,000u) + LightFog.hlsl
        // now own unified engine-level 3D volumetric transmittance and Hillaire aerial perspective with zero double-fogging.
    }
    // Wave 63 Live Visual Debug Layers (flags.w = 28..33):
    // 28: Material & Actor/Creature/Fur/Cloth (14) / Foliage / Glacier Class ID Map
    // 29: Linear Depth & Silhouette Discontinuity Inspector
    // 30: Shadow Stability & RT Point-Light Slot Map (R=sunShadow, G=RT top-3 slot vis/occupancy, B=ReSTIR vis)
    // 31: Moss / Weathering / Snow / Glacier Mask Inspector (R=snow, G=moss, B=ice/glacier; magenta alarm if moss hits actor!)
    // 32: Actor Skin / SSS / Specular & Contact-AO Inspector (bright actor highlight, dimmed world)
    // 33: NaN / Inf / Negative / HDR > 32.0 Energy Overflow Heatmap (checked BEFORE sanitizeLit3!)
    if (flags.w == 28u) {
        const float shadeL = saturate(dot(albedo, float3(0.2126f, 0.7152f, 0.0722f)));
        return float4(debugClassColor(base, isActorCloth, iceMask) * (0.42f + 0.58f * shadeL), game.a);
    }
    if (flags.w == 29u) {
        const float zSafeD = max(z, 1.0f);
        const float zRamp = saturate(log2(zSafeD) / 15.0f);
        const float zDisc = smoothstep(0.03f, 0.12f, (abs(ddx(zSafeD)) + abs(ddy(zSafeD))) / zSafeD);
        const float3 depthCol = float3(zRamp * 0.85f, zRamp * 0.55f, 1.0f - zRamp * 0.65f);
        return float4(lerp(depthCol, float3(0.15f, 1.0f, 0.95f), zDisc), game.a);
    }
    if (flags.w == 30u) {
        const float activeSlots = (rIdx.x != 0u ? 1.0f : 0.0f) + (rIdx.y != 0u ? 1.0f : 0.0f) + (rIdx.z != 0u ? 1.0f : 0.0f);
        const float slotVisSum  = (rIdx.x != 0u ? rv.y : 0.0f) + (rIdx.y != 0u ? rv.z : 0.0f) + (rIdx.z != 0u ? rv.w : 0.0f);
        const float slotVisAvg  = activeSlots > 0.5f ? (slotVisSum / activeSlots) : 0.0f;
        const float slotG = saturate((activeSlots / 3.0f) * (0.30f + 0.70f * slotVisAvg));
        return float4(saturate(sunShadow), slotG, saturate(restVis), game.a);
    }
    if (flags.w == 31u) {
        if ((isActorFeat || isActorCloth) && weatherMossMask > 0.001f) {
            return float4(1.0f, 0.0f, 1.0f, game.a);   // Alarm: moss leaked onto actor/creature!
        }
        const float3 mCol = (isActorFeat || isActorCloth)
            ? float3(0.06f, 0.06f, 0.14f)
            : float3(saturate(wet.snow), saturate(weatherMossMask * 2.2f), saturate(iceMask));
        return float4(mCol, game.a);
    }
    if (flags.w == 32u) {
        if (isActorFeat || isActorCloth) {
            const float specL = saturate(dot(max(spec, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f)) * 2.5f);
            return float4(isActorFeat ? 0.92f : 0.48f, saturate(combinedOcc), specL, game.a);
        }
        const float wL = dot(albedo, float3(0.2126f, 0.7152f, 0.0722f)) * 0.18f;
        return float4(wL.xxx, game.a);
    }
    if (flags.w == 33u) {
        return float4(debugHdrEnergyHeatmap(lit), game.a);
    }

    // Clean Physical HDR Shoulder in native DX12 mode (!match): preserves full 1:1 linear HDR punch up to 1.20
    // then rolls off smoothly up to ~2.75 so ice caves, snowbanks & bright surfaces stay strictly below BloomPass (2.25),
    // while true HDR flames/sun/emissives stay crisp through BloomPass and Tonemap.hlsl (AgX / ACES 2.0)!
    lit = sanitizeLit3(lit);
    const float litPeak = max(lit.r, max(lit.g, lit.b));
    const float kneeStart = (!isExterior && iceMask > 0.10f) ? 0.72f : 1.20f;
    const float kneeSlope = (!isExterior && iceMask > 0.10f) ? 1.45f : 0.62f;
    const float3 hdrLit = (litPeak > kneeStart)
        ? lit * ((kneeStart + (litPeak - kneeStart) / (1.0f + (litPeak - kneeStart) * kneeSlope)) / litPeak)
        : max(lit, 0.0f.xxx);
    const float3 finalLitRaw = match ? knee3(lit) : hdrLit;
    // Preserve genuinely chromatic emissive tavern windows, lanterns, braziers, and magic runes
    // while strictly excluding achromatic white/blue snowfields & glaciers (fixes glowing Dawnstar snow/ice patches).
    const float gLuma = dot(game.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float rLuma = dot(finalLitRaw, float3(0.2126f, 0.7152f, 0.0722f));
    const float gMax = max(game.r, max(game.g, game.b));
    const float gMin = min(game.r, min(game.g, game.b));
    const float gSat = (gMax - gMin) / max(gMax, 1.0e-4f);
    const float zPix = viewZOf(d);
    // Preserve warm emissive tavern windows, lanterns, braziers & fire runes while strictly excluding cold cyan ice caves (13.59.17):
    const bool isWarmEmissive = (game.r > game.b * 0.95f) || (game.g > game.b * 1.15f);
    const float emissiveW = (!match && !skin && base != 5u && isWarmEmissive && zPix < 5500.0f && gSat > 0.32f && gLuma > rLuma * 1.85f && gLuma > 0.22f)
                          ? saturate((gLuma - rLuma * 1.85f) * 2.5f) * saturate((5500.0f - zPix) / 1500.0f) : 0.0f;
    const float3 finalLit = lerp(finalLitRaw, max(finalLitRaw, game.rgb * 0.92f), emissiveW);
    // Wave 64 Custom Pipeline Debug Views (flags.w = 34, 35, 36):
    // 34: Flora, Bark, Pine & Foliage Lighting Audit (green=base 5 foliage, lime=base 1 pine sapling, amber=bark trunk,
    //     bright RED alarm=crushed-black bark luma<0.015, bright MAGENTA alarm=bleached-chalk non-snow foliage!)
    // 35: Motion Vector, Disocclusion & Cel-Outline Risk Map (depth/normal silhouette jumps, edge-gate risk & actor borders)
    // 36: Thermal / Fire-Heat / Local-Light Energy & Specular Balance Inspector (R=fireHeat+warmIrrad, G=sootHeat, B=spec)
    if (flags.w == 34u) {
        const float3 flSafe = sanitizeLit3(finalLit);
        const float flLum = dot(flSafe, float3(0.2126f, 0.7152f, 0.0722f));
        const float flMax = max(flSafe.r, max(flSafe.g, flSafe.b));
        const float flMin = min(flSafe.r, min(flSafe.g, flSafe.b));
        const float flSat = (flMax - flMin) / max(flMax, 1.0e-4f);
        if (isWoodBark) {
            if (flLum < 0.015f) { return float4(1.0f, 0.04f, 0.04f, game.a); }   // RED alarm: crushed-black bark!
            return float4(float3(0.88f, 0.56f, 0.16f) * (0.45f + 0.55f * saturate(flLum * 3.0f)), game.a);
        }
        if (base == 5u || isGreenPineCard) {
            if (wet.snow < 0.18f && flSat < 0.12f && flLum > 0.42f) {
                return float4(1.0f, 0.0f, 1.0f, game.a);   // MAGENTA alarm: bleached chalky non-snow foliage!
            }
            const float3 floraC = (base == 5u) ? float3(0.12f, 0.80f, 0.24f) : float3(0.50f, 0.94f, 0.16f);
            return float4(floraC * (0.42f + 0.58f * saturate(flLum * 2.4f)), game.a);
        }
        return float4((flLum * 0.14f).xxx, game.a);
    }
    if (flags.w == 35u) {
        const float zSafe35 = max(z, 1.0f);
        const float zJump = (abs(ddx(zSafe35)) + abs(ddy(zSafe35))) / zSafe35;
        const float nJump = length(ddx(nSmooth)) + length(ddy(nSmooth));
        const float edgeRisk = smoothstep(0.022f, 0.095f, zJump);
        const float normRisk = smoothstep(0.22f, 0.75f, nJump);
        const bool isAct35 = (isActorFeat || isActorCloth);
        const float3 col35 = isAct35
            ? lerp(float3(0.10f, 0.36f, 0.85f), float3(0.18f, 1.00f, 0.96f), saturate(max(edgeRisk, normRisk * 0.7f)))
            : float3(saturate(edgeRisk * 1.15f), saturate(normRisk * 0.85f), 0.08f);
        return float4(col35, game.a);
    }
    if (flags.w == 36u) {
        const float warmEnergy = saturate(max(fireHeatDbg, localWarmIrradDbg * 0.85f));
        const float sootEnergy = saturate(sootHeatDbg * 1.4f);
        const float specEnergy = saturate(dot(max(spec, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f)) * 1.6f);
        return float4(warmEnergy, sootEnergy, specEnergy, game.a);
    }
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
    if (flags.w == 22u) {
        // View 22: Radiometric Bloom-Threshold & Energy-Conservation Audit:
        // Green = safe diffuse (0..1.20), Amber = HDR shoulder (1.20..1.48), Red = active Bloom trigger (>1.48), Magenta = NaN/Inf/neg!
        const bool bad = any(isnan(finalLit)) || any(isinf(finalLit)) || any(finalLit < -1.0e-4f);
        const float pk = max(finalLit.r, max(finalLit.g, finalLit.b));
        const float3 col22 = bad ? float3(1.0f, 0.0f, 1.0f)
                           : pk > 1.48f ? float3(1.0f, 0.12f, 0.08f)
                           : pk > 1.20f ? float3(1.0f, 0.72f, 0.10f)
                           : float3(0.10f, saturate(pk / 1.20f) * 0.85f + 0.10f, 0.18f);
        return float4(col22, game.a);
    }
    if (flags.w == 23u) {
        // View 23: WorldField Toroidal PDE & Snow/Wetness Continuity Audit (R=wave height, G=wet/foam, B=snowpack depth):
        const float2 simUv = frac((p.xy + camPosW.xy) / 2048.0f);
        const float4 wf = WorldField.SampleLevel(Lin, simUv, 0.0f);
        return float4(saturate(abs(wf.r) * 4.0f), saturate(wf.b), saturate(wf.a), game.a);
    }
    if (flags.w == 24u) {
        // View 24: 4-Quadrant Pipeline Continuity Split (TL: RtVis | TR: RtBounce GI | BL: RtRefl | BR: EnvCube IBL):
        const float2 q = float2(px) / max(dims.xy, float2(1.0f, 1.0f));
        if (abs(q.x - 0.5f) < 0.0012f || abs(q.y - 0.5f) < 0.0018f) { return float4(1.0f, 1.0f, 1.0f, game.a); }
        if (q.y < 0.5f) {
            return q.x < 0.5f ? float4(rv.x, rv.y, rv.z, game.a)
                              : float4(saturate(bounce * 3.0f), game.a);
        }
        return q.x < 0.5f ? float4(saturate(RtRefl.Load(ip).rgb), game.a)
                          : float4(saturate(sampleLiveEnvCube(reflect(-s.v, s.n), 0.12f)), game.a);
    }
    return float4(sanitizeLit3(finalLit), isfinite(game.a) ? saturate(game.a) : 1.0f);
}
