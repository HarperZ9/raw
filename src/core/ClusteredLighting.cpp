//=============================================================================
//  ClusteredLighting.cpp — GPU clustered forward+ lighting implementation
//
//  Two compute passes:
//    1. ClusterBuildCS: build frustum AABBs per cluster
//    2. ClusterCullCS:  cull lights per cluster, write compact index list
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "ClusteredLighting.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

using namespace DirectX;

namespace SB
{

// ── Embedded HLSL compute shaders ─────────────────────────────────────────

static const char kClusterBuildCS[] = R"HLSL(
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

// Convert screen-space + depth to view-space position
float3 ScreenToView(float2 screenCoord, float depth)
{
    float2 ndc = float2(
        screenCoord.x / ScreenWidth  *  2.0 - 1.0,
        (1.0 - screenCoord.y / ScreenHeight) * 2.0 - 1.0
    );
    float4 clip = float4(ndc, depth, 1.0);
    float4 view = mul(InvProjMatrix, clip);
    return view.xyz / view.w;
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

    // Near plane corners
    float3 nbl = ScreenToView(tileMins, nearDepth / FarZ);
    float3 nbr = ScreenToView(float2(tileMaxs.x, tileMins.y), nearDepth / FarZ);
    float3 ntl = ScreenToView(float2(tileMins.x, tileMaxs.y), nearDepth / FarZ);
    float3 ntr = ScreenToView(tileMaxs, nearDepth / FarZ);

    // Far plane corners
    float3 fbl = ScreenToView(tileMins, farDepth / FarZ);
    float3 fbr = ScreenToView(float2(tileMaxs.x, tileMins.y), farDepth / FarZ);
    float3 ftl = ScreenToView(float2(tileMins.x, tileMaxs.y), farDepth / FarZ);
    float3 ftr = ScreenToView(tileMaxs, farDepth / FarZ);

    minView = min(minView, min(min(nbl, nbr), min(ntl, ntr)));
    minView = min(minView, min(min(fbl, fbr), min(ftl, ftr)));
    maxView = max(maxView, max(max(nbl, nbr), max(ntl, ntr)));
    maxView = max(maxView, max(max(fbl, fbr), max(ftl, ftr)));

    Clusters[DTid.x].minPoint = float4(minView, 0);
    Clusters[DTid.x].maxPoint = float4(maxView, 0);
}
)HLSL";

static const char kClusterCullCS[] = R"HLSL(
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
)HLSL";

// ── FNV-1a hash for temporal cache ────────────────────────────────────────

static uint64_t HashLightSet(const std::vector<SceneLight>& lights)
{
    constexpr uint64_t kOffset = 14695981039346656037ULL;
    constexpr uint64_t kPrime  = 1099511628211ULL;

    uint64_t hash = kOffset;
    for (auto& l : lights) {
        // Hash all SceneLight fields: position(3) + radius(1) + color(3) + intensity(1) = 32 bytes
        // plus direction(3) + spotAngle(1) = 16 bytes — total 48 bytes
        auto* bytes = reinterpret_cast<const uint8_t*>(&l);
        constexpr int kHashBytes = static_cast<int>(
            sizeof(l.position) + sizeof(l.radius) + sizeof(l.color) +
            sizeof(l.intensity) + sizeof(l.direction) + sizeof(l.spotAngle));
        for (int i = 0; i < kHashBytes; ++i) {
            hash ^= bytes[i];
            hash *= kPrime;
        }
    }
    return hash;
}

// ── Initialize ────────────────────────────────────────────────────────────

bool ClusteredLighting::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (m_initialized) return true;

    m_device = device;
    m_context = context;

    if (!CompileComputeShaders(device)) {
        SKSE::log::error("ClusteredLighting: failed to compile compute shaders");
        return false;
    }

    if (!CreateBuffers(device)) {
        SKSE::log::error("ClusteredLighting: failed to create GPU buffers");
        Shutdown();
        return false;
    }

    m_initialized = true;
    SKSE::log::info("ClusteredLighting: initialized — {} max lights, {}x{}x{} clusters",
        kMaxLights, kClusterGridX, kClusterGridY, kClusterGridZ);
    return true;
}

void ClusteredLighting::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    SafeRelease(m_clusterBuildCS);
    SafeRelease(m_clusterCullCS);
    SafeRelease(m_lightBuffer);
    SafeRelease(m_lightBufferSRV);
    SafeRelease(m_lightBufferUAV);
    SafeRelease(m_clusterGridBuffer);
    SafeRelease(m_clusterGridSRV);
    SafeRelease(m_clusterGridUAV);
    SafeRelease(m_lightIndexBuffer);
    SafeRelease(m_lightIndexSRV);
    SafeRelease(m_lightIndexUAV);
    SafeRelease(m_constantsCB);
    SafeRelease(m_counterBuffer);
    SafeRelease(m_counterUAV);

    m_initialized = false;
}

// ── Compile compute shaders ───────────────────────────────────────────────

bool ClusteredLighting::CompileComputeShaders(ID3D11Device* device)
{
    auto CompileCS = [&](const char* source, size_t len, const char* entry,
                         ID3D11ComputeShader** outCS) -> bool
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(source, len, nullptr, nullptr, nullptr,
                                entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                &blob, &errors);
        if (FAILED(hr)) {
            if (errors) {
                SKSE::log::error("ClusteredLighting CS compile error: {}",
                    static_cast<const char*>(errors->GetBufferPointer()));
                errors->Release();
            }
            return false;
        }
        if (errors) errors->Release();

        hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                         nullptr, outCS);
        blob->Release();
        return SUCCEEDED(hr);
    };

    if (!CompileCS(kClusterBuildCS, sizeof(kClusterBuildCS) - 1, "main", &m_clusterBuildCS))
        return false;

    if (!CompileCS(kClusterCullCS, sizeof(kClusterCullCS) - 1, "main", &m_clusterCullCS))
        return false;

    return true;
}

// ── Create GPU buffers ────────────────────────────────────────────────────

bool ClusteredLighting::CreateBuffers(ID3D11Device* device)
{
    HRESULT hr;

    // ── Light buffer (StructuredBuffer<GPULightData>) ────────────────────
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth           = kMaxLights * sizeof(GPULightData);
        desc.Usage               = D3D11_USAGE_DYNAMIC;
        desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(GPULightData);
        hr = device->CreateBuffer(&desc, nullptr, &m_lightBuffer);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements  = kMaxLights;
        hr = device->CreateShaderResourceView(m_lightBuffer, &srvDesc, &m_lightBufferSRV);
        if (FAILED(hr)) {
            m_lightBuffer->Release();
            m_lightBuffer = nullptr;
            return false;
        }
    }

    // ── Cluster AABB buffer (for build CS output / cull CS input) ─────────
    // Using same struct as ClusterInfo but with AABB data (32 bytes per cluster)
    {
        struct ClusterAABB { float minPt[4]; float maxPt[4]; };

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth           = kTotalClusters * sizeof(ClusterAABB);
        desc.Usage               = D3D11_USAGE_DEFAULT;
        desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(ClusterAABB);
        hr = device->CreateBuffer(&desc, nullptr, &m_clusterGridBuffer);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements  = kTotalClusters;
        hr = device->CreateShaderResourceView(m_clusterGridBuffer, &srvDesc, &m_clusterGridSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = kTotalClusters;
        hr = device->CreateUnorderedAccessView(m_clusterGridBuffer, &uavDesc, &m_clusterGridUAV);
        if (FAILED(hr)) return false;
    }

    // NOTE: Cluster output (offset/count) currently shares m_clusterGridBuffer.
    // The cull CS writes ClusterOut into m_clusterGridUAV directly.
    // A separate buffer may be needed when integrating with the final pipeline.

    // ── Light index list (Buffer<uint>) ──────────────────────────────────
    {
        uint32_t maxIndices = kTotalClusters * kMaxLightsPerCluster;

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth  = maxIndices * sizeof(uint32_t);
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        hr = device->CreateBuffer(&desc, nullptr, &m_lightIndexBuffer);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format              = DXGI_FORMAT_R32_UINT;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements  = maxIndices;
        hr = device->CreateShaderResourceView(m_lightIndexBuffer, &srvDesc, &m_lightIndexSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R32_UINT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = maxIndices;
        hr = device->CreateUnorderedAccessView(m_lightIndexBuffer, &uavDesc, &m_lightIndexUAV);
        if (FAILED(hr)) return false;
    }

    // ── Constants CB ─────────────────────────────────────────────────────
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(ClusterBuildCB);
        desc.Usage           = D3D11_USAGE_DYNAMIC;
        desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
        hr = device->CreateBuffer(&desc, nullptr, &m_constantsCB);
        if (FAILED(hr)) return false;
    }

    // ── Global counter buffer (for atomic index allocation) ──────────────
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth  = sizeof(uint32_t);
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_UNORDERED_ACCESS;
        hr = device->CreateBuffer(&desc, nullptr, &m_counterBuffer);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R32_UINT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = 1;
        hr = device->CreateUnorderedAccessView(m_counterBuffer, &uavDesc, &m_counterUAV);
        if (FAILED(hr)) return false;
    }

    return true;
}

// ── Collect lights from game scene graph ──────────────────────────────────

void ClusteredLighting::CollectLights()
{
    m_sceneLights.clear();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto* cell = player->GetParentCell();
    if (!cell) return;

    auto playerPos = player->GetPosition();

    // Search all light references in a large radius around the player.
    // Use the same ForEachReferenceInRange + TESObjectLIGH approach as LightTracker.
    static constexpr float kSearchRadius = 8192.0f;  // 2x LightTracker's range

    cell->ForEachReferenceInRange(playerPos, kSearchRadius,
        [this](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult
    {
        if (ref.IsDisabled() || ref.IsDeleted())
            return RE::BSContainer::ForEachResult::kContinue;

        auto* baseObj = ref.GetBaseObject();
        if (!baseObj)
            return RE::BSContainer::ForEachResult::kContinue;

        auto* lightForm = baseObj->As<RE::TESObjectLIGH>();
        if (!lightForm)
            return RE::BSContainer::ForEachResult::kContinue;

        auto lightPos = ref.GetPosition();

        SceneLight sl;
        sl.position  = { lightPos.x, lightPos.y, lightPos.z };
        sl.radius    = static_cast<float>(lightForm->data.radius);
        sl.color     = {
            lightForm->data.color.red   / 255.0f,
            lightForm->data.color.green / 255.0f,
            lightForm->data.color.blue  / 255.0f
        };
        sl.intensity   = lightForm->fade;

        // Spot light detection: extract direction from reference rotation + FOV from form data
        bool isSpot = lightForm->data.flags.all(RE::TES_LIGHT_FLAGS::kSpotlight);
        if (isSpot && lightForm->data.fov > 0.0f) {
            sl.spotAngle = lightForm->data.fov * 0.5f * 3.14159265f / 180.0f;  // half-angle radians
            // Direction from reference rotation (Skyrim: X=pitch, Z=yaw, Y-up)
            auto angle = ref.GetAngle();
            float pitch = angle.x;
            float yaw   = angle.z;
            sl.direction = {
                std::cos(pitch) * std::sin(yaw),
                std::cos(pitch) * std::cos(yaw),
               -std::sin(pitch)
            };
        } else {
            sl.direction = { 0, 0, 0 };
            sl.spotAngle = 0;
        }

        // Shadow casting: check all shadow type flags
        sl.castsShadow = lightForm->data.flags.all(RE::TES_LIGHT_FLAGS::kSpotShadow)
                      || lightForm->data.flags.all(RE::TES_LIGHT_FLAGS::kHemiShadow)
                      || lightForm->data.flags.all(RE::TES_LIGHT_FLAGS::kOmniShadow);

        if (sl.radius > 0.1f && m_sceneLights.size() < kMaxLights) {
            m_sceneLights.push_back(sl);
        }

        return RE::BSContainer::ForEachResult::kContinue;
    });

    m_visibleLightCount = static_cast<uint32_t>(m_sceneLights.size());
}

// ── Upload light data to GPU ──────────────────────────────────────────────

void ClusteredLighting::UploadLightData(ID3D11DeviceContext* ctx)
{
    if (m_sceneLights.empty()) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(m_lightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    auto* gpu = static_cast<GPULightData*>(mapped.pData);
    for (size_t i = 0; i < m_sceneLights.size(); ++i) {
        auto& sl = m_sceneLights[i];
        gpu[i].positionAndRadius = { sl.position.x, sl.position.y, sl.position.z, sl.radius };
        gpu[i].colorAndIntensity = { sl.color.x, sl.color.y, sl.color.z, sl.intensity };
        gpu[i].directionAndAngle = { sl.direction.x, sl.direction.y, sl.direction.z,
                                     sl.spotAngle > 0 ? std::cos(sl.spotAngle) : -1.0f };
        gpu[i].flags = (sl.spotAngle > 0 ? 1u : 0u) | (sl.castsShadow ? 2u : 0u);
        gpu[i].pad[0] = gpu[i].pad[1] = gpu[i].pad[2] = 0;
    }

    ctx->Unmap(m_lightBuffer, 0);
}

// ── Temporal cache check ──────────────────────────────────────────────────

bool ClusteredLighting::ShouldRebuildClusters() const
{
    if (m_framesSinceRebuild >= kForceRebuildInterval)
        return true;  // Force periodic rebuild

    if (m_sceneLights.size() != m_lastLightCount)
        return true;  // Light count changed

    uint64_t hash = HashLightSet(m_sceneLights);
    return (hash != m_lastLightHash);  // Light positions changed
}

// ── Dispatch compute passes ───────────────────────────────────────────────

void ClusteredLighting::Dispatch(
    ID3D11DeviceContext* ctx,
    const XMFLOAT4X4& view,
    const XMFLOAT4X4& proj,
    float nearZ, float farZ,
    uint32_t screenWidth, uint32_t screenHeight)
{
    if (!m_initialized || !m_enabled) return;
    if (m_sceneLights.empty()) {
        m_clusteredLightCount = 0;
        return;
    }

    // Temporal cache: skip cluster rebuild if lights haven't moved
    m_framesSinceRebuild++;
    bool needRebuild = ShouldRebuildClusters();

    if (!needRebuild) {
        m_temporalCacheHit = true;
        return;
    }
    m_temporalCacheHit = false;
    m_framesSinceRebuild = 0;

    // Update temporal cache state
    m_lastLightCount = static_cast<uint32_t>(m_sceneLights.size());
    m_lastLightHash  = HashLightSet(m_sceneLights);

    // Upload light data
    UploadLightData(ctx);

    // Update constants CB
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = ctx->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            auto* cb = static_cast<ClusterBuildCB*>(mapped.pData);
            cb->viewMatrix   = view;
            cb->projMatrix   = proj;

            // Compute inverse projection
            XMMATRIX projMat = XMLoadFloat4x4(&proj);
            XMMATRIX invProj = XMMatrixInverse(nullptr, projMat);
            XMStoreFloat4x4(&cb->invProjMatrix, invProj);

            cb->nearZ        = nearZ;
            cb->farZ         = farZ;
            cb->screenWidth  = static_cast<float>(screenWidth);
            cb->screenHeight = static_cast<float>(screenHeight);
            cb->lightCount   = m_visibleLightCount;
            cb->clusterGridX = kClusterGridX;
            cb->clusterGridY = kClusterGridY;
            cb->clusterGridZ = kClusterGridZ;
            ctx->Unmap(m_constantsCB, 0);
        }
    }

    // ── Save CS state ────────────────────────────────────────────────────
    ID3D11ComputeShader* prevCS = nullptr;
    ID3D11Buffer* prevCB = nullptr;
    ID3D11ShaderResourceView* prevSRV[2] = {};
    ID3D11UnorderedAccessView* prevUAV[3] = {};
    ctx->CSGetShader(&prevCS, nullptr, nullptr);
    ctx->CSGetConstantBuffers(0, 1, &prevCB);
    ctx->CSGetShaderResources(0, 2, prevSRV);
    ctx->CSGetUnorderedAccessViews(0, 3, prevUAV);

    // ── Pass 1: Cluster build ────────────────────────────────────────────
    {
        ctx->CSSetShader(m_clusterBuildCS, nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &m_constantsCB);
        ID3D11UnorderedAccessView* uavs[] = { m_clusterGridUAV };
        UINT initCounts[] = { 0 };
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, initCounts);

        uint32_t groups = (kTotalClusters + 63) / 64;
        ctx->Dispatch(groups, 1, 1);
    }

    // Clear global counter
    {
        UINT clearVal[4] = { 0, 0, 0, 0 };
        ctx->ClearUnorderedAccessViewUint(m_counterUAV, clearVal);
    }

    // ── Pass 2: Cluster cull ─────────────────────────────────────────────
    {
        ctx->CSSetShader(m_clusterCullCS, nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &m_constantsCB);

        // SRVs: t0 = lights, t1 = cluster AABBs
        ID3D11ShaderResourceView* srvs[] = { m_lightBufferSRV, m_clusterGridSRV };
        ctx->CSSetShaderResources(0, 2, srvs);

        // UAVs: u0 = cluster output, u1 = light index list, u2 = counter
        ID3D11UnorderedAccessView* uavs[] = { m_clusterGridUAV, m_lightIndexUAV, m_counterUAV };
        UINT initCounts[] = { 0, 0, 0 };
        ctx->CSSetUnorderedAccessViews(0, 3, uavs, initCounts);

        uint32_t groups = (kTotalClusters + 63) / 64;
        ctx->Dispatch(groups, 1, 1);
    }

    // ── Restore CS state ─────────────────────────────────────────────────
    ctx->CSSetShader(prevCS, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &prevCB);
    ctx->CSSetShaderResources(0, 2, prevSRV);
    ctx->CSSetUnorderedAccessViews(0, 3, prevUAV, nullptr);

    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    SafeRelease(prevCS);
    SafeRelease(prevCB);
    SafeRelease(prevSRV[0]);
    SafeRelease(prevSRV[1]);
    SafeRelease(prevUAV[0]);
    SafeRelease(prevUAV[1]);
    SafeRelease(prevUAV[2]);

    m_clusteredLightCount = m_visibleLightCount;
}

} // namespace SB
