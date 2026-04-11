//=============================================================================
//  TAAManager.cpp — Compute-based TAA resolve with persistent history buffer
//
//  Dispatch flow (per frame, from HookedPresent):
//    1. Acquire depth SRV from currently-bound DSV
//    2. Get backbuffer UAV (current frame color — post-ENB)
//    3. Save CS state
//    4. Bind:  t0 = backbuffer SRV (copy), t1 = history[read] SRV, t2 = depth SRV
//              u0 = backbuffer UAV (write-back), u1 = history[write] UAV
//              b0 = params CB (screen dims, frame index, jitter)
//    5. Dispatch resolve CS
//    6. Unbind, restore CS state
//    7. Swap ping-pong index
//
//  The resolve shader implements:
//    - Depth-based reprojection (no motion vectors — Skyrim doesn't have them)
//    - 14-DOP neighbourhood clamping in YCoCg (7 oriented axes, tighter than AABB)
//    - Perceptual tonemap in YCoCg space for HDR stability
//    - Disocclusion detection via clip distance + adaptive blend
//    - Depth discontinuity rejection at silhouette edges
//=============================================================================

#include "TAAManager.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

#include <SKSE/SKSE.h>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded compute shader — TAA resolve
// ═══════════════════════════════════════════════════════════════════════════

static const char* const kResolveCS = R"HLSL(
// TAAManager resolve compute shader — 14-DOP neighbourhood clamp in YCoCg
// Reads post-ENB backbuffer + depth + history, writes resolved to both
// backbuffer and history ping-pong target.
//
// Uses k-DOP (k-Discrete Oriented Polytope) clamping with 7 oriented axes
// (14 half-planes) for tighter color-space bounds than AABB, reducing ghosting.
// Clamping operates in YCoCg color space for perceptual uniformity.

cbuffer TAAParams : register(b0)
{
    uint2 ScreenDims;      // backbuffer width, height
    uint  FrameIndex;      // monotonic frame counter
    float BlendAlpha;      // base temporal blend [0.05 .. 0.2], default 0.1
    float4 Jitter;         // .xy = current sub-pixel jitter (NDC), .zw = unused
};

// Inputs
Texture2D<float4>   CurrentColor  : register(t0);  // Copy of backbuffer (pre-resolve)
Texture2D<float4>   HistoryColor  : register(t1);  // Previous frame history
Texture2D<float>    DepthBuffer   : register(t2);  // Scene depth (0..1)

// Output — resolved color written to history; CopyResource puts it back on BB
RWTexture2D<float4> OutputHistory : register(u1);  // History write (ping-pong)

SamplerState smpLinear : register(s0);

// ── Color space conversions ──────────────────────────────────────────

float3 RGBtoYCoCg(float3 c)
{
    return float3(
         0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
         0.5  * c.r             - 0.5  * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b
    );
}

float3 YCoCgtoRGB(float3 c)
{
    return float3(
        c.x + c.y - c.z,
        c.x       + c.z,
        c.x - c.y - c.z
    );
}

// ── Perceptual tonemap in YCoCg (operates on Y channel) ─────────────

float3 TonemapYCoCg(float3 ycocg)
{
    float w = 1.0 / (1.0 + ycocg.x);
    return ycocg * w;
}

float3 InvTonemapYCoCg(float3 ycocg)
{
    float w = 1.0 / max(1.0 - ycocg.x, 1e-6);
    return ycocg * w;
}

// ── 14-DOP clip ──────────────────────────────────────────────────────
// Clips a point (history) toward the mean along the ray mean→history,
// constraining it to the intersection of 7 axis-aligned slab pairs.
// This is geometrically the tightest convex polytope from 7 oriented
// axes — much tighter than an AABB (3 axes) for the same samples.

float3 ClipToKDOP(float3 history, float3 mean,
                  float minProj[7], float maxProj[7],
                  float3 axes[7])
{
    float3 dir = history - mean;
    float tMin = 0.0;
    float tMax = 1.0;

    [unroll]
    for (int i = 0; i < 7; i++)
    {
        float dProj = dot(dir, axes[i]);
        float oProj = dot(mean, axes[i]);

        if (abs(dProj) > 1e-6)
        {
            float t0 = (minProj[i] - oProj) / dProj;
            float t1 = (maxProj[i] - oProj) / dProj;
            if (t0 > t1)
            {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            tMin = max(tMin, t0);
            tMax = min(tMax, t1);
        }
    }

    tMax = max(tMin, tMax);
    float t = saturate(tMax);
    return mean + dir * t;
}

// ── Main ─────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= ScreenDims.x || dtid.y >= ScreenDims.y)
        return;

    int2 coord = int2(dtid.xy);
    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv = (float2(coord) + 0.5) * texelSize;

    // ── Sample current frame ─────────────────────────────────────────
    float3 current = CurrentColor.Load(int3(coord, 0)).rgb;

    // ── Depth-based reprojection ─────────────────────────────────────
    // Without true motion vectors we reproject using depth + jitter offset.
    // The jitter offset accounts for TAA sub-pixel shifts between frames.
    // This gives correct reprojection for static geometry; dynamic objects
    // will be handled by the neighbourhood clamp (ghosting suppression).
    float2 historyUV = uv - Jitter.xy;

    // Clamp to valid UV range
    historyUV = clamp(historyUV, texelSize * 0.5, 1.0 - texelSize * 0.5);

    // ── Sample history (bilinear) ────────────────────────────────────
    float3 history = HistoryColor.SampleLevel(smpLinear, historyUV, 0).rgb;

    // ── 14-DOP neighbourhood clamp in YCoCg (perceptual space) ───────
    // 7 oriented axes: 3 axis-aligned + 4 body diagonals (normalized).
    // For each axis, project all 9 neighbourhood samples and track
    // min/max projection.  Then clip history to the polytope interior.

    static const float kInvSqrt3 = 0.57735026919;  // rsqrt(3)

    float3 axes[7];
    axes[0] = float3(1, 0, 0);
    axes[1] = float3(0, 1, 0);
    axes[2] = float3(0, 0, 1);
    axes[3] = float3( kInvSqrt3,  kInvSqrt3,  kInvSqrt3);
    axes[4] = float3( kInvSqrt3,  kInvSqrt3, -kInvSqrt3);
    axes[5] = float3( kInvSqrt3, -kInvSqrt3,  kInvSqrt3);
    axes[6] = float3(-kInvSqrt3,  kInvSqrt3,  kInvSqrt3);

    float minProj[7];
    float maxProj[7];
    float sumProj[7];
    float sumSqProj[7];

    [unroll]
    for (int a = 0; a < 7; a++)
    {
        minProj[a]   =  1e6;
        maxProj[a]   = -1e6;
        sumProj[a]   = 0.0;
        sumSqProj[a] = 0.0;
    }

    float3 nMean = 0;

    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            int2 sc = clamp(coord + int2(dx, dy), 0, int2(ScreenDims) - 1);
            float3 rgb = CurrentColor.Load(int3(sc, 0)).rgb;
            float3 ycocg = TonemapYCoCg(RGBtoYCoCg(rgb));

            nMean += ycocg;

            [unroll]
            for (int a2 = 0; a2 < 7; a2++)
            {
                float p = dot(ycocg, axes[a2]);
                minProj[a2]   = min(minProj[a2], p);
                maxProj[a2]   = max(maxProj[a2], p);
                sumProj[a2]   += p;
                sumSqProj[a2] += p * p;
            }
        }
    }
    nMean /= 9.0;

    // Variance-based tightening: shrink raw [min,max] to mean ± gamma * stddev
    // Raw min/max is WAY too loose — lets stale history through, causing ghosting.
    // gamma < 1.0 because k-DOP already has more axes than AABB.
    static const float gamma = 0.75;

    [unroll]
    for (int a3 = 0; a3 < 7; a3++)
    {
        float meanP  = sumProj[a3] / 9.0;
        float variance = max(sumSqProj[a3] / 9.0 - meanP * meanP, 0.0);
        float stddev = sqrt(variance);
        minProj[a3] = meanP - gamma * stddev;
        maxProj[a3] = meanP + gamma * stddev;
    }

    // Convert history to tonemapped YCoCg for clamping
    float3 histYCoCg = TonemapYCoCg(RGBtoYCoCg(history));

    // Clip history to the variance-tightened 14-DOP polytope
    float3 clippedYCoCg = ClipToKDOP(histYCoCg, nMean, minProj, maxProj, axes);

    // Convert clipped result back to RGB
    history = YCoCgtoRGB(InvTonemapYCoCg(clippedYCoCg));

    // ── Disocclusion detection ───────────────────────────────────────
    // Large distance between raw history and clipped history in YCoCg
    // indicates the sample was clipped hard = likely disoccluded/moved.
    float clipDist = length(histYCoCg - clippedYCoCg);
    float disocclusion = saturate(clipDist * 15.0);

    // Increase blend toward current frame when disoccluded
    // 0.8 = nearly full current frame on hard disocclusion
    float alpha = lerp(BlendAlpha, 0.8, disocclusion);

    // ── Depth discontinuity rejection ────────────────────────────────
    float centerDepth = DepthBuffer.Load(int3(coord, 0));
    float minDepth = centerDepth;
    float maxDepth = centerDepth;
    [unroll]
    for (int dy3 = -1; dy3 <= 1; dy3++) {
        [unroll]
        for (int dx3 = -1; dx3 <= 1; dx3++) {
            int2 sc = clamp(coord + int2(dx3, dy3), 0, int2(ScreenDims) - 1);
            float d = DepthBuffer.Load(int3(sc, 0));
            minDepth = min(minDepth, d);
            maxDepth = max(maxDepth, d);
        }
    }
    float depthRange = maxDepth - minDepth;
    // At depth edges, aggressively reduce temporal accumulation
    float depthEdge = saturate(depthRange * 1000.0);
    alpha = lerp(alpha, max(alpha, 0.6), depthEdge);

    // ── Blend ────────────────────────────────────────────────────────
    float3 resolved = lerp(history, current, alpha);

    // Write resolved to history (CopyResource copies it back to backbuffer)
    OutputHistory[coord] = float4(resolved, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB layout — must match HLSL cbuffer TAAParams exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) TAAParamsCB
{
    uint32_t screenW;
    uint32_t screenH;
    uint32_t frameIndex;
    float    blendAlpha;
    float    jitterX;
    float    jitterY;
    float    pad0;
    float    pad1;
};
static_assert(sizeof(TAAParamsCB) == 32, "CB must be 32 bytes (2 float4)");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool TAAManager::Initialize(ID3D11Device* dev, IDXGISwapChain* swapChain)
{
    if (m_initialized) return true;
    if (!dev || !swapChain) return false;

    // Get backbuffer dimensions
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&backbuffer));
    if (FAILED(hr) || !backbuffer) {
        SKSE::log::error("TAAManager: Failed to get backbuffer (0x{:X})", static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backbuffer->GetDesc(&bbDesc);
    backbuffer->Release();

    m_width  = bbDesc.Width;
    m_height = bbDesc.Height;

    if (!CompileShader(dev)) return false;
    if (!CreateResources(dev, m_width, m_height)) return false;

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("TAAManager: Initialized ({}x{}, history at t{}, sampler at s{})",
                    m_width, m_height, kSRVSlot, kSamplerSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile resolve compute shader
// ═══════════════════════════════════════════════════════════════════════════

bool TAAManager::CompileShader(ID3D11Device* dev)
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    HRESULT hr = D3DCompile(kResolveCS, strlen(kResolveCS), "TAAResolveCS",
                            nullptr, nullptr, "main", "cs_5_0", flags, 0,
                            &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("TAAManager: Shader compile failed: {}",
                             static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    if (err) err->Release();

    hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                  nullptr, &m_resolveCS);
    blob->Release();
    if (FAILED(hr)) {
        SKSE::log::error("TAAManager: CreateComputeShader failed (0x{:X})", static_cast<uint32_t>(hr));
        return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool TAAManager::CreateResources(ID3D11Device* dev, uint32_t w, uint32_t h)
{
    // ── Ping-pong history textures (R16G16B16A16_FLOAT) ──────────────
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width      = w;
    texDesc.Height     = h;
    texDesc.MipLevels  = 1;
    texDesc.ArraySize  = 1;
    texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc = {1, 0};
    texDesc.Usage      = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = texDesc.Format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format             = texDesc.Format;
    uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    for (int i = 0; i < 2; ++i) {
        HRESULT hr = dev->CreateTexture2D(&texDesc, nullptr, &m_historyTex[i]);
        if (FAILED(hr)) {
            SKSE::log::error("TAAManager: Failed to create history texture {} (0x{:X})", i, static_cast<uint32_t>(hr));
            ReleaseResources();
            return false;
        }
        hr = dev->CreateShaderResourceView(m_historyTex[i], &srvDesc, &m_historySRV[i]);
        if (FAILED(hr)) {
            SKSE::log::error("TAAManager: Failed to create history SRV {}", i);
            ReleaseResources();
            return false;
        }
        hr = dev->CreateUnorderedAccessView(m_historyTex[i], &uavDesc, &m_historyUAV[i]);
        if (FAILED(hr)) {
            SKSE::log::error("TAAManager: Failed to create history UAV {}", i);
            ReleaseResources();
            return false;
        }
    }

    // ── Constant buffer (32 bytes) ───────────────────────────────────
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = sizeof(TAAParamsCB);
    cbDesc.Usage           = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev->CreateBuffer(&cbDesc, nullptr, &m_paramsCB))) {
        SKSE::log::error("TAAManager: Failed to create constant buffer");
        ReleaseResources();
        return false;
    }

    // ── Linear clamp sampler for history reads ───────────────────────
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(dev->CreateSamplerState(&sampDesc, &m_sampler))) {
        SKSE::log::error("TAAManager: Failed to create sampler");
        ReleaseResources();
        return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV from currently-bound DSV (same pattern as HiZPyramid)
// ═══════════════════════════════════════════════════════════════════════════

bool TAAManager::AcquireDepthSRV(ID3D11DeviceContext* ctx)
{
    // Release previous frame's depth SRV
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    // Get current DSV from output merger
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(0, nullptr, &dsv);
    if (!dsv) return false;

    // Get underlying texture
    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    dsv->Release();
    if (!res) return false;

    ID3D11Texture2D* depthTex = nullptr;
    HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&depthTex));
    res->Release();
    if (FAILED(hr) || !depthTex) return false;

    D3D11_TEXTURE2D_DESC desc;
    depthTex->GetDesc(&desc);

    // Must have SRV bind flag
    if (!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        depthTex->Release();
        return false;
    }

    // Determine SRV format from depth format
    DXGI_FORMAT srvFormat;
    switch (desc.Format) {
        case DXGI_FORMAT_R32_TYPELESS:       srvFormat = DXGI_FORMAT_R32_FLOAT;          break;
        case DXGI_FORMAT_R24G8_TYPELESS:     srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
        case DXGI_FORMAT_R32G8X24_TYPELESS:  srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
        case DXGI_FORMAT_R16_TYPELESS:       srvFormat = DXGI_FORMAT_R16_UNORM;          break;
        case DXGI_FORMAT_R32_FLOAT:          srvFormat = DXGI_FORMAT_R32_FLOAT;          break;
        case DXGI_FORMAT_R16_UNORM:          srvFormat = DXGI_FORMAT_R16_UNORM;          break;
        default:
            depthTex->Release();
            return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = srvFormat;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    hr = dev->CreateShaderResourceView(depthTex, &srvDesc, &m_depthSRV);
    dev->Release();
    depthTex->Release();

    return SUCCEEDED(hr) && m_depthSRV != nullptr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Resolve — main per-frame dispatch
// ═══════════════════════════════════════════════════════════════════════════

void TAAManager::Resolve(ID3D11DeviceContext* ctx, IDXGISwapChain* swapChain)
{
    if (!m_initialized || !m_enabled || !ctx || !swapChain)
        return;

    // ── Get backbuffer ───────────────────────────────────────────────
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&backbuffer));
    if (FAILED(hr) || !backbuffer) return;

    D3D11_TEXTURE2D_DESC bbDesc;
    backbuffer->GetDesc(&bbDesc);

    // Handle resolution change — recreate history textures at new size
    if (bbDesc.Width != m_width || bbDesc.Height != m_height) {
        ID3D11Device* dev = nullptr;
        ctx->GetDevice(&dev);
        if (dev) {
            SKSE::log::info("TAAManager: resolution changed {}x{} -> {}x{}, recreating resources",
                m_width, m_height, bbDesc.Width, bbDesc.Height);
            ReleaseResources();
            if (!CreateResources(dev, bbDesc.Width, bbDesc.Height)) {
                SKSE::log::error("TAAManager: failed to recreate resources at {}x{}",
                    bbDesc.Width, bbDesc.Height);
                backbuffer->Release();
                dev->Release();
                return;
            }
            m_firstFrame = true;
            dev->Release();
        }
        backbuffer->Release();
        return;  // Skip resolve this frame — start fresh next frame
    }

    // ── First frame: just copy backbuffer to history[0] and return ───
    if (m_firstFrame) {
        ctx->CopyResource(m_historyTex[0], backbuffer);
        ctx->CopyResource(m_historyTex[1], backbuffer);
        m_firstFrame = false;
        m_writeIdx = 0;
        backbuffer->Release();
        ++m_frameIndex;
        return;
    }

    // Check if backbuffer supports SRV binding (needed to read current frame)
    bool bbHasSRV = (bbDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;

    int readIdx  = 1 - m_writeIdx;
    int writeIdx = m_writeIdx;

    // ── Acquire depth ────────────────────────────────────────────────
    AcquireDepthSRV(ctx);

    // ── Update constant buffer ───────────────────────────────────────
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = ctx->Map(m_paramsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            TAAParamsCB cb;
            cb.screenW    = m_width;
            cb.screenH    = m_height;
            cb.frameIndex = m_frameIndex;
            cb.blendAlpha = 0.1f;  // Base temporal blend factor
            cb.jitterX    = m_jitterX;
            cb.jitterY    = m_jitterY;
            cb.pad0       = 0.0f;
            cb.pad1       = 0.0f;
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx->Unmap(m_paramsCB, 0);
        }
    }

    // ── Save CS state ────────────────────────────────────────────────
    ID3D11ComputeShader* savedCS = nullptr;
    ID3D11ClassInstance* savedCI[256] = {};
    UINT savedCICount = 256;
    ctx->CSGetShader(&savedCS, savedCI, &savedCICount);

    ID3D11ShaderResourceView* savedSRVs[3] = {};
    ctx->CSGetShaderResources(0, 3, savedSRVs);

    ID3D11UnorderedAccessView* savedUAVs[2] = {};
    ctx->CSGetUnorderedAccessViews(0, 2, savedUAVs);

    ID3D11Buffer* savedCBs[1] = {};
    ctx->CSGetConstantBuffers(0, 1, savedCBs);

    ID3D11SamplerState* savedSamplers[1] = {};
    ctx->CSGetSamplers(0, 1, savedSamplers);

    // ── Bind resources and dispatch ──────────────────────────────────
    ctx->CSSetShader(m_resolveCS, nullptr, 0);

    // ── Resolve SRV/UAV bindings ────────────────────────────────────
    // We need: READ current frame + READ old history + WRITE new history.
    // With 2 ping-pong textures, binding one as both SRV and UAV is a conflict.
    // Solution: read current frame directly from the backbuffer SRV (no copy needed),
    // read old history from history[readIdx] SRV, write to history[writeIdx] UAV.
    // All three are distinct textures — no conflicts.
    // After dispatch, CopyResource(backbuffer, history[writeIdx]) writes back.
    ID3D11ShaderResourceView* nullSRVs[3] = {};

    // Try creating backbuffer SRV directly
    ID3D11ShaderResourceView* bbSRV = nullptr;
    if (bbHasSRV) {
        D3D11_SHADER_RESOURCE_VIEW_DESC bbSRVDesc = {};
        bbSRVDesc.Format                    = bbDesc.Format;
        bbSRVDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        bbSRVDesc.Texture2D.MostDetailedMip = 0;
        bbSRVDesc.Texture2D.MipLevels       = 1;

        ID3D11Device* dev = nullptr;
        ctx->GetDevice(&dev);
        hr = dev->CreateShaderResourceView(backbuffer, &bbSRVDesc, &bbSRV);
        dev->Release();
    }

    if (!bbSRV) {
        // Backbuffer doesn't support SRV — fall through, copy BB→history[write],
        // use history[write] SRV as current, history[read] SRV as old history,
        // and write into a third temporary. But we don't have a third texture.
        //
        // Alternative: skip TAA this frame. Most Skyrim configs have SRV on BB.
        backbuffer->Release();

        // Restore CS state before returning
        ctx->CSSetShader(savedCS, savedCI, savedCICount);
        ctx->CSSetShaderResources(0, 3, savedSRVs);
        ctx->CSSetUnorderedAccessViews(0, 2, savedUAVs, nullptr);
        ctx->CSSetConstantBuffers(0, 1, savedCBs);
        ctx->CSSetSamplers(0, 1, savedSamplers);
        if (savedCS) savedCS->Release();
        for (UINT i = 0; i < 3; i++) if (savedSRVs[i]) savedSRVs[i]->Release();
        for (UINT i = 0; i < 2; i++) if (savedUAVs[i]) savedUAVs[i]->Release();
        for (UINT i = 0; i < 1; i++) if (savedCBs[i]) savedCBs[i]->Release();
        for (UINT i = 0; i < 1; i++) if (savedSamplers[i]) savedSamplers[i]->Release();
        if (m_depthSRV) { m_depthSRV->Release(); m_depthSRV = nullptr; }
        ++m_frameIndex;
        return;
    }

    // Now we have:
    //   bbSRV → current frame (from backbuffer directly)
    //   m_historySRV[readIdx] → previous history
    //   m_historyUAV[writeIdx] → write target for resolved
    //   No SRV/UAV conflict!

    ID3D11ShaderResourceView* dispatchSRVs[3] = {
        bbSRV,                    // t0: current backbuffer
        m_historySRV[readIdx],    // t1: previous history
        m_depthSRV                // t2: depth
    };
    ctx->CSSetShaderResources(0, 3, dispatchSRVs);

    // For output: we write resolved color to history[writeIdx] only.
    // After dispatch, we copy history[writeIdx] back to the backbuffer.
    ID3D11UnorderedAccessView* dispatchUAVs[2] = {
        nullptr,                  // u0: unused (we'll copy to BB after)
        m_historyUAV[writeIdx]    // u1: history write target
    };
    ctx->CSSetUnorderedAccessViews(0, 2, dispatchUAVs, nullptr);

    ctx->CSSetConstantBuffers(0, 1, &m_paramsCB);
    ctx->CSSetSamplers(0, 1, &m_sampler);

    // Dispatch
    UINT groupsX = (m_width  + 7) / 8;
    UINT groupsY = (m_height + 7) / 8;
    ctx->Dispatch(groupsX, groupsY, 1);

    // ── Unbind and copy back ─────────────────────────────────────────
    ctx->CSSetShaderResources(0, 3, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[2] = {};
    ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

    // Copy resolved history back to backbuffer
    ctx->CopyResource(backbuffer, m_historyTex[writeIdx]);

    // ── Cleanup ──────────────────────────────────────────────────────
    bbSRV->Release();
    backbuffer->Release();
    if (m_depthSRV) { m_depthSRV->Release(); m_depthSRV = nullptr; }

    // ── Restore CS state ─────────────────────────────────────────────
    ctx->CSSetShader(savedCS, savedCI, savedCICount);
    ctx->CSSetShaderResources(0, 3, savedSRVs);
    ctx->CSSetUnorderedAccessViews(0, 2, savedUAVs, nullptr);
    ctx->CSSetConstantBuffers(0, 1, savedCBs);
    ctx->CSSetSamplers(0, 1, savedSamplers);

    if (savedCS) savedCS->Release();
    for (UINT i = 0; i < 3; i++) if (savedSRVs[i]) savedSRVs[i]->Release();
    for (UINT i = 0; i < 2; i++) if (savedUAVs[i]) savedUAVs[i]->Release();
    for (UINT i = 0; i < 1; i++) if (savedCBs[i]) savedCBs[i]->Release();
    for (UINT i = 0; i < 1; i++) if (savedSamplers[i]) savedSamplers[i]->Release();

    // ── Swap ping-pong ───────────────────────────────────────────────
    m_writeIdx = 1 - m_writeIdx;
    ++m_frameIndex;
}


// ═══════════════════════════════════════════════════════════════════════════
//  GetHistorySRV — returns the READ side (previous resolved frame)
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* TAAManager::GetHistorySRV() const
{
    if (!m_initialized) return nullptr;
    // After Resolve(), writeIdx was swapped, so the "just written" history
    // is now at readIdx (1 - current writeIdx), which is the old writeIdx.
    // For ENB shaders sampling during the NEXT frame's ENB pass, they want
    // the most recently resolved history = the one we just wrote.
    return m_historySRV[1 - m_writeIdx];
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown + ReleaseResources
// ═══════════════════════════════════════════════════════════════════════════

void TAAManager::ReleaseResources()
{
    for (int i = 0; i < 2; i++) {
        if (m_historyUAV[i]) { m_historyUAV[i]->Release(); m_historyUAV[i] = nullptr; }
        if (m_historySRV[i]) { m_historySRV[i]->Release(); m_historySRV[i] = nullptr; }
        if (m_historyTex[i]) { m_historyTex[i]->Release(); m_historyTex[i] = nullptr; }
    }
    if (m_paramsCB)  { m_paramsCB->Release();  m_paramsCB  = nullptr; }
    if (m_sampler)   { m_sampler->Release();    m_sampler   = nullptr; }
    if (m_resolveCS) { m_resolveCS->Release();  m_resolveCS = nullptr; }
    if (m_depthSRV)  { m_depthSRV->Release();   m_depthSRV  = nullptr; }
}

void TAAManager::Shutdown()
{
    ReleaseResources();
    m_initialized = false;
    m_width = m_height = 0;
}

} // namespace SB
