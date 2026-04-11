//=============================================================================
//  SkylightingRenderer.cpp — Hemisphere-based sky visibility
//
//  Dispatch flow (per frame, PostGeometry stage):
//    0. Dispatch ProbeUpdate CS:  no-op stub (outputs 1.0 to probe grid)
//    1. Dispatch ProbeQuery CS:   no-op stub (outputs 1.0 per pixel)
//    2. Dispatch Spatial CS:      5×5 bilateral filter → denoised sky visibility
//    3. Dispatch Temporal CS:     ping-pong accumulation → stable output
//    4. Register SRV at t29
//
//  Algorithm (kSkylightScreenSpaceCS — intended to replace ProbeQuery):
//    For each pixel, march outward in N screen-space directions.  At each
//    step, reconstruct view-space positions and test whether the sample
//    occludes the pixel's sky view by comparing elevation angles against
//    the surface normal.  Directions that escape the screen or reach a sky
//    pixel count as visible.  Output: single float (0=occluded, 1=full sky).
//
//  Note: ProbeUpdate/ProbeQuery are currently no-op stubs.  The screen-
//  space algorithm is in kSkylightScreenSpaceCS (not yet wired into
//  CompileShaders — requires swapping the compiled shader and CB struct).
//=============================================================================

#include "SkylightingRenderer.h"
#include "HiZPyramid.h"
#include "SRVInjector.h"
#include "SharedGPUResources.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "MotionVectorGen.h"
#include "SceneData.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 0: ProbeUpdate CS (3D voxel grid)
// ═══════════════════════════════════════════════════════════════════════════

static const char kProbeUpdateCS[] = "#error Deploy external Shaders/Skylighting_ProbeUpdate.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 1: ProbeQuery CS (full-res per-pixel)
// ═══════════════════════════════════════════════════════════════════════════

static const char kProbeQueryCS[] = "#error Deploy external Shaders/Skylighting_ProbeQuery.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Screen-space Skylighting CS (main algorithm)
//  Not yet wired into CompileShaders — needs CB struct + compile hookup.
// ═══════════════════════════════════════════════════════════════════════════

static const char kSkylightScreenSpaceCS[] = "#error Deploy external Shaders/Skylighting_ScreenSpace.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: Spatial Denoise CS (5×5 bilateral)
// ═══════════════════════════════════════════════════════════════════════════

static const char kSpatialCS[] = "#error Deploy external Shaders/Skylighting_Spatial.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 3: Temporal Accumulation CS (ping-pong)
// ═══════════════════════════════════════════════════════════════════════════

static const char kTemporalCS[] = "#error Deploy external Shaders/Skylighting_Temporal.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) ProbeUpdateCBData
{
    float    viewProj[16];       // 64 bytes
    float    gridCenter[3];      // +64  (12 bytes)
    float    probeRangeXY;       // +76  (4 bytes)
    float    probeRangeZ;        // +80  (4 bytes)
    uint32_t probeResXY;         // +84  (4 bytes)
    uint32_t probeResZ;          // +88  (4 bytes)
    uint32_t frameIndex;         // +92  (4 bytes)
    uint32_t screenW;            // +96  (4 bytes)
    uint32_t screenH;            // +100 (4 bytes)
    float    pad[2];             // +104 (8 bytes)
};
static_assert(sizeof(ProbeUpdateCBData) == 112, "ProbeUpdateCB must be 112 bytes");

struct alignas(16) SkylightCBData
{
    float    projMatrix[16];     // 64 bytes — ProjMatrix for view-space reconstruction
    uint32_t screenW;            // +64
    uint32_t screenH;            // +68
    float    nearZ;              // +72
    float    farZ;               // +76
    float    sampleRadius;       // +80  — world-space hemisphere sample radius
    float    intensity;          // +84
    int32_t  numDirections;      // +88
    int32_t  numSteps;           // +92
    uint32_t frameIndex;         // +96
    float    fpDepthThreshold;   // +100 — first-person depth cutoff
    float    pad[2];             // +104 (8 bytes)
};
static_assert(sizeof(SkylightCBData) == 112, "SkylightCB must be 112 bytes");

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

bool SkylightingRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                      IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm = ComputeManager::Get();
    auto& pl = RenderPipeline::Get();

    if (!cm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("SkylightingRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("SkylightingRenderer: failed to get backbuffer (0x{:X})",
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

    m_pipelineHandle = pl.AddPass({
        .name     = "Skylighting",
        .stage    = PipelineStage::PostGeometry,
        .priority = 17,  // After ContactShadows (16)
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("SkylightingRenderer: initialized ({}x{}, probe grid {}x{}x{}, range XY={:.0f} Z={:.0f}, t{})",
                    m_screenW, m_screenH, kProbeResXY, kProbeResXY, kProbeResZ,
                    m_probeRangeXY, m_probeRangeZ, kSRVSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool SkylightingRenderer::CompileShaders()
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
            SKSE::log::error("SkylightingRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("SkylightingRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("Skylighting_ProbeUpdate",  kProbeUpdateCS, &m_probeUpdateCS)) return false;
    if (!CompileCS("Skylighting_ScreenSpace",   kSkylightScreenSpaceCS, &m_skylightCS)) return false;
    if (!CompileCS("Skylighting_Spatial",      kSpatialCS,     &m_spatialCS))     return false;
    if (!CompileCS("Skylighting_Temporal",     kTemporalCS,    &m_temporalCS))    return false;

    return true;
}

bool SkylightingRenderer::RecompileShaders()
{
    SafeRelease(m_probeUpdateCS);
    SafeRelease(m_skylightCS);
    SafeRelease(m_spatialCS);
    SafeRelease(m_temporalCS);
    return CompileShaders();
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool SkylightingRenderer::CreateResources()
{
    HRESULT hr;

    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R16_FLOAT, &m_skyRaw, &m_skyRawSRV, &m_skyRawUAV, "skyRaw"))
        return false;
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R16_FLOAT, &m_skySpatial, &m_skySpatialSRV, &m_skySpatialUAV, "skySpatial"))
        return false;
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R16_FLOAT, &m_skyHistory[0], &m_skyHistorySRV[0], &m_skyHistoryUAV[0], "skyHistory0"))
        return false;
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R16_FLOAT, &m_skyHistory[1], &m_skyHistorySRV[1], &m_skyHistoryUAV[1], "skyHistory1"))
        return false;

    // ── Volumetric sky probe grid: 128×128×64 R16G16B16A16_FLOAT ─────
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width     = kProbeResXY;
        desc.Height    = kProbeResXY;
        desc.Depth     = kProbeResZ;
        desc.MipLevels = 1;
        desc.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture3D(&desc, nullptr, &m_probeGrid);
        if (FAILED(hr)) {
            SKSE::log::error("SkylightingRenderer: failed to create probe grid (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        // SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MostDetailedMip = 0;
        srvDesc.Texture3D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_probeGrid, &srvDesc, &m_probeGridSRV);
        if (FAILED(hr)) return false;

        // UAV
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format                = desc.Format;
        uavDesc.ViewDimension         = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice    = 0;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize       = kProbeResZ;
        hr = m_device->CreateUnorderedAccessView(m_probeGrid, &uavDesc, &m_probeGridUAV);
        if (FAILED(hr)) return false;
    }

    // Trilinear sampler for probe grid lookup
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        hr = m_device->CreateSamplerState(&sd, &m_linearSampler);
        if (FAILED(hr)) return false;
    }

    // Constant buffers
    // Constant buffers
    if (!CreateCB(m_device, sizeof(ProbeUpdateCBData), &m_probeUpdateCB)) return false;
    if (!CreateCB(m_device, sizeof(SkylightCBData), &m_skylightCB)) return false;
    if (!CreateCB(m_device, sizeof(SpatialCBData), &m_spatialCB)) return false;
    if (!CreateCB(m_device, sizeof(TemporalCBData), &m_temporalCB)) return false;
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV
// ═══════════════════════════════════════════════════════════════════════════

bool SkylightingRenderer::AcquireDepthSRV()
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
//  GetSkylightSRV
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* SkylightingRenderer::GetSkylightSRV() const
{
    if (!m_initialized) return nullptr;
    // Return temporally accumulated output (falls back to raw if temporal not ready)
    auto* temporal = m_skyHistorySRV[1 - m_pingPongIdx];
    return temporal ? temporal : m_skyRawSRV;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution
// ═══════════════════════════════════════════════════════════════════════════

void SkylightingRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) return;

    auto& cm = ComputeManager::Get();

    // Prefer proxy-captured depth (always available during Present hook)
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    if (!depthSRV) {
        if (!AcquireDepthSRV()) return;
        depthSRV = m_depthSRV;
    }

    // HiZ is now built at PostGeometry:1 (before this pass),
    // so it's fresh during both mid-frame and Present-time dispatch.
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }

    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    // First frame: clear history
    if (m_firstFrame) {
        float clearColor[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        // R16_FLOAT 1.0 = 0x3C00
        UINT clearVal[4] = {0x3C00, 0, 0, 0};
        ctx.context->ClearUnorderedAccessViewUint(m_skyHistoryUAV[0], clearVal);
        ctx.context->ClearUnorderedAccessViewUint(m_skyHistoryUAV[1], clearVal);
        m_firstFrame = false;
        m_pingPongIdx = 0;
    }

    cm.SaveCSState();

    // Bind pre-computed linear depth at t31 for all passes
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // Pass 0 (ProbeUpdate) SKIPPED — screen-space skylighting does not read the probe grid.
    // The 128x128x64 grid resources are retained for potential future hybrid approach.

    // ═══════════════════════════════════════════════════════════════════
    //  Pass 1: Screen-space hemisphere sky visibility
    // ═══════════════════════════════════════════════════════════════════
    {
        SkylightCBData cb = {};
        std::memcpy(cb.projMatrix, scene.ProjMatrix(), sizeof(float) * 16);
        cb.screenW          = m_screenW;
        cb.screenH          = m_screenH;
        cb.nearZ            = nearZ;
        cb.farZ             = farZ;
        cb.sampleRadius     = m_sampleRadius;
        cb.intensity        = m_intensity;
        cb.numDirections    = m_numDirections;
        cb.numSteps         = m_numSteps;
        cb.frameIndex       = m_frameIndex;
        cb.fpDepthThreshold = 16.0f;

        UploadCB(ctx.context, m_skylightCB, &cb, sizeof(cb));

        // t0 = depth, t30 = blue noise (already bound globally), t31 = linear depth (already bound)
        ctx.context->CSSetShaderResources(0, 1, &depthSRV);
        auto* bnSRV = SharedGPUResources::Get().GetBlueNoiseSRV();
        if (bnSRV) ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &bnSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_skyRawUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_skylightCB);
        ctx.context->CSSetShader(m_skylightCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════
    //  Pass 2: Spatial denoise
    // ═══════════════════════════════════════════════════════════════════
    {
        SpatialCBData cb = {};
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.depthThreshold = 0.05f;

        UploadCB(ctx.context, m_spatialCB, &cb, sizeof(cb));

        ID3D11ShaderResourceView* srvs[] = { m_skyRawSRV, depthSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_skySpatialUAV, nullptr);
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

    // ═══════════════════════════════════════════════════════════════════
    //  Pass 3: Temporal accumulation
    // ═══════════════════════════════════════════════════════════════════
    {
        int readIdx  = 1 - m_pingPongIdx;
        int writeIdx = m_pingPongIdx;

        TemporalCBData cb = {};
        cb.screenW              = m_screenW;
        cb.screenH              = m_screenH;
        cb.nearZ                = nearZ;
        cb.farZ                 = farZ;
        cb.temporalAlpha        = 0.15f;  // Faster convergence for stable skylighting
        cb.depthRejectThreshold = 0.1f;
        cb.frameIndex           = m_frameIndex;

        UploadCB(ctx.context, m_temporalCB, &cb, sizeof(cb));

        // t0 = spatial, t1 = history, t2 = depth, t3 = motion vectors
        auto* motionSRV = MotionVectorGen::Get().GetMotionSRV();
        ID3D11ShaderResourceView* srvs[] = { m_skySpatialSRV, m_skyHistorySRV[readIdx], depthSRV, motionSRV };
        ctx.context->CSSetShaderResources(0, 4, srvs);
        // Bind linear sampler at s0 for bilinear history sampling
        ID3D11SamplerState* samplers[] = { m_linearSampler };
        if (m_linearSampler) ctx.context->CSSetSamplers(0, 1, samplers);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_skyHistoryUAV[writeIdx], nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_temporalCB);
        ctx.context->CSSetShader(m_temporalCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[4] = {};
        ctx.context->CSSetShaderResources(0, 4, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

        m_pingPongIdx = readIdx;
    }

    // Unbind linear depth from t31
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // Register output SRV at t29
    SRVInjector::Get().RegisterSRV(kSRVSlot, GetSkylightSRV());

    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    m_frameIndex++;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void SkylightingRenderer::Shutdown()
{
    if (!m_initialized) return;

    SRVInjector::Get().UnregisterSRV(kSRVSlot);

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("SkylightingRenderer: shut down");
}


void SkylightingRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_probeUpdateCS);
    SafeRelease(m_probeGrid);
    SafeRelease(m_probeGridSRV);
    SafeRelease(m_probeGridUAV);
    SafeRelease(m_probeUpdateCB);
    SafeRelease(m_linearSampler);

    SafeRelease(m_skylightCS);
    SafeRelease(m_spatialCS);
    SafeRelease(m_temporalCS);

    SafeRelease(m_skyRaw);
    SafeRelease(m_skyRawSRV);
    SafeRelease(m_skyRawUAV);
    SafeRelease(m_skylightCB);

    SafeRelease(m_skySpatial);
    SafeRelease(m_skySpatialSRV);
    SafeRelease(m_skySpatialUAV);
    SafeRelease(m_spatialCB);

    SafeRelease(m_skyHistory[0]);
    SafeRelease(m_skyHistorySRV[0]);
    SafeRelease(m_skyHistoryUAV[0]);
    SafeRelease(m_skyHistory[1]);
    SafeRelease(m_skyHistorySRV[1]);
    SafeRelease(m_skyHistoryUAV[1]);
    SafeRelease(m_temporalCB);

    SafeRelease(m_depthSRV);
}

} // namespace SB
