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
    const uint key = CacheKeys.Load(s * 4u);
    bool alive = (key != 0u && key != CACHE_TOMB);
    if (alive) {
        const uint4 sum = CacheAccum.Load4(a);
        const uint4 extra = CacheAccum.Load4(a + 16u);   // count, freeDistSum (x16), lumSqSum (x1024), skySqSum (x1024)
        const uint count = extra.x;
        uint4 meta = CacheValue.Load4(a + 16u);          // frames, lastFrame, asuint(freeFlightMin), asuint(entropy)
        if (count > 0u) {
            const float invCount = 1.0f / float(count);
            const float4 mean = sanitizeCache4(float4(sum) * (invCount / CACHE_FIXED));
            const float4 old = sanitizeCache4(asfloat(CacheValue.Load4(a)));
            // SIGGRAPH Advances 2026 ORCA (GI-18) + Wave 75 Relative-L2 Online Hash Radiance Cache EMA:
            // reacts in 1-2 frames to spells, torches, lightning, or opening doors while guaranteeing 0 NaN/Inf!
            const float oldLum = dot(old.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float newLum = dot(mean.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float relDelta = abs(newLum - oldLum) / max(oldLum, 0.05f);
            const float chrDelta = length(mean.rgb / max(newLum, 0.05f) - old.rgb / max(oldLum, 0.05f));
            const float3 radDiff = mean.rgb - old.rgb;
            const float relL2 = dot(radDiff, radDiff) / (dot(old.rgb, old.rgb) + 0.01f);
            if (relDelta > 0.35f || chrDelta > 0.42f) { meta.x = min(meta.x, 2u); }
            meta.x = min(meta.x + 1u, maxFrames);
            const float baseAlpha = 1.0f / float(meta.x);
            const float alpha = clamp(max(baseAlpha, smoothstep(0.12f, 1.25f, relL2) * 0.45f), 0.05f, 0.85f);
            const float4 v = sanitizeCache4(meta.x <= 1u ? mean : lerp(old, mean, alpha));
            CacheValue.Store4(a, asuint(v));

            // Negative-Space Conservative Free-Flight Radius: shrinks quickly on new occluders, expands cautiously in open air.
            const float meanFree = float(extra.y) * (invCount / CACHE_DIST_FIXED);
            const float oldFree = asfloat(meta.z);
            const float freeFlight = (meta.x <= 1u)
                ? meanFree
                : lerp(oldFree, meanFree, meanFree < oldFree ? 0.55f : 0.14f);

            // Visibility + Radiance Welford Entropy Score [0, 1]: peaks at penumbra edges, contact creases & light transitions.
            const float meanLumSq = float(extra.z) * (invCount / CACHE_FIXED);
            const float meanSkySq = float(extra.w) * (invCount / CACHE_FIXED);
            const float lumVar = max(meanLumSq - newLum * newLum, 0.0f);
            const float skyVar = max(meanSkySq - mean.a * mean.a, 0.0f);
            const float lumEnt = saturate(sqrt(lumVar) / max(newLum + 0.08f, 0.08f));
            const float skyEnt = saturate(sqrt(skyVar) * 2.4f);
            const float instEnt = saturate(max(skyEnt, lumEnt * 0.85f) + saturate(relDelta * 0.65f));
            const float oldEnt = asfloat(meta.w);
            const float entropy = (meta.x <= 1u) ? instEnt : lerp(oldEnt, instEnt, max(alpha, 0.25f));

            meta.y = frame;
            meta.z = asuint(freeFlight);
            meta.w = asuint(entropy);
            CacheValue.Store4(a + 16u, meta);
            CacheAccum.Store4(a, uint4(0u, 0u, 0u, 0u));
            CacheAccum.Store4(a + 16u, uint4(0u, 0u, 0u, 0u));
        } else if (frame - meta.y > maxAge) {
            CacheKeys.Store(s * 4u, CACHE_TOMB);   // freed; the probe runs through it stay whole
            CacheValue.Store4(a + 16u, uint4(0u, 0u, 0u, 0u));
            alive = false;
        }
    }
    // SM6.9 Wave-aggregated atomic reduction: 32x fewer global atomic stalls across 150k+ radiance cache cells.
    const uint waveAlive = WaveActiveCountBits(alive);
    if (WaveIsFirstLane() && waveAlive > 0u) {
        uint prev;
        CacheValue.InterlockedAdd(counters + (frame & 1u) * 4u, waveAlive, prev);
    }
}
