//=============================================================================
//  SSRRenderer.cpp — Screen-Space Reflections via Hi-Z ray marching
//
//  Dispatch flow (per frame, PostGeometry stage):
//    1. Copy backbuffer for reflection color sampling
//    2. Dispatch RayMarch CS: Hi-Z accelerated ray march → hit UV + confidence
//    3. Dispatch Resolve CS: sample scene color at hit → reflected color
//    4. Dispatch Temporal CS: ping-pong history accumulation → final reflection
//    5. Register SRV at t21 for shader passes
//=============================================================================

#include "SSRRenderer.h"
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
//  Embedded HLSL — Pass 1: RayMarch CS (half-res, Hi-Z accelerated)
// ═══════════════════════════════════════════════════════════════════════════

static const char kRayMarchCS[] = R"HLSL(
// Screen-space ray march for reflections.
// Reference: McGuire & Mara 2014, "Efficient GPU Screen-Space Ray Tracing"
// Uses Hi-Z pyramid for coarse-to-fine acceleration.
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Output: float4(hitUV.xy, hitViewZ, confidence)

cbuffer RayMarchCB : register(b0)
{
    float4x4 ProjMatrix;       // Projection matrix (row-major)
    float4x4 InvProjMatrix;    // Inverse projection (for unproject)
    uint2    HalfDims;         // Half-res output dimensions
    uint2    ScreenDims;       // Full-res dimensions
    float    NearZ;
    float    FarZ;
    float    MaxDistance;       // World-space max ray distance
    float    Thickness;        // Depth comparison thickness
    int      MaxSteps;         // Max march iterations
    int      MipCount;         // Hi-Z pyramid mip count
    uint     FrameIndex;       // Temporal jitter
    float    FPDepthThreshold; // First-person depth mask (view-space units)
};

Texture2D<float> HiZTex : register(t0);       // Hi-Z pyramid (all mip levels)
Texture2D<float4> BlueNoise : register(t30); // 128x128 R2 quasi-random blue noise
Texture2D<float> LinearDepth : register(t31); // Pre-computed linearized depth
SamplerState PointSamp   : register(s0);     // Point sampler

RWTexture2D<float4> HitOutput : register(u0); // UV.xy + viewZ + confidence

static const float PI = 3.14159265359;

float3 UVToViewPos(float2 uv, float linearZ)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float3 viewPos;
    viewPos.x = ndc.x * linearZ / ProjMatrix[0][0];
    viewPos.y = ndc.y * linearZ / ProjMatrix[1][1];
    viewPos.z = linearZ;
    return viewPos;
}

float3 ViewPosToUV(float3 viewPos)
{
    // Project view-space position to UV
    float2 ndc;
    ndc.x = viewPos.x * ProjMatrix[0][0] / viewPos.z;
    ndc.y = viewPos.y * ProjMatrix[1][1] / viewPos.z;
    float2 uv;
    uv.x = ndc.x * 0.5 + 0.5;
    uv.y = 1.0 - (ndc.y * 0.5 + 0.5);
    return float3(uv, viewPos.z);
}

float3 ReconstructNormal(int2 fullCoord, float2 texelSize)
{
    float dC = LinearDepth.Load(int3(fullCoord, 0));
    float dR = LinearDepth.Load(int3(fullCoord + int2(1, 0), 0));
    float dU = LinearDepth.Load(int3(fullCoord + int2(0, -1), 0));

    float2 uv  = (float2(fullCoord) + 0.5) * texelSize;
    float2 uvR = (float2(fullCoord) + float2(1.5, 0.5)) * texelSize;
    float2 uvU = (float2(fullCoord) + float2(0.5, -0.5)) * texelSize;

    float3 posC = UVToViewPos(uv,  dC);
    float3 posR = UVToViewPos(uvR, dR);
    float3 posU = UVToViewPos(uvU, dU);

    return normalize(cross(posU - posC, posR - posC));
}

// ── Blue noise temporal jitter (R2 quasi-random, Roberts 2018) ───────────
static const float GOLDEN_RATIO = 0.6180339887;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 halfCoord = int2(DTid.xy);
    int2 fullCoord = halfCoord * 2;
    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv = (float2(fullCoord) + 0.5) * texelSize;

    float rawDepth = HiZTex.Load(int3(fullCoord, 0));

    // Skip sky
    if (rawDepth < 0.00001)
    {
        HitOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Skip first-person geometry — no reflections on close-range FP model
    float linearZ = LinearDepth.Load(int3(fullCoord, 0));
    {
        if (linearZ < FPDepthThreshold)
        {
            HitOutput[halfCoord] = float4(0, 0, 0, 0);
            return;
        }
    }

    float3 viewPos = UVToViewPos(uv, linearZ);
    float3 viewNormal = ReconstructNormal(fullCoord, texelSize);

    // View direction (from camera to pixel in view space)
    float3 viewDir = normalize(viewPos);

    // Reflect
    float3 reflDir = reflect(viewDir, viewNormal);

    // Skip reflections pointing toward camera
    if (reflDir.z < 0.01)
    {
        HitOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Project reflected ray endpoint for screen-space direction
    float3 rayEnd = viewPos + reflDir * MaxDistance;
    float3 rayEndUV = ViewPosToUV(rayEnd);

    // Screen-space ray direction
    float2 rayDirSS = rayEndUV.xy - uv;
    float rayLenSS = length(rayDirSS);

    if (rayLenSS < 0.001)
    {
        HitOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Normalize and determine step count based on screen-space length
    float2 rayStepSS = rayDirSS / rayLenSS;

    // Blue noise temporal jitter — R2 quasi-random for better convergence
    float4 bn = BlueNoise.Load(int3(int2(DTid.xy % 128), 0));
    float jitter = frac(bn.x + float(FrameIndex) * GOLDEN_RATIO) * 0.5;

    // Hi-Z ray march: start at a reasonable mip and step coarsely
    float confidence = 0.0;
    float2 hitUV = float2(0, 0);
    float hitViewZ = 0;

    // Step size in UV space — at mip 0 this is ~1 pixel
    float baseStep = max(texelSize.x, texelSize.y);

    // Start ray well clear of the starting surface to prevent self-intersection.
    // The initial offset must be large enough that the ray clears the surface's
    // depth tolerance, but small enough to not miss thin nearby reflectors.
    float startOffset = max(baseStep * viewPos.z * 32.0, Thickness * 2.0);
    float3 currentPos = viewPos + reflDir * startOffset * (1.0 + jitter);
    float traveled = startOffset;

    // Minimum travel distance before accepting hits (anti-self-intersection)
    float minTravel = Thickness * 4.0;

    int currentMip = min(MipCount - 1, 3);  // Start at mip 3 for coarse march

    for (int i = 0; i < MaxSteps; i++)
    {
        float3 currentUV = ViewPosToUV(currentPos);

        // Out of screen?
        if (currentUV.x < 0.0 || currentUV.x > 1.0 ||
            currentUV.y < 0.0 || currentUV.y > 1.0 ||
            currentUV.z < 0.0)
        {
            break;
        }

        // Sample depth — use Hi-Z at coarse mips, linear depth at mip 0
        int2 sampleCoord = int2(currentUV.xy * float2(ScreenDims));
        float sceneZ;
        if (currentMip > 0) {
            // Coarse march: read Hi-Z at reduced resolution (reversed-Z)
            // Convert reversed-Z to linear for comparison
            int2 mipCoord = sampleCoord >> currentMip;
            float rawZ = HiZTex.Load(int3(mipCoord, currentMip));
            // reversed-Z linearization: N*F / (N + z*(F-N))
            sceneZ = (rawZ < 0.0001) ? FarZ : NearZ * FarZ / (NearZ + rawZ * (FarZ - NearZ));
        } else {
            // Fine march: use pre-computed linear depth for precision
            sceneZ = LinearDepth.Load(int3(sampleCoord, 0));
        }
        float rayZ = currentUV.z;

        // Hit test: ray is behind the surface
        float depthDiff = rayZ - sceneZ;
        if (depthDiff > 0 && depthDiff < Thickness * (1 + traveled * 0.01)
            && traveled > minTravel)
        {
            // Refine: step down mip levels
            if (currentMip > 0)
            {
                currentMip--;
                continue;  // Re-test at finer mip without advancing
            }

            // Hit confirmed at mip 0
            hitUV = currentUV.xy;
            hitViewZ = sceneZ;

            // Confidence: fade at screen edges, fade with distance
            float2 edgeFade = smoothstep(0.0, 0.05, currentUV.xy) *
                              smoothstep(0.0, 0.05, 1.0 - currentUV.xy);
            float edgeConf = edgeFade.x * edgeFade.y;
            float distConf = saturate(1.0 - traveled / MaxDistance);
            confidence = edgeConf * distConf;
            break;
        }

        // If we didn't hit, step mip back up if overshot
        if (depthDiff < 0 && currentMip < min(MipCount - 1, 3))
        {
            currentMip++;
        }

        // Advance ray
        float stepLen = baseStep * float(1 << currentMip) * currentPos.z;
        currentPos += reflDir * stepLen;
        traveled += stepLen;

        if (traveled > MaxDistance)
            break;
    }

    HitOutput[halfCoord] = float4(hitUV, hitViewZ, confidence);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: Resolve CS (half-res)
// ═══════════════════════════════════════════════════════════════════════════

static const char kResolveCS[] = R"HLSL(
// Resolve — sample scene color at ray hit UV, apply fades.
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer ResolveCB : register(b0)
{
    uint2  HalfDims;
    uint2  ScreenDims;
    float  Intensity;
    float3 pad0;
};

Texture2D<float4> HitBuffer    : register(t0);  // UV + viewZ + confidence
Texture2D<float4> SceneColor   : register(t1);  // Backbuffer copy
SamplerState      LinearSamp   : register(s0);

RWTexture2D<float4> ReflOutput : register(u0);  // Resolved reflection color

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 coord = int2(DTid.xy);
    float4 hit = HitBuffer.Load(int3(coord, 0));

    float2 hitUV     = hit.xy;
    float  confidence = hit.w;

    if (confidence < 0.001)
    {
        ReflOutput[coord] = float4(0, 0, 0, 0);
        return;
    }

    // Sample scene color at hit position (use linear filtering for smooth result)
    float3 reflColor = SceneColor.SampleLevel(LinearSamp, hitUV, 0).rgb;

    // Simple Fresnel approximation baked into confidence
    // (Full Fresnel would need the view angle, which we don't store in hit buffer)

    ReflOutput[coord] = float4(reflColor * Intensity, confidence);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 3: Temporal Denoise CS (half-res, ping-pong)
// ═══════════════════════════════════════════════════════════════════════════

static const char kTemporalCS[] = R"HLSL(
// Temporal accumulation for SSR — exponential blend with confidence-aware
// history rejection.

cbuffer TemporalCB : register(b0)
{
    uint2  HalfDims;
    float  TemporalAlpha;       // Blend weight for new frame (0.05-0.2)
    uint   FrameIndex;
};

Texture2D<float4> ReflCurrent  : register(t0);  // Resolved reflection (this frame)
Texture2D<float4> ReflHistory  : register(t1);  // Previous frame's accumulated

RWTexture2D<float4> ReflOutput : register(u0);  // Output accumulated reflection

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float4 current = ReflCurrent.Load(int3(coord, 0));
    float4 history = ReflHistory.Load(int3(coord, 0));

    // First few frames: no valid history yet — output current directly
    if (FrameIndex < 3) { ReflOutput[coord] = current; return; }

    // Confidence-aware blending:
    // High confidence = trust current frame, low = rely on history
    float confidenceCurrent = current.w;
    float confidenceHistory = history.w;

    // Blend strategy based on hit confidence
    float alpha = TemporalAlpha;

    if (confidenceCurrent < 0.001)
    {
        // No hit this frame — fade history toward zero quickly
        // (prevents persistent black artifacts from stale data)
        alpha = 0.15;
        current = float4(0, 0, 0, 0);
    }
    else if (confidenceHistory < 0.001)
    {
        // History was empty — trust current fully
        alpha = 1.0;
    }

    float4 result = lerp(history, current, alpha);

    // Fade out very low-confidence results entirely
    if (result.w < 0.01) result = float4(0, 0, 0, 0);

    ReflOutput[coord] = result;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) RayMarchCBData
{
    float    projMatrix[16];      // 64 bytes
    float    invProjMatrix[16];   // 64 bytes
    uint32_t halfW;               // +128
    uint32_t halfH;
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;               // +144
    float    farZ;
    float    maxDistance;
    float    thickness;
    int32_t  maxSteps;            // +160
    int32_t  mipCount;
    uint32_t frameIndex;
    float    fpDepthThreshold;  // first-person depth mask (view-space units)
};
static_assert(sizeof(RayMarchCBData) == 176, "RayMarchCB must be 176 bytes");

struct alignas(16) ResolveCBData
{
    uint32_t halfW;
    uint32_t halfH;
    uint32_t screenW;
    uint32_t screenH;
    float    intensity;
    float    pad[3];
};
static_assert(sizeof(ResolveCBData) == 32, "ResolveCB must be 32 bytes");

struct alignas(16) TemporalCBData
{
    uint32_t halfW;
    uint32_t halfH;
    float    temporalAlpha;
    uint32_t frameIndex;
};
static_assert(sizeof(TemporalCBData) == 16, "TemporalCB must be 16 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool SSRRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm = ComputeManager::Get();
    auto& pl = RenderPipeline::Get();

    if (!cm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("SSRRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("SSRRenderer: failed to get backbuffer (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backTex->GetDesc(&bbDesc);
    backTex->Release();

    m_screenW = bbDesc.Width;
    m_screenH = bbDesc.Height;
    m_halfW   = (m_screenW + 1) / 2;
    m_halfH   = (m_screenH + 1) / 2;

    if (!CompileShaders()) return false;
    if (!CreateResources()) {
        ReleaseResources();
        return false;
    }

    // Register as PostGeometry pipeline pass
    m_pipelineHandle = pl.AddPass({
        .name     = "SSR",
        .stage    = PipelineStage::PostGeometry,
        .priority = 25,  // After HiZ (10), GTAO (15), SSGI (20)
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("SSRRenderer: initialized ({}x{}, half={}x{}, steps={}, dist={:.0f}, t{})",
                    m_screenW, m_screenH, m_halfW, m_halfH,
                    m_maxSteps, m_maxDistance, kSRVSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool SSRRenderer::CompileShaders()
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
            SKSE::log::error("SSRRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("SSRRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("SSR_RayMarch",  kRayMarchCS, &m_rayMarchCS)) return false;
    if (!CompileCS("SSR_Resolve",   kResolveCS,  &m_resolveCS))  return false;
    if (!CompileCS("SSR_Temporal",  kTemporalCS, &m_temporalCS)) return false;

    return true;
}

bool SSRRenderer::RecompileShaders()
{
    SafeRelease(m_rayMarchCS);
    SafeRelease(m_resolveCS);
    SafeRelease(m_temporalCS);
    return CompileShaders();
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool SSRRenderer::CreateResources()
{
    HRESULT hr;

    // Pass 1 output: hit buffer (UV + viewZ + confidence)
    if (!CreateGPUTexture(m_device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_hitBuffer, &m_hitBufferSRV, &m_hitBufferUAV, "hitBuffer"))
        return false;

    // Pass 2 output: resolved reflection color
    if (!CreateGPUTexture(m_device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_reflRaw, &m_reflRawSRV, &m_reflRawUAV, "reflRaw"))
        return false;

    // Pass 3: ping-pong history
    if (!CreateGPUTexture(m_device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_reflHistory[0], &m_reflHistorySRV[0], &m_reflHistoryUAV[0], "reflHistory0"))
        return false;
    if (!CreateGPUTexture(m_device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_reflHistory[1], &m_reflHistorySRV[1], &m_reflHistoryUAV[1], "reflHistory1"))
        return false;

    // Backbuffer copy for reflection color sampling (SRV only)
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_backbufferCopy, &m_backbufferCopySRV, nullptr, "backbufferCopy"))
        return false;
    // Constant buffers
    // Constant buffers
    if (!CreateCB(m_device, sizeof(RayMarchCBData), &m_rayMarchCB)) return false;
    if (!CreateCB(m_device, sizeof(ResolveCBData), &m_resolveCB)) return false;
    if (!CreateCB(m_device, sizeof(TemporalCBData), &m_temporalCB)) return false;
    // Samplers
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter        = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc= D3D11_COMPARISON_NEVER;
        desc.MaxLOD        = D3D11_FLOAT32_MAX;
        hr = m_device->CreateSamplerState(&desc, &m_linearSampler);
        if (FAILED(hr)) return false;

        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        hr = m_device->CreateSamplerState(&desc, &m_pointSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV from currently-bound DSV
// ═══════════════════════════════════════════════════════════════════════════

bool SSRRenderer::AcquireDepthSRV()
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
//  GetReflectionSRV
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* SSRRenderer::GetReflectionSRV() const
{
    if (!m_initialized) return nullptr;
    // Return temporally accumulated output (falls back to raw if temporal not ready)
    auto* temporal = m_reflHistorySRV[1 - m_pingPongIdx];
    return temporal ? temporal : m_reflRawSRV;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PostGeometry stage)
// ═══════════════════════════════════════════════════════════════════════════

void SSRRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Gate: scene matrices must be valid (camera data verified non-garbage)
    if (!SceneMatrices::Get().IsValid()) return;

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
    auto& hiz = HiZPyramid::Get();
    int mipCount = 1;
    if (hiz.IsInitialized() && hiz.GetSRV()) {
        depthSRV = hiz.GetSRV();
        mipCount = static_cast<int>(hiz.GetMipCount());
    }

    // ── Copy scene color for reflection sampling ────────────────────────
    // During mid-frame dispatch, the backbuffer doesn't contain the scene —
    // the game renders to an internal RT exposed via ctx.gameSceneRTV.
    // Extract the texture from that RTV first; fall back to swapchain only
    // when gameSceneRTV is null (Present-time dispatch).
    {
        ID3D11Texture2D* sceneTex = nullptr;

        if (ctx.gameSceneRTV) {
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                res->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&sceneTex));
                res->Release();
            }
        }

        if (!sceneTex) {
            // Fallback: swapchain backbuffer (Present-time path)
            auto* sc = ctx.swapChain ? ctx.swapChain : D3D11Hook::GetSwapChain();
            if (sc) {
                sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&sceneTex));
            }
        }

        if (!sceneTex) {
            if (m_depthSRV) { m_depthSRV->Release(); m_depthSRV = nullptr; }
            return;
        }

        // Guard: skip if scene RT isn't a full-color format
        {
            D3D11_TEXTURE2D_DESC guardDesc;
            sceneTex->GetDesc(&guardDesc);
            if (guardDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT &&
                guardDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
                guardDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
                guardDesc.Format != DXGI_FORMAT_R11G11B10_FLOAT) {
                sceneTex->Release();
                return;
            }
        }

        ctx.context->CopyResource(m_backbufferCopy, sceneTex);
        sceneTex->Release();
    }

    auto& scene = SceneMatrices::Get();
    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    // First frame: clear history
    if (m_firstFrame) {
        float clearColor[4] = {0, 0, 0, 0};
        ctx.context->ClearUnorderedAccessViewFloat(m_reflHistoryUAV[0], clearColor);
        ctx.context->ClearUnorderedAccessViewFloat(m_reflHistoryUAV[1], clearColor);
        m_firstFrame = false;
        m_pingPongIdx = 0;
    }

    cm.SaveCSState();

    // Bind pre-computed linear depth at t31 for all passes
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: RayMarch — Hi-Z accelerated screen-space ray march
    // ═════════════════════════════════════════════════════════════════════
    {
        RayMarchCBData cb = {};
        std::memcpy(cb.projMatrix,    scene.ProjMatrix(),        sizeof(float) * 16);
        std::memcpy(cb.invProjMatrix, scene.InvViewProjMatrix(), sizeof(float) * 16);
        cb.halfW       = m_halfW;
        cb.halfH       = m_halfH;
        cb.screenW     = m_screenW;
        cb.screenH     = m_screenH;
        cb.nearZ       = nearZ;
        cb.farZ        = farZ;
        cb.maxDistance  = m_maxDistance;
        cb.thickness   = m_thickness;
        cb.maxSteps    = m_maxSteps;
        cb.mipCount    = mipCount;
        cb.frameIndex  = m_frameIndex;
        cb.fpDepthThreshold = 16.0f;  // ~16 view-space units

        UploadCB(ctx.context, m_rayMarchCB, &cb, sizeof(cb));

        ctx.context->CSSetShaderResources(0, 1, &depthSRV);
        auto* bnSRV = SharedGPUResources::Get().GetBlueNoiseSRV();
        if (bnSRV) ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &bnSRV);
        ID3D11SamplerState* samplers[] = { m_pointSampler };
        ctx.context->CSSetSamplers(0, 1, samplers);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_hitBufferUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_rayMarchCB);
        ctx.context->CSSetShader(m_rayMarchCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        { ID3D11ShaderResourceView* n = nullptr; ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &n); }
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: Resolve — sample scene color at hit positions
    // ═════════════════════════════════════════════════════════════════════
    {
        ResolveCBData cb = {};
        cb.halfW     = m_halfW;
        cb.halfH     = m_halfH;
        cb.screenW   = m_screenW;
        cb.screenH   = m_screenH;
        cb.intensity = m_intensity;

        UploadCB(ctx.context, m_resolveCB, &cb, sizeof(cb));

        ID3D11ShaderResourceView* srvs[] = { m_hitBufferSRV, m_backbufferCopySRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ID3D11SamplerState* samplers[] = { m_linearSampler };
        ctx.context->CSSetSamplers(0, 1, samplers);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_reflRawUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_resolveCB);
        ctx.context->CSSetShader(m_resolveCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 3: Temporal accumulation (ping-pong)
    // ═════════════════════════════════════════════════════════════════════
    {
        int readIdx  = 1 - m_pingPongIdx;
        int writeIdx = m_pingPongIdx;

        TemporalCBData cb = {};
        cb.halfW         = m_halfW;
        cb.halfH         = m_halfH;
        cb.temporalAlpha = 0.25f;
        cb.frameIndex    = m_frameIndex;

        UploadCB(ctx.context, m_temporalCB, &cb, sizeof(cb));

        // t0 = current resolve, t1 = history, t2 = motion vectors
        auto* motionSRV = MotionVectorGen::Get().GetMotionSRV();
        ID3D11ShaderResourceView* srvs[] = { m_reflRawSRV, m_reflHistorySRV[readIdx], motionSRV };
        ctx.context->CSSetShaderResources(0, 3, srvs);
        // Bind linear sampler at s0 for bilinear history + motion vector sampling
        ctx.context->CSSetSamplers(0, 1, &m_linearSampler);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_reflHistoryUAV[writeIdx], nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_temporalCB);
        ctx.context->CSSetShader(m_temporalCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[3] = {};
        ctx.context->CSSetShaderResources(0, 3, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

        // Swap ping-pong
        m_pingPongIdx = readIdx;
    }

    // Unbind linear depth from t31
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // Register output SRV at t21
    SRVInjector::Get().RegisterSRV(kSRVSlot, GetReflectionSRV());

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

void SSRRenderer::Shutdown()
{
    if (!m_initialized) return;

    SRVInjector::Get().UnregisterSRV(kSRVSlot);

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("SSRRenderer: shut down");
}


void SSRRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_rayMarchCS);
    SafeRelease(m_resolveCS);
    SafeRelease(m_temporalCS);

    SafeRelease(m_hitBuffer);
    SafeRelease(m_hitBufferSRV);
    SafeRelease(m_hitBufferUAV);
    SafeRelease(m_rayMarchCB);

    SafeRelease(m_reflRaw);
    SafeRelease(m_reflRawSRV);
    SafeRelease(m_reflRawUAV);
    SafeRelease(m_resolveCB);

    SafeRelease(m_backbufferCopy);
    SafeRelease(m_backbufferCopySRV);

    SafeRelease(m_reflHistory[0]);
    SafeRelease(m_reflHistorySRV[0]);
    SafeRelease(m_reflHistoryUAV[0]);
    SafeRelease(m_reflHistory[1]);
    SafeRelease(m_reflHistorySRV[1]);
    SafeRelease(m_reflHistoryUAV[1]);
    SafeRelease(m_temporalCB);

    SafeRelease(m_linearSampler);
    SafeRelease(m_pointSampler);
    SafeRelease(m_depthSRV);
}

} // namespace SB
