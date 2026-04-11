#include "SceneCompositor.h"
#include "GTAORenderer.h"
#include "SSRRenderer.h"
#include "SSGIRenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "VolumetricClouds.h"
#include "D3D11Hook.h"
#include <d3dcompiler.h>
#include <cstring>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Scene composite pixel shader
//
//  Calibrated against Boris's enbeffectprepass.fx composite patterns:
//    - AO: multiplicative, lerp(1, ao, intensity) — Boris uses ~0.5-0.8
//    - GI: additive but max-clamped to prevent blow-out — Boris uses subtle IL
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

static const char* kCompositePS = R"HLSL(

Texture2D<float4> SceneColor   : register(t0);
Texture2D<float4> AOBounceTex  : register(t1);  // VB-SSGI: .rgb=bounce, .a=AO
Texture2D<float4> GITex        : register(t2);
Texture2D<float4> SSRTex       : register(t3);
Texture2D<float4> CloudTex     : register(t4);
Texture2D<float>  ShadowTex    : register(t5);
Texture2D<float>  SkylightTex  : register(t6);

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);

cbuffer CompositeCB : register(b0)
{
    float aoIntensity;       // [0,1] strength of AO darkening
    float giIntensity;       // [0,1] strength of indirect light
    float ssrIntensity;      // [0,1] strength of reflections
    float cloudIntensity;    // [0,1] strength of cloud inscatter
    uint  enableFlags;       // bit 0=AO, 1=GI, 2=SSR, 3=Clouds, 4=ContactShadow, 5=Skylight
    float giMaxAdd;          // max luminance GI can add (prevents blow-out)
    float shadowIntensity;   // [0,1] strength of contact shadows
    float skylightIntensity; // [0,1] strength of skylighting
    uint  debugMode;         // 0=off, 1=AO, 2=GI, 3=SSR, 4=Clouds, 5=Shadow, 6=Skylight, 7=HDR heatmap
    uint  hdr10Enabled;     // 0=SDR, 1=HDR10
    float paperWhiteNits;   // SDR white level (200)
    float peakNits;         // Display peak (1000)
};

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// ── YCoCg → RGB decode (GTAO bounce + SSGI GI buffers use YCoCg encoding) ──
float3 YCoCgToRGB(float3 ycocg)
{
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    return float3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

// BT.709 to BT.2020 color space conversion
float3 BT709toBT2020(float3 c)
{
    // ITU-R BT.2087 conversion matrix
    return float3(
        0.6274 * c.r + 0.3293 * c.g + 0.0433 * c.b,
        0.0691 * c.r + 0.9195 * c.g + 0.0114 * c.b,
        0.0164 * c.r + 0.0880 * c.g + 0.8956 * c.b
    );
}

// PQ (ST.2084) OETF — linear light (normalized to 10000 nits) → PQ signal
float3 LinearToPQ(float3 L)
{
    // L is linear light normalized so 1.0 = 10000 nits
    float3 Lp = pow(max(L, 0.0), 0.1593017578125);  // m1 = 2610/16384
    float3 num = 0.8359375 + 18.8515625 * Lp;        // c1 + c2*Lp^m1
    float3 den = 1.0 + 18.6875 * Lp;                  // 1 + c3*Lp^m1
    return pow(num / den, 78.84375);                   // m2 = 2523/32
}

// SDR to HDR10 conversion
float3 SDRtoHDR10(float3 linearColor, float paperWhiteNits, float peakNits)
{
    // Scale linear light to nits (SDR 1.0 = paperWhiteNits)
    float3 nits = linearColor * paperWhiteNits;

    // Normalize to PQ reference (10000 nits)
    float3 normalized = nits / 10000.0;

    // Apply PQ OETF
    return LinearToPQ(normalized);
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.texcoord;
    float3 color = SceneColor.Sample(PointSampler, uv).rgb;

    // Sanitize: reject NaN/inf from scene
    if (any(isnan(color)) || any(isinf(color)))
        color = float3(0.0, 0.0, 0.0);

    float sceneLuma = Luminance(color);

    // ── AO + Bounce: multiplicative darkening + short-range indirect ──
    // VB-SSGI output: .rgb = bounce in YCoCg (Y, Co, Cg), .a = AO
    // AO: 1.0 = fully lit, 0.0 = fully occluded (multiplicative)
    // Bounce: short-range indirect illumination (additive, YCoCg encoded)
    if (enableFlags & 1u)
    {
        float4 aoBounce = AOBounceTex.Sample(PointSampler, uv);
        float ao = saturate(aoBounce.a);
        color *= lerp(1.0, ao, aoIntensity);

        // Decode YCoCg → RGB for additive bounce light
        float3 bounce = max(YCoCgToRGB(aoBounce.rgb), 0.0);
        float darkBoost = saturate(1.0 - sceneLuma * 0.5);
        color += bounce * aoIntensity * darkBoost;
    }

    // ── GI: additive indirect light (clamped) ─────────────────────────
    // Boris's IL: very subtle additive bounce. We clamp the maximum
    // contribution to prevent blow-out: GI cannot add more than
    // giMaxAdd luminance units (scaled by intensity).
    // Also scale GI by inverse scene brightness — dark areas get more
    // bounce, bright areas are already lit.
    if (enableFlags & 2u)
    {
        // SSGI outputs YCoCg — decode to RGB.  Note: SSGI's own composite PS
        // also decodes when blending directly, but SceneCompositor reads the
        // raw half-res buffer (t26) which is still in YCoCg space.
        float3 gi = max(YCoCgToRGB(GITex.Sample(LinearSampler, uv).rgb), 0.0);
        float giLuma = Luminance(gi);

        // Reinhard soft-clamp: compress GI luminance so it can't exceed giMaxAdd
        float giScale = giIntensity;
        if (giLuma > 0.001)
        {
            float clampedLuma = giLuma / (1.0 + giLuma / giMaxAdd);
            giScale *= clampedLuma / giLuma;
        }

        // Dark areas receive more bounce (Boris's implicit weighting)
        float darkBoost = saturate(1.0 - sceneLuma * 0.5);
        color += gi * giScale * darkBoost;
    }

    // ── SSR: energy-conserving reflections ─────────────────────────────
    // Boris can't do SSR (screen-space only, no material data).
    // We use Fresnel weight in alpha + energy conservation:
    // reflected light REPLACES diffuse, not adds to it.
    // lerp(diffuse, reflection, fresnel * intensity)
    if (enableFlags & 4u)
    {
        float4 ssr = SSRTex.Sample(LinearSampler, uv);
        float3 reflColor = max(ssr.rgb, 0.0);
        float  confidence = saturate(ssr.a);  // Fresnel × hit confidence

        // Energy-conserving blend: reflection replaces some diffuse
        float reflWeight = confidence * ssrIntensity;
        color = lerp(color, reflColor, reflWeight);
    }

    // ── Contact Shadows: multiplicative darkening ──────────────────────
    // Shadow mask: 1.0 = lit, 0.0 = shadowed. Same pattern as AO.
    if (enableFlags & 16u)
    {
        float shadow = saturate(ShadowTex.Sample(PointSampler, uv).r);
        color *= lerp(1.0, shadow, shadowIntensity);
    }

    // ── Skylighting: ambient modulation ──────────────────────────────
    // Sky visibility: 0.0 = fully occluded, 1.0 = full sky exposure.
    // Modulates ambient component — areas under overhangs get darker.
    if (enableFlags & 32u)
    {
        float skyVis = saturate(SkylightTex.Sample(PointSampler, uv).r);
        // Blend toward darkened ambient based on sky occlusion
        float skyFactor = lerp(1.0 - skylightIntensity * 0.5, 1.0, skyVis);
        color *= skyFactor;
    }

    // ── Clouds: over-blend via transmittance ──────────────────────────
    // Standard volumetric: color = scene * transmittance + inscatter
    if (enableFlags & 8u)
    {
        float4 cloud = CloudTex.Sample(LinearSampler, uv);
        float3 inscatter    = max(cloud.rgb, 0.0);
        float  transmittance = saturate(cloud.a);
        color = color * transmittance + inscatter * cloudIntensity;
    }

    // Final safety clamp (prevent negative or extreme values)
    color = max(color, 0.0);

    // ── Debug visualization modes ──────────────────────────────────────
    // debugMode: 0=off (normal composite), 1=AO, 2=GI, 3=SSR, 4=Clouds,
    //            5=ContactShadow, 6=Skylight, 7=HDR heatmap
    if (debugMode == 1u)
        return float4(saturate(AOBounceTex.Sample(PointSampler, uv).aaa), 1.0);
    if (debugMode == 2u)
        return float4(max(YCoCgToRGB(GITex.Sample(LinearSampler, uv).rgb), 0.0), 1.0);
    if (debugMode == 3u)
        return float4(max(SSRTex.Sample(LinearSampler, uv).rgb, 0.0), 1.0);
    if (debugMode == 4u)
        return float4(max(CloudTex.Sample(LinearSampler, uv).rgb, 0.0), 1.0);
    if (debugMode == 5u)
        return float4(saturate(ShadowTex.Sample(PointSampler, uv).rrr), 1.0);
    if (debugMode == 6u)
        return float4(saturate(SkylightTex.Sample(PointSampler, uv).rrr), 1.0);
    if (debugMode == 7u)
    {
        // HDR heatmap: blue=dark, green=mid, red=bright, white=HDR
        float lum = Luminance(color);
        float3 heat;
        if (lum < 0.1) heat = float3(0, 0, lum * 10.0);           // blue
        else if (lum < 0.5) heat = float3(0, (lum-0.1)/0.4, 0);   // green
        else if (lum < 1.0) heat = float3((lum-0.5)/0.5, 1.0-(lum-0.5)/0.5, 0); // yellow->red
        else heat = float3(1, 1, saturate((lum-1.0)/5.0));         // white
        return float4(heat, 1.0);
    }

    // ── HDR10 output path ─────────────────────────────────────────────
    if (hdr10Enabled)
    {
        // Convert BT.709 -> BT.2020
        color = BT709toBT2020(color);
        // Apply PQ transfer function
        color = SDRtoHDR10(color, paperWhiteNits, peakNits);
    }

    return float4(color, 1.0);
}

)HLSL";


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

    // ── Register pipeline pass ──────────────────────────────────────
    // PostGeometry: composite BEFORE the game's BSImageSpaceShader chain
    // reads the scene RT, so our AO/GI/SSR/etc. are visible in the final
    // output.  Priority 90 runs after all individual effects (15-25).
    m_pipelineHandle = pl.AddPass({
        .name     = "SceneCompositor",
        .stage    = PipelineStage::PostGeometry,
        .priority = 90,
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

    // Diagnostic: log every 300 frames (~5s at 60fps) for persistent monitoring
    {
        static uint32_t s_diagFrame = 0;
        if (s_diagFrame++ % 300 == 0) {
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
        if (s_flagFrame++ % 300 == 0) SKSE::log::info("SceneCompositor: compositing with flags=0x{:X}, passID={}, dbgMode={}", flags, m_compositePass, m_debugMode);
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

    ctx.context->CopyResource(m_bbCopyTex, sceneTex);
    sceneTex->Release();

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
    // For now: force enableFlags=0 to confirm the CopyResource + shader
    // roundtrip preserves the scene correctly.
    // TODO: remove this after pipeline verification.

    // ── Build constant buffer ────────────────────────────────────────
    CompositeCBData cb = {};
    cb.aoIntensity       = m_aoIntensity;
    cb.giIntensity       = m_giIntensity;
    cb.ssrIntensity      = m_ssrIntensity;
    cb.cloudIntensity    = m_cloudIntensity;
    cb.enableFlags       = flags;
    cb.giMaxAdd          = m_giMaxAdd;
    cb.shadowIntensity   = m_shadowIntensity;
    cb.skylightIntensity = m_skylightIntensity;
    cb.debugMode         = static_cast<uint32_t>(m_debugMode);
    cb.hdr10Enabled      = m_hdr10Enabled ? 1u : 0u;
    cb.paperWhiteNits    = m_paperWhiteNits;
    cb.peakNits          = m_peakNits;

    // ── Build SRV array (t0-t6) ──────────────────────────────────────
    ID3D11ShaderResourceView* srvs[7] = {
        m_bbCopySRV,   // t0: scene color
        aoSRV,         // t1: AO (may be null — shader checks enableFlags)
        giSRV,         // t2: GI
        ssrSRV,        // t3: SSR
        cloudSRV,      // t4: Clouds
        shadowSRV,     // t5: Contact shadows
        skyltSRV,      // t6: Skylighting
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
        .srvCount     = 7,
        .samplers     = samplers,
        .samplerCount = 2,
        .cbData       = &cb,
        .cbSize       = sizeof(cb),
    });

    if (ownRTV)
        sceneRTV->Release();
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
