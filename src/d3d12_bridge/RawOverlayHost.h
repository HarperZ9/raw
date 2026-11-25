#pragma once
// RawOverlayHost.h: Native DX12 Zero-Dependency <0.02ms GPU Compute Telemetry & Sparkline HUD Overlay.
// Original clean-room implementation, MIT License.
//
// Draws a crisp dark-slate translucent telemetry card with embedded 5x7 ASCII glyphs (up to 8 lines x 64 chars)
// and a 64-sample color-coded GPU frame-time sparkline directly onto a target texture via RawOverlayPass.hlsl.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

enum OverlayColorTag : uint8_t {
    kOverlayColorDefault = 0u,
    kOverlayColorCyan    = 1u,
    kOverlayColorGreen   = 2u,
    kOverlayColorAmber   = 3u,
    kOverlayColorCrimson = 4u,
    kOverlayColorMuted   = 5u
};

struct OverlayHudData {
    static constexpr uint32_t kMaxLines      = 8u;
    static constexpr uint32_t kMaxCols       = 64u;
    static constexpr uint32_t kMaxSparkCount = 64u;

    bool     enabled                   = true;
    uint32_t originX                   = 16u;
    uint32_t originY                   = 16u;
    uint32_t glyphScale                = 1u;     // 1 or 2
    float    opacity                   = 0.86f;  // 0..1 glass panel opacity
    bool     srgbOutput                = false;  // true if target stores sRGB-encoded values in UNORM

    uint32_t lineCount                 = 0u;     // 0..8
    char     lines[kMaxLines][kMaxCols + 1u] = {};
    uint8_t  lineColorTag[kMaxLines]   = {};

    uint32_t sparkCount                = 0u;     // 0..64
    float    sparkSamplesMs[kMaxSparkCount] = {};
    float    greenMaxMs                = 8.333f; // < 8.33ms = 120+ FPS green
    float    amberMaxMs                = 16.667f;// 8.33..16.67ms = 60..120 FPS amber; >16.67ms crimson
    float    graphMaxMs                = 25.0f;  // vertical full-scale ceiling in ms
};

struct OverlayTelemetrySnapshot {
    float       fps             = 0.0f;
    float       frameMs         = 0.0f;
    float       fireMs          = 0.0f;
    float       waterMs         = 0.0f;
    float       giMs            = 0.0f;
    float       xpbdMs          = 0.0f;
    float       overlayMs       = 0.0f;
    const char* seasonName      = "AUTUMN";
    const char* weatherName     = "CLEAR";
    float       tempC           = 8.0f;
    float       windMps         = 4.0f;
    float       snowPackM       = 0.0f;
    int32_t     cellX           = 4;
    int32_t     cellY           = -12;
    const char* landmarkName    = "RIVERWOOD";
    uint32_t    activeCells     = 9u;
    uint32_t    activeMeshlets  = 0u;
    float       lruHitRatePct   = 0.0f;
    bool        giEnabled       = true;
    bool        fluidEnabled    = true;
    bool        xpbdEnabled     = true;
    const float* historyFrameMs = nullptr;
    uint32_t     historyCount   = 0u;
};

struct OverlayPanelRect {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
};

// Pure CPU formatter: populates crisp uppercase telemetry lines and up to 64 sparkline samples.
void FormatEngineTelemetryLines(const OverlayTelemetrySnapshot& snap, OverlayHudData& outHud);

// Pure CPU layout helper returning the exact card bounding box dispatched on the GPU.
OverlayPanelRect ComputeOverlayPanelRect(const OverlayHudData& hud, uint32_t targetWidth, uint32_t targetHeight);

class RawOverlayHost {
public:
    static constexpr uint32_t kRingCount = 4u;

    RawOverlayHost() = default;
    ~RawOverlayHost();

    RawOverlayHost(const RawOverlayHost&) = delete;
    RawOverlayHost& operator=(const RawOverlayHost&) = delete;

    bool Init(ID3D12Device* dev, std::string* outErr = nullptr, D3D12_SHADER_BYTECODE customShader = {});
    bool Ready() const { return m_ready; }

    // Records the single cs_overlay_draw dispatch directly onto targetTex and restores currentState.
    // When hud.enabled == false, emits ZERO GPU commands and leaves targetTex bit-exact untouched.
    void RecordOverlay(ID3D12GraphicsCommandList* cmd,
                       ID3D12Resource* targetTex,
                       D3D12_RESOURCE_STATES currentState,
                       uint32_t width,
                       uint32_t height,
                       const OverlayHudData& hud);

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastOverlayMs() const { return m_lastOverlayMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                         m_ready = false;
    ID3D12Device*                                m_dev = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_pso;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbBuf;
    uint8_t*                                     m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>      m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_tsReadback;
    uint64_t                                     m_tsFreqHz = 0u;
    uint32_t                                     m_frameIdx = 0u;
    float                                        m_lastOverlayMs = 0.0f;
    bool                                         m_hasValidTiming = false;
};

} // namespace raw::bridge
