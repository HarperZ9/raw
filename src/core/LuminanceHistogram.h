#pragma once
//=============================================================================
//  LuminanceHistogram — Compute-based 256-bin luminance histogram
//
//  Dispatches two compute shaders per frame:
//    Pass 1: Bins backbuffer luminance into 256-bin histogram (log-space)
//    Pass 2: Reduces histogram to avg/min/max/percentiles
//
//  Additional metering pass:
//    Pass 3: Spatially-weighted luminance metering (Evaluative/CenterWeighted/Spot)
//            with auto-exposure computation (EV100 + temporal adaptation)
//
//  Results exposed as:
//    - HistogramResult struct for CPU (FeedbackProcessor)
//    - 256×1 R32_FLOAT SRV at t17 for ENB shaders
//=============================================================================

#include <chrono>
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11Buffer;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct IDXGISwapChain;

namespace SB
{
    /// Metering mode for auto-exposure weighting
    enum class MeteringMode : uint8_t {
        Evaluative     = 0,   // 5-zone matrix metering (center 40%, edges 15% each)
        CenterWeighted = 1,   // Gaussian falloff from center (sigma = 0.3)
        Spot           = 2    // Center 5% circular mask only
    };

    struct HistogramResult
    {
        float bins[256]{};          // Normalized histogram (sum ≈ 1.0)
        float avgLuminance = 0.f;
        float minLuminance = 0.f;
        float maxLuminance = 0.f;
        float p05 = 0.f;           // 5th percentile
        float p50 = 0.f;           // Median
        float p95 = 0.f;           // 95th percentile
        float avgR = 0.f, avgG = 0.f, avgB = 0.f;

        // Auto-exposure results (from metering pass)
        float exposureEV = 0.f;      // Computed EV100 for auto-exposure
        float targetExposure = 1.f;   // Linear exposure multiplier (2^-EV)
    };

    class LuminanceHistogram
    {
    public:
        static LuminanceHistogram& Get()
        {
            static LuminanceHistogram instance;
            return instance;
        }

        bool Initialize(ID3D11Device* a_device, IDXGISwapChain* a_swapChain);
        void Shutdown();

        /// Dispatch compute shader to analyze the backbuffer.
        /// Call from HookedPresent BEFORE FeedbackProcessor::CollectFeedback.
        void Dispatch(ID3D11DeviceContext* a_ctx, IDXGISwapChain* a_swapChain);

        /// Read back results from the staging buffer (1-frame delay).
        /// Call from HookedPresent after Dispatch.
        void ReadBack(ID3D11DeviceContext* a_ctx);

        /// Get latest results for CPU consumption (FeedbackProcessor).
        const HistogramResult& GetResult() const { return m_result[1 - m_writeIndex]; }

        /// Get histogram SRV for shader binding (t17).
        ID3D11ShaderResourceView* GetHistogramSRV() const { return m_histogramSRV; }

        static constexpr uint32_t kSRVSlot = 17;

        bool IsInitialized() const { return m_initialized; }
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }

        // ── Metering / Auto-Exposure API ─────────────────────────────────────

        MeteringMode GetMeteringMode() const { return m_meteringMode; }
        void SetMeteringMode(MeteringMode m) { m_meteringMode = m; }

        float GetExposureCompensation() const { return m_exposureComp; }
        void SetExposureCompensation(float ec) { m_exposureComp = ec; }

        float GetAdaptationSpeed() const { return m_adaptSpeed; }
        void SetAdaptationSpeed(float s) { m_adaptSpeed = s; }

        float GetCurrentExposure() const { return m_currentExposure; }
        float GetCurrentEV() const { return m_currentEV; }

        float GetMinEV() const { return m_minEV; }
        float GetMaxEV() const { return m_maxEV; }
        void SetEVRange(float minEV, float maxEV) { m_minEV = minEV; m_maxEV = maxEV; }

    private:
        LuminanceHistogram() = default;

        bool CompileComputeShaders(ID3D11Device* a_device);
        bool CreateResources(ID3D11Device* a_device, uint32_t w, uint32_t h);
        void ReleaseResources();

        bool m_initialized = false;
        bool m_enabled = true;

        // Compute shaders
        ID3D11ComputeShader* m_histogramCS = nullptr;
        ID3D11ComputeShader* m_reductionCS = nullptr;

        // GPU resources
        ID3D11Buffer* m_histogramBuffer = nullptr;    // RWStructuredBuffer<uint> [256]
        ID3D11Buffer* m_statsBuffer = nullptr;         // RWStructuredBuffer<float4> [4]
        ID3D11Buffer* m_stagingHistogram = nullptr;    // CPU-readable copy
        ID3D11Buffer* m_stagingStats = nullptr;        // CPU-readable copy

        // SRV for ENB shaders: 256×1 R32_FLOAT texture
        ID3D11Texture2D* m_histogramTex = nullptr;
        ID3D11ShaderResourceView* m_histogramSRV = nullptr;

        // UAVs for compute dispatch
        ID3D11UnorderedAccessView* m_histogramBufUAV = nullptr;
        ID3D11UnorderedAccessView* m_statsBufUAV = nullptr;

        uint32_t m_width = 0, m_height = 0;

        // Double-buffered results
        HistogramResult m_result[2]{};
        int m_writeIndex = 0;
        bool m_hasData = false;
        bool m_pendingReadback = false;

        // ── Metering / Auto-Exposure state ───────────────────────────────────

        MeteringMode m_meteringMode = MeteringMode::Evaluative;
        float m_exposureComp = 0.0f;    // EV compensation (-3 to +3)
        float m_adaptSpeed = 2.0f;      // Adaptation speed (EV/sec)
        float m_currentExposure = 1.0f;  // Smoothed linear exposure
        float m_currentEV = 0.0f;       // Current EV100
        float m_minEV = -4.0f;          // Min auto-exposure EV
        float m_maxEV = 16.0f;          // Max auto-exposure EV

        // Metering compute shader + GPU resources
        ID3D11ComputeShader* m_meteringCS = nullptr;
        ID3D11Buffer* m_meteringCB = nullptr;            // Constant buffer for MeteringCB
        ID3D11Buffer* m_meteringBuffer = nullptr;        // RWStructuredBuffer<uint> [4] for metering result
        ID3D11Buffer* m_stagingMetering = nullptr;       // CPU readback
        ID3D11UnorderedAccessView* m_meteringBufUAV = nullptr;

        // Delta-time tracking for temporal adaptation
        std::chrono::high_resolution_clock::time_point m_lastReadbackTime{};
        bool m_hasLastReadbackTime = false;
    };

} // namespace SB
