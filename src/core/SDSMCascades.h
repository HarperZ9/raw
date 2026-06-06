#pragma once
//=============================================================================
//  SDSMCascades — Sample Distribution Shadow Maps
//
//  Analyzes the scene depth distribution to compute optimal shadow cascade
//  split distances.  Instead of using fixed logarithmic or uniform splits,
//  SDSM places cascade boundaries where the actual geometry is, maximizing
//  shadow map resolution where it matters most.
//
//  Pipeline:
//    1. Depth Histogram CS: 256-bin histogram of linearized depth (reuses
//       LuminanceHistogram's pattern).
//    2. CPU Readback: Read histogram with 1-frame delay.
//    3. Split Computation: CDF-based partitioning — each cascade covers
//       approximately the same number of depth samples.
//    4. Application: Write computed splits to Skyrim's shadow INI settings
//       via RE::INISettingCollection.
//
//  Game settings modified:
//    fShadowDistance          — total shadow distance (only if auto-distance)
//    iShadowMapResolutionPrimary — not modified (resolution stays as-is)
//
//  Output: ComputedSplits struct with 4 cascade near/far distances.
//  Also exposes shadow analysis data in SRVInjector domain for debug overlay.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <array>

namespace SB
{

struct CascadeSplits
{
    float splits[4];     // Near distance for each cascade (cascade 0 = camera near)
    float maxDistance;    // Far distance of the last cascade
    bool  valid = false;
};

class SDSMCascades
{
public:
    static SDSMCascades& Get()
    {
        static SDSMCascades inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Settings
    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }
    float GetMinShadowDistance() const { return m_minDistance; }
    void  SetMinShadowDistance(float d) { m_minDistance = d; }
    float GetMaxShadowDistance() const { return m_maxDistance; }
    void  SetMaxShadowDistance(float d) { m_maxDistance = d; }

    // Per-frame update: dispatch histogram, readback, compute splits
    void Update(ID3D11DeviceContext* ctx);

    // Get computed cascade splits
    const CascadeSplits& GetSplits() const { return m_splits; }

    // Depth distribution info (for debug overlay)
    float GetMedianDepth() const { return m_medianDepth; }
    float GetP90Depth()    const { return m_p90Depth; }
    float GetP99Depth()    const { return m_p99Depth; }

private:
    SDSMCascades() = default;

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    // Compute cascade splits from histogram data
    void ComputeSplitsFromHistogram(const uint32_t* histogram, uint32_t binCount);

    // Apply splits to game shadow settings
    void ApplySplitsToGame();

    bool m_initialized = false;
    bool m_enabled     = true;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // ── Depth histogram compute ───────────────────────────────────────
    static constexpr uint32_t kHistogramBins = 256;
    ID3D11ComputeShader*       m_histogramCS   = nullptr;
    ID3D11Buffer*              m_histogramBuf   = nullptr;  // RWStructuredBuffer<uint> [256]
    ID3D11UnorderedAccessView* m_histogramUAV   = nullptr;
    ID3D11ShaderResourceView*  m_histogramSRV   = nullptr;
    ID3D11Buffer*              m_histogramCB    = nullptr;

    // Readback (double-buffered)
    ID3D11Buffer* m_histogramStaging[2] = {};
    int           m_readbackIdx = 0;
    bool          m_readbackReady = false;

    // Depth SRV (per-frame)
    ID3D11ShaderResourceView* m_depthSRV = nullptr;

    // ── Computed results ──────────────────────────────────────────────
    CascadeSplits m_splits;
    float m_medianDepth = 0.0f;
    float m_p90Depth    = 0.0f;
    float m_p99Depth    = 0.0f;

    // ── Settings ──────────────────────────────────────────────────────
    float m_minDistance = 500.0f;     // Minimum total shadow distance
    float m_maxDistance = 8000.0f;    // Maximum total shadow distance

    uint32_t m_frameIndex = 0;
};

} // namespace SB
