//=============================================================================
// ClusterBuildCS — Compute AABB for each frustum cluster
//
// Dispatch: ceil(GridX*GridY*GridZ / 64), 1, 1
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

struct ClusterAABB
{
    float4 minPoint;
    float4 maxPoint;
};

RWStructuredBuffer<ClusterAABB> Clusters : register(u0);

// Convert screen-space + view-space Z to view-space position
// Uses projection diagonal for direct reconstruction (avoids clip-space round-trip error)
float3 ScreenToView(float2 screenCoord, float viewZ)
{
    float2 ndc = float2(
        screenCoord.x / ScreenWidth  *  2.0 - 1.0,
        (1.0 - screenCoord.y / ScreenHeight) * 2.0 - 1.0
    );
    return float3(ndc.x * viewZ / ProjMatrix[0][0],
                  ndc.y * viewZ / ProjMatrix[1][1],
                  viewZ);
}

// Logarithmic depth slice: denser near camera, sparser far away
float SliceDepth(uint slice)
{
    float logRatio = log2(FarZ / NearZ);
    return NearZ * exp2(logRatio * float(slice) / float(ClusterGridZ));
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint totalClusters = ClusterGridX * ClusterGridY * ClusterGridZ;
    if (DTid.x >= totalClusters)
        return;

    // Decode linear index to 3D cluster coordinate
    uint z = DTid.x / (ClusterGridX * ClusterGridY);
    uint rem = DTid.x % (ClusterGridX * ClusterGridY);
    uint y = rem / ClusterGridX;
    uint x = rem % ClusterGridX;

    // Screen-space tile bounds
    float tileW = ScreenWidth  / float(ClusterGridX);
    float tileH = ScreenHeight / float(ClusterGridY);

    float2 tileMins = float2(float(x) * tileW, float(y) * tileH);
    float2 tileMaxs = float2(float(x + 1) * tileW, float(y + 1) * tileH);

    // Depth bounds (logarithmic slicing)
    float nearDepth = SliceDepth(z);
    float farDepth  = SliceDepth(z + 1);

    // Convert 4 screen corners at near/far depths to view space
    float3 minView = float3(1e30, 1e30, 1e30);
    float3 maxView = float3(-1e30, -1e30, -1e30);

    // Near plane corners (pass view-space Z directly from logarithmic slicing)
    float3 nbl = ScreenToView(tileMins, nearDepth);
    float3 nbr = ScreenToView(float2(tileMaxs.x, tileMins.y), nearDepth);
    float3 ntl = ScreenToView(float2(tileMins.x, tileMaxs.y), nearDepth);
    float3 ntr = ScreenToView(tileMaxs, nearDepth);

    // Far plane corners
    float3 fbl = ScreenToView(tileMins, farDepth);
    float3 fbr = ScreenToView(float2(tileMaxs.x, tileMins.y), farDepth);
    float3 ftl = ScreenToView(float2(tileMins.x, tileMaxs.y), farDepth);
    float3 ftr = ScreenToView(tileMaxs, farDepth);

    minView = min(minView, min(min(nbl, nbr), min(ntl, ntr)));
    minView = min(minView, min(min(fbl, fbr), min(ftl, ftr)));
    maxView = max(maxView, max(max(nbl, nbr), max(ntl, ntr)));
    maxView = max(maxView, max(max(fbl, fbr), max(ftl, ftr)));

    Clusters[DTid.x].minPoint = float4(minView, 0);
    Clusters[DTid.x].maxPoint = float4(maxView, 0);
}
