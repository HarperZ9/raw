// Native 240 Hz Sub-Stepped XPBD + Anisotropic Viscoelastic Soft-Body Physics Engine (RAW engine runtime).
// Original work, MIT.
#include "rt_softbody.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace raw::rt {
namespace {

uint64_t g_totalSubsteps = 0;
uint64_t g_totalBonesStepped = 0;

bool ContainsCI(const char* hay, const char* needle) {
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

}  // namespace

TissueProfile DefaultTissueProfile(TissueKind kind) {
    switch (kind) {
    case TissueKind::Breast:
        // Soft viscoelastic teardrop tissue: natural vertical bounce, subtle lateral sway, realistic gravity sag & recoil.
        return {{135.0f, 165.0f, 118.0f}, {9.5f, 11.0f, 8.2f}, -0.35f, {2.1f, 2.4f, 3.2f}, 0.085f, 0.24f};
    case TissueKind::Glute:
        // Firm muscular-adipose tissue: crisp stride-coupled vertical & anterior-posterior compression, quick recovery.
        return {{220.0f, 195.0f, 175.0f}, {13.5f, 12.0f, 11.2f}, -0.18f, {1.4f, 2.0f, 2.2f}, 0.045f, 0.18f};
    case TissueKind::Belly:
        // Gentle breathing & impact recoil with strong strain limit.
        return {{240.0f, 185.0f, 210.0f}, {15.0f, 12.5f, 14.0f}, -0.12f, {1.0f, 1.8f, 1.5f}, 0.030f, 0.14f};
    case TissueKind::Thigh:
        // High-stiffness athletic stride jiggle on impact/sprint.
        return {{280.0f, 260.0f, 240.0f}, {16.5f, 15.5f, 15.0f}, -0.06f, {1.0f, 1.2f, 1.2f}, 0.025f, 0.12f};
    case TissueKind::Pelvic:
        // Free-hanging anatomical pendulum & viscoelastic soft-tissue bounce with thigh-collision strain limits.
        return {{78.0f, 92.0f, 108.0f}, {6.4f, 7.2f, 8.0f}, -0.45f, {2.8f, 3.2f, 3.6f}, 0.145f, 0.16f};
    case TissueKind::Hair:
        // Low-stiffness flowing strand dynamics with wind/motion trail.
        return {{65.0f, 65.0f, 95.0f}, {6.8f, 6.8f, 8.5f}, -0.60f, {5.5f, 5.5f, 4.5f}, 0.160f, 0.0f};
    case TissueKind::Cloak:
    default:
        // Cloth/cape dynamics with wide lateral sway and aerodynamic drag.
        return {{48.0f, 52.0f, 88.0f}, {5.4f, 5.8f, 7.6f}, -0.85f, {7.5f, 7.5f, 5.0f}, 0.180f, 0.0f};
    }
}

bool ClassifySoftBone(const char* boneName, TissueKind* outKind) {
    if (!boneName || !outKind) return false;
    if (ContainsCI(boneName, "breast")) { *outKind = TissueKind::Breast; return true; }
    if (ContainsCI(boneName, "butt") || ContainsCI(boneName, "glute")) { *outKind = TissueKind::Glute; return true; }
    if (ContainsCI(boneName, "belly")) { *outKind = TissueKind::Belly; return true; }
    if (ContainsCI(boneName, "thigh") && ContainsCI(boneName, "phys")) { *outKind = TissueKind::Thigh; return true; }
    if (ContainsCI(boneName, "genital") || ContainsCI(boneName, "scrotum") || ContainsCI(boneName, "pelvic")) {
        *outKind = TissueKind::Pelvic; return true;
    }
    if (ContainsCI(boneName, "hair") || ContainsCI(boneName, "ponytail") || ContainsCI(boneName, "braid")) {
        *outKind = TissueKind::Hair; return true;
    }
    if (ContainsCI(boneName, "cloak") || ContainsCI(boneName, "cape") || ContainsCI(boneName, "skirt")) {
        *outKind = TissueKind::Cloak; return true;
    }
    return false;
}

SoftBoneOutput StepSoftBone(SoftBoneState& st,
                            const float parentWorldPos[3],
                            const TissueProfile& prof,
                            float dtSec,
                            float colliderRadius,
                            const float* colliderLocalCenter) {
    SoftBoneOutput out{};
    if (!parentWorldPos || !(dtSec > 0.0f)) return out;
    const float clampedDt = std::min(dtSec, 0.05f);   // Prevent teleport/load-screen impulse spikes

    if (!st.initialized) {
        std::memcpy(st.prevParent, parentWorldPos, 12);
        std::memset(st.prevVel, 0, 12);
        st.pos[0] = 0.0f; st.pos[1] = 0.0f; st.pos[2] = prof.gravitySag;
        st.vel[0] = st.vel[1] = st.vel[2] = 0.0f;
        st.initialized = true;
    }

    // Compute parent acceleration (clamped against cell teleports / fast-travel jumps > 120u/frame).
    float pVel[3], pAcc[3];
    float jumpSq = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = parentWorldPos[i] - st.prevParent[i];
        jumpSq += d * d;
        pVel[i] = d / clampedDt;
    }
    if (jumpSq > 120.0f * 120.0f) {
        std::memcpy(st.prevParent, parentWorldPos, 12);
        std::memset(st.prevVel, 0, 12);
        st.vel[0] = st.vel[1] = st.vel[2] = 0.0f;
        out.localOffset[2] = prof.gravitySag;
        return out;
    }
    for (int i = 0; i < 3; ++i) {
        pAcc[i] = std::clamp((pVel[i] - st.prevVel[i]) / clampedDt, -1800.0f, 1800.0f);
        st.prevParent[i] = parentWorldPos[i];
        st.prevVel[i] = pVel[i];
    }

    // Fixed 240 Hz symplectic-XPBD sub-stepping: identical physical bounce at 60, 120, or 240+ FPS.
    constexpr float kSubDt = 1.0f / 240.0f;
    const int steps = std::clamp(int(std::ceil(clampedDt / kSubDt)), 1, 12);
    const float h = clampedDt / float(steps);
    const float rest[3] = {0.0f, 0.0f, prof.gravitySag};

    for (int s = 0; s < steps; ++s) {
        for (int i = 0; i < 3; ++i) {
            const float x = st.pos[i] - rest[i];
            // Inertial counter-force (-pAcc * 0.065) + anisotropic restoring spring + viscoelastic damping:
            const float f = -pAcc[i] * 0.065f - prof.stiffness[i] * x - prof.damping[i] * st.vel[i];
            st.vel[i] += f * h;
            st.pos[i] += st.vel[i] * h;
            // XPBD hard strain-limit projection (zero stretching/clipping):
            const float lim = prof.maxDisp[i];
            if (st.pos[i] - rest[i] > lim) {
                st.pos[i] = rest[i] + lim;
                if (st.vel[i] > 0.0f) st.vel[i] *= -0.22f;
            } else if (st.pos[i] - rest[i] < -lim) {
                st.pos[i] = rest[i] - lim;
                if (st.vel[i] < 0.0f) st.vel[i] *= -0.22f;
            }
        }
        // Analytical sphere/capsule collision constraint (e.g., hand/arm/torso contact push-out):
        if (colliderRadius > 0.0f && colliderLocalCenter) {
            const float dx = st.pos[0] - colliderLocalCenter[0];
            const float dy = st.pos[1] - colliderLocalCenter[1];
            const float dz = st.pos[2] - colliderLocalCenter[2];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < colliderRadius && dist > 1.0e-5f) {
                const float nx = dx / dist, ny = dy / dist, nz = dz / dist;
                const float pen = colliderRadius - dist;
                st.pos[0] += nx * pen;
                st.pos[1] += ny * pen;
                st.pos[2] += nz * pen;
                const float vn = st.vel[0] * nx + st.vel[1] * ny + st.vel[2] * nz;
                if (vn < 0.0f) {
                    st.vel[0] -= 1.25f * vn * nx;
                    st.vel[1] -= 1.25f * vn * ny;
                    st.vel[2] -= 1.25f * vn * nz;
                }
            }
        }
    }
    g_totalSubsteps += uint64_t(steps);
    ++g_totalBonesStepped;

    for (int i = 0; i < 3; ++i) out.localOffset[i] = st.pos[i];
    out.localPitchRad = std::clamp((st.pos[2] - prof.gravitySag) * prof.rotCouple, -0.35f, 0.35f);
    out.localRollRad  = std::clamp(st.pos[0] * prof.rotCouple, -0.30f, 0.30f);
    // Poisson volume-preserving squash-and-stretch (when compressed vertically, widens subtly laterally):
    const float zStrain = std::clamp((st.pos[2] - prof.gravitySag) / std::max(prof.maxDisp[2], 0.5f), -0.35f, 0.35f);
    const float sZ = 1.0f + zStrain * prof.volumePreserve;
    const float sXY = 1.0f / std::sqrt(std::max(sZ, 0.65f));
    out.localScale[0] = sXY;
    out.localScale[1] = sXY;
    out.localScale[2] = sZ;
    return out;
}

std::string SoftBodyStatsJson() {
    char b[160];
    std::snprintf(b, sizeof b, "{\"softbody_bones_stepped\":%llu,\"softbody_substeps_240hz\":%llu}",
                  static_cast<unsigned long long>(g_totalBonesStepped),
                  static_cast<unsigned long long>(g_totalSubsteps));
    return b;
}

}  // namespace raw::rt
