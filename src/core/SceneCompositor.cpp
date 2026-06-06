#include "SceneCompositor.h"
#include "GTAORenderer.h"
#include "SSRRenderer.h"
#include "SSGIRenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "VolumetricClouds.h"
#include "SharedGPUResources.h"
#include "SceneData.h"
#include "D3D11Hook.h"
#include <d3dcompiler.h>
#include <cstring>
#include "GPUResource.h"
#include "WeatherParameterManager.h"

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Scene composite pixel shader
//
//  Calibrated for Skyrim SE rendering pipeline:
//    - AO: multiplicative, lerp(1, ao, intensity), ~0.5-0.8
//    - GI: additive but max-clamped to prevent blow-out, subtle IL
//    - SSR: Fresnel-weighted, energy-conserving (reflection replaces, not adds)
//    - Clouds: standard volumetric over-blend
//
//  Safety: all intermediate values clamped to prevent NaN/inf propagation.
//  Half/quarter-res inputs upsampled via bilinear.
//
//  SRV layout (bound at t0+ by RenderPassManager):
//    t0 — Scene color (backbuffer copy, full-res)
//    t1 — AO+Bounce (R16G16B16A16_FLOAT, full-res) [GTAORenderer / VB-SSGI]
//    t2 — GI (R16G16B16A16_FLOAT, half-res)   [SSGIRenderer]
//    t3 — SSR (R16G16B16A16_FLOAT, half-res)  [SSRRenderer]
//    t4 — Clouds (R16G16B16A16_FLOAT, qtr-res) [VolumetricClouds]
//    t5 — Contact Shadows (R8_UNORM, full-res) [ContactShadowRenderer]
//    t6 — Skylighting (R16_FLOAT, full-res)    [SkylightingRenderer]
// ═══════════════════════════════════════════════════════════════════════════

static const char* kCompositePS = "#error Deploy external Shaders/SceneComposite.hlsl — embedded HLSL removed\n";


// ═══════════════════════════════════════════════════════════════════════════
//  CB layout (must match HLSL, 16-byte aligned)
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) CompositeCBData
{
    float aoIntensity;
    float giIntensity;
    float ssrIntensity;
    float cloudIntensity;
    uint32_t enableFlags;
    float giMaxAdd;
    float shadowIntensity;
    float skylightIntensity;
    uint32_t debugMode;
    uint32_t hdr10Enabled;
    float paperWhiteNits;
    float peakNits;
    float screenDimsX;
    float screenDimsY;
    float nearZ;
    float farZ;
};


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool SceneCompositor::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                  IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    m_device  = dev;
    m_context = ctx;

    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    // ── Register the composite fullscreen pass ───────────────────────
    m_compositePass = rpm.RegisterPass({
        .name     = "SceneComposite",
        .psSource = kCompositePS,
    });

    if (!m_compositePass) {
        SKSE::log::error("SceneCompositor: failed to compile composite PS");
        return false;
    }

    // ── Create backbuffer copy texture + SRV ─────────────────────────
    {
        ID3D11Texture2D* backTex = nullptr;
        HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&backTex));
        if (FAILED(hr) || !backTex) {
            SKSE::log::error("SceneCompositor: failed to get backbuffer");
            return false;
        }

        D3D11_TEXTURE2D_DESC bbDesc;
        backTex->GetDesc(&bbDesc);
        backTex->Release();

        bbDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
        bbDesc.Usage          = D3D11_USAGE_DEFAULT;
        bbDesc.CPUAccessFlags = 0;
        bbDesc.MiscFlags      = 0;

        hr = dev->CreateTexture2D(&bbDesc, nullptr, &m_bbCopyTex);
        if (FAILED(hr)) {
            SKSE::log::error("SceneCompositor: failed to create BB copy tex");
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = bbDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels       = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        hr = dev->CreateShaderResourceView(m_bbCopyTex, &srvDesc, &m_bbCopySRV);
        if (FAILED(hr)) {
            SKSE::log::error("SceneCompositor: failed to create BB copy SRV");
            m_bbCopyTex->Release();
            m_bbCopyTex = nullptr;
            return false;
        }
    }

    // ── Samplers ─────────────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;

        // s0: point (full-res exact reads)
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        dev->CreateSamplerState(&sd, &m_pointSampler);

        // s1: bilinear (half/quarter-res upsample)
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        dev->CreateSamplerState(&sd, &m_linearSampler);
    }

    // ── Register compositor at PostGeometry ──────────────────────────
    // Composites ALL effects (AO, CS, GI, SSR, Skylighting) onto the
    // game's live scene RT at PostGeometry, BEFORE the game's own post-
    // processing (tonemapping, gamma). This ensures correct blending:
    // multiplicative AO operates on linear HDR scene color, not the
    // already-tonemapped backbuffer.
    //
    // ExecutePass handles both cases:
    //   - Mid-frame: ctx.gameSceneRTV is the game's active scene RT
    //   - PrePresent fallback: uses swapchain backbuffer if scene RT is null
    // Register compositor at PrePresent — composites onto the backbuffer.
    // Effects run at PostGeometry (via PhaseDispatcher) and their output SRVs
    // persist in the renderers. The compositor reads them at PrePresent.
    //
    // Why PrePresent, not PostGeometry:
    //   PostGeometry gives us the game's live scene RT but CopyResource fails
    //   when format mismatches (game uses R16G16B16A16F, copy was R8G8B8A8).
    //   PrePresent uses the backbuffer which has a known/stable format.
    m_pipelineHandle = pl.AddPass({
        .name     = "SceneCompositor",
        .stage    = PipelineStage::PrePresent,
        .priority = 400,
        .enabled  = true,
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    SKSE::log::info("SceneCompositor: initialized (pass={}, pipeline={})",
        m_compositePass, m_pipelineHandle);
    SKSE::log::info("  Defaults: AO={:.2f}, GI={:.2f}, SSR={:.2f}, Cloud={:.2f}, giMaxAdd={:.2f}",
        m_aoIntensity, m_giIntensity, m_ssrIntensity, m_cloudIntensity, m_giMaxAdd);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution
// ═══════════════════════════════════════════════════════════════════════════

void SceneCompositor::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;
    m_executedThisFrame = true; // Prevent PrePresent fallback from double-compositing

    // ── Collect SRVs from subsystems ─────────────────────────────────
    auto& gtao    = GTAORenderer::Get();
    auto& ssgi    = SSGIRenderer::Get();
    auto& ssr     = SSRRenderer::Get();
    auto& clouds  = VolumetricClouds::Get();
    auto& cshadow = ContactShadowRenderer::Get();
    auto& skylit  = SkylightingRenderer::Get();

    ID3D11ShaderResourceView* aoSRV     = (gtao.IsInitialized()    && gtao.IsEnabled())    ? gtao.GetOutputSRV()        : nullptr;
    ID3D11ShaderResourceView* giSRV     = (ssgi.IsInitialized()    && ssgi.IsEnabled())    ? ssgi.GetGISRV()            : nullptr;
    ID3D11ShaderResourceView* ssrSRV    = (ssr.IsInitialized()     && ssr.IsEnabled())     ? ssr.GetReflectionSRV()     : nullptr;
    ID3D11ShaderResourceView* cloudSRV  = (clouds.IsInitialized()  && clouds.IsEnabled())  ? clouds.GetCloudSRV()       : nullptr;
    ID3D11ShaderResourceView* shadowSRV = (cshadow.IsInitialized() && cshadow.IsEnabled()) ? cshadow.GetShadowSRV()     : nullptr;
    ID3D11ShaderResourceView* skyltSRV  = (skylit.IsInitialized()  && skylit.IsEnabled())  ? skylit.GetSkylightSRV()    : nullptr;

    // Diagnostic: log every 1800 frames (~30s at 60fps) for persistent monitoring
    {
        static uint32_t s_diagFrame = 0;
        if (s_diagFrame++ % 1800 == 0) {
            SKSE::log::info("SceneCompositor[f{}]: AO(init={} en={} srv={}) CS(init={} en={} srv={}) "
                "Sky(init={} en={} srv={}) SSR(init={} en={} srv={}) GI(init={} en={} srv={}) "
                "Clouds(init={} en={} srv={}) gameRTV={}",
                s_diagFrame,
                gtao.IsInitialized(), gtao.IsEnabled(), (void*)aoSRV,
                cshadow.IsInitialized(), cshadow.IsEnabled(), (void*)shadowSRV,
                skylit.IsInitialized(), skylit.IsEnabled(), (void*)skyltSRV,
                ssr.IsInitialized(), ssr.IsEnabled(), (void*)ssrSRV,
                ssgi.IsInitialized(), ssgi.IsEnabled(), (void*)giSRV,
                clouds.IsInitialized(), clouds.IsEnabled(), (void*)cloudSRV,
                (void*)ctx.gameSceneRTV);
        }
    }

    // Build enable flags (only enable effects that have valid SRV output)
    uint32_t flags = 0;
    if (aoSRV)     flags |= 1u;
    if (giSRV)     flags |= 2u;
    if (ssrSRV)    flags |= 4u;
    if (cloudSRV)  flags |= 8u;
    if (shadowSRV) flags |= 16u;
    if (skyltSRV)  flags |= 32u;

    // Nothing to composite — early out
    if (flags == 0) {
        static uint32_t s_noFlagFrame = 0;
        if (s_noFlagFrame++ % 600 == 0) SKSE::log::warn("SceneCompositor: flags==0, no SRVs available — skipping composite (frame {})", s_noFlagFrame);
        return;
    }
    {
        static uint32_t s_flagFrame = 0;
        if (s_flagFrame++ % 1800 == 0) SKSE::log::info("SceneCompositor: compositing with flags=0x{:X}, passID={}, dbgMode={}", flags, m_compositePass, m_debugMode);
    }

    // ── Copy scene color → temp texture ─────────────────────────────
    // Mid-frame: extract the scene texture from the game's active RTV.
    // PrePresent fallback: use the swapchain backbuffer.
    ID3D11Texture2D* sceneTex = nullptr;
    ID3D11RenderTargetView* sceneRTV = nullptr;
    bool ownRTV = false;  // true if we created the RTV and must release it

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
        // Don't AddRef — backup keeps it alive during ExecuteStage
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
        rtvDesc.Format        = texDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        m_device->CreateRenderTargetView(sceneTex, &rtvDesc, &sceneRTV);
        ownRTV = true;
    }

    if (!sceneTex || !sceneRTV) {
        if (sceneTex) sceneTex->Release();
        if (ownRTV && sceneRTV) sceneRTV->Release();
        return;
    }

    // ── Guard: only composite onto full-color scene RTs ─────────────
    // The phase detector sometimes fires when a non-scene RT is bound
    // (e.g., 2-channel temp textures). Compositing onto those would
    // corrupt game state and cause black smearing artifacts.
    {
        D3D11_TEXTURE2D_DESC guardDesc;
        sceneTex->GetDesc(&guardDesc);
        bool validSceneRT = (guardDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                             guardDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                             guardDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                             guardDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
                             guardDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM);
        if (!validSceneRT) {
            static uint32_t s_skipLog = 0;
            if (s_skipLog++ < 10)
                SKSE::log::warn("SceneCompositor: skipping non-scene RT (fmt={}, {}x{})",
                    static_cast<int>(guardDesc.Format), guardDesc.Width, guardDesc.Height);
            sceneTex->Release();
            if (ownRTV) sceneRTV->Release();
            return;
        }
    }

    // ── Ensure copy texture matches the game's scene RT format/size ──
    // The backbuffer is R8G8B8A8_UNORM but the game's internal scene RT
    // is often R16G16B16A16_FLOAT.  CopyResource requires identical
    // format+dimensions, so we lazily recreate the copy texture if needed.
    {
        D3D11_TEXTURE2D_DESC sceneDesc;
        sceneTex->GetDesc(&sceneDesc);

        D3D11_TEXTURE2D_DESC copyDesc;
        m_bbCopyTex->GetDesc(&copyDesc);

        if (sceneDesc.Format != copyDesc.Format ||
            sceneDesc.Width  != copyDesc.Width  ||
            sceneDesc.Height != copyDesc.Height)
        {
            SKSE::log::info("SceneCompositor: scene RT format/size changed — "
                "scene={}x{} fmt={}, copy={}x{} fmt={} — recreating",
                sceneDesc.Width, sceneDesc.Height, static_cast<int>(sceneDesc.Format),
                copyDesc.Width, copyDesc.Height, static_cast<int>(copyDesc.Format));

            if (m_bbCopySRV) { m_bbCopySRV->Release(); m_bbCopySRV = nullptr; }
            if (m_bbCopyTex) { m_bbCopyTex->Release(); m_bbCopyTex = nullptr; }

            D3D11_TEXTURE2D_DESC newDesc = sceneDesc;
            newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            newDesc.Usage          = D3D11_USAGE_DEFAULT;
            newDesc.CPUAccessFlags = 0;
            newDesc.MiscFlags      = 0;

            HRESULT hr = m_device->CreateTexture2D(&newDesc, nullptr, &m_bbCopyTex);
            if (FAILED(hr)) {
                SKSE::log::error("SceneCompositor: failed to recreate copy tex");
                sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = newDesc.Format;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels       = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            hr = m_device->CreateShaderResourceView(m_bbCopyTex, &srvDesc, &m_bbCopySRV);
            if (FAILED(hr)) {
                SKSE::log::error("SceneCompositor: failed to recreate copy SRV");
                m_bbCopyTex->Release();
                m_bbCopyTex = nullptr;
                sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }
        }
    }

    // Unbind the scene RT from OM before copying — CopyResource silently
    // fails if the source texture is still bound as a render target.
    ID3D11RenderTargetView* nullRTVs[1] = { nullptr };
    ctx.context->OMSetRenderTargets(1, nullRTVs, nullptr);

    ctx.context->CopyResource(m_bbCopyTex, sceneTex);

    // ── DIAGNOSTIC: force scene darken to verify shader pipeline ─────
    // Uses only t0 (scene copy) — no effect SRV dependency.
    // enableFlags=0 → composite shader skips all effects, just outputs
    // scene color unmodified.  We set debugMode=0 and all intensities
    // to zero EXCEPT we force AO on with a null SRV (shader reads 0 →
    // darkens by 60%).
    //
    // Actually, simplest: set enableFlags = 0, debugMode = 0, shader
    // returns scene color as-is.  If scene looks NORMAL = shader pipeline
    // works.  Then we test with enableFlags = 1 (AO only) and intentionally
    // pass a black SRV = scene should darken.
    //
    // ── Build constant buffer (weather-modulated intensities) ────────
    const auto& wp = WeatherParameterManager::Get().GetCurrent();
    CompositeCBData cb = {};
    cb.aoIntensity       = m_aoIntensity       * wp.aoIntensity;
    cb.giIntensity       = m_giIntensity       * wp.giIntensity;
    cb.ssrIntensity      = m_ssrIntensity      * wp.ssrIntensity;
    cb.cloudIntensity    = m_cloudIntensity;
    cb.enableFlags       = flags;
    cb.giMaxAdd          = m_giMaxAdd;
    cb.shadowIntensity   = m_shadowIntensity   * wp.contactShadowStr;
    cb.skylightIntensity = m_skylightIntensity * wp.skylightIntensity;
    cb.debugMode         = static_cast<uint32_t>(m_debugMode);
    cb.hdr10Enabled      = m_hdr10Enabled ? 1u : 0u;
    cb.paperWhiteNits    = m_paperWhiteNits;
    cb.peakNits          = m_peakNits;
    // Get screen dims from backbuffer copy texture
    float screenW = 1920.0f, screenH = 1080.0f;
    if (m_bbCopyTex) {
        D3D11_TEXTURE2D_DESC bbDesc;
        m_bbCopyTex->GetDesc(&bbDesc);
        screenW = static_cast<float>(bbDesc.Width);
        screenH = static_cast<float>(bbDesc.Height);
    }
    cb.screenDimsX       = screenW;
    cb.screenDimsY       = screenH;
    auto& sm = SceneMatrices::Get();
    cb.nearZ             = sm.IsValid() ? sm.NearClip() : 15.0f;
    cb.farZ              = sm.IsValid() ? sm.FarClip()  : 353840.0f;

    // ── Build SRV array (t0-t8) ──────────────────────────────────────
    ID3D11ShaderResourceView* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();

    // Raw depth from interception (live game depth, standard Z: near=0 far=1)
    ID3D11ShaderResourceView* rawDepthSRV = D3D11Hook::GetGameDepthSRV();

    ID3D11ShaderResourceView* srvs[9] = {
        m_bbCopySRV,     // t0: scene color
        aoSRV,           // t1: AO
        giSRV,           // t2: GI
        ssrSRV,          // t3: SSR
        cloudSRV,        // t4: Clouds
        shadowSRV,       // t5: Contact shadows
        skyltSRV,        // t6: Skylighting
        linearDepthSRV,  // t7: Linear depth (for debug viz)
        rawDepthSRV,     // t8: Raw game depth (intercepted SRV)
    };

    ID3D11SamplerState* samplers[2] = {
        m_pointSampler,   // s0
        m_linearSampler,  // s1
    };

    // ── Execute fullscreen composite pass ────────────────────────────
    RenderPassManager::Get().Execute({
        .passID       = m_compositePass,
        .rtv          = sceneRTV,
        .srvs         = srvs,
        .srvCount     = 9,
        .samplers     = samplers,
        .samplerCount = 2,
        .cbData       = &cb,
        .cbSize       = sizeof(cb),
    });

    if (ownRTV)
        sceneRTV->Release();
}


// ═══════════════════════════════════════════════════════════════════════════
//  Debug overlay at PrePresent — draws AFTER all game rendering
//  Only activates when a debug visualization mode is set (debugMode > 0).
//  Renders directly to the backbuffer so nothing can overwrite it.
// ═══════════════════════════════════════════════════════════════════════════

void SceneCompositor::ExecuteDebugOverlay(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Don't composite during main menu / loading — effects haven't run yet,
    // their output buffers are zero, and multiplying by zero = black screen.
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->Is3DLoaded()) return;

    auto* sc = ctx.swapChain;
    if (!sc) sc = D3D11Hook::GetSwapChain();
    if (!sc) return;

    // Get the backbuffer as our render target
    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&backbuffer))))
        return;

    D3D11_TEXTURE2D_DESC bbDesc;
    backbuffer->GetDesc(&bbDesc);

    ID3D11RenderTargetView* bbRTV = nullptr;
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = bbDesc.Format;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    HRESULT hr = m_device->CreateRenderTargetView(backbuffer, &rtvDesc, &bbRTV);
    if (FAILED(hr)) { backbuffer->Release(); return; }

    // Ensure copy texture matches backbuffer
    if (m_bbCopyTex) {
        D3D11_TEXTURE2D_DESC copyDesc;
        m_bbCopyTex->GetDesc(&copyDesc);
        if (copyDesc.Width != bbDesc.Width || copyDesc.Height != bbDesc.Height ||
            copyDesc.Format != bbDesc.Format) {
            if (m_bbCopySRV) { m_bbCopySRV->Release(); m_bbCopySRV = nullptr; }
            if (m_bbCopyTex) { m_bbCopyTex->Release(); m_bbCopyTex = nullptr; }
        }
    }
    if (!m_bbCopyTex) {
        D3D11_TEXTURE2D_DESC newDesc = bbDesc;
        newDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        newDesc.Usage = D3D11_USAGE_DEFAULT;
        newDesc.CPUAccessFlags = 0;
        newDesc.MiscFlags = 0;
        m_device->CreateTexture2D(&newDesc, nullptr, &m_bbCopyTex);
        if (m_bbCopyTex) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvD = {};
            srvD.Format = newDesc.Format;
            srvD.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvD.Texture2D.MipLevels = 1;
            m_device->CreateShaderResourceView(m_bbCopyTex, &srvD, &m_bbCopySRV);
        }
    }

    if (!m_bbCopyTex || !m_bbCopySRV) {
        bbRTV->Release(); backbuffer->Release();
        return;
    }

    // Copy backbuffer → SRV texture (scene color for t0)
    ctx.context->CopyResource(m_bbCopyTex, backbuffer);

    // Build CB — full composite when debugMode==0, debug viz when > 0
    CompositeCBData cb = {};

    // Build enable flags from available SRVs
    // NOTE: GetOutputSRV returns nullptr if the renderer is not initialized
    ID3D11ShaderResourceView* aoSRV    = GTAORenderer::Get().GetOutputSRV();
    {
        static uint32_t s_compLog = 0;
        if (s_compLog++ < 10)
            SKSE::log::info("Compositor: ao={} cs={} sky={} ssr={} gi={} enabled={} dbg={}",
                (void*)aoSRV,
                (void*)ContactShadowRenderer::Get().GetShadowSRV(),
                (void*)SkylightingRenderer::Get().GetSkylightSRV(),
                (void*)SSRRenderer::Get().GetReflectionSRV(),
                (void*)SSGIRenderer::Get().GetGISRV(),
                m_enabled, m_debugMode);
    }
    ID3D11ShaderResourceView* giSRV    = SSGIRenderer::Get().GetGISRV();
    ID3D11ShaderResourceView* ssrSRV   = SSRRenderer::Get().GetReflectionSRV();
    ID3D11ShaderResourceView* shadowSRV = ContactShadowRenderer::Get().GetShadowSRV();
    ID3D11ShaderResourceView* skyltSRV = SkylightingRenderer::Get().GetSkylightSRV();

    uint32_t flags = 0;
    if (aoSRV)     flags |= 1u;
    if (giSRV)     flags |= 2u;
    if (ssrSRV)    flags |= 4u;
    if (shadowSRV) flags |= 16u;
    if (skyltSRV)  flags |= 32u;

    const auto& wp = WeatherParameterManager::Get().GetCurrent();
    auto& sm2 = SceneMatrices::Get();
    cb.aoIntensity       = m_aoIntensity       * wp.aoIntensity;
    cb.giIntensity       = m_giIntensity       * wp.giIntensity;
    cb.ssrIntensity      = m_ssrIntensity      * wp.ssrIntensity;
    cb.cloudIntensity    = m_cloudIntensity;
    cb.enableFlags       = flags;
    cb.giMaxAdd          = m_giMaxAdd;
    cb.shadowIntensity   = m_shadowIntensity   * wp.contactShadowStr;
    cb.skylightIntensity = m_skylightIntensity * wp.skylightIntensity;
    cb.debugMode     = static_cast<uint32_t>(m_debugMode);
    cb.hdr10Enabled  = m_hdr10Enabled ? 1u : 0u;
    cb.paperWhiteNits = m_paperWhiteNits;
    cb.peakNits      = m_peakNits;
    cb.screenDimsX   = static_cast<float>(bbDesc.Width);
    cb.screenDimsY   = static_cast<float>(bbDesc.Height);
    cb.nearZ         = sm2.IsValid() ? sm2.NearClip() : 15.0f;
    cb.farZ          = sm2.IsValid() ? sm2.FarClip()  : 353840.0f;

    // Build SRV array
    ID3D11ShaderResourceView* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ID3D11ShaderResourceView* rawDepthSRV = D3D11Hook::GetGameDepthSRV();
    ID3D11ShaderResourceView* srvs[9] = {
        m_bbCopySRV,     // t0: scene color
        aoSRV,           // t1: AO
        giSRV,           // t2: GI
        ssrSRV,          // t3: SSR
        nullptr,         // t4: Clouds
        shadowSRV,       // t5: Shadows
        skyltSRV,        // t6: Skylighting
        linearDepthSRV,  // t7: Linear depth
        rawDepthSRV,     // t8: Raw game depth
    };
    ID3D11SamplerState* samplers[2] = { m_pointSampler, m_linearSampler };

    RenderPassManager::Get().Execute({
        .passID       = m_compositePass,
        .rtv          = bbRTV,
        .srvs         = srvs,
        .srvCount     = 9,
        .samplers     = samplers,
        .samplerCount = 2,
        .cbData       = &cb,
        .cbSize       = sizeof(cb),
    });

    bbRTV->Release();
    backbuffer->Release();
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void SceneCompositor::Shutdown()
{
    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    if (m_bbCopySRV)      { m_bbCopySRV->Release();      m_bbCopySRV = nullptr; }
    if (m_bbCopyTex)      { m_bbCopyTex->Release();      m_bbCopyTex = nullptr; }
    if (m_pointSampler)   { m_pointSampler->Release();   m_pointSampler = nullptr; }
    if (m_linearSampler)  { m_linearSampler->Release();  m_linearSampler = nullptr; }

    m_compositePass = 0;
    m_initialized   = false;
}

} // namespace SB
