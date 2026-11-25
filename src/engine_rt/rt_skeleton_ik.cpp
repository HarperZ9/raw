// rt_skeleton_ik.cpp: Procedural Full-Body Skeletal Kinematics, 2-Bone Foot/Arm IK & Gaze/Slope Lean (Zero SKSE).
// Original work, MIT.
#include "rt_skeleton_ik.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw::rt {
namespace {

inline bool IsFinite3(Vec3 a) noexcept {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
inline float SafeFloat(float v, float fallback = 0.0f) noexcept {
    return std::isfinite(v) ? v : fallback;
}
inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(Vec3 a) {
    if (!IsFinite3(a)) return 0.0f;
    return std::sqrt(Dot(a, a));
}

inline Vec3 Norm(Vec3 a, Vec3 fallback) {
    if (!IsFinite3(a)) return fallback;
    const float l = Len(a);
    return (std::isfinite(l) && l > 1.0e-5f) ? Mul(a, 1.0f / l) : fallback;
}

inline Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline bool ContainsCI(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return false;
    const size_t nLen = std::strlen(needle);
    for (const char* p = hay; *p; ++p) {
        size_t i = 0;
        while (i < nLen && p[i]) {
            char a = p[i]; if (a >= 'A' && a <= 'Z') a = char(a + 32);
            char b = needle[i]; if (b >= 'A' && b <= 'Z') b = char(b + 32);
            if (a != b) break;
            ++i;
        }
        if (i == nLen) return true;
    }
    return false;
}

// O(1) finite angle wrap to [-pi, pi] (replaces while-loop that could hang on huge/Inf angles).
inline float WrapAnglePi(float a) noexcept {
    if (!std::isfinite(a)) return 0.0f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float r = std::remainder(a, kTwoPi);
    return std::isfinite(r) ? r : 0.0f;
}

// Re-orthonormalizes a 3x3 row-major rotation matrix via Gram-Schmidt (prevents multi-frame drift/shear).
inline void OrthonormalizeMat3(float m[9]) noexcept {
    Vec3 r0 = Norm({m[0], m[1], m[2]}, {1.0f, 0.0f, 0.0f});
    Vec3 r1{m[3], m[4], m[5]};
    r1 = Norm(Sub(r1, Mul(r0, Dot(r1, r0))), {0.0f, 1.0f, 0.0f});
    Vec3 r2 = Norm(Cross(r0, r1), {0.0f, 0.0f, 1.0f});
    m[0] = r0.x; m[1] = r0.y; m[2] = r0.z;
    m[3] = r1.x; m[4] = r1.y; m[5] = r1.z;
    m[6] = r2.x; m[7] = r2.y; m[8] = r2.z;
}

}  // namespace

Quat4 NormalizeQuat(Quat4 q) noexcept {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(n2) || n2 < 1.0e-12f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float inv = 1.0f / std::sqrt(n2);
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

Quat4 SlerpQuat(Quat4 q0, Quat4 q1, float t) noexcept {
    const float u = std::clamp(SafeFloat(t, 0.0f), 0.0f, 1.0f);
    const Quat4 a = NormalizeQuat(q0);
    Quat4 b = NormalizeQuat(q1);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    // Enforce shortest-path antipodal continuity (prevents 360-degree limb/head snaps).
    if (dot < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    dot = std::clamp(dot, 0.0f, 1.0f);
    if (dot > 0.9995f) {
        return NormalizeQuat({
            a.x + (b.x - a.x) * u,
            a.y + (b.y - a.y) * u,
            a.z + (b.z - a.z) * u,
            a.w + (b.w - a.w) * u
        });
    }
    const float theta0 = std::acos(dot);
    const float sin0   = std::sin(theta0);
    if (!std::isfinite(sin0) || sin0 < 1.0e-5f) {
        return NormalizeQuat({
            a.x + (b.x - a.x) * u,
            a.y + (b.y - a.y) * u,
            a.z + (b.z - a.z) * u,
            a.w + (b.w - a.w) * u
        });
    }
    const float s0 = std::sin((1.0f - u) * theta0) / sin0;
    const float s1 = std::sin(u * theta0) / sin0;
    return NormalizeQuat({
        s0 * a.x + s1 * b.x,
        s0 * a.y + s1 * b.y,
        s0 * a.z + s1 * b.z,
        s0 * a.w + s1 * b.w
    });
}

Quat4 DampQuat(Quat4 current, Quat4 target, float lambdaPerSec, float dtSec) noexcept {
    const float lam = std::clamp(SafeFloat(lambdaPerSec, 0.0f), 0.0f, 120.0f);
    const float dt  = std::clamp(SafeFloat(dtSec, 0.0f), 0.0f, 0.25f);
    if (lam <= 0.0f || dt <= 0.0f) return NormalizeQuat(current);
    const float alpha = 1.0f - std::exp(-lam * dt);
    return SlerpQuat(current, target, alpha);
}

void QuatToMat3(Quat4 q, float outRowMajor3x3[9]) noexcept {
    if (!outRowMajor3x3) return;
    const Quat4 u = NormalizeQuat(q);
    const float xx = u.x * u.x, yy = u.y * u.y, zz = u.z * u.z;
    const float xy = u.x * u.y, xz = u.x * u.z, yz = u.y * u.z;
    const float wx = u.w * u.x, wy = u.w * u.y, wz = u.w * u.z;
    outRowMajor3x3[0] = 1.0f - 2.0f * (yy + zz);
    outRowMajor3x3[1] = 2.0f * (xy - wz);
    outRowMajor3x3[2] = 2.0f * (xz + wy);
    outRowMajor3x3[3] = 2.0f * (xy + wz);
    outRowMajor3x3[4] = 1.0f - 2.0f * (xx + zz);
    outRowMajor3x3[5] = 2.0f * (yz - wx);
    outRowMajor3x3[6] = 2.0f * (xz - wy);
    outRowMajor3x3[7] = 2.0f * (yz + wx);
    outRowMajor3x3[8] = 1.0f - 2.0f * (xx + yy);
}

TwoBoneIkResult SolveTwoBoneIK(Vec3 root, Vec3 target, Vec3 poleDir, float lenUpper, float lenLower) {
    TwoBoneIkResult out{};
    if (!IsFinite3(root) || !IsFinite3(target) || !IsFinite3(poleDir)) return out;
    if (!std::isfinite(lenUpper) || !std::isfinite(lenLower)) return out;
    if (lenUpper <= 1.0e-4f || lenLower <= 1.0e-4f) return out;

    const Vec3 toTarget = Sub(target, root);
    const float rawDist = Len(toTarget);
    const float maxReach = (lenUpper + lenLower) * 0.999f;
    const float minReach = std::fabs(lenUpper - lenLower) + 1.0e-3f;
    if (minReach >= maxReach) return out;
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
    out.solved = IsFinite3(out.midPos) && IsFinite3(out.endPos);
    return out;
}

FootPlantOutput ComputeFootPlanting(const FootPlantInput& in) {
    FootPlantOutput out{};
    const float snowDepth   = std::clamp(SafeFloat(in.snowDepthU, 0.0f), 0.0f, 64.0f);
    const float maxDrop     = std::clamp(SafeFloat(in.maxLegDrop, 28.0f), 1.0f, 64.0f);
    const float pelvisBaseZ = SafeFloat(in.pelvisBaseZ, 0.0f);
    const float leftZ       = SafeFloat(in.leftGroundZ, pelvisBaseZ);
    const float rightZ      = SafeFloat(in.rightGroundZ, pelvisBaseZ);

    // Deep powder snow lets boots sink cleanly (~28% of volumetric depth) and slows stride slightly.
    const float snowSink = std::clamp(snowDepth * 0.28f, 0.0f, 10.0f);
    const float effL = leftZ - snowSink;
    const float effR = rightZ - snowSink;
    const float minFoot = (std::min)(effL, effR);
    const float drop = std::clamp(minFoot - pelvisBaseZ, -maxDrop, maxDrop * 0.5f);

    out.pelvisOffsetZ  = drop;
    out.leftFootLiftZ  = std::clamp(effL - (pelvisBaseZ + drop), -maxDrop, maxDrop);
    out.rightFootLiftZ = std::clamp(effR - (pelvisBaseZ + drop), -maxDrop, maxDrop);

    const Vec3 nL = Norm(in.leftGroundN,  {0.0f, 0.0f, 1.0f});
    const Vec3 nR = Norm(in.rightGroundN, {0.0f, 0.0f, 1.0f});
    out.leftAnklePitchRad  = std::clamp(std::atan2(nL.y, (std::max)(nL.z, 0.25f)), -0.58f, 0.58f);
    out.leftAnkleRollRad   = std::clamp(std::atan2(-nL.x, (std::max)(nL.z, 0.25f)), -0.45f, 0.45f);
    out.rightAnklePitchRad = std::clamp(std::atan2(nR.y, (std::max)(nR.z, 0.25f)), -0.58f, 0.58f);
    out.rightAnkleRollRad  = std::clamp(std::atan2(-nR.x, (std::max)(nR.z, 0.25f)), -0.45f, 0.45f);
    out.strideDampMult     = std::clamp(1.0f - snowDepth * 0.012f, 0.65f, 1.0f);
    return out;
}

SpineGazeOutput ComputeSpineGaze(float targetYawRad, float targetPitchRad,
                                 float lateralTurnVelRadS, float timeSec, float fatigue01) {
    SpineGazeOutput out{};
    const float yaw     = std::clamp(SafeFloat(targetYawRad, 0.0f), -1.35f, 1.35f);
    const float pitch   = std::clamp(SafeFloat(targetPitchRad, 0.0f), -0.85f, 0.85f);
    const float turnVel = SafeFloat(lateralTurnVelRadS, 0.0f);
    const float tSec    = SafeFloat(timeSec, 0.0f);
    const float fat     = std::clamp(SafeFloat(fatigue01, 0.0f), 0.0f, 1.0f);
    static const float kYawW[4]   = {0.18f, 0.26f, 0.24f, 0.32f};
    static const float kPitchW[4] = {0.14f, 0.24f, 0.28f, 0.34f};
    for (int i = 0; i < 4; ++i) {
        out.spineYawRad[i]   = yaw * kYawW[i];
        out.spinePitchRad[i] = pitch * kPitchW[i];
    }
    out.bankRollRad = std::clamp(turnVel * 0.14f, -0.22f, 0.22f);
    const float breathHz = 0.24f + fat * 0.36f;
    out.breathChestLift = std::sin(WrapAnglePi(tSec * 6.2831853f * breathHz)) * (0.28f + 0.42f * fat);
    return out;
}

HumanoidBoneRole ClassifyHumanoidBone(const char* boneName) {
    if (!boneName || !boneName[0]) return HumanoidBoneRole::None;
    // Only match canonical NPC core skeleton bones (avoid hijacking physics/weapon helper nodes).
    if (ContainsCI(boneName, "pelvis") && ContainsCI(boneName, "npc")) return HumanoidBoneRole::Pelvis;
    if (ContainsCI(boneName, "spine2") || ContainsCI(boneName, "[spn2]")) return HumanoidBoneRole::Spine2;
    if (ContainsCI(boneName, "spine1") || ContainsCI(boneName, "[spn1]")) return HumanoidBoneRole::Spine1;
    if (ContainsCI(boneName, "npc spine") || ContainsCI(boneName, "[spn0]")) return HumanoidBoneRole::Spine0;
    if (ContainsCI(boneName, "npc neck") || ContainsCI(boneName, "[neck]")) return HumanoidBoneRole::Neck;
    if (ContainsCI(boneName, "npc head") || ContainsCI(boneName, "[head]")) return HumanoidBoneRole::Head;
    if (ContainsCI(boneName, "l thigh") || ContainsCI(boneName, "[lthg]")) return HumanoidBoneRole::LeftThigh;
    if (ContainsCI(boneName, "l calf")  || ContainsCI(boneName, "[lclf]")) return HumanoidBoneRole::LeftCalf;
    if (ContainsCI(boneName, "l foot")  || ContainsCI(boneName, "[lft"))   return HumanoidBoneRole::LeftFoot;
    if (ContainsCI(boneName, "r thigh") || ContainsCI(boneName, "[rthg]")) return HumanoidBoneRole::RightThigh;
    if (ContainsCI(boneName, "r calf")  || ContainsCI(boneName, "[rclf]")) return HumanoidBoneRole::RightCalf;
    if (ContainsCI(boneName, "r foot")  || ContainsCI(boneName, "[rft"))   return HumanoidBoneRole::RightFoot;
    return HumanoidBoneRole::None;
}

ActorIkPoseDelta StepActorHumanoidIk(ActorIkMotionState& st,
                                     const float rootWorldPos[3],
                                     const float rootForwardXY[2],
                                     float leftFootWorldZ,
                                     float rightFootWorldZ,
                                     float pelvisWorldZ,
                                     float snowDepthU,
                                     float dtSec,
                                     float timeSec) {
    ActorIkPoseDelta out{};
    if (!rootWorldPos || !rootForwardXY || !std::isfinite(dtSec) || !(dtSec > 0.0f)) return out;
    if (!std::isfinite(rootWorldPos[0]) || !std::isfinite(rootWorldPos[1]) || !std::isfinite(rootWorldPos[2])) return out;
    if (!std::isfinite(rootForwardXY[0]) || !std::isfinite(rootForwardXY[1])) return out;
    const float safePelvisZ = SafeFloat(pelvisWorldZ, rootWorldPos[2]);
    const float safeLeftZ   = SafeFloat(leftFootWorldZ, safePelvisZ);
    const float safeRightZ  = SafeFloat(rightFootWorldZ, safePelvisZ);
    const float safeSnow    = std::clamp(SafeFloat(snowDepthU, 0.0f), 0.0f, 36.0f);
    const float safeTime    = SafeFloat(timeSec, 0.0f);
    const float clampedDt   = std::clamp(dtSec, 1.0e-4f, 0.05f);
    const float fwdLenSq    = rootForwardXY[0] * rootForwardXY[0] + rootForwardXY[1] * rootForwardXY[1];
    const float yawRad      = (fwdLenSq > 1.0e-8f)
                                  ? std::atan2(rootForwardXY[1], rootForwardXY[0])
                                  : (st.initialized ? st.prevYawRad : 0.0f);

    if (!st.initialized || !std::isfinite(st.smoothSpeedU) || !std::isfinite(st.smoothTurnVel) ||
        !std::isfinite(st.smoothPelvisZ) || !std::isfinite(st.smoothFootDeltaZ)) {
        std::memcpy(st.prevRootPos, rootWorldPos, 12);
        st.prevYawRad = yawRad;
        st.smoothTurnVel = 0.0f;
        st.smoothSpeedU = 0.0f;
        st.smoothPelvisZ = 0.0f;
        st.smoothFootDeltaZ = 0.0f;
        st.initialized = true;
    }

    const float dx = rootWorldPos[0] - st.prevRootPos[0];
    const float dy = rootWorldPos[1] - st.prevRootPos[1];
    const float dz = rootWorldPos[2] - st.prevRootPos[2];
    const float jumpDist = std::sqrt(dx * dx + dy * dy + dz * dz);
    // Smooth continuous teleport attenuation instead of a hard binary boolean jump snap:
    const float continuity = 1.0f - std::clamp((jumpDist - 120.0f) / 120.0f, 0.0f, 1.0f);
    const float speedU     = (std::sqrt(dx * dx + dy * dy) / clampedDt) * continuity;
    const float rawTurnVel = (WrapAnglePi(yawRad - st.prevYawRad) / clampedDt) * continuity;
    std::memcpy(st.prevRootPos, rootWorldPos, 12);
    st.prevYawRad = yawRad;

    const float alpha = 1.0f - std::exp(-clampedDt * 12.0f);
    st.smoothSpeedU  += (std::clamp(speedU, 0.0f, 700.0f) - st.smoothSpeedU) * alpha;
    st.smoothTurnVel += (std::clamp(rawTurnVel, -4.5f, 4.5f) - st.smoothTurnVel) * alpha;

    // 1. Spine / Head Gaze, Turn Banking & Rhythmic Breathing (works on ALL vanilla & modded humanoids)
    const float speedNorm = std::clamp(st.smoothSpeedU / 360.0f, 0.0f, 1.0f);
    const float leadYaw   = std::clamp(st.smoothTurnVel * 0.24f, -0.42f, 0.42f);
    const float slopePitch = std::clamp(((dz * continuity) / clampedDt) * 0.0012f, -0.14f, 0.14f);
    const SpineGazeOutput gaze = ComputeSpineGaze(leadYaw, slopePitch, st.smoothTurnVel * (0.35f + 0.65f * speedNorm),
                                                  safeTime, speedNorm * 0.75f);
    for (int i = 0; i < 4; ++i) {
        out.spineYawRad[i]   = std::clamp(gaze.spineYawRad[i], -0.22f, 0.22f);
        out.spinePitchRad[i] = std::clamp(gaze.spinePitchRad[i], -0.16f, 0.16f);
    }
    out.spineBankRollRad = std::clamp(gaze.bankRollRad, -0.16f, 0.16f);
    out.chestBreathLiftZ = std::clamp(gaze.breathChestLift, -0.45f, 0.45f);

    // 2. Foot Planting & Analytical 2-Bone Leg Adaptation (slope/stair delta between L and R feet)
    const float rawFootDelta = std::clamp(safeLeftZ - safeRightZ, -18.0f, 18.0f);
    st.smoothFootDeltaZ += (rawFootDelta - st.smoothFootDeltaZ) * alpha;

    FootPlantInput fp{};
    fp.pelvisBaseZ  = safePelvisZ;
    fp.leftGroundZ  = safePelvisZ + st.smoothFootDeltaZ * 0.35f;
    fp.rightGroundZ = safePelvisZ - st.smoothFootDeltaZ * 0.35f;
    fp.maxLegDrop   = 12.0f;
    fp.snowDepthU   = safeSnow;
    // Estimate local ground slope normal from vertical climb rate + foot height differential
    const float slopeY = std::clamp(slopePitch * 1.4f, -0.45f, 0.45f);
    const float slopeX = std::clamp(st.smoothFootDeltaZ * 0.025f, -0.35f, 0.35f);
    fp.leftGroundN  = Norm({-slopeX, slopeY, 1.0f}, {0.0f, 0.0f, 1.0f});
    fp.rightGroundN = Norm({-slopeX, slopeY, 1.0f}, {0.0f, 0.0f, 1.0f});
    const FootPlantOutput fpo = ComputeFootPlanting(fp);

    // Smooth pelvis micro-drop (clamped strictly so Havok locomotion never sinks into the floor!)
    const float targetPelvisDelta = std::clamp(fpo.pelvisOffsetZ * 0.45f, -4.5f, 2.0f);
    st.smoothPelvisZ += (targetPelvisDelta - st.smoothPelvisZ) * alpha;
    out.pelvisDeltaZ = st.smoothPelvisZ;

    // Solve 2-bone leg bend delta for raised foot vs nominal straight leg (Skyrim humanoid leg: ~40u thigh + ~40u calf)
    constexpr float kThighLen = 40.0f;
    constexpr float kCalfLen  = 40.0f;
    const TwoBoneIkResult restLeg = SolveTwoBoneIK({0, 0, 0}, {0, 0, -76.0f}, {0, 1, 0}, kThighLen, kCalfLen);
    const TwoBoneIkResult leftLeg = SolveTwoBoneIK({0, 0, 0}, {0, 0, -76.0f + std::clamp(fpo.leftFootLiftZ, -8.0f, 12.0f)},
                                                   {0, 1, 0}, kThighLen, kCalfLen);
    const TwoBoneIkResult rightLeg = SolveTwoBoneIK({0, 0, 0}, {0, 0, -76.0f + std::clamp(fpo.rightFootLiftZ, -8.0f, 12.0f)},
                                                    {0, 1, 0}, kThighLen, kCalfLen);
    if (restLeg.solved && leftLeg.solved && rightLeg.solved) {
        out.leftThighPitchRad  = std::clamp((leftLeg.upperBendRad  - restLeg.upperBendRad)  * 0.55f, -0.25f, 0.25f);
        out.leftCalfPitchRad   = std::clamp((leftLeg.midBendRad    - restLeg.midBendRad)    * 0.55f, -0.35f, 0.35f);
        out.rightThighPitchRad = std::clamp((rightLeg.upperBendRad - restLeg.upperBendRad)  * 0.55f, -0.25f, 0.25f);
        out.rightCalfPitchRad  = std::clamp((rightLeg.midBendRad   - restLeg.midBendRad)    * 0.55f, -0.35f, 0.35f);
    }
    out.leftAnklePitchRad  = std::clamp(fpo.leftAnklePitchRad  * 0.65f, -0.32f, 0.32f);
    out.leftAnkleRollRad   = std::clamp(fpo.leftAnkleRollRad   * 0.65f, -0.26f, 0.26f);
    out.rightAnklePitchRad = std::clamp(fpo.rightAnklePitchRad * 0.65f, -0.32f, 0.32f);
    out.rightAnkleRollRad  = std::clamp(fpo.rightAnkleRollRad  * 0.65f, -0.26f, 0.26f);
    out.valid = true;
    return out;
}

void ComposeAdditiveLocalRotation(float inOutRotate3x3[9], float pitchRad, float rollRad, float yawRad) {
    if (!inOutRotate3x3) return;
    if (!std::isfinite(pitchRad) || !std::isfinite(rollRad) || !std::isfinite(yawRad)) return;
    if (std::fabs(pitchRad) < 1.0e-5f && std::fabs(rollRad) < 1.0e-5f && std::fabs(yawRad) < 1.0e-5f) return;

    // Validate input matrix is finite and near-orthonormal (protects against corrupted pointers/scales)
    float normSq = 0.0f;
    for (int i = 0; i < 9; ++i) {
        if (!std::isfinite(inOutRotate3x3[i])) return;
        normSq += inOutRotate3x3[i] * inOutRotate3x3[i];
    }
    if (normSq < 2.2f || normSq > 3.8f) return;

    const float cp = std::cos(pitchRad), sp = std::sin(pitchRad);
    const float cr = std::cos(rollRad),  sr = std::sin(rollRad);
    const float cy = std::cos(yawRad),   sy = std::sin(yawRad);

    // dR = Rz(yaw) * Rx(pitch) * Ry(roll)
    const float dR[9] = {
        cy * cr - sy * sp * sr,  -sy * cp,  cy * sr + sy * sp * cr,
        sy * cr + cy * sp * sr,   cy * cp,  sy * sr - cy * sp * cr,
        -cp * sr,                 sp,       cp * cr
    };

    float outR[9]{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            outR[r * 3 + c] =
                inOutRotate3x3[r * 3 + 0] * dR[0 * 3 + c] +
                inOutRotate3x3[r * 3 + 1] * dR[1 * 3 + c] +
                inOutRotate3x3[r * 3 + 2] * dR[2 * 3 + c];
        }
    }
    OrthonormalizeMat3(outR);
    std::memcpy(inOutRotate3x3, outR, sizeof(outR));
}

}  // namespace raw::rt
