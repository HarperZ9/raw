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
