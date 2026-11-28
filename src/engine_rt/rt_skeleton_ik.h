#pragma once
// rt_skeleton_ik.h: Procedural Full-Body Skeletal Kinematics, 2-Bone Foot/Arm IK & Gaze/Slope Lean (Zero SKSE).
// Runs natively inside raw_engine.dll pre-Main::Draw alongside the 240 Hz XPBD soft-body solver (rt_softbody.h).
// Original work, MIT.

#include <cstdint>

namespace raw::rt {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Quat4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

// Hardened unit-quaternion operations: strict finite checks, shortest-path antipodal SLERP
// (dot < 0 -> negate q1), near-zero sin(theta) normalized NLERP fallback, and exponential temporal damping.
Quat4 NormalizeQuat(Quat4 q) noexcept;
Quat4 SlerpQuat(Quat4 q0, Quat4 q1, float t) noexcept;
Quat4 DampQuat(Quat4 current, Quat4 target, float lambdaPerSec, float dtSec) noexcept;
void  QuatToMat3(Quat4 q, float outRowMajor3x3[9]) noexcept;

struct TwoBoneIkResult {
    bool  solved = false;
    Vec3  midPos{};      // solved knee / elbow joint world position
    Vec3  endPos{};      // solved ankle / wrist world position (clamped to reach)
    float upperBendRad = 0.0f;
    float midBendRad   = 0.0f;
    float reachRatio   = 0.0f;
};

struct FootPlantInput {
    float leftGroundZ  = 0.0f;
    float rightGroundZ = 0.0f;
    float pelvisBaseZ  = 0.0f;
    float maxLegDrop   = 28.0f;   // max pelvis drop on steep stairs/rocks (game units)
    float snowDepthU   = 0.0f;    // live volumetric snow depth sinks boots slightly into powder
    Vec3  leftGroundN  = {0.0f, 0.0f, 1.0f};
    Vec3  rightGroundN = {0.0f, 0.0f, 1.0f};
};

struct FootPlantOutput {
    float pelvisOffsetZ  = 0.0f;
    float leftFootLiftZ  = 0.0f;
    float rightFootLiftZ = 0.0f;
    float leftAnklePitchRad  = 0.0f;
    float leftAnkleRollRad   = 0.0f;
    float rightAnklePitchRad = 0.0f;
    float rightAnkleRollRad  = 0.0f;
    float strideDampMult     = 1.0f;  // snow-weight stride dampening (0.65..1.0)
};

struct SpineGazeOutput {
    float spineYawRad[4]   = {};  // distributed across Spine0, Spine1, Neck, Head
    float spinePitchRad[4] = {};
    float bankRollRad      = 0.0f; // velocity-coupled turn banking lean
    float breathChestLift  = 0.0f; // subtle rhythmic breathing expansion
};

// Live humanoid skeleton bone classification (vanilla Skyrim & XPMSSE compatible).
enum class HumanoidBoneRole : uint8_t {
    None       = 0,
    Pelvis     = 1,
    Spine0     = 2,
    Spine1     = 3,
    Spine2     = 4,
    Neck       = 5,
    Head       = 6,
    LeftThigh  = 7,
    LeftCalf   = 8,
    LeftFoot   = 9,
    RightThigh = 10,
    RightCalf  = 11,
    RightFoot  = 12,
};

struct ActorIkMotionState {
    float    prevRootPos[3]   = {};
    float    prevYawRad       = 0.0f;
    float    smoothTurnVel    = 0.0f;
    float    smoothSpeedU     = 0.0f;
    float    smoothPelvisZ    = 0.0f;
    float    smoothFootDeltaZ = 0.0f;
    bool     initialized      = false;
};

struct ActorIkPoseDelta {
    float pelvisDeltaZ      = 0.0f;
    float spinePitchRad[4]  = {};  // Spine0, Spine1, Spine2/Neck, Head
    float spineYawRad[4]    = {};
    float spineBankRollRad  = 0.0f;
    float chestBreathLiftZ  = 0.0f;
    float leftThighPitchRad = 0.0f;
    float leftCalfPitchRad  = 0.0f;
    float leftAnklePitchRad = 0.0f;
    float leftAnkleRollRad  = 0.0f;
    float rightThighPitchRad = 0.0f;
    float rightCalfPitchRad  = 0.0f;
    float rightAnklePitchRad = 0.0f;
    float rightAnkleRollRad  = 0.0f;
    bool  valid             = false;
};

// Exact analytical 2-bone Law-of-Cosines IK solver with pole-vector plane lock and soft-stretch hyperextension guard.
TwoBoneIkResult SolveTwoBoneIK(Vec3 root, Vec3 target, Vec3 poleDir, float lenUpper, float lenLower);

// Computes terrain-conforming pelvis drop, stair/rock foot elevation, ankle slope alignment, and snow-weight damping.
FootPlantOutput ComputeFootPlanting(const FootPlantInput& in);

// Distributes target gaze yaw/pitch across 4 upper-body bones plus velocity-banked turn lean & breathing.
SpineGazeOutput ComputeSpineGaze(float targetYawRad, float targetPitchRad,
                                 float lateralTurnVelRadS, float timeSec, float fatigue01);

// Classifies vanilla Skyrim / XPMSSE skeleton bone names into live IK roles.
HumanoidBoneRole ClassifyHumanoidBone(const char* boneName);

// Advances per-actor humanoid IK (2-bone leg/ankle adaptation, spine/head gaze, breathing & turn banking).
ActorIkPoseDelta StepActorHumanoidIk(ActorIkMotionState& state,
                                     const float rootWorldPos[3],
                                     const float rootForwardXY[2],
                                     float leftFootWorldZ,
                                     float rightFootWorldZ,
                                     float pelvisWorldZ,
                                     float snowDepthU,
                                     float dtSec,
                                     float timeSec);

// Composes an additive local Euler delta (R_out = R_in * Rz(yaw) * Rx(pitch) * Ry(roll)) onto a 3x3 row-major matrix.
// Preserves Havok's current-frame animation pose without overwriting bind poses.
void ComposeAdditiveLocalRotation(float inOutRotate3x3[9], float pitchRad, float rollRad, float yawRad);

}  // namespace raw::rt
