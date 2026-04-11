//=============================================================================
//  TreeLODLightingRenderer.cpp — Screen-space tree LOD lighting correction
//
//  Identifies tree LOD pixels via material classification, reads depth +
//  atmosphere LUTs, computes corrected ambient + directional lighting to
//  match full-detail trees.
//
//  Replaces Community Shaders' Tree LOD Lighting with a compute approach
//  that integrates with RAW's physically-based atmosphere LUTs.
//=============================================================================

#include "TreeLODLightingRenderer.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"   // SceneMatrices
#include "ComputeManager.h"
#include "MaterialClassifier.h"
#include "SharedGPUResources.h"
#include <d3dcompiler.h>
#include <cstring>
#include "GPUResource.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kTreeLODLightingCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Tree LOD lighting correction.
// Distant tree LODs receive flat lighting from the engine because their
// geometry is simplified billboard quads.  This pass identifies vegetation
// pixels at LOD distances (deep linear depth), reconstructs approximate
// normals from depth gradients, and applies directional + ambient fill to
// reduce the flat "cardboard" appearance.
//
// Writes corrected color additively onto the backbuffer UAV.

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
}

static const uint MAT_VEGETATION = 6;

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<uint>     tMaterialID : register(t1);
Texture2D<float>    tDepth      : register(t2);
Texture2D<float4>   tAtmosLUT   : register(t3);  // Atmosphere transmittance LUT
Texture2D<float>    LinearDepth : register(t31);
RWTexture2D<float4> uBackbuffer : register(u0);
SamplerState SamplerState_Linear : register(s0);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Linearize reversed-Z depth to view-space distance.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

// Reconstruct world position from pixel coord + reversed-Z depth.
float3 ReconstructWorldPos(uint2 pixelCoord, float depth)
{
    float2 uv = (float2(pixelCoord) + 0.5) / float2(screenWidth, screenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// Reconstruct a coarse normal from depth gradients (screen-space).
float3 ReconstructNormal(uint2 pixelCoord)
{
    float dC = LinearDepth.Load(int3(pixelCoord, 0));
    float dR = LinearDepth.Load(int3(pixelCoord + uint2(1, 0), 0));
    float dD = LinearDepth.Load(int3(pixelCoord + uint2(0, 1), 0));

    float3 posC = ReconstructWorldPos(pixelCoord, tDepth.Load(int3(pixelCoord, 0)));
    float3 posR = ReconstructWorldPos(pixelCoord + uint2(1, 0),
                                       tDepth.Load(int3(pixelCoord + uint2(1, 0), 0)));
    float3 posD = ReconstructWorldPos(pixelCoord + uint2(0, 1),
                                       tDepth.Load(int3(pixelCoord + uint2(0, 1), 0)));

    float3 ddx = posR - posC;
    float3 ddy = posD - posC;
    return normalize(cross(ddy, ddx));
}

// LOD distance threshold — only apply correction beyond this depth (game units).
// Typical LOD transition for trees is ~2000-4000 units.
static const float LOD_DEPTH_START = 1500.0;
static const float LOD_DEPTH_FULL  = 4000.0;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    // Material check — only process vegetation pixels.
    uint matID = tMaterialID.Load(int3(DTid.xy, 0));
    if (matID != MAT_VEGETATION)
        return;

    // Depth — HiZ reversed-Z (near=1, far=0).
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.00001)
        return;

    // First-person geometry skip.
    float linearZ = LinearDepth.Load(int3(DTid.xy, 0));
    if (linearZ < 16.0)
        return;

    // LOD distance fade — only apply correction to distant vegetation.
    float lodFade = saturate((linearZ - LOD_DEPTH_START) / (LOD_DEPTH_FULL - LOD_DEPTH_START));
    if (lodFade < 0.01)
        return;

    // Read existing scene color.
    float4 sceneColor = uBackbuffer[DTid.xy];

    // Reconstruct coarse normal from depth derivatives.
    float3 normal = ReconstructNormal(DTid.xy);

    // ── Directional lighting correction ─────────────────────────────────
    // sunDirection is the direction TO the sun.
    float NdotL = saturate(dot(normal, sunDirection));

    // Shadow-side ambient fill: stronger where the surface faces away from sun.
    float shadowSide = 1.0 - NdotL;
    float3 ambientFill = ambientMatchStrength * shadowSide;

    // Hemisphere ambient — blend upper/lower by vertical component of normal.
    float hemiBlend = normal.z * 0.5 + 0.5;
    float3 hemiAmbient = lerp(ambientColorLower, ambientColorUpper, hemiBlend);

    // ── Sun directional boost ───────────────────────────────────────────
    // LOD trees are often too dark on the lit side. Boost direct lighting.
    float3 directBoost = sunColor * NdotL * directionalStrength;

    // ── Atmosphere transmittance (optional) ─────────────────────────────
    // Sample atmosphere LUT based on view-space depth for distance fade.
    float2 atmosUV = float2(saturate(linearZ / farZ), 0.5);
    float3 atmosTrans = tAtmosLUT.SampleLevel(SamplerState_Linear, atmosUV, 0).rgb;
    // If LUT is unavailable (all zeros), default to white.
    if (dot(atmosTrans, float3(1, 1, 1)) < 0.01)
        atmosTrans = float3(1, 1, 1);

    // ── Compose correction ──────────────────────────────────────────────
    // Color match: blend original luminance toward expected tree color.
    float sceneLuma = dot(sceneColor.rgb, float3(0.299, 0.587, 0.114));

    float3 correction = (hemiAmbient * ambientFill + directBoost) * atmosTrans;
    correction *= lodFade;  // Smooth fade at LOD transition boundary.

    // Modulate by scene luminance to prevent over-brightening very dark pixels.
    correction *= saturate(sceneLuma * 3.0);

    // Color match blend: lerp scene toward a more natural tree tone to fix
    // the washed-out billboard appearance.
    float3 targetGreen = float3(0.08, 0.12, 0.05);
    float3 colorMatched = lerp(sceneColor.rgb, sceneColor.rgb * (targetGreen / max(sceneLuma, 0.01)),
                                colorMatchBlend * lodFade * 0.1);

    float3 finalColor = colorMatched + correction;
    uBackbuffer[DTid.xy] = float4(finalColor, sceneColor.a);
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
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = ShaderLoader::Compile("TreeLODLighting_Main", kTreeLODLightingCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("TreeLODLightingCS compile failed");
        return false;
    }

    HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_treeLodCS);
    blob->Release();
    return SUCCEEDED(hr);
}

bool TreeLODLightingRenderer::CreateResources()
{
    return CreateCB(m_device, sizeof(TreeLODCBData), &m_constantsCB);
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
