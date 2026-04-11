#include "FeedbackProcessor.h"
#include "ENBInterface.h"

#include <d3d11.h>
#include <dxgi.h>
#include <SKSE/SKSE.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <string>

namespace SB
{
    // ── 5x5 grid constants ───────────────────────────────────────────────────
    static constexpr int kGridSize    = 5;
    static constexpr int kGridSamples = kGridSize * kGridSize;  // 25
    static constexpr int kGridCenter  = 12;  // row 2, col 2 in 5x5

    // Sample positions: 10%, 30%, 50%, 70%, 90% of each dimension
    static constexpr float kGridFractions[kGridSize] = { 0.10f, 0.30f, 0.50f, 0.70f, 0.90f };

    // Gaussian center-weighting kernel (sigma ~= 1.0, pre-normalized, sum = 1.0)
    static constexpr float kGaussWeight[kGridSamples] = {
        0.003f, 0.013f, 0.022f, 0.013f, 0.003f,  // row 0 (y=10%)
        0.013f, 0.060f, 0.098f, 0.060f, 0.013f,  // row 1 (y=30%)
        0.022f, 0.098f, 0.162f, 0.098f, 0.022f,  // row 2 (y=50%) — center
        0.013f, 0.060f, 0.098f, 0.060f, 0.013f,  // row 3 (y=70%)
        0.003f, 0.013f, 0.022f, 0.013f, 0.003f,  // row 4 (y=90%)
    };

    // ── Histogram bin thresholds ─────────────────────────────────────────────
    static constexpr float kHistShadow = 0.05f;   // shadows < 0.05
    static constexpr float kHistDark   = 0.18f;   // darks < 0.18 (middle gray)
    static constexpr float kHistMid    = 0.50f;   // mids < 0.50

    FeedbackProcessor& FeedbackProcessor::Get()
    {
        static FeedbackProcessor instance;
        return instance;
    }

    bool FeedbackProcessor::Initialize(ID3D11Device* device, IDXGISwapChain* swapChain)
    {
        if (m_initialized)
            return true;

        if (!device || !swapChain)
            return false;

        // Get backbuffer desc to determine format and dimensions
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&backBuffer));
        if (FAILED(hr) || !backBuffer) {
            SKSE::log::error("FeedbackProcessor: failed to get backbuffer (hr=0x{:X})",
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_TEXTURE2D_DESC bbDesc;
        backBuffer->GetDesc(&bbDesc);
        backBuffer->Release();

        m_backbufferWidth = bbDesc.Width;
        m_backbufferHeight = bbDesc.Height;
        m_backbufferFormat = static_cast<int>(bbDesc.Format);

        // Skip if backbuffer is multisampled (rare for swap chains)
        if (bbDesc.SampleDesc.Count > 1) {
            SKSE::log::warn("FeedbackProcessor: backbuffer is multisampled ({}x) — skipping",
                bbDesc.SampleDesc.Count);
            return false;
        }

        // Create 1x1 staging texture for center-pixel readback
        D3D11_TEXTURE2D_DESC stagingDesc{};
        stagingDesc.Width = 1;
        stagingDesc.Height = 1;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = bbDesc.Format;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.BindFlags = 0;
        stagingDesc.MiscFlags = 0;

        hr = device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
        if (FAILED(hr)) {
            SKSE::log::error("FeedbackProcessor: failed to create 1x1 staging texture (hr=0x{:X})",
                static_cast<uint32_t>(hr));
            return false;
        }

        // Create 25x1 staging texture for 5x5 grid sampling
        D3D11_TEXTURE2D_DESC gridDesc = stagingDesc;
        gridDesc.Width = kGridSamples;
        gridDesc.Height = 1;

        hr = device->CreateTexture2D(&gridDesc, nullptr, &m_gridStagingTexture);
        if (FAILED(hr)) {
            SKSE::log::error("FeedbackProcessor: failed to create {}x1 staging texture (hr=0x{:X})",
                kGridSamples, static_cast<uint32_t>(hr));
            // Non-fatal: center pixel still works
            m_gridStagingTexture = nullptr;
        }

        m_initialized = true;
        SKSE::log::info("FeedbackProcessor: initialized ({}x{}, format={}, grid={}x{})",
            m_backbufferWidth, m_backbufferHeight, m_backbufferFormat,
            m_gridStagingTexture ? kGridSize : 0,
            m_gridStagingTexture ? kGridSize : 0);
        return true;
    }

    // ── Half-float to float conversion ──────────────────────────────────────
    static float HalfToFloat(uint16_t h)
    {
        uint32_t sign = (h & 0x8000u) << 16;
        uint32_t exponent = (h >> 10) & 0x1Fu;
        uint32_t mantissa = h & 0x3FFu;

        if (exponent == 0) {
            if (mantissa == 0) {
                float f;
                std::memcpy(&f, &sign, 4);
                return f;
            }
            while (!(mantissa & 0x400u)) {
                mantissa <<= 1;
                exponent--;
            }
            exponent++;
            mantissa &= ~0x400u;
        } else if (exponent == 31) {
            uint32_t result = sign | 0x7F800000u | (mantissa << 13);
            float f;
            std::memcpy(&f, &result, 4);
            return f;
        }

        exponent = exponent + (127 - 15);
        mantissa <<= 13;

        uint32_t result = sign | (exponent << 23) | mantissa;
        float f;
        std::memcpy(&f, &result, 4);
        return f;
    }

    // ── Decode a single pixel from mapped staging texture data ──────────────
    static void DecodePixel(const void* pData, UINT rowPitch, int columnIndex,
                            DXGI_FORMAT format, float& r, float& g, float& b)
    {
        r = g = b = 0.0f;

        if (format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            auto* pixel = static_cast<const uint8_t*>(pData) + columnIndex * 4;
            r = pixel[0] / 255.0f;
            g = pixel[1] / 255.0f;
            b = pixel[2] / 255.0f;
        }
        else if (format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                 format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        {
            auto* pixel = static_cast<const uint8_t*>(pData) + columnIndex * 4;
            b = pixel[0] / 255.0f;
            g = pixel[1] / 255.0f;
            r = pixel[2] / 255.0f;
        }
        else if (format == DXGI_FORMAT_R10G10B10A2_UNORM)
        {
            auto val = *(static_cast<const uint32_t*>(pData) + columnIndex);
            r = (val & 0x3FFu) / 1023.0f;
            g = ((val >> 10) & 0x3FFu) / 1023.0f;
            b = ((val >> 20) & 0x3FFu) / 1023.0f;
        }
        else if (format == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            auto* pixel = static_cast<const uint16_t*>(pData) + columnIndex * 4;
            r = HalfToFloat(pixel[0]);
            g = HalfToFloat(pixel[1]);
            b = HalfToFloat(pixel[2]);
        }
    }

    // ── Estimate correlated color temperature from linear RGB ────────────────
    static float EstimateColorTemp(float r, float g, float b)
    {
        float X = 0.4124f * r + 0.3576f * g + 0.1805f * b;
        float Y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        float Z = 0.0193f * r + 0.1192f * g + 0.9505f * b;

        float sum = X + Y + Z;
        if (sum < 1e-6f)
            return 6500.0f;

        float cx = X / sum;
        float cy = Y / sum;

        float denom = 0.1858f - cy;
        if (std::abs(denom) < 1e-6f)
            return 6500.0f;

        float n = (cx - 0.3320f) / denom;
        float cct = 449.0f * n * n * n + 3525.0f * n * n + 6823.3f * n + 5520.33f;

        return std::clamp(cct, 1000.0f, 40000.0f);
    }

    void FeedbackProcessor::CollectFeedback(ID3D11DeviceContext* context,
                                            IDXGISwapChain* swapChain)
    {
        if (!m_initialized || !context || !swapChain)
            return;

        auto& fb = m_feedback[m_writeIndex];
        fb = {};  // clear

        // ── Get backbuffer ───────────────────────────────────────────────────
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&backBuffer));
        if (FAILED(hr) || !backBuffer)
            return;

        // ── Detect backbuffer resolution/format change (alt-tab, fullscreen toggle) ──
        D3D11_TEXTURE2D_DESC bbDesc;
        backBuffer->GetDesc(&bbDesc);
        if (bbDesc.Width != m_backbufferWidth || bbDesc.Height != m_backbufferHeight ||
            static_cast<int>(bbDesc.Format) != m_backbufferFormat)
        {
            SKSE::log::info("FeedbackProcessor: backbuffer changed {}x{} fmt={} -> {}x{} fmt={}",
                m_backbufferWidth, m_backbufferHeight, m_backbufferFormat,
                bbDesc.Width, bbDesc.Height, static_cast<int>(bbDesc.Format));
            m_backbufferWidth  = bbDesc.Width;
            m_backbufferHeight = bbDesc.Height;
            m_backbufferFormat = static_cast<int>(bbDesc.Format);

            // Recreate staging textures with new format
            ID3D11Device* device = nullptr;
            context->GetDevice(&device);
            if (device) {
                if (m_stagingTexture) { m_stagingTexture->Release(); m_stagingTexture = nullptr; }
                if (m_gridStagingTexture) { m_gridStagingTexture->Release(); m_gridStagingTexture = nullptr; }

                D3D11_TEXTURE2D_DESC stagingDesc{};
                stagingDesc.Width = 1;
                stagingDesc.Height = 1;
                stagingDesc.MipLevels = 1;
                stagingDesc.ArraySize = 1;
                stagingDesc.Format = bbDesc.Format;
                stagingDesc.SampleDesc.Count = 1;
                stagingDesc.Usage = D3D11_USAGE_STAGING;
                stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

                device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);

                D3D11_TEXTURE2D_DESC gridDesc = stagingDesc;
                gridDesc.Width = kGridSamples;
                device->CreateTexture2D(&gridDesc, nullptr, &m_gridStagingTexture);

                device->Release();
            }

            if (!m_stagingTexture) {
                backBuffer->Release();
                return;
            }
        }

        auto format = static_cast<DXGI_FORMAT>(m_backbufferFormat);

        // ── 1. Center pixel readback ─────────────────────────────────────────
        D3D11_BOX srcBox;
        srcBox.left   = m_backbufferWidth / 2;
        srcBox.top    = m_backbufferHeight / 2;
        srcBox.right  = srcBox.left + 1;
        srcBox.bottom = srcBox.top + 1;
        srcBox.front  = 0;
        srcBox.back   = 1;

        context->CopySubresourceRegion(
            m_stagingTexture, 0,
            0, 0, 0,
            backBuffer, 0,
            &srcBox
        );

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = context->Map(m_stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            backBuffer->Release();
            return;
        }

        float r = 0.f, g = 0.f, b = 0.f;
        DecodePixel(mapped.pData, mapped.RowPitch, 0, format, r, g, b);
        context->Unmap(m_stagingTexture, 0);

        // Compute center luminance (Rec.709)
        float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        luminance = (std::min)(luminance, 100.0f);

        // Temporal smoothing via EMA
        if (!m_emaInitialized) {
            m_emaLuminance = luminance;
            m_emaInitialized = true;
        } else {
            m_emaLuminance += m_config.centerLumAlpha * (luminance - m_emaLuminance);
        }

        // Pack center pixel into feedback
        fb.Luminance.x = m_emaLuminance;
        fb.Luminance.y = luminance;
        fb.Luminance.z = r;
        fb.Luminance.w = g;
        fb.Scene.x = b;
        fb.Scene.w = 1.0f;  // feedback valid

        // ── 2. 5x5 grid sampling ────────────────────────────────────────────
        if (m_gridStagingTexture) {
            // Compute grid positions from fractions
            uint32_t gridX[kGridSize], gridY[kGridSize];
            for (int i = 0; i < kGridSize; ++i) {
                gridX[i] = static_cast<uint32_t>(m_backbufferWidth * kGridFractions[i]);
                gridY[i] = static_cast<uint32_t>(m_backbufferHeight * kGridFractions[i]);
            }

            // Copy 25 pixels into the 25x1 staging texture
            for (int gy = 0; gy < kGridSize; ++gy) {
                for (int gx = 0; gx < kGridSize; ++gx) {
                    int idx = gy * kGridSize + gx;
                    D3D11_BOX gridBox;
                    gridBox.left   = gridX[gx];
                    gridBox.top    = gridY[gy];
                    gridBox.right  = gridBox.left + 1;
                    gridBox.bottom = gridBox.top + 1;
                    gridBox.front  = 0;
                    gridBox.back   = 1;

                    context->CopySubresourceRegion(
                        m_gridStagingTexture, 0,
                        idx, 0, 0,
                        backBuffer, 0,
                        &gridBox
                    );
                }
            }

            // Map and decode all 25 pixels
            D3D11_MAPPED_SUBRESOURCE gridMapped;
            hr = context->Map(m_gridStagingTexture, 0, D3D11_MAP_READ, 0, &gridMapped);
            if (SUCCEEDED(hr)) {
                float samples[kGridSamples][3]; // [pixel][r,g,b]
                float lumSamples[kGridSamples];

                for (int i = 0; i < kGridSamples; ++i) {
                    DecodePixel(gridMapped.pData, gridMapped.RowPitch, i, format,
                                samples[i][0], samples[i][1], samples[i][2]);
                    lumSamples[i] = 0.2126f * samples[i][0]
                                  + 0.7152f * samples[i][1]
                                  + 0.0722f * samples[i][2];
                    lumSamples[i] = (std::min)(lumSamples[i], 100.0f);
                }

                context->Unmap(m_gridStagingTexture, 0);

                // ── Scene statistics (Gaussian-weighted) ─────────────────────
                float weightedLum = 0.0f;
                float weightedR = 0.0f, weightedG = 0.0f, weightedB = 0.0f;
                float minLum = lumSamples[0];
                float maxLum = lumSamples[0];
                float logSum = 0.0f;

                for (int i = 0; i < kGridSamples; ++i) {
                    weightedLum += lumSamples[i] * kGaussWeight[i];
                    weightedR   += samples[i][0] * kGaussWeight[i];
                    weightedG   += samples[i][1] * kGaussWeight[i];
                    weightedB   += samples[i][2] * kGaussWeight[i];
                    minLum = (std::min)(minLum, lumSamples[i]);
                    maxLum = (std::max)(maxLum, lumSamples[i]);
                    logSum += std::log(lumSamples[i] + 0.001f);
                }

                float lumRange = maxLum - minLum;

                // Key value (log-average luminance, Reinhard-style)
                float keyValue = std::exp(logSum / static_cast<float>(kGridSamples));

                // Contrast ratio
                float contrastRatio = maxLum / (std::max)(minLum, 0.001f);
                contrastRatio = (std::min)(contrastRatio, 10000.0f);

                // Periphery average: all samples except center (index 12)
                float peripheryLum = 0.0f;
                for (int i = 0; i < kGridSamples; ++i) {
                    if (i != kGridCenter)
                        peripheryLum += lumSamples[i];
                }
                peripheryLum /= static_cast<float>(kGridSamples - 1);

                float centerPeripheryRatio = lumSamples[kGridCenter] / (std::max)(peripheryLum, 0.001f);
                centerPeripheryRatio = (std::min)(centerPeripheryRatio, 100.0f);

                // Color temperature from weighted average color
                float colorTemp = EstimateColorTemp(weightedR, weightedG, weightedB);

                // ── 4-bin luminance histogram (unweighted counts) ────────────
                int histBins[4] = { 0, 0, 0, 0 };
                for (int i = 0; i < kGridSamples; ++i) {
                    float L = lumSamples[i];
                    if      (L < kHistShadow) histBins[0]++;
                    else if (L < kHistDark)   histBins[1]++;
                    else if (L < kHistMid)    histBins[2]++;
                    else                      histBins[3]++;
                }

                constexpr float invSamples = 1.0f / static_cast<float>(kGridSamples);
                fb.Histogram.x = histBins[0] * invSamples;
                fb.Histogram.y = histBins[1] * invSamples;
                fb.Histogram.z = histBins[2] * invSamples;
                fb.Histogram.w = histBins[3] * invSamples;

                // ── EMA smooth scene metrics ─────────────────────────────────
                if (!m_emaInitialized) {
                    m_emaSceneAvgLum = weightedLum;
                    m_emaSceneAvgR = weightedR;
                    m_emaSceneAvgG = weightedG;
                    m_emaSceneAvgB = weightedB;
                } else {
                    m_emaSceneAvgLum += m_config.sceneAvgAlpha * (weightedLum - m_emaSceneAvgLum);
                    m_emaSceneAvgR += m_config.sceneColorAlpha * (weightedR - m_emaSceneAvgR);
                    m_emaSceneAvgG += m_config.sceneColorAlpha * (weightedG - m_emaSceneAvgG);
                    m_emaSceneAvgB += m_config.sceneColorAlpha * (weightedB - m_emaSceneAvgB);
                }

                // ── Temporal analysis ────────────────────────────────────────

                // Scene cut detection
                float lumDelta = weightedLum - m_prevLuminance;
                float sceneCut = (std::abs(lumDelta) > m_config.sceneCutThreshold) ? 1.0f : 0.0f;

                // Luminance velocity (signed, EMA-smoothed)
                m_emaLumVelocity += m_config.lumVelocityAlpha * (lumDelta - m_emaLumVelocity);

                // Color shift magnitude (Euclidean RGB delta)
                float dr = weightedR - m_prevAvgR;
                float dg = weightedG - m_prevAvgG;
                float db = weightedB - m_prevAvgB;
                float colorDelta = std::sqrt(dr * dr + dg * dg + db * db);
                m_emaColorShift += m_config.colorShiftAlpha * (colorDelta - m_emaColorShift);

                // Stability score (Welford's online variance with soft decay)
                if (m_varianceCount < static_cast<int>(m_config.stabilityWindow)) {
                    m_varianceCount++;
                }
                float delta1 = weightedLum - m_varianceMean;
                m_varianceMean += delta1 / static_cast<float>(m_varianceCount);
                float delta2 = weightedLum - m_varianceMean;
                m_varianceM2 += delta1 * delta2;
                m_varianceM2 *= 0.998f;  // soft decay to prevent stale dominance

                float variance = (m_varianceCount > 1)
                    ? m_varianceM2 / static_cast<float>(m_varianceCount - 1)
                    : 0.0f;
                float stability = 1.0f / (1.0f + variance * 100.0f);

                fb.Temporal.x = sceneCut;
                fb.Temporal.y = m_emaLumVelocity;
                fb.Temporal.z = m_emaColorShift;
                fb.Temporal.w = stability;

                // Update previous-frame state
                m_prevLuminance = weightedLum;
                m_prevAvgR = weightedR;
                m_prevAvgG = weightedG;
                m_prevAvgB = weightedB;

                // ── Pack scene stats ─────────────────────────────────────────
                fb.Scene.y = m_emaSceneAvgLum;
                fb.Scene.z = lumRange;

                fb.SceneStats.x = keyValue;
                fb.SceneStats.y = contrastRatio;
                fb.SceneStats.z = peripheryLum;
                fb.SceneStats.w = centerPeripheryRatio;

                fb.SceneColor.x = m_emaSceneAvgR;
                fb.SceneColor.y = m_emaSceneAvgG;
                fb.SceneColor.z = m_emaSceneAvgB;
                fb.SceneColor.w = colorTemp;
            }
        }

        backBuffer->Release();

        // Flip double buffer
        m_writeIndex = 1 - m_writeIndex;
        m_hasData = true;
    }

    void FeedbackProcessor::DistributeFeedback(AllData& data)
    {
        if (!m_hasData)
            return;

        // Read from the buffer we're NOT currently writing to
        int readIndex = 1 - m_writeIndex;
        data.feedback = m_feedback[readIndex];

        // Tier C: Pack ENBGetParameter readback slots into float4 params
        // ENBReadback.xyzw = first 4 single-float slots (data[0] of each)
        for (int i = 0; i < 4 && i < m_readbackSlotCount; ++i) {
            if (m_readbackSlots[i].valid)
                (&data.feedback.ENBReadback.x)[i] = m_readbackSlots[i].data[0];
        }
        // ENBReadback4 = slot 4 as float4, or slots 4-7 as single floats
        if (m_readbackSlotCount > 4 && m_readbackSlots[4].valid) {
            if (m_readbackSlots[4].dataSize == 16) {
                data.feedback.ENBReadback4.x = m_readbackSlots[4].data[0];
                data.feedback.ENBReadback4.y = m_readbackSlots[4].data[1];
                data.feedback.ENBReadback4.z = m_readbackSlots[4].data[2];
                data.feedback.ENBReadback4.w = m_readbackSlots[4].data[3];
            } else {
                for (int i = 4; i < 8 && i < m_readbackSlotCount; ++i) {
                    if (m_readbackSlots[i].valid)
                        (&data.feedback.ENBReadback4.x)[i - 4] = m_readbackSlots[i].data[0];
                }
            }
        }
    }

    // ── INI parsing helpers ──────────────────────────────────────────────────

    static std::string TrimWhitespace(const std::string& s)
    {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return {};
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    void FeedbackProcessor::ParseReadbackConfig(std::ifstream& file)
    {
        std::string line;
        while (std::getline(file, line)) {
            auto trimmed = TrimWhitespace(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                continue;

            // Stop if we hit a new section
            if (trimmed[0] == '[')
                break;

            // Parse: SlotN = shader, paramName, size
            auto eq = trimmed.find('=');
            if (eq == std::string::npos)
                continue;

            std::string val = trimmed.substr(eq + 1);

            // Split by comma: shader, paramName, size
            auto comma1 = val.find(',');
            if (comma1 == std::string::npos) continue;
            auto comma2 = val.find(',', comma1 + 1);
            if (comma2 == std::string::npos) continue;

            std::string shader = TrimWhitespace(val.substr(0, comma1));
            std::string param  = TrimWhitespace(val.substr(comma1 + 1, comma2 - comma1 - 1));
            std::string sizeStr = TrimWhitespace(val.substr(comma2 + 1));

            if (shader.empty() || param.empty() || sizeStr.empty())
                continue;

            int dataSize = 0;
            try {
                dataSize = std::stoi(sizeStr);
            } catch (...) {
                SKSE::log::warn("FeedbackProcessor: invalid readback size '{}' for {}/{}",
                    sizeStr, shader, param);
                continue;
            }

            if (dataSize != 4 && dataSize != 16) {
                SKSE::log::warn("FeedbackProcessor: readback size must be 4 or 16, got {} for {}/{}",
                    dataSize, shader, param);
                continue;
            }

            if (m_readbackSlotCount >= kMaxReadbackSlots) {
                SKSE::log::warn("FeedbackProcessor: max readback slots ({}) reached, ignoring {}/{}",
                    kMaxReadbackSlots, shader, param);
                break;
            }

            auto& slot = m_readbackSlots[m_readbackSlotCount++];
            // Uppercase shader name — ENB's internal lookup is case-sensitive
            std::string shaderUpper = shader;
            for (auto& c : shaderUpper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            std::strncpy(slot.shader, shaderUpper.c_str(), sizeof(slot.shader) - 1);
            std::strncpy(slot.paramName, param.c_str(), sizeof(slot.paramName) - 1);
            slot.dataSize = dataSize;

            SKSE::log::info("FeedbackProcessor: readback slot [{}] = {}/{} ({}B)",
                m_readbackSlotCount - 1, slot.shader, slot.paramName, slot.dataSize);
        }
    }

    void FeedbackProcessor::LoadConfig(const std::filesystem::path& configDir)
    {
        auto path = configDir / "FeedbackConfig.ini";

        std::ifstream file(path);
        if (!file.is_open()) {
            SKSE::log::info("FeedbackProcessor: no config at {} — using defaults", path.string());
            return;
        }

        std::string currentSection;
        std::string line;
        while (std::getline(file, line)) {
            auto trimmed = TrimWhitespace(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                continue;

            // Section header
            if (trimmed[0] == '[') {
                auto close = trimmed.find(']');
                if (close != std::string::npos)
                    currentSection = trimmed.substr(1, close - 1);
                else
                    currentSection = trimmed.substr(1);

                // Delegate [ENBReadback] to dedicated parser
                if (currentSection == "ENBReadback") {
                    ParseReadbackConfig(file);
                    currentSection.clear();
                }
                continue;
            }

            // [Feedback] section keys
            if (currentSection != "Feedback")
                continue;

            auto eq = trimmed.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = TrimWhitespace(trimmed.substr(0, eq));
            std::string val = TrimWhitespace(trimmed.substr(eq + 1));

            try {
                if      (key == "centerLumAlpha")    m_config.centerLumAlpha    = std::stof(val);
                else if (key == "sceneAvgAlpha")     m_config.sceneAvgAlpha     = std::stof(val);
                else if (key == "sceneColorAlpha")   m_config.sceneColorAlpha   = std::stof(val);
                else if (key == "lumVelocityAlpha")  m_config.lumVelocityAlpha  = std::stof(val);
                else if (key == "colorShiftAlpha")   m_config.colorShiftAlpha   = std::stof(val);
                else if (key == "sceneCutThreshold") m_config.sceneCutThreshold = std::stof(val);
                else if (key == "stabilityWindow")   m_config.stabilityWindow   = std::stof(val);
            } catch (...) {
                SKSE::log::warn("FeedbackProcessor: invalid value for '{}': '{}'", key, val);
            }
        }

        SKSE::log::info("FeedbackProcessor: config loaded — centerLum={:.3f}, sceneAvg={:.3f}, "
            "sceneColor={:.3f}, lumVel={:.3f}, colorShift={:.3f}, sceneCut={:.3f}, "
            "readbackSlots={}",
            m_config.centerLumAlpha, m_config.sceneAvgAlpha, m_config.sceneColorAlpha,
            m_config.lumVelocityAlpha, m_config.colorShiftAlpha, m_config.sceneCutThreshold,
            m_readbackSlotCount);
    }

    void FeedbackProcessor::ReadENBParameters()
    {
        if (m_readbackSlotCount == 0 || !ENBInterface::GetParameter)
            return;

        for (int i = 0; i < m_readbackSlotCount; ++i) {
            auto& slot = m_readbackSlots[i];

            ENBInterface::ENBParameter outParam;
            int result = ENBInterface::GetParameter(
                nullptr,            // filename = NULL for shader variables
                slot.shader,        // category = shader name
                slot.paramName,     // keyname = UIName annotation
                &outParam           // ENBParameter output struct
            );

            if (result) {
                int copySize = (slot.dataSize <= static_cast<int>(outParam.Size))
                    ? slot.dataSize : static_cast<int>(outParam.Size);
                if (copySize > 0)
                    std::memcpy(slot.data, outParam.Data, copySize);
                slot.valid = true;
            } else {
                slot.valid = false;
                if (!slot.loggedFailure) {
                    SKSE::log::warn("FeedbackProcessor: ENBGetParameter failed for '{}/{}'",
                        slot.shader, slot.paramName);
                    slot.loggedFailure = true;
                }
            }
        }
    }
}
