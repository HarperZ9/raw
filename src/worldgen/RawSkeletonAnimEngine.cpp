// RawSkeletonAnimEngine.cpp: Clean-Room Procedural 24-Bone Character Skeleton Animation,
// Analytic 2-Bone / FABRIK IK, Live Terrain Foot-IK & XPBD Attachment Anchor Solver.
// Original work, MIT. Zero Windows / D3D12 / proprietary SDK dependencies.

#include "RawSkeletonAnimEngine.h"

#include <algorithm>
#include <cmath>

namespace raw::worldgen {
namespace {

constexpr float kPi       = 3.14159265358979323846f;
constexpr float kTwoPi    = 2.0f * kPi;
constexpr float kDegToRad = kPi / 180.0f;

inline bool IsFinite3(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline float SafeFloat(float v, float fallback = 0.0f) noexcept {
    return std::isfinite(v) ? v : fallback;
}

inline Vec3 SafeVec3(Vec3 v, Vec3 fallback = {}) noexcept {
    return IsFinite3(v) ? v : fallback;
}

std::array<BoneDef, kHumanoidBoneCount> BuildHumanoidRigTable() {
    // Skyrim Z-up humanoid proportions (~128u tall, facing +Y, +X right, +Z up):
    std::array<BoneDef, kHumanoidBoneCount> rig = {{
        {BoneId::Pelvis,          -1, "Pelvis",          { 0.0f,  0.0f,  68.0f}, 0.0f},
        {BoneId::Spine1,           0, "Spine1",          { 0.0f,  0.0f,  12.0f}, 0.0f},
        {BoneId::Spine2,           1, "Spine2",          { 0.0f,  0.0f,  14.0f}, 0.0f},
        {BoneId::Spine3,           2, "Spine3",          { 0.0f,  0.0f,  14.0f}, 0.0f},
        {BoneId::Neck,             3, "Neck",            { 0.0f,  0.0f,   8.0f}, 0.0f},
        {BoneId::Head,             4, "Head",            { 0.0f,  0.0f,  10.0f}, 0.0f},
        {BoneId::ClavicleL,        3, "ClavicleL",       {-6.0f,  0.0f,   5.0f}, 0.0f},
        {BoneId::UpperArmL,        6, "UpperArmL",       {-8.0f,  0.0f,  -2.0f}, 0.0f},
        {BoneId::ForearmL,         7, "ForearmL",        { 0.0f,  0.0f, -20.0f}, 0.0f},
        {BoneId::HandL,            8, "HandL",           { 0.0f,  0.0f, -18.0f}, 0.0f},
        {BoneId::ClavicleR,        3, "ClavicleR",       { 6.0f,  0.0f,   5.0f}, 0.0f},
        {BoneId::UpperArmR,       10, "UpperArmR",       { 8.0f,  0.0f,  -2.0f}, 0.0f},
        {BoneId::ForearmR,        11, "ForearmR",        { 0.0f,  0.0f, -20.0f}, 0.0f},
        {BoneId::HandR,           12, "HandR",           { 0.0f,  0.0f, -18.0f}, 0.0f},
        {BoneId::ThighL,           0, "ThighL",          {-8.0f,  0.0f,  -4.0f}, 0.0f},
        {BoneId::CalfL,           14, "CalfL",           { 0.0f,  0.0f, -30.0f}, 0.0f},
        {BoneId::FootL,           15, "FootL",           { 0.0f,  0.0f, -28.0f}, 0.0f},
        {BoneId::ToeL,            16, "ToeL",            { 0.0f, 10.0f,  -6.0f}, 0.0f},
        {BoneId::ThighR,           0, "ThighR",          { 8.0f,  0.0f,  -4.0f}, 0.0f},
        {BoneId::CalfR,           18, "CalfR",           { 0.0f,  0.0f, -30.0f}, 0.0f},
        {BoneId::FootR,           19, "FootR",           { 0.0f,  0.0f, -28.0f}, 0.0f},
        {BoneId::ToeR,            20, "ToeR",            { 0.0f, 10.0f,  -6.0f}, 0.0f},
        {BoneId::HairRootAnchor,   5, "HairRootAnchor",  { 0.0f, -4.0f,   8.0f}, 0.0f},
        {BoneId::CapeClaspAnchor,  3, "CapeClaspAnchor", { 0.0f, -7.0f,   6.0f}, 0.0f},
    }};
    for (BoneDef& b : rig) {
        b.restLength = (b.parentIndex >= 0) ? Length(b.restLocalOffset) : 0.0f;
    }
    return rig;
}

bool IsUpperBodyBone(uint32_t idx) noexcept {
    return (idx >= static_cast<uint32_t>(BoneId::Spine1) && idx <= static_cast<uint32_t>(BoneId::HandR)) ||
           idx == static_cast<uint32_t>(BoneId::HairRootAnchor) ||
           idx == static_cast<uint32_t>(BoneId::CapeClaspAnchor);
}

void ApplySolvedLegToPose(SkeletonPose& pose, uint32_t thighIdx, uint32_t calfIdx,
                          uint32_t footIdx, uint32_t toeIdx,
                          Vec3 targetAnklePos, Vec3 polePos, Vec3 groundNormal) {
    const auto& rig = RawSkeletonAnimEngine::HumanoidRig();

    const Vec3 oldHip   = pose.world[thighIdx].pos;
    const Vec3 oldKnee  = pose.world[calfIdx].pos;
    const Vec3 oldAnkle = pose.world[footIdx].pos;

    const TwoBoneIkResult ik = RawSkeletonAnimEngine::SolveTwoBoneIK(
        oldHip, oldKnee, oldAnkle, targetAnklePos, polePos);

    // Rotate thigh & calf world frames minimally to align with solved IK vectors:
    const Vec3 oldThighDir = Normalize(oldKnee - oldHip);
    const Vec3 newThighDir = Normalize(ik.midPos - oldHip);
    const Quat dqThigh     = QuatFromToRotation(oldThighDir, newThighDir);
    pose.world[thighIdx].rot = QuatNormalize(QuatMul(dqThigh, pose.world[thighIdx].rot));

    const Vec3 rotatedOldCalfDir = QuatRotateVec(dqThigh, Normalize(oldAnkle - oldKnee));
    const Vec3 newCalfDir        = Normalize(ik.endPos - ik.midPos);
    const Quat dqCalf            = QuatFromToRotation(rotatedOldCalfDir, newCalfDir);
    pose.world[calfIdx].pos      = ik.midPos;
    pose.world[calfIdx].rot      = QuatNormalize(QuatMul(dqCalf, QuatMul(dqThigh, pose.world[calfIdx].rot)));

    // Align ankle pitch/roll to terrain slope normal (sanitized & clamped safely to <= 45 deg):
    const Vec3 safeN = SafeVec3(groundNormal, {0.0f, 0.0f, 1.0f});
    const Vec3 clampedN = Normalize(Vec3{
        Clamp(safeN.x, -0.707f, 0.707f),
        Clamp(safeN.y, -0.707f, 0.707f),
        (std::max)(0.707f, safeN.z)
    });
    const Quat slopeAlign = QuatFromToRotation({0.0f, 0.0f, 1.0f}, clampedN);
    pose.world[footIdx].pos = ik.endPos;
    pose.world[footIdx].rot = QuatNormalize(QuatMul(slopeAlign, pose.world[footIdx].rot));

    // Propagate toe rigidly from solved foot transform (exact bone-length invariance):
    pose.world[toeIdx].pos = pose.world[footIdx].pos +
                             QuatRotateVec(pose.world[footIdx].rot, rig[toeIdx].restLocalOffset);
    pose.world[toeIdx].rot = pose.world[footIdx].rot;
}

}  // namespace

// ============================================================================
// Unit Quaternion Algebra
// ============================================================================

Quat QuatIdentity() noexcept {
    return {0.0f, 0.0f, 0.0f, 1.0f};
}

float QuatNorm(Quat q) noexcept {
    return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

Quat QuatNormalize(Quat q) noexcept {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
        return QuatIdentity();
    }
    const float n = QuatNorm(q);
    if (n < 1.0e-7f) {
        return QuatIdentity();
    }
    const float inv = 1.0f / n;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

Quat QuatConjugate(Quat q) noexcept {
    return {-q.x, -q.y, -q.z, q.w};
}

Quat QuatMul(Quat a, Quat b) noexcept {
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) || !std::isfinite(a.w) ||
        !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z) || !std::isfinite(b.w)) {
        return QuatIdentity();
    }
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

Vec3 QuatRotateVec(Quat q, Vec3 v) noexcept {
    if (!IsFinite3(v)) {
        return {};
    }
    const float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    const Quat uq = (std::isfinite(n2) && std::fabs(n2 - 1.0f) <= 1.0e-3f) ? q : QuatNormalize(q);
    const Vec3 u{uq.x, uq.y, uq.z};
    const Vec3 t = Cross(u, v) * 2.0f;
    return v + t * uq.w + Cross(u, t);
}

Quat QuatFromAxisAngle(Vec3 axis, float rad) noexcept {
    if (!IsFinite3(axis) || !std::isfinite(rad)) {
        return QuatIdentity();
    }
    const float len = Length(axis);
    if (len < 1.0e-6f) {
        return QuatIdentity();
    }
    const Vec3 a = axis / len;
    const float half = 0.5f * rad;
    const float s = std::sin(half);
    return QuatNormalize({a.x * s, a.y * s, a.z * s, std::cos(half)});
}

Quat QuatFromEulerXYZ(float rx, float ry, float rz) noexcept {
    const float hx = 0.5f * SafeFloat(rx);
    const float hy = 0.5f * SafeFloat(ry);
    const float hz = 0.5f * SafeFloat(rz);
    const float cx = std::cos(hx), sx = std::sin(hx);
    const float cy = std::cos(hy), sy = std::sin(hy);
    const float cz = std::cos(hz), sz = std::sin(hz);
    // Exact qz * qy * qx renormalized to unit length:
    return QuatNormalize({
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz
    });
}

Quat QuatFromToRotation(Vec3 fromDir, Vec3 toDir) noexcept {
    const Vec3 sf = SafeVec3(fromDir, {0.0f, 0.0f, 1.0f});
    const Vec3 st = SafeVec3(toDir,   {0.0f, 0.0f, 1.0f});
    if (Length(sf) < 1.0e-6f || Length(st) < 1.0e-6f) {
        return QuatIdentity();
    }
    const Vec3 f = Normalize(sf);
    const Vec3 t = Normalize(st);
    const float d = Clamp(Dot(f, t), -1.0f, 1.0f);
    if (d > 0.99999f) {
        return QuatIdentity();
    }
    if (d < -0.99999f) {
        Vec3 ortho = Cross({1.0f, 0.0f, 0.0f}, f);
        if (Length(ortho) < 1.0e-4f) {
            ortho = Cross({0.0f, 1.0f, 0.0f}, f);
        }
        return QuatFromAxisAngle(Normalize(ortho), kPi);
    }
    const Vec3 axis = Cross(f, t);
    return QuatNormalize({axis.x, axis.y, axis.z, 1.0f + d});
}

Quat QuatSlerp(Quat a, Quat b, float t) noexcept {
    const float u = Clamp01(SafeFloat(t, 0.0f));
    const float n2a = a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w;
    const float n2b = b.x * b.x + b.y * b.y + b.z * b.z + b.w * b.w;
    const Quat qa = (std::isfinite(n2a) && std::fabs(n2a - 1.0f) <= 1.0e-4f) ? a : QuatNormalize(a);
    Quat qb       = (std::isfinite(n2b) && std::fabs(n2b - 1.0f) <= 1.0e-4f) ? b : QuatNormalize(b);
    float dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
    // Enforce shortest-path antipodal continuity so limbs/heads never snap 360 degrees:
    if (dot < 0.0f) {
        qb = {-qb.x, -qb.y, -qb.z, -qb.w};
        dot = -dot;
    }
    dot = Clamp(dot, 0.0f, 1.0f);
    // Near-zero angle NLERP fallback (guards sin(theta0) -> 0 without step discontinuity):
    if (dot > 0.9995f) {
        return QuatNormalize({
            Lerp(qa.x, qb.x, u), Lerp(qa.y, qb.y, u),
            Lerp(qa.z, qb.z, u), Lerp(qa.w, qb.w, u)
        });
    }
    const float theta0 = std::acos(dot);
    const float sin0   = std::sin(theta0);
    if (!std::isfinite(sin0) || sin0 < 1.0e-5f) {
        return QuatNormalize({
            Lerp(qa.x, qb.x, u), Lerp(qa.y, qb.y, u),
            Lerp(qa.z, qb.z, u), Lerp(qa.w, qb.w, u)
        });
    }
    const float s0 = std::sin((1.0f - u) * theta0) / sin0;
    const float s1 = std::sin(u * theta0) / sin0;
    return QuatNormalize({
        s0 * qa.x + s1 * qb.x,
        s0 * qa.y + s1 * qb.y,
        s0 * qa.z + s1 * qb.z,
        s0 * qa.w + s1 * qb.w
    });
}

Quat QuatDamp(Quat current, Quat target, float lambdaPerSec, float dtSec) noexcept {
    const float lam = Clamp(SafeFloat(lambdaPerSec, 0.0f), 0.0f, 120.0f);
    const float dt  = Clamp(SafeFloat(dtSec, 0.0f), 0.0f, 0.25f);
    if (lam <= 0.0f || dt <= 0.0f) {
        return QuatNormalize(current);
    }
    const float alpha = 1.0f - std::exp(-lam * dt);
    return QuatSlerp(current, target, alpha);
}

// ============================================================================
// Skeleton Hierarchy & Procedural Clips
// ============================================================================

const std::array<BoneDef, kHumanoidBoneCount>& RawSkeletonAnimEngine::HumanoidRig() {
    static const std::array<BoneDef, kHumanoidBoneCount> kRig = BuildHumanoidRigTable();
    return kRig;
}

const BoneDef* RawSkeletonAnimEngine::GetBoneDef(BoneId id) {
    const size_t idx = static_cast<size_t>(id);
    return (idx < kHumanoidBoneCount) ? &HumanoidRig()[idx] : nullptr;
}

const char* RawSkeletonAnimEngine::GetClipName(AnimClipId clip) {
    switch (clip) {
        case AnimClipId::Idle:       return "Idle";
        case AnimClipId::Walk:       return "Walk";
        case AnimClipId::Run:        return "Run";
        case AnimClipId::Crouch:     return "Crouch";
        case AnimClipId::MeleeSwing: return "MeleeSwing";
        case AnimClipId::BowDraw:    return "BowDraw";
        default:                     return "UnknownClip";
    }
}

void RawSkeletonAnimEngine::EvaluateProceduralClip(AnimClipId clip, float phaseSec,
                                                   std::array<BoneTransform, kHumanoidBoneCount>& outLocal) {
    const auto& rig = HumanoidRig();
    const float t = SafeFloat(phaseSec, 0.0f);

    for (size_t i = 0u; i < kHumanoidBoneCount; ++i) {
        outLocal[i].pos = rig[i].restLocalOffset;
        outLocal[i].rot = QuatIdentity();
    }

    auto setRot = [&](BoneId id, float rxDeg, float ryDeg, float rzDeg) {
        outLocal[static_cast<size_t>(id)].rot =
            QuatFromEulerXYZ(rxDeg * kDegToRad, ryDeg * kDegToRad, rzDeg * kDegToRad);
    };

    switch (clip) {
        case AnimClipId::Idle: {
            const float breath = std::sin(t * 2.1f);
            outLocal[0].pos.z += breath * 0.8f;
            setRot(BoneId::Spine2,    breath * 1.5f, 0.0f, 0.0f);
            setRot(BoneId::Head,      breath * -1.0f, 0.0f, std::sin(t * 0.7f) * 3.0f);
            setRot(BoneId::UpperArmL, 0.0f, -6.0f, 0.0f);
            setRot(BoneId::UpperArmR, 0.0f,  6.0f, 0.0f);
            setRot(BoneId::ForearmL,  8.0f,  0.0f, 0.0f);
            setRot(BoneId::ForearmR,  8.0f,  0.0f, 0.0f);
            break;
        }
        case AnimClipId::Walk: {
            const float cyc = t * 5.2f;
            const float s   = std::sin(cyc);
            const float c   = std::cos(cyc);
            outLocal[0].pos.z += std::abs(s) * 2.2f - 1.0f;
            setRot(BoneId::Pelvis,    2.0f, s * 2.5f, s * 5.0f);
            setRot(BoneId::Spine2,    3.0f, 0.0f, -s * 6.0f);
            setRot(BoneId::ThighL,    s * 26.0f, 0.0f, 0.0f);
            setRot(BoneId::CalfL,     std::max(0.0f, -c) * -32.0f, 0.0f, 0.0f);
            setRot(BoneId::ThighR,   -s * 26.0f, 0.0f, 0.0f);
            setRot(BoneId::CalfR,     std::max(0.0f,  c) * -32.0f, 0.0f, 0.0f);
            setRot(BoneId::UpperArmL,-s * 20.0f, -5.0f, 0.0f);
            setRot(BoneId::ForearmL,  18.0f + std::max(0.0f, -s) * 14.0f, 0.0f, 0.0f);
            setRot(BoneId::UpperArmR, s * 20.0f,  5.0f, 0.0f);
            setRot(BoneId::ForearmR,  18.0f + std::max(0.0f,  s) * 14.0f, 0.0f, 0.0f);
            break;
        }
        case AnimClipId::Run: {
            const float cyc = t * 9.5f;
            const float s   = std::sin(cyc);
            const float c   = std::cos(cyc);
            outLocal[0].pos.z += std::abs(s) * 4.5f - 2.5f;
            setRot(BoneId::Pelvis,   -10.0f, s * 4.0f, s * 9.0f);
            setRot(BoneId::Spine1,    -6.0f, 0.0f, -s * 5.0f);
            setRot(BoneId::Spine2,    -6.0f, 0.0f, -s * 7.0f);
            setRot(BoneId::Head,      14.0f, 0.0f, 0.0f);
            setRot(BoneId::ThighL,    s * 48.0f, 0.0f, 0.0f);
            setRot(BoneId::CalfL,     std::max(0.0f, -c) * -65.0f - 8.0f, 0.0f, 0.0f);
            setRot(BoneId::ThighR,   -s * 48.0f, 0.0f, 0.0f);
            setRot(BoneId::CalfR,     std::max(0.0f,  c) * -65.0f - 8.0f, 0.0f, 0.0f);
            setRot(BoneId::UpperArmL,-s * 42.0f, -8.0f, 0.0f);
            setRot(BoneId::ForearmL,  55.0f, 0.0f, 0.0f);
            setRot(BoneId::UpperArmR, s * 42.0f,  8.0f, 0.0f);
            setRot(BoneId::ForearmR,  55.0f, 0.0f, 0.0f);
            break;
        }
        case AnimClipId::Crouch: {
            const float cyc = t * 3.6f;
            const float s   = std::sin(cyc);
            outLocal[0].pos.z -= 18.0f; // Stealth crouch pelvis drop!
            setRot(BoneId::Pelvis,   -16.0f, 0.0f, 0.0f);
            setRot(BoneId::Spine2,   -14.0f, 0.0f, 0.0f);
            setRot(BoneId::Head,      24.0f, 0.0f, 0.0f);
            setRot(BoneId::ThighL,    52.0f + s * 14.0f, 0.0f, 0.0f);
            setRot(BoneId::CalfL,    -78.0f, 0.0f, 0.0f);
            setRot(BoneId::ThighR,    52.0f - s * 14.0f, 0.0f, 0.0f);
            setRot(BoneId::CalfR,    -78.0f, 0.0f, 0.0f);
            setRot(BoneId::UpperArmL, 18.0f, -12.0f, 0.0f);
            setRot(BoneId::ForearmL,  48.0f, 0.0f, 0.0f);
            setRot(BoneId::UpperArmR, 18.0f,  12.0f, 0.0f);
            setRot(BoneId::ForearmR,  48.0f, 0.0f, 0.0f);
            break;
        }
        case AnimClipId::MeleeSwing: {
            const float swing = std::sin(Clamp01(std::fmod(std::abs(t), 1.0f)) * kPi);
            const float twist = -38.0f + swing * 82.0f;
            setRot(BoneId::Spine2,    -6.0f, 0.0f, twist * 0.45f);
            setRot(BoneId::Spine3,    -4.0f, 0.0f, twist * 0.55f);
            setRot(BoneId::UpperArmR, 40.0f + swing * 55.0f, 22.0f, twist * 0.6f);
            setRot(BoneId::ForearmR,  35.0f - swing * 20.0f, 0.0f, 0.0f);
            setRot(BoneId::UpperArmL, 25.0f, -25.0f, 0.0f);
            setRot(BoneId::ForearmL,  65.0f, 0.0f, 0.0f);
            break;
        }
        case AnimClipId::BowDraw: {
            const float draw = Clamp01(t);
            setRot(BoneId::Spine2,    0.0f, 0.0f, -22.0f * draw);
            setRot(BoneId::Spine3,    0.0f, 0.0f, -18.0f * draw);
            setRot(BoneId::Head,      0.0f, 0.0f,  35.0f * draw);
            setRot(BoneId::UpperArmL, 82.0f * draw, -12.0f, 15.0f * draw);
            setRot(BoneId::ForearmL,   8.0f * draw, 0.0f, 0.0f);
            setRot(BoneId::UpperArmR, 76.0f * draw, 35.0f * draw, -40.0f * draw);
            setRot(BoneId::ForearmR,  98.0f * draw, 0.0f, 0.0f);
            break;
        }
        default:
            break;
    }
}

void RawSkeletonAnimEngine::EvaluateBlendTree(const AnimBlendParams& params,
                                              std::array<BoneTransform, kHumanoidBoneCount>& outLocal) {
    std::array<BoneTransform, kHumanoidBoneCount> poseA{};
    std::array<BoneTransform, kHumanoidBoneCount> poseB{};
    EvaluateProceduralClip(params.primaryClip, params.phaseSec, poseA);

    const float alpha = Clamp01(SafeFloat(params.blendAlpha, 0.0f));
    if (alpha <= 1.0e-5f) {
        outLocal = poseA;
    } else if (alpha >= 1.0f - 1.0e-5f) {
        EvaluateProceduralClip(params.secondaryClip, params.phaseSec, outLocal);
    } else {
        EvaluateProceduralClip(params.secondaryClip, params.phaseSec, poseB);
        for (size_t i = 0u; i < kHumanoidBoneCount; ++i) {
            outLocal[i].pos = {
                Lerp(poseA[i].pos.x, poseB[i].pos.x, alpha),
                Lerp(poseA[i].pos.y, poseB[i].pos.y, alpha),
                Lerp(poseA[i].pos.z, poseB[i].pos.z, alpha)
            };
            outLocal[i].rot = QuatSlerp(poseA[i].rot, poseB[i].rot, alpha);
        }
    }

    // Optional upper-body additive combat overlay (e.g., MeleeSwing / BowDraw while running):
    const float addW = Clamp01(SafeFloat(params.upperBodyAdditiveWeight, 0.0f));
    if (addW > 1.0e-4f) {
        std::array<BoneTransform, kHumanoidBoneCount> overlay{};
        EvaluateProceduralClip(params.upperBodyClip, params.upperBodyPhaseSec, overlay);
        for (uint32_t i = 0u; i < kHumanoidBoneCount; ++i) {
            if (IsUpperBodyBone(i)) {
                const Quat deltaQ = QuatSlerp(QuatIdentity(), overlay[i].rot, addW);
                outLocal[i].rot = QuatNormalize(QuatMul(outLocal[i].rot, deltaQ));
            }
        }
    }
}

void RawSkeletonAnimEngine::ComputeForwardKinematics(Vec3 rootPos, float rootYawRad,
                                                     const std::array<BoneTransform, kHumanoidBoneCount>& local,
                                                     SkeletonPose& outPose) {
    const auto& rig = HumanoidRig();
    const Vec3 safeRoot = SafeVec3(rootPos);
    const Quat rootQ    = QuatFromAxisAngle({0.0f, 0.0f, 1.0f}, SafeFloat(rootYawRad, 0.0f));

    outPose.local            = local;
    outPose.pelvisDropZ      = 0.0f;
    outPose.leftFootPlanted  = false;
    outPose.rightFootPlanted = false;
    outPose.leftFootGroundNormal  = {0.0f, 0.0f, 1.0f};
    outPose.rightFootGroundNormal = {0.0f, 0.0f, 1.0f};

    for (size_t i = 0u; i < kHumanoidBoneCount; ++i) {
        const int32_t pIdx = rig[i].parentIndex;
        const Quat locR    = QuatNormalize(local[i].rot);
        if (pIdx < 0) {
            outPose.world[i].pos = safeRoot + QuatRotateVec(rootQ, local[i].pos);
            outPose.world[i].rot = QuatNormalize(QuatMul(rootQ, locR));
        } else {
            const BoneTransform& parent = outPose.world[static_cast<size_t>(pIdx)];
            outPose.world[i].pos = parent.pos + QuatRotateVec(parent.rot, local[i].pos);
            outPose.world[i].rot = QuatNormalize(QuatMul(parent.rot, locR));
        }
    }
}

// ============================================================================
// Analytic 2-Bone Law-of-Cosines IK & Iterative FABRIK Fallback
// ============================================================================

TwoBoneIkResult RawSkeletonAnimEngine::SolveTwoBoneIK(Vec3 rootPos, Vec3 midPos, Vec3 endPos,
                                                      Vec3 targetPos, Vec3 poleWorldPos) {
    TwoBoneIkResult res{};
    const Vec3 A = SafeVec3(rootPos);
    const Vec3 B = SafeVec3(midPos);
    const Vec3 C = SafeVec3(endPos);
    const Vec3 T = SafeVec3(targetPos, C);
    const Vec3 P = SafeVec3(poleWorldPos, B);

    const float L1 = std::max(1.0e-4f, Length(B - A));
    const float L2 = std::max(1.0e-4f, Length(C - B));

    const Vec3  toTarget = T - A;
    const float rawDist  = Length(toTarget);
    const float minReach = std::abs(L1 - L2) + 1.0e-3f;
    const float maxReach = (L1 + L2) * 0.9995f; // Strictly prevents hyperextension pop!

    const float clampedDist = Clamp(rawDist, minReach, maxReach);
    res.clampedToReach = (rawDist > maxReach || rawDist < minReach);

    const Vec3 eX = (rawDist > 1.0e-6f) ? (toTarget / rawDist) : Normalize(C - A);

    // Construct pole-plane normal axis eY strictly orthogonal to eX:
    const Vec3 toPole   = P - A;
    Vec3       poleProj = toPole - eX * Dot(toPole, eX);
    if (Length(poleProj) < 1.0e-5f) {
        poleProj = Cross(eX, {1.0f, 0.0f, 0.0f});
        if (Length(poleProj) < 1.0e-5f) {
            poleProj = Cross(eX, {0.0f, 1.0f, 0.0f});
        }
    }
    const Vec3 eY = Normalize(poleProj);

    // Exact Law of Cosines for angle at root A:
    const float cosAlpha = Clamp((L1 * L1 + clampedDist * clampedDist - L2 * L2) /
                                 (2.0f * L1 * clampedDist), -1.0f, 1.0f);
    const float sinAlpha = std::sqrt(std::max(0.0f, 1.0f - cosAlpha * cosAlpha));

    res.midPos     = A + eX * (L1 * cosAlpha) + eY * (L1 * sinAlpha);
    res.endPos     = A + eX * clampedDist;
    res.reachError = Length(res.endPos - T);
    return res;
}

float RawSkeletonAnimEngine::SolveFabrikChain(std::vector<Vec3>& joints, Vec3 targetPos,
                                              uint32_t maxIterations, float tolerance) {
    if (joints.size() < 2u || !IsFinite3(targetPos)) {
        return 0.0f;
    }
    const size_t n = joints.size();
    for (size_t i = 0u; i < n; ++i) {
        if (!IsFinite3(joints[i])) {
            return 0.0f;
        }
    }
    const uint32_t safeMaxIters = (std::min)(maxIterations, 64u);
    const float safeTol = (std::max)(SafeFloat(tolerance, 1.0e-4f), 1.0e-6f);
    std::vector<float> lengths(n - 1u, 0.0f);
    float totalLen = 0.0f;
    for (size_t i = 0u; i + 1u < n; ++i) {
        lengths[i] = (std::max)(1.0e-4f, Length(joints[i + 1u] - joints[i]));
        totalLen  += lengths[i];
    }

    auto safeDir = [](Vec3 delta, Vec3 fallback) noexcept -> Vec3 {
        const float len = Length(delta);
        return (std::isfinite(len) && len > 1.0e-6f) ? (delta / len) : fallback;
    };

    const Vec3 rootAnchor = joints[0];
    const float distToTarget = Length(targetPos - rootAnchor);

    // Unreachable target: stretch straight along root->target preserving exact bone lengths:
    if (distToTarget >= totalLen * 0.9995f) {
        const Vec3 dir = safeDir(targetPos - rootAnchor, {0.0f, 0.0f, -1.0f});
        for (size_t i = 0u; i + 1u < n; ++i) {
            joints[i + 1u] = joints[i] + dir * lengths[i];
        }
        return Length(joints.back() - targetPos);
    }

    for (uint32_t iter = 0u; iter < safeMaxIters; ++iter) {
        if (Length(joints.back() - targetPos) <= safeTol) {
            break;
        }
        // Backward pass (end -> root):
        joints.back() = targetPos;
        for (size_t i = n - 1u; i-- > 0u; ) {
            const Vec3 dir = safeDir(joints[i] - joints[i + 1u], {0.0f, 0.0f, 1.0f});
            joints[i] = joints[i + 1u] + dir * lengths[i];
        }
        // Forward pass (root pinned -> end):
        joints[0] = rootAnchor;
        for (size_t i = 0u; i + 1u < n; ++i) {
            const Vec3 dir = safeDir(joints[i + 1u] - joints[i], {0.0f, 0.0f, -1.0f});
            joints[i + 1u] = joints[i] + dir * lengths[i];
        }
    }
    return Length(joints.back() - targetPos);
}

// ============================================================================
// Live Terrain Foot-IK & XPBD Anchor Extraction
// ============================================================================

void RawSkeletonAnimEngine::ApplyTerrainFootIK(Vec3 rootPos, float rootYawRad,
                                               const RawWorldPhysics& physics,
                                               SkeletonPose& ioPose) {
    constexpr uint32_t kThighL = static_cast<uint32_t>(BoneId::ThighL);
    constexpr uint32_t kCalfL  = static_cast<uint32_t>(BoneId::CalfL);
    constexpr uint32_t kFootL  = static_cast<uint32_t>(BoneId::FootL);
    constexpr uint32_t kToeL   = static_cast<uint32_t>(BoneId::ToeL);
    constexpr uint32_t kThighR = static_cast<uint32_t>(BoneId::ThighR);
    constexpr uint32_t kCalfR  = static_cast<uint32_t>(BoneId::CalfR);
    constexpr uint32_t kFootR  = static_cast<uint32_t>(BoneId::FootR);
    constexpr uint32_t kToeR   = static_cast<uint32_t>(BoneId::ToeR);
    constexpr float    kAnkleSoleOffsetZ = 6.0f;

    const bool hasStaticBvh = (physics.GetStats().bvhCellCount > 0u);
    auto probeGround = [&](Vec3 footWorld, float& outGroundZ, Vec3& outNormal) -> bool {
        if (hasStaticBvh) {
            const RayHit hit = physics.RayCast({footWorld.x, footWorld.y, footWorld.z + 64.0f},
                                               {0.0f, 0.0f, -1.0f}, 160.0f);
            if (hit.hit) {
                outGroundZ = hit.point[2];
                outNormal  = Normalize(Vec3{hit.normal[0], hit.normal[1], hit.normal[2]});
                return true;
            }
        }
        float hz = 0.0f;
        Vec3  hn{0.0f, 0.0f, 1.0f};
        if (physics.SampleTerrainHeight(footWorld.x, footWorld.y, &hz, &hn)) {
            outGroundZ = hz;
            outNormal  = hn;
            return true;
        }
        return false;
    };

    float groundZL = 0.0f, groundZR = 0.0f;
    Vec3  normalL{0.0f, 0.0f, 1.0f};
    Vec3  normalR{0.0f, 0.0f, 1.0f};

    const bool hitL = probeGround(ioPose.world[kFootL].pos, groundZL, normalL);
    const bool hitR = probeGround(ioPose.world[kFootR].pos, groundZR, normalR);
    if (!hitL && !hitR) {
        return;
    }

    // Compute pelvis drop so downhill foot stays comfortably inside max leg reach (zero hyperextension):
    const float legMaxReach = (Length(ioPose.world[kCalfL].pos - ioPose.world[kThighL].pos) +
                               Length(ioPose.world[kFootL].pos - ioPose.world[kCalfL].pos)) * 0.96f;
    float requiredDropZ = 0.0f;
    if (hitL) {
        const float targetAnkleL = groundZL + kAnkleSoleOffsetZ;
        const float distL = ioPose.world[kThighL].pos.z - targetAnkleL;
        if (distL > legMaxReach) {
            requiredDropZ = std::min(requiredDropZ, legMaxReach - distL);
        }
        requiredDropZ = std::min(requiredDropZ, groundZL - rootPos.z);
    }
    if (hitR) {
        const float targetAnkleR = groundZR + kAnkleSoleOffsetZ;
        const float distR = ioPose.world[kThighR].pos.z - targetAnkleR;
        if (distR > legMaxReach) {
            requiredDropZ = std::min(requiredDropZ, legMaxReach - distR);
        }
        requiredDropZ = std::min(requiredDropZ, groundZR - rootPos.z);
    }
    const float pelvisDropZ = Clamp(requiredDropZ, -36.0f, 0.0f);
    ioPose.pelvisDropZ = pelvisDropZ;

    if (pelvisDropZ < -1.0e-4f) {
        for (BoneTransform& bt : ioPose.world) {
            bt.pos.z += pelvisDropZ;
        }
    }

    // Forward knee pole direction in character facing frame (+Y rotated by rootYawRad):
    const Quat rootQ = QuatFromAxisAngle({0.0f, 0.0f, 1.0f}, SafeFloat(rootYawRad, 0.0f));
    const Vec3 fwd   = QuatRotateVec(rootQ, {0.0f, 1.0f, 0.0f});

    if (hitL) {
        const Vec3 targetL{ioPose.world[kFootL].pos.x, ioPose.world[kFootL].pos.y, groundZL + kAnkleSoleOffsetZ};
        const Vec3 poleL = ioPose.world[kCalfL].pos + fwd * 40.0f;
        ApplySolvedLegToPose(ioPose, kThighL, kCalfL, kFootL, kToeL, targetL, poleL, normalL);
        ioPose.leftFootPlanted      = true;
        ioPose.leftFootGroundNormal = normalL;
    }
    if (hitR) {
        const Vec3 targetR{ioPose.world[kFootR].pos.x, ioPose.world[kFootR].pos.y, groundZR + kAnkleSoleOffsetZ};
        const Vec3 poleR = ioPose.world[kCalfR].pos + fwd * 40.0f;
        ApplySolvedLegToPose(ioPose, kThighR, kCalfR, kFootR, kToeR, targetR, poleR, normalR);
        ioPose.rightFootPlanted      = true;
        ioPose.rightFootGroundNormal = normalR;
    }
}

XpbdAttachmentAnchors RawSkeletonAnimEngine::ExtractXpbdAnchors(const SkeletonPose& pose) {
    XpbdAttachmentAnchors out{};
    const auto& w = pose.world;

    out.hairRootPos       = w[static_cast<size_t>(BoneId::HairRootAnchor)].pos;
    out.capeClaspCenter   = w[static_cast<size_t>(BoneId::CapeClaspAnchor)].pos;

    const BoneTransform& clavL = w[static_cast<size_t>(BoneId::ClavicleL)];
    const BoneTransform& clavR = w[static_cast<size_t>(BoneId::ClavicleR)];
    out.capeShoulderLeft  = clavL.pos + QuatRotateVec(clavL.rot, {-6.0f, -6.0f, 2.0f});
    out.capeShoulderRight = clavR.pos + QuatRotateVec(clavR.rot, { 6.0f, -6.0f, 2.0f});

    const BoneTransform& pelv = w[static_cast<size_t>(BoneId::Pelvis)];
    out.beltLeft  = pelv.pos + QuatRotateVec(pelv.rot, {-10.0f,  0.0f, 2.0f});
    out.beltRight = pelv.pos + QuatRotateVec(pelv.rot, { 10.0f,  0.0f, 2.0f});
    out.beltBack  = pelv.pos + QuatRotateVec(pelv.rot, {  0.0f, -9.0f, 2.0f});

    out.torsoCapsuleA = pelv.pos;
    out.torsoCapsuleB = w[static_cast<size_t>(BoneId::Spine3)].pos;
    out.headCenter    = w[static_cast<size_t>(BoneId::Head)].pos;
    return out;
}

void RawSkeletonAnimEngine::EvaluateCharacterRig(Vec3 rootPos, float rootYawRad,
                                                 const AnimBlendParams& params,
                                                 const RawWorldPhysics* physicsOpt,
                                                 SkeletonPose& outPose,
                                                 XpbdAttachmentAnchors* outAnchorsOpt) {
    std::array<BoneTransform, kHumanoidBoneCount> local{};
    EvaluateBlendTree(params, local);
    ComputeForwardKinematics(rootPos, rootYawRad, local, outPose);
    if (physicsOpt != nullptr) {
        ApplyTerrainFootIK(rootPos, rootYawRad, *physicsOpt, outPose);
    }
    if (outAnchorsOpt != nullptr) {
        *outAnchorsOpt = ExtractXpbdAnchors(outPose);
    }
}

}  // namespace raw::worldgen
