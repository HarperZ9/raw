#pragma once
// RawTsrUpscaleHost.h: Native DX12 GPU Compute Host for RAW-TSR (Temporal Super-Resolution
// with Halton (2,3) Sub-Pixel Un-Jitter, 5-Tap Catmull-Rom History Reprojection, YCoCg
// Variance-Clipping Anti-Ghosting, Reactive Mask Pass-Through & RCAS Sharpening, SM 6.6 / 6.10).
//
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

// Deterministic 16-phase Halton (2, 3) sub-pixel jitter in low-res pixel offsets [-0.5, +0.5].
inline void ComputeHaltonJitter(uint32_t frameIdx, float& outJitterX, float& outJitterY)
{
    auto halton = [](uint32_t idx, uint32_t base) -> float {
        float f = 1.0f;
        float r = 0.0f;
        while (idx > 0u) {
            f /= static_cast<float>(base);
            r += f * static_cast<float>(idx % base);
            idx /= base;
        }
        return r;
    };
    const uint32_t phase = (frameIdx % 16u) + 1u;
    outJitterX = halton(phase, 2u) - 0.5f;
    outJitterY = halton(phase, 3u) - 0.5f;
}

struct TsrUpscaleDesc {
    uint32_t              initialInputWidth   = 640u;
    uint32_t              initialInputHeight  = 360u;
    uint32_t              initialOutputWidth  = 1280u;
    uint32_t              initialOutputHeight = 720u;
    D3D12_SHADER_BYTECODE customResolveShader = {}; // optional override; uses raw_tsr_resolve_cs.h
    D3D12_SHADER_BYTECODE customRcasShader    = {}; // optional override; uses raw_tsr_rcas_cs.h
};

struct TsrUpscaleParams {
    float jitterX       = 0.0f;  // Current frame Halton (2,3) low-res pixel offset [-0.5, +0.5]
    float jitterY       = 0.0f;
    float nearZ         = 0.5f;  // Camera near plane (Standard-Z 0=near, 1=far)
    float farZ          = 500.0f;
    float historyWeight = 0.88f; // Temporal accumulation weight [0..0.96]
    float varianceGamma = 1.15f; // YCoCg variance-clipping anti-ghosting box sigma multiplier
    float rcasSharpness = 0.65f; // RCAS contrast-adaptive sharpening strength [0..1]
    bool  resetHistory  = false; // Discard prior history (camera cut / teleport / resize)
};

class RawTsrUpscaleHost {
public:
    static constexpr uint32_t kCbRingCount = 4u;

    RawTsrUpscaleHost() = default;
    ~RawTsrUpscaleHost();

    RawTsrUpscaleHost(const RawTsrUpscaleHost&) = delete;
    RawTsrUpscaleHost& operator=(const RawTsrUpscaleHost&) = delete;

    bool Init(ID3D12Device* dev, const TsrUpscaleDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Runs cs_tsr_resolve -> cs_tsr_rcas_sharpen upscaling (inW x inH) -> (outW x outH).
    // velocityTex and reactiveMask may be nullptr (gracefully binds null SRVs).
    // Auto-reallocates internal ping-pong history & output on resize (auto-resetting history).
    // Cleanly restores all input resource states; leaves OutputResource in SRV state.
    bool Execute(ID3D12GraphicsCommandList* cmd,
                 ID3D12Resource* inputColor,
                 ID3D12Resource* inputDepth,
                 ID3D12Resource* velocityTex,
                 ID3D12Resource* reactiveMask,
                 uint32_t inWidth,
                 uint32_t inHeight,
                 uint32_t outWidth,
                 uint32_t outHeight,
                 const TsrUpscaleParams& params,
                 D3D12_RESOURCE_STATES srcColorState    = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATES srcDepthState    = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATES srcVelState      = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATES srcReactiveState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    void ResetHistory() { m_pendingReset = true; }

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetOutputSRVCpu() const;
    ID3D12Resource*             GetOutputResource() const { return m_sharpenedOut.Get(); }
    D3D12_RESOURCE_STATES       OutputResourceState() const { return m_sharpenedState; }

    uint32_t InputWidth() const { return m_inWidth; }
    uint32_t InputHeight() const { return m_inHeight; }
    uint32_t OutputWidth() const { return m_outWidth; }
    uint32_t OutputHeight() const { return m_outHeight; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastResolveMs() const { return m_lastResolveMs; }
    float GetLastRcasMs() const { return m_lastRcasMs; }
    float GetLastTotalMs() const { return m_lastResolveMs + m_lastRcasMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    bool EnsureOutputs(uint32_t outWidth, uint32_t outHeight);
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool          m_ready        = false;
    bool          m_pendingReset = true;
    ID3D12Device* m_dev          = nullptr;
    uint32_t      m_inWidth      = 0u;
    uint32_t      m_inHeight     = 0u;
    uint32_t      m_outWidth     = 0u;
    uint32_t      m_outHeight    = 0u;
    uint32_t      m_histIndex    = 0u; // ping-pong index of current write target

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_resolvePso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_rcasPso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_history[2];
    D3D12_RESOURCE_STATES                  m_historyState[2] = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_sharpenedOut;
    D3D12_RESOURCE_STATES                  m_sharpenedState  = D3D12_RESOURCE_STATE_COMMON;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz       = 0u;
    uint32_t                                m_frameIdx       = 0u;
    float                                   m_lastResolveMs  = 0.0f;
    float                                   m_lastRcasMs     = 0.0f;
    bool                                    m_hasValidTiming = false;
};

} // namespace raw::bridge
