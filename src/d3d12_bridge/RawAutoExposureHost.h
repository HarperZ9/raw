#pragma once
// RawAutoExposureHost.h: Native DX12 GPU Compute Host for 256-Bin Center-Weighted Log-Luminance
// Histogram & Percentile-Trimmed Asymmetric Human Eye-Adaptation Auto-Exposure (SM 6.6 / 6.10).
//
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

// Exact GPU readback/SRV layout matching ExposureState in RawAutoExposureSim.hlsl (32 bytes).
struct AutoExposureGpuState {
    float    adaptedLum      = 0.18f;
    float    adaptedExposure = 1.0f;
    float    targetLum       = 0.18f;
    float    targetEv100     = 0.0f;
    uint32_t totalWeight     = 0u;
    uint32_t validPixels     = 0u;
    uint32_t trimmedWeight   = 0u;
    uint32_t frameCount      = 0u;
};
static_assert(sizeof(AutoExposureGpuState) == 32u, "AutoExposureGpuState must be 32 bytes");

struct AutoExposureDesc {
    D3D12_SHADER_BYTECODE customHistShader  = {}; // optional override; uses raw_exposure_hist_cs.h when empty
    D3D12_SHADER_BYTECODE customAdaptShader = {}; // optional override; uses raw_exposure_adapt_cs.h when empty
};

struct AutoExposureParams {
    float dtSec          = 1.0f / 60.0f;
    float minLogLum      = -10.0f; // 2^-10 = 0.000976
    float maxLogLum      =  14.0f; // 2^+14 = 16384.0
    float keyValue       =  0.18f; // Photographic 18% middle-gray target
    float tauBrightSec   =  0.35f; // Fast photopic contraction into bright sunlight
    float tauDarkSec     =  1.40f; // Slower scotopic dilation into dark dungeons
    float lowPercentile  =  0.15f; // Trim bottom 15% dark pixels
    float highPercentile =  0.95f; // Trim top 5% fireflies/specular outliers
    float minExposure    =  0.005f;
    float maxExposure    =  32.0f;
    float minLumClamp    =  1.0e-4f;
    bool  resetAdapt     =  false; // Snap adaptedLum directly to targetLum on this Execute
};

class RawAutoExposureHost {
public:
    static constexpr uint32_t kCbRingCount = 4u;

    RawAutoExposureHost() = default;
    ~RawAutoExposureHost();

    RawAutoExposureHost(const RawAutoExposureHost&) = delete;
    RawAutoExposureHost& operator=(const RawAutoExposureHost&) = delete;

    bool Init(ID3D12Device* dev, const AutoExposureDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Runs cs_exposure_histogram + cs_exposure_adapt on hdrColorTex.
    // Cleanly restores hdrColorTex back to srcState and leaves ExposureStateBuf in SRV state.
    bool Execute(ID3D12GraphicsCommandList* cmd,
                 ID3D12Resource* hdrColorTex,
                 uint32_t width,
                 uint32_t height,
                 const AutoExposureParams& params,
                 D3D12_RESOURCE_STATES srcState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Resets adaptation state so the next Execute snaps immediately to target luminance.
    // If cmd is provided, also zeroes ExposureStateBuf on the GPU queue immediately.
    void ResetAdaptation(ID3D12GraphicsCommandList* cmd = nullptr);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetExposureBufferSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetExposureBufferSRVCpu() const;
    ID3D12Resource*             GetExposureBufferResource() const { return m_exposureBuf.Get(); }
    D3D12_RESOURCE_STATES       ExposureBufferResourceState() const { return m_exposureState; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastHistMs() const { return m_lastHistMs; }
    float GetLastAdaptMs() const { return m_lastAdaptMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool          m_ready        = false;
    bool          m_needsZero    = true;
    bool          m_pendingReset = true;
    ID3D12Device* m_dev          = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_histPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_adaptPso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_histBuf;
    D3D12_RESOURCE_STATES                  m_histState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_exposureBuf;
    D3D12_RESOURCE_STATES                  m_exposureState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_zeroUploadBuf;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz       = 0u;
    uint32_t                                m_frameIdx       = 0u;
    float                                   m_lastHistMs     = 0.0f;
    float                                   m_lastAdaptMs    = 0.0f;
    bool                                    m_hasValidTiming = false;
};

} // namespace raw::bridge
