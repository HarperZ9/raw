// rt_shouts_survival_gmst.cpp: Wave 57 Binary-Verified 1.7.104.0 Shouts, Disease/Bleedout,
// Survival Warmth/Hydrodynamics & Leveling/XP GMSTs (Zero overlap across all 252 prior GMSTs!).
// Original work, MIT.
#include "rt_shouts_survival_gmst.h"
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
// with STRICT ZERO OVERLAP against all 252 previously cataloged GMSTs across the 11 prior modules:
//   - Shouts & Thu'um Voice Cadence (0x6D8F45, 0x41DC30, 0x3B2510, 0x483B70, 0x8D0B00):
//       fShoutTime1                   (0x20AB910, float def=0.2,   readers 0x6D8F58, 0x6D932E) -> 0.16
//       fShoutTime2                   (0x20AB928, float def=0.9,   readers 0x6D8FCE, 0x6D9325) -> 0.72
//       fShoutTimeout                 (0x20AB940, float def=8.0,   reader  0x6D91B3)           -> 6.5
//       fVoiceRateBase                (0x20A12B0, float def=5.0,   reader  0x41DC33)           -> 5.5
//       iVoicePointsDefault           (0x20A1298, int32 def=50,    4 readers incl. 0x3B25F7)   -> 50
//       fAIShoutToleranceDegrees      (0x20A0680, float def=5.0,   3 readers incl. 0x484169)   -> 6.5
//       fCombatShoutReleaseTime       (0x20A3038, float def=0.5,   readers 0x8D0BBE, 0x8D1C09) -> 0.42
//   - Disease, Bleedout & Ward/Rune Rules (0x422080, 0x3B2510, 0x41DCB0, 0x6D7B90, 0x422220):
//       fMagicDiseaseTransferMult     (0x20AB4A8, float def=1.0,   reader  0x422098)           -> 1.15
//       fBleedoutDefault              (0x20A13A0, float def=0.2,   4 readers incl. 0x3B25EB)   -> 0.18
//       fBleedoutRate                 (0x20A13B8, float def=15.0,  4 readers incl. 0x41DCCC)   -> 12.0
//       fWardAngleForExplosions       (0x20ABAD8, float def=90.0,  readers 0x6D7CAB, 0x6D8464) -> 105.0
//       iMaxPlayerRunes               (0x20ABBF8, int32 def=6,     reader  0x422225)           -> 6
//   - Survival Warmth, Cold-Water & NPC Fall Realism (0x41DDA0, 0x67A7E0, 0x526950, 0x526820, 0x6BD530):
//       fSurvArmorScalar              (0x20A1568, float def=1.0,   readers 0x41DE58, 0x41E7C4) -> 1.12
//       fSurvTorchBonus               (0x20A1580, float def=50.0,  readers 0x41DDBF, 0x41E730) -> 60.0
//       fActorLeaveWaterBreathTimer   (0x20A8C28, float def=4.0,   reader  0x67AF32)           -> 3.2
//       fSubmergedLinearDampingH      (0x20A9408, float def=2.0,   reader  0x526DDC)           -> 2.25
//       fSubmergedMaxWaterDistance    (0x20A93F0, float def=48.0,  reader  0x52690E)           -> 48.0
//       fJumpFallHeightMinNPC         (0x20A88B0, float def=300.0, reader  0x6BD598)           -> 340.0
//   - Leveling, XP Curve & Skill Progression (0x78DC60, 0x3C66F0, 0x421550, 0x421830):
//       fXPLevelUpBase                (0x20A8CB8, float def=170.0, 3 readers incl. 0x78DC91)   -> 180.0
//       fXPLevelUpMult                (0x20A8CD0, float def=30.0,  3 readers incl. 0x78DC89)   -> 32.0
//       iAVDhmsLevelUp                (0x209CA98, int32 def=10,    readers 0x3C6B6D, 0x94ADE8) -> 10
//       iXPBase                       (0x20AA548, int32 def=200,   reader  0x421564)           -> 200
//       iXPBumpBase                   (0x20AA560, int32 def=100,   reader  0x42156D)           -> 100
//       fSkillUsageRechargeMult       (0x20AAA28, float def=1.0,   reader  0x421836)           -> 1.20
//   - Dead 0-Reader Legacy GMSTs (confirmed 0 .text readers via overlap-safe ModRM scan, strictly never written!):
//       fCombatShoutLongRecoveryTime  (0x20A3068, float def=60.0,  0 .text readers)
//       fBleedoutRecover              (0x20A1388, float def=0.0,   0 .text readers)
//       iCrimeGoldWerewolf            (0x20A8DC0, int32 def=100,   0 .text readers)
//       fSurvNormalBodyBonus          (0x20A1448, float def=27.0,  0 .text readers)
//       fDiffMultXPH                  (0x20AA6B0, float def=0.0,   0 .text readers)
//       iXPRewardKillOpponent         (0x20A9F90, int32 def=20,    0 .text readers)
constexpr ShoutsSurvivalGmstSpec kShoutsSurvivalSpecs[] = {
    {"fShoutTime1",                  0x20AB910u, false, 0.2f,   0.16f,  0,   0,   true,  "Shout"},
    {"fShoutTime2",                  0x20AB928u, false, 0.9f,   0.72f,  0,   0,   true,  "Shout"},
    {"fShoutTimeout",                0x20AB940u, false, 8.0f,   6.5f,   0,   0,   true,  "Shout"},
    {"fVoiceRateBase",               0x20A12B0u, false, 5.0f,   5.5f,   0,   0,   true,  "Shout"},
    {"iVoicePointsDefault",          0x20A1298u, true,  0.0f,   0.0f,   50,  50,  true,  "Shout"},
    {"fAIShoutToleranceDegrees",     0x20A0680u, false, 5.0f,   6.5f,   0,   0,   true,  "Shout"},
    {"fCombatShoutReleaseTime",      0x20A3038u, false, 0.5f,   0.42f,  0,   0,   true,  "Shout"},
    {"fMagicDiseaseTransferMult",    0x20AB4A8u, false, 1.0f,   1.15f,  0,   0,   true,  "Disease"},
    {"fBleedoutDefault",             0x20A13A0u, false, 0.2f,   0.18f,  0,   0,   true,  "Bleedout"},
    {"fBleedoutRate",                0x20A13B8u, false, 15.0f,  12.0f,  0,   0,   true,  "Bleedout"},
    {"fWardAngleForExplosions",      0x20ABAD8u, false, 90.0f,  105.0f, 0,   0,   true,  "Ward"},
    {"iMaxPlayerRunes",              0x20ABBF8u, true,  0.0f,   0.0f,   6,   6,   true,  "Ward"},
    {"fSurvArmorScalar",             0x20A1568u, false, 1.0f,   1.12f,  0,   0,   true,  "Survival"},
    {"fSurvTorchBonus",              0x20A1580u, false, 50.0f,  60.0f,  0,   0,   true,  "Survival"},
    {"fActorLeaveWaterBreathTimer",  0x20A8C28u, false, 4.0f,   3.2f,   0,   0,   true,  "Survival"},
    {"fSubmergedLinearDampingH",     0x20A9408u, false, 2.0f,   2.25f,  0,   0,   true,  "Survival"},
    {"fSubmergedMaxWaterDistance",   0x20A93F0u, false, 48.0f,  48.0f,  0,   0,   true,  "Survival"},
    {"fJumpFallHeightMinNPC",        0x20A88B0u, false, 300.0f, 340.0f, 0,   0,   true,  "Survival"},
    {"fXPLevelUpBase",               0x20A8CB8u, false, 170.0f, 180.0f, 0,   0,   true,  "Leveling"},
    {"fXPLevelUpMult",               0x20A8CD0u, false, 30.0f,  32.0f,  0,   0,   true,  "Leveling"},
    {"iAVDhmsLevelUp",               0x209CA98u, true,  0.0f,   0.0f,   10,  10,  true,  "Leveling"},
    {"iXPBase",                      0x20AA548u, true,  0.0f,   0.0f,   200, 200, true,  "Leveling"},
    {"iXPBumpBase",                  0x20AA560u, true,  0.0f,   0.0f,   100, 100, true,  "Leveling"},
    {"fSkillUsageRechargeMult",      0x20AAA28u, false, 1.0f,   1.20f,  0,   0,   true,  "Leveling"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fCombatShoutLongRecoveryTime", 0x20A3068u, false, 60.0f,  60.0f,  0,   0,   false, "Shout"},
    {"fBleedoutRecover",             0x20A1388u, false, 0.0f,   0.0f,   0,   0,   false, "Bleedout"},
    {"iCrimeGoldWerewolf",           0x20A8DC0u, true,  0.0f,   0.0f,   100, 100, false, "Disease"},
    {"fSurvNormalBodyBonus",         0x20A1448u, false, 27.0f,  27.0f,  0,   0,   false, "Survival"},
    {"fDiffMultXPH",                 0x20AA6B0u, false, 0.0f,   0.0f,   0,   0,   false, "Leveling"},
    {"iXPRewardKillOpponent",        0x20A9F90u, true,  0.0f,   0.0f,   20,  20,  false, "Leveling"},
};

constexpr size_t kShoutsSurvivalSpecCount = sizeof(kShoutsSurvivalSpecs) / sizeof(kShoutsSurvivalSpecs[0]);

std::mutex             g_ssvStateMu;
RawShoutsSurvivalState g_ssvState{};

}  // namespace

const ShoutsSurvivalGmstSpec* ShoutsSurvivalGmstTable(size_t* outCount) {
    if (outCount) *outCount = kShoutsSurvivalSpecCount;
    return kShoutsSurvivalSpecs;
}

bool VerifyShoutsSurvivalGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kShoutsSurvivalSpecCount; ++i) {
        const auto& spec = kShoutsSurvivalSpecs[i];
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
    return verified == kShoutsSurvivalSpecCount;
}

ShoutCooldownResult EvaluateShoutCooldown(uint32_t wordCount1To3, float baseWordCooldownSec,
                                          float cooldownReductionFrac) {
    ShoutCooldownResult out{};
    const uint32_t words = std::clamp(wordCount1To3, 1u, 3u);
    const float baseCd   = std::isfinite(baseWordCooldownSec) ? std::clamp(baseWordCooldownSec, 3.0f, 120.0f) : 15.0f;
    const float red      = std::isfinite(cooldownReductionFrac) ? std::clamp(cooldownReductionFrac, 0.0f, 0.75f) : 0.0f;

    out.holdTimeSec      = (words == 1u) ? 0.16f : ((words == 2u) ? 0.72f : 1.35f);
    const float wordMult = (words == 1u) ? 1.0f  : ((words == 2u) ? 1.55f : 2.35f);
    out.baseCooldownSec  = baseCd * wordMult;
    out.finalCooldownSec = std::clamp(out.baseCooldownSec * (1.0f - red), 3.0f, 180.0f);
    return out;
}

SurvivalWarmthResult EvaluateSurvivalWarmth(float armorWarmthBase, bool hasTorch, bool isWet, float airTempC) {
    SurvivalWarmthResult out{};
    const float baseArmor = std::isfinite(armorWarmthBase) ? std::clamp(armorWarmthBase, 0.0f, 220.0f) : 60.0f;
    const float tempC     = std::isfinite(airTempC) ? std::clamp(airTempC, -45.0f, 35.0f) : 0.0f;

    // Matches verified fSurvArmorScalar=1.12 and fSurvTorchBonus=60.0:
    float warmth = baseArmor * 1.12f + (hasTorch ? 60.0f : 0.0f);
    if (isWet) {
        warmth *= 0.55f;
    }
    out.totalWarmthRating = std::clamp(warmth, 0.0f, 300.0f);

    const float coldSeverity = std::clamp((10.0f - tempC) / 40.0f, 0.0f, 1.25f);
    const float insulRed     = std::clamp(out.totalWarmthRating / 320.0f, 0.0f, 0.78f);
    const float wetPenalty   = isWet ? 1.35f : 1.0f;
    out.coldDrainMult = std::clamp((0.35f + coldSeverity) * (1.0f - insulRed) * wetPenalty, 0.22f, 1.65f);
    return out;
}

float EvaluateLevelUpXpRequired(uint32_t currentLevel, float xpBase, float xpMult) {
    const uint32_t lvl = std::clamp(currentLevel, 1u, 250u);
    const float b = std::isfinite(xpBase) ? std::clamp(xpBase, 50.0f, 1000.0f) : 180.0f;
    const float m = std::isfinite(xpMult) ? std::clamp(xpMult, 5.0f, 200.0f) : 32.0f;
    return b + static_cast<float>(lvl - 1u) * m;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedShoutSurvEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                       g_ssvGmstMu;
std::vector<SavedShoutSurvEntry> g_savedSsv;
bool                             g_ssvApplied = false;

static bool SehReadSsvU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteSsvU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitShoutsSurvivalGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[ShoutsSurvivalGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[ShoutsSurvivalGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_ssvGmstMu);
    if (g_ssvApplied) {
        return "[ShoutsSurvivalGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedSsv.clear();

    for (size_t i = 0; i < kShoutsSurvivalSpecCount; ++i) {
        const auto& spec = kShoutsSurvivalSpecs[i];
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
        if (!SehReadSsvU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteSsvU32(field, target32)) {
            g_savedSsv.push_back({field, orig32});
            ++applied;
        }
    }
    g_ssvApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_ssvStateMu);
        g_ssvState.gmstsVerified        = 1u;
        g_ssvState.gmstsAppliedCount    = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_ssvState.deadLegacySkipped    = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_ssvState.shoutsSurvivalActive = g_ssvApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[ShoutsSurvivalGMST] applied %zu binary-verified shout/disease/survival/leveling GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownShoutsSurvivalGmst() {
    std::lock_guard<std::mutex> lk(g_ssvGmstMu);
    if (!g_ssvApplied) return;
    for (const auto& e : g_savedSsv) {
        SehWriteSsvU32(e.ptr, e.saved32);
    }
    g_savedSsv.clear();
    g_ssvApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_ssvStateMu);
        g_ssvState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetShoutsSurvivalState(raw::rt::RawShoutsSurvivalState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_ssvStateMu);
    *out = raw::rt::g_ssvState;
    return 1;
}
