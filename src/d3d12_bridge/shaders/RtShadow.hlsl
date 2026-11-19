// RtShadow.hlsl: ray-traced shadows on the game's own geometry (RtScene.cpp, docs/RAYTRACING.md).
// One thread a pixel. From the surface the G-buffer gives, one inline ray query toward the sun and
// one toward each of the three point lights of the pixel's cluster that would light it most.
// Each ray aims at a jittered point of its source (the sun's disc, a sphere of SourceRadius around
// the light), so the game's temporal anti-aliasing turns the frame-to-frame noise into penumbrae.
// Out: Vis = (sun, light 0, light 1, light 2) visibility; Lights = their indices + 1, over 255.
// Original work, MIT.
#define ATLAS_SLOT t9
#include "LightingCommon.hlsli"

Texture2D<float>  Depth  : register(t0);
Texture2D<float4> Normal : register(t1);
Texture2D<float4> Spec   : register(t2);
RaytracingAccelerationStructure Scene : register(t3);
RWTexture2D<float4> Vis    : register(u0);
RWTexture2D<float4> Lights : register(u1);
cbuffer R : register(b1) { float sunLength, sourceRadius, sunCone, frameIndex; };

uint clusterOf(float2 px, float z) {
    const uint tx = min(uint(px.x * dims.z * TILES_X), TILES_X - 1u);
    const uint ty = min(uint(px.y * dims.w * TILES_Y), TILES_Y - 1u);
    const float sl = log(max(z, cluster.x) / cluster.x) * cluster.z;
    return (min(uint(max(sl, 0.0f)), SLICES - 1u) * TILES_Y + ty) * TILES_X + tx;
}

float hash(float2 p, float k) { return frac(sin(dot(p + k, float2(12.9898f, 78.233f))) * 43758.5453f); }

// A unit vector spread around d by up to `spread` radians (a disc of directions).
float3 jitterDir(float3 d, float spread, float2 r) {
    const float3 t = normalize(abs(d.y) < 0.99f ? cross(d, float3(0, 1, 0)) : cross(d, float3(1, 0, 0)));
    const float3 b = cross(d, t);
    const float a = 6.2831853f * r.x, s = spread * sqrt(r.y);
    return normalize(d + (t * cos(a) + b * sin(a)) * s);
}

float visible(float3 o, float3 d, float tMax) {
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    RayDesc r;
    r.Origin = o; r.Direction = d; r.TMin = 0.0f; r.TMax = tMax;
    q.TraceRayInline(Scene, 0, 0xFF, r);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0f : 1.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int3 ip = int3(id.xy, 0);
    float4 vis = 1.0f, idx = 0.0f;
    const float d = Depth.Load(ip);
    const uint base = uint(Spec.Load(ip).z * 255.0f + 0.5f) & 127u;
    if (d < 1.0f && base != 0u && base < 7u) {
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, d);
        const float3 n = worldNormal(Normal.Load(ip));
        const float z = viewZOf(d);
        const float3 o = p + n * (0.5f + 0.003f * z);
        const float2 r = float2(hash(px, frameIndex * 0.618f), hash(px.yx, frameIndex * 0.381f + 7.0f));
        if (sunDir.w > 0.5f && dot(n, sunDir.xyz) > 0.0f) vis.x = visible(o, jitterDir(sunDir.xyz, sunCone, r), sunLength);
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
            vis[k + 1u] = visible(o, toC / max(len, 1.0e-4f), max(len - sourceRadius - 1.0f, 0.0f));
            idx[k] = float(best[k] + 1u) / 255.0f;
        }
    }
    Vis[id.xy] = vis;
    Lights[id.xy] = idx;
}
