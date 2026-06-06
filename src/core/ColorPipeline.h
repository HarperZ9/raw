#pragma once
//=============================================================================
//  ColorPipeline --- Full-spectrum color science pipeline
//
//  Full-spectrum color science pipeline implemented as a single
//  fullscreen pixel shader mega-pass.  Each stage is independently togglable
//  via a bitmask in the constant buffer.
//
//  Pipeline stages (in execution order):
//    1.  Exposure + White Balance       (auto-EV + Tanner Helland Kelvin->RGB)
//    2.  Stevens Effect                 (adaptation-scaled contrast)
//    3.  Purkinje Shift                 (scotopic blue-shift at low luminance)
//    4.  Local Tone Mapping             (bloom-driven local contrast)
//    5.  FILM Pipeline                  (neg/print stock, Beer-Lambert, interimage)
//    6.  Log-domain Contrast            (fallback when FILM disabled)
//    7.  Hunt Effect                    (brightness-dependent saturation)
//    8.  Tonemapping                    (8 operators: AgX/ACES/Reinhard/Hejl/
//                                        Hable/Lottes/GranTurismo/None)
//    9.  AgX Punchy Look               (optional saturation boost in AgX space)
//    10. GRADE Pipeline                 (highlight desat, printer lights,
//                                        split-toning in Oklab, ASC-CDL, bleach)
//    11. Extended Grading               (lift/gamma/gain, vibrance, S-curve)
//    12. Final Output                   (saturation, dither, sRGB/PQ encoding)
//
//  Pipeline stage: PrePresent, priority 50
//    After BloomRenderer (10), before ToneMapManager (100).
//
//  Input:  Backbuffer copy (t0) + BloomRenderer output (t1) + histogram stats
//  Output: Writes directly to backbuffer RTV
//
//  VRAM budget: ~24 MB at 1920x1080 (backbuffer copy R16G16B16A16_FLOAT)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPassManager.h"
#include "RenderPipeline.h"

namespace SB
{

// ---- Stage bitmask flags --------------------------------------------------

enum ColorPipelineStage : uint32_t
{
    CPS_Exposure  = 1u << 0,   // 1
    CPS_Stevens   = 1u << 1,   // 2
    CPS_Purkinje  = 1u << 2,   // 4
    CPS_LocalTM   = 1u << 3,   // 8
    CPS_Film      = 1u << 4,   // 16
    CPS_Contrast  = 1u << 5,   // 32
    CPS_Hunt      = 1u << 6,   // 64
    CPS_ToneMap   = 1u << 7,   // 128
    CPS_AgXPunchy = 1u << 8,   // 256
    CPS_Grade     = 1u << 9,   // 512
    CPS_ExtGrade  = 1u << 10,  // 1024
    CPS_Dither    = 1u << 11,  // 2048

    CPS_AllStages = 0xFFF,
    CPS_Default   = CPS_Exposure | CPS_ToneMap | CPS_Dither,
};

// ---- Tone curve selection -------------------------------------------------

enum class ColorToneCurve : int
{
    AgX = 0,             // Troy Sobotka
    ACES,                // Stephen Hill (fitted RRT+ODT)
    ReinhardExtended,    // x(1+x/w^2)/(1+x)
    HejlBurgess,        // Hejl 2010 single-pass filmic
    Hable,               // Uncharted 2
    Lottes,              // AMD parametric
    GranTurismo,         // Uchimura two-segment
    None,                // Linear clamp
    Count
};

// ---- Constant buffer layout (must match HLSL exactly) ---------------------

struct alignas(16) ColorPipelineCBData
{
    // ---- float4 row 0 ----
    float   currentEV;          // Auto-exposure EV from LuminanceHistogram
    float   deltaTime;          // Frame delta
    uint32_t stageMask;         // Bitmask of enabled stages
    int32_t  toneCurve;         // 0-7 tone operator selection

    // ---- float4 row 1 ----
    float   filmToe;            // FILM: toe strength
    float   filmShoulder;       // FILM: shoulder strength
    float   filmGamma;          // FILM: mid gamma
    float   filmDensity;        // FILM: Beer-Lambert density

    // ---- float4 row 2 ----
    float   interimageStrength; // FILM: cross-channel inhibition
    float   purkinjeStrength;   // Auto-computed from EV (scotopic)
    float   huntStrength;       // Hunt effect saturation multiplier
    float   localTMStrength;    // Local tone mapping blend factor

    // ---- float4 row 3 ----
    float   printerR;           // GRADE: printer lights R (1-50, default 25)
    float   printerG;           // GRADE: printer lights G
    float   printerB;           // GRADE: printer lights B
    float   colorTemp;          // GRADE: color temperature (Kelvin)

    // ---- float4 row 4 ----
    float   shadowTintA;        // GRADE: shadow tint Oklab a
    float   shadowTintB;        // GRADE: shadow tint Oklab b
    float   shadowTintStrength; // GRADE: shadow tint strength
    float   highlightTintA;     // GRADE: highlight tint Oklab a

    // ---- float4 row 5 ----
    float   highlightTintB;     // GRADE: highlight tint Oklab b
    float   highlightTintStrength;
    float   cdlSlopeR;         // ASC-CDL slope (per channel)
    float   cdlSlopeG;

    // ---- float4 row 6 ----
    float   cdlSlopeB;
    float   cdlSaturation;     // ASC-CDL global saturation
    float   cdlOffsetR;        // ASC-CDL offset (per channel)
    float   cdlOffsetG;

    // ---- float4 row 7 ----
    float   cdlOffsetB;
    float   cdlPower;          // ASC-CDL power (uniform)
    float   bleachAmount;      // GRADE: bleach bypass strength
    float   saturation;        // Final output saturation

    // ---- float4 row 8 ----
    float   ditherSeed;        // Random seed per frame
    int32_t hdrOutput;         // 0 = SDR sRGB, 1 = HDR PQ
    float   paperWhiteNits;    // HDR paper white
    float   maxNits;           // HDR peak luminance

    // ---- float4 row 9 (exposure / white balance) ----
    float   whiteBalanceTemp;  // Exposure white balance (Kelvin, 1000-40000)
    float   exposureComp;      // Manual exposure compensation (EV offset)
    float   punchySaturation;  // AgX punchy look saturation boost
    float   stevensAdaptation; // Stevens effect adaptation level

    // ---- float4 row 10 (extended grading) ----
    float   liftR, liftG, liftB;
    float   gammaR;

    // ---- float4 row 11 ----
    float   gammaG, gammaB;
    float   gainR, gainG;

    // ---- float4 row 12 ----
    float   gainB;
    float   vibrance;          // Vibrance (protects saturated colors)
    float   sCurveContrast;    // S-curve contrast strength
    float   vanillaInfluence;   // Blend factor for vanilla ImageSpace params (0-1)

    // ---- float4 row 13 (film presets) ----
    float   filmToeR, filmToeG, filmToeB;
    float   filmShoulderR;

    // ---- float4 row 14 ----
    float   filmShoulderG, filmShoulderB;
    float   filmGammaR, filmGammaG;

    // ---- float4 row 15 ----
    float   filmGammaB;
    float   filmPrintToe;      // Print stock toe
    float   filmPrintShoulder; // Print stock shoulder
    float   filmPrintGamma;    // Print stock gamma
};
static_assert(sizeof(ColorPipelineCBData) % 16 == 0,
              "ColorPipelineCBData must be 16-byte aligned");


class ColorPipeline
{
public:
    static ColorPipeline& Get()
    {
        static ColorPipeline inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Enable / disable
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool v) { m_enabled = v; }

    // ---- Stage mask -------------------------------------------------------
    void     SetStageMask(uint32_t mask) { m_stageMask = mask; }
    uint32_t GetStageMask() const        { return m_stageMask; }
    void     EnableStage(ColorPipelineStage s)  { m_stageMask |= s; }
    void     DisableStage(ColorPipelineStage s) { m_stageMask &= ~s; }
    bool     IsStageEnabled(ColorPipelineStage s) const { return (m_stageMask & s) != 0; }

    // ---- Tone curve -------------------------------------------------------
    ColorToneCurve GetToneCurve() const         { return m_toneCurve; }
    void           SetToneCurve(ColorToneCurve c) { m_toneCurve = c; }

    // ---- Exposure ---------------------------------------------------------
    float GetExposureCompensation() const  { return m_exposureComp; }
    void  SetExposureCompensation(float v) { m_exposureComp = v; }
    float GetWhiteBalanceTemp() const      { return m_whiteBalanceTemp; }
    void  SetWhiteBalanceTemp(float k)     { m_whiteBalanceTemp = k; }

    // ---- Film -------------------------------------------------------------
    float GetFilmToe() const         { return m_filmToe; }
    void  SetFilmToe(float v)        { m_filmToe = v; }
    float GetFilmShoulder() const    { return m_filmShoulder; }
    void  SetFilmShoulder(float v)   { m_filmShoulder = v; }
    float GetFilmGamma() const       { return m_filmGamma; }
    void  SetFilmGamma(float v)      { m_filmGamma = v; }
    float GetFilmDensity() const     { return m_filmDensity; }
    void  SetFilmDensity(float v)    { m_filmDensity = v; }
    float GetInterimageStrength() const  { return m_interimageStrength; }
    void  SetInterimageStrength(float v) { m_interimageStrength = v; }

    // ---- Local TM ---------------------------------------------------------
    float GetLocalTMStrength() const { return m_localTMStrength; }
    void  SetLocalTMStrength(float v){ m_localTMStrength = v; }

    // ---- Hunt effect ------------------------------------------------------
    float GetHuntStrength() const    { return m_huntStrength; }
    void  SetHuntStrength(float v)   { m_huntStrength = v; }

    // ---- Grade: Printer lights --------------------------------------------
    float GetPrinterR() const { return m_printerR; }
    float GetPrinterG() const { return m_printerG; }
    float GetPrinterB() const { return m_printerB; }
    void  SetPrinterLights(float r, float g, float b)
        { m_printerR = r; m_printerG = g; m_printerB = b; }

    // ---- Grade: Color temp ------------------------------------------------
    float GetGradeColorTemp() const  { return m_gradeColorTemp; }
    void  SetGradeColorTemp(float k) { m_gradeColorTemp = k; }

    // ---- Grade: ASC-CDL ---------------------------------------------------
    void  SetCDLSlope(float r, float g, float b)  { m_cdlSlopeR=r; m_cdlSlopeG=g; m_cdlSlopeB=b; }
    void  SetCDLOffset(float r, float g, float b) { m_cdlOffsetR=r; m_cdlOffsetG=g; m_cdlOffsetB=b; }
    void  SetCDLPower(float p)       { m_cdlPower = p; }
    float GetCDLSaturation() const   { return m_cdlSaturation; }
    void  SetCDLSaturation(float s)  { m_cdlSaturation = s; }

    // ---- Grade: Bleach bypass ---------------------------------------------
    float GetBleachAmount() const    { return m_bleachAmount; }
    void  SetBleachAmount(float v)   { m_bleachAmount = v; }

    // ---- Final output -----------------------------------------------------
    float GetSaturation() const      { return m_saturation; }
    void  SetSaturation(float v)     { m_saturation = v; }
    int   GetOutputMode() const        { return m_outputMode; } // 0=sRGB, 1=PQ, 2=scRGB, 3=Skyrim1.6
    void  SetOutputMode(int mode)     { m_outputMode = mode; }
    bool  IsHDROutput() const         { return m_outputMode == 1; }
    float GetPaperWhiteNits() const  { return m_paperWhiteNits; }
    void  SetPaperWhiteNits(float n) { m_paperWhiteNits = n; }
    float GetMaxNits() const         { return m_maxNits; }
    void  SetMaxNits(float n)        { m_maxNits = n; }

    // ---- Extended grading -------------------------------------------------
    void  SetLift(float r, float g, float b)  { m_liftR=r; m_liftG=g; m_liftB=b; }
    void  SetGamma(float r, float g, float b) { m_gammaR=r; m_gammaG=g; m_gammaB=b; }
    void  SetGain(float r, float g, float b)  { m_gainR=r; m_gainG=g; m_gainB=b; }
    float GetVibrance() const       { return m_vibrance; }
    void  SetVibrance(float v)      { m_vibrance = v; }
    float GetSCurveContrast() const { return m_sCurveContrast; }
    void  SetSCurveContrast(float v){ m_sCurveContrast = v; }

    // ---- AgX punchy look --------------------------------------------------
    float GetPunchySaturation() const  { return m_punchySaturation; }
    void  SetPunchySaturation(float v) { m_punchySaturation = v; }

    // ---- Current computed exposure (read-only) ----------------------------
    float GetCurrentEV() const { return m_currentEV; }

private:
    ColorPipeline() = default;

    // Pipeline pass callback
    void ExecutePass(PassContext& ctx);

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: default disabled

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Computed state
    float m_currentEV = 0.0f;

    // ---- Settings ---------------------------------------------------------
    uint32_t       m_stageMask       = CPS_Default;
    ColorToneCurve m_toneCurve       = ColorToneCurve::AgX;
    float          m_exposureComp    = 0.0f;
    float          m_whiteBalanceTemp = 6500.0f;

    // Film
    float m_filmToe            = 0.04f;
    float m_filmShoulder       = 0.97f;
    float m_filmGamma          = 0.80f;
    float m_filmDensity        = 0.60f;
    float m_interimageStrength = 0.15f;

    // Local TM
    float m_localTMStrength = 0.30f;

    // Hunt
    float m_huntStrength = 0.15f;

    // Grade: printer lights
    float m_printerR = 25.0f;
    float m_printerG = 25.0f;
    float m_printerB = 25.0f;

    // Grade: color temp
    float m_gradeColorTemp = 6500.0f;

    // Grade: ASC-CDL
    float m_cdlSlopeR  = 1.0f, m_cdlSlopeG  = 1.0f, m_cdlSlopeB  = 1.0f;
    float m_cdlOffsetR = 0.0f, m_cdlOffsetG = 0.0f, m_cdlOffsetB = 0.0f;
    float m_cdlPower   = 1.0f;
    float m_cdlSaturation = 1.0f;

    // Grade: split-toning (Oklab ab + strength)
    float m_shadowTintA = 0.0f, m_shadowTintB = 0.0f, m_shadowTintStrength = 0.0f;
    float m_highlightTintA = 0.0f, m_highlightTintB = 0.0f, m_highlightTintStrength = 0.0f;

    // Grade: bleach bypass
    float m_bleachAmount = 0.0f;

    // Final output
    float m_saturation     = 1.0f;
    int   m_outputMode     = 0;  // 0=sRGB 2.2, 1=PQ HDR10, 2=scRGB, 3=Skyrim gamma 1.6
    float m_paperWhiteNits = 200.0f;
    float m_maxNits         = 1000.0f;

    // Extended grading
    float m_liftR = 0.0f, m_liftG = 0.0f, m_liftB = 0.0f;
    float m_gammaR = 1.0f, m_gammaG = 1.0f, m_gammaB = 1.0f;
    float m_gainR  = 1.0f, m_gainG  = 1.0f, m_gainB  = 1.0f;
    float m_vibrance      = 0.0f;
    float m_sCurveContrast = 0.0f;

    // AgX punchy
    float m_punchySaturation = 0.0f;

    // Vanilla ImageSpace influence
    float m_vanillaInfluence = 1.0f;

    // ---- GPU resources ----------------------------------------------------
    RenderPassID                m_mainPass        = 0;
    ID3D11Buffer*               m_mainCB          = nullptr;
    ID3D11SamplerState*         m_linearSampler   = nullptr;
    ID3D11SamplerState*         m_pointSampler    = nullptr;
    ID3D11Texture2D*            m_backbufferCopy    = nullptr;
    ID3D11ShaderResourceView*   m_backbufferCopySRV = nullptr;
    PassHandle                  m_pipelineHandle  = 0;
};

} // namespace SB
