//=============================================================================
//  TemporalSuperRes.cpp — Temporal super-resolution upscaler
//
//  Pass 1 (Upscale CS):
//    Reads: current frame (render-res), history (display-res), motion vectors,
//           depth buffer, material classification (t25)
//    Writes: display-res R16G16B16A16_FLOAT upscaled output + history update
//
//  Pass 2 (Sharpen PS):
//    Reads: upscaled output
//    Writes: backbuffer with AMD CAS-style contrast-adaptive sharpening
//
//  Pipeline: PrePresent, priority 50 (runs before tone mapping at 100)
//=============================================================================

#include "TemporalSuperRes.h"
#include "ShaderLoader.h"
#include "MotionVectorGen.h"
#include "SceneData.h"
#include "MaterialClassifier.h"
#include "TAAManager.h"
#include "D3D11Hook.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "GPUResource.h"


namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Quality preset helpers
// ═══════════════════════════════════════════════════════════════════════════

const char* TSRQualityName(TSRQuality q)
{
    switch (q) {
        case TSRQuality::Performance: return "Performance (50%)";
        case TSRQuality::Balanced:    return "Balanced (67%)";
        case TSRQuality::Quality:     return "Quality (75%)";
        case TSRQuality::Native:      return "Native (100%)";
        default:                      return "Unknown";
    }
}

float TSRQualityScale(TSRQuality q)
{
    switch (q) {
        case TSRQuality::Performance: return 0.50f;
        case TSRQuality::Balanced:    return 0.67f;
        case TSRQuality::Quality:     return 0.75f;
        case TSRQuality::Native:      return 1.00f;
        default:                      return 0.67f;
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Halton(2,3) 16-sample jitter sequence (precomputed, in [0,1] range)
// ═══════════════════════════════════════════════════════════════════════════

const float TemporalSuperRes::kHaltonX[kJitterSamples] = {
    0.500000f, 0.250000f, 0.750000f, 0.125000f,
    0.625000f, 0.375000f, 0.875000f, 0.062500f,
    0.562500f, 0.312500f, 0.812500f, 0.187500f,
    0.687500f, 0.437500f, 0.937500f, 0.031250f,
};

const float TemporalSuperRes::kHaltonY[kJitterSamples] = {
    0.333333f, 0.666667f, 0.111111f, 0.444444f,
    0.777778f, 0.222222f, 0.555556f, 0.888889f,
    0.037037f, 0.370370f, 0.703704f, 0.148148f,
    0.481481f, 0.814815f, 0.259259f, 0.592593f,
};


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL: Upscale Compute Shader
// ═══════════════════════════════════════════════════════════════════════════

static const char kUpscaleCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Temporal super-resolution upscaler: jittered rendering + temporal accumulation.
// Reprojects history with motion vectors, applies AABB variance clipping,
// blends with bicubic Catmull-Rom history sampling, anti-ghosting via luminance rejection.

cbuffer TSRUpscaleCB : register(b0)
{
    uint2  RenderDims;         // render resolution (lower)
    uint2  DisplayDims;        // display resolution (output)
    float2 JitterOffset;       // current sub-pixel jitter (NDC)
    uint   FrameIndex;         // monotonic frame counter
    float  pad0;
    float4 MaterialHistoryWeight;  // per-material history blend: [default, arch, foliage, skin]
    float2 MotionScale;        // motion vector UV scale
    float2 RcpRenderDims;      // 1.0 / RenderDims
    float2 RcpDisplayDims;     // 1.0 / DisplayDims
    float  RenderToDisplayX;   // DisplayDims.x / RenderDims.x
    float  RenderToDisplayY;   // DisplayDims.y / RenderDims.y
}

Texture2D<float4>   CurrentFrame   : register(t0);  // render-res current frame
Texture2D<float4>   HistoryBuffer  : register(t1);  // display-res previous output
Texture2D<float2>   MotionVectors  : register(t2);  // screen-space motion (UV delta)
Texture2D<float>    DepthBuffer    : register(t3);  // scene depth (0..1)
Texture2D<uint>     MaterialBuffer : register(t4);  // material classification (R8_UINT, t25)
RWTexture2D<float4> OutputColor    : register(u0);  // display-res upscaled result
SamplerState LinearClamp : register(s0);
SamplerState PointClamp  : register(s1);

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Catmull-Rom bicubic filter weight
float CatmullRomWeight(float x)
{
    float ax = abs(x);
    if (ax < 1.0)
        return (1.5 * ax - 2.5) * ax * ax + 1.0;
    else if (ax < 2.0)
        return ((-0.5 * ax + 2.5) * ax - 4.0) * ax + 2.0;
    return 0.0;
}

// Bicubic Catmull-Rom sample from history buffer
float4 SampleHistoryCatmullRom(float2 uv)
{
    float2 texelPos = uv * float2(DisplayDims) - 0.5;
    float2 texelFloor = floor(texelPos);
    float2 f = texelPos - texelFloor;

    float4 result = 0.0;
    float  totalWeight = 0.0;

    [unroll]
    for (int y = -1; y <= 2; y++)
    {
        [unroll]
        for (int x = -1; x <= 2; x++)
        {
            int2 coord = int2(texelFloor) + int2(x, y);
            coord = clamp(coord, int2(0, 0), int2(DisplayDims) - 1);

            float wx = CatmullRomWeight(float(x) - f.x);
            float wy = CatmullRomWeight(float(y) - f.y);
            float w = wx * wy;

            result += HistoryBuffer.Load(int3(coord, 0)) * w;
            totalWeight += w;
        }
    }

    return result / max(totalWeight, 1e-6);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= DisplayDims.x || dtid.y >= DisplayDims.y)
        return;

    // Display-res UV
    float2 displayUV = (float2(dtid.xy) + 0.5) * RcpDisplayDims;

    // ── Sample current frame at render resolution ───────────────────
    // Map display UV to render-res UV (accounting for jitter)
    float2 renderUV = displayUV - JitterOffset * RcpRenderDims;
    float4 currentColor = CurrentFrame.SampleLevel(LinearClamp, renderUV, 0);

    // ── Reproject: where was this pixel in the previous frame? ──────
    float2 motionVec = MotionVectors.SampleLevel(LinearClamp, displayUV, 0);
    float2 historyUV = displayUV - motionVec * MotionScale;

    // ── Sample history with bicubic Catmull-Rom ─────────────────────
    float4 historyColor;
    bool historyValid = true;

    if (any(historyUV < 0.0) || any(historyUV > 1.0))
    {
        // Out of bounds: no valid history
        historyValid = false;
        historyColor = currentColor;
    }
    else
    {
        historyColor = SampleHistoryCatmullRom(historyUV);
    }

    // ── Neighborhood clipping (AABB variance clip) ──────────────────
    // Gather a 3x3 neighborhood from the current frame to compute color AABB
    float3 neighborMin = float3(1e10, 1e10, 1e10);
    float3 neighborMax = float3(-1e10, -1e10, -1e10);
    float3 neighborMean = 0.0;
    float3 neighborM2 = 0.0;

    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            float2 sampleUV = displayUV + float2(dx, dy) * RcpRenderDims;
            float3 s = CurrentFrame.SampleLevel(PointClamp, sampleUV, 0).rgb;

            neighborMin = min(neighborMin, s);
            neighborMax = max(neighborMax, s);
            neighborMean += s;
            neighborM2 += s * s;
        }
    }

    neighborMean /= 9.0;
    neighborM2 /= 9.0;
    float3 variance = max(neighborM2 - neighborMean * neighborMean, 0.0);
    float3 sigma = sqrt(variance);

    // Variance-based AABB clip: tighter than min/max, reduces ghosting
    float3 clipMin = neighborMean - sigma * 1.25;
    float3 clipMax = neighborMean + sigma * 1.25;

    // Clip history to the AABB
    float3 clippedHistory = clamp(historyColor.rgb, clipMin, clipMax);

    // ── Anti-ghosting: luminance-based rejection ────────────────────
    float lumCurrent = Luminance(currentColor.rgb);
    float lumHistory = Luminance(clippedHistory);
    float lumDiff = abs(lumCurrent - lumHistory);

    // Large luminance difference indicates ghosting — reduce history weight
    float ghostRejection = saturate(lumDiff * 4.0);

    // ── Material-aware history weight ───────────────────────────────
    float historyWeight = MaterialHistoryWeight.x; // Default blend
    uint materialID = MaterialBuffer.Load(int3(
        int2(renderUV * float2(RenderDims)), 0));

    // Select per-material weight (0=default, 1=architecture, 2=foliage, 3=skin)
    if (materialID == 1) historyWeight = MaterialHistoryWeight.y;
    else if (materialID == 2) historyWeight = MaterialHistoryWeight.z;
    else if (materialID == 3) historyWeight = MaterialHistoryWeight.w;

    // Reduce history contribution when ghosting is detected
    historyWeight = lerp(historyWeight, 0.0, ghostRejection);

    // First frame or invalid history: use current frame directly
    if (!historyValid || FrameIndex == 0)
        historyWeight = 0.0;

    // ── Blend current + clipped history ─────────────────────────────
    float3 result = lerp(currentColor.rgb, clippedHistory, historyWeight);

    OutputColor[dtid.xy] = float4(result, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL: Sharpen Pixel Shader (AMD CAS-style)
// ═══════════════════════════════════════════════════════════════════════════

static const char kSharpenPS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// RCAS (Robust Contrast-Adaptive Sharpening): 5-tap cross pattern
// that avoids ringing artifacts. Based on AMD FidelityFX CAS concepts.

cbuffer TSRSharpenCB : register(b0)
{
    float  Sharpness;              // [0..1] global sharpness control
    uint2  DisplayDims;            // display resolution
    float  pad0;
    float4 MaterialSharpenWeight;  // per-material sharpen: [default, arch, foliage, skin]
    float2 RcpDisplayDims;         // 1.0 / DisplayDims
    float2 pad1;
}

Texture2D<float4> UpscaledColor  : register(t0);
Texture2D<uint>   MaterialBuffer : register(t1);
SamplerState PointSampler        : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 main(VSOut input) : SV_Target
{
    // Integer pixel coordinate
    int2 pos = int2(input.pos.xy);

    // 5-tap cross pattern: center + north + south + east + west
    float3 center = UpscaledColor.Load(int3(pos, 0)).rgb;
    float3 north  = UpscaledColor.Load(int3(pos + int2( 0, -1), 0)).rgb;
    float3 south  = UpscaledColor.Load(int3(pos + int2( 0,  1), 0)).rgb;
    float3 east   = UpscaledColor.Load(int3(pos + int2( 1,  0), 0)).rgb;
    float3 west   = UpscaledColor.Load(int3(pos + int2(-1,  0), 0)).rgb;

    // Per-channel min/max of the cross neighborhood
    float3 minRGB = min(min(north, south), min(east, west));
    float3 maxRGB = max(max(north, south), max(east, west));

    // Soft minimum and maximum (includes center)
    minRGB = min(minRGB, center);
    maxRGB = max(maxRGB, center);

    // Compute the RCAS sharpening amount
    // This is the reciprocal of the local contrast, clamped to avoid ringing
    float3 ampRGB = saturate(min(minRGB, 2.0 - maxRGB) / max(maxRGB, 1e-5));

    // Use the minimum channel amplitude to prevent color shifts
    float amp = min(min(ampRGB.r, ampRGB.g), ampRGB.b);

    // Scale by user sharpness control
    // Negative weight sharpens, clamped to [-0.5, 0] to avoid ringing
    float sharpWeight = -amp * Sharpness;
    sharpWeight = max(sharpWeight, -0.5); // Clamp to prevent ringing

    // Material-aware sharpness
    uint materialID = MaterialBuffer.Load(int3(pos, 0));
    float materialSharp = MaterialSharpenWeight.x; // default
    if (materialID == 1) materialSharp = MaterialSharpenWeight.y; // architecture
    else if (materialID == 2) materialSharp = MaterialSharpenWeight.z; // foliage
    else if (materialID == 3) materialSharp = MaterialSharpenWeight.w; // skin

    sharpWeight *= materialSharp;

    // Apply sharpening: weighted sum of neighbors vs center
    // result = (center + weight * (N + S + E + W)) / (1 + 4 * weight)
    float3 result = (center + sharpWeight * (north + south + east + west))
                  / (1.0 + 4.0 * sharpWeight);

    // Clamp to prevent negative values
    result = max(result, 0.0);

    return float4(result, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB layout structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) TSRUpscaleCBData
{
    uint32_t renderW;
    uint32_t renderH;
    uint32_t displayW;
    uint32_t displayH;
    float    jitterX;
    float    jitterY;
    uint32_t frameIndex;
    float    pad0;
    float    materialHistoryWeight[4];  // float4
    float    motionScaleX;
    float    motionScaleY;
    float    rcpRenderW;
    float    rcpRenderH;
    float    rcpDisplayW;
    float    rcpDisplayH;
    float    renderToDisplayX;
    float    renderToDisplayY;
};
static_assert(sizeof(TSRUpscaleCBData) == 80, "TSRUpscaleCBData must be 80 bytes (5 float4)");

struct alignas(16) TSRSharpenCBData
{
    float    sharpness;
    uint32_t displayW;
    uint32_t displayH;
    float    pad0;
    float    materialSharpenWeight[4];  // float4
    float    rcpDisplayW;
    float    rcpDisplayH;
    float    pad1[2];
};
static_assert(sizeof(TSRSharpenCBData) == 48, "TSRSharpenCBData alignment check");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool TemporalSuperRes::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!cm.IsInitialized() || !rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("TemporalSuperRes: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // ── Get display resolution from backbuffer ───────────────────────
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backbuffer));
    if (FAILED(hr) || !backbuffer) {
        SKSE::log::error("TemporalSuperRes: failed to get backbuffer (0x{:X})",
                          static_cast<uint32_t>(hr));
        return false;
    }
    D3D11_TEXTURE2D_DESC bbDesc;
    backbuffer->GetDesc(&bbDesc);
    backbuffer->Release();

    m_displayW = bbDesc.Width;
    m_displayH = bbDesc.Height;
    m_renderW  = static_cast<uint32_t>(m_displayW * m_renderScale);
    m_renderH  = static_cast<uint32_t>(m_displayH * m_renderScale);

    // Ensure render dims are at least 1 and even (avoids shader edge issues)
    m_renderW = (std::max)(m_renderW & ~1u, 2u);
    m_renderH = (std::max)(m_renderH & ~1u, 2u);

    // ── Compile shaders ──────────────────────────────────────────────
    if (!CompileUpscaleCS(dev)) return false;
    if (!CompileSharpenPS())    return false;

    // ── Create GPU resources ─────────────────────────────────────────
    if (!CreateResources(dev))  return false;
    if (!CreateSamplers(dev))   return false;

    // ── Register pipeline pass (PrePresent, priority 50) ─────────────
    m_pipelineHandle = pl.AddPass({
        .name     = "TemporalSuperRes",
        .stage    = PipelineStage::PreUI,
        .priority = 50,  // Before tone mapping (100)
        .execute  = [this](PassContext& pctx) { Execute(pctx); },
    });

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("TemporalSuperRes: initialized (display={}x{}, render={}x{}, scale={:.0f}%, quality={})",
                    m_displayW, m_displayH, m_renderW, m_renderH,
                    m_renderScale * 100.0f, TSRQualityName(m_quality));
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile upscale compute shader
// ═══════════════════════════════════════════════════════════════════════════

bool TemporalSuperRes::CompileUpscaleCS(ID3D11Device* dev)
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = ShaderLoader::Compile("TSR_Upscale", kUpscaleCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("TemporalSuperRes: Upscale CS compile failed");
        return false;
    }

    HRESULT hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                  nullptr, &m_upscaleCS);
    blob->Release();
    if (FAILED(hr)) {
        SKSE::log::error("TemporalSuperRes: CreateComputeShader failed (0x{:X})",
                          static_cast<uint32_t>(hr));
        return false;
    }
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile sharpen pixel shader (via RenderPassManager)
// ═══════════════════════════════════════════════════════════════════════════

bool TemporalSuperRes::CompileSharpenPS()
{
    auto& rpm = RenderPassManager::Get();
    m_sharpenPass = rpm.RegisterPass({
        .name     = "TSRSharpen",
        .psSource = kSharpenPS,
    });
    if (!m_sharpenPass) {
        SKSE::log::error("TemporalSuperRes: failed to register sharpen pass");
        return false;
    }
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool TemporalSuperRes::CreateResources(ID3D11Device* dev)
{
    HRESULT hr;

    // ── History textures (2x display-res, R16G16B16A16_FLOAT, ping-pong) ──
    for (int i = 0; i < 2; ++i) {
        char name[32]; snprintf(name, sizeof(name), "tsrHistory%d", i);
        if (!CreateGPUTexture(dev, m_displayW, m_displayH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                              &m_historyTex[i], &m_historySRV[i], &m_historyUAV[i], name)) {
            SKSE::log::error("TemporalSuperRes: failed to create history tex {}", i);
            ReleaseResources();
            return false;
        }
    }

    // ── Render-resolution input copy (SRV only) ──────────────────────────
    if (!CreateGPUTexture(dev, m_renderW, m_renderH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                          &m_renderInputTex, &m_renderInputSRV, nullptr, "tsrRenderInput")) {
        SKSE::log::error("TemporalSuperRes: failed to create render input tex");
        ReleaseResources();
        return false;
    }

    // ── Upscale output (display-res, written by CS, read by sharpen PS) ──
    if (!CreateGPUTexture(dev, m_displayW, m_displayH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                          &m_upscaleOutputTex, &m_upscaleOutputSRV, &m_upscaleOutputUAV, "tsrUpscaleOut")) {
        SKSE::log::error("TemporalSuperRes: failed to create upscale output tex");
        ReleaseResources();
        return false;
    }

    // ── Constant buffers ─────────────────────────────────────────────
    if (!CreateCB(dev, sizeof(TSRUpscaleCBData), &m_upscaleCB)) {
        SKSE::log::error("TemporalSuperRes: failed to create upscale CB");
        ReleaseResources();
        return false;
    }
    if (!CreateCB(dev, sizeof(TSRSharpenCBData), &m_sharpenCB)) {
        SKSE::log::error("TemporalSuperRes: failed to create sharpen CB");
        ReleaseResources();
        return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create samplers
// ═══════════════════════════════════════════════════════════════════════════

bool TemporalSuperRes::CreateSamplers(ID3D11Device* dev)
{
    // Linear clamp (for history bilinear reads)
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &m_linearClampSampler))) {
            SKSE::log::error("TemporalSuperRes: failed to create linear sampler");
            return false;
        }
    }

    // Point clamp (for material buffer / exact texel reads)
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &m_pointClampSampler))) {
            SKSE::log::error("TemporalSuperRes: failed to create point sampler");
            return false;
        }
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV from currently-bound DSV (same pattern as TAAManager)
// ═══════════════════════════════════════════════════════════════════════════

bool TemporalSuperRes::AcquireDepthSRV(ID3D11DeviceContext* ctx)
{
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(0, nullptr, &dsv);
    if (!dsv) return false;

    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    dsv->Release();
    if (!res) return false;

    ID3D11Texture2D* depthTex = nullptr;
    HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&depthTex));
    res->Release();
    if (FAILED(hr) || !depthTex) return false;

    D3D11_TEXTURE2D_DESC desc;
    depthTex->GetDesc(&desc);

    if (!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        depthTex->Release();
        return false;
    }

    DXGI_FORMAT srvFormat;
    switch (desc.Format) {
        case DXGI_FORMAT_R32_TYPELESS:       srvFormat = DXGI_FORMAT_R32_FLOAT;               break;
        case DXGI_FORMAT_R24G8_TYPELESS:     srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;   break;
        case DXGI_FORMAT_R32G8X24_TYPELESS:  srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
        case DXGI_FORMAT_R16_TYPELESS:       srvFormat = DXGI_FORMAT_R16_UNORM;               break;
        case DXGI_FORMAT_R32_FLOAT:          srvFormat = DXGI_FORMAT_R32_FLOAT;               break;
        case DXGI_FORMAT_R16_UNORM:          srvFormat = DXGI_FORMAT_R16_UNORM;               break;
        default:
            depthTex->Release();
            return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = srvFormat;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    ID3D11Device* d = nullptr;
    ctx->GetDevice(&d);
    hr = d->CreateShaderResourceView(depthTex, &srvDesc, &m_depthSRV);
    d->Release();
    depthTex->Release();

    return SUCCEEDED(hr) && m_depthSRV != nullptr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire motion vectors SRV
//  Skyrim's built-in TAA writes motion vectors when jitter is active.
//  We try to find them from the currently bound RTs.  If not available,
//  we fall back to jitter-only reprojection (same as TAAManager).
// ═══════════════════════════════════════════════════════════════════════════

bool TemporalSuperRes::AcquireMotionVectorsSRV(ID3D11DeviceContext* ctx)
{
    if (m_motionSRV) {
        m_motionSRV->Release();
        m_motionSRV = nullptr;
    }

    auto& mvGen = MotionVectorGen::Get();
    if (!mvGen.IsInitialized() || !m_depthSRV)
        return false;

    // Dispatch depth-reprojection motion vector generation
    auto& scene = SceneMatrices::Get();
    mvGen.Dispatch(ctx, m_depthSRV,
                   scene.InvViewProjMatrix(),
                   scene.PrevViewProjMatrix());

    // Acquire the output SRV (AddRef since we Release it each frame)
    m_motionSRV = mvGen.GetMotionSRV();
    if (m_motionSRV) {
        m_motionSRV->AddRef();
        return true;
    }
    return false;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Recreate render-resolution target (for dynamic resolution changes)
// ═══════════════════════════════════════════════════════════════════════════

bool TemporalSuperRes::RecreateRenderTarget(ID3D11Device* dev, uint32_t w, uint32_t h)
{
    if (m_renderInputSRV) { m_renderInputSRV->Release(); m_renderInputSRV = nullptr; }
    if (m_renderInputTex) { m_renderInputTex->Release(); m_renderInputTex = nullptr; }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width      = w;
    texDesc.Height     = h;
    texDesc.MipLevels  = 1;
    texDesc.ArraySize  = 1;
    texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc = {1, 0};
    texDesc.Usage      = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = dev->CreateTexture2D(&texDesc, nullptr, &m_renderInputTex);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = texDesc.Format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;
    hr = dev->CreateShaderResourceView(m_renderInputTex, &srvDesc, &m_renderInputSRV);
    if (FAILED(hr)) return false;

    m_renderW = w;
    m_renderH = h;
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Dynamic resolution — adjust render scale to hit frame time target
// ═══════════════════════════════════════════════════════════════════════════

void TemporalSuperRes::UpdateDynamicResolution(float deltaTime)
{
    if (!m_dynamicRes || m_quality == TSRQuality::Native) return;

    float frameTimeMs = deltaTime * 1000.0f;
    m_frameTimeAccum += frameTimeMs;
    m_frameTimeSamples++;

    // Update every 30 frames for stability
    if (m_frameTimeSamples < 30) return;

    float avgFrameTime = m_frameTimeAccum / static_cast<float>(m_frameTimeSamples);
    m_frameTimeAccum   = 0.0f;
    m_frameTimeSamples = 0;

    // Adjust render scale based on frame time vs target
    float ratio = m_targetFrameTimeMs / (std::max)(avgFrameTime, 1.0f);

    // Scale adjustment: if we're over budget, reduce; under budget, increase
    // Use square root for less aggressive scaling (area scales quadratically)
    float scaleAdj = std::sqrt(ratio);
    float newScale = m_renderScale * scaleAdj;

    // Clamp to quality preset bounds
    float minScale = TSRQualityScale(TSRQuality::Performance);  // 0.50
    float maxScale = TSRQualityScale(m_quality);                 // preset max

    newScale = std::clamp(newScale, minScale, maxScale);

    // Only adjust if change is significant (>2%)
    if (std::abs(newScale - m_renderScale) > 0.02f) {
        m_renderScale = newScale;
        uint32_t newW = (std::max)(static_cast<uint32_t>(m_displayW * m_renderScale) & ~1u, 2u);
        uint32_t newH = (std::max)(static_cast<uint32_t>(m_displayH * m_renderScale) & ~1u, 2u);

        if (newW != m_renderW || newH != m_renderH) {
            if (RecreateRenderTarget(m_device, newW, newH)) {
                SKSE::log::info("TemporalSuperRes: dynamic res adjusted to {}x{} ({:.0f}%)",
                                newW, newH, m_renderScale * 100.0f);
            }
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Execute — per-frame upscale + sharpen
// ═══════════════════════════════════════════════════════════════════════════

void TemporalSuperRes::Execute(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();

    // ── Dynamic resolution adjustment ────────────────────────────────
    UpdateDynamicResolution(ctx.deltaTime);

    // ── Compute jitter for this frame (Halton 2,3) ───────────────────
    uint32_t jIdx = m_frameIndex % kJitterSamples;
    float jitterPixelX = (kHaltonX[jIdx] - 0.5f);  // [-0.5, 0.5] pixel
    float jitterPixelY = (kHaltonY[jIdx] - 0.5f);
    // Convert to NDC offset (per-pixel in render resolution)
    float jitterNdcX = jitterPixelX / static_cast<float>(m_renderW);
    float jitterNdcY = jitterPixelY / static_cast<float>(m_renderH);
    m_jitterX = jitterNdcX;
    m_jitterY = jitterNdcY;

    // ── Get scene texture ────────────────────────────────────────────
    // During mid-frame dispatch, use the game's active scene RT (R16G16B16A16_FLOAT).
    // At Present-time, fall back to the swapchain backbuffer.
    ID3D11Texture2D* sceneTex = nullptr;
    bool ownSceneTex = false;

    if (ctx.gameSceneRTV) {
        // Mid-frame: extract texture from game's active RTV
        ID3D11Resource* res = nullptr;
        ctx.gameSceneRTV->GetResource(&res);
        if (res) {
            res->QueryInterface(__uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&sceneTex));
            res->Release();
        }
        ownSceneTex = (sceneTex != nullptr);
    }

    if (!sceneTex) {
        // Present-time fallback: swapchain backbuffer
        auto* sc = D3D11Hook::GetSwapChain();
        if (!sc) return;
        HRESULT hr2 = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&sceneTex));
        if (FAILED(hr2) || !sceneTex) return;
        ownSceneTex = true;
    }

    // ── Native mode: passthrough (no upscaling) ──────────────────────
    if (m_quality == TSRQuality::Native) {
        if (ownSceneTex) sceneTex->Release();
        ++m_frameIndex;
        return;
    }

    // Check format compatibility for CopyResource/CopySubresourceRegion
    D3D11_TEXTURE2D_DESC sceneDesc;
    sceneTex->GetDesc(&sceneDesc);

    // ── First frame: seed history ────────────────────────────────────
    if (m_firstFrame) {
        // History is R16G16B16A16_FLOAT; only seed if scene format matches
        if (sceneDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
            ctx.context->CopyResource(m_historyTex[0], sceneTex);
            ctx.context->CopyResource(m_historyTex[1], sceneTex);
        }
        // If format doesn't match, history stays zeroed (acceptable for first frame)
        m_firstFrame = false;
        m_writeIdx = 0;
        if (ownSceneTex) sceneTex->Release();
        ++m_frameIndex;
        return;
    }

    // ── Copy scene to render-resolution input ────────────────────────
    // m_renderInputTex is R16G16B16A16_FLOAT. CopySubresourceRegion
    // requires matching formats.
    HRESULT hr;
    if (sceneDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        // Format matches — direct copy
        D3D11_BOX srcBox = {};
        srcBox.left   = 0;
        srcBox.top    = 0;
        srcBox.front  = 0;
        srcBox.right  = (std::min)(m_renderW, sceneDesc.Width);
        srcBox.bottom = (std::min)(m_renderH, sceneDesc.Height);
        srcBox.back   = 1;
        ctx.context->CopySubresourceRegion(m_renderInputTex, 0, 0, 0, 0,
                                            sceneTex, 0, &srcBox);
    } else {
        // Format mismatch (e.g., R8G8B8A8_UNORM backbuffer at Present-time).
        // TSR requires HDR input; skip this frame's upscale but still increment.
        static int s_fmtWarnCount = 0;
        if (s_fmtWarnCount++ < 5) {
            SKSE::log::warn("TSR: scene format {} != R16G16B16A16_FLOAT — skipping upscale "
                "(mid-frame dispatch provides matching format)",
                static_cast<int>(sceneDesc.Format));
        }
        if (ownSceneTex) sceneTex->Release();
        ++m_frameIndex;
        return;
    }

    // ── Acquire depth and motion vector SRVs ─────────────────────────
    AcquireDepthSRV(ctx.context);
    AcquireMotionVectorsSRV(ctx.context);

    // Get material classification SRV
    auto& matClass = MaterialClassifier::Get();
    ID3D11ShaderResourceView* materialSRV = matClass.IsInitialized() ? matClass.GetMaterialSRV() : nullptr;

    // ── Update upscale CB ────────────────────────────────────────────
    int readIdx  = 1 - m_writeIdx;
    int writeIdx = m_writeIdx;

    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = ctx.context->Map(m_upscaleCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            TSRUpscaleCBData cb;
            cb.renderW  = m_renderW;
            cb.renderH  = m_renderH;
            cb.displayW = m_displayW;
            cb.displayH = m_displayH;
            cb.jitterX  = m_jitterX;
            cb.jitterY  = m_jitterY;
            cb.frameIndex = m_frameIndex;
            cb.pad0     = 0.0f;
            std::memcpy(cb.materialHistoryWeight, m_materialHistoryWeight, sizeof(float) * 4);
            cb.motionScaleX   = m_motionScaleX;
            cb.motionScaleY   = m_motionScaleY;
            cb.rcpRenderW     = 1.0f / static_cast<float>(m_renderW);
            cb.rcpRenderH     = 1.0f / static_cast<float>(m_renderH);
            cb.rcpDisplayW    = 1.0f / static_cast<float>(m_displayW);
            cb.rcpDisplayH    = 1.0f / static_cast<float>(m_displayH);
            cb.renderToDisplayX = static_cast<float>(m_displayW) / static_cast<float>(m_renderW);
            cb.renderToDisplayY = static_cast<float>(m_displayH) / static_cast<float>(m_renderH);
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_upscaleCB, 0);
        }
    }

    // ═════════════════════════════════════════════════════════════════
    //  Pass 1: Upscale CS
    // ═════════════════════════════════════════════════════════════════

    cm.SaveCSState();

    ctx.context->CSSetShader(m_upscaleCS, nullptr, 0);

    // Bind SRVs: t0=current frame, t1=history[read], t2=motion vectors, t3=depth, t4=material
    // Create a null SRV for motion vectors if not available
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11ShaderResourceView* csSRVs[5] = {
        m_renderInputSRV,         // t0: current frame (render-res)
        m_historySRV[readIdx],    // t1: history (display-res)
        m_motionSRV ? m_motionSRV : nullSRV,  // t2: motion vectors (null if unavailable)
        m_depthSRV,               // t3: depth
        materialSRV,              // t4: material classification
    };
    cm.CSSetSRVs(0, 5, csSRVs);

    // Bind UAV: u0 = upscale output (also written to history)
    ID3D11UnorderedAccessView* csUAVs[1] = { m_upscaleOutputUAV };
    cm.CSSetUAVs(0, 1, csUAVs);

    // Bind CB and samplers
    cm.CSSetCBs(0, 1, &m_upscaleCB);
    ID3D11SamplerState* csSamplers[2] = { m_linearClampSampler, m_pointClampSampler };
    cm.CSSetSamplers(0, 2, csSamplers);

    // Dispatch at display resolution (output-res)
    UINT groupsX = (m_displayW + 7) / 8;
    UINT groupsY = (m_displayH + 7) / 8;
    ctx.context->Dispatch(groupsX, groupsY, 1);

    // Unbind
    cm.CSClearSRVs(0, 5);
    cm.CSClearUAVs(0, 1);

    cm.RestoreCSState();

    // Copy upscale output to history[write] for next frame
    ctx.context->CopyResource(m_historyTex[writeIdx], m_upscaleOutputTex);

    // ═════════════════════════════════════════════════════════════════
    //  Pass 2: Sharpen PS (fullscreen via RenderPassManager)
    // ═════════════════════════════════════════════════════════════════

    // Get output RTV — use game scene RTV during mid-frame, or create from sceneTex
    ID3D11RenderTargetView* backRTV = nullptr;
    bool ownBackRTV = false;
    if (ctx.gameSceneRTV) {
        backRTV = ctx.gameSceneRTV;
        backRTV->AddRef();
        ownBackRTV = true;
    } else {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = sceneDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        hr = m_device->CreateRenderTargetView(sceneTex, &rtvDesc, &backRTV);
        ownBackRTV = (backRTV != nullptr);
    }

    if (backRTV) {
        // Prepare sharpen CB data
        TSRSharpenCBData sharpenCBData;
        sharpenCBData.sharpness = m_sharpness;
        sharpenCBData.displayW  = m_displayW;
        sharpenCBData.displayH  = m_displayH;
        sharpenCBData.pad0      = 0.0f;
        std::memcpy(sharpenCBData.materialSharpenWeight, m_materialSharpenWeight, sizeof(float) * 4);
        sharpenCBData.rcpDisplayW = 1.0f / static_cast<float>(m_displayW);
        sharpenCBData.rcpDisplayH = 1.0f / static_cast<float>(m_displayH);
        sharpenCBData.pad1[0] = sharpenCBData.pad1[1] = 0.0f;

        // SRVs: t0 = upscaled color, t1 = material buffer
        ID3D11ShaderResourceView* psSRVs[2] = {
            m_upscaleOutputSRV,
            materialSRV,
        };

        rpm.Execute({
            .passID       = m_sharpenPass,
            .rtv          = backRTV,
            .srvs         = psSRVs,
            .srvCount     = 2,
            .samplers     = &m_pointClampSampler,
            .samplerCount = 1,
            .cbData       = &sharpenCBData,
            .cbSize       = sizeof(sharpenCBData),
        });

        if (ownBackRTV) backRTV->Release();
    }

    // ── Cleanup ──────────────────────────────────────────────────────
    if (ownSceneTex && sceneTex) sceneTex->Release();
    if (m_depthSRV)  { m_depthSRV->Release();  m_depthSRV  = nullptr; }
    if (m_motionSRV) { m_motionSRV->Release(); m_motionSRV = nullptr; }

    // ── Swap ping-pong ───────────────────────────────────────────────
    m_writeIdx = 1 - m_writeIdx;
    ++m_frameIndex;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown + Release
// ═══════════════════════════════════════════════════════════════════════════

void TemporalSuperRes::ReleaseResources()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    for (int i = 0; i < 2; ++i) {
        SafeRelease(m_historyUAV[i]);
        SafeRelease(m_historySRV[i]);
        SafeRelease(m_historyTex[i]);
    }

    SafeRelease(m_renderInputSRV);
    SafeRelease(m_renderInputTex);

    SafeRelease(m_upscaleOutputUAV);
    SafeRelease(m_upscaleOutputSRV);
    SafeRelease(m_upscaleOutputTex);

    SafeRelease(m_upscaleCB);
    SafeRelease(m_sharpenCB);
    SafeRelease(m_upscaleCS);

    SafeRelease(m_linearClampSampler);
    SafeRelease(m_pointClampSampler);

    SafeRelease(m_depthSRV);
    SafeRelease(m_motionSRV);
}

void TemporalSuperRes::Shutdown()
{
    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();

    m_initialized = false;
    m_device  = nullptr;
    m_context = nullptr;
    m_displayW = m_displayH = 0;
    m_renderW  = m_renderH  = 0;
    m_frameIndex = 0;
    m_writeIdx = 0;
    m_firstFrame = true;

    SKSE::log::info("TemporalSuperRes: shutdown");
}

} // namespace SB
