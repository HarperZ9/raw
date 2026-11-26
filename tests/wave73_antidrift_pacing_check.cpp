// wave73_antidrift_pacing_check.cpp: Standalone C++23 /W4 /WX verification for Wave 73
// (100 Hz deterministic fixed-step frame pacing, input-latency telemetry across Skyrim SE /
// Fallout 4 / Starfield CE2 Native DX12, 10-step Deja null-space temporal anti-drift guard,
// and 4 planted-failure negative controls). Original work, MIT.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/d3d12_bridge/NativeDx12Host.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("  [PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", name);
    }
}

}  // namespace

int main() {
    using namespace raw::versions;
    using namespace raw::bridge;

    std::printf("=== RAW Wave 73: 100 Hz Frame Pacing & 10-Step Anti-Drift ABI Check ===\n");

    const EngineProfile skyrim = ResolveEngineProfileByExe("SkyrimSE.exe", 1, 6);
    const EngineProfile fo4 = ResolveEngineProfileByExe("Fallout4.exe", 1, 10);
    const EngineProfile fo76 = ResolveEngineProfileByExe("Fallout76.exe", 1, 7);
    const EngineProfile starfield = ResolveEngineProfileByExe("Starfield.exe", 1, 14);

    // 1. Cross-title 100 Hz pacing policies
    const FramePacingPolicy skyPol = ResolveFramePacingPolicy(skyrim);
    const FramePacingPolicy fo4Pol = ResolveFramePacingPolicy(fo4);
    const FramePacingPolicy sfPol = ResolveFramePacingPolicy(starfield);
    const FramePacingPolicy fo76Pol = ResolveFramePacingPolicy(fo76);

    Check(std::abs(skyPol.fixedTickHz - 100.0) < 1.0e-9 && skyPol.enableHavokSubstepGuard &&
              std::abs(skyPol.maxInputLatencyMs - 45.0) < 1.0e-9 && !skyPol.multiplayerBlocked,
          "SkyrimSE 100 Hz policy enables Havok 60Hz substep guard and 45ms latency budget");
    Check(std::abs(fo4Pol.fixedTickHz - 100.0) < 1.0e-9 && fo4Pol.enableHavokSubstepGuard &&
              fo4Pol.enablePrevisPacingGuard && std::abs(fo4Pol.maxInputLatencyMs - 45.0) < 1.0e-9,
          "Fallout4 100 Hz policy enables Havok + PreVis pacing guard and 45ms latency budget");
    Check(std::abs(sfPol.fixedTickHz - 100.0) < 1.0e-9 && !sfPol.enableHavokSubstepGuard &&
              sfPol.preferAsyncComputeOverlap && std::abs(sfPol.maxInputLatencyMs - 35.0) < 1.0e-9,
          "Starfield CE2 Native DX12 policy enables async-compute overlap and 35ms latency budget");

    // 2. NativeDx12HostContract surfaces validated Starfield 100 Hz pacing policy
    NativeDx12HostDesc sfDesc{};
    sfDesc.agilitySdkVersion = 722;
    sfDesc.engineProfile = starfield;
    sfDesc.hwProfile.shaderModel = 0x6A;
    NativeDx12FrameInputs sfFrame{};
    sfFrame.width = 3840;
    sfFrame.height = 2160;
    sfFrame.colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sfFrame.normalFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
    const NativeDx12ValidationResult sfVal = ValidateNativeDx12HostContract(sfDesc, sfFrame, true);
    Check(sfVal.ok && sfVal.caps.supportsSm610Preview && sfVal.caps.pacingPolicy.preferAsyncComputeOverlap &&
              std::abs(sfVal.caps.pacingPolicy.fixedTickHz - 100.0) < 1.0e-9,
          "ValidateNativeDx12HostContract exports validated Starfield 100 Hz pacing contract");

    // 3. Deterministic 100 Hz tick exactness across 60 / 90 / 120 / 144 / 240 FPS
    const int rates[] = {60, 90, 120, 144, 240};
    bool allRatesExact = true;
    for (int fps : rates) {
        DeterministicFramePacer pacer(sfPol);
        const double dt = 1.0 / static_cast<double>(fps);
        for (int f = 0; f < fps; ++f) {
            const FixedStepResult r = pacer.Step(dt, 4.0);
            if (r.inputStale || r.overbudgetSpike || r.interpAlpha < 0.0 || r.interpAlpha > 1.0) {
                allRatesExact = false;
            }
        }
        const FramePacingTelemetry tel = pacer.ComputeTelemetry();
        // Over exactly 1.0 s at 100 Hz, fixed ticks emitted must be 99 or 100 (floating-point rounding boundary).
        if (tel.totalFixedTicks < 99u || tel.totalFixedTicks > 100u || !tel.allWithinLatencyBudget || tel.droppedTicks != 0u) {
            allRatesExact = false;
        }
    }
    Check(allRatesExact, "Deterministic 100 Hz pacer emits exact fixed ticks across 60/90/120/144/240 FPS");

    // 4. Frame spike clamping (250 ms stall -> clamped cleanly to maxSubsteps=8, finite alpha)
    {
        DeterministicFramePacer pacer(skyPol);
        const FixedStepResult spike = pacer.Step(0.250, 2.0);
        Check(spike.overbudgetSpike && spike.substeps == kDefaultMaxSubsteps &&
                  spike.droppedSubsteps >= 1u && spike.interpAlpha >= 0.0 && spike.interpAlpha <= 1.0,
              "Frame spike (250ms) clamps cleanly to maxSubsteps=8 without spiral of death");
    }

    // 5. 10-step Deja null-space temporal anti-drift (steady-state + moving foliage contrast)
    {
        const TemporalAntiDriftReport steady = VerifyTenStepTemporalAntiDrift(0.85, 0.85, 0.24, 0.24, 0.25, 1.0e-6);
        Check(steady.ok && steady.maxRelativeLumaDrift <= 1.0e-6 && steady.maxChromaOvershoot <= 1.0e-9,
              "10-step Deja anti-drift steady-state is an exact fixed point (drift <= 1e-6)");

        const TemporalAntiDriftReport moving = VerifyTenStepTemporalAntiDrift(0.35, 1.45, 0.12, 0.34, 0.28, 0.025);
        Check(moving.ok && moving.maxRelativeLumaDrift <= 0.025 && moving.maxChromaOvershoot <= 1.0e-6,
              "10-step Deja anti-drift conserves moving-foliage luminance (<2.5% drift) & 0 chroma overshoot");
    }

    // 6. Planted-Failure Negative Control A: injected +2.5%/frame 10-step accumulation bias MUST be caught!
    {
        const TemporalAntiDriftReport biased =
            VerifyTenStepTemporalAntiDrift(0.35, 1.45, 0.12, 0.34, 0.28, 0.025, 0.025, 1.0);
        Check(!biased.ok && biased.maxRelativeLumaDrift > 0.05,
              "Planted control A: injected +2.5%/step 10-frame accumulation bias caught");
    }

    // 7. Planted-Failure Negative Control B: non-conservative specular MV gain (>1.0) MUST be caught!
    {
        const TemporalAntiDriftReport badSpec =
            VerifyTenStepTemporalAntiDrift(0.50, 1.20, 0.20, 0.30, 0.30, 0.025, 0.0, 1.18);
        Check(!badSpec.ok && !badSpec.conservativeSpecularMv && badSpec.maxChromaOvershoot > 0.01,
              "Planted control B: non-conservative specular MV gain (1.18x) caught");
    }

    // 8. Planted-Failure Negative Control C: input-latency staleness spike (>45ms) MUST be flagged!
    {
        DeterministicFramePacer pacer(skyPol);
        const FixedStepResult stale = pacer.Step(0.01667, 38.0);  // 54.67 ms > 45.0 ms budget
        const FramePacingTelemetry tel = pacer.ComputeTelemetry();
        Check(stale.inputStale && tel.staleInputFrames == 1u && !tel.allWithinLatencyBudget,
              "Planted control C: input-latency staleness violation (54.67ms > 45ms) caught");
    }

    // 9. Planted-Failure Negative Control D: Fallout 76 multiplayer blocked + SkyrimSE rejected on NativeDx12Host!
    {
        DeterministicFramePacer fo76Pacer(fo76Pol);
        const FixedStepResult blocked = fo76Pacer.Step(0.016, 2.0);
        NativeDx12HostDesc wrongHost = sfDesc;
        wrongHost.engineProfile = skyrim;
        const NativeDx12ValidationResult wrongVal = ValidateNativeDx12HostContract(wrongHost, sfFrame, true);
        Check(fo76Pol.multiplayerBlocked && blocked.substeps == 0u && blocked.inputStale &&
                  !wrongVal.ok && wrongVal.error == NativeDx12ContractError::WrongEngineGenForNativeDx12,
              "Planted control D: Fallout 76 multiplayer pacer & SkyrimSE->Starfield NativeDX12 mismatch rejected");
    }

    std::printf("=== Wave 73 Result: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
