//=============================================================================
//  IndirectSpecularRenderer.cpp — Indirect Specular via SSR + Cubemap Fallback
//
//  Dispatch flow (per frame, PreENB stage):
//    1. Gather inputs: SSR (t21), dynamic cubemap (t30), depth, normals, matID
//    2. Dispatch IndirectSpecular CS: blend SSR near-field + cubemap far-field,
//       apply roughness-based weighting + Schlick Fresnel
//    3. Register combined SRV at t32 for ENB shaders
//
//  Algorithm:
//    For each pixel, reconstruct the world position and compute the reflection
//    direction from the G-buffer normal.  Sample SSR for near-field reflections
//    and the dynamic cubemap (with roughness-based mip) for far-field.  Blend
//    based on SSR confidence and surface roughness.  Apply Fresnel attenuation
//    for physically-correct energy conservation.
//=============================================================================

#include "IndirectSpecularRenderer.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"
#include "ComputeManager.h"
#include "SSRRenderer.h"
#include "DynamicCubemapRenderer.h"
#include "SRVInjector.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// =============================================================================
//  Embedded HLSL -- Indirect Specular CS (full-res)
// =============================================================================

static const char kIndirectSpecularCS[] = R"HLSL(
// IndirectSpecularCS -- Combine SSR near-field + cubemap far-field reflections.
//
// For each pixel:
//   1. Read depth, skip sky pixels
//   2. Reconstruct world position from depth + inverse view-projection
//   3. Read G-buffer normal, decode from [0,1] to [-1,1]
//   4. Compute reflection direction = reflect(-viewDir, normal)
//   5. Estimate roughness from material classification
//   6. Skip pixels above roughness threshold (too rough for specular)
//   7. Sample SSR (half-res) and cubemap (roughness-based mip)
//   8. Blend SSR + cubemap based on SSR confidence and roughness
//   9. Apply Schlick Fresnel approximation
//  10. Output combined indirect specular

cbuffer IndirectSpecularCB : register(b0)
{
    float3   CameraPos;
    float    Intensity;
    float4x4 InvViewProj;
    float4x4 ViewMatrix;
    float    NearZ;
    float    FarZ;
    uint     ScreenWidth;
    uint     ScreenHeight;
    float    CubemapFallback;
    float    FresnelBias;
    float    RoughnessThreshold;
    uint     FrameIndex;
};

Texture2D<float4>   tSSR        : register(t0);  // SSR output (.rgb=color, .a=confidence)
TextureCube<float4> tCubemap    : register(t1);  // Dynamic cubemap with mips
Texture2D<float>    tDepth      : register(t2);  // Depth buffer
Texture2D<float4>   tNormals    : register(t3);  // G-buffer normals
Texture2D<uint>     tMaterialID : register(t4);  // Material classification

SamplerState sLinear : register(s0);

RWTexture2D<float4> uOutput : register(u0);

// Material roughness lookup table
float GetRoughnessFromMaterial(uint matID)
{
    if (matID == 1) return 0.35;  // Skin
    if (matID == 2) return 0.25;  // Metal
    if (matID == 3) return 0.85;  // Stone
    if (matID == 4) return 0.90;  // Foliage
    if (matID == 5) return 0.02;  // Water
    if (matID == 6) return 0.70;  // Snow
    if (matID == 7) return 0.05;  // Glass
    if (matID == 8) return 0.90;  // Fabric
    if (matID == 9) return 0.80;  // Wood
    if (matID == 10) return 0.85; // Terrain
    return 0.50; // Unknown
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenWidth || DTid.y >= ScreenHeight)
        return;

    int2 pixel = int2(DTid.xy);

    // 1. Read depth, skip sky pixels
    float depth = tDepth.Load(int3(pixel, 0));
    if (depth >= 0.999)
    {
        uOutput[pixel] = float4(0, 0, 0, 0);
        return;
    }

    // 2. Decode G-buffer normal from [0,1] to [-1,1]
    float4 normalEnc = tNormals.Load(int3(pixel, 0));
    float3 normal = normalize(normalEnc.xyz * 2.0 - 1.0);

    // 3. Reconstruct world position from depth
    float2 uv = (float2(pixel) + 0.5) / float2(ScreenWidth, ScreenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos4 = mul(InvViewProj, clipPos);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    // 4. Compute view direction and reflection direction
    float3 viewDir = normalize(CameraPos - worldPos);
    float3 reflectDir = reflect(-viewDir, normal);

    // 5. Estimate roughness from material type
    uint matID = tMaterialID.Load(int3(pixel, 0));
    float roughness = GetRoughnessFromMaterial(matID);

    // 6. Skip pixels above roughness threshold (too rough for specular)
    if (roughness > RoughnessThreshold)
    {
        uOutput[pixel] = float4(0, 0, 0, 0);
        return;
    }

    // 7. Sample SSR (half-res -- scale pixel coordinates)
    int2 ssrCoord = pixel / 2;
    float4 ssr = tSSR.Load(int3(ssrCoord, 0));

    // 8. Sample cubemap with roughness-based mip selection
    //    roughness 0 -> mip 0 (sharp), roughness 1 -> mip 6 (blurry)
    float4 cube = tCubemap.SampleLevel(sLinear, reflectDir, roughness * 6.0);

    // 9. Blend SSR and cubemap based on SSR confidence and roughness
    //    Smooth surfaces trust SSR more; rough surfaces fall back to cubemap
    float ssrWeight = ssr.a * (1.0 - roughness * roughness);
    float3 combined = lerp(cube.rgb * CubemapFallback, ssr.rgb, ssrWeight);

    // 10. Schlick Fresnel approximation
    float NdotV = saturate(dot(viewDir, normal));
    float F = FresnelBias + (1.0 - FresnelBias) * pow(1.0 - NdotV, 5.0);

    // 11. Final output: combined specular * Fresnel * intensity
    float confidence = max(ssr.a, CubemapFallback > 0 ? 0.5 : 0);
    uOutput[pixel] = float4(combined * F * Intensity, confidence);
}
)HLSL";


// =============================================================================
//  CB structure -- must match HLSL cbuffer exactly
// =============================================================================

struct alignas(16) IndirectSpecularCBData
{
    float    cameraPos[3];        // +0    (12 bytes)
    float    intensity;           // +12   (4 bytes)
    float    invViewProj[16];     // +16   (64 bytes)
    float    viewMatrix[16];      // +80   (64 bytes)
    float    nearZ;               // +144
    float    farZ;                // +148
    uint32_t screenWidth;         // +152
    uint32_t screenHeight;        // +156
    float    cubemapFallback;     // +160
    float    fresnelBias;         // +164
    float    roughnessThreshold;  // +168
    uint32_t frameIndex;          // +172
};                                // +176 total
static_assert(sizeof(IndirectSpecularCBData) == 176, "IndirectSpecularCB must be 176 bytes");


// =============================================================================
//  Initialize
// =============================================================================

bool IndirectSpecularRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& pl = RenderPipeline::Get();

    if (!pl.IsInitialized()) {
        SKSE::log::error("IndirectSpecularRenderer: RenderPipeline not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("IndirectSpecularRenderer: failed to get backbuffer (0x{:X})",
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

    // Register SRV at t32 for ENB shaders
    SRVInjector::Get().RegisterSRV(kSRVSlot, m_outputSRV);

    // Register as PreENB pipeline pass (after SSR @ 25, uses SSR + cubemap outputs)
    m_pipelineHandle = pl.AddPass({
        .name     = "IndirectSpecular",
        .stage    = PipelineStage::PostGeometry,
        .priority = 21,
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_frameIndex  = 0;

    SKSE::log::info("IndirectSpecularRenderer: initialized ({}x{}, t{})",
                    m_screenW, m_screenH, kSRVSlot);
    return true;
}


// =============================================================================
//  Compile shader
// =============================================================================

bool IndirectSpecularRenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    HRESULT hr = D3DCompile(kIndirectSpecularCS, strlen(kIndirectSpecularCS),
                            "IndirectSpecularCS", nullptr, nullptr,
                            "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("IndirectSpecularRenderer: compile failed: {}",
                             static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    if (err) err->Release();

    hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                        blob->GetBufferSize(),
                                        nullptr, &m_indirectSpecularCS);
    blob->Release();
    if (FAILED(hr)) {
        SKSE::log::error("IndirectSpecularRenderer: CreateComputeShader failed (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    return true;
}


// =============================================================================
//  Create GPU resources
// =============================================================================

bool IndirectSpecularRenderer::CreateResources()
{
    HRESULT hr;

    // Output texture: full-res R16G16B16A16_FLOAT (SRV + UAV)
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

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_outputTex);
        if (FAILED(hr)) {
            SKSE::log::error("IndirectSpecularRenderer: failed to create output texture (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_outputTex, &srvDesc, &m_outputSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = desc.Format;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(m_outputTex, &uavDesc, &m_outputUAV);
        if (FAILED(hr)) return false;
    }

    // Constant buffer
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.ByteWidth      = sizeof(IndirectSpecularCBData);

        hr = m_device->CreateBuffer(&desc, nullptr, &m_constantsCB);
        if (FAILED(hr)) {
            SKSE::log::error("IndirectSpecularRenderer: failed to create CB (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    // Linear clamp sampler (for cubemap + SSR sampling)
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy  = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&desc, &m_linearClampSampler);
        if (FAILED(hr)) {
            SKSE::log::error("IndirectSpecularRenderer: failed to create sampler (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    return true;
}


// =============================================================================
//  GetOutputSRV
// =============================================================================

ID3D11ShaderResourceView* IndirectSpecularRenderer::GetOutputSRV() const
{
    if (!m_initialized || !m_enabled) return nullptr;
    return m_outputSRV;
}


// =============================================================================
//  Per-frame execution (called by RenderPipeline at PreENB stage)
// =============================================================================

void IndirectSpecularRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Gate: scene matrices must be valid (camera data verified non-garbage)
    if (!SceneMatrices::Get().IsValid()) return;

    // ── Gather input SRVs ──────────────────────────────────────────────
    ID3D11ShaderResourceView* ssrSRV = SSRRenderer::Get().GetReflectionSRV();
    ID3D11ShaderResourceView* cubemapSRV = DynamicCubemapRenderer::Get().GetCubemapSRV();
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    // Prefer HiZ (reversed-Z) over raw game depth per pipeline rules
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }
    ID3D11ShaderResourceView* normalsSRV = D3D11Hook::GetGBufferNormalsSRV();
    ID3D11ShaderResourceView* materialSRV = D3D11Hook::GetGBufferMaterialSRV();

    // Need at minimum depth and normals to produce meaningful output
    if (!depthSRV || !normalsSRV) return;

    // SSR or cubemap must be available (otherwise nothing to blend)
    if (!ssrSRV && !cubemapSRV) return;

    auto& scene = SceneMatrices::Get();

    // ── Update constant buffer ─────────────────────────────────────────
    {
        IndirectSpecularCBData cb = {};
        const float* camPos = scene.CameraPos();
        cb.cameraPos[0]      = camPos[0];
        cb.cameraPos[1]      = camPos[1];
        cb.cameraPos[2]      = camPos[2];
        cb.intensity          = m_intensity;
        std::memcpy(cb.invViewProj, scene.InvViewProjMatrix(), sizeof(float) * 16);
        std::memcpy(cb.viewMatrix,  scene.ViewMatrix(),        sizeof(float) * 16);
        cb.nearZ              = scene.NearClip();
        cb.farZ               = scene.FarClip();
        cb.screenWidth        = m_screenW;
        cb.screenHeight       = m_screenH;
        cb.cubemapFallback    = m_cubemapFallback;
        cb.fresnelBias        = m_fresnelBias;
        cb.roughnessThreshold = m_roughnessThreshold;
        cb.frameIndex         = m_frameIndex;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_constantsCB, 0);
        }
    }

    // ── Save CS state via ComputeManager ─────────────────────────────
    auto& cm = ComputeManager::Get();
    cm.SaveCSState();

    // ── Bind resources and dispatch ────────────────────────────────────
    ID3D11ShaderResourceView* srvs[5] = {
        ssrSRV,       // t0 — SSR output
        nullptr,      // t1 — cubemap (bound separately as it's a TextureCube)
        depthSRV,     // t2 — depth buffer
        normalsSRV,   // t3 — G-buffer normals
        materialSRV   // t4 — material classification
    };
    // Bind cubemap at t1 (TextureCube uses the same SRV slot mechanism)
    srvs[1] = cubemapSRV;

    ctx.context->CSSetShaderResources(0, 5, srvs);
    ctx.context->CSSetSamplers(0, 1, &m_linearClampSampler);
    ctx.context->CSSetUnorderedAccessViews(0, 1, &m_outputUAV, nullptr);
    ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);
    ctx.context->CSSetShader(m_indirectSpecularCS, nullptr, 0);

    UINT groupsX = (m_screenW + 7) / 8;
    UINT groupsY = (m_screenH + 7) / 8;
    ctx.context->Dispatch(groupsX, groupsY, 1);

    // ── Unbind all ─────────────────────────────────────────────────────
    ID3D11ShaderResourceView* nullSRVs[5] = {};
    ctx.context->CSSetShaderResources(0, 5, nullSRVs);
    ID3D11UnorderedAccessView* nullUAV[1] = {};
    ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

    // ── Restore CS state ───────────────────────────────────────────────
    cm.RestoreCSState();

    // Update SRV registration (in case output changed)
    SRVInjector::Get().RegisterSRV(kSRVSlot, m_outputSRV);

    m_frameIndex++;
}


// =============================================================================
//  Shutdown
// =============================================================================

void IndirectSpecularRenderer::Shutdown()
{
    if (!m_initialized) return;

    SRVInjector::Get().UnregisterSRV(kSRVSlot);

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("IndirectSpecularRenderer: shut down");
}


void IndirectSpecularRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_indirectSpecularCS);
    SafeRelease(m_outputTex);
    SafeRelease(m_outputSRV);
    SafeRelease(m_outputUAV);
    SafeRelease(m_constantsCB);
    SafeRelease(m_linearClampSampler);
}

} // namespace SB
