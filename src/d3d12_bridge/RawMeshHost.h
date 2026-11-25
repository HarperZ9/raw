#pragma once
// RawMeshHost.h: Live Hardware DX12 Amplification + Mesh Shader (as_6_6 / ms_6_6) Runtime Host.
// Creates the native D3D12_PIPELINE_STATE_STREAM_DESC (AS + MS) pipeline from RawMesh.hlsl DXIL,
// uploads RawMeshAsset meshlets (.glb/.obj or runtime-meshletized game draws) to GPU buffers,
// and executes wave-compacted GPU-culled DispatchMesh() with curved Vlachos PN-Triangle subdivision.
// Original work, MIT.
#include "RawAssetBridge.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace raw::bridge
{

struct RawMeshGpuAsset {
    std::string                          name;
    uint32_t                             meshletCount = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> meshletBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuf;
};

struct alignas(256) RawMeshFrameCB {
    float    worldToClip[16];
    float    frustumPlanes[6][4];
    float    camPosAndNear[4];   // xyz = camera pos, w = PN-Triangle subdivision distance threshold
    uint32_t meshletCount;
    float    curvatureGain;      // Vlachos cubic Bezier normal projection gain (default 0.72)
    float    displaceAmp;        // world-space micro-displacement scale
    float    snowDepthU;         // live 3D volumetric snowpack buildup thickness (0..48 world units)
};

class RawMeshHost {
public:
    bool Init(ID3D12Device* dev, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }
    bool UploadAsset(const RawMeshAsset& src, RawMeshGpuAsset* outGpu, std::string* outErr = nullptr);
    void Dispatch(ID3D12GraphicsCommandList* cmd, const RawMeshGpuAsset& asset, const RawMeshFrameCB& cb);

private:
    bool                                    m_ready = false;
    Microsoft::WRL::ComPtr<ID3D12Device2>   m_dev2;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_cbBuf;
    void*                                   m_cbMapped = nullptr;
};

} // namespace raw::bridge
