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
//-----------------------------------------------------------------------------
// TSR Upscale CS — Temporal accumulation upscaler
// Threads: 8x8, dispatched at display resolution
//-----------------------------------------------------------------------------

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
};

// Inputs
Texture2D<float4>   CurrentFrame   : register(t0);  // render-res current frame
Texture2D<float4>   HistoryBuffer  : register(t1);  // display-res previous output
Texture2D<float2>   MotionVectors  : register(t2);  // screen-space motion (UV delta)
Texture2D<float>    DepthBuffer    : register(t3);  // scene depth (0..1)
Texture2D<uint>     MaterialBuffer : register(t4);  // material classification (R8_UINT, t25)

// Output
RWTexture2D<float4> OutputColor    : register(u0);  // display-res upscaled result

// Samplers
SamplerState LinearClamp : register(s0);
SamplerState PointClamp  : register(s1);

// ── Utilities ────────────────────────────────────────────────────────────

float Luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 ToneMap(float3 c)    { return c / (1.0 + Luminance(c)); }
float3 InvToneMap(float3 c) { return c / max(1.0 - Luminance(c), 1e-6); }

// Lanczos-2 kernel (1D)
float Lanczos2(float x)
{
    if (abs(x) < 1e-6) return 1.0;
    if (abs(x) >= 2.0) return 0.0;
    float pi_x = 3.14159265 * x;
    return (sin(pi_x) / pi_x) * (sin(pi_x * 0.5) / (pi_x * 0.5));
}

// Sample current frame with Lanczos-2 reconstruction
// srcUV is in render-resolution UV space
float3 SampleLanczos2(float2 srcUV)
{
    float2 srcPixel = srcUV * float2(RenderDims) - 0.5;
    int2 baseCoord = int2(floor(srcPixel));
    float2 frac_ = srcPixel - float2(baseCoord);

    float3 result = 0;
    float totalWeight = 0;

    [unroll]
    for (int dy = -1; dy <= 2; dy++)
    {
        float wy = Lanczos2(float(dy) - frac_.y);
        [unroll]
        for (int dx = -1; dx <= 2; dx++)
        {
            float wx = Lanczos2(float(dx) - frac_.x);
            float w = wx * wy;

            int2 sc = clamp(baseCoord + int2(dx, dy), 0, int2(RenderDims) - 1);
            float3 s = CurrentFrame.Load(int3(sc, 0)).rgb;

            result += s * w;
            totalWeight += w;
        }
    }

    return result / max(totalWeight, 1e-6);
}

// Get history weight for a given material ID
float GetMaterialHistoryWeight(uint matID)
{
    // Map material IDs to weight indices:
    //   0 = default (unknown, terrain, etc.)
    //   1 = architecture (stone=3, metal=2, glass=7, wood=9)
    //   2 = foliage (foliage=4, water=5)
    //   3 = skin (skin=1, fabric=8)
    if (matID == 3 || matID == 2 || matID == 7 || matID == 9)
        return MaterialHistoryWeight.y;  // architecture — high history weight
    if (matID == 4 || matID == 5)
        return MaterialHistoryWeight.z;  // foliage — low history weight
    if (matID == 1 || matID == 8)
        return MaterialHistoryWeight.w;  // skin — moderate
    return MaterialHistoryWeight.x;      // default
}

// ── Main ─────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= DisplayDims.x || dtid.y >= DisplayDims.y)
        return;

    int2 outCoord = int2(dtid.xy);
    float2 outUV = (float2(outCoord) + 0.5) * RcpDisplayDims;

    // ── Map display coord to render-res UV ───────────────────────────
    // Account for jitter offset
    float2 srcUV = outUV - JitterOffset;

    // ── Sample current frame via Lanczos-2 ───────────────────────────
    float3 current = SampleLanczos2(srcUV);

    // ── Read motion vectors at render resolution ─────────────────────
    // Convert display UV to render-res coord for MV lookup
    int2 renderCoord = clamp(int2(srcUV * float2(RenderDims)), 0, int2(RenderDims) - 1);
    float2 motion = MotionVectors.Load(int3(renderCoord, 0)).xy * MotionScale;

    // ── Reproject history ────────────────────────────────────────────
    float2 historyUV = outUV - motion;
    historyUV = clamp(historyUV, RcpDisplayDims * 0.5, 1.0 - RcpDisplayDims * 0.5);
    float3 history = HistoryBuffer.SampleLevel(LinearClamp, historyUV, 0).rgb;

    // ── 3x3 neighbourhood min/max in current frame (tonemapped space) ─
    float3 nMin = float3(1e6, 1e6, 1e6);
    float3 nMax = float3(-1e6, -1e6, -1e6);
    float3 nMean = 0;
    float3 nM2 = 0;

    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            // Sample from render-res neighbourhood
            int2 sc = clamp(renderCoord + int2(dx, dy), 0, int2(RenderDims) - 1);
            float3 s = ToneMap(CurrentFrame.Load(int3(sc, 0)).rgb);
            nMin = min(nMin, s);
            nMax = max(nMax, s);
            nMean += s;
        }
    }
    nMean /= 9.0;

    // Variance clip (tighter AABB using stddev)
    [unroll]
    for (int dy2 = -1; dy2 <= 1; dy2++)
    {
        [unroll]
        for (int dx2 = -1; dx2 <= 1; dx2++)
        {
            int2 sc = clamp(renderCoord + int2(dx2, dy2), 0, int2(RenderDims) - 1);
            float3 s = ToneMap(CurrentFrame.Load(int3(sc, 0)).rgb);
            float3 diff = s - nMean;
            nM2 += diff * diff;
        }
    }
    float3 stddev = sqrt(nM2 / 9.0);
    float gamma = 1.0;  // Tighter than TAA (1.25) for better upscale quality
    float3 clipMin = nMean - gamma * stddev;
    float3 clipMax = nMean + gamma * stddev;

    // Clamp reprojected history to neighbourhood bounds
    float3 histTM = ToneMap(history);
    float3 clipped = clamp(histTM, clipMin, clipMax);
    float clipDist = length(histTM - clipped);
    history = InvToneMap(clipped);

    // ── Velocity-based rejection ─────────────────────────────────────
    float speed = length(motion * float2(DisplayDims));  // pixel velocity
    float velRejection = saturate(speed * 0.1);  // [0..1] higher = more rejection

    // ── Depth-based weighting ────────────────────────────────────────
    float centerDepth = DepthBuffer.Load(int3(renderCoord, 0));
    float depthMin = centerDepth;
    float depthMax = centerDepth;
    [unroll]
    for (int dy3 = -1; dy3 <= 1; dy3++)
    {
        [unroll]
        for (int dx3 = -1; dx3 <= 1; dx3++)
        {
            int2 sc = clamp(renderCoord + int2(dx3, dy3), 0, int2(RenderDims) - 1);
            float d = DepthBuffer.Load(int3(sc, 0));
            depthMin = min(depthMin, d);
            depthMax = max(depthMax, d);
        }
    }
    float depthEdge = saturate((depthMax - depthMin) * 500.0);

    // ── Material-aware blend weight ──────────────────────────────────
    uint matID = MaterialBuffer.Load(int3(renderCoord, 0));
    float materialWeight = GetMaterialHistoryWeight(matID);

    // ── Compute final blend alpha ────────────────────────────────────
    // Base: (1 - materialWeight) = how much current frame to use
    float alpha = 1.0 - materialWeight;

    // Increase alpha (more current) when:
    //   - high velocity (motion rejection)
    //   - history was clipped hard (disocclusion)
    //   - at depth edges
    float disocclusion = saturate(clipDist * 10.0);
    alpha = max(alpha, velRejection * 0.6);
    alpha = lerp(alpha, max(alpha, 0.5), disocclusion);
    alpha = lerp(alpha, max(alpha, 0.4), depthEdge);

    // Clamp to sane range
    alpha = clamp(alpha, 0.05, 0.8);

    // ── Blend ────────────────────────────────────────────────────────
    float3 result = lerp(history, current, alpha);

    OutputColor[outCoord] = float4(result, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL: Sharpen Pixel Shader (AMD CAS-style)
// ═══════════════════════════════════════════════════════════════════════════

static const char kSharpenPS[] = R"HLSL(
//-----------------------------------------------------------------------------
// TSR Sharpen PS — Contrast-Adaptive Sharpening (CAS-style)
// Material-aware: architecture sharpened more, foliage less
//-----------------------------------------------------------------------------

cbuffer TSRSharpenCB : register(b0)
{
    float  Sharpness;              // [0..1] global sharpness control
    uint2  DisplayDims;            // display resolution
    float  pad0;
    float4 MaterialSharpenWeight;  // per-material sharpen: [default, arch, foliage, skin]
    float2 RcpDisplayDims;         // 1.0 / DisplayDims
    float2 pad1;
};

Texture2D<float4> UpscaledColor  : register(t0);
Texture2D<uint>   MaterialBuffer : register(t1);
SamplerState PointSampler        : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float Luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// Get sharpening weight for a given material ID (same mapping as upscale)
float GetMaterialSharpenWeight(uint matID)
{
    if (matID == 3 || matID == 2 || matID == 7 || matID == 9)
        return MaterialSharpenWeight.y;  // architecture
    if (matID == 4 || matID == 5)
        return MaterialSharpenWeight.z;  // foliage
    if (matID == 1 || matID == 8)
        return MaterialSharpenWeight.w;  // skin
    return MaterialSharpenWeight.x;      // default
}

float4 main(VSOut input) : SV_Target
{
    int2 coord = int2(input.pos.xy);

    // Load center + cross neighbours
    float3 center = UpscaledColor.Load(int3(coord, 0)).rgb;
    float3 north  = UpscaledColor.Load(int3(clamp(coord + int2( 0,-1), 0, int2(DisplayDims) - 1), 0)).rgb;
    float3 south  = UpscaledColor.Load(int3(clamp(coord + int2( 0, 1), 0, int2(DisplayDims) - 1), 0)).rgb;
    float3 east   = UpscaledColor.Load(int3(clamp(coord + int2( 1, 0), 0, int2(DisplayDims) - 1), 0)).rgb;
    float3 west   = UpscaledColor.Load(int3(clamp(coord + int2(-1, 0), 0, int2(DisplayDims) - 1), 0)).rgb;

    // CAS: compute min/max of cross pattern
    float3 cMin = min(center, min(min(north, south), min(east, west)));
    float3 cMax = max(center, max(max(north, south), max(east, west)));

    // Adaptive sharpening weight based on local contrast
    // Low contrast regions get sharpened more (amplification limited by contrast)
    // High contrast regions (edges) get less sharpening (already sharp)
    float3 rcpRange = 1.0 / max(cMax - cMin, 1e-5);
    float3 peak = saturate(cMin * rcpRange);  // 0 at edges, 1 at flat

    // AMD CAS-style weight: sqrt(min(peak)) * sharpness_scale
    float peakScalar = sqrt(min(peak.r, min(peak.g, peak.b)));

    // Material-aware sharpening
    uint matID = MaterialBuffer.Load(int3(coord, 0));
    float matSharp = GetMaterialSharpenWeight(matID);

    // Final sharpening strength
    float sharp = peakScalar * Sharpness * matSharp;

    // Apply sharpening: weighted Laplacian
    // center + sharp * (center - average_of_neighbours)
    float3 avg = (north + south + east + west) * 0.25;
    float3 sharpened = center + sharp * (center - avg);

    // Clamp to prevent ringing
    sharpened = clamp(sharpened, cMin, cMax);

    // Ensure non-negative
    sharpened = max(sharpened, 0.0);

    return float4(sharpened, 1.0);
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

    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    HRESULT hr = D3DCompile(kUpscaleCS, strlen(kUpscaleCS), "TSRUpscaleCS",
                            nullptr, nullptr, "main", "cs_5_0", flags, 0,
                            &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("TemporalSuperRes: Upscale CS compile failed: {}",
                              static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    if (err) err->Release();

    hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
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
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width      = m_displayW;
        texDesc.Height     = m_displayH;
        texDesc.MipLevels  = 1;
        texDesc.ArraySize  = 1;
        texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc = {1, 0};
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = texDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = texDesc.Format;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        for (int i = 0; i < 2; ++i) {
            hr = dev->CreateTexture2D(&texDesc, nullptr, &m_historyTex[i]);
            if (FAILED(hr)) {
                SKSE::log::error("TemporalSuperRes: failed to create history tex {} (0x{:X})",
                                  i, static_cast<uint32_t>(hr));
                ReleaseResources();
                return false;
            }
            hr = dev->CreateShaderResourceView(m_historyTex[i], &srvDesc, &m_historySRV[i]);
            if (FAILED(hr)) {
                SKSE::log::error("TemporalSuperRes: failed to create history SRV {}", i);
                ReleaseResources();
                return false;
            }
            hr = dev->CreateUnorderedAccessView(m_historyTex[i], &uavDesc, &m_historyUAV[i]);
            if (FAILED(hr)) {
                SKSE::log::error("TemporalSuperRes: failed to create history UAV {}", i);
                ReleaseResources();
                return false;
            }
        }
    }

    // ── Render-resolution input copy (for reading current frame as SRV) ──
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width      = m_renderW;
        texDesc.Height     = m_renderH;
        texDesc.MipLevels  = 1;
        texDesc.ArraySize  = 1;
        texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc = {1, 0};
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

        hr = dev->CreateTexture2D(&texDesc, nullptr, &m_renderInputTex);
        if (FAILED(hr)) {
            SKSE::log::error("TemporalSuperRes: failed to create render input tex");
            ReleaseResources();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = texDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = dev->CreateShaderResourceView(m_renderInputTex, &srvDesc, &m_renderInputSRV);
        if (FAILED(hr)) {
            SKSE::log::error("TemporalSuperRes: failed to create render input SRV");
            ReleaseResources();
            return false;
        }
    }

    // ── Upscale output (display-res, written by CS, read by sharpen PS) ──
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width      = m_displayW;
        texDesc.Height     = m_displayH;
        texDesc.MipLevels  = 1;
        texDesc.ArraySize  = 1;
        texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc = {1, 0};
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = dev->CreateTexture2D(&texDesc, nullptr, &m_upscaleOutputTex);
        if (FAILED(hr)) {
            SKSE::log::error("TemporalSuperRes: failed to create upscale output tex");
            ReleaseResources();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = texDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = dev->CreateShaderResourceView(m_upscaleOutputTex, &srvDesc, &m_upscaleOutputSRV);
        if (FAILED(hr)) {
            ReleaseResources();
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = texDesc.Format;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = dev->CreateUnorderedAccessView(m_upscaleOutputTex, &uavDesc, &m_upscaleOutputUAV);
        if (FAILED(hr)) {
            ReleaseResources();
            return false;
        }
    }

    // ── Constant buffers ─────────────────────────────────────────────
    {
        // Upscale CB: round up to 16-byte alignment
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth     = (sizeof(TSRUpscaleCBData) + 15u) & ~15u;
        cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = dev->CreateBuffer(&cbDesc, nullptr, &m_upscaleCB);
        if (FAILED(hr)) {
            SKSE::log::error("TemporalSuperRes: failed to create upscale CB");
            ReleaseResources();
            return false;
        }
    }
    {
        // Sharpen CB
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth     = sizeof(TSRSharpenCBData);
        cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = dev->CreateBuffer(&cbDesc, nullptr, &m_sharpenCB);
        if (FAILED(hr)) {
            SKSE::log::error("TemporalSuperRes: failed to create sharpen CB");
            ReleaseResources();
            return false;
        }
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
