//=============================================================================
//  GrassLightingRenderer.cpp — Screen-space grass lighting correction
//
//  Identifies vegetation pixels via MaterialClassifier (t25), reads G-buffer
//  normals (t26) + depth, computes correct ambient + directional + multi-light
//  contribution, and composites the corrected color onto the backbuffer.
//
//  Replaces Community Shaders' Grass Lighting with a compute-based approach
//  that supports up to 2048 lights via ClusteredLighting (vs CS's ~16).
//=============================================================================

#include "GrassLightingRenderer.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"   // SceneMatrices
#include "ClusteredLighting.h"
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

static const char* kGrassLightingCS = R"HLSL(
//  GrassLightingCS — Corrects lighting on vegetation pixels
//
//  Inputs:
//    t0  = Backbuffer (SRV, read original color)
//    t1  = MaterialClassifier output (R8_UINT, material ID per pixel)
//    t2  = G-buffer normals (R10G10B10A2_UNORM or R16G16B16A16_FLOAT)
//    t3  = Depth buffer (R32_FLOAT or R24_UNORM_X8_TYPELESS)
//    t4  = Light buffer (StructuredBuffer<LightData>, from ClusteredLighting)
//    t5  = Cluster grid (StructuredBuffer<ClusterInfo>)
//    t6  = Light index list (Buffer<uint>)
//  Output:
//    u0  = Backbuffer (UAV, write corrected color)

cbuffer GrassLightingCB : register(b0)
{
    float3 sunDirection;      float  ambientBoost;
    float3 sunColor;          float  subsurfaceStrength;
    float3 ambientColor;      float  multiLightIntensity;
    float  windSway;
    float  gameTime;
    float  nearZ;
    float  farZ;
    uint   screenWidth;
    uint   screenHeight;
    uint   lightCount;
    uint   pad0;
    float4x4 invViewProj;
};

// Material IDs from MaterialClassifier
static const uint MAT_VEGETATION = 6;  // SceneMaterial::Vegetation
static const uint MAT_TERRAIN    = 5;  // SceneMaterial::Terrain

struct LightData
{
    float4 positionAndRadius;
    float4 colorAndIntensity;
    float4 directionAndAngle;
    uint   flags;
    uint3  pad;
};

struct ClusterInfo
{
    uint offset;
    uint count;
    uint2 pad;
};

Texture2D<float4>           tBackbuffer     : register(t0);
Texture2D<uint>             tMaterialID     : register(t1);
Texture2D<float4>           tNormals        : register(t2);
Texture2D<float>            tDepth          : register(t3);
StructuredBuffer<LightData> tLights         : register(t4);
StructuredBuffer<ClusterInfo> tClusters     : register(t5);
Buffer<uint>                tLightIndices   : register(t6);
Texture2D<float>            LinearDepth     : register(t31);

RWTexture2D<float4>         uBackbuffer     : register(u0);

// Subsurface scattering approximation for vegetation
float3 SubsurfaceScatter(float3 lightDir, float3 viewDir, float3 normal, float3 lightColor, float strength)
{
    // Wrap lighting for translucency
    float3 backLight = lightDir + normal * 0.5;
    float  backNdotL = saturate(dot(-normalize(backLight), viewDir));
    float  sss = pow(backNdotL, 3.0) * strength;
    return lightColor * sss;
}

// Get cluster index for a pixel
uint GetClusterIndex(uint2 pixel, float linearDepth)
{
    uint clusterX = pixel.x * 16 / screenWidth;
    uint clusterY = pixel.y * 16 / screenHeight;
    // Log-depth slicing
    float logDepth = log2(linearDepth / nearZ) / log2(farZ / nearZ);
    uint clusterZ = (uint)(saturate(logDepth) * 32);
    clusterX = min(clusterX, 15);
    clusterY = min(clusterY, 15);
    clusterZ = min(clusterZ, 31);
    return clusterZ * 16 * 16 + clusterY * 16 + clusterX;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    uint2 pixel = DTid.xy;

    // Check material ID — only process vegetation
    uint matID = tMaterialID[pixel];
    if (matID != MAT_VEGETATION)
        return;

    // Read original color
    float4 original = tBackbuffer[pixel];

    // Read normal (decode from [0,1] to [-1,1])
    float3 normal = tNormals[pixel].xyz * 2.0 - 1.0;
    normal = normalize(normal);

    // Read and linearize depth
    float rawDepth = tDepth[pixel];
    float linearDepth = LinearDepth[pixel];

    // Reconstruct world position
    float2 uv = (float2(pixel) + 0.5) / float2(screenWidth, screenHeight);
    float4 clipPos = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos4 = mul(invViewProj, clipPos);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    float3 viewDir = normalize(-worldPos);  // Approximate

    // ── Directional (sun) contribution ──────────────────────────────
    float NdotL = saturate(dot(normal, sunDirection));
    float3 directional = sunColor * NdotL;

    // Subsurface scattering for back-lit grass
    float3 sss = SubsurfaceScatter(sunDirection, viewDir, normal, sunColor, subsurfaceStrength);

    // ── Ambient correction ──────────────────────────────────────────
    // Vanilla grass gets flat ambient; we add hemisphere-weighted ambient
    float skyWeight = saturate(normal.z * 0.5 + 0.5);
    float3 correctedAmbient = ambientColor * (1.0 + ambientBoost * skyWeight);

    // ── Multi-light contribution from ClusteredLighting ─────────────
    float3 multiLight = float3(0, 0, 0);
    if (lightCount > 0)
    {
        uint clusterIdx = GetClusterIndex(pixel, linearDepth);
        ClusterInfo cluster = tClusters[clusterIdx];

        for (uint i = 0; i < cluster.count && i < 32; ++i)
        {
            uint lightIdx = tLightIndices[cluster.offset + i];
            if (lightIdx >= lightCount) break;

            LightData light = tLights[lightIdx];
            float3 lightPos = light.positionAndRadius.xyz;
            float  lightRadius = light.positionAndRadius.w;
            float3 lightColor = light.colorAndIntensity.xyz * light.colorAndIntensity.w;

            float3 toLight = lightPos - worldPos;
            float dist = length(toLight);
            if (dist >= lightRadius) continue;

            float3 lightDir = toLight / max(dist, 0.001);
            float attenuation = saturate(1.0 - dist / lightRadius);
            attenuation *= attenuation;  // Quadratic falloff

            float lNdotL = saturate(dot(normal, lightDir));
            multiLight += lightColor * lNdotL * attenuation;

            // Per-light subsurface for nearby lights
            multiLight += SubsurfaceScatter(lightDir, viewDir, normal, lightColor, subsurfaceStrength * 0.5) * attenuation;
        }
    }

    // ── Composite ───────────────────────────────────────────────────
    // Estimate the vanilla lighting to subtract, then add corrected
    // Vanilla grass ≈ flat ambient + weak directional
    float3 vanillaEstimate = ambientColor * 0.6 + sunColor * max(dot(normal, sunDirection), 0.1) * 0.3;

    float3 corrected = original.rgb;
    // Remove estimated vanilla contribution and add corrected
    corrected = corrected * (correctedAmbient + directional + sss + multiLight * multiLightIntensity)
                / max(vanillaEstimate, float3(0.01, 0.01, 0.01));

    // Soft clamp to prevent blowout
    corrected = corrected / (1.0 + corrected * 0.1);

    uBackbuffer[pixel] = float4(corrected, original.a);
}
)HLSL";


// ── Constant buffer layout ────────────────────────────────────────────────

struct alignas(16) GrassLightingCBData
{
    float sunDirection[3];    float ambientBoost;
    float sunColor[3];        float subsurfaceStrength;
    float ambientColor[3];    float multiLightIntensity;
    float windSway;
    float gameTime;
    float nearZ;
    float farZ;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t lightCount;
    uint32_t pad0;
    float invViewProj[16];
};


// ── Initialize ────────────────────────────────────────────────────────────

bool GrassLightingRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
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

    // Register as PreENB pipeline pass (same-frame for ENB shaders)
    auto& pipeline = RenderPipeline::Get();
    if (pipeline.IsInitialized()) {
        PassDef pass;
        pass.name     = "GrassLighting";
        pass.stage    = PipelineStage::PostGeometry;
        pass.priority = 18;  // After ContactShadows(16), Skylighting(17)
        pass.enabled  = m_enabled;
        pass.execute  = [this](PassContext& ctx) { ExecutePass(ctx); };
        m_pipelineHandle = pipeline.AddPass(pass);
    }

    m_initialized = true;
    SKSE::log::info("GrassLightingRenderer: initialized ({}x{})", m_screenW, m_screenH);
    return true;
}

void GrassLightingRenderer::Shutdown()
{
    ReleaseResources();
    m_initialized = false;
}

bool GrassLightingRenderer::CompileShaders()
{
    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(kGrassLightingCS, strlen(kGrassLightingCS),
        "GrassLightingCS", nullptr, nullptr, "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("GrassLightingCS compile error: {}",
                static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }

    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_grassLightCS);
    blob->Release();
    if (err) err->Release();
    return SUCCEEDED(hr);
}

bool GrassLightingRenderer::CreateResources()
{
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth      = sizeof(GrassLightingCBData);
    cbd.Usage           = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_device->CreateBuffer(&cbd, nullptr, &m_constantsCB));
}

void GrassLightingRenderer::ReleaseResources()
{
    if (m_grassLightCS) { m_grassLightCS->Release(); m_grassLightCS = nullptr; }
    if (m_constantsCB)  { m_constantsCB->Release();  m_constantsCB = nullptr; }
    if (m_backbufferUAV){ m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
}

// ── Per-frame execution ───────────────────────────────────────────────────

void GrassLightingRenderer::ExecutePass(PassContext& ctx)
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
    auto* normalsSRV  = D3D11Hook::GetGBufferNormalsSRV();

    if (!depthSRV || !materialSRV || !normalsSRV) return;

    // Save OM state before backbuffer UAV creation (D3D11 auto-unbinds RTV)
    auto& cm = ComputeManager::Get();
    cm.SaveOMState();

    // Create per-frame backbuffer UAV
    ID3D11Texture2D* backbufferTex = nullptr;
    if (ctx.swapChain) {
        ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbufferTex);
    }
    if (!backbufferTex) { cm.RestoreOMState(); return; }

    // Create UAV for the backbuffer
    if (m_backbufferUAV) { m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    HRESULT hr = m_device->CreateUnorderedAccessView(backbufferTex, &uavDesc, &m_backbufferUAV);
    backbufferTex->Release();
    if (FAILED(hr)) { cm.RestoreOMState(); return; }

    // Update constant buffer with current frame data
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    auto* cb = static_cast<GrassLightingCBData*>(mapped.pData);
    memset(cb, 0, sizeof(GrassLightingCBData));

    // Sun direction + color from SceneMatrices (populated per frame from trackers)
    auto& sm = SceneMatrices::Get();
    if (!sm.IsValid()) { ctx.context->Unmap(m_constantsCB, 0); return; }

    memcpy(cb->sunDirection, sm.SunDirection(), sizeof(float) * 3);
    memcpy(cb->sunColor,     sm.SunColor(),     sizeof(float) * 3);
    // Ambient: approximate from sun color * 0.3 (corrected by ambientBoost in shader)
    cb->ambientColor[0] = sm.SunColor()[0] * 0.3f;
    cb->ambientColor[1] = sm.SunColor()[1] * 0.3f;
    cb->ambientColor[2] = sm.SunColor()[2] * 0.3f;

    cb->ambientBoost       = m_ambientBoost;
    cb->subsurfaceStrength = m_subsurfaceStrength;
    cb->multiLightIntensity= m_multiLightIntensity;
    cb->windSway           = m_windSway;
    cb->gameTime           = static_cast<float>(sm.FrameIndex());
    cb->nearZ              = sm.NearClip();
    cb->farZ               = sm.FarClip();
    cb->screenWidth        = m_screenW;
    cb->screenHeight       = m_screenH;

    // Light count from ClusteredLighting (may not be initialized yet)
    auto& cl = ClusteredLighting::Get();
    cb->lightCount = cl.IsInitialized() ? cl.GetVisibleLightCount() : 0;

    memcpy(cb->invViewProj, sm.InvViewProjMatrix(), sizeof(float) * 16);

    ctx.context->Unmap(m_constantsCB, 0);

    // Save CS state before dispatch
    cm.SaveCSState();

    // Bind and dispatch
    ctx.context->CSSetShader(m_grassLightCS, nullptr, 0);
    ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);

    ID3D11ShaderResourceView* srvs[] = {
        nullptr,       // t0: backbuffer SRV — bound below
        materialSRV,   // t1: material ID
        normalsSRV,    // t2: normals
        depthSRV,      // t3: depth
        cl.GetLightBufferSRV(),  // t4: light data
        cl.GetClusterGridSRV(),  // t5: cluster grid
        cl.GetLightIndexSRV(),   // t6: light indices
    };

    // Create backbuffer SRV (same texture, different view)
    // NOTE: Can't have both SRV and UAV on same resource simultaneously.
    // Use the previous approach: read from t0 = copy, write to u0 = backbuffer.
    // For simplicity, the shader reads and writes the same UAV texture.
    // The compute shader handles read-before-write per pixel.

    ctx.context->CSSetShaderResources(1, 6, &srvs[1]);
    ctx.context->CSSetUnorderedAccessViews(0, 1, &m_backbufferUAV, nullptr);

    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    uint32_t groupsX = (m_screenW + 7) / 8;
    uint32_t groupsY = (m_screenH + 7) / 8;
    ctx.context->Dispatch(groupsX, groupsY, 1);

    // Unbind
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ID3D11ShaderResourceView*  nullSRVs[6] = {};
    ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ctx.context->CSSetShaderResources(1, 6, nullSRVs);
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
