// rt_skeleton_ik.cpp: Procedural Full-Body Skeletal Kinematics, 2-Bone Foot/Arm IK & Gaze/Slope Lean (Zero SKSE).
// Original work, MIT.
#include "rt_skeleton_ik.h"

#include <algorithm>
#include <cmath>

namespace raw::rt {
namespace {

inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(Vec3 a) { return std::sqrt(Dot(a, a)); }

inline Vec3 Norm(Vec3 a, Vec3 fallback) {
    const float l = Len(a);
    return l > 1.0e-5f ? Mul(a, 1.0f / l) : fallback;
}

inline Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

}  // namespace

TwoBoneIkResult SolveTwoBoneIK(Vec3 root, Vec3 target, Vec3 poleDir, float lenUpper, float lenLower) {
    TwoBoneIkResult out{};
    if (lenUpper <= 1.0e-4f || lenLower <= 1.0e-4f) return out;

    const Vec3 toTarget = Sub(target, root);
    const float rawDist = Len(toTarget);
    const float maxReach = (lenUpper + lenLower) * 0.999f;
    const float minReach = std::fabs(lenUpper - lenLower) + 1.0e-3f;
    const float d = std::clamp(rawDist, minReach, maxReach);
    const Vec3 fwd = Norm(toTarget, {0.0f, 0.0f, -1.0f});

    // Law of cosines for upper-limb angle alpha (between root->target and root->mid)
    const float cosAlpha = std::clamp((lenUpper * lenUpper + d * d - lenLower * lenLower) / (2.0f * lenUpper * d), -1.0f, 1.0f);
    const float alpha = std::acos(cosAlpha);

    // Law of cosines for interior knee/elbow angle beta
    const float cosBeta = std::clamp((lenUpper * lenUpper + lenLower * lenLower - d * d) / (2.0f * lenUpper * lenLower), -1.0f, 1.0f);
    const float beta = std::acos(cosBeta);

    // Orthonormalize pole vector against root->target axis
    Vec3 orthoPole = Sub(poleDir, Mul(fwd, Dot(poleDir, fwd)));
    if (Len(orthoPole) < 1.0e-4f) {
        Vec3 alt = std::fabs(fwd.z) < 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
        orthoPole = Cross(fwd, alt);
    }
    const Vec3 bendAxis = Norm(orthoPole, {0.0f, 1.0f, 0.0f});

    out.midPos = Add(root, Add(Mul(fwd, lenUpper * std::cos(alpha)), Mul(bendAxis, lenUpper * std::sin(alpha))));
    out.endPos = Add(root, Mul(fwd, d));
    out.upperBendRad = alpha;
    out.midBendRad = 3.14159265f - beta;
    out.reachRatio = d / (lenUpper + lenLower);
    out.solved = true;
    return out;
}

FootPlantOutput ComputeFootPlanting(const FootPlantInput& in) {
    FootPlantOutput out{};
    // Deep powder snow lets boots sink cleanly (~28% of volumetric depth) and slows stride slightly.
    const float snowSink = std::clamp(in.snowDepthU * 0.28f, 0.0f, 10.0f);
    const float effL = in.leftGroundZ - snowSink;
    const float effR = in.rightGroundZ - snowSink;
    const float minFoot = std::min(effL, effR);
    const float drop = std::clamp(minFoot - in.pelvisBaseZ, -in.maxLegDrop, in.maxLegDrop * 0.5f);

    out.pelvisOffsetZ  = drop;
    out.leftFootLiftZ  = std::clamp(effL - (in.pelvisBaseZ + drop), -in.maxLegDrop, in.maxLegDrop);
    out.rightFootLiftZ = std::clamp(effR - (in.pelvisBaseZ + drop), -in.maxLegDrop, in.maxLegDrop);

    const Vec3 nL = Norm(in.leftGroundN,  {0.0f, 0.0f, 1.0f});
    const Vec3 nR = Norm(in.rightGroundN, {0.0f, 0.0f, 1.0f});
    out.leftAnklePitchRad  = std::clamp(std::atan2(nL.y, std::max(nL.z, 0.25f)), -0.58f, 0.58f);
    out.leftAnkleRollRad   = std::clamp(std::atan2(-nL.x, std::max(nL.z, 0.25f)), -0.45f, 0.45f);
    out.rightAnklePitchRad = std::clamp(std::atan2(nR.y, std::max(nR.z, 0.25f)), -0.58f, 0.58f);
    out.rightAnkleRollRad  = std::clamp(std::atan2(-nR.x, std::max(nR.z, 0.25f)), -0.45f, 0.45f);
    out.strideDampMult     = std::clamp(1.0f - in.snowDepthU * 0.012f, 0.65f, 1.0f);
    return out;
}

SpineGazeOutput ComputeSpineGaze(float targetYawRad, float targetPitchRad,
                                 float lateralTurnVelRadS, float timeSec, float fatigue01) {
    SpineGazeOutput out{};
    const float yaw = std::clamp(targetYawRad, -1.35f, 1.35f);
    const float pitch = std::clamp(targetPitchRad, -0.85f, 0.85f);
    static const float kYawW[4]   = {0.18f, 0.26f, 0.24f, 0.32f};
    static const float kPitchW[4] = {0.14f, 0.24f, 0.28f, 0.34f};
    for (int i = 0; i < 4; ++i) {
        out.spineYawRad[i]   = yaw * kYawW[i];
        out.spinePitchRad[i] = pitch * kPitchW[i];
    }
    out.bankRollRad = std::clamp(lateralTurnVelRadS * 0.14f, -0.22f, 0.22f);
    const float breathHz = 0.24f + std::clamp(fatigue01, 0.0f, 1.0f) * 0.36f;
    out.breathChestLift = std::sin(timeSec * 6.2831853f * breathHz) * (0.28f + 0.42f * std::clamp(fatigue01, 0.0f, 1.0f));
    return out;
}

}  // namespace raw::rt
