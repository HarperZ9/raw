// RtCache.hlsl: the radiance cache's resolve, once a frame after the trace (RtCache.cpp). Per slot:
// this frame's samples, averaged, are blended into the cell's value with weight 1 / frames (up to
// maxFrames, so the value follows a change within that many frames); the accumulator is cleared. A cell
// that took no sample for maxAge frames is freed. reset: every slot is cleared (a new cell, first use).
// Counters at the end of the value buffer: cells alive, by frame parity. Original work, MIT.
#define CACHE_KEYS u0
#define CACHE_ACCUM u1
#define CACHE_VALUE u2
#include "RtCacheCommon.hlsli"

cbuffer C : register(b0) { uint slots, frame, reset, maxAge, maxFrames, c5, c6, c7; };

[numthreads(64, 1, 1)]
void resolve(uint3 id : SV_DispatchThreadID) {
    const uint s = id.x;
    const uint counters = slots * 32u;
    if (s == 0u) { CacheValue.Store(counters + ((frame + 1u) & 1u) * 4u, 0u); }
    if (s >= slots) { return; }
    const uint a = s * 32u;
    if (reset != 0u) {
        CacheKeys.Store(s * 4u, 0u);
        CacheAccum.Store4(a, uint4(0u, 0u, 0u, 0u)); CacheAccum.Store4(a + 16u, uint4(0u, 0u, 0u, 0u));
        CacheValue.Store4(a, uint4(0u, 0u, 0u, 0u)); CacheValue.Store4(a + 16u, uint4(0u, 0u, 0u, 0u));
        return;
    }
    if (CacheKeys.Load(s * 4u) == 0u) { return; }
    const uint4 sum = CacheAccum.Load4(a);
    const uint count = CacheAccum.Load(a + 16u);
    uint2 meta = CacheValue.Load2(a + 16u);   // frames of history, last frame with a sample
    if (count > 0u) {
        const float4 mean = float4(sum) / (CACHE_FIXED * float(count));
        const float4 old = asfloat(CacheValue.Load4(a));
        meta.x = min(meta.x + 1u, maxFrames);
        const float4 v = meta.x <= 1u ? mean : lerp(old, mean, 1.0f / float(meta.x));
        CacheValue.Store4(a, asuint(v));
        meta.y = frame;
        CacheValue.Store2(a + 16u, meta);
        CacheAccum.Store4(a, uint4(0u, 0u, 0u, 0u));
        CacheAccum.Store(a + 16u, 0u);
    } else if (frame - meta.y > maxAge) {
        CacheKeys.Store(s * 4u, 0u);
        CacheValue.Store4(a + 16u, uint4(0u, 0u, 0u, 0u));
        return;
    }
    uint prev;
    CacheValue.InterlockedAdd(counters + (frame & 1u) * 4u, 1u, prev);
}
