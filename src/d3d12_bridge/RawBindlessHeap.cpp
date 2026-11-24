// RawBindlessHeap.cpp: SM 6.6 ResourceDescriptorHeap[] bindless SRV pool for RAW-LUMEN hardware DXR hits.
// Original work, MIT.
#include "RawBindlessHeap.h"

namespace raw::bridge
{

bool RawBindlessHeap::Init(ID3D12Device* dev)
{
    if (!dev) return false;
    D3D12_FEATURE_DATA_D3D12_OPTIONS o0{};
    if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o0, sizeof o0)) ||
        o0.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3) {
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kMaxDescriptors;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) return false;
    m_dev = dev;
    m_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_nextSlot = 0;
    m_ready = true;
    return true;
}

uint32_t RawBindlessHeap::RegisterTexture2D(ID3D12Resource* res, DXGI_FORMAT fmt)
{
    if (!m_ready || !res) return 0xFFFFFFFFu;
    std::lock_guard<std::mutex> lk(m_mu);
    if (auto it = m_texMap.find(res); it != m_texMap.end()) return it->second;
    if (m_nextSlot >= kMaxDescriptors) return 0xFFFFFFFFu;

    const D3D12_RESOURCE_DESC rd = res->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = (fmt != DXGI_FORMAT_UNKNOWN) ? fmt : rd.Format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = rd.MipLevels ? rd.MipLevels : 1u;

    const uint32_t slot = m_nextSlot++;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += SIZE_T(slot) * SIZE_T(m_inc);
    m_dev->CreateShaderResourceView(res, &sd, cpu);
    m_texMap.emplace(res, slot);
    return slot;
}

uint32_t RawBindlessHeap::RegisterMaterial(const RawBindlessMaterial& mat)
{
    std::lock_guard<std::mutex> lk(m_mu);
    const uint32_t id = static_cast<uint32_t>(m_materials.size());
    m_materials.push_back(mat);
    return id;
}

} // namespace raw::bridge
