#pragma once
// RawBindlessHeap.h: RAW-LUMEN — Shader-Visible SM 6.6 ResourceDescriptorHeap[] Bindless Material Pool.
// Holds up to 65,536 deduplicated SRV descriptors (albedo, normal, TruePBR _rmaos.dds, ENB _m.dds,
// and WIC .png/.jpg/.tga textures) so Hardware DXR 1.2 off-screen reflection and multi-bounce GI rays
// sample exact full-resolution hit textures via ResourceDescriptorHeap[mat.albedoSrvIdx] with zero proxy SDFs.
// Original work, MIT.
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace raw::bridge
{

struct RawBindlessMaterial {
    uint32_t albedoIdx = 0xFFFFFFFFu;
    uint32_t normalIdx = 0xFFFFFFFFu;
    uint32_t rmaosIdx  = 0xFFFFFFFFu;
    uint32_t heightIdx = 0xFFFFFFFFu;
    float    roughness = 0.65f;
    float    metallic  = 0.0f;
    float    coatWeight = 0.0f;
    float    fuzzWeight = 0.0f;
};

class RawBindlessHeap {
public:
    static constexpr uint32_t kMaxDescriptors = 65536u;

    bool Init(ID3D12Device* dev);
    bool Ready() const { return m_ready; }
    ID3D12DescriptorHeap* Heap() const { return m_heap.Get(); }
    uint32_t Count() const { return m_nextSlot; }

    // Registers a 2D texture SRV into the bindless heap (deduplicated by ID3D12Resource*); returns slot index or 0xFFFFFFFFu.
    uint32_t RegisterTexture2D(ID3D12Resource* res, DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN);

    // Registers a material tuple into the GPU-readable material table; returns material index.
    uint32_t RegisterMaterial(const RawBindlessMaterial& mat);
    const std::vector<RawBindlessMaterial>& Materials() const { return m_materials; }

private:
    bool                                           m_ready = false;
    ID3D12Device*                                  m_dev = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>   m_heap;
    uint32_t                                       m_inc = 0;
    uint32_t                                       m_nextSlot = 0;
    std::mutex                                     m_mu;
    std::unordered_map<ID3D12Resource*, uint32_t>  m_texMap;
    std::vector<RawBindlessMaterial>               m_materials;
};

} // namespace raw::bridge
