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

namespace SB
{

// ── Autoexposure compute shader ───────────────────────────────────────────
// Reads histogram result buffer, computes target EV, temporal smoothing
static const char kAutoExposureCS[] = R"HLSL(
cbuffer AutoExpCB : register(b0)
{
    float PrevEV;
    float DeltaTime;
    float AdaptSpeed;
    float ExposureComp;
    float MinEV;
    float MaxEV;
    float pad0, pad1;
};

// Histogram stats from LuminanceHistogram (bound as SRV)
// Layout: [avgLum, minLum, maxLum, p05, p50, p95, avgR, avgG, avgB, ...]
StructuredBuffer<float> HistogramStats : register(t0);

RWStructuredBuffer<float> ExposureOut : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float avgLum = HistogramStats[0];
    float p50    = HistogramStats[4];  // median luminance

    // Use geometric mean of average and median for robust metering
    float keyLum = sqrt(max(avgLum, 0.001) * max(p50, 0.001));

    // Target EV such that keyLum maps to 18% grey (0.18)
    // EV = log2(keyLum / 0.18)
    float targetEV = log2(max(keyLum, 0.0001) / 0.18);

    // Apply exposure compensation
    targetEV -= ExposureComp;

    // Clamp to sane range
    targetEV = clamp(targetEV, MinEV, MaxEV);

    // Temporal adaptation (exponential smoothing)
    float speed = AdaptSpeed * DeltaTime;
    speed = clamp(speed, 0.0, 1.0);

    float newEV = lerp(PrevEV, targetEV, speed);

    ExposureOut[0] = newEV;
}
)HLSL";

// ── Tone mapping pixel shader ─────────────────────────────────────────────
static const char kToneMapPS[] = R"HLSL(
cbuffer ToneMapCB : register(b0)
{
    float CurrentEV;
    int   CurveType;       // 0=AgX, 1=ACES, 2=Reinhard, 3=None
    int   HDROutput;       // 0=SDR, 1=HDR
    float PaperWhiteNits;
    float MaxNits;
    float VanillaInfluence;
    float Padding0, Padding1;
};

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
};

Texture2D<float4> SceneColor : register(t0);
SamplerState PointSampler : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// ── AgX tone mapping ──────────────────────────────────────────────────────
// Based on Troy Sobotka's AgX, a perceptual tone mapper that handles
// saturated colors better than ACES (no purple fringing on bright blues)

float3 AgXDefaultContrastApprox(float3 x)
{
    // 6th-order polynomial approximation of the AgX log-contrast curve
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}

float3 AgX(float3 color)
{
    // AgX transform (log space encoding → contrast → inverse)
    const float3x3 AgXInsetMatrix = float3x3(
        0.856627, 0.137318, 0.11189,
        0.0951212, 0.761241, 0.0767994,
        0.0482516, 0.101439, 0.811302
    );
    const float3x3 AgXOutsetMatrix = float3x3(
        1.1271,   -0.1413,  -0.14132,
        -0.11060,  1.15785, -0.11060,
        -0.016507,-0.016507, 1.25194
    );

    const float MinEV = -12.47393;
    const float MaxEV = 4.026069;

    color = mul(AgXInsetMatrix, color);
    color = max(color, 1e-10);
    color = log2(color);
    color = (color - MinEV) / (MaxEV - MinEV);
    color = saturate(color);
    color = AgXDefaultContrastApprox(color);
    color = mul(AgXOutsetMatrix, color);
    color = saturate(color);
    return color;
}

// ── ACES fitted approximation (Stephen Hill) ──────────────────────────────
float3 ACESFitted(float3 color)
{
    const float3x3 ACESInputMat = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );
    const float3x3 ACESOutputMat = float3x3(
        1.60475, -0.53108, -0.07367,
        -0.10208, 1.10813, -0.00605,
        -0.00327, -0.07276, 1.07602
    );

    color = mul(ACESInputMat, color);
    // RRT + ODT fit
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;
    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// ── Extended Reinhard ─────────────────────────────────────────────────────
float3 ReinhardExtended(float3 color, float maxWhite)
{
    float3 num = color * (1.0 + color / (maxWhite * maxWhite));
    return num / (1.0 + color);
}

// ── PQ (Perceptual Quantizer, ST.2084) for HDR10 output ──────────────────
float3 LinearToPQ(float3 L)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    float3 Lp = pow(saturate(L / 10000.0), m1);
    return pow((c1 + c2 * Lp) / (1.0 + c3 * Lp), m2);
}

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;
    float3 color = SceneColor.Sample(PointSampler, uv).rgb;

    // Skyrim's internal scene RT is in gamma ~1.6 space.
    // Linearize before tonemapping so operators receive true linear input.
    static const float SKYRIM_GAMMA = 1.6;
    color = pow(max(color, 0.0), SKYRIM_GAMMA);

    // Apply exposure (convert EV to multiplier)
    float exposure = exp2(-CurrentEV);
    color *= exposure;

    if (HDROutput)
    {
        // HDR path: tone map to preserve highlights, then encode as PQ
        // Scale to nits (paper white = reference white)
        float3 nits = color * PaperWhiteNits;

        // Soft highlight compression (preserves more dynamic range than hard clip)
        float maxC = max(max(nits.r, nits.g), nits.b);
        if (maxC > MaxNits)
        {
            float compress = MaxNits / maxC;
            compress = lerp(compress, 1.0, 0.1); // partial compression, keep some rolloff
            nits *= compress;
        }

        // Vanilla ImageSpace adjustments (weather-aware)
        if (VanillaInfluence > 0.0)
        {
            float vLum = dot(nits, float3(0.2126, 0.7152, 0.0722));
            float vSat = lerp(1.0, VP_Saturation, VanillaInfluence);
            nits = lerp(vLum.xxx, nits, vSat);
            nits *= lerp(1.0, VP_Brightness, VanillaInfluence);
            nits = lerp(0.5 * PaperWhiteNits, nits, lerp(1.0, VP_Contrast, VanillaInfluence));
            float3 vTint = float3(VP_TintR, VP_TintG, VP_TintB);
            nits = lerp(nits, nits * vTint, VP_TintAmount * VanillaInfluence);
        }

        return float4(LinearToPQ(nits), 1.0);
    }
    else
    {
        // SDR path: full tone mapping
        float3 mapped;

        if (CurveType == 0) // AgX
        {
            mapped = AgX(color);
        }
        else if (CurveType == 1) // ACES
        {
            mapped = ACESFitted(color);
        }
        else if (CurveType == 2) // Reinhard
        {
            mapped = ReinhardExtended(color, 4.0);
        }
        else // None
        {
            mapped = saturate(color);
        }

        // Vanilla ImageSpace adjustments (weather-aware)
        if (VanillaInfluence > 0.0)
        {
            float vLum = dot(mapped, float3(0.2126, 0.7152, 0.0722));
            float vSat = lerp(1.0, VP_Saturation, VanillaInfluence);
            mapped = lerp(vLum.xxx, mapped, vSat);
            mapped *= lerp(1.0, VP_Brightness, VanillaInfluence);
            mapped = lerp(0.5, mapped, lerp(1.0, VP_Contrast, VanillaInfluence));
            float3 vTint = float3(VP_TintR, VP_TintG, VP_TintB);
            mapped = lerp(mapped, mapped * vTint, VP_TintAmount * VanillaInfluence);
        }

        // sRGB gamma (linear → display)
        mapped = pow(max(mapped, 0.0), 1.0 / 2.2);

        return float4(mapped, 1.0);
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
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(ToneMapCBData);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&desc, nullptr, &m_toneMapCB)))
            return false;
    }

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

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_toneMapCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &tmCB, sizeof(tmCB));
            ctx.context->Unmap(m_toneMapCB, 0);
        }

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
