// RtAlpha: see the header. Textures (masks and colour copies) are in RtAlphaTex.cpp. Original work, MIT.
#include "RtAlpha.h"

#include <algorithm>
#include <cstring>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

RtAlpha::~RtAlpha()
{
    for (auto& set : m_tex) for (Tex& t : set) if (t.src) t.src->Release();
}

bool RtAlpha::Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, std::string& err)
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
    // Face normals: one 32-bit word per triangle, a sixth of the coordinate pool (24 bytes per triangle).
    if (!buffer(m_pool, kPool, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt coordinates") ||
        !buffer(m_normals, kPool / 6, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt face normals") ||
        !buffer(m_records, kMaxRecords * 16ull, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt alpha records") ||
        !buffer(m_mats, kMaxRecords * 16ull, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt material records")) {
        err = "alpha resources"; return false;
    }
    const D3D12_RANGE none{0, 0};
    if (FAILED(m_records->Map(0, &none, reinterpret_cast<void**>(&m_rec))) || FAILED(m_mats->Map(0, &none, reinterpret_cast<void**>(&m_mat)))) {
        err = "alpha records map"; return false;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = UINT(kPool / 4); ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    dev->CreateUnorderedAccessView(m_pool.Get(), nullptr, &ud, {cpu0.ptr + SIZE_T(base) * m_inc});
    ud.Buffer.NumElements = UINT(kPool / 24);
    dev->CreateUnorderedAccessView(m_normals.Get(), nullptr, &ud, {cpu0.ptr + SIZE_T(base + NormalOffset()) * m_inc});
    return Pipelines(err);
}

void RtAlpha::Reset()
{
    for (auto& set : m_tex) { for (Tex& t : set) if (t.src) t.src->Release(); set.clear(); }
    for (auto& m : m_texOf) m.clear();
    m_uv.clear(); m_texJobs.clear();
    m_recordCount = 0; m_alphaRecords = 0; m_poolUsed = 0; m_albedoBytes = 0;
}

uint32_t RtAlpha::AddMesh(const RtDraw& d, ID3D12Resource* v12, ID3D12Resource* i12, uint64_t vStart, uint64_t iStart,
                          uint32_t triangles, bool alphaTested, std::vector<ID3D11Resource*>& toHand,
                          ID3D12Resource* (*native)(ID3D11Resource*))
{
    const uint64_t bytes = uint64_t(triangles) * 3 * 8;
    if (!d.alphaSrv || m_recordCount >= kMaxRecords || m_poolUsed + bytes > kPool || !Room()) {
        ++(alphaTested ? m_noMask : m_noAlbedo); return 0;
    }
    uint32_t mask = 0;
    if (alphaTested) {
        mask = TexFor(d.alphaSrv, false, toHand, native);
        if (mask == ~0u) { ++m_noMask; return 0; }
    }
    const uint32_t albedo = TexFor(d.alphaSrv, true, toHand, native);
    if (albedo == ~0u) {
        ++m_noAlbedo;
        if (!alphaTested) return 0;   // an opaque mesh without a colour copy joins uncoloured, recordless
    }
    // The coordinates come from the position's vertex buffer, or from their own stream.
    ID3D11Buffer* uvb = d.uvVb ? d.uvVb : d.vb;
    ID3D12Resource* uv12 = d.uvVb ? native(d.uvVb) : v12;
    D3D11_BUFFER_DESC vd{}, id{};
    uvb->GetDesc(&vd); d.ib->GetDesc(&id);
    if (!uv12 || vd.Usage == D3D11_USAGE_DYNAMIC) { ++m_noMask; return 0; }
    const uint64_t uvStart = d.uvVb ? uint64_t(d.uvVbOffset) + int64_t(d.baseVertex) * d.uvStride : vStart;
    if (d.uvVb) toHand.push_back(d.uvVb);
    UvJob j{};
    j.v12 = uv12; j.i12 = i12; j.vbBytes = vd.ByteWidth; j.ibBytes = id.ByteWidth;
    const bool half = d.uvFormat == DXGI_FORMAT_R16G16_FLOAT || d.uvFormat == DXGI_FORMAT_R16G16B16A16_FLOAT;
    j.c[0] = uint32_t(uvStart); j.c[1] = d.uvVb ? d.uvStride : d.stride; j.c[2] = d.uvOffset; j.c[3] = half ? 0u : 1u;
    j.c[4] = uint32_t(iStart); j.c[5] = d.ibFormat == DXGI_FORMAT_R32_UINT ? 1u : 0u; j.c[6] = triangles;
    j.c[7] = uint32_t(m_poolUsed / 8);   // the first coordinate, in 8-byte units (three per triangle)
    // Face normals from the positions, in the same buffer (when the coordinates have their own stream,
    // the positions are not there: no normals, and the hit faces the ray).
    j.c[8] = d.posOffset; j.c[9] = d.posFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 0u : 1u;
    j.c[10] = j.c[7] / 3; j.c[11] = d.uvVb ? 0u : 1u;
    m_uv.push_back(j);
    const uint32_t rec = m_recordCount++;
    m_rec[rec * 4] = j.c[7];
    m_rec[rec * 4 + 1] = mask;
    m_rec[rec * 4 + 2] = alphaTested ? m_tex[0][mask].w : 0;
    m_rec[rec * 4 + 3] = alphaTested ? m_tex[0][mask].h : 0;
    m_mat[rec * 4] = albedo == ~0u ? 0u : albedo + 1;
    m_mat[rec * 4 + 1] = j.c[10];
    m_mat[rec * 4 + 2] = (alphaTested ? 1u : 0u) | (d.uvVb ? 0u : 2u);
    m_mat[rec * 4 + 3] = 0;
    m_poolUsed += bytes;
    m_alphaRecords += alphaTested;
    return rec + 1;
}

bool RtAlpha::Record(ID3D12GraphicsCommandList4* cl, uint32_t slot)
{
    if (m_uv.empty() && m_texJobs.empty()) return false;
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = m_heap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = m_heap->GetGPUDescriptorHandleForHeapStart();
    const uint32_t stage = m_base + 1 + kMaxMasks + slot * kStage;
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetComputeRootDescriptorTable(2, {gpu0.ptr + UINT64(m_base) * m_inc});
    cl->SetComputeRootDescriptorTable(3, {gpu0.ptr + UINT64(m_base + 1) * m_inc});
    cl->SetComputeRootDescriptorTable(4, {gpu0.ptr + UINT64(m_base + NormalOffset()) * m_inc});
    uint32_t at = stage;
    auto raw = [&](ID3D12Resource* r, uint64_t bytes, uint32_t k) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_TYPELESS; sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.NumElements = UINT(bytes / 4); sd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        m_dev->CreateShaderResourceView(r, &sd, {cpu0.ptr + SIZE_T(k) * m_inc});
    };
    if (!m_uv.empty()) {
        cl->SetPipelineState(m_uvPso.Get());
        for (const UvJob& j : m_uv) {
            raw(j.v12, j.vbBytes, at);
            raw(j.i12, j.ibBytes, at + 1);
            cl->SetComputeRootDescriptorTable(1, {gpu0.ptr + UINT64(at) * m_inc});
            cl->SetComputeRoot32BitConstants(0, 12, j.c, 0);
            cl->Dispatch((j.c[6] + 63) / 64, 1, 1);
            at += 3;
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        bool set = false;
        for (const TexJob& j : m_texJobs) {
            if (j.albedo != (pass == 1)) continue;
            if (!set) { cl->SetPipelineState(pass ? m_albedoPso.Get() : m_maskPso.Get()); set = true; }
            m_dev->CreateShaderResourceView(j.src12, &j.srv, {cpu0.ptr + SIZE_T(at + 2) * m_inc});
            cl->SetComputeRootDescriptorTable(1, {gpu0.ptr + UINT64(at) * m_inc});
            const uint32_t first = j.albedo ? m_base + AlbedoOffset() : m_base + 1;
            cl->SetComputeRootDescriptorTable(3, {gpu0.ptr + UINT64(first + j.index) * m_inc});
            const uint32_t c[12] = {j.w, j.h};
            cl->SetComputeRoot32BitConstants(0, 12, c, 0);
            cl->Dispatch((j.w + 7) / 8, (j.h + 7) / 8, 1);
            at += 3;
        }
    }
    m_uv.clear();
    m_texJobs.clear();
    return true;
}

} // namespace raw::bridge
