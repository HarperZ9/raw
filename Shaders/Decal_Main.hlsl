// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Screen-space deferred decal projection.
// For each pixel, reconstructs world position from depth, then tests
// against every active decal's oriented bounding box. If the pixel
// lies inside a decal volume and the surface normal is compatible
// with the decal's projection axis, a procedural decal pattern is
// sampled and alpha-blended onto the backbuffer.
//
// Reference: Persson, "Volume Decals", GPU Pro 5, 2014.

struct GPUDecal
{
    float3 position;    float normalFade;
    float4 rotation;    // quaternion (x, y, z, w)
    float3 size;        float opacity;
    float4 color;
    uint   pattern;     uint3 pad;
};

cbuffer DecalCB : register(b0)
{
    float4x4 invViewProj;
    float3   cameraPos;     float nearZ;
    float    farZ;
    uint     screenWidth;
    uint     screenHeight;
    uint     decalCount;
    float    globalOpacity;
    float    normalThreshold;
    float2   pad;
}

Texture2D<float4>          tBackbuffer : register(t0);
Texture2D<float>           tDepth      : register(t1);
Texture2D<float4>          tNormals    : register(t2);
StructuredBuffer<GPUDecal> tDecals     : register(t3);
RWTexture2D<float4>        uBackbuffer : register(u0);

// ---------------------------------------------------------------------------
// Quaternion helpers
// ---------------------------------------------------------------------------

// Rotate a vector by a unit quaternion.
float3 QuatRotate(float4 q, float3 v)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Inverse rotate (conjugate of unit quaternion).
float3 QuatRotateInverse(float4 q, float3 v)
{
    float4 qConj = float4(-q.xyz, q.w);
    return QuatRotate(qConj, v);
}

// ---------------------------------------------------------------------------
// Procedural decal patterns
// ---------------------------------------------------------------------------

// Generate a simple decal pattern based on local UV and pattern ID.
// Returns float4(rgb, alpha).
float4 SampleDecalPattern(float2 uv, uint pattern)
{
    float alpha = 1.0;

    // Pattern 0: solid rectangle with soft edges.
    if (pattern == 0)
    {
        float2 edgeDist = 1.0 - abs(uv * 2.0 - 1.0);
        float edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 8.0);
        alpha = edgeFade;
    }
    // Pattern 1: circle.
    else if (pattern == 1)
    {
        float2 centered = uv * 2.0 - 1.0;
        float dist = length(centered);
        alpha = saturate((1.0 - dist) * 4.0);
    }
    // Pattern 2: cross/splat.
    else if (pattern == 2)
    {
        float2 centered = abs(uv * 2.0 - 1.0);
        float crossDist = min(centered.x, centered.y);
        alpha = saturate((0.3 - crossDist) * 8.0);
    }
    // Pattern 3: ring.
    else if (pattern == 3)
    {
        float2 centered = uv * 2.0 - 1.0;
        float dist = length(centered);
        alpha = saturate((1.0 - abs(dist - 0.6)) * 8.0 - 3.0);
    }
    // Default: solid fill.
    else
    {
        alpha = 1.0;
    }

    return float4(1.0, 1.0, 1.0, alpha);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    if (decalCount == 0)
        return;

    // Depth — HiZ reversed-Z (near=1, far=0).
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.0001)
        return;

    // Reconstruct world position.
    float2 uv = (float2(DTid.xy) + 0.5) / float2(screenWidth, screenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, rawDepth, 1.0);
    float4 worldPos4 = mul(clipPos, invViewProj);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    // Decode surface normal.
    float4 rawNormal = tNormals.Load(int3(DTid.xy, 0));
    float3 normal = normalize(rawNormal.xyz * 2.0 - 1.0);

    // Read original scene color (from backbuffer copy at t0).
    float4 sceneColor = tBackbuffer.Load(int3(DTid.xy, 0));

    // Accumulate decal contributions.
    float3 accumColor = sceneColor.rgb;

    // Iterate over all active decals.  Cap iterations to prevent
    // excessive per-pixel cost; in practice decalCount is small.
    uint maxDecals = min(decalCount, 64u);

    for (uint d = 0; d < maxDecals; ++d)
    {
        GPUDecal decal = tDecals[d];

        // Transform world position into decal's local space.
        float3 localPos = QuatRotateInverse(decal.rotation, worldPos - decal.position);

        // Half-extents of the decal box.
        float3 halfSize = decal.size * 0.5;

        // Inside-box test: all local coords must be within [-halfSize, halfSize].
        float3 absLocal = abs(localPos);
        if (absLocal.x > halfSize.x || absLocal.y > halfSize.y || absLocal.z > halfSize.z)
            continue;

        // ── Normal attenuation ──────────────────────────────────────────
        // Decal projects along local Z axis.  Reject surfaces facing away.
        float3 decalForward = QuatRotate(decal.rotation, float3(0, 0, 1));
        float normalDot = abs(dot(normal, decalForward));
        if (normalDot < normalThreshold)
            continue;

        float normalFade = saturate((normalDot - normalThreshold) /
                                     max(1.0 - normalThreshold, 0.001));
        normalFade = lerp(1.0, normalFade, decal.normalFade);

        // ── Decal UV ────────────────────────────────────────────────────
        // Map local XY [-halfSize, halfSize] to UV [0, 1].
        float2 decalUV = (localPos.xy / halfSize.xy) * 0.5 + 0.5;

        // ── Sample pattern ──────────────────────────────────────────────
        float4 patternSample = SampleDecalPattern(decalUV, decal.pattern);

        // ── Depth fade at decal box boundaries ──────────────────────────
        float depthFade = saturate((halfSize.z - absLocal.z) / (halfSize.z * 0.2));

        // ── Alpha blend ─────────────────────────────────────────────────
        float finalAlpha = patternSample.a * decal.opacity * globalOpacity *
                           normalFade * depthFade;

        float3 decalColor = decal.color.rgb * patternSample.rgb;
        accumColor = lerp(accumColor, decalColor, saturate(finalAlpha));
    }

    uBackbuffer[DTid.xy] = float4(accumColor, sceneColor.a);
}
