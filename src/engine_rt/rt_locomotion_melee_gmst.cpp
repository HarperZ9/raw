// rt_locomotion_melee_gmst.cpp: Wave 53 Binary-Verified 1.7.104.0 Locomotion, Armor-Weight Fatigue,
// Power-Attack & Melee Combat GMSTs.
// Original work, MIT.
#include "rt_locomotion_melee_gmst.h"
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

// Every single GMST RVA, on-disk float default, and .text reader verified directly against SkyrimSE.exe 1.7.104.0:
//   - Locomotion & Armor-Weight Curve (fn:Actor::ComputeMoveWeightPenalty 0x420820, 0x420900, 0x420980, 0x3FD820):
//       fMoveWeightMin                       (0x20AA3E0, def=0.0,     8 readers at 0x420820+) -> 8.0
//       fMoveWeightMax                       (0x20AA3F8, def=150.0,   4 readers at 0x420820+) -> 110.0
//       fMoveEncumEffect                     (0x20AA410, def=0.4,     3 readers at 0x42085F+) -> 0.32
//       fMoveEncumEffectNoWeapon             (0x20AA428, def=0.3,     reader  0x4208DF)       -> 0.22
//       fMoveCharWalkBase                    (0x20AA440, def=85.0,    reader  0x42094B)       -> 96.0
//       fMoveCharRunBase                     (0x20AA4A0, def=370.0,   reader  0x4209D1)       -> 385.0
//       fMoveSprintMult                      (0x20AA4B8, def=1.3,     reader  0x4209F4)       -> 1.38
//       fJumpMoveMult                        (0x20A89B8, def=0.3,     reader  0x3FD853)       -> 0.42
//   - Armor-Weight Sprint Fatigue & Carry Encumbrance (fn:Actor::ComputeSprintStaminaDrain 0x4214C0, 0x420B30):
//       fSprintStaminaWeightBase             (0x20AAB90, def=1.0,     reader  0x4214CF)       -> 0.85
//       fSprintStaminaWeightMult             (0x20AABA8, def=0.0,     reader  0x4214C3)       -> 0.008
//       fSprintStaminaDrainMult              (0x20A8B68, def=15.0,    reader  0x4214DB)       -> 14.5
//       fActorStrengthEncumbranceMult        (0x20A8BC8, def=5.0,     reader  0x420B33)       -> 5.0
//   - Power-Attack Stamina, Bash & Melee Precision (0x41F760, 0x449B00, 0x685CD0, 0x6CBEE0, 0x75D410):
//       fStaminaAttackWeaponBase             (0x20A7ED8, def=7.0,     reader  0x41F816)       -> 8.5
//       fStaminaAttackWeaponMult             (0x20A7EF0, def=0.1,     reader  0x41F7F3)       -> 0.14
//       fPowerAttackStaminaPenalty           (0x20A7F08, def=5.0,     reader  0x41F81E)       -> 6.5
//       fStaminaBashBase                     (0x20A7BC0, def=5.0,     reader  0x41F7B6)       -> 7.0
//       fPowerAttackCoolDownTime             (0x20A5298, def=2.0,     reader  0x75D413)       -> 0.45
//       fCombatHitConeAngle                  (0x20A5CE8, def=35.0,    3 readers at 0x449B1A+) -> 24.0
//       fMeleeSweepViewAngleMult             (0x20A4A28, def=2.0,     reader  0x6CC2BF)       -> 0.75
//       fActorDefaultTurningSpeed            (0x20A03B0, def=6.28319, reader  0x685D53)       -> 5.40
//   - Dead Legacy 0-Reader GMSTs (confirmed 0 .text readers via exhaustive ModRM scan, strictly never written!):
//       fPowerAttackDefaultBonus             (0x20A5790, def=3.0,     0 .text readers)
//       fSprintEncumbranceMult               (0x20AA4D0, def=1.0,     0 .text readers)
//       fMeleeMovementRestrictionsUpdateTime (0x20A4A10, def=1.0,     0 .text readers)
//       fMovementNearTargetAvoidRadius       (0x20A48D8, def=64.0,    0 .text readers)
//       fMovementNearTargetAvoidCost         (0x20A48F0, def=25.0,    0 .text readers)
constexpr LocomotionMeleeGmstSpec kLocomotionMeleeSpecs[] = {
    {"fMoveWeightMin",                       0x20AA3E0u, 0.0f,       8.0f,   true,  "Locomotion"},
    {"fMoveWeightMax",                       0x20AA3F8u, 150.0f,     110.0f, true,  "Locomotion"},
    {"fMoveEncumEffect",                     0x20AA410u, 0.4f,       0.32f,  true,  "Locomotion"},
    {"fMoveEncumEffectNoWeapon",             0x20AA428u, 0.3f,       0.22f,  true,  "Locomotion"},
    {"fMoveCharWalkBase",                    0x20AA440u, 85.0f,      96.0f,  true,  "Locomotion"},
    {"fMoveCharRunBase",                     0x20AA4A0u, 370.0f,     385.0f, true,  "Locomotion"},
    {"fMoveSprintMult",                      0x20AA4B8u, 1.3f,       1.38f,  true,  "Locomotion"},
    {"fJumpMoveMult",                        0x20A89B8u, 0.3f,       0.42f,  true,  "Locomotion"},
    {"fSprintStaminaWeightBase",             0x20AAB90u, 1.0f,       0.85f,  true,  "Fatigue"},
    {"fSprintStaminaWeightMult",             0x20AABA8u, 0.0f,       0.008f, true,  "Fatigue"},
    {"fSprintStaminaDrainMult",              0x20A8B68u, 15.0f,      14.5f,  true,  "Fatigue"},
    {"fActorStrengthEncumbranceMult",        0x20A8BC8u, 5.0f,       5.0f,   true,  "Fatigue"},
    {"fStaminaAttackWeaponBase",             0x20A7ED8u, 7.0f,       8.5f,   true,  "PowerAttack"},
    {"fStaminaAttackWeaponMult",             0x20A7EF0u, 0.1f,       0.14f,  true,  "PowerAttack"},
    {"fPowerAttackStaminaPenalty",           0x20A7F08u, 5.0f,       6.5f,   true,  "PowerAttack"},
    {"fStaminaBashBase",                     0x20A7BC0u, 5.0f,       7.0f,   true,  "PowerAttack"},
    {"fPowerAttackCoolDownTime",             0x20A5298u, 2.0f,       0.45f,  true,  "Melee"},
    {"fCombatHitConeAngle",                  0x20A5CE8u, 35.0f,      24.0f,  true,  "Melee"},
    {"fMeleeSweepViewAngleMult",             0x20A4A28u, 2.0f,       0.75f,  true,  "Melee"},
    {"fActorDefaultTurningSpeed",            0x20A03B0u, 6.2831855f, 5.40f,  true,  "Melee"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fPowerAttackDefaultBonus",             0x20A5790u, 3.0f,       3.0f,   false, "PowerAttack"},
    {"fSprintEncumbranceMult",               0x20AA4D0u, 1.0f,       1.0f,   false, "Fatigue"},
    {"fMeleeMovementRestrictionsUpdateTime", 0x20A4A10u, 1.0f,       1.0f,   false, "Melee"},
    {"fMovementNearTargetAvoidRadius",       0x20A48D8u, 64.0f,      64.0f,  false, "Locomotion"},
    {"fMovementNearTargetAvoidCost",         0x20A48F0u, 25.0f,      25.0f,  false, "Locomotion"},
};

constexpr size_t kLocomotionMeleeSpecCount = sizeof(kLocomotionMeleeSpecs) / sizeof(kLocomotionMeleeSpecs[0]);

std::mutex              g_lmStateMu;
RawLocomotionMeleeState g_lmState{};

}  // namespace

const LocomotionMeleeGmstSpec* LocomotionMeleeGmstTable(size_t* outCount) {
    if (outCount) *outCount = kLocomotionMeleeSpecCount;
    return kLocomotionMeleeSpecs;
}

bool VerifyLocomotionMeleeGmstTable(const Image& img, uint32_t* outVerifiedCount) {
    if (outVerifiedCount) *outVerifiedCount = 0u;
    if (!img.base || img.sizeOfImage < 0x20B0000u) return false;

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
    for (size_t i = 0; i < kLocomotionMeleeSpecCount; ++i) {
        const auto& spec = kLocomotionMeleeSpecs[i];
        if (spec.expectedRva + 24u > img.sizeOfImage) return false;

        const uint8_t* obj = img.base + spec.expectedRva;
        float val = 0.0f;
        uint64_t nameVa = 0ull;
        std::memcpy(&val, obj + 8u, 4u);
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

        const float tol = std::max(1.0e-4f, std::fabs(spec.exeDefault) * 1.0e-4f);
        if (!std::isfinite(val) || std::fabs(val - spec.exeDefault) > tol) return false;

        ++verified;
    }

    if (outVerifiedCount) *outVerifiedCount = verified;
    return verified == kLocomotionMeleeSpecCount;
}

LocomotionProfile EvaluateLocomotionProfile(const LocomotionInput& in) {
    LocomotionProfile out{};
    const float w = std::isfinite(in.equippedArmorWeight) ? std::clamp(in.equippedArmorWeight, 0.0f, 200.0f) : 0.0f;

    // Exact 0x420820 weight-fraction curve with verified fMoveWeightMin=8.0, fMoveWeightMax=110.0:
    const float wFrac = std::clamp((w - 8.0f) / (110.0f - 8.0f), 0.0f, 1.0f);
    out.armorWeightFrac01 = wFrac;

    const float encumEffect = in.weaponDrawn ? 0.32f : 0.22f;
    out.speedMultiplier = std::clamp(1.0f - encumEffect * wFrac, 0.60f, 1.0f);

    float baseSpeed = 385.0f;  // fMoveCharRunBase
    if (in.isSneaking) {
        baseSpeed *= 0.62f;
    } else if (in.isSprinting) {
        baseSpeed *= 1.38f;    // fMoveSprintMult
    }
    out.effectiveSpeedUnitsSec = std::clamp(baseSpeed * out.speedMultiplier, 80.0f, 650.0f);

    // Exact 0x4214C0 sprint stamina drain = (fSprintStaminaWeightBase + w * fSprintStaminaWeightMult) * fSprintStaminaDrainMult:
    out.sprintStaminaDrainPerS = std::clamp((0.85f + w * 0.008f) * 14.5f, 8.0f, 40.0f);
    return out;
}

PowerAttackResult EvaluatePowerAttackCost(const PowerAttackInput& in) {
    PowerAttackResult out{};
    const float w = std::isfinite(in.weaponWeight) ? std::clamp(in.weaponWeight, 1.0f, 60.0f) : 12.0f;
    const float s = std::isfinite(in.combatSkill0_100) ? std::clamp(in.combatSkill0_100, 0.0f, 100.0f) / 100.0f : 0.25f;

    // Grounded 0x41F760 power-attack stamina curve: (base + weight * mult) * gripScale + penalty, with skill efficiency:
    const float rawCost = (8.5f + w * 0.14f) * (in.isTwoHanded ? 1.35f : 1.0f) + 6.5f;
    const float dualMul = in.isDualPowerAttack ? 1.45f : 1.0f;
    const float effMul  = 1.0f - 0.18f * s;

    out.staminaCost     = std::clamp(rawCost * dualMul * effMul, 8.0f, 85.0f);
    out.effectiveReachU = std::clamp((in.isTwoHanded ? 128.0f : 86.0f) + w * 0.85f, 75.0f, 175.0f);
    out.hitConeAngleDeg = 24.0f;
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedLocoMeleeEntry {
    float* ptr        = nullptr;
    float  savedValue = 0.0f;
};

std::mutex                       g_lmGmstMu;
std::vector<SavedLocoMeleeEntry> g_savedLm;
bool                             g_lmApplied = false;

static bool SehReadLmFloat(const float* p, float* out) noexcept {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteLmFloat(float* p, float v) noexcept {
    if (!p || !std::isfinite(v)) return false;
    __try {
        *p = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitLocomotionMeleeGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[LocomotionMeleeGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[LocomotionMeleeGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_lmGmstMu);
    if (g_lmApplied) {
        return "[LocomotionMeleeGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedLm.clear();

    for (size_t i = 0; i < kLocomotionMeleeSpecCount; ++i) {
        const auto& spec = kLocomotionMeleeSpecs[i];
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

        auto* field = reinterpret_cast<float*>(const_cast<uint8_t*>(img.base) + rva + 8u);
        float orig = 0.0f;
        if (!SehReadLmFloat(field, &orig)) continue;
        if (SehWriteLmFloat(field, spec.balancedValue)) {
            g_savedLm.push_back({field, orig});
            ++applied;
        }
    }
    g_lmApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_lmStateMu);
        g_lmState.gmstsVerified         = 1u;
        g_lmState.gmstsAppliedCount     = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_lmState.deadLegacySkipped     = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_lmState.locomotionMeleeActive = g_lmApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[LocomotionMeleeGMST] applied %zu binary-verified locomotion/fatigue/power-attack/melee GMSTs (%zu dead 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownLocomotionMeleeGmst() {
    std::lock_guard<std::mutex> lk(g_lmGmstMu);
    if (!g_lmApplied) return;
    for (const auto& e : g_savedLm) {
        SehWriteLmFloat(e.ptr, e.savedValue);
    }
    g_savedLm.clear();
    g_lmApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_lmStateMu);
        g_lmState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetLocomotionMeleeState(raw::rt::RawLocomotionMeleeState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_lmStateMu);
    *out = raw::rt::g_lmState;
    return 1;
}
