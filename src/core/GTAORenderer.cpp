//=============================================================================
//  GTAORenderer.cpp — VB-SSGI (Visibility Bitmask Screen-Space GI)
//
//  Upgraded from GTAO (Jimenez 2016) to visibility bitmask approach
//  (Activision 2019, "Practical Real-Time Strategies for Accurate Indirect
//  Occlusion"). Computes both ambient occlusion and short-range indirect
//  bounce lighting via 32-bit visibility bitmask encoding.
//
//  Dispatch flow (per frame, PostGeometry stage):
//    1. Copy backbuffer → m_sceneColorTex (bounce light source)
//    2. Dispatch VB-SSGI CS:  visibility bitmask AO + bounce gather → raw float4
//    3. Dispatch Spatial CS:  5x5 bilateral filter → denoised float4
//    4. Dispatch Temporal CS:  ping-pong history accumulation → final float4
//    5. Register SRV at t20 (.rgb = bounce, .a = AO)
//=============================================================================

#include "GTAORenderer.h"
#include "RangeOracle.h"   // Tier 1.3 per-pass range oracle (gated, default off)
#include "HiZPyramid.h"
#include "SRVInjector.h"
#include "SharedGPUResources.h"
#include "ShaderLoader.h"
#include "GPUResource.h"
#include "D3D11Hook.h"
#include "SceneData.h"
#include "MotionVectorGen.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 1: VB-SSGI CS (full-res)
//  Visibility bitmask AO + indirect bounce light gathering
// ═══════════════════════════════════════════════════════════════════════════

static const char kGTAOCS[] = "#error Deploy external Shaders/GTAO_Main.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: Spatial Denoise CS (full-res, 5x5 bilateral)
//  Updated for float4 (bounce.rgb, ao.a) input/output
// ═══════════════════════════════════════════════════════════════════════════

static const char kSpatialCS[] = "#error Deploy external Shaders/GTAO_Spatial.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 3: Temporal Accumulation CS (full-res, ping-pong)
//  Updated for float4 (bounce.rgb, ao.a) input/output
// ═══════════════════════════════════════════════════════════════════════════

static const char kTemporalCS[] = "#error Deploy external Shaders/GTAO_Temporal.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) GTAOCBData
{
    float    projMatrix[16];     // 64 bytes — row-major 4x4
    uint32_t screenW;            // +64
    uint32_t screenH;            // +68
    float    nearZ;              // +72
    float    farZ;               // +76
    float    aoRadius;           // +80
    float    aoIntensity;        // +84
    int32_t  numDirections;      // +88
    int32_t  numSteps;           // +92
    uint32_t frameIndex;         // +96
    float    bounceIntensity;    // +100
    uint32_t bounceEnabled;      // +104
    float    fpDepthThreshold;   // +108 — first-person depth mask (view-space units)
};
static_assert(sizeof(GTAOCBData) == 112, "GTAOCB must be 112 bytes");

struct alignas(16) SpatialCBData
{
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    depthThreshold;
    float    pad[3];
};
static_assert(sizeof(SpatialCBData) == 32, "SpatialCB must be 32 bytes");

struct alignas(16) TemporalCBData
{
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    temporalAlpha;
    float    depthRejectThreshold;
    uint32_t frameIndex;
    float    pad;
};
static_assert(sizeof(TemporalCBData) == 32, "TemporalCB must be 32 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool GTAORenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm = ComputeManager::Get();
    auto& pl = RenderPipeline::Get();

    if (!cm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("GTAORenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("GTAORenderer: failed to get backbuffer (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backTex->GetDesc(&bbDesc);
    backTex->Release();

    m_screenW = bbDesc.Width;
    m_screenH = bbDesc.Height;

    if (!CompileShaders()) return false;
    if (!CreateResources()) {
        ReleaseResources();
        return false;
    }

    // Register as PostGeometry pipeline pass — dispatched mid-frame by
    // PhaseDispatcher when the game transitions out of GeometryMain phase.
    // Live depth + SceneMatrices available via UpdateFromNiCamera().
    m_pipelineHandle = pl.AddPass({
        .name     = "VB-SSGI",
        .stage    = PipelineStage::PostGeometry,
        .priority = 15,  // After HiZ (10), before SSGI (20)
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
        // Tier 1.3 (gated [Diagnostics] GpuReadback, default off; GPU path operator-validated):
        // hand this pass's HDR AO+GI output SRV to the range oracle AT EXECUTE TIME (output is
        // ping-pong double-buffered, so call the accessor in the lambda, never capture a raw ptr).
        .post_execute = [this](PassContext& c, const char* n, ID3D11ShaderResourceView*) {
            RangeOracle::Inspect(c.frameIndex, n, GetOutputSRV());
        },
    });

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("GTAORenderer: VB-SSGI initialized ({}x{}, dirs={}, steps={}, radius={:.1f}, bounce={}, t{})",
                    m_screenW, m_screenH, m_numDirections, m_numSteps, m_aoRadius, m_bounceEnabled, kSRVSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool GTAORenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    auto CompileCS = [&](const char* name, const char* source,
                         ID3D11ComputeShader** outCS) -> bool
    {
        ID3DBlob* blob = ShaderLoader::Compile(name, source, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("GTAORenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("GTAORenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("GTAO_Main",    kGTAOCS,     &m_gtaoCS))     return false;
    if (!CompileCS("GTAO_Spatial", kSpatialCS,  &m_spatialCS))  return false;
    if (!CompileCS("GTAO_Temporal",kTemporalCS, &m_temporalCS)) return false;

    return true;
}

bool GTAORenderer::RecompileShaders()
{
    SafeRelease(m_gtaoCS);
    SafeRelease(m_spatialCS);
    SafeRelease(m_temporalCS);
    return CompileShaders();
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool GTAORenderer::CreateResources()
{
    const auto fmt = DXGI_FORMAT_R32_FLOAT;

    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, fmt, &m_aoRaw, &m_aoRawSRV, &m_aoRawUAV, "aoRaw"))
        return false;
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, fmt, &m_aoSpatial, &m_aoSpatialSRV, &m_aoSpatialUAV, "aoSpatial"))
        return false;
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, fmt, &m_aoHistory[0], &m_aoHistorySRV[0], &m_aoHistoryUAV[0], "aoHistory0"))
        return false;
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, fmt, &m_aoHistory[1], &m_aoHistorySRV[1], &m_aoHistoryUAV[1], "aoHistory1"))
        return false;

    // Scene color copy (SRV only, no UAV)
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R8G8B8A8_UNORM,
                          &m_sceneColorTex, &m_sceneColorSRV, nullptr, "sceneColor"))
        return false;

    // Constant buffers
    if (!CreateCB(m_device, sizeof(GTAOCBData), &m_gtaoCB)) return false;
    if (!CreateCB(m_device, sizeof(SpatialCBData), &m_spatialCB)) return false;
    if (!CreateCB(m_device, sizeof(TemporalCBData), &m_temporalCB)) return false;

    // Point sampler (for Load-style reads via sampler when needed)
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter        = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc= D3D11_COMPARISON_NEVER;
        desc.MaxLOD        = D3D11_FLOAT32_MAX;
        HRESULT hr = m_device->CreateSamplerState(&desc, &m_pointSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV from currently-bound DSV
// ═══════════════════════════════════════════════════════════════════════════

bool GTAORenderer::AcquireDepthSRV()
{
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    ID3D11DepthStencilView* dsv = nullptr;
    m_context->OMGetRenderTargets(0, nullptr, &dsv);
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
        case DXGI_FORMAT_R32_TYPELESS:      srvFormat = DXGI_FORMAT_R32_FLOAT;              break;
        case DXGI_FORMAT_R24G8_TYPELESS:    srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;  break;
        case DXGI_FORMAT_R32G8X24_TYPELESS: srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
        case DXGI_FORMAT_R16_TYPELESS:      srvFormat = DXGI_FORMAT_R16_UNORM;              break;
        case DXGI_FORMAT_R32_FLOAT:         srvFormat = DXGI_FORMAT_R32_FLOAT;              break;
        case DXGI_FORMAT_R16_UNORM:         srvFormat = DXGI_FORMAT_R16_UNORM;              break;
        default:
            depthTex->Release();
            return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = srvFormat;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    hr = m_device->CreateShaderResourceView(depthTex, &srvDesc, &m_depthSRV);
    depthTex->Release();

    return SUCCEEDED(hr) && m_depthSRV != nullptr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  GetOutputSRV — returns the most recently accumulated output SRV
//  (.rgb = bounce light, .a = ambient occlusion)
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* GTAORenderer::GetOutputSRV() const
{
    if (!m_initialized) return nullptr;
    // Return temporally accumulated output (falls back to spatial if temporal not ready)
    auto* temporal = m_aoHistorySRV[1 - m_pingPongIdx];
    return temporal ? temporal : (m_aoSpatialSRV ? m_aoSpatialSRV : m_aoRawSRV);
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PostGeometry stage)
// ═══════════════════════════════════════════════════════════════════════════

void GTAORenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Gate: scene matrices must be valid (camera data verified non-garbage)
    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) {
        static int s_logCount = 0;
        if (s_logCount++ < 5) SKSE::log::warn("VB-SSGI: SceneMatrices invalid, skipping");
        return;
    }

    auto& cm = ComputeManager::Get();

    // ── Acquire depth ───────────────────────────────────────────────────
    // With depth interception (Step 2), the game's depth texture has a live
    // SRV. At PostGeometry time (after fixed phase detection, Step 1), all
    // geometry is complete. Build HiZ from the live depth for current-frame
    // reversed-Z + linear depth.
    auto& hiz = HiZPyramid::Get();
    if (hiz.IsInitialized() && hiz.IsEnabled()) {
        hiz.BuildPyramid(ctx.context);
        SharedGPUResources::Get().Update(ctx.context);
    }

    ID3D11ShaderResourceView* depthSRV = nullptr;
    if (hiz.IsInitialized() && hiz.GetSRV()) {
        depthSRV = hiz.GetSRV();
    } else {
        depthSRV = D3D11Hook::GetGameDepthSRV();
    }
    if (!depthSRV) {
        if (!AcquireDepthSRV()) return;
        depthSRV = m_depthSRV;
    }

    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    // ── Copy scene color texture (for bounce gathering) ─────────────────
    // During mid-frame dispatch the backbuffer does NOT contain the scene;
    // the game renders to an internal RT exposed via ctx.gameSceneRTV.
    // Extract the underlying texture from that RTV first, falling back to
    // the legacy backbuffer path only when gameSceneRTV is null (PrePresent).
    if (m_bounceEnabled && m_sceneColorTex) {
        ID3D11Texture2D* sceneSrc   = nullptr;
        bool             needRelease = false;

        // --- Mid-frame path: read the game's current render target -------
        if (ctx.gameSceneRTV) {
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                                  reinterpret_cast<void**>(&sceneSrc));
                res->Release();
                if (FAILED(hr)) sceneSrc = nullptr;
                // sceneSrc obtained via QI — must be released by us
                needRelease = (sceneSrc != nullptr);
            }
        }

        // --- Fallback: pre-UI snapshot or swap chain backbuffer ----------
        if (!sceneSrc) {
            sceneSrc = D3D11Hook::GetPreUISceneTex();
            if (sceneSrc) {
                needRelease = false;  // owned by D3D11Hook, do not release
            } else {
                auto* sc = D3D11Hook::GetSwapChain();
                if (sc) {
                    sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                  reinterpret_cast<void**>(&sceneSrc));
                    needRelease = (sceneSrc != nullptr);
                }
            }
        }

        if (sceneSrc) {
            D3D11_TEXTURE2D_DESC srcDesc;
            sceneSrc->GetDesc(&srcDesc);

            // Ensure copy texture format matches source (game scene RT is HDR float,
            // but m_sceneColorTex was created as R8G8B8A8_UNORM at init time).
            D3D11_TEXTURE2D_DESC copyDesc;
            m_sceneColorTex->GetDesc(&copyDesc);
            if (srcDesc.Format != copyDesc.Format ||
                srcDesc.Width  != copyDesc.Width  ||
                srcDesc.Height != copyDesc.Height)
            {
                m_sceneColorSRV->Release(); m_sceneColorSRV = nullptr;
                m_sceneColorTex->Release(); m_sceneColorTex = nullptr;

                D3D11_TEXTURE2D_DESC newDesc = srcDesc;
                newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
                newDesc.Usage          = D3D11_USAGE_DEFAULT;
                newDesc.CPUAccessFlags = 0;
                newDesc.MiscFlags      = 0;
                newDesc.MipLevels      = 1;
                newDesc.ArraySize      = 1;
                newDesc.SampleDesc     = {1, 0};
                m_device->CreateTexture2D(&newDesc, nullptr, &m_sceneColorTex);

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                    = newDesc.Format;
                srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MostDetailedMip = 0;
                srvDesc.Texture2D.MipLevels       = 1;
                m_device->CreateShaderResourceView(m_sceneColorTex, &srvDesc, &m_sceneColorSRV);

                SKSE::log::info("VB-SSGI: scene color texture recreated — {}x{} fmt={}",
                    srcDesc.Width, srcDesc.Height, static_cast<uint32_t>(srcDesc.Format));
            }

            ctx.context->CopyResource(m_sceneColorTex, sceneSrc);
            if (needRelease) {
                sceneSrc->Release();
            }
        }
    }

    // First frame: clear history to 1.0 (fully lit / no occlusion)
    if (m_firstFrame) {
        float clearVal[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        ctx.context->ClearUnorderedAccessViewFloat(m_aoHistoryUAV[0], clearVal);
        ctx.context->ClearUnorderedAccessViewFloat(m_aoHistoryUAV[1], clearVal);
        m_firstFrame = false;
        m_pingPongIdx = 0;
    }

    cm.SaveCSState();

    // Diagnostics removed — GTAO confirmed working

    // Bind pre-computed linear depth at CS t31
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: VB-SSGI — visibility bitmask AO + bounce gather
    // ═════════════════════════════════════════════════════════════════════
    {
        GTAOCBData cb = {};
        std::memcpy(cb.projMatrix, scene.ProjMatrix(), sizeof(float) * 16);
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.aoRadius       = m_aoRadius;
        cb.aoIntensity    = m_aoIntensity;
        cb.numDirections  = m_numDirections;
        cb.numSteps       = m_numSteps;
        cb.frameIndex     = m_frameIndex;
        cb.bounceIntensity = m_bounceIntensity;
        cb.bounceEnabled  = m_bounceEnabled ? 1u : 0u;
        cb.fpDepthThreshold = 0.0f;  // disabled — let all geometry contribute to AO

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_gtaoCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_gtaoCB, 0);
        }

        // DIAGNOSTIC: Restore full dispatch to confirm gray output
        // t0 = depth, t1 = scene color, t30 = blue noise
        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_sceneColorSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        auto* bnSRV = SharedGPUResources::Get().GetBlueNoiseSRV();
        if (bnSRV) ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &bnSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_aoRawUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_gtaoCB);
        ctx.context->CSSetShader(m_gtaoCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear bindings
        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        { ID3D11ShaderResourceView* n = nullptr; ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &n); }
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: Spatial denoise — 5x5 bilateral filter on float4
    // ═════════════════════════════════════════════════════════════════════
    {
        SpatialCBData cb = {};
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.depthThreshold = 0.05f;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_spatialCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_spatialCB, 0);
        }

        // t0 = raw VB-SSGI output, t1 = depth
        ID3D11ShaderResourceView* srvs[] = { m_aoRawSRV, depthSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_aoSpatialUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_spatialCB);
        ctx.context->CSSetShader(m_spatialCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 3: Temporal accumulation (ping-pong) on float4
    // ═════════════════════════════════════════════════════════════════════
    {
        int readIdx  = 1 - m_pingPongIdx;
        int writeIdx = m_pingPongIdx;

        TemporalCBData cb = {};
        cb.screenW              = m_screenW;
        cb.screenH              = m_screenH;
        cb.nearZ                = nearZ;
        cb.farZ                 = farZ;
        cb.temporalAlpha        = 0.2f;   // was 0.1 — faster convergence, less ghosting
        cb.depthRejectThreshold = 0.1f;
        cb.frameIndex           = m_frameIndex;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_temporalCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_temporalCB, 0);
        }

        // t0 = spatially denoised (current), t1 = history (previous), t2 = depth, t3 = motion vectors
        auto* motionSRV = MotionVectorGen::Get().GetMotionSRV();
        ID3D11ShaderResourceView* srvs[] = { m_aoSpatialSRV, m_aoHistorySRV[readIdx], depthSRV, motionSRV };
        ctx.context->CSSetShaderResources(0, 4, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_aoHistoryUAV[writeIdx], nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_temporalCB);
        ctx.context->CSSetShader(m_temporalCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[4] = {};
        ctx.context->CSSetShaderResources(0, 4, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

        // Swap ping-pong
        m_pingPongIdx = readIdx;
    }

    // Unbind linear depth
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // Register output SRV at t20
    SRVInjector::Get().RegisterSRV(kSRVSlot, GetOutputSRV());

    // Release per-frame depth SRV
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    m_frameIndex++;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void GTAORenderer::Shutdown()
{
    if (!m_initialized) return;

    SRVInjector::Get().UnregisterSRV(kSRVSlot);

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("GTAORenderer: VB-SSGI shut down");
}


void GTAORenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_gtaoCS);
    SafeRelease(m_spatialCS);
    SafeRelease(m_temporalCS);

    SafeRelease(m_aoRaw);
    SafeRelease(m_aoRawSRV);
    SafeRelease(m_aoRawUAV);
    SafeRelease(m_gtaoCB);

    SafeRelease(m_aoSpatial);
    SafeRelease(m_aoSpatialSRV);
    SafeRelease(m_aoSpatialUAV);
    SafeRelease(m_spatialCB);

    SafeRelease(m_aoHistory[0]);
    SafeRelease(m_aoHistorySRV[0]);
    SafeRelease(m_aoHistoryUAV[0]);
    SafeRelease(m_aoHistory[1]);
    SafeRelease(m_aoHistorySRV[1]);
    SafeRelease(m_aoHistoryUAV[1]);
    SafeRelease(m_temporalCB);

    SafeRelease(m_sceneColorTex);
    SafeRelease(m_sceneColorSRV);

    SafeRelease(m_pointSampler);
    SafeRelease(m_depthSRV);
}

} // namespace SB
