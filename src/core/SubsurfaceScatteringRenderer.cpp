//=============================================================================
//  SubsurfaceScatteringRenderer.cpp — Screen-space subsurface scattering
//
//  Separable Burley diffusion profile for skin (MAT_SKIN=1) and foliage
//  (MAT_FOLIAGE=4).  Two-pass compute (horizontal + vertical) with
//  depth-aware bilateral weights to prevent cross-object bleeding.
//
//  Dispatch flow (per frame, PreENB stage):
//    1. CopyResource:  backbuffer -> m_bbCopyTex
//    2. Horizontal CS: read m_bbCopyTex (t0) -> write m_intermediateUAV (u0)
//    3. Vertical CS:   read m_intermediateSRV (t0) -> write m_backbufferUAV (u0)
//
//  Only pixels classified as skin or foliage are blurred; all others pass
//  through unmodified.
//=============================================================================

#include "SubsurfaceScatteringRenderer.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"
#include "MaterialClassifier.h"
#include "ComputeManager.h"
#include "SharedGPUResources.h"
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kSSSBlurCS = R"HLSL(
//  SSSBlurCS — Separable Burley diffusion profile blur
//
//  Single shader used for BOTH horizontal and vertical passes.
//  Direction is selected via the constant buffer (1,0) or (0,1).
//
//  Inputs:
//    t0 = Source color (copy of backbuffer or intermediate)
//    t1 = MaterialClassifier output (R8_UINT, material ID per pixel)
//    t2 = Depth buffer (R32_FLOAT or similar)
//    t3 = G-buffer normals (R16G16B16A16_FLOAT or R10G10B10A2_UNORM)
//  Output:
//    u0 = Destination (intermediate or backbuffer UAV)

cbuffer SSSBlurCB : register(b0)
{
    float2 direction;       // (1,0) for horizontal, (0,1) for vertical
    float  sssRadius;       // Screen-space blur radius
    float  sssStrength;     // Overall SSS intensity
    float  translucency;    // Back-lighting translucency factor
    float3 skinWidth;       // Per-channel Burley widths for skin (R, G, B)
    float  foliageWidth;    // Foliage SSS width (single channel, broader)
    float  nearZ;
    float  farZ;
    float  pad0;
    uint   screenWidth;
    uint   screenHeight;
    uint   frameIndex;
    uint   pad1;
};

// Material IDs from MaterialClassifier
static const uint MAT_SKIN    = 1;
static const uint MAT_FOLIAGE = 4;

Texture2D<float4> tInput      : register(t0);
Texture2D<uint>   tMaterialID : register(t1);
Texture2D<float>  tDepth      : register(t2);
Texture2D<float4> tNormals    : register(t3);
Texture2D<float>  LinearDepth : register(t31);

RWTexture2D<float4> uOutput   : register(u0);

SamplerState sPointClamp : register(s0);

// ── Burley diffusion profile approximation ──────────────────────────────
// Two-term sum-of-Gaussians fit to the normalized Burley diffusion profile.
// R(r) = A * exp(-r / (2*s)) + B * exp(-r / (6*s))
// where s = scatter width, r = distance.  Weights A,B chosen so integral = 1.
float BurleyWeight(float r, float s)
{
    float rr = abs(r);
    float w1 = exp(-rr / max(2.0 * s, 0.0001));
    float w2 = exp(-rr / max(6.0 * s, 0.0001));
    return 0.65 * w1 + 0.35 * w2;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    uint2 pixel = DTid.xy;

    // Read material ID — only process skin or foliage
    uint matID = tMaterialID[pixel];
    float4 centerColor = tInput[pixel];

    if (matID != MAT_SKIN && matID != MAT_FOLIAGE)
    {
        // Pass through unmodified
        uOutput[pixel] = centerColor;
        return;
    }

    // Choose blur width based on material
    float3 width;
    if (matID == MAT_SKIN)
    {
        width = skinWidth;
    }
    else // MAT_FOLIAGE
    {
        width = float3(foliageWidth, foliageWidth, foliageWidth);
    }

    // Scale widths by global radius
    width *= sssRadius;

    // Linearize center pixel depth
    float centerRawDepth = tDepth[pixel];
    float centerLinearZ  = LinearDepth[pixel];

    // Read center normal for translucency weighting
    float3 centerNormal = tNormals[pixel].xyz * 2.0 - 1.0;
    centerNormal = normalize(centerNormal);

    // Depth threshold for bilateral weight (proportional to center depth)
    float depthThreshold = centerLinearZ * 0.02;
    depthThreshold = max(depthThreshold, 0.1);

    // Texel size in the blur direction
    float2 texelStep = direction / float2(screenWidth, screenHeight);

    // ── 25-tap separable kernel (-12 to +12) ─────────────────────────────
    float3 accumColor  = float3(0, 0, 0);
    float3 accumWeight = float3(0, 0, 0);

    for (int i = -12; i <= 12; i++)
    {
        float2 offset = float(i) * texelStep;
        int2 sampleCoord = int2(pixel) + int2(round(float(i) * direction));

        // Clamp to screen bounds
        sampleCoord = clamp(sampleCoord, int2(0, 0),
                            int2(screenWidth - 1, screenHeight - 1));

        float4 sampleColor = tInput[sampleCoord];
        float  sampleRawDepth = tDepth[sampleCoord];
        float  sampleLinearZ  = LinearDepth[sampleCoord];

        // Bilateral depth weight — prevents bleeding across depth edges
        float depthDiff = abs(sampleLinearZ - centerLinearZ);
        float bilateralW = exp(-(depthDiff * depthDiff) /
                               max(depthThreshold * depthThreshold, 0.0001));

        // Normalized distance for Burley profile
        float dist = abs(float(i)) / max(float(screenWidth + screenHeight) * 0.5, 1.0);

        // Per-channel Burley diffusion weight
        float3 burleyW;
        burleyW.r = BurleyWeight(dist, width.r);
        burleyW.g = BurleyWeight(dist, width.g);
        burleyW.b = BurleyWeight(dist, width.b);

        // Combined weight
        float3 w = burleyW * bilateralW;

        accumColor  += sampleColor.rgb * w;
        accumWeight += w;
    }

    // Normalize
    float3 blurred = accumColor / max(accumWeight, float3(0.0001, 0.0001, 0.0001));

    // Blend between original and blurred based on SSS strength
    float3 result = lerp(centerColor.rgb, blurred, sssStrength);

    uOutput[pixel] = float4(result, centerColor.a);
}
)HLSL";


// ── Constant buffer layout ────────────────────────────────────────────────

struct alignas(16) SSSBlurCBData
{
    float    direction[2];    // +0   (1,0) horizontal or (0,1) vertical
    float    sssRadius;       // +8
    float    sssStrength;     // +12
    float    translucency;    // +16
    float    skinWidth[3];    // +20  per-channel Burley widths (R, G, B)
    float    foliageWidth;    // +32
    float    nearZ;           // +36
    float    farZ;            // +40
    float    pad0;            // +44
    uint32_t screenWidth;     // +48
    uint32_t screenHeight;    // +52
    uint32_t frameIndex;      // +56
    uint32_t pad1;            // +60
};                            // total = 64 bytes (4 x 16)
static_assert(sizeof(SSSBlurCBData) == 64, "SSSBlurCBData must be 64 bytes");


// ── Initialize ────────────────────────────────────────────────────────────

bool SubsurfaceScatteringRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                                IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    DXGI_SWAP_CHAIN_DESC scDesc;
    if (FAILED(sc->GetDesc(&scDesc))) return false;
    m_screenW = scDesc.BufferDesc.Width;
    m_screenH = scDesc.BufferDesc.Height;

    if (!CompileShaders()) return false;
    if (!CreateResources()) {
        ReleaseResources();
        return false;
    }

    // Register as PreENB pipeline pass (after ContactShadows/Skylighting/GrassLighting)
    auto& pipeline = RenderPipeline::Get();
    if (pipeline.IsInitialized()) {
        PassDef pass;
        pass.name     = "SubsurfaceScattering";
        pass.stage    = PipelineStage::PostGeometry;
        pass.priority = 23;
        pass.enabled  = m_enabled;
        pass.execute  = [this](PassContext& ctx) { ExecutePass(ctx); };
        m_pipelineHandle = pipeline.AddPass(pass);
    }

    m_initialized = true;
    SKSE::log::info("SubsurfaceScatteringRenderer: initialized ({}x{})", m_screenW, m_screenH);
    return true;
}

void SubsurfaceScatteringRenderer::Shutdown()
{
    if (!m_initialized) return;

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("SubsurfaceScatteringRenderer: shut down");
}


// ── Compile shaders ───────────────────────────────────────────────────────

bool SubsurfaceScatteringRenderer::CompileShaders()
{
    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(kSSSBlurCS, strlen(kSSSBlurCS),
        "SSSBlurCS", nullptr, nullptr, "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("SSSBlurCS compile error: {}",
                static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }

    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                        nullptr, &m_sssBlurCS);
    blob->Release();
    if (err) err->Release();
    return SUCCEEDED(hr);
}


// ── Create GPU resources ──────────────────────────────────────────────────

bool SubsurfaceScatteringRenderer::CreateResources()
{
    HRESULT hr;

    // ── Intermediate texture (horizontal blur output) ─────────────────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_intermediateTex);
        if (FAILED(hr)) {
            SKSE::log::error("SubsurfaceScatteringRenderer: failed to create intermediate texture (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_intermediateTex, &srvDesc, &m_intermediateSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(m_intermediateTex, &uavDesc, &m_intermediateUAV);
        if (FAILED(hr)) return false;
    }

    // ── Backbuffer copy texture (SRV only — source for reading) ───────────
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
        if (FAILED(hr)) {
            SKSE::log::error("SubsurfaceScatteringRenderer: failed to create backbuffer copy texture (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_bbCopyTex, &srvDesc, &m_bbCopySRV);
        if (FAILED(hr)) return false;
    }

    // ── Constant buffer ───────────────────────────────────────────────────
    {
        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth      = sizeof(SSSBlurCBData);
        cbd.Usage           = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
        hr = m_device->CreateBuffer(&cbd, nullptr, &m_constantsCB);
        if (FAILED(hr)) return false;
    }

    // ── Point clamp sampler ───────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&desc, &m_pointSampler);
        if (FAILED(hr)) {
            SKSE::log::error("SubsurfaceScatteringRenderer: failed to create point sampler (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    return true;
}


void SubsurfaceScatteringRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_sssBlurCS);
    SafeRelease(m_constantsCB);

    SafeRelease(m_intermediateTex);
    SafeRelease(m_intermediateSRV);
    SafeRelease(m_intermediateUAV);

    SafeRelease(m_bbCopyTex);
    SafeRelease(m_bbCopySRV);

    SafeRelease(m_backbufferUAV);
    SafeRelease(m_pointSampler);
}


// ── Per-frame execution ───────────────────────────────────────────────────

void SubsurfaceScatteringRenderer::ExecutePass(PassContext& ctx)
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

    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) return;

    // Acquire SRVs from other systems
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    // Prefer HiZ (reversed-Z) over raw game depth per pipeline rules
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }
    auto* materialSRV = MaterialClassifier::Get().IsInitialized()
                            ? MaterialClassifier::Get().GetMaterialSRV() : nullptr;
    auto* normalsSRV  = D3D11Hook::GetGBufferNormalsSRV();

    if (!depthSRV || !materialSRV || !normalsSRV) return;

    // Save CS + OM state before dispatches (backbuffer UAV unbinds RTV)
    auto& cm = ComputeManager::Get();
    cm.SaveCSState();
    cm.SaveOMState();

    // ── Get backbuffer texture ────────────────────────────────────────────
    ID3D11Texture2D* backbufferTex = nullptr;
    if (ctx.swapChain) {
        ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbufferTex);
    }
    if (!backbufferTex) { cm.RestoreOMState(); return; }

    // ── Step 1: Copy backbuffer to m_bbCopyTex ────────────────────────────
    ctx.context->CopyResource(m_bbCopyTex, backbufferTex);

    // ── Create per-frame backbuffer UAV ───────────────────────────────────
    if (m_backbufferUAV) { m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    HRESULT hr = m_device->CreateUnorderedAccessView(backbufferTex, &uavDesc, &m_backbufferUAV);
    backbufferTex->Release();
    if (FAILED(hr)) { cm.RestoreOMState(); return; }

    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    uint32_t groupsX = (m_screenW + 7) / 8;
    uint32_t groupsY = (m_screenH + 7) / 8;

    // Bind linearized depth for both passes
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: Horizontal blur — read m_bbCopySRV (t0) -> write m_intermediateUAV (u0)
    // ═════════════════════════════════════════════════════════════════════
    {
        // Update constant buffer — horizontal direction
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return;

        auto* cb = static_cast<SSSBlurCBData*>(mapped.pData);
        std::memset(cb, 0, sizeof(SSSBlurCBData));

        cb->direction[0]  = 1.0f;  // Horizontal
        cb->direction[1]  = 0.0f;
        cb->sssRadius     = m_sssRadius;
        cb->sssStrength   = m_sssStrength;
        cb->translucency  = m_translucency;
        cb->skinWidth[0]  = m_skinWidthR;
        cb->skinWidth[1]  = m_skinWidthG;
        cb->skinWidth[2]  = m_skinWidthB;
        cb->foliageWidth  = m_foliageWidth;
        cb->nearZ         = nearZ;
        cb->farZ          = farZ;
        cb->screenWidth   = m_screenW;
        cb->screenHeight  = m_screenH;
        cb->frameIndex    = m_frameIndex;

        ctx.context->Unmap(m_constantsCB, 0);

        // Bind resources
        ctx.context->CSSetShader(m_sssBlurCS, nullptr, 0);
        ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);
        ctx.context->CSSetSamplers(0, 1, &m_pointSampler);

        ID3D11ShaderResourceView* srvs[] = {
            m_bbCopySRV,   // t0: source color (backbuffer copy)
            materialSRV,   // t1: material ID
            depthSRV,      // t2: depth
            normalsSRV,    // t3: normals
        };
        ctx.context->CSSetShaderResources(0, 4, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_intermediateUAV, nullptr);

        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Unbind
        ID3D11ShaderResourceView*  nullSRVs[4] = {};
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        ctx.context->CSSetShaderResources(0, 4, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: Vertical blur — read m_intermediateSRV (t0) -> write m_backbufferUAV (u0)
    // ═════════════════════════════════════════════════════════════════════
    {
        // Update constant buffer — vertical direction
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return;

        auto* cb = static_cast<SSSBlurCBData*>(mapped.pData);
        std::memset(cb, 0, sizeof(SSSBlurCBData));

        cb->direction[0]  = 0.0f;
        cb->direction[1]  = 1.0f;  // Vertical
        cb->sssRadius     = m_sssRadius;
        cb->sssStrength   = m_sssStrength;
        cb->translucency  = m_translucency;
        cb->skinWidth[0]  = m_skinWidthR;
        cb->skinWidth[1]  = m_skinWidthG;
        cb->skinWidth[2]  = m_skinWidthB;
        cb->foliageWidth  = m_foliageWidth;
        cb->nearZ         = nearZ;
        cb->farZ          = farZ;
        cb->screenWidth   = m_screenW;
        cb->screenHeight  = m_screenH;
        cb->frameIndex    = m_frameIndex;

        ctx.context->Unmap(m_constantsCB, 0);

        // Bind resources
        ID3D11ShaderResourceView* srvs[] = {
            m_intermediateSRV,  // t0: source color (horizontal blur output)
            materialSRV,        // t1: material ID
            depthSRV,           // t2: depth
            normalsSRV,         // t3: normals
        };
        ctx.context->CSSetShaderResources(0, 4, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_backbufferUAV, nullptr);

        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Unbind all resources
        ID3D11ShaderResourceView*  nullSRVs[4] = {};
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        ctx.context->CSSetShaderResources(0, 4, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    // Unbind linearized depth
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    // Unbind shader and sampler
    ctx.context->CSSetShader(nullptr, nullptr, 0);
    ID3D11SamplerState* nullSampler = nullptr;
    ctx.context->CSSetSamplers(0, 1, &nullSampler);

    // Restore CS state + OM state
    cm.RestoreCSState();
    cm.RestoreOMState();

    m_frameIndex++;
}

} // namespace SB
