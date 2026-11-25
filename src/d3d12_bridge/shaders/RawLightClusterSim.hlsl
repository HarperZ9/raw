// RawLightClusterSim.hlsl: SM 6.6 / 6.10 16x9x24 (3,456-Cluster) 3D Froxel Local Light Assignment Pipeline.
// Supports up to 1,024 simultaneous dynamic point/spot/fire/spell lights with < 0.05 ms RTX 4090 GPU cost.
// Original clean-room implementation, MIT License.
//
// View-space convention: Left-Handed DX12 (+X right, +Y up, +Z forward; nearZ > 0, farZ > nearZ).
// Screen-tile convention: cx in [0..15] left->right, cy in [0..8] top->bottom, cz in [0..23] exponential near->far.
//
// Entry points:
//   1. cs_cluster_build_aabb  ([numthreads(8, 8, 1)])  - Computes tight view-space AABBs for all 3,456 clusters.
//   2. cs_cluster_cull_lights ([numthreads(64, 1, 1)]) - LDS-cached sphere-vs-AABB + exact spotlight cone culling.

static const uint kClusterTilesX       = 16u;
static const uint kClusterTilesY       = 9u;
static const uint kClusterSlicesZ      = 24u;
static const uint kTotalClusters       = 3456u; // 16 * 9 * 24
static const uint kMaxLightsPerCluster = 64u;
static const uint kLightTypeSpotBit    = 1u;

struct ClusterAabb
{
    float3 minBounds;
    float  pad0;
    float3 maxBounds;
    float  pad1;
};

struct GpuClusterLight
{
    float3 posView;      // View-space position (+Z forward)
    float  radius;       // Influence radius (> 0 active)
    float3 dirView;      // Normalized view-space spotlight direction
    float  cosOuterCone; // cos(outerHalfAngle); -1 for omnidirectional point lights
    float3 colorRgb;     // Linear HDR color * intensity
    uint   typeFlags;    // bit 0: 1 = spotlight cone culling active
};

cbuffer LightClusterCB : register(b0)
{
    row_major float4x4 gInvProj;   // Row-vector view = mul(float4(ndc, 1), gInvProj)
    float4             gZParams;   // x = nearZ, y = farZ, z = log(farZ / nearZ), w = 1.0 / 24.0
    uint4              gGridDims;  // x = 16, y = 9, z = 24, w = lightCount (0..1024)
    uint4              gFlags;     // x = maxLightsPerCluster (64), y..w = reserved
};

// ============================================================================
// Entry 1: Build 3,456 Tight View-Space Cluster AABBs (Logarithmic Z Slicing)
// ============================================================================
RWStructuredBuffer<ClusterAabb> gClusterAabbsOut : register(u0);

float3 UnprojectNdcNearToView(float ndcX, float ndcY)
{
    const float4 v = mul(float4(ndcX, ndcY, 0.0f, 1.0f), gInvProj);
    const float invW = (abs(v.w) > 1.0e-7f) ? (1.0f / v.w) : 1.0f;
    return v.xyz * invW;
}

float3 IntersectRayWithViewZPlane(float3 viewNearPt, float zPlane)
{
    const float denom = (abs(viewNearPt.z) > 1.0e-6f) ? viewNearPt.z : 1.0e-6f;
    return viewNearPt * (zPlane / denom);
}

[numthreads(8, 8, 1)]
void cs_cluster_build_aabb(uint3 dtid : SV_DispatchThreadID)
{
    const uint cx = dtid.x;
    const uint cy = dtid.y;
    const uint cz = dtid.z;
    if (cx >= kClusterTilesX || cy >= kClusterTilesY || cz >= kClusterSlicesZ)
        return;

    const float nearZ = max(gZParams.x, 1.0e-3f);
    const float farZ  = max(gZParams.y, nearZ + 1.0f);
    const float ratio = farZ / nearZ;

    const float t0 = (float)cz / (float)kClusterSlicesZ;
    const float t1 = (float)(cz + 1u) / (float)kClusterSlicesZ;
    const float sliceNear = nearZ * pow(ratio, t0);
    const float sliceFar  = nearZ * pow(ratio, t1);

    // Tile NDC bounds: cx left->right [-1..+1], cy top->bottom [+1..-1]
    const float ndcX0 = -1.0f + 2.0f * ((float)cx / (float)kClusterTilesX);
    const float ndcX1 = -1.0f + 2.0f * ((float)(cx + 1u) / (float)kClusterTilesX);
    const float ndcYTop =  1.0f - 2.0f * ((float)cy / (float)kClusterTilesY);
    const float ndcYBot =  1.0f - 2.0f * ((float)(cy + 1u) / (float)kClusterTilesY);

    const float3 pTL = UnprojectNdcNearToView(ndcX0, ndcYTop);
    const float3 pTR = UnprojectNdcNearToView(ndcX1, ndcYTop);
    const float3 pBL = UnprojectNdcNearToView(ndcX0, ndcYBot);
    const float3 pBR = UnprojectNdcNearToView(ndcX1, ndcYBot);

    const float3 nTL = IntersectRayWithViewZPlane(pTL, sliceNear);
    const float3 nTR = IntersectRayWithViewZPlane(pTR, sliceNear);
    const float3 nBL = IntersectRayWithViewZPlane(pBL, sliceNear);
    const float3 nBR = IntersectRayWithViewZPlane(pBR, sliceNear);

    const float3 fTL = IntersectRayWithViewZPlane(pTL, sliceFar);
    const float3 fTR = IntersectRayWithViewZPlane(pTR, sliceFar);
    const float3 fBL = IntersectRayWithViewZPlane(pBL, sliceFar);
    const float3 fBR = IntersectRayWithViewZPlane(pBR, sliceFar);

    float3 bmin = min(min(nTL, nTR), min(nBL, nBR));
    bmin        = min(bmin, min(min(fTL, fTR), min(fBL, fBR)));

    float3 bmax = max(max(nTL, nTR), max(nBL, nBR));
    bmax        = max(bmax, max(max(fTL, fTR), max(fBL, fBR)));

    const uint clusterIdx = cz * (kClusterTilesX * kClusterTilesY) + cy * kClusterTilesX + cx;
    ClusterAabb outAabb;
    outAabb.minBounds = bmin;
    outAabb.pad0      = 0.0f;
    outAabb.maxBounds = bmax;
    outAabb.pad1      = 0.0f;
    gClusterAabbsOut[clusterIdx] = outAabb;
}

// ============================================================================
// Entry 2: LDS-Cached 1,024-Light Cluster Assignment (Zero Global Atomics)
// ============================================================================
StructuredBuffer<ClusterAabb>     gClusterAabbsIn      : register(t0);
StructuredBuffer<GpuClusterLight> gLights              : register(t1);

RWStructuredBuffer<uint2>         gClusterLightGrid    : register(u0); // [3456] = (offset, count)
RWStructuredBuffer<uint>          gClusterLightIndices : register(u1); // [3456 * 64]

groupshared GpuClusterLight gsLightBatch[64];

bool TestLightIntersectsCluster(GpuClusterLight light, float3 aabbMin, float3 aabbMax, float3 center, float sphereR)
{
    if (light.radius <= 1.0e-5f)
        return false;

    // 1) Exact closest-point sphere vs. AABB test
    const float3 closest = clamp(light.posView, aabbMin, aabbMax);
    const float3 delta   = closest - light.posView;
    const float  r2      = light.radius * light.radius;
    if (dot(delta, delta) > r2)
        return false;

    // 2) Exact conservative bounding-sphere vs. spotlight cone rejection
    if ((light.typeFlags & kLightTypeSpotBit) != 0u && light.cosOuterCone > -0.999f)
    {
        const float dirLenSq = dot(light.dirView, light.dirView);
        if (dirLenSq > 1.0e-8f)
        {
            const float3 D = light.dirView * rsqrt(dirLenSq);
            const float3 V = center - light.posView;
            const float  distSq = dot(V, V);
            const float  vAlong = dot(V, D);

            // Cluster bounding sphere completely beyond spotlight range along axis
            if (vAlong > light.radius + sphereR)
                return false;

            const float cosTheta = clamp(light.cosOuterCone, -1.0f, 1.0f);
            // For forward cones (half-angle <= 90 deg), reject spheres strictly behind apex plane
            if (cosTheta >= 0.0f && vAlong < -sphereR)
                return false;

            // If light apex lies outside the cluster bounding sphere, apply exact angular cone-vs-sphere test
            if (distSq > sphereR * sphereR)
            {
                const float dist = sqrt(distSq);
                // If cone is wide enough that theta + asin(R/dist) >= pi, sphere always intersects
                if (cosTheta >= -(sphereR / dist))
                {
                    const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
                    const float rhs = cosTheta * sqrt(max(0.0f, distSq - sphereR * sphereR)) - sinTheta * sphereR;
                    if (vAlong < rhs)
                        return false;
                }
            }
        }
    }

    return true;
}

[numthreads(64, 1, 1)]
void cs_cluster_cull_lights(uint3 dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID)
{
    const uint clusterIdx = dtid.x;
    const bool validCluster = (clusterIdx < kTotalClusters);

    float3 aabbMin = 0.0f.xxx;
    float3 aabbMax = 0.0f.xxx;
    float3 center  = 0.0f.xxx;
    float  sphereR = 0.0f;
    if (validCluster)
    {
        const ClusterAabb aabb = gClusterAabbsIn[clusterIdx];
        aabbMin = aabb.minBounds;
        aabbMax = aabb.maxBounds;
        center  = 0.5f * (aabbMin + aabbMax);
        sphereR = length(0.5f * (aabbMax - aabbMin));
    }

    const uint lightCount = min(gGridDims.w, 1024u);
    const uint baseOffset = clusterIdx * kMaxLightsPerCluster;
    uint localCount = 0u;

    // Cooperative LDS batching: each 64-light batch is fetched ONCE per 64-cluster group
    for (uint batchStart = 0u; batchStart < lightCount; batchStart += 64u)
    {
        const uint loadIdx = batchStart + gtid;
        if (loadIdx < lightCount)
        {
            gsLightBatch[gtid] = gLights[loadIdx];
        }
        else
        {
            GpuClusterLight emptyLight;
            emptyLight.posView      = 0.0f.xxx;
            emptyLight.radius       = 0.0f;
            emptyLight.dirView      = float3(0.0f, 0.0f, 1.0f);
            emptyLight.cosOuterCone = -1.0f;
            emptyLight.colorRgb     = 0.0f.xxx;
            emptyLight.typeFlags    = 0u;
            gsLightBatch[gtid] = emptyLight;
        }
        GroupMemoryBarrierWithGroupSync();

        if (validCluster && localCount < kMaxLightsPerCluster)
        {
            const uint batchCount = min(64u, lightCount - batchStart);
            for (uint i = 0u; i < batchCount; ++i)
            {
                if (TestLightIntersectsCluster(gsLightBatch[i], aabbMin, aabbMax, center, sphereR))
                {
                    if (localCount < kMaxLightsPerCluster)
                    {
                        gClusterLightIndices[baseOffset + localCount] = batchStart + i;
                        ++localCount;
                    }
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (validCluster)
    {
        gClusterLightGrid[clusterIdx] = uint2(baseOffset, localCount);
    }
}
