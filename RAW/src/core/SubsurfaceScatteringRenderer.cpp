//=============================================================================
//  SubsurfaceScatteringRenderer.cpp — Screen-space subsurface scattering
//
//  Separable Burley diffusion profile for skin (MAT_SKIN=1) and foliage
//  (MAT_FOLIAGE=4).  Two-pass compute (horizontal + vertical) with
//  depth-aware bilateral weights to prevent cross-object bleeding.
//
//  Dispatch flow (per frame, PostGeometry stage):
//    1. CopyResource:  backbuffer -> m_bbCopyTex
//    2. Horizontal CS: read m_bbCopyTex (t0) -> write m_intermediateUAV (u0)
//    3. Vertical CS:   read m_intermediateSRV (t0) -> write m_backbufferUAV (u0)
//
//  Only pixels classified as skin or foliage are blurred; all others pass
//  through unmodified.
//=============================================================================

#include "SubsurfaceScatteringRenderer.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"
#include "MaterialClassifier.h"
#include "ComputeManager.h"
#include "SharedGPUResources.h"
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

static const char* kSSSBlurCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Separable bilateral Burley diffusion-profile blur for skin SSS.
// Dispatched twice per frame: once with direction=(1,0) for horizontal,
// once with direction=(0,1) for vertical.
//
// Only blurs pixels classified as skin (MAT_SKIN=1) or foliage
// (MAT_FOLIAGE=4) by MaterialClassifier. Non-SSS pixels pass through.
//
// Bilateral weight uses depth similarity to prevent bleed across
// silhouette edges:
//   weight = BurleyProfile(offset) * exp(-depthDiff^2 / (2*thickness^2))
//
// Reference: Burley, "Extending the Disney BRDF to a BSDF with
// Integrated Subsurface Scattering", SIGGRAPH 2015.
// Jimenez et al., "Separable Subsurface Scattering", CGF 2015.

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
}

static const uint MAT_SKIN    = 1;
static const uint MAT_FOLIAGE = 4;

Texture2D<float4> tInput      : register(t0);
Texture2D<uint>   tMaterialID : register(t1);
Texture2D<float>  tDepth      : register(t2);
Texture2D<float4> tNormals    : register(t3);
Texture2D<float>  LinearDepth : register(t31);
RWTexture2D<float4> uOutput   : register(u0);
SamplerState sPointClamp : register(s0);

// ---------------------------------------------------------------------------
// Burley diffusion profile (normalised Gaussian approximation)
// ---------------------------------------------------------------------------

// Burley's diffusion profile R(r) ~ A * exp(-r/d) + B * exp(-r/(3d))
// where d is the diffusion width.  We use the sum-of-Gaussians
// approximation from Jimenez et al. for separability.
//
// For a single channel with width 'w':
//   weight(offset) = 0.233 * G(offset, 0.0484*w) +
//                    0.100 * G(offset, 0.187*w)  +
//                    0.118 * G(offset, 0.567*w)  +
//                    0.113 * G(offset, 1.99*w)   +
//                    0.358 * G(offset, 7.41*w)
//
// We use a simplified 3-Gaussian kernel for efficiency.

float BurleyWeight(float offset, float width)
{
    float o2 = offset * offset;
    float w1 = width * 0.15;
    float w2 = width * 0.55;
    float w3 = width * 2.0;

    float g1 = exp(-o2 / (2.0 * w1 * w1 + 0.0001));
    float g2 = exp(-o2 / (2.0 * w2 * w2 + 0.0001));
    float g3 = exp(-o2 / (2.0 * w3 * w3 + 0.0001));

    return 0.40 * g1 + 0.35 * g2 + 0.25 * g3;
}

// Number of blur taps on each side of center.
static const int KERNEL_RADIUS = 11;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
    {
        return;
    }

    // Read center pixel material.
    uint matID = tMaterialID.Load(int3(DTid.xy, 0));
    bool isSkin    = (matID == MAT_SKIN);
    bool isFoliage = (matID == MAT_FOLIAGE);

    // Pass through non-SSS pixels unchanged.
    if (!isSkin && !isFoliage)
    {
        uOutput[DTid.xy] = tInput.Load(int3(DTid.xy, 0));
        return;
    }

    // Depth — HiZ reversed-Z.
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.00001)
    {
        uOutput[DTid.xy] = tInput.Load(int3(DTid.xy, 0));
        return;
    }

    // First-person geometry skip.
    float centerLinearZ = LinearDepth.Load(int3(DTid.xy, 0));
    if (centerLinearZ < 16.0)
    {
        uOutput[DTid.xy] = tInput.Load(int3(DTid.xy, 0));
        return;
    }

    // Per-channel Burley widths.
    float3 widths;
    if (isSkin)
        widths = skinWidth;
    else
        widths = float3(foliageWidth, foliageWidth, foliageWidth);

    // Scale blur radius by depth (closer = more pixels, further = fewer).
    // This approximates a world-space-constant blur width.
    float depthScale = saturate(100.0 / centerLinearZ);
    float effectiveRadius = sssRadius * depthScale;

    // Center pixel color.
    float4 centerColor = tInput.Load(int3(DTid.xy, 0));

    // Bilateral blur kernel.
    float3 colorAccum = float3(0, 0, 0);
    float3 weightAccum = float3(0, 0, 0);

    // Thickness parameter for bilateral depth rejection.
    // Larger = more permissive bleed across depth edges.
    float thickness = max(centerLinearZ * 0.02, 2.0);

    for (int i = -KERNEL_RADIUS; i <= KERNEL_RADIUS; ++i)
    {
        float offset = float(i);
        int2 sampleCoord = int2(DTid.xy) + int2(direction * offset);

        // Bounds check.
        if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
            (uint)sampleCoord.x >= screenWidth || (uint)sampleCoord.y >= screenHeight)
            continue;

        // Only blur within same material class.
        uint sampleMat = tMaterialID.Load(int3(sampleCoord, 0));
        if (sampleMat != matID)
        {
            // Weight center pixel's color for foreign material samples.
            float3 bw;
            bw.r = BurleyWeight(offset, widths.r * effectiveRadius);
            bw.g = BurleyWeight(offset, widths.g * effectiveRadius);
            bw.b = BurleyWeight(offset, widths.b * effectiveRadius);
            colorAccum += centerColor.rgb * bw;
            weightAccum += bw;
            continue;
        }

        float4 sampleColor = tInput.Load(int3(sampleCoord, 0));

        // Bilateral depth weight: reject samples with large depth difference.
        float sampleLinearZ = LinearDepth.Load(int3(sampleCoord, 0));
        float depthDiff = abs(centerLinearZ - sampleLinearZ);
        float depthWeight = exp(-(depthDiff * depthDiff) / (2.0 * thickness * thickness));

        // Burley profile weight per channel.
        float3 bw;
        bw.r = BurleyWeight(offset, widths.r * effectiveRadius);
        bw.g = BurleyWeight(offset, widths.g * effectiveRadius);
        bw.b = BurleyWeight(offset, widths.b * effectiveRadius);

        float3 w = bw * depthWeight;
        colorAccum += sampleColor.rgb * w;
        weightAccum += w;
    }

    // Normalise.
    float3 blurred;
    blurred.r = (weightAccum.r > 0.0001) ? (colorAccum.r / weightAccum.r) : centerColor.r;
    blurred.g = (weightAccum.g > 0.0001) ? (colorAccum.g / weightAccum.g) : centerColor.g;
    blurred.b = (weightAccum.b > 0.0001) ? (colorAccum.b / weightAccum.b) : centerColor.b;

    // Blend between original and blurred by sssStrength.
    float3 finalColor = lerp(centerColor.rgb, blurred, sssStrength);

    uOutput[DTid.xy] = float4(finalColor, centerColor.a);
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

    // Register as PostGeometry pipeline pass (after ContactShadows/Skylighting/GrassLighting)
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
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = ShaderLoader::Compile("SSS_Blur", kSSSBlurCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("SSSBlurCS compile failed");
        return false;
    }

    HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                        nullptr, &m_sssBlurCS);
    blob->Release();
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
    if (!CreateCB(m_device, sizeof(SSSBlurCBData), &m_constantsCB)) return false;

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
