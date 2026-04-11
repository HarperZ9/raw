#include "LuminanceHistogram.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <chrono>
#include <cstring>
#include <cmath>
#include <algorithm>

#include <SKSE/SKSE.h>

// ── Embedded HLSL compute shaders ─────────────────────────────────────────

static constexpr const char* kHistogramCS = R"(
Texture2D<float4> BackBuffer : register(t0);

RWStructuredBuffer<uint> Histogram : register(u0);     // 256 bins
RWStructuredBuffer<float4> Stats   : register(u1);     // [0]=sum(rgb,lum)

groupshared uint gs_hist[256];

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    // Clear shared memory
    if (GI < 256)
        gs_hist[GI] = 0;
    GroupMemoryBarrierWithGroupSync();

    uint2 dims;
    BackBuffer.GetDimensions(dims.x, dims.y);

    if (DTid.x < dims.x && DTid.y < dims.y)
    {
        float3 color = BackBuffer[DTid.xy].rgb;
        color = clamp(color, 0.0, 64.0);

        // Rec.709 luminance
        float lum = dot(color, float3(0.2126, 0.7152, 0.0722));

        // Log-space binning: maps [~0.001, ~1024] to [0, 255]
        float logLum = log2(lum + 0.001);
        uint bin = (uint)clamp(floor((logLum + 10.0) / 20.0 * 256.0), 0.0, 255.0);

        InterlockedAdd(gs_hist[bin], 1);
    }

    GroupMemoryBarrierWithGroupSync();

    // Merge shared memory to global
    if (GI < 256)
        InterlockedAdd(Histogram[GI], gs_hist[GI]);
}
)";

static constexpr const char* kReductionCS = R"(
RWStructuredBuffer<uint> Histogram : register(u0);
RWStructuredBuffer<float4> Stats   : register(u1);

[numthreads(1, 1, 1)]
void CSReduction(uint3 DTid : SV_DispatchThreadID)
{
    uint totalPixels = 0;
    float sumLogLum = 0.0;
    uint minBin = 255, maxBin = 0;

    for (uint i = 0; i < 256; i++)
    {
        uint count = Histogram[i];
        totalPixels += count;
        if (count > 0 && i < minBin) minBin = i;
        if (count > 0 && i > maxBin) maxBin = i;

        float binCenter = (float(i) + 0.5) / 256.0 * 20.0 - 10.0;
        sumLogLum += binCenter * float(count);
    }

    float invTotal = (totalPixels > 0) ? 1.0 / float(totalPixels) : 0.0;

    // Log-average luminance
    float avgLogLum = sumLogLum * invTotal;
    float avgLum = exp2(avgLogLum);

    // Min/max luminance from bin edges
    float minLum = exp2(float(minBin) / 256.0 * 20.0 - 10.0);
    float maxLum = exp2(float(maxBin + 1) / 256.0 * 20.0 - 10.0);

    // Percentiles (prefix sum)
    uint cumulative = 0;
    float p05 = minLum, p50 = avgLum, p95 = maxLum;
    uint threshold05 = uint(float(totalPixels) * 0.05);
    uint threshold50 = uint(float(totalPixels) * 0.50);
    uint threshold95 = uint(float(totalPixels) * 0.95);
    bool found05 = false, found50 = false, found95 = false;

    for (uint j = 0; j < 256; j++)
    {
        cumulative += Histogram[j];
        float binLum = exp2((float(j) + 0.5) / 256.0 * 20.0 - 10.0);
        if (!found05 && cumulative >= threshold05) { p05 = binLum; found05 = true; }
        if (!found50 && cumulative >= threshold50) { p50 = binLum; found50 = true; }
        if (!found95 && cumulative >= threshold95) { p95 = binLum; found95 = true; }
    }

    Stats[0] = float4(avgLum, minLum, maxLum, float(totalPixels));
    Stats[1] = float4(p05, p50, p95, 0.0);
}
)";

// ── Metering compute shader ────────────────────────────────────────────────
//
//  Spatially-weighted luminance metering with groupshared parallel reduction.
//  Uses fixed-point encoding (log2(lum) * 65536 -> int) for InterlockedAdd
//  since D3D11 SM5.0 has no atomic float add.
//
//  Outputs: MeteringResult[0].x = sum of fixed-point weighted log-lum
//           MeteringResult[0].y = sum of fixed-point weights
//           MeteringResult[1].x = weighted min lum (fixed-point log2)
//           MeteringResult[1].y = weighted max lum (fixed-point log2)
//
//  CPU reduction divides sum_log_lum / sum_weight, then exp2() to get
//  geometric mean luminance for EV100 computation.

static constexpr const char* kMeteringCS = R"(
cbuffer MeteringCB : register(b0)
{
    uint2 ScreenDims;
    uint  MeteringMode;   // 0=Evaluative, 1=CenterWeighted, 2=Spot
    float Pad;
};

Texture2D<float4> BackBuffer : register(t0);

// [0].xy = (sumFixedLogLum, sumFixedWeight)  — accumulated via InterlockedAdd
// [1].xy = (minFixedLogLum, maxFixedLogLum)  — accumulated via InterlockedMin/Max
RWStructuredBuffer<uint> MeteringResult : register(u0);  // 4 uints

groupshared float gs_logLum[256];     // weighted log-luminance per thread
groupshared float gs_weight[256];     // weight per thread

// Fixed-point scale: 16.16 gives ~1e-5 precision over [-10, +10] range
static const float FP_SCALE = 65536.0;

[numthreads(16, 16, 1)]
void CSMetering(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    gs_logLum[GI] = 0.0;
    gs_weight[GI] = 0.0;
    GroupMemoryBarrierWithGroupSync();

    if (DTid.x < ScreenDims.x && DTid.y < ScreenDims.y)
    {
        float2 uv = (float2(DTid.xy) + 0.5) / float2(ScreenDims);
        float2 center = float2(0.5, 0.5);
        float dist = length(uv - center);

        // Compute spatial weight based on metering mode
        float weight = 1.0;
        if (MeteringMode == 1)  // Center-weighted: Gaussian falloff
        {
            weight = exp(-dist * dist / (2.0 * 0.3 * 0.3));
        }
        else if (MeteringMode == 2)  // Spot: center 5% circle
        {
            weight = (dist < 0.05) ? 1.0 : 0.0;
        }
        else  // Evaluative: 5-zone matrix metering
        {
            // Center zone (r < 0.2): 40% weight -> boost 2.5x
            // Mid zone   (r < 0.4): base weight 1.0x
            // Edge zone  (r >= 0.4): 15% per quadrant -> 0.5x
            if (dist < 0.2)
                weight = 2.5;
            else if (dist < 0.4)
                weight = 1.0;
            else
                weight = 0.5;
        }

        float3 color = BackBuffer[DTid.xy].rgb;
        color = clamp(color, 0.0, 64.0);

        // Rec.709 luminance, clamped to avoid log2(0)
        float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
        lum = max(lum, 0.001);

        gs_logLum[GI] = log2(lum) * weight;
        gs_weight[GI] = weight;
    }

    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction (256 threads -> 1)
    for (uint s = 128; s > 0; s >>= 1)
    {
        if (GI < s)
        {
            gs_logLum[GI] += gs_logLum[GI + s];
            gs_weight[GI] += gs_weight[GI + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Thread 0: atomically accumulate this workgroup's result into global buffer
    if (GI == 0)
    {
        // Encode as signed fixed-point offset by a large bias to keep positive
        // log2(lum) range: roughly [-10, +6] => biased to [0, 16] * 65536
        int fixedLogLum = (int)(gs_logLum[0] * FP_SCALE);
        int fixedWeight = (int)(gs_weight[0] * FP_SCALE);

        // Use InterlockedAdd on uint (reinterpreted from signed int — works
        // because two's complement addition is bit-identical for signed/unsigned)
        InterlockedAdd(MeteringResult[0], (uint)fixedLogLum);
        InterlockedAdd(MeteringResult[1], (uint)fixedWeight);
    }
}
)";

namespace SB
{
    // ── Initialization ────────────────────────────────────────────────────

    bool LuminanceHistogram::Initialize(ID3D11Device* a_device, IDXGISwapChain* a_swapChain)
    {
        if (m_initialized) return true;
        if (!a_device || !a_swapChain) return false;

        // Get backbuffer dimensions
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = a_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr) || !backBuffer) {
            SKSE::log::error("LuminanceHistogram: failed to get backbuffer");
            return false;
        }

        D3D11_TEXTURE2D_DESC bbDesc;
        backBuffer->GetDesc(&bbDesc);
        backBuffer->Release();

        if (!CompileComputeShaders(a_device)) return false;
        if (!CreateResources(a_device, bbDesc.Width, bbDesc.Height)) return false;

        m_initialized = true;
        SKSE::log::info("LuminanceHistogram: initialized ({}x{}, 256-bin histogram at t{}, metering + auto-exposure)",
            m_width, m_height, kSRVSlot);
        return true;
    }

    bool LuminanceHistogram::CompileComputeShaders(ID3D11Device* a_device)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* errBlob = nullptr;
        HRESULT hr;

        // Compile histogram CS
        hr = D3DCompile(kHistogramCS, std::strlen(kHistogramCS), "SB_HistogramCS",
            nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) {
                SKSE::log::error("LuminanceHistogram: histogram CS compile failed: {}",
                    static_cast<const char*>(errBlob->GetBufferPointer()));
                errBlob->Release();
            }
            return false;
        }

        hr = a_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
            nullptr, &m_histogramCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("LuminanceHistogram: CreateComputeShader failed for histogram");
            return false;
        }

        // Compile reduction CS
        hr = D3DCompile(kReductionCS, std::strlen(kReductionCS), "SB_ReductionCS",
            nullptr, nullptr, "CSReduction", "cs_5_0", 0, 0, &blob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) {
                SKSE::log::error("LuminanceHistogram: reduction CS compile failed: {}",
                    static_cast<const char*>(errBlob->GetBufferPointer()));
                errBlob->Release();
            }
            return false;
        }

        hr = a_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
            nullptr, &m_reductionCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("LuminanceHistogram: CreateComputeShader failed for reduction");
            return false;
        }

        // Compile metering CS
        hr = D3DCompile(kMeteringCS, std::strlen(kMeteringCS), "SB_MeteringCS",
            nullptr, nullptr, "CSMetering", "cs_5_0", 0, 0, &blob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) {
                SKSE::log::error("LuminanceHistogram: metering CS compile failed: {}",
                    static_cast<const char*>(errBlob->GetBufferPointer()));
                errBlob->Release();
            }
            return false;
        }

        hr = a_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
            nullptr, &m_meteringCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("LuminanceHistogram: CreateComputeShader failed for metering");
            return false;
        }

        return true;
    }

    bool LuminanceHistogram::CreateResources(ID3D11Device* a_device, uint32_t w, uint32_t h)
    {
        m_width = w;
        m_height = h;
        HRESULT hr;

        // Histogram structured buffer: 256 × uint
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = 256 * sizeof(uint32_t);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(uint32_t);
            hr = a_device->CreateBuffer(&desc, nullptr, &m_histogramBuffer);
            if (FAILED(hr)) return false;

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = 256;
            hr = a_device->CreateUnorderedAccessView(m_histogramBuffer, &uavDesc, &m_histogramBufUAV);
            if (FAILED(hr)) return false;
        }

        // Stats structured buffer: 4 × float4
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = 4 * sizeof(float) * 4; // 4 float4s
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(float) * 4;
            hr = a_device->CreateBuffer(&desc, nullptr, &m_statsBuffer);
            if (FAILED(hr)) return false;

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = 4;
            hr = a_device->CreateUnorderedAccessView(m_statsBuffer, &uavDesc, &m_statsBufUAV);
            if (FAILED(hr)) return false;
        }

        // Staging buffers for CPU readback
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = 256 * sizeof(uint32_t);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            hr = a_device->CreateBuffer(&desc, nullptr, &m_stagingHistogram);
            if (FAILED(hr)) return false;

            desc.ByteWidth = 4 * sizeof(float) * 4;
            hr = a_device->CreateBuffer(&desc, nullptr, &m_stagingStats);
            if (FAILED(hr)) return false;
        }

        // 256×1 R32_FLOAT texture for SRV binding to ENB shaders
        {
            D3D11_TEXTURE2D_DESC texDesc{};
            texDesc.Width = 256;
            texDesc.Height = 1;
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.Format = DXGI_FORMAT_R32_FLOAT;
            texDesc.SampleDesc.Count = 1;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
            texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            hr = a_device->CreateTexture2D(&texDesc, nullptr, &m_histogramTex);
            if (FAILED(hr)) return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            hr = a_device->CreateShaderResourceView(m_histogramTex, &srvDesc, &m_histogramSRV);
            if (FAILED(hr)) return false;
        }

        // ── Metering resources ───────────────────────────────────────────────

        // Metering constant buffer (MeteringCB: uint2 ScreenDims, uint MeteringMode, float Pad)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = 16; // uint2 + uint + float = 16 bytes (1 float4)
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = a_device->CreateBuffer(&desc, nullptr, &m_meteringCB);
            if (FAILED(hr)) return false;
        }

        // Metering result structured buffer: 4 × uint (sumLogLum, sumWeight, reserved, reserved)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = 4 * sizeof(uint32_t);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(uint32_t);
            hr = a_device->CreateBuffer(&desc, nullptr, &m_meteringBuffer);
            if (FAILED(hr)) return false;

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = 4;
            hr = a_device->CreateUnorderedAccessView(m_meteringBuffer, &uavDesc, &m_meteringBufUAV);
            if (FAILED(hr)) return false;
        }

        // Metering staging buffer for CPU readback
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = 4 * sizeof(uint32_t);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            hr = a_device->CreateBuffer(&desc, nullptr, &m_stagingMetering);
            if (FAILED(hr)) return false;
        }

        return true;
    }

    // ── Per-Frame Dispatch ────────────────────────────────────────────────

    void LuminanceHistogram::Dispatch(ID3D11DeviceContext* a_ctx, IDXGISwapChain* a_swapChain)
    {
        if (!m_initialized || !m_enabled || !a_ctx || !a_swapChain) return;

        // Get backbuffer
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = a_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr) || !backBuffer) return;

        // Create temporary SRV for backbuffer
        D3D11_TEXTURE2D_DESC bbDesc;
        backBuffer->GetDesc(&bbDesc);

        ID3D11ShaderResourceView* backbufferSRV = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = bbDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        ID3D11Device* dev = nullptr;
        a_ctx->GetDevice(&dev);
        hr = dev->CreateShaderResourceView(backBuffer, &srvDesc, &backbufferSRV);
        dev->Release();
        backBuffer->Release();

        if (FAILED(hr) || !backbufferSRV) return;

        // Save existing CS state
        ID3D11ComputeShader* prevCS = nullptr;
        ID3D11ShaderResourceView* prevSRV = nullptr;
        ID3D11UnorderedAccessView* prevUAV0 = nullptr;
        ID3D11UnorderedAccessView* prevUAV1 = nullptr;
        ID3D11Buffer* prevCB0 = nullptr;
        a_ctx->CSGetShader(&prevCS, nullptr, nullptr);
        a_ctx->CSGetShaderResources(0, 1, &prevSRV);
        a_ctx->CSGetUnorderedAccessViews(0, 1, &prevUAV0);
        a_ctx->CSGetUnorderedAccessViews(1, 1, &prevUAV1);
        a_ctx->CSGetConstantBuffers(0, 1, &prevCB0);

        // Clear histogram, stats, and metering buffers
        const UINT clearZero[4] = { 0, 0, 0, 0 };
        a_ctx->ClearUnorderedAccessViewUint(m_histogramBufUAV, clearZero);
        a_ctx->ClearUnorderedAccessViewUint(m_statsBufUAV, clearZero);
        a_ctx->ClearUnorderedAccessViewUint(m_meteringBufUAV, clearZero);

        // Pass 1: Histogram binning
        a_ctx->CSSetShader(m_histogramCS, nullptr, 0);
        a_ctx->CSSetShaderResources(0, 1, &backbufferSRV);
        ID3D11UnorderedAccessView* uavs[2] = { m_histogramBufUAV, m_statsBufUAV };
        a_ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

        uint32_t groupsX = (m_width + 15) / 16;
        uint32_t groupsY = (m_height + 15) / 16;
        a_ctx->Dispatch(groupsX, groupsY, 1);

        // Unbind SRV between passes
        ID3D11ShaderResourceView* nullSRV = nullptr;
        a_ctx->CSSetShaderResources(0, 1, &nullSRV);

        // Pass 2: Reduction
        a_ctx->CSSetShader(m_reductionCS, nullptr, 0);
        a_ctx->Dispatch(1, 1, 1);

        // Unbind histogram/stats UAVs
        ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
        a_ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

        // Pass 3: Metering — spatially-weighted luminance average
        {
            // Update metering constant buffer
            D3D11_MAPPED_SUBRESOURCE cbMapped{};
            HRESULT cbHr = a_ctx->Map(m_meteringCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped);
            if (SUCCEEDED(cbHr)) {
                struct {
                    uint32_t screenW;
                    uint32_t screenH;
                    uint32_t mode;
                    float    pad;
                } cbData;
                cbData.screenW = m_width;
                cbData.screenH = m_height;
                cbData.mode    = static_cast<uint32_t>(m_meteringMode);
                cbData.pad     = 0.0f;
                std::memcpy(cbMapped.pData, &cbData, sizeof(cbData));
                a_ctx->Unmap(m_meteringCB, 0);
            }

            a_ctx->CSSetShader(m_meteringCS, nullptr, 0);
            a_ctx->CSSetConstantBuffers(0, 1, &m_meteringCB);
            a_ctx->CSSetShaderResources(0, 1, &backbufferSRV);
            a_ctx->CSSetUnorderedAccessViews(0, 1, &m_meteringBufUAV, nullptr);

            a_ctx->Dispatch(groupsX, groupsY, 1);

            // Unbind metering resources
            ID3D11UnorderedAccessView* nullUAV = nullptr;
            a_ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
            a_ctx->CSSetShaderResources(0, 1, &nullSRV);
        }

        a_ctx->CSSetShader(nullptr, nullptr, 0);

        // Copy to staging for readback next frame
        a_ctx->CopyResource(m_stagingHistogram, m_histogramBuffer);
        a_ctx->CopyResource(m_stagingStats, m_statsBuffer);
        a_ctx->CopyResource(m_stagingMetering, m_meteringBuffer);
        m_pendingReadback = true;

        // Restore CS state
        a_ctx->CSSetShader(prevCS, nullptr, 0);
        a_ctx->CSSetShaderResources(0, 1, &prevSRV);
        a_ctx->CSSetUnorderedAccessViews(0, 1, &prevUAV0, nullptr);
        if (prevUAV1) a_ctx->CSSetUnorderedAccessViews(1, 1, &prevUAV1, nullptr);
        a_ctx->CSSetConstantBuffers(0, 1, &prevCB0);
        if (prevCS) prevCS->Release();
        if (prevSRV) prevSRV->Release();
        if (prevUAV0) prevUAV0->Release();
        if (prevUAV1) prevUAV1->Release();
        if (prevCB0) prevCB0->Release();

        backbufferSRV->Release();
    }

    // ── Readback ──────────────────────────────────────────────────────────

    void LuminanceHistogram::ReadBack(ID3D11DeviceContext* a_ctx)
    {
        if (!m_initialized || !m_enabled || !a_ctx || !m_pendingReadback) return;

        auto& result = m_result[m_writeIndex];

        // Read histogram
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = a_ctx->Map(m_stagingHistogram, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            uint32_t rawBins[256];
            std::memcpy(rawBins, mapped.pData, sizeof(rawBins));
            a_ctx->Unmap(m_stagingHistogram, 0);

            // Normalize
            uint32_t total = 0;
            for (int i = 0; i < 256; i++) total += rawBins[i];
            float invTotal = (total > 0) ? 1.0f / static_cast<float>(total) : 0.f;
            for (int i = 0; i < 256; i++)
                result.bins[i] = static_cast<float>(rawBins[i]) * invTotal;

            // Update histogram texture for SRV
            // Convert normalized bins to float array and upload
            float floatBins[256];
            for (int i = 0; i < 256; i++)
                floatBins[i] = result.bins[i];

            a_ctx->UpdateSubresource(m_histogramTex, 0, nullptr, floatBins,
                256 * sizeof(float), 256 * sizeof(float));
        }

        // Read stats
        hr = a_ctx->Map(m_stagingStats, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            float stats[8]; // 2 × float4
            std::memcpy(stats, mapped.pData, sizeof(stats));
            a_ctx->Unmap(m_stagingStats, 0);

            result.avgLuminance = stats[0];
            result.minLuminance = stats[1];
            result.maxLuminance = stats[2];
            // stats[3] = totalPixels
            result.p05 = stats[4];
            result.p50 = stats[5];
            result.p95 = stats[6];
        }

        // ── Read metering results + compute auto-exposure ────────────────────

        // Compute delta time for temporal adaptation
        auto now = std::chrono::high_resolution_clock::now();
        float dt = 0.016f; // default 60fps
        if (m_hasLastReadbackTime) {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                now - m_lastReadbackTime);
            dt = elapsed.count() / 1'000'000.0f;
            if (dt < 0.0001f) dt = 0.0001f;
            if (dt > 0.5f) dt = 0.5f;
        }
        m_lastReadbackTime = now;
        m_hasLastReadbackTime = true;

        hr = a_ctx->Map(m_stagingMetering, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            uint32_t meteringRaw[4];
            std::memcpy(meteringRaw, mapped.pData, sizeof(meteringRaw));
            a_ctx->Unmap(m_stagingMetering, 0);

            // Decode fixed-point: sumLogLum and sumWeight were encoded as
            // int(value * 65536) then InterlockedAdd'd as uint (two's complement safe)
            constexpr float FP_INV = 1.0f / 65536.0f;
            float sumLogLum = static_cast<float>(static_cast<int32_t>(meteringRaw[0])) * FP_INV;
            float sumWeight = static_cast<float>(static_cast<int32_t>(meteringRaw[1])) * FP_INV;

            // Compute geometric-mean luminance from weighted log-average
            float avgLogLum = (sumWeight > 0.001f) ? (sumLogLum / sumWeight) : 0.0f;
            float avgLum = std::exp2(avgLogLum);

            // Avoid degenerate values
            avgLum = std::clamp(avgLum, 0.001f, 100000.0f);

            // EV100 from average luminance (ISO 100 standard)
            // EV100 = log2(L * S / K) where S=100 (ISO), K=12.5 (calibration)
            float targetEV = std::log2(avgLum * 100.0f / 12.5f);

            // Apply exposure compensation
            targetEV += m_exposureComp;

            // Clamp to configured EV range
            targetEV = std::clamp(targetEV, m_minEV, m_maxEV);

            // Temporal smoothing: exponential approach to target
            // rate = 1 - exp(-adaptSpeed * dt) gives frame-rate-independent lerp
            float rate = 1.0f - std::exp(-m_adaptSpeed * dt);
            m_currentEV = m_currentEV + (targetEV - m_currentEV) * rate;

            // Linear exposure multiplier from EV100
            // exposure = 1 / (1.2 * 2^EV100)
            m_currentExposure = 1.0f / (1.2f * std::exp2(m_currentEV));

            // Store in result
            result.exposureEV = m_currentEV;
            result.targetExposure = m_currentExposure;
        }

        m_writeIndex = 1 - m_writeIndex;
        m_hasData = true;
        m_pendingReadback = false;
    }

    // ── Shutdown ──────────────────────────────────────────────────────────

    void LuminanceHistogram::Shutdown()
    {
        ReleaseResources();
        m_initialized = false;
    }

    void LuminanceHistogram::ReleaseResources()
    {
        if (m_histogramCS)      { m_histogramCS->Release();      m_histogramCS = nullptr; }
        if (m_reductionCS)      { m_reductionCS->Release();      m_reductionCS = nullptr; }
        if (m_meteringCS)       { m_meteringCS->Release();        m_meteringCS = nullptr; }
        if (m_histogramBuffer)  { m_histogramBuffer->Release();  m_histogramBuffer = nullptr; }
        if (m_statsBuffer)      { m_statsBuffer->Release();      m_statsBuffer = nullptr; }
        if (m_stagingHistogram) { m_stagingHistogram->Release();  m_stagingHistogram = nullptr; }
        if (m_stagingStats)     { m_stagingStats->Release();      m_stagingStats = nullptr; }
        if (m_histogramTex)     { m_histogramTex->Release();      m_histogramTex = nullptr; }
        if (m_histogramSRV)     { m_histogramSRV->Release();      m_histogramSRV = nullptr; }
        if (m_histogramBufUAV)  { m_histogramBufUAV->Release();   m_histogramBufUAV = nullptr; }
        if (m_statsBufUAV)      { m_statsBufUAV->Release();       m_statsBufUAV = nullptr; }
        if (m_meteringCB)       { m_meteringCB->Release();        m_meteringCB = nullptr; }
        if (m_meteringBuffer)   { m_meteringBuffer->Release();    m_meteringBuffer = nullptr; }
        if (m_stagingMetering)  { m_stagingMetering->Release();   m_stagingMetering = nullptr; }
        if (m_meteringBufUAV)   { m_meteringBufUAV->Release();    m_meteringBufUAV = nullptr; }

        m_hasLastReadbackTime = false;
    }

} // namespace SB
