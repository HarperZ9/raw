//=============================================================================
// ClusterCullCS — Cull lights against cluster AABBs, build index lists
//
// Dispatch: ceil(TotalClusters / 64), 1, 1
// Each thread handles one cluster, tests all lights.
//=============================================================================

cbuffer ClusterCB : register(b0)
{
    float4x4 ViewMatrix;
    float4x4 ProjMatrix;
    float4x4 InvProjMatrix;
    float    NearZ;
    float    FarZ;
    float    ScreenWidth;
    float    ScreenHeight;
    uint     LightCount;
    uint     ClusterGridX;
    uint     ClusterGridY;
    uint     ClusterGridZ;
};

struct LightData
{
    float4 positionAndRadius;   // xyz = world pos, w = radius
    float4 colorAndIntensity;   // xyz = linear RGB, w = intensity
    float4 directionAndAngle;   // xyz = spot dir, w = cos(halfAngle)
    uint   flags;
    uint   pad0, pad1, pad2;
};

struct ClusterAABB
{
    float4 minPoint;
    float4 maxPoint;
};

struct ClusterOut
{
    uint offset;
    uint count;
    uint pad0, pad1;
};

StructuredBuffer<LightData>    Lights         : register(t0);
StructuredBuffer<ClusterAABB>  ClusterAABBs   : register(t1);
RWStructuredBuffer<ClusterOut> ClusterGrid     : register(u0);
RWBuffer<uint>                 LightIndexList  : register(u1);
RWBuffer<uint>                 GlobalCounter   : register(u2);

// Sphere vs AABB intersection test
bool SphereIntersectsAABB(float3 center, float radius, float3 aabbMin, float3 aabbMax)
{
    float3 closest = clamp(center, aabbMin, aabbMax);
    float3 diff = center - closest;
    return dot(diff, diff) <= (radius * radius);
}

#define MAX_LIGHTS_PER_CLUSTER 128

groupshared uint gs_lightCount;
groupshared uint gs_lightIndices[MAX_LIGHTS_PER_CLUSTER];
groupshared uint gs_globalOffset;

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    uint totalClusters = ClusterGridX * ClusterGridY * ClusterGridZ;
    if (DTid.x >= totalClusters)
        return;

    // Each thread = one cluster (simple for now; optimize later with
    // per-thread light ranges for larger light counts)

    float3 aabbMin = ClusterAABBs[DTid.x].minPoint.xyz;
    float3 aabbMax = ClusterAABBs[DTid.x].maxPoint.xyz;

    // Collect lights that intersect this cluster
    uint localCount = 0;
    uint localIndices[MAX_LIGHTS_PER_CLUSTER];

    for (uint i = 0; i < LightCount && localCount < MAX_LIGHTS_PER_CLUSTER; ++i)
    {
        // Transform light position to view space
        float3 worldPos = Lights[i].positionAndRadius.xyz;
        float  radius   = Lights[i].positionAndRadius.w;

        float4 viewPos = mul(ViewMatrix, float4(worldPos, 1.0));

        if (SphereIntersectsAABB(viewPos.xyz, radius, aabbMin, aabbMax))
        {
            localIndices[localCount] = i;
            localCount++;
        }
    }

    // Allocate space in the global light index list
    uint globalOffset = 0;
    if (localCount > 0)
    {
        InterlockedAdd(GlobalCounter[0], localCount, globalOffset);
    }

    // Write cluster info
    ClusterGrid[DTid.x].offset = globalOffset;
    ClusterGrid[DTid.x].count  = localCount;

    // Write light indices
    for (uint j = 0; j < localCount; ++j)
    {
        LightIndexList[globalOffset + j] = localIndices[j];
    }
}
