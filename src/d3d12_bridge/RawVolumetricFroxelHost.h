#pragma once
// RawVolumetricFroxelHost.h: Native DX12 GPU Compute Host for RAW's True 3D 160x90x64
// Exponential-Z Volumetric Froxel Fog, Altitude-Stratified Valley Mist, Henyey-Greenstein Mie
// Scattering, Local Point-Light Volumetric Halos & Temporal Reprojection Solver (SM 6.6 / 6.10).
//
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

// 32-byte local point/torch/campfire light matching RawVolumetricFroxelSim.hlsl byte-for-byte.
struct FroxelPointLight {
    float posW[3]   = {0.0f, 0.0f, 0.0f};
    float radius    = 10.0f;
    float color[3]  = {1.0f, 0.65f, 0.25f};
    float intensity = 0.0f;
};
static_assert(sizeof(FroxelPointLight) == 32u, "FroxelPointLight must be 32 bytes");

struct VolumetricFroxelDesc {
    uint32_t              gridWidth            = 160u;
    uint32_t              gridHeight           = 90u;
    uint32_t              gridDepth            = 64u;
    D3D12_SHADER_BYTECODE customInjectShader   = {}; // optional override; uses raw_froxel_inject_cs.h when empty
    D3D12_SHADER_BYTECODE customIntegrateShader = {}; // optional override; uses raw_froxel_integrate_cs.h when empty
};

struct VolumetricFroxelParams {
    // Row-major matrices for row-vector transforms: clip = mul(float4(worldPos, 1), viewProj).
    float            invViewProj[16]     = {};
    float            prevViewProj[16]    = {};
    float            cameraPosW[3]       = {0.0f, 0.0f, 0.0f};
    float            cameraForwardW[3]   = {0.0f, 1.0f, 0.0f};
    float            prevCameraPosW[3]   = {0.0f, 0.0f, 0.0f};
    float            prevCameraForwardW[3] = {0.0f, 1.0f, 0.0f};
    float            nearZ               = 1.0f;
    float            farZ                = 300.0f;
    float            simTimeSec          = 0.0f;
    float            historyWeight       = 0.85f; // set 0.0 to disable temporal blend / reset history
    bool             enableTemporal      = true;
    bool             enableHaltonJitter  = true;

    // Altitude-stratified mountain/valley mist & weather
    float            baseFogDensity      = 0.015f;
    float            valleyFloorZ        = 0.0f;
    float            fogScaleHeight      = 40.0f;
    float            scatterAlbedo       = 0.90f;
    float            rainIntensity       = 0.0f;
    float            snowIntensity       = 0.0f;
    float            noiseAmp            = 0.25f;
    float            noiseScale          = 0.04f;
    float            windVelW[3]         = {2.0f, 0.5f, 0.0f};

    // Directional Sun/Moon Mie scattering + ambient sky fill
    float            mieAnisotropyG      = 0.68f;
    float            sunDirW[3]          = {0.0f, 1.0f, 0.25f}; // unit direction toward sun
    float            sunIntensity        = 6.0f;
    float            sunColor[3]         = {1.0f, 0.92f, 0.78f};
    float            ambientIntensity    = 0.35f;
    float            ambientColor[3]     = {0.35f, 0.48f, 0.72f};

    // Up to 16 local point/torch/campfire lights
    uint32_t         lightCount          = 0u;
    FroxelPointLight lights[16]          = {};
};

class RawVolumetricFroxelHost {
public:
    static constexpr uint32_t kCbRingCount = 4u;

    RawVolumetricFroxelHost() = default;
    ~RawVolumetricFroxelHost();

    RawVolumetricFroxelHost(const RawVolumetricFroxelHost&) = delete;
    RawVolumetricFroxelHost& operator=(const RawVolumetricFroxelHost&) = delete;

    bool Init(ID3D12Device* dev, const VolumetricFroxelDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Executes cs_froxel_inject + cs_froxel_integrate with ping-pong temporal reprojection & GPU timestamps.
    // Leaves IntegratedFroxelVolume in NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE state.
    bool Execute(ID3D12GraphicsCommandList* cmd, const VolumetricFroxelParams& params);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetIntegratedVolumeSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetIntegratedVolumeSRVCpu() const;
    ID3D12Resource*             GetIntegratedVolumeResource() const { return m_integratedVol.Get(); }
    D3D12_RESOURCE_STATES       IntegratedVolumeResourceState() const { return m_integratedState; }

    uint32_t GridWidth() const { return m_gridW; }
    uint32_t GridHeight() const { return m_gridH; }
    uint32_t GridDepth() const { return m_gridD; }

    void  ResetHistory() { m_hasHistory = false; }
    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastInjectMs() const { return m_lastInjectMs; }
    float GetLastIntegrateMs() const { return m_lastIntegrateMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool          m_ready = false;
    ID3D12Device* m_dev   = nullptr;
    uint32_t      m_gridW = 160u;
    uint32_t      m_gridH = 90u;
    uint32_t      m_gridD = 64u;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_injectPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_integratePso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_injectVol[2];
    D3D12_RESOURCE_STATES                  m_injectState[2]  = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_integratedVol;
    D3D12_RESOURCE_STATES                  m_integratedState = D3D12_RESOURCE_STATE_COMMON;
    uint32_t                               m_pingIdx         = 0u;
    bool                                   m_hasHistory      = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz        = 0u;
    uint32_t                                m_frameIdx        = 0u;
    float                                   m_lastInjectMs    = 0.0f;
    float                                   m_lastIntegrateMs = 0.0f;
    bool                                    m_hasValidTiming  = false;
};

} // namespace raw::bridge
