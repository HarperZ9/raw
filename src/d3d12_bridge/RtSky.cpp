// RtSky: see the header. Original work, MIT.
#include "RtSky.h"
#include "rt_sky_filter.h"   // kDxil_rt_sky_filter (dxc, cs_6_6)

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

namespace
{
bool Tex(ID3D12Device* dev, ComPtr<ID3D12Resource>& r, UINT w, UINT h, DXGI_FORMAT f, const wchar_t* name,
         D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Format = f;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)))) return false;
    r->SetName(name);
    return true;
}
} // namespace

bool RtSky::Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, UINT width, UINT height, std::string& err)
{
    m_dev = dev; m_heap = heap; m_base = base; m_w = width; m_h = height;
    m_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // The filter reads the light-index output (RGBA8) as a typed UAV.
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R8G8B8A8_UNORM};
    if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof fs)) || !(fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) {
        err = "typed UAV loads of RGBA8 not supported"; return false;
    }
    const DXGI_FORMAT H = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!Tex(dev, m_raw, width, height, H, L"rt sky rays (bounce rgb, sky a)") ||
        !Tex(dev, m_hist[0], width, height, H, L"rt sky history 0") || !Tex(dev, m_hist[1], width, height, H, L"rt sky history 1") ||
        !Tex(dev, m_bhist[0], width, height, H, L"rt bounce history 0") || !Tex(dev, m_bhist[1], width, height, H, L"rt bounce history 1") ||
        !Tex(dev, m_bounce, width, height, H, L"rt bounce", D3D12_RESOURCE_STATE_COMMON) ||
        !Tex(dev, m_res[0], width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, L"rt light reservoirs 0") ||
        !Tex(dev, m_res[1], width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, L"rt light reservoirs 1") ||
        !Tex(dev, m_sunHist[0], width, height, H, L"rt sun shadow history 0") ||
        !Tex(dev, m_sunHist[1], width, height, H, L"rt sun shadow history 1")) {
        err = "sky textures"; return false;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
    auto uav = [&](ID3D12Resource* r, DXGI_FORMAT f, uint32_t k) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = f; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        dev->CreateUnorderedAccessView(r, nullptr, &ud, {cpu.ptr + SIZE_T(base + k) * m_inc});
    };
    uav(m_raw.Get(), H, 0); uav(m_hist[0].Get(), H, 1); uav(m_hist[1].Get(), H, 2);
    uav(m_raw.Get(), H, 3); uav(m_hist[1].Get(), H, 4); uav(m_hist[0].Get(), H, 5);
    uav(m_bhist[0].Get(), H, 6); uav(m_bhist[1].Get(), H, 7); uav(m_bounce.Get(), H, 8);
    uav(m_bhist[1].Get(), H, 9); uav(m_bhist[0].Get(), H, 10); uav(m_bounce.Get(), H, 11);
    const DXGI_FORMAT R = DXGI_FORMAT_R32G32B32A32_FLOAT;
    uav(m_res[0].Get(), R, 14); uav(m_res[1].Get(), R, 15); uav(m_res[1].Get(), R, 17); uav(m_res[0].Get(), R, 18);
    uav(m_sunHist[0].Get(), H, 20); uav(m_sunHist[1].Get(), H, 21);
    uav(m_sunHist[1].Get(), H, 22); uav(m_sunHist[0].Get(), H, 23);
    SetMotion(nullptr);

    // Root: the lighting pass's uniform (b0), constants (b1), depth (t0), motion (t2), the rays and the
    // two histories (u0..u2, one table per parity), the light-index output (u3), bounce (u4..u6),
    // visibility output (u7), and temporal sun-shadow history ping-pong (u8..u9).
    D3D12_DESCRIPTOR_RANGE depth{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE motion{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0};
    D3D12_DESCRIPTOR_RANGE hist{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE lights{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3, 0, 0};
    D3D12_DESCRIPTOR_RANGE bounce{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 4, 0, 0};   // u4 in, u5 out history; u6 the output
    D3D12_DESCRIPTOR_RANGE visRange{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 7, 0, 0}; // u7: RtVisibility (in-place sun .x)
    D3D12_DESCRIPTOR_RANGE sunHist{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 8, 0, 0};  // u8 in, u9 out sun shadow history
    D3D12_ROOT_PARAMETER p[9]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[0].Descriptor = {0, 0};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[1].Constants = {1, 0, 4};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[2].DescriptorTable = {1, &depth};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = {1, &motion};
    p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[4].DescriptorTable = {1, &hist};
    p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[5].DescriptorTable = {1, &lights};
    p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[6].DescriptorTable = {1, &bounce};
    p[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[7].DescriptorTable = {1, &visRange};
    p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[8].DescriptorTable = {1, &sunHist};
    D3D12_ROOT_SIGNATURE_DESC rs{9, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "sky root signature";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_sky_filter, sizeof kDxil_rt_sky_filter};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) { err = "sky filter pipeline"; return false; }
    return true;
}

bool RtSky::Reload(const void* dxil, size_t bytes, std::string& err)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {dxil, bytes};
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) { err = "the device refused the reloaded RtSkyFilter.hlsl"; return false; }
    m_pso = pso;
    return true;
}

void RtSky::SetMotion(ID3D12Resource* motion)
{
    // No motion: a null view reads zero (the reuse then looks at the same pixel).
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R16G16_FLOAT; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
    m_dev->CreateShaderResourceView(motion, &sd, {cpu.ptr + SIZE_T(m_base + 16) * m_inc});
    m_dev->CreateShaderResourceView(motion, &sd, {cpu.ptr + SIZE_T(m_base + 19) * m_inc});
}

void RtSky::SetColour(ID3D12Resource* colour)
{
    // The same resource every frame: rewriting the descriptor with the same view is harmless to the
    // frames in flight.
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_dev->CreateShaderResourceView(colour, &sd, {m_heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(ColourDescriptor()) * m_inc});
}

void RtSky::Record(ID3D12GraphicsCommandList4* cl, D3D12_GPU_VIRTUAL_ADDRESS uniform, ID3D12Resource* motion, uint64_t frame, float)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_heap->GetGPUDescriptorHandleForHeapStart();
    if (motion) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R16G16_FLOAT; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_dev->CreateShaderResourceView(motion, &sd, {cpu.ptr + SIZE_T(m_base + 12) * m_inc});
    }
    D3D12_RESOURCE_BARRIER b[2]{};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;   // the trace wrote the rays and the light indices
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition = {m_bounce.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    cl->ResourceBarrier(2, b);
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_pso.Get());
    cl->SetComputeRootConstantBufferView(0, uniform);
    const uint32_t c[4] = {motion ? 1u : 0u, m_reset ? 1u : 0u, 32u, 0u};
    cl->SetComputeRoot32BitConstants(1, 4, c, 0);
    cl->SetComputeRootDescriptorTable(2, gpu);
    cl->SetComputeRootDescriptorTable(3, motion ? D3D12_GPU_DESCRIPTOR_HANDLE{gpu.ptr + UINT64(m_base + 12) * m_inc} : gpu);   // no motion: never read
    cl->SetComputeRootDescriptorTable(4, {gpu.ptr + UINT64(m_base + ((frame & 1) ? 3 : 0)) * m_inc});
    cl->SetComputeRootDescriptorTable(5, {gpu.ptr + UINT64(4) * m_inc});
    cl->SetComputeRootDescriptorTable(6, {gpu.ptr + UINT64(m_base + ((frame & 1) ? 9 : 6)) * m_inc});
    cl->SetComputeRootDescriptorTable(7, {gpu.ptr + UINT64(3) * m_inc});
    cl->SetComputeRootDescriptorTable(8, {gpu.ptr + UINT64(m_base + ((frame & 1) ? 22 : 20)) * m_inc});
    cl->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    cl->ResourceBarrier(1, &b[1]);
    m_reset = false;
}

} // namespace raw::bridge
