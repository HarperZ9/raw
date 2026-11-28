// rt_ai_packages.cpp: Wave 49 Binary-Verified 1.7.104.0 Flanking, Ranged, Companion & Stealth-Detection AI GMSTs.
// Original work, MIT.
#include "rt_ai_packages.h"
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
//   - Flanking & Melee Spacing (fn:CombatAI::FlankingAngleCalc 0x899E80, 0x41F5C3, 0x6C80EF, 0x855D03):
//       fCombatDistance                           (0x20A71E8, def=128.0,  readers 0x41F5C3, 0x5CF72B) -> 124.0
//       fCombatBashReach                          (0x20A57A8, def=128.0,  readers 0x6C80EF, 0x8B7B62) -> 112.0
//       fCombatFlankingAngleOffset                (0x20A2120, def=22.5,   reader  0x89AC93)           -> 28.0
//       fCombatFlankingAngleOffsetMax             (0x20A2138, def=30.0,   reader  0x89AC9F)           -> 42.0
//       fCombatFlankingMaxTurnAngle               (0x20A2300, def=45.0,   reader  0x89ABDE)           -> 60.0
//       fCombatFlankingStalkTimeMax               (0x20A2360, def=10.0,   readers 0x898718, 0x8F20F9) -> 5.5
//       fCombatFlankingDirectionGoalAngleOffset   (0x20A2198, def=15.0,   reader  0x899FA0)           -> 20.0
//       fCombatFlankingDirectionRotateAngleOffset (0x20A21B0, def=15.0,   reader  0x89A9EE)           -> 22.0
//       fCombatThreatRatioUpdateTime              (0x20A5190, def=5.0,    readers 0x855D03, 0x8560DB) -> 2.5
//   - Ranged Aim & Standoff (fn:CombatAI::RangedAimDispersion 0x55D650, 0x86AEC7, 0x8F2319):
//       fCombatRangedAimVariance                  (0x20A1BF8, def=0.9,    reader  0x55D818)           -> 1.15
//       fCombatRangedMinimumRange                 (0x20A1F58, def=256.0,  reader  0x86AEC7)           -> 340.0
//       fCombatDiveBombChanceMax                  (0x20A27C8, def=1.0,    reader  0x8F2319)           -> 0.85
//   - Search & Stealth Detection Awareness (fn:CombatAI::SearchRadiusBounds 0x81E070, fn:AI::DetectionViewConeEval 0x6A2830):
//       fCombatSearchInteriorRadiusMin            (0x20A3A88, def=768.0,  reader  0x81E638)           -> 1024.0
//       fCombatSearchExteriorRadiusMin            (0x20A3A58, def=1280.0, reader  0x81E64A)           -> 1664.0
//       fCombatSearchSightRadius                  (0x20A3A40, def=1536.0, reader  0x81D5BA)           -> 1920.0
//       fDetectionViewCone                        (0x209EE20, def=190.0,  5 readers incl. 0x6A28CB)   -> 168.0
//       fDetectionSneakLightMod                   (0x209ECB8, def=0.0,    readers 0x41D0E0, 0x41D35F) -> -12.0
//       fDetectionNightEyeBonus                   (0x20A9330, def=3.0,    readers 0x69109A, 0x6A3710) -> 3.5
//   - Companion / Follower Anti-Doorway-Blocking (fn:AI::FollowerDoorSpacing 0x684740, 0x41D7AE):
//       fFollowerSpacingAtDoors                   (0x20A0DD0, def=200.0,  reader  0x684887)           -> 280.0
//       fFollowSpaceBetweenFollowers              (0x20A0DB8, def=100.0,  reader  0x41D7AE)           -> 135.0
//   - Dead Legacy 0-Reader GMSTs (verified on disk, cleanly skipped!):
//       fCombatTeammateFollowRadiusBase           (0x20A4BC0, def=512.0,  0 .text readers)
//       fCombatTeammateFollowRadiusMin            (0x20A4BA8, def=512.0,  0 .text readers)
//       fCombatRangedStandoffTimer                (0x20A6990, def=6.0,    0 .text readers)
//       fCombatSearchInvestigateTime              (0x20A3C20, def=2.5,    0 .text readers)
constexpr AiPackageGmstSpec kAiPackageSpecs[] = {
    {"fCombatDistance",                           0x20A71E8u, 128.0f,  124.0f,  true,  "Flanking"},
    {"fCombatBashReach",                          0x20A57A8u, 128.0f,  112.0f,  true,  "Flanking"},
    {"fCombatFlankingAngleOffset",                0x20A2120u, 22.5f,   28.0f,   true,  "Flanking"},
    {"fCombatFlankingAngleOffsetMax",             0x20A2138u, 30.0f,   42.0f,   true,  "Flanking"},
    {"fCombatFlankingMaxTurnAngle",               0x20A2300u, 45.0f,   60.0f,   true,  "Flanking"},
    {"fCombatFlankingStalkTimeMax",               0x20A2360u, 10.0f,   5.5f,    true,  "Flanking"},
    {"fCombatFlankingDirectionGoalAngleOffset",   0x20A2198u, 15.0f,   20.0f,   true,  "Flanking"},
    {"fCombatFlankingDirectionRotateAngleOffset", 0x20A21B0u, 15.0f,   22.0f,   true,  "Flanking"},
    {"fCombatThreatRatioUpdateTime",              0x20A5190u, 5.0f,    2.5f,    true,  "Flanking"},
    {"fCombatRangedAimVariance",                  0x20A1BF8u, 0.9f,    1.15f,   true,  "Ranged"},
    {"fCombatRangedMinimumRange",                 0x20A1F58u, 256.0f,  340.0f,  true,  "Ranged"},
    {"fCombatDiveBombChanceMax",                  0x20A27C8u, 1.0f,    0.85f,   true,  "Ranged"},
    {"fCombatSearchInteriorRadiusMin",            0x20A3A88u, 768.0f,  1024.0f, true,  "Search"},
    {"fCombatSearchExteriorRadiusMin",            0x20A3A58u, 1280.0f, 1664.0f, true,  "Search"},
    {"fCombatSearchSightRadius",                  0x20A3A40u, 1536.0f, 1920.0f, true,  "Search"},
    {"fDetectionViewCone",                        0x209EE20u, 190.0f,  168.0f,  true,  "Detection"},
    {"fDetectionSneakLightMod",                   0x209ECB8u, 0.0f,    -12.0f,  true,  "Detection"},
    {"fDetectionNightEyeBonus",                   0x20A9330u, 3.0f,    3.5f,    true,  "Detection"},
    {"fFollowerSpacingAtDoors",                   0x20A0DD0u, 200.0f,  280.0f,  true,  "Companion"},
    {"fFollowSpaceBetweenFollowers",              0x20A0DB8u, 100.0f,  135.0f,  true,  "Companion"},
    // Dead 0-reader legacy GMSTs (verified in .data, strictly never written):
    {"fCombatTeammateFollowRadiusBase",           0x20A4BC0u, 512.0f,  512.0f,  false, "Companion"},
    {"fCombatTeammateFollowRadiusMin",            0x20A4BA8u, 512.0f,  512.0f,  false, "Companion"},
    {"fCombatRangedStandoffTimer",                0x20A6990u, 6.0f,    6.0f,    false, "Ranged"},
    {"fCombatSearchInvestigateTime",              0x20A3C20u, 2.5f,    2.5f,    false, "Search"},
};

constexpr size_t kAiPackageSpecCount = sizeof(kAiPackageSpecs) / sizeof(kAiPackageSpecs[0]);

std::mutex         g_aiStateMu;
RawAiPackagesState g_aiState{};

float SanitizeUnit(float v) {
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

}  // namespace

const AiPackageGmstSpec* AiPackagesTable(size_t* outCount) {
    if (outCount) *outCount = kAiPackageSpecCount;
    return kAiPackageSpecs;
}

bool VerifyAiPackagesGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kAiPackageSpecCount; ++i) {
        const auto& spec = kAiPackageSpecs[i];
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
    return verified == kAiPackageSpecCount;
}

TacticalAiProfile EvaluateTacticalAiProfile(const TacticalAiEnv& env) {
    TacticalAiProfile out{};

    const float rain  = env.inInterior ? 0.0f : SanitizeUnit(env.rain01);
    const float snow  = env.inInterior ? 0.0f : SanitizeUnit(env.snow01);
    const float fog   = env.inInterior ? (SanitizeUnit(env.fog01) * 0.25f) : SanitizeUnit(env.fog01);
    const float dark  = SanitizeUnit(env.nightDarkness01);
    const float wind  = env.inInterior ? 0.0f : (std::isfinite(env.windBeaufort) ? std::clamp(env.windBeaufort, 0.0f, 12.0f) : 2.0f);
    const float wind01 = wind / 12.0f;

    // Composite atmospheric + optical attenuation (strictly monotonic: worsening weather/night reduces visibility):
    const float obscuration = std::clamp(0.42f * snow + 0.30f * fog + 0.22f * rain + 0.34f * dark, 0.0f, 0.75f);
    const float vis = std::clamp(1.0f - obscuration, 0.25f, 1.0f);
    out.visibilityFactor01 = vis;

    // Human peripheral cone narrows in low-light/blizzard conditions (168 deg bright noon -> ~112 deg midnight blizzard):
    out.effectiveViewConeDeg = std::clamp(104.0f + 64.0f * vis, 104.0f, 168.0f);

    // Search sight radius scales smoothly with optical visibility (interior cells use tighter base radius):
    const float baseSight = env.inInterior ? 1152.0f : 1920.0f;
    out.effectiveSightRadiusU = std::clamp(baseSight * (0.40f + 0.60f * vis), 480.0f, 1920.0f);

    // Ranged aim dispersion widens under crosswind, precipitation, fog, and darkness (no laser-aimbots in a blizzard!):
    const float aimPenalty = 0.45f * wind01 + 0.35f * snow + 0.22f * rain + 0.20f * fog + 0.22f * dark;
    out.rangedAimDispersionMult = std::clamp(1.0f + aimPenalty, 1.0f, 2.25f);

    // Flanking aggression tightens slightly in open visibility and stays cautious in blinding storms / narrow interiors:
    const float flankBase = env.inInterior ? 0.92f : 1.08f;
    out.flankingAggressionMult = std::clamp(flankBase * (0.85f + 0.22f * vis), 0.80f, 1.35f);

    // Search persistence rises in tight interiors and drops slightly during severe whiteouts where tracks vanish:
    const float persistBase = env.inInterior ? 1.18f : 1.0f;
    out.searchPersistenceMult = std::clamp(persistBase * (0.82f + 0.22f * vis), 0.75f, 1.40f);

    return out;
}

void AiPackagesUpdateEnvironment(const TacticalAiEnv& env) {
    const TacticalAiProfile prof = EvaluateTacticalAiProfile(env);
    std::lock_guard<std::mutex> lk(g_aiStateMu);
    g_aiState.effectiveViewConeDeg    = prof.effectiveViewConeDeg;
    g_aiState.effectiveSightRadiusU   = prof.effectiveSightRadiusU;
    g_aiState.rangedAimDispersionMult = prof.rangedAimDispersionMult;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedAiGmstEntry {
    float* ptr        = nullptr;
    float  savedValue = 0.0f;
};

std::mutex                    g_aiGmstMu;
std::vector<SavedAiGmstEntry> g_savedAi;
bool                          g_aiApplied = false;

static bool SehReadAiFloat(const float* p, float* out) noexcept {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteAiFloat(float* p, float v) noexcept {
    if (!p || !std::isfinite(v)) return false;
    __try {
        *p = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitAiPackages(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[AiPackages] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[AiPackages] skipped (non-CE1 or online profile)";
    }

    const LiveEngineConfig cfg = GetLiveConfig();
    if (!cfg.tacticalSquadAi) {
        std::lock_guard<std::mutex> slk(g_aiStateMu);
        g_aiState.tacticalAiActive = 0u;
        return "[AiPackages] disabled via raw_engine.ini tacticalSquadAi=0";
    }

    std::lock_guard<std::mutex> lk(g_aiGmstMu);
    if (g_aiApplied) {
        return "[AiPackages] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedAi.clear();

    for (size_t i = 0; i < kAiPackageSpecCount; ++i) {
        const auto& spec = kAiPackageSpecs[i];
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
        if (!SehReadAiFloat(field, &orig)) continue;
        if (SehWriteAiFloat(field, spec.balancedValue)) {
            g_savedAi.push_back({field, orig});
            ++applied;
        }
    }
    g_aiApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_aiStateMu);
        g_aiState.gmstsVerified     = 1u;
        g_aiState.gmstsAppliedCount = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_aiState.deadLegacySkipped = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_aiState.tacticalAiActive  = g_aiApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[AiPackages] applied %zu binary-verified flanking/ranged/companion/detection AI GMSTs (%zu dead 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownAiPackages() {
    std::lock_guard<std::mutex> lk(g_aiGmstMu);
    if (!g_aiApplied) return;
    for (const auto& e : g_savedAi) {
        SehWriteAiFloat(e.ptr, e.savedValue);
    }
    g_savedAi.clear();
    g_aiApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_aiStateMu);
        g_aiState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetAiPackagesState(raw::rt::RawAiPackagesState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_aiStateMu);
    *out = raw::rt::g_aiState;
    return 1;
}
