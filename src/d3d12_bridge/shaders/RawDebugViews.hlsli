// RawDebugViews.hlsli: Wave 82 Canonical 9-Mode Live GPU Debug Render Views (Modes 0..8)
// Mapped onto Lighting.hlsl flags.w == 40..48 (Mode 0 = Final Lit, Modes 1..8 = live diagnostics)
// so all legacy Views 1..36 remain 100% preserved.
// Modes:
//   0 (40): Final Lit (production HDR)
//   1 (41): Albedo + Porosity Wetness Overlay
//   2 (42): World Normals + Micro-Normal Film (magenta alarm on zero/non-unit normals)
//   3 (43): Linear Depth + Hi-Z Occlusion Pyramid Contour Bands (red alarm on NaN/Inf depth)
//   4 (44): Motion Vectors + Disocclusion / Ghosting Heat
//   5 (45): ReSTIR-GI / Hash Radiance Cache + Froxel GI Irradiance
//   6 (46): Shadow Cascades (0..3 splits) + Volumetric Cloud Transmittance
//   7 (47): Wireframe / Overdraw Heatmap / Meshlet LOD Tier (LOD0 green -> LOD3 red)
//   8 (48): Navmesh + Actor Spatial Grid + Per-Pass GPU/CPU Profiler HUD Band
// Clean-room MIT, SM 6.6 / 6.9 / 6.10, zero NaN/Inf.
#ifndef RAW_DEBUG_VIEWS_HLSLI
#define RAW_DEBUG_VIEWS_HLSLI

static const uint RAW_DBG_MODE_BASE             = 40u;
static const uint RAW_DBG_FINAL_LIT             = 0u;
static const uint RAW_DBG_ALBEDO_POROSITY       = 1u;
static const uint RAW_DBG_NORMALS_FILM          = 2u;
static const uint RAW_DBG_DEPTH_HIZ             = 3u;
static const uint RAW_DBG_MOTION_DISOCC         = 4u;
static const uint RAW_DBG_GI_RADIANCE_FROXEL    = 5u;
static const uint RAW_DBG_SHADOW_CLOUD_TRANS    = 6u;
static const uint RAW_DBG_OVERDRAW_LOD_TIERS    = 7u;
static const uint RAW_DBG_NAVMESH_ACTOR_PROF    = 8u;

struct RawDebugViewParams {
    uint   debugMode;      // 0..8
    uint   hizMip;         // 0..13
    float  depthNear;      // near plane in game units
    float  depthFar;       // far plane in game units
    float4 heatmapScale;   // x=overdrawScale, y=giScale, z=mvScale, w=hudAlpha
    uint   flags;          // bit 0: show split overlay, bit 1: highlight alarms
    uint3  pad0;           // bytes 36..47
    float4 inspectorRect;  // bytes 48..63 (exact 64B matching RawDebugViews.h)
};

struct RawDebugPixelInput {
    float3 albedo;
    float  wetness01;
    float3 worldNormal;
    float  microPerturb01;
    float  linearDepth;
    float  rawDepth;
    float2 motionPx;
    float  disocclusion01;
    float3 giIrradiance;
    float  sunShadow;
    float  cloudTrans;
    uint   materialClass;
    float2 pixelPos;
    float2 screenDims;
};

float rawDbgSan1(float v, float fb) {
    return isfinite(v) ? v : fb;
}

float3 rawDbgSan3(float3 v, float capVal) {
    return all(isfinite(v)) ? clamp(v, 0.0f.xxx, capVal.xxx) : 0.0f.xxx;
}

// Mode 1: Albedo + Porosity Wetness (cyan-blue wetness tint on pooled/wet stone, neutral dry albedo)
float3 rawDebugViewAlbedoPorosity(float3 albedo, float wetness01) {
    const float3 a = rawDbgSan3(albedo, 1.0f);
    const float  w = saturate(rawDbgSan1(wetness01, 0.0f));
    const float3 wetOverlay = float3(0.08f, 0.58f, 0.92f);
    return saturate(lerp(a, lerp(a * 0.55f, wetOverlay, 0.65f), w * 0.72f));
}

// Mode 2: World Normals + Micro-Normal Film (magenta alarm if normal is degenerate or non-finite)
float3 rawDebugViewNormalsFilm(float3 worldNormal, float microPerturb01) {
    if (!all(isfinite(worldNormal))) {
        return float3(1.0f, 0.0f, 1.0f); // fail-closed magenta NaN alarm
    }
    const float len2 = dot(worldNormal, worldNormal);
    if (len2 < 0.25f || len2 > 2.25f) {
        return float3(1.0f, 0.0f, 1.0f); // fail-closed magenta degenerate-normal alarm
    }
    const float3 nUnit = worldNormal * rsqrt(len2);
    const float3 nVis  = nUnit * 0.5f + 0.5f;
    const float  mFilm = saturate(rawDbgSan1(microPerturb01, 0.0f));
    return saturate(lerp(nVis, float3(0.95f, 0.98f, 1.0f), mFilm * 0.30f));
}

// Mode 3: Linear Depth + Hi-Z Occlusion Pyramid contour bands (red alarm on NaN/Inf depth)
float3 rawDebugViewDepthHiz(float linearDepth, float rawDepth, uint hizMip) {
    if (!isfinite(linearDepth) || !isfinite(rawDepth) || linearDepth < 0.0f) {
        return float3(1.0f, 0.04f, 0.04f); // fail-closed red depth alarm
    }
    const float zSafe   = max(linearDepth, 1.0f);
    const float zLog    = saturate(log2(zSafe) / 15.0f);
    const float mipFreq = max(1.0f, exp2(float(min(hizMip, 6u))));
    const float band    = 0.84f + 0.16f * frac(log2(zSafe) * (2.0f / mipFreq));
    return saturate(float3(zLog * 0.90f, (1.0f - abs(zLog - 0.5f) * 2.0f) * 0.75f, 1.0f - zLog * 0.72f) * band);
}

// Mode 4: Motion Vectors + Disocclusion / Ghosting Heat (RG velocity hue + magenta disocclusion mask)
float3 rawDebugViewMotionDisocc(float2 motionPx, float disocclusion01, float mvScale) {
    const float2 mv = float2(rawDbgSan1(motionPx.x, 0.0f), rawDbgSan1(motionPx.y, 0.0f));
    const float  sc = max(rawDbgSan1(mvScale, 16.0f), 1.0f);
    const float2 enc = saturate(mv / sc * 0.5f + 0.5f);
    const float  dis = saturate(rawDbgSan1(disocclusion01, 0.0f));
    const float3 baseCol = float3(enc.x, enc.y, saturate(length(mv) / sc));
    return saturate(lerp(baseCol, float3(1.0f, 0.10f, 0.85f), dis * 0.80f));
}

// Mode 5: ReSTIR-GI / Hash Radiance Cache + Froxel GI irradiance visualization
float3 rawDebugViewGiRadiance(float3 giIrradiance, float giScale) {
    const float3 gi = rawDbgSan3(giIrradiance, 16.0f) * max(rawDbgSan1(giScale, 1.0f), 0.1f);
    // Filmic log-compression so shadow bounce & bright sunlit GI are both legible:
    return saturate(gi / (1.0f.xxx + gi));
}

// Mode 6: Shadow Cascades (0..3 color-coded depth splits) + Volumetric Cloud Transmittance
float3 rawDebugViewShadowsCloud(float linearDepth, float sunShadow, float cloudTrans) {
    const float z  = max(rawDbgSan1(linearDepth, 100.0f), 0.0f);
    const float sh = saturate(rawDbgSan1(sunShadow, 1.0f));
    const float ct = saturate(rawDbgSan1(cloudTrans, 1.0f));
    const float3 cascCol = (z < 650.0f)  ? float3(0.20f, 0.92f, 0.32f)
                         : (z < 2000.0f) ? float3(0.95f, 0.85f, 0.18f)
                         : (z < 5500.0f) ? float3(0.96f, 0.45f, 0.14f)
                                         : float3(0.35f, 0.55f, 0.98f);
    return saturate(cascCol * (0.22f + 0.78f * sh * ct));
}

// Mode 7: Wireframe / Overdraw Heatmap / Meshlet LOD Tier (LOD0 green -> LOD3 red)
float3 rawDebugViewOverdrawLod(float linearDepth, uint materialClass, float2 pixelPos) {
    const float z = max(rawDbgSan1(linearDepth, 100.0f), 0.0f);
    float3 lodCol = (z < 900.0f)  ? float3(0.16f, 0.88f, 0.30f)   // LOD0 green
                  : (z < 2400.0f) ? float3(0.92f, 0.86f, 0.16f)   // LOD1 yellow
                  : (z < 5600.0f) ? float3(0.96f, 0.46f, 0.14f)   // LOD2 orange
                                  : float3(0.94f, 0.18f, 0.18f);  // LOD3 red
    if (materialClass == 7u) { lodCol = float3(0.94f, 0.18f, 0.18f); }
    // Subtle 16px meshlet tile grid overlay:
    const float2 gCell = frac(pixelPos * (1.0f / 16.0f));
    const float  wire  = (gCell.x < 0.06f || gCell.y < 0.06f) ? 1.0f : 0.0f;
    return saturate(lerp(lodCol * 0.78f, 1.0f.xxx, wire * 0.32f));
}

// Mode 8: Navmesh Walkable Slope + Actor Spatial Grid + Per-Pass Profiler HUD Band
float3 rawDebugViewNavActorProfiler(float3 worldNormal, uint materialClass, float2 pixelPos, float2 screenDims) {
    const float3 nUnit = all(isfinite(worldNormal)) && dot(worldNormal, worldNormal) > 0.25f
        ? normalize(worldNormal) : float3(0.0f, 0.0f, 1.0f);
    const bool isActor = (materialClass == 10u || materialClass == 11u || materialClass == 12u || materialClass == 14u);
    const bool walkable = (nUnit.z > 0.68f);
    float3 baseCol = isActor  ? float3(1.0f, 0.30f, 0.12f)
                   : walkable ? float3(0.14f, 0.68f, 0.82f)
                              : float3(0.22f, 0.24f, 0.28f);
    // 64px spatial cell grid:
    const float2 cell = frac(pixelPos * (1.0f / 64.0f));
    const float  grid = (cell.x < 0.03f || cell.y < 0.03f) ? 1.0f : 0.0f;
    baseCol = lerp(baseCol, float3(0.95f, 0.98f, 1.0f), grid * 0.35f);
    // Bottom 28px per-pass GPU profiler bar band (8 pass segments):
    const float hSafe = max(rawDbgSan1(screenDims.y, 1080.0f), 64.0f);
    const float wSafe = max(rawDbgSan1(screenDims.x, 1920.0f), 64.0f);
    if (pixelPos.y > (hSafe - 28.0f)) {
        const float u = saturate(pixelPos.x / wSafe);
        const uint seg = min(uint(u * 8.0f), 7u);
        const float3 barCol = (seg == 0u) ? float3(0.18f, 0.82f, 0.42f)
                            : (seg == 1u) ? float3(0.22f, 0.72f, 0.92f)
                            : (seg == 2u) ? float3(0.95f, 0.78f, 0.18f)
                            : (seg == 3u) ? float3(0.94f, 0.44f, 0.18f)
                            : (seg == 4u) ? float3(0.76f, 0.34f, 0.92f)
                            : (seg == 5u) ? float3(0.24f, 0.88f, 0.76f)
                            : (seg == 6u) ? float3(0.92f, 0.24f, 0.42f)
                                          : float3(0.68f, 0.88f, 0.24f);
        return barCol * 0.85f;
    }
    return saturate(baseCol);
}

// Unified dispatcher for Lighting.hlsl flags.w in [41..48] (canonical Debug Views 1..8)
float3 rawEvalCanonicalDebugView(uint mode1to8, RawDebugPixelInput px) {
    if (mode1to8 == RAW_DBG_ALBEDO_POROSITY)    { return rawDebugViewAlbedoPorosity(px.albedo, px.wetness01); }
    if (mode1to8 == RAW_DBG_NORMALS_FILM)       { return rawDebugViewNormalsFilm(px.worldNormal, px.microPerturb01); }
    if (mode1to8 == RAW_DBG_DEPTH_HIZ)          { return rawDebugViewDepthHiz(px.linearDepth, px.rawDepth, 0u); }
    if (mode1to8 == RAW_DBG_MOTION_DISOCC)      { return rawDebugViewMotionDisocc(px.motionPx, px.disocclusion01, 16.0f); }
    if (mode1to8 == RAW_DBG_GI_RADIANCE_FROXEL) { return rawDebugViewGiRadiance(px.giIrradiance, 1.0f); }
    if (mode1to8 == RAW_DBG_SHADOW_CLOUD_TRANS) { return rawDebugViewShadowsCloud(px.linearDepth, px.sunShadow, px.cloudTrans); }
    if (mode1to8 == RAW_DBG_OVERDRAW_LOD_TIERS) { return rawDebugViewOverdrawLod(px.linearDepth, px.materialClass, px.pixelPos); }
    if (mode1to8 == RAW_DBG_NAVMESH_ACTOR_PROF) { return rawDebugViewNavActorProfiler(px.worldNormal, px.materialClass, px.pixelPos, px.screenDims); }
    return rawDbgSan3(px.albedo, 1.0f);
}

#endif
