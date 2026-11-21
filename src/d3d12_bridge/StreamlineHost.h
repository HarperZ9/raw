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
    // Live neural mode: 0 = Off, 1 = DLAA Transformer (Preset K), 2 = DLSS Ray Reconstruction (Preset F).
    int GetMode() const { return m_mode; }
    void SetMode(int m) { m_mode = (m < 0 ? 0 : (m > 2 ? 2 : m)); }
    bool GetReflex() const { return m_reflexOn; }
    void SetReflex(bool on) { m_reflexOn = on; }

    // Per-frame NVIDIA Reflex Low-Latency sleep/pacing (no-op if Reflex is unsupported).
    void ReflexBeginFrame(uint32_t frameIdx);

    // Live per-frame NVIDIA DLAA Transformer (Preset K) or DLSS Ray Reconstruction (Preset F) on RAW's DX12 HDR buffer.
    // All resources enter and exit in D3D12_RESOURCE_STATE_COMMON.
    // Returns true if Streamline evaluated into colorOut (caller should read colorOut instead of colorIn).
    bool Evaluate(ID3D12CommandQueue* queue, ID3D12Resource* colorIn, ID3D12Resource* colorOut,
                  ID3D12Resource* depth, ID3D12Resource* mvec,
                  ID3D12Resource* albedo, ID3D12Resource* normal, ID3D12Resource* spec,
                  uint32_t width, uint32_t height, uint32_t frameIdx, LogFn log);

private:
    StreamlineHost() = default;
    StreamlineCaps m_caps{};
    int m_mode = 1;
    bool m_reflexOn = true;
    bool m_tried = false;
};

} // namespace raw::bridge
