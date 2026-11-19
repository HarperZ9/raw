// RtCacheCommon.hlsli: the world-space radiance cache shared by the trace (RtShadow.hlsl) and its
// resolve (RtCache.hlsl); RtCache.h has the design. A cell is a cube of the world (absolute position:
// camera-relative position plus the camera's place, RtTuning::cameraPos) at a size that doubles with
// distance from the camera, split by which of the six axis directions the surface faces. A cell lives
// in a hash table of `slots` entries with linear probing: Keys holds its checksum (0: free), Accum this
// frame's samples (bounce rgb and sky, fixed point, and a count), Value the resolved mean (bounce rgb,
// sky visibility), its frames of history and the frame it last took a sample. Original work, MIT.
#ifndef CACHE_KEYS
#define CACHE_KEYS u6
#define CACHE_ACCUM u7
#define CACHE_VALUE u8
#endif
RWByteAddressBuffer CacheKeys  : register(CACHE_KEYS);
RWByteAddressBuffer CacheAccum : register(CACHE_ACCUM);   // 32 bytes a slot: r, g, b, sky (x 1024), count
RWByteAddressBuffer CacheValue : register(CACHE_VALUE);   // 32 bytes a slot: float r, g, b, sky; frames; last frame

static const uint CACHE_PROBES = 8u;
static const float CACHE_FIXED = 1024.0f;

uint pcg(uint v) {
    const uint s = v * 747796405u + 2891336453u;
    const uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
