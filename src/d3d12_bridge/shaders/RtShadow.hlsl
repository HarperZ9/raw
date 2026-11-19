// RtShadow.hlsl: ray-traced shadows on the game's own geometry (RtScene.cpp, docs/RAYTRACING.md).
// One thread a pixel. From the surface the G-buffer gives, one inline ray query toward the sun and
// one toward each of the three point lights of the pixel's cluster that would light it most.
// Each ray aims at a jittered point of its source (the sun's disc, a sphere of SourceRadius around
// the light), so the game's temporal anti-aliasing turns the frame-to-frame noise into penumbrae.
// Out: Vis = (sun, light 0, light 1, light 2) visibility; Lights = their indices + 1, over 255.
//
// Alpha-tested meshes (leaves, branch cards, grass) are non-opaque in the scene. At each candidate
// hit on one, its record (instance ID - 1) gives its texture coordinates in the pool and its alpha
// mask (RtAlpha.cpp); the hit counts only where the mask is at or above the game's cut.
//
// Facets: a low-polygon surface shaded with a smooth normal map faces the sun where its triangles
// already turn away, and the neighbouring triangle shadows it in hard flat patches (the shadow
// terminator problem). Where the geometric normal (from the depth buffer) turns from the sun, the sun
// ray starts up to termDist further out, so the surface's own facets cannot shadow it there. The
// lighting pass takes the darker of this and the game's filtered mask, so a near occluder skipped
// here still shadows through the mask.
// Original work, MIT.
#define ATLAS_SLOT t9
#include "LightingCommon.hlsli"
#include "Weather.hlsli"

Texture2D<float>  Depth  : register(t0);
Texture2D<float4> Normal : register(t1);
Texture2D<float4> Spec   : register(t2);
RaytracingAccelerationStructure Scene : register(t3);
StructuredBuffer<uint4> Records : register(t4);   // alpha meshes: (first coordinate, mask, mask width, height)
RWTexture2D<float4> Vis    : register(u0);
RWTexture2D<float4> Lights : register(u1);
RWByteAddressBuffer Coords : register(u2);        // float2 per triangle corner
RWTexture2D<unorm float> Masks[] : register(u0, space1);
RWTexture2D<float4> Refl : register(u4);          // reflected radiance, confidence (1: the hit is on screen)
RWTexture2D<float4> SkyRaw : register(u3);        // RtSky.cpp: one sky ray's bounce (rgb) and sky (a)
Texture2D<float4> GameColour : register(t5);      // the game's lit frame: the radiance of what a sky ray hits
cbuffer R : register(b1) { float sunLength, sourceRadius, sunCone, frameIndex, alphaRef, termDist, skyLength, reflectLength; };

uint clusterOf(float2 px, float z) {
    const uint tx = min(uint(px.x * dims.z * TILES_X), TILES_X - 1u);
    const uint ty = min(uint(px.y * dims.w * TILES_Y), TILES_Y - 1u);
    const float sl = log(max(z, cluster.x) / cluster.x) * cluster.z;
    return (min(uint(max(sl, 0.0f)), SLICES - 1u) * TILES_Y + ty) * TILES_X + tx;
}

// One pixel in each 2 x 2 block traces a sky ray each frame, the block's four in turn; the filter's
// spatial mean and history fill the rest.
bool skyTurn(uint2 p) { return ((p.x & 1u) | ((p.y & 1u) << 1)) == (uint(frameIndex) & 3u); }

float hash(float2 p, float k) { return frac(sin(dot(p + k, float2(12.9898f, 78.233f))) * 43758.5453f); }

// A cosine-weighted direction around n.
float3 cosineDir(float3 n, float2 r) {
    const float3 t = normalize(abs(n.z) < 0.99f ? cross(n, float3(0, 0, 1)) : cross(n, float3(1, 0, 0)));
    const float3 b = cross(n, t);
    const float a = 6.2831853f * r.x, s = sqrt(r.y);
    return normalize(t * (cos(a) * s) + b * (sin(a) * s) + n * sqrt(max(1.0f - r.y, 0.0f)));
}

// A unit vector spread around d by up to `spread` radians (a disc of directions).
float3 jitterDir(float3 d, float spread, float2 r) {
    const float3 t = normalize(abs(d.y) < 0.99f ? cross(d, float3(0, 1, 0)) : cross(d, float3(1, 0, 0)));
    const float3 b = cross(d, t);
    const float a = 6.2831853f * r.x, s = spread * sqrt(r.y);
    return normalize(d + (t * cos(a) + b * sin(a)) * s);
}

// The alpha of a candidate hit on an alpha-tested mesh.
float hitAlpha(uint id, uint prim, float2 b) {
    if (id == 0u) { return 1.0f; }
    const uint4 rec = Records[id - 1u];
    const uint a = (rec.x + prim * 3u) * 8u;
    const float2 uv = asfloat(Coords.Load2(a)) * (1.0f - b.x - b.y) + asfloat(Coords.Load2(a + 8u)) * b.x + asfloat(Coords.Load2(a + 16u)) * b.y;
    const uint w = rec.z, h = rec.w;
    // Bilinear, wrapping: nearest texels drew blocky leaf shadows.
    const float2 f = frac(uv) * float2(w, h) - 0.5f;
    const int2 b0 = int2(floor(f));
    const float2 fa = f - float2(b0);
    const uint2 m = uint2(w, h);
    const uint2 t0 = uint2((b0 + int2(m)) % int2(m)), t1 = uint2((b0 + 1 + int2(m)) % int2(m));
    const float v00 = Masks[NonUniformResourceIndex(rec.y)][t0], v10 = Masks[NonUniformResourceIndex(rec.y)][uint2(t1.x, t0.y)];
    const float v01 = Masks[NonUniformResourceIndex(rec.y)][uint2(t0.x, t1.y)], v11 = Masks[NonUniformResourceIndex(rec.y)][t1];
    return lerp(lerp(v00, v10, fa.x), lerp(v01, v11, fa.x), fa.y);
}

#define candidateAlpha(q) hitAlpha((q).CandidateInstanceID(), (q).CandidatePrimitiveIndex(), (q).CandidateTriangleBarycentrics())

// Where a point is on screen and not hidden: its colour in the game's frame.
bool onScreen(float3 hp, out float3 c) {
    const float4 s = screenOf(hp);
    c = 0.0f;
    if (s.w > 1.0f && all(s.xy >= 0.0f) && s.x < dims.x && s.y < dims.y) {
        const float zs = viewZOf(Depth.Load(int3(s.xy, 0)));
        if (abs(zs - s.w) < 0.03f * s.w + 4.0f) { c = min(GameColour.Load(int3(s.xy, 0)).rgb, 16.0f.xxx); return true; }
    }
    return false;
}

float visible(float3 o, float3 d, float tMin, float tMax) {
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    RayDesc r;
    r.Origin = o; r.Direction = d; r.TMin = tMin; r.TMax = max(tMax, tMin);
    q.TraceRayInline(Scene, 0, 0xFF, r);
    [loop] while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && candidateAlpha(q) >= alphaRef) { q.CommitNonOpaqueTriangleHit(); }
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0f : 1.0f;
}

// The surface's geometric normal from the depth buffer: the shorter-step neighbours on each axis.
float3 geometricNormal(float2 px, float3 p, float3 fallback) {
    const float2 lim = dims.xy - 1.0f;
    const float2 pxR = float2(min(px.x + 1.0f, lim.x), px.y), pxL = float2(max(px.x - 1.0f, 0.0f), px.y);
    const float2 pxD = float2(px.x, min(px.y + 1.0f, lim.y)), pxU = float2(px.x, max(px.y - 1.0f, 0.0f));
    const float3 r = worldAt(pxR, Depth.Load(int3(pxR, 0))) - p, l = p - worldAt(pxL, Depth.Load(int3(pxL, 0)));
    const float3 dn = worldAt(pxD, Depth.Load(int3(pxD, 0))) - p, u = p - worldAt(pxU, Depth.Load(int3(pxU, 0)));
    const float3 dx = dot(r, r) < dot(l, l) ? r : l;
    const float3 dy = dot(dn, dn) < dot(u, u) ? dn : u;
    float3 n = cross(dy, dx);
    const float len = length(n);
    if (len < 1.0e-6f) { return fallback; }
    n /= len;
    return dot(n, fallback) < 0.0f ? -n : n;   // the side the shading normal is on
}

#include "RtHit.hlsli"

// What a reflection ray that leaves the scene sees: the game's own sky where it is on screen (the
// direction projected, clamped into the frame and walked up towards the top row to the first sky pixel),
// else the directional ambient for that direction. Puddles read by the sky they mirror.
float3 skyAt(float3 rd) {
    const float4 s = screenOf(rd * 1.0e6f);
    if (s.w > 0.0f) {
        float2 xy = clamp(s.xy, 0.0f.xx, dims.xy - 1.0f);
        [unroll] for (int k = 0; k < 5; ++k) {
            if (Depth.Load(int3(xy, 0)) >= 1.0f) { return min(GameColour.Load(int3(xy, 0)).rgb, 16.0f.xxx); }
            xy.y = floor(xy.y * 0.5f);
        }
    }
    return max(ambientAt(rd), 0.0f.xxx) * 1.5f;
}


// One more ray from an off-screen hit, for the cache cell there: so cells the camera never sees fill,
// and light that has bounced more than once reaches the pixels through them (RtCache.h).
void cacheFromHit(float3 hp, float3 n, float2 r) {
    const float3 d = cosineDir(n, r);
    float3 bounce = 0.0f;
    float sky = 1.0f;
    if (d.z > 0.0f) {
        const float3 o = hp + n * (0.5f + 0.002f * length(hp));
        Hit h;
        if (traceHit(o, d, skyLength, h)) {
            float3 n2;
            sky = 0.0f;
            bounce = radianceAt(h, o, d, r.yx, false, n2);
        }
    }
    cacheAdd(hp, n, bounce, sky);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int3 ip = int3(id.xy, 0);
    float4 vis = 1.0f, idx = 0.0f;
    float sky = 1.0f;
    float3 bounce = 0.0f;
    const float d = Depth.Load(ip);
    const uint base = uint(Spec.Load(ip).z * 255.0f + 0.5f) & 127u;
    if (d < 1.0f && base != 0u && base < 7u) {
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, d);
        const float3 n = worldNormal(Normal.Load(ip));
        const float z = viewZOf(d);
        const float3 ng = geometricNormal(px, p, n);
        const float3 o = p + ng * (0.5f + 0.003f * z);
        const float2 r = float2(hash(px, frameIndex * 0.618f), hash(px.yx, frameIndex * 0.381f + 7.0f));
        if (sunDir.w > 0.5f && sunColour.w > 0.5f && dot(n, sunDir.xyz) > 0.0f) {
            // Near the terminator of the coarse surface: start past its own facets.
            const float turn = saturate(1.0f - dot(ng, sunDir.xyz) / 0.35f);
            vis.x = visible(o, jitterDir(sunDir.xyz, sunCone, r), termDist * turn * turn, sunLength);
        }
        // Sky rays on a checkerboard that alternates each frame; RtSkyFilter's spatial mean fills the rest.
        if (skyLength > 0.0f && skyTurn(id.xy)) {
            // Game space has z up. Below the horizon a ray stands for the ground term: seen.
            const float2 rs = float2(hash(px, frameIndex * 0.754877f + 3.0f), hash(px.yx, frameIndex * 0.569840f + 11.0f));
            const float3 sd = cosineDir(n, rs);
            if (sd.z > 0.0f) {
                Hit h;
                if (traceHit(o, sd, skyLength, h)) {
                    // On screen: the game's frame. Off screen: the hit's own material, lit with the cache
                    // (and the hit's cell takes one more ray of its own).
                    float3 hn;
                    sky = 0.0f;
                    bounce = radianceAt(h, o, sd, rs.yx, true, hn);
                    float3 c;
                    if ((giFlags & GI_CACHE) != 0u && !onScreen(o + sd * h.t, c)) { cacheFromHit(o + sd * h.t, hn, frac(rs * 1.618f + 0.31f)); }
                }
            }
            if ((giFlags & GI_CACHE) != 0u) { cacheAddWave(p, n, bounce, sky); }
        }
        // The three lights that would light this pixel most (colour luminance x window x n.l).
        uint best[3] = {~0u, ~0u, ~0u};
        float score[3] = {0.0f, 0.0f, 0.0f};
        const uint4 bits = clusters[clusterOf(px, z)];
        [loop] for (uint w = 0u; w < 4u; w++) {
            uint b = bits[w];
            [loop] while (b != 0u) {
                const uint li = w * 32u + firstbitlow(b);
                b &= b - 1u;
                if (li >= flags.y) { continue; }
                const float3 toL = lightPosR[li].xyz - p;
                const float dist = length(toL);
                const float x = saturate(dist / lightPosR[li].w);
                const float sc = dot(lightColS[li].rgb, float3(0.2126f, 0.7152f, 0.0722f)) * (1.0f - x * x) * saturate(dot(n, toL / max(dist, 1.0e-4f)));
                if (sc <= score[2]) { continue; }
                if (sc > score[0]) { best[2] = best[1]; score[2] = score[1]; best[1] = best[0]; score[1] = score[0]; best[0] = li; score[0] = sc; }
                else if (sc > score[1]) { best[2] = best[1]; score[2] = score[1]; best[1] = li; score[1] = sc; }
                else { best[2] = li; score[2] = sc; }
            }
        }
        [unroll] for (uint k = 0u; k < 3u; k++) {
            if (best[k] == ~0u) { continue; }
            const float3 c = lightPosR[best[k]].xyz + (float3(r.x, r.y, frac(r.x + r.y + 0.5f)) * 2.0f - 1.0f) * sourceRadius;
            const float3 toC = c - o;
            const float len = length(toC);
            vis[k + 1u] = visible(o, toC / max(len, 1.0e-4f), 0.0f, max(len - sourceRadius - 1.0f, 0.0f));
            idx[k] = float(best[k] + 1u) / 255.0f;
        }
    }
    // Reflection: glossy lit surfaces (the game's lobe, not foliage), one ray around the mirror
    // direction spread by the roughness.
    float4 refl = 0.0f;
    bool reflTraced = false;
    // Wet pixels (Weather.hlsli) reflect too: rain makes the ground glossy and puddles mirrors.
    Wet rain;
    rain.wet = 0.0f; rain.puddle = 0.0f; rain.snow = 0.0f;
    if (d < 1.0f && base >= 1u && base <= 4u && sunColour.w > 0.5f) {
        const float3 wp = worldAt(float2(id.xy), d), wn = worldNormal(Normal.Load(ip));
        rain = wetAt(wp, wn, geometricNormal(float2(id.xy), wp, wn), 1.0f);
    }
    const bool wetGloss = max(rain.wet, rain.puddle) * (1.0f - rain.snow) > 0.2f;   // snow covers the water
    if (reflectLength > 0.0f && d < 1.0f && base >= 1u && base <= 4u) {
        const float4 sp = Spec.Load(ip);
        if (((sp.y > 0.25f && sp.x > 0.05f) || wetGloss) && ((id.x + id.y + uint(frameIndex)) & 1u) == 0u) {
            const float2 px = float2(id.xy);
            const float3 p = worldAt(px, d);
            const float3 n = normalize(lerp(worldNormal(Normal.Load(ip)), float3(0.0f, 0.0f, 1.0f), rain.puddle));
            const float3 v = normalize(-p);
            const float shin = exp2(sp.y * 13.0f);
            const float alpha = lerp(lerp(clamp(sqrt(2.0f / (shin + 2.0f)), 0.03f, 1.0f), 0.12f, rain.wet), 0.03f, rain.puddle);
            const float2 rr = float2(hash(px, frameIndex * 0.291f + 5.0f), hash(px.yx, frameIndex * 0.733f + 2.0f));
            const float3 rd = jitterDir(reflect(-v, n), alpha * 0.5f, rr);
            const float3 ng = geometricNormal(px, p, n);
            reflTraced = true;
            if (dot(rd, ng) > 0.0f) {
                const float3 o = p + ng * (0.5f + 0.003f * viewZOf(d));
                Hit h;
                float3 c, hn;
                // On screen: the game's frame. Off screen (materials on): the hit's own shading.
                if (traceHit(o, rd, reflectLength, h)) {
                    if (onScreen(o + rd * h.t, c)) { refl = float4(c, 1.0f); }
                    else if ((giFlags & GI_MATERIALS) != 0u) { refl = float4(radianceAt(h, o, rd, rr, true, hn), 1.0f); }
                } else if (wetGloss) { refl = float4(skyAt(rd), 1.0f); }   // wet ground and puddles mirror the sky
            }
        }
    }
#ifdef RT_DEBUG
    // The debug views live in their own build (-DRT_DEBUG=1, RtScene's debug pipeline): their code in the
    // main kernel raised its register count and the trace's cost with it.
    if (reflectLength < 0.0f && d < 1.0f) {
        // RtDebug: the primary ray from the camera through the pixel, alpha-tested. Out: the committed
        // hit's texture coordinates and mask value on an alpha-tested mesh, magenta on an opaque one,
        // black when the scene has nothing there. Leaves must show as leaves, not as cards.
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        RayDesc r;
        r.Origin = 0.0f; r.Direction = normalize(worldAt(float2(id.xy), d)); r.TMin = 1.0f; r.TMax = 100000.0f;
        q.TraceRayInline(Scene, 0, 0xFF, r);
        float3 dbg = 0.0f;
        [loop] while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
                const float a = candidateAlpha(q);
                if (a >= alphaRef) { q.CommitNonOpaqueTriangleHit(); }
            }
        }
        if (debugView >= 4u) {
            // A mirror for every pixel: 4 shows only what lies off screen (on-screen hits black), 5 all of it,
            // the off-screen part shaded from the hit's material and the cache. Dark blue: the ray left.
            const float3 pw = worldAt(float2(id.xy), d);
            const float3 nw = worldNormal(Normal.Load(ip));
            const float3 md = reflect(normalize(pw), nw);
            const float3 mo = pw + nw * (0.5f + 0.003f * viewZOf(d));
            Hit mh;
            float3 mc, mn;
            dbg = float3(0.0f, 0.0f, 0.15f);
            if (traceHit(mo, md, 20000.0f, mh)) {
                dbg = onScreen(mo + md * mh.t, mc) ? (debugView == 5u ? mc : 0.0f.xxx)
                                                    : radianceAt(mh, mo, md, float2(hash(float2(id.xy), giFrame * 0.37f), hash(float2(id.yx), giFrame * 0.71f)), true, mn);
            }
        } else if (debugView == 3u && q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            // The colour copies: the primary ray's hit coloured from its mesh's copy, unlit.
            Hit h;
            h.t = q.CommittedRayT(); h.id = q.CommittedInstanceID(); h.prim = q.CommittedPrimitiveIndex();
            h.bary = q.CommittedTriangleBarycentrics(); h.o2w = q.CommittedObjectToWorld3x4();
            dbg = hitAlbedo(h);
        } else if (debugView == 2u) {
            // The cache: the ambient light its cell gives this pixel's own surface (dark red: no cell).
            const float3 pw = worldAt(float2(id.xy), d);
            const float3 nw = worldNormal(Normal.Load(ip));
            float4 c;
            dbg = cacheRead(pw, nw, 0.0f.xxx, c) ? ambientAt(nw) * (sunColour.w > 0.5f ? c.a : 1.0f) + c.rgb : float3(0.25f, 0.0f, 0.0f);
        } else if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const uint cid = q.CommittedInstanceID();
            if (cid == 0u) { dbg = float3(1.0f, 0.0f, 1.0f); }
            else {
                const uint4 rec = Records[cid - 1u];
                const uint a = (rec.x + q.CommittedPrimitiveIndex() * 3u) * 8u;
                const float2 b = q.CommittedTriangleBarycentrics();
                const float2 uv = asfloat(Coords.Load2(a)) * (1.0f - b.x - b.y) + asfloat(Coords.Load2(a + 8u)) * b.x + asfloat(Coords.Load2(a + 16u)) * b.y;
                dbg = float3(frac(uv), float(rec.y & 7u) / 7.0f);
            }
        }
        refl = float4(dbg, 1.0f);
    }
#endif
    // Glossy pixels not traced this frame keep last frame's reflection; every other pixel clears.
    const float4 sp0 = d < 1.0f ? Spec.Load(ip) : 0.0f;
    const bool glossy = reflectLength > 0.0f && d < 1.0f && base >= 1u && base <= 4u && ((sp0.y > 0.25f && sp0.x > 0.05f) || wetGloss);
    if (reflTraced || !glossy || reflectLength < 0.0f) { Refl[id.xy] = refl; }
    Vis[id.xy] = vis;
    Lights[id.xy] = float4(idx.xyz, sky);
    if (skyLength > 0.0f && skyTurn(id.xy)) { SkyRaw[id.xy] = float4(bounce, sky); }
}
