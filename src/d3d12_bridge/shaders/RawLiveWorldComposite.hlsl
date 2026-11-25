// RawLiveWorldComposite.hlsl: Live World-Space GPU Compositor & Debug-Layer Inspector (SM 6.6 / 6.10).
// Composites live GPU solvers directly onto HDR ColorOut (RGBA16F) strictly anchored in 3D world space
// via `camPosW + worldAt(px, SceneDepth)` and strictly occluded by `SceneDepth`:
//   1. RawSnowDeformHost (512x512): world-space snow footprint trenches, raised compacted rims,
//      campfire thermal slush rings, black-ice sheen, and snow micro-crystal sparkle.
//   2. RawWaterCausticsHost (512x512): Beer-Lambert chromatic underwater caustics on submerged
//      riverbeds & shallow rock contacts below `waterPlaneZ`.
//   3. RawGrassWindHost (256x256, 65,536 blades): world-anchored wind-swaying Nordic grass micro-tufts,
//      travelling gust sheen ripples, actor-trampled trails, snow droop, and wildfire char.
//   4. World-space glacier crystalline facet volume & conifer canopy micro-depth lift.
//   5. Live Visual Debug Layers (activeDebugView = 1, 2, 8, 28, 29, 30, 31, 32, 33) cycled via F11.
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

// World-space 3D Voronoi facet distance for glacial ice micro-crystals.
float GlacierFacet3D(float3 pM)
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
    return saturate((d2 - d1) * 2.4f);
}

float3 DebugClassColor(uint base, uint cls, bool isSky)
{
    if (isSky)          return float3(0.04f, 0.06f, 0.14f);
    if ((cls & 128u) != 0u) return float3(0.95f, 0.30f, 0.95f); // First-person / special
    if (base == 14u)    return float3(0.08f, 0.96f, 0.96f); // Skinned Actor / Creature / Cloth (Wave 63!)
    if (base == 10u)    return float3(0.98f, 0.52f, 0.36f); // Skin
    if (base == 11u)    return float3(0.96f, 0.80f, 0.18f); // Hair
    if (base == 12u)    return float3(0.98f, 0.98f, 0.98f); // Eye
    if (base == 4u)     return float3(0.56f, 0.44f, 0.18f); // Terrain / Landscape
    if (base == 5u)     return float3(0.14f, 0.82f, 0.24f); // Foliage / Trees
    if (base == 6u)     return float3(0.32f, 0.72f, 0.98f); // Glacier / Parallax / Ice
    if (base == 7u)     return float3(0.22f, 0.54f, 0.62f); // Distant LOD
    if (base == 1u)     return float3(0.46f, 0.48f, 0.54f); // Architecture / Solid
    if (base == 0u)     return float3(0.08f, 0.08f, 0.10f); // Unclassified
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

    const float2 px       = float2(dtid.xy);
    const float3 worldRel = isSky ? float3(0.0f, 0.0f, 0.0f) : WorldRelAt(px, d);
    const float3 worldAbs = camPosW.xyz + worldRel;
    const float3 worldM   = worldAbs * kInvUnitsPerMeter;
    const float  distM    = isSky ? 1.0e5f : (length(worldRel) * kInvUnitsPerMeter);
    const float3 N        = (hasNorm && !isSky) ? DecodeWorldNormal(SceneNormal.Load(ip)) : float3(0.0f, 0.0f, 1.0f);

    // Precompute world-space UVs for our 3 live GPU solver domains:
    const float2 snowUv  = (worldM.xy - snowDomain.xy) * snowDomain.z;
    const bool   inSnow  = (snowDomain.w > 0.01f) && all(snowUv > 0.01f) && all(snowUv < 0.99f);
    const float2 grassUv = (worldM.xy - grassDomain.xy) * grassDomain.z + 0.5f;
    const bool   inGrass = (grassDomain.w > 0.01f) && all(grassUv > 0.01f) && all(grassUv < 0.99f);
    const float2 caustUv = worldM.xy * max(waterAndCaustics.y, 0.02f);

    // =========================================================================
    // LIVE VISUAL DEBUG LAYERS (cycled via F11: 0, 28, 29, 30, 31, 32, 33, 1, 2, 8)
    // =========================================================================
    if (dbgView != 0u) {
        float3 dbgRgb = SanitizeColor3(origColor.rgb);

        if (dbgView == 28u) {
            // Material & Actor/Creature/Foliage/Glacier Class ID Map (including base == 14u Skinned Actor)
            const float shade = isSky ? 1.0f : (0.45f + 0.55f * saturate(dot(N, normalize(sunDir.xyz)) * 0.5f + 0.5f));
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
            const float ndl = isSky ? 0.0f : saturate(dot(N, normalize(sunDir.xyz)));
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
                // Bright crimson proves actors/dogs/NPCs are 100% protected from moss/snow/grass!
                dbgRgb = float3(0.95f, 0.08f, 0.12f);
            } else {
                const bool subWater  = (worldAbs.z < waterAndCaustics.x + 12.0f && worldAbs.z > waterAndCaustics.x - 900.0f);
                const float albLum   = dot(alb, float3(0.2126f, 0.7152f, 0.0722f));
                const bool snowySurf = (base == 4u) && (albLum > 0.46f || waterAndCaustics.w > 0.08f);
                const bool grassSurf = (base == 4u) && !snowySurf && !subWater && (N.z > 0.62f) && (distM < 45.0f);
                dbgRgb = float3(0.16f, 0.17f, 0.20f);
                if (subWater)       dbgRgb = float3(0.10f, 0.45f, 1.00f);
                else if (snowySurf) dbgRgb = float3(0.25f, 0.92f, 0.98f);
                else if (grassSurf) dbgRgb = float3(0.15f, 0.88f, 0.25f);
                else if (base == 6u) dbgRgb = float3(0.85f, 0.95f, 1.00f);
            }
        } else if (dbgView == 32u) {
            // Actor Skin / Hair / Eye / Skinned-14 & Surface Normal/Spec Inspector
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
        } else if (dbgView == 1u) {
            // World-Space Surface Normals
            dbgRgb = isSky ? float3(0.05f, 0.06f, 0.12f) : (N * 0.5f + 0.5f);
        } else if (dbgView == 2u) {
            // Raw Live GPU Field Tri-Channel Inspector (R=SnowDeform, G=GrassWindBend, B=Caustics)
            const float4 sVal = inSnow  ? SnowState.SampleLevel(LinClamp, snowUv, 0) : float4(0.0f, 0.0f, 0.0f, 0.0f);
            const float4 gVal = inGrass ? GrassBend.SampleLevel(LinClamp, grassUv, 0) : float4(0.0f, 0.0f, 0.0f, 0.0f);
            const float4 cVal = WaterCaustics.SampleLevel(LinWrap, caustUv, 0);
            dbgRgb = float3(
                saturate(sVal.r * 1.4f + sVal.g * 0.5f),
                saturate(length(gVal.xy) * 1.2f + gVal.b * 0.4f),
                saturate(dot(cVal.rgb, float3(0.33f, 0.34f, 0.33f)) * 0.85f));
        } else if (dbgView == 8u) {
            // Raw Albedo Inspector
            dbgRgb = isSky ? float3(0.05f, 0.06f, 0.10f) : saturate(alb);
        }

        ColorOut[dtid.xy] = float4(SanitizeColor3(dbgRgb), origColor.a);
        return;
    }

    // =========================================================================
    // LIVE WORLD-SPACE COMPOSITING MODE (activeDebugView == 0)
    // =========================================================================
    if (menuPause || lpDebug || isSky || isActor) {
        return;
    }

    float3 col      = SanitizeColor3(origColor.rgb);
    const float3 sD = (length(sunDir.xyz) > 1.0e-4f) ? normalize(sunDir.xyz) : float3(0.4f, 0.3f, 0.86f);
    const float3 sC = SanitizeColor3(sunCol.rgb) * max(sunDir.w, 0.15f);
    bool modified   = false;

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

            // Footprint depression trench vs compacted rim height differential:
            const float trench  = saturate((baseH - sState.r) / max(baseH * 0.85f, 0.12f));
            const float rim     = saturate((sState.r - baseH) / 0.12f);
            const float compact = saturate(sState.g);
            const float slush   = saturate(sState.b);
            const float blkIce  = saturate(sState.a);
            const float sparkle = saturate(sNormY.a);

            // Convert SnowNormal (Y-up) into Skyrim Z-up for directional trench wall shading:
            const float3 tNorm  = normalize(float3(sNormY.x, sNormY.z, max(sNormY.y, 0.15f)));
            const float  ndotS  = saturate(dot(tNorm, sD));
            const float  baseNS = saturate(dot(N, sD));
            const float  slopeSh = clamp(0.65f + 0.70f * (ndotS - baseNS), 0.52f, 1.28f);

            // Deepen shadowed boot/NPC snow trenches with cool glacial shadow tint + rim catchlight:
            const float  trackDepth = max(trench, compact * 0.65f);
            const float3 trenchTint = lerp(float3(1.0f, 1.0f, 1.0f), float3(0.56f, 0.68f, 0.86f), trackDepth * 0.62f * wSnow);
            col *= trenchTint * lerp(1.0f, slopeSh, saturate(trackDepth + rim) * wSnow);

            // Raised rim sunlit micro-lip:
            col += sC * (rim * 0.085f * wSnow);

            // Campfire/thermal slush wetting darkens snow cleanly; black ice adds crisp specular glint:
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
                // Physically scaled chromatic caustic bounce modulated by bed albedo & sunlight:
                const float3 bedTint = max(alb, float3(0.18f, 0.20f, 0.22f));
                col += cRgb * bedTint * (0.48f * wCaust * max(sunDir.w, 0.20f));
                modified = true;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 3. WORLD-ANCHORED 3D WIND-SWAYING NORDIC GRASS MICRO-TUFTS (RawGrassWindHost)
    // -------------------------------------------------------------------------
    if (!isInt && inGrass && base == 4u && N.z > 0.62f && distM < 45.0f &&
        worldAbs.z > waterAndCaustics.x + 14.0f) {
        const float albLum   = dot(alb, float3(0.2126f, 0.7152f, 0.0722f));
        const float greenDom = saturate((alb.g - alb.b * 0.85f) * 3.8f) * saturate(1.0f - (albLum - 0.45f) * 3.5f);
        const float loamOk   = saturate((0.50f - albLum) * 2.5f) * saturate((alb.g - 0.06f) * 6.0f);
        const float turfMask = max(greenDom, loamOk * 0.55f) * saturate((N.z - 0.62f) * 3.5f);

        if (turfMask > 0.05f) {
            const float2 gEdge     = min(grassUv, 1.0f - grassUv);
            const float  gEdgeFade = smoothstep(0.01f, 0.10f, min(gEdge.x, gEdge.y));
            const float  gDistFade = 1.0f - smoothstep(26.0f, 45.0f, distM);
            const float  wGrass    = turfMask * gEdgeFade * gDistFade * saturate(grassDomain.w);

            const float4 gBend     = GrassBend.SampleLevel(LinClamp, grassUv, 0);
            const float2 bendXZ    = float2(isfinite(gBend.x) ? clamp(gBend.x, -1.5f, 1.5f) : 0.0f,
                                            isfinite(gBend.y) ? clamp(gBend.y, -1.5f, 1.5f) : 0.0f);
            const float  bendMag   = length(bendXZ);
            const float  droop01   = saturate(isfinite(gBend.z) ? gBend.z : 0.0f);
            const float  char01    = saturate(isfinite(gBend.w) ? gBend.w : 0.0f);

            // World-anchored multi-octave blade tuft pattern advected by real GPU wind/trample bend:
            const float2 pTuft     = worldM.xy * 5.2f + bendXZ * 0.65f;
            const float  tuftCoarse = ValueNoise2D(pTuft);
            const float  tuftFine   = ValueNoise2D(worldM.xy * 13.5f + bendXZ * 1.15f);
            const float  bladeTip   = saturate((tuftCoarse * 0.62f + tuftFine * 0.38f - 0.34f) * 2.3f);

            // Travelling wind-wave sheen ripple (sunlight catching bent grass blades):
            const float  sunAlign   = saturate(dot(normalize(float3(bendXZ.x, bendXZ.y, 1.0f)), sD));
            const float  gustSheen  = saturate(bendMag * 0.55f) * (0.35f + 0.65f * sunAlign);

            // Root ambient occlusion + lush sunlit blade tip contrast + trampled path + wildfire char:
            const float3 lushTip    = float3(0.24f, 0.36f, 0.11f) * max(sC, float3(0.25f, 0.28f, 0.22f));
            const float  aoRoot     = lerp(0.84f, 1.12f, bladeTip);
            col *= lerp(1.0f, aoRoot, wGrass * 0.55f);
            col += lushTip * ((bladeTip * 0.14f + gustSheen * 0.18f) * (1.0f - droop01 * 0.55f) * (1.0f - char01) * wGrass);

            // Wildfire thermal char darkens & stiffens scorched undergrowth:
            col *= lerp(1.0f, 0.34f, char01 * 0.85f * wGrass);
            modified = true;
        }
    }

    // -------------------------------------------------------------------------
    // 4. WORLD-SPACE GLACIER CRYSTALLINE FACETS & CONIFER CANOPY MICRO-DEPTH
    // -------------------------------------------------------------------------
    if (!isInt && base == 6u && distM < 85.0f) {
        const float coolIce = saturate((alb.b - alb.r * 0.88f) * 4.0f + (dot(alb, float3(0.33f, 0.33f, 0.34f)) - 0.42f));
        if (coolIce > 0.10f) {
            const float facet    = GlacierFacet3D(worldM * 1.35f);
            const float distFade = 1.0f - smoothstep(50.0f, 85.0f, distM);
            const float wIce     = saturate(coolIce) * distFade;
            // Subtle glacial blue-cyan subsurface depth in facet troughs + sunlit crystal edges:
            const float3 iceSub  = float3(0.06f, 0.18f, 0.26f) * (1.0f - facet) * max(sunDir.w, 0.25f);
            const float3 iceEdge = sC * (smoothstep(0.65f, 0.96f, facet) * 0.10f);
            col += (iceSub + iceEdge) * (0.45f * wIce);
            modified = true;
        }
    } else if (!isInt && base == 5u && distM < 85.0f) {
        // Gentle conifer canopy sunlit chlorophyll depth & crushed-shadow rescue (never touches actors!):
        const float distFade = 1.0f - smoothstep(50.0f, 85.0f, distM);
        const float lum      = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
        const float needleN  = ValueNoise2D(worldM.xy * 3.8f + worldM.z * 1.7f);
        const float shadowLift = saturate((0.08f - lum) * 10.0f) * distFade;
        col += float3(0.014f, 0.022f, 0.010f) * shadowLift * (0.6f + 0.4f * needleN);
        modified = true;
    }

    if (modified) {
        ColorOut[dtid.xy] = float4(SanitizeColor3(col), origColor.a);
    }
}
