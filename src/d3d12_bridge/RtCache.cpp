// RtCache: see the header. Original work, MIT.
#include "RtCache.h"
#include "rt_cache_resolve.h"   // kDxil_rt_cache_resolve (dxc, cs_6_6)

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

bool RtCache::Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, std::string& err)
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
    const uint64_t valueBytes = uint64_t(kSlots) * 32 + 16;   // and two counters
    if (!buffer(m_keys, uint64_t(kSlots) * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt cache keys") ||
        !buffer(m_accum, uint64_t(kSlots) * 32, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt cache accumulators") ||
        !buffer(m_value, valueBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt cache values") ||
        !buffer(m_read, 3 * 4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false, L"rt cache count")) {
        err = "cache resources"; return false;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = heap->GetCPUDescriptorHandleForHeapStart();
    ID3D12Resource* r[3] = {m_keys.Get(), m_accum.Get(), m_value.Get()};
    const uint64_t bytes[3] = {uint64_t(kSlots) * 4, uint64_t(kSlots) * 32, valueBytes};
    for (int k = 0; k < 3; ++k) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = UINT(bytes[k] / 4); ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        dev->CreateUnorderedAccessView(r[k], nullptr, &ud, {cpu0.ptr + SIZE_T(base + k) * m_inc});
    }
    D3D12_DESCRIPTOR_RANGE uav{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 0};
    D3D12_ROOT_PARAMETER p[2]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 8};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable = {1, &uav};
    D3D12_ROOT_SIGNATURE_DESC rs{2, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "cache root signature";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_cache_resolve, sizeof kDxil_rt_cache_resolve};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) { err = "cache resolve pipeline"; return false; }
    return true;
}

void RtCache::Record(ID3D12GraphicsCommandList4* cl, uint64_t frame, uint32_t slot)
{
    D3D12_RESOURCE_BARRIER u{};
    u.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;   // the trace's adds before the resolve reads them
    cl->ResourceBarrier(1, &u);
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_pso.Get());
    const uint32_t c[8] = {kSlots, uint32_t(frame), m_reset ? 1u : 0u, kMaxAge, kMaxFrames, 0, 0, 0};
    cl->SetComputeRoot32BitConstants(0, 8, c, 0);
    cl->SetComputeRootDescriptorTable(1, {m_heap->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(m_base) * m_inc});
    cl->Dispatch((kSlots + 63) / 64, 1, 1);
    m_reset = false;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {m_value.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    cl->ResourceBarrier(1, &b);
    cl->CopyBufferRegion(m_read.Get(), slot * 4ull, m_value.Get(), uint64_t(kSlots) * 32 + (frame & 1) * 4, 4);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cl->ResourceBarrier(1, &b);
}

uint32_t RtCache::Cells(uint32_t retiredSlot)
{
    uint32_t* p = nullptr;
    const D3D12_RANGE r{retiredSlot * 4ull, retiredSlot * 4ull + 4};
    if (FAILED(m_read->Map(0, &r, reinterpret_cast<void**>(&p)))) return 0;
    const uint32_t n = p[retiredSlot];
    const D3D12_RANGE none{0, 0};
    m_read->Unmap(0, &none);
    return n;
}

} // namespace raw::bridge
