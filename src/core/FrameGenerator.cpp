//=============================================================================
//  FrameGenerator.cpp — DLSS 3-style compute-based frame generation
//
//  Dispatch flow (per frame, from PrePresent pipeline stage):
//    1. Copy current backbuffer into history[writeIdx]
//    2. Check scene cut flag (FeedbackProcessor Temporal.x) — skip if set
//    3. Dispatch OpticalFlow CS at quarter-res (or half-res in Low mode)
//       - SAD block matching with 8x8 search window
//       - 2-level hierarchical refinement (High mode only)
//    4. Dispatch FrameSynth CS at full-res
//       - Warp frame N using flow to produce frame N+0.5
//       - Detect disocclusion via flow divergence, fill from nearest valid
//    5. On next Present: insert synthesized frame before real frame
//       - CopyResource(backbuffer, synthTex) + Present + CopyResource(backbuffer, realFrame)
//    6. Swap ping-pong index
//=============================================================================

#include "FrameGenerator.h"
#include "FeedbackProcessor.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

#include <SKSE/SKSE.h>

namespace SB
{

// ═════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Optical Flow compute shader (quarter-res, 8x8 threads)
// ═════════════════════════════════════════════════════════════════════════════

static const char* const kOpticalFlowCS = R"HLSL(
// OpticalFlow CS — block-matching motion estimation
// Runs at quarter-res (or half-res in Low mode).
// Each thread computes a motion vector for one quarter-res texel.
// Uses SAD (sum of absolute differences) over a reference block,
// searching an 8x8 window centered on the source position.
// High mode: 2-level hierarchical refinement (coarse 4x4 step + fine 1x1 step).

cbuffer FrameGenParams : register(b0)
{
    uint2  ScreenDims;       // Full-res backbuffer dimensions
    uint2  FlowDims;         // Flow map dimensions (quarter or half res)
    float  FlowScale;        // Pixels-to-UV conversion factor
    float  BlendWeight;      // Synthesis blend weight [0..1]
    uint   FrameIndex;       // Monotonic frame counter
    uint   QualityMode;      // 1 = Low (no refinement), 2 = High (hierarchical)
};

Texture2D<float4> PrevFrame : register(t0);   // History[read] — frame N-1
Texture2D<float4> CurrFrame : register(t1);   // History[write] — frame N

RWTexture2D<float2> FlowOut : register(u0);   // R16G16_FLOAT motion vectors (pixels)

// Block size for matching
static const int kBlockRadius = 2;   // 5x5 block (2*2+1 = 5)
static const int kSearchRadius = 4;  // 8x8 search window (±4)

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Sum of absolute differences over a 5x5 block
float ComputeSAD(int2 refPos, int2 candPos, int2 fullDims)
{
    float sad = 0.0;

    [unroll]
    for (int dy = -kBlockRadius; dy <= kBlockRadius; dy++)
    {
        [unroll]
        for (int dx = -kBlockRadius; dx <= kBlockRadius; dx++)
        {
            int2 rp = clamp(refPos + int2(dx, dy), 0, fullDims - 1);
            int2 cp = clamp(candPos + int2(dx, dy), 0, fullDims - 1);

            float lr = Luminance(CurrFrame.Load(int3(rp, 0)).rgb);
            float lc = Luminance(PrevFrame.Load(int3(cp, 0)).rgb);

            sad += abs(lr - lc);
        }
    }

    return sad;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= FlowDims.x || dtid.y >= FlowDims.y)
        return;

    // Map flow-res texel to full-res center
    float scaleX = float(ScreenDims.x) / float(FlowDims.x);
    float scaleY = float(ScreenDims.y) / float(FlowDims.y);
    int2 fullPos = int2(float2(dtid.xy) * float2(scaleX, scaleY) + float2(scaleX, scaleY) * 0.5);
    fullPos = clamp(fullPos, 0, int2(ScreenDims) - 1);

    int2 fullDims = int2(ScreenDims);

    float bestSAD = 1e10;
    int2  bestOff = int2(0, 0);

    if (QualityMode == 2)
    {
        // ── Level 1: Coarse search (step=4, ±4 range = ±16 pixel reach) ──
        [loop]
        for (int cy = -kSearchRadius; cy <= kSearchRadius; cy++)
        {
            [loop]
            for (int cx = -kSearchRadius; cx <= kSearchRadius; cx++)
            {
                int2 offset = int2(cx, cy) * 4;
                int2 candPos = fullPos + offset;

                if (candPos.x < 0 || candPos.y < 0 ||
                    candPos.x >= fullDims.x || candPos.y >= fullDims.y)
                    continue;

                float sad = ComputeSAD(fullPos, candPos, fullDims);
                if (sad < bestSAD)
                {
                    bestSAD = sad;
                    bestOff = offset;
                }
            }
        }

        // ── Level 2: Fine refinement around coarse winner (step=1, ±3) ──
        int2 coarseBest = bestOff;

        [loop]
        for (int fy = -3; fy <= 3; fy++)
        {
            [loop]
            for (int fx = -3; fx <= 3; fx++)
            {
                int2 offset = coarseBest + int2(fx, fy);
                int2 candPos = fullPos + offset;

                if (candPos.x < 0 || candPos.y < 0 ||
                    candPos.x >= fullDims.x || candPos.y >= fullDims.y)
                    continue;

                float sad = ComputeSAD(fullPos, candPos, fullDims);
                if (sad < bestSAD)
                {
                    bestSAD = sad;
                    bestOff = offset;
                }
            }
        }
    }
    else
    {
        // ── Low quality: single-pass search (step=1, ±4) ─────────────
        [loop]
        for (int sy = -kSearchRadius; sy <= kSearchRadius; sy++)
        {
            [loop]
            for (int sx = -kSearchRadius; sx <= kSearchRadius; sx++)
            {
                int2 offset = int2(sx, sy);
                int2 candPos = fullPos + offset;

                if (candPos.x < 0 || candPos.y < 0 ||
                    candPos.x >= fullDims.x || candPos.y >= fullDims.y)
                    continue;

                float sad = ComputeSAD(fullPos, candPos, fullDims);
                if (sad < bestSAD)
                {
                    bestSAD = sad;
                    bestOff = offset;
                }
            }
        }
    }

    // Store motion vector in pixels (from N to N-1)
    FlowOut[dtid.xy] = float2(bestOff);
}
)HLSL";


// ═════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Frame Synthesis compute shader (full-res, 8x8 threads)
// ═════════════════════════════════════════════════════════════════════════════

static const char* const kFrameSynthCS = R"HLSL(
// FrameSynth CS — warp frame N using optical flow to synthesize frame N+0.5
// Handles disocclusion via flow divergence detection and nearest-valid fill.

cbuffer FrameGenParams : register(b0)
{
    uint2  ScreenDims;
    uint2  FlowDims;
    float  FlowScale;
    float  BlendWeight;
    uint   FrameIndex;
    uint   QualityMode;
};

Texture2D<float4>  CurrFrame   : register(t0);   // Frame N (current)
Texture2D<float4>  PrevFrame   : register(t1);   // Frame N-1 (history)
Texture2D<float2>  FlowMap     : register(t2);   // Motion vectors (pixels, quarter-res)

RWTexture2D<float4> SynthOut   : register(u0);   // Synthesized frame N+0.5

SamplerState smpLinear : register(s0);
SamplerState smpPoint  : register(s1);

float2 SampleFlow(int2 fullPos)
{
    // Map full-res position to flow-res UV
    float2 flowUV = (float2(fullPos) + 0.5) / float2(ScreenDims);
    return FlowMap.SampleLevel(smpLinear, flowUV, 0);
}

// Detect disocclusion via flow divergence
// High divergence = motion boundary = likely disoccluded region
float ComputeDivergence(int2 coord)
{
    float2 flowC = SampleFlow(coord);
    float2 flowL = SampleFlow(coord + int2(-1,  0));
    float2 flowR = SampleFlow(coord + int2( 1,  0));
    float2 flowU = SampleFlow(coord + int2( 0, -1));
    float2 flowD = SampleFlow(coord + int2( 0,  1));

    float divX = (flowR.x - flowL.x) * 0.5;
    float divY = (flowD.y - flowU.y) * 0.5;

    return abs(divX) + abs(divY);
}

// Search for nearest valid (non-disoccluded) pixel in a spiral pattern
float3 FindNearestValid(int2 coord, float divThreshold)
{
    // Start from center, spiral outward up to 4 pixels
    [loop]
    for (int radius = 1; radius <= 4; radius++)
    {
        [loop]
        for (int dy = -radius; dy <= radius; dy++)
        {
            [loop]
            for (int dx = -radius; dx <= radius; dx++)
            {
                // Only check border of current radius ring
                if (abs(dx) != radius && abs(dy) != radius)
                    continue;

                int2 nc = clamp(coord + int2(dx, dy), 0, int2(ScreenDims) - 1);
                float div = ComputeDivergence(nc);
                if (div < divThreshold)
                {
                    float2 flow = SampleFlow(nc);
                    float2 warpPos = float2(nc) + flow * 0.5;
                    float2 warpUV = (warpPos + 0.5) / float2(ScreenDims);
                    warpUV = clamp(warpUV, 0.0, 1.0);

                    float3 warped = CurrFrame.SampleLevel(smpLinear, warpUV, 0).rgb;
                    float3 curr = CurrFrame.Load(int3(nc, 0)).rgb;
                    return lerp(curr, warped, BlendWeight);
                }
            }
        }
    }

    // Fallback: use current frame unmodified
    return CurrFrame.Load(int3(coord, 0)).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= ScreenDims.x || dtid.y >= ScreenDims.y)
        return;

    int2 coord = int2(dtid.xy);

    // Sample flow at this full-res position
    float2 flow = SampleFlow(coord);
    float flowMag = length(flow);

    // ── Flow divergence check (disocclusion detection) ────────────
    float div = ComputeDivergence(coord);
    float divThreshold = 2.0;

    float3 result;

    if (div > divThreshold)
    {
        // Disoccluded region: fill from nearest valid pixel
        result = FindNearestValid(coord, divThreshold);
    }
    else
    {
        // Warp current frame by half the motion vector to get frame N+0.5
        float2 warpPos = float2(coord) + flow * 0.5;
        float2 warpUV = (warpPos + 0.5) / float2(ScreenDims);
        warpUV = clamp(warpUV, 0.0, 1.0);

        float3 warped = CurrFrame.SampleLevel(smpLinear, warpUV, 0).rgb;
        float3 curr = CurrFrame.Load(int3(coord, 0)).rgb;

        // Blend warped and current based on confidence
        // Lower flow magnitude = more reliable motion estimate
        float confidence = saturate(1.0 - flowMag * 0.02);
        float blend = BlendWeight * confidence;

        result = lerp(curr, warped, blend);
    }

    SynthOut[coord] = float4(result, 1.0);
}
)HLSL";


// ═════════════════════════════════════════════════════════════════════════════
//  CB layout — must match HLSL cbuffer FrameGenParams exactly
// ═════════════════════════════════════════════════════════════════════════════

struct alignas(16) FrameGenParamsCB
{
    uint32_t screenW;
    uint32_t screenH;
    uint32_t flowW;
    uint32_t flowH;
    float    flowScale;
    float    blendWeight;
    uint32_t frameIndex;
    uint32_t qualityMode;
};
static_assert(sizeof(FrameGenParamsCB) == 32, "CB must be 32 bytes (2 float4)");


// ═════════════════════════════════════════════════════════════════════════════
//  Quality mode name
// ═════════════════════════════════════════════════════════════════════════════

const char* FrameGenQualityName(FrameGenQuality q)
{
    switch (q)
    {
        case FrameGenQuality::Off:  return "Off";
        case FrameGenQuality::Low:  return "Low";
        case FrameGenQuality::High: return "High";
        default:                    return "Unknown";
    }
}


// ═════════════════════════════════════════════════════════════════════════════
//  Initialize
// ═════════════════════════════════════════════════════════════════════════════

bool FrameGenerator::Initialize(ID3D11Device* dev, IDXGISwapChain* swapChain)
{
    if (m_initialized) return true;
    if (!dev || !swapChain) return false;

    // Get backbuffer dimensions
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&backbuffer));
    if (FAILED(hr) || !backbuffer) {
        SKSE::log::error("FrameGenerator: Failed to get backbuffer (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backbuffer->GetDesc(&bbDesc);
    backbuffer->Release();

    m_width  = bbDesc.Width;
    m_height = bbDesc.Height;

    if (!CompileShaders(dev)) return false;
    if (!CreateResources(dev, m_width, m_height)) return false;

    RegisterPipelinePass();

    m_initialized = true;
    m_firstFrame  = true;
    m_synthReady  = false;
    m_frameIndex  = 0;

    SKSE::log::info("FrameGenerator: Initialized ({}x{}, flow {}x{}, quality={})",
                    m_width, m_height, m_flowW, m_flowH,
                    FrameGenQualityName(m_quality));
    return true;
}


// ═════════════════════════════════════════════════════════════════════════════
//  Compile compute shaders
// ═════════════════════════════════════════════════════════════════════════════

bool FrameGenerator::CompileShaders(ID3D11Device* dev)
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // ── Optical Flow CS ─────────────────────────────────────────────
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(kOpticalFlowCS, strlen(kOpticalFlowCS),
                                "SB_OpticalFlowCS", nullptr, nullptr,
                                "main", "cs_5_0", flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("FrameGenerator: OpticalFlow CS compile failed: {}",
                                 static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            return false;
        }
        if (err) err->Release();

        hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                      nullptr, &m_opticalFlowCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: CreateComputeShader failed for OpticalFlow (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    // ── Frame Synthesis CS ──────────────────────────────────────────
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(kFrameSynthCS, strlen(kFrameSynthCS),
                                "SB_FrameSynthCS", nullptr, nullptr,
                                "main", "cs_5_0", flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("FrameGenerator: FrameSynth CS compile failed: {}",
                                 static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            return false;
        }
        if (err) err->Release();

        hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                      nullptr, &m_frameSynthCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: CreateComputeShader failed for FrameSynth (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    return true;
}


// ═════════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═════════════════════════════════════════════════════════════════════════════

bool FrameGenerator::CreateResources(ID3D11Device* dev, uint32_t w, uint32_t h)
{
    HRESULT hr;

    // Flow resolution depends on quality mode
    // (recalculated in Execute too, but set here for initial allocation)
    m_flowW = w / 4;
    m_flowH = h / 4;

    // ── History textures (2x full-res R16G16B16A16_FLOAT, ping-pong) ──
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width      = w;
        texDesc.Height     = h;
        texDesc.MipLevels  = 1;
        texDesc.ArraySize  = 1;
        texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc = {1, 0};
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = texDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;

        for (int i = 0; i < 2; ++i) {
            hr = dev->CreateTexture2D(&texDesc, nullptr, &m_historyTex[i]);
            if (FAILED(hr)) {
                SKSE::log::error("FrameGenerator: Failed to create history texture {} (0x{:X})",
                                 i, static_cast<uint32_t>(hr));
                ReleaseResources();
                return false;
            }

            hr = dev->CreateShaderResourceView(m_historyTex[i], &srvDesc, &m_historySRV[i]);
            if (FAILED(hr)) {
                SKSE::log::error("FrameGenerator: Failed to create history SRV {}", i);
                ReleaseResources();
                return false;
            }
        }
    }

    // ── Flow buffer (quarter-res R16G16_FLOAT) ──────────────────────
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width      = m_flowW;
        texDesc.Height     = m_flowH;
        texDesc.MipLevels  = 1;
        texDesc.ArraySize  = 1;
        texDesc.Format     = DXGI_FORMAT_R16G16_FLOAT;
        texDesc.SampleDesc = {1, 0};
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = dev->CreateTexture2D(&texDesc, nullptr, &m_flowTex);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create flow texture (0x{:X})",
                             static_cast<uint32_t>(hr));
            ReleaseResources();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = texDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = dev->CreateShaderResourceView(m_flowTex, &srvDesc, &m_flowSRV);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create flow SRV");
            ReleaseResources();
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = texDesc.Format;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = dev->CreateUnorderedAccessView(m_flowTex, &uavDesc, &m_flowUAV);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create flow UAV");
            ReleaseResources();
            return false;
        }
    }

    // ── Synthesis buffer (full-res R16G16B16A16_FLOAT) ──────────────
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width      = w;
        texDesc.Height     = h;
        texDesc.MipLevels  = 1;
        texDesc.ArraySize  = 1;
        texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc = {1, 0};
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = dev->CreateTexture2D(&texDesc, nullptr, &m_synthTex);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create synthesis texture (0x{:X})",
                             static_cast<uint32_t>(hr));
            ReleaseResources();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = texDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = dev->CreateShaderResourceView(m_synthTex, &srvDesc, &m_synthSRV);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create synthesis SRV");
            ReleaseResources();
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = texDesc.Format;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = dev->CreateUnorderedAccessView(m_synthTex, &uavDesc, &m_synthUAV);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create synthesis UAV");
            ReleaseResources();
            return false;
        }
    }

    // ── Constant buffer (32 bytes = 2 float4) ──────────────────────
    {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth      = sizeof(FrameGenParamsCB);
        cbDesc.Usage           = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
        hr = dev->CreateBuffer(&cbDesc, nullptr, &m_paramsCB);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create constant buffer");
            ReleaseResources();
            return false;
        }
    }

    // ── Samplers ────────────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.MaxAnisotropy = 1;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = dev->CreateSamplerState(&sampDesc, &m_pointSampler);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create point sampler");
            ReleaseResources();
            return false;
        }

        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        hr = dev->CreateSamplerState(&sampDesc, &m_linearSampler);
        if (FAILED(hr)) {
            SKSE::log::error("FrameGenerator: Failed to create linear sampler");
            ReleaseResources();
            return false;
        }
    }

    return true;
}


// ═════════════════════════════════════════════════════════════════════════════
//  Register as a PrePresent pipeline pass (priority 900)
// ═════════════════════════════════════════════════════════════════════════════

void FrameGenerator::RegisterPipelinePass()
{
    auto& pipeline = RenderPipeline::Get();
    if (!pipeline.IsInitialized()) {
        SKSE::log::warn("FrameGenerator: RenderPipeline not initialized, skipping pass registration");
        return;
    }

    m_passHandle = pipeline.AddPass({
        .name     = "FrameGenerator",
        .stage    = PipelineStage::PrePresent,
        .priority = 900,
        .enabled  = m_enabled,
        .execute  = [this](PassContext& ctx) { Execute(ctx); }
    });
}


// ═════════════════════════════════════════════════════════════════════════════
//  Execute — main per-frame entry point
// ═════════════════════════════════════════════════════════════════════════════

void FrameGenerator::Execute(PassContext& ctx)
{
    if (!m_initialized || !IsEnabled() || !ctx.context || !ctx.swapChain)
        return;

    auto* dctx = ctx.context;

    // ── Get backbuffer ──────────────────────────────────────────────
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                           reinterpret_cast<void**>(&backbuffer));
    if (FAILED(hr) || !backbuffer) return;

    // ── Handle first frame: seed history, no synthesis possible ─────
    if (m_firstFrame) {
        dctx->CopyResource(m_historyTex[0], backbuffer);
        dctx->CopyResource(m_historyTex[1], backbuffer);
        m_firstFrame = false;
        m_writeIdx = 0;
        m_synthReady = false;
        backbuffer->Release();
        ++m_frameIndex;
        return;
    }

    // ── Check scene cut (FeedbackProcessor Temporal.x) ──────────────
    bool sceneCut = false;
    {
        auto& feedback = FeedbackProcessor::Get();
        if (feedback.IsInitialized()) {
            // Scene cut flag is distributed via DistributeFeedback -> AllData
            // but we can also check the readback slot's temporal flag directly.
            // For simplicity, check if m_sceneCutPrev was set (1-frame delay is fine).
            sceneCut = m_sceneCutPrev;
        }
    }

    // ── Copy current backbuffer to history[writeIdx] ────────────────
    int writeIdx = m_writeIdx;
    int readIdx  = 1 - m_writeIdx;
    dctx->CopyResource(m_historyTex[writeIdx], backbuffer);

    if (sceneCut) {
        // Scene cut: skip generation, just update history and move on
        SKSE::log::trace("FrameGenerator: Scene cut detected, skipping synthesis");
        m_synthReady = false;
        backbuffer->Release();
        m_writeIdx = 1 - m_writeIdx;
        ++m_frameIndex;
        return;
    }

    // ── Insert previously synthesized frame if ready ────────────────
    if (m_synthReady) {
        PresentSynthesizedFrame(dctx, ctx.swapChain);
    }

    // ── Update constant buffer ──────────────────────────────────────
    {
        // Compute flow resolution based on quality mode
        uint32_t flowDivisor = (m_quality == FrameGenQuality::Low) ? 2 : 4;
        m_flowW = m_width  / flowDivisor;
        m_flowH = m_height / flowDivisor;

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = dctx->Map(m_paramsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            FrameGenParamsCB cb;
            cb.screenW     = m_width;
            cb.screenH     = m_height;
            cb.flowW       = m_flowW;
            cb.flowH       = m_flowH;
            cb.flowScale   = 1.0f / static_cast<float>(flowDivisor);
            cb.blendWeight = 0.5f;
            cb.frameIndex  = m_frameIndex;
            cb.qualityMode = static_cast<uint32_t>(m_quality);
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            dctx->Unmap(m_paramsCB, 0);
        }
    }

    // ── Save CS state ───────────────────────────────────────────────
    ID3D11ComputeShader* savedCS = nullptr;
    ID3D11ClassInstance* savedCI[256] = {};
    UINT savedCICount = 256;
    dctx->CSGetShader(&savedCS, savedCI, &savedCICount);

    ID3D11ShaderResourceView* savedSRVs[3] = {};
    dctx->CSGetShaderResources(0, 3, savedSRVs);

    ID3D11UnorderedAccessView* savedUAVs[1] = {};
    dctx->CSGetUnorderedAccessViews(0, 1, savedUAVs);

    ID3D11Buffer* savedCBs[1] = {};
    dctx->CSGetConstantBuffers(0, 1, savedCBs);

    ID3D11SamplerState* savedSamplers[2] = {};
    dctx->CSGetSamplers(0, 2, savedSamplers);

    // ── Pass 1: Optical Flow ────────────────────────────────────────
    DispatchOpticalFlow(dctx);

    // ── Pass 2: Frame Synthesis ─────────────────────────────────────
    DispatchFrameSynth(dctx);

    m_synthReady = true;

    // ── Restore CS state ────────────────────────────────────────────
    dctx->CSSetShader(savedCS, savedCI, savedCICount);
    dctx->CSSetShaderResources(0, 3, savedSRVs);
    dctx->CSSetUnorderedAccessViews(0, 1, savedUAVs, nullptr);
    dctx->CSSetConstantBuffers(0, 1, savedCBs);
    dctx->CSSetSamplers(0, 2, savedSamplers);

    if (savedCS) savedCS->Release();
    for (UINT i = 0; i < 3; i++) if (savedSRVs[i]) savedSRVs[i]->Release();
    for (UINT i = 0; i < 1; i++) if (savedUAVs[i]) savedUAVs[i]->Release();
    for (UINT i = 0; i < 1; i++) if (savedCBs[i])  savedCBs[i]->Release();
    for (UINT i = 0; i < 2; i++) if (savedSamplers[i]) savedSamplers[i]->Release();

    backbuffer->Release();

    // ── Update scene cut for next frame ─────────────────────────────
    {
        auto& feedback = FeedbackProcessor::Get();
        if (feedback.IsInitialized()) {
            // We can't easily access AllData here, so we approximate:
            // if FeedbackProcessor is available, we rely on the pipeline.
            // The actual sceneCut flag will be read next frame via DistributeFeedback.
            m_sceneCutPrev = false;  // Will be set by external call if needed
        }
    }

    // ── Swap ping-pong ──────────────────────────────────────────────
    m_writeIdx = 1 - m_writeIdx;
    ++m_frameIndex;
}


// ═════════════════════════════════════════════════════════════════════════════
//  Dispatch Optical Flow CS
// ═════════════════════════════════════════════════════════════════════════════

void FrameGenerator::DispatchOpticalFlow(ID3D11DeviceContext* ctx)
{
    int readIdx  = 1 - m_writeIdx;
    int writeIdx = m_writeIdx;

    // Bind shader
    ctx->CSSetShader(m_opticalFlowCS, nullptr, 0);

    // t0 = previous frame (history[read]), t1 = current frame (history[write])
    ID3D11ShaderResourceView* srvs[2] = {
        m_historySRV[readIdx],
        m_historySRV[writeIdx]
    };
    ctx->CSSetShaderResources(0, 2, srvs);

    // u0 = flow output
    ctx->CSSetUnorderedAccessViews(0, 1, &m_flowUAV, nullptr);

    // b0 = params
    ctx->CSSetConstantBuffers(0, 1, &m_paramsCB);

    // Dispatch at flow resolution
    UINT groupsX = (m_flowW + 7) / 8;
    UINT groupsY = (m_flowH + 7) / 8;
    ctx->Dispatch(groupsX, groupsY, 1);

    // Unbind to avoid SRV/UAV conflicts with next pass
    ID3D11ShaderResourceView* nullSRVs[2] = {};
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ctx->CSSetShaderResources(0, 2, nullSRVs);
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}


// ═════════════════════════════════════════════════════════════════════════════
//  Dispatch Frame Synthesis CS
// ═════════════════════════════════════════════════════════════════════════════

void FrameGenerator::DispatchFrameSynth(ID3D11DeviceContext* ctx)
{
    int readIdx  = 1 - m_writeIdx;
    int writeIdx = m_writeIdx;

    // Bind shader
    ctx->CSSetShader(m_frameSynthCS, nullptr, 0);

    // t0 = current frame, t1 = previous frame, t2 = flow map
    ID3D11ShaderResourceView* srvs[3] = {
        m_historySRV[writeIdx],   // current frame N
        m_historySRV[readIdx],    // previous frame N-1
        m_flowSRV                 // optical flow vectors
    };
    ctx->CSSetShaderResources(0, 3, srvs);

    // u0 = synthesis output
    ctx->CSSetUnorderedAccessViews(0, 1, &m_synthUAV, nullptr);

    // b0 = params
    ctx->CSSetConstantBuffers(0, 1, &m_paramsCB);

    // s0 = linear sampler, s1 = point sampler
    ID3D11SamplerState* samplers[2] = { m_linearSampler, m_pointSampler };
    ctx->CSSetSamplers(0, 2, samplers);

    // Dispatch at full resolution
    UINT groupsX = (m_width  + 7) / 8;
    UINT groupsY = (m_height + 7) / 8;
    ctx->Dispatch(groupsX, groupsY, 1);

    // Unbind
    ID3D11ShaderResourceView* nullSRVs[3] = {};
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ctx->CSSetShaderResources(0, 3, nullSRVs);
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}


// ═════════════════════════════════════════════════════════════════════════════
//  Present synthesized frame — double-present insertion
// ═════════════════════════════════════════════════════════════════════════════

void FrameGenerator::PresentSynthesizedFrame(ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (!m_synthReady || !sc) return;

    // Get the backbuffer
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backbuffer));
    if (FAILED(hr) || !backbuffer) {
        m_synthReady = false;
        return;
    }

    // Save current backbuffer content (the real frame N)
    // to a temporary location — we use history[read] since it holds N-1
    // and we're about to overwrite the backbuffer with the synth frame.
    // After Present of the synth frame, we restore the real frame.
    int readIdx = 1 - m_writeIdx;

    // Copy synthesized frame into backbuffer
    ctx->CopyResource(backbuffer, m_synthTex);

    // Present the synthesized frame (frame N+0.5 from previous pair)
    sc->Present(0, 0);

    // Restore the real frame N back to the backbuffer.
    // The caller (pipeline) will Present the real frame after this.
    ctx->CopyResource(backbuffer, m_historyTex[m_writeIdx]);

    backbuffer->Release();
    m_synthReady = false;
}


// ═════════════════════════════════════════════════════════════════════════════
//  Shutdown + ReleaseResources
// ═════════════════════════════════════════════════════════════════════════════

void FrameGenerator::ReleaseResources()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    SafeRelease(m_opticalFlowCS);
    SafeRelease(m_frameSynthCS);

    for (int i = 0; i < 2; i++) {
        SafeRelease(m_historySRV[i]);
        SafeRelease(m_historyTex[i]);
    }

    SafeRelease(m_flowUAV);
    SafeRelease(m_flowSRV);
    SafeRelease(m_flowTex);

    SafeRelease(m_synthUAV);
    SafeRelease(m_synthSRV);
    SafeRelease(m_synthTex);

    SafeRelease(m_paramsCB);
    SafeRelease(m_pointSampler);
    SafeRelease(m_linearSampler);
}

void FrameGenerator::Shutdown()
{
    if (m_passHandle != 0) {
        auto& pipeline = RenderPipeline::Get();
        if (pipeline.IsInitialized()) {
            pipeline.RemovePass(m_passHandle);
        }
        m_passHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;
    m_enabled     = false;
    m_synthReady  = false;
    m_firstFrame  = true;
    m_width = m_height = 0;
    m_flowW = m_flowH = 0;
    m_frameIndex = 0;
}

} // namespace SB
