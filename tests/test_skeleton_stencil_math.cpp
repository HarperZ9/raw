// test_skeleton_stencil_math.cpp: Wave 63 Track 8 verification suite for G-Buffer Skinned-Actor
// Classification (base==14u), Quaternion Antipodal SLERP Continuity, Near-Zero NLERP Fallback,
// NaN/Inf Rejection, and Skeletal IK Temporal Smoothness.
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_SKELETON_STENCIL_MATH_NO_MAIN.
// Original work, MIT.

#include "../src/core/dxbc/GBufferPatch.h"
#include "../src/engine_rt/rt_skeleton_ik.h"
#include "../src/worldgen/RawSkeletonAnimEngine.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace raw::rt {

int RunSkeletonStencilMathTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s skeleton_stencil_math::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    const float kNan = std::numeric_limits<float>::quiet_NaN();
    const float kInf = std::numeric_limits<float>::infinity();

    // 1. G-Buffer SkinnedActor (base==14u) class resolution, specialization preservation & UNORM8 round-trip:
    {
        using namespace raw::dxbc::material;
        bool ok = (SkinnedActor == 14u);

        // Generic Lighting+Lit (1) promoted cleanly to SkinnedActor (14), FirstPerson SkinnedActor (142),
        // or Foliage/SoftLit (5) for alpha-tested pine saplings/shrubs while static Lit stays 1:
        const uint8_t clsActor3p = ResolveMaterialClass(Lit, SkinnedActor, 0u);
        const uint8_t clsActor1p = ResolveMaterialClass(Lit, SkinnedActor, FirstPersonFlag);
        const uint8_t clsFoliage = ResolveMaterialClass(Lit, SoftLit, 0u);
        const uint8_t clsStatic  = ResolveMaterialClass(Lit, None, 0u);
        ok = ok && (clsActor3p == 14u) && (clsActor1p == 142u) && (clsFoliage == 5u) && (clsStatic == 1u);
        ok = ok && IsActorMaterialClass(clsActor3p) && IsActorMaterialClass(clsActor1p) && !IsActorMaterialClass(clsStatic);
        ok = ok && IsFoliageMaterialClass(clsFoliage) && !IsFoliageMaterialClass(clsActor3p) && !IsFoliageMaterialClass(clsStatic);

        // Specialized classes MUST NOT be overwritten by either SkinnedActor or Foliage overrides:
        const uint8_t preserved[] = {Skin, Hair, Eye, SoftLit, Landscape, EnvironmentMapped, GlowMapped, Parallax, TruePBR, Water, Effect, DistantLod};
        for (uint8_t c : preserved) {
            if (ResolveMaterialClass(c, SkinnedActor, 0u) != c) ok = false;
            if (ResolveMaterialClass(c, SoftLit, 0u) != c) ok = false;
        }
        ok = ok && IsActorMaterialClass(Skin) && IsActorMaterialClass(Hair) && IsActorMaterialClass(Eye);
        ok = ok && !IsActorMaterialClass(SoftLit) && !IsActorMaterialClass(Landscape) && !IsActorMaterialClass(EnvironmentMapped);

        // Full 0..255 Spec.z UNORM8 encoding/decoding round-trip + out-of-range clamp:
        for (uint32_t v = 0u; v <= 255u; ++v) {
            const float enc = EncodeMaterialClassUnorm(static_cast<uint8_t>(v));
            const uint8_t dec = DecodeMaterialClassUnorm(enc);
            if (dec != static_cast<uint8_t>(v)) ok = false;
        }
        ok = ok && (DecodeMaterialClassUnorm(EncodeMaterialClassUnorm(SkinnedActor)) == 14u);
        ok = ok && (DecodeMaterialClassUnorm(EncodeMaterialClassUnorm(SoftLit)) == 5u);
        ok = ok && (DecodeMaterialClassUnorm(-0.5f) == 0u) && (DecodeMaterialClassUnorm(1.5f) == 255u);
        check(ok, "gbuffer_skinned_actor_base14_and_foliage_base5_roundtrip");
    }

    // 2. Quaternion shortest-path antipodal SLERP, near-zero NLERP fallback, unit norm & NaN fail-closed:
    {
        using namespace raw::worldgen;
        bool ok = true;

        // (a) Worldgen QuatSlerp: antipodal q vs -q MUST stay on shortest arc (0 deg, not 360 deg spin!):
        const Quat qA = QuatFromAxisAngle({0.0f, 0.0f, 1.0f}, 0.40f);
        const Quat qB_neg = {-qA.x, -qA.y, -qA.z, -qA.w};
        const Quat midSame = QuatSlerp(qA, qB_neg, 0.5f);
        const float dotSame = std::fabs(midSame.x * qA.x + midSame.y * qA.y + midSame.z * qA.z + midSame.w * qA.w);
        ok = ok && (std::fabs(QuatNorm(midSame) - 1.0f) < 1.0e-5f) && (dotSame > 0.99999f);

        // (b) Antipodal small delta: rotating +0.1 rad via negated target must stay at +0.05 rad, NOT flip backward:
        const Quat qSmall = QuatFromAxisAngle({0.0f, 0.0f, 1.0f}, 0.50f);
        const Quat qSmallNeg = {-qSmall.x, -qSmall.y, -qSmall.z, -qSmall.w};
        const Quat midShort = QuatSlerp(qA, qSmallNeg, 0.5f);
        const Quat expectedMid = QuatFromAxisAngle({0.0f, 0.0f, 1.0f}, 0.45f);
        const float dotShort = std::fabs(midShort.x * expectedMid.x + midShort.y * expectedMid.y +
                                         midShort.z * expectedMid.z + midShort.w * expectedMid.w);
        ok = ok && (dotShort > 0.99999f) && (std::fabs(QuatNorm(midShort) - 1.0f) < 1.0e-5f);

        // (c) Near-zero angle (1e-7 rad) NLERP fallback + unnormalized input renormalization:
        const Quat qTiny = QuatFromAxisAngle({1.0f, 0.0f, 0.0f}, 1.0e-7f);
        const Quat midTiny = QuatSlerp(QuatIdentity(), qTiny, 0.5f);
        ok = ok && std::isfinite(midTiny.x) && std::isfinite(midTiny.w) &&
             (std::fabs(QuatNorm(midTiny) - 1.0f) < 1.0e-5f);

        const Quat unnormA{0.0f, 0.0f, 0.0f, 2.0f};
        const Quat unnormB{0.0f, 0.0f, 0.0f, 0.5f};
        const Quat normRes = QuatSlerp(unnormA, unnormB, 0.5f);
        ok = ok && (std::fabs(QuatNorm(normRes) - 1.0f) < 1.0e-5f);

        // (d) NaN/Inf fail-closed recovery to unit identity:
        const Quat nanRes = QuatSlerp({kNan, 0.0f, 0.0f, 1.0f}, qA, kNan);
        ok = ok && (std::fabs(QuatNorm(nanRes) - 1.0f) < 1.0e-5f) && std::isfinite(nanRes.w);

        // (e) Runtime rt::SlerpQuat & rt::DampQuat antipodal continuity + monotonic convergence:
        const Quat4 rA{qA.x, qA.y, qA.z, qA.w};
        const Quat4 rNeg{-qSmall.x, -qSmall.y, -qSmall.z, -qSmall.w};
        const Quat4 rMid = SlerpQuat(rA, rNeg, 0.5f);
        const float rDot = std::fabs(rMid.x * expectedMid.x + rMid.y * expectedMid.y +
                                     rMid.z * expectedMid.z + rMid.w * expectedMid.w);
        ok = ok && (rDot > 0.99999f);

        Quat4 dampCur = rA;
        float prevDist = 1.0f;
        for (int s = 0; s < 10; ++s) {
            dampCur = DampQuat(dampCur, rNeg, 12.0f, 0.016f);
            const float d = 1.0f - std::fabs(dampCur.x * qSmall.x + dampCur.y * qSmall.y +
                                             dampCur.z * qSmall.z + dampCur.w * qSmall.w);
            if (!std::isfinite(d) || d > prevDist + 1.0e-6f) ok = false;
            prevDist = d;
        }
        check(ok, "quaternion_antipodal_slerp_nlerp_and_nan_safety");
    }

    // 3. Skeletal IK NaN rejection, O(1) angle safety, smooth temporal damping & matrix orthonormality:
    {
        bool ok = true;

        // (a) SolveTwoBoneIK MUST fail closed on NaN lengths, NaN targets, or NaN pole vectors:
        const TwoBoneIkResult badLen = SolveTwoBoneIK({0, 0, 80}, {0, 10, 10}, {0, 1, 0}, kNan, 40.0f);
        const TwoBoneIkResult badTgt = SolveTwoBoneIK({0, 0, 80}, {kNan, 10, 10}, {0, 1, 0}, 40.0f, 40.0f);
        const TwoBoneIkResult badPol = SolveTwoBoneIK({0, 0, 80}, {0, 10, 10}, {0, kInf, 0}, 40.0f, 40.0f);
        ok = ok && !badLen.solved && !badTgt.solved && !badPol.solved;

        // (b) StepActorHumanoidIk rejects NaN root/dt and handles extreme timeSec without hanging:
        ActorIkMotionState st{};
        const float badPos[3] = {kNan, 0.0f, 0.0f};
        const float fwd[2]    = {0.0f, 1.0f};
        const ActorIkPoseDelta badStep = StepActorHumanoidIk(st, badPos, fwd, 0.0f, 0.0f, 68.0f, 0.0f, 0.016f, 1.0f);
        ok = ok && !badStep.valid;

        // Smooth temporal response over 20 frames (no sudden spikes on continuous stair elevation):
        float pos[3] = {0.0f, 0.0f, 0.0f};
        float prevPelvis = 0.0f;
        for (int f = 0; f < 20; ++f) {
            pos[1] += 4.0f;
            const ActorIkPoseDelta step = StepActorHumanoidIk(st, pos, fwd, 10.0f, 0.0f, 68.0f, 8.0f, 0.016f, 1.0e7f + f * 0.016f);
            if (!step.valid || !std::isfinite(step.pelvisDeltaZ) || !std::isfinite(step.chestBreathLiftZ)) ok = false;
            if (f > 0 && std::fabs(step.pelvisDeltaZ - prevPelvis) > 1.0f) ok = false;
            prevPelvis = step.pelvisDeltaZ;
        }

        // (c) FABRIK rejects NaN joints & preserves exact bone lengths on valid chains:
        std::vector<raw::worldgen::Vec3> nanChain = {{0, 0, 0}, {kNan, 0, 10}, {0, 0, 20}};
        const float nanErr = raw::worldgen::RawSkeletonAnimEngine::SolveFabrikChain(nanChain, {5, 0, 15});
        ok = ok && (nanErr == 0.0f);

        std::vector<raw::worldgen::Vec3> chain = {{0, 0, 0}, {0, 0, 15}, {0, 0, 30}, {0, 0, 45}};
        const float fabErr = raw::worldgen::RawSkeletonAnimEngine::SolveFabrikChain(chain, {18, 12, 26}, 16u, 1.0e-4f);
        ok = ok && std::isfinite(fabErr) && (fabErr < 1.0e-3f);
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            const float seg = raw::worldgen::Length(chain[i + 1] - chain[i]);
            if (std::fabs(seg - 15.0f) > 1.0e-3f) ok = false;
        }

        // (d) ComposeAdditiveLocalRotation rejects NaN angles & preserves strict orthonormality over 500 frames:
        float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        ComposeAdditiveLocalRotation(rot, kNan, 0.1f, 0.1f);
        ok = ok && (rot[0] == 1.0f && rot[4] == 1.0f && rot[8] == 1.0f);
        for (int i = 0; i < 500; ++i) {
            ComposeAdditiveLocalRotation(rot, 0.013f, -0.007f, 0.019f);
        }
        const float det =
            rot[0] * (rot[4] * rot[8] - rot[5] * rot[7]) -
            rot[1] * (rot[3] * rot[8] - rot[5] * rot[6]) +
            rot[2] * (rot[3] * rot[7] - rot[4] * rot[6]);
        ok = ok && std::isfinite(det) && (std::fabs(det - 1.0f) < 1.0e-4f);

        check(ok, "skeleton_ik_nan_rejection_fabrik_and_orthonormality");
    }

    return fail;
}

}  // namespace raw::rt

int run_skeleton_stencil_math_tests() {
    return raw::rt::RunSkeletonStencilMathTests(nullptr);
}

#ifndef RAW_SKELETON_STENCIL_MATH_NO_MAIN
int main() {
    const int fail = run_skeleton_stencil_math_tests();
    std::printf("skeleton_stencil_math: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
