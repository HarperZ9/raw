// rt_progression_economy.cpp: Wave 48 Binary-Verified 1.7.104.0 Economy Spread, Crafting Anti-Exploit
// & Movement/Fall/Progression GMSTs.
// Original work, MIT.
#include "rt_progression_economy.h"
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

// Every single GMST RVA, on-disk float default, and .text reader verified directly against SkyrimSE.exe 1.7.104.0:
//   - Economy (0x420BE0..0x420C28):
//       fBarterMin                (0x20A8C40, def=1.40,   reader 0x420BE0) -> 1.35
//       fBarterMax                (0x20A8C58, def=2.40,   readers 0x420BEB, 0x420C07) -> 3.40
//       fBarterBuyMin             (0x20A8C70, def=1.05,   reader 0x420C13) -> 1.05
//       fBarterSellMax            (0x20A8C88, def=0.95,   reader 0x420C28) -> 0.95
//   - Crafting Anti-Exploit (0x421850..0x421B15):
//       fEnchantingSkillFactor    (0x20AAA40, def=3.00,   reader 0x421850) -> 1.20
//       fEnchantingSkillCostScale (0x20AAA88, def=0.50,   reader 0x4218CD) -> 0.52
//       fAlchemySkillFactor       (0x20AAAD0, def=2.00,   reader 0x421928) -> 1.40
//       fSmithingConditionFactor  (0x20AAB48, def=0.5825, reader 0x421A0E) -> 0.50
//       fSmithingArmorMax         (0x20AAB78, def=10.0,   reader 0x421B15) -> 6.50
//       fSmithingWeaponMax        (0x20AAB60, def=10.0,   reader 0x421A89) -> 6.50
//   - Movement & Physical Fall Curve (0x675284, 0x6BD57B..0x6BD58B):
//       fJumpHeightMin            (0x20A8958, def=64.0,   readers 0x675284, 0x7CF220) -> 78.0
//       fJumpFallHeightMin        (0x20A8868, def=600.0,  reader 0x6BD57B) -> 420.0
//       fJumpFallHeightMult       (0x20A8898, def=0.10,   reader 0x6BD583) -> 0.085
//       fJumpFallHeightExponent   (0x20A8880, def=1.65,   reader 0x6BD58B) -> 1.55
//   - Progression (0x78DF01, 0x78DF7F, 0x94AE0D):
//       fSkillUseCurve            (0x20A8CA0, def=1.25,   readers 0x78DF01, 0x78EA00, 0x78EAEB) -> 1.75
//       fXPPerSkillRank           (0x20A8CE8, def=1.00,   reader 0x78DF7F) -> 1.05
//       fLevelUpCarryWeightMod    (0x20A8D00, def=10.0,   reader 0x94AE0D) -> 7.50
//   - Dead Legacy 0-Reader GMSTs (verified on disk, cleanly skipped!):
//       fAlchemyGoldMult          (0x20AAC50, def=1.00,   0 .text readers)
//       fJumpDoubleMult           (0x20A8988, def=0.50,   0 .text readers)
constexpr ProgressionEconomyGmstSpec kProgEconSpecs[] = {
    {"fBarterMin",                0x20A8C40u, 1.40f,   1.35f,  true,  "Economy"},
    {"fBarterMax",                0x20A8C58u, 2.40f,   3.40f,  true,  "Economy"},
    {"fBarterBuyMin",             0x20A8C70u, 1.05f,   1.05f,  true,  "Economy"},
    {"fBarterSellMax",            0x20A8C88u, 0.95f,   0.95f,  true,  "Economy"},
    {"fEnchantingSkillFactor",    0x20AAA40u, 3.00f,   1.20f,  true,  "Crafting"},
    {"fEnchantingSkillCostScale", 0x20AAA88u, 0.50f,   0.52f,  true,  "Crafting"},
    {"fAlchemySkillFactor",       0x20AAAD0u, 2.00f,   1.40f,  true,  "Crafting"},
    {"fSmithingConditionFactor",  0x20AAB48u, 0.5825f, 0.50f,  true,  "Crafting"},
    {"fSmithingArmorMax",         0x20AAB78u, 10.0f,   6.50f,  true,  "Crafting"},
    {"fSmithingWeaponMax",        0x20AAB60u, 10.0f,   6.50f,  true,  "Crafting"},
    {"fJumpHeightMin",            0x20A8958u, 64.0f,   78.0f,  true,  "Movement"},
    {"fJumpFallHeightMin",        0x20A8868u, 600.0f,  420.0f, true,  "Movement"},
    {"fJumpFallHeightMult",       0x20A8898u, 0.10f,   0.085f, true,  "Movement"},
    {"fJumpFallHeightExponent",   0x20A8880u, 1.65f,   1.55f,  true,  "Movement"},
    {"fSkillUseCurve",            0x20A8CA0u, 1.25f,   1.75f,  true,  "Progression"},
    {"fXPPerSkillRank",           0x20A8CE8u, 1.00f,   1.05f,  true,  "Progression"},
    {"fLevelUpCarryWeightMod",    0x20A8D00u, 10.0f,   7.50f,  true,  "Progression"},
    {"fAlchemyGoldMult",          0x20AAC50u, 1.00f,   1.00f,  false, "Crafting"},
    {"fJumpDoubleMult",           0x20A8988u, 0.50f,   0.50f,  false, "Movement"},
};

constexpr size_t kProgEconSpecCount = sizeof(kProgEconSpecs) / sizeof(kProgEconSpecs[0]);

std::mutex                 g_peStateMu;
RawProgressionEconomyState g_peState{};

}  // namespace

const ProgressionEconomyGmstSpec* ProgressionEconomyTable(size_t* outCount) {
    if (outCount) *outCount = kProgEconSpecCount;
    return kProgEconSpecs;
}

bool VerifyProgressionEconomyGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kProgEconSpecCount; ++i) {
        const auto& spec = kProgEconSpecs[i];
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
    return verified == kProgEconSpecCount;
}

CraftingCapResult EvaluateCraftingCaps(float skillLevel0_100, float fortifyBonusFrac,
                                       float skillWeight, float maxCap) {
    CraftingCapResult out{};
    const float skill = std::isfinite(skillLevel0_100) ? std::clamp(skillLevel0_100, 0.0f, 100.0f) : 15.0f;
    const float fort  = std::isfinite(fortifyBonusFrac) ? std::max(fortifyBonusFrac, 0.0f) : 0.0f;
    const float w     = std::isfinite(skillWeight) ? std::clamp(skillWeight, 0.1f, 2.0f) : 0.85f;
    const float cap   = std::isfinite(maxCap) ? std::clamp(maxCap, 1.25f, kCraftingHardAsymptote) : kCraftingHardAsymptote;

    const float span    = cap - 1.0f;
    const float rawGain = w * (0.12f + 0.88f * (skill / 100.0f)) + fort;
    out.rawUnboundedMult = 1.0f + rawGain;

    // Smooth strictly-monotonic hyperbolic-tangent asymptote: NEVER reaches or exceeds maxCap (<= 2.25x),
    // guaranteeing any iterative Fortify-Alchemy <-> Fortify-Enchanting feedback loop converges cleanly!
    const float dampedGain = span * std::tanh(rawGain / std::max(span, 1.0e-4f));
    out.effectiveMult      = 1.0f + dampedGain;
    out.diminishingFrac    = (rawGain > 1.0e-4f) ? std::clamp(1.0f - (dampedGain / rawGain), 0.0f, 1.0f) : 0.0f;
    return out;
}

BarterSpreadResult EvaluateBarterSpread(float speechSkill0_100, float barterMin, float barterMax,
                                        float barterBuyMin, float barterSellMax) {
    BarterSpreadResult out{};
    const float s = std::isfinite(speechSkill0_100) ? std::clamp(speechSkill0_100, 0.0f, 100.0f) / 100.0f : 0.15f;
    const float bMin = std::max(barterMin, 1.05f);
    const float bMax = std::max(barterMax, bMin);
    const float factor = bMax - (bMax - bMin) * s;

    out.buyFactor   = std::max(factor, std::max(barterBuyMin, 1.0f));
    out.sellFactor  = std::min(1.0f / std::max(factor, 1.0f), std::clamp(barterSellMax, 0.25f, 0.99f));
    out.spreadRatio = out.buyFactor / std::max(out.sellFactor, 1.0e-4f);
    return out;
}

float EvaluateFallDamage(float fallHeightU, float minHeightU, float heightMult, float exponent) {
    if (!std::isfinite(fallHeightU) || fallHeightU <= minHeightU) return 0.0f;
    const float excess = fallHeightU - std::max(minHeightU, 0.0f);
    const float scaled = excess * std::max(heightMult, 0.0f);
    if (scaled <= 0.0f) return 0.0f;
    const float dmg = std::pow(scaled, std::clamp(exponent, 0.5f, 3.0f));
    return std::isfinite(dmg) ? dmg : 0.0f;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedProgEconEntry {
    float* ptr = nullptr;
    float  savedValue = 0.0f;
};

std::mutex g_peGmstMu;
std::vector<SavedProgEconEntry> g_savedPe;
bool g_peApplied = false;

static bool SehReadPeFloat(const float* p, float* out) noexcept {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWritePeFloat(float* p, float v) noexcept {
    if (!p || !std::isfinite(v)) return false;
    __try {
        *p = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitProgressionEconomy(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[ProgressionEconomy] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[ProgressionEconomy] skipped (non-CE1 or online profile)";
    }

    const LiveEngineConfig cfg = GetLiveConfig();
    std::lock_guard<std::mutex> lk(g_peGmstMu);
    if (g_peApplied) {
        return "[ProgressionEconomy] already active";
    }

    size_t applied = 0u;
    size_t skippedDead = 0u;
    g_savedPe.clear();

    for (size_t i = 0; i < kProgEconSpecCount; ++i) {
        const auto& spec = kProgEconSpecs[i];
        if (!spec.hasLiveReader) {
            ++skippedDead;
            continue;
        }
        if (std::strcmp(spec.category, "Economy") == 0 && !cfg.livingEconomyEnabled) {
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
        if (!SehReadPeFloat(field, &orig)) continue;
        if (SehWritePeFloat(field, spec.balancedValue)) {
            g_savedPe.push_back({field, orig});
            ++applied;
        }
    }
    g_peApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_peStateMu);
        g_peState.gmstsVerified     = 1u;
        g_peState.gmstsAppliedCount = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_peState.deadLegacySkipped = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_peState.antiExploitActive = 1u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[ProgressionEconomy] applied %zu binary-verified economy/crafting/fall/progression GMSTs (%zu dead 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownProgressionEconomy() {
    std::lock_guard<std::mutex> lk(g_peGmstMu);
    if (!g_peApplied) return;
    for (const auto& e : g_savedPe) {
        SehWritePeFloat(e.ptr, e.savedValue);
    }
    g_savedPe.clear();
    g_peApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_peStateMu);
        g_peState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetProgressionEconomyState(raw::rt::RawProgressionEconomyState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_peStateMu);
    *out = raw::rt::g_peState;
    return 1;
}
