//=============================================================================
//  PerfMonitor.cpp — GPU timing queries + performance governor
//
//  Uses D3D11 timestamp queries to measure GPU frame time.
//  Implements a self-tuning quality scale that smoothly reduces when
//  GPU budget is exceeded and recovers when headroom is available.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "PerfMonitor.h"
#include <d3d11.h>
#include <SKSE/SKSE.h>
#include <chrono>

namespace SB
{

struct PerfMonitor::TimestampQuery
{
    ID3D11Query* disjoint[2]  = {};
    ID3D11Query* tsBegin[2]   = {};
    ID3D11Query* tsEnd[2]     = {};
    bool         pending[2]   = {};
};

PerfMonitor& PerfMonitor::Get()
{
    static PerfMonitor instance;
    return instance;
}

bool PerfMonitor::Initialize(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
{
    if (m_initialized) return true;
    if (!a_device || !a_context) return false;

    m_device  = a_device;
    m_context = a_context;

    m_queries = new TimestampQuery();

    D3D11_QUERY_DESC disjointDesc{};
    disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

    D3D11_QUERY_DESC tsDesc{};
    tsDesc.Query = D3D11_QUERY_TIMESTAMP;

    for (int i = 0; i < 2; i++) {
        if (FAILED(a_device->CreateQuery(&disjointDesc, &m_queries->disjoint[i])) ||
            FAILED(a_device->CreateQuery(&tsDesc, &m_queries->tsBegin[i])) ||
            FAILED(a_device->CreateQuery(&tsDesc, &m_queries->tsEnd[i])))
        {
            SKSE::log::warn("PerfMonitor: failed to create GPU timestamp queries");
            delete m_queries;
            m_queries = nullptr;
            return false;
        }
    }

    m_initialized = true;
    m_qualityScale = 1.0f;
    SKSE::log::info("PerfMonitor: initialized — GPU timing + performance governor active");
    return true;
}

void PerfMonitor::BeginFrame()
{
    if (!m_initialized || !m_queries) return;

    // CPU timing
    m_cpuFrameStart = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();

    // Start GPU timestamp for current frame
    int idx = m_queryIndex;
    m_context->Begin(m_queries->disjoint[idx]);
    m_context->End(m_queries->tsBegin[idx]);
    m_queries->pending[idx] = true;
}

void PerfMonitor::EndFrame()
{
    if (!m_initialized || !m_queries) return;

    int idx = m_queryIndex;

    // End GPU timestamp
    m_context->End(m_queries->tsEnd[idx]);
    m_context->End(m_queries->disjoint[idx]);

    // CPU frame time
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    float cpuMs = static_cast<float>(now - m_cpuFrameStart) / 1'000'000.f;

    // Read PREVIOUS frame's GPU results (2-frame pipeline)
    int readIdx = 1 - idx;
    if (m_queries->pending[readIdx]) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
        UINT64 tsBegin = 0, tsEnd = 0;

        if (m_context->GetData(m_queries->disjoint[readIdx], &disjointData,
                sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            m_context->GetData(m_queries->tsBegin[readIdx], &tsBegin,
                sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            m_context->GetData(m_queries->tsEnd[readIdx], &tsEnd,
                sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
        {
            if (!disjointData.Disjoint && disjointData.Frequency > 0) {
                float gpuMs = static_cast<float>(tsEnd - tsBegin)
                    / static_cast<float>(disjointData.Frequency) * 1000.f;

                // EMA smoothing (alpha=0.1)
                if (!m_emaInit) {
                    m_emaGpu = gpuMs;
                    m_emaCpu = cpuMs;
                    m_emaInit = true;
                } else {
                    m_emaGpu = m_emaGpu * 0.9f + gpuMs * 0.1f;
                    m_emaCpu = m_emaCpu * 0.9f + cpuMs * 0.1f;
                }

                m_gpuFrameMs = m_emaGpu;
                m_cpuFrameMs = m_emaCpu;
            }
            m_queries->pending[readIdx] = false;
        }
    }

    // ── Performance governor ─────────────────────────────────────────
    float targetMs = 1000.f / m_targetFps;
    float gpuBudget = m_gpuFrameMs / targetMs;
    m_budgetPct = gpuBudget;

    if (gpuBudget > 0.95f) {
        // Over budget: reduce quality smoothly
        m_qualityScale -= 0.002f;
        if (m_qualityScale < 0.5f) m_qualityScale = 0.5f;
        m_frameDrops++;
    } else if (gpuBudget < 0.75f) {
        // Under budget: recover quality
        m_qualityScale += 0.001f;
        if (m_qualityScale > 1.0f) m_qualityScale = 1.0f;
    }

    m_presentMs = cpuMs;  // approximate

    // Flip double buffer
    m_queryIndex = 1 - m_queryIndex;
}

PerfData PerfMonitor::GetData() const
{
    PerfData data{};
    data.Timing.x = m_gpuFrameMs;
    data.Timing.y = m_cpuFrameMs;
    data.Timing.z = m_presentMs;
    data.Timing.w = m_targetFps;
    data.Budget.x = m_budgetPct;
    data.Budget.y = m_qualityScale;
    data.Budget.z = 0.f;  // thermal state (reserved)
    data.Budget.w = static_cast<float>(m_frameDrops);
    return data;
}

}  // namespace SB
