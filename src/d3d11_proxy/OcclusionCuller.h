#pragma once
//=============================================================================
//  OcclusionCuller — CPU-side GPU occlusion culling via depth readback
//
//  Reads back the depth buffer each frame (1-frame delay, double-buffered),
//  builds a CPU Hi-Z pyramid, and tests draw calls against it.
//
//  Integration points:
//    - WrappedContext::DrawIndexed: Query ShouldCull() before forwarding
//    - WrappedContext::VSSetConstantBuffers: Track bound VS CBs
//    - WrappedSwapChain::Present: Call OnPresent() for depth readback
//
//  Requires: RenderPhaseDetector (skip culling during DepthPrepass/ShadowMap)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <array>

namespace SB::Proxy
{

class OcclusionCuller
{
public:
    static OcclusionCuller& Get()
    {
        static OcclusionCuller inst;
        return inst;
    }

    // Initialize with the real device (called from WrappedDevice creation)
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool v) { m_enabled = v; }

    // ── Per-frame operations ──────────────────────────────────────────

    // Called from Present: kick off depth readback for next frame
    void OnPresent(ID3D11DeviceContext* ctx);

    // ── Draw call culling ─────────────────────────────────────────────

    // Called from WrappedContext::VSSetConstantBuffers to track bound VS CBs
    void OnVSSetConstantBuffers(UINT startSlot, UINT numBuffers,
                                 ID3D11Buffer* const* ppCBs);

    // Test whether a DrawIndexed call should be culled.
    // Returns true if the object is fully occluded and the draw can be skipped.
    // Only tests during GeometryMain/AlphaBlend phases; never culls
    // DepthPrepass, ShadowMap, or PostProcess draws.
    bool ShouldCull(uint32_t indexCount);

    // Called on ClearState to reset tracked VS CB bindings
    void OnClearState();

    // Receive exact NiAVObject::worldBound (world center XYZ + radius) from raw_engine.dll BSLightingShader::SetupGeometry
    void SetExactDrawBound(float cx, float cy, float cz, float radius)
    {
        m_exactBound[0] = cx; m_exactBound[1] = cy; m_exactBound[2] = cz; m_exactBound[3] = radius;
        m_hasExactBound = true;
    }

    // Receive GPU-reduced 8x8 conservative MAX-depth mip0 from DX12 HiZDownsample.hlsl (240x135 R32F, 2 frames retired).
    void UpdateHiZFromGpu(const float* mip0, uint32_t w, uint32_t h, const float w2c[16], const float cam[3]);

    // ── Statistics ────────────────────────────────────────────────────

    uint32_t GetDrawsTested()   const { return m_drawsTested; }
    uint32_t GetDrawsCulled()   const { return m_drawsCulled; }
    uint32_t GetDrawsPassed()   const { return m_drawsTested - m_drawsCulled; }
    uint32_t GetFrustumCulled() const { return m_frustumCulled; }
    uint32_t GetSubPxCulled()   const { return m_subPxCulled; }
    uint32_t GetHiZCulled()     const { return m_hiZCulled; }
    uint64_t GetTrisTested()    const { return m_trisTested; }
    uint64_t GetTrisCulled()    const { return m_trisCulled; }
    uint32_t GetHiZWidth()      const { return m_hizW; }
    uint32_t GetHiZHeight()     const { return m_hizH; }
    uint32_t GetHiZMipCount()   const { return m_hizMips; }

private:
    OcclusionCuller() = default;

    // Build CPU Hi-Z pyramid from mapped depth data (supports R24G8_TYPELESS and R32_FLOAT)
    void BuildCPUHiZ(const uint8_t* rawData, uint32_t width, uint32_t height, uint32_t rowPitchBytes, DXGI_FORMAT fmt);

    // Test a screen-space bounding rect against the Standard-Z (0=near, 1=far) CPU Hi-Z
    // minX/minY/maxX/maxY in [0,1] UV space, nearDepth in [0,1]
    bool TestRect(float minX, float minY, float maxX, float maxY, float nearDepth) const;

    // Estimate bounding sphere radius from index count and world-matrix scale
    float EstimateBoundingRadius(uint32_t indexCount, float scale) const;

    bool m_initialized = false;
    bool m_enabled     = true;   // DXBC shader-class-filtered NiAVObject::worldBound culling (structurally excludes Landscape & DistantLod)
    bool m_hizReady    = false;
    float m_exactBound[4] = {};
    bool  m_hasExactBound = false;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // ── Depth readback (triple-buffered for zero-stall DX12 frames-in-flight) ──
    static constexpr int kRingSlots = 3;
    ID3D11Texture2D* m_depthStaging[kRingSlots] = {};
    DXGI_FORMAT      m_stagingFmt = DXGI_FORMAT_UNKNOWN;
    float            m_stageW2C[kRingSlots][4][4] = {};
    float            m_stageCam[kRingSlots][3] = {};
    bool             m_stageHasCam[kRingSlots] = {};
    float            m_hizW2C[4][4] = {};
    float            m_hizCam[3] = {};
    bool             m_hizHasCam = false;
    int              m_readbackIdx = 0;  // 3-slot ring: write [idx], read [(idx + 1) % 3] (2 frames old, GPU-retired)
    uint32_t         m_depthW = 0;
    uint32_t         m_depthH = 0;

    // ── CPU Hi-Z pyramid ──────────────────────────────────────────────
    static constexpr uint32_t kMaxHiZMips = 12;
    struct HiZMip {
        std::vector<float> data;
        uint32_t width  = 0;
        uint32_t height = 0;
    };
    HiZMip   m_hizMipChain[kMaxHiZMips];
    uint32_t m_hizW    = 0;
    uint32_t m_hizH    = 0;
    uint32_t m_hizMips = 0;

    // ── VS constant buffer tracking ───────────────────────────────────
    static constexpr uint32_t kMaxVSCBSlots = 8;
    ID3D11Buffer* m_boundVSCBs[kMaxVSCBSlots] = {};

    // ── Per-frame stats ───────────────────────────────────────────────
    uint32_t m_drawsTested   = 0;
    uint32_t m_drawsCulled   = 0;
    uint32_t m_frustumCulled = 0;
    uint32_t m_subPxCulled   = 0;
    uint32_t m_hiZCulled     = 0;
    uint64_t m_trisTested    = 0;
    uint64_t m_trisCulled    = 0;
    uint64_t m_frameCount    = 0;

    // ── Screen dimensions (for projection) ────────────────────────────
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;
};

} // namespace SB::Proxy
