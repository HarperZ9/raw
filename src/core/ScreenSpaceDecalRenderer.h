#pragma once
//=============================================================================
//  ScreenSpaceDecalRenderer — Screen-space deferred decal projection
//
//  Projects decal volumes (oriented bounding boxes) into screen space and
//  blends their color/pattern onto the backbuffer.  Supports up to 64
//  simultaneous decals with per-decal lifetime management.
//
//  Single compute shader pass:
//    1. For each pixel, reconstruct world position from depth
//    2. Transform world position into each decal's local space
//    3. If inside decal box [-1,1]^3, sample decal pattern (procedural noise)
//    4. Blend onto backbuffer using alpha + normal-aware attenuation
//
//  Output: Modifies backbuffer in-place via UAV (no SRV slot).
//  Registered as PostGeometry pipeline pass, priority 22 — runs after
//  ContactShadows(16), Skylighting(17), GrassLighting(18).
//
//  VRAM budget: ~4 MB at 1920x1080 (CB + structured buffer + backbuffer copy)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <mutex>
#include "RenderPipeline.h"

namespace SB
{

class ScreenSpaceDecalRenderer
{
public:
    static ScreenSpaceDecalRenderer& Get()
    {
        static ScreenSpaceDecalRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }

    // ── Settings ────────────────────────────────────────────────────────

    float GetGlobalOpacity() const { return m_globalOpacity; }
    void  SetGlobalOpacity(float v) { m_globalOpacity = v; }
    float GetNormalThreshold() const { return m_normalThreshold; }
    void  SetNormalThreshold(float v) { m_normalThreshold = v; }
    int   GetMaxDecals() const { return m_maxDecals; }

    // ── Decal management ────────────────────────────────────────────────

    struct DecalDef
    {
        float    position[3];     // World-space center
        float    rotation[4];     // Quaternion (x, y, z, w)
        float    size[3];         // Half-extents
        float    color[4];        // RGBA
        float    normalFade;      // How much surface normal affects blending (0=ignore, 1=strict)
        float    opacity;         // Overall opacity
        float    lifetime;        // Seconds remaining (-1 = permanent)
        uint32_t pattern;         // 0=solid, 1=circle, 2=splatter, 3=impact
    };

    /// Add a decal.  Returns a unique ID (never 0).  Thread-safe.
    uint32_t AddDecal(const DecalDef& decal);

    /// Remove a decal by ID.  Thread-safe.
    void RemoveDecal(uint32_t id);

    /// Remove all decals.  Thread-safe.
    void ClearAllDecals();

    /// Number of currently active decals.
    uint32_t GetActiveDecalCount() const;

private:
    ScreenSpaceDecalRenderer() = default;

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    // Pipeline pass callback
    void ExecutePass(PassContext& ctx);

    bool m_initialized = false;
    bool m_enabled     = false;   // opt-in: writes backbuffer via UAV

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Screen dimensions
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // Settings
    float m_globalOpacity   = 1.0f;   // Global opacity multiplier (0-1)
    float m_normalThreshold = 0.5f;   // Min normal alignment for decal application (0-1)
    int   m_maxDecals       = 64;

    // ── CPU-side decal storage ────────────────────────────────────────
    struct DecalEntry
    {
        DecalDef  def;
        uint32_t  id  = 0;
        float     age = 0.0f;   // Seconds since creation
    };

    std::vector<DecalEntry> m_decals;
    uint32_t                m_nextDecalID = 1;
    mutable std::mutex      m_decalMutex;

    // ── GPU resources ─────────────────────────────────────────────────

    // Compute shader
    ID3D11ComputeShader*       m_decalCS        = nullptr;
    ID3D11Buffer*              m_constantsCB    = nullptr;

    // Structured buffer for GPU decal data (64 elements, dynamic)
    ID3D11Buffer*              m_decalBuffer    = nullptr;
    ID3D11ShaderResourceView*  m_decalBufferSRV = nullptr;

    // Backbuffer copy texture (for reading original color)
    ID3D11Texture2D*           m_backbufferCopy    = nullptr;
    ID3D11ShaderResourceView*  m_backbufferCopySRV = nullptr;

    // UAV for in-place backbuffer write
    ID3D11UnorderedAccessView* m_backbufferUAV  = nullptr;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;
    uint32_t   m_frameIndex     = 0;
};

} // namespace SB
