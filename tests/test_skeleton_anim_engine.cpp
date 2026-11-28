// test_skeleton_anim_engine.cpp: Verification Suite for `raw::worldgen::RawSkeletonAnimEngine`.
// Exports BOTH `int run_skeleton_anim_engine_tests()` and
// `int raw::worldgen::RunSkeletonAnimEngineTests(void (*checkFn)(bool, const char*) = nullptr)`
// plus `#ifdef RAW_SKELETON_ANIM_TEST_STANDALONE` standalone `main()`.
// Original work, MIT.

#include "RawSkeletonAnimEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace raw::worldgen {
namespace {

constexpr float kPiDegToRad = 3.14159265358979323846f / 180.0f;

bool IsFinite3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

TerrainCellHeightfield MakeSlopedField(uint64_t key, float slopeDegX, float baseZ) {
    TerrainCellHeightfield hf{};
    hf.cellKey     = key;
    hf.originX     = -2048.0f;
    hf.originY     = -2048.0f;
    hf.sizeX       = 4096.0f;
    hf.sizeY       = 4096.0f;
    hf.gridW       = 33u;
    hf.gridH       = 33u;
    hf.waterLevelZ = -14000.0f;
    hf.heights.resize(33u * 33u, baseZ);
    const float tanS = std::tan(slopeDegX * kPiDegToRad);
    const float dx   = hf.sizeX / 32.0f;
    for (uint32_t iy = 0u; iy < 33u; ++iy) {
        for (uint32_t ix = 0u; ix < 33u; ++ix) {
            const float wx = hf.originX + static_cast<float>(ix) * dx;
            hf.heights[static_cast<size_t>(iy) * 33u + ix] = baseZ + wx * tanS;
        }
    }
    return hf;
}

}  // namespace

int RunSkeletonAnimEngineTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (!cond) {
            ++failures;
        }
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    const auto& rig = RawSkeletonAnimEngine::HumanoidRig();

    // =========================================================================
    // Test 1: Planted-failure controls & NaN sanitization
    // =========================================================================
    {
        const BoneDef* badBone = RawSkeletonAnimEngine::GetBoneDef(BoneId::Count);
        const Quat zeroNorm    = QuatNormalize({0.0f, 0.0f, 0.0f, 0.0f});
        const float qNan       = std::numeric_limits<float>::quiet_NaN();
        const Quat nanNorm     = QuatNormalize({qNan, 0.0f, 0.0f, 1.0f});

        // Unreachable IK target far beyond L1+L2 must clamp cleanly without NaN or hyperextension:
        const TwoBoneIkResult overReach = RawSkeletonAnimEngine::SolveTwoBoneIK(
            {0.0f, 0.0f, 60.0f}, {0.0f, 0.0f, 30.0f}, {0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, -500.0f}, {0.0f, 40.0f, 30.0f});

        const bool lenPreserved = std::abs(Length(overReach.midPos - Vec3{0, 0, 60}) - 30.0f) < 1.0e-3f &&
                                  std::abs(Length(overReach.endPos - overReach.midPos) - 30.0f) < 1.0e-3f;

        check(badBone == nullptr &&
                  std::abs(QuatNorm(zeroNorm) - 1.0f) < 1.0e-5f &&
                  std::abs(QuatNorm(nanNorm) - 1.0f) < 1.0e-5f &&
                  overReach.clampedToReach && IsFinite3(overReach.midPos) &&
                  IsFinite3(overReach.endPos) && lenPreserved,
              "skeleton_anim_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: Unit Quaternion Norm & Bone-Length Invariance (< 1e-4) Across All 6 Clips
    // =========================================================================
    {
        float maxQuatErr = 0.0f;
        float maxLenErr  = 0.0f;
        bool  allFinite  = true;

        for (uint8_t c = 0u; c < static_cast<uint8_t>(AnimClipId::Count); ++c) {
            AnimBlendParams bp{};
            bp.primaryClip = static_cast<AnimClipId>(c);
            bp.phaseSec    = 0.37f + static_cast<float>(c) * 0.21f;

            SkeletonPose pose{};
            RawSkeletonAnimEngine::EvaluateCharacterRig({100.0f, -50.0f, 10.0f}, 0.7f, bp, nullptr, pose);

            for (size_t i = 0u; i < kHumanoidBoneCount; ++i) {
                if (!IsFinite3(pose.world[i].pos)) allFinite = false;
                const float qErr = std::abs(QuatNorm(pose.world[i].rot) - 1.0f);
                if (qErr > maxQuatErr) maxQuatErr = qErr;

                const int32_t pIdx = rig[i].parentIndex;
                if (pIdx >= 0) {
                    const float curLen = Length(pose.world[i].pos - pose.world[static_cast<size_t>(pIdx)].pos);
                    const float lErr   = std::abs(curLen - rig[i].restLength);
                    if (lErr > maxLenErr) maxLenErr = lErr;
                }
            }
        }

        std::printf("  [FK Invariants 24 bones x 6 clips] maxQuatErr=%.2e maxLenErr=%.2e\n",
                    maxQuatErr, maxLenErr);

        check(allFinite && maxQuatErr < 1.0e-5f && maxLenErr < 1.0e-4f,
              "skeleton_anim_quaternion_norm_and_bone_length_invariance");
    }

    // =========================================================================
    // Test 3: Analytic 2-Bone Law-of-Cosines IK (< 1e-3) + Pole Lock + FABRIK Fallback
    // =========================================================================
    {
        const Vec3 hip{0.0f, 0.0f, 64.0f};
        const Vec3 knee{0.0f, 0.0f, 34.0f};   // L1 = 30
        const Vec3 ankle{0.0f, 0.0f, 6.0f};   // L2 = 28
        const Vec3 target{4.0f, 14.0f, 22.0f}; // Reachable bent knee target
        const Vec3 pole{0.0f, 60.0f, 34.0f};   // Knee bends forward (+Y)

        const TwoBoneIkResult ik = RawSkeletonAnimEngine::SolveTwoBoneIK(hip, knee, ankle, target, pole);
        const float errL1 = std::abs(Length(ik.midPos - hip) - 30.0f);
        const float errL2 = std::abs(Length(ik.endPos - ik.midPos) - 28.0f);
        const bool  poleCorrect = (ik.midPos.y > 0.5f * (hip.y + target.y));

        // 4-joint FABRIK chain accuracy & bone-length conservation:
        std::vector<Vec3> chain = {{0, 0, 0}, {0, 0, 20}, {0, 0, 40}, {0, 0, 60}};
        const Vec3 fabTarget{28.0f, 16.0f, 32.0f};
        const float fabErr = RawSkeletonAnimEngine::SolveFabrikChain(chain, fabTarget, 16u, 1.0e-4f);
        float maxFabLenErr = 0.0f;
        for (size_t i = 0u; i + 1u < chain.size(); ++i) {
            maxFabLenErr = std::max(maxFabLenErr, std::abs(Length(chain[i + 1u] - chain[i]) - 20.0f));
        }

        std::printf("  [2-Bone IK + FABRIK] reachErr=%.2e errL1=%.2e errL2=%.2e poleOK=%d fabrikErr=%.2e\n",
                    ik.reachError, errL1, errL2, poleCorrect ? 1 : 0, fabErr);

        check(!ik.clampedToReach && ik.reachError < 1.0e-3f && errL1 < 1.0e-4f &&
                  errL2 < 1.0e-4f && poleCorrect && fabErr < 1.0e-3f && maxFabLenErr < 1.0e-4f,
              "skeleton_anim_two_bone_ik_and_fabrik");
    }

    // =========================================================================
    // Test 4: Live RawWorldPhysics Uneven Terrain Slope Foot-IK (Pelvis Drop & Contact)
    // =========================================================================
    {
        RawWorldPhysics slopeWorld;
        slopeWorld.AddTerrainHeightfield(MakeSlopedField(PackCellKey(0, 0), 22.0f, 100.0f));

        // Stand sideways on 22-deg X slope so left foot (-X) is downhill and right foot (+X) is uphill!
        const float rootX = 0.0f;
        const float rootY = 0.0f;
        float centerZ = 0.0f;
        Vec3  slopeN{};
        slopeWorld.SampleTerrainHeight(rootX, rootY, &centerZ, &slopeN);

        AnimBlendParams idle{};
        idle.primaryClip = AnimClipId::Idle;
        SkeletonPose pose{};
        XpbdAttachmentAnchors anchors{};
        RawSkeletonAnimEngine::EvaluateCharacterRig({rootX, rootY, centerZ}, 0.0f, idle,
                                                    &slopeWorld, pose, &anchors);

        const Vec3 footL = pose.world[static_cast<size_t>(BoneId::FootL)].pos;
        const Vec3 footR = pose.world[static_cast<size_t>(BoneId::FootR)].pos;
        float groundZL = 0.0f, groundZR = 0.0f;
        slopeWorld.SampleTerrainHeight(footL.x, footL.y, &groundZL);
        slopeWorld.SampleTerrainHeight(footR.x, footR.y, &groundZR);

        const float errL = std::abs((footL.z - 6.0f) - groundZL);
        const float errR = std::abs((footR.z - 6.0f) - groundZR);

        // Verify zero leg hyperextension: hip-to-ankle distance <= 0.9996 * (30 + 28) = 57.98u:
        const float lenL = Length(footL - pose.world[static_cast<size_t>(BoneId::ThighL)].pos);
        const float lenR = Length(footR - pose.world[static_cast<size_t>(BoneId::ThighR)].pos);

        // Verify XPBD anchors are finite and within character envelope:
        const bool anchorsOk = IsFinite3(anchors.hairRootPos) && IsFinite3(anchors.capeShoulderLeft) &&
                               IsFinite3(anchors.capeShoulderRight) && IsFinite3(anchors.beltBack) &&
                               IsFinite3(anchors.torsoCapsuleA) && IsFinite3(anchors.headCenter) &&
                               (anchors.hairRootPos.z > anchors.torsoCapsuleB.z) &&
                               (anchors.torsoCapsuleB.z > anchors.torsoCapsuleA.z);

        std::printf(
            "  [Terrain Foot-IK 22deg] pelvisDrop=%.2fu footErrL=%.3fu footErrR=%.3fu legDist=(%.1f,%.1f)/58.0 anchors=%d\n",
            pose.pelvisDropZ, errL, errR, lenL, lenR, anchorsOk ? 1 : 0);

        check(pose.leftFootPlanted && pose.rightFootPlanted && pose.pelvisDropZ < -0.5f &&
                  errL < 0.5f && errR < 0.5f && lenL <= 58.0f && lenR <= 58.0f &&
                  (footR.z > footL.z + 2.0f) && anchorsOk,
              "skeleton_anim_terrain_foot_ik_and_xpbd_anchors");
    }

    // =========================================================================
    // Test 5: Blend Tree Continuity + 64-Rig Performance Proof (< 0.25 ms)
    // =========================================================================
    {
        // Sweep Idle -> Run blend alpha from 0.0 -> 1.0 in 20 steps and verify smooth continuous hand/foot motion:
        float maxStepJump = 0.0f;
        SkeletonPose prevPose{};
        for (int s = 0; s <= 20; ++s) {
            AnimBlendParams bp{};
            bp.primaryClip             = AnimClipId::Idle;
            bp.secondaryClip           = AnimClipId::Run;
            bp.blendAlpha              = static_cast<float>(s) / 20.0f;
            bp.phaseSec                = 0.4f;
            bp.upperBodyClip           = AnimClipId::BowDraw;
            bp.upperBodyAdditiveWeight = bp.blendAlpha * 0.5f;
            bp.upperBodyPhaseSec       = 0.8f;

            SkeletonPose curPose{};
            RawSkeletonAnimEngine::EvaluateCharacterRig({0.0f, 0.0f, 0.0f}, 0.0f, bp, nullptr, curPose);
            if (s > 0) {
                for (size_t i = 0u; i < kHumanoidBoneCount; ++i) {
                    const float d = Length(curPose.world[i].pos - prevPose.world[i].pos);
                    if (d > maxStepJump) maxStepJump = d;
                }
            }
            prevPose = curPose;
        }

        // Benchmark 64 complete 24-bone rigs (blend tree + FK + terrain Foot-IK + XPBD anchors):
        RawWorldPhysics benchWorld;
        benchWorld.AddTerrainHeightfield(MakeSlopedField(PackCellKey(0, 0), 12.0f, 0.0f));

        std::vector<SkeletonPose> poses(64u);
        std::vector<XpbdAttachmentAnchors> anchors(64u);
        AnimBlendParams runBp{};
        runBp.primaryClip             = AnimClipId::Walk;
        runBp.secondaryClip           = AnimClipId::Run;
        runBp.blendAlpha              = 0.65f;
        runBp.upperBodyClip           = AnimClipId::MeleeSwing;
        runBp.upperBodyAdditiveWeight = 0.35f;

        // Warmup 5 iterations, then average 20 batches of 64 rigs:
        for (int w = 0; w < 5; ++w) {
            for (size_t r = 0u; r < 64u; ++r) {
                runBp.phaseSec = static_cast<float>(r) * 0.1f;
                RawSkeletonAnimEngine::EvaluateCharacterRig(
                    {static_cast<float>(r) * 15.0f - 480.0f, 0.0f, 0.0f}, 0.25f,
                    runBp, &benchWorld, poses[r], &anchors[r]);
            }
        }

        constexpr int kBenchIters = 20;
        const auto t0 = std::chrono::steady_clock::now();
        for (int it = 0; it < kBenchIters; ++it) {
            for (size_t r = 0u; r < 64u; ++r) {
                runBp.phaseSec = static_cast<float>(it) * 0.016f + static_cast<float>(r) * 0.1f;
                RawSkeletonAnimEngine::EvaluateCharacterRig(
                    {static_cast<float>(r) * 15.0f - 480.0f, 0.0f, 0.0f}, 0.25f,
                    runBp, &benchWorld, poses[r], &anchors[r]);
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double msPer64Rigs =
            std::chrono::duration<double, std::milli>(t1 - t0).count() / static_cast<double>(kBenchIters);

        std::printf("  [Blend Continuity + 64-Rig Perf] maxStepJump=%.2fu 64RigsWithFootIK=%.3fms\n",
                    maxStepJump, msPer64Rigs);

        check(maxStepJump < 8.0f && msPer64Rigs < 0.25,
              "skeleton_anim_blend_continuity_and_64_rig_perf");
    }

    return failures;
}

}  // namespace raw::worldgen

int run_skeleton_anim_engine_tests() {
    return raw::worldgen::RunSkeletonAnimEngineTests(nullptr);
}

#ifdef RAW_SKELETON_ANIM_TEST_STANDALONE
int main() {
    const int failures = run_skeleton_anim_engine_tests();
    std::printf("RawSkeletonAnimEngine standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
