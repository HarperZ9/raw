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

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kWaterBlendingCS = R"HLSL(
//  WaterBlendingCS — Soft shoreline blending + caustic patterns
//
//  Reads water pixels from material classifier, applies:
//    1. Depth-based edge softening at water-terrain boundaries
//    2. Animated caustic pattern projection
//    3. Water color correction from game data

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
};

static const uint MAT_WATER = 255;  // Water has a special material ID

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<uint>     tMaterialID : register(t1);
Texture2D<float>    tDepth      : register(t2);
Texture2D<float>    tNoise      : register(t3);
Texture2D<float>    LinearDepth : register(t31);

RWTexture2D<float4> uBackbuffer : register(u0);

// Procedural caustic pattern using Voronoi-like cells
float CausticPattern(float2 worldXY, float time)
{
    float2 p = worldXY * causticScale;
    float2 p1 = p + float2(time * 0.03, time * 0.02);
    float2 p2 = p * 1.3 + float2(-time * 0.02, time * 0.04);

    // Two-layer sine caustics
    float c1 = sin(p1.x * 6.28) * sin(p1.y * 6.28);
    float c2 = sin(p2.x * 4.71 + 1.57) * sin(p2.y * 4.71 + 1.57);

    float caustic = saturate((c1 + c2) * 0.5 + 0.5);
    caustic = pow(caustic, 3.0);  // Sharpen
    return caustic;
}

// Edge detection: difference between water depth and terrain depth
float GetWaterEdgeFactor(uint2 pixel, float waterDepth)
{
    // Sample neighboring pixels for depth discontinuity
    float maxDepthDiff = 0;
    [unroll]
    for (int dy = -1; dy <= 1; dy++) {
        [unroll]
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            uint2 neighbor = uint2(
                clamp(int(pixel.x) + dx, 0, int(screenWidth) - 1),
                clamp(int(pixel.y) + dy, 0, int(screenHeight) - 1));
            float neighborDepth = LinearDepth[neighbor];
            float diff = abs(neighborDepth - waterDepth);
            maxDepthDiff = max(maxDepthDiff, diff);
        }
    }
    // Normalize by blend width
    return saturate(maxDepthDiff / max(edgeBlendWidth, 0.01));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    uint2 pixel = DTid.xy;
    uint matID = tMaterialID[pixel];

    // Water detection: material ID or depth-heuristic near water plane
    // MaterialClassifier maps water as a distinct ID
    // For BSWaterShader pixels, we use ID 255 (reserved)
    bool isWater = (matID == MAT_WATER);

    if (!isWater) return;

    float4 original = tBackbuffer[pixel];
    float rawDepth = tDepth[pixel];
    float linearDepth = LinearDepth[pixel];

    // Reconstruct world position
    float2 uv = (float2(pixel) + 0.5) / float2(screenWidth, screenHeight);
    float4 clipPos = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos4 = mul(invViewProj, clipPos);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    // ── Shoreline edge blending ─────────────────────────────────────
    float edgeFactor = GetWaterEdgeFactor(pixel, linearDepth);
    float edgeAlpha = saturate(1.0 - edgeFactor);

    // ── Caustics ────────────────────────────────────────────────────
    float caustic = CausticPattern(worldPos.xy, gameTime);
    float3 causticColor = sunColor * caustic * causticIntensity;

    // Attenuate caustics by sun angle (no caustics at night)
    float sunUp = saturate(sunDirection.z);
    causticColor *= sunUp;

    // ── Depth fog ───────────────────────────────────────────────────
    // Distance from water plane to terrain below
    float waterPlaneY = waterPlane.w;  // Water surface height
    float terrainDepth = max(waterPlaneY - worldPos.z, 0);
    float fogFactor = saturate(terrainDepth / 512.0) * depthFogStrength;

    // Blend toward water color with depth
    float3 fogColor = waterColor * 0.3;

    // ── Composite ───────────────────────────────────────────────────
    float3 result = original.rgb;

    // Add caustics under water surface
    result += causticColor * (1.0 - fogFactor);

    // Apply depth fog
    result = lerp(result, fogColor, fogFactor);

    // Soften edges at shoreline
    result = lerp(result, original.rgb, edgeFactor * 0.5);

    uBackbuffer[pixel] = float4(result, original.a);
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
    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(kWaterBlendingCS, strlen(kWaterBlendingCS),
        "WaterBlendingCS", nullptr, nullptr, "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("WaterBlendingCS compile error: {}",
                static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }

    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_waterBlendCS);
    blob->Release();
    if (err) err->Release();
    return SUCCEEDED(hr);
}

bool WaterBlendingRenderer::CreateResources()
{
    // Constant buffer
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth      = sizeof(WaterBlendCBData);
    cbd.Usage           = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&cbd, nullptr, &m_constantsCB)))
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
