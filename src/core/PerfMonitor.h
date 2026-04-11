#pragma once
#include "BridgeData.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace SB
{
    class PerfMonitor
    {
    public:
        static PerfMonitor& Get();

        bool Initialize(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
        void BeginFrame();
        void EndFrame();

        PerfData GetData() const;

        float GetQualityScale() const { return m_qualityScale; }
        bool  IsInitialized()  const { return m_initialized; }

    private:
        PerfMonitor() = default;

        bool  m_initialized = false;
        float m_qualityScale = 1.0f;
        float m_gpuFrameMs = 0.f;
        float m_cpuFrameMs = 0.f;
        float m_presentMs  = 0.f;
        float m_targetFps  = 60.f;
        float m_budgetPct  = 0.f;
        uint32_t m_frameDrops = 0;

        // D3D11 timestamp queries
        ID3D11Device*        m_device  = nullptr;
        ID3D11DeviceContext* m_context = nullptr;

        struct TimestampQuery;
        TimestampQuery* m_queries = nullptr;
        int m_queryIndex = 0;

        // EMA smoothing
        float m_emaGpu = 0.f;
        float m_emaCpu = 0.f;
        bool  m_emaInit = false;

        // CPU timing
        long long m_cpuFrameStart = 0;
    };
}
