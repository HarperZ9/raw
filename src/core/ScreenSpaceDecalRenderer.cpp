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
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"
#include "ComputeManager.h"
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include <mutex>
#include "GPUResource.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace SB
{

// ── Embedded HLSL ─────────────────────────────────────────────────────────

static const char* kDecalCS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Screen-space deferred decal projection.
// For each pixel, reconstructs world position from depth, then tests
// against every active decal's oriented bounding box. If the pixel
// lies inside a decal volume and the surface normal is compatible
// with the decal's projection axis, a procedural decal pattern is
// sampled and alpha-blended onto the backbuffer.
//
// Reference: Persson, "Volume Decals", GPU Pro 5, 2014.

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
}

Texture2D<float4>          tBackbuffer : register(t0);
Texture2D<float>           tDepth      : register(t1);
Texture2D<float4>          tNormals    : register(t2);
StructuredBuffer<GPUDecal> tDecals     : register(t3);
RWTexture2D<float4>        uBackbuffer : register(u0);

// ---------------------------------------------------------------------------
// Quaternion helpers
// ---------------------------------------------------------------------------

// Rotate a vector by a unit quaternion.
float3 QuatRotate(float4 q, float3 v)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Inverse rotate (conjugate of unit quaternion).
float3 QuatRotateInverse(float4 q, float3 v)
{
    float4 qConj = float4(-q.xyz, q.w);
    return QuatRotate(qConj, v);
}

// ---------------------------------------------------------------------------
// Procedural decal patterns
// ---------------------------------------------------------------------------

// Generate a simple decal pattern based on local UV and pattern ID.
// Returns float4(rgb, alpha).
float4 SampleDecalPattern(float2 uv, uint pattern)
{
    float alpha = 1.0;

    // Pattern 0: solid rectangle with soft edges.
    if (pattern == 0)
    {
        float2 edgeDist = 1.0 - abs(uv * 2.0 - 1.0);
        float edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 8.0);
        alpha = edgeFade;
    }
    // Pattern 1: circle.
    else if (pattern == 1)
    {
        float2 centered = uv * 2.0 - 1.0;
        float dist = length(centered);
        alpha = saturate((1.0 - dist) * 4.0);
    }
    // Pattern 2: cross/splat.
    else if (pattern == 2)
    {
        float2 centered = abs(uv * 2.0 - 1.0);
        float crossDist = min(centered.x, centered.y);
        alpha = saturate((0.3 - crossDist) * 8.0);
    }
    // Pattern 3: ring.
    else if (pattern == 3)
    {
        float2 centered = uv * 2.0 - 1.0;
        float dist = length(centered);
        alpha = saturate((1.0 - abs(dist - 0.6)) * 8.0 - 3.0);
    }
    // Default: solid fill.
    else
    {
        alpha = 1.0;
    }

    return float4(1.0, 1.0, 1.0, alpha);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    if (decalCount == 0)
        return;

    // Depth — HiZ reversed-Z (near=1, far=0).
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.00001)
        return;

    // Reconstruct world position.
    float2 uv = (float2(DTid.xy) + 0.5) / float2(screenWidth, screenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, rawDepth, 1.0);
    float4 worldPos4 = mul(clipPos, invViewProj);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    // Decode surface normal.
    float4 rawNormal = tNormals.Load(int3(DTid.xy, 0));
    float3 normal = normalize(rawNormal.xyz * 2.0 - 1.0);

    // Read original scene color (from backbuffer copy at t0).
    float4 sceneColor = tBackbuffer.Load(int3(DTid.xy, 0));

    // Accumulate decal contributions.
    float3 accumColor = sceneColor.rgb;

    // Iterate over all active decals.  Cap iterations to prevent
    // excessive per-pixel cost; in practice decalCount is small.
    uint maxDecals = min(decalCount, 64u);

    for (uint d = 0; d < maxDecals; ++d)
    {
        GPUDecal decal = tDecals[d];

        // Transform world position into decal's local space.
        float3 localPos = QuatRotateInverse(decal.rotation, worldPos - decal.position);

        // Half-extents of the decal box.
        float3 halfSize = decal.size * 0.5;

        // Inside-box test: all local coords must be within [-halfSize, halfSize].
        float3 absLocal = abs(localPos);
        if (absLocal.x > halfSize.x || absLocal.y > halfSize.y || absLocal.z > halfSize.z)
            continue;

        // ── Normal attenuation ──────────────────────────────────────────
        // Decal projects along local Z axis.  Reject surfaces facing away.
        float3 decalForward = QuatRotate(decal.rotation, float3(0, 0, 1));
        float normalDot = abs(dot(normal, decalForward));
        if (normalDot < normalThreshold)
            continue;

        float normalFade = saturate((normalDot - normalThreshold) /
                                     max(1.0 - normalThreshold, 0.001));
        normalFade = lerp(1.0, normalFade, decal.normalFade);

        // ── Decal UV ────────────────────────────────────────────────────
        // Map local XY [-halfSize, halfSize] to UV [0, 1].
        float2 decalUV = (localPos.xy / halfSize.xy) * 0.5 + 0.5;

        // ── Sample pattern ──────────────────────────────────────────────
        float4 patternSample = SampleDecalPattern(decalUV, decal.pattern);

        // ── Depth fade at decal box boundaries ──────────────────────────
        float depthFade = saturate((halfSize.z - absLocal.z) / (halfSize.z * 0.2));

        // ── Alpha blend ─────────────────────────────────────────────────
        float finalAlpha = patternSample.a * decal.opacity * globalOpacity *
                           normalFade * depthFade;

        float3 decalColor = decal.color.rgb * patternSample.rgb;
        accumColor = lerp(accumColor, decalColor, saturate(finalAlpha));
    }

    uBackbuffer[DTid.xy] = float4(accumColor, sceneColor.a);
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

    // Register as PostGeometry pipeline pass
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
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = ShaderLoader::Compile("Decal_Main", kDecalCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("ScreenSpaceDecalCS compile failed");
        return false;
    }

    HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_decalCS);
    blob->Release();
    return SUCCEEDED(hr);
}


// ── Resource creation ─────────────────────────────────────────────────────

bool ScreenSpaceDecalRenderer::CreateResources()
{
    // Constant buffer
    if (!CreateCB(m_device, sizeof(DecalCBData), &m_constantsCB))
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
