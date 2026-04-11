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
#include "ShaderLoader.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

#include <SKSE/SKSE.h>
#include "GPUResource.h"

namespace SB
{

// ═════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Optical Flow compute shader (quarter-res, 8x8 threads)
// ═════════════════════════════════════════════════════════════════════════════

static const char* const kOpticalFlowCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Quarter-res block matching optical flow via SAD (Sum of Absolute Differences).
// For each block in the current frame, search a neighborhood in the previous frame
// for the best match. Output: per-block motion vectors in pixel units.

cbuffer FrameGenParams : register(b0)
{
    uint2  ScreenDims;       // Full-res backbuffer dimensions
    uint2  FlowDims;         // Flow map dimensions (quarter or half res)
    float  FlowScale;        // Pixels-to-UV conversion factor (1/divisor)
    float  BlendWeight;      // Synthesis blend weight [0..1]
    uint   FrameIndex;       // Monotonic frame counter
    uint   QualityMode;      // 1 = Low (no refinement), 2 = High (hierarchical)
}

static const int kBlockRadius = 2;   // 5x5 block (2*2+1 = 5)
static const int kSearchRadius = 4;  // Search ±4 pixels in previous frame

Texture2D<float4> PrevFrame : register(t0);   // History[read] — frame N-1
Texture2D<float4> CurrFrame : register(t1);   // History[write] — frame N
RWTexture2D<float2> FlowOut : register(u0);   // R16G16_FLOAT motion vectors (pixels)

// Convert RGB to luminance for SAD comparison
float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= FlowDims.x || dtid.y >= FlowDims.y)
    {
        return;
    }

    // Center of this block in full-res coordinates
    // Each flow texel corresponds to a block in full-res space
    float scale = 1.0 / FlowScale; // e.g., 4.0 for quarter-res
    int2 blockCenter = int2(float2(dtid.xy) * scale + scale * 0.5);

    // Clamp to screen bounds
    blockCenter = clamp(blockCenter, int2(kBlockRadius, kBlockRadius),
                        int2(ScreenDims) - int2(kBlockRadius + 1, kBlockRadius + 1));

    // Search for the best matching block in the previous frame
    float bestSAD = 1e30;
    int2 bestOffset = int2(0, 0);

    // Determine search radius based on quality mode
    int searchRadius = kSearchRadius;
    if (QualityMode >= 2)
        searchRadius = kSearchRadius * 2; // Wider search for High quality

    for (int sy = -searchRadius; sy <= searchRadius; sy++)
    {
        for (int sx = -searchRadius; sx <= searchRadius; sx++)
        {
            int2 offset = int2(sx, sy);
            float sad = 0.0;

            // Compare 5x5 block (kBlockRadius=2 -> 5x5)
            [unroll]
            for (int by = -kBlockRadius; by <= kBlockRadius; by++)
            {
                [unroll]
                for (int bx = -kBlockRadius; bx <= kBlockRadius; bx++)
                {
                    int2 currPos = blockCenter + int2(bx, by);
                    int2 prevPos = currPos + offset;

                    // Clamp to valid texture coordinates
                    currPos = clamp(currPos, int2(0, 0), int2(ScreenDims) - 1);
                    prevPos = clamp(prevPos, int2(0, 0), int2(ScreenDims) - 1);

                    float currLum = Luminance(CurrFrame.Load(int3(currPos, 0)).rgb);
                    float prevLum = Luminance(PrevFrame.Load(int3(prevPos, 0)).rgb);

                    sad += abs(currLum - prevLum);
                }
            }

            if (sad < bestSAD)
            {
                bestSAD = sad;
                bestOffset = offset;
            }
        }
    }

    // Output motion vector in pixel units (from current to previous)
    FlowOut[dtid.xy] = float2(bestOffset);
}
)HLSL";


// ═════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Frame Synthesis compute shader (full-res, 8x8 threads)
// ═════════════════════════════════════════════════════════════════════════════

static const char* const kFrameSynthCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Synthesize an intermediate frame (N+0.5) by warping previous frame using optical flow.
// Handles disocclusion (holes) by blending with the current frame.

cbuffer FrameGenParams : register(b0)
{
    uint2  ScreenDims;
    uint2  FlowDims;
    float  FlowScale;
    float  BlendWeight;
    uint   FrameIndex;
    uint   QualityMode;
}

Texture2D<float4>  CurrFrame   : register(t0);   // Frame N (current)
Texture2D<float4>  PrevFrame   : register(t1);   // Frame N-1 (history)
Texture2D<float2>  FlowMap     : register(t2);   // Motion vectors (pixels, quarter-res)
RWTexture2D<float4> SynthOut   : register(u0);   // Synthesized frame N+0.5
SamplerState smpLinear : register(s0);
SamplerState smpPoint  : register(s1);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= ScreenDims.x || dtid.y >= ScreenDims.y)
        return;

    float2 pixelUV = (float2(dtid.xy) + 0.5) / float2(ScreenDims);

    // Sample flow map at this pixel's corresponding flow-res coordinate
    float2 flowUV = pixelUV; // flow map covers the same UV range
    float2 flow = FlowMap.SampleLevel(smpLinear, flowUV, 0);

    // Flow is in pixel units at full-res. For the intermediate frame (N+0.5),
    // we warp by half the motion.
    float2 halfFlow = flow * 0.5;

    // Warp: where was this pixel in the previous frame?
    // Previous frame location = current pixel + halfFlow (motion from curr->prev)
    float2 prevUV = pixelUV + halfFlow / float2(ScreenDims);

    // Where will this pixel be in the current frame relative to the midpoint?
    float2 currUV = pixelUV - halfFlow / float2(ScreenDims);

    // Sample both warped frames
    float4 warpedPrev = PrevFrame.SampleLevel(smpLinear, prevUV, 0);
    float4 warpedCurr = CurrFrame.SampleLevel(smpLinear, currUV, 0);

    // ── Disocclusion detection ──────────────────────────────────────
    // Check if the warped UV is out of bounds (indicates disocclusion)
    bool prevOOB = any(prevUV < 0.0) || any(prevUV > 1.0);
    bool currOOB = any(currUV < 0.0) || any(currUV > 1.0);

    // Check flow consistency: sample flow at neighboring pixels
    // and detect divergence (indicates object boundaries / disocclusion)
    float2 flowRight = FlowMap.SampleLevel(smpLinear,
        pixelUV + float2(1.0 / FlowDims.x, 0), 0);
    float2 flowDown  = FlowMap.SampleLevel(smpLinear,
        pixelUV + float2(0, 1.0 / FlowDims.y), 0);

    float divergence = abs(flowRight.x - flow.x) + abs(flowDown.y - flow.y);
    float disocclusionMask = saturate(divergence * 0.1);

    // ── Blend synthesized frame ─────────────────────────────────────
    float4 synthesized;

    if (prevOOB && currOOB)
    {
        // Both warps are out of bounds: fall back to current frame
        synthesized = CurrFrame.Load(int3(dtid.xy, 0));
    }
    else if (prevOOB)
    {
        synthesized = warpedCurr;
    }
    else if (currOOB)
    {
        synthesized = warpedPrev;
    }
    else
    {
        // Normal case: blend the two warped samples
        // Weight toward current frame in disoccluded regions
        float prevWeight = (1.0 - disocclusionMask) * BlendWeight;
        float currWeight = 1.0 - prevWeight;
        synthesized = warpedPrev * prevWeight + warpedCurr * currWeight;
    }

    // In heavily disoccluded regions, blend more with the raw current frame
    float4 rawCurr = CurrFrame.Load(int3(dtid.xy, 0));
    synthesized = lerp(synthesized, rawCurr, disocclusionMask);

    SynthOut[dtid.xy] = synthesized;
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
        ID3DBlob* blob = ShaderLoader::Compile("FrameGen_OpticalFlow", kOpticalFlowCS, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("FrameGenerator: OpticalFlow CS compile failed");
            return false;
        }

        HRESULT hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
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
        ID3DBlob* blob = ShaderLoader::Compile("FrameGen_Synthesis", kFrameSynthCS, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("FrameGenerator: FrameSynth CS compile failed");
            return false;
        }

        HRESULT hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
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
    if (!CreateCB(dev, sizeof(FrameGenParamsCB), &m_paramsCB)) {
        SKSE::log::error("FrameGenerator: Failed to create constant buffer");
        ReleaseResources();
        return false;
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

    // Scene cut detection (FeedbackProcessor removed — use stored flag)
    bool sceneCut = m_sceneCutPrev;

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

    // Scene cut flag for next frame
    m_sceneCutPrev = false;

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
