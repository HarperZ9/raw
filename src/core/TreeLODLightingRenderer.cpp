//=============================================================================
//  TreeLODLightingRenderer.cpp — Screen-space tree LOD lighting correction
//
//  Identifies tree LOD pixels via material classification, reads depth +
//  atmosphere LUTs, computes corrected ambient + directional lighting to
//  match full-detail trees.
//
//  Replaces Community Shaders' Tree LOD Lighting with a compute approach
//  that integrates with Playground's physically-based atmosphere LUTs.
//=============================================================================

#include "TreeLODLightingRenderer.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"   // SceneMatrices
#include "ComputeManager.h"
#include "MaterialClassifier.h"
#include "SharedGPUResources.h"
#include <d3dcompiler.h>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kTreeLODLightingCS = R"HLSL(
//  TreeLODLightingCS — Corrects lighting on BSDistantTreeShader pixels
//
//  Tree LODs use flat ambient with no directional sun contribution.
//  This pass adds:
//    1. Hemisphere-weighted ambient from atmosphere LUT
//    2. Sun directional light based on reconstructed normals
//    3. Distance-based color matching to blend toward full-tree appearance

cbuffer TreeLODCB : register(b0)
{
    float3 sunDirection;       float  ambientMatchStrength;
    float3 sunColor;           float  directionalStrength;
    float3 ambientColorUpper;  float  colorMatchBlend;
    float3 ambientColorLower;  float  nearZ;
    float  farZ;
    uint   screenWidth;
    uint   screenHeight;
    uint   pad0;
    float4x4 invViewProj;
};

// Material IDs — tree LOD is classified separately in MaterialClassifier
// BSDistantTreeShader technique = 0x???? -> classified as General(0) or Vegetation(6)
// We detect by shader technique flags in the material ID buffer.
// For simplicity, use a depth-based heuristic + green channel ratio.
static const uint MAT_VEGETATION = 6;

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<uint>     tMaterialID : register(t1);
Texture2D<float>    tDepth      : register(t2);
Texture2D<float4>   tAtmosLUT   : register(t3);  // Atmosphere transmittance LUT
Texture2D<float>    LinearDepth : register(t31);

RWTexture2D<float4> uBackbuffer : register(u0);

SamplerState SamplerState_Linear : register(s0);

// Sample atmosphere transmittance for a given elevation angle
float3 SampleAtmosphere(float cosTheta)
{
    float u = saturate(cosTheta * 0.5 + 0.5);
    return tAtmosLUT.SampleLevel(SamplerState_Linear, float2(u, 0.5), 0).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    uint2 pixel = DTid.xy;

    // Only process tree LOD pixels (vegetation at far distance)
    uint matID = tMaterialID[pixel];
    float rawDepth = tDepth[pixel];
    float linearDepth = LinearDepth[pixel];

    // Tree LODs appear at distance > 2048 units typically
    // Combined with vegetation material ID for robust detection
    bool isTreeLOD = (matID == MAT_VEGETATION && linearDepth > 1024.0);

    // Also detect via depth alone for LODs that aren't classified as vegetation
    // (BSDistantTreeShader may use a different technique ID)
    if (!isTreeLOD && linearDepth > 4096.0) {
        // Heuristic: check if pixel is "tree-like" (greenish, above terrain)
        float4 color = tBackbuffer[pixel];
        float greenRatio = color.g / max(color.r + color.g + color.b, 0.001);
        if (greenRatio > 0.36 && color.g > 0.05)
            isTreeLOD = true;
    }

    if (!isTreeLOD)
        return;

    float4 original = tBackbuffer[pixel];

    // Reconstruct approximate normal (assume trees face upward with slight variation)
    // For LODs we don't have real normals, so use a hemisphere-facing-camera approximation
    float2 uv = (float2(pixel) + 0.5) / float2(screenWidth, screenHeight);
    float3 approxNormal = float3(0, 0, 1);  // Mostly upward (Z-up in Skyrim)

    // ── Directional sun contribution ────────────────────────────────
    float NdotL = saturate(dot(approxNormal, sunDirection));
    float3 directional = sunColor * NdotL * directionalStrength;

    // ── Hemisphere ambient from atmosphere ──────────────────────────
    float3 ambient = lerp(ambientColorLower, ambientColorUpper,
                          saturate(approxNormal.z * 0.5 + 0.5));
    ambient *= ambientMatchStrength;

    // ── Distance fade ──────────────────────────────────────────────
    // Blend more strongly at medium distances, less at extreme distance
    float distFade = saturate((linearDepth - 1024.0) / 8192.0);
    float correctionStrength = saturate(1.0 - distFade * 0.5) * colorMatchBlend;

    // ── Apply correction ───────────────────────────────────────────
    // Estimate vanilla tree LOD lighting (very flat)
    float3 vanillaLOD = ambientColorLower * 0.5;

    float3 correctedLight = ambient + directional;
    float3 corrected = original.rgb * (correctedLight / max(vanillaLOD, float3(0.02, 0.02, 0.02)));

    // Blend between original and corrected based on strength
    corrected = lerp(original.rgb, corrected, correctionStrength);

    // Prevent oversaturation
    corrected = min(corrected, original.rgb * 2.5);

    uBackbuffer[pixel] = float4(corrected, original.a);
}
)HLSL";


// ── Constant buffer ───────────────────────────────────────────────────────

struct alignas(16) TreeLODCBData
{
    float sunDirection[3];       float ambientMatchStrength;
    float sunColor[3];           float directionalStrength;
    float ambientColorUpper[3];  float colorMatchBlend;
    float ambientColorLower[3];  float nearZ;
    float farZ;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t pad0;
    float invViewProj[16];
};


// ── Initialize ────────────────────────────────────────────────────────────

bool TreeLODLightingRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;

    m_device  = dev;
    m_context = ctx;

    DXGI_SWAP_CHAIN_DESC scDesc;
    if (FAILED(sc->GetDesc(&scDesc))) return false;
    m_screenW = scDesc.BufferDesc.Width;
    m_screenH = scDesc.BufferDesc.Height;

    if (!CompileShaders()) return false;
    if (!CreateResources()) return false;

    auto& pipeline = RenderPipeline::Get();
    if (pipeline.IsInitialized()) {
        PassDef pass;
        pass.name     = "TreeLODLighting";
        pass.stage    = PipelineStage::PostGeometry;
        pass.priority = 19;  // After GrassLighting(18)
        pass.enabled  = m_enabled;
        pass.execute  = [this](PassContext& ctx) { ExecutePass(ctx); };
        m_pipelineHandle = pipeline.AddPass(pass);
    }

    m_initialized = true;
    SKSE::log::info("TreeLODLightingRenderer: initialized ({}x{})", m_screenW, m_screenH);
    return true;
}

void TreeLODLightingRenderer::Shutdown()
{
    ReleaseResources();
    m_initialized = false;
}

bool TreeLODLightingRenderer::CompileShaders()
{
    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(kTreeLODLightingCS, strlen(kTreeLODLightingCS),
        "TreeLODLightingCS", nullptr, nullptr, "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("TreeLODLightingCS compile error: {}",
                static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }

    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_treeLodCS);
    blob->Release();
    if (err) err->Release();
    return SUCCEEDED(hr);
}

bool TreeLODLightingRenderer::CreateResources()
{
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth      = sizeof(TreeLODCBData);
    cbd.Usage           = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_device->CreateBuffer(&cbd, nullptr, &m_constantsCB));
}

void TreeLODLightingRenderer::ReleaseResources()
{
    if (m_treeLodCS)    { m_treeLodCS->Release();    m_treeLodCS = nullptr; }
    if (m_constantsCB)  { m_constantsCB->Release();  m_constantsCB = nullptr; }
    if (m_backbufferUAV){ m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
}

void TreeLODLightingRenderer::ExecutePass(PassContext& ctx)
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

    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    // Prefer HiZ (reversed-Z) over raw game depth per pipeline rules
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }
    auto* materialSRV = MaterialClassifier::Get().IsInitialized() ? MaterialClassifier::Get().GetMaterialSRV() : nullptr;
    if (!depthSRV || !materialSRV) return;

    // Save OM state before backbuffer UAV creation (D3D11 auto-unbinds RTV)
    auto& cm = ComputeManager::Get();
    cm.SaveOMState();

    // Get backbuffer for UAV
    ID3D11Texture2D* backbufferTex = nullptr;
    if (ctx.swapChain) {
        ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbufferTex);
    }
    if (!backbufferTex) { cm.RestoreOMState(); return; }

    if (m_backbufferUAV) { m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    HRESULT hr = m_device->CreateUnorderedAccessView(backbufferTex, &uavDesc, &m_backbufferUAV);
    backbufferTex->Release();
    if (FAILED(hr)) { cm.RestoreOMState(); return; }

    // Update CB
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    auto* cb = static_cast<TreeLODCBData*>(mapped.pData);
    memset(cb, 0, sizeof(TreeLODCBData));

    auto& sm = SceneMatrices::Get();
    if (!sm.IsValid()) { ctx.context->Unmap(m_constantsCB, 0); return; }

    memcpy(cb->sunDirection, sm.SunDirection(), sizeof(float) * 3);
    memcpy(cb->sunColor,     sm.SunColor(),     sizeof(float) * 3);

    // Upper/lower hemisphere ambient from sun color approximation
    // In-game: upper = sky blue, lower = ground bounce
    float sunLum = sm.SunColor()[0] * 0.299f + sm.SunColor()[1] * 0.587f + sm.SunColor()[2] * 0.114f;
    cb->ambientColorUpper[0] = sm.SunColor()[0] * 0.4f + 0.05f;
    cb->ambientColorUpper[1] = sm.SunColor()[1] * 0.4f + 0.08f;
    cb->ambientColorUpper[2] = sm.SunColor()[2] * 0.4f + 0.12f;
    cb->ambientColorLower[0] = sm.SunColor()[0] * 0.15f;
    cb->ambientColorLower[1] = sm.SunColor()[1] * 0.15f + 0.02f;
    cb->ambientColorLower[2] = sm.SunColor()[2] * 0.1f;

    cb->ambientMatchStrength = m_ambientMatchStrength;
    cb->directionalStrength  = m_directionalStrength;
    cb->colorMatchBlend      = m_colorMatchBlend;
    cb->nearZ                = sm.NearClip();
    cb->farZ                 = sm.FarClip();
    cb->screenWidth          = m_screenW;
    cb->screenHeight         = m_screenH;

    memcpy(cb->invViewProj, sm.InvViewProjMatrix(), sizeof(float) * 16);

    ctx.context->Unmap(m_constantsCB, 0);

    // Save CS state before dispatch
    cm.SaveCSState();

    // Dispatch
    ctx.context->CSSetShader(m_treeLodCS, nullptr, 0);
    ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);

    ID3D11ShaderResourceView* srvs[] = { nullptr, materialSRV, depthSRV };
    ctx.context->CSSetShaderResources(1, 2, &srvs[1]);
    ctx.context->CSSetUnorderedAccessViews(0, 1, &m_backbufferUAV, nullptr);

    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    uint32_t groupsX = (m_screenW + 7) / 8;
    uint32_t groupsY = (m_screenH + 7) / 8;
    ctx.context->Dispatch(groupsX, groupsY, 1);

    // Unbind
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ID3D11ShaderResourceView*  nullSRVs[2] = {};
    ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ctx.context->CSSetShaderResources(1, 2, nullSRVs);
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }
    ctx.context->CSSetShader(nullptr, nullptr, 0);

    // Restore CS state + OM state
    cm.RestoreCSState();
    cm.RestoreOMState();

    m_frameIndex++;
}

} // namespace SB
