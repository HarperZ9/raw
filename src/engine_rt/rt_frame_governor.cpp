// rt_frame_governor.cpp: Wave 54 Hysteresis-Damped PI Frame-Pacing & Quality Scaling Governor (`raw::rt`).
// Original work, MIT.

#include "rt_frame_governor.h"

#include "rt_live_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace raw::rt {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    if (!std::isfinite(v)) return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float LerpF(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

struct FrameGovernorRuntime {
    std::mutex          mu;
    bool                initialized = false;
    FramePacingGovernor gov{};
} g_fg;

}  // namespace

const char* GovernorRegimeName(GovernorRegime r) noexcept {
    switch (r) {
        case GovernorRegime::DeadbandHold:  return "DeadbandHold";
        case GovernorRegime::ScalingDown:   return "ScalingDown";
        case GovernorRegime::RecoveringUp:  return "RecoveringUp";
        case GovernorRegime::DisabledUnity: return "DisabledUnity";
        default:                            return "Unknown";
    }
}

FramePacingGovernor::FramePacingGovernor(const FrameGovernorConfig& cfg) noexcept {
    SetConfig(cfg);
    Reset();
}

void FramePacingGovernor::SetConfig(const FrameGovernorConfig& cfg) noexcept {
    m_cfg = cfg;
    m_cfg.targetFps         = ClampF(m_cfg.targetFps,         30.0f, 240.0f);
    m_cfg.minRenderScale    = ClampF(m_cfg.minRenderScale,    0.65f, 1.00f);
    m_cfg.deadbandLowRatio  = ClampF(m_cfg.deadbandLowRatio,  0.50f, 0.98f);
    m_cfg.deadbandHighRatio = ClampF(m_cfg.deadbandHighRatio, 1.01f, 1.50f);
    m_cfg.recoveryHoldSec   = ClampF(m_cfg.recoveryHoldSec,   0.10f, 10.0f);
    m_cfg.emaTauSec         = ClampF(m_cfg.emaTauSec,         0.01f, 2.0f);
    m_cfg.kpDown            = ClampF(m_cfg.kpDown,            0.05f, 5.0f);
    m_cfg.kiDown            = ClampF(m_cfg.kiDown,            0.00f, 3.0f);
    m_cfg.maxDownSlewPerSec = ClampF(m_cfg.maxDownSlewPerSec, 0.02f, 2.0f);
    m_cfg.maxUpSlewPerSec   = ClampF(m_cfg.maxUpSlewPerSec,   0.01f, 1.0f);

    m_state.targetFps     = m_cfg.targetFps;
    m_state.targetFrameMs = 1000.0f / m_cfg.targetFps;
    ApplyQualityMultipliers();
}

void FramePacingGovernor::Reset() noexcept {
    m_state = {};
    m_state.governorEnabled = 1u;
    m_state.regime          = static_cast<uint8_t>(GovernorRegime::DeadbandHold);
    m_state.targetFps       = m_cfg.targetFps;
    m_state.targetFrameMs   = 1000.0f / m_cfg.targetFps;
    m_state.emaFrameMs      = m_state.targetFrameMs;
    m_state.qualityFactor01 = 1.0f;
    m_integralErr           = 0.0f;
    m_headroomSec           = 0.0f;
    ApplyQualityMultipliers();
}

void FramePacingGovernor::ApplyQualityMultipliers() noexcept {
    const float q = ClampF(m_state.qualityFactor01, 0.0f, 1.0f);
    m_state.qualityFactor01     = q;
    m_state.renderScale         = LerpF(m_cfg.minRenderScale, 1.0f, q);
    m_state.volumetricQuality01 = LerpF(0.50f,                1.0f, q);
    m_state.shadowDistanceScale = LerpF(0.65f,                1.0f, q);
    m_state.floraLodScale       = LerpF(0.60f,                1.0f, q);
    m_state.giRayBudget01       = LerpF(0.50f,                1.0f, q);
}

void FramePacingGovernor::Step(double frameMs) noexcept {
    if (!std::isfinite(frameMs) || frameMs <= 0.0) return;

    // Clamp extreme suspend/load-screen gaps (>250ms) so a single loading hitch doesn't peg integral windup
    const float clampedMs = static_cast<float>(std::clamp(frameMs, 0.5, 250.0));
    const float dtSec     = std::min(clampedMs * 0.001f, 0.10f);
    ++m_state.totalFrames;

    // 1. Exponential moving average of frame time (filters single-frame micro-jitter)
    const float emaAlpha = 1.0f - std::exp(-dtSec / m_cfg.emaTauSec);
    m_state.emaFrameMs   = ClampF(m_state.emaFrameMs + (clampedMs - m_state.emaFrameMs) * emaAlpha, 0.5f, 250.0f);

    const float targetMs = m_state.targetFrameMs;
    const float loMs     = targetMs * m_cfg.deadbandLowRatio;
    const float hiMs     = targetMs * m_cfg.deadbandHighRatio;

    // 2. Asymmetric Deadband + PI Control
    if (m_state.emaFrameMs > hiMs) {
        // Over-budget: reset headroom timer & apply bounded PI scale-down
        m_headroomSec = 0.0f;
        const float normErr = (m_state.emaFrameMs - hiMs) / targetMs;
        // Anti-windup integral clamp
        m_integralErr = ClampF(m_integralErr + normErr * dtSec, 0.0f, 2.0f);

        const float desiredRate = m_cfg.kpDown * normErr + m_cfg.kiDown * m_integralErr;
        const float rate        = ClampF(desiredRate, 0.0f, m_cfg.maxDownSlewPerSec);
        const float nextQ       = ClampF(m_state.qualityFactor01 - rate * dtSec, 0.0f, 1.0f);
        if (nextQ < m_state.qualityFactor01 - 1.0e-6f) {
            ++m_state.scaleDownSteps;
        }
        m_state.qualityFactor01 = nextQ;
        m_state.regime          = static_cast<uint8_t>(GovernorRegime::ScalingDown);
    } else if (m_state.emaFrameMs < loMs) {
        // Under-budget headroom: bleed down integral error and require sustained hold before gentle recovery
        m_integralErr = std::max(0.0f, m_integralErr - dtSec * 1.5f);
        m_headroomSec += dtSec;

        if (m_headroomSec >= m_cfg.recoveryHoldSec && m_state.qualityFactor01 < 1.0f) {
            const float headroomRatio = ClampF((loMs - m_state.emaFrameMs) / targetMs, 0.15f, 1.0f);
            const float upRate        = ClampF(m_cfg.maxUpSlewPerSec * (0.5f + 0.5f * headroomRatio),
                                               0.01f, m_cfg.maxUpSlewPerSec);
            const float nextQ         = ClampF(m_state.qualityFactor01 + upRate * dtSec, 0.0f, 1.0f);
            if (nextQ > m_state.qualityFactor01 + 1.0e-6f) {
                ++m_state.scaleUpSteps;
            }
            m_state.qualityFactor01 = nextQ;
            m_state.regime          = static_cast<uint8_t>(GovernorRegime::RecoveringUp);
        } else {
            m_state.regime = static_cast<uint8_t>(GovernorRegime::DeadbandHold);
        }
    } else {
        // Inside deadband [0.88 * targetMs .. 1.06 * targetMs]: hold quality steady (ZERO thrashing!)
        m_headroomSec = 0.0f;
        m_integralErr *= std::exp(-dtSec * 2.0f);
        m_state.regime = static_cast<uint8_t>(GovernorRegime::DeadbandHold);
    }

    ApplyQualityMultipliers();
}

// --- Runtime Lifecycle & C ABI ---

std::string FrameGovernorInit() {
    const LiveEngineConfig cfg = GetLiveConfig();
    FrameGovernorConfig gc{};
    gc.targetFps = cfg.targetFps;
    {
        std::lock_guard<std::mutex> lk(g_fg.mu);
        g_fg.initialized = true;
        g_fg.gov.SetConfig(gc);
        g_fg.gov.Reset();
    }
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "[FrameGovernor] initialized (DynamicGovernor=%u, TargetFps=%.0f [%.2fms], deadband=0.88x..1.06x, minRender=%.2f)",
                  cfg.dynamicGovernorEnabled ? 1u : 0u,
                  double(gc.targetFps), double(1000.0f / gc.targetFps), double(gc.minRenderScale));
    return buf;
}

void FrameGovernorOnPresent(double frameMs) {
    const LiveEngineConfig cfg = GetLiveConfig();
    if (!cfg.dynamicGovernorEnabled) return;

    std::lock_guard<std::mutex> lk(g_fg.mu);
    if (!g_fg.initialized) g_fg.initialized = true;
    if (std::fabs(g_fg.gov.Config().targetFps - cfg.targetFps) > 0.25f) {
        FrameGovernorConfig gc = g_fg.gov.Config();
        gc.targetFps = cfg.targetFps;
        g_fg.gov.SetConfig(gc);
    }
    g_fg.gov.Step(frameMs);
}

void FrameGovernorShutdown() {
    std::lock_guard<std::mutex> lk(g_fg.mu);
    g_fg.initialized = false;
    g_fg.gov.Reset();
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetFrameGovernorState(raw::rt::RawFrameGovernorState* out) {
    if (!out) return 0;
    const raw::rt::LiveEngineConfig cfg = raw::rt::GetLiveConfig();
    std::lock_guard<std::mutex> lk(raw::rt::g_fg.mu);
    *out = raw::rt::g_fg.gov.State();
    if (!cfg.dynamicGovernorEnabled) {
        // Exact 1.00 unity across all 5 quality multipliers when DynamicGovernor=0!
        out->governorEnabled     = 0u;
        out->regime              = static_cast<uint8_t>(raw::rt::GovernorRegime::DisabledUnity);
        out->qualityFactor01     = 1.0f;
        out->renderScale         = 1.0f;
        out->volumetricQuality01 = 1.0f;
        out->shadowDistanceScale = 1.0f;
        out->floraLodScale       = 1.0f;
        out->giRayBudget01       = 1.0f;
    } else {
        out->governorEnabled = 1u;
    }
    return raw::rt::g_fg.initialized ? 1 : 0;
}
