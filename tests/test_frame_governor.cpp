// test_frame_governor.cpp: Wave 54 Deterministic Verification for Hysteresis-Damped PI Frame Governor (`raw::rt`).
// Compiles both as a standalone binary (RAW_FRAME_GOVERNOR_STANDALONE_MAIN) and as part of ce_runtime_check.
// Original work, MIT.

#include "rt_frame_governor.h"
#include "rt_live_config.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raw::rt {

namespace {

inline bool IsFiniteState(const RawFrameGovernorState& s) noexcept {
    return std::isfinite(s.targetFps) &&
           std::isfinite(s.targetFrameMs) &&
           std::isfinite(s.emaFrameMs) &&
           std::isfinite(s.qualityFactor01) &&
           std::isfinite(s.renderScale) &&
           std::isfinite(s.volumetricQuality01) &&
           std::isfinite(s.shadowDistanceScale) &&
           std::isfinite(s.floraLodScale) &&
           std::isfinite(s.giRayBudget01);
}

}  // namespace

int RunFrameGovernorTests(void (*checkFn)(bool, const char*) = nullptr) {
    int localFail = 0;
    auto Check = [&](bool cond, const char* msg) {
        if (checkFn) {
            checkFn(cond, msg);
        } else {
            std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
        }
        if (!cond) ++localFail;
    };

    // -------------------------------------------------------------------------
    // Check 1: Deadband Stability Proof (15.5..17.2 ms oscillation @ 60 FPS)
    // -------------------------------------------------------------------------
    {
        FrameGovernorConfig cfg{};
        cfg.targetFps = 60.0f; // targetMs = 16.667 ms, deadband = [14.667 .. 17.667] ms
        FramePacingGovernor gov(cfg);

        bool allFinite = true;
        bool stayedDeadband = true;
        for (int i = 0; i < 180; ++i) {
            // Smooth oscillation strictly inside [15.5 .. 17.2] ms
            const double phase = static_cast<double>(i) * 0.37;
            const double frameMs = 16.35 + 0.85 * std::sin(phase);
            gov.Step(frameMs);
            const auto& st = gov.State();
            if (!IsFiniteState(st)) allFinite = false;
            if (st.regime != static_cast<uint8_t>(GovernorRegime::DeadbandHold)) {
                stayedDeadband = false;
            }
        }

        const auto& st = gov.State();
        std::printf("[FrameGovernor] Check1 Deadband: frames=%llu ema=%.2fms Q=%.3f render=%.3f down=%u up=%u regime=%s\n",
                    static_cast<unsigned long long>(st.totalFrames),
                    double(st.emaFrameMs), double(st.qualityFactor01), double(st.renderScale),
                    st.scaleDownSteps, st.scaleUpSteps,
                    GovernorRegimeName(static_cast<GovernorRegime>(st.regime)));

        Check(allFinite && stayedDeadband &&
              st.totalFrames == 180ull &&
              st.scaleDownSteps == 0u &&
              st.scaleUpSteps == 0u &&
              std::fabs(st.qualityFactor01 - 1.0f) < 1.0e-5f &&
              std::fabs(st.renderScale - 1.0f) < 1.0e-5f &&
              std::fabs(st.volumetricQuality01 - 1.0f) < 1.0e-5f &&
              std::fabs(st.shadowDistanceScale - 1.0f) < 1.0e-5f &&
              std::fabs(st.floraLodScale - 1.0f) < 1.0e-5f &&
              std::fabs(st.giRayBudget01 - 1.0f) < 1.0e-5f,
              "FrameGovernor deadband stability: 15.5..17.2ms oscillation yields 0 scale steps & exact 1.00 unity");
    }

    // -------------------------------------------------------------------------
    // Check 2: Over-Budget Convergence & Bounded Slew Proof (sustained 28.0 ms)
    // -------------------------------------------------------------------------
    FramePacingGovernor sharedGov{};
    {
        FrameGovernorConfig cfg{};
        cfg.targetFps      = 60.0f;
        cfg.minRenderScale = 0.75f;
        sharedGov.SetConfig(cfg);
        sharedGov.Reset();

        bool allFinite = true;
        bool slewBounded = true;
        bool monotonicDown = true;
        float prevQ = sharedGov.State().qualityFactor01;
        const float maxStepQ = cfg.maxDownSlewPerSec * 0.028f + 1.0e-4f;

        for (int i = 0; i < 120; ++i) {
            sharedGov.Step(28.0);
            const auto& st = sharedGov.State();
            if (!IsFiniteState(st)) allFinite = false;
            const float deltaQ = prevQ - st.qualityFactor01;
            if (deltaQ < -1.0e-5f) monotonicDown = false;
            if (deltaQ > maxStepQ) slewBounded = false;
            prevQ = st.qualityFactor01;
        }

        const auto& st = sharedGov.State();
        std::printf("[FrameGovernor] Check2 OverBudget(28ms x120): ema=%.2fms Q=%.3f render=%.3f vol=%.3f shadow=%.3f flora=%.3f gi=%.3f down=%u\n",
                    double(st.emaFrameMs), double(st.qualityFactor01),
                    double(st.renderScale), double(st.volumetricQuality01),
                    double(st.shadowDistanceScale), double(st.floraLodScale),
                    double(st.giRayBudget01), st.scaleDownSteps);

        Check(allFinite && slewBounded && monotonicDown &&
              st.scaleDownSteps > 10u &&
              st.renderScale < 0.85f && st.renderScale >= 0.75f &&
              st.volumetricQuality01 < 0.80f && st.volumetricQuality01 >= 0.50f &&
              st.giRayBudget01 < 0.80f && st.giRayBudget01 >= 0.50f &&
              st.shadowDistanceScale >= 0.65f && st.shadowDistanceScale < 0.90f &&
              st.floraLodScale >= 0.60f && st.floraLodScale < 0.90f &&
              st.regime == static_cast<uint8_t>(GovernorRegime::ScalingDown),
              "FrameGovernor over-budget convergence: 28ms frames smoothly shed quality within bounded slew & [0.75..0.85) renderScale");
    }

    // -------------------------------------------------------------------------
    // Check 3: Sustained Headroom Recovery Proof (10.0 ms after scaled-down)
    // -------------------------------------------------------------------------
    {
        const float qBeforeRecovery = sharedGov.State().qualityFactor01;
        const uint32_t upBefore = sharedGov.State().scaleUpSteps;

        // First 1.0s of 10ms frames (100 frames) MUST NOT prematurely jump back up (< 1.5s hold)
        bool noPrematureJump = true;
        bool allFinite = true;
        for (int i = 0; i < 100; ++i) {
            sharedGov.Step(10.0);
            const auto& st = sharedGov.State();
            if (!IsFiniteState(st)) allFinite = false;
            if (st.scaleUpSteps > upBefore || st.qualityFactor01 > qBeforeRecovery + 1.0e-5f) {
                noPrematureJump = false;
            }
        }

        // Next 13.0s of 10ms frames (1300 frames): smoothly recover back to exact 1.00 unity with zero overshoot
        bool noOvershoot = true;
        bool upSlewBounded = true;
        float prevQ = sharedGov.State().qualityFactor01;
        const float maxUpStep = sharedGov.Config().maxUpSlewPerSec * 0.010f + 1.0e-4f;
        for (int i = 0; i < 1300; ++i) {
            sharedGov.Step(10.0);
            const auto& st = sharedGov.State();
            if (!IsFiniteState(st)) allFinite = false;
            if (st.qualityFactor01 > 1.0f + 1.0e-6f || st.renderScale > 1.0f + 1.0e-6f) {
                noOvershoot = false;
            }
            const float deltaUp = st.qualityFactor01 - prevQ;
            if (deltaUp > maxUpStep) upSlewBounded = false;
            prevQ = st.qualityFactor01;
        }

        const auto& st = sharedGov.State();
        std::printf("[FrameGovernor] Check3 Recovery(10ms x1400): ema=%.2fms Q=%.3f render=%.3f vol=%.3f gi=%.3f up=%u noPremature=%u\n",
                    double(st.emaFrameMs), double(st.qualityFactor01),
                    double(st.renderScale), double(st.volumetricQuality01),
                    double(st.giRayBudget01), st.scaleUpSteps, noPrematureJump ? 1u : 0u);

        Check(allFinite && noPrematureJump && noOvershoot && upSlewBounded &&
              st.scaleUpSteps > 50u &&
              std::fabs(st.qualityFactor01 - 1.0f) < 1.0e-5f &&
              std::fabs(st.renderScale - 1.0f) < 1.0e-5f &&
              std::fabs(st.volumetricQuality01 - 1.0f) < 1.0e-5f &&
              std::fabs(st.shadowDistanceScale - 1.0f) < 1.0e-5f &&
              std::fabs(st.floraLodScale - 1.0f) < 1.0e-5f &&
              std::fabs(st.giRayBudget01 - 1.0f) < 1.0e-5f,
              "FrameGovernor sustained headroom recovery: holds 1.5s without premature jump, smoothly recovers to 1.00 unity");
    }

    // -------------------------------------------------------------------------
    // Check 4: RAW_GetFrameGovernorState C ABI + [Performance] INI Round-Trip & DynamicGovernor=0 Unity
    // -------------------------------------------------------------------------
    {
        // 4a. Default INI serialization & parsing + clamping
        const LiveEngineConfig defCfg = DefaultLiveEngineConfig();
        const std::string defIni = FormatDefaultLiveConfigIni(defCfg);
        const bool hasPerfSection =
            defIni.find("[Performance]") != std::string::npos &&
            defIni.find("DynamicGovernor=1") != std::string::npos &&
            defIni.find("TargetFps=60") != std::string::npos;

        LiveEngineConfig parsed144{};
        const bool parseOk = ParseLiveConfigIni(
            "[Performance]\nDynamicGovernor=0\nTargetFps=144\n", parsed144);

        LiveEngineConfig clampHi{};
        ParseLiveConfigIni("[Performance]\nTargetFps=999\n", clampHi);
        LiveEngineConfig clampLo{};
        ParseLiveConfigIni("[Performance]\nTargetFps=5\n", clampLo);

        // 4b. Live runtime C ABI check: scale down when enabled, then exact 1.00 unity when DynamicGovernor=0
        LiveEngineConfig activeCfg = DefaultLiveEngineConfig();
        activeCfg.dynamicGovernorEnabled = true;
        activeCfg.targetFps = 60.0f;
        SetLiveConfigSnapshot(activeCfg);

        const std::string initMsg = FrameGovernorInit();
        for (int i = 0; i < 120; ++i) {
            FrameGovernorOnPresent(32.0);
        }
        RawFrameGovernorState stActive{};
        const int rcActive = RAW_GetFrameGovernorState(&stActive);

        // Toggle DynamicGovernor=0 live
        activeCfg.dynamicGovernorEnabled = false;
        SetLiveConfigSnapshot(activeCfg);

        RawFrameGovernorState stDisabled{};
        const int rcDisabled = RAW_GetFrameGovernorState(&stDisabled);
        const int rcNull     = RAW_GetFrameGovernorState(nullptr);

        // Restore clean defaults
        SetLiveConfigSnapshot(DefaultLiveEngineConfig());
        FrameGovernorShutdown();

        // 4c. F10 LiveStatusDump formatting includes [Frame Pacing Governor]
        LiveStatusDumpData dump{};
        dump.govRegime       = stActive.regime;
        dump.govEmaMs        = stActive.emaFrameMs;
        dump.govRenderScale  = stActive.renderScale;
        dump.govVolumetric01 = stActive.volumetricQuality01;
        dump.govShadowScale  = stActive.shadowDistanceScale;
        dump.govFloraScale   = stActive.floraLodScale;
        dump.govGiBudget01   = stActive.giRayBudget01;
        const std::string dumpTxt = FormatLiveStatusDump(dump);
        const bool hasDumpSection =
            dumpTxt.find("[Frame Pacing Governor]") != std::string::npos &&
            dumpTxt.find("Scale(render=") != std::string::npos &&
            dumpTxt.find("gi=") != std::string::npos;

        std::printf("[FrameGovernor] Check4 C-ABI/INI: init='%s' activeRender=%.3f disabledRender=%.3f regime=%s\n",
                    initMsg.c_str(), double(stActive.renderScale), double(stDisabled.renderScale),
                    GovernorRegimeName(static_cast<GovernorRegime>(stDisabled.regime)));

        Check(hasPerfSection && parseOk &&
              !parsed144.dynamicGovernorEnabled &&
              std::fabs(parsed144.targetFps - 144.0f) < 1.0e-4f &&
              std::fabs(clampHi.targetFps - 240.0f) < 1.0e-4f &&
              std::fabs(clampLo.targetFps - 30.0f) < 1.0e-4f &&
              rcNull == 0 && rcActive == 1 && rcDisabled == 1 &&
              stActive.governorEnabled == 1u &&
              stActive.renderScale < 0.85f &&
              stDisabled.governorEnabled == 0u &&
              stDisabled.regime == static_cast<uint8_t>(GovernorRegime::DisabledUnity) &&
              std::fabs(stDisabled.renderScale - 1.0f) < 1.0e-6f &&
              std::fabs(stDisabled.volumetricQuality01 - 1.0f) < 1.0e-6f &&
              std::fabs(stDisabled.shadowDistanceScale - 1.0f) < 1.0e-6f &&
              std::fabs(stDisabled.floraLodScale - 1.0f) < 1.0e-6f &&
              std::fabs(stDisabled.giRayBudget01 - 1.0f) < 1.0e-6f &&
              hasDumpSection,
              "FrameGovernor C ABI & INI round-trip: TargetFps clamp, DynamicGovernor=0 returns exact 1.00 unity, F10 dump formatted");
    }

    return localFail;
}

}  // namespace raw::rt

#ifdef RAW_FRAME_GOVERNOR_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 54: Hysteresis-Damped PI Frame-Pacing Governor Suite ===\n");
    const int fail = raw::rt::RunFrameGovernorTests(nullptr);
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
