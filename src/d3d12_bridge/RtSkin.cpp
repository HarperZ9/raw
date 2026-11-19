// RtSkin: see the header. Original work, MIT.
#include "RtSkin.h"
#include "rt_skin.h"   // kDxil_rt_skin (dxc, cs_6_6)

#include <algorithm>
#include <cstring>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;
namespace
{
constexpr uint64_t Align256(uint64_t v) { return (v + 255) & ~255ull; }
}

bool RtSkin::Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, std::string& err)
{
    m_dev = dev; m_heap = heap; m_base = base;
    m_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto buffer = [&](ComPtr<ID3D12Resource>& r, uint64_t bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES s, bool uav, const wchar_t* name) {
        D3D12_HEAP_PROPERTIES hp{type};
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, s, nullptr, IID_PPV_ARGS(&r)))) return false;
        r->SetName(name);
        return true;
    };
    bool ok = buffer(m_scratch, kScratch, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt skin scratch");
    const D3D12_RANGE none{0, 0};
    for (int k = 0; ok && k < 3; ++k)
        ok = buffer(m_pool[k], kPool, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt skin positions") &&
             buffer(m_blas[k], kBlas, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true, L"rt skin blas") &&
             buffer(m_bones[k], kBones, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt skin bones") &&
             SUCCEEDED(m_bones[k]->Map(0, &none, reinterpret_cast<void**>(&m_bonesMapped[k])));
    if (!ok) { err = "skin resources"; return false; }
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE uav{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER p[4]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 28};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable = {1, &srv};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[2].Descriptor = {2, 0};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = {1, &uav};
    D3D12_ROOT_SIGNATURE_DESC rs{4, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "skin root signature";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_skin, sizeof kDxil_rt_skin};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) { err = "skin pipeline"; return false; }
    return true;
}

void RtSkin::SetDraws(std::vector<RtDraw>& draws, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    m_jobs.clear(); m_boneData.clear(); m_vertices = 0;
    uint32_t outFirst = 0;
    for (const RtDraw& d : draws) {
        if (!d.skinned || !d.bones || m_jobs.size() >= kMaxDraws) continue;
        D3D11_BUFFER_DESC vd{}, bd{}, id{};
        d.vb->GetDesc(&vd); d.blendVb->GetDesc(&bd); d.ib->GetDesc(&id);
        ID3D12Resource* v12 = native(d.vb);
        ID3D12Resource* b12 = native(d.blendVb);
        ID3D12Resource* i12 = native(d.ib);
        const int64_t vStart = int64_t(d.vbOffset) + int64_t(d.baseVertex) * d.stride;
        const int64_t bStart = int64_t(d.blendVbOffset) + int64_t(d.baseVertex) * d.blendStride;
        const uint32_t isz = d.ibFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
        const uint64_t iStart = uint64_t(d.ibOffset) + uint64_t(d.startIndex) * isz;
        if (!v12 || !b12 || !i12 || !d.stride || !d.blendStride || vStart < 0 || bStart < 0 || uint64_t(vStart) >= vd.ByteWidth ||
            uint64_t(bStart) >= bd.ByteWidth || iStart + uint64_t(d.indexCount) * isz > id.ByteWidth) continue;
        const uint32_t count = uint32_t(std::min<uint64_t>({(vd.ByteWidth - uint64_t(vStart)) / d.stride, (bd.ByteWidth - uint64_t(bStart)) / d.blendStride, uint64_t(kMaxVertices)}));
        if (uint64_t(outFirst + count) * 12 > kPool) break;
        Job j{};
        j.vb = v12; j.bv = b12; j.ib = i12; j.vbBytes = vd.ByteWidth; j.bvBytes = bd.ByteWidth; j.iStart = iStart;
        j.ibFormat = d.ibFormat; j.indexCount = d.indexCount / 3 * 3;
        const uint32_t posFmt = d.posFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 0u : 1u;
        uint32_t c[28] = {uint32_t(vStart), d.stride, d.posOffset, posFmt, uint32_t(bStart), d.blendStride, d.idxOffset, d.wOffset,
                          count, outFirst, uint32_t(m_jobs.size() * 240), d.skinWorld ? 1u : 0u};
        std::memcpy(c + 12, m_cam, 12);
        if (d.skinWorld) std::memcpy(c + 16, d.world, 48);   // rows of the world block: where the bind pose stands
        std::memcpy(j.c, c, sizeof c);
        m_boneData.insert(m_boneData.end(), d.bones, d.bones + 960);
        m_jobs.push_back(j);
        outFirst += count;
        m_vertices += count;
        toHand.push_back(d.vb); toHand.push_back(d.blendVb); toHand.push_back(d.ib);
    }
}

bool RtSkin::Record(ID3D12GraphicsCommandList4* cl, uint32_t slot)
{
    m_built = 0;
    if (m_jobs.empty()) return false;
    std::memcpy(m_bonesMapped[slot], m_boneData.data(), m_boneData.size() * sizeof(float));
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = m_heap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = m_heap->GetGPUDescriptorHandleForHeapStart();
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_pso.Get());
    cl->SetComputeRootShaderResourceView(2, m_bones[slot]->GetGPUVirtualAddress());
    auto raw = [&](ID3D12Resource* r, uint64_t bytes, uint32_t k, bool uav) {
        if (uav) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
            ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = UINT(bytes / 4); ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            m_dev->CreateUnorderedAccessView(r, nullptr, &ud, {cpu0.ptr + SIZE_T(k) * m_inc});
            return;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_TYPELESS; sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.NumElements = UINT(bytes / 4); sd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        m_dev->CreateShaderResourceView(r, &sd, {cpu0.ptr + SIZE_T(k) * m_inc});
    };
    uint32_t at = m_base + slot * kMaxDraws * 3;
    for (const Job& j : m_jobs) {
        raw(j.vb, j.vbBytes, at, false);
        raw(j.bv, j.bvBytes, at + 1, false);
        raw(m_pool[slot].Get(), kPool, at + 2, true);
        cl->SetComputeRootDescriptorTable(1, {gpu0.ptr + UINT64(at) * m_inc});
        cl->SetComputeRootDescriptorTable(3, {gpu0.ptr + UINT64(at + 2) * m_inc});
        cl->SetComputeRoot32BitConstants(0, 28, j.c, 0);
        cl->Dispatch((j.c[8] + 63) / 64, 1, 1);
        at += 3;
    }
    D3D12_RESOURCE_BARRIER u{};
    u.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    u.UAV.pResource = m_pool[slot].Get();
    cl->ResourceBarrier(1, &u);
    uint64_t blasAt = 0, scratchAt = 0;
    for (Job& j : m_jobs) {
        D3D12_RAYTRACING_GEOMETRY_DESC g{};
        g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        g.Triangles.VertexBuffer = {m_pool[slot]->GetGPUVirtualAddress() + uint64_t(j.c[9]) * 12, 12};
        g.Triangles.VertexCount = j.c[8];
        g.Triangles.IndexFormat = j.ibFormat;
        g.Triangles.IndexBuffer = j.ib->GetGPUVirtualAddress() + j.iStart;
        g.Triangles.IndexCount = j.indexCount;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
        in.NumDescs = 1; in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs = &g;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pre{};
        m_dev->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pre);
        const uint64_t size = Align256(pre.ResultDataMaxSizeInBytes), scratch = Align256(pre.ScratchDataSizeInBytes);
        j.built = false;
        if (blasAt + size > kBlas || scratchAt + scratch > kScratch) continue;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC b{};
        b.Inputs = in;
        b.DestAccelerationStructureData = m_blas[slot]->GetGPUVirtualAddress() + blasAt;
        b.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress() + scratchAt;
        cl->BuildRaytracingAccelerationStructure(&b, 0, nullptr);
        j.blas = blasAt; j.built = true;
        blasAt += size; scratchAt += scratch;
        ++m_built;
    }
    return true;
}

uint32_t RtSkin::Place(D3D12_RAYTRACING_INSTANCE_DESC* out, uint32_t room, uint32_t slot, uint64_t& tris) const
{
    uint32_t n = 0;
    for (const Job& j : m_jobs) {
        if (!j.built || n >= room) continue;
        D3D12_RAYTRACING_INSTANCE_DESC& x = out[n++];
        std::memset(&x, 0, sizeof x);
        x.Transform[0][0] = x.Transform[1][1] = x.Transform[2][2] = 1.0f;
        x.InstanceMask = 0xFF;
        x.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
        x.AccelerationStructure = m_blas[slot]->GetGPUVirtualAddress() + j.blas;
        tris += j.indexCount / 3;
    }
    return n;
}

} // namespace raw::bridge
