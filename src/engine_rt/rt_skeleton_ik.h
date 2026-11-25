#pragma once
// rt_skeleton_ik.h: Procedural Full-Body Skeletal Kinematics, 2-Bone Foot/Arm IK & Gaze/Slope Lean (Zero SKSE).
// Runs natively inside raw_engine.dll alongside the 240 Hz XPBD soft-body solver (rt_softbody.h).
// Original work, MIT.

#include <cstdint>

namespace raw::rt {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

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

// Exact analytical 2-bone Law-of-Cosines IK solver with pole-vector plane lock and soft-stretch hyperextension guard.
TwoBoneIkResult SolveTwoBoneIK(Vec3 root, Vec3 target, Vec3 poleDir, float lenUpper, float lenLower);

// Computes terrain-conforming pelvis drop, stair/rock foot elevation, ankle slope alignment, and snow-weight damping.
FootPlantOutput ComputeFootPlanting(const FootPlantInput& in);

// Distributes target gaze yaw/pitch across 4 upper-body bones plus velocity-banked turn lean & breathing.
SpineGazeOutput ComputeSpineGaze(float targetYawRad, float targetPitchRad,
                                 float lateralTurnVelRadS, float timeSec, float fatigue01);

}  // namespace raw::rt
