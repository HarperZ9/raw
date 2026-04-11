//=============================================================================
//  DynamicCubemapRenderer.cpp — Real-time environment cubemap generation
//
//  Captures the scene into a 6-face cubemap texture, one face per frame
//  (6-frame rotation cycle).  Each face is captured from the backbuffer
//  by projecting the appropriate viewport direction, then downsampled
//  to the cubemap face.  Includes mipmap generation for roughness-based
//  reflection lookups.
//
//  Replaces Community Shaders' Dynamic Cubemaps feature with a compute-based
//  approach that produces temporally-stable, physically-correct reflections.
//=============================================================================

#include "DynamicCubemapRenderer.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "SceneData.h"   // SceneMatrices
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

static const char* kCubemapCaptureCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Capture current scene into one face of a cubemap via reprojection from the backbuffer.

cbuffer CubemapCaptureCB : register(b0)
{
    float4x4 faceViewMatrix;   // View matrix for this cube face
    float4x4 faceProjection;   // 90-degree FOV projection
    float4x4 cameraViewProj;   // Current camera VP (for reprojection)
    float4x4 cameraInvViewProj;
    uint   faceIndex;
    uint   faceResolution;
    float  blendFactor;
    float  gameTime;
    uint   screenWidth;
    uint   screenHeight;
    uint2  pad;
}

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<float>    tDepth      : register(t1);
SamplerState        sLinear     : register(s0);
RWTexture2DArray<float4> uCubemap : register(u0);

static const float PI = 3.14159265359;

// Get cubemap face direction vectors
// Returns (right, up, forward) for the given face
void GetFaceVectors(uint face, out float3 forward, out float3 right, out float3 up)
{
    // Standard cubemap face directions (LH)
    switch (face)
    {
        case 0: forward = float3( 1, 0, 0); right = float3(0, 0, 1); up = float3(0, 1, 0); break; // +X
        case 1: forward = float3(-1, 0, 0); right = float3(0, 0,-1); up = float3(0, 1, 0); break; // -X
        case 2: forward = float3( 0, 1, 0); right = float3(1, 0, 0); up = float3(0, 0,-1); break; // +Y
        case 3: forward = float3( 0,-1, 0); right = float3(1, 0, 0); up = float3(0, 0, 1); break; // -Y
        case 4: forward = float3( 0, 0, 1); right = float3(1, 0, 0); up = float3(0, 1, 0); break; // +Z
        default:forward = float3( 0, 0,-1); right = float3(-1,0, 0); up = float3(0, 1, 0); break; // -Z
    }
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= faceResolution || DTid.y >= faceResolution)
        return;

    // Map cubemap texel to a direction vector
    float2 uv = (float2(DTid.xy) + 0.5) / float(faceResolution);
    // UV to [-1, 1]
    float2 ndc = uv * 2.0 - 1.0;

    float3 forward, right, up;
    GetFaceVectors(faceIndex, forward, right, up);

    // Direction from the cube center through this texel (90-degree FOV)
    float3 dir = normalize(forward + ndc.x * right - ndc.y * up);

    // Project this direction into current camera's screen space
    // Treat as a far-away point: worldPos = dir * large_distance
    float4 clipPos = mul(cameraViewProj, float4(dir * 100000.0, 1.0));

    // Check if visible in current camera frustum
    if (clipPos.w <= 0.0)
    {
        // Behind camera — blend toward existing content (don't overwrite)
        return;
    }

    float2 screenNDC = clipPos.xy / clipPos.w;
    float2 screenUV = float2(screenNDC.x * 0.5 + 0.5, -screenNDC.y * 0.5 + 0.5);

    // Bounds check: only sample if within the current viewport
    if (screenUV.x < 0.0 || screenUV.x > 1.0 || screenUV.y < 0.0 || screenUV.y > 1.0)
        return;

    // Sample backbuffer at the projected location
    float4 sceneColor = tBackbuffer.SampleLevel(sLinear, screenUV, 0);

    // Temporal blend: smoothly update cubemap face
    float4 existing = uCubemap[uint3(DTid.xy, faceIndex)];
    float4 result = lerp(existing, sceneColor, blendFactor);

    uCubemap[uint3(DTid.xy, faceIndex)] = result;
}
)HLSL";

static const char* kMipGenCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Generate mip chain for cubemap face via 2x2 box filter downsample.

cbuffer MipGenCB : register(b0)
{
    uint srcMipLevel;
    uint dstMipSize;
    uint faceIndex;
    uint pad;
}

Texture2DArray<float4>      tSrcMip : register(t0);
RWTexture2DArray<float4>    uDstMip : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= dstMipSize || DTid.y >= dstMipSize)
        return;

    // Source coordinates: each dst texel maps to a 2x2 block in the source mip
    uint2 srcBase = DTid.xy * 2;

    // 2x2 box filter: average 4 source texels
    float4 s00 = tSrcMip.Load(int4(srcBase + uint2(0, 0), faceIndex, srcMipLevel));
    float4 s10 = tSrcMip.Load(int4(srcBase + uint2(1, 0), faceIndex, srcMipLevel));
    float4 s01 = tSrcMip.Load(int4(srcBase + uint2(0, 1), faceIndex, srcMipLevel));
    float4 s11 = tSrcMip.Load(int4(srcBase + uint2(1, 1), faceIndex, srcMipLevel));

    float4 avg = (s00 + s10 + s01 + s11) * 0.25;

    uDstMip[uint3(DTid.xy, faceIndex)] = avg;
}
)HLSL";


// ── Constant buffers ──────────────────────────────────────────────────────

struct alignas(16) CubemapCaptureCBData
{
    float faceViewMatrix[16];
    float faceProjection[16];
    float cameraViewProj[16];
    float cameraInvViewProj[16];
    uint32_t faceIndex;
    uint32_t faceResolution;
    float    blendFactor;
    float    gameTime;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t pad[2];
};

struct alignas(16) MipGenCBData
{
    uint32_t srcMipLevel;
    uint32_t dstMipSize;
    uint32_t faceIndex;
    uint32_t pad;
};


// ── Cubemap face view matrices (world-space) ─────────────────────────────

static void BuildFaceViewMatrices(DirectX::XMFLOAT4X4 out[6])
{
    using namespace DirectX;

    // Eye at origin, looking along each axis
    XMVECTOR eye = XMVectorZero();
    struct FaceDir { XMVECTOR target; XMVECTOR up; };
    FaceDir faces[6] = {
        { XMVectorSet( 1, 0, 0, 0), XMVectorSet(0, 1, 0, 0) },  // +X
        { XMVectorSet(-1, 0, 0, 0), XMVectorSet(0, 1, 0, 0) },  // -X
        { XMVectorSet( 0, 1, 0, 0), XMVectorSet(0, 0,-1, 0) },  // +Y
        { XMVectorSet( 0,-1, 0, 0), XMVectorSet(0, 0, 1, 0) },  // -Y
        { XMVectorSet( 0, 0, 1, 0), XMVectorSet(0, 1, 0, 0) },  // +Z
        { XMVectorSet( 0, 0,-1, 0), XMVectorSet(0, 1, 0, 0) },  // -Z
    };

    for (int i = 0; i < 6; ++i) {
        XMMATRIX v = XMMatrixLookAtLH(eye, faces[i].target, faces[i].up);
        XMStoreFloat4x4(&out[i], v);
    }
}


// ── Initialize ────────────────────────────────────────────────────────────

bool DynamicCubemapRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
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

    // Pre-compute face view matrices
    BuildFaceViewMatrices(m_faceViewMatrices);

    // 90-degree FOV projection (standard for cubemap capture)
    using namespace DirectX;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, 0.1f, 131072.0f);
    XMStoreFloat4x4(&m_faceProjMatrix, proj);

    // Register as PostGeometry pipeline pass
    auto& pipeline = RenderPipeline::Get();
    if (pipeline.IsInitialized()) {
        PassDef pass;
        pass.name     = "DynamicCubemap";
        pass.stage    = PipelineStage::PostGeometry;
        pass.priority = 26;
        pass.enabled  = m_enabled;
        pass.execute  = [this](PassContext& ctx) { ExecutePass(ctx); };
        m_pipelineHandle = pipeline.AddPass(pass);
    }

    m_initialized = true;
    SKSE::log::info("DynamicCubemapRenderer: initialized ({}x{} per face, t{})",
        m_faceResolution, m_faceResolution, kSRVSlot);
    return true;
}

void DynamicCubemapRenderer::SetFaceResolution(uint32_t v)
{
    v = (v < 32) ? 32 : (v > 512) ? 512 : v;
    if (v != m_faceResolution && m_initialized) {
        m_faceResolution = v;
        RebuildCubemap();
    } else {
        m_faceResolution = v;
    }
}

void DynamicCubemapRenderer::Shutdown()
{
    ReleaseResources();
    m_initialized = false;
}

bool DynamicCubemapRenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Capture CS
    ID3DBlob* blob = ShaderLoader::Compile("Cubemap_Capture", kCubemapCaptureCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("CubemapCaptureCS compile failed");
        return false;
    }
    HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_captureCS);
    blob->Release();
    if (FAILED(hr)) return false;

    // Mip gen CS
    blob = ShaderLoader::Compile("Cubemap_MipGen", kMipGenCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("CubemapMipGenCS compile failed");
        return false;
    }
    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_mipGenCS);
    blob->Release();
    return SUCCEEDED(hr);
}

bool DynamicCubemapRenderer::CreateResources()
{
    // Calculate mip count
    uint32_t mipLevels = 1;
    {
        uint32_t s = m_faceResolution;
        while (s > 1) { s >>= 1; mipLevels++; }
    }

    // Create cubemap texture array (6 faces)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width      = m_faceResolution;
    texDesc.Height     = m_faceResolution;
    texDesc.MipLevels  = mipLevels;
    texDesc.ArraySize  = 6;
    texDesc.Format     = DXGI_FORMAT_R11G11B10_FLOAT;
    texDesc.SampleDesc = { 1, 0 };
    texDesc.Usage      = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
    texDesc.MiscFlags  = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

    if (FAILED(m_device->CreateTexture2D(&texDesc, nullptr, &m_cubemapTex)))
        return false;

    // Cubemap SRV (for game shaders to sample)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = mipLevels;
    srvDesc.TextureCube.MostDetailedMip = 0;
    if (FAILED(m_device->CreateShaderResourceView(m_cubemapTex, &srvDesc, &m_cubemapSRV)))
        return false;

    // Per-face RTVs for the capture pass (mip 0 only)
    for (uint32_t face = 0; face < 6; ++face) {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = texDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.FirstArraySlice = face;
        rtvDesc.Texture2DArray.ArraySize = 1;
        if (FAILED(m_device->CreateRenderTargetView(m_cubemapTex, &rtvDesc, &m_faceRTVs[face])))
            return false;
    }

    // Constant buffer
    return CreateCB(m_device, sizeof(CubemapCaptureCBData), &m_constantsCB);
}

void DynamicCubemapRenderer::RebuildCubemap()
{
    // Release old resources and recreate with new resolution
    if (m_cubemapSRV) { m_cubemapSRV->Release(); m_cubemapSRV = nullptr; }
    if (m_cubemapTex) { m_cubemapTex->Release(); m_cubemapTex = nullptr; }
    for (auto& rtv : m_faceRTVs) {
        if (rtv) { rtv->Release(); rtv = nullptr; }
    }
    CreateResources();
}

void DynamicCubemapRenderer::ReleaseResources()
{
    if (m_captureCS)   { m_captureCS->Release();   m_captureCS = nullptr; }
    if (m_mipGenCS)    { m_mipGenCS->Release();    m_mipGenCS = nullptr; }
    if (m_constantsCB) { m_constantsCB->Release(); m_constantsCB = nullptr; }
    if (m_cubemapSRV)  { m_cubemapSRV->Release();  m_cubemapSRV = nullptr; }
    if (m_cubemapTex)  { m_cubemapTex->Release();  m_cubemapTex = nullptr; }
    if (m_cubemapStaging) { m_cubemapStaging->Release(); m_cubemapStaging = nullptr; }
    for (auto& rtv : m_faceRTVs) {
        if (rtv) { rtv->Release(); rtv = nullptr; }
    }
}

// ── Per-frame execution ───────────────────────────────────────────────────

void DynamicCubemapRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_enabled || !m_initialized || !m_cubemapTex) return;

    // Update scheduling: capture one face per frame (round-robin)
    // With updateFrequency < 1, skip frames
    m_frameIndex++;
    uint32_t framesPerFace = static_cast<uint32_t>(1.0f / m_updateFrequency + 0.5f);
    if (framesPerFace < 1) framesPerFace = 1;
    if ((m_frameIndex % framesPerFace) != 0) return;

    CaptureFace(m_currentFace, ctx);
    m_currentFace = (m_currentFace + 1) % 6;

    // Generate mips after completing a full rotation (every 6 captures)
    if (m_currentFace == 0) {
        GenerateMips();
    }
}

void DynamicCubemapRenderer::CaptureFace(uint32_t faceIndex, PassContext& ctx)
{
    if (!D3D11Hook::IsProxyActive()) return;

    // Create UAV for the specific face (mip 0)
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.MipSlice = 0;
    uavDesc.Texture2DArray.FirstArraySlice = 0;
    uavDesc.Texture2DArray.ArraySize = 6;

    ID3D11UnorderedAccessView* cubemapUAV = nullptr;
    HRESULT hr = m_device->CreateUnorderedAccessView(m_cubemapTex, &uavDesc, &cubemapUAV);
    if (FAILED(hr)) return;

    // Get backbuffer SRV for sampling
    // NOTE: We need a SRV of the backbuffer. If HDR swap chain, it's R16G16B16A16_FLOAT.
    ID3D11Texture2D* bbTex = nullptr;
    if (ctx.swapChain) {
        ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bbTex);
    }
    if (!bbTex) { cubemapUAV->Release(); return; }

    // Query actual backbuffer format (HDR = R16G16B16A16_FLOAT, SDR = R8G8B8A8 or R10G10B10A2)
    D3D11_TEXTURE2D_DESC bbDesc;
    bbTex->GetDesc(&bbDesc);

    ID3D11ShaderResourceView* bbSRV = nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = bbDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = m_device->CreateShaderResourceView(bbTex, &srvDesc, &bbSRV);
    bbTex->Release();
    if (FAILED(hr)) { cubemapUAV->Release(); return; }

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) { bbSRV->Release(); cubemapUAV->Release(); return; }

    auto* cb = static_cast<CubemapCaptureCBData*>(mapped.pData);
    memset(cb, 0, sizeof(CubemapCaptureCBData));

    memcpy(cb->faceViewMatrix, &m_faceViewMatrices[faceIndex], sizeof(float) * 16);
    memcpy(cb->faceProjection, &m_faceProjMatrix, sizeof(float) * 16);

    auto& sm = SceneMatrices::Get();
    if (sm.IsValid()) {
        memcpy(cb->cameraViewProj, sm.ViewProjMatrix(), sizeof(float) * 16);
        memcpy(cb->cameraInvViewProj, sm.InvViewProjMatrix(), sizeof(float) * 16);
        cb->gameTime = static_cast<float>(sm.FrameIndex());
    }

    cb->faceIndex      = faceIndex;
    cb->faceResolution = m_faceResolution;
    cb->blendFactor    = m_blendSpeed;
    cb->screenWidth    = m_screenW;
    cb->screenHeight   = m_screenH;

    ctx.context->Unmap(m_constantsCB, 0);

    // Dispatch capture
    ctx.context->CSSetShader(m_captureCS, nullptr, 0);
    ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);
    ctx.context->CSSetShaderResources(0, 1, &bbSRV);
    ctx.context->CSSetUnorderedAccessViews(0, 1, &cubemapUAV, nullptr);

    uint32_t groups = (m_faceResolution + 7) / 8;
    ctx.context->Dispatch(groups, groups, 1);

    // Unbind
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ID3D11ShaderResourceView*  nullSRV = nullptr;
    ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ctx.context->CSSetShaderResources(0, 1, &nullSRV);
    ctx.context->CSSetShader(nullptr, nullptr, 0);

    bbSRV->Release();
    cubemapUAV->Release();
}

void DynamicCubemapRenderer::GenerateMips()
{
    // Use GenerateMips for simplicity (built-in D3D11 function)
    if (m_cubemapSRV) {
        m_context->GenerateMips(m_cubemapSRV);
    }
}

} // namespace SB
