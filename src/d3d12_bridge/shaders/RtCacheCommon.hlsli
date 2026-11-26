// RtCacheCommon.hlsli: World-Space Negative-Space Visibility & Directional Entropy Radiance Oracle.
// Shared by the trace (RtShadow.hlsl) and its resolve (RtCache.hlsl); zero extra VRAM (32 bytes/slot):
//   CacheAccum (32B): r, g, b, sky (x1024); count; freeDistSum (x16); lumSqSum (x1024); skySqSum (x1024)
//   CacheValue (32B): float4(bounce.rgb, sky); uint frames, lastFrame; float freeFlightMin, entropy
// Original work, MIT.
#ifndef CACHE_KEYS
#define CACHE_KEYS u6
#define CACHE_ACCUM u7
#define CACHE_VALUE u8
#endif
RWByteAddressBuffer CacheKeys  : register(CACHE_KEYS);
RWByteAddressBuffer CacheAccum : register(CACHE_ACCUM);
RWByteAddressBuffer CacheValue : register(CACHE_VALUE);

static const uint  CACHE_PROBES     = 8u;
static const float CACHE_FIXED      = 1024.0f;
static const float CACHE_DIST_FIXED = 16.0f;
// A freed slot: lookups probe past it (a 0 there would cut the run of every cell placed after it, and
// those cells were found again as new ones), inserts take it back when their run has no free slot.
static const uint  CACHE_TOMB       = 0xFFFFFFFFu;

uint pcg(uint v) {
    const uint s = v * 747796405u + 2891336453u;
    const uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

// Wave 75: Multi-resolution L=8 world-space hash key + finite radiance sanitizer shared across RtCache & RtShadow.
float4 sanitizeCache4(float4 v) {
    return float4(
        isfinite(v.x) ? clamp(v.x, 0.0f, 32.0f) : 0.0f,
        isfinite(v.y) ? clamp(v.y, 0.0f, 32.0f) : 0.0f,
        isfinite(v.z) ? clamp(v.z, 0.0f, 32.0f) : 0.0f,
        isfinite(v.w) ? saturate(v.w) : 0.0f);
}

uint hashCacheKeyMultiRes(float3 worldPos, float3 worldNormal, uint lodLevel, float baseCellSize) {
    const uint  lod = min(lodLevel, 7u);
    const float cellSize = max(baseCellSize, 1.0f) * float(1u << lod);
    const int3  qPos = int3(floor(worldPos / cellSize));
    const int3  qNrm = int3(round(clamp(worldNormal, -1.0f.xxx, 1.0f.xxx) * 3.0f));
    uint h = (uint(qPos.x) * 73856093u) ^ (uint(qPos.y) * 19349663u) ^ (uint(qPos.z) * 83492791u);
    h ^= (uint(qNrm.x + 4) * 2654435761u) ^ (uint(qNrm.y + 4) * 2246822519u) ^ (uint(qNrm.z + 4) * 3266489917u);
    h = pcg(h ^ ((lod + 1u) * 668265263u));
    return (h == 0u || h == CACHE_TOMB) ? 1u : h;
}
