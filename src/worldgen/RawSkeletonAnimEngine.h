#pragma once
// RawSkeletonAnimEngine.h: Clean-Room Procedural 24-Bone Character Skeleton Animation,
// Analytic 2-Bone / FABRIK IK, Live Terrain Foot-IK & XPBD Attachment Anchor Solver (`raw::worldgen`).
// Pure portable C++23 with zero Windows, D3D12, or proprietary SDK dependencies.
// Features:
//   - 24-bone humanoid hierarchy (Pelvis, Spine1-3, Neck, Head, Clavicle/UpperArm/Forearm/Hand L/R,
//     Thigh/Calf/Foot/Toe L/R, HairRootAnchor, CapeClaspAnchor) with unit quaternion FK & additive blending
//   - Analytic 2-bone Law-of-Cosines IK with pole-vector knee/elbow lock + iterative FABRIK fallback
//   - Live `RawWorldPhysics` heightfield Terrain Foot-IK (pelvis drop, ankle normal pitch/roll, zero hyperextension)
//   - Procedural locomotion & combat blend tree (Idle, Walk, Run, Crouch, MeleeSwing, BowDraw)
//   - World-space XPBD attachment anchor export (hair roots, cape shoulders, belt, torso/head colliders).
// Original work, MIT.

#include "RawWorldPhysics.h"

#include <array>
#include <cstdint>
#include <vector>

namespace raw::worldgen {

inline constexpr uint32_t kHumanoidBoneCount = 24u;

enum class BoneId : uint8_t {
    Pelvis          = 0,
    Spine1          = 1,
    Spine2          = 2,
    Spine3          = 3,
    Neck            = 4,
    Head            = 5,
    ClavicleL       = 6,
    UpperArmL       = 7,
    ForearmL        = 8,
    HandL           = 9,
    ClavicleR       = 10,
    UpperArmR       = 11,
    ForearmR        = 12,
    HandR           = 13,
    ThighL          = 14,
    CalfL           = 15,
    FootL           = 16,
    ToeL            = 17,
    ThighR          = 18,
    CalfR           = 19,
    FootR           = 20,
    ToeR            = 21,
    HairRootAnchor  = 22,
    CapeClaspAnchor = 23,
    Count           = 24,
};

enum class AnimClipId : uint8_t {
    Idle       = 0,
    Walk       = 1,
    Run        = 2,
    Crouch     = 3,
    MeleeSwing = 4,
    BowDraw    = 5,
    Count      = 6,
};

#ifndef RAW_WORLDGEN_QUAT_DEFINED
#define RAW_WORLDGEN_QUAT_DEFINED
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};
#endif

struct BoneTransform {
    Vec3 pos{};
    Quat rot{};
};

struct BoneDef {
    BoneId      id          = BoneId::Pelvis;
    int32_t     parentIndex = -1; // Strictly < self index for single-pass FK
    const char* name        = "";
    Vec3        restLocalOffset{};
    float       restLength  = 0.0f;
};

struct SkeletonPose {
    std::array<BoneTransform, kHumanoidBoneCount> local{};
    std::array<BoneTransform, kHumanoidBoneCount> world{};
    float pelvisDropZ      = 0.0f;
    bool  leftFootPlanted  = false;
    bool  rightFootPlanted = false;
    Vec3  leftFootGroundNormal{0.0f, 0.0f, 1.0f};
    Vec3  rightFootGroundNormal{0.0f, 0.0f, 1.0f};
};

struct AnimBlendParams {
    AnimClipId primaryClip             = AnimClipId::Idle;
    AnimClipId secondaryClip           = AnimClipId::Walk;
    float      blendAlpha              = 0.0f; // [0..1] SLERP blend between primary & secondary
    float      phaseSec                = 0.0f; // Continuous clip clock (seconds)
    AnimClipId upperBodyClip           = AnimClipId::MeleeSwing;
    float      upperBodyAdditiveWeight = 0.0f; // [0..1] additive upper-body combat overlay
    float      upperBodyPhaseSec       = 0.0f;
};

struct TwoBoneIkResult {
    Vec3  midPos{};
    Vec3  endPos{};
    float reachError     = 0.0f;
    bool  clampedToReach = false;
};

struct XpbdAttachmentAnchors {
    Vec3 hairRootPos{};
    Vec3 capeShoulderLeft{};
    Vec3 capeShoulderRight{};
    Vec3 capeClaspCenter{};
    Vec3 beltLeft{};
    Vec3 beltRight{};
    Vec3 beltBack{};
    Vec3 torsoCapsuleA{};
    Vec3 torsoCapsuleB{};
    Vec3 headCenter{};
};

// Unit quaternion helpers (strict unit norm, shortest-arc SLERP):
Quat  QuatIdentity() noexcept;
float QuatNorm(Quat q) noexcept;
Quat  QuatNormalize(Quat q) noexcept;
Quat  QuatConjugate(Quat q) noexcept;
Quat  QuatMul(Quat a, Quat b) noexcept;
Vec3  QuatRotateVec(Quat q, Vec3 v) noexcept;
Quat  QuatFromAxisAngle(Vec3 axis, float rad) noexcept;
Quat  QuatFromEulerXYZ(float rx, float ry, float rz) noexcept;
Quat  QuatFromToRotation(Vec3 fromDir, Vec3 toDir) noexcept;
Quat  QuatSlerp(Quat a, Quat b, float t) noexcept;

class RawSkeletonAnimEngine {
public:
    static const std::array<BoneDef, kHumanoidBoneCount>& HumanoidRig();
    static const BoneDef* GetBoneDef(BoneId id);
    static const char*    GetClipName(AnimClipId clip);

    // Evaluates a single procedural animation clip in local bone space:
    static void EvaluateProceduralClip(AnimClipId clip, float phaseSec,
                                       std::array<BoneTransform, kHumanoidBoneCount>& outLocal);

    // Evaluates blended base clip + optional upper-body additive overlay:
    static void EvaluateBlendTree(const AnimBlendParams& params,
                                  std::array<BoneTransform, kHumanoidBoneCount>& outLocal);

    // Single-pass forward kinematics transforming local pose into world space at (rootPos, rootYawRad):
    static void ComputeForwardKinematics(Vec3 rootPos, float rootYawRad,
                                         const std::array<BoneTransform, kHumanoidBoneCount>& local,
                                         SkeletonPose& outPose);

    // Analytic 2-bone Law-of-Cosines IK with pole-vector knee/elbow plane lock (zero hyperextension):
    static TwoBoneIkResult SolveTwoBoneIK(Vec3 rootPos, Vec3 midPos, Vec3 endPos,
                                          Vec3 targetPos, Vec3 poleWorldPos);

    // General N-joint iterative FABRIK fallback preserving exact bone lengths:
    static float SolveFabrikChain(std::vector<Vec3>& joints, Vec3 targetPos,
                                  uint32_t maxIterations = 12u, float tolerance = 1.0e-4f);

    // Applies live `RawWorldPhysics` heightfield/BVH Terrain Foot-IK (pelvis drop + 2-bone leg IK + ankle alignment):
    static void ApplyTerrainFootIK(Vec3 rootPos, float rootYawRad,
                                   const RawWorldPhysics& physics,
                                   SkeletonPose& ioPose);

    // Full single-call rig update: blend tree -> FK -> optional terrain Foot-IK -> XPBD attachment anchors:
    static void EvaluateCharacterRig(Vec3 rootPos, float rootYawRad,
                                     const AnimBlendParams& params,
                                     const RawWorldPhysics* physicsOpt,
                                     SkeletonPose& outPose,
                                     XpbdAttachmentAnchors* outAnchorsOpt = nullptr);

    // Extracts world-space XPBD cloth/hair/belt anchors & torso/head collision proxies from a solved pose:
    static XpbdAttachmentAnchors ExtractXpbdAnchors(const SkeletonPose& pose);
};

}  // namespace raw::worldgen
