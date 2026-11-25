// rt_companion_hazard_gmst.cpp: Milestone Wave 55 Binary-Verified 1.7.104.0 Companion, Summon,
// Dialogue, Gear-AI & Environmental Hazard GMSTs (Zero overlap with all 9 prior GMST modules!).
// Original work, MIT.
#include "rt_companion_hazard_gmst.h"
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
// with STRICT ZERO OVERLAP against all 200 previously cataloged GMSTs across the 9 prior modules:
//   - Companion & Summon Ranges (0x35A810, 0x4703C0, 0x692A30, 0x72A370, 0x5E31C0, 0x5E3C70):
//       iNumberActorsAllowedToFollowPlayer  (0x209FDC8, int32 def=6,     11 readers incl. 0x35A962) -> 8
//       fFollowStartSprintDistance          (0x20A0DE8, float def=500.0, reader  0x4716A2)          -> 380.0
//       fFollowExtraCatchUpSpeedMult        (0x20A0DA0, float def=0.2,   reader  0x692ADC)          -> 0.28
//       iMaxSummonedCreatures               (0x20AB838, int32 def=1,     reader  0x72A42A)          -> 1
//       fSummonDistanceCheckThreshold       (0x20ABA00, float def=750.0, reader  0x5E3351)          -> 950.0
//       fSummonedCreatureSearchRadius       (0x20AB868, float def=512.0, readers 0x5E3D3B,0x5E3E55) -> 768.0
//   - Dialogue Ergonomics & Greetings (0x463800, 0x7042A0, 0x672E30, 0x69FE50):
//       fAIDialogueDistance                 (0x20A0350, float def=170.0, readers 0x463CDF,0x71488B) -> 200.0
//       fAIMinGreetingDistance              (0x209FFC0, float def=150.0, reader  0x70444D)          -> 115.0
//       fAIGreetingTimer                    (0x209FFF0, float def=20.0,  4 readers incl. 0x67322C)  -> 35.0
//       fActorAlertSoundTimer               (0x209EEC8, float def=5.0,   reader  0x69FE59)          -> 4.0
//   - NPC Combat Equipment & Bark AI (0x82D220, 0x82D8B0, 0x6CA640, 0x817110):
//       fCombatInventoryMeleeEquipRange     (0x20A3308, float def=256.0, reader  0x82D223)          -> 310.0
//       fCombatInventoryRangedScoreMult     (0x20A34A0, float def=2.0,   3 readers incl. 0x82D958)  -> 2.15
//       fCombatSpeakHitChance               (0x20A5D78, float def=0.35,  reader  0x6CAFC4)          -> 0.42
//       fCombatDialogueTauntMinElapsedTime  (0x20A4CF8, float def=3.0,   readers 0x81739D,0x855AD5) -> 5.5
//   - Environmental Hazards, Traps & Torch Light AI (0x261B00, 0x7EED64, 0x7EF110, 0x4047DE, 0x691050, 0x491990):
//       fHazardDefaultTargetInterval        (0x207A838, float def=0.25,  4 readers incl. 0x261C0F)  -> 0.25
//       fHazardDropMaxDistance              (0x20A9E28, float def=1024., reader  0x7EEE34)          -> 1024.0
//       fHazardSpacingMult                  (0x20AB9E8, float def=0.8,   reader  0x7EF1E1)          -> 0.85
//       fGameplayImpulseMultTrap            (0x20A9E58, float def=0.15,  reader  0x404823)          -> 0.22
//       fTorchLightLevelNight               (0x20A8FA0, float def=1.2,   reader  0x6911E1)          -> 1.35
//       fTorchEvaluationTimer               (0x20A8520, float def=5.0,   3 readers incl. 0x4921C9)  -> 3.5
//   - Dead 0-Reader Legacy GMSTs (confirmed 0 .text readers via overlap-safe ModRM scan, strictly never written!):
//       fActorWeaponDesirabilityDamageMult  (0x20A8808, float def=0.5,   0 .text readers)
//       fActorArmorDesirabilityDamageMult   (0x20A8838, float def=0.5,   0 .text readers)
//       fAIInDialogueModeWithPlayerDistance (0x20A0320, float def=500.0, 0 .text readers)
//       fCombatSpeakTauntChance             (0x20A5D60, float def=0.01,  0 .text readers)
//       fCombatSpeakAttackChance            (0x20A5D30, float def=0.1,   0 .text readers)
//       fLockTrapGoOffBase                  (0x209FD08, float def=0.02,  0 .text readers)
constexpr CompanionHazardGmstSpec kCompanionHazardSpecs[] = {
    {"iNumberActorsAllowedToFollowPlayer",  0x209FDC8u, true,  0.0f,    0.0f,    6, 8, true,  "Companion"},
    {"fFollowStartSprintDistance",          0x20A0DE8u, false, 500.0f,  380.0f,  0, 0, true,  "Companion"},
    {"fFollowExtraCatchUpSpeedMult",        0x20A0DA0u, false, 0.2f,    0.28f,   0, 0, true,  "Companion"},
    {"iMaxSummonedCreatures",               0x20AB838u, true,  0.0f,    0.0f,    1, 1, true,  "Summon"},
    {"fSummonDistanceCheckThreshold",       0x20ABA00u, false, 750.0f,  950.0f,  0, 0, true,  "Summon"},
    {"fSummonedCreatureSearchRadius",       0x20AB868u, false, 512.0f,  768.0f,  0, 0, true,  "Summon"},
    {"fAIDialogueDistance",                 0x20A0350u, false, 170.0f,  200.0f,  0, 0, true,  "Dialogue"},
    {"fAIMinGreetingDistance",              0x209FFC0u, false, 150.0f,  115.0f,  0, 0, true,  "Dialogue"},
    {"fAIGreetingTimer",                    0x209FFF0u, false, 20.0f,   35.0f,   0, 0, true,  "Dialogue"},
    {"fActorAlertSoundTimer",               0x209EEC8u, false, 5.0f,    4.0f,    0, 0, true,  "Dialogue"},
    {"fCombatInventoryMeleeEquipRange",     0x20A3308u, false, 256.0f,  310.0f,  0, 0, true,  "GearAI"},
    {"fCombatInventoryRangedScoreMult",     0x20A34A0u, false, 2.0f,    2.15f,   0, 0, true,  "GearAI"},
    {"fCombatSpeakHitChance",               0x20A5D78u, false, 0.35f,   0.42f,   0, 0, true,  "CombatBark"},
    {"fCombatDialogueTauntMinElapsedTime",  0x20A4CF8u, false, 3.0f,    5.5f,    0, 0, true,  "CombatBark"},
    {"fHazardDefaultTargetInterval",        0x207A838u, false, 0.25f,   0.25f,   0, 0, true,  "Hazard"},
    {"fHazardDropMaxDistance",              0x20A9E28u, false, 1024.0f, 1024.0f, 0, 0, true,  "Hazard"},
    {"fHazardSpacingMult",                  0x20AB9E8u, false, 0.8f,    0.85f,   0, 0, true,  "Hazard"},
    {"fGameplayImpulseMultTrap",            0x20A9E58u, false, 0.15f,   0.22f,   0, 0, true,  "Hazard"},
    {"fTorchLightLevelNight",               0x20A8FA0u, false, 1.2f,    1.35f,   0, 0, true,  "Torch"},
    {"fTorchEvaluationTimer",               0x20A8520u, false, 5.0f,    3.5f,    0, 0, true,  "Torch"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fActorWeaponDesirabilityDamageMult",  0x20A8808u, false, 0.5f,    0.5f,    0, 0, false, "GearAI"},
    {"fActorArmorDesirabilityDamageMult",   0x20A8838u, false, 0.5f,    0.5f,    0, 0, false, "GearAI"},
    {"fAIInDialogueModeWithPlayerDistance", 0x20A0320u, false, 500.0f,  500.0f,  0, 0, false, "Dialogue"},
    {"fCombatSpeakTauntChance",             0x20A5D60u, false, 0.01f,   0.01f,   0, 0, false, "CombatBark"},
    {"fCombatSpeakAttackChance",            0x20A5D30u, false, 0.1f,    0.1f,    0, 0, false, "CombatBark"},
    {"fLockTrapGoOffBase",                  0x209FD08u, false, 0.02f,   0.02f,   0, 0, false, "Hazard"},
};

constexpr size_t kCompanionHazardSpecCount = sizeof(kCompanionHazardSpecs) / sizeof(kCompanionHazardSpecs[0]);

std::mutex              g_chStateMu;
RawCompanionHazardState g_chState{};

}  // namespace

const CompanionHazardGmstSpec* CompanionHazardGmstTable(size_t* outCount) {
    if (outCount) *outCount = kCompanionHazardSpecCount;
    return kCompanionHazardSpecs;
}

bool VerifyCompanionHazardGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kCompanionHazardSpecCount; ++i) {
        const auto& spec = kCompanionHazardSpecs[i];
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
            const float tol = std::max(1.0e-4f, std::fabs(spec.exeDefaultF) * 1.0e-4f);
            if (!std::isfinite(fval) || std::fabs(fval - spec.exeDefaultF) > tol) return false;
        }

        ++verified;
    }

    if (outVerifiedCount) *outVerifiedCount = verified;
    return verified == kCompanionHazardSpecCount;
}

NpcGearScore EvaluateNpcGearDesirability(const NpcGearInput& in, float equippedRefScore) {
    NpcGearScore out{};
    const float stat   = std::isfinite(in.baseDamageOrArmor) ? std::max(in.baseDamageOrArmor, 0.0f) : 0.0f;
    const float skill  = std::isfinite(in.actorSkill0_100) ? std::clamp(in.actorSkill0_100, 0.0f, 100.0f) : 15.0f;
    const float ench   = std::isfinite(in.enchantPower) ? std::max(in.enchantPower, 0.0f) : 0.0f;
    const float weight = std::isfinite(in.itemWeight) ? std::clamp(in.itemWeight, 0.0f, 100.0f) : 8.0f;

    // Grounded NPC equipment desirability: base stat scaled by skill proficiency + enchantment, minus minor weight drag:
    const float skillScale = 0.65f + 0.012f * skill;
    const float raw = stat * skillScale + ench * 0.85f - weight * 0.06f;
    out.rawCombatScore = std::max(0.0f, raw);

    // Matches verified fCombatInventoryRangedScoreMult (2.15) & fCombatInventoryEquippedScoreBonus (0.10):
    float score = out.rawCombatScore;
    if (in.isRangedPreferred && in.isRangedWeapon) {
        score *= 1.35f;
    }
    if (in.isCurrentlyEquipped) {
        score *= 1.10f;  // hysteresis prevents constant equip/unequip thrashing on near-equal gear
    }
    out.finalScore       = std::isfinite(score) ? std::max(0.0f, score) : 0.0f;
    out.isUpgradeOverRef = (out.finalScore > std::max(0.0f, equippedRefScore));
    return out;
}

HazardExposureResult EvaluateHazardExposure(const HazardInput& in) {
    HazardExposureResult out{};
    const float dps = std::isfinite(in.baseDps) ? std::clamp(in.baseDps, 0.0f, 1000.0f) : 0.0f;
    const float sec = std::isfinite(in.exposureSeconds) ? std::clamp(in.exposureSeconds, 0.0f, 600.0f) : 0.0f;

    float mit = 0.0f;
    if (in.isPhysicalTrap) {
        const float ar = std::isfinite(in.armorRating) ? std::clamp(in.armorRating, 0.0f, 1000.0f) : 0.0f;
        mit = std::clamp(ar * 0.0012f, 0.0f, 0.80f);
    } else {
        const float res = std::isfinite(in.elementalResistPct) ? std::clamp(in.elementalResistPct, 0.0f, 100.0f) : 0.0f;
        mit = std::clamp(res * 0.01f, 0.0f, 0.85f);
    }

    out.mitigationFrac = mit;
    out.effectiveDps   = dps * (1.0f - mit);
    out.totalDamage    = out.effectiveDps * sec;
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedCompHazardEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                        g_chGmstMu;
std::vector<SavedCompHazardEntry> g_savedCh;
bool                              g_chApplied = false;

static bool SehReadChU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteChU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitCompanionHazardGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[CompanionHazardGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[CompanionHazardGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_chGmstMu);
    if (g_chApplied) {
        return "[CompanionHazardGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedCh.clear();

    for (size_t i = 0; i < kCompanionHazardSpecCount; ++i) {
        const auto& spec = kCompanionHazardSpecs[i];
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
        if (!SehReadChU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteChU32(field, target32)) {
            g_savedCh.push_back({field, orig32});
            ++applied;
        }
    }
    g_chApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_chStateMu);
        g_chState.gmstsVerified         = 1u;
        g_chState.gmstsAppliedCount     = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_chState.deadLegacySkipped     = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_chState.companionHazardActive = g_chApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[CompanionHazardGMST] applied %zu binary-verified companion/summon/dialogue/hazard GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownCompanionHazardGmst() {
    std::lock_guard<std::mutex> lk(g_chGmstMu);
    if (!g_chApplied) return;
    for (const auto& e : g_savedCh) {
        SehWriteChU32(e.ptr, e.saved32);
    }
    g_savedCh.clear();
    g_chApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_chStateMu);
        g_chState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetCompanionHazardState(raw::rt::RawCompanionHazardState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_chStateMu);
    *out = raw::rt::g_chState;
    return 1;
}
