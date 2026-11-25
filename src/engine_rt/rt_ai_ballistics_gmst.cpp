// rt_ai_ballistics_gmst.cpp: Milestone Wave 62 Binary-Verified 1.7.104.0 AI Detection, Acoustic Sound,
// Ragdoll/Knockback Impulse, Projectile Ballistics & AI Melee GMSTs (Zero overlap across all 402 prior GMSTs!).
// Original work, MIT.
#include "rt_ai_ballistics_gmst.h"
#include "rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_callgraph.h"
#include "rt_version.h"
#include <windows.h>
#endif

namespace raw::rt {
namespace {

// Every single GMST RVA, on-disk float/int32 default, and .text reader verified directly against SkyrimSE.exe 1.7.104.0
// with STRICT ZERO OVERLAP against all 402 previously cataloged GMSTs across the 16 prior modules:
//   - AI Detection & Line-of-Sight (0x6A33B0, 0x6FF780, 0x819EF0):
//       fDetectionLOSDistanceAngle                 (0x209F0A8, float def=60.0,    reader  0x6A35DB)           -> 56.0
//       fDetectionLOSDistanceMultExterior          (0x209F0C0, float def=0.5,     reader  0x6A35FB)           -> 0.60
//       fDetectionLOSDistanceMultInterior          (0x209F0D8, float def=0.75,    reader  0x6A3605)           -> 0.80
//       fDetectionStateExpireTime                  (0x209F090, float def=30.0,    reader  0x6FF94B)           -> 24.0
//       fDetectProjectileDistanceNPC               (0x209F018, float def=512.0,   reader  0x6A3BBD)           -> 640.0
//       iCombatStealthPointSneakDetectionThreshold (0x20A3800, int32 def=70,      reader  0x81A0DB)           -> 65
//   - Acoustic Sound & Noise Levels (0x41D3F0, 0x41D230):
//       iSoundLevelNormal                          (0x209EEF8, int32 def=50,      reader  0x41D415)           -> 50
//       iSoundLevelLoud                            (0x209EF10, int32 def=100,     readers 0x41D41C, 0x7455E2) -> 105
//       iSoundLevelVeryLoud                        (0x209EF28, int32 def=200,     3 readers incl. 0x41D29A)   -> 210
//       fDetectEventSneakDistanceVeryLoud          (0x209EFB8, float def=3072.0,  reader  0x41D2A4)           -> 3400.0
//   - Ragdoll Impulse, Knockback & Death Force (0x4047DE, 0x7E8080, 0x6CA640, 0x6CECF5):
//       fGameplayImpulseMultBiped                  (0x20A9E70, float def=0.2,     reader  0x40482D)           -> 0.24
//       fGameplayImpulseMultClutter                (0x20A9EA0, float def=1.0,     reader  0x40484B)           -> 0.65
//       fGameplayImpulseMinMass                    (0x20A9EB8, float def=5.0,     reader  0x40486E)           -> 6.5
//       fExplosionMaxImpulse                       (0x20A9D08, float def=8000.0,  reader  0x7E82BA)           -> 6400.0
//       fKnockbackForceMax                         (0x20A72D8, float def=512.0,   reader  0x6CB315)           -> 480.0
//       fDeathForceMassBase                        (0x20A75D8, float def=3.0,     reader  0x6CEE32)           -> 3.5
//   - Projectile & Arrow Ballistics (0x18A070, 0x269010, 0x7DE03A, 0x8683C0, 0x55DDA0):
//       fAttachedArrowAgeMax                       (0x2077620, float def=30.0,    3 readers incl. 0x18A0E2)   -> 45.0
//       iMaxArrowsInQuiver                         (0x207A950, int32 def=20,      6 readers incl. 0x26909E)   -> 24
//       fArrowMinSpeedForTrails                    (0x20A7488, float def=1500.0,  reader  0x7DE045)           -> 1250.0
//       iBallisticProjectilePathPickSegments       (0x20A4998, int32 def=4,       reader  0x8687A1)           -> 6
//       fCombatRangedProjectileFiringArc           (0x20A1BE0, float def=30.0,    3 readers incl. 0x55DE41)   -> 32.0
//   - AI Melee & Stagger Mass Offset (0x41E7F0, 0x8B7A60, 0x41F560):
//       fAIMeleeWeaponMult                         (0x20A4368, float def=2.0,     reader  0x41E808)           -> 2.15
//       fCombatMeleeTrackTargetDistanceMax         (0x20A1988, float def=512.0,   reader  0x8B7C25)           -> 576.0
//       fStaggerMassOffsetBase                     (0x20A7E90, float def=3.0,     readers 0x41F56B, 0x41F6E9) -> 3.25
//   - Dead 0-Reader Legacy GMSTs (confirmed 0 .text readers via overlap-safe ModRM scan, strictly never written!):
//       fProjectileMaxDistance                     (0x20A63A8, float def=32768.0, 0 .text readers)
//       fProjectileDeflectionTime                  (0x20A4938, float def=1.0,     0 .text readers)
//       fProjectileCollisionImpulseScale           (0x20A6420, float def=1.0,     0 .text readers)
//       fArrowGravityMin                           (0x20A73B0, float def=0.05,    0 .text readers)
//       fAIMeleeArmorMult                          (0x20A4380, float def=1.0,     0 .text readers)
//       iDetectionHighNumPicks                     (0x20B69B0, int32 def=40,      0 .text readers)
constexpr AiBallisticsGmstSpec kAiBallisticsSpecs[] = {
    {"fDetectionLOSDistanceAngle",                 0x209F0A8u, false, 60.0f,    56.0f,    0,   0,   true,  "Detection"},
    {"fDetectionLOSDistanceMultExterior",          0x209F0C0u, false, 0.5f,     0.60f,    0,   0,   true,  "Detection"},
    {"fDetectionLOSDistanceMultInterior",          0x209F0D8u, false, 0.75f,    0.80f,    0,   0,   true,  "Detection"},
    {"fDetectionStateExpireTime",                  0x209F090u, false, 30.0f,    24.0f,    0,   0,   true,  "Detection"},
    {"fDetectProjectileDistanceNPC",               0x209F018u, false, 512.0f,   640.0f,   0,   0,   true,  "Detection"},
    {"iCombatStealthPointSneakDetectionThreshold", 0x20A3800u, true,  0.0f,     0.0f,     70,  65,  true,  "Detection"},
    {"iSoundLevelNormal",                          0x209EEF8u, true,  0.0f,     0.0f,     50,  50,  true,  "Sound"},
    {"iSoundLevelLoud",                            0x209EF10u, true,  0.0f,     0.0f,     100, 105, true,  "Sound"},
    {"iSoundLevelVeryLoud",                        0x209EF28u, true,  0.0f,     0.0f,     200, 210, true,  "Sound"},
    {"fDetectEventSneakDistanceVeryLoud",          0x209EFB8u, false, 3072.0f,  3400.0f,  0,   0,   true,  "Sound"},
    {"fGameplayImpulseMultBiped",                  0x20A9E70u, false, 0.2f,     0.24f,    0,   0,   true,  "Ragdoll"},
    {"fGameplayImpulseMultClutter",                0x20A9EA0u, false, 1.0f,     0.65f,    0,   0,   true,  "Ragdoll"},
    {"fGameplayImpulseMinMass",                    0x20A9EB8u, false, 5.0f,     6.5f,     0,   0,   true,  "Ragdoll"},
    {"fExplosionMaxImpulse",                       0x20A9D08u, false, 8000.0f,  6400.0f,  0,   0,   true,  "Ragdoll"},
    {"fKnockbackForceMax",                         0x20A72D8u, false, 512.0f,   480.0f,   0,   0,   true,  "Ragdoll"},
    {"fDeathForceMassBase",                        0x20A75D8u, false, 3.0f,     3.5f,     0,   0,   true,  "Ragdoll"},
    {"fAttachedArrowAgeMax",                       0x2077620u, false, 30.0f,    45.0f,    0,   0,   true,  "Ballistics"},
    {"iMaxArrowsInQuiver",                         0x207A950u, true,  0.0f,     0.0f,     20,  24,  true,  "Ballistics"},
    {"fArrowMinSpeedForTrails",                    0x20A7488u, false, 1500.0f,  1250.0f,  0,   0,   true,  "Ballistics"},
    {"iBallisticProjectilePathPickSegments",       0x20A4998u, true,  0.0f,     0.0f,     4,   6,   true,  "Ballistics"},
    {"fCombatRangedProjectileFiringArc",           0x20A1BE0u, false, 30.0f,    32.0f,    0,   0,   true,  "Ballistics"},
    {"fAIMeleeWeaponMult",                         0x20A4368u, false, 2.0f,     2.15f,    0,   0,   true,  "AIMelee"},
    {"fCombatMeleeTrackTargetDistanceMax",         0x20A1988u, false, 512.0f,   576.0f,   0,   0,   true,  "AIMelee"},
    {"fStaggerMassOffsetBase",                     0x20A7E90u, false, 3.0f,     3.25f,    0,   0,   true,  "Stagger"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fProjectileMaxDistance",                     0x20A63A8u, false, 32768.0f, 32768.0f, 0,   0,   false, "Ballistics"},
    {"fProjectileDeflectionTime",                  0x20A4938u, false, 1.0f,     1.0f,     0,   0,   false, "Ballistics"},
    {"fProjectileCollisionImpulseScale",           0x20A6420u, false, 1.0f,     1.0f,     0,   0,   false, "Ragdoll"},
    {"fArrowGravityMin",                           0x20A73B0u, false, 0.05f,    0.05f,    0,   0,   false, "Ballistics"},
    {"fAIMeleeArmorMult",                          0x20A4380u, false, 1.0f,     1.0f,     0,   0,   false, "AIMelee"},
    {"iDetectionHighNumPicks",                     0x20B69B0u, true,  0.0f,     0.0f,     40,  40,  false, "Detection"},
};

constexpr size_t kAiBallisticsSpecCount = sizeof(kAiBallisticsSpecs) / sizeof(kAiBallisticsSpecs[0]);

std::mutex           g_abStateMu;
RawAiBallisticsState g_abState{};

}  // namespace

const AiBallisticsGmstSpec* AiBallisticsGmstTable(size_t* outCount) {
    if (outCount) *outCount = kAiBallisticsSpecCount;
    return kAiBallisticsSpecs;
}

bool VerifyAiBallisticsGmstTable(const Image& img, uint32_t* outVerifiedCount) {
    if (outVerifiedCount) *outVerifiedCount = 0u;
    if (!img.base || img.sizeOfImage < 0x20C0000u) return false;

    uint64_t imageBase = reinterpret_cast<uint64_t>(img.base);
    if (img.base[0] == 'M' && img.base[1] == 'Z') {
        uint32_t lfanew = 0u;
        std::memcpy(&lfanew, img.base + 0x3Cu, 4u);
        if (lfanew + 0x40u < img.sizeOfImage) {
            uint64_t optBase = 0ull;
            std::memcpy(&optBase, img.base + lfanew + 0x30u, 8u);
            if (optBase >= 0x100000000ull) imageBase = optBase;
        }
    }

    uint32_t verified = 0u;
    for (size_t i = 0; i < kAiBallisticsSpecCount; ++i) {
        const auto& spec = kAiBallisticsSpecs[i];
        if (spec.expectedRva + 24u > img.sizeOfImage) return false;

        const uint8_t* obj = img.base + spec.expectedRva;
        uint64_t nameVa = 0ull;
        std::memcpy(&nameVa, obj + 16u, 8u);

        uint64_t nameRva = 0ull;
        if (nameVa >= imageBase && (nameVa - imageBase) < img.sizeOfImage) {
            nameRva = nameVa - imageBase;
        } else if (nameVa >= reinterpret_cast<uint64_t>(img.base) &&
                   (nameVa - reinterpret_cast<uint64_t>(img.base)) < img.sizeOfImage) {
            nameRva = nameVa - reinterpret_cast<uint64_t>(img.base);
        } else {
            return false;
        }

        const size_t nameLen = std::strlen(spec.name);
        if (nameRva + nameLen + 1u > img.sizeOfImage) return false;
        if (std::memcmp(img.base + nameRva, spec.name, nameLen + 1u) != 0) return false;

        if (spec.isInt32) {
            int32_t ival = 0;
            std::memcpy(&ival, obj + 8u, 4u);
            if (ival != spec.exeDefaultI) return false;
        } else {
            float fval = 0.0f;
            std::memcpy(&fval, obj + 8u, 4u);
            const float tol = (std::max)(1.0e-4f, std::fabs(spec.exeDefaultF) * 1.0e-4f);
            if (!std::isfinite(fval) || std::fabs(fval - spec.exeDefaultF) > tol) return false;
        }

        ++verified;
    }

    if (outVerifiedCount) *outVerifiedCount = verified;
    return verified == kAiBallisticsSpecCount;
}

AcousticDetectionResult EvaluateAcousticDetectionScore(float soundLevel, float distanceU,
                                                       float losAngleDeg, bool isExterior) {
    AcousticDetectionResult out{};
    const float snd  = std::isfinite(soundLevel)  ? std::clamp(soundLevel, 0.0f, 300.0f)    : 0.0f;
    const float dist = std::isfinite(distanceU)   ? (std::max)(distanceU, 1.0f)             : 1000.0f;
    const float ang  = std::isfinite(losAngleDeg) ? std::fabs(losAngleDeg)                  : 180.0f;

    // Acoustic falloff scaled to balanced fDetectEventSneakDistanceVeryLoud=3400 & iSoundLevelVeryLoud=210:
    const float maxHearingDist = 3400.0f * (snd / 210.0f);
    out.soundScore01 = (maxHearingDist > 1.0f && dist < maxHearingDist)
                           ? std::clamp(1.0f - (dist / maxHearingDist), 0.0f, 1.0f)
                           : 0.0f;

    // LOS cone visibility using balanced fDetectionLOSDistanceAngle=56, Ext=0.60, Int=0.80:
    const float losMult  = isExterior ? 0.60f : 0.80f;
    const float maxSight = 3000.0f * losMult;
    if (ang <= 56.0f && dist < maxSight) {
        const float angFactor  = 1.0f - 0.5f * (ang / 56.0f);
        const float distFactor = 1.0f - (dist / maxSight);
        out.losVisibility01    = std::clamp(angFactor * distFactor, 0.0f, 1.0f);
    } else {
        out.losVisibility01 = 0.0f;
    }

    out.combinedAlert01 = std::clamp(1.0f - (1.0f - out.soundScore01) * (1.0f - out.losVisibility01), 0.0f, 1.0f);
    out.detected        = (out.combinedAlert01 >= 0.65f);
    return out;
}

BallisticImpulseResult EvaluateBallisticImpactImpulse(float projectileSpeedU, float projectileMassKg,
                                                      float targetMassKg, bool isBiped) {
    BallisticImpulseResult out{};
    const float spd   = std::isfinite(projectileSpeedU) ? std::clamp(projectileSpeedU, 0.0f, 20000.0f) : 0.0f;
    const float pMass = std::isfinite(projectileMassKg) ? std::clamp(projectileMassKg, 0.01f, 50.0f)   : 0.1f;
    const float tMass = std::isfinite(targetMassKg)     ? (std::max)(targetMassKg, 6.5f)               : 70.0f;

    out.trailActive = (spd >= 1250.0f);
    out.rawMomentum = spd * pMass;

    // Scale impulse by balanced fGameplayImpulseMultBiped=0.24 vs fGameplayImpulseMultClutter=0.65,
    // attenuated by target inertia (min mass 6.5) and capped at fKnockbackForceMax=480:
    const float mult = isBiped ? 0.24f : 0.65f;
    const float rawImp = (out.rawMomentum * mult) / std::sqrt(tMass / 6.5f);
    out.appliedImpulse = std::clamp(rawImp, 0.0f, 480.0f);
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedAiBallisticsEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                          g_abGmstMu;
std::vector<SavedAiBallisticsEntry> g_savedAb;
bool                                g_abApplied = false;

static bool SehReadAbU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteAbU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitAiBallisticsGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[AiBallisticsGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[AiBallisticsGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_abGmstMu);
    if (g_abApplied) {
        return "[AiBallisticsGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedAb.clear();

    for (size_t i = 0; i < kAiBallisticsSpecCount; ++i) {
        const auto& spec = kAiBallisticsSpecs[i];
        if (!spec.hasLiveReader) {
            ++skippedDead;
            continue;
        }
        uint32_t rva = 0u;
        for (const auto& r : refs) {
            if (r.setting && r.name == spec.name) {
                rva = r.rva;
                break;
            }
        }
        if (rva == 0u) continue;
        if (TableVersion() == "1.7.104.0" && rva != spec.expectedRva) continue;

        uint8_t* field = const_cast<uint8_t*>(img.base) + rva + 8u;
        uint32_t orig32 = 0u;
        if (!SehReadAbU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteAbU32(field, target32)) {
            g_savedAb.push_back({field, orig32});
            ++applied;
        }
    }
    g_abApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_abStateMu);
        g_abState.gmstsVerified      = 1u;
        g_abState.gmstsAppliedCount  = static_cast<uint8_t>((std::min)(applied, static_cast<size_t>(255u)));
        g_abState.deadLegacySkipped  = static_cast<uint8_t>((std::min)(skippedDead, static_cast<size_t>(255u)));
        g_abState.aiBallisticsActive = g_abApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[AiBallisticsGMST] applied %zu binary-verified detection/sound/ragdoll/ballistics GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownAiBallisticsGmst() {
    std::lock_guard<std::mutex> lk(g_abGmstMu);
    if (!g_abApplied) return;
    for (const auto& e : g_savedAb) {
        SehWriteAbU32(e.ptr, e.saved32);
    }
    g_savedAb.clear();
    g_abApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_abStateMu);
        g_abState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetAiBallisticsState(raw::rt::RawAiBallisticsState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_abStateMu);
    *out = raw::rt::g_abState;
    return 1;
}
