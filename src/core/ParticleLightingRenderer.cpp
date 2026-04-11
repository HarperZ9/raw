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
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"
#include "MaterialClassifier.h"
#include "ComputeManager.h"
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

// Shared constant buffer declaration (used by all three shaders)
static const char* kCBDecl = R"HLSL(
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
};

float luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
)HLSL";

// ── Pass 1: Emissive Detection CS ─────────────────────────────────────────

static const char* kEmissiveDetectCS = R"HLSL(
//  EmissiveDetectCS — Full-res to quarter-res downsample
//
//  Samples a 4x4 block of full-res pixels, keeps the brightest emissive
//  pixel (by luminance).  MaterialClassifier MAT_EMISSIVE=11 pixels pass
//  regardless of luminance threshold.
//
//  Inputs:
//    t0  = Backbuffer copy (SRV, full-res)
//    t1  = MaterialClassifier output (R8_UINT)
//  Output:
//    u0  = Quarter-res emissive buffer (R16G16B16A16_FLOAT)

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
};

float luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

Texture2D<float4> tBackbuffer : register(t0);
Texture2D<uint>   tMaterialID : register(t1);
RWTexture2D<float4> uEmissive : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= quarterWidth || DTid.y >= quarterHeight) return;

    // Sample 4x4 block of full-res pixels
    uint2 basePixel = DTid.xy * 4;
    float3 maxEmissive = float3(0, 0, 0);
    float maxLum = 0;

    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            uint2 p = basePixel + uint2(x, y);
            if (p.x >= fullWidth || p.y >= fullHeight) continue;

            float4 color = tBackbuffer[p];
            uint matID = tMaterialID[p];
            float lum = luminance(color.rgb);

            bool isEmissive = (matID == 11) || (lum > luminanceThreshold);
            if (isEmissive && lum > maxLum)
            {
                maxLum = lum;
                maxEmissive = color.rgb;
            }
        }
    }

    uEmissive[DTid.xy] = float4(maxEmissive, maxLum > 0 ? 1.0 : 0.0);
}
)HLSL";

// ── Pass 2: Light Scatter CS ──────────────────────────────────────────────

static const char* kLightScatterCS = R"HLSL(
//  LightScatterCS — Quarter-res radial light scatter
//
//  For each quarter-res texel, gathers light contribution from nearby
//  emissive pixels with distance falloff and depth-aware weighting.
//
//  Inputs:
//    t0  = Quarter-res emissive buffer (SRV)
//    t1  = Depth buffer (SRV)
//    s0  = Linear sampler
//  Output:
//    u0  = Quarter-res scatter result (R16G16B16A16_FLOAT)

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
};

Texture2D<float4> tEmissive : register(t0);
Texture2D<float>  tDepth    : register(t1);
RWTexture2D<float4> uScatter : register(u0);
SamplerState sLinear : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= quarterWidth || DTid.y >= quarterHeight) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(quarterWidth, quarterHeight);
    float centerDepth = tDepth.SampleLevel(sLinear, uv, 0);

    float3 accum = float3(0, 0, 0);
    float totalWeight = 0;

    // Gather light from nearby emissive pixels
    int searchRadius = (int)(scatterRadius / 4.0); // in quarter-res pixels
    searchRadius = min(searchRadius, 32);

    for (int dy = -searchRadius; dy <= searchRadius; dy++)
    {
        for (int dx = -searchRadius; dx <= searchRadius; dx++)
        {
            int2 samplePos = int2(DTid.xy) + int2(dx, dy);
            if (samplePos.x < 0 || samplePos.y < 0 ||
                (uint)samplePos.x >= quarterWidth || (uint)samplePos.y >= quarterHeight) continue;

            float4 emissive = tEmissive[samplePos];
            if (emissive.a < 0.5) continue; // No emissive source here

            float dist = length(float2(dx, dy));
            if (dist > searchRadius) continue;

            // Distance falloff
            float falloff = pow(saturate(1.0 - dist / searchRadius), falloffExponent);

            // Depth similarity
            float2 sampleUV = (float2(samplePos) + 0.5) / float2(quarterWidth, quarterHeight);
            float sampleDepth = tDepth.SampleLevel(sLinear, sampleUV, 0);
            float depthWeight = exp(-abs(centerDepth - sampleDepth) / max(depthTolerance, 0.001));

            float w = falloff * depthWeight;
            accum += emissive.rgb * w;
            totalWeight += w;
        }
    }

    if (totalWeight > 0) accum /= totalWeight;
    uScatter[DTid.xy] = float4(accum * intensity, totalWeight > 0 ? 1.0 : 0.0);
}
)HLSL";

// ── Pass 3: Composite CS ─────────────────────────────────────────────────

static const char* kCompositeCS = R"HLSL(
//  CompositeCS — Quarter-res scatter to full-res additive blend
//
//  Bilinearly upsamples the quarter-res scatter result and additively
//  blends it onto the backbuffer via UAV.
//
//  Inputs:
//    t0  = Quarter-res scatter result (SRV)
//    t1  = Depth buffer (SRV)
//    s0  = Linear sampler
//  Output:
//    u0  = Backbuffer (UAV, additive blend)

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
};

Texture2D<float4> tScatter : register(t0);
Texture2D<float>  tDepth   : register(t1);
SamplerState sLinear : register(s0);
RWTexture2D<float4> uBackbuffer : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= fullWidth || DTid.y >= fullHeight) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(fullWidth, fullHeight);
    float4 scatter = tScatter.SampleLevel(sLinear, uv, 0);

    if (scatter.a < 0.01) return; // No contribution

    float4 original = uBackbuffer[DTid.xy];

    // Additive blend with soft clamp
    float3 result = original.rgb + scatter.rgb;

    uBackbuffer[DTid.xy] = float4(result, original.a);
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

    // Register as PreENB pipeline pass
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
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;

        HRESULT hr = D3DCompile(source, strlen(source),
            name, nullptr, nullptr, "main", "cs_5_0", flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("ParticleLighting {} compile error: {}",
                    name, static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            return false;
        }

        hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outCS);
        blob->Release();
        if (err) err->Release();
        return SUCCEEDED(hr);
    };

    if (!compileCS(kEmissiveDetectCS, "EmissiveDetectCS", &m_emissiveDetectCS)) return false;
    if (!compileCS(kLightScatterCS,   "LightScatterCS",   &m_lightScatterCS))   return false;
    if (!compileCS(kCompositeCS,      "CompositeCS",      &m_compositeCS))      return false;

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
    {
        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth      = sizeof(ParticleLightCBData);
        cbd.Usage           = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;

        hr = m_device->CreateBuffer(&cbd, nullptr, &m_constantsCB);
        if (FAILED(hr)) return false;
    }

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
