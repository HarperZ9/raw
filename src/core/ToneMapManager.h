#pragma once
//=============================================================================
//  ToneMapManager — HDR autoexposure + tone mapping pipeline
//
//  Two-pass system:
//    1. AutoExposure CS: reads histogram (t17), computes EV with temporal
//       smoothing, writes to 1x1 R32_FLOAT exposure buffer
//    2. ToneMap PS: fullscreen pass reads backbuffer + exposure, applies
//       tone curve (AgX/ACES/Reinhard), outputs display-ready color
//
//  HDR output: PQ (ST.2084) for HDR10, scRGB passthrough for scRGB displays
//  SDR output: AgX (default), ACES fitted, or Reinhard extended
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"

namespace SB
{

enum class ToneCurve : int
{
    AgX = 0,        // Default — best saturated color handling
    ACES,           // Academy Color Encoding System (fitted approximation)
    Reinhard,       // Extended Reinhard with luminance
    None,           // Linear clamp (debug)
    Count
};

class ToneMapManager
{
public:
    static ToneMapManager& Get()
    {
        static ToneMapManager inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Settings
    ToneCurve GetToneCurve() const { return m_curve; }
    void      SetToneCurve(ToneCurve c) { m_curve = c; }
    float     GetExposureCompensation() const { return m_exposureComp; }
    void      SetExposureCompensation(float ev) { m_exposureComp = ev; }
    float     GetAdaptSpeed() const { return m_adaptSpeed; }
    void      SetAdaptSpeed(float s) { m_adaptSpeed = s; }
    bool      IsHDROutput() const { return m_hdrOutput; }
    void      SetHDROutput(bool hdr) { m_hdrOutput = hdr; }
    float     GetPaperWhiteNits() const { return m_paperWhiteNits; }
    void      SetPaperWhiteNits(float n) { m_paperWhiteNits = n; }
    float     GetMaxNits() const { return m_maxNits; }
    void      SetMaxNits(float n) { m_maxNits = n; }
    float     GetVanillaInfluence() const { return m_vanillaInfluence; }
    void      SetVanillaInfluence(float v) { m_vanillaInfluence = v; }

    // Current computed exposure
    float     GetCurrentEV() const { return m_currentEV; }

private:
    ToneMapManager() = default;

    bool m_initialized = false;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Settings
    ToneCurve m_curve          = ToneCurve::AgX;
    float     m_exposureComp   = 0.0f;    // EV offset
    float     m_adaptSpeed     = 2.0f;    // adaptation speed (EV/sec)
    bool      m_hdrOutput      = false;
    float     m_paperWhiteNits = 200.0f;
    float     m_maxNits        = 1000.0f;
    float     m_vanillaInfluence = 1.0f;

    // Computed state
    float     m_currentEV      = 0.0f;

    // Autoexposure compute shader
    ComputeShaderID m_autoExpCS = 0;
    ComputeManager::BufferResource m_exposureBuf;   // 1-element float (current EV)
    ID3D11Buffer* m_autoExpCB = nullptr;

    // Tone map fullscreen pass
    RenderPassID m_toneMapPass = 0;
    ID3D11Buffer* m_toneMapCB = nullptr;
    ID3D11SamplerState* m_pointSampler = nullptr;

    // Pipeline pass handle
    PassHandle m_pipelineHandle = 0;

    // Backbuffer copy (we read backbuffer as SRV, write to backbuffer as RTV)
    ID3D11Texture2D*            m_backbufferCopy    = nullptr;
    ID3D11ShaderResourceView*   m_backbufferCopySRV = nullptr;

    // Internal methods
    void ExecutePass(PassContext& ctx);
};

} // namespace SB
