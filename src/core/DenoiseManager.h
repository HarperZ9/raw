#pragma once
//=============================================================================
//  DenoiseManager.h — Shared denoising compute shader library
//
//  Provides pre-compiled compute shaders for common denoising patterns used
//  across multiple rendering systems (GTAO, SSR, SSGI, ContactShadows,
//  Skylighting, and future effects).
//
//  NOT a pipeline pass — this is a utility library that other renderers call.
//  Each renderer is responsible for its own SaveCSState/RestoreCSState around
//  batches of denoise dispatches.
//
//  Shader library (5 compute shaders):
//    1. Joint Bilateral Filter CS       — depth+normal edge-stopping (float)
//    2. Joint Bilateral RGBA CS         — same for R16G16B16A16_FLOAT inputs
//    3. A-Trous Wavelet CS              — SVGF-style multi-level wavelet filter
//    4. Temporal Accumulation CS         — motion-aware temporal blend (float)
//    5. Temporal Accumulation RGBA CS   — same for float4 inputs
//
//  Usage:
//    auto& dm = DenoiseManager::Get();
//    dm.Initialize(device, context);
//    // ... from within a renderer's ExecutePass, between Save/RestoreCSState:
//    dm.DispatchBilateral(noisySRV, depthSRV, outputUAV, w, h);
//    dm.DispatchTemporal(current, history, depth, prevDepth, output, w, h);
//=============================================================================

#include <d3d11.h>
#include <cstdint>

namespace SB
{

class DenoiseManager
{
public:
    static DenoiseManager& Get()
    {
        static DenoiseManager inst;
        return inst;
    }

    /// Initialize: compile all 5 shader variants and create shared resources.
    /// Call at kDataLoaded after ComputeManager is initialized.
    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // ── Joint Bilateral Filter (single-channel float) ─────────────────
    //
    // Gaussian spatial kernel x depth edge-stopping x normal edge-stopping.
    // Reads: t0 = noisy input (R8/R16_FLOAT), t1 = depth
    // Writes: u0 = filtered output
    void DispatchBilateral(ID3D11ShaderResourceView*  noisyInput,
                           ID3D11ShaderResourceView*  depthSRV,
                           ID3D11UnorderedAccessView* output,
                           uint32_t width, uint32_t height,
                           float depthSigma  = 0.03f,
                           float normalSigma = 0.5f,
                           int   kernelRadius = 2);

    // ── Joint Bilateral Filter (RGBA float4) ──────────────────────────
    //
    // Same algorithm but for R16G16B16A16_FLOAT inputs (SSR, SSGI).
    void DispatchBilateralRGBA(ID3D11ShaderResourceView*  noisyInput,
                               ID3D11ShaderResourceView*  depthSRV,
                               ID3D11UnorderedAccessView* output,
                               uint32_t width, uint32_t height,
                               float depthSigma  = 0.03f,
                               int   kernelRadius = 2);

    // ── A-Trous Wavelet (single pass at given step size) ──────────────
    //
    // SVGF-style 5x5 B3-spline wavelet with depth + luminance edge-stopping.
    // Call 3 times with stepSize = 1, 2, 4 for a full 3-level filter.
    void DispatchATrous(ID3D11ShaderResourceView*  input,
                        ID3D11ShaderResourceView*  depthSRV,
                        ID3D11UnorderedAccessView* output,
                        uint32_t width, uint32_t height,
                        int   stepSize       = 1,
                        float depthSigma     = 0.03f,
                        float luminanceSigma = 0.1f);

    // ── Full 3-level A-Trous (needs ping-pong textures) ──────────────
    //
    // Dispatches 3 a-trous passes with step sizes 1, 2, 4.
    // Caller provides two scratch textures for ping-pong:
    //   input -> pingUAV (step=1) -> pongUAV (step=2) -> pingUAV (step=4)
    // Final output lands in pingUAV.
    void DispatchATrousFull(ID3D11ShaderResourceView*  input,
                            ID3D11ShaderResourceView*  depthSRV,
                            ID3D11UnorderedAccessView* pingUAV,
                            ID3D11ShaderResourceView*  pingSRV,
                            ID3D11UnorderedAccessView* pongUAV,
                            ID3D11ShaderResourceView*  pongSRV,
                            uint32_t width, uint32_t height);

    // ── Temporal Accumulation (single-channel float) ──────────────────
    //
    // Motion-aware temporal blending with reprojection + disocclusion detection.
    // Reads: t0 = current, t1 = history, t2 = depth, t3 = prev depth
    // Writes: u0 = blended output
    void DispatchTemporal(ID3D11ShaderResourceView*  current,
                          ID3D11ShaderResourceView*  history,
                          ID3D11ShaderResourceView*  depthSRV,
                          ID3D11ShaderResourceView*  prevDepthSRV,
                          ID3D11UnorderedAccessView* output,
                          uint32_t width, uint32_t height,
                          float blendAlpha          = 0.05f,
                          float depthRejectThreshold = 0.01f);

    // ── Temporal Accumulation (RGBA float4) ───────────────────────────
    //
    // Same algorithm but for R16G16B16A16_FLOAT inputs.
    void DispatchTemporalRGBA(ID3D11ShaderResourceView*  current,
                              ID3D11ShaderResourceView*  history,
                              ID3D11ShaderResourceView*  depthSRV,
                              ID3D11ShaderResourceView*  prevDepthSRV,
                              ID3D11UnorderedAccessView* output,
                              uint32_t width, uint32_t height,
                              float blendAlpha          = 0.1f,
                              float depthRejectThreshold = 0.01f);

    // ── Shared samplers (accessible by other systems) ─────────────────
    ID3D11SamplerState* GetPointClampSampler()  const { return m_pointSampler; }
    ID3D11SamplerState* GetLinearClampSampler() const { return m_linearSampler; }

    // Camera matrices — must be set each frame before temporal dispatches.
    // Typically called from the renderer's ExecutePass before DispatchTemporal.
    void SetMatrices(const float* prevViewProj, const float* invViewProj);

    // Near/far clip — must be set each frame for depth linearization.
    void SetClipPlanes(float nearZ, float farZ) { m_nearZ = nearZ; m_farZ = farZ; }

private:
    DenoiseManager() = default;

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    // Helper: update CB via Map/WRITE_DISCARD
    bool UpdateCB(ID3D11Buffer* cb, const void* data, uint32_t size);

    bool m_initialized = false;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // ── Compiled compute shaders ──────────────────────────────────────
    ID3D11ComputeShader* m_bilateralCS     = nullptr;
    ID3D11ComputeShader* m_bilateralRGBACS = nullptr;
    ID3D11ComputeShader* m_atrousCS        = nullptr;
    ID3D11ComputeShader* m_temporalCS      = nullptr;
    ID3D11ComputeShader* m_temporalRGBACS  = nullptr;

    // ── Shared constant buffers (reused across dispatches) ────────────
    ID3D11Buffer* m_bilateralCB = nullptr;
    ID3D11Buffer* m_atrousCB    = nullptr;
    ID3D11Buffer* m_temporalCB  = nullptr;

    // ── Shared samplers ───────────────────────────────────────────────
    ID3D11SamplerState* m_pointSampler  = nullptr;
    ID3D11SamplerState* m_linearSampler = nullptr;

    // ── Per-frame camera data (set via SetMatrices/SetClipPlanes) ─────
    float m_prevViewProj[16] = {};
    float m_invViewProj[16]  = {};
    float m_nearZ = 1.0f;
    float m_farZ  = 50000.0f;
};

} // namespace SB
