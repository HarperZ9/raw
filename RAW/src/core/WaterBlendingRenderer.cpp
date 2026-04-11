//=============================================================================
//  WaterBlendingRenderer.cpp — Water surface enhancement
//
//  Identifies water pixels via MaterialClassifier, reads depth buffer for
//  water-terrain edge detection, applies soft depth blending + screen-space
//  caustic patterns + corrected water color from SceneObserver data.
//
//  Replaces Community Shaders' Water Blending with a compute approach that
//  adds caustics, soft shoreline blending, and depth-dependent fog.
//=============================================================================

#include "WaterBlendingRenderer.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"   // SceneMatrices
#include "ComputeManager.h"
#include "MaterialClassifier.h"
#include "SharedGPUResources.h"
#include <d3dcompiler.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "GPUResource.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kWaterBlendingCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Water edge blending.
// Softens hard water-terrain edges by comparing water depth with
// neighbouring non-water depth. At boundaries where the depth
// difference is small, alpha-blends the water pixel with the
// underlying terrain color to create a smooth shoreline transition.
// Also applies a simple depth-based fog tint for underwater depth.

cbuffer WaterBlendCB : register(b0)
{
    float  edgeBlendWidth;
    float  causticIntensity;
    float  causticScale;
    float  depthFogStrength;
    float3 waterColor;      float  waterAlpha;
    float3 sunDirection;    float  gameTime;
    float3 sunColor;        float  nearZ;
    float  farZ;
    uint   screenWidth;
    uint   screenHeight;
    uint   pad0;
    float4 waterPlane;      // nx, ny, nz, d
    float4x4 invViewProj;
}

static const uint MAT_WATER = 255;  // Water has a special material ID

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<uint>     tMaterialID : register(t1);
Texture2D<float>    tDepth      : register(t2);
Texture2D<float>    tNoise      : register(t3);
Texture2D<float>    LinearDepth : register(t31);
RWTexture2D<float4> uBackbuffer : register(u0);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Linearize reversed-Z depth to view-space distance.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

// Simple procedural caustic pattern using two overlapping noise octaves.
float CausticPattern(float2 worldXY, float time)
{
    float2 uv1 = worldXY * causticScale + float2(time * 0.03, time * 0.02);
    float2 uv2 = worldXY * causticScale * 1.3 + float2(-time * 0.02, time * 0.04);

    // Wrap to [0,1] for noise texture sampling.
    uv1 = frac(uv1);
    uv2 = frac(uv2);

    // tNoise is 128x128 R8_UNORM procedural noise.
    float n1 = tNoise.Load(int3(uint2(uv1 * 128.0) % 128, 0));
    float n2 = tNoise.Load(int3(uint2(uv2 * 128.0) % 128, 0));

    // Combine two octaves with sharpening.
    float pattern = saturate((n1 + n2) - 0.6);
    return pattern * pattern * causticIntensity;
}

// Search radius for finding nearest non-water depth (in pixels).
static const int EDGE_SEARCH_RADIUS = 3;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    // Depth — HiZ reversed-Z.
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.00001)
        return;

    // First-person geometry skip.
    float linearZ = LinearDepth.Load(int3(DTid.xy, 0));
    if (linearZ < 16.0)
        return;

    // Material check — we process pixels that ARE water.
    uint matID = tMaterialID.Load(int3(DTid.xy, 0));
    bool isWater = (matID == MAT_WATER);

    if (!isWater)
    {
        // For non-water pixels adjacent to water, check if we should receive
        // a caustic projection.  Find nearest water pixel in the search radius.
        bool nearWater = false;
        for (int dy = -EDGE_SEARCH_RADIUS; dy <= EDGE_SEARCH_RADIUS && !nearWater; ++dy)
        {
            for (int dx = -EDGE_SEARCH_RADIUS; dx <= EDGE_SEARCH_RADIUS && !nearWater; ++dx)
            {
                int2 sampleCoord = int2(DTid.xy) + int2(dx, dy);
                if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
                    (uint)sampleCoord.x >= screenWidth || (uint)sampleCoord.y >= screenHeight)
                    continue;
                uint neighborMat = tMaterialID.Load(int3(sampleCoord, 0));
                if (neighborMat == MAT_WATER)
                    nearWater = true;
            }
        }

        if (!nearWater)
            return;

        // Apply subtle caustic pattern to terrain near water edges.
        float2 uv = (float2(DTid.xy) + 0.5) / float2(screenWidth, screenHeight);
        float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        float4 clipPos = float4(ndc, rawDepth, 1.0);
        float4 worldPos = mul(clipPos, invViewProj);
        worldPos.xyz /= worldPos.w;

        float caustic = CausticPattern(worldPos.xy, gameTime);
        float4 sceneColor = uBackbuffer[DTid.xy];
        float3 causticColor = sunColor * caustic * 0.3;
        uBackbuffer[DTid.xy] = float4(sceneColor.rgb + causticColor, sceneColor.a);
        return;
    }

    // ── Water pixel processing ──────────────────────────────────────────

    float4 sceneColor = uBackbuffer[DTid.xy];

    // Find the nearest non-water depth for edge detection.
    float nearestTerrainDepth = 0.0;  // reversed-Z: 0 = far
    bool foundTerrain = false;

    for (int dy = -EDGE_SEARCH_RADIUS; dy <= EDGE_SEARCH_RADIUS; ++dy)
    {
        for (int dx = -EDGE_SEARCH_RADIUS; dx <= EDGE_SEARCH_RADIUS; ++dx)
        {
            if (dx == 0 && dy == 0) continue;
            int2 sampleCoord = int2(DTid.xy) + int2(dx, dy);
            if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
                (uint)sampleCoord.x >= screenWidth || (uint)sampleCoord.y >= screenHeight)
                continue;

            uint neighborMat = tMaterialID.Load(int3(sampleCoord, 0));
            if (neighborMat != MAT_WATER)
            {
                float neighborDepth = tDepth.Load(int3(sampleCoord, 0));
                if (neighborDepth > nearestTerrainDepth)  // reversed-Z: larger = closer
                {
                    nearestTerrainDepth = neighborDepth;
                    foundTerrain = true;
                }
            }
        }
    }

    // ── Edge blend ──────────────────────────────────────────────────────
    float edgeAlpha = waterAlpha;
    if (foundTerrain)
    {
        float waterLinear   = LinearizeDepth(rawDepth);
        float terrainLinear = LinearizeDepth(nearestTerrainDepth);
        float depthDiff     = abs(waterLinear - terrainLinear);

        // Smooth blend at edges: alpha goes from 0 (fully transparent) to
        // waterAlpha over the edgeBlendWidth distance.
        float edgeFade = saturate(depthDiff / max(edgeBlendWidth, 0.01));
        edgeAlpha = waterAlpha * edgeFade;
    }

    // ── Depth fog ───────────────────────────────────────────────────────
    // Tint water toward waterColor based on depth below the water plane.
    float2 uv = (float2(DTid.xy) + 0.5) / float2(screenWidth, screenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, rawDepth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    worldPos.xyz /= worldPos.w;

    float waterDepthBelow = max(waterPlane.w - worldPos.z, 0.0);
    float fogFactor = 1.0 - exp(-waterDepthBelow * depthFogStrength * 0.01);
    fogFactor = saturate(fogFactor);

    // ── Caustic on water surface ────────────────────────────────────────
    float caustic = CausticPattern(worldPos.xy, gameTime);

    // ── Composite ───────────────────────────────────────────────────────
    float3 waterTint = lerp(sceneColor.rgb, waterColor, fogFactor);
    waterTint += sunColor * caustic;

    float3 blended = lerp(sceneColor.rgb, waterTint, edgeAlpha);
    uBackbuffer[DTid.xy] = float4(blended, sceneColor.a);
}
)HLSL";


// ── Constant buffer ───────────────────────────────────────────────────────

struct alignas(16) WaterBlendCBData
{
    float  edgeBlendWidth;
    float  causticIntensity;
    float  causticScale;
    float  depthFogStrength;
    float  waterColor[3];     float waterAlpha;
    float  sunDirection[3];   float gameTime;
    float  sunColor[3];       float nearZ;
    float  farZ;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t pad0;
    float  waterPlane[4];
    float  invViewProj[16];
};


// ── Initialize ────────────────────────────────────────────────────────────

bool WaterBlendingRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
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
        pass.name     = "WaterBlending";
        pass.stage    = PipelineStage::PostGeometry;
        pass.priority = 24;  // After TreeLODLighting(19)
        pass.enabled  = m_enabled;
        pass.execute  = [this](PassContext& ctx) { ExecutePass(ctx); };
        m_pipelineHandle = pipeline.AddPass(pass);
    }

    m_initialized = true;
    SKSE::log::info("WaterBlendingRenderer: initialized ({}x{})", m_screenW, m_screenH);
    return true;
}

void WaterBlendingRenderer::Shutdown()
{
    ReleaseResources();
    m_initialized = false;
}

bool WaterBlendingRenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = ShaderLoader::Compile("WaterBlending_Main", kWaterBlendingCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("WaterBlendingCS compile failed");
        return false;
    }

    HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_waterBlendCS);
    blob->Release();
    return SUCCEEDED(hr);
}

bool WaterBlendingRenderer::CreateResources()
{
    // Constant buffer
    if (!CreateCB(m_device, sizeof(WaterBlendCBData), &m_constantsCB))
        return false;

    // Generate procedural noise texture (128x128 R8_UNORM)
    const uint32_t noiseSize = 128;
    std::vector<uint8_t> noiseData(noiseSize * noiseSize);
    uint32_t seed = 0x12345678;
    for (auto& v : noiseData) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        v = static_cast<uint8_t>(seed & 0xFF);
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width      = noiseSize;
    texDesc.Height     = noiseSize;
    texDesc.MipLevels  = 1;
    texDesc.ArraySize  = 1;
    texDesc.Format     = DXGI_FORMAT_R8_UNORM;
    texDesc.SampleDesc = { 1, 0 };
    texDesc.Usage      = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem     = noiseData.data();
    initData.SysMemPitch = noiseSize;

    if (FAILED(m_device->CreateTexture2D(&texDesc, &initData, &m_noiseTex)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    if (FAILED(m_device->CreateShaderResourceView(m_noiseTex, &srvDesc, &m_noiseSRV)))
        return false;

    return true;
}

void WaterBlendingRenderer::ReleaseResources()
{
    if (m_waterBlendCS) { m_waterBlendCS->Release(); m_waterBlendCS = nullptr; }
    if (m_constantsCB)  { m_constantsCB->Release();  m_constantsCB = nullptr; }
    if (m_noiseSRV)     { m_noiseSRV->Release();     m_noiseSRV = nullptr; }
    if (m_noiseTex)     { m_noiseTex->Release();      m_noiseTex = nullptr; }
    if (m_backbufferUAV){ m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
}

void WaterBlendingRenderer::ExecutePass(PassContext& ctx)
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

    auto* cb = static_cast<WaterBlendCBData*>(mapped.pData);
    memset(cb, 0, sizeof(WaterBlendCBData));

    auto& sm = SceneMatrices::Get();
    if (!sm.IsValid()) { ctx.context->Unmap(m_constantsCB, 0); return; }

    cb->edgeBlendWidth   = m_edgeBlendWidth;
    cb->causticIntensity = m_causticIntensity;
    cb->causticScale     = m_causticScale;
    cb->depthFogStrength = m_depthFogStrength;

    // Default water color (bluish-green) — can be refined from SceneObserver hooks later
    cb->waterColor[0] = 0.15f;
    cb->waterColor[1] = 0.35f;
    cb->waterColor[2] = 0.40f;
    cb->waterAlpha     = 0.7f;

    memcpy(cb->sunDirection, sm.SunDirection(), sizeof(float) * 3);
    memcpy(cb->sunColor,     sm.SunColor(),     sizeof(float) * 3);

    cb->gameTime     = static_cast<float>(sm.FrameIndex());
    cb->nearZ        = sm.NearClip();
    cb->farZ         = sm.FarClip();
    cb->screenWidth  = m_screenW;
    cb->screenHeight = m_screenH;

    // Water plane: default XY plane at camera Z (refined when SceneObserver water hook fires)
    cb->waterPlane[0] = 0.0f;
    cb->waterPlane[1] = 0.0f;
    cb->waterPlane[2] = 1.0f;
    cb->waterPlane[3] = sm.CameraPosZ();

    memcpy(cb->invViewProj, sm.InvViewProjMatrix(), sizeof(float) * 16);

    ctx.context->Unmap(m_constantsCB, 0);

    // Save CS state before dispatch
    cm.SaveCSState();

    // Dispatch
    ctx.context->CSSetShader(m_waterBlendCS, nullptr, 0);
    ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);

    ID3D11ShaderResourceView* srvs[] = { nullptr, materialSRV, depthSRV, m_noiseSRV };
    ctx.context->CSSetShaderResources(1, 3, &srvs[1]);
    ctx.context->CSSetUnorderedAccessViews(0, 1, &m_backbufferUAV, nullptr);

    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    uint32_t groupsX = (m_screenW + 7) / 8;
    uint32_t groupsY = (m_screenH + 7) / 8;
    ctx.context->Dispatch(groupsX, groupsY, 1);

    // Unbind
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ID3D11ShaderResourceView*  nullSRVs[3] = {};
    ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ctx.context->CSSetShaderResources(1, 3, nullSRVs);
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
