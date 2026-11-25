// RtHit.hlsli: what a ray hit is made of, for RtShadow.hlsl (docs/RAYTRACING.md, "Materials and the
// radiance cache"). A hit off screen has no colour in the game's frame; it is shaded here from the
// mesh's own material (RtAlpha.h: coordinates, face normal, colour copy) and the world-space radiance
// cache (RtCache.h). Needs LightingCommon.hlsli, Records, Coords, Masks and the trace helpers first.
// Original work, MIT.
#include "RtCacheCommon.hlsli"

StructuredBuffer<uint4> Mats : register(t6);              // colour copy + 1 (0 none), first face normal, flags (1 alpha, 2 normals)
RWByteAddressBuffer FaceNormals : register(u5);           // octahedral, two snorm16, one per triangle
RWTexture2D<unorm float4> Albedos[4096] : register(u0, space2);
cbuffer G : register(b2) { float3 camPos; float cacheCell; uint cacheSlots, giFlags, debugView, giFrame; float4 lodRect; };

static const uint GI_MATERIALS = 1u, GI_CACHE = 2u, GI_REST = 4u, GI_REUSE = 8u, GI_FAR = 16u;

struct Hit { float t; uint id, prim; float2 bary; float3x4 o2w; };

// Distant land (the game's LOD terrain chunks) carries LOD_LAND in its instance ID over its record + 1. Inside the
// loaded area (lodRect: centre xy, half size zw, camera-relative) the game's vertex shader keeps it under the
// full-detail ground, so a hit there is no surface the frame has (RtScene.cpp, SceneGeometry.cpp).
static const uint LOD_LAND = 0x800000u, RECORD_BITS = 0x7FFFFFu;
bool lodHidden(uint id, float3 hp) { return (id & LOD_LAND) != 0u && all(abs(hp.xy - lodRect.xy) < lodRect.zw); }

// The first hit along a ray with explicit TMin (supports negative-space empty-corridor skipping).
bool traceHitTMin(float3 o, float3 d, float tMin, float tMax, out Hit h) {
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    RayDesc r;
    r.Origin = o; r.Direction = d; r.TMin = tMin; r.TMax = max(tMax, tMin);
    q.TraceRayInline(Scene, 0, 0xFF, r);
    [loop] while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
            const uint full = q.CandidateInstanceID(), id = full & RECORD_BITS;
            if (lodHidden(full, o + d * q.CandidateTriangleRayT())) { continue; }
            if (id == 0u || Records[id - 1u].z == 0u) { q.CommitNonOpaqueTriangleHit(); continue; }   // no mask: opaque
            const uint4 rec = Records[id - 1u];
            const uint a = (rec.x + q.CandidatePrimitiveIndex() * 3u) * 8u;
            const float2 b = q.CandidateTriangleBarycentrics();
            const float2 uv = asfloat(Coords.Load2(a)) * (1.0f - b.x - b.y) + asfloat(Coords.Load2(a + 8u)) * b.x + asfloat(Coords.Load2(a + 16u)) * b.y;
            // Nearest texel: these rays are averaged over frames anyway.
            if (Masks[NonUniformResourceIndex(rec.y)][uint2(frac(uv) * float2(rec.zw)) % rec.zw] >= alphaRef) { q.CommitNonOpaqueTriangleHit(); }
        }
    }
    h.t = -1.0f; h.id = 0u; h.prim = 0u; h.bary = 0.0f; h.o2w = (float3x4)0;
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) { return false; }
    h.t = q.CommittedRayT(); h.id = q.CommittedInstanceID() & RECORD_BITS; h.prim = q.CommittedPrimitiveIndex();
    h.bary = q.CommittedTriangleBarycentrics(); h.o2w = q.CommittedObjectToWorld3x4();
    return true;
}

bool traceHit(float3 o, float3 d, float tMax, out Hit h) {
    return traceHitTMin(o, d, 0.0f, tMax, h);
}

float3 unpackOct(uint w) {
    const float2 e = float2(int2(w << 16, w) >> 16) / 32767.0f;
    float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f) { n.xy = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f); }
    return normalize(n);
}

// The hit's face normal in world space, turned toward the ray; a mesh without normals faces the ray.
float3 hitNormal(Hit h, float3 d) {
    if (h.id == 0u) { return -d; }
    const uint4 m = Mats[h.id - 1u];
    if ((m.z & 2u) == 0u) { return -d; }
    const uint w = FaceNormals.Load((m.y + h.prim) * 4u);
    if (w == 0u) { return -d; }
    float3 n = mul(h.o2w, float4(unpackOct(w), 0.0f));
    const float len = length(n);
    if (len < 1.0e-6f) { return -d; }
    n /= len;
    return dot(n, d) > 0.0f ? -n : n;
}

// The colour copy at the hit (bilinear, wrapping), in the game's own encoding as its lit shader reads
// it; a mesh without one reads a mid grey.
float3 hitAlbedo(Hit h) {
    if (h.id == 0u) { return 0.3f.xxx; }
    const uint4 m = Mats[h.id - 1u];
    if (m.x == 0u) { return 0.3f.xxx; }
    const uint4 rec = Records[h.id - 1u];
    const uint a = (rec.x + h.prim * 3u) * 8u;
    const float2 b = h.bary;
    const float2 uv = asfloat(Coords.Load2(a)) * (1.0f - b.x - b.y) + asfloat(Coords.Load2(a + 8u)) * b.x + asfloat(Coords.Load2(a + 16u)) * b.y;
    const uint i = m.x - 1u;
    uint w, hh;
    Albedos[NonUniformResourceIndex(i)].GetDimensions(w, hh);
    const float2 f = frac(uv) * float2(w, hh) - 0.5f;
    const int2 b0 = int2(floor(f));
    const float2 fa = f - float2(b0);
    const int2 sz = int2(w, hh);
    const uint2 t0 = uint2((b0 + sz) % sz), t1 = uint2((b0 + 1 + sz) % sz);
    const float3 c00 = Albedos[NonUniformResourceIndex(i)][t0].rgb, c10 = Albedos[NonUniformResourceIndex(i)][uint2(t1.x, t0.y)].rgb;
    const float3 c01 = Albedos[NonUniformResourceIndex(i)][uint2(t0.x, t1.y)].rgb, c11 = Albedos[NonUniformResourceIndex(i)][t1].rgb;
    return lerp(lerp(c00, c10, fa.x), lerp(c01, c11, fa.x), fa.y);
}

float3 ambientAt(float3 n) {
    return float3(dot(ambient[0].xyz, n) + ambient[0].w, dot(ambient[1].xyz, n) + ambient[1].w, dot(ambient[2].xyz, n) + ambient[2].w);
}

// ---- The radiance cache (RtCache.h) ----

uint faceOf(float3 n) {
    const float3 a = abs(n);
    const uint ax = a.x > a.y ? (a.x > a.z ? 0u : 2u) : (a.y > a.z ? 1u : 2u);
    return ax * 2u + (n[ax] < 0.0f ? 1u : 0u);
}

// The cell of a camera-relative point facing n: its first slot and its checksum (never 0). jitter
// moves the point up to half a cell, so the cells' edges turn into noise the filters average.
void cellOf(float3 p, float3 n, float3 jitter, out uint slot, out uint check) {
    const float dist = length(p);
    const uint lvl = uint(clamp(floor(log2(max(dist / (cacheCell * 32.0f), 1.0f))), 0.0f, 12.0f));
    const float size = cacheCell * exp2(float(lvl));
    const int3 c = int3(floor((p + camPos) / size + jitter));
    const uint tag = lvl * 8u + faceOf(n);
    const uint h = pcg(uint(c.x) + pcg(uint(c.y) + pcg(uint(c.z) + pcg(tag))));
    slot = h % cacheSlots;
    check = clamp(pcg(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u ^ tag * 2654435761u), 1u, CACHE_TOMB - 1u);
}

// Full Negative-Space Oracle read: returns bounce+sky mean, conservative free-flight radius, entropy [0,1], and history frames.
bool cacheReadOracle(float3 p, float3 n, float3 jitter, out float4 v, out float freeFlight, out float entropy, out uint frames) {
    v = 0.0f; freeFlight = 0.0f; entropy = 1.0f; frames = 0u;
    uint s, check;
    cellOf(p, n, jitter, s, check);
    [loop] for (uint i = 0u; i < CACHE_PROBES; i++) {
        const uint k = CacheKeys.Load(s * 4u);
        if (k == check) {
            const uint4 meta = CacheValue.Load4(s * 32u + 16u);
            if (meta.x == 0u) { return false; }
            v = asfloat(CacheValue.Load4(s * 32u));
            frames = meta.x;
            freeFlight = asfloat(meta.z);
            entropy = asfloat(meta.w);
            return true;
        }
        if (k == 0u) { return false; }
        s = s + 1u == cacheSlots ? 0u : s + 1u;
    }
    return false;
}

// The cell's resolved value (bounce rgb, sky share); false when the cell has none yet.
bool cacheRead(float3 p, float3 n, float3 jitter, out float4 v) {
    float ff, ent; uint fr;
    return cacheReadOracle(p, n, jitter, v, ff, ent, fr);
}

// Samples into a cell's slot (found or claimed along its probe run): bounce rgb, sky, free-flight distance, and squared moments.
void cacheInsert(uint s, uint check, uint3 c, uint skyFixed, uint count, uint distFixed, uint lumSqFixed, uint skySqFixed) {
    uint tomb = ~0u;
    bool placed = false;
    [loop] for (uint i = 0u; i < CACHE_PROBES; i++) {
        uint prev;
        CacheKeys.InterlockedCompareExchange(s * 4u, 0u, check, prev);
        if (prev == 0u || prev == check) { placed = true; break; }
        if (prev == CACHE_TOMB && tomb == ~0u) { tomb = s; }
        s = s + 1u == cacheSlots ? 0u : s + 1u;
    }
    if (!placed && tomb != ~0u) {
        uint prev;
        CacheKeys.InterlockedCompareExchange(tomb * 4u, CACHE_TOMB, check, prev);
        placed = prev == CACHE_TOMB || prev == check;
        s = tomb;
    }
    if (!placed) { return; }
    const uint a = s * 32u;
    uint o;
    CacheAccum.InterlockedAdd(a, c.r, o); CacheAccum.InterlockedAdd(a + 4u, c.g, o); CacheAccum.InterlockedAdd(a + 8u, c.b, o);
    CacheAccum.InterlockedAdd(a + 12u, skyFixed, o);
    CacheAccum.InterlockedAdd(a + 16u, count, o);
    CacheAccum.InterlockedAdd(a + 20u, distFixed, o);
    CacheAccum.InterlockedAdd(a + 24u, lumSqFixed, o);
    CacheAccum.InterlockedAdd(a + 28u, skySqFixed, o);
}

// One sample for the cell: bounce light, sky visibility, and observed free-flight distance.
void cacheAdd(float3 p, float3 n, float3 bounce, float sky, float freeDist = 0.0f) {
    uint s, check;
    cellOf(p, n, 0.0f.xxx, s, check);
    const float3 bc = min(max(bounce, 0.0f), 4.0f);
    const float sk = saturate(sky);
    const float lum = dot(bc, float3(0.2126f, 0.7152f, 0.0722f));
    cacheInsert(s, check, uint3(bc * CACHE_FIXED), uint(sk * CACHE_FIXED), 1u,
                uint(clamp(freeDist, 0.0f, 4096.0f) * CACHE_DIST_FIXED),
                uint(min(lum * lum, 16.0f) * CACHE_FIXED),
                uint((sk * sk) * CACHE_FIXED));
}

// Wave-coalesced oracle update: lanes sharing a cell reduce via WaveActiveSum so each cell takes 1 atomic batch per wave.
void cacheAddWave(float3 p, float3 n, float3 bounce, float sky, float freeDist = 0.0f) {
    uint s, check;
    cellOf(p, n, 0.0f.xxx, s, check);
    const float3 bc = min(max(bounce, 0.0f), 4.0f);
    const float sk = saturate(sky);
    const float lum = dot(bc, float3(0.2126f, 0.7152f, 0.0722f));
    const uint3 c = uint3(bc * CACHE_FIXED);
    const uint k = uint(sk * CACHE_FIXED);
    const uint df = uint(clamp(freeDist, 0.0f, 4096.0f) * CACHE_DIST_FIXED);
    const uint ls = uint(min(lum * lum, 16.0f) * CACHE_FIXED);
    const uint ks2 = uint((sk * sk) * CACHE_FIXED);
    [loop] for (;;) {
        if (WaveReadLaneFirst(check) == check) {
            const uint3 cs = WaveActiveSum(c);
            const uint ks = WaveActiveSum(k);
            const uint ds = WaveActiveSum(df);
            const uint lss = WaveActiveSum(ls);
            const uint kss = WaveActiveSum(ks2);
            const uint count = WaveActiveCountBits(true);
            if (WaveIsFirstLane()) { cacheInsert(s, check, cs, ks, count, ds, lss, kss); }
            break;
        }
    }
}

// ---- Shading a hit the camera does not see ----

// As the game lights a surface (Lighting.hlsl, match mode): its colour times the sun (traced), every
// point light in reach (unshadowed), and the directional ambient, here scaled by the cell's sky share
// with the cell's bounce added (exteriors, as the lighting pass does per pixel).
float3 shadeHit(float3 hp, float3 n, float3 albedo, float2 r, bool traceSun) {
    float3 light = 0.0f;
    const float3 o = hp + n * (0.5f + 0.002f * length(hp));
    float3 uSunH, uSkyZH, uSkyHH, uAmbUpH, uAmbDnH;
    if (sunColour.w > 0.5f) { evalWorldAtmosphere(uSunH, uSkyZH, uSkyHH, uAmbUpH, uAmbDnH); }
    else { uSunH = sunColour.rgb; uAmbUpH = ambientAt(float3(0.0f, 0.0f, 1.0f)); uAmbDnH = ambientAt(float3(0.0f, 0.0f, -1.0f)); }
    if (sunDir.w > 0.5f) {
        const float nl = dot(n, sunDir.xyz);
        if (nl > 0.0f) {
            const float vis = sunColour.w > 0.5f && traceSun ? visible(o, jitterDir(sunDir.xyz, sunCone, r), 0.0f, sunLength) : 1.0f;
            light += uSunH * nl * vis;
        }
    }
    [loop] for (uint li = 0u; li < flags.y; li++) {
        const float3 toL = lightPosR[li].xyz - hp;
        const float dist = length(toL);
        const float x = saturate(dist / lightPosR[li].w);
        if (x >= 1.0f) { continue; }
        light += lightColS[li].rgb * (1.0f - x * x) * saturate(dot(n, toL / max(dist, 1.0e-4f)));
    }
    float3 amb = ambientAt(n);
    if (sunColour.w > 0.5f) {
        amb = lerp(amb, lerp(uAmbDnH, uAmbUpH, saturate(n.z * 0.5f + 0.5f)), 0.48f);
    }
    float4 c;
    const float3 j = float3(r, frac(r.x + r.y * 1.618f)) - 0.5f;
    if ((giFlags & GI_CACHE) != 0u && cacheRead(hp, n, j, c)) {
        if (sunColour.w > 0.5f) { amb *= lerp(1.0f, c.a, cluster.w); }
        amb += c.rgb * proj.w;
    } else if (sunColour.w > 0.5f) {
        amb *= 0.6f;   // no cell yet: a typical sky share
    }
    return albedo * (light + amb);
}

// The light a hit sends back: the game's frame where the hit is on screen and not hidden; otherwise
// its shading from its material, or (materials off) a dim share of the cell's ambient.
float3 radianceAt(Hit h, float3 o, float3 d, float2 r, bool traceSun, out float3 n) {
    const float3 hp = o + d * h.t;
    float3 c;
    const bool mats = (giFlags & GI_MATERIALS) != 0u;
    n = mats ? hitNormal(h, d) : -d;
    if (onScreen(hp, c)) { return c; }
    if (!mats) { return 0.25f * float3(ambient[0].w, ambient[1].w, ambient[2].w); }
    return shadeHit(hp, n, hitAlbedo(h), r, traceSun);
}
