#pragma once
// RawFramePacing.h: Deterministic 100 Hz Fixed-Timestep Frame Pacing, Input-Latency Telemetry,
// and 10-Step Temporal Anti-Drift Verification Contract across Creation Engine 1 / 1.5 / 2.0
// (Skyrim SE / Fallout 4 / Starfield Native DX12). Original work, MIT.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "../common/raw_engine_profile.h"

namespace raw::bridge {

inline constexpr double   kFixedSimHz            = 100.0;
inline constexpr double   kFixedTickSec          = 0.01;
inline constexpr double   kDefaultMaxFrameDtSec  = 0.10;
inline constexpr double   kDefaultInputStaleMs   = 50.0;
inline constexpr uint32_t kDefaultMaxSubsteps    = 8u;
inline constexpr size_t   kPacingWindowSize      = 64u;

struct FramePacingPolicy {
    raw::versions::EngineTitle title                     = raw::versions::EngineTitle::Unknown;
    double                     fixedTickHz               = kFixedSimHz;
    double                     fixedTickSec              = kFixedTickSec;
    uint32_t                   maxSubstepsPerFrame       = kDefaultMaxSubsteps;
    uint32_t                   targetSwapchainQueueDepth = 2u;
    double                     maxInputLatencyMs         = kDefaultInputStaleMs;
    bool                       enableHavokSubstepGuard   = false;
    bool                       enablePrevisPacingGuard   = false;
    bool                       preferAsyncComputeOverlap = false;
    bool                       multiplayerBlocked        = false;
};

inline FramePacingPolicy ResolveFramePacingPolicy(const raw::versions::EngineProfile& profile) {
    FramePacingPolicy policy{};
    policy.title = profile.title;
    policy.fixedTickHz = kFixedSimHz;
    policy.fixedTickSec = kFixedTickSec;
    policy.maxSubstepsPerFrame = kDefaultMaxSubsteps;
    policy.targetSwapchainQueueDepth = 2u;
    policy.multiplayerBlocked = profile.isMultiplayerOnline;
    if (profile.isMultiplayerOnline) {
        policy.maxSubstepsPerFrame = 0u;
        policy.maxInputLatencyMs = 0.0;
        return policy;
    }
    switch (profile.gen) {
        case raw::versions::EngineGen::CE1_DX11:
            policy.enableHavokSubstepGuard = profile.hasHavok60HzBug;
            policy.maxInputLatencyMs = 45.0;
            policy.preferAsyncComputeOverlap = false;
            break;
        case raw::versions::EngineGen::CE1_5_FO4_DX11:
            policy.enableHavokSubstepGuard = profile.hasHavok60HzBug;
            policy.enablePrevisPacingGuard = profile.hasPrevisPrecombines;
            policy.maxInputLatencyMs = 45.0;
            policy.preferAsyncComputeOverlap = false;
            break;
        case raw::versions::EngineGen::CE2_Starfield_DX12:
            policy.enableHavokSubstepGuard = false;
            policy.enablePrevisPacingGuard = false;
            policy.maxInputLatencyMs = 35.0;
            policy.preferAsyncComputeOverlap = profile.renderContract.requiresAsyncComputeQueue;
            break;
        default:
            policy.maxInputLatencyMs = kDefaultInputStaleMs;
            break;
    }
    return policy;
}

struct FixedStepResult {
    uint32_t substeps          = 0u;
    uint32_t droppedSubsteps   = 0u;
    double   interpAlpha       = 0.0;
    double   clampedFrameDtSec = 0.0;
    double   inputLatencyMs    = 0.0;
    bool     overbudgetSpike   = false;
    bool     inputStale        = false;
};

struct FramePacingTelemetry {
    uint64_t totalFrames            = 0u;
    uint64_t totalFixedTicks        = 0u;
    uint64_t droppedTicks           = 0u;
    uint64_t staleInputFrames       = 0u;
    double   p50FrameMs             = 0.0;
    double   p99FrameMs             = 0.0;
    double   jitterMs               = 0.0;
    double   maxInputLatencyMs      = 0.0;
    bool     allWithinLatencyBudget = true;
};

// Deterministic 100 Hz fixed-step accumulator & frame-pacing telemetry tracker.
// Zero dynamic allocation, zero wall-clock drift, safe across 30..240+ FPS and frame spikes.
class DeterministicFramePacer {
public:
    explicit DeterministicFramePacer(const FramePacingPolicy& policy)
        : m_policy(policy) {}

    FixedStepResult Step(double rawFrameDeltaSec, double inputSampleAgeMs = 0.0) {
        FixedStepResult out{};
        if (m_policy.multiplayerBlocked || m_policy.fixedTickSec <= 0.0 || m_policy.maxSubstepsPerFrame == 0u) {
            out.inputStale = true;
            return out;
        }
        const double finiteDt = (std::isfinite(rawFrameDeltaSec) && rawFrameDeltaSec > 0.0) ? rawFrameDeltaSec : 0.0;
        const double maxAccumDt = static_cast<double>(m_policy.maxSubstepsPerFrame) * m_policy.fixedTickSec;
        const double clampedDt = (std::min)(finiteDt, kDefaultMaxFrameDtSec);
        out.clampedFrameDtSec = clampedDt;
        out.overbudgetSpike = (finiteDt > maxAccumDt);

        m_accumulatorSec += clampedDt;
        uint32_t steps = 0u;
        while (m_accumulatorSec + 1.0e-12 >= m_policy.fixedTickSec && steps < m_policy.maxSubstepsPerFrame) {
            m_accumulatorSec = (std::max)(0.0, m_accumulatorSec - m_policy.fixedTickSec);
            ++steps;
        }
        if (m_accumulatorSec >= m_policy.fixedTickSec) {
            const uint32_t dropped = static_cast<uint32_t>(std::floor(m_accumulatorSec / m_policy.fixedTickSec));
            out.droppedSubsteps = dropped;
            m_accumulatorSec -= static_cast<double>(dropped) * m_policy.fixedTickSec;
            m_droppedTicks += dropped;
        }
        out.substeps = steps;
        out.interpAlpha = (std::clamp)(m_accumulatorSec / m_policy.fixedTickSec, 0.0, 1.0);

        const double frameMs = finiteDt * 1000.0;
        const double safeInputAge = (std::isfinite(inputSampleAgeMs) && inputSampleAgeMs > 0.0) ? inputSampleAgeMs : 0.0;
        out.inputLatencyMs = safeInputAge + frameMs;
        out.inputStale = (out.inputLatencyMs > m_policy.maxInputLatencyMs);

        m_windowMs[m_windowCursor] = frameMs;
        m_windowCursor = (m_windowCursor + 1u) % kPacingWindowSize;
        if (m_windowCount < kPacingWindowSize) {
            ++m_windowCount;
        }
        ++m_totalFrames;
        m_totalFixedTicks += steps;
        if (out.inputStale) {
            ++m_staleInputFrames;
        }
        if (out.inputLatencyMs > m_maxObservedInputLatencyMs) {
            m_maxObservedInputLatencyMs = out.inputLatencyMs;
        }
        return out;
    }

    FramePacingTelemetry ComputeTelemetry() const {
        FramePacingTelemetry t{};
        t.totalFrames = m_totalFrames;
        t.totalFixedTicks = m_totalFixedTicks;
        t.droppedTicks = m_droppedTicks;
        t.staleInputFrames = m_staleInputFrames;
        t.maxInputLatencyMs = m_maxObservedInputLatencyMs;
        t.allWithinLatencyBudget = (m_staleInputFrames == 0u) && !m_policy.multiplayerBlocked;
        if (m_windowCount == 0u) {
            return t;
        }
        std::array<double, kPacingWindowSize> sorted{};
        for (size_t i = 0; i < m_windowCount; ++i) {
            sorted[i] = m_windowMs[i];
        }
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(m_windowCount));
        const size_t i50 = (m_windowCount - 1u) / 2u;
        const size_t i99 = ((m_windowCount - 1u) * 99u) / 100u;
        t.p50FrameMs = sorted[i50];
        t.p99FrameMs = sorted[i99];
        t.jitterMs = (std::max)(0.0, t.p99FrameMs - t.p50FrameMs);
        return t;
    }

    const FramePacingPolicy& Policy() const { return m_policy; }

private:
    FramePacingPolicy                     m_policy{};
    double                                m_accumulatorSec = 0.0;
    std::array<double, kPacingWindowSize> m_windowMs{};
    size_t                                m_windowCursor = 0u;
    size_t                                m_windowCount = 0u;
    uint64_t                              m_totalFrames = 0u;
    uint64_t                              m_totalFixedTicks = 0u;
    uint64_t                              m_droppedTicks = 0u;
    uint64_t                              m_staleInputFrames = 0u;
    double                                m_maxObservedInputLatencyMs = 0.0;
};

struct TemporalAntiDriftReport {
    double maxRelativeLumaDrift   = 0.0;
    double maxChromaOvershoot     = 0.0;
    double finalLuminance         = 0.0;
    double expectedLuminance      = 0.0;
    bool   conservativeSpecularMv = true;
    bool   ok                     = false;
};

// CPU mirror of the Wave 73 Deja null-space 10-step Karis-YCoCg anti-drift guard.
// Validates that 10 chained temporal blends conserve linear luminance & chroma envelope
// and rejects injected accumulation bias or non-conservative specular motion-vector gains.
inline TemporalAntiDriftReport VerifyTenStepTemporalAntiDrift(
    double initLuma,
    double altLuma,
    double initChroma,
    double altChroma,
    double alpha,
    double maxRelDriftTol = 0.025,
    double injectedStepBias = 0.0,
    double specularMvGain = 1.0) {
    TemporalAntiDriftReport rep{};
    const double a = (std::clamp)(alpha, 0.05, 0.95);
    double histLinL = (std::max)(initLuma, 0.0);
    double histKarisChroma = (std::max)(initChroma, 0.0);
    double exactLinL = histLinL;

    rep.conservativeSpecularMv = (specularMvGain > 0.0 && specularMvGain <= 1.0);

    for (int step = 0; step < 10; ++step) {
        const double curLinL = ((step & 1) == 0) ? (std::max)(altLuma, 0.0) : (std::max)(initLuma, 0.0);
        const double curChroma = ((step & 1) == 0) ? (std::max)(altChroma, 0.0) : (std::max)(initChroma, 0.0);

        const double curKarisL = curLinL / (1.0 + curLinL);
        const double histKarisL = histLinL / (1.0 + histLinL);
        const double blendKarisL = histKarisL + (curKarisL - histKarisL) * a;
        double reconLinL = blendKarisL / (std::max)(1.0 - blendKarisL, 1.0e-5);

        // Wave 73 Deja null-space luminance energy projection (strictly bounded within [min, max]).
        const double minL = (std::min)(histLinL, curLinL);
        const double maxL = (std::max)(histLinL, curLinL);
        const double targetLinL = (std::clamp)(histLinL + (curLinL - histLinL) * a, minL, maxL);
        if (reconLinL > 1.0e-4) {
            const double corr = (std::clamp)(targetLinL / reconLinL, 0.90, 1.25);
            reconLinL *= corr;
        }
        // Optional planted-drift injection for negative-control testing.
        reconLinL = reconLinL * (1.0 + injectedStepBias) * specularMvGain;

        const double rawChroma = (histKarisChroma + (curChroma - histKarisChroma) * a) * specularMvGain;
        const double maxChromaEnv = (std::max)(histKarisChroma, curChroma);
        const double clampedChroma = (specularMvGain <= 1.0) ? (std::min)(rawChroma, maxChromaEnv) : rawChroma;
        const double overshoot = (std::max)(0.0, clampedChroma - maxChromaEnv);
        rep.maxChromaOvershoot = (std::max)(rep.maxChromaOvershoot, overshoot);

        exactLinL = exactLinL + (curLinL - exactLinL) * a;
        const double relDrift = std::abs(reconLinL - exactLinL) / (std::max)(exactLinL, 1.0e-4);
        rep.maxRelativeLumaDrift = (std::max)(rep.maxRelativeLumaDrift, relDrift);

        histLinL = (std::max)(reconLinL, 0.0);
        histKarisChroma = clampedChroma;
    }

    rep.finalLuminance = histLinL;
    rep.expectedLuminance = exactLinL;
    rep.ok = rep.conservativeSpecularMv
          && (rep.maxRelativeLumaDrift <= maxRelDriftTol)
          && (rep.maxChromaOvershoot <= 1.0e-6);
    return rep;
}

}  // namespace raw::bridge
