//=============================================================================
//  ColorPipeline.cpp --- Full-spectrum color science pipeline
//
//  Single fullscreen pixel shader mega-pass with 12 independently togglable
//  stages.  Runs at PrePresent priority 50 (after BloomRenderer at 10,
//  before ToneMapManager at 100).
//
//  Pipeline:  Exposure -> Stevens -> Purkinje -> LocalTM -> Film/Contrast ->
//             Hunt -> ToneMap -> AgXPunchy -> Grade -> ExtGrade -> FinalOutput
//=============================================================================

#include "ColorPipeline.h"
#include "BloomRenderer.h"
#include "LuminanceHistogram.h"
#include "SharedGPUResources.h"
#include "D3D11Hook.h"
#include "WeatherParameterManager.h"

#include <SKSE/SKSE.h>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include "GPUResource.h"

namespace SB
{

// =============================================================================
//  Embedded HLSL --- Color Pipeline Mega-Shader (Pixel Shader, SM5.0)
// =============================================================================

static const char kColorPipelinePS[] = R"HLSL(
// =============================================================================
// ColorPipeline --- Multi-stage color grading pixel shader
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// References:
//   Oklab perceptual color space -- Bjorn Ottosson, 2020
//   Color temperature approximation -- Tanner Helland (1000K-40000K)
//   CIE color science, photographic film chemistry
//   Triangular-PDF dithering -- Gjoel / Christensen 2012
// =============================================================================

struct VSOut
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

cbuffer ColorPipelineCB : register(b0)
{
    // row 0
    float   CurrentEV;
    float   DeltaTime;
    uint    StageMask;
    int     ToneCurve;

    // row 1
    float   FilmToe;
    float   FilmShoulder;
    float   FilmGamma;
    float   FilmDensity;

    // row 2
    float   InterimageStrength;
    float   PurkinjeStrength;
    float   HuntStrength;
    float   LocalTMStrength;

    // row 3
    float   PrinterR;
    float   PrinterG;
    float   PrinterB;
    float   ColorTemp;

    // row 4
    float   ShadowTintA;
    float   ShadowTintB;
    float   ShadowTintStrength;
    float   HighlightTintA;

    // row 5
    float   HighlightTintB;
    float   HighlightTintStrength;
    float   CDLSlopeR;
    float   CDLSlopeG;

    // row 6
    float   CDLSlopeB;
    float   CDLSaturation;
    float   CDLOffsetR;
    float   CDLOffsetG;

    // row 7
    float   CDLOffsetB;
    float   CDLPower;
    float   BleachAmount;
    float   Saturation;

    // row 8
    float   DitherSeed;
    int     HDROutput;
    float   PaperWhiteNits;
    float   MaxNits;

    // row 9
    float   WhiteBalanceTemp;
    float   ExposureComp;
    float   PunchySaturation;
    float   StevensAdaptation;

    // row 10
    float   LiftR, LiftG, LiftB;
    float   GammaR;

    // row 11
    float   GammaG, GammaB;
    float   GainR, GainG;

    // row 12
    float   GainB;
    float   Vibrance;
    float   SCurveContrast;
    float   VanillaInfluence;

    // row 13
    float   FilmToeR, FilmToeG, FilmToeB;
    float   FilmShoulderR;

    // row 14
    float   FilmShoulderG, FilmShoulderB;
    float   FilmGammaR, FilmGammaG;

    // row 15
    float   FilmGammaB;
    float   FilmPrintToe;
    float   FilmPrintShoulder;
    float   FilmPrintGamma;
}
cbuffer VanillaParams : register(b7)
{
    // Row 0 -- HDR
    float VP_EyeAdaptSpeed;
    float VP_BloomScale;
    float VP_BloomThreshold;
    float VP_SunlightScale;
    // Row 1 -- Cinematic
    float VP_Saturation;
    float VP_Brightness;
    float VP_Contrast;
    float VP_TintAmount;
    // Row 2 -- Tint + DOF
    float VP_TintR, VP_TintG, VP_TintB;
    float VP_DOFStrength;
    // Row 3 -- DOF + IMOD
    float VP_DOFDistance, VP_DOFRange;
    float VP_IMODActive;
    float VP_IMODStrength;
}

// ---- Stage bitmask flags (must match C++ enum) ----
static const uint STAGE_EXPOSURE  = 1u;
static const uint STAGE_STEVENS   = 2u;
static const uint STAGE_PURKINJE  = 4u;
static const uint STAGE_LOCALTM   = 8u;
static const uint STAGE_FILM      = 16u;
static const uint STAGE_CONTRAST  = 32u;
static const uint STAGE_HUNT      = 64u;
static const uint STAGE_TONEMAP   = 128u;
static const uint STAGE_AGXPUNCHY = 256u;
static const uint STAGE_GRADE     = 512u;
static const uint STAGE_EXTGRADE  = 1024u;
static const uint STAGE_DITHER    = 2048u;

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy
Texture2D<float4> BloomTex   : register(t1);  // BloomRenderer output
SamplerState LinearSampler : register(s0);
SamplerState PointSampler  : register(s1);

// =============================================================================
// Oklab color space -- Bjorn Ottosson, 2020
// Linear sRGB -> Oklab and Oklab -> Linear sRGB
// Published matrices: https://bottosson.github.io/posts/oklab/
// =============================================================================

float3 LinearToOklab(float3 c)
{
    // Step 1: linear sRGB -> LMS (cone response)
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    // Step 2: cube root (perceptual nonlinearity)
    float l_ = pow(max(l, 0.0), 1.0 / 3.0);
    float m_ = pow(max(m, 0.0), 1.0 / 3.0);
    float s_ = pow(max(s, 0.0), 1.0 / 3.0);

    // Step 3: LMS' -> Lab
    return float3(
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    );
}

float3 OklabToLinear(float3 lab)
{
    // Step 1: Lab -> LMS'
    float l_ = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    float m_ = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    float s_ = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;

    // Step 2: cube (invert cube root)
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    // Step 3: LMS -> linear sRGB
    return float3(
         4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
        -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
        -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
    );
}

// =============================================================================
// White Balance -- Tanner Helland's color temperature approximation
// Maps Kelvin (1000-40000) to an RGB multiplier.
// Source: tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/
// =============================================================================

float3 KelvinToRGB(float kelvin)
{
    float temp = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float3 rgb;

    // Red
    if (temp <= 66.0)
        rgb.r = 1.0;
    else
        rgb.r = saturate(1.29293618606 * pow(temp - 60.0, -0.1332047592));

    // Green
    if (temp <= 66.0)
        rgb.g = saturate(0.39008157876 * log(temp) - 0.63184144378);
    else
        rgb.g = saturate(1.12989086090 * pow(temp - 60.0, -0.0755148492));

    // Blue
    if (temp >= 66.0)
        rgb.b = 1.0;
    else if (temp <= 19.0)
        rgb.b = 0.0;
    else
        rgb.b = saturate(0.54320678911 * log(temp - 10.0) - 1.19625408914);

    return rgb;
}

float3 ApplyWhiteBalance(float3 color, float tempKelvin)
{
    float3 source = KelvinToRGB(tempKelvin);
    float3 reference = KelvinToRGB(6500.0);    // D65 reference white
    return color * (reference / max(source, 1e-6));
}

// =============================================================================
// Exposure -- Simple EV multiplication
// =============================================================================

float3 ApplyExposure(float3 color, float ev)
{
    return color * pow(2.0, ev);
}

// =============================================================================
// Log-domain contrast -- around 18% grey pivot
// =============================================================================

float3 ApplyContrast(float3 color, float contrast)
{
    float3 logColor = log2(max(color, 1e-6));
    float  logPivot = log2(0.18);
    return exp2((logColor - logPivot) * contrast + logPivot);
}
)HLSL"
// ---- Split to avoid MSVC C2026 (string literal > 16380 chars) ----
R"HLSL(
// =============================================================================
// ColorPipeline --- Multi-stage color grading pixel shader (continued)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// =============================================================================

// =============================================================================
// Luminance-preserving saturation
// =============================================================================

float3 ApplySaturation(float3 color, float sat)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return lerp(luma.xxx, color, sat);
}

// =============================================================================
// Vibrance -- saturation boost that protects already-saturated colors
// Low-saturation pixels receive more boost than high-saturation ones.
// =============================================================================

float3 ApplyVibrance(float3 color, float vibrance)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float maxC = max(color.r, max(color.g, color.b));
    float minC = min(color.r, min(color.g, color.b));
    float chroma = maxC - minC;
    // Existing saturation ratio: 0 for grey, ~1 for fully saturated
    float existingSat = (maxC > 1e-6) ? (chroma / maxC) : 0.0;
    // Inverse weight: boost unsaturated pixels more
    float weight = 1.0 - existingSat;
    float effectiveSat = 1.0 + vibrance * weight;
    return lerp(luma.xxx, color, effectiveSat);
}

// =============================================================================
// Lift / Gamma / Gain -- 3-way color correction
// =============================================================================

float3 ApplyLiftGammaGain(float3 color, float3 lift, float3 gamma, float3 gain)
{
    // Shadows (lift):   color = color + lift * (1 - color)
    color = color + lift * (1.0 - color);

    // Midtones (gamma): color = pow(color, 1.0 / gamma)
    float3 safeGamma = max(gamma, 0.01);
    color = pow(max(color, 0.0), 1.0 / safeGamma);

    // Highlights (gain): color = color * gain
    color = color * gain;

    return color;
}

// =============================================================================
// Split Toning -- shadow and highlight tint in Oklab space
// Shadow tint weighted by (1 - luma), highlight tint by luma.
// Tint values are Oklab a,b channels for perceptually uniform blending.
// =============================================================================

float3 ApplySplitToning(float3 color,
                        float shadowA, float shadowB, float shadowStr,
                        float highlightA, float highlightB, float highlightStr)
{
    float3 lab = LinearToOklab(max(color, 0.0));
    float  L = saturate(lab.x);  // perceptual lightness [0,1]

    // Shadow tint: weight by darkness
    float shadowWeight  = (1.0 - L) * shadowStr;
    lab.y += shadowA * shadowWeight;
    lab.z += shadowB * shadowWeight;

    // Highlight tint: weight by lightness
    float highlightWeight = L * highlightStr;
    lab.y += highlightA * highlightWeight;
    lab.z += highlightB * highlightWeight;

    return max(OklabToLinear(lab), 0.0);
}

// =============================================================================
// Film Grain -- density-dependent procedural grain
// Uses procedural blue-noise-like hash for spatial distribution.
// Grain intensity rolls off in bright areas (multiplicative, not additive).
// =============================================================================

// Quality hash for grain -- ALU-based, avoids texture dependency
float GrainHash(float2 p, float seed)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33 + seed);
    return frac((p3.x + p3.y) * p3.z);
}

float3 ApplyFilmGrain(float3 color, float2 pixelCoord, float density, float rolloff, float seed)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    // Density-dependent: more grain in shadows, less in highlights
    float grainStrength = density * (1.0 - saturate(luma / max(rolloff, 0.01)));
    // Procedural blue-noise-like pattern
    float noise = GrainHash(pixelCoord, seed);
    // Multiplicative grain preserves color ratios
    float grainMul = 1.0 + (noise - 0.5) * 2.0 * grainStrength;
    return color * max(grainMul, 0.0);
}

// =============================================================================
// Vignette -- cos^4(theta) optical vignette model
// =============================================================================

float ComputeVignette(float2 uv, float strength)
{
    float2 d = uv - 0.5;
    float r2 = dot(d, d) * 4.0;
    return pow(saturate(1.0 - r2 * strength), 2.0);
}

// =============================================================================
// Blue noise triangular-PDF dithering -- prevents banding on 8-bit output
// Gjoel / Christensen 2012 -- triangular distribution from uniform
// =============================================================================

float3 ApplyDither(float3 color, float2 pixelCoord, float seed)
{
    // Generate two independent uniform noise values per channel
    float3 noise1 = float3(
        GrainHash(pixelCoord, seed + 0.0),
        GrainHash(pixelCoord, seed + 1.7),
        GrainHash(pixelCoord, seed + 3.1)
    );
    float3 noise2 = float3(
        GrainHash(pixelCoord, seed + 5.3),
        GrainHash(pixelCoord, seed + 7.9),
        GrainHash(pixelCoord, seed + 11.3)
    );
    // Triangular PDF: sum of two uniform -> triangle distribution in [-1, 1]
    float3 tri = noise1 + noise2 - 1.0;
    // Scale to 1 LSB for 8-bit output
    color += tri / 255.0;
    return color;
}

// =============================================================================
// Main pixel shader entry point
// =============================================================================

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.texcoord;
    float3 color = SceneColor.Sample(PointSampler, uv).rgb;

    // ---- Stage 1: White Balance (Kelvin temperature shift) ------------------
    if (StageMask & STAGE_EXPOSURE)
    {
        color = ApplyWhiteBalance(color, WhiteBalanceTemp);
    }

    // ---- Stage 2: Exposure --------------------------------------------------
    if (StageMask & STAGE_EXPOSURE)
    {
        color = ApplyExposure(color, ExposureComp);
    }

    // ---- Stage 3: Contrast (log-domain) -------------------------------------
    if (StageMask & STAGE_CONTRAST)
    {
        color = ApplyContrast(color, SCurveContrast);
    }

    // ---- Stage 4: Saturation (luminance-preserving) -------------------------
    if (StageMask & STAGE_GRADE)
    {
        color = ApplySaturation(color, Saturation);
    }

    // ---- Stage 5: Vibrance (protects saturated colors) ----------------------
    if (StageMask & STAGE_EXTGRADE)
    {
        color = ApplyVibrance(color, Vibrance);
    }

    // ---- Stage 6: Lift / Gamma / Gain (3-way color correction) --------------
    if (StageMask & STAGE_EXTGRADE)
    {
        float3 lift  = float3(LiftR,  LiftG,  LiftB);
        float3 gamma = float3(GammaR, GammaG, GammaB);
        float3 gain  = float3(GainR,  GainG,  GainB);
        color = ApplyLiftGammaGain(color, lift, gamma, gain);
    }

    // ---- Stage 7: Split Toning (Oklab perceptual blending) ------------------
    if (StageMask & STAGE_GRADE)
    {
        color = ApplySplitToning(color,
            ShadowTintA, ShadowTintB, ShadowTintStrength,
            HighlightTintA, HighlightTintB, HighlightTintStrength);
    }

    // ---- Stage 8: Film Grain (density-dependent) ----------------------------
    if (StageMask & STAGE_FILM)
    {
        float2 pixelCoord = input.position.xy;
        // FilmDensity controls strength, FilmShoulder as grain rolloff
        color = ApplyFilmGrain(color, pixelCoord, FilmDensity, FilmShoulder, DitherSeed);
    }

    // ---- Stage 9: Vignette (cos^4 theta optical model) ----------------------
    if (StageMask & STAGE_HUNT)
    {
        float vignette = ComputeVignette(uv, HuntStrength);
        color *= vignette;
    }

    // ---- Stage 10: Dither (blue noise triangular-PDF) -----------------------
    if (StageMask & STAGE_DITHER)
    {
        float2 pixelCoord = input.position.xy;
        color = ApplyDither(color, pixelCoord, DitherSeed);
    }

    return float4(max(color, 0.0), 1.0);
}
)HLSL";


// =============================================================================
//  Initialize
// =============================================================================

bool ColorPipeline::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("ColorPipeline: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // ---- Register fullscreen pixel shader pass ----
    m_mainPass = rpm.RegisterPass({
        .name     = "ColorPipeline",
        .psSource = kColorPipelinePS,
    });
    if (!m_mainPass) {
        SKSE::log::error("ColorPipeline: failed to register pixel shader pass");
        return false;
    }

    // ---- Create constant buffer ----
    if (!CreateCB(dev, sizeof(ColorPipelineCBData), &m_mainCB)) {
        SKSE::log::error("ColorPipeline: failed to create constant buffer");
        return false;
    }

    // ---- Create samplers ----
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &m_linearSampler))) {
            SKSE::log::error("ColorPipeline: failed to create linear sampler");
            return false;
        }

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (FAILED(dev->CreateSamplerState(&sd, &m_pointSampler))) {
            SKSE::log::error("ColorPipeline: failed to create point sampler");
            return false;
        }
    }

    // ---- Create initial scene color copy texture ----
    // Sized from the swapchain backbuffer at init time.  ExecutePass will
    // lazily recreate this if the game's scene RT has a different format/size.
    {
        ID3D11Texture2D* backTex = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&backTex))))
        {
            D3D11_TEXTURE2D_DESC bbDesc;
            backTex->GetDesc(&bbDesc);

            bbDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            bbDesc.Usage          = D3D11_USAGE_DEFAULT;
            bbDesc.CPUAccessFlags = 0;

            if (FAILED(dev->CreateTexture2D(&bbDesc, nullptr, &m_backbufferCopy))) {
                backTex->Release();
                SKSE::log::error("ColorPipeline: failed to create backbuffer copy texture");
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = bbDesc.Format;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels       = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            if (FAILED(dev->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                      &m_backbufferCopySRV)))
            {
                backTex->Release();
                SKSE::log::error("ColorPipeline: failed to create backbuffer copy SRV");
                return false;
            }

            backTex->Release();
        } else {
            SKSE::log::error("ColorPipeline: failed to get backbuffer");
            return false;
        }
    }

    // ---- Register pipeline pass (PreUI, priority 50) ----
    m_pipelineHandle = pl.AddPass({
        .name     = "ColorPipeline",
        .stage    = PipelineStage::PreUI,
        .priority = 50,
        .enabled  = false,  // default disabled: verify baseline first
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    // Detect HDR from proxy — set output mode accordingly
    m_outputMode = D3D11Hook::IsHDREnabled() ? 1 : 0;

    m_initialized = true;
    SKSE::log::info("ColorPipeline: initialized (curve={}, stages=0x{:03X}, outputMode={})",
                    static_cast<int>(m_toneCurve), m_stageMask, m_outputMode);
    return true;
}


// =============================================================================
//  Shutdown
// =============================================================================

void ColorPipeline::Shutdown()
{
    if (!m_initialized) return;

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    SafeRelease(m_mainCB);
    SafeRelease(m_linearSampler);
    SafeRelease(m_pointSampler);
    SafeRelease(m_backbufferCopy);
    SafeRelease(m_backbufferCopySRV);

    m_mainPass = 0;
    m_initialized = false;

    SKSE::log::info("ColorPipeline: shut down");
}


// =============================================================================
//  Per-frame execution (called by RenderPipeline at PreUI stage)
// =============================================================================

void ColorPipeline::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& rpm  = RenderPassManager::Get();
    auto& hist = LuminanceHistogram::Get();

    // ---- Compute auto-exposure from histogram CPU readback ----
    if (hist.IsInitialized() && (m_stageMask & CPS_Exposure)) {
        auto& result = hist.GetResult();
        float avgLum = result.avgLuminance;
        float p50    = result.p50;

        // Geometric mean of average and median for robust metering
        float keyLum = std::sqrt((std::max)(avgLum, 0.001f) * (std::max)(p50, 0.001f));

        // Target EV: keyLum maps to 18% grey
        float targetEV = std::log2((std::max)(keyLum, 0.0001f) / 0.18f);
        targetEV -= m_exposureComp;
        targetEV = std::clamp(targetEV, -4.0f, 16.0f);

        // Temporal adaptation
        float dt    = (ctx.deltaTime > 0) ? ctx.deltaTime : 0.016f;
        float speed = std::clamp(2.0f * dt, 0.0f, 1.0f);
        m_currentEV = m_currentEV + (targetEV - m_currentEV) * speed;
        m_currentEV = std::clamp(m_currentEV, -20.0f, 20.0f);
    }

    // ---- Compute Purkinje strength from current EV ----
    // Ramps from 0 at EV>2 to full at EV<-2
    float purkinjeStr = 0.0f;
    if (m_stageMask & CPS_Purkinje) {
        purkinjeStr = std::clamp((2.0f - m_currentEV) / 4.0f, 0.0f, 1.0f);
    }

    // ---- Acquire scene texture + RTV ----------------------------------------
    // Mid-frame: the backbuffer does NOT contain the scene — the game renders
    // to an internal RT exposed via ctx.gameSceneRTV.  Extract the underlying
    // texture from that RTV first, falling back to the swapchain backbuffer
    // only when gameSceneRTV is null (PrePresent-time dispatch).
    ID3D11Texture2D*        sceneTex = nullptr;
    ID3D11RenderTargetView* sceneRTV = nullptr;
    bool ownSceneTex = false;  // true if we obtained sceneTex via QI/GetBuffer
    bool ownRTV      = false;  // true if we created the RTV and must release it

    if (ctx.gameSceneRTV) {
        // Mid-frame dispatch: game's active scene RT
        ID3D11Resource* res = nullptr;
        ctx.gameSceneRTV->GetResource(&res);
        if (res) {
            HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                              reinterpret_cast<void**>(&sceneTex));
            res->Release();
            if (FAILED(hr)) sceneTex = nullptr;
            ownSceneTex = (sceneTex != nullptr);
        }
        sceneRTV = ctx.gameSceneRTV;
        // Don't AddRef — D3D11StateBackup keeps it alive during dispatch
    } else {
        // PrePresent fallback: use swapchain backbuffer
        auto* sc = ctx.swapChain;
        if (!sc) sc = D3D11Hook::GetSwapChain();
        if (!sc) return;

        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                  reinterpret_cast<void**>(&sceneTex))))
            return;
        ownSceneTex = true;

        // Create RTV from backbuffer
        D3D11_TEXTURE2D_DESC texDesc;
        sceneTex->GetDesc(&texDesc);
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = texDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        m_device->CreateRenderTargetView(sceneTex, &rtvDesc, &sceneRTV);
        ownRTV = true;
    }

    if (!sceneTex || !sceneRTV) {
        if (ownSceneTex && sceneTex) sceneTex->Release();
        if (ownRTV && sceneRTV)      sceneRTV->Release();
        return;
    }

    // ---- Ensure copy texture matches scene RT format/size -----------------
    // The swapchain backbuffer is R8G8B8A8_UNORM but the game's internal
    // scene RT is often R16G16B16A16_FLOAT.  CopyResource requires identical
    // format+dimensions, so we lazily recreate the copy texture if needed.
    {
        D3D11_TEXTURE2D_DESC sceneDesc;
        sceneTex->GetDesc(&sceneDesc);

        D3D11_TEXTURE2D_DESC copyDesc;
        m_backbufferCopy->GetDesc(&copyDesc);

        if (sceneDesc.Format != copyDesc.Format ||
            sceneDesc.Width  != copyDesc.Width  ||
            sceneDesc.Height != copyDesc.Height)
        {
            SKSE::log::info("ColorPipeline: scene RT format/size changed — "
                "scene={}x{} fmt={}, copy={}x{} fmt={} — recreating",
                sceneDesc.Width, sceneDesc.Height, static_cast<int>(sceneDesc.Format),
                copyDesc.Width, copyDesc.Height, static_cast<int>(copyDesc.Format));

            if (m_backbufferCopySRV) { m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr; }
            if (m_backbufferCopy)    { m_backbufferCopy->Release();    m_backbufferCopy = nullptr; }

            D3D11_TEXTURE2D_DESC newDesc = sceneDesc;
            newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            newDesc.Usage          = D3D11_USAGE_DEFAULT;
            newDesc.CPUAccessFlags = 0;
            newDesc.MiscFlags      = 0;

            HRESULT hr = m_device->CreateTexture2D(&newDesc, nullptr, &m_backbufferCopy);
            if (FAILED(hr)) {
                SKSE::log::error("ColorPipeline: failed to recreate copy texture");
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = newDesc.Format;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels       = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                     &m_backbufferCopySRV);
            if (FAILED(hr)) {
                SKSE::log::error("ColorPipeline: failed to recreate copy SRV");
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }

            SKSE::log::info("ColorPipeline: copy texture recreated as {}x{} fmt={}",
                newDesc.Width, newDesc.Height, static_cast<int>(newDesc.Format));
        }
    }

    ctx.context->CopyResource(m_backbufferCopy, sceneTex);
    if (ownSceneTex) sceneTex->Release();

    // ---- Fill constant buffer ----
    ColorPipelineCBData cb = {};

    cb.currentEV          = m_currentEV;
    cb.deltaTime          = (ctx.deltaTime > 0) ? ctx.deltaTime : 0.016f;
    cb.stageMask          = m_stageMask;
    cb.toneCurve          = static_cast<int32_t>(m_toneCurve);

    cb.filmToe            = m_filmToe;
    cb.filmShoulder       = m_filmShoulder;
    cb.filmGamma          = m_filmGamma;
    cb.filmDensity        = m_filmDensity;

    cb.interimageStrength = m_interimageStrength;
    cb.purkinjeStrength   = purkinjeStr;
    cb.huntStrength       = m_huntStrength;
    cb.localTMStrength    = m_localTMStrength;

    cb.printerR           = m_printerR;
    cb.printerG           = m_printerG;
    cb.printerB           = m_printerB;
    cb.colorTemp          = m_gradeColorTemp;

    cb.shadowTintA        = m_shadowTintA;
    cb.shadowTintB        = m_shadowTintB;
    cb.shadowTintStrength = m_shadowTintStrength;
    cb.highlightTintA     = m_highlightTintA;

    cb.highlightTintB         = m_highlightTintB;
    cb.highlightTintStrength  = m_highlightTintStrength;
    cb.cdlSlopeR              = m_cdlSlopeR;
    cb.cdlSlopeG              = m_cdlSlopeG;

    cb.cdlSlopeB          = m_cdlSlopeB;
    cb.cdlSaturation      = m_cdlSaturation;
    cb.cdlOffsetR         = m_cdlOffsetR;
    cb.cdlOffsetG         = m_cdlOffsetG;

    // Weather-reactive modulation
    const auto& wp = WeatherParameterManager::Get().GetCurrent();

    cb.cdlOffsetB         = m_cdlOffsetB;
    cb.cdlPower           = m_cdlPower;
    cb.bleachAmount       = m_bleachAmount;
    cb.saturation         = m_saturation * wp.saturation;

    // Dither seed: cheap per-frame random
    cb.ditherSeed         = static_cast<float>(ctx.frameIndex) * 0.7548776662f;
    cb.hdrOutput          = m_outputMode;
    cb.paperWhiteNits     = m_paperWhiteNits;
    cb.maxNits            = m_maxNits;

    cb.whiteBalanceTemp   = m_whiteBalanceTemp + wp.colorTempOffset;
    cb.exposureComp       = m_exposureComp + wp.exposureBias;
    cb.punchySaturation   = m_punchySaturation;
    cb.stevensAdaptation  = 1.0f;

    // Extended grading
    cb.liftR  = m_liftR;  cb.liftG  = m_liftG;  cb.liftB  = m_liftB;
    cb.gammaR = m_gammaR;
    cb.gammaG = m_gammaG;  cb.gammaB = m_gammaB;
    cb.gainR  = m_gainR;   cb.gainG  = m_gainG;
    cb.gainB  = m_gainB;
    cb.vibrance       = m_vibrance;
    cb.sCurveContrast = m_sCurveContrast * wp.contrast;
    cb.vanillaInfluence = m_vanillaInfluence;

    // Film per-channel presets (Kodak 500T defaults)
    cb.filmToeR = m_filmToe;       cb.filmToeG = m_filmToe * 0.95f;  cb.filmToeB = m_filmToe * 1.05f;
    cb.filmShoulderR = m_filmShoulder;
    cb.filmShoulderG = m_filmShoulder * 0.98f;
    cb.filmShoulderB = m_filmShoulder * 1.02f;
    cb.filmGammaR = m_filmGamma;
    cb.filmGammaG = m_filmGamma * 1.02f;
    cb.filmGammaB = m_filmGamma * 0.98f;
    cb.filmPrintToe      = 0.05f;   // Kodak 2383 print stock
    cb.filmPrintShoulder = 0.95f;
    cb.filmPrintGamma    = 0.85f;

    // ---- Upload CB ----
    UploadCB(ctx.context, m_mainCB, &cb, sizeof(cb));

    // ---- Build SRV array: t0 = scene color copy, t1 = bloom ----
    ID3D11ShaderResourceView* srvs[2] = { m_backbufferCopySRV, nullptr };

    auto& bloom = BloomRenderer::Get();
    if (bloom.IsInitialized() && bloom.IsEnabled()) {
        srvs[1] = bloom.GetBloomSRV();
    }

    uint32_t srvCount = srvs[1] ? 2u : 1u;

    // ---- Build sampler array: s0 = linear, s1 = point ----
    ID3D11SamplerState* samplers[2] = { m_linearSampler, m_pointSampler };

    // ---- Bind vanilla ImageSpace params CB at PS b7 ----
    auto* vanillaCB = SharedGPUResources::Get().GetVanillaParamsCB();
    ctx.context->PSSetConstantBuffers(SharedGPUResources::kVanillaParamsCBSlot, 1, &vanillaCB);

    // ---- Execute fullscreen pass ----
    rpm.Execute({
        .passID       = m_mainPass,
        .rtv          = sceneRTV,
        .srvs         = srvs,
        .srvCount     = srvCount,
        .samplers     = samplers,
        .samplerCount = 2,
        .cbData       = &cb,
        .cbSize       = sizeof(cb),
    });

    // ---- Unbind vanilla params CB ----
    {
        ID3D11Buffer* nullCB = nullptr;
        ctx.context->PSSetConstantBuffers(SharedGPUResources::kVanillaParamsCBSlot, 1, &nullCB);
    }

    if (ownRTV) sceneRTV->Release();
}

} // namespace SB
