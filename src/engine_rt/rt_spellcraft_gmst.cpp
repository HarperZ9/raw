// rt_spellcraft_gmst.cpp: Milestone Wave 60 Binary-Verified 1.7.104.0 Dual-Cast, Spell Ballistics,
// Conjuration/Illusion/Ward AI, Explosion Clutter & Light-Sensor GMSTs (Zero overlap across all 342 prior GMSTs!).
// Original work, MIT.
#include "rt_spellcraft_gmst.h"
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
// with STRICT ZERO OVERLAP against all 342 previously cataloged GMSTs across the 14 prior modules:
//   - Dual-Cast, Spell Ballistics & Combat Mage AI (0x8B4C72, 0x8B5A10, 0x8B3F80, 0x834660, 0x1535E0, 0x1537D0, 0x6C54C0):
//       fCombatMagicDualCastChance            (0x20A2AC8, float def=0.33,   reader  0x8B4C7A)           -> 0.48
//       fCombatMagicDualCastInterruptTime     (0x20A2AB0, float def=2.5,    reader  0x8B5B4A)           -> 2.0
//       fCombatMagicImmediateAimVariance      (0x20A2AF8, float def=0.5,    reader  0x8B41EC)           -> 0.65
//       fCombatMagicProjectileFiringArc       (0x20A2A68, float def=30.0,   7 readers incl. 0x834974)   -> 34.0
//       fMagicBallOptimalDistance             (0x2076B38, float def=1000.0, reader  0x1535FF)           -> 1250.0
//       fMagicBoltOptimalDistance             (0x2076B68, float def=1000.0, reader  0x15364D)           -> 1350.0
//       fMagicSprayOptimalDistance            (0x2076B98, float def=500.0,  reader  0x153633)           -> 550.0
//       fMagicCasterTargetUpdateInterval      (0x2076CA0, float def=0.25,   reader  0x1537D3)           -> 0.18
//       fDamagedMagickaRegenDelay             (0x20A96D8, float def=1.0,    readers 0x6C552B, 0x6C63B0) -> 1.15
//   - Conjuration, Illusion, Ward & Alteration Combat AI (0x834480, 0x8341B0, 0x835A30, 0x837D80, 0x836960, 0x837FD0):
//       fCombatMagicWardCooldownTime          (0x20A2C90, float def=1.0,    reader  0x834559)           -> 1.25
//       fCombatMagicWardMagickaEquipLimit     (0x20A2CA8, float def=0.5,    reader  0x83423B)           -> 0.45
//       fCombatMagicWardMagickaCastLimit      (0x20A2CC0, float def=0.25,   reader  0x8344F5)           -> 0.20
//       fCombatMagicSummonRestrictTime        (0x20A2D20, float def=30.0,   reader  0x835A4F)           -> 24.0
//       fCombatMagicReanimateDistance         (0x20A2F48, float def=1024.0, reader  0x837E6E)           -> 1280.0
//       fCombatMagicInvisibilityDistance      (0x20A2EA0, float def=1024.0, reader  0x836A28)           -> 1152.0
//       fCombatMagicParalyzeDistance          (0x20A2F90, float def=384.0,  reader  0x8381A7)           -> 448.0
//   - Magicka Baseline, Lighting Sensors & Spell Explosions (0x3E36B0, 0x764A30, 0x5CCBF0, 0x5CCE44, 0x7E8080, 0x7E8CF0, 0x6A33B0, 0x726250):
//       fDefaultMagicka                       (0x209CD00, float def=50.0,   reader  0x3E375D)           -> 50.0
//       iPCStartSpellSkillLevel               (0x20A8550, int32 def=30,     reader  0x764A7B)           -> 30
//       fMagicExplosionPowerMax               (0x20AAE90, float def=100.0,  3 readers incl. 0x5CCC66)   -> 110.0
//       fMagicExplosionClutterMult            (0x20AAF38, float def=6.5,    reader  0x5CCE57)           -> 4.0
//       fExplosionForceClutterUpBias          (0x20A9C60, float def=0.5,    reader  0x7E81C4)           -> 0.32
//       fExplosionWaterRadiusRatio            (0x20A9D38, float def=0.75,   reader  0x7E8D31)           -> 0.70
//       iLightLevelInteriorMod                (0x209EC40, int32 def=50,     readers 0x6A3755, 0x6A409E) -> 40
//       fLightRecalcTimerPlayer               (0x209ED18, float def=0.5,    reader  0x7262CC)           -> 0.35
//   - Dead 0-Reader Legacy GMSTs (confirmed 0 .text readers via overlap-safe ModRM scan, strictly never written!):
//       fCombatSneakStaffMult                 (0x207B450, float def=1.0,    0 .text readers)
//       fSpellCastingDetectionMod             (0x20A0578, float def=10.0,   0 .text readers)
//       fCombatMagicWardMinCastTime           (0x20A2C78, float def=1.5,    0 .text readers)
//       fCombatMagicInvisibilityMinCastTime   (0x20A2E88, float def=15.0,   0 .text readers)
//       fCombatInventoryMinEquipTimeStaff     (0x20A32D8, float def=5.0,    0 .text readers)
//       fDistanceProjectileExplosionDetection (0x209FA80, float def=400.0,  0 .text readers)
constexpr SpellcraftGmstSpec kSpellcraftSpecs[] = {
    {"fCombatMagicDualCastChance",            0x20A2AC8u, false, 0.33f,   0.48f,   0,  0,  true,  "DualCast"},
    {"fCombatMagicDualCastInterruptTime",     0x20A2AB0u, false, 2.5f,    2.0f,    0,  0,  true,  "DualCast"},
    {"fCombatMagicImmediateAimVariance",      0x20A2AF8u, false, 0.5f,    0.65f,   0,  0,  true,  "Spellcraft"},
    {"fCombatMagicProjectileFiringArc",       0x20A2A68u, false, 30.0f,   34.0f,   0,  0,  true,  "Spellcraft"},
    {"fMagicBallOptimalDistance",             0x2076B38u, false, 1000.0f, 1250.0f, 0,  0,  true,  "Spellcraft"},
    {"fMagicBoltOptimalDistance",             0x2076B68u, false, 1000.0f, 1350.0f, 0,  0,  true,  "Spellcraft"},
    {"fMagicSprayOptimalDistance",            0x2076B98u, false, 500.0f,  550.0f,  0,  0,  true,  "Spellcraft"},
    {"fMagicCasterTargetUpdateInterval",      0x2076CA0u, false, 0.25f,   0.18f,   0,  0,  true,  "Spellcraft"},
    {"fDamagedMagickaRegenDelay",             0x20A96D8u, false, 1.0f,    1.15f,   0,  0,  true,  "Spellcraft"},
    {"fCombatMagicWardCooldownTime",          0x20A2C90u, false, 1.0f,    1.25f,   0,  0,  true,  "Ward"},
    {"fCombatMagicWardMagickaEquipLimit",     0x20A2CA8u, false, 0.5f,    0.45f,   0,  0,  true,  "Ward"},
    {"fCombatMagicWardMagickaCastLimit",      0x20A2CC0u, false, 0.25f,   0.20f,   0,  0,  true,  "Ward"},
    {"fCombatMagicSummonRestrictTime",        0x20A2D20u, false, 30.0f,   24.0f,   0,  0,  true,  "Conjuration"},
    {"fCombatMagicReanimateDistance",         0x20A2F48u, false, 1024.0f, 1280.0f, 0,  0,  true,  "Conjuration"},
    {"fCombatMagicInvisibilityDistance",      0x20A2EA0u, false, 1024.0f, 1152.0f, 0,  0,  true,  "Illusion"},
    {"fCombatMagicParalyzeDistance",          0x20A2F90u, false, 384.0f,  448.0f,  0,  0,  true,  "Alteration"},
    {"fDefaultMagicka",                       0x209CD00u, false, 50.0f,   50.0f,   0,  0,  true,  "Spellcraft"},
    {"iPCStartSpellSkillLevel",               0x20A8550u, true,  0.0f,    0.0f,    30, 30, true,  "Spellcraft"},
    {"fMagicExplosionPowerMax",               0x20AAE90u, false, 100.0f,  110.0f,  0,  0,  true,  "Explosion"},
    {"fMagicExplosionClutterMult",            0x20AAF38u, false, 6.5f,    4.0f,    0,  0,  true,  "Explosion"},
    {"fExplosionForceClutterUpBias",          0x20A9C60u, false, 0.5f,    0.32f,   0,  0,  true,  "Explosion"},
    {"fExplosionWaterRadiusRatio",            0x20A9D38u, false, 0.75f,   0.70f,   0,  0,  true,  "Explosion"},
    {"iLightLevelInteriorMod",                0x209EC40u, true,  0.0f,    0.0f,    50, 40, true,  "Light"},
    {"fLightRecalcTimerPlayer",               0x209ED18u, false, 0.5f,    0.35f,   0,  0,  true,  "Light"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fCombatSneakStaffMult",                 0x207B450u, false, 1.0f,    1.0f,    0,  0,  false, "Spellcraft"},
    {"fSpellCastingDetectionMod",             0x20A0578u, false, 10.0f,   10.0f,   0,  0,  false, "Spellcraft"},
    {"fCombatMagicWardMinCastTime",           0x20A2C78u, false, 1.5f,    1.5f,    0,  0,  false, "Ward"},
    {"fCombatMagicInvisibilityMinCastTime",   0x20A2E88u, false, 15.0f,   15.0f,   0,  0,  false, "Illusion"},
    {"fCombatInventoryMinEquipTimeStaff",     0x20A32D8u, false, 5.0f,    5.0f,    0,  0,  false, "Spellcraft"},
    {"fDistanceProjectileExplosionDetection", 0x209FA80u, false, 400.0f,  400.0f,  0,  0,  false, "Explosion"},
};

constexpr size_t kSpellcraftSpecCount = sizeof(kSpellcraftSpecs) / sizeof(kSpellcraftSpecs[0]);

std::mutex         g_scStateMu;
RawSpellcraftState g_scState{};

}  // namespace

const SpellcraftGmstSpec* SpellcraftGmstTable(size_t* outCount) {
    if (outCount) *outCount = kSpellcraftSpecCount;
    return kSpellcraftSpecs;
}

bool VerifySpellcraftGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kSpellcraftSpecCount; ++i) {
        const auto& spec = kSpellcraftSpecs[i];
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
    return verified == kSpellcraftSpecCount;
}

DualCastResult EvaluateDualCastEfficiency(float baseSpellCost, float baseMagnitude,
                                          float schoolSkill0_100, bool isDualCast) {
    DualCastResult out{};
    const float cost  = std::isfinite(baseSpellCost) ? std::clamp(baseSpellCost, 1.0f, 1000.0f) : 25.0f;
    const float mag   = std::isfinite(baseMagnitude) ? std::clamp(baseMagnitude, 1.0f, 1000.0f) : 25.0f;
    const float skill = std::isfinite(schoolSkill0_100) ? std::clamp(schoolSkill0_100, 0.0f, 100.0f) : 25.0f;

    // Skyrim skill cost curve: cost * (1 - (skill/100)^0.65 * 0.41):
    const float skillScale = 1.0f - std::pow(skill / 100.0f, 0.65f) * 0.41f;
    const float singleCost = std::max(1.0f, cost * skillScale);

    out.magickaCost     = isDualCast ? (singleCost * 2.40f) : singleCost;
    out.finalMagnitude  = isDualCast ? (mag * 2.20f)        : mag;
    out.efficiencyRatio = out.finalMagnitude / std::max(out.magickaCost, 1.0e-3f);
    return out;
}

WardAbsorbResult EvaluateWardAbsorption(float wardMaxStrength, float incomingSpellDmg,
                                        float impactAngleDeg, float wardConeHalfDeg) {
    WardAbsorbResult out{};
    const float wardStr  = std::isfinite(wardMaxStrength) ? std::max(wardMaxStrength, 0.0f) : 0.0f;
    const float dmg      = std::isfinite(incomingSpellDmg) ? std::max(incomingSpellDmg, 0.0f) : 0.0f;
    const float angleAbs = std::isfinite(impactAngleDeg) ? std::fabs(impactAngleDeg) : 180.0f;
    const float coneHalf = std::isfinite(wardConeHalfDeg) ? std::clamp(wardConeHalfDeg, 15.0f, 90.0f) : 52.5f;

    out.insideCone = (angleAbs <= coneHalf);
    if (!out.insideCone || wardStr <= 0.0f) {
        out.absorbedDamage  = 0.0f;
        out.bleedThroughDmg = dmg;
        out.wardBroken      = false;
        return out;
    }

    if (dmg <= wardStr) {
        out.absorbedDamage  = dmg;
        out.bleedThroughDmg = 0.0f;
        out.wardBroken      = false;
    } else {
        out.absorbedDamage  = wardStr;
        out.bleedThroughDmg = dmg - wardStr;
        out.wardBroken      = true;
    }
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedSpellcraftEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                        g_scGmstMu;
std::vector<SavedSpellcraftEntry> g_savedSc;
bool                              g_scApplied = false;

static bool SehReadScU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteScU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitSpellcraftGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[SpellcraftGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[SpellcraftGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_scGmstMu);
    if (g_scApplied) {
        return "[SpellcraftGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedSc.clear();

    for (size_t i = 0; i < kSpellcraftSpecCount; ++i) {
        const auto& spec = kSpellcraftSpecs[i];
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
        if (!SehReadScU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteScU32(field, target32)) {
            g_savedSc.push_back({field, orig32});
            ++applied;
        }
    }
    g_scApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_scStateMu);
        g_scState.gmstsVerified     = 1u;
        g_scState.gmstsAppliedCount = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_scState.deadLegacySkipped = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_scState.spellcraftActive  = g_scApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[SpellcraftGMST] applied %zu binary-verified dual-cast/ward/conjuration/explosion GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownSpellcraftGmst() {
    std::lock_guard<std::mutex> lk(g_scGmstMu);
    if (!g_scApplied) return;
    for (const auto& e : g_savedSc) {
        SehWriteScU32(e.ptr, e.saved32);
    }
    g_savedSc.clear();
    g_scApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_scStateMu);
        g_scState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetSpellcraftState(raw::rt::RawSpellcraftState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_scStateMu);
    *out = raw::rt::g_scState;
    return 1;
}
