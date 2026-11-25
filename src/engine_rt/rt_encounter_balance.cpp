// rt_encounter_balance.cpp: Wave 46 Binary-Verified 2027 Archery Ballistics, De-Exploited Armor Curve
// & Combat Magicka/Stamina Resource Balance verified directly against SkyrimSE.exe 1.7.104.0 GMSTs.
// Original work, MIT.
#include "rt_encounter_balance.h"
#include "rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_callgraph.h"
#include "rt_live_config.h"
#include "rt_version.h"
#include <windows.h>
#endif

namespace raw::rt {
namespace {

// Every single RVA, default float, and live .text reader verified directly against SkyrimSE.exe 1.7.104.0:
//   - Archery (0x41FAA0..0x41FC3B, 0x76202E, 0x7F7423):
//       fArrowGravityBase   (0x20A7380, def=0.30,   1 reader @ 0x41FAA0) -> 0.24
//       fArrowGravityMult   (0x20A7398, def=0.002,  1 reader @ 0x41FAA8) -> 0.0016 (fixes old 0.85 anti-gravity bug!)
//       fArrowWeakGravity   (0x20A7470, def=1.75,   2 readers @ 0x41FAC4, 0x7DDF4E) -> 1.45
//       fArrowBowMinTime    (0x20A73C8, def=0.80,   2 readers @ 0x41FAF0, 0x76203D) -> 0.45
//       fBowDrawTime        (0x20A7410, def=1.6667, 2 readers @ 0x41FB20, 0x76202E) -> 1.45
//       fArrowBowFastMult   (0x20A73E0, def=1.00,   2 readers @ 0x41FBD9, 0x41FC2B) -> 1.18
//       fArrowMinBowVelocity(0x20A7458, def=0.70,   1 reader @ 0x41FC33) -> 0.82
//       fArrowWobbleAmplitude(0x207A900,def=0.10,   2 readers @ 0x7F7423, 0x7F757B) -> 0.00
//       fArrowSpeedMult     (0x20A7308, def=1500.0, 0 readers - legacy Oblivion GMST, verified only, never written!)
//   - Armor Mitigation (0x6C9190, 0x7F0A9C, 0x7F0B0D, 0x41F46B..0x41F487):
//       fArmorScalingFactor (0x20A7D70, def=0.15,   2 readers @ 0x6C9190, 0x7F0A9C) -> 0.11
//       fMaxArmorRating     (0x20A7D40, def=95.0,   1 reader @ 0x7F0B0D) -> 75.0
//       fArmorRatingPCBase  (0x20A7DD0, def=1.00,   2 readers @ 0x41F473, 0x41F487) -> 1.00
//       fArmorRatingPCMax   (0x20A7DB8, def=2.50,   1 reader @ 0x41F46B) -> 2.15
//   - Combat Resource Economy (0x6C49D9, 0x41F7F0..0x41F81B):
//       fCombatMagickaRegenRateMult (0x20A1328, def=0.75, 1 reader @ 0x6C49D9) -> 0.52
//       fPowerAttackStaminaPenalty  (0x20A7F08, def=5.00, 1 reader @ 0x41F81B) -> 1.85
//       fStaminaAttackWeaponBase    (0x20A7ED8, def=7.00, 1 reader @ 0x41F813) -> 14.0
//       fStaminaAttackWeaponMult    (0x20A7EF0, def=0.10, 1 reader @ 0x41F7F0) -> 0.42
//       fBlockAmountHandToHandMult  (0x20A71D0, def=0.25, 0 readers - legacy GMST, verified only, never written!)
constexpr EncounterBalanceGmstSpec kBalanceSpecs[] = {
    {"fArrowGravityBase",           0x20A7380u, 0.30f,    0.24f,   true,  "Archery"},
    {"fArrowGravityMult",           0x20A7398u, 0.002f,   0.0016f, true,  "Archery"},
    {"fArrowWeakGravity",           0x20A7470u, 1.75f,    1.45f,   true,  "Archery"},
    {"fArrowBowMinTime",            0x20A73C8u, 0.80f,    0.45f,   true,  "Archery"},
    {"fBowDrawTime",                0x20A7410u, 1.6667f,  1.45f,   true,  "Archery"},
    {"fArrowBowFastMult",           0x20A73E0u, 1.00f,    1.18f,   true,  "Archery"},
    {"fArrowMinBowVelocity",        0x20A7458u, 0.70f,    0.82f,   true,  "Archery"},
    {"fArrowWobbleAmplitude",       0x207A900u, 0.10f,    0.00f,   true,  "Archery"},
    {"fArrowSpeedMult",             0x20A7308u, 1500.0f,  1500.0f, false, "Archery"},
    {"fArmorScalingFactor",         0x20A7D70u, 0.15f,    0.11f,   true,  "Armor"},
    {"fMaxArmorRating",             0x20A7D40u, 95.0f,    75.0f,   true,  "Armor"},
    {"fArmorRatingPCBase",          0x20A7DD0u, 1.00f,    1.00f,   true,  "Armor"},
    {"fArmorRatingPCMax",           0x20A7DB8u, 2.50f,    2.15f,   true,  "Armor"},
    {"fCombatMagickaRegenRateMult", 0x20A1328u, 0.75f,    0.52f,   true,  "Resources"},
    {"fPowerAttackStaminaPenalty",  0x20A7F08u, 5.00f,    1.85f,   true,  "Resources"},
    {"fStaminaAttackWeaponBase",    0x20A7ED8u, 7.00f,    14.0f,   true,  "Resources"},
    {"fStaminaAttackWeaponMult",    0x20A7EF0u, 0.10f,    0.42f,   true,  "Resources"},
    {"fBlockAmountHandToHandMult",  0x20A71D0u, 0.25f,    0.25f,   false, "Resources"},
};

constexpr size_t kBalanceSpecCount = sizeof(kBalanceSpecs) / sizeof(kBalanceSpecs[0]);

}  // namespace

const EncounterBalanceGmstSpec* EncounterBalanceTable(size_t* outCount) {
    if (outCount) *outCount = kBalanceSpecCount;
    return kBalanceSpecs;
}

ArrowBallisticsResult EvaluateArrowBallistics(float drawTimeSec, float bowSpeed, float projBaseSpeed,
                                              float arrowBowMinTime, float bowDrawTime,
                                              float arrowGravityBase, float arrowGravityMult,
                                              float arrowWeakGravity, float arrowFastMult) {
    ArrowBallisticsResult out{};
    const float spd = std::max(bowSpeed, 0.1f);
    const float minT = std::max(arrowBowMinTime, 0.05f);
    const float spanT = std::max(bowDrawTime - minT, 0.1f) / spd;
    constexpr float kMinPower = 0.35f;

    if (drawTimeSec <= minT) {
        out.drawPower01 = kMinPower;
    } else {
        const float frac = std::clamp((drawTimeSec - minT) / spanT, 0.0f, 1.0f);
        out.drawPower01 = kMinPower + (1.0f - kMinPower) * frac;
    }

    out.velocityMult = std::clamp(out.drawPower01 * std::max(arrowFastMult, 0.5f), 0.25f, 2.0f);
    const float effProjSpd = std::max(projBaseSpeed, 0.0f) * out.velocityMult;
    // Exact SkyrimSE 1.7.104.0 formula at 0x41FAA0..0x41FAC8:
    //   grav = max(0.05, (fArrowGravityBase - fArrowGravityMult * speed)) + (1 - drawPower) * fArrowWeakGravity
    const float baseDrop = arrowGravityBase - arrowGravityMult * effProjSpd;
    out.gravityFactor = std::max(0.05f, baseDrop) + (1.0f - out.drawPower01) * std::max(arrowWeakGravity, 0.0f);
    return out;
}

float EvaluateArmorMitigationPct(float effectiveArmor, float scalingFactor, float maxArmorRating) {
    const float ar  = std::max(effectiveArmor, 0.0f);
    const float sc  = std::clamp(scalingFactor, 0.01f, 0.50f);
    const float cap = std::clamp(maxArmorRating, 10.0f, 95.0f);
    return std::clamp(ar * sc, 0.0f, cap);
}

float EvaluatePowerAttackStaminaCost(float weaponWeight, float baseCost, float weightMult, float penaltyMult) {
    // Exact SkyrimSE 1.7.104.0 formula at 0x41F7F0..0x41F823:
    //   cost = (weaponWeight * fStaminaAttackWeaponMult + fStaminaAttackWeaponBase) * fPowerAttackStaminaPenalty
    const float w = std::max(weaponWeight, 0.0f);
    return std::max(0.0f, (w * std::max(weightMult, 0.0f) + std::max(baseCost, 0.0f)) * std::max(penaltyMult, 0.0f));
}

bool VerifyEncounterBalanceGmstTable(const Image& img, uint32_t* outVerifiedCount) {
    if (outVerifiedCount) *outVerifiedCount = 0u;
    if (!img.base || img.sizeOfImage < 0x20B0000u) return false;

    // Extract PE ImageBase so on-disk mapped buffers AND live loaded images both translate Setting+0x10 name pointers!
    uint64_t imageBase = reinterpret_cast<uint64_t>(img.base);
    if (img.base[0] == 'M' && img.base[1] == 'Z') {
        uint32_t lfanew = 0u;
        std::memcpy(&lfanew, img.base + 0x3Cu, 4u);
        if (lfanew + 0x40u < img.sizeOfImage) {
            uint64_t optBase = 0ull;
            std::memcpy(&optBase, img.base + lfanew + 0x30u, 8u);
            if (optBase >= 0x100000000ull) {
                imageBase = optBase;
            }
        }
    }

    uint32_t verified = 0u;
    for (size_t i = 0; i < kBalanceSpecCount; ++i) {
        const auto& spec = kBalanceSpecs[i];
        if (spec.expectedRva + 24u > img.sizeOfImage) return false;

        const uint8_t* obj = img.base + spec.expectedRva;
        float val = 0.0f;
        uint64_t nameVa = 0ull;
        std::memcpy(&val, obj + 8u, 4u);
        std::memcpy(&nameVa, obj + 16u, 8u);

        // Check name pointer against either PE preferred ImageBase (on-disk file mapping) or live runtime base:
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

        // Verify exact on-disk default float value (tolerance handles IEEE float32 representation):
        const float tol = std::max(1.0e-4f, std::fabs(spec.exeDefault) * 1.0e-4f);
        if (!std::isfinite(val) || std::fabs(val - spec.exeDefault) > tol) return false;

        ++verified;
    }

    if (outVerifiedCount) *outVerifiedCount = verified;
    return verified == kBalanceSpecCount;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedBalanceEntry {
    float* ptr = nullptr;
    float  savedValue = 0.0f;
};

std::mutex g_balMu;
std::vector<SavedBalanceEntry> g_savedBalance;
bool g_balanceApplied = false;

static bool SehReadFloatSafe(const float* p, float* out) noexcept {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteFloatSafe(float* p, float v) noexcept {
    if (!p || !std::isfinite(v)) return false;
    __try {
        *p = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string ApplyEncounterBalanceGmsts(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[EncounterBalance] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[EncounterBalance] skipped (non-CE1 or online profile)";
    }
    if (!GetLiveConfig().nonSpongyDifficulty) {
        return "[EncounterBalance] skipped (NonSpongyDifficulty=0 in raw_engine.ini)";
    }

    std::lock_guard<std::mutex> lk(g_balMu);
    if (g_balanceApplied) {
        return "[EncounterBalance] already active";
    }

    size_t applied = 0u;
    size_t skippedDead = 0u;
    g_savedBalance.clear();

    for (size_t i = 0; i < kBalanceSpecCount; ++i) {
        const auto& spec = kBalanceSpecs[i];
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
        // Strict RVA cross-check on 1.7.104.0 when table provides verified address:
        if (TableVersion() == "1.7.104.0" && rva != spec.expectedRva) continue;

        auto* field = reinterpret_cast<float*>(const_cast<uint8_t*>(img.base) + rva + 8u);
        float orig = 0.0f;
        if (!SehReadFloatSafe(field, &orig)) continue;
        if (SehWriteFloatSafe(field, spec.balancedValue)) {
            g_savedBalance.push_back({field, orig});
            ++applied;
        }
    }
    g_balanceApplied = (applied > 0u);

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[EncounterBalance] applied %zu binary-verified archery/armor/resource GMSTs (%zu legacy 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void RestoreEncounterBalanceGmsts() {
    std::lock_guard<std::mutex> lk(g_balMu);
    if (!g_balanceApplied) return;
    for (const auto& e : g_savedBalance) {
        SehWriteFloatSafe(e.ptr, e.savedValue);
    }
    g_savedBalance.clear();
    g_balanceApplied = false;
}
#endif

}  // namespace raw::rt
