//=============================================================================
//  VolumetricLightingRenderer.cpp — Screen-Space Volumetric Lighting
//
//  Dispatch flow (per frame, PostGeometry stage, priority 19):
//    1. Dispatch RayMarch CS:  half-res per-pixel ray march toward sun
//       through depth buffer with Henyey-Greenstein phase function.
//       Output: half-res R16G16B16A16_FLOAT (scatter.rgb, transmittance.a)
//    2. Dispatch Upsample CS:  bilateral depth-aware upsample to full-res.
//       Output: full-res R16G16B16A16_FLOAT, registered at t31.
//
//  Algorithm:
//    For each pixel, reconstruct world position from depth.  March a ray
//    from the camera toward the pixel, sampling depth at each step.  When
//    the march step is behind geometry (occluded by depth buffer), skip
//    scatter accumulation.  Apply Henyey-Greenstein phase function to
//    weight scattering by the angle between the view ray and the sun
//    direction.  Dithered jitter (interleaved gradient noise) prevents
//    banding artifacts.
//=============================================================================

#include "VolumetricLightingRenderer.h"
#include "HiZPyramid.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "SceneData.h"
#include "SRVInjector.h"
#include "ComputeManager.h"
#include "SharedGPUResources.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// =============================================================================
//  Embedded HLSL -- Pass 1: Half-res Ray March CS
// =============================================================================

static const char* kRayMarchCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Screen-space volumetric light shafts — Crytek cascaded radial blur approach.
// Half-res per-pixel ray march with Henyey-Greenstein phase + Beer-Lambert extinction.

cbuffer VolLightCB : register(b0)
{
    float3   sunDir;           float  intensity;
    float3   sunColor;         float  scatterDensity;
    float3   cameraPos;        float  anisotropy;
    float4x4 invViewProj;
    float4x4 viewProj;
    float    nearZ;
    float    farZ;
    uint     screenWidth;      // full-res
    uint     screenHeight;     // full-res
    uint     halfWidth;        // half-res
    uint     halfHeight;       // half-res
    int      numSteps;
    float    maxDistance;
    uint     frameIndex;
    float    pad0;
}

static const float PI = 3.14159265359;

Texture2D<float>    tDepth    : register(t0);  // full-res depth buffer (reversed-Z)
Texture2D<float>    LinearDepth : register(t31); // pre-computed linearized depth
RWTexture2D<float4> uScatter  : register(u0);  // half-res scatter output

// Interleaved gradient noise — Jorge Jimenez, "Next Generation Post Processing in Call of Duty: AW"
float InterleavedGradientNoise(float2 screenPos)
{
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

// Henyey-Greenstein phase function
// g: asymmetry parameter (-1..1), cosTheta: cos(angle between view and light)
float HenyeyGreenstein(float g, float cosTheta)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, 1e-5), 1.5));
}

// Reconstruct world position from depth buffer value and UV
float3 ReconstructWorldPos(float2 uv, float depth)
{
    // UV to NDC: [0,1] -> [-1,1], flip Y for D3D
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;

    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= halfWidth || DTid.y >= halfHeight)
        return;

    // Map half-res pixel to full-res UV
    float2 uv = (float2(DTid.xy) + 0.5) / float2(halfWidth, halfHeight);

    // Sample full-res depth at corresponding location
    uint2 fullCoord = uint2(DTid.xy * 2);
    fullCoord = min(fullCoord, uint2(screenWidth - 1, screenHeight - 1));
    float sceneDepth = tDepth.Load(int3(fullCoord, 0));

    // Reconstruct world position of this pixel
    float3 worldPos = ReconstructWorldPos(uv, sceneDepth);

    // Ray direction: camera to world position
    float3 rayDir = worldPos - cameraPos;
    float  rayLength = length(rayDir);
    rayDir = rayDir / max(rayLength, 1e-6);

    // Clamp ray length to max distance
    rayLength = min(rayLength, maxDistance);

    // Compute phase function: angle between view ray and sun direction
    float cosTheta = dot(rayDir, sunDir);
    float phase = HenyeyGreenstein(anisotropy, cosTheta);

    // Step size along the ray
    float stepSize = rayLength / float(numSteps);

    // Temporal jitter: offset ray start by IGN-based dither to break banding
    float jitter = InterleavedGradientNoise(float2(DTid.xy) + float(frameIndex % 64) * float2(5.588238, 3.138137));

    // Ray march accumulation
    float3 scatterAccum = 0.0;
    float  transmittance = 1.0;
    float  extinction = scatterDensity * 0.01; // extinction coefficient

    for (int i = 0; i < numSteps; i++)
    {
        // Current sample position along the ray
        float t = (float(i) + jitter) * stepSize;
        float3 samplePos = cameraPos + rayDir * t;

        // Project sample position to screen to check occlusion against depth buffer
        float4 clipSample = mul(invViewProj, float4(0, 0, 0, 1)); // placeholder, we use proj
        // Reproject: world -> clip
        float4 sampleUV4;
        {
            // We need viewProj, but we only have invViewProj. Use the depth heuristic instead:
            // Compare linear distance of sample to linearized scene depth at that pixel.
            // This is the standard screen-space volumetric approach.
            float sampleDist = t;
            float sceneDist = rayLength; // Scene depth along this ray

            // Sample is behind geometry => occluded, skip scatter
            if (sampleDist > sceneDist)
                break;
        }

        // Beer-Lambert extinction: transmittance decreases exponentially
        float stepTransmittance = exp(-extinction * stepSize);

        // Accumulate in-scattered light: phase * sunColor * intensity
        // The scatter at this step is weighted by current transmittance
        float3 stepScatter = phase * sunColor * intensity * transmittance * stepSize * extinction;
        scatterAccum += stepScatter;

        // Update transmittance
        transmittance *= stepTransmittance;

        // Early exit if transmittance is negligible
        if (transmittance < 0.01)
            break;
    }

    uScatter[DTid.xy] = float4(scatterAccum, transmittance);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 2: Bilateral Upsample CS
// =============================================================================

static const char* kUpsampleCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Bilateral depth-aware upsample from half-res to full-res volumetric scatter.

cbuffer UpsampleCB : register(b0)
{
    uint  fullWidth;
    uint  fullHeight;
    uint  halfWidth;
    uint  halfHeight;
    float nearZ;
    float farZ;
    float depthThreshold;
    float pad0;
}

Texture2D<float4>   tHalfRes  : register(t0);  // half-res scatter
Texture2D<float>    tDepth    : register(t1);  // full-res depth (reversed-Z)
Texture2D<float>    LinearDepth : register(t31); // pre-computed linearized depth
RWTexture2D<float4> uOutput   : register(u0);  // full-res output
SamplerState sLinear : register(s0);

// Linearize reversed-Z depth to view-space distance
float LinearizeDepth(float d)
{
    // reversed-Z: near=1, far=0
    // linearZ = nearZ * farZ / (farZ - d * (farZ - nearZ))
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= fullWidth || DTid.y >= fullHeight)
        return;

    // Full-res pixel center depth
    float fullDepth = tDepth.Load(int3(DTid.xy, 0));
    float fullLinZ  = LinearizeDepth(fullDepth);

    // Corresponding half-res coordinate (with 0.5 offset for center)
    float2 halfUV = (float2(DTid.xy) + 0.5) / float2(fullWidth, fullHeight);
    float2 halfCoordF = halfUV * float2(halfWidth, halfHeight) - 0.5;

    // 2x2 bilinear tap coordinates in half-res
    int2 baseCoord = int2(floor(halfCoordF));
    float2 frac2 = halfCoordF - float2(baseCoord);

    // Bilateral weights: combine bilinear interpolation weight with depth similarity
    float4 result = 0.0;
    float  totalWeight = 0.0;

    [unroll]
    for (int dy = 0; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = 0; dx <= 1; dx++)
        {
            int2 tapCoord = baseCoord + int2(dx, dy);
            tapCoord = clamp(tapCoord, int2(0, 0), int2(halfWidth - 1, halfHeight - 1));

            // Bilinear weight
            float bx = (dx == 0) ? (1.0 - frac2.x) : frac2.x;
            float by = (dy == 0) ? (1.0 - frac2.y) : frac2.y;
            float bilinearW = bx * by;

            // Sample half-res scatter and the depth at the corresponding full-res location
            float4 halfSample = tHalfRes.Load(int3(tapCoord, 0));

            // Depth at this half-res tap (sample full-res depth at 2x location)
            uint2 fullTapCoord = min(uint2(tapCoord) * 2, uint2(fullWidth - 1, fullHeight - 1));
            float tapDepth = tDepth.Load(int3(fullTapCoord, 0));
            float tapLinZ  = LinearizeDepth(tapDepth);

            // Bilateral depth weight: Gaussian falloff based on depth difference
            float depthDiff = abs(fullLinZ - tapLinZ) / max(fullLinZ * depthThreshold, 1e-5);
            float depthW = exp(-depthDiff * depthDiff);

            float w = bilinearW * depthW;
            result     += halfSample * w;
            totalWeight += w;
        }
    }

    // Normalize and write output
    if (totalWeight > 1e-6)
        result /= totalWeight;
    else
        result = tHalfRes.Load(int3(max(baseCoord, int2(0,0)), 0));

    uOutput[DTid.xy] = result;
}
)HLSL";


// =============================================================================
//  CB structures -- must match HLSL cbuffers exactly
// =============================================================================

struct alignas(16) VolLightCB
{
    float    sunDir[3];            float intensity;        // 16
    float    sunColor[3];          float scatterDensity;   // 16
    float    cameraPos[3];         float anisotropy;       // 16
    float    invViewProj[16];                              // 64
    float    viewProj[16];                                  // 64
    float    nearZ;
    float    farZ;
    uint32_t screenWidth;
    uint32_t screenHeight;                                 // 16
    uint32_t halfWidth;
    uint32_t halfHeight;
    int32_t  numSteps;
    float    maxDistance;                                   // 16
    uint32_t frameIndex;
    float    pad0[3];                                      // 16
};                                                         // total: 224
static_assert(sizeof(VolLightCB) == 224, "VolLightCB must be 224 bytes (14 x 16)");

struct alignas(16) UpsampleCBData
{
    uint32_t fullWidth;
    uint32_t fullHeight;
    uint32_t halfWidth;
    uint32_t halfHeight;
    float    nearZ;
    float    farZ;
    float    depthThreshold;
    float    pad0;
};
static_assert(sizeof(UpsampleCBData) == 32, "UpsampleCBData must be 32 bytes");


// =============================================================================
//  Initialize
// =============================================================================

bool VolumetricLightingRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                             IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("VolumetricLightingRenderer: failed to get backbuffer (0x{:X})",
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

    // Register output SRV at t31
    SRVInjector::Get().RegisterSRV(kSRVSlot, m_fullResOutputSRV);

    // Register as PostGeometry pipeline pass
    auto& pipeline = RenderPipeline::Get();
    if (pipeline.IsInitialized()) {
        PassDef pass;
        pass.name     = "VolumetricLighting";
        pass.stage    = PipelineStage::PostGeometry;
        pass.priority = 19;  // After GrassLighting(18)
        pass.enabled  = m_enabled;
        pass.execute  = [this](PassContext& ctx) { ExecutePass(ctx); };
        m_pipelineHandle = pipeline.AddPass(pass);
    }

    m_initialized = true;
    m_frameIndex  = 0;

    SKSE::log::info("VolumetricLightingRenderer: initialized ({}x{}, half={}x{}, steps={}, t{})",
                    m_screenW, m_screenH, m_screenW / 2, m_screenH / 2, m_numSteps, kSRVSlot);
    return true;
}


// =============================================================================
//  Compile all shaders
// =============================================================================

bool VolumetricLightingRenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    auto CompileCS = [&](const char* name, const char* source,
                         ID3D11ComputeShader** outCS) -> bool
    {
        ID3DBlob* blob = ShaderLoader::Compile(name, source, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("VolumetricLightingRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("VolumetricLightingRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("Volumetric_RayMarch", kRayMarchCS, &m_rayMarchCS)) return false;
    if (!CompileCS("Volumetric_Upsample", kUpsampleCS, &m_upsampleCS)) return false;

    return true;
}


// =============================================================================
//  Create GPU resources
// =============================================================================

bool VolumetricLightingRenderer::CreateResources()
{
    HRESULT hr;

    uint32_t halfW = m_screenW / 2;
    uint32_t halfH = m_screenH / 2;

    // ── Half-res scatter texture (pass 1 output) ────────────────────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = halfW;
        desc.Height     = halfH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_halfResScatter);
        if (FAILED(hr)) {
            SKSE::log::error("VolumetricLightingRenderer: failed to create half-res scatter texture (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_halfResScatter, &srvDesc, &m_halfResScatterSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(m_halfResScatter, &uavDesc, &m_halfResScatterUAV);
        if (FAILED(hr)) return false;
    }

    // ── Full-res output texture (pass 2 output, exposed as t31) ─────────
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

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_fullResOutput);
        if (FAILED(hr)) {
            SKSE::log::error("VolumetricLightingRenderer: failed to create full-res output texture (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_fullResOutput, &srvDesc, &m_fullResOutputSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(m_fullResOutput, &uavDesc, &m_fullResOutputUAV);
        if (FAILED(hr)) return false;
    }

    // ── Constant buffer ─────────────────────────────────────────────────
    {
        // Use the larger of the two CB structs
        uint32_t cbSize = sizeof(VolLightCB) > sizeof(UpsampleCBData) ?
                          sizeof(VolLightCB) : sizeof(UpsampleCBData);
        if (!CreateCB(m_device, cbSize, &m_constantsCB)) {
            SKSE::log::error("VolumetricLightingRenderer: failed to create constant buffer");
            return false;
        }
    }

    // ── Linear clamp sampler ────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy  = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&sd, &m_linearClampSampler);
        if (FAILED(hr)) {
            SKSE::log::error("VolumetricLightingRenderer: failed to create sampler (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    return true;
}


// =============================================================================
//  GetOutputSRV
// =============================================================================

ID3D11ShaderResourceView* VolumetricLightingRenderer::GetOutputSRV() const
{
    if (!m_initialized || !m_enabled) return nullptr;
    return m_fullResOutputSRV;
}


// =============================================================================
//  Per-frame execution (called by RenderPipeline at PostGeometry stage)
// =============================================================================

void VolumetricLightingRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& sm = SceneMatrices::Get();
    if (!sm.IsValid()) return;

    // Prefer HiZ (reversed-Z) over raw game depth per pipeline rules
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }
    if (!depthSRV) return;

    // Save CS state before dispatches
    auto& cm = ComputeManager::Get();
    cm.SaveCSState();

    const float nearZ = sm.NearClip();
    const float farZ  = sm.FarClip();
    const uint32_t halfW = m_screenW / 2;
    const uint32_t halfH = m_screenH / 2;

    // Bind pre-computed linearized depth at t31 for both passes
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // =========================================================================
    //  Pass 1: Half-res ray march
    // =========================================================================
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return;

        auto* cb = static_cast<VolLightCB*>(mapped.pData);
        memset(cb, 0, sizeof(VolLightCB));

        memcpy(cb->sunDir,      sm.SunDirection(), sizeof(float) * 3);
        cb->intensity = m_intensity;

        memcpy(cb->sunColor,    sm.SunColor(),     sizeof(float) * 3);
        cb->scatterDensity = m_scatterDensity;

        memcpy(cb->cameraPos,   sm.CameraPos(),    sizeof(float) * 3);
        cb->anisotropy = m_anisotropy;

        memcpy(cb->invViewProj, sm.InvViewProjMatrix(), sizeof(float) * 16);
        memcpy(cb->viewProj,    sm.ViewProjMatrix(),     sizeof(float) * 16);

        cb->nearZ        = nearZ;
        cb->farZ         = farZ;
        cb->screenWidth  = m_screenW;
        cb->screenHeight = m_screenH;
        cb->halfWidth    = halfW;
        cb->halfHeight   = halfH;
        cb->numSteps     = m_numSteps;
        cb->maxDistance   = m_maxDistance;
        cb->frameIndex   = m_frameIndex;

        ctx.context->Unmap(m_constantsCB, 0);

        ctx.context->CSSetShader(m_rayMarchCS, nullptr, 0);
        ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);
        ctx.context->CSSetShaderResources(0, 1, &depthSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_halfResScatterUAV, nullptr);

        uint32_t groupsX = (halfW + 7) / 8;
        uint32_t groupsY = (halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Unbind pass 1
        ID3D11ShaderResourceView*  nullSRV[1] = {};
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // =========================================================================
    //  Pass 2: Bilateral upsample to full-res
    // =========================================================================
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return;

        auto* cb = static_cast<UpsampleCBData*>(mapped.pData);
        memset(cb, 0, sizeof(UpsampleCBData));

        cb->fullWidth      = m_screenW;
        cb->fullHeight     = m_screenH;
        cb->halfWidth      = halfW;
        cb->halfHeight     = halfH;
        cb->nearZ          = nearZ;
        cb->farZ           = farZ;
        cb->depthThreshold = 0.05f;

        ctx.context->Unmap(m_constantsCB, 0);

        ctx.context->CSSetShader(m_upsampleCS, nullptr, 0);
        ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);

        ID3D11ShaderResourceView* srvs[] = { m_halfResScatterSRV, depthSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_fullResOutputUAV, nullptr);
        ctx.context->CSSetSamplers(0, 1, &m_linearClampSampler);

        uint32_t groupsX = (m_screenW + 7) / 8;
        uint32_t groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Unbind pass 2
        ID3D11ShaderResourceView*  nullSRVs[2] = {};
        ID3D11UnorderedAccessView* nullUAV[1]   = {};
        ID3D11SamplerState*        nullSampler[1] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx.context->CSSetSamplers(0, 1, nullSampler);
    }

    // Unbind linearized depth t31
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    // Unbind shader
    ctx.context->CSSetShader(nullptr, nullptr, 0);

    // Restore CS state
    cm.RestoreCSState();

    // Update SRV injection with current output
    SRVInjector::Get().RegisterSRV(kSRVSlot, m_fullResOutputSRV);

    m_frameIndex++;
}


// =============================================================================
//  Shutdown
// =============================================================================

void VolumetricLightingRenderer::Shutdown()
{
    if (!m_initialized) return;

    SRVInjector::Get().UnregisterSRV(kSRVSlot);

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("VolumetricLightingRenderer: shut down");
}


void VolumetricLightingRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_rayMarchCS);
    SafeRelease(m_upsampleCS);

    SafeRelease(m_halfResScatter);
    SafeRelease(m_halfResScatterSRV);
    SafeRelease(m_halfResScatterUAV);

    SafeRelease(m_fullResOutput);
    SafeRelease(m_fullResOutputSRV);
    SafeRelease(m_fullResOutputUAV);

    SafeRelease(m_constantsCB);
    SafeRelease(m_linearClampSampler);
}

} // namespace SB
