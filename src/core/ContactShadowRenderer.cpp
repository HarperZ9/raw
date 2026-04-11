//=============================================================================
//  ContactShadowRenderer.cpp — Screen-Space Contact Shadows
//
//  Dispatch flow (per frame, PreENB stage):
//    1. Dispatch Shadow CS:  per-pixel screen-space ray march toward sun → raw mask
//    2. Dispatch Denoise CS:  5×5 bilateral filter → denoised shadow mask
//    3. Register SRV at t28 for ENB shaders
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
#include "D3D11Hook.h"
#include "SceneData.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 1: Contact Shadow CS (full-res)
// ═══════════════════════════════════════════════════════════════════════════

static const char kContactShadowCS[] = R"HLSL(
// Contact Shadow CS — Screen-space ray march toward the directional light.
//
// For each pixel, we:
//   1. Reconstruct the view-space position from the depth buffer
//   2. Project the sun direction into screen space
//   3. Step along that 2D direction, comparing depth at each step
//   4. If sampled depth is in front of expected depth (within thickness), shadowed
//
// This catches fine contact shadows that shadow maps miss (hair, grass blades,
// character limbs, small debris, fences, etc.)

cbuffer ContactShadowCB : register(b0)
{
    float4x4 ViewMatrix;        // View matrix (row-major)
    float4x4 ProjMatrix;        // Projection matrix (row-major)
    uint2    ScreenDims;        // Full-res dimensions
    float    NearZ;
    float    FarZ;
    float3   SunDirWorld;       // Normalized world-space sun direction (toward sun)
    float    RayLength;         // Screen-space ray length (0.0-1.0 fraction of screen)
    float    Thickness;         // Depth thickness tolerance (view-space units)
    float    Intensity;         // Shadow darkness (0-2)
    int      MaxSteps;          // Max ray march steps (4-64)
    uint     FrameIndex;        // For temporal dither
    float    FPDepthThreshold;  // First-person depth mask (view-space units)
    float    pad0;
};

Texture2D<float> DepthTex : register(t0);   // Hi-Z mip 0 or raw depth
Texture2D<float> LinearDepth : register(t31); // Pre-computed linearized depth

RWTexture2D<float> ShadowOutput : register(u0); // Full-res shadow mask (1=lit, 0=shadowed)

// ── Screen UV → view-space position ──────────────────────────────────────
float3 UVToViewPos(float2 uv, float linearZ)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float3 viewPos;
    viewPos.x = ndc.x * linearZ / ProjMatrix[0][0];
    viewPos.y = ndc.y * linearZ / ProjMatrix[1][1];
    viewPos.z = linearZ;
    return viewPos;
}

// ── View-space position → screen UV ──────────────────────────────────────
float2 ViewPosToUV(float3 viewPos)
{
    float2 ndc;
    ndc.x = (viewPos.x * ProjMatrix[0][0]) / viewPos.z;
    ndc.y = (viewPos.y * ProjMatrix[1][1]) / viewPos.z;
    float2 uv;
    uv.x = ndc.x * 0.5 + 0.5;
    uv.y = 1.0 - (ndc.y * 0.5 + 0.5);
    return uv;
}

// ── Dither for temporal stability ────────────────────────────────────────
float DitherOffset(uint2 coord, uint seed)
{
    uint h = coord.x * 1597 + coord.y * 51749 + seed * 95317;
    h = (h ^ (h >> 16)) * 0x45d9f3b;
    h = (h ^ (h >> 16)) * 0x45d9f3b;
    h = h ^ (h >> 16);
    return float(h) / 4294967295.0;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);
    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv = (float2(coord) + 0.5) * texelSize;

    float rawDepth = DepthTex.Load(int3(coord, 0));

    // Skip sky (reversed-Z: sky near 0.0)
    if (rawDepth < 0.0001)
    {
        ShadowOutput[coord] = 1.0;
        return;
    }

    // Skip first-person geometry (hands/weapons) — no contact shadows on FP model
    float startLinearZ = LinearDepth.Load(int3(coord, 0));
    {
        if (startLinearZ < FPDepthThreshold)
        {
            ShadowOutput[coord] = 1.0;  // fully lit
            return;
        }
    }

    float3 viewPos = UVToViewPos(uv, startLinearZ);

    // Transform sun direction from world to view space
    float3 sunDirView;
    sunDirView.x = dot(SunDirWorld, float3(ViewMatrix[0][0], ViewMatrix[1][0], ViewMatrix[2][0]));
    sunDirView.y = dot(SunDirWorld, float3(ViewMatrix[0][1], ViewMatrix[1][1], ViewMatrix[2][1]));
    sunDirView.z = dot(SunDirWorld, float3(ViewMatrix[0][2], ViewMatrix[1][2], ViewMatrix[2][2]));
    sunDirView = normalize(sunDirView);

    // Project the ray endpoint to screen space to get the 2D ray direction
    float3 rayEndView = viewPos + sunDirView * (viewPos.z * 0.5);
    float2 rayEndUV = ViewPosToUV(rayEndView);
    float2 rayDir2D = rayEndUV - uv;
    float rayDirLen = length(rayDir2D);

    if (rayDirLen < 0.0001)
    {
        ShadowOutput[coord] = 1.0;
        return;
    }

    rayDir2D /= rayDirLen;  // normalize

    // Scale ray direction by the configured ray length
    float2 stepDir = rayDir2D * RayLength / float(MaxSteps);

    // Temporal dither to reduce banding
    float dither = DitherOffset(DTid.xy, FrameIndex) * 0.5;

    // Track the depth along the ray (interpolate between start and end)
    float3 rayEndPos = viewPos + sunDirView * (startLinearZ * RayLength * 2.0);
    float endLinearZ = rayEndPos.z;
    float deltaZ = (endLinearZ - startLinearZ) / float(MaxSteps);

    float shadow = 1.0;

    for (int step = 1; step <= MaxSteps; step++)
    {
        float t = (float(step) + dither) / float(MaxSteps);
        float2 sampleUV = uv + stepDir * (float(step) + dither);

        // Check bounds
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            break;

        int2 sampleCoord = int2(sampleUV * float2(ScreenDims));
        sampleCoord = clamp(sampleCoord, 0, int2(ScreenDims) - 1);

        float sampleDepth = DepthTex.Load(int3(sampleCoord, 0));
        if (sampleDepth < 0.0001)
            continue;  // Skip sky

        float sampleLinearZ = LinearDepth.Load(int3(sampleCoord, 0));
        float expectedZ = startLinearZ + deltaZ * (float(step) + dither);

        // The sample is "in shadow" if the scene depth at this screen position
        // is closer to the camera than our expected ray depth,
        // AND the difference is within the thickness tolerance.
        float depthDelta = expectedZ - sampleLinearZ;

        if (depthDelta > 0.0 && depthDelta < Thickness * expectedZ)
        {
            // Soft falloff based on step position (shadows near the pixel are darker)
            float falloff = 1.0 - t;
            falloff = falloff * falloff;  // quadratic falloff

            shadow = min(shadow, 1.0 - Intensity * falloff);
        }
    }

    ShadowOutput[coord] = saturate(shadow);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: Spatial Denoise CS (full-res, 5×5 bilateral)
// ═══════════════════════════════════════════════════════════════════════════

static const char kDenoiseCS[] = R"HLSL(
// Spatial denoise — 5x5 bilateral filter with depth-gradient edge stopping.
// Preserves shadow edges at depth discontinuities while smoothing noise
// from the limited-step ray march.

cbuffer DenoiseCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthThreshold;     // Edge-stopping depth sensitivity
    float3 pad0;
};

Texture2D<float> ShadowInput : register(t0);   // Raw contact shadows
Texture2D<float> DepthTex    : register(t1);   // Depth buffer
Texture2D<float> LinearDepth : register(t31);  // Pre-computed linearized depth

RWTexture2D<float> ShadowOutput : register(u0); // Denoised shadow mask

// 5x5 Gaussian weights
static const float kWeights[3] = { 0.375, 0.25, 0.0625 };

float GaussianWeight(int offset)
{
    return kWeights[abs(offset)];
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float centerDepth  = LinearDepth.Load(int3(coord, 0));
    float centerShadow = ShadowInput.Load(int3(coord, 0));

    float totalShadow = 0.0;
    float totalWeight  = 0.0;

    for (int dy = -2; dy <= 2; dy++)
    {
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 sampleCoord = coord + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, 0, int2(ScreenDims) - 1);

            float sampleDepth  = LinearDepth.Load(int3(sampleCoord, 0));
            float sampleShadow = ShadowInput.Load(int3(sampleCoord, 0));

            // Spatial weight (separable Gaussian)
            float spatialW = GaussianWeight(dx) * GaussianWeight(dy);

            // Depth edge stopping
            float depthDiff = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01);
            float depthW = exp(-depthDiff / max(DepthThreshold, 0.001));

            float w = spatialW * depthW;
            totalShadow += sampleShadow * w;
            totalWeight += w;
        }
    }

    ShadowOutput[coord] = totalShadow / max(totalWeight, 0.0001);
}
)HLSL";


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

    // Register as PreENB pipeline pass (shadow mask visible to ENB shaders)
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
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(source, strlen(source), name,
                                nullptr, nullptr, "main", "cs_5_0",
                                flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("ContactShadowRenderer: {} compile failed: {}", name,
                                 static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            return false;
        }
        if (err) err->Release();

        hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
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

    if (!CompileCS("ContactShadowMain",   kContactShadowCS, &m_shadowCS))  return false;
    if (!CompileCS("ContactShadowDenoise",kDenoiseCS,        &m_denoiseCS)) return false;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool ContactShadowRenderer::CreateResources()
{
    HRESULT hr;

    auto CreateMaskTexture = [&](const char* name,
                                  ID3D11Texture2D** outTex,
                                  ID3D11ShaderResourceView** outSRV,
                                  ID3D11UnorderedAccessView** outUAV) -> bool
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, outTex);
        if (FAILED(hr)) {
            SKSE::log::error("ContactShadowRenderer: failed to create {} texture (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(*outTex, &srvDesc, outSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R8_UNORM;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(*outTex, &uavDesc, outUAV);
        if (FAILED(hr)) return false;

        return true;
    };

    // Pass 1 output: raw shadow mask
    if (!CreateMaskTexture("shadowRaw", &m_shadowRaw, &m_shadowRawSRV, &m_shadowRawUAV))
        return false;

    // Pass 2 output: denoised shadow mask
    if (!CreateMaskTexture("shadowFinal", &m_shadowFinal, &m_shadowFinalSRV, &m_shadowFinalUAV))
        return false;

    // Constant buffers
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;

        desc.ByteWidth = sizeof(ContactShadowCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_shadowCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(DenoiseCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_denoiseCB);
        if (FAILED(hr)) return false;
    }

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
    return m_shadowFinalSRV;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PreENB stage)
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
        cb.fpDepthThreshold = 16.0f;  // ~16 view-space units (between ENB 11.76, CS 18.0)

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_shadowCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_shadowCB, 0);
        }

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

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_denoiseCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_denoiseCB, 0);
        }

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
