// test_cam_sweep_control.cpp: Wave 65 Track 1 verification suite for headless in-process camera/player
// motion (`cam_sweep`, `cam_stop`, `set_pose`) math, finite/NaN guards, pitch clamp, yaw wrap,
// forward projection, command parsing, and multi-frame sweep termination.
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_CAM_SWEEP_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_view.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace raw::rt {

int RunCamSweepControlTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s cam_sweep_control::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    const float kNan = std::numeric_limits<float>::quiet_NaN();
    const float kInf = std::numeric_limits<float>::infinity();

    // 1. Finite guards, pitch clamping [-1.25, +1.25] and yaw wrapping [0, 2*PI):
    {
        bool ok = true;
        ok = ok && (std::fabs(ClampPitchRad(2.5f) - 1.25f) < 1.0e-5f);
        ok = ok && (std::fabs(ClampPitchRad(-3.0f) - (-1.25f)) < 1.0e-5f);
        ok = ok && (ClampPitchRad(kNan) == 0.0f) && (ClampPitchRad(kInf) == 0.0f);

        const float yNeg = WrapYawTwoPi(-0.25f);
        const float yBig = WrapYawTwoPi(kCamTwoPi * 3.0f + 0.5f);
        ok = ok && (yNeg >= 0.0f && yNeg < kCamTwoPi) && (std::fabs(yNeg - (kCamTwoPi - 0.25f)) < 1.0e-4f);
        ok = ok && (yBig >= 0.0f && yBig < kCamTwoPi) && (std::fabs(yBig - 0.5f) < 1.0e-4f);
        ok = ok && (WrapYawTwoPi(kNan) == 0.0f) && (WrapYawTwoPi(kInf) == 0.0f);

        // Reject NaN/Inf/out-of-bounds in ComputeSetPlayerPose & ComputeCameraSweepStep without mutating pose:
        CameraPose pose{};
        ok = ok && ComputeSetPlayerPose(pose, 100.0f, -200.0f, 50.0f, 2.0f, -0.5f);
        ok = ok && (std::fabs(pose.pitchRad - 1.25f) < 1.0e-5f);
        ok = ok && (std::fabs(pose.yawRad - (kCamTwoPi - 0.5f)) < 1.0e-4f);

        CameraPose badPose = pose;
        ok = ok && !ComputeSetPlayerPose(badPose, kNan, 0.0f, 0.0f, 0.0f, 0.0f);
        ok = ok && !ComputeSetPlayerPose(badPose, 2.0e6f, 0.0f, 0.0f, 0.0f, 0.0f);
        ok = ok && !ComputeCameraSweepStep(badPose, kNan, 0.0f, 0.0f);
        ok = ok && !ComputeCameraSweepStep(badPose, 0.0f, kInf, 0.0f);
        ok = ok && (badPose.pos[0] == pose.pos[0] && badPose.yawRad == pose.yawRad);
        check(ok, "finite_guards_pitch_clamp_and_yaw_wrap");
    }

    // 2. Skyrim Z-up forward vector projection (+Y at yaw=0, +X at yaw=+pi/2) & command parsing:
    {
        bool ok = true;
        CameraPose pNorth{};
        ok = ok && ComputeSetPlayerPose(pNorth, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f);
        ok = ok && ComputeCameraSweepStep(pNorth, 0.0f, 0.0f, 20.0f);
        ok = ok && (std::fabs(pNorth.pos[0] - 0.0f) < 1.0e-4f) &&
             (std::fabs(pNorth.pos[1] - 20.0f) < 1.0e-4f) &&
             (std::fabs(pNorth.pos[2] - 10.0f) < 1.0e-4f);

        CameraPose pEast{};
        ok = ok && ComputeSetPlayerPose(pEast, 0.0f, 0.0f, 10.0f, 0.0f, kCamTwoPi * 0.25f);
        ok = ok && ComputeCameraSweepStep(pEast, 0.0f, 0.0f, 20.0f);
        ok = ok && (std::fabs(pEast.pos[0] - 20.0f) < 1.0e-3f) &&
             (std::fabs(pEast.pos[1] - 0.0f) < 1.0e-3f);

        float dy = 0.0f, dp = 0.0f, df = 0.0f;
        int frames = 0;
        ok = ok && ParseCamSweepArgs("0.008", "0.0012 0.0 240", dy, dp, df, frames);
        ok = ok && (std::fabs(dy - 0.008f) < 1.0e-5f) && (std::fabs(dp - 0.0012f) < 1.0e-5f) && (frames == 240);
        ok = ok && !ParseCamSweepArgs("0.008", "0.0012 0.0 0", dy, dp, df, frames);
        ok = ok && !ParseCamSweepArgs("bad", "0 0 10", dy, dp, df, frames);

        float sx = 0.0f, sy = 0.0f, sz = 0.0f, sp = 0.0f, syw = 0.0f;
        ok = ok && ParseSetPoseArgs("123.5", "-456.0 78.25 0.15 1.57", sx, sy, sz, sp, syw);
        ok = ok && (std::fabs(sx - 123.5f) < 1.0e-4f) && (std::fabs(syw - 1.57f) < 1.0e-4f);
        ok = ok && !ParseSetPoseArgs("123.5", "-456.0", sx, sy, sz, sp, syw);
        check(ok, "forward_projection_and_command_parsing");
    }

    // 3. Multi-frame sweep lifecycle, pitch saturation, yaw rollover, early stop & exact termination:
    {
        bool ok = true;
        CameraSweepState st{};
        ok = ok && !StartCameraSweepState(st, kNan, 0.0f, 0.0f, 10);
        ok = ok && !StartCameraSweepState(st, 0.01f, 0.0f, 0.0f, 0);
        ok = ok && StartCameraSweepState(st, 0.008f, 0.0012f, 0.5f, 240);
        ok = ok && st.active && (st.framesRemaining == 240u);

        CameraPose pose{};
        ComputeSetPlayerPose(pose, 0.0f, 0.0f, 0.0f, 1.15f, 6.20f);
        int finishedCalls = 0;
        for (int i = 0; i < 245; ++i) {
            if (st.active) {
                const bool applied = ComputeCameraSweepStep(pose, st.dYawRadPerFrame, st.dPitchRadPerFrame, st.dFwdUnitsPerFrame);
                if (AdvanceCameraSweepState(st, applied)) ++finishedCalls;
            }
        }
        ok = ok && !st.active && (st.framesRemaining == 0u) && (st.framesExecuted == 240u) &&
             (st.framesApplied == 240u) && (finishedCalls == 1);
        ok = ok && (std::fabs(pose.pitchRad - 1.25f) < 1.0e-5f);
        ok = ok && (pose.yawRad >= 0.0f && pose.yawRad < kCamTwoPi);

        // Early cam_stop terminates immediately:
        ok = ok && StartCameraSweepState(st, 0.01f, 0.0f, 0.0f, 100);
        AdvanceCameraSweepState(st, true);
        AdvanceCameraSweepState(st, true);
        StopCameraSweepState(st);
        ok = ok && !st.active && (st.framesRemaining == 0u) && (st.framesExecuted == 2u);
        check(ok, "multi_frame_sweep_lifecycle_and_early_stop");
    }

    // 4. Wave 66: set_time clamping/parsing, auto_dismiss parsing & non-blocking modal dismiss state machine:
    {
        bool ok = true;
        float h = 0.0f;
        ok = ok && ParseSetTimeArg("13.5", h) && (std::fabs(h - 13.5f) < 1.0e-4f);
        ok = ok && ParseSetTimeArg("-5.0", h) && (std::fabs(h - 0.0f) < 1.0e-4f);
        ok = ok && ParseSetTimeArg("28.0", h) && (std::fabs(h - 23.99f) < 1.0e-4f);
        ok = ok && !ParseSetTimeArg("not_a_number", h);
        ok = ok && !ComputeSetGameHour(h, kNan) && !ComputeSetGameHour(h, kInf);

        bool autoEn = false;
        ok = ok && ParseAutoDismissArg("1", autoEn) && autoEn;
        ok = ok && ParseAutoDismissArg("0", autoEn) && !autoEn;
        ok = ok && !ParseAutoDismissArg("maybe", autoEn);

        // Explicit dismiss_modal: frame 0 -> PostRight, frame 1 -> None, frame 2 -> PostReturn (exact 2-frame spacing):
        ModalDismissState md{};
        TriggerModalDismiss(md);
        const ModalKeyAction a0 = StepModalDismissState(md, false, false);
        const ModalKeyAction a1 = StepModalDismissState(md, false, false);
        const ModalKeyAction a2 = StepModalDismissState(md, false, false);
        const ModalKeyAction a3 = StepModalDismissState(md, false, false);
        ok = ok && (a0 == ModalKeyAction::PostRight) && (a1 == ModalKeyAction::None) &&
             (a2 == ModalKeyAction::PostReturn) && (a3 == ModalKeyAction::None) &&
             (md.dismissedCount == 1u) && (md.phase == ModalDismissPhase::Idle);

        // Auto-dismiss while cam_sweep active respects cooldown, then triggers automatically when paused:
        ok = ok && (StepModalDismissState(md, true, true) == ModalKeyAction::None); // inside cooldown
        for (int i = 0; i < 20; ++i) StepModalDismissState(md, false, false);       // drain cooldown
        const ModalKeyAction autoRight = StepModalDismissState(md, true, true);
        const ModalKeyAction autoWait  = StepModalDismissState(md, true, true);
        const ModalKeyAction autoRet   = StepModalDismissState(md, true, true);
        ok = ok && (autoRight == ModalKeyAction::PostRight) && (autoWait == ModalKeyAction::None) &&
             (autoRet == ModalKeyAction::PostReturn) && (md.dismissedCount == 2u);

        // Auto-dismiss disabled does not trigger on paused modal:
        md.autoDismissEnabled = false;
        md.cooldownFrames = 0u;
        ok = ok && (StepModalDismissState(md, true, true) == ModalKeyAction::None);
        check(ok, "set_time_and_modal_dismiss_state_machine");
    }

    return fail;
}

}  // namespace raw::rt

int run_cam_sweep_control_tests() {
    return raw::rt::RunCamSweepControlTests(nullptr);
}

#ifndef RAW_CAM_SWEEP_NO_MAIN
int main() {
    const int fail = run_cam_sweep_control_tests();
    std::printf("cam_sweep_control: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
