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

private:
    StreamlineHost() = default;
    StreamlineCaps m_caps{};
    bool m_tried = false;
};

} // namespace raw::bridge
