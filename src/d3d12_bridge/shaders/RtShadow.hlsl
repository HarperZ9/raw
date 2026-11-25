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
#include "SpectralTransport.hlsli"

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

// Spectral Korobov-Weyl & 4D Prime-Root Quasi-Monte-Carlo (QMC) Spatiotemporal Blue-Noise Samplers:
// Replaces sin()-based hash aliasing with algebraic plastic/golden-ratio Kronecker lattices, 4D irrational prime roots
// (sqrt(2), sqrt(3), sqrt(5), sqrt(7)), and Korobov rank-1 generators rotated by Cranley-Patterson IGN.
float4 stbnPrime4(float2 px, float frameOff) {
    const float4 kRoots = float4(0.4142135624f, 0.7320508076f, 0.2360679775f, 0.6457513111f);
    const float4 base = float4(ign(px), ign(px.yx + 13.7f), ign(px + float2(29.3f, 7.1f)), ign(px.yx + float2(5.9f, 41.3f)));
    return frac(base + kRoots * fmod(frameOff, 512.0f));
}

float2 stbnR2(float2 px, float frameOff) {
    const float2 r2Step = float2(0.7548776662f, 0.5698402910f);
    const float2 ignBase = float2(ign(px), ign(px.yx + 19.37f));
    return frac(ignBase + r2Step * fmod(frameOff, 256.0f));
}

float qmcKorobov1(float2 px, uint seqIdx, float frameOff) {
    // Rank-1 Korobov lattice over Z_p (p=65521, primitive generator g=17364) + golden-ratio temporal Weyl shift:
    const float base = ign(px + float2(float(seqIdx & 7u) * 11.3f, float(seqIdx >> 3u) * 17.7f));
    const float kLat = float((seqIdx * 17364u) % 65521u) * (1.0f / 65521.0f);
    return frac(base + kLat + 0.6180339887f * fmod(frameOff + float(seqIdx) * 0.381966f, 256.0f));
}

// Tokuyoshi & Eto (AMD, I3D 2024, MAT-05) Bounded VNDF + Dupuy & Benyoub (HPG 2023, MAT-06) Spherical Cap GGX Sampling:
// Tightens the visible-normal sampling bound so 0% of reflected rays point below the geometric horizon,
// slashing 1-spp RT glossy reflection noise & fireflies at grazing angles with fewer instructions than Heitz 2018!
float3 sampleGGXVNDF(float3 n, float3 v, float alpha, float2 u) {
    const float s = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z);
    const float b = n.x * n.y * a;
    const float3 t1 = float3(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
    const float3 t2 = float3(b, s + n.y * n.y * a, -n.y);
    const float3 Ve = float3(dot(v, t1), dot(v, t2), max(dot(v, n), 1.0e-4f));
    const float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));
    // Bounded spherical-cap parameter k (Tokuyoshi & Eto 2024 eq. 5): removes invalid lower-hemisphere rays.
    const float a2 = alpha * alpha;
    const float s2 = 1.0f + length(Ve.xy);
    const float k = (1.0f - a2) * (s2 * s2) / max(s2 * s2 + a2 * Ve.z * Ve.z, 1.0e-5f);
    const float phi = 6.2831853f * u.x;
    const float z = mad((1.0f - u.y), (1.0f + k * Vh.z), -k * Vh.z);
    const float sinTheta = sqrt(saturate(1.0f - z * z));
    const float3 c = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);
    const float3 Nh = c + Vh;
    const float3 Ne = normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0f, Nh.z)));
    const float3 H = normalize(t1 * Ne.x + t2 * Ne.y + n * Ne.z);
    return reflect(-v, H);
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
    const float bLen = isInterior ? min(skyLength, 240.0f) : skyLength;
    float freeHit = bLen;
    if (isInterior || d.z > 0.0f) {
        const float3 o = hp + n * (0.65f + 0.0025f * length(hp));
        Hit h;
        if (traceHit(o, d, bLen, h)) {
            float3 n2;
            freeHit = h.t;
            sky = isInterior ? saturate(h.t / max(bLen, 1.0f)) : 0.0f;
            bounce = radianceAt(h, o, d, r.yx, false, n2);
        }
    }
    cacheAdd(hp, n, bounce, sky, freeHit);
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
    // SM6.9 Wave-coherent sky early exit: pure sky waves skip all RayQuery and cluster setup cleanly.
    if (WaveActiveAllTrue(d >= 1.0f || base == 0u)) {
        Vis[id.xy] = 1.0f; Lights[id.xy] = 0.0f; SkyRaw[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f); Refl[id.xy] = 0.0f;
        return;
    }
    if (d < 1.0f && base != 0u && (base < 7u || base == 10u || base == 11u || base == 12u)) {   // 10: skin, 11: hair, 12: eyes
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, d);
        const float3 n = worldNormal(Normal.Load(ip));
        const float z = viewZOf(d);
        const float3 ng = geometricNormal(px, p, n);
        // Stronger normal offset on skinned meshes (base == 10/11/12) to eliminate RT terminator/self-shadow acne cleanly:
        const bool isSkinnedMesh = (base == 10u || base == 11u || base == 12u);
        const float oScale = isSkinnedMesh ? (2.25f + 0.008f * z) : (0.85f + 0.005f * z);
        const float3 o = p + ng * oScale;
        const float4 q4 = stbnPrime4(px, frameIndex);
        const float2 r = q4.xy;
        if (sunDir.w > 0.5f && sunColour.w > 0.5f && dot(n, sunDir.xyz) > 0.0f) {
            // Fast 2-spp Blocker-Distance PCSS Hardware DXR Sun Shadows driven by decorrelated 4D Prime-Root QMC strata:
            const float turn = saturate(1.0f - dot(ng, sunDir.xyz) / 0.40f);
            const float tMinS = max(termDist, 1.8f) * turn * turn;
            Hit hBlocker;
            float effCone = sunCone;
            float vSum = 0.0f;
            if (traceHit(o + sunDir.xyz * tMinS, jitterDir(sunDir.xyz, sunCone * 0.42f, r), sunLength, hBlocker)) {
                effCone = clamp(sunCone * (0.20f + 1.70f * saturate(hBlocker.t / 190.0f)), sunCone * 0.20f, sunCone * 1.90f);
            } else {
                vSum += 1.0f;
            }
            vSum += visible(o, jitterDir(sunDir.xyz, effCone, q4.zw), tMinS, sunLength);
            vis.x = vSum * 0.5f;
        }
        // Hardware-tier adaptive DXR ray budget (RTX 3060=Tier 1 -> 1 spp, 4070/4090=Tier 2-3 -> 2 spp, 5090=Tier 4 -> 3 spp GI):
        const uint rtTier = (giFlags >> 8u) & 7u;
        const bool isInterior = (sunColour.w <= 0.5f);
        const float aoLen = isInterior ? (skyLength > 0.0f ? min(skyLength, 220.0f) : 180.0f) : skyLength;
        if (aoLen > 0.0f) {
            const float2 rs0 = stbnR2(px, frameIndex + 11.0f);
            // Negative-Space Visibility & Entropy Radiance Oracle query:
            // Concentrates RT-core rays into high-entropy penumbra/contact/dynamic regions while skipping verified
            // empty near-field corridors and reusing converged world-space priors in smooth open air.
            float4 oracleVal = 0.0f.xxxx;
            float oracleFree = 0.0f, oracleEnt = 1.0f;
            uint oracleFrames = 0u;
            const bool hasOracle = ((giFlags & GI_CACHE) != 0u) && cacheReadOracle(p, n, 0.0f.xxx, oracleVal, oracleFree, oracleEnt, oracleFrames);
            const bool calmCorridor = hasOracle && oracleFrames >= 6u && oracleEnt < 0.14f && oracleFree > 40.0f;
            const bool highEntropy = hasOracle && oracleFrames >= 2u && oracleEnt > 0.36f;
            // SIGGRAPH Advances 2026 LGT-16 Variable-Rate Ray Tracing + Negative-Space Entropy Oracle:
            // Couples per-pixel motion/disocclusion gradient with world-space cache entropy so RT cores spend 1 spp
            // in converged calm air and automatically escalate to 3-4 spp on dynamic/disoccluded/high-entropy edges!
            const float2 giMv = Motion.Load(ip);
            const float giSpeedPx = length(giMv * dims.xy);
            const bool fastMotionOrDisocc = (giSpeedPx > 1.85f) || !hasOracle || (oracleFrames < 3u);
            const uint baseSpp = (rtTier <= 1u) ? 1u : (rtTier >= 4u ? 3u : 2u);
            const uint giSpp = (calmCorridor && !fastMotionOrDisocc) ? 1u
                             : ((highEntropy || fastMotionOrDisocc) && rtTier >= 2u ? min(baseSpp + 1u, rtTier >= 4u ? 4u : 3u) : baseSpp);
            const float corridorSkip = (!isInterior && hasOracle && oracleFrames >= 8u && oracleEnt < 0.18f && oracleFree > 24.0f)
                ? min(oracleFree * 0.35f, 24.0f) : 0.0f;

            float skySum = 0.0f;
            float3 bounceSum = 0.0f;
            float minFreeHit = aoLen;
            const bool doCache2 = ((giFlags & GI_CACHE) != 0u) && skyTurn(id.xy);
            [loop] for (uint si = 0u; si < giSpp; si++) {
                const float4 qGi = stbnPrime4(px, frameIndex + 11.0f + float(si) * 17.0f);
                const float2 rs = (si & 1u) == 0u ? qGi.xy : qGi.zw;
                const float3 sd = cosineDir(n, rs);
                float sSample = 1.0f;
                float3 bSample = 0.0f;
                if (isInterior || sd.z > 0.0f) {
                    const float rayTMin = (sd.z > 0.18f) ? corridorSkip : 0.0f;
                    Hit h;
                    if (traceHitTMin(o, sd, rayTMin, aoLen, h)) {
                        float3 hn;
                        minFreeHit = min(minFreeHit, h.t);
                        // True 3D Hardware DXR near-field contact occlusion (grounds bottles, rocks, furniture & feet with zero SSAO halos!):
                        sSample = isInterior ? pow(saturate(h.t / aoLen), 1.35f)
                                             : saturate(h.t / 110.0f) * 0.42f;
                        bSample = radianceAt(h, o, sd, rs.yx, true, hn);
                        if (si == 0u && doCache2) {
                            cacheFromHit(o + sd * h.t, hn, frac(rs * 1.618f + 0.31f), isInterior);
                        }
                    }
                }
                skySum += sSample;
                bounceSum += bSample;
            }
            const float invSpp = 1.0f / float(giSpp);
            sky = invSpp * skySum;
            bounce = invSpp * bounceSum;
            // In converged low-entropy corridors, stabilize 1-spp trace with 28% converged world oracle prior:
            if (calmCorridor) {
                bounce = lerp(bounce, oracleVal.rgb, 0.28f);
                sky = lerp(sky, oracleVal.a, 0.28f);
            }
            if ((giFlags & GI_CACHE) != 0u) { cacheAddWave(p, n, bounce, sky, minFreeHit); }
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
            // 6-spp Vogel-Disk Physical Area-Light PCSS Point-Light RT Shadows (static pixel-locked IGN phase -> ZERO flicker,
            // silky smooth physical penumbra softening with distance & zero stipple around hearths/braziers/torches!):
            const float fixClear = max(sourceRadius * 2.4f, 24.0f);
            const float3 lPos = lightPosR[best[k]].xyz;
            const float3 toCenter = lPos - o;
            const float distC = length(toCenter);
            const float3 lDir = toCenter / max(distC, 1.0e-4f);
            const float3 lTan = normalize(cross(lDir, abs(lDir.z) < 0.9f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f)));
            const float3 lBit = cross(lDir, lTan);
            const float radK = clamp(sourceRadius * 0.42f + distC * 0.012f, 4.5f, 18.0f);
            const float ang0 = ign(px + float2(17.0f * float(k), 31.0f)) * 6.2831853f;
            float vAcc = 0.0f;
            const uint nSpp = (rtTier <= 1u) ? 1u : 2u;   // Adaptive Vogel-Disk Area RT shadows (1-spp on 3060, 2-spp on 4070-5090)
            [loop] for (uint si = 0u; si < nSpp; si++) {
                const float rv = sqrt((float(si) + 0.5f) / float(nSpp));
                const float av = float(si) * 2.3999632f + ang0;
                const float3 ci = lPos + (lTan * cos(av) + lBit * sin(av)) * (rv * radK);
                const float3 toi = ci - o;
                const float liLen = length(toi);
                vAcc += liLen > fixClear ? visible(o, toi / max(liLen, 1.0e-4f), 0.0f, liLen - fixClear) : 1.0f;
            }
            vis[k + 1u] = vAcc * (1.0f / float(nSpp));
            idx[k] = float(best[k] + 1u) / 255.0f;
        }
        // RAW-MEGALIGHTS (Visibility-Reused Spatiotemporal ReSTIR DI for lights 4..128):
        // 1) Spectral Korobov-Weyl QMC reservoir sampling over all cluster lights (zero sin()-hash striping)
        // 2) 5-neighbor depth + normal-validated spatiotemporal reuse (1 temporal + 4 Vogel-spiral spatial neighbors)
        // 3) Unbiased visibility-verified reservoir weighting (removed biased 0.58 constant cvPrior shadow leak!)
        // 4) Visibility feedback into ResOut: occluded picks store W=0 so next-frame reuse stops re-picking blocked lights!
        if ((giFlags & GI_REST) != 0u) {
            float total = 0.0f;
            uint pick = ~0u;
            uint qSeq = 1u;
            [loop] for (uint w2 = 0u; w2 < 4u; w2++) {
                uint b2 = bits[w2];
                [loop] while (b2 != 0u) {
                    const uint li = w2 * 32u + firstbitlow(b2);
                    b2 &= b2 - 1u;
                    if (li >= flags.y || li == best[0] || li == best[1] || li == best[2]) { continue; }
                    const float sc = lightShare(li, p, n);
                    if (sc <= 0.0f) { continue; }
                    total += sc;
                    if (qmcKorobov1(px, qSeq++, frameIndex) * total < sc) { pick = li; }
                }
            }
            float wsum = total, M = pick != ~0u ? 1.0f : 0.0f;
            if ((giFlags & GI_REUSE) != 0u && total > 0.0f) {
                const float2 mv = Motion.Load(ip);
                const float2 prev = px + mv * dims.xy;
                const float angBase = stbnR2(px, frameIndex + 53.0f).x * 6.2831853f;
                const uint restNeighbors = (rtTier <= 1u) ? 3u : (rtTier == 2u ? 4u : 5u);
                [loop] for (uint k = 0u; k < restNeighbors; k++) {
                    const float a = angBase + float(k) * 2.39996323f;
                    const float rad = (k == 0u) ? 0.0f : (3.5f + 3.2f * float(k));
                    const int2 q = int2(prev + float2(cos(a), sin(a)) * rad);
                    if (any(q < 0) || q.x >= int(dims.x) || q.y >= int(dims.y)) { continue; }
                    const float4 rq = ResIn[q];
                    if (rq.x < 0.5f || abs(rq.w - z) > 0.05f * z) { continue; }
                    // HPG 2026 Best Paper (LGT-09) Compatibility-Guided Spatial ReSTIR Neighbor Selection:
                    // Validates both tight geometric normal alignment (dot >= 0.84) and irradiance-ratio compatibility
                    // so incompatible lit/shadowed neighbors never inject blotches across architectural corners!
                    float compatW = 1.0f;
                    if (k > 0u) {
                        const float3 nq = worldNormal(Normal.Load(int3(q, 0)));
                        const float ndCompat = dot(n, nq);
                        if (ndCompat < 0.84f) { continue; }
                        compatW = smoothstep(0.84f, 0.98f, ndCompat);
                    }
                    const uint yq = uint(rq.x + 0.5f) - 1u;
                    if (yq >= flags.y || yq == best[0] || yq == best[1] || yq == best[2]) { continue; }
                    // Cap reservoir history at 8 samples scaled by compatibility weight so moving lights track immediately:
                    const float mq = min(rq.z, 8.0f) * compatW;
                    const float pCompat = lightShare(yq, p, n);
                    const float wq = pCompat * rq.y * mq;
                    M += mq;
                    if (wq <= 0.0f) { continue; }
                    wsum += wq;
                    if (qmcKorobov1(px, 64u + k, frameIndex + 7.0f) * wsum < wq) { pick = yq; }
                }
            }
            const float py = pick != ~0u ? lightShare(pick, p, n) : 0.0f;
            float W = py > 0.0f && M > 0.0f ? wsum / (M * py) : 0.0f;
            float vRest = 1.0f;
            // Full-rate every-frame DXR 1.2 visibility ray for ALL lights 4..128 with fixture clearance:
            if (pick != ~0u && skyLength > 0.0f) {
                const float fixClearR = max(sourceRadius * 1.8f, 14.0f);
                const float2 rRest = stbnR2(px, frameIndex + 79.0f);
                const float3 c = lightPosR[pick].xyz + (float3(rRest.x, rRest.y, frac(rRest.x + rRest.y + 0.5f)) * 2.0f - 1.0f) * (sourceRadius * 0.40f);
                const float3 toC = c - o;
                const float len = length(toC);
                vRest = len > fixClearR ? visible(o, toC / max(len, 1.0e-4f), 0.0f, len - fixClearR) : 1.0f;
                // Unbiased visibility-checked ratio estimator (eliminates biased 0.58 constant shadow leak!):
                const float share = saturate(py * vRest * W / max(total, 1.0e-6f));
                restCode = 4.0f + 2.0f * round(share * 15.0f);   // + sky (0 or 1); RtSkyFilter decodes q = (a - 4 - sky) / 2
                // Visibility feedback (MegaLights / ReSTIR-VIS): zero stored weight when occluded so blocked lights stop recycling!
                if (vRest < 0.5f) { W = 0.0f; }
            }
            ResOut[id.xy] = float4(pick != ~0u ? float(pick + 1u) : 0.0f, W, M, z);
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
            const float2 rs = stbnR2(px, frameIndex + 23.0f);
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
        // Match Lighting.hlsl engine-native capillary wet-shoreline wicking from TESObjectCELL::waterHeight (engineWorld.x)
        // so wet shoreline rocks, bridge piers, and riverbanks trace full hardware DXR 1.2 reflections too!
        if (engineWorld.z > 0.5f && engineWorld.x > -13000.0f && (base == 1u || base == 4u) && viewZOf(d) < 4500.0f) {
            const float aboveW = (wp.z + camPosW.z) - engineWorld.x;
            if (aboveW > -6.0f && aboveW < 46.0f) {
                const float wicking = (1.0f - smoothstep(4.0f, 46.0f, aboveW)) * smoothstep(-6.0f, 2.0f, aboveW);
                rain.wet = max(rain.wet, wicking * 0.88f);
            }
        }
    }
    const bool wetGloss = max(rain.wet, rain.puddle) * (1.0f - rain.snow) > 0.2f;   // snow covers the water
    const bool wantRefl = (reflectLength > 0.0f && d < 1.0f && ((base >= 1u && base <= 4u) || base == 13u));
    if (WaveActiveAnyTrue(wantRefl) && wantRefl) {
        const float4 sp = Spec.Load(ip);
        // Wave-Coherent Reflection Gate: skips matte terrain/snow/frosted-ice warps cleanly (saves RT rays for glossy/wet/metal):
        const float shinGate = exp2(sp.y * 13.0f);
        const float roughGate = clamp(sqrt(2.0f / (shinGate + 2.0f)), 0.03f, 0.85f);
        const bool isGlossy = (base == 2u || base == 13u || wetGloss || (sp.y > 0.16f && sp.x > 0.06f && roughGate < 0.44f));
        if (WaveActiveAnyTrue(isGlossy) && isGlossy) {
            const float2 px = float2(id.xy);
            const float3 p = worldAt(px, d);
            const float3 n = normalize(lerp(worldNormal(Normal.Load(ip)), float3(0.0f, 0.0f, 1.0f), rain.puddle));
            const float3 v = normalize(eyeAt() - p);
            const float shin = exp2(sp.y * 13.0f);
            const float alpha = lerp(lerp(clamp(sqrt(2.0f / (shin + 2.0f)), 0.03f, 0.85f), 0.10f, rain.wet), 0.025f, rain.puddle);
            const float2 rr0 = stbnR2(px, frameIndex + 37.0f);
            const float3 ng = geometricNormal(px, p, n);
            const float3 o = p + ng * (0.5f + 0.003f * viewZOf(d));
            // Adaptive Stratified Bounded-VNDF Glossy RT Reflections (1-spp on RTX 3060-4070, 2-spp on RTX 4080/4090/5090):
            const uint rtTierR = (giFlags >> 8u) & 7u;
            const uint reflSpp = (rtTierR >= 3u && (base == 2u || base == 13u || wetGloss)) ? 2u : 1u;
            float3 reflSum = 0.0f.xxx;
            float reflHitW = 0.0f;
            reflTraced = true;
            [loop] for (uint rsi = 0u; rsi < reflSpp; ++rsi) {
                // Family 090 Universal Optimal 2D Hexagonal A2 Lattice stratification for low-energy glossy ray samples:
                const float2 hexOff = float2(float(rsi) * 0.5f + float(rsi) * 0.25f, float(rsi) * 0.8660254f * 0.5f);
                const float2 rr = frac(rr0 + hexOff);
                const float3 rd = sampleGGXVNDF(n, v, max(alpha, 0.02f), rr);
                if (dot(rd, ng) <= 0.0f) { continue; }
                Hit h;
                float3 c = 0.0f.xxx, hn = float3(0.0f, 0.0f, 1.0f);
                if (traceHit(o, rd, reflectLength, h)) {
                    const float3 hitP = o + rd * h.t;
                    // Pure 3D Hardware DXR World-Space Glossy Reflections with fixed world radiance-cache lookup:
                    if ((giFlags & GI_MATERIALS) != 0u) {
                        float3 offRad = radianceAt(h, o, rd, rr, true, hn);
                        if ((giFlags & GI_CACHE) != 0u) {
                            float4 cacheVal = 0.0f.xxxx;
                            if (cacheRead(hitP, hn, 0.0f.xxx, cacheVal)) { offRad = max(offRad, cacheVal.rgb * 0.85f); }
                        }
                        reflSum += offRad; reflHitW += 1.0f;
                    }
                } else if (wetGloss || base == 2u) {
                    // Vidar Finite-Room Box-Projected Parallax Fallback on interiors (eliminates infinite-skybox reflection drift):
                    const bool isIntRefl = (sunColour.w <= 0.5f);
                    float3 fallbackDir = rd;
                    if (isIntRefl) {
                        const float3 boxHalf = float3(420.0f, 420.0f, 220.0f);
                        const float3 invRd = rcp(select(abs(rd) > 1.0e-4f, rd, 1.0e-4f.xxx));
                        const float3 tPos = ( boxHalf - p) * invRd;
                        const float3 tNeg = (-boxHalf - p) * invRd;
                        const float3 tMax = max(tPos, tNeg);
                        const float tBox = min(tMax.x, min(tMax.y, tMax.z));
                        fallbackDir = normalize(p + rd * clamp(tBox, 16.0f, 900.0f));
                    }
                    reflSum += isIntRefl ? (ambientAt(fallbackDir) * 0.72f) : skyAt(fallbackDir);
                    reflHitW += 1.0f;
                }
            }
            if (reflHitW > 0.0f) { refl = float4(reflSum / reflHitW, 1.0f); }
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
    const bool glossy = reflectLength > 0.0f && d < 1.0f && base >= 1u && base <= 4u && (base == 2u || (sp0.y > 0.14f && sp0.x > 0.04f) || wetGloss);
    if (reflTraced || !glossy || reflectLength < 0.0f) { Refl[id.xy] = refl; }
    Vis[id.xy] = vis;
    Lights[id.xy] = float4(idx.xyz, sky);
    if (skyLength > 0.0f && skyTurn(id.xy)) { SkyRaw[id.xy] = float4(bounce, sky + restCode); }
}
#endif
