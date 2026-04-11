//=============================================================================
//  ScreenSpaceDecalRenderer.cpp — Screen-space deferred decal projection
//
//  Projects oriented bounding box decals into screen space.  For each pixel,
//  reconstructs world position from depth, transforms into each decal's local
//  space, and if inside the box, samples a procedural pattern and blends onto
//  the backbuffer.  Normal-aware attenuation fades decals on surfaces that
//  face away from the decal's projection axis.
//
//  Single compute pass: reads depth (t1), normals (t2), backbuffer copy (t0),
//  structured decal buffer (t3), writes backbuffer UAV (u0).
//=============================================================================

#include "ScreenSpaceDecalRenderer.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"
#include "ComputeManager.h"
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include <mutex>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kDecalCS = R"HLSL(
//  ScreenSpaceDecalCS — Deferred decal projection via compute shader
//
//  Inputs:
//    t0  = Backbuffer copy (SRV, read original color)
//    t1  = Depth buffer
//    t2  = G-buffer normals
//    t3  = StructuredBuffer<GPUDecal>, up to 64 active decals
//  Output:
//    u0  = Backbuffer (UAV, write blended result)

struct GPUDecal
{
    float3 position;    float normalFade;
    float4 rotation;    // quaternion (x, y, z, w)
    float3 size;        float opacity;
    float4 color;
    uint   pattern;     uint3 pad;
};

cbuffer DecalCB : register(b0)
{
    float4x4 invViewProj;
    float3   cameraPos;     float nearZ;
    float    farZ;
    uint     screenWidth;
    uint     screenHeight;
    uint     decalCount;
    float    globalOpacity;
    float    normalThreshold;
    float2   pad;
};

Texture2D<float4>          tBackbuffer : register(t0);
Texture2D<float>           tDepth      : register(t1);
Texture2D<float4>          tNormals    : register(t2);
StructuredBuffer<GPUDecal> tDecals     : register(t3);
RWTexture2D<float4>        uBackbuffer : register(u0);

// Quaternion rotation: v' = q * v * q^-1
float3 rotateByQuat(float3 v, float4 q)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Inverse quaternion rotation (conjugate for unit quaternions)
float3 rotateByQuatInv(float3 v, float4 q)
{
    return rotateByQuat(v, float4(-q.xyz, q.w));
}

// Procedural decal pattern
float sampleDecalPattern(float2 localUV, uint pattern)
{
    if (pattern == 0) return 1.0; // Solid

    if (pattern == 1) // Circle
    {
        float d = length(localUV);
        return smoothstep(1.0, 0.9, d);
    }

    if (pattern == 2) // Splatter (procedural noise)
    {
        float n = frac(sin(dot(localUV * 7.0, float2(12.9898, 78.233))) * 43758.5453);
        float d = length(localUV);
        return smoothstep(1.0, 0.3, d) * step(0.3, n);
    }

    if (pattern == 3) // Impact (ring)
    {
        float d = length(localUV);
        return smoothstep(0.8, 0.7, d) * smoothstep(0.4, 0.5, d);
    }

    return 1.0;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Bounds check
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    uint2 pixel = DTid.xy;

    // Read depth — skip sky pixels (depth = 1.0 or 0.0 depending on convention)
    float rawDepth = tDepth[pixel];
    if (rawDepth >= 1.0 || rawDepth <= 0.0)
        return;

    // Reconstruct world position from depth
    float2 uv = (float2(pixel) + 0.5) / float2(screenWidth, screenHeight);
    float4 clipPos = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    clipPos.y = -clipPos.y;  // D3D UV convention
    float4 worldPos4 = mul(invViewProj, clipPos);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    // Read surface normal (decode from [0,1] to [-1,1])
    float3 surfaceNormal = tNormals[pixel].xyz * 2.0 - 1.0;
    surfaceNormal = normalize(surfaceNormal);

    // Read original color
    float4 color = tBackbuffer[pixel];

    // Process each active decal
    for (uint i = 0; i < decalCount; ++i)
    {
        GPUDecal decal = tDecals[i];

        // Transform world position into decal's local space
        float3 localPos = worldPos - decal.position;
        localPos = rotateByQuatInv(localPos, decal.rotation);

        // Scale by inverse half-extents to normalize to [-1,1]^3
        float3 absSize = max(decal.size, float3(0.001, 0.001, 0.001));
        localPos /= absSize;

        // Check if inside decal volume [-1,1]^3
        if (any(abs(localPos) > 1.0))
            continue;

        // Normal alignment check
        // Decal "up" axis is the local Y axis rotated by the decal quaternion
        float3 decalUp = rotateByQuat(float3(0, 1, 0), decal.rotation);
        float normalDot = abs(dot(surfaceNormal, decalUp));
        float normalWeight = smoothstep(normalThreshold, normalThreshold + 0.2, normalDot);
        normalWeight = lerp(1.0, normalWeight, decal.normalFade);

        if (normalWeight <= 0.0)
            continue;

        // Use local XZ as UV for pattern sampling (mapped to [-1,1])
        float2 localUV = localPos.xz;

        // Sample procedural pattern
        float pattern = sampleDecalPattern(localUV, decal.pattern);
        if (pattern <= 0.0)
            continue;

        // Edge fade — soft edges at box boundaries
        float3 edgeDist = 1.0 - abs(localPos);
        float edgeFade = saturate(edgeDist.x * 5.0) *
                         saturate(edgeDist.y * 5.0) *
                         saturate(edgeDist.z * 5.0);

        // Final blend factor
        float blendFactor = pattern * decal.opacity * edgeFade * normalWeight * globalOpacity;
        blendFactor = saturate(blendFactor);

        // Blend decal color onto surface
        color.rgb = lerp(color.rgb, decal.color.rgb, blendFactor * decal.color.a);
    }

    // Write final result
    uBackbuffer[pixel] = color;
}
)HLSL";


// ── GPU decal struct (must match HLSL GPUDecal exactly) ───────────────────

struct GPUDecal
{
    float position[3];   float normalFade;
    float rotation[4];   // quaternion (x, y, z, w)
    float size[3];       float opacity;
    float color[4];
    uint32_t pattern;    uint32_t pad[3];
};

static_assert(sizeof(GPUDecal) == 80, "GPUDecal must be 80 bytes (match HLSL)");


// ── Constant buffer layout ────────────────────────────────────────────────

struct alignas(16) DecalCBData
{
    float    invViewProj[16];
    float    cameraPos[3];      float nearZ;
    float    farZ;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t decalCount;
    float    globalOpacity;
    float    normalThreshold;
    float    pad[2];
};

static_assert(sizeof(DecalCBData) % 16 == 0, "DecalCBData must be 16-byte aligned");


// ── Decal management (thread-safe) ────────────────────────────────────────

uint32_t ScreenSpaceDecalRenderer::AddDecal(const DecalDef& decal)
{
    std::lock_guard<std::mutex> lock(m_decalMutex);

    if (static_cast<int>(m_decals.size()) >= m_maxDecals) {
        SKSE::log::warn("ScreenSpaceDecalRenderer: max decal count ({}) reached, ignoring AddDecal", m_maxDecals);
        return 0;
    }

    DecalEntry entry;
    entry.def = decal;
    entry.id  = m_nextDecalID++;
    entry.age = 0.0f;
    m_decals.push_back(entry);

    return entry.id;
}

void ScreenSpaceDecalRenderer::RemoveDecal(uint32_t id)
{
    std::lock_guard<std::mutex> lock(m_decalMutex);

    for (auto it = m_decals.begin(); it != m_decals.end(); ++it) {
        if (it->id == id) {
            m_decals.erase(it);
            return;
        }
    }
}

void ScreenSpaceDecalRenderer::ClearAllDecals()
{
    std::lock_guard<std::mutex> lock(m_decalMutex);
    m_decals.clear();
}

uint32_t ScreenSpaceDecalRenderer::GetActiveDecalCount() const
{
    std::lock_guard<std::mutex> lock(m_decalMutex);
    return static_cast<uint32_t>(m_decals.size());
}


// ── Initialize ────────────────────────────────────────────────────────────

bool ScreenSpaceDecalRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
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

    // Register as PreENB pipeline pass
    auto& pipeline = RenderPipeline::Get();
    if (pipeline.IsInitialized()) {
        PassDef pass;
        pass.name     = "ScreenSpaceDecals";
        pass.stage    = PipelineStage::PostGeometry;
        pass.priority = 22;
        pass.enabled  = m_enabled;
        pass.execute  = [this](PassContext& ctx) { ExecutePass(ctx); };
        m_pipelineHandle = pipeline.AddPass(pass);
    }

    m_initialized = true;
    SKSE::log::info("ScreenSpaceDecalRenderer: initialized ({}x{}, max {} decals)", m_screenW, m_screenH, m_maxDecals);
    return true;
}

void ScreenSpaceDecalRenderer::Shutdown()
{
    ClearAllDecals();
    ReleaseResources();
    m_initialized = false;
}


// ── Shader compilation ────────────────────────────────────────────────────

bool ScreenSpaceDecalRenderer::CompileShaders()
{
    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(kDecalCS, strlen(kDecalCS),
        "ScreenSpaceDecalCS", nullptr, nullptr, "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("ScreenSpaceDecalCS compile error: {}",
                static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }

    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_decalCS);
    blob->Release();
    if (err) err->Release();
    return SUCCEEDED(hr);
}


// ── Resource creation ─────────────────────────────────────────────────────

bool ScreenSpaceDecalRenderer::CreateResources()
{
    // Constant buffer
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth      = sizeof(DecalCBData);
    cbd.Usage           = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&cbd, nullptr, &m_constantsCB)))
        return false;

    // Structured buffer for GPU decal data (64 elements, dynamic)
    D3D11_BUFFER_DESC sbd = {};
    sbd.ByteWidth           = sizeof(GPUDecal) * m_maxDecals;
    sbd.Usage               = D3D11_USAGE_DYNAMIC;
    sbd.BindFlags            = D3D11_BIND_SHADER_RESOURCE;
    sbd.CPUAccessFlags       = D3D11_CPU_ACCESS_WRITE;
    sbd.MiscFlags            = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    sbd.StructureByteStride  = sizeof(GPUDecal);
    if (FAILED(m_device->CreateBuffer(&sbd, nullptr, &m_decalBuffer)))
        return false;

    // SRV for structured buffer
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements  = m_maxDecals;
    if (FAILED(m_device->CreateShaderResourceView(m_decalBuffer, &srvDesc, &m_decalBufferSRV)))
        return false;

    // Backbuffer copy texture (for reading original color while writing via UAV)
    D3D11_TEXTURE2D_DESC copyDesc = {};
    copyDesc.Width            = m_screenW;
    copyDesc.Height           = m_screenH;
    copyDesc.MipLevels        = 1;
    copyDesc.ArraySize        = 1;
    copyDesc.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    copyDesc.SampleDesc.Count = 1;
    copyDesc.Usage            = D3D11_USAGE_DEFAULT;
    copyDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&copyDesc, nullptr, &m_backbufferCopy)))
        return false;

    // SRV for backbuffer copy
    D3D11_SHADER_RESOURCE_VIEW_DESC copySRVDesc = {};
    copySRVDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
    copySRVDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    copySRVDesc.Texture2D.MostDetailedMip = 0;
    copySRVDesc.Texture2D.MipLevels       = 1;
    if (FAILED(m_device->CreateShaderResourceView(m_backbufferCopy, &copySRVDesc, &m_backbufferCopySRV)))
        return false;

    return true;
}

void ScreenSpaceDecalRenderer::ReleaseResources()
{
    if (m_decalCS)          { m_decalCS->Release();          m_decalCS = nullptr; }
    if (m_constantsCB)      { m_constantsCB->Release();      m_constantsCB = nullptr; }
    if (m_decalBuffer)      { m_decalBuffer->Release();      m_decalBuffer = nullptr; }
    if (m_decalBufferSRV)   { m_decalBufferSRV->Release();   m_decalBufferSRV = nullptr; }
    if (m_backbufferCopy)   { m_backbufferCopy->Release();   m_backbufferCopy = nullptr; }
    if (m_backbufferCopySRV){ m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr; }
    if (m_backbufferUAV)    { m_backbufferUAV->Release();    m_backbufferUAV = nullptr; }
}


// ── Per-frame execution ───────────────────────────────────────────────────

void ScreenSpaceDecalRenderer::ExecutePass(PassContext& ctx)
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

    // Lock decals for this frame
    std::lock_guard<std::mutex> lock(m_decalMutex);

    // Update lifetimes and remove expired decals
    for (auto it = m_decals.begin(); it != m_decals.end(); ) {
        it->age += ctx.deltaTime;
        if (it->def.lifetime >= 0.0f) {
            it->def.lifetime -= ctx.deltaTime;
            if (it->def.lifetime <= 0.0f) {
                it = m_decals.erase(it);
                continue;
            }
        }
        ++it;
    }

    // Nothing to draw
    if (m_decals.empty()) return;

    // Acquire SRVs
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    // Prefer HiZ (reversed-Z) over raw game depth per pipeline rules
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }
    auto* normalsSRV = D3D11Hook::GetGBufferNormalsSRV();
    if (!depthSRV || !normalsSRV) return;

    auto& sm = SceneMatrices::Get();
    if (!sm.IsValid()) return;

    // ── Save OM state + Get backbuffer and create per-frame UAV ──────────
    auto& cm = ComputeManager::Get();
    cm.SaveOMState();

    ID3D11Texture2D* backbufferTex = nullptr;
    if (ctx.swapChain) {
        ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbufferTex);
    }
    if (!backbufferTex) { cm.RestoreOMState(); return; }

    // Copy backbuffer to read texture (can't SRV + UAV same resource)
    ctx.context->CopyResource(m_backbufferCopy, backbufferTex);

    // Recreate UAV each frame (backbuffer may be different after resize)
    if (m_backbufferUAV) { m_backbufferUAV->Release(); m_backbufferUAV = nullptr; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    HRESULT hr = m_device->CreateUnorderedAccessView(backbufferTex, &uavDesc, &m_backbufferUAV);
    backbufferTex->Release();
    if (FAILED(hr)) { cm.RestoreOMState(); return; }

    // ── Upload active decals to GPU structured buffer ────────────────────

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx.context->Map(m_decalBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    auto* gpuDecals = static_cast<GPUDecal*>(mapped.pData);
    uint32_t count = static_cast<uint32_t>(m_decals.size());
    if (count > static_cast<uint32_t>(m_maxDecals))
        count = static_cast<uint32_t>(m_maxDecals);

    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = m_decals[i].def;
        auto& dst = gpuDecals[i];

        memcpy(dst.position, src.position, sizeof(float) * 3);
        dst.normalFade = src.normalFade;
        memcpy(dst.rotation, src.rotation, sizeof(float) * 4);
        memcpy(dst.size, src.size, sizeof(float) * 3);
        dst.opacity = src.opacity;
        memcpy(dst.color, src.color, sizeof(float) * 4);
        dst.pattern = src.pattern;
        dst.pad[0] = 0; dst.pad[1] = 0; dst.pad[2] = 0;
    }

    ctx.context->Unmap(m_decalBuffer, 0);

    // ── Update constant buffer ───────────────────────────────────────────

    hr = ctx.context->Map(m_constantsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    auto* cb = static_cast<DecalCBData*>(mapped.pData);
    memset(cb, 0, sizeof(DecalCBData));

    memcpy(cb->invViewProj, sm.InvViewProjMatrix(), sizeof(float) * 16);
    memcpy(cb->cameraPos, sm.CameraPos(), sizeof(float) * 3);
    cb->nearZ           = sm.NearClip();
    cb->farZ            = sm.FarClip();
    cb->screenWidth     = m_screenW;
    cb->screenHeight    = m_screenH;
    cb->decalCount      = count;
    cb->globalOpacity   = m_globalOpacity;
    cb->normalThreshold = m_normalThreshold;

    ctx.context->Unmap(m_constantsCB, 0);

    // ── Save CS state before dispatch ──────────────────────────────────
    cm.SaveCSState();

    // ── Bind and dispatch ────────────────────────────────────────────────

    ctx.context->CSSetShader(m_decalCS, nullptr, 0);
    ctx.context->CSSetConstantBuffers(0, 1, &m_constantsCB);

    ID3D11ShaderResourceView* srvs[] = {
        m_backbufferCopySRV,   // t0: backbuffer copy (read)
        depthSRV,              // t1: depth
        normalsSRV,            // t2: normals
        m_decalBufferSRV,      // t3: decal structured buffer
    };
    ctx.context->CSSetShaderResources(0, 4, srvs);
    ctx.context->CSSetUnorderedAccessViews(0, 1, &m_backbufferUAV, nullptr);

    uint32_t groupsX = (m_screenW + 7) / 8;
    uint32_t groupsY = (m_screenH + 7) / 8;
    ctx.context->Dispatch(groupsX, groupsY, 1);

    // ── Unbind ───────────────────────────────────────────────────────────

    ID3D11UnorderedAccessView* nullUAV     = nullptr;
    ID3D11ShaderResourceView*  nullSRVs[4] = {};
    ctx.context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ctx.context->CSSetShaderResources(0, 4, nullSRVs);
    ctx.context->CSSetShader(nullptr, nullptr, 0);

    // Restore CS state + OM state
    cm.RestoreCSState();
    cm.RestoreOMState();

    m_frameIndex++;
}

} // namespace SB
