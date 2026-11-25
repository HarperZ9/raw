// RawRadianceCascades.hlsl: Real-Time GPU 4-Cascade Penumbra-Exact Radiance Cascades Probe GI Solver (SM 6.6 / 6.10).
// Operates on a 256 x 256 packed probe/angle atlas (R16G16B16A16_FLOAT: rgb = linear HDR radiance, a = transmittance):
//   - Cascade 0: 128 x 128 spatial probes x   4 angular intervals (2x2 block/probe, spacing  2 texels)
//   - Cascade 1:  64 x  64 spatial probes x  16 angular intervals (4x4 block/probe, spacing  4 texels)
//   - Cascade 2:  32 x  32 spatial probes x  64 angular intervals (8x8 block/probe, spacing  8 texels)
//   - Cascade 3:  16 x  16 spatial probes x 256 angular intervals (16x16 block/probe, spacing 16 texels)
//
// 2 Compute Entry Points ([numthreads(8, 8, 1)] -> 32 x 32 x 1 thread groups):
//   1. cs_rc_trace_merge : Traces exact non-overlapping radial intervals [tStart(c)..tEnd(c)] along collinear
//                          child rays against SceneEmissiveAlpha and bilinearly merges upper cascade (c+1)
//                          weighted by local transmittance T (strictly linear in emitter RGB).
//   2. cs_rc_resolve     : Integrates Cascade 0's 4 angular intervals and bilinearly upsamples to a
//                          noise-free, zero-lag 256 x 256 RGBA16F diffuse GI irradiance map.
//
// Original clean-room implementation, MIT License.

static const uint  kAtlasRes    = 256u;
static const float kAtlasResF   = 256.0f;
static const float kInvAtlasRes = 1.0f / 256.0f;
static const float kTwoPi       = 6.28318530718f;

cbuffer RadianceCascadeCB : register(b0)
{
    uint4  cascadeInfo;    // x = cascadeIndex (0..3), y = cascadeCount (4), z = stepCount, w = atlasRes (256)
    float4 intervalInfo;   // x = tStart (texels), y = tEnd (texels), z = opacityScale, w = emissiveScale
    float4 resolveInfo;    // xyz = skyRadianceRGB, w = irradianceScale
    float4 sceneDims;      // xy = sceneTexDims, zw = 1.0 / sceneTexDims
};

Texture2D<float4>   SceneEmissiveAlpha : register(t0); // rgb = linear HDR emission, a = solid occluder opacity [0..1]
Texture2D<float4>   UpperCascade       : register(t1); // cascade c+1 atlas (null on c == 3; Cascade 0 on resolve)
RWTexture2D<float4> CascadeOut         : register(u0); // current cascade atlas output OR resolved 256x256 irradiance

SamplerState LinClamp : register(s0);

// Marches a single ray from originPx (in [0..256] atlas texel space) along unit direction rayDir
// across the exact radial interval [tStart, tEnd], returning float4(accumulatedRadRGB, transmittance).
// Strictly linear in SceneEmissiveAlpha.rgb (transmittance depends solely on alpha).
float4 traceRayInterval(float2 originPx, float2 rayDir, float tStart, float tEnd, uint stepCount)
{
    const float span = max(tEnd - tStart, 0.0f);
    const uint steps = max(stepCount, 1u);
    const float ds = span / float(steps);
    const float opacityScale = max(intervalInfo.z, 0.01f);
    const float emissiveScale = max(intervalInfo.w, 0.0f);

    float3 rad = float3(0.0f, 0.0f, 0.0f);
    float trans = 1.0f;

    [loop] for (uint s = 0u; s < steps; ++s) {
        const float t = tStart + (float(s) + 0.5f) * ds;
        const float2 p = originPx + rayDir * t;
        const float2 uv = p * kInvAtlasRes;
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) {
            break;
        }
        const float4 sampleVal = SceneEmissiveAlpha.SampleLevel(LinClamp, uv, 0.0f);
        const float alpha = saturate(sampleVal.a);
        const float sigma = alpha * opacityScale;
        const float stepTrans = exp(-sigma * ds);
        const float stepOcc = 1.0f - stepTrans;

        // Solid/semi-opaque emitters contribute energy-conserving stepOcc * rgb (independent of ds);
        // purely transparent volumetric plasma (alpha ~ 0) contributes linearly via ds * emissiveScale.
        const float3 emit = (alpha > 1.0e-3f) ? (sampleVal.rgb * stepOcc) : (sampleVal.rgb * (ds * emissiveScale));
        rad += trans * emit;
        trans *= stepTrans;
        if (trans < 0.002f) {
            trans = 0.0f;
            break;
        }
    }
    return float4(rad, trans);
}

// Bilinearly samples upper cascade (c+1) for a specific upper angular index upperDirIdx
// across the 4 enclosing upper-cascade probes around upperCoord (zero cross-angle bleeding).
float4 sampleUpperCascadeBilinear(float2 upperCoord, uint upperProbeGrid, uint upperDirGrid, uint upperDirIdx)
{
    const int2 pBase = int2(floor(upperCoord));
    const float2 w = frac(upperCoord);
    const int maxP = int(upperProbeGrid) - 1;
    const int2 p0 = clamp(pBase, int2(0, 0), int2(maxP, maxP));
    const int2 p1 = clamp(pBase + int2(1, 1), int2(0, 0), int2(maxP, maxP));

    const int2 dirOff = int2(int(upperDirIdx & (upperDirGrid - 1u)), int(upperDirIdx / upperDirGrid));
    const int dg = int(upperDirGrid);

    const float4 v00 = UpperCascade.Load(int3(p0.x * dg + dirOff.x, p0.y * dg + dirOff.y, 0));
    const float4 v10 = UpperCascade.Load(int3(p1.x * dg + dirOff.x, p0.y * dg + dirOff.y, 0));
    const float4 v01 = UpperCascade.Load(int3(p0.x * dg + dirOff.x, p1.y * dg + dirOff.y, 0));
    const float4 v11 = UpperCascade.Load(int3(p1.x * dg + dirOff.x, p1.y * dg + dirOff.y, 0));

    const float4 r0 = lerp(v00, v10, w.x);
    const float4 r1 = lerp(v01, v11, w.x);
    return lerp(r0, r1, w.y);
}

// ----------------------------------------------------------------------------
// Pass 1..4: cs_rc_trace_merge (executed top-down c = 3 -> 2 -> 1 -> 0)
// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void cs_rc_trace_merge(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= kAtlasRes)) {
        return;
    }

    const uint c = min(cascadeInfo.x, 3u);
    const uint cCount = max(cascadeInfo.y, 1u);
    const uint dirGrid = 2u << c;         // 2, 4, 8, 16
    const uint probeGrid = 128u >> c;     // 128, 64, 32, 16
    const uint dirCount = dirGrid * dirGrid; // 4, 16, 64, 256
    const float probeSpacing = float(2u << c); // 2, 4, 8, 16 texels

    const uint2 probeXY = tid.xy / dirGrid;
    const uint2 dirXY   = tid.xy & (dirGrid - 1u);
    const uint  dirIdx  = dirXY.y * dirGrid + dirXY.x;

    const float2 probePos = (float2(probeXY) + 0.5f) * probeSpacing;
    const float tStart = intervalInfo.x;
    const float tEnd   = intervalInfo.y;
    const uint steps   = cascadeInfo.z;

    if (c + 1u >= cCount) {
        // Top cascade (c == 3): 256 angular intervals covering [tStart..tMax] + far-field sky radiance
        const float angle = kTwoPi * (float(dirIdx) + 0.5f) / float(dirCount);
        const float2 rayDir = float2(cos(angle), sin(angle));
        float4 res = traceRayInterval(probePos, rayDir, tStart, tEnd, steps);
        res.rgb += res.a * resolveInfo.xyz;
        CascadeOut[tid.xy] = res;
        return;
    }

    // Lower cascades (c = 2, 1, 0): penumbra-exact collinear merge with the 4 child directions in cascade c+1
    const uint upperDirGrid   = dirGrid * 2u;
    const uint upperProbeGrid = probeGrid >> 1u;
    const uint upperDirCount  = upperDirGrid * upperDirGrid;
    const float2 upperCoord   = (float2(probeXY) + 0.5f) * 0.5f - 0.5f;

    float3 mergedRad = float3(0.0f, 0.0f, 0.0f);
    float  mergedTrans = 0.0f;

    [unroll] for (uint m = 0u; m < 4u; ++m) {
        const uint upperDirIdx = dirIdx * 4u + m;
        const float angle = kTwoPi * (float(upperDirIdx) + 0.5f) / float(upperDirCount);
        const float2 rayDir = float2(cos(angle), sin(angle));

        const float4 localSeg = traceRayInterval(probePos, rayDir, tStart, tEnd, steps);
        const float4 upperSeg = sampleUpperCascadeBilinear(upperCoord, upperProbeGrid, upperDirGrid, upperDirIdx);

        mergedRad   += localSeg.rgb + localSeg.a * upperSeg.rgb;
        mergedTrans += localSeg.a * upperSeg.a;
    }

    CascadeOut[tid.xy] = float4(mergedRad * 0.25f, mergedTrans * 0.25f);
}

// ----------------------------------------------------------------------------
// Pass 5: cs_rc_resolve
// Integrates Cascade 0's 4 merged angular intervals per probe and bilinearly
// upsamples the 128 x 128 probe field to a smooth 256 x 256 RGBA16F GI map.
// ----------------------------------------------------------------------------
float4 loadCascade0ProbeIrradiance(int2 p)
{
    const int2 base = p * 2;
    const float4 q0 = UpperCascade.Load(int3(base + int2(0, 0), 0));
    const float4 q1 = UpperCascade.Load(int3(base + int2(1, 0), 0));
    const float4 q2 = UpperCascade.Load(int3(base + int2(0, 1), 0));
    const float4 q3 = UpperCascade.Load(int3(base + int2(1, 1), 0));
    return 0.25f * (q0 + q1 + q2 + q3);
}

[numthreads(8, 8, 1)]
void cs_rc_resolve(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= kAtlasRes)) {
        return;
    }

    // Map pixel center (tid + 0.5) into Cascade 0 probe-index space (128x128 probes at 1,3,5..255)
    const float2 pCoord = (float2(tid.xy) + 0.5f) * 0.5f - 0.5f;
    const int2 pBase = int2(floor(pCoord));
    const float2 w = frac(pCoord);
    const int2 p0 = clamp(pBase, int2(0, 0), int2(127, 127));
    const int2 p1 = clamp(pBase + int2(1, 1), int2(0, 0), int2(127, 127));

    const float4 i00 = loadCascade0ProbeIrradiance(int2(p0.x, p0.y));
    const float4 i10 = loadCascade0ProbeIrradiance(int2(p1.x, p0.y));
    const float4 i01 = loadCascade0ProbeIrradiance(int2(p0.x, p1.y));
    const float4 i11 = loadCascade0ProbeIrradiance(int2(p1.x, p1.y));

    const float4 irrad = lerp(lerp(i00, i10, w.x), lerp(i01, i11, w.x), w.y);
    const float scale = max(resolveInfo.w, 0.0f);
    CascadeOut[tid.xy] = float4(irrad.rgb * scale, saturate(irrad.a));
}
