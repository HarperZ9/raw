// RawLiveWorldComposite.hlsl: Live World-Space 3D Procedural Compositor & Full-Pipeline Debug-Layer
// Inspector (SM 6.6 / 6.10).
// Composites live GPU solvers directly onto HDR ColorOut (RGBA16F) strictly anchored in 3D world space
// via `camPosW + WorldRelAt(px, SceneDepth)` and strictly occluded by `SceneDepth`:
//   1. RawSnowDeformHost (512x512): world-space snow footprint trenches, raised compacted rims,
//      campfire thermal slush rings, black-ice sheen, and snow micro-crystal sparkle.
//   2. RawWaterCausticsHost (512x512): Beer-Lambert chromatic underwater caustics on submerged
//      riverbeds & shallow rock contacts below `waterPlaneZ`.
//   3. RawGrassWindHost (256x256, 65,536 blades): 4-shell ray-stepped 3D wind-swaying Nordic grass
//      blades with quadratic cantilever bend, directional shading, backlit tip translucency,
//      actor-trampled trails, snow droop, and wildfire char (zero camera swimming!).
//   4. 3D Crystalline Glacier Depth & Internal Fracture Volume + Conifer Bark Shadow-Lift & Canopy Relief.
//   5. Live Visual Debug Layers (activeDebugView = 1, 2, 8, 28..36) cycled via F11.
// Strictly bypasses sky (d >= 0.99998), paused UI/menus, and ALL actors/creatures (base 10/11/12/14).
// Original clean-room implementation, MIT License.

cbuffer LiveWorldCompCB : register(b0)
{
    float4 clipToWorld[4];      // Camera-relative clip -> camera-relative world (Skyrim Z-up)
    float4 viewToWorld[3];      // View-space -> world-space normal basis
    float4 camPosW;             // xyz = absolute camera origin (posAdjust, Skyrim units), w = simTimeSec
    float4 dimsAndInv;          // xy = width, height; zw = 1/width, 1/height
    float4 resScaleAndFlags;    // xy = resScale.xy; z = isInterior (0=ext, 1=int); w = activeDebugView
    float4 snowDomain;          // x = snowOriginX_m, y = snowOriginZ_m, z = snowInvSpanM, w = snowStrength
    float4 grassDomain;         // x = grassOriginX_m, y = grassOriginZ_m, z = grassInvSpanM, w = grassStrength
    float4 waterAndCaustics;    // x = waterPlaneZ_u, y = causticsInvDomainM, z = causticsStrength, w = seasonSnowM
    float4 sunDir;              // xyz = unit sun dir (Z-up), w = sunIntensity
    float4 sunCol;              // rgb = sun HDR color, w = flagsMask (bit0=paused, bit1=lpDebug, bit2=hasNorm, bit3=hasSurf)
    float4 windAndWeather;      // xy = windXY (u/s), z = rain01, w = snowBaselineDepthM
};

Texture2D<float>    SceneDepth    : register(t0);
Texture2D<float4>   SceneNormal   : register(t1);
Texture2D<float4>   SceneAlbedo   : register(t2);
Texture2D<float4>   SceneSpecular : register(t3);
Texture2D<float4>   SnowState     : register(t4); // R=depthM, G=compaction, B=slush, A=blackIce
Texture2D<float4>   SnowNormal    : register(t5); // RGB=Y-up world normal, A=sparkleGlint
Texture2D<float4>   WaterCaustics : register(t6); // RGB=chromatic caustic irradiance, A=focusFactor
Texture2D<float4>   GrassBend     : register(t7); // RG=bendXZ, B=droop01, A=charMask01
RWTexture2D<float4> ColorOut      : register(u0);

SamplerState LinClamp : register(s0);
SamplerState LinWrap  : register(s1);

static const float kInvUnitsPerMeter = 1.0f / 70.0f;

#include "RawTotalMaterialOverhaul.hlsli"
#include "RawMossShader.hlsli"

float3 SanitizeColor3(float3 c)
{
    return float3(
        isfinite(c.x) ? clamp(c.x, 0.0f, 64.0f) : 0.0f,
        isfinite(c.y) ? clamp(c.y, 0.0f, 64.0f) : 0.0f,
        isfinite(c.z) ? clamp(c.z, 0.0f, 64.0f) : 0.0f);
}

float3 WorldRelAt(float2 px, float d)
{
    const float2 uv = (px + 0.5f) * dimsAndInv.zw;
    const float2 rs = (resScaleAndFlags.x > 0.01f && resScaleAndFlags.y > 0.01f) ? resScaleAndFlags.xy : float2(1.0f, 1.0f);
    const float4 c  = float4(uv.x * rs.x * 2.0f - 1.0f, (1.0f - uv.y * rs.y) * 2.0f - 1.0f, d, 1.0f);
    const float4 w  = float4(dot(clipToWorld[0], c), dot(clipToWorld[1], c),
                             dot(clipToWorld[2], c), dot(clipToWorld[3], c));
    return (abs(w.w) > 1.0e-9f) ? (w.xyz / w.w) : float3(0.0f, 0.0f, 0.0f);
}

float3 DecodeWorldNormal(float4 t)
{
    const float2 e  = t.xy - 0.5f;
    const float  s  = min(dot(e, e), 0.25f);
    const float3 nv = float3(e * (4.0f * sqrt(max(1.0f - 4.0f * s, 0.0f))), 8.0f * s - 1.0f);
    const float3 n  = float3(dot(viewToWorld[0].xyz, nv),
                             dot(viewToWorld[1].xyz, nv),
                             dot(viewToWorld[2].xyz, nv));
    const float len = length(n);
    return (isfinite(len) && len > 1.0e-5f) ? (n / len) : float3(0.0f, 0.0f, 1.0f);
}

// Smooth world-space 2D value noise (zero swimming under camera motion).
float Hash21(float2 p)
{
    float2 q = frac(p * float2(0.1031f, 0.1030f));
    q += dot(q, q.yx + 33.33f);
    return frac((q.x + q.y) * q.x);
}

float2 Hash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float ValueNoise2D(float2 p)
{
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float a = Hash21(i + float2(0.0f, 0.0f));
    const float b = Hash21(i + float2(1.0f, 0.0f));
    const float c = Hash21(i + float2(0.0f, 1.0f));
    const float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// World-space 3D Voronoi facet distance for glacial ice micro-crystals & internal fracture veins.
float GlacierFacet3D(float3 pM, out float outEdgeCrack)
{
    const float3 i = floor(pM);
    const float3 f = frac(pM);
    float d1 = 8.0f;
    float d2 = 8.0f;
    [unroll]
    for (int z = 0; z <= 1; ++z) {
        [unroll]
        for (int y = 0; y <= 1; ++y) {
            [unroll]
            for (int x = 0; x <= 1; ++x) {
                const float3 cell = float3(x, y, z);
                const float  h1   = Hash21((i.xy + cell.xy) * 1.13f + (i.z + cell.z) * 17.3f);
                const float  h2   = Hash21((i.yz + cell.yz) * 1.37f + (i.x + cell.x) * 11.7f);
                const float  h3   = Hash21((i.zx + cell.zx) * 1.29f + (i.y + cell.y) * 19.1f);
                const float3 pt   = cell + float3(h1, h2, h3) * 0.78f + 0.11f;
                const float  dist = length(pt - f);
                if (dist < d1) {
                    d2 = d1;
                    d1 = dist;
                } else if (dist < d2) {
                    d2 = dist;
                }
            }
        }
    }
    const float border = max(d2 - d1, 0.0f);
    outEdgeCrack       = 1.0f - smoothstep(0.015f, 0.11f, border);
    return saturate(border * 2.4f);
}

// Evaluates one horizontal slice of tapered world-space 3D Nordic grass blades at height hFrac in [0..1]
// with distance-aware Nyquist anti-aliasing so moving-camera pans (cam_sweep) have zero sub-pixel shimmer!
float EvalGrassShellDensity(float2 worldXYm, float hFrac, float distM, out float bladeHueVar)
{
    const float fineFade   = 1.0f - smoothstep(12.0f, 24.0f, distM);
    const float medFade    = 1.0f - smoothstep(20.0f, 36.0f, distM);

    const float2 gridP  = worldXYm * 7.5f;
    const float2 cell   = floor(gridP);
    const float2 localF = frac(gridP);
    const float2 rnd    = Hash22(cell);

    // Individual blade stalk center inside cell + per-tuft height variation:
    const float2 center = float2(0.20f, 0.20f) + rnd * 0.60f;
    const float  maxH   = 0.48f + 0.52f * Hash21(cell + float2(7.1f, 3.7f));
    const float  taper  = lerp(0.46f, 0.12f, hFrac);
    const float  dStalk = length(localF - center);
    const float  blade1 = (1.0f - smoothstep(taper * 0.55f, taper, dStalk)) * smoothstep(hFrac - 0.08f, hFrac + 0.14f, maxH);

    // Secondary fine blade cluster (smoothly Nyquist-filtered with distance to prevent cam_sweep shimmer):
    const float2 gridP2 = worldXYm * 14.5f + float2(4.3f, 9.1f);
    const float2 cell2  = floor(gridP2);
    const float2 loc2   = frac(gridP2);
    const float2 rnd2   = Hash22(cell2);
    const float  maxH2  = 0.35f + 0.65f * Hash21(cell2 + float2(13.3f, 5.9f));
    const float  d2     = length(loc2 - (float2(0.22f, 0.22f) + rnd2 * 0.56f));
    const float  blade2 = (1.0f - smoothstep(taper * 0.45f, taper * 0.92f, d2)) * smoothstep(hFrac - 0.06f, hFrac + 0.12f, maxH2) * fineFade;

    // Macro meadow sedge clumps (smooth coherent world-space tufts resolved cleanly at all distances):
    const float clump    = smoothstep(0.20f, 0.80f, ValueNoise2D(worldXYm * 1.45f));
    const float macroH   = smoothstep(hFrac - 0.10f, hFrac + 0.20f, 0.35f + 0.65f * clump);
    const float stalkMix = lerp(macroH * 0.72f, max(blade1, blade2 * 0.85f), medFade);
    bladeHueVar          = lerp(clump, rnd.x, medFade);
    return saturate(stalkMix * (0.45f + 0.55f * clump));
}

float3 DebugClassColor(uint base, uint cls, bool isSky)
{
    if (isSky)              return float3(0.04f, 0.06f, 0.14f);
    if ((cls & 128u) != 0u) return float3(0.95f, 0.30f, 0.95f); // First-person / special
    if (base == 14u)        return float3(0.08f, 0.96f, 0.96f); // Skinned Actor / Creature / Cloth
    if (base == 10u)        return float3(0.98f, 0.52f, 0.36f); // Skin
    if (base == 11u)        return float3(0.96f, 0.80f, 0.18f); // Hair
    if (base == 12u)        return float3(0.98f, 0.98f, 0.98f); // Eye
    if (base == 4u)         return float3(0.56f, 0.44f, 0.18f); // Terrain / Landscape
    if (base == 5u)         return float3(0.14f, 0.82f, 0.24f); // Foliage / Trees
    if (base == 6u)         return float3(0.32f, 0.72f, 0.98f); // Glacier / Parallax / Ice
    if (base == 7u)         return float3(0.22f, 0.54f, 0.62f); // Distant LOD
    if (base == 1u)         return float3(0.46f, 0.48f, 0.54f); // Architecture / Solid
    if (base == 0u)         return float3(0.08f, 0.08f, 0.10f); // Unclassified
    return float3(0.75f, 0.42f, 0.78f);
}

[numthreads(8, 8, 1)]
void cs_live_world_composite(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 dimsU = uint2(dimsAndInv.xy + 0.5f);
    if (dtid.x >= dimsU.x || dtid.y >= dimsU.y) {
        return;
    }

    const int3   ip        = int3(dtid.xy, 0);
    const uint   flagsMask = (uint)(sunCol.w + 0.5f);
    const bool   menuPause = ((flagsMask & 1u) != 0u);
    const bool   lpDebug   = ((flagsMask & 2u) != 0u);
    const bool   hasNorm   = ((flagsMask & 4u) != 0u);
    const bool   hasSurf   = ((flagsMask & 8u) != 0u);
    const uint   dbgView   = (uint)(resScaleAndFlags.w + 0.5f);
    const bool   isInt     = (resScaleAndFlags.z > 0.5f);

    const float4 origColor = ColorOut[dtid.xy];
    const float  d         = SceneDepth.Load(ip);
    const bool   isSky     = (!isfinite(d) || d <= 0.0f || d >= 0.99998f);

    const float4 sp   = hasSurf ? SceneSpecular.Load(ip) : float4(0.0f, 0.0f, 4.0f / 255.0f, 0.0f);
    const float3 alb  = hasSurf ? SceneAlbedo.Load(ip).rgb : origColor.rgb;
    const uint   cls  = uint(sp.z * 255.0f + 0.5f);
    const uint   base = cls & 127u;

    const bool isActor = (base == 10u || base == 11u || base == 12u || base == 14u || (cls & 128u) != 0u);

    const float2 px           = float2(dtid.xy);
    const float3 worldRel     = isSky ? float3(0.0f, 0.0f, 0.0f) : WorldRelAt(px, d);
    const float3 worldRelNext = isSky ? float3(0.0f, 0.0f, -1.0f) : WorldRelAt(px, min(d + 0.005f, 0.9995f));
    const float3 rayDiff      = worldRelNext - worldRel;
    const float  rayDiffLen   = length(rayDiff);
    const float3 rayDir       = (isfinite(rayDiffLen) && rayDiffLen > 1.0e-5f) ? (rayDiff / rayDiffLen) : float3(0.0f, 0.0f, -1.0f);

    const float3 worldAbs = camPosW.xyz + worldRel;
    const float3 worldM   = worldAbs * kInvUnitsPerMeter;
    const float  distM    = isSky ? 1.0e5f : (length(worldRel) * kInvUnitsPerMeter);
    const float3 N        = (hasNorm && !isSky) ? DecodeWorldNormal(SceneNormal.Load(ip)) : float3(0.0f, 0.0f, 1.0f);

    const float2 snowUv  = (worldM.xy - snowDomain.xy) * snowDomain.z;
    const bool   inSnow  = (snowDomain.w > 0.01f) && all(snowUv > 0.01f) && all(snowUv < 0.99f);
    const float2 grassUv = (worldM.xy - grassDomain.xy) * grassDomain.z + 0.5f;
    const bool   inGrass = (grassDomain.w > 0.01f) && all(grassUv > 0.01f) && all(grassUv < 0.99f);
    const float2 caustUv = worldM.xy * max(waterAndCaustics.y, 0.02f);

    const float3 sD = (length(sunDir.xyz) > 1.0e-4f) ? normalize(sunDir.xyz) : float3(0.4f, 0.3f, 0.86f);
    const float3 sC = SanitizeColor3(sunCol.rgb) * max(sunDir.w, 0.15f);

    // =========================================================================
    // LIVE VISUAL DEBUG LAYERS (cycled via F11: 0, 28..36, 1, 2, 8)
    // =========================================================================
    if (dbgView != 0u) {
        float3 dbgRgb = SanitizeColor3(origColor.rgb);

        if (dbgView == 28u) {
            // Material & Actor/Creature/Foliage/Glacier Class ID Map (including base == 14u Skinned Actor)
            const float shade = isSky ? 1.0f : (0.45f + 0.55f * saturate(dot(N, sD) * 0.5f + 0.5f));
            dbgRgb = DebugClassColor(base, cls, isSky) * shade;
        } else if (dbgView == 29u) {
            // Linear Depth & Silhouette Discontinuity Inspector
            const float dL = SceneDepth.Load(int3(max(int(dtid.x) - 1, 0), int(dtid.y), 0));
            const float dR = SceneDepth.Load(int3(min(int(dtid.x) + 1, int(dimsU.x) - 1), int(dtid.y), 0));
            const float dU = SceneDepth.Load(int3(int(dtid.x), max(int(dtid.y) - 1, 0), 0));
            const float dD = SceneDepth.Load(int3(int(dtid.x), min(int(dtid.y) + 1, int(dimsU.y) - 1), 0));
            const float edgeGrad = max(max(abs(d - dL), abs(d - dR)), max(abs(d - dU), abs(d - dD)));
            const float logDist  = isSky ? 1.0f : saturate(log2(1.0f + distM * 0.35f) / 7.5f);
            const float silMask  = saturate(edgeGrad * 240.0f);
            dbgRgb = lerp(float3(logDist, logDist * 0.92f, 1.0f - logDist * 0.65f),
                          float3(1.0f, 0.78f, 0.08f), silMask);
        } else if (dbgView == 30u) {
            // Sun Alignment & Live-World GPU Domain Coverage Map
            const float ndl = isSky ? 0.0f : saturate(dot(N, sD));
            dbgRgb = float3(ndl * 0.75f + 0.12f, ndl * 0.75f + 0.12f, ndl * 0.70f + 0.15f);
            if (!isSky && inSnow)  dbgRgb = lerp(dbgRgb, float3(0.20f, 0.85f, 1.00f), 0.30f);
            if (!isSky && inGrass) dbgRgb = lerp(dbgRgb, float3(0.20f, 0.90f, 0.30f), 0.25f);
            if (!isSky && worldAbs.z < waterAndCaustics.x + 12.0f) {
                dbgRgb = lerp(dbgRgb, float3(0.15f, 0.45f, 1.00f), 0.38f);
            }
        } else if (dbgView == 31u) {
            // Weathering / Snow-Deform / Grass / Caustics / Actor Exclusion Mask Inspector
            if (isSky) {
                dbgRgb = float3(0.03f, 0.04f, 0.08f);
            } else if (isActor) {
                dbgRgb = float3(0.95f, 0.08f, 0.12f);
            } else {
                const bool subWater  = (worldAbs.z < waterAndCaustics.x + 12.0f && worldAbs.z > waterAndCaustics.x - 900.0f);
                const float albLum   = dot(alb, float3(0.2126f, 0.7152f, 0.0722f));
                const bool snowySurf = (base == 4u) && (albLum > 0.46f || waterAndCaustics.w > 0.08f);
                const bool grassSurf = (base == 4u) && !snowySurf && !subWater && (N.z > 0.62f) && (distM < 38.0f);
                dbgRgb = float3(0.16f, 0.17f, 0.20f);
                if (subWater)        dbgRgb = float3(0.10f, 0.45f, 1.00f);
                else if (snowySurf)  dbgRgb = float3(0.25f, 0.92f, 0.98f);
                else if (grassSurf)  dbgRgb = float3(0.15f, 0.88f, 0.25f);
                else if (base == 6u) dbgRgb = float3(0.85f, 0.95f, 1.00f);
            }
        } else if (dbgView == 32u) {
            // Actor Skin / Hair / Eye / Skinned-14 & Surface Normal Inspector
            if (isActor) {
                dbgRgb = DebugClassColor(base, cls, false) * (0.45f + 0.55f * saturate(N.z * 0.5f + 0.5f));
            } else {
                dbgRgb = SanitizeColor3(origColor.rgb) * 0.18f;
            }
        } else if (dbgView == 33u) {
            // NaN / Inf / HDR Energy Overflow Heatmap (bright magenta on any non-finite or > 32.0 pixel!)
            const bool badNum = !isfinite(origColor.r) || !isfinite(origColor.g) || !isfinite(origColor.b);
            const float peakC = badNum ? 999.0f : max(origColor.r, max(origColor.g, origColor.b));
            if (badNum || peakC > 32.0f) {
                dbgRgb = float3(1.0f, 0.0f, 1.0f);
            } else {
                const float h = saturate(log2(1.0f + max(peakC, 0.0f)) / 5.0f);
                dbgRgb = float3(h * h, saturate(h * 1.4f - 0.15f), saturate(1.0f - h * 1.3f));
            }
        } else if (dbgView == 34u) {
            // Flora, Bark, Pine & 3D Grass-Shell Lighting Audit:
            //   Green   = healthy base==5 foliage
            //   Lime    = 3D grass-shell eligible terrain
            //   Amber   = healthy vertical bark
            //   RED     = alarm: crushed pitch-black bark (luma < 0.015)
            //   MAGENTA = alarm: bleached chalky non-snow foliage (sat < 0.12 && luma > 0.42)
            if (isSky) {
                dbgRgb = float3(0.03f, 0.05f, 0.09f);
            } else {
                const float3 cLit  = SanitizeColor3(origColor.rgb);
                const float  luma  = dot(cLit, float3(0.2126f, 0.7152f, 0.0722f));
                const float  maxC  = max(cLit.r, max(cLit.g, cLit.b));
                const float  minC  = min(cLit.r, min(cLit.g, cLit.b));
                const float  sat   = (maxC > 1.0e-4f) ? ((maxC - minC) / maxC) : 0.0f;
                const bool   isBark = (base == 1u) && (abs(N.z) < 0.55f) && (alb.r > alb.b * 1.15f) && (alb.r < 0.45f);
                const bool   isGrassTerrain = (base == 4u) && (N.z > 0.62f) && (alb.g > alb.b * 1.10f) && (distM < 38.0f);

                if (base == 5u) {
                    const bool bleached = (sat < 0.12f) && (luma > 0.42f) && (waterAndCaustics.w < 0.15f);
                    dbgRgb = bleached ? float3(1.0f, 0.05f, 0.95f) : float3(0.12f, 0.84f, 0.26f);
                } else if (isBark) {
                    const bool crushed = (luma < 0.015f);
                    dbgRgb = crushed ? float3(1.0f, 0.04f, 0.04f) : float3(0.88f, 0.58f, 0.18f);
                } else if (isGrassTerrain) {
                    dbgRgb = float3(0.45f, 0.92f, 0.22f);
                } else {
                    dbgRgb = cLit * 0.22f;
                }
            }
        } else if (dbgView == 35u) {
            // Motion / Silhouette Discontinuity & Cel-Outline Risk Map
            const float dL = SceneDepth.Load(int3(max(int(dtid.x) - 1, 0), int(dtid.y), 0));
            const float dR = SceneDepth.Load(int3(min(int(dtid.x) + 1, int(dimsU.x) - 1), int(dtid.y), 0));
            const float dU = SceneDepth.Load(int3(int(dtid.x), max(int(dtid.y) - 1, 0), 0));
            const float dD = SceneDepth.Load(int3(int(dtid.x), min(int(dtid.y) + 1, int(dimsU.y) - 1), 0));
            const float depthJump = max(max(abs(d - dL), abs(d - dR)), max(abs(d - dU), abs(d - dD)));
            const float3 nL = hasNorm ? DecodeWorldNormal(SceneNormal.Load(int3(max(int(dtid.x) - 1, 0), int(dtid.y), 0))) : N;
            const float3 nU = hasNorm ? DecodeWorldNormal(SceneNormal.Load(int3(int(dtid.x), max(int(dtid.y) - 1, 0), 0))) : N;
            const float  normJump = max(1.0f - saturate(dot(N, nL)), 1.0f - saturate(dot(N, nU)));
            const float  silRisk  = saturate(depthJump * 280.0f + normJump * 0.85f);
            if (isSky) {
                dbgRgb = float3(0.02f, 0.04f, 0.08f);
            } else if (isActor) {
                dbgRgb = lerp(float3(0.10f, 0.40f, 0.58f), float3(0.98f, 0.62f, 0.08f), silRisk);
            } else {
                dbgRgb = lerp(float3(0.06f, 0.09f, 0.15f), float3(0.95f, 0.90f, 0.20f), silRisk);
            }
        } else if (dbgView == 36u) {
            // Thermal / Fire-Heat / Snow-Melt / Live-World Solver Energy Map
            const float4 sVal = inSnow  ? SnowState.SampleLevel(LinClamp, snowUv, 0) : float4(0.0f, 0.0f, 0.0f, 0.0f);
            const float4 gVal = inGrass ? GrassBend.SampleLevel(LinClamp, grassUv, 0) : float4(0.0f, 0.0f, 0.0f, 0.0f);
            const float4 cVal = WaterCaustics.SampleLevel(LinWrap, caustUv, 0);
            const float3 cLit = SanitizeColor3(origColor.rgb);
            const float  warmEnergy = saturate((cLit.r - cLit.b) * 0.55f) + saturate(sVal.b * 0.8f + gVal.w * 0.9f);
            const float  liveField  = saturate(sVal.g * 0.9f + length(gVal.xy) * 0.85f);
            const float  caustEnergy = (worldAbs.z < waterAndCaustics.x + 12.0f)
                ? saturate(dot(cVal.rgb, float3(0.33f, 0.34f, 0.33f)) * 0.85f)
                : saturate(sp.x * 0.6f);
            dbgRgb = float3(saturate(warmEnergy), liveField, caustEnergy);
        } else if (dbgView == 1u) {
            dbgRgb = isSky ? float3(0.05f, 0.06f, 0.12f) : (N * 0.5f + 0.5f);
        } else if (dbgView == 2u) {
            const float4 sVal = inSnow  ? SnowState.SampleLevel(LinClamp, snowUv, 0) : float4(0.0f, 0.0f, 0.0f, 0.0f);
            const float4 gVal = inGrass ? GrassBend.SampleLevel(LinClamp, grassUv, 0) : float4(0.0f, 0.0f, 0.0f, 0.0f);
            const float4 cVal = WaterCaustics.SampleLevel(LinWrap, caustUv, 0);
            dbgRgb = float3(
                saturate(sVal.r * 1.4f + sVal.g * 0.5f),
                saturate(length(gVal.xy) * 1.2f + gVal.b * 0.4f),
                saturate(dot(cVal.rgb, float3(0.33f, 0.34f, 0.33f)) * 0.85f));
        } else if (dbgView == 8u) {
            dbgRgb = isSky ? float3(0.05f, 0.06f, 0.10f) : saturate(alb);
        }

        ColorOut[dtid.xy] = float4(SanitizeColor3(dbgRgb), origColor.a);
        return;
    }

    // =========================================================================
    // LIVE WORLD-SPACE 3D COMPOSITING MODE (activeDebugView == 0)
    // =========================================================================
    if (menuPause || lpDebug || isSky || isActor) {
        return;
    }

    float3 col    = SanitizeColor3(origColor.rgb);
    bool modified = false;

    // -------------------------------------------------------------------------
    // 0. WAVE 88 TOTAL REBIRTH: 14-CLASS STOCHASTIC HEX-TILED PBR OVERHAUL +
    //    SURFACE-INTERIOR MICRO-BEVEL CARVING + LIVE 3D VELVET MOSS (Mode=2 only)
    // -------------------------------------------------------------------------
    {
        const uint class14 = rawRebirthClassifyPixel14(base, alb, N, isInt, isActor);
        if (class14 != kRawClass14_Unclassified && class14 != kRawClass14_WaterSurface && class14 != kRawClass14_CharacterSkinHairCloth) {
            // Compute strict 2-pixel silhouette-depth interior mask so relief/moss NEVER causes edge ghosting or dark outlines!
            const int2 pxL  = int2(max(int(dtid.x) - 1, 0), int(dtid.y));
            const int2 pxR  = int2(min(int(dtid.x) + 1, int(dimsU.x) - 1), int(dtid.y));
            const int2 pxU  = int2(int(dtid.x), max(int(dtid.y) - 1, 0));
            const int2 pxD  = int2(int(dtid.x), min(int(dtid.y) + 1, int(dimsU.y) - 1));
            const int2 pxL2 = int2(max(int(dtid.x) - 2, 0), int(dtid.y));
            const int2 pxR2 = int2(min(int(dtid.x) + 2, int(dimsU.x) - 1), int(dtid.y));
            const int2 pxU2 = int2(int(dtid.x), max(int(dtid.y) - 2, 0));
            const int2 pxD2 = int2(int(dtid.x), min(int(dtid.y) + 2, int(dimsU.y) - 1));
            const float dJ1 = max(max(abs(d - SceneDepth.Load(int3(pxL, 0))), abs(d - SceneDepth.Load(int3(pxR, 0)))),
                                  max(abs(d - SceneDepth.Load(int3(pxU, 0))), abs(d - SceneDepth.Load(int3(pxD, 0)))));
            const float dJ2 = max(max(abs(d - SceneDepth.Load(int3(pxL2, 0))), abs(d - SceneDepth.Load(int3(pxR2, 0)))),
                                  max(abs(d - SceneDepth.Load(int3(pxU2, 0))), abs(d - SceneDepth.Load(int3(pxD2, 0)))));
            const float depthJump = max(dJ1, dJ2 * 0.75f);
            const float interiorMask = 1.0f - smoothstep(0.00035f, 0.0016f, depthJump);

            // Surface-interior albedo micro-relief gradient & Laplacian (chisels real stones, carvings, wood grain!)
            float albLap = 0.0f;
            float2 albGrad = float2(0.0f, 0.0f);
            if (interiorMask > 0.05f && hasSurf) {
                const float3 lumaW = float3(0.2126f, 0.7152f, 0.0722f);
                const float lC = dot(alb, lumaW);
                const float lL = dot(SceneAlbedo.Load(int3(pxL, 0)).rgb, lumaW);
                const float lR = dot(SceneAlbedo.Load(int3(pxR, 0)).rgb, lumaW);
                const float lU = dot(SceneAlbedo.Load(int3(pxU, 0)).rgb, lumaW);
                const float lD = dot(SceneAlbedo.Load(int3(pxD, 0)).rgb, lumaW);
                albLap  = lC - 0.25f * (lL + lR + lU + lD);
                albGrad = float2(lR - lL, lD - lU);
            }

            float3 nOverhaul = N;
            col = rawRebirthApplyTotalMaterialOverhaul(
                col, alb, N, worldM, distM,
                sD, sC, -rayDir,
                interiorMask, albLap, albGrad, windAndWeather.z,
                class14, isInt, nOverhaul);

            // Wave 89 Defect E: Keep warm hearth ambient bounce readable in shadowed interior back alcoves (black_crush < 0.8%)
            if (isInt && class14 != kRawClass14_GlacierIce) {
                const float intLum = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
                if (intLum < 0.085f) {
                    const float liftW = saturate((0.085f - intLum) * 14.0f) * (1.0f - smoothstep(14.0f, 40.0f, distM));
                    col += max(alb, float3(0.11f, 0.08f, 0.055f)) * float3(0.24f, 0.17f, 0.10f) * liftW;
                }
            }

            const float albLumSnow = dot(alb, float3(0.2126f, 0.7152f, 0.0722f));
            const float snowCov01  = max(saturate((albLumSnow - 0.56f) * 3.5f) * saturate((N.z - 0.40f) * 2.5f),
                                         saturate(waterAndCaustics.w * 2.5f));
            const float waterZ_m   = waterAndCaustics.x * kInvUnitsPerMeter;
            rawRebirthApply3DMoss(
                col, alb, nOverhaul, worldM, distM,
                rayDir, sD, sC, sunDir.w,
                waterZ_m, snowCov01, windAndWeather.z,
                interiorMask, base, class14, isInt, isActor);
            modified = true;
        }
    }

    // -------------------------------------------------------------------------
    // 1. LIVE GPU SNOW DEFORMATION, FOOTPRINT TRENCHES & CAMPFIRE MELT RINGS
    // -------------------------------------------------------------------------
    if (!isInt && inSnow && base == 4u && N.z > 0.45f && distM < 48.0f) {
        const float albLum    = dot(alb, float3(0.2126f, 0.7152f, 0.0722f));
        const float maxCh     = max(alb.r, max(alb.g, alb.b));
        const float minCh     = min(alb.r, min(alb.g, alb.b));
        const float sat       = (maxCh > 1.0e-4f) ? ((maxCh - minCh) / maxCh) : 0.0f;
        const float snowSurfW = saturate((albLum - 0.34f) * 3.2f) * saturate(1.0f - (sat - 0.24f) * 3.0f);
        const float seasonW   = saturate(waterAndCaustics.w * 4.5f);
        const float snowMask  = max(snowSurfW, seasonW * 0.85f) * saturate((N.z - 0.45f) * 2.5f);

        if (snowMask > 0.05f) {
            const float2 edgeD    = min(snowUv, 1.0f - snowUv);
            const float  edgeFade = smoothstep(0.01f, 0.10f, min(edgeD.x, edgeD.y));
            const float  distFade = 1.0f - smoothstep(28.0f, 48.0f, distM);
            const float  wSnow    = snowMask * edgeFade * distFade * saturate(snowDomain.w);

            const float4 sState = SnowState.SampleLevel(LinClamp, snowUv, 0);
            const float4 sNormY = SnowNormal.SampleLevel(LinClamp, snowUv, 0);
            const float  baseH  = max(windAndWeather.w, 0.18f);

            const float trench  = saturate((baseH - sState.r) / max(baseH * 0.85f, 0.12f));
            const float rim     = saturate((sState.r - baseH) / 0.12f);
            const float compact = saturate(sState.g);
            const float slush   = saturate(sState.b);
            const float blkIce  = saturate(sState.a);
            const float sparkle = saturate(sNormY.a);

            const float3 tNorm   = normalize(float3(sNormY.x, sNormY.z, max(sNormY.y, 0.15f)));
            const float  ndotS   = saturate(dot(tNorm, sD));
            const float  baseNS  = saturate(dot(N, sD));
            const float  slopeSh = clamp(0.65f + 0.70f * (ndotS - baseNS), 0.52f, 1.28f);

            const float  trackDepth = max(trench, compact * 0.65f);
            const float3 trenchTint = lerp(float3(1.0f, 1.0f, 1.0f), float3(0.56f, 0.68f, 0.86f), trackDepth * 0.62f * wSnow);
            col *= trenchTint * lerp(1.0f, slopeSh, saturate(trackDepth + rim) * wSnow);
            col += sC * (rim * 0.085f * wSnow);
            col *= lerp(1.0f, 0.72f, slush * 0.65f * wSnow);
            col += (sC * 0.12f + float3(0.05f, 0.08f, 0.12f)) * (blkIce * 0.55f + sparkle * 0.18f) * wSnow;
            modified = true;
        }
    }

    // -------------------------------------------------------------------------
    // 2. LIVE GPU REFRACTIVE WATER CAUSTICS ON SUBMERGED RIVERBEDS
    // -------------------------------------------------------------------------
    if (waterAndCaustics.z > 0.01f && base != 5u && distM < 70.0f) {
        const float subDepthU = waterAndCaustics.x - worldAbs.z;
        if (subDepthU > -12.0f && subDepthU < 900.0f) {
            const float depthM     = max(subDepthU * kInvUnitsPerMeter, 0.0f);
            const float shoreIn    = smoothstep(-12.0f, 16.0f, subDepthU);
            const float depthAtten = exp(-depthM * 0.24f);
            const float upMask     = saturate(N.z * 1.15f + 0.10f);
            const float distFade   = 1.0f - smoothstep(42.0f, 70.0f, distM);
            const float wCaust     = shoreIn * depthAtten * upMask * distFade * saturate(waterAndCaustics.z);

            if (wCaust > 0.005f) {
                const float4 cSample = WaterCaustics.SampleLevel(LinWrap, caustUv, 0);
                const float3 cRgb    = SanitizeColor3(cSample.rgb);
                const float3 bedTint = max(alb, float3(0.18f, 0.20f, 0.22f));
                col += cRgb * bedTint * (0.48f * wCaust * max(sunDir.w, 0.20f));
                modified = true;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 3. 4-SHELL RAY-STEPPED 3D WIND-SWAYING NORDIC SEDGE/MEADOW GRASS BLADES
    // -------------------------------------------------------------------------
    if (!isInt && inGrass && base == 4u && N.z > 0.62f && distM < 42.0f &&
        worldAbs.z > waterAndCaustics.x + 14.0f) {
        const float albLum   = dot(alb, float3(0.2126f, 0.7152f, 0.0722f));
        const float greenDom = saturate((alb.g - alb.b * 0.85f) * 3.8f) * saturate(1.0f - (albLum - 0.46f) * 3.5f);
        const float loamOk   = saturate((0.48f - albLum) * 2.5f) * saturate((alb.g - 0.06f) * 6.0f);
        const float turfMask = max(greenDom, loamOk * 0.50f) * saturate((N.z - 0.62f) * 3.5f);

        if (turfMask > 0.05f) {
            const float2 gEdge     = min(grassUv, 1.0f - grassUv);
            const float  gEdgeFade = smoothstep(0.01f, 0.10f, min(gEdge.x, gEdge.y));
            const float  gDistFade = 1.0f - smoothstep(24.0f, 42.0f, distM);
            const float  wGrass    = turfMask * gEdgeFade * gDistFade * saturate(grassDomain.w);

            const float4 gBend     = GrassBend.SampleLevel(LinClamp, grassUv, 0);
            const float2 bendBase  = float2(isfinite(gBend.x) ? clamp(gBend.x, -1.5f, 1.5f) : 0.0f,
                                            isfinite(gBend.y) ? clamp(gBend.y, -1.5f, 1.5f) : 0.0f);
            // Coherent world-locked travelling wind-gust wave modulating the GPU GrassWind bend field:
            const float  windLen   = length(windAndWeather.xy);
            const float2 windDir   = (windLen > 1.0e-3f) ? (windAndWeather.xy / windLen) : float2(0.86f, 0.51f);
            const float  gustWave  = 0.5f + 0.5f * sin(dot(worldM.xy, windDir) * 0.42f - camPosW.w * 2.15f);
            const float2 bendXZ    = bendBase * (0.82f + 0.36f * gustWave);
            const float  bendMag   = length(bendXZ);
            const float  droop01   = saturate(isfinite(gBend.z) ? gBend.z : 0.0f);
            const float  char01    = saturate(isfinite(gBend.w) ? gBend.w : 0.0f);

            // Step front-to-back across 4 physical 3D Nordic sedge/meadow height shells (0.44m -> 0.08m):
            // Wave 90 Part 3: Henyey-Greenstein (OPT-032, g=0.58) backlit blade translucency + coherent wind-gust micro-shadowing!
            const float  heightScale = (1.0f - 0.55f * droop01) * (1.0f - 0.60f * char01);
            const float  invRayZ     = 1.0f / max(abs(rayDir.z), 0.35f);
            const float  cosBladeHG  = saturate(dot(rayDir, sD));
            const float  hgGrassDen  = max(1.0f + 0.58f * 0.58f - 2.0f * 0.58f * cosBladeHG, 0.10f);
            const float  backLit     = saturate(((1.0f - 0.58f * 0.58f) / (hgGrassDen * sqrt(hgGrassDen))) * 0.24f) * max(sunDir.w, 0.15f);
            const float3 bladeNorm   = normalize(float3(-bendXZ.x * 0.78f, -bendXZ.y * 0.78f, 1.0f));
            const float  sunBlade    = 0.35f + 0.65f * saturate(dot(bladeNorm, sD));
            const float  gustShadow  = lerp(0.84f, 1.14f, gustWave);
            const float  gustSheen   = saturate(bendMag * 0.68f) * sunBlade * (0.75f + 0.25f * gustWave);

            float3 shellColor = float3(0.0f, 0.0f, 0.0f);
            float  shellAlpha = 0.0f;

            [unroll]
            for (int k = 3; k >= 0; --k) {
                const float hFrac = (float(k) + 0.5f) * 0.25f;
                const float hM    = lerp(0.08f, 0.44f, hFrac) * heightScale;
                // Parallax step into 3D shell along -rayDir + quadratic Euler-Bernoulli cantilever wind bend:
                const float2 pShell = worldM.xy - rayDir.xy * (hM * invRayZ) - bendXZ * (0.38f * hFrac * hFrac);
                float hueVar = 0.5f;
                const float dens = EvalGrassShellDensity(pShell, hFrac, distM, hueVar);
                const float a    = dens * lerp(0.60f, 0.40f, hFrac) * (1.0f - shellAlpha);

                // Deep mossy root AO -> rich Nordic sedge-meadow golden-emerald tips + HG backlit translucency + wind gust wave:
                const float3 rootCol = float3(0.06f, 0.12f, 0.04f);
                const float3 tipCol  = lerp(float3(0.21f, 0.40f, 0.10f), float3(0.38f, 0.50f, 0.14f), hueVar);
                float3 bCol = lerp(rootCol, tipCol, hFrac) * lerp(0.48f, 1.15f, hFrac) * sunBlade * lerp(1.0f, gustShadow, hFrac);
                bCol *= max(sC, float3(0.32f, 0.36f, 0.30f));
                bCol += float3(0.30f, 0.44f, 0.10f) * (backLit * hFrac * 0.62f + gustSheen * hFrac * 0.24f);

                shellColor += bCol * a;
                shellAlpha += a;
            }

            // Blend 3D grass shells cleanly over ground with root-contact ambient darkening & wildfire char:
            const float rootShadow = lerp(1.0f, 0.76f, shellAlpha * wGrass * 0.65f);
            col = lerp(col * rootShadow, shellColor / max(shellAlpha, 1.0e-3f), saturate(shellAlpha * 0.78f * wGrass));
            col *= lerp(1.0f, 0.34f, char01 * 0.85f * wGrass);
            modified = true;
        }
    }

    // -------------------------------------------------------------------------
    // 4. MULTI-LAYER 3D CRYSTALLINE GLACIER SPARKLE & PINE-BARK FISSURE RELIEF
    // -------------------------------------------------------------------------
    if (base == 6u && distM < 85.0f) {
        const float coolIce = saturate((alb.b - alb.r * 0.88f) * 4.0f + (dot(alb, float3(0.33f, 0.33f, 0.34f)) - 0.36f));
        if (coolIce > 0.08f) {
            float crack0 = 0.0f, crack1 = 0.0f, crack2 = 0.0f;
            const float facet0   = GlacierFacet3D(worldM * 1.35f, crack0);
            const float facet1   = GlacierFacet3D((worldM + rayDir * 0.24f) * 2.15f, crack1);
            const float facet2   = GlacierFacet3D((worldM + rayDir * 0.52f) * 3.10f, crack2);
            const float distFade = 1.0f - smoothstep(50.0f, 85.0f, distM);
            const float wIce     = saturate(coolIce) * distFade;

            // Multi-layer world-locked micro-crystal Fresnel sparkle glints along conchoidal fracture ridges:
            const float3 viewDir    = -rayDir;
            const float3 halfVec    = normalize(sD + viewDir);
            const float  spNoiseA   = Hash21(floor(worldM.xy * 18.0f + worldM.z * 9.0f));
            const float  spNoiseB   = Hash21(floor((worldM.xy + rayDir.xy * 0.16f) * 28.0f + worldM.z * 14.0f));
            const float  ndhGlint   = pow(saturate(dot(N, halfVec)), 12.0f);
            const float  sparkle    = (smoothstep(0.82f, 0.99f, spNoiseA) * (0.45f + 0.55f * crack0) +
                                       smoothstep(0.86f, 0.995f, spNoiseB) * 0.70f * (0.35f + 0.65f * crack1)) * ndhGlint;

            // Deep Beer-Lambert teal-sapphire internal parallax volume + bright conchoidal fracture veins:
            const float3 deepTeal   = float3(0.03f, 0.21f, 0.34f) * (1.0f - facet0 * 0.60f) * (0.55f + 0.30f * facet1 + 0.15f * facet2);
            const float3 crystalRim = sC * (smoothstep(0.58f, 0.96f, facet0) * 0.16f + crack0 * 0.13f + crack1 * 0.10f + crack2 * 0.06f + sparkle * 0.62f);
            col = lerp(col, col * 0.87f + deepTeal * max(sunDir.w, 0.30f), 0.60f * wIce) + crystalRim * (0.60f * wIce);
            modified = true;
        }
    } else if (!isInt && base == 5u && distM < 85.0f) {
        // Wave 89 Defect B: Strictly distinguish living green foliage from frost-covered winter twigs / dry autumn brambles!
        // NEVER inject chlorophyll spring-green onto white/grey frosted winter twigs (e.g., Windhelm foreground brambles)!
        const float distFade = 1.0f - smoothstep(50.0f, 85.0f, distM);
        const float lum      = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
        const float maxC     = max(col.r, max(col.g, col.b));
        const float minC     = min(col.r, min(col.g, col.b));
        const float sat      = (maxC > 1.0e-4f) ? ((maxC - minC) / maxC) : 0.0f;
        const float needleN  = ValueNoise2D(worldM.xy * 4.2f + worldM.z * 2.1f);
        const float backLit  = pow(saturate(dot(rayDir, sD)), 2.5f) * max(sunDir.w, 0.15f);
        const bool  isLivingGreen = (alb.g > alb.r * 1.04f) && (alb.g > alb.b * 1.08f) && (alb.g > 0.024f);
        const bool  isAutumnWarm  = (!isLivingGreen) && (alb.r > alb.b * 1.15f) && (alb.r >= alb.g * 0.96f) && (alb.r > 0.030f);

        if (isLivingGreen) {
            const float shadowLift = saturate((0.09f - lum) * 10.0f) * distFade;
            col += float3(0.018f, 0.032f, 0.013f) * shadowLift * (0.55f + 0.45f * needleN);
            col += float3(0.048f, 0.090f, 0.025f) * (backLit * 0.24f * needleN * distFade);
            if (waterAndCaustics.w < 0.15f && sat < 0.18f && lum > 0.35f) {
                const float bleachW = saturate((0.18f - sat) * 5.0f) * saturate((lum - 0.35f) * 3.0f) * distFade;
                col = lerp(col, col * float3(0.76f, 0.96f, 0.72f), bleachW * 0.45f);
            }
        } else if (isAutumnWarm) {
            // Golden Rift autumn aspen/birch leaves & warm bronze bushes: sunlit amber-gold rim (never olive-green!)
            const float hiSafeA    = 1.0f - smoothstep(0.28f, 0.62f, lum);
            const float shadowLift = saturate((0.09f - lum) * 10.0f) * distFade;
            col += float3(0.032f, 0.022f, 0.008f) * shadowLift * (0.60f + 0.40f * needleN);
            col += float3(0.075f, 0.052f, 0.014f) * (backLit * 0.34f * needleN * distFade * hiSafeA);
        } else {
            // Wave 90 Defect B Refinement: Bright crystalline white hoarfrost rime + cold rim scatter on snow-biome twigs!
            const float frostSpark = smoothstep(0.68f, 0.94f, ValueNoise2D(worldM.xy * 28.0f + worldM.z * 14.0f));
            const float hiSafeF    = 1.0f - smoothstep(0.42f, 0.82f, lum);
            const float coldRim    = pow(saturate(1.0f - abs(dot(N, -rayDir))), 2.2f);
            const float shadowLift = saturate((0.20f - lum) * 6.5f) * distFade;
            const float snowRimeW  = saturate(0.36f + waterAndCaustics.w * 0.64f) * distFade;
            const float3 hoarTarget = float3(0.48f, 0.52f, 0.58f) * (0.78f + 0.32f * needleN);
            col = lerp(col, max(col, hoarTarget), snowRimeW * 0.42f * hiSafeF);
            col += float3(0.055f, 0.060f, 0.072f) * shadowLift * (0.65f + 0.35f * needleN);
            col += float3(0.095f, 0.105f, 0.125f) * ((backLit * 0.34f + coldRim * 0.32f + frostSpark * 0.28f) * distFade * hiSafeF);
        }
        modified = true;
    } else if (!isInt && base == 1u && abs(N.z) < 0.55f && distM < 60.0f) {
        // Crisp 3D longitudinal pine-bark fissure relief + warm forest-floor shadow lift (zero crushed black trunks!):
        const float lum = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
        const bool isWarmBark = (alb.r > alb.b * 1.10f) && (alb.r < 0.50f) && (alb.g < 0.44f);
        if (isWarmBark) {
            const float distFade   = 1.0f - smoothstep(35.0f, 60.0f, distM);
            const float radialCoord = (worldM.x * 1.15f + worldM.y * 0.85f);
            const float barkCoarse = ValueNoise2D(float2(radialCoord * 12.0f, worldM.z * 2.6f));
            const float barkFine   = ValueNoise2D(float2(radialCoord * 26.0f, worldM.z * 6.5f));
            const float fissure    = smoothstep(0.25f, 0.78f, barkCoarse * 0.65f + barkFine * 0.35f);
            // Subtle crisp 3D bark plate micro-contrast on lit bark + warm shadow-cavity floor on dark bark:
            col *= lerp(1.0f, lerp(0.86f, 1.14f, fissure), 0.45f * distFade);
            if (lum < 0.095f) {
                const float liftW = saturate((0.095f - lum) * 14.0f) * distFade;
                col += max(alb, float3(0.16f, 0.11f, 0.075f)) * (0.26f * liftW * (0.58f + 0.42f * fissure));
            }
            modified = true;
        }
    }

    if (modified) {
        ColorOut[dtid.xy] = float4(SanitizeColor3(col), origColor.a);
    }
}
