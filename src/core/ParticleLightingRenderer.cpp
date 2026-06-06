//=============================================================================
//  ParticleLightingRenderer.cpp — Screen-space emissive light propagation
//
//  Three-pass compute pipeline:
//    Pass 1: Emissive Detect (full-res -> quarter-res downsample)
//            Reads backbuffer copy + MaterialClassifier, writes quarter-res
//            emissive detection buffer with max-luminance filter.
//    Pass 2: Light Scatter (quarter-res)
//            Reads emissive detection + depth, writes scattered light result
//            with radial falloff weighted by depth proximity.
//    Pass 3: Composite (quarter-res -> full-res additive blend)
//            Reads scatter result + depth, writes directly to backbuffer UAV.
//
//  Dependencies: D3D11Hook (depth), MaterialClassifier (material IDs)
//=============================================================================

#include "ParticleLightingRenderer.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"
#include "MaterialClassifier.h"
#include "ComputeManager.h"
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

// Shared constant buffer declaration (used by all three shaders)
static const char* kCBDecl = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// ParticleLighting — shared CB declarations (unused standalone shader).
// The actual per-pass shaders are kEmissiveDetectCS, kLightScatterCS,
// and kCompositeCS below.

cbuffer ParticleLightCB : register(b0)
{
    float  luminanceThreshold;
    float  intensity;
    float  scatterRadius;
    float  falloffExponent;
    float  depthTolerance;
    float  nearZ;
    float  farZ;
    uint   fullWidth;
    uint   fullHeight;
    uint   quarterWidth;
    uint   quarterHeight;
    uint   frameIndex;
    float4x4 invViewProj;
    float3 cameraPos;
    float  pad;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Placeholder — not dispatched; see per-pass shaders.
}
)HLSL";

// ── Pass 1: Emissive Detection CS ─────────────────────────────────────────

static const char* kEmissiveDetectCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 1: Emissive Detection.
// Reads full-resolution backbuffer copy, identifies bright emissive
// pixels (luminance above threshold), and downsamples them into
// a quarter-resolution emissive buffer using a 4x4 block max filter.

cbuffer ParticleLightCB : register(b0)
{
    float  luminanceThreshold;
    float  intensity;
    float  scatterRadius;
    float  falloffExponent;
    float  depthTolerance;
    float  nearZ;
    float  farZ;
    uint   fullWidth;
    uint   fullHeight;
    uint   quarterWidth;
    uint   quarterHeight;
    uint   frameIndex;
    float4x4 invViewProj;
    float3 cameraPos;
    float  pad;
}

Texture2D<float4> tBackbuffer : register(t0);
Texture2D<uint>   tMaterialID : register(t1);
RWTexture2D<float4> uEmissive : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= quarterWidth || DTid.y >= quarterHeight)
        return;

    // Map quarter-res pixel to full-res 4x4 block origin.
    uint2 baseCoord = DTid.xy * 4;

    // Max-luminance filter across the 4x4 block: pick the brightest
    // emissive pixel to preserve small point lights.
    float  maxLuma = 0.0;
    float3 maxColor = float3(0, 0, 0);

    for (uint dy = 0; dy < 4; ++dy)
    {
        for (uint dx = 0; dx < 4; ++dx)
        {
            uint2 sampleCoord = baseCoord + uint2(dx, dy);
            if (sampleCoord.x >= fullWidth || sampleCoord.y >= fullHeight)
                continue;

            float4 color = tBackbuffer.Load(int3(sampleCoord, 0));
            float luma = dot(color.rgb, float3(0.299, 0.587, 0.114));

            if (luma > maxLuma)
            {
                maxLuma = luma;
                maxColor = color.rgb;
            }
        }
    }

    // Threshold: only keep pixels above the emissive luminance cutoff.
    float emissive = saturate((maxLuma - luminanceThreshold) /
                               max(1.0 - luminanceThreshold, 0.001));

    // Soft-knee: smooth the threshold transition.
    emissive = emissive * emissive;

    float3 result = maxColor * emissive;
    uEmissive[DTid.xy] = float4(result, emissive);
}
)HLSL";

// ── Pass 2: Light Scatter CS ──────────────────────────────────────────────

static const char* kLightScatterCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 2: Light Scatter.
// Reads quarter-res emissive detection buffer and performs a radial
// blur / light scatter kernel.  Each texel gathers contributions from
// surrounding emissive sources, weighted by distance falloff and
// depth proximity (to prevent scatter across depth discontinuities).

cbuffer ParticleLightCB : register(b0)
{
    float  luminanceThreshold;
    float  intensity;
    float  scatterRadius;
    float  falloffExponent;
    float  depthTolerance;
    float  nearZ;
    float  farZ;
    uint   fullWidth;
    uint   fullHeight;
    uint   quarterWidth;
    uint   quarterHeight;
    uint   frameIndex;
    float4x4 invViewProj;
    float3 cameraPos;
    float  pad;
}

Texture2D<float4> tEmissive : register(t0);
Texture2D<float>  tDepth    : register(t1);
RWTexture2D<float4> uScatter : register(u0);
SamplerState sLinear : register(s0);

// Linearize reversed-Z depth.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

// Number of scatter samples along each ring.
static const uint SCATTER_SAMPLES = 16;
static const float PI = 3.14159265;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= quarterWidth || DTid.y >= quarterHeight)
        return;

    // Map quarter-res texel to approximate full-res location for depth read.
    uint2 fullResCoord = DTid.xy * 4 + uint2(2, 2);
    fullResCoord = min(fullResCoord, uint2(fullWidth - 1, fullHeight - 1));

    float centerDepth = tDepth.Load(int3(fullResCoord, 0));

    // Sky check.
    if (centerDepth < 0.0001)
    {
        uScatter[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float centerLinearZ = LinearizeDepth(centerDepth);

    // Read this pixel's own emissive contribution.
    float4 selfEmissive = tEmissive.Load(int3(DTid.xy, 0));

    // Gather scattered light from surrounding emissive sources.
    float3 scatterAccum = float3(0, 0, 0);
    float  totalWeight  = 0.0;

    // Scatter radius in quarter-res pixels.
    float radiusPx = scatterRadius;

    for (uint ring = 1; ring <= 3; ++ring)
    {
        float r = radiusPx * (float(ring) / 3.0);

        for (uint s = 0; s < SCATTER_SAMPLES; ++s)
        {
            float angle = (float(s) / float(SCATTER_SAMPLES)) * 2.0 * PI;
            float2 offset = float2(cos(angle), sin(angle)) * r;

            float2 sampleUV = (float2(DTid.xy) + 0.5 + offset) /
                               float2(quarterWidth, quarterHeight);

            // Bounds check.
            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
                sampleUV.y < 0.0 || sampleUV.y > 1.0)
                continue;

            float4 emissiveSample = tEmissive.SampleLevel(sLinear, sampleUV, 0);

            // Depth proximity weighting: read depth at sample location.
            uint2 sampleFullRes = uint2(sampleUV * float2(fullWidth, fullHeight));
            sampleFullRes = min(sampleFullRes, uint2(fullWidth - 1, fullHeight - 1));
            float sampleDepth = tDepth.Load(int3(sampleFullRes, 0));
            float sampleLinearZ = LinearizeDepth(max(sampleDepth, 0.0001));

            float depthDiff = abs(centerLinearZ - sampleLinearZ);
            float depthWeight = exp(-depthDiff * depthDiff /
                                     max(depthTolerance * depthTolerance, 0.01));

            // Distance falloff.
            float dist = length(offset);
            float distWeight = pow(saturate(1.0 - dist / radiusPx), falloffExponent);

            float w = distWeight * depthWeight;
            scatterAccum += emissiveSample.rgb * w;
            totalWeight += w;
        }
    }

    if (totalWeight > 0.0)
        scatterAccum /= totalWeight;

    // Combine self-emission with scattered light.
    float3 result = selfEmissive.rgb + scatterAccum * intensity;
    uScatter[DTid.xy] = float4(result, 1.0);
}
)HLSL";

// ── Pass 3: Composite CS ─────────────────────────────────────────────────

static const char* kCompositeCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 3: Composite.
// Upsamples the quarter-res scattered light result to full resolution
// using bilinear filtering and additively blends it onto the backbuffer.
// Depth-aware rejection prevents light bleeding across depth edges.

cbuffer ParticleLightCB : register(b0)
{
    float  luminanceThreshold;
    float  intensity;
    float  scatterRadius;
    float  falloffExponent;
    float  depthTolerance;
    float  nearZ;
    float  farZ;
    uint   fullWidth;
    uint   fullHeight;
    uint   quarterWidth;
    uint   quarterHeight;
    uint   frameIndex;
    float4x4 invViewProj;
    float3 cameraPos;
    float  pad;
}

Texture2D<float4> tScatter : register(t0);
Texture2D<float>  tDepth   : register(t1);
SamplerState sLinear : register(s0);
RWTexture2D<float4> uBackbuffer : register(u0);

// Linearize reversed-Z depth.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= fullWidth || DTid.y >= fullHeight)
        return;

    // Depth — HiZ reversed-Z.
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.00001)
        return;

    // First-person geometry skip.
    float linearZ = LinearizeDepth(rawDepth);
    if (linearZ < 16.0)
        return;

    // Upsample: map full-res pixel to quarter-res UV.
    float2 quarterUV = (float2(DTid.xy) + 0.5) / float2(fullWidth, fullHeight);

    // Bilinear sample from quarter-res scatter result.
    float4 scatterSample = tScatter.SampleLevel(sLinear, quarterUV, 0);

    // Skip negligible contributions early.
    float scatterLuma = dot(scatterSample.rgb, float3(0.299, 0.587, 0.114));
    if (scatterLuma < 0.001)
        return;

    // Depth-aware edge rejection: compare full-res depth against the
    // quarter-res neighbourhood to prevent light bleeding at edges.
    // Sample 4 nearest quarter-res depth values.
    float2 quarterPixel = quarterUV * float2(quarterWidth, quarterHeight) - 0.5;
    int2 baseQP = int2(floor(quarterPixel));

    float depthReject = 0.0;
    float totalW = 0.0;
    for (int dy = 0; dy <= 1; ++dy)
    {
        for (int dx = 0; dx <= 1; ++dx)
        {
            int2 qCoord = baseQP + int2(dx, dy);
            qCoord = clamp(qCoord, int2(0, 0),
                           int2(quarterWidth - 1, quarterHeight - 1));

            // Map back to full-res for depth comparison.
            uint2 fullCoord = uint2(qCoord) * 4 + uint2(2, 2);
            fullCoord = min(fullCoord, uint2(fullWidth - 1, fullHeight - 1));
            float sampleDepth = tDepth.Load(int3(fullCoord, 0));
            float sampleLinearZ = LinearizeDepth(max(sampleDepth, 0.0001));

            float depthDiff = abs(linearZ - sampleLinearZ);
            float w = exp(-depthDiff * depthDiff /
                           max(depthTolerance * depthTolerance, 0.01));
            depthReject += w;
            totalW += 1.0;
        }
    }
    depthReject = (totalW > 0.0) ? (depthReject / totalW) : 1.0;

    // Additive blend onto backbuffer, modulated by depth edge rejection.
    float4 sceneColor = uBackbuffer[DTid.xy];
    float3 addLight = scatterSample.rgb * intensity * depthReject;

    uBackbuffer[DTid.xy] = float4(sceneColor.rgb + addLight, sceneColor.a);
}
)HLSL";


// ── Constant buffer layout ────────────────────────────────────────────────

struct alignas(16) ParticleLightCBData
{
    float    luminanceThreshold;
    float    intensity;
    float    scatterRadius;
    float    falloffExponent;
    float    depthTolerance;
    float    nearZ;
    float    farZ;
    uint32_t fullWidth;
    uint32_t fullHeight;
    uint32_t quarterWidth;
    uint32_t quarterHeight;
    uint32_t frameIndex;
    float    invViewProj[16];
    float    cameraPos[3];
    float    pad;
};


// ── Initialize ────────────────────────────────────────────────────────────

bool ParticleLightingRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;

    m_device  = dev;
    m_context = ctx;

    DXGI_SWAP_CHAIN_DESC scDesc;
    if (FAILED(sc->GetDesc(&scDesc))) return false;
    m_screenW  = scDesc.BufferDesc.Width;
    m_screenH  = scDesc.BufferDesc.Height;
    m_quarterW = (m_screenW + 3) / 4;
    m_quarterH = (m_screenH + 3) / 4;

    if (!CompileShaders()) return false;
    if (!CreateResources()) return false;

    // Register as PostGeometry pipeline pass
    auto& pipeline = RenderPipeline::Get();
    if (pipeline.IsInitialized()) {
        PassDef pass;
        pass.name     = "ParticleLighting";
        pass.stage    = PipelineStage::PostGeometry;
        pass.priority = 28;  // after SSR(25), before VolumetricClouds(80)
        pass.enabled  = m_enabled;
        pass.execute  = [this](PassContext& ctx) { ExecutePass(ctx); };
        m_pipelineHandle = pipeline.AddPass(pass);
    }

    m_initialized = true;
    SKSE::log::info("ParticleLightingRenderer: initialized ({}x{}, quarter {}x{})",
                    m_screenW, m_screenH, m_quarterW, m_quarterH);
    return true;
}

void ParticleLightingRenderer::Shutdown()
{
    ReleaseResources();
    m_initialized = false;
}

bool ParticleLightingRenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Helper lambda for compile + create
    auto compileCS = [&](const char* source, const char* name, ID3D11ComputeShader** outCS) -> bool {
        ID3DBlob* blob = ShaderLoader::Compile(name, source, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("ParticleLighting {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outCS);
        blob->Release();
        return SUCCEEDED(hr);
    };

    if (!compileCS(kEmissiveDetectCS, "ParticleLight_EmissiveDetect", &m_emissiveDetectCS)) return false;
    if (!compileCS(kLightScatterCS,   "ParticleLight_Scatter",       &m_lightScatterCS))   return false;
    if (!compileCS(kCompositeCS,      "ParticleLight_Composite",     &m_compositeCS))      return false;

    return true;
}

bool ParticleLightingRenderer::CreateResources()
{
    HRESULT hr;

    // ── Quarter-res emissive detection texture (SRV + UAV) ──────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_quarterW;
        desc.Height     = m_quarterH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_emissiveTex);
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(m_emissiveTex, nullptr, &m_emissiveSRV);
        if (FAILED(hr)) return false;

        hr = m_device->CreateUnorderedAccessView(m_emissiveTex, nullptr, &m_emissiveUAV);
        if (FAILED(hr)) return false;
    }

    // ── Quarter-res scatter result texture (SRV + UAV) ──────────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_quarterW;
        desc.Height     = m_quarterH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_scatterTex);
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(m_scatterTex, nullptr, &m_scatterSRV);
        if (FAILED(hr)) return false;

        hr = m_device->CreateUnorderedAccessView(m_scatterTex, nullptr, &m_scatterUAV);
        if (FAILED(hr)) return false;
    }

    // ── Full-res backbuffer copy texture (SRV only) ─────────────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_bbCopyTex);
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(m_bbCopyTex, nullptr, &m_bbCopySRV);
        if (FAILED(hr)) return false;
    }

    // ── Constant buffer (dynamic, 16-byte aligned) ──────────────────
    if (!CreateCB(m_device, sizeof(ParticleLightCBData), &m_constantsCB)) return false;

    // ── Linear sampler ──────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy  = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&sd, &m_linearSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}

void ParticleLightingRenderer::ReleaseResources()
{
    if (m_emissiveDetectCS) { m_emissiveDetectCS->Release(); m_emissiveDetectCS = nullptr; }
    if (m_lightScatterCS)   { m_lightScatterCS->Release();   m_lightScatterCS   = nullptr; }
    if (m_compositeCS)      { m_compositeCS->Release();      m_compositeCS      = nullptr; }

    if (m_emissiveTex) { m_emissiveTex->Release(); m_emissiveTex = nullptr; }
    if (m_emissiveSRV) { m_emissiveSRV->Release(); m_emissiveSRV = nullptr; }
    if (m_emissiveUAV) { m_emissiveUAV->Release(); m_emissiveUAV = nullptr; }

    if (m_scatterTex)  { m_scatterTex->Release();  m_scatterTex  = nullptr; }
    if (m_scatterSRV)  { m_scatterSRV->Release();  m_scatterSRV  = nullptr; }
    if (m_scatterUAV)  { m_scatterUAV->Release();  m_scatterUAV  = nullptr; }

    if (m_bbCopyTex)   { m_bbCopyTex->Release();   m_bbCopyTex   = nullptr; }
    if (m_bbCopySRV)   { m_bbCopySRV->Release();   m_bbCopySRV   = nullptr; }

    if (m_constantsCB)   { m_constantsCB->Release();   m_constantsCB   = nullptr; }
    if (m_backbufferUAV) { m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
    if (m_linearSampler) { m_linearSampler->Release(); m_linearSampler = nullptr; }
}


// ── Per-frame execution ───────────────────────────────────────────────────

void ParticleLightingRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_enabled || !m_initialized) return;

    // Guard: skip if scene RT isn't a full-color format (prevents black smearing
    // when phase detector fires on non-scene temp textures)
    if (ctx.gameSceneRTV) {
        ID3D11Resource* guardRes = nullptr;
        ctx.gameSceneRTV->GetResource(&guardRes);
        if (guardRes) {
            ID3D11Texture2D* guardTex = nullptr;
            guardRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&guardTex);
            guardRes->Release();
            if (guardTex) {
                D3D11_TEXTURE2D_DESC guardDesc;
                guardTex->GetDesc(&guardDesc);
                guardTex->Release();
                if (guardDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT &&
                    guardDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
                    guardDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
                    guardDesc.Format != DXGI_FORMAT_R11G11B10_FLOAT &&
                    guardDesc.Format != DXGI_FORMAT_R10G10B10A2_UNORM) {
                    return;
                }
            }
        }
    }

    // Acquire SRVs from other systems
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    // Prefer HiZ (reversed-Z) over raw game depth per pipeline rules
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }
    auto* materialSRV = MaterialClassifier::Get().IsInitialized() ? MaterialClassifier::Get().GetMaterialSRV() : nullptr;

    if (!depthSRV || !materialSRV) return;

    // Validate SceneMatrices
    auto& sm = SceneMatrices::Get();
    if (!sm.IsValid()) return;

    // Save OM state before backbuffer UAV creation (D3D11 auto-unbinds RTV)
    auto& cm = ComputeManager::Get();
    cm.SaveOMState();

    // ── Get backbuffer texture ──────────────────────────────────────
    ID3D11Texture2D* backbufferTex = nullptr;
    if (ctx.swapChain) {
        ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbufferTex);
    }
    if (!backbufferTex) { cm.RestoreOMState(); return; }

    // ── Copy backbuffer to m_bbCopyTex (for reading emissive pixels) ─
    ctx.context->CopyResource(m_bbCopyTex, backbufferTex);

    // ── Create per-frame backbuffer UAV ─────────────────────────────
    if (m_backbufferUAV) { m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    HRESULT hr = m_device->CreateUnorderedAccessView(backbufferTex, &uavDesc, &m_backbufferUAV);
    backbufferTex->Release();
    if (FAILED(hr)) { cm.RestoreOMState(); return; }

    // ── Update constant buffer ──────────────────────────────────────
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    auto* cb = static_cast<ParticleLightCBData*>(mapped.pData);
    memset(cb, 0, sizeof(ParticleLightCBData));

    cb->luminanceThreshold = m_luminanceThreshold;
    cb->intensity          = m_intensity;
    cb->scatterRadius      = m_scatterRadius;
    cb->falloffExponent    = m_falloffExponent;
    cb->depthTolerance     = m_depthTolerance;
    cb->nearZ              = sm.NearClip();
    cb->farZ               = sm.FarClip();
    cb->fullWidth          = m_screenW;
    cb->fullHeight         = m_screenH;
    cb->quarterWidth       = m_quarterW;
    cb->quarterHeight      = m_quarterH;
    cb->frameIndex         = sm.FrameIndex();
    memcpy(cb->invViewProj, sm.InvViewProjMatrix(), sizeof(float) * 16);
    memcpy(cb->cameraPos,   sm.CameraPos(),         sizeof(float) * 3);
    cb->pad = 0.0f;

    ctx.context->Unmap(m_constantsCB, 0);

    // Save CS state before dispatches
    cm.SaveCSState();

    // Bind CB at b0 (shared by all three passes)
    ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);

    // Bind linear sampler at s0 (used by scatter + composite)
    ctx.context->CSSetSamplers(0, 1, &m_linearSampler);

    // Null resources for unbinding
    ID3D11ShaderResourceView*  nullSRV  = nullptr;
    ID3D11UnorderedAccessView* nullUAV  = nullptr;
    ID3D11ShaderResourceView*  nullSRVs[2] = {nullptr, nullptr};

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  Pass 1: Emissive Detection (full-res -> quarter-res)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    {
        ctx.context->CSSetShader(m_emissiveDetectCS, nullptr, 0);

        // t0 = backbuffer copy, t1 = material ID
        ID3D11ShaderResourceView* srvs[] = { m_bbCopySRV, materialSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);

        // u0 = quarter-res emissive output
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_emissiveUAV, nullptr);

        uint32_t groupsX = (m_quarterW + 7) / 8;
        uint32_t groupsY = (m_quarterH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Unbind
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  Pass 2: Light Scatter (quarter-res)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    {
        ctx.context->CSSetShader(m_lightScatterCS, nullptr, 0);

        // t0 = emissive buffer, t1 = depth
        ID3D11ShaderResourceView* srvs[] = { m_emissiveSRV, depthSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);

        // u0 = quarter-res scatter output
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_scatterUAV, nullptr);

        uint32_t groupsX = (m_quarterW + 7) / 8;
        uint32_t groupsY = (m_quarterH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Unbind
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  Pass 3: Composite (quarter-res -> full-res additive blend)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    {
        ctx.context->CSSetShader(m_compositeCS, nullptr, 0);

        // t0 = scatter result, t1 = depth
        ID3D11ShaderResourceView* srvs[] = { m_scatterSRV, depthSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);

        // u0 = backbuffer UAV (write in-place)
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_backbufferUAV, nullptr);

        uint32_t groupsX = (m_screenW + 7) / 8;
        uint32_t groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Unbind
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    // ── Final cleanup ───────────────────────────────────────────────
    ctx.context->CSSetShader(nullptr, nullptr, 0);

    // Restore CS state + OM state
    cm.RestoreCSState();
    cm.RestoreOMState();

    m_frameIndex++;
}

} // namespace SB
