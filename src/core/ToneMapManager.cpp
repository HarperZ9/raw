//=============================================================================
//  ToneMapManager.cpp — HDR autoexposure + tone mapping
//=============================================================================

#include "ToneMapManager.h"
#include "LuminanceHistogram.h"
#include "SharedGPUResources.h"
#include "D3D11Hook.h"

#include <SKSE/SKSE.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"

namespace SB
{

// ── Autoexposure compute shader ───────────────────────────────────────────
// Reads histogram result buffer, computes target EV, temporal smoothing
static const char kAutoExposureCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// GPU autoexposure from luminance histogram.
// Reference: standard real-time HDR exposure (geometric mean + percentile metering).
//
// Reads a 256-bin log-luminance histogram (normalized, sum ~ 1.0) and computes
// a temporally-smoothed exposure multiplier.  The histogram covers the range
// [2^MinEV .. 2^MaxEV] mapped linearly across bins 0..255.

cbuffer AutoExpCB : register(b0)
{
    float PrevEV;
    float DeltaTime;
    float AdaptSpeed;
    float ExposureComp;
    float MinEV;
    float MaxEV;
    float pad0, pad1;
}

StructuredBuffer<float> HistogramStats : register(t0);
RWStructuredBuffer<float> ExposureOut : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // ── Accumulate weighted log-luminance from the 256-bin histogram ──
    // Each bin i represents a luminance range centered at:
    //   EV_i = MinEV + (i + 0.5) / 256.0 * (MaxEV - MinEV)
    //   lum_i = 2^EV_i
    //
    // We trim the bottom 10% and top 2% of the distribution to reject
    // outliers (deep shadows, specular highlights / sun disk).

    float evRange = MaxEV - MinEV;
    float totalWeight = 0.0;
    float weightedLogSum = 0.0;

    // First pass: compute total weight for percentile thresholds
    float cumulative = 0.0;
    float totalMass = 0.0;
    for (uint i = 0; i < 256; i++)
        totalMass += HistogramStats[i];

    // Guard against empty histogram (no pixels rendered yet)
    if (totalMass < 1e-6)
    {
        ExposureOut[0] = PrevEV;
        return;
    }

    float lowThreshold  = totalMass * 0.10;  // Skip bottom 10%
    float highThreshold = totalMass * 0.98;  // Skip top 2%

    // Second pass: geometric mean over the accepted range
    cumulative = 0.0;
    for (uint j = 0; j < 256; j++)
    {
        float binWeight = HistogramStats[j];
        float prevCumulative = cumulative;
        cumulative += binWeight;

        // Trim: only include bins within [10%, 98%] of the CDF
        if (cumulative < lowThreshold || prevCumulative > highThreshold)
            continue;

        // Clamp the contribution to only the portion within the accepted range
        float lo = max(prevCumulative, lowThreshold);
        float hi = min(cumulative, highThreshold);
        float accepted = hi - lo;

        // EV at the center of this bin
        float binEV = MinEV + ((float)j + 0.5) / 256.0 * evRange;

        weightedLogSum += binEV * accepted;
        totalWeight += accepted;
    }

    // Geometric mean luminance via weighted average of log2(luminance) = EV
    float avgEV = (totalWeight > 1e-6) ? (weightedLogSum / totalWeight) : 0.0;
    float avgLuminance = exp2(avgEV);

    // ── Target exposure: map avgLuminance to 18% grey (standard key value) ──
    // exposure = 0.18 / avgLuminance  =>  targetEV = log2(avgLuminance / 0.18)
    float targetEV = log2(max(avgLuminance, 1e-6) / 0.18);

    // Apply user exposure compensation
    targetEV -= ExposureComp;

    // Clamp to configured range
    targetEV = clamp(targetEV, MinEV, MaxEV);

    // ── Temporal smoothing: exponential moving average ──
    float alpha = saturate(AdaptSpeed * DeltaTime);
    float smoothedEV = lerp(PrevEV, targetEV, alpha);
    smoothedEV = clamp(smoothedEV, -20.0, 20.0);

    ExposureOut[0] = smoothedEV;
}
)HLSL";

// ── Tone mapping pixel shader ─────────────────────────────────────────────
static const char kToneMapPS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Tone mapping pixel shader.
// Implements 12 published tone curves selected by CurveType:
//   0 = AgX           (Troy Sobotka)
//   1 = ACES Fitted   (Stephen Hill)
//   2 = Reinhard Ext  (Reinhard et al., 2002)
//   3 = Hejl-Burgess  (Jim Hejl, GDC 2010)
//   4 = ACES Narkowicz (Krzysztof Narkowicz, 2014)
//   5 = AgX Punchy    (Troy Sobotka, saturated variant)
//   6 = PBR Neutral   (Khronos Group)
//   7 = Uncharted 2   (John Hable / Naughty Dog)
//   8 = Lottes        (Timothy Lottes, 2016)
//   9 = Uchimura      (Hajime Uchimura, Gran Turismo)
//  10 = Tony McMapface (perceptual balance)
//  11 = Linear        (debug passthrough)
//
// After tonemapping: Skyrim gamma 1.6 correction, then sRGB 2.2 encoding
// (SDR), or PQ ST.2084 encoding (HDR10).

cbuffer ToneMapCB : register(b0)
{
    float CurrentEV;
    int   CurveType;       // 0-11 tone curve selection
    int   HDROutput;       // 0=SDR, 1=HDR
    float PaperWhiteNits;
    float MaxNits;
    float VanillaInfluence;
    float Padding0, Padding1;
}
cbuffer VanillaParams : register(b7)
{
    // Row 0 — HDR
    float VP_EyeAdaptSpeed;
    float VP_BloomScale;
    float VP_BloomThreshold;
    float VP_SunlightScale;
    // Row 1 — Cinematic
    float VP_Saturation;
    float VP_Brightness;
    float VP_Contrast;
    float VP_TintAmount;
    // Row 2 — Tint + DOF
    float VP_TintR, VP_TintG, VP_TintB;
    float VP_DOFStrength;
    // Row 3 — DOF + IMOD
    float VP_DOFDistance, VP_DOFRange;
    float VP_IMODActive;
    float VP_IMODStrength;
}

static const float SKYRIM_GAMMA = 1.6;

Texture2D<float4> SceneColor : register(t0);
SamplerState PointSampler : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// ── Vanilla cinematic grading ────────────────────────────────────────────
// Applies Skyrim's ImageSpace saturation, brightness, contrast, and tint.
float3 ApplyVanillaGrade(float3 color)
{
    // Saturation: lerp toward luminance
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(lum, lum, lum), color, VP_Saturation);

    // Brightness: multiplicative scale
    color *= VP_Brightness;

    // Contrast: pivot around 0.5
    color = (color - 0.5) * VP_Contrast + 0.5;

    // Tint: lerp toward tint color
    float3 tint = float3(VP_TintR, VP_TintG, VP_TintB);
    color = lerp(color, color * tint, VP_TintAmount);

    return color;
}

// ── Curve 0: AgX (Troy Sobotka) ──────────────────────────────────────────
// Published specification: https://github.com/sobotka/AgX
//
// AgX is a display rendering transform designed for well-behaved behavior
// in high-saturation, high-dynamic-range regions.  It works by:
// 1. Transforming to a log-encoded AgX space via an inset matrix
// 2. Applying a contrast curve (6th order polynomial approximation)
// 3. Transforming back via an outset matrix

float3 AgXDefaultContrastApprox(float3 x)
{
    // 6th order polynomial fit to the AgX default contrast curve.
    // Attempt to match the response of the published AgX Base contrast
    // look from the Blender/AgX specification.
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2
         - 40.14 * x4 * x
         + 31.96 * x4
         - 6.868 * x2 * x
         + 0.4298 * x2
         + 0.1191 * x
         - 0.00232;
}

float3 AgX(float3 color)
{
    // AgX inset matrix: transforms from working space (sRGB linear primaries)
    // into the AgX log encoding space.
    // Published in the AgX specification by Troy Sobotka.
    static const float3x3 AgXInsetMatrix = float3x3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );

    // AgX outset matrix: inverse transform back from AgX space to working space.
    static const float3x3 AgXOutsetMatrix = float3x3(
         1.19687900512017,  -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368,  1.15190312990417,  -0.0980434066481054,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116
    );

    // Encode into AgX log space
    // Clamp to avoid log2 of zero/negative
    color = max(color, 1e-10);
    color = mul(AgXInsetMatrix, color);

    // Log2 encoding, clamped to [min_ev, max_ev] range then normalized to [0,1]
    // AgX uses a log range of approximately [-12.47393, 4.026069]
    static const float AgXMinEV = -12.47393;
    static const float AgXMaxEV = 4.026069;
    color = log2(color);
    color = (color - AgXMinEV) / (AgXMaxEV - AgXMinEV);
    color = saturate(color);

    // Apply the contrast sigmoid approximation
    color = AgXDefaultContrastApprox(color);

    // Transform back to working space
    color = mul(AgXOutsetMatrix, color);
    color = max(color, 0.0);

    return color;
}

// ── Curve 1: ACES Fitted (Stephen Hill) ──────────────────────────────────
// Published: Stephen Hill, "ACES Filmic Tone Mapping Curve" blog post.
// RRT (Reference Rendering Transform) + ODT (Output Device Transform)
// fitted to a single rational polynomial per channel after matrix transforms.

float3 ACESFitted(float3 color)
{
    // sRGB -> ACEScg input matrix (RRT working space)
    static const float3x3 ACESInputMat = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );

    // ACEScg -> sRGB output matrix
    static const float3x3 ACESOutputMat = float3x3(
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    );

    color = mul(ACESInputMat, color);

    // RRT + ODT approximation (rational polynomial)
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;

    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// ── Curve 2: Reinhard Extended ───────────────────────────────────────────
// Published: Reinhard et al., "Photographic Tone Reproduction for Digital
// Images", SIGGRAPH 2002.
//
// Luminance-based mapping with a configurable white point that controls
// where the curve approaches 1.0.

float3 ReinhardExtended(float3 color, float whitePoint)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float wp2 = whitePoint * whitePoint;
    float mapped = (luma * (1.0 + luma / wp2)) / (1.0 + luma);
    return color * (mapped / max(luma, 0.001));
}

// ── Curve 3: Hejl-Burgess (Uncharted 2 simplified) ──────────────────────
// Published: Jim Hejl, "Filmic Tonemapping for Real-time Rendering",
// GDC 2010.
//
// Single-pass filmic curve with built-in gamma approximation.  The result
// already has a rough sRGB-like gamma baked in, but we still apply our
// full gamma pipeline afterward for consistency.

float3 Hejl(float3 color)
{
    color = max(0.0, color - 0.004);
    return (color * (6.2 * color + 0.5))
         / (color * (6.2 * color + 1.7) + 0.06);
}

// ── Additional operators (published mathematical specifications) ─────────

// ACES Narkowicz (Krzysztof Narkowicz, 2014 — simple fitted curve)
float3 ACESNarkowicz(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// AgX Punchy (Troy Sobotka — saturated variant with contrast push)
float3 AgXPunchy(float3 color)
{
    float3 base = AgX(color);
    // Push saturation + contrast for a punchier look
    float luma = dot(base, float3(0.2126, 0.7152, 0.0722));
    float3 sat = lerp(luma.xxx, base, 1.35);
    return saturate(sat * 1.05);
}

// PBR Neutral (Khronos Group — KHR_PBR_Neutral extension)
float3 PBRNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}

// Uncharted 2 / Hable (John Hable, Naughty Dog, GDC 2010)
float3 Uncharted2Partial(float3 x)
{
    const float A = 0.15, B = 0.50, C = 0.10;
    const float D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
float3 Uncharted2(float3 color)
{
    float3 curr = Uncharted2Partial(color);
    float3 white = Uncharted2Partial(11.2.xxx);
    return curr / white;
}

// Lottes (Timothy Lottes, 2016 — AMD)
float3 Lottes(float3 color)
{
    const float a = 1.6, d = 0.977;
    const float hdrMax = 8.0, midIn = 0.18, midOut = 0.267;
    float b = (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
              ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    float c = (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
              ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    return pow(color, a) / (pow(color, a * d) * b + c);
}

// Uchimura (Hajime Uchimura, Gran Turismo, CEDEC 2017)
float3 Uchimura(float3 x)
{
    const float P = 1.0, a = 1.0, m = 0.22, l = 0.4, c = 1.33, b = 0.0;
    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;
    float3 w0 = 1.0 - smoothstep(0.0, m, x);
    float3 w2 = step(m + l0, x);
    float3 w1 = 1.0 - w0 - w2;
    float3 T = m * pow(x / m, c) + b;
    float3 S = P - (P - S1) * exp(CP * (x - S0));
    float3 L = m + a * (x - m);
    return T * w0 + L * w1 + S * w2;
}

// Tony McMapface (perceptually balanced simple operator)
float3 TonyMcMapface(float3 color)
{
    float3 encoded = color / (color + 1.0);
    float luma = dot(encoded, float3(0.2126, 0.7152, 0.0722));
    float satFactor = 1.0 - pow(luma, 2.0) * 0.4;
    return lerp(luma.xxx, encoded, satFactor);
}

// ── PQ (ST.2084) encoding for HDR10 ─────────────────────────────────────
// Published: SMPTE ST 2084 (Perceptual Quantizer).
// Maps linear luminance (in normalized units where 1.0 = 10000 nits) to
// the PQ EOTF^-1 nonlinear encoding for HDR10 displays.

float3 LinearToPQ(float3 linearColor)
{
    // PQ constants from SMPTE ST.2084
    static const float m1 = 0.1593017578125;    // 2610 / 16384
    static const float m2 = 78.84375;           // 2523 / 32 * 128
    static const float c1 = 0.8359375;          // 3424 / 4096
    static const float c2 = 18.8515625;         // 2413 / 128 * 32
    static const float c3 = 18.6875;            // 2392 / 128 * 32

    // Input is in normalized linear light [0, 1] where 1.0 = 10000 nits
    float3 Lm1 = pow(max(linearColor, 0.0), m1);
    float3 numerator = c1 + c2 * Lm1;
    float3 denominator = 1.0 + c3 * Lm1;
    return pow(numerator / denominator, m2);
}

// ─────────────────────────────────────────────────────────────────────────

float4 main(VSOut input) : SV_Target
{
    float3 color = SceneColor.Sample(PointSampler, input.uv).rgb;

    // ── Linearize from Skyrim's gamma 1.6 space ─────────────────────
    // Skyrim's internal scene color is stored in a gamma 1.6 encoding.
    // Convert to linear light for tonemapping.
    color = pow(max(color, 0.0), SKYRIM_GAMMA);

    // ── Apply exposure ──────────────────────────────────────────────
    // CurrentEV is log2(avgLuminance / 0.18).
    // Exposure multiplier = 2^(-CurrentEV) = 0.18 / avgLuminance.
    float exposure = exp2(-CurrentEV);
    color *= exposure;

    // ── Apply vanilla cinematic grading (blended by VanillaInfluence) ──
    if (VanillaInfluence > 0.001)
    {
        float3 graded = ApplyVanillaGrade(color);
        color = lerp(color, graded, VanillaInfluence);
    }

    // ── Tone curve ──────────────────────────────────────────────────
    float3 mapped;
    if (CurveType == 0)
    {
        mapped = AgX(color);
    }
    else if (CurveType == 1)
    {
        mapped = ACESFitted(color);
    }
    else if (CurveType == 2)
    {
        // White point at 4.0 gives good highlight rolloff for game content
        mapped = ReinhardExtended(color, 4.0);
    }
    else if (CurveType == 3)
        mapped = Hejl(color);
    else if (CurveType == 4)
        mapped = ACESNarkowicz(color);
    else if (CurveType == 5)
        mapped = AgXPunchy(color);
    else if (CurveType == 6)
        mapped = PBRNeutral(color);
    else if (CurveType == 7)
        mapped = Uncharted2(color);
    else if (CurveType == 8)
        mapped = Lottes(color);
    else if (CurveType == 9)
        mapped = Uchimura(color);
    else if (CurveType == 10)
        mapped = TonyMcMapface(color);
    else
        mapped = saturate(color);  // 11 = Linear / debug passthrough

    // ── Output encoding ─────────────────────────────────────────────
    if (HDROutput)
    {
        // HDR10 PQ output (SMPTE ST.2084)
        // Scale from [0,1] scene-referred to nit-based, normalized to 10000 nits.
        float3 hdrColor = mapped * (PaperWhiteNits / 10000.0);
        hdrColor = min(hdrColor, MaxNits / 10000.0);
        return float4(LinearToPQ(hdrColor), 1.0);
    }
    else
    {
        // SDR: apply sRGB gamma encoding (linear -> 2.2 gamma)
        float3 sdrColor = pow(max(mapped, 0.0), 1.0 / 2.2);
        return float4(sdrColor, 1.0);
    }
}
)HLSL";

// ── CB structures ─────────────────────────────────────────────────────────

struct AutoExpCBData
{
    float prevEV;
    float deltaTime;
    float adaptSpeed;
    float exposureComp;
    float minEV;
    float maxEV;
    float pad[2];
};

struct ToneMapCBData
{
    float currentEV;
    int   curveType;
    int   hdrOutput;
    float paperWhiteNits;
    float maxNits;
    float vanillaInfluence;
    float pad[2];
};

// ── Initialize ────────────────────────────────────────────────────────────

bool ToneMapManager::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;

    m_device = dev;
    m_context = ctx;

    auto& cm = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& pl = RenderPipeline::Get();

    if (!cm.IsInitialized() || !rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("ToneMapManager: prerequisites not initialized");
        return false;
    }

    // Register tone map fullscreen pixel shader
    m_toneMapPass = rpm.RegisterPass({
        .name = "ToneMap",
        .psSource = kToneMapPS,
    });
    if (!m_toneMapPass) {
        SKSE::log::error("ToneMapManager: failed to register ToneMap pass");
        return false;
    }

    // Create tone map CB
    if (!CreateCB(dev, sizeof(ToneMapCBData), &m_toneMapCB)) return false;
    // Create point sampler
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(dev->CreateSamplerState(&sd, &m_pointSampler)))
            return false;
    }

    // Create backbuffer copy texture (for reading as SRV while writing to backbuffer RTV)
    {
        ID3D11Texture2D* backTex = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backTex)))) {
            D3D11_TEXTURE2D_DESC bbDesc;
            backTex->GetDesc(&bbDesc);
            bbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            bbDesc.Usage = D3D11_USAGE_DEFAULT;
            bbDesc.CPUAccessFlags = 0;
            if (FAILED(dev->CreateTexture2D(&bbDesc, nullptr, &m_backbufferCopy))) {
                backTex->Release();
                return false;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = bbDesc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            if (FAILED(dev->CreateShaderResourceView(m_backbufferCopy, &srvDesc, &m_backbufferCopySRV))) {
                backTex->Release();
                return false;
            }
            backTex->Release();
        } else {
            return false;
        }
    }

    // Register as PrePresent pipeline pass (runs after all rendering, before display)
    // Gamma handling corrected: input now linearized from Skyrim's gamma 1.6 space,
    // output encodes to sRGB 2.2 for display.  Still default-disabled (opt-in via DebugGUI).
    m_pipelineHandle = pl.AddPass({
        .name = "ToneMapping",
        .stage = PipelineStage::PreUI,
        .priority = 100,  // Run early in PreUI (before film grain, etc.)
        .enabled = false,
        .execute = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    // Detect HDR from proxy
    m_hdrOutput = D3D11Hook::IsHDREnabled();

    m_initialized = true;
    SKSE::log::info("ToneMapManager: initialized (curve={}, HDR={}, adaptSpeed={})",
        static_cast<int>(m_curve), m_hdrOutput, m_adaptSpeed);
    return true;
}

void ToneMapManager::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    SafeRelease(m_toneMapCB);
    SafeRelease(m_pointSampler);
    SafeRelease(m_backbufferCopy);
    SafeRelease(m_backbufferCopySRV);
    m_initialized = false;
}

// ── Per-frame execution ───────────────────────────────────────────────────

void ToneMapManager::ExecutePass(PassContext& ctx)
{
    if (!m_initialized) return;

    auto& cm = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& hist = LuminanceHistogram::Get();

    // ── Pass 1: Autoexposure (CPU-side from histogram readback) ──────
    // LuminanceHistogram provides CPU-readback stats (1-frame delay).
    // We compute exposure on CPU to avoid an extra compute dispatch.
    if (hist.IsInitialized()) {
        auto& result = hist.GetResult();
        float avgLum = result.avgLuminance;
        float p50    = result.p50;

        // Use geometric mean of average and median for robust metering
        float keyLum = std::sqrt((std::max)(avgLum, 0.001f) * (std::max)(p50, 0.001f));

        // Target EV: keyLum maps to 18% grey
        float targetEV = std::log2((std::max)(keyLum, 0.0001f) / 0.18f);
        targetEV -= m_exposureComp;
        targetEV = std::clamp(targetEV, -4.0f, 16.0f);

        // Temporal adaptation
        float dt = (ctx.deltaTime > 0) ? ctx.deltaTime : 0.016f;
        float speed = std::clamp(m_adaptSpeed * dt, 0.0f, 1.0f);
        m_currentEV = m_currentEV + (targetEV - m_currentEV) * speed;
        m_currentEV = std::clamp(m_currentEV, -20.0f, 20.0f);
    }

    // ── Pass 2: Tone mapping (fullscreen PS) ──────────────────────────
    // Copy scene color → temp texture (so we can read it as SRV while
    // writing back to the scene RTV).
    //
    // Mid-frame (gameSceneRTV set): extract scene texture from the game's
    // active RTV via GetResource + QueryInterface.
    // PrePresent fallback: use swapChain->GetBuffer(0,...).
    {
        ID3D11Texture2D* sceneTex = nullptr;
        ID3D11RenderTargetView* sceneRTV = nullptr;
        bool ownRTV = false;

        if (ctx.gameSceneRTV) {
            // Mid-frame dispatch: game's active scene RT
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                res->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&sceneTex));
                res->Release();
            }
            sceneRTV = ctx.gameSceneRTV;
            // Don't AddRef — D3D11StateBackup keeps it alive during dispatch
        } else {
            // PrePresent fallback: use backbuffer
            auto* sc = ctx.swapChain;
            if (!sc) sc = D3D11Hook::GetSwapChain();
            if (!sc) return;

            if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&sceneTex))))
                return;

            // Create RTV from backbuffer
            D3D11_TEXTURE2D_DESC texDesc;
            sceneTex->GetDesc(&texDesc);
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = texDesc.Format;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            m_device->CreateRenderTargetView(sceneTex, &rtvDesc, &sceneRTV);
            ownRTV = true;
        }

        if (!sceneTex || !sceneRTV) {
            if (sceneTex) sceneTex->Release();
            if (ownRTV && sceneRTV) sceneRTV->Release();
            return;
        }

        // ── Ensure copy texture matches scene RT format/size ────────
        // The backbuffer is R8G8B8A8_UNORM but the game's internal scene
        // RT is often R16G16B16A16_FLOAT.  CopyResource requires identical
        // format+dimensions, so lazily recreate the copy texture if needed.
        {
            D3D11_TEXTURE2D_DESC sceneDesc;
            sceneTex->GetDesc(&sceneDesc);

            D3D11_TEXTURE2D_DESC copyDesc;
            m_backbufferCopy->GetDesc(&copyDesc);

            if (sceneDesc.Format != copyDesc.Format ||
                sceneDesc.Width  != copyDesc.Width  ||
                sceneDesc.Height != copyDesc.Height)
            {
                SKSE::log::info("ToneMapManager: scene RT format/size changed — "
                    "scene={}x{} fmt={}, copy={}x{} fmt={} — recreating",
                    sceneDesc.Width, sceneDesc.Height, static_cast<int>(sceneDesc.Format),
                    copyDesc.Width, copyDesc.Height, static_cast<int>(copyDesc.Format));

                if (m_backbufferCopySRV) { m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr; }
                if (m_backbufferCopy)    { m_backbufferCopy->Release();    m_backbufferCopy    = nullptr; }

                D3D11_TEXTURE2D_DESC newDesc = sceneDesc;
                newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
                newDesc.Usage          = D3D11_USAGE_DEFAULT;
                newDesc.CPUAccessFlags = 0;
                newDesc.MiscFlags      = 0;

                HRESULT hr = m_device->CreateTexture2D(&newDesc, nullptr, &m_backbufferCopy);
                if (FAILED(hr)) {
                    SKSE::log::error("ToneMapManager: failed to recreate copy tex");
                    sceneTex->Release();
                    if (ownRTV) sceneRTV->Release();
                    return;
                }

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                    = newDesc.Format;
                srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels       = 1;
                srvDesc.Texture2D.MostDetailedMip = 0;

                hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc, &m_backbufferCopySRV);
                if (FAILED(hr)) {
                    SKSE::log::error("ToneMapManager: failed to recreate copy SRV");
                    m_backbufferCopy->Release();
                    m_backbufferCopy = nullptr;
                    sceneTex->Release();
                    if (ownRTV) sceneRTV->Release();
                    return;
                }
            }
        }

        ctx.context->CopyResource(m_backbufferCopy, sceneTex);
        sceneTex->Release();

        // Update tone map CB
        ToneMapCBData tmCB;
        tmCB.currentEV      = m_currentEV;
        tmCB.curveType       = static_cast<int>(m_curve);
        tmCB.hdrOutput       = m_hdrOutput ? 1 : 0;
        tmCB.paperWhiteNits  = m_paperWhiteNits;
        tmCB.maxNits            = m_maxNits;
        tmCB.vanillaInfluence   = m_vanillaInfluence;
        tmCB.pad[0] = tmCB.pad[1] = 0;

        UploadCB(ctx.context, m_toneMapCB, &tmCB, sizeof(tmCB));

        // Bind vanilla params CB at PS b7
        auto* vanillaCB = SharedGPUResources::Get().GetVanillaParamsCB();
        ctx.context->PSSetConstantBuffers(SharedGPUResources::kVanillaParamsCBSlot, 1, &vanillaCB);

        // Execute tone map fullscreen pass
        rpm.Execute({
            .passID       = m_toneMapPass,
            .rtv          = sceneRTV,
            .srvs         = &m_backbufferCopySRV,
            .srvCount     = 1,
            .samplers     = &m_pointSampler,
            .samplerCount = 1,
            .cbData       = &tmCB,
            .cbSize       = sizeof(tmCB),
        });

        // Unbind vanilla params CB
        {
            ID3D11Buffer* nullCB = nullptr;
            ctx.context->PSSetConstantBuffers(SharedGPUResources::kVanillaParamsCBSlot, 1, &nullCB);
        }

        if (ownRTV)
            sceneRTV->Release();
    }
}

} // namespace SB
