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
RWTexture2D<float4> ResIn  : register(u9);        // last frame's reservoirs: light + 1, W, M, view z (RtRestReuse)
RWTexture2D<float4> ResOut : register(u10);
Texture2D<float2> Motion   : register(t7);        // the game's motion vectors (uv units, to last frame)
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

// How much light li would give the point (luminance x window x n.l): the trace's light choice and ReSTIR's target.
float lightShare(uint li, float3 p, float3 n) {
    const float3 toL = lightPosR[li].xyz - p;
    const float dist = length(toL);
    const float x = saturate(dist / lightPosR[li].w);
    return dot(lightColS[li].rgb, float3(0.2126f, 0.7152f, 0.0722f)) * (1.0f - x * x) * saturate(dot(n, toL / max(dist, 1.0e-4f)));
}

bool lodHidden(uint id, float3 hp);   // RtHit.hlsli: a distant-land hit inside the loaded area

// The alpha of a candidate hit on an alpha-tested mesh (the instance ID's record bits); 1 on a mesh with no mask.
float hitAlpha(uint id, uint prim, float2 b) {
    id &= 0x7FFFFFu;
    if (id == 0u || Records[id - 1u].z == 0u) { return 1.0f; }
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
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && !lodHidden(q.CandidateInstanceID(), o + d * q.CandidateTriangleRayT()) &&
            candidateAlpha(q) >= alphaRef) { q.CommitNonOpaqueTriangleHit(); }
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

// Distant LOD (class 7): terrain, trees and buildings past the loaded cells, which the ray-tracing scene does not
// hold. Their rays march through the depth buffer instead, in steps that grow by a third each, and only other
// distant LOD counts as an occluder: everything nearer is in the scene, and in screen space it would only stand in
// front. A hit is a pixel that sits nearer than the ray's point by more than 0.4 % and less than a quarter of its
// distance plus 2,000 units (a ridge, not something far in front of it). So a ridge shadows the valley beyond the
// cascades, a slope under a ridge sees less sky, and the light the ridge sends back is its colour in the game's frame.
bool farOccluded(float3 o, float3 dir, float tMax, uint steps, out float3 colour) {
    colour = 0.0f;
    float t = 0.0f, step = 0.01f * max(length(o), 100.0f);
    [loop] for (uint k = 0u; k < steps; k++) {
        t += step;
        step *= 1.33f;
        if (t > tMax) { break; }
        const float4 s = screenOf(o + dir * t);
        if (s.w <= 1.0f || any(s.xy < 0.0f) || s.x >= dims.x || s.y >= dims.y) { break; }
        const int3 sp = int3(s.xy, 0);
        const float dq = Depth.Load(sp);
        if (dq >= 1.0f || (uint(Spec.Load(sp).z * 255.0f + 0.5f) & 127u) != 7u) { continue; }
        const float zs = viewZOf(dq);
        if (zs < s.w * 0.996f && s.w - zs < 0.25f * s.w + 2000.0f) {
            colour = min(GameColour.Load(sp).rgb, 16.0f.xxx);
            return true;
        }
    }
    return false;
}

// What a reflection ray that leaves the scene sees: the game's own sky where it is on screen (the
// direction projected, clamped into the frame and walked up towards the top row to the first sky pixel),
// else the directional ambient for that direction. Puddles read by the sky they mirror.
float3 skyAt(float3 rd) {
    const float4 s = screenOf(rd * 1.0e6f);
    if (s.w > 0.0f && all(s.xy >= 2.0f.xx) && all(s.xy <= dims.xy - 3.0f)) {
        float2 xy = s.xy;
        [unroll] for (int k = 0; k < 4; ++k) {
            if (Depth.Load(int3(xy, 0)) >= 1.0f) { return min(GameColour.Load(int3(xy, 0)).rgb, 3.0f.xxx); }
            xy.y = floor(xy.y * 0.5f);
        }
    }
    return evalDynamicEnvCube(rd, 0.14f);
}


// Second-bounce hardware RayQuery from a hit (exterior or interior): populates the persistent world radiance cache
// so sunlight and interior torchlight propagate cleanly across 2+ bounces into deep corners and under roofs.
void cacheFromHit(float3 hp, float3 n, float2 r, bool isInterior) {
    const float3 d = cosineDir(n, r);
    float3 bounce = 0.0f;
    float sky = 1.0f;
    if (isInterior || d.z > 0.0f) {
        const float3 o = hp + n * (0.65f + 0.0025f * length(hp));
        const float bLen = isInterior ? min(skyLength, 240.0f) : skyLength;
        Hit h;
        if (traceHit(o, d, bLen, h)) {
            float3 n2;
            sky = isInterior ? saturate(h.t / max(bLen, 1.0f)) : 0.0f;
            bounce = radianceAt(h, o, d, r.yx, false, n2);
        }
    }
    cacheAdd(hp, n, bounce, sky);
}

#ifndef RT_NO_MAIN   // RtWater.hlsl includes the bindings and helpers above, not this kernel
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int3 ip = int3(id.xy, 0);
    float4 vis = 1.0f, idx = 0.0f;
    float sky = 1.0f;
    float3 bounce = 0.0f;
    float restCode = 0.0f;   // 4 + 2 x the untraced lights' share in fifteenths, when one was sampled (RtSkyFilter decodes it)
    const float d = Depth.Load(ip);
    const uint base = uint(Spec.Load(ip).z * 255.0f + 0.5f) & 127u;
    if (d < 1.0f && base != 0u && (base < 7u || base == 10u)) {   // 10: skin
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, d);
        const float3 n = worldNormal(Normal.Load(ip));
        const float z = viewZOf(d);
        const float3 ng = geometricNormal(px, p, n);
        // Stronger normal offset on skinned meshes (base == 10) and general surfaces to eliminate RT terminator/self-shadow acne.
        const float oScale = (base == 10u) ? (1.80f + 0.008f * z) : (0.85f + 0.005f * z);
        const float3 o = p + ng * oScale;
        const float2 r = float2(hash(px, frameIndex * 0.618f), hash(px.yx, frameIndex * 0.381f + 7.0f));
        if (sunDir.w > 0.5f && sunColour.w > 0.5f && dot(n, sunDir.xyz) > 0.0f) {
            // Near the terminator of the coarse surface: start past its own facets.
            // Overdrive 2-spp stratified RayQuery soft sun penumbra shadows.
            const float turn = saturate(1.0f - dot(ng, sunDir.xyz) / 0.40f);
            const float tMinS = max(termDist, 1.8f) * turn * turn;
            const float2 r2 = frac(r + float2(0.5f, 0.5f));
            const float v0 = visible(o, jitterDir(sunDir.xyz, sunCone, r), tMinS, sunLength);
            const float v1 = visible(o, jitterDir(sunDir.xyz, sunCone, r2), tMinS, sunLength);
            vis.x = 0.5f * (v0 + v1);
        }
        // Full-resolution 2-spp stratified SM6.5 RayQuery sky/RTAO + GI bounce (4x sample density vs 0.5-spp checkerboard).
        const bool isInterior = sunDir.w <= 0.5f;
        const float aoLen = isInterior ? (skyLength > 0.0f ? min(skyLength, 220.0f) : 180.0f) : skyLength;
        if (aoLen > 0.0f) {
            const float2 rs0 = float2(hash(px, frameIndex * 0.754877f + 3.0f), hash(px.yx, frameIndex * 0.569840f + 11.0f));
            float skySum = 0.0f;
            float3 bounceSum = 0.0f;
            [unroll] for (uint si = 0u; si < 2u; si++) {
                const float2 rs = frac(rs0 + float2(0.5f * float(si), 0.381966f * float(si)));
                const float3 sd = cosineDir(n, rs);
                float sSample = 1.0f;
                float3 bSample = 0.0f;
                if (isInterior || sd.z > 0.0f) {
                    Hit h;
                    if (traceHit(o, sd, aoLen, h)) {
                        float3 hn;
                        sSample = isInterior ? saturate(h.t / aoLen) : 0.0f;
                        bSample = radianceAt(h, o, sd, rs.yx, true, hn);
                        float3 c;
                        if (si == 0u && (giFlags & GI_CACHE) != 0u && !onScreen(o + sd * h.t, c)) {
                            cacheFromHit(o + sd * h.t, hn, frac(rs * 1.618f + 0.31f), isInterior);
                        }
                    }
                }
                skySum += sSample;
                bounceSum += bSample;
            }
            sky = 0.5f * skySum;
            bounce = 0.5f * bounceSum;
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
        // Every other light of the cluster. Each frame, every pixel picks one of them in proportion to the light it
        // would give (weighted reservoir sampling over the whole list: the pick is exact). With RtRestReuse the pick
        // is then resampled against last frame's reservoirs here and at two neighbours, found along the motion
        // vector (ReSTIR's temporal and spatial reuse, Bitterli et al. 2020; target the unshadowed light, history
        // capped at 20 samples, a neighbour kept only at the same view depth). On the sky ray's turn the kept light
        // gets one shadow ray, and p(y) V(y) W over the exact unshadowed sum is the share of all those lights'
        // light that arrives (the ratio estimator of Heitz et al. 2018), filtered with the sky rays.
        if ((giFlags & GI_REST) != 0u) {
            float total = 0.0f;
            uint pick = ~0u;
            [loop] for (uint w2 = 0u; w2 < 4u; w2++) {
                uint b2 = bits[w2];
                [loop] while (b2 != 0u) {
                    const uint li = w2 * 32u + firstbitlow(b2);
                    b2 &= b2 - 1u;
                    if (li >= flags.y || li == best[0] || li == best[1] || li == best[2]) { continue; }
                    const float sc = lightShare(li, p, n);
                    if (sc <= 0.0f) { continue; }
                    total += sc;
                    if (hash(px + float(li) * 0.37f, frameIndex * 0.917f + 13.0f) * total < sc) { pick = li; }
                }
            }
            float wsum = total, M = pick != ~0u ? 1.0f : 0.0f;
            if ((giFlags & GI_REUSE) != 0u && total > 0.0f) {
                const float2 mv = Motion.Load(ip);
                const float2 prev = px + mv * dims.xy;
                [unroll] for (uint k = 0u; k < 3u; k++) {
                    const float a = hash(px, frameIndex * 0.37f + float(k) * 3.1f) * 6.2831853f;
                    const float rad = k == 0u ? 0.0f : 4.0f + 12.0f * hash(px.yx, frameIndex * 0.53f + float(k));
                    const int2 q = int2(prev + float2(cos(a), sin(a)) * rad);
                    if (any(q < 0) || q.x >= int(dims.x) || q.y >= int(dims.y)) { continue; }
                    const float4 rq = ResIn[q];
                    if (rq.x < 0.5f || abs(rq.w - z) > 0.05f * z) { continue; }
                    const uint yq = uint(rq.x + 0.5f) - 1u;
                    if (yq >= flags.y || yq == best[0] || yq == best[1] || yq == best[2]) { continue; }
                    const float mq = min(rq.z, 20.0f);
                    const float wq = lightShare(yq, p, n) * rq.y * mq;
                    M += mq;
                    if (wq <= 0.0f) { continue; }
                    wsum += wq;
                    if (hash(px + float(k) * 1.7f, frameIndex * 0.811f + 29.0f) * wsum < wq) { pick = yq; }
                }
            }
            const float py = pick != ~0u ? lightShare(pick, p, n) : 0.0f;
            const float W = py > 0.0f && M > 0.0f ? wsum / (M * py) : 0.0f;
            ResOut[id.xy] = float4(pick != ~0u ? float(pick + 1u) : 0.0f, W, M, z);
            if (pick != ~0u && skyLength > 0.0f && skyTurn(id.xy)) {
                const float3 c = lightPosR[pick].xyz + (float3(r.y, r.x, frac(r.x * 1.7f + 0.3f)) * 2.0f - 1.0f) * sourceRadius;
                const float3 toC = c - o;
                const float len = length(toC);
                const float v = visible(o, toC / max(len, 1.0e-4f), 0.0f, max(len - sourceRadius - 1.0f, 0.0f));
                const float share = saturate(py * v * W / max(total, 1.0e-6f));
                restCode = 4.0f + 2.0f * round(share * 15.0f);   // + sky (0 or 1); RtSkyFilter decodes q = (a - 4 - sky) / 2
            }
        }
    }
    // Reflection: glossy lit surfaces (the game's lobe, not foliage), one ray around the mirror
    // direction spread by the roughness.
    if (d < 1.0f && base == 7u && (giFlags & GI_FAR) != 0u) {
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, d);
        const float3 n = worldNormal(Normal.Load(ip));
        const float3 o = p + n * (4.0f + 0.002f * viewZOf(d));
        float3 c;
        if (sunDir.w > 0.5f && sunColour.w > 0.5f) { vis.x = farOccluded(o, sunDir.xyz, 150000.0f, 26u, c) ? 0.0f : 1.0f; }
        if (skyLength > 0.0f && skyTurn(id.xy)) {
            const float2 rs = float2(hash(px, frameIndex * 0.754877f + 3.0f), hash(px.yx, frameIndex * 0.569840f + 11.0f));
            const float3 sd = cosineDir(n, rs);
            if (sd.z > 0.0f && farOccluded(o, sd, 60000.0f, 20u, c)) { sky = 0.0f; bounce = c; }
        }
    }
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
        if ((sp.y > 0.25f && sp.x > 0.05f) || wetGloss) {
            const float2 px = float2(id.xy);
            const float3 p = worldAt(px, d);
            const float3 n = normalize(lerp(worldNormal(Normal.Load(ip)), float3(0.0f, 0.0f, 1.0f), rain.puddle));
            const float3 v = normalize(eyeAt() - p);
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
    if (reflectLength < 0.0f && d < 1.0f && debugView == 6u) {
        // RtDebug=6: a path-traced reference of the light each pixel receives (the irradiance the lighting
        // pass multiplies the albedo by, in the game's units: a light of colour c facing the surface gives c).
        // Sun and one point light by next-event estimation with shadow rays, then up to three diffuse bounces
        // whose hits take their colour from the meshes' colour copies; a ray that leaves the scene sees the
        // sky as the game's directional ambient in its direction. No radiance cache, no screen lookups. The
        // mean over frames accumulates in the reflection output (alpha 1000 + samples); the camera must hold.
        const float2 px = float2(id.xy);
        const float3 pw = worldAt(px, d);
        float3 n = worldNormal(Normal.Load(ip));
        float3 ng = geometricNormal(px, pw, n);
        float3 o = pw + ng * (0.5f + 0.003f * viewZOf(d));
        float3 E = 0.0f, thr = 1.0f;
        [loop] for (uint b = 0u; b < 4u; b++) {
            const float2 r0 = float2(hash(px, giFrame * 0.37f + b * 3.1f), hash(px.yx, giFrame * 0.71f + b * 5.7f));
            const float2 r1 = float2(hash(px + 17.0f, giFrame * 0.53f + b * 1.3f), hash(px.yx + 29.0f, giFrame * 0.29f + b * 2.9f));
            float3 dl = 0.0f;   // direct light at this vertex of the path
            if (sunDir.w > 0.5f) {
                const float nl = dot(n, sunDir.xyz);
                if (nl > 0.0f) {
                    const float vis = sunColour.w > 0.5f ? visible(o, jitterDir(sunDir.xyz, sunCone, r0), 0.0f, sunLength) : 1.0f;
                    dl += sunColour.rgb * nl * vis;
                }
            }
            if (flags.y > 0u) {
                const uint li = min(uint(r1.x * float(flags.y)), flags.y - 1u);
                const float3 toL = lightPosR[li].xyz - o;
                const float dist = length(toL);
                const float x = saturate(dist / lightPosR[li].w);
                const float nl = dot(n, toL / max(dist, 1.0e-4f));
                if (x < 1.0f && nl > 0.0f)
                    dl += lightColS[li].rgb * ((1.0f - x * x) * nl * float(flags.y) * visible(o, toL / max(dist, 1.0e-4f), 0.0f, max(dist - 2.0f, 0.0f)));
            }
            E += thr * dl;
            if (b == 3u) { break; }
            const float3 rd = cosineDir(n, r1.yx);
            if (dot(rd, ng) <= 0.0f) { break; }
            Hit h;
            if (!traceHit(o, rd, 60000.0f, h)) { E += thr * max(ambientAt(rd), 0.0f.xxx); break; }
            const float3 hp = o + rd * h.t;
            n = hitNormal(h, rd);
            ng = n;
            thr *= hitAlbedo(h);
            o = hp + n * (0.5f + 0.002f * length(hp));
        }
        const float4 old = Refl[id.xy];
        const float cnt = old.a > 1000.5f ? old.a - 1000.0f : 0.0f;
        const float3 mean = (old.rgb * cnt + E) / (cnt + 1.0f);
        Refl[id.xy] = float4(mean, 1000.0f + min(cnt + 1.0f, 1040.0f));
        Vis[id.xy] = vis;
        Lights[id.xy] = float4(idx.xyz, sky);
        return;
    }
    // The debug views live in their own build (-DRT_DEBUG=1, RtScene's debug pipeline): their code in the
    // main kernel raised its register count and the trace's cost with it.
    if (reflectLength < 0.0f && d < 1.0f) {
        // RtDebug: the primary ray from the camera through the pixel, alpha-tested. Out: the committed
        // hit's texture coordinates and mask value on an alpha-tested mesh, magenta on an opaque one,
        // black when the scene has nothing there. Leaves must show as leaves, not as cards.
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        RayDesc r;
        const float3 eye = eyeAt();
        r.Origin = eye; r.Direction = normalize(worldAt(float2(id.xy), d) - eye); r.TMin = 1.0f; r.TMax = 100000.0f;
        q.TraceRayInline(Scene, 0, 0xFF, r);
        float3 dbg = 0.0f;
        [loop] while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
                if (lodHidden(q.CandidateInstanceID(), r.Origin + r.Direction * q.CandidateTriangleRayT())) { continue; }
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
            h.t = q.CommittedRayT(); h.id = q.CommittedInstanceID() & 0x7FFFFFu; h.prim = q.CommittedPrimitiveIndex();
            h.bary = q.CommittedTriangleBarycentrics(); h.o2w = q.CommittedObjectToWorld3x4();
            dbg = hitAlbedo(h);
        } else if (debugView == 2u) {
            // The cache: the ambient light its cell gives this pixel's own surface (dark red: no cell).
            const float3 pw = worldAt(float2(id.xy), d);
            const float3 nw = worldNormal(Normal.Load(ip));
            float4 c;
            dbg = cacheRead(pw, nw, 0.0f.xxx, c) ? ambientAt(nw) * (sunColour.w > 0.5f ? c.a : 1.0f) + c.rgb : float3(0.25f, 0.0f, 0.0f);
        } else if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const uint cid = q.CommittedInstanceID() & 0x7FFFFFu;
            if (cid == 0u || Records[cid - 1u].z == 0u) { dbg = float3(1.0f, 0.0f, 1.0f); }
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
    if (skyLength > 0.0f && skyTurn(id.xy)) { SkyRaw[id.xy] = float4(bounce, sky + restCode); }
}
#endif
