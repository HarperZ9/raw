#pragma once
// rt_ai_ballistics_gmst.h: Milestone Wave 62 Binary-Verified 1.7.104.0 AI Detection, Acoustic Sound,
// Ragdoll/Knockback Impulse, Projectile Ballistics & AI Melee GMSTs (432 Total Unique Verified GMSTs!).
// 100% binary-verified RVAs, .rdata name pointers & on-disk defaults (float + int32);
// fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct AiBallisticsGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "Detection" | "Sound" | "Ragdoll" | "Ballistics" | "AIMelee" | "Stagger"
};

const AiBallisticsGmstSpec* AiBallisticsGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyAiBallisticsGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Acoustic Detection & Ballistic Ragdoll Impulse Evaluators
// =================================================================================================
struct AcousticDetectionResult {
    float soundScore01     = 0.0f;   // [0..1] distance-attenuated acoustic perceptibility
    float losVisibility01  = 0.0f;   // [0..1] cone-bounded line-of-sight factor
    float combinedAlert01  = 0.0f;   // [0..1] combined detection score
    bool  detected         = false;  // exceeds sneak detection threshold (65/100)
};

AcousticDetectionResult EvaluateAcousticDetectionScore(float soundLevel, float distanceU,
                                                       float losAngleDeg, bool isExterior);

struct BallisticImpulseResult {
    float rawMomentum      = 0.0f;
    float appliedImpulse   = 0.0f;   // bounded by fKnockbackForceMax=480 & scaled by biped/clutter mult
    bool  trailActive      = false;  // speed >= fArrowMinSpeedForTrails (1250 u/s)
};

BallisticImpulseResult EvaluateBallisticImpactImpulse(float projectileSpeedU, float projectileMassKg,
                                                      float targetMassKg, bool isBiped);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetAiBallisticsState)
// =================================================================================================
struct RawAiBallisticsState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t aiBallisticsActive       = 1u;
    float   detectionLosAngleDeg     = 56.0f;
    float   explosionMaxImpulse      = 6400.0f;
    float   knockbackForceMax        = 480.0f;
    float   attachedArrowAgeSec      = 45.0f;
    int32_t sneakDetectThreshold     = 65;
    int32_t ballisticPickSegments    = 6;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitAiBallisticsGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownAiBallisticsGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetAiBallisticsState(raw::rt::RawAiBallisticsState* out);
