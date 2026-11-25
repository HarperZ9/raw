// RawMeshHost.cpp: Hardware DX12 Amplification + Mesh Shader (as_6_6 / ms_6_6) PSO & DispatchMesh host.
// Original work, MIT.
#include "RawMeshHost.h"
#include "raw_mesh_as.h"   // kDxil_raw_mesh_as (dxc, as_6_6)
#include "raw_mesh_ms.h"   // kDxil_raw_mesh_ms (dxc, ms_6_6)
#include "raw_subdiv_cs.h" // kDxil_raw_subdiv_cs (dxc, cs_6_6)

#include <cstring>

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename T>
struct alignas(void*) Subobject {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
    T                                   data{};
};

struct MeshPsoStream {
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE>            as;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE>            ms;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC>    rast;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> ds;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> rtv;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT>    dsv;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC>        sample;
};

bool CreateUploadBuffer(ID3D12Device* dev, const void* data, size_t bytes, ComPtr<ID3D12Resource>& out)
{
    if (!dev || bytes == 0) return false;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 255) & ~size_t(255);
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out))))
        return false;
    if (data) {
        void* p = nullptr;
        D3D12_RANGE r{0, 0};
        if (FAILED(out->Map(0, &r, &p))) return false;
        std::memcpy(p, data, bytes);
        out->Unmap(0, nullptr);
    }
    return true;
}
} // namespace

bool RawMeshHost::Init(ID3D12Device* dev, std::string* outErr)
{
    if (!dev) { if (outErr) *outErr = "null D3D12 device"; return false; }
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
    if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof o7)) ||
        o7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1) {
        if (outErr) *outErr = "GPU does not support DX12 Mesh Shader Tier 1";
        return false;
    }
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&m_dev2)))) {
        if (outErr) *outErr = "ID3D12Device2 unavailable";
        return false;
    }

    // Root signature: b0 (CBV), t0 (Meshlets SRV), t1 (Vertices SRV), t2 (Indices SRV)
    D3D12_ROOT_PARAMETER rp[4]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rp[0].Descriptor = {0, 0}; rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[1].Descriptor = {0, 0}; rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[2].Descriptor = {1, 0}; rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_MESH;
    rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[3].Descriptor = {2, 0}; rp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_MESH;
    D3D12_ROOT_SIGNATURE_DESC rsd{4, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> sig, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errBlob)) ||
        FAILED(m_dev2->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        if (outErr) *outErr = "RAW-MESH root signature creation failed";
        return false;
    }

    MeshPsoStream st{};
    st.root.data = m_root.Get();
    st.as.data = {kDxil_raw_mesh_as, sizeof kDxil_raw_mesh_as};
    st.ms.data = {kDxil_raw_mesh_ms, sizeof kDxil_raw_mesh_ms};
    st.rast.data.FillMode = D3D12_FILL_MODE_SOLID;
    st.rast.data.CullMode = D3D12_CULL_MODE_BACK;
    st.rast.data.DepthClipEnable = TRUE;
    st.ds.data.DepthEnable = TRUE;
    st.ds.data.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    st.ds.data.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    st.rtv.data.NumRenderTargets = 0;   // depth/prepass-ready; pixel shader attaches when bound to G-Buffer
    st.dsv.data = DXGI_FORMAT_D32_FLOAT;
    st.sample.data = {1, 0};

    D3D12_PIPELINE_STATE_STREAM_DESC psd{sizeof st, &st};
    if (FAILED(m_dev2->CreatePipelineState(&psd, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RAW-MESH AS+MS CreatePipelineState failed";
        return false;
    }
    if (!CreateUploadBuffer(dev, nullptr, sizeof(RawMeshFrameCB), m_cbBuf)) {
        if (outErr) *outErr = "RAW-MESH CBV allocation failed";
        return false;
    }
    D3D12_RANGE r{0, 0};
    if (FAILED(m_cbBuf->Map(0, &r, &m_cbMapped))) return false;

    // Optional RawSubdiv.hlsl (cs_6_6) 1->4 curved PN-Triangle compute pipeline (works on ALL DX12 SM 6.6 GPUs):
    D3D12_ROOT_PARAMETER sp[5]{};
    sp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; sp[0].Descriptor = {0, 0}; sp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; sp[1].Descriptor = {0, 0}; sp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; sp[2].Descriptor = {1, 0}; sp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; sp[3].Descriptor = {0, 0}; sp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; sp[4].Descriptor = {1, 0}; sp[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC srsd{5, sp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> sSig, sErr;
    if (SUCCEEDED(D3D12SerializeRootSignature(&srsd, D3D_ROOT_SIGNATURE_VERSION_1, &sSig, &sErr)) &&
        SUCCEEDED(m_dev2->CreateRootSignature(0, sSig->GetBufferPointer(), sSig->GetBufferSize(), IID_PPV_ARGS(&m_subdivRoot)))) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC cpd{};
        cpd.pRootSignature = m_subdivRoot.Get();
        cpd.CS = {kDxil_raw_subdiv_cs, sizeof kDxil_raw_subdiv_cs};
        if (SUCCEEDED(m_dev2->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_subdivPso))) &&
            CreateUploadBuffer(dev, nullptr, sizeof(RawSubdivCB), m_subdivCbBuf)) {
            m_subdivCbBuf->Map(0, &r, &m_subdivCbMapped);
        }
    }
    m_ready = true;
    return true;
}

bool RawMeshHost::UploadAsset(const RawMeshAsset& src, RawMeshGpuAsset* outGpu, std::string* outErr)
{
    if (!m_ready || !outGpu || src.meshlets.empty() || src.vertices.empty() || src.indices.empty()) {
        if (outErr) *outErr = "RawMeshHost not ready or empty asset";
        return false;
    }
    outGpu->name = src.name;
    outGpu->meshletCount = static_cast<uint32_t>(src.meshlets.size());
    const bool ok = CreateUploadBuffer(m_dev2.Get(), src.meshlets.data(), src.meshlets.size() * sizeof(RawMeshletDesc), outGpu->meshletBuf)
                 && CreateUploadBuffer(m_dev2.Get(), src.vertices.data(), src.vertices.size() * sizeof(RawMeshVertex), outGpu->vertexBuf)
                 && CreateUploadBuffer(m_dev2.Get(), src.indices.data(), src.indices.size() * sizeof(uint32_t), outGpu->indexBuf);
    if (!ok && outErr) *outErr = "failed to upload RAW-MESH GPU buffers";
    return ok;
}

void RawMeshHost::Dispatch(ID3D12GraphicsCommandList* cmd, const RawMeshGpuAsset& asset, const RawMeshFrameCB& cb)
{
    if (!m_ready || !cmd || asset.meshletCount == 0 || !m_cbMapped) return;
    ComPtr<ID3D12GraphicsCommandList6> cmd6;
    if (FAILED(cmd->QueryInterface(IID_PPV_ARGS(&cmd6)))) return;
    RawMeshFrameCB local = cb;
    local.meshletCount = asset.meshletCount;
    std::memcpy(m_cbMapped, &local, sizeof local);
    cmd6->SetGraphicsRootSignature(m_root.Get());
    cmd6->SetPipelineState(m_pso.Get());
    cmd6->SetGraphicsRootConstantBufferView(0, m_cbBuf->GetGPUVirtualAddress());
    cmd6->SetGraphicsRootShaderResourceView(1, asset.meshletBuf->GetGPUVirtualAddress());
    cmd6->SetGraphicsRootShaderResourceView(2, asset.vertexBuf->GetGPUVirtualAddress());
    cmd6->SetGraphicsRootShaderResourceView(3, asset.indexBuf->GetGPUVirtualAddress());
    cmd6->DispatchMesh((asset.meshletCount + 31u) / 32u, 1u, 1u);
}

void RawMeshHost::SubdivideMesh(ID3D12GraphicsCommandList* cmd,
                                D3D12_GPU_VIRTUAL_ADDRESS inVertsSrv, D3D12_GPU_VIRTUAL_ADDRESS inIdxSrv,
                                D3D12_GPU_VIRTUAL_ADDRESS outVertsUav, D3D12_GPU_VIRTUAL_ADDRESS outIdxUav,
                                const RawSubdivCB& cb)
{
    if (!cmd || !m_subdivPso || !m_subdivRoot || !m_subdivCbMapped || cb.inTriangleCount == 0) return;
    std::memcpy(m_subdivCbMapped, &cb, sizeof cb);
    cmd->SetComputeRootSignature(m_subdivRoot.Get());
    cmd->SetPipelineState(m_subdivPso.Get());
    cmd->SetComputeRootConstantBufferView(0, m_subdivCbBuf->GetGPUVirtualAddress());
    cmd->SetComputeRootShaderResourceView(1, inVertsSrv);
    cmd->SetComputeRootShaderResourceView(2, inIdxSrv);
    cmd->SetComputeRootUnorderedAccessView(3, outVertsUav);
    cmd->SetComputeRootUnorderedAccessView(4, outIdxUav);
    cmd->Dispatch((cb.inTriangleCount + 63u) / 64u, 1u, 1u);
}

} // namespace raw::bridge
