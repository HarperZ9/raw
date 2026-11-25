// RawLightClusterHost.cpp: Native DX12 16x9x24 (3,456-Cluster) 3D Froxel Local Light Assignment Host.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawLightClusterHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_cluster_build_aabb_cs.h")
#define RAW_CLUSTER_HAS_BUILD_DXIL 1
#include "raw_cluster_build_aabb_cs.h"
#else
#define RAW_CLUSTER_HAS_BUILD_DXIL 0
#endif

#if __has_include("raw_cluster_cull_lights_cs.h")
#define RAW_CLUSTER_HAS_CULL_DXIL 1
#include "raw_cluster_cull_lights_cs.h"
#else
#define RAW_CLUSTER_HAS_CULL_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawLightClusterSim.hlsl byte-for-byte (256-byte aligned).
struct alignas(256) GpuLightClusterCB {
    float    invProj[16];  // row-major 4x4
    float    zParams[4];   // x = nearZ, y = farZ, z = log(far/near), w = 1/24
    uint32_t gridDims[4];  // x = 16, y = 9, z = 24, w = lightCount
    uint32_t flags[4];     // x = 64, y..w = 0
    uint32_t _pad[36];     // 64 + 16 + 16 + 16 + 144 = 256 bytes exact
};
static_assert(sizeof(GpuLightClusterCB) == 256u, "GpuLightClusterCB must be 256 bytes");

// Fixed shader-visible descriptor heap layout:
//   Slot 0: ClusterAabb UAV    (build pass u0)
//   Slot 1: ClusterAabb SRV    (cull pass t0)
//   Slot 2: Lights SRV         (cull pass t1)
//   Slot 3: ClusterGrid UAV    (cull pass u0)
//   Slot 4: LightIndices UAV   (cull pass u1)
//   Slot 5: ClusterGrid SRV    (external lighting consumer)
//   Slot 6: LightIndices SRV   (external lighting consumer)
constexpr uint32_t kTotalDescCount     = 8u;
constexpr uint32_t kSlotAabbUav        = 0u;
constexpr uint32_t kSlotAabbSrv        = 1u;
constexpr uint32_t kSlotLightsSrv      = 2u;
constexpr uint32_t kSlotGridUav        = 3u;
constexpr uint32_t kSlotIndicesUav     = 4u;
constexpr uint32_t kSlotGridExtSrv     = 5u;
constexpr uint32_t kSlotIndicesExtSrv  = 6u;

bool CreateBuffer(ID3D12Device* dev, uint64_t bytes, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heapType;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 255ull) & ~255ull;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(&out)));
}

void WriteStructuredSrv(ID3D12Device* dev, ID3D12Resource* res, uint32_t count, uint32_t stride,
                        D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Buffer.FirstElement = 0;
    d.Buffer.NumElements = count;
    d.Buffer.StructureByteStride = stride;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteStructuredUav(ID3D12Device* dev, ID3D12Resource* res, uint32_t count, uint32_t stride,
                        D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    d.Buffer.FirstElement = 0;
    d.Buffer.NumElements = count;
    d.Buffer.StructureByteStride = stride;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

Vec3 UnprojectNdcNearCpu(float ndcX, float ndcY, const float invProj[16])
{
    // Row-vector mul: v = [ndcX, ndcY, 0, 1] * invProj
    const float vx = ndcX * invProj[0] + ndcY * invProj[4] + invProj[12];
    const float vy = ndcX * invProj[1] + ndcY * invProj[5] + invProj[13];
    const float vz = ndcX * invProj[2] + ndcY * invProj[6] + invProj[14];
    const float vw = ndcX * invProj[3] + ndcY * invProj[7] + invProj[15];
    const float invW = (std::fabs(vw) > 1.0e-7f) ? (1.0f / vw) : 1.0f;
    return {vx * invW, vy * invW, vz * invW};
}

Vec3 IntersectZPlaneCpu(const Vec3& p, float zPlane)
{
    const float denom = (std::fabs(p.z) > 1.0e-6f) ? p.z : 1.0e-6f;
    const float s = zPlane / denom;
    return {p.x * s, p.y * s, p.z * s};
}

bool TestLightClusterCpu(const GpuClusterLight& l, const ClusterAabb& aabb)
{
    if (l.radius <= 1.0e-5f) return false;

    const float cx = std::clamp(l.posView[0], aabb.minBounds[0], aabb.maxBounds[0]);
    const float cy = std::clamp(l.posView[1], aabb.minBounds[1], aabb.maxBounds[1]);
    const float cz = std::clamp(l.posView[2], aabb.minBounds[2], aabb.maxBounds[2]);
    const float dx = cx - l.posView[0];
    const float dy = cy - l.posView[1];
    const float dz = cz - l.posView[2];
    if (dx * dx + dy * dy + dz * dz > l.radius * l.radius) return false;

    if ((l.typeFlags & kClusterLightSpotBit) != 0u && l.cosOuterCone > -0.999f) {
        const float dirLenSq = l.dirView[0] * l.dirView[0] + l.dirView[1] * l.dirView[1] + l.dirView[2] * l.dirView[2];
        if (dirLenSq > 1.0e-8f) {
            const float invD = 1.0f / std::sqrt(dirLenSq);
            const float Dx = l.dirView[0] * invD;
            const float Dy = l.dirView[1] * invD;
            const float Dz = l.dirView[2] * invD;

            const float midX = 0.5f * (aabb.minBounds[0] + aabb.maxBounds[0]);
            const float midY = 0.5f * (aabb.minBounds[1] + aabb.maxBounds[1]);
            const float midZ = 0.5f * (aabb.minBounds[2] + aabb.maxBounds[2]);
            const float extX = 0.5f * (aabb.maxBounds[0] - aabb.minBounds[0]);
            const float extY = 0.5f * (aabb.maxBounds[1] - aabb.minBounds[1]);
            const float extZ = 0.5f * (aabb.maxBounds[2] - aabb.minBounds[2]);
            const float sphereR = std::sqrt(extX * extX + extY * extY + extZ * extZ);

            const float Vx = midX - l.posView[0];
            const float Vy = midY - l.posView[1];
            const float Vz = midZ - l.posView[2];
            const float distSq = Vx * Vx + Vy * Vy + Vz * Vz;
            const float vAlong = Vx * Dx + Vy * Dy + Vz * Dz;

            if (vAlong > l.radius + sphereR) return false;

            const float cosTheta = std::clamp(l.cosOuterCone, -1.0f, 1.0f);
            if (cosTheta >= 0.0f && vAlong < -sphereR) return false;

            if (distSq > sphereR * sphereR) {
                const float dist = std::sqrt(distSq);
                if (cosTheta >= -(sphereR / dist)) {
                    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                    const float rhs = cosTheta * std::sqrt(std::max(0.0f, distSq - sphereR * sphereR)) - sinTheta * sphereR;
                    if (vAlong < rhs) return false;
                }
            }
        }
    }

    return true;
}

} // namespace

void BuildPerspectiveInvProjLH(float fovYRadians, float aspect, float nearZ, float farZ, float outInvProj[16])
{
    std::memset(outInvProj, 0, sizeof(float) * 16u);
    const float safeNear = std::max(nearZ, 1.0e-3f);
    const float safeFar  = std::max(farZ, safeNear + 1.0f);
    const float safeAsp  = std::max(aspect, 1.0e-3f);
    const float tanHalf  = std::tan(std::clamp(fovYRadians, 0.05f, 3.05f) * 0.5f);

    // Row-vector DX12 LH perspective P:
    //   x' = x / (aspect * tanHalf), y' = y / tanHalf, z' = z * F/(F-N) - N*F/(F-N), w' = z
    // Exact inverse P^-1 for row-vector [nx, ny, nz, 1] * P^-1:
    //   vx = nx * (aspect * tanHalf)
    //   vy = ny * tanHalf
    //   vz = 1
    //   vw = (1/N) - nz * (F - N) / (N * F)
    outInvProj[0]  = safeAsp * tanHalf;
    outInvProj[5]  = tanHalf;
    outInvProj[11] = -(safeFar - safeNear) / (safeNear * safeFar);
    outInvProj[14] = 1.0f;
    outInvProj[15] = 1.0f / safeNear;
}

void CpuBuildAndAssignClusters(const GpuClusterLight* lights,
                               uint32_t lightCount,
                               const LightClusterViewParams& view,
                               std::vector<ClusterAabb>& outAabbs,
                               std::vector<ClusterLightGridEntry>& outGrid,
                               std::vector<uint32_t>& outIndices)
{
    constexpr uint32_t kX = RawLightClusterHost::kTilesX;
    constexpr uint32_t kY = RawLightClusterHost::kTilesY;
    constexpr uint32_t kZ = RawLightClusterHost::kSlicesZ;
    constexpr uint32_t kTotal = RawLightClusterHost::kTotalClusters;
    constexpr uint32_t kMaxPer = RawLightClusterHost::kMaxLightsPerCluster;

    outAabbs.resize(kTotal);
    outGrid.resize(kTotal);
    outIndices.assign(RawLightClusterHost::kTotalIndexEntries, 0u);

    const float nearZ = std::max(view.nearZ, 1.0e-3f);
    const float farZ  = std::max(view.farZ, nearZ + 1.0f);
    const float ratio = farZ / nearZ;
    const uint32_t safeCount = (lights && lightCount > 0u) ? std::min(lightCount, RawLightClusterHost::kMaxLights) : 0u;

    for (uint32_t cz = 0u; cz < kZ; ++cz) {
        const float sliceNear = nearZ * std::pow(ratio, static_cast<float>(cz) / static_cast<float>(kZ));
        const float sliceFar  = nearZ * std::pow(ratio, static_cast<float>(cz + 1u) / static_cast<float>(kZ));
        for (uint32_t cy = 0u; cy < kY; ++cy) {
            const float ndcYTop = 1.0f - 2.0f * (static_cast<float>(cy) / static_cast<float>(kY));
            const float ndcYBot = 1.0f - 2.0f * (static_cast<float>(cy + 1u) / static_cast<float>(kY));
            for (uint32_t cx = 0u; cx < kX; ++cx) {
                const float ndcX0 = -1.0f + 2.0f * (static_cast<float>(cx) / static_cast<float>(kX));
                const float ndcX1 = -1.0f + 2.0f * (static_cast<float>(cx + 1u) / static_cast<float>(kX));

                const Vec3 p[4] = {
                    UnprojectNdcNearCpu(ndcX0, ndcYTop, view.invProj),
                    UnprojectNdcNearCpu(ndcX1, ndcYTop, view.invProj),
                    UnprojectNdcNearCpu(ndcX0, ndcYBot, view.invProj),
                    UnprojectNdcNearCpu(ndcX1, ndcYBot, view.invProj)
                };
                Vec3 corners[8];
                for (int i = 0; i < 4; ++i) {
                    corners[i]     = IntersectZPlaneCpu(p[i], sliceNear);
                    corners[i + 4] = IntersectZPlaneCpu(p[i], sliceFar);
                }

                ClusterAabb aabb{};
                aabb.minBounds[0] = corners[0].x; aabb.minBounds[1] = corners[0].y; aabb.minBounds[2] = corners[0].z;
                aabb.maxBounds[0] = corners[0].x; aabb.maxBounds[1] = corners[0].y; aabb.maxBounds[2] = corners[0].z;
                for (int i = 1; i < 8; ++i) {
                    aabb.minBounds[0] = std::min(aabb.minBounds[0], corners[i].x);
                    aabb.minBounds[1] = std::min(aabb.minBounds[1], corners[i].y);
                    aabb.minBounds[2] = std::min(aabb.minBounds[2], corners[i].z);
                    aabb.maxBounds[0] = std::max(aabb.maxBounds[0], corners[i].x);
                    aabb.maxBounds[1] = std::max(aabb.maxBounds[1], corners[i].y);
                    aabb.maxBounds[2] = std::max(aabb.maxBounds[2], corners[i].z);
                }

                const uint32_t idx = cz * (kX * kY) + cy * kX + cx;
                outAabbs[idx] = aabb;

                const uint32_t baseOff = idx * kMaxPer;
                uint32_t count = 0u;
                for (uint32_t li = 0u; li < safeCount && count < kMaxPer; ++li) {
                    if (TestLightClusterCpu(lights[li], aabb)) {
                        outIndices[baseOff + count] = li;
                        ++count;
                    }
                }
                outGrid[idx] = {baseOff, count};
            }
        }
    }
}

RawLightClusterHost::~RawLightClusterHost()
{
    Shutdown();
}

void RawLightClusterHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0, nullptr);
        m_cbMapped = nullptr;
    }
    if (m_lightUploadBuf && m_lightUploadMapped) {
        m_lightUploadBuf->Unmap(0, nullptr);
        m_lightUploadMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_lightUploadBuf.Reset();
    m_lightIndicesBuf.Reset();
    m_clusterGridBuf.Reset();
    m_lightBuf.Reset();
    m_aabbBuf.Reset();
    m_cullPso.Reset();
    m_cullRoot.Reset();
    m_buildPso.Reset();
    m_buildRoot.Reset();
    m_heap.Reset();
    m_dev = nullptr;
    m_ready = false;
    m_frameIdx = 0u;
    m_lastBuildMs = 0.0f;
    m_lastAssignMs = 0.0f;
    m_hasValidTiming = false;
}

bool RawLightClusterHost::Init(ID3D12Device* dev, const LightClusterDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawLightClusterHost: null ID3D12Device";
        return false;
    }

    D3D12_SHADER_BYTECODE buildCs = desc.customBuildShader;
    if (!buildCs.pShaderBytecode || buildCs.BytecodeLength == 0) {
#if RAW_CLUSTER_HAS_BUILD_DXIL
        buildCs = {kDxil_raw_cluster_build_aabb, sizeof kDxil_raw_cluster_build_aabb};
#else
        if (outErr) *outErr = "RawLightClusterHost: missing raw_cluster_build_aabb_cs.h and no customBuildShader";
        return false;
#endif
    }

    D3D12_SHADER_BYTECODE cullCs = desc.customCullShader;
    if (!cullCs.pShaderBytecode || cullCs.BytecodeLength == 0) {
#if RAW_CLUSTER_HAS_CULL_DXIL
        cullCs = {kDxil_raw_cluster_cull_lights, sizeof kDxil_raw_cluster_cull_lights};
#else
        if (outErr) *outErr = "RawLightClusterHost: missing raw_cluster_cull_lights_cs.h and no customCullShader";
        return false;
#endif
    }

    m_dev = dev;
    m_maxLights = std::clamp(desc.maxLights, 1u, kMaxLights);

    // 1) Shader-visible descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawLightClusterHost: CreateDescriptorHeap failed";
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2) Build-AABB root signature: b0 (CBV), u0 (ClusterAabb UAV)
    {
        D3D12_DESCRIPTOR_RANGE uavR{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER rp[2]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor = {0, 0};
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {1, &uavR};
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_buildRoot)))) {
            if (outErr) *outErr = "RawLightClusterHost: build root signature failed";
            return false;
        }
    }

    // 3) Cull-Lights root signature: b0 (CBV), t0..t1 (Aabb + Lights SRVs), u0..u1 (Grid + Indices UAVs)
    {
        D3D12_DESCRIPTOR_RANGE srvR{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
        D3D12_DESCRIPTOR_RANGE uavR{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 0};
        D3D12_ROOT_PARAMETER rp[3]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor = {0, 0};
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {1, &srvR};
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[2].DescriptorTable = {1, &uavR};
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{3, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_cullRoot)))) {
            if (outErr) *outErr = "RawLightClusterHost: cull root signature failed";
            return false;
        }
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_buildRoot.Get();
    pd.CS = buildCs;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_buildPso)))) {
        if (outErr) *outErr = "RawLightClusterHost: CreateComputePipelineState (build) failed";
        return false;
    }

    pd.pRootSignature = m_cullRoot.Get();
    pd.CS = cullCs;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_cullPso)))) {
        if (outErr) *outErr = "RawLightClusterHost: CreateComputePipelineState (cull) failed";
        return false;
    }

    // 4) GPU buffers & 4-frame persistently mapped upload rings
    const uint64_t aabbBytes    = static_cast<uint64_t>(kTotalClusters) * sizeof(ClusterAabb);
    const uint64_t lightBytes   = static_cast<uint64_t>(m_maxLights) * sizeof(GpuClusterLight);
    const uint64_t gridBytes    = static_cast<uint64_t>(kTotalClusters) * sizeof(ClusterLightGridEntry);
    const uint64_t indicesBytes = static_cast<uint64_t>(kTotalIndexEntries) * sizeof(uint32_t);
    m_lightRingStride           = (lightBytes + 255ull) & ~255ull;
    const uint64_t lightUpBytes = m_lightRingStride * kRingCount;
    const uint64_t cbBytes      = static_cast<uint64_t>(kRingCount) * sizeof(GpuLightClusterCB);

    if (!CreateBuffer(dev, aabbBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_COMMON, m_aabbBuf) ||
        !CreateBuffer(dev, lightBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_COMMON, m_lightBuf) ||
        !CreateBuffer(dev, gridBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_COMMON, m_clusterGridBuf) ||
        !CreateBuffer(dev, indicesBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_COMMON, m_lightIndicesBuf) ||
        !CreateBuffer(dev, lightUpBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ, m_lightUploadBuf) ||
        !CreateBuffer(dev, cbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawLightClusterHost: GPU buffer allocation failed";
        return false;
    }

    m_aabbState         = D3D12_RESOURCE_STATE_COMMON;
    m_lightState        = D3D12_RESOURCE_STATE_COMMON;
    m_clusterGridState  = D3D12_RESOURCE_STATE_COMMON;
    m_lightIndicesState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RANGE rr{0, 0};
    void* ptr = nullptr;
    if (FAILED(m_cbBuf->Map(0, &rr, &ptr))) {
        if (outErr) *outErr = "RawLightClusterHost: failed to map CBV ring";
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(ptr);
    std::memset(m_cbMapped, 0, static_cast<size_t>(cbBytes));

    ptr = nullptr;
    if (FAILED(m_lightUploadBuf->Map(0, &rr, &ptr))) {
        if (outErr) *outErr = "RawLightClusterHost: failed to map light upload ring";
        return false;
    }
    m_lightUploadMapped = static_cast<uint8_t*>(ptr);
    std::memset(m_lightUploadMapped, 0, static_cast<size_t>(lightUpBytes));

    // 5) Populate static descriptors
    WriteStructuredUav(dev, m_aabbBuf.Get(),         kTotalClusters,     sizeof(ClusterAabb),           CpuAt(kSlotAabbUav));
    WriteStructuredSrv(dev, m_aabbBuf.Get(),         kTotalClusters,     sizeof(ClusterAabb),           CpuAt(kSlotAabbSrv));
    WriteStructuredSrv(dev, m_lightBuf.Get(),        m_maxLights,        sizeof(GpuClusterLight),       CpuAt(kSlotLightsSrv));
    WriteStructuredUav(dev, m_clusterGridBuf.Get(),  kTotalClusters,     sizeof(ClusterLightGridEntry), CpuAt(kSlotGridUav));
    WriteStructuredUav(dev, m_lightIndicesBuf.Get(), kTotalIndexEntries, sizeof(uint32_t),              CpuAt(kSlotIndicesUav));
    WriteStructuredSrv(dev, m_clusterGridBuf.Get(),  kTotalClusters,     sizeof(ClusterLightGridEntry), CpuAt(kSlotGridExtSrv));
    WriteStructuredSrv(dev, m_lightIndicesBuf.Get(), kTotalIndexEntries, sizeof(uint32_t),              CpuAt(kSlotIndicesExtSrv));

    // 6) 4-frame timestamp query heap (4 queries/frame: BuildStart, BuildEnd, CullStart, CullEnd)
    {
        constexpr uint32_t kTotalQueries = kRingCount * 4u;
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kTotalQueries;
        if (FAILED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap))) ||
            !CreateBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t),
                          D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback)) {
            m_tsHeap.Reset();
            m_tsReadback.Reset();
        }
    }

    m_ready = true;
    return true;
}

void RawLightClusterHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                                     D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target)
{
    if (!res || curState == target) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = curState;
    b.Transition.StateAfter = target;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
    curState = target;
}

bool RawLightClusterHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    const D3D12_RANGE rr{byteOff, byteOff + 2u * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0, &rr, &ptr))) return false;
    const auto* ticks = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(ptr) + byteOff);
    const uint64_t t0 = ticks[0];
    const uint64_t t1 = ticks[1];
    const D3D12_RANGE wr{0, 0};
    m_tsReadback->Unmap(0, &wr);
    if (t1 < t0) return false;
    const double ms = static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz);
    if (!std::isfinite(ms) || ms < 0.0 || ms > 5000.0) return false;
    outMs = static_cast<float>(ms);
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE RawLightClusterHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawLightClusterHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

bool RawLightClusterHost::BuildAndAssign(ID3D12GraphicsCommandList* cmd,
                                         const GpuClusterLight* lights,
                                         uint32_t lightCount,
                                         const LightClusterViewParams& view)
{
    if (!m_ready || !cmd || !m_cbMapped || !m_lightUploadMapped) return false;

    // Harvest 3-frames-old retired timestamp slots (non-blocking)
    if (m_frameIdx >= (kRingCount - 1u)) {
        const uint32_t retired = (m_frameIdx - (kRingCount - 1u)) % kRingCount;
        float bMs = 0.0f, aMs = 0.0f;
        if (HarvestTimestampPair(retired * 4u + 0u, bMs) &&
            HarvestTimestampPair(retired * 4u + 2u, aMs)) {
            m_lastBuildMs = bMs;
            m_lastAssignMs = aMs;
            m_hasValidTiming = true;
        }
    }

    const uint32_t safeCount = (lights && lightCount > 0u) ? std::min(lightCount, m_maxLights) : 0u;
    const float nearZ = std::max(view.nearZ, 1.0e-3f);
    const float farZ  = std::max(view.farZ, nearZ + 1.0f);

    const uint32_t ring = m_frameIdx % kRingCount;

    // Upload active lights via 4-frame ring into VRAM m_lightBuf
    if (safeCount > 0u) {
        const uint64_t upOff = static_cast<uint64_t>(ring) * m_lightRingStride;
        const size_t copyBytes = static_cast<size_t>(safeCount) * sizeof(GpuClusterLight);
        std::memcpy(m_lightUploadMapped + upOff, lights, copyBytes);
        Transition(cmd, m_lightBuf.Get(), m_lightState, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyBufferRegion(m_lightBuf.Get(), 0u, m_lightUploadBuf.Get(), upOff, copyBytes);
    }
    Transition(cmd, m_lightBuf.Get(), m_lightState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    GpuLightClusterCB cb{};
    std::memcpy(cb.invProj, view.invProj, sizeof(cb.invProj));
    cb.zParams[0]  = nearZ;
    cb.zParams[1]  = farZ;
    cb.zParams[2]  = std::log(farZ / nearZ);
    cb.zParams[3]  = 1.0f / static_cast<float>(kSlicesZ);
    cb.gridDims[0] = kTilesX;
    cb.gridDims[1] = kTilesY;
    cb.gridDims[2] = kSlicesZ;
    cb.gridDims[3] = safeCount;
    cb.flags[0]    = kMaxLightsPerCluster;

    const size_t cbOff = static_cast<size_t>(ring) * sizeof(GpuLightClusterCB);
    std::memcpy(m_cbMapped + cbOff, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_cbBuf->GetGPUVirtualAddress() + cbOff;

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);

    // Pass 1: Build 16x9x24 tight view-space cluster AABBs
    Transition(cmd, m_aabbBuf.Get(), m_aabbState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 4u + 0u);
    }
    cmd->SetComputeRootSignature(m_buildRoot.Get());
    cmd->SetPipelineState(m_buildPso.Get());
    cmd->SetComputeRootConstantBufferView(0, cbGpu);
    cmd->SetComputeRootDescriptorTable(1, GpuAt(kSlotAabbUav));
    cmd->Dispatch((kTilesX + 7u) / 8u, (kTilesY + 7u) / 8u, kSlicesZ);
    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 4u + 1u);
    }

    // Pass 2: LDS-cached 1,024-light sphere/cone culling -> ClusterLightGrid + ClusterLightIndices
    Transition(cmd, m_aabbBuf.Get(), m_aabbState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_clusterGridBuf.Get(), m_clusterGridState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_lightIndicesBuf.Get(), m_lightIndicesState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 4u + 2u);
    }
    cmd->SetComputeRootSignature(m_cullRoot.Get());
    cmd->SetPipelineState(m_cullPso.Get());
    cmd->SetComputeRootConstantBufferView(0, cbGpu);
    cmd->SetComputeRootDescriptorTable(1, GpuAt(kSlotAabbSrv));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(kSlotGridUav));
    cmd->Dispatch((kTotalClusters + 63u) / 64u, 1u, 1u);
    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 4u + 3u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 4u + 0u, 4u,
                              m_tsReadback.Get(), static_cast<UINT64>(ring * 4u) * sizeof(uint64_t));
    }

    constexpr D3D12_RESOURCE_STATES kAllSrv =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    Transition(cmd, m_clusterGridBuf.Get(),  m_clusterGridState,  kAllSrv);
    Transition(cmd, m_lightIndicesBuf.Get(), m_lightIndicesState, kAllSrv);

    ++m_frameIdx;
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawLightClusterHost::GetClusterGridSRV() const { return GpuAt(kSlotGridExtSrv); }
D3D12_CPU_DESCRIPTOR_HANDLE RawLightClusterHost::GetClusterGridSRVCpu() const { return CpuAt(kSlotGridExtSrv); }
D3D12_GPU_DESCRIPTOR_HANDLE RawLightClusterHost::GetLightIndicesSRV() const { return GpuAt(kSlotIndicesExtSrv); }
D3D12_CPU_DESCRIPTOR_HANDLE RawLightClusterHost::GetLightIndicesSRVCpu() const { return CpuAt(kSlotIndicesExtSrv); }

} // namespace raw::bridge
