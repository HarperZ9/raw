// RtAlpha's textures: the alpha masks and colour copies made from the game's textures, and the
// pipelines that make them (RtAlpha.h). Original work, MIT.
#include "RtAlpha.h"
#include "rt_alpha_uv.h"       // kDxil_rt_alpha_uv (dxc, cs_6_6)
#include "rt_alpha_mask.h"     // kDxil_rt_alpha_mask
#include "rt_alpha_albedo.h"   // kDxil_rt_alpha_albedo

#include <algorithm>
#include <cmath>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

bool RtAlpha::Pipelines(std::string& err)
{
    // Root: constants (b0), the job's game buffers and texture (t0..t2), the pool (u0), the mask or
    // colour copy (u1), the face normals (u2).
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE pool{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE mask{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0};
    D3D12_DESCRIPTOR_RANGE normals{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0, 0};
    D3D12_ROOT_PARAMETER p[5]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 12};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable = {1, &srv};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[2].DescriptorTable = {1, &pool};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = {1, &mask};
    p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[4].DescriptorTable = {1, &normals};
    D3D12_STATIC_SAMPLER_DESC ss{};
    ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    ss.MaxLOD = D3D12_FLOAT32_MAX;
    ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rs{5, p, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
        FAILED(m_dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "alpha root signature";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_alpha_uv, sizeof kDxil_rt_alpha_uv};
    if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_uvPso)))) { err = "alpha coordinates pipeline"; return false; }
    pd.CS = {kDxil_rt_alpha_mask, sizeof kDxil_rt_alpha_mask};
    if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_maskPso)))) { err = "alpha mask pipeline"; return false; }
    pd.CS = {kDxil_rt_alpha_albedo, sizeof kDxil_rt_alpha_albedo};
    if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_albedoPso)))) { err = "colour copy pipeline"; return false; }
    return true;
}

uint32_t RtAlpha::TexFor(ID3D11ShaderResourceView* v, bool albedo, std::vector<ID3D11Resource*>& toHand,
                         ID3D12Resource* (*native)(ID3D11Resource*))
{
    const int set = albedo ? 1 : 0;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    if (!r) return ~0u;
    auto it = m_texOf[set].find(r);
    if (it != m_texOf[set].end()) { r->Release(); return it->second; }
    ID3D12Resource* r12 = native(r);
    ID3D11Texture2D* t2 = nullptr;
    const size_t limit = albedo ? kMaxAlbedo : kMaxMasks;
    if (!r12 || m_tex[set].size() >= limit || FAILED(r->QueryInterface(IID_PPV_ARGS(&t2)))) { r->Release(); return ~0u; }
    D3D11_TEXTURE2D_DESC td{};
    t2->GetDesc(&td);
    t2->Release();
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    v->GetDesc(&vd);
    const UINT mip = vd.Texture2D.MostDetailedMip;
    const UINT w0 = std::max(1u, td.Width >> mip), h0 = std::max(1u, td.Height >> mip);
    const UINT side = albedo ? m_albedoSide : kMaskSide;
    const float scale = std::min(1.0f, float(side) / float(std::max(w0, h0)));
    const UINT w = std::max(1u, UINT(std::lround(w0 * scale))), h = std::max(1u, UINT(std::lround(h0 * scale)));
    const DXGI_FORMAT fmt = albedo ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;
    Tex t;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Format = fmt;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&t.tex)))) {
        r->Release();
        return ~0u;
    }
    t.tex->SetName(albedo ? L"rt colour copy" : L"rt alpha mask");
    const uint32_t index = uint32_t(m_tex[set].size());
    const uint32_t first = albedo ? m_base + AlbedoOffset() : m_base + 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = fmt; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_dev->CreateUnorderedAccessView(t.tex.Get(), nullptr, &ud, {m_heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(first + index) * m_inc});
    TexJob j{};
    j.src12 = r12; j.index = index; j.w = w; j.h = h; j.albedo = albedo;
    j.srv.Format = vd.Format; j.srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    j.srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    j.srv.Texture2D.MostDetailedMip = mip; j.srv.Texture2D.MipLevels = vd.Texture2D.MipLevels;
    m_texJobs.push_back(j);
    t.w = w; t.h = h;
    t.src = r;   // the reference GetResource added: the game texture lives while its copy does
    m_tex[set].push_back(std::move(t));
    m_texOf[set].emplace(r, index);
    if (albedo) m_albedoBytes += uint64_t(w) * h * 4;
    toHand.push_back(r);   // the job reads it this frame
    return index;
}

} // namespace raw::bridge
