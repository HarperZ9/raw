//=============================================================================
//  PipelineTest.cpp — Experimental fullscreen render passes
//
//  Two passes injected at PrePresent via RenderPipeline, using ping-pong
//  managed RTs to avoid redundant backbuffer copies:
//
//    1. Setup (priority 99) — single CopyResource: backbuffer → PingA
//    2. Vignette (priority 100) — read PingA → write PingB
//    3. Film Grain (priority 110) — read PingB → write backbuffer
//
//  With this pattern, only ONE CopyResource per frame regardless of how
//  many chained passes exist (vs N copies with the naive approach).
//=============================================================================

#include "PipelineTest.h"
#include "RenderPipeline.h"
#include "RenderPassManager.h"
#include "D3D11Hook.h"

#include <SKSE/SKSE.h>
#include <d3d11.h>
#include <dxgi.h>

namespace SB::PipelineTest
{

// ═══════════════════════════════════════════════════════════════════════════
//  HLSL Pixel Shaders (compiled at init time via D3DCompile)
// ═══════════════════════════════════════════════════════════════════════════

static const char* kVignettePS = R"HLSL(
Texture2D<float4> SceneColor : register(t0);
SamplerState LinearSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    float4 color = SceneColor.Sample(LinearSampler, input.texcoord);

    // Radial vignette: smooth falloff from center to edges
    float2 d = input.texcoord - 0.5;
    float dist = length(d) * 1.414;  // normalize so corners = 1.0
    float vignette = smoothstep(1.1, 0.4, dist);

    return float4(color.rgb * vignette, color.a);
}
)HLSL";


static const char* kFilmGrainPS = R"HLSL(
cbuffer FrameData : register(b0)
{
    float FrameIndex;
    float Time;
    float ScreenW;
    float ScreenH;
};

Texture2D<float4> SceneColor : register(t0);
SamplerState LinearSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// Simple hash for procedural noise
float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float4 main(PSInput input) : SV_Target
{
    float4 color = SceneColor.Sample(LinearSampler, input.texcoord);

    // Per-pixel animated grain using frame index as temporal seed
    float2 seed = input.texcoord * float2(ScreenW, ScreenH) + FrameIndex * 7.13;
    float noise = hash12(seed) * 2.0 - 1.0;

    // Grain strength: subtle overall, slightly stronger in dark areas
    float lum = dot(color.rgb, float3(0.299, 0.587, 0.114));
    float grainStrength = 0.04 * (1.0 - lum * 0.5);

    color.rgb += noise * grainStrength;
    return float4(saturate(color.rgb), color.a);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Internal state
// ═══════════════════════════════════════════════════════════════════════════

static bool s_initialized = false;
static bool s_vignetteEnabled = false;
static bool s_filmGrainEnabled = false;

// RenderPassManager pass IDs (1-based, 0 = invalid)
static RenderPassID s_vignettePassID = 0;
static RenderPassID s_grainPassID    = 0;

// RenderPipeline handles
static PassHandle s_setupHandle    = 0;
static PassHandle s_vignetteHandle = 0;
static PassHandle s_grainHandle    = 0;

// Ping-pong managed RTs (owned by RenderPipeline's RT pool)
static ManagedRT* s_pingA = nullptr;
static ManagedRT* s_pingB = nullptr;

// Backbuffer RTV (created once at init from swap chain)
static ID3D11RenderTargetView* s_backbufferRTV = nullptr;

// Linear clamp sampler
static ID3D11SamplerState* s_linearSampler = nullptr;

// Frame counter for grain animation
static uint32_t s_frameCounter = 0;

// Constant buffer layout (matches HLSL FrameData, 16-byte aligned)
struct alignas(16) FrameCB
{
    float frameIndex;
    float time;
    float screenW;
    float screenH;
};


// ═══════════════════════════════════════════════════════════════════════════
//  Resource creation / teardown
// ═══════════════════════════════════════════════════════════════════════════

static bool CreateResources(ID3D11Device* dev, IDXGISwapChain* sc)
{
    // Get backbuffer to determine format
    ID3D11Texture2D* bb = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    if (FAILED(hr) || !bb) return false;

    D3D11_TEXTURE2D_DESC bbDesc;
    bb->GetDesc(&bbDesc);

    // Create backbuffer RTV (used as final output target)
    hr = dev->CreateRenderTargetView(bb, nullptr, &s_backbufferRTV);
    bb->Release();
    if (FAILED(hr)) {
        SKSE::log::error("PipelineTest: Failed to create backbuffer RTV");
        return false;
    }

    // Create ping-pong RTs from RenderPipeline's managed pool
    // These match the backbuffer format and dimensions
    auto& pipeline = RenderPipeline::Get();
    s_pingA = &pipeline.GetOrCreateRT("PipelineTest_PingA", bbDesc.Format);
    s_pingB = &pipeline.GetOrCreateRT("PipelineTest_PingB", bbDesc.Format);

    if (!s_pingA->Valid() || !s_pingB->Valid()) {
        SKSE::log::error("PipelineTest: Managed RT creation failed");
        return false;
    }

    // Linear clamp sampler
    D3D11_SAMPLER_DESC sd{};
    sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy  = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD         = D3D11_FLOAT32_MAX;

    hr = dev->CreateSamplerState(&sd, &s_linearSampler);
    if (FAILED(hr)) {
        SKSE::log::error("PipelineTest: Failed to create sampler");
        return false;
    }

    SKSE::log::info("PipelineTest: Resources created ({}x{}, format={}, ping-pong RTs from pool)",
        bbDesc.Width, bbDesc.Height, static_cast<int>(bbDesc.Format));
    return true;
}

static void ReleaseResources()
{
    if (s_backbufferRTV) { s_backbufferRTV->Release(); s_backbufferRTV = nullptr; }
    if (s_linearSampler) { s_linearSampler->Release(); s_linearSampler = nullptr; }
    // s_pingA/s_pingB are owned by RenderPipeline's RT pool — don't release here
    s_pingA = nullptr;
    s_pingB = nullptr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Pass execute callbacks (called by RenderPipeline each frame)
// ═══════════════════════════════════════════════════════════════════════════

// Setup pass: single CopyResource backbuffer → PingA (the only copy per frame)
static void ExecuteSetup(PassContext& ctx)
{
    // Skip setup if no effects are enabled
    if (!s_vignetteEnabled && !s_filmGrainEnabled) return;
    if (!s_pingA || !s_pingA->Valid()) return;

    ID3D11Texture2D* bb = nullptr;
    ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    if (!bb) return;
    ctx.context->CopyResource(s_pingA->texture, bb);
    bb->Release();
}

// Vignette: read PingA → write PingB (or backbuffer if grain is disabled)
static void ExecuteVignette(PassContext& ctx)
{
    if (!s_vignetteEnabled || !s_vignettePassID) return;
    if (!s_pingA || !s_pingB) return;

    // If grain is disabled, write directly to backbuffer (skip PingB)
    ID3D11RenderTargetView* outputRTV = s_filmGrainEnabled ? s_pingB->rtv : s_backbufferRTV;
    if (!outputRTV) return;

    RenderPassManager::Get().Execute({
        .passID       = s_vignettePassID,
        .rtv          = outputRTV,
        .srvs         = &s_pingA->srv,
        .srvCount     = 1,
        .samplers     = &s_linearSampler,
        .samplerCount = 1,
    });
}

// Film Grain: read PingB (or PingA if vignette disabled) → write backbuffer
static void ExecuteFilmGrain(PassContext& ctx)
{
    if (!s_filmGrainEnabled || !s_grainPassID) return;

    // If vignette is disabled, read directly from PingA (setup's output)
    ID3D11ShaderResourceView* inputSRV = s_vignetteEnabled ? s_pingB->srv : s_pingA->srv;
    if (!inputSRV || !s_backbufferRTV) return;

    FrameCB cb;
    cb.frameIndex = static_cast<float>(s_frameCounter++);
    cb.time       = ctx.deltaTime;
    cb.screenW    = static_cast<float>(ctx.screenW);
    cb.screenH    = static_cast<float>(ctx.screenH);

    RenderPassManager::Get().Execute({
        .passID       = s_grainPassID,
        .rtv          = s_backbufferRTV,
        .srvs         = &inputSRV,
        .srvCount     = 1,
        .samplers     = &s_linearSampler,
        .samplerCount = 1,
        .cbData       = &cb,
        .cbSize       = sizeof(cb),
    });
}


// ═══════════════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════════════

void Initialize()
{
    if (s_initialized) return;

    auto* dev = D3D11Hook::GetDevice();
    auto* sc  = D3D11Hook::GetSwapChain();
    auto& rpm = RenderPassManager::Get();
    auto& pipeline = RenderPipeline::Get();

    if (!dev || !sc || !rpm.IsInitialized() || !pipeline.IsInitialized()) {
        SKSE::log::warn("PipelineTest: Prerequisites not ready — skipping init");
        return;
    }

    // Create D3D11 resources (backbuffer RTV, ping-pong RTs, sampler)
    if (!CreateResources(dev, sc)) {
        SKSE::log::error("PipelineTest: Resource creation failed");
        return;
    }

    // Register shader passes with RenderPassManager (compiles HLSL)
    s_vignettePassID = rpm.RegisterPass({
        .name     = "SB_TestVignette",
        .psSource = kVignettePS,
    });

    s_grainPassID = rpm.RegisterPass({
        .name     = "SB_TestFilmGrain",
        .psSource = kFilmGrainPS,
    });

    if (!s_vignettePassID)
        SKSE::log::error("PipelineTest: Vignette PS compilation failed");
    if (!s_grainPassID)
        SKSE::log::error("PipelineTest: Film grain PS compilation failed");

    // Register passes with RenderPipeline at PrePresent stage
    // Setup (priority 99): single copy BB → PingA
    s_setupHandle = pipeline.AddPass({
        .name     = "TestSetup",
        .stage    = PipelineStage::PrePresent,
        .priority = 99,
        .execute  = ExecuteSetup,
    });

    // Vignette (priority 100): read PingA → write PingB
    if (s_vignettePassID) {
        s_vignetteHandle = pipeline.AddPass({
            .name     = "TestVignette",
            .stage    = PipelineStage::PrePresent,
            .priority = 100,
            .execute  = ExecuteVignette,
        });
    }

    // Film Grain (priority 110): read PingB → write backbuffer
    if (s_grainPassID) {
        s_grainHandle = pipeline.AddPass({
            .name     = "TestFilmGrain",
            .stage    = PipelineStage::PrePresent,
            .priority = 110,
            .execute  = ExecuteFilmGrain,
        });
    }

    s_initialized = true;
    SKSE::log::info("PipelineTest: Initialized — vignette={}, grain={}, ping-pong RTs, {} pipeline passes",
        s_vignettePassID ? "OK" : "FAIL",
        s_grainPassID ? "OK" : "FAIL",
        pipeline.GetPassCount());
}

void Shutdown()
{
    if (!s_initialized) return;

    auto& pipeline = RenderPipeline::Get();
    if (s_setupHandle)    { pipeline.RemovePass(s_setupHandle);    s_setupHandle    = 0; }
    if (s_vignetteHandle) { pipeline.RemovePass(s_vignetteHandle); s_vignetteHandle = 0; }
    if (s_grainHandle)    { pipeline.RemovePass(s_grainHandle);    s_grainHandle    = 0; }

    ReleaseResources();

    s_vignettePassID = 0;
    s_grainPassID    = 0;
    s_frameCounter   = 0;
    s_initialized    = false;

    SKSE::log::info("PipelineTest: Shut down");
}

bool IsInitialized() { return s_initialized; }

void SetVignetteEnabled(bool enabled)
{
    s_vignetteEnabled = enabled;
    if (s_vignetteHandle)
        RenderPipeline::Get().SetPassEnabled(s_vignetteHandle, enabled);
}

void SetFilmGrainEnabled(bool enabled)
{
    s_filmGrainEnabled = enabled;
    if (s_grainHandle)
        RenderPipeline::Get().SetPassEnabled(s_grainHandle, enabled);
}

bool IsVignetteEnabled()  { return s_vignetteEnabled; }
bool IsFilmGrainEnabled() { return s_filmGrainEnabled; }

} // namespace SB::PipelineTest
