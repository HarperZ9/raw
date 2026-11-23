#pragma once
// Native 240 Hz Sub-Stepped XPBD + Anisotropic Viscoelastic Soft-Body Physics Engine (RAW engine runtime).
// Replaces legacy Euler CBPC and heavy CPU Bullet HDT-SMP bone physics with a zero-allocation,
// framerate-independent C++23 solver for breasts, glutes, thighs, belly, hair, and cloak bone chains.
// Original work, MIT.
#include <cstdint>
#include <string>

namespace raw::rt {

enum class TissueKind : uint8_t {
    Breast    = 0,
    Glute     = 1,
    Belly     = 2,
    Thigh     = 3,
    Pelvic    = 4,   // Multi-axis anatomical pendulum/scrotal viscoelastic dynamics
    Hair      = 5,
    Cloak     = 6,
};

struct TissueProfile {
    float stiffness[3];     // Anisotropic spring stiffness (X lateral, Y anterior-posterior, Z vertical)
    float damping[3];       // Viscoelastic damping ratios per axis
    float gravitySag;       // Rest-pose gravity sag offset (game units, -Z)
    float maxDisp[3];       // XPBD hard strain limit per axis (prevents clipping/stretching at any FPS)
    float rotCouple;        // Rotational inertia coupling (radians per unit displacement)
    float volumePreserve;   // Squash-and-stretch Poisson volume preservation ratio (0..0.45)
};

struct SoftBoneState {
    float pos[3] = {};          // Current local displacement (game units)
    float vel[3] = {};          // Current local velocity (game units/sec)
    float prevParent[3] = {};   // Previous parent world position (for acceleration forcing)
    float prevVel[3] = {};      // Previous parent world velocity
    bool  initialized = false;
};

struct SoftBoneOutput {
    float localOffset[3] = {};  // Local translation delta to apply to NiAVObject::local.translate
    float localPitchRad = 0.0f; // Local X pitch rotation delta (natural teardrop tilt)
    float localRollRad  = 0.0f; // Local Y roll rotation delta (lateral sway)
    float localScale[3] = {1.0f, 1.0f, 1.0f}; // Poisson volume-preserving squash/stretch
};

// Returns calibrated 2027 viscoelastic tissue parameters for each anatomical / cloth / hair region.
TissueProfile DefaultTissueProfile(TissueKind kind);

// Advances one soft-body bone by dtSec using fixed 240 Hz sub-steps with XPBD strain limiting & capsule clamping.
SoftBoneOutput StepSoftBone(SoftBoneState& state,
                            const float parentWorldPos[3],
                            const TissueProfile& prof,
                            float dtSec,
                            float colliderRadius = 0.0f,
                            const float* colliderLocalCenter = nullptr);

// Classifies a Skyrim skeleton bone name (XPMSSE / 3BA / UBE / vanilla) into a TissueKind; returns false if rigid.
bool ClassifySoftBone(const char* boneName, TissueKind* outKind);

std::string SoftBodyStatsJson();

}  // namespace raw::rt
