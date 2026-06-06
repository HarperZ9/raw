#pragma once
//=============================================================================
//  TAAManager — Compute-based Temporal Anti-Aliasing resolve
//
//  Provides a persistent full-res color history buffer that survives the
//  entire render pipeline.  Each frame a compute shader reads the current
//  pipeline RT + the previous frame's history, reprojects via depth +
//  camera matrices, neighbourhood-clips the history, blends, and writes
//  the resolved result back to both the pipeline RT and the history buffer
//  (ping-pong).
//
//  The history texture is exposed as a PS SRV at t22 so shader passes can
//  sample it for their own temporal effects (denoise, ghosting, etc.).
//
//  Execution point:
//    HookedPresent -> DoOverlayWork, BEFORE SRVInjector::ClearAll().
//    At this point the full render pipeline has finished compositing
//    into the backbuffer.  We resolve against the backbuffer directly.
//=============================================================================

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11Buffer;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct ID3D11SamplerState;
struct IDXGISwapChain;

namespace SB
{

class TAAManager
{
public:
    static TAAManager& Get()
    {
        static TAAManager instance;
        return instance;
    }

    /// Initialize resources.  Call at kDataLoaded after ComputeManager + SRVInjector.
    bool Initialize(ID3D11Device* dev, IDXGISwapChain* swapChain);
    void Shutdown();

    /// Run the TAA resolve pass.
    /// Reads backbuffer + depth + history[back], writes resolved color
    /// to backbuffer and history[front], then swaps ping-pong index.
    /// Call from DoOverlayWork in HookedPresent BEFORE SRVInjector::ClearAll.
    void Resolve(ID3D11DeviceContext* ctx, IDXGISwapChain* swapChain);

    /// Get the history SRV for SRVInjector registration (t22).
    ID3D11ShaderResourceView* GetHistorySRV() const;

    /// Get the trilinear sampler for history reads (s3).
    ID3D11SamplerState* GetSampler() const { return m_sampler; }

    static constexpr uint32_t kSRVSlot     = 22;  // t22 — Temporal history
    static constexpr uint32_t kSamplerSlot = 3;   // s3  — History sampler

    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled()     const { return m_enabled; }
    void SetEnabled(bool v)    { m_enabled = v; }

    /// Set jitter offset (NDC) for the current frame.
    /// Call from DoFrameUpdate after RenderTracker fills SB_Render_Jitter.
    void SetJitter(float x, float y) { m_jitterX = x; m_jitterY = y; }

    uint32_t GetWidth()  const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

private:
    TAAManager() = default;

    bool CompileShader(ID3D11Device* dev);
    bool CreateResources(ID3D11Device* dev, uint32_t w, uint32_t h);
    bool AcquireDepthSRV(ID3D11DeviceContext* ctx);
    void ReleaseResources();

    bool m_initialized = false;
    bool m_enabled     = false;  // Opt-in: overwrites backbuffer
    bool m_firstFrame  = true;   // Skip resolve on first frame (no history yet)

    // Compute shader
    ID3D11ComputeShader* m_resolveCS = nullptr;

    // Ping-pong history textures (R16G16B16A16_FLOAT, full-res)
    ID3D11Texture2D*            m_historyTex[2]  = {};
    ID3D11ShaderResourceView*   m_historySRV[2]  = {};
    ID3D11UnorderedAccessView*  m_historyUAV[2]  = {};
    int m_writeIdx = 0;  // Current write target; read = 1 - writeIdx

    // Constant buffer (per-dispatch parameters)
    ID3D11Buffer* m_paramsCB = nullptr;

    // Sampler for history reads (trilinear clamp)
    ID3D11SamplerState* m_sampler = nullptr;

    // Temporary depth SRV (reacquired each frame)
    ID3D11ShaderResourceView* m_depthSRV = nullptr;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    uint32_t m_frameIndex = 0;

    // Jitter offset (NDC), set by DoFrameUpdate each frame
    float m_jitterX = 0.0f;
    float m_jitterY = 0.0f;
};

} // namespace SB
