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
#include "ShaderLoader.h"
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
#include "GPUResource.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kGrassLightingCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Grass subsurface scattering approximation.
// Reads scene color + HiZ depth (reversed-Z) + material mask from
// MaterialClassifier. For pixels classified as vegetation, computes a
// fake translucency term based on the alignment between the view
// direction and the negated (light + normal*wrap) vector, then applies
// multi-light contribution from ClusteredLighting and blends the
// result additively onto the backbuffer UAV.
//
// Reference: Barré-Brisebois & Bouchard, "Approximating Translucency
// for a Fast, Cheap and Convincing Subsurface Scattering Look",
// GDC 2011.

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
}

static const uint MAT_VEGETATION = 6;  // SceneMaterial::Vegetation
static const uint MAT_TERRAIN    = 5;  // SceneMaterial::Terrain

Texture2D<float4>           tBackbuffer     : register(t0);
Texture2D<uint>             tMaterialID     : register(t1);
Texture2D<float4>           tNormals        : register(t2);
Texture2D<float>            tDepth          : register(t3);
StructuredBuffer<LightData> tLights         : register(t4);
StructuredBuffer<ClusterInfo> tClusters     : register(t5);
Buffer<uint>                tLightIndices   : register(t6);
Texture2D<float>            LinearDepth     : register(t31);
RWTexture2D<float4>         uBackbuffer     : register(u0);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Reconstruct world position from depth + pixel coord via inverse VP.
float3 ReconstructWorldPos(uint2 pixelCoord, float depth)
{
    float2 uv = (float2(pixelCoord) + 0.5) / float2(screenWidth, screenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// Linearize reversed-Z depth to view-space distance.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

// Barré-Brisebois/Bouchard translucency approximation.
// viewDir  : normalised direction from surface to camera
// lightDir : normalised direction TO the light source
// normal   : surface normal
// wrapFactor: normal distortion amount (0.5 typical for grass)
float ComputeTranslucency(float3 viewDir, float3 lightDir, float3 normal, float wrapFactor)
{
    float3 halfThick = -(lightDir + normal * wrapFactor);
    halfThick = normalize(halfThick);
    float vdotH = saturate(dot(viewDir, halfThick));
    return pow(vdotH, 4.0);
}

// Cluster index for a pixel (16x16 tiles, log-depth 32 slices).
uint3 GetClusterIndex(uint2 pixelCoord, float linearZ)
{
    uint tileX = pixelCoord.x / 16;
    uint tileY = pixelCoord.y / 16;
    uint tilesX = (screenWidth  + 15) / 16;
    uint tilesY = (screenHeight + 15) / 16;

    float logNear = log2(max(nearZ, 1.0));
    float logFar  = log2(farZ);
    float logZ    = log2(max(linearZ, 1.0));
    uint  slice   = (uint)clamp((logZ - logNear) / (logFar - logNear) * 32.0, 0.0, 31.0);

    return uint3(tileX, tileY, slice);
}

uint FlattenClusterIndex(uint3 ci)
{
    uint tilesX = (screenWidth  + 15) / 16;
    uint tilesY = (screenHeight + 15) / 16;
    return ci.z * tilesX * tilesY + ci.y * tilesX + ci.x;
}

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

    // Depth — HiZ is reversed-Z (near=1, far=0).
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.00001)
        return;

    // First-person geometry skip.
    float linearZ = LinearDepth.Load(int3(DTid.xy, 0));
    if (linearZ < 16.0)
        return;

    // Reconstruct world position and derive view direction.
    float3 worldPos = ReconstructWorldPos(DTid.xy, rawDepth);
    float3 cameraPos = mul(float4(0, 0, 0, 1), invViewProj).xyz /
                        mul(float4(0, 0, 0, 1), invViewProj).w;
    float3 viewDir = normalize(cameraPos - worldPos);

    // Decode G-buffer normal (octahedron or packed — assume xyz in [0,1] mapped to [-1,1]).
    float4 rawNormal = tNormals.Load(int3(DTid.xy, 0));
    float3 normal = normalize(rawNormal.xyz * 2.0 - 1.0);

    // Read existing scene color.
    float4 sceneColor = uBackbuffer[DTid.xy];

    // ── Sun translucency ────────────────────────────────────────────────
    float sunTranslucency = ComputeTranslucency(viewDir, sunDirection, normal, 0.5);
    float3 sunContrib = sunColor * sunTranslucency * subsurfaceStrength;

    // ── Ambient fill ────────────────────────────────────────────────────
    // Hemisphere-based ambient: lerp upper/lower by normal.z.
    float hemiBlend = normal.z * 0.5 + 0.5;
    float3 ambientContrib = ambientColor * ambientBoost * hemiBlend;

    // ── Multi-light translucency via ClusteredLighting ──────────────────
    float3 multiLightContrib = float3(0, 0, 0);
    if (lightCount > 0)
    {
        uint3 ci = GetClusterIndex(DTid.xy, linearZ);
        uint  flatIdx = FlattenClusterIndex(ci);
        ClusterInfo cluster = tClusters[flatIdx];

        for (uint i = 0; i < cluster.count && i < 32; ++i)
        {
            uint lightIdx = tLightIndices[cluster.offset + i];
            LightData light = tLights[lightIdx];

            float3 lightPos    = light.positionAndRadius.xyz;
            float  lightRadius = light.positionAndRadius.w;
            float3 lightColor  = light.colorAndIntensity.xyz;
            float  lightPower  = light.colorAndIntensity.w;

            float3 toLight = lightPos - worldPos;
            float  dist    = length(toLight);
            if (dist > lightRadius) continue;

            float3 lightDir = toLight / max(dist, 0.001);
            float  atten    = saturate(1.0 - dist / lightRadius);
            atten *= atten; // Quadratic falloff.

            float trans = ComputeTranslucency(viewDir, lightDir, normal, 0.5);
            multiLightContrib += lightColor * lightPower * trans * atten;
        }
        multiLightContrib *= multiLightIntensity;
    }

    // ── Composite ───────────────────────────────────────────────────────
    float3 totalAdd = sunContrib + ambientContrib + multiLightContrib;

    // Modulate by existing scene luminance to avoid over-brightening dark grass.
    float sceneLuma = dot(sceneColor.rgb, float3(0.299, 0.587, 0.114));
    totalAdd *= saturate(sceneLuma * 2.0);

    uBackbuffer[DTid.xy] = float4(sceneColor.rgb + totalAdd, sceneColor.a);
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

    // Register as PostGeometry pipeline pass
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
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = ShaderLoader::Compile("GrassLighting_Main", kGrassLightingCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("GrassLightingCS compile failed");
        return false;
    }

    HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_grassLightCS);
    blob->Release();
    return SUCCEEDED(hr);
}

bool GrassLightingRenderer::CreateResources()
{
    return CreateCB(m_device, sizeof(GrassLightingCBData), &m_constantsCB);
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
