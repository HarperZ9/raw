//=============================================================================
//  ContactShadowRenderer.cpp — Screen-Space Contact Shadows
//
//  Dispatch flow (per frame, PostGeometry stage):
//    1. Dispatch Shadow CS:  per-pixel screen-space ray march toward sun → raw mask
//    2. Dispatch Denoise CS:  5x5 bilateral filter → denoised shadow mask
//    3. Register SRV at t28 for shader passes
//
//  Algorithm:
//    For each pixel, project the sun direction into screen space.  March a
//    ray along that 2D direction, testing depth at each step.  If the sampled
//    depth is closer than the ray's expected depth (within a thickness
//    tolerance), the pixel is shadowed.  This produces soft contact shadows
//    at object boundaries that vanilla Skyrim's shadow maps completely miss.
//
//  Quality comparable to Community Shaders' screen-space shadows but runs
//  as a standalone compute pass with no BSShader hooks required.
//=============================================================================

#include "ContactShadowRenderer.h"
#include "HiZPyramid.h"
#include "SRVInjector.h"
#include "SharedGPUResources.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "SceneData.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 1: Contact Shadow CS (full-res)
// ═══════════════════════════════════════════════════════════════════════════

static const char kContactShadowCS[] = "#error Deploy external Shaders/ContactShadow_Main.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: Spatial Denoise CS (full-res, 5×5 bilateral)
// ═══════════════════════════════════════════════════════════════════════════

static const char kDenoiseCS[] = "#error Deploy external Shaders/ContactShadow_Denoise.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) ContactShadowCBData
{
    float    viewMatrix[16];     // 64 bytes — row-major 4x4
    float    projMatrix[16];     // 64 bytes — row-major 4x4
    uint32_t screenW;            // +128
    uint32_t screenH;            // +132
    float    nearZ;              // +136
    float    farZ;               // +140
    float    sunDirWorld[3];     // +144
    float    rayLength;          // +156
    float    thickness;          // +160
    float    intensity;          // +164
    int32_t  maxSteps;           // +168
    uint32_t frameIndex;         // +172
    float    fpDepthThreshold;   // +176 — first-person depth mask (view-space units)
    float    pad;                // +180  → total 192 (12 * 16)
};
static_assert(sizeof(ContactShadowCBData) == 192, "ContactShadowCB must be 192 bytes");

struct alignas(16) DenoiseCBData
{
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    depthThreshold;
    float    pad[3];
};
static_assert(sizeof(DenoiseCBData) == 32, "DenoiseCB must be 32 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool ContactShadowRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                         IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm = ComputeManager::Get();
    auto& pl = RenderPipeline::Get();

    if (!cm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("ContactShadowRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("ContactShadowRenderer: failed to get backbuffer (0x{:X})",
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

    // Register as PostGeometry pipeline pass
    m_pipelineHandle = pl.AddPass({
        .name     = "ContactShadows",
        .stage    = PipelineStage::PostGeometry,
        .priority = 16,  // After GTAO (15), alongside other screen-space effects
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_frameIndex  = 0;

    SKSE::log::info("ContactShadowRenderer: initialized ({}x{}, steps={}, rayLen={:.2f}, t{})",
                    m_screenW, m_screenH, m_maxSteps, m_rayLength, kSRVSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool ContactShadowRenderer::CompileShaders()
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
            SKSE::log::error("ContactShadowRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("ContactShadowRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("ContactShadow_Main",   kContactShadowCS, &m_shadowCS))  return false;
    if (!CompileCS("ContactShadow_Denoise",kDenoiseCS,        &m_denoiseCS)) return false;

    return true;
}

bool ContactShadowRenderer::RecompileShaders()
{
    SafeRelease(m_shadowCS);
    SafeRelease(m_denoiseCS);
    return CompileShaders();
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool ContactShadowRenderer::CreateResources()
{

    // Pass 1 output: raw shadow mask
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R8_UNORM, &m_shadowRaw, &m_shadowRawSRV, &m_shadowRawUAV, "shadowRaw"))
        return false;

    // Pass 2 output: denoised shadow mask
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R8_UNORM, &m_shadowFinal, &m_shadowFinalSRV, &m_shadowFinalUAV, "shadowFinal"))
        return false;

    // Constant buffers
    // Constant buffers
    if (!CreateCB(m_device, sizeof(ContactShadowCBData), &m_shadowCB)) return false;
    if (!CreateCB(m_device, sizeof(DenoiseCBData), &m_denoiseCB)) return false;
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV from currently-bound DSV
// ═══════════════════════════════════════════════════════════════════════════

bool ContactShadowRenderer::AcquireDepthSRV()
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
//  GetShadowSRV — returns the final denoised shadow mask
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* ContactShadowRenderer::GetShadowSRV() const
{
    if (!m_initialized) return nullptr;
    // Return spatially denoised output for cleaner shadows
    return m_shadowFinalSRV ? m_shadowFinalSRV : m_shadowRawSRV;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PostGeometry stage)
// ═══════════════════════════════════════════════════════════════════════════

void ContactShadowRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) return;

    auto& cm = ComputeManager::Get();

    // ── Acquire depth ───────────────────────────────────────────────────
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

    cm.SaveCSState();

    // Bind pre-computed linear depth at CS t31
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: Contact Shadow ray march
    // ═════════════════════════════════════════════════════════════════════
    {
        ContactShadowCBData cb = {};
        std::memcpy(cb.viewMatrix, scene.ViewMatrix(), sizeof(float) * 16);
        std::memcpy(cb.projMatrix, scene.ProjMatrix(), sizeof(float) * 16);
        cb.screenW       = m_screenW;
        cb.screenH       = m_screenH;
        cb.nearZ         = nearZ;
        cb.farZ          = farZ;
        cb.sunDirWorld[0]= scene.SunDirection()[0];
        cb.sunDirWorld[1]= scene.SunDirection()[1];
        cb.sunDirWorld[2]= scene.SunDirection()[2];
        cb.rayLength     = m_rayLength;
        cb.thickness     = m_thickness;
        cb.intensity     = m_intensity;
        cb.maxSteps      = m_maxSteps;
        cb.frameIndex    = m_frameIndex;
        cb.fpDepthThreshold = 0.0f;  // disabled — let all geometry cast contact shadows

        UploadCB(ctx.context, m_shadowCB, &cb, sizeof(cb));

        ctx.context->CSSetShaderResources(0, 1, &depthSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_shadowRawUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_shadowCB);
        ctx.context->CSSetShader(m_shadowCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear bindings
        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: Spatial denoise — 5×5 bilateral filter
    // ═════════════════════════════════════════════════════════════════════
    {
        DenoiseCBData cb = {};
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.depthThreshold = 0.03f;

        UploadCB(ctx.context, m_denoiseCB, &cb, sizeof(cb));

        // t0 = raw shadow, t1 = depth
        ID3D11ShaderResourceView* srvs[] = { m_shadowRawSRV, depthSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_shadowFinalUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_denoiseCB);
        ctx.context->CSSetShader(m_denoiseCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // Unbind linear depth
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // Register output SRV at t28
    SRVInjector::Get().RegisterSRV(kSRVSlot, GetShadowSRV());

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

void ContactShadowRenderer::Shutdown()
{
    if (!m_initialized) return;

    SRVInjector::Get().UnregisterSRV(kSRVSlot);

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("ContactShadowRenderer: shut down");
}


void ContactShadowRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_shadowCS);
    SafeRelease(m_denoiseCS);

    SafeRelease(m_shadowRaw);
    SafeRelease(m_shadowRawSRV);
    SafeRelease(m_shadowRawUAV);
    SafeRelease(m_shadowCB);

    SafeRelease(m_shadowFinal);
    SafeRelease(m_shadowFinalSRV);
    SafeRelease(m_shadowFinalUAV);
    SafeRelease(m_denoiseCB);

    SafeRelease(m_depthSRV);
}

} // namespace SB
