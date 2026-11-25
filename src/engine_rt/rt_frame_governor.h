#pragma once
// rt_frame_governor.h: Wave 54 Hysteresis-Damped PI Frame-Pacing & Quality Scaling Governor (`raw::rt`).
// Asymmetric proportional-integral controller with [0.88x .. 1.06x] targetMs deadband, fast bounded
// over-budget quality shedding, and 1.5s sustained-headroom gentle recovery back to 1.00 unity.
// Zero per-frame heap allocations; zero external dependencies.
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {

enum class GovernorRegime : uint8_t {
    DeadbandHold  = 0u,
    ScalingDown   = 1u,
    RecoveringUp  = 2u,
    DisabledUnity = 3u,
};

const char* GovernorRegimeName(GovernorRegime r) noexcept;

struct FrameGovernorConfig {
    float targetFps         = 60.0f; // [30..240]
    float minRenderScale    = 0.75f; // [0.65..1.00]
    float deadbandLowRatio  = 0.88f; // Headroom threshold (< 0.88 * targetMs)
    float deadbandHighRatio = 1.06f; // Over-budget threshold (> 1.06 * targetMs)
    float recoveryHoldSec   = 1.50f; // Sustained headroom duration before scaling back up
    float emaTauSec         = 0.12f; // Exponential smoothing time constant for frame times
    float kpDown            = 0.65f; // Proportional gain on over-budget error
    float kiDown            = 0.30f; // Integral gain with strict anti-windup clamping
    float maxDownSlewPerSec = 0.40f; // Bounded fast-down quality slew rate (1/s)
    float maxUpSlewPerSec   = 0.15f; // Gentle asymmetric recovery slew rate (1/s)
};

// Exported C ABI governor state struct (RAW_GetFrameGovernorState).
struct RawFrameGovernorState {
    uint8_t  governorEnabled     = 1u;
    uint8_t  regime              = 0u; // GovernorRegime 0..3
    uint16_t reserved            = 0u;
    float    targetFps           = 60.0f;
    float    targetFrameMs       = 16.6667f;
    float    emaFrameMs          = 16.6667f;
    float    qualityFactor01     = 1.0f; // Unified control output [0..1]
    float    renderScale         = 1.0f; // [minRenderScale .. 1.00]
    float    volumetricQuality01 = 1.0f; // [0.50 .. 1.00]
    float    shadowDistanceScale = 1.0f; // [0.65 .. 1.00]
    float    floraLodScale       = 1.0f; // [0.60 .. 1.00]
    float    giRayBudget01       = 1.0f; // [0.50 .. 1.00]
    uint32_t scaleDownSteps      = 0u;
    uint32_t scaleUpSteps        = 0u;
    uint64_t totalFrames         = 0ull;
};

// Pure deterministic hysteresis-damped PI frame-pacing governor (zero allocations).
class FramePacingGovernor {
public:
    explicit FramePacingGovernor(const FrameGovernorConfig& cfg = {}) noexcept;

    void Reset() noexcept;
    void SetConfig(const FrameGovernorConfig& cfg) noexcept;
    [[nodiscard]] const FrameGovernorConfig& Config() const noexcept { return m_cfg; }

    // Steps the PI deadband governor with measured frame time in milliseconds.
    void Step(double frameMs) noexcept;

    [[nodiscard]] const RawFrameGovernorState& State() const noexcept { return m_state; }

private:
    void ApplyQualityMultipliers() noexcept;

    FrameGovernorConfig   m_cfg{};
    RawFrameGovernorState m_state{};
    float                 m_integralErr    = 0.0f;
    float                 m_headroomSec    = 0.0f;
};

// Runtime lifecycle hooks (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string FrameGovernorInit();
void        FrameGovernorOnPresent(double frameMs);
void        FrameGovernorShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetFrameGovernorState(raw::rt::RawFrameGovernorState* out);
