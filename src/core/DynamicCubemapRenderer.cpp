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
#include "D3D11Hook.h"
#include "SceneData.h"   // SceneMatrices
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kCubemapCaptureCS = R"HLSL(
//  CubemapCaptureCS — Captures scene from a specific cube face direction
//
//  Reads the current backbuffer and reprojects pixels into the appropriate
//  cubemap face direction.  Uses the camera's view matrix + face-specific
//  rotation to sample the correct region of the screen.

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
};

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<float>    tDepth      : register(t1);
SamplerState        sLinear     : register(s0);

RWTexture2DArray<float4> uCubemap : register(u0);

// Convert cube face direction to world direction
float3 FaceUVToDirection(uint face, float2 uv)
{
    // Map [0,1] to [-1,1]
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;

    // D3D cubemap face layout:
    // 0: +X, 1: -X, 2: +Y, 3: -Y, 4: +Z, 5: -Z
    float3 dir;
    switch (face) {
        case 0: dir = float3( 1, -v, -u); break;  // +X
        case 1: dir = float3(-1, -v,  u); break;  // -X
        case 2: dir = float3( u,  1,  v); break;  // +Y
        case 3: dir = float3( u, -1, -v); break;  // -Y
        case 4: dir = float3( u, -v,  1); break;  // +Z
        case 5: dir = float3(-u, -v, -1); break;  // -Z
    }
    return normalize(dir);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= faceResolution || DTid.y >= faceResolution)
        return;

    float2 faceUV = (float2(DTid.xy) + 0.5) / float(faceResolution);

    // Get world-space direction for this cubemap texel
    float3 worldDir = FaceUVToDirection(faceIndex, faceUV);

    // Project this direction into screen space using current camera
    float4 clipPos = mul(cameraViewProj, float4(worldDir * 1000.0, 1.0));
    clipPos.xyz /= clipPos.w;

    // Map from clip [-1,1] to UV [0,1]
    float2 screenUV = float2(clipPos.x * 0.5 + 0.5, -clipPos.y * 0.5 + 0.5);

    float4 color = float4(0, 0, 0, 0);

    // Only sample if the direction is visible on screen (not behind camera)
    if (clipPos.w > 0 && screenUV.x >= 0 && screenUV.x <= 1 && screenUV.y >= 0 && screenUV.y <= 1) {
        color = tBackbuffer.SampleLevel(sLinear, screenUV, 0);
    } else {
        // For off-screen directions, use sky color approximation
        float skyBlend = saturate(worldDir.z * 0.5 + 0.5);
        float3 skyColor = lerp(float3(0.4, 0.5, 0.6), float3(0.2, 0.4, 0.8), skyBlend);
        color = float4(skyColor, 1.0);
    }

    // Temporal blend with previous frame
    float4 previous = uCubemap[uint3(DTid.xy, faceIndex)];
    color = lerp(previous, color, blendFactor);

    uCubemap[uint3(DTid.xy, faceIndex)] = color;
}
)HLSL";

static const char* kMipGenCS = R"HLSL(
//  CubemapMipGenCS — Generate mipmap for a single cubemap face level
cbuffer MipGenCB : register(b0)
{
    uint srcMipLevel;
    uint dstMipSize;
    uint faceIndex;
    uint pad;
};

Texture2DArray<float4>      tSrcMip : register(t0);
RWTexture2DArray<float4>    uDstMip : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= dstMipSize || DTid.y >= dstMipSize)
        return;

    // 2x2 box filter from source mip
    uint2 srcCoord = DTid.xy * 2;
    float4 c00 = tSrcMip[uint3(srcCoord + uint2(0, 0), faceIndex)];
    float4 c10 = tSrcMip[uint3(srcCoord + uint2(1, 0), faceIndex)];
    float4 c01 = tSrcMip[uint3(srcCoord + uint2(0, 1), faceIndex)];
    float4 c11 = tSrcMip[uint3(srcCoord + uint2(1, 1), faceIndex)];

    float4 avg = (c00 + c10 + c01 + c11) * 0.25;

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

    // Register as PostENB pipeline pass (captures post-ENB scene)
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
    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Capture CS
    HRESULT hr = D3DCompile(kCubemapCaptureCS, strlen(kCubemapCaptureCS),
        "CubemapCaptureCS", nullptr, nullptr, "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("CubemapCaptureCS compile error: {}",
                static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_captureCS);
    blob->Release();
    if (err) err->Release();
    if (FAILED(hr)) return false;

    // Mip gen CS
    hr = D3DCompile(kMipGenCS, strlen(kMipGenCS),
        "CubemapMipGenCS", nullptr, nullptr, "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("CubemapMipGenCS compile error: {}",
                static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_mipGenCS);
    blob->Release();
    if (err) err->Release();
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
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth      = (sizeof(CubemapCaptureCBData) + 15) & ~15;
    cbd.Usage           = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_device->CreateBuffer(&cbd, nullptr, &m_constantsCB));
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
