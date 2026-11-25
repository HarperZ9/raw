#pragma once
// NativeDx12Host.h: Native Direct3D 12 Adoption Seam for Creation Engine 2 (Starfield) & Clean-Room CE Runtime.
// Bypasses the D3D11->D3D12 translation layer (raw::tx) completely: adopts an existing native ID3D12Device,
// ID3D12CommandQueue, and native DX12 G-buffer/depth/motion/HDR resources directly into RAW's SM 6.9/6.10
// render graph (LightingPass, RtScene, RawMeshHost, Rawlss5Pass). Original work, MIT.

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>

#include "D3D12Bridge.h"
#include "HardwareProfile.h"
#include "../common/raw_engine_profile.h"

namespace raw::bridge {

struct NativeDx12FrameInputs {
    ID3D12Resource* hdrColorIn   = nullptr;   // RGBA16F scene radiance before post/tonemap
    ID3D12Resource* hdrColorOut  = nullptr;   // RGBA16F reconstructed output (may alias hdrColorIn via barrier)
    ID3D12Resource* depth        = nullptr;   // R32F / D32F hardware depth
    ID3D12Resource* normal       = nullptr;   // world/view normals
    ID3D12Resource* albedo       = nullptr;   // base color / albedo manifold (feeds RAWLSS 7.0 demodulation)
    ID3D12Resource* material     = nullptr;   // roughness/metallic/specular layer
    ID3D12Resource* motion       = nullptr;   // RG16F velocity vectors
    ID3D12Resource* emissive     = nullptr;   // optional emissive buffer
    uint32_t        width        = 0;
    uint32_t        height       = 0;
    DXGI_FORMAT     colorFormat  = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT     normalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};

struct NativeDx12HostDesc {
    ID3D12Device*                device = nullptr;
    ID3D12CommandQueue*          directQueue = nullptr;
    ID3D12CommandQueue*          asyncComputeQueue = nullptr;   // optional: overlaps BLAS/TLAS & froxel scans
    raw::versions::EngineProfile engineProfile{};
    HardwareProfile              hwProfile{};
};

// Populates a HostLayer compatible with raw::bridge passes directly from a native DX12 host (zero D3D11 wrapper).
inline HostLayer MakeNativeDx12HostLayer(const NativeDx12HostDesc& desc) {
    HostLayer h{};
    h.device = desc.device;
    h.queue = desc.directQueue;
    h.native = nullptr;    // native DX12 resources need no ID3D11Resource* unwrapping
    h.prepare = nullptr;
    return h;
}

}  // namespace raw::bridge
