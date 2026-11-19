// RtAlpha: see the header. Original work, MIT.
#include "RtAlpha.h"
#include "rt_alpha_uv.h"     // kDxil_rt_alpha_uv (dxc, cs_6_6)
#include "rt_alpha_mask.h"   // kDxil_rt_alpha_mask

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

RtAlpha::~RtAlpha()
{
    for (Mask& m : m_masks) if (m.src) m.src->Release();
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
    if (!buffer(m_pool, kPool, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt alpha coordinates") ||
        !buffer(m_records, kMaxRecords * 8ull, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt alpha records")) {
        err = "alpha resources"; return false;
    }
    const D3D12_RANGE none{0, 0};
    if (FAILED(m_records->Map(0, &none, reinterpret_cast<void**>(&m_rec)))) { err = "alpha records map"; return false; }
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = UINT(kPool / 4); ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    dev->CreateUnorderedAccessView(m_pool.Get(), nullptr, &ud, {heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(base) * m_inc});

    // Root: constants (b0), the job's game buffers and texture (t0..t2), the pool (u0), the mask (u1).
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE pool{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE mask{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0};
    D3D12_ROOT_PARAMETER p[4]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 8};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable = {1, &srv};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[2].DescriptorTable = {1, &pool};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = {1, &mask};
    D3D12_STATIC_SAMPLER_DESC ss{};
    ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    ss.MaxLOD = D3D12_FLOAT32_MAX;
    ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rs{4, p, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "alpha root signature";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_alpha_uv, sizeof kDxil_rt_alpha_uv};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_uvPso)))) { err = "alpha coordinates pipeline"; return false; }
    pd.CS = {kDxil_rt_alpha_mask, sizeof kDxil_rt_alpha_mask};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_maskPso)))) { err = "alpha mask pipeline"; return false; }
    return true;
}

void RtAlpha::Reset()
{
    for (Mask& m : m_masks) if (m.src) m.src->Release();
    m_masks.clear(); m_maskOf.clear(); m_uv.clear(); m_maskJobs.clear();
    m_recordCount = 0; m_poolUsed = 0;
}

uint32_t RtAlpha::MaskFor(ID3D11ShaderResourceView* v, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    if (!r) return ~0u;
    auto it = m_maskOf.find(r);
    if (it != m_maskOf.end()) { r->Release(); return it->second; }
    ID3D12Resource* r12 = native(r);
    ID3D11Texture2D* t2 = nullptr;
    if (!r12 || m_masks.size() >= kMaxMasks || FAILED(r->QueryInterface(IID_PPV_ARGS(&t2)))) { r->Release(); return ~0u; }
    D3D11_TEXTURE2D_DESC td{};
    t2->GetDesc(&td);
    t2->Release();
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    v->GetDesc(&vd);
    const UINT mip = vd.Texture2D.MostDetailedMip;
    const UINT w0 = std::max(1u, td.Width >> mip), h0 = std::max(1u, td.Height >> mip);
    const float scale = std::min(1.0f, float(kMaskSide) / float(std::max(w0, h0)));
    const UINT w = std::max(1u, UINT(std::lround(w0 * scale))), h = std::max(1u, UINT(std::lround(h0 * scale)));
    Mask m;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8_UNORM;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m.tex)))) {
        r->Release();
        return ~0u;
    }
    m.tex->SetName(L"rt alpha mask");
    const uint32_t index = uint32_t(m_masks.size());
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R8_UNORM; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_dev->CreateUnorderedAccessView(m.tex.Get(), nullptr, &ud, {m_heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(m_base + 1 + index) * m_inc});
    MaskJob j{};
    j.src12 = r12; j.mask = index; j.w = w; j.h = h;
    j.srv.Format = vd.Format; j.srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    j.srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    j.srv.Texture2D.MostDetailedMip = mip; j.srv.Texture2D.MipLevels = vd.Texture2D.MipLevels;
    m_maskJobs.push_back(j);
    m.src = r;   // the reference GetResource added: the game texture lives while its mask does
    m_masks.push_back(std::move(m));
    m_maskOf.emplace(r, index);
    toHand.push_back(r);   // the mask job reads it this frame
    return index;
}

uint32_t RtAlpha::AddMesh(const RtDraw& d, ID3D12Resource* v12, ID3D12Resource* i12, uint64_t vStart, uint64_t iStart,
                          uint32_t triangles, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    const uint64_t bytes = uint64_t(triangles) * 3 * 8;
    if (!d.alphaSrv || m_recordCount >= kMaxRecords || m_poolUsed + bytes > kPool || !Room()) {
        ++m_noMask; return 0;
    }
    const uint32_t mask = MaskFor(d.alphaSrv, toHand, native);
    if (mask == ~0u) { ++m_noMask; return 0; }
    D3D11_BUFFER_DESC vd{}, id{};
    d.vb->GetDesc(&vd); d.ib->GetDesc(&id);
    UvJob j{};
    j.v12 = v12; j.i12 = i12; j.vbBytes = vd.ByteWidth; j.ibBytes = id.ByteWidth;
    const bool half = d.uvFormat == DXGI_FORMAT_R16G16_FLOAT || d.uvFormat == DXGI_FORMAT_R16G16B16A16_FLOAT;
    j.c[0] = uint32_t(vStart); j.c[1] = d.stride; j.c[2] = d.uvOffset; j.c[3] = half ? 0u : 1u;
    j.c[4] = uint32_t(iStart); j.c[5] = d.ibFormat == DXGI_FORMAT_R32_UINT ? 1u : 0u; j.c[6] = triangles;
    j.c[7] = uint32_t(m_poolUsed / 8);   // the first coordinate, in 8-byte units
    m_uv.push_back(j);
    const uint32_t rec = m_recordCount++;
    m_rec[rec * 2] = j.c[7];
    m_rec[rec * 2 + 1] = mask;
    m_poolUsed += bytes;
    return rec + 1;
}

bool RtAlpha::Record(ID3D12GraphicsCommandList4* cl, uint32_t slot)
{
    if (m_uv.empty() && m_maskJobs.empty()) return false;
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = m_heap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = m_heap->GetGPUDescriptorHandleForHeapStart();
    const uint32_t stage = m_base + 1 + kMaxMasks + slot * kStage;
    const D3D12_GPU_DESCRIPTOR_HANDLE pool{gpu0.ptr + UINT64(m_base) * m_inc};
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetComputeRootDescriptorTable(2, pool);
    cl->SetComputeRootDescriptorTable(3, {gpu0.ptr + UINT64(m_base + 1) * m_inc});
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
            cl->SetComputeRoot32BitConstants(0, 8, j.c, 0);
            cl->Dispatch((j.c[6] + 63) / 64, 1, 1);
            at += 3;
        }
    }
    if (!m_maskJobs.empty()) {
        cl->SetPipelineState(m_maskPso.Get());
        for (const MaskJob& j : m_maskJobs) {
            m_dev->CreateShaderResourceView(j.src12, &j.srv, {cpu0.ptr + SIZE_T(at + 2) * m_inc});
            cl->SetComputeRootDescriptorTable(1, {gpu0.ptr + UINT64(at) * m_inc});
            cl->SetComputeRootDescriptorTable(3, {gpu0.ptr + UINT64(m_base + 1 + j.mask) * m_inc});
            const uint32_t c[8] = {j.w, j.h, 0, 0, 0, 0, 0, 0};
            cl->SetComputeRoot32BitConstants(0, 8, c, 0);
            cl->Dispatch((j.w + 7) / 8, (j.h + 7) / 8, 1);
            at += 3;
        }
    }
    m_uv.clear();
    m_maskJobs.clear();
    return true;
}

} // namespace raw::bridge
