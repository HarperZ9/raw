//=============================================================================
//  SDSMCascades.cpp — Sample Distribution Shadow Maps
//
//  Per-frame flow:
//    1. Dispatch histogram CS: 256-bin depth histogram from Hi-Z mip 0
//    2. Copy histogram → staging[writeIdx]
//    3. Map staging[readIdx] → compute cascade splits from depth CDF
//    4. Apply splits to game settings (temporally smoothed)
//=============================================================================

#include "SDSMCascades.h"
#include "ShaderLoader.h"
#include "HiZPyramid.h"
#include "ComputeManager.h"
#include "D3D11Hook.h"
#include "SceneData.h"
#include "SharedGPUResources.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "GPUResource.h"

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Depth Histogram CS
// ═══════════════════════════════════════════════════════════════════════════

static const char kHistogramCS[] = R"HLSL(
// SDSM depth histogram — 256-bin logarithmic depth distribution
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reference: Lauritzen et al. 2011, "Sample Distribution Shadow Maps"
//
// Reads pre-linearized depth (t31, view-space Z) and bins each pixel
// into a 256-bin logarithmic histogram covering [NearZ .. MaxDistance].
// Uses groupshared memory for per-group local histogram, then atomically
// merges into the global histogram to reduce contention.
//
// Sky pixels (linearDepth <= 0) are skipped.
// Pixels beyond MaxDistance are clamped to the last bin.

cbuffer HistogramCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  MaxDistance;     // Max shadow distance for histogram range
    float3 pad0;
}

Texture2D<float> DepthTex : register(t0);
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth
RWStructuredBuffer<uint> Histogram : register(u0);  // 256 bins

#define NUM_BINS      256
#define GROUP_SIZE_X  16
#define GROUP_SIZE_Y  16
#define GROUP_SIZE    (GROUP_SIZE_X * GROUP_SIZE_Y)

groupshared uint localHistogram[NUM_BINS];

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    // ── Initialize local histogram ───────────────────────────────────
    // Each thread clears one or more bins
    for (uint i = GI; i < NUM_BINS; i += GROUP_SIZE)
        localHistogram[i] = 0;

    GroupMemoryBarrierWithGroupSync();

    // ── Accumulate this pixel into local histogram ───────────────────
    if (all(DTid.xy < ScreenDims))
    {
        float linZ = LinearDepth.Load(int3(DTid.xy, 0));

        // Skip sky pixels and invalid depth
        if (linZ > 0.0)
        {
            // Clamp to [NearZ, MaxDistance] range
            float clampedNear = max(NearZ, 1.0);
            float z = clamp(linZ, clampedNear, MaxDistance);

            // Logarithmic binning: bin = (log2(z) - log2(near)) / (log2(max) - log2(near)) * NUM_BINS
            float logNear = log2(clampedNear);
            float logMax  = log2(MaxDistance);
            float logRange = logMax - logNear;

            uint bin;
            if (logRange > 0.0)
            {
                float t = (log2(z) - logNear) / logRange;
                bin = min(uint(t * (float)NUM_BINS), NUM_BINS - 1);
            }
            else
            {
                bin = 0;
            }

            InterlockedAdd(localHistogram[bin], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // ── Merge local histogram into global buffer ─────────────────────
    for (uint j = GI; j < NUM_BINS; j += GROUP_SIZE)
    {
        if (localHistogram[j] > 0)
            InterlockedAdd(Histogram[j], localHistogram[j]);
    }
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structure
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) HistogramCBData
{
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    maxDistance;
    float    pad[3];
};
static_assert(sizeof(HistogramCBData) == 32, "HistogramCB must be 32 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool SDSMCascades::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) return false;

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

    m_initialized = true;
    m_readbackReady = false;
    m_frameIndex = 0;

    SKSE::log::info("SDSMCascades: initialized ({}x{}, {} bins, distance {:.0f}-{:.0f})",
                    m_screenW, m_screenH, kHistogramBins, m_minDistance, m_maxDistance);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile shaders
// ═══════════════════════════════════════════════════════════════════════════

bool SDSMCascades::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = ShaderLoader::Compile("SDSM_Histogram", kHistogramCS, "main", "cs_5_0", flags);
    if (!blob) {
        SKSE::log::error("SDSMCascades: histogram CS compile failed");
        return false;
    }

    HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                        blob->GetBufferSize(),
                                        nullptr, &m_histogramCS);
    blob->Release();
    if (FAILED(hr)) return false;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool SDSMCascades::CreateResources()
{
    HRESULT hr;

    // Histogram buffer (256 x uint32)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth           = kHistogramBins * sizeof(uint32_t);
        desc.Usage               = D3D11_USAGE_DEFAULT;
        desc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(uint32_t);

        hr = m_device->CreateBuffer(&desc, nullptr, &m_histogramBuf);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = kHistogramBins;
        hr = m_device->CreateUnorderedAccessView(m_histogramBuf, &uavDesc, &m_histogramUAV);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements  = kHistogramBins;
        hr = m_device->CreateShaderResourceView(m_histogramBuf, &srvDesc, &m_histogramSRV);
        if (FAILED(hr)) return false;
    }

    // Staging buffers (double-buffered readback)
    for (int i = 0; i < 2; i++) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = kHistogramBins * sizeof(uint32_t);
        desc.Usage           = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags  = D3D11_CPU_ACCESS_READ;

        hr = m_device->CreateBuffer(&desc, nullptr, &m_histogramStaging[i]);
        if (FAILED(hr)) return false;
    }

    // Constant buffer
    if (!CreateCB(m_device, sizeof(HistogramCBData), &m_histogramCB)) return false;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame update
// ═══════════════════════════════════════════════════════════════════════════

void SDSMCascades::Update(ID3D11DeviceContext* ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& cm = ComputeManager::Get();
    auto& scene = SceneMatrices::Get();

    // Get depth SRV (prefer Hi-Z)
    auto& hiz = HiZPyramid::Get();
    ID3D11ShaderResourceView* depthSRV = nullptr;
    if (hiz.IsInitialized() && hiz.GetSRV()) {
        depthSRV = hiz.GetSRV();
    }
    if (!depthSRV) return;

    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    int writeIdx = m_readbackIdx;
    int readIdx  = 1 - m_readbackIdx;

    cm.SaveCSState();

    // Bind pre-computed linearized depth at t31
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ── Dispatch histogram ──────────────────────────────────────────────
    {
        // Clear histogram
        UINT clearVal[4] = {0, 0, 0, 0};
        ctx->ClearUnorderedAccessViewUint(m_histogramUAV, clearVal);

        // Update CB
        HistogramCBData cb = {};
        cb.screenW     = m_screenW;
        cb.screenH     = m_screenH;
        cb.nearZ       = nearZ;
        cb.farZ        = farZ;
        cb.maxDistance  = m_maxDistance;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx->Map(m_histogramCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx->Unmap(m_histogramCB, 0);
        }

        ctx->CSSetShaderResources(0, 1, &depthSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, &m_histogramUAV, nullptr);
        ctx->CSSetConstantBuffers(0, 1, &m_histogramCB);
        ctx->CSSetShader(m_histogramCS, nullptr, 0);

        UINT groupsX = (m_screenW + 15) / 16;
        UINT groupsY = (m_screenH + 15) / 16;
        ctx->Dispatch(groupsX, groupsY, 1);

        // Clear bindings
        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // Unbind linearized depth t31
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // ── Copy histogram to staging ───────────────────────────────────────
    ctx->CopyResource(m_histogramStaging[writeIdx], m_histogramBuf);

    // ── Read back previous frame's histogram ────────────────────────────
    if (m_readbackReady) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = ctx->Map(m_histogramStaging[readIdx], 0,
                               D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (SUCCEEDED(hr)) {
            ComputeSplitsFromHistogram(
                static_cast<const uint32_t*>(mapped.pData), kHistogramBins);
            ctx->Unmap(m_histogramStaging[readIdx], 0);

            ApplySplitsToGame();
        }
    } else {
        m_readbackReady = true;
    }

    m_readbackIdx = readIdx;
    m_frameIndex++;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compute cascade splits from histogram CDF
// ═══════════════════════════════════════════════════════════════════════════

void SDSMCascades::ComputeSplitsFromHistogram(const uint32_t* histogram, uint32_t binCount)
{
    auto& scene = SceneMatrices::Get();
    const float nearZ = scene.NearClip();

    // Build CDF (prefix sum)
    uint32_t totalSamples = 0;
    for (uint32_t i = 0; i < binCount; i++)
        totalSamples += histogram[i];

    if (totalSamples == 0) {
        m_splits.valid = false;
        return;
    }

    // Convert bin index to depth distance
    auto BinToDepth = [&](float normalizedBin) -> float {
        float clampedNear = nearZ > 1.0f ? nearZ : 1.0f;
        float logNear = std::log2(clampedNear);
        float logMax  = std::log2(m_maxDistance);
        float logDepth = logNear + normalizedBin * (logMax - logNear);
        return std::pow(2.0f, logDepth);
    };

    // Find percentile depths
    uint32_t cumulative = 0;
    float p50Bin = 0, p90Bin = 0, p99Bin = 0;
    bool found50 = false, found90 = false, found99 = false;

    for (uint32_t i = 0; i < binCount; i++) {
        cumulative += histogram[i];
        float percentile = static_cast<float>(cumulative) / totalSamples;

        if (!found50 && percentile >= 0.50f) { p50Bin = (i + 0.5f) / binCount; found50 = true; }
        if (!found90 && percentile >= 0.90f) { p90Bin = (i + 0.5f) / binCount; found90 = true; }
        if (!found99 && percentile >= 0.99f) { p99Bin = (i + 0.5f) / binCount; found99 = true; }
    }

    m_medianDepth = BinToDepth(p50Bin);
    m_p90Depth    = BinToDepth(p90Bin);
    m_p99Depth    = BinToDepth(p99Bin);

    // Compute 4 cascade splits using CDF-based partitioning:
    // Each cascade should cover approximately 25% of the depth samples.
    // This concentrates resolution where there's actual geometry.
    float splitPercentiles[4] = {0.0f, 0.25f, 0.50f, 0.80f};
    float splitDepths[4];
    uint32_t cumIdx = 0;
    uint32_t cumCount = 0;

    for (int s = 0; s < 4; s++) {
        uint32_t targetCount = static_cast<uint32_t>(splitPercentiles[s] * totalSamples);
        while (cumIdx < binCount && cumCount < targetCount) {
            cumCount += histogram[cumIdx];
            cumIdx++;
        }
        float normalizedBin = (cumIdx + 0.5f) / binCount;
        splitDepths[s] = BinToDepth(normalizedBin);
    }

    // Temporal smoothing (lerp toward new values)
    constexpr float kSmoothFactor = 0.1f;
    if (m_splits.valid) {
        for (int i = 0; i < 4; i++) {
            m_splits.splits[i] = m_splits.splits[i] + kSmoothFactor * (splitDepths[i] - m_splits.splits[i]);
        }
        float newMax = m_p99Depth * 1.1f;
        if (newMax > m_maxDistance) newMax = m_maxDistance;
        if (newMax < m_minDistance) newMax = m_minDistance;
        m_splits.maxDistance = m_splits.maxDistance + kSmoothFactor * (newMax - m_splits.maxDistance);
    } else {
        for (int i = 0; i < 4; i++)
            m_splits.splits[i] = splitDepths[i];
        float clampedMax = m_p99Depth * 1.1f;
        if (clampedMax < m_minDistance) clampedMax = m_minDistance;
        if (clampedMax > m_maxDistance) clampedMax = m_maxDistance;
        m_splits.maxDistance = clampedMax;
    }

    m_splits.valid = true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Apply splits to game shadow settings
// ═══════════════════════════════════════════════════════════════════════════

void SDSMCascades::ApplySplitsToGame()
{
    if (!m_splits.valid) return;

    auto* ini = RE::INISettingCollection::GetSingleton();
    if (!ini) return;

    // fShadowDistance: total shadow render distance
    auto* shadowDist = ini->GetSetting("fShadowDistance:Display");
    if (shadowDist) {
        shadowDist->data.f = m_splits.maxDistance;
    }

    // Note: Skyrim doesn't expose per-cascade split ratios directly.
    // The game uses a hardcoded logarithmic/linear blend for cascade
    // partitioning. The most impactful thing we can do is dynamically
    // adjust fShadowDistance to match the actual scene depth range.
    //
    // If RE discovery reveals writable cascade split addresses in
    // BSShadowDirectionalLight, we can patch those directly for
    // true per-cascade SDSM splits. For now, dynamic shadow distance
    // already provides significant quality improvement by preventing
    // shadow map resolution waste on empty far-field areas.
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void SDSMCascades::Shutdown()
{
    if (!m_initialized) return;
    ReleaseResources();
    m_initialized = false;
    SKSE::log::info("SDSMCascades: shut down");
}

void SDSMCascades::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_histogramCS);
    SafeRelease(m_histogramBuf);
    SafeRelease(m_histogramUAV);
    SafeRelease(m_histogramSRV);
    SafeRelease(m_histogramCB);
    SafeRelease(m_histogramStaging[0]);
    SafeRelease(m_histogramStaging[1]);
    SafeRelease(m_depthSRV);
}

} // namespace SB
