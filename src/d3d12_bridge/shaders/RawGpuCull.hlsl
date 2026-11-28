// RawGpuCull.hlsl: GPU-Driven Conservative Hi-Z Occlusion Pyramid & 65,536-Instance Frustum/Occlusion/Distance Culler.
// Compiles cleanly with DXC 1.10 (-HV 2021 -Gis -O3 -enable-16bit-types -WX) for BOTH cs_6_6 and cs_6_10.
//
// Standard-Z depth convention (matching RAW / Skyrim D3D12Bridge):
//   0.0 = near plane, 1.0 = far plane.
//   Conservative Hi-Z pyramid reduces with max() so each mip texel holds the FARTHEST depth in its footprint;
//   an object is occluded iff its nearest projected corner depth minObjZ > max(HiZ 2x2 taps) + depthBias.
//
// Entry points:
//   1. cs_hiz_reduce     ([numthreads(8, 8, 1)])  - Builds Mip 0 copy or conservative max() downsample with odd-edge coverage.
//   2. cs_cull_instances ([numthreads(64, 1, 1)]) - 6-plane AABB frustum + distance LOD + conservative Hi-Z occlusion + wave-compacted indirect output.
//
// Original clean-room implementation, MIT License.

// ============================================================================
// Entry 1: Conservative Hi-Z Pyramid Reduction (Standard-Z max())
// ============================================================================
cbuffer HiZReduceCB : register(b0)
{
    uint gSrcWidth;
    uint gSrcHeight;
    uint gDstWidth;
    uint gDstHeight;
    uint gPassMode;   // 0 = copy/resample source depth -> Hi-Z mip 0, 1 = 2x2 (+odd edge) mip downsample
    uint gPad0;
    uint gPad1;
    uint gPad2;
};

Texture2D<float>   gHiZSrc : register(t0);
RWTexture2D<float> gHiZDst : register(u0);

[numthreads(8, 8, 1)]
void cs_hiz_reduce(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gDstWidth || dtid.y >= gDstHeight)
        return;

    if (gPassMode == 0u && gSrcWidth == gDstWidth && gSrcHeight == gDstHeight)
    {
        gHiZDst[dtid.xy] = gHiZSrc.Load(int3(int2(dtid.xy), 0));
        return;
    }

    // Standard 2x2 footprint with conservative odd-dimension extra row/column at the right/bottom edges
    // so thin background gaps at odd borders NEVER cause false occlusion.
    const int maxX = max(0, (int)gSrcWidth - 1);
    const int maxY = max(0, (int)gSrcHeight - 1);

    const int sx0 = min((int)(dtid.x * 2u), maxX);
    const int sy0 = min((int)(dtid.y * 2u), maxY);
    const int sx1 = min(sx0 + 1, maxX);
    const int sy1 = min(sy0 + 1, maxY);

    float d = max(
        max(gHiZSrc.Load(int3(sx0, sy0, 0)), gHiZSrc.Load(int3(sx1, sy0, 0))),
        max(gHiZSrc.Load(int3(sx0, sy1, 0)), gHiZSrc.Load(int3(sx1, sy1, 0))));

    const bool extraX = ((gSrcWidth & 1u) != 0u) && (gSrcWidth > 1u) && (dtid.x == gDstWidth - 1u);
    const bool extraY = ((gSrcHeight & 1u) != 0u) && (gSrcHeight > 1u) && (dtid.y == gDstHeight - 1u);

    if (extraX)
    {
        const int sx2 = min(sx0 + 2, maxX);
        d = max(d, max(gHiZSrc.Load(int3(sx2, sy0, 0)), gHiZSrc.Load(int3(sx2, sy1, 0))));
    }
    if (extraY)
    {
        const int sy2 = min(sy0 + 2, maxY);
        d = max(d, max(gHiZSrc.Load(int3(sx0, sy2, 0)), gHiZSrc.Load(int3(sx1, sy2, 0))));
    }
    if (extraX && extraY)
    {
        const int sx2 = min(sx0 + 2, maxX);
        const int sy2 = min(sy0 + 2, maxY);
        d = max(d, gHiZSrc.Load(int3(sx2, sy2, 0)));
    }

    gHiZDst[dtid.xy] = d;
}

// ============================================================================
// Entry 2: 65,536-Instance Frustum + Distance + Conservative Hi-Z Culler
// ============================================================================
struct GpuCullInstance
{
    float3 center;
    float  radius;
    float3 aabbMin;
    float  maxDrawDist;
    float3 aabbMax;
    uint   meshletOffset;
    uint   meshletCount;
    uint   instanceId;
    uint   lodBiasFlags; // bit 0: skipHiZ, bit 1: forceVisible
    uint   pad0;
};

cbuffer CullViewCB : register(b0)
{
    row_major float4x4 gViewProj;          // Row-vector clip = mul(float4(worldPos, 1), gViewProj)
    float4             gFrustumPlanes[6];  // Inward-pointing normalized planes: dot(n, p) + d >= 0 is inside
    float4             gCamPosAndFlags;    // xyz = cameraPosW, w = asfloat(cullFlags)
    float4             gHiZParams;         // x = hizW, y = hizH, z = hizMipCount, w = depthBias
    uint4              gCounts;            // x = instanceCount, y = defaultIndexCount, z = maxVisible, w = cullFlags
};

StructuredBuffer<GpuCullInstance> gInstances      : register(t0);
Texture2D<float>                  gHiZPyramid     : register(t1);

RWStructuredBuffer<uint>          gVisibleIndices : register(u0);
RWByteAddressBuffer               gVisibleCounts  : register(u1); // [0]=vis, [1]=meshlets, [2]=frustumCulled, [3]=distCulled, [4]=hizCulled
RWByteAddressBuffer               gIndirectArgs   : register(u2); // 0..19: DrawIndexedArgs, 32..43: DispatchMeshArgs

bool TestAabbFrustum(float3 bmin, float3 bmax)
{
    [unroll]
    for (uint i = 0u; i < 6u; ++i)
    {
        const float4 p = gFrustumPlanes[i];
        const float3 pv = float3(
            (p.x >= 0.0f) ? bmax.x : bmin.x,
            (p.y >= 0.0f) ? bmax.y : bmin.y,
            (p.z >= 0.0f) ? bmax.z : bmin.z);
        if (dot(p.xyz, pv) + p.w < 0.0f)
            return false;
    }
    return true;
}

bool TestAabbHiZVisible(float3 bmin, float3 bmax)
{
    const int hizW    = (int)gHiZParams.x;
    const int hizH    = (int)gHiZParams.y;
    const int maxMip  = max(0, (int)gHiZParams.z - 1);
    const float bias  = gHiZParams.w;

    if (hizW <= 0 || hizH <= 0)
        return true;

    float2 uvMin = float2(1.0e9f, 1.0e9f);
    float2 uvMax = float2(-1.0e9f, -1.0e9f);
    float  minObjZ = 1.0e9f;

    [unroll]
    for (uint c = 0u; c < 8u; ++c)
    {
        const float3 corner = float3(
            ((c & 1u) != 0u) ? bmax.x : bmin.x,
            ((c & 2u) != 0u) ? bmax.y : bmin.y,
            ((c & 4u) != 0u) ? bmax.z : bmin.z);
        const float4 clip = mul(float4(corner, 1.0f), gViewProj);

        // Strictly conservative near-plane guard: any corner behind or on the near plane
        // cannot be safely projected to a tight screen rect -> never Hi-Z cull.
        if (clip.w <= 1.0e-4f || clip.z <= 0.0f)
            return true;

        const float invW = 1.0f / clip.w;
        const float ndcX = clip.x * invW;
        const float ndcY = clip.y * invW;
        const float ndcZ = clip.z * invW;

        const float2 uv = float2(ndcX * 0.5f + 0.5f, 0.5f - ndcY * 0.5f);
        uvMin   = min(uvMin, uv);
        uvMax   = max(uvMax, uv);
        minObjZ = min(minObjZ, ndcZ);
    }

    // Clamp projected rect to screen viewport [0, 1]
    uvMin = saturate(uvMin);
    uvMax = saturate(uvMax);

    // If clamped minObjZ is at or behind near plane, conservatively keep visible.
    if (minObjZ <= 0.0f)
        return true;

    const float rectW  = (uvMax.x - uvMin.x) * (float)hizW;
    const float rectH  = (uvMax.y - uvMin.y) * (float)hizH;
    const float maxDim = max(rectW, rectH);

    int mip = (maxDim <= 1.0f) ? 0 : (int)ceil(log2(maxDim));
    mip = clamp(mip, 0, maxMip);

    int mipW = max(1, hizW >> mip);
    int mipH = max(1, hizH >> mip);

    int2 pMin = clamp((int2)floor(uvMin * float2((float)mipW, (float)mipH)), int2(0, 0), int2(mipW - 1, mipH - 1));
    int2 pMax = clamp((int2)floor(uvMax * float2((float)mipW, (float)mipH)), int2(0, 0), int2(mipW - 1, mipH - 1));

    // Guarantee projected footprint fits strictly within a 2x2 texel quad even at odd/NPOT mip boundaries.
    if ((pMax.x - pMin.x > 1 || pMax.y - pMin.y > 1) && mip < maxMip)
    {
        mip += 1;
        mipW = max(1, hizW >> mip);
        mipH = max(1, hizH >> mip);
        pMin = clamp((int2)floor(uvMin * float2((float)mipW, (float)mipH)), int2(0, 0), int2(mipW - 1, mipH - 1));
        pMax = clamp((int2)floor(uvMax * float2((float)mipW, (float)mipH)), int2(0, 0), int2(mipW - 1, mipH - 1));
    }
    else if (mip > 0)
    {
        // Optionally refine 1 mip finer if the footprint already fits within 2x2 texels (tighter silhouette, still 100% conservative).
        const int finerMip = mip - 1;
        const int fW = max(1, hizW >> finerMip);
        const int fH = max(1, hizH >> finerMip);
        const int2 fMin = clamp((int2)floor(uvMin * float2((float)fW, (float)fH)), int2(0, 0), int2(fW - 1, fH - 1));
        const int2 fMax = clamp((int2)floor(uvMax * float2((float)fW, (float)fH)), int2(0, 0), int2(fW - 1, fH - 1));
        if ((fMax.x - fMin.x) <= 1 && (fMax.y - fMin.y) <= 1)
        {
            mip  = finerMip;
            pMin = fMin;
            pMax = fMax;
        }
    }

    const float z00 = gHiZPyramid.Load(int3(pMin.x, pMin.y, mip));
    const float z10 = gHiZPyramid.Load(int3(pMax.x, pMin.y, mip));
    const float z01 = gHiZPyramid.Load(int3(pMin.x, pMax.y, mip));
    const float z11 = gHiZPyramid.Load(int3(pMax.x, pMax.y, mip));
    const float maxHiZ = max(max(z00, z10), max(z01, z11));

    // Standard-Z: occluded strictly iff nearest object corner is farther than the farthest depth in its screen footprint.
    return !(minObjZ > (maxHiZ + bias));
}

[numthreads(64, 1, 1)]
void cs_cull_instances(uint3 dtid : SV_DispatchThreadID)
{
    const uint idx          = dtid.x;
    const uint instCount    = gCounts.x;
    const uint maxVisible   = gCounts.z;
    const uint cullFlags    = gCounts.w;
    const bool enableFrustum = (cullFlags & 1u) != 0u;
    const bool enableDist    = (cullFlags & 2u) != 0u;
    const bool enableHiZ     = (cullFlags & 4u) != 0u;

    bool visible       = false;
    uint cullReason    = 0u; // 0 = alive/oob, 1 = frustum, 2 = distance, 3 = Hi-Z
    uint meshletsToAdd = 0u;

    if (idx < instCount)
    {
        const GpuCullInstance inst = gInstances[idx];
        const bool forceVis = (inst.lodBiasFlags & 2u) != 0u;
        const bool skipHiZ  = (inst.lodBiasFlags & 1u) != 0u;

        if (forceVis)
        {
            visible = true;
            meshletsToAdd = inst.meshletCount;
        }
        else
        {
            // 1. Exact 6-plane AABB frustum test
            bool alive = true;
            if (enableFrustum && !TestAabbFrustum(inst.aabbMin, inst.aabbMax))
            {
                alive = false;
                cullReason = 1u;
            }

            // 2. Distance LOD cutoff (closest point on AABB to camera)
            if (alive && enableDist && inst.maxDrawDist > 0.0f)
            {
                const float3 closest = clamp(gCamPosAndFlags.xyz, inst.aabbMin, inst.aabbMax);
                const float3 delta   = closest - gCamPosAndFlags.xyz;
                const float  distSq  = dot(delta, delta);
                if (distSq > inst.maxDrawDist * inst.maxDrawDist)
                {
                    alive = false;
                    cullReason = 2u;
                }
            }

            // 3. Conservative screen-space AABB vs Hi-Z pyramid occlusion test
            if (alive && enableHiZ && !skipHiZ)
            {
                if (!TestAabbHiZVisible(inst.aabbMin, inst.aabbMax))
                {
                    alive = false;
                    cullReason = 3u;
                }
            }

            visible = alive;
            meshletsToAdd = alive ? inst.meshletCount : 0u;
        }
    }

    // Wave-compacted atomic appends (uniform control flow for SM 6.6 / 6.10 & WARP safety)
    const uint waveVisCount     = WaveActiveCountBits(visible);
    const uint waveLaneOffset   = WavePrefixCountBits(visible);
    const uint waveMeshletSum   = WaveActiveSum(meshletsToAdd);
    const uint waveFrustumCull  = WaveActiveCountBits(cullReason == 1u);
    const uint waveDistCull     = WaveActiveCountBits(cullReason == 2u);
    const uint waveHiZCull      = WaveActiveCountBits(cullReason == 3u);

    uint waveBaseOffset = 0u;
    if (WaveIsFirstLane())
    {
        if (waveVisCount > 0u)
        {
            gVisibleCounts.InterlockedAdd(0u, waveVisCount, waveBaseOffset);
            // Update D3D12_DRAW_INDEXED_ARGUMENTS.InstanceCount (byte offset 4)
            gIndirectArgs.InterlockedAdd(4u, waveVisCount);
        }
        if (waveMeshletSum > 0u)
        {
            gVisibleCounts.InterlockedAdd(4u, waveMeshletSum);
            // Update D3D12_DISPATCH_MESH_ARGUMENTS.ThreadGroupCountX (byte offset 32)
            gIndirectArgs.InterlockedAdd(32u, waveMeshletSum);
        }
        if (waveFrustumCull > 0u)
            gVisibleCounts.InterlockedAdd(8u, waveFrustumCull);
        if (waveDistCull > 0u)
            gVisibleCounts.InterlockedAdd(12u, waveDistCull);
        if (waveHiZCull > 0u)
            gVisibleCounts.InterlockedAdd(16u, waveHiZCull);
    }

    waveBaseOffset = WaveReadLaneFirst(waveBaseOffset);
    if (visible)
    {
        const uint dstSlot = waveBaseOffset + waveLaneOffset;
        if (dstSlot < maxVisible)
            gVisibleIndices[dstSlot] = idx;
    }
}

// ============================================================================
// Wave 83: Two-Pass Hi-Z Disocclusion Retest + Meshlet Backface Cone + Continuous LOD Cut
// ============================================================================
struct GpuMeshletDesc
{
    float4 boundSphere;          // xyz center, w radius
    uint   packedConeAxisCutoff; // snorm8 xyz + cutoff
    float  lodErrorWorld;        // child world-space geometric error
    float  parentErrorWorld;     // parent DAG error (>= lodErrorWorld for watertight monotonic cut)
    uint   meshletOffset;
    uint   triVertCounts;        // low16=verts, high16=tris
    uint   psoBucketId;
    uint   pad0;
    uint   pad1;
};

RWStructuredBuffer<uint> gDisoccCandidates : register(u3); // Pass 1 -> Pass 2 Hi-Z-culled candidate IDs
RWByteAddressBuffer      gDisoccCount      : register(u4); // [0]=Pass 1 candidate count, [1]=Pass 2 recovered count

bool TestMeshletBackfaceCone(float4 boundSphere, uint packedCone, float3 camPosW)
{
    const float ax = float(int(packedCone << 24) >> 24) / 127.0f;
    const float ay = float(int(packedCone << 16) >> 24) / 127.0f;
    const float az = float(int(packedCone << 8)  >> 24) / 127.0f;
    const float cut = float(int(packedCone)      >> 24) / 127.0f;
    const float3 toCam = boundSphere.xyz - camPosW;
    const float  dist  = max(length(toCam), 1.0e-4f);
    // Conservative sphere-cone backface test (visible if dot < cutoff * dist + radius):
    return dot(toCam, float3(ax, ay, az)) < (cut * dist + boundSphere.w);
}

bool EvalMeshletLodCut(float lodErrorWorld, float parentErrorWorld, float viewZ, float projScalePx, float targetPxErr)
{
    const float zSafe = max(viewZ, 1.0f);
    const float childPx  = (lodErrorWorld * projScalePx) / zSafe;
    const float parentPx = (parentErrorWorld * projScalePx) / zSafe;
    return (childPx <= targetPxErr) && (parentPx > targetPxErr);
}

// Pass 1 (previous-frame Hi-Z): draws previously visible occluders and queues Hi-Z-culled candidates for Pass 2 retest.
[numthreads(64, 1, 1)]
void cs_cull_pass1_prev_hiz(uint3 dtid : SV_DispatchThreadID)
{
    const uint idx        = dtid.x;
    const uint instCount  = gCounts.x;
    const uint maxVisible = gCounts.z;
    const uint cullFlags  = gCounts.w;
    bool visible          = false;
    bool queueForPass2    = false;
    uint meshletsToAdd    = 0u;

    if (idx < instCount)
    {
        const GpuCullInstance inst = gInstances[idx];
        bool alive = true;
        if ((cullFlags & 1u) != 0u && !TestAabbFrustum(inst.aabbMin, inst.aabbMax)) { alive = false; }
        if (alive && (cullFlags & 2u) != 0u && inst.maxDrawDist > 0.0f)
        {
            const float3 closest = clamp(gCamPosAndFlags.xyz, inst.aabbMin, inst.aabbMax);
            const float3 delta   = closest - gCamPosAndFlags.xyz;
            if (dot(delta, delta) > inst.maxDrawDist * inst.maxDrawDist) { alive = false; }
        }
        if (alive)
        {
            if ((cullFlags & 4u) != 0u && (inst.lodBiasFlags & 3u) == 0u && !TestAabbHiZVisible(inst.aabbMin, inst.aabbMax))
            {
                queueForPass2 = true;
            }
            else
            {
                visible = true;
                meshletsToAdd = inst.meshletCount;
            }
        }
    }

    const uint wVisCount = WaveActiveCountBits(visible);
    const uint wVisLane  = WavePrefixCountBits(visible);
    const uint wDisCount = WaveActiveCountBits(queueForPass2);
    const uint wDisLane  = WavePrefixCountBits(queueForPass2);
    const uint wMeshSum  = WaveActiveSum(meshletsToAdd);

    uint visBase = 0u, disBase = 0u;
    if (WaveIsFirstLane())
    {
        if (wVisCount > 0u)
        {
            gVisibleCounts.InterlockedAdd(0u, wVisCount, visBase);
            gIndirectArgs.InterlockedAdd(4u, wVisCount);
        }
        if (wMeshSum > 0u)
        {
            gVisibleCounts.InterlockedAdd(4u, wMeshSum);
            gIndirectArgs.InterlockedAdd(32u, wMeshSum);
        }
        if (wDisCount > 0u)
        {
            gDisoccCount.InterlockedAdd(0u, wDisCount, disBase);
        }
    }
    visBase = WaveReadLaneFirst(visBase);
    disBase = WaveReadLaneFirst(disBase);
    if (visible && (visBase + wVisLane) < maxVisible) { gVisibleIndices[visBase + wVisLane] = idx; }
    if (queueForPass2 && (disBase + wDisLane) < maxVisible) { gDisoccCandidates[disBase + wDisLane] = idx; }
}

// Pass 2 (current-frame rebuilt Hi-Z): retests ONLY Pass-1 Hi-Z-culled candidates so newly disoccluded
// geometry renders on the EXACT same frame with zero 1-frame pop-in!
[numthreads(64, 1, 1)]
void cs_cull_pass2_disoccluded(uint3 dtid : SV_DispatchThreadID)
{
    const uint candSlot   = dtid.x;
    const uint candCount  = gDisoccCount.Load(0u);
    const uint maxVisible = gCounts.z;
    bool recovered        = false;
    uint instIdx          = 0u;
    uint meshletsToAdd    = 0u;

    if (candSlot < candCount)
    {
        instIdx = gDisoccCandidates[candSlot];
        if (instIdx < gCounts.x)
        {
            const GpuCullInstance inst = gInstances[instIdx];
            if (TestAabbHiZVisible(inst.aabbMin, inst.aabbMax))
            {
                recovered = true;
                meshletsToAdd = inst.meshletCount;
            }
        }
    }

    const uint wRecCount = WaveActiveCountBits(recovered);
    const uint wRecLane  = WavePrefixCountBits(recovered);
    const uint wMeshSum  = WaveActiveSum(meshletsToAdd);

    uint visBase = 0u;
    if (WaveIsFirstLane() && wRecCount > 0u)
    {
        gVisibleCounts.InterlockedAdd(0u, wRecCount, visBase);
        gIndirectArgs.InterlockedAdd(4u, wRecCount);
        gDisoccCount.InterlockedAdd(4u, wRecCount);
        if (wMeshSum > 0u)
        {
            gVisibleCounts.InterlockedAdd(4u, wMeshSum);
            gIndirectArgs.InterlockedAdd(32u, wMeshSum);
        }
    }
    visBase = WaveReadLaneFirst(visBase);
    if (recovered && (visBase + wRecLane) < maxVisible) { gVisibleIndices[visBase + wRecLane] = instIdx; }
}
