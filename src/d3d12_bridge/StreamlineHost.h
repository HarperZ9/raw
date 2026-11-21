#pragma once
// StreamlineHost: runtime-loaded NVIDIA Streamline SDK (v2.14+) DX12 host for DLSS / DLAA (Transformer),
// DLSS Ray Reconstruction (sl.dlss_d), NVIDIA Reflex Low Latency, and DeepDVC.
// Loads sl.interposer.dll dynamically via LoadLibraryW (zero hard import; graceful no-op on non-NVIDIA GPUs).
// Original work, MIT.
#include <d3d12.h>
#include <cstdint>

namespace raw::bridge
{

struct StreamlineCaps
{
    bool available = false;
    bool dlss = false;
    bool dlssRR = false;
    bool reflex = false;
    bool deepDvc = false;
};

class StreamlineHost
{
public:
    using LogFn = void(*)(const char*, ...);

    static StreamlineHost& Get();

    bool Initialize(ID3D12Device* dev12, LUID adapterLuid, LogFn log);
    void Shutdown();
    StreamlineCaps Caps() const { return m_caps; }

    // Per-frame NVIDIA Reflex Low-Latency sleep/pacing (no-op if Reflex is unsupported).
    void ReflexBeginFrame(uint32_t frameIdx);

    // Live per-frame NVIDIA DLAA Transformer (sl::DLSSMode::eDLAA, ePresetK) on RAW's native DX12 HDR buffer.
    // All 4 input/output resources enter and exit in D3D12_RESOURCE_STATE_COMMON.
    // Returns true if DLAA evaluated into colorOut (caller should read colorOut instead of colorIn).
    bool EvaluateDLAA(ID3D12CommandQueue* queue, ID3D12Resource* colorIn, ID3D12Resource* colorOut,
                      ID3D12Resource* depth, ID3D12Resource* mvec, uint32_t width, uint32_t height,
                      uint32_t frameIdx, LogFn log);

private:
    StreamlineHost() = default;
    StreamlineCaps m_caps{};
    bool m_tried = false;
};

} // namespace raw::bridge
