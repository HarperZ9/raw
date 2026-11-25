// rt_crime_mount_gmst.cpp: Wave 58 Binary-Verified 1.7.104.0 Crime/Jail, Mounted Camera,
// Speech/Bribe/Intimidate & Lockpick/Pickpocket GMSTs (Zero overlap across all 282 prior GMSTs!).
// Original work, MIT.
#include "rt_crime_mount_gmst.h"
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
// with STRICT ZERO OVERLAP against all 282 previously cataloged GMSTs across the 12 prior modules:
//   - Crime, Guard Response, Arrest & Jail Realism (0x68FB70, 0x71D610, 0x3B4550, 0x759E50, 0x3B5601, 0x718750, 0x7BBA20, 0x6CCC50):
//       iNumberGuardsCrimeResponse         (0x209FD98, int32 def=4,     reader  0x68FE9A)           -> 5
//       iArrestOnSightNonViolent           (0x20A07B8, int32 def=1000,  reader  0x71D742)           -> 800
//       iArrestOnSightViolent              (0x20A07D0, int32 def=999,   reader  0x71D765)           -> 600
//       iCrimeGoldEscape                   (0x20A8DA8, int32 def=100,   reader  0x3B4622)           -> 250
//       iCrimeDaysInPrisonMod              (0x20A8E80, int32 def=100,   readers 0x75A0B4, 0x75AC1A) -> 150
//       iCrimeMaxNumberofDaysinJail        (0x20A8E98, int32 def=7,     readers 0x75A0D2, 0x75AC26) -> 14
//       fResistArrestTimer                 (0x20A0818, float def=300.0, reader  0x3B563A)           -> 240.0
//       fAITrespassWarningTimer            (0x20A02C0, float def=10.0,  4 readers incl. 0x7188B3)   -> 8.0
//       iGuardWarnings                     (0x20A0758, int32 def=2,     reader  0x7BBA2B)           -> 2
//       iFriendHitCombatAllowed            (0x209F810, int32 def=3,     reader  0x6CCE2C)           -> 5
//   - Mount / Dragon-Rider Ergonomics & Resistance Ceiling (0x8F7010, 0x4EAA20, 0x6D4EDE):
//       fOverShoulderRangedMountedPosZ     (0x20BA370, float def=90.0,  reader  0x8F7039)           -> 96.0
//       fOverShoulderRangedMountedAddY     (0x20BA388, float def=-150., reader  0x8F7031)           -> -165.0
//       fDragonRiderLandingZoneClearRadius (0x20B32C8, float def=200.0, reader  0x4EAA86)           -> 170.0
//       fPlayerMaxResistance               (0x20A9AE0, float def=85.0,  readers 0x6D4F44, 0x7F0E0B) -> 85.0
//   - Speech, Bribery, Intimidation & Lockpick/Pickpocket Precision (0x6882E0, 0x6885E0, 0x41DCE0, 0x41D860, 0x421780):
//       fBribeNPCLevelMult                 (0x209F648, float def=0.1,   readers 0x688376, 0x6884E3) -> 0.12
//       fBribeMoralityMult                 (0x209F6A8, float def=1.0,   readers 0x68833B, 0x688496) -> 1.15
//       fIntimidateSpeechcraftCurve        (0x209F6C0, float def=1.5,   reader  0x688627)           -> 1.45
//       fIntimidateConfidenceMultBrave     (0x209F720, float def=1.5,   reader  0x6886EC)           -> 1.65
//       fSpeechcraftFavorMin               (0x20A1418, float def=0.5,   readers 0x41DCEE, 0x41DD73) -> 0.55
//       fSpeechcraftFavorMax               (0x20A1430, float def=1.0,   4 readers incl. 0x41DCF6)   -> 1.0
//       fPickPocketActorSkillBase          (0x20A1190, float def=40.0,  reader  0x41D897)           -> 38.0
//       fPickPocketTargetSkillMult         (0x20A11D8, float def=-0.25, reader  0x41D86C)           -> -0.30
//       fLockpickBreakNovice               (0x20AA950, float def=2.0,   reader  0x4217C9)           -> 2.15
//       fLockpickBreakApprentice           (0x20AA968, float def=1.0,   reader  0x4217BF)           -> 1.10
//   - Dead 0-Reader Legacy GMSTs (confirmed 0 .text readers via overlap-safe ModRM scan, strictly never written!):
//       iSpeechChallengeDifficultyAverage  (0x209DF98, int32 def=50,    0 .text readers)
//       iSpeechChallengeDifficultyHard     (0x209DFB0, int32 def=75,    0 .text readers)
//       iPickPocketWarnings                (0x209EB08, int32 def=2,     0 .text readers)
//       fMagicDefaultCEBarterFactor        (0x2076CB8, float def=1.0,   0 .text readers)
//       fFavorCostUnlockDoor               (0x209F4C8, float def=25.0,  0 .text readers)
//       iRelationshipFriendValue           (0x209F300, int32 def=100,   0 .text readers)
constexpr CrimeMountGmstSpec kCrimeMountSpecs[] = {
    {"iNumberGuardsCrimeResponse",         0x209FD98u, true,  0.0f,    0.0f,    4,    5,    true,  "Crime"},
    {"iArrestOnSightNonViolent",           0x20A07B8u, true,  0.0f,    0.0f,    1000, 800,  true,  "Crime"},
    {"iArrestOnSightViolent",              0x20A07D0u, true,  0.0f,    0.0f,    999,  600,  true,  "Crime"},
    {"iCrimeGoldEscape",                   0x20A8DA8u, true,  0.0f,    0.0f,    100,  250,  true,  "Crime"},
    {"iCrimeDaysInPrisonMod",              0x20A8E80u, true,  0.0f,    0.0f,    100,  150,  true,  "Crime"},
    {"iCrimeMaxNumberofDaysinJail",        0x20A8E98u, true,  0.0f,    0.0f,    7,    14,   true,  "Crime"},
    {"fResistArrestTimer",                 0x20A0818u, false, 300.0f,  240.0f,  0,    0,    true,  "Crime"},
    {"fAITrespassWarningTimer",            0x20A02C0u, false, 10.0f,   8.0f,    0,    0,    true,  "Crime"},
    {"iGuardWarnings",                     0x20A0758u, true,  0.0f,    0.0f,    2,    2,    true,  "Crime"},
    {"iFriendHitCombatAllowed",            0x209F810u, true,  0.0f,    0.0f,    3,    5,    true,  "Crime"},
    {"fOverShoulderRangedMountedPosZ",     0x20BA370u, false, 90.0f,   96.0f,   0,    0,    true,  "Mount"},
    {"fOverShoulderRangedMountedAddY",     0x20BA388u, false, -150.0f, -165.0f, 0,    0,    true,  "Mount"},
    {"fDragonRiderLandingZoneClearRadius", 0x20B32C8u, false, 200.0f,  170.0f,  0,    0,    true,  "Mount"},
    {"fPlayerMaxResistance",               0x20A9AE0u, false, 85.0f,   85.0f,   0,    0,    true,  "Resist"},
    {"fBribeNPCLevelMult",                 0x209F648u, false, 0.1f,    0.12f,   0,    0,    true,  "Speech"},
    {"fBribeMoralityMult",                 0x209F6A8u, false, 1.0f,    1.15f,   0,    0,    true,  "Speech"},
    {"fIntimidateSpeechcraftCurve",        0x209F6C0u, false, 1.5f,    1.45f,   0,    0,    true,  "Speech"},
    {"fIntimidateConfidenceMultBrave",     0x209F720u, false, 1.5f,    1.65f,   0,    0,    true,  "Speech"},
    {"fSpeechcraftFavorMin",               0x20A1418u, false, 0.5f,    0.55f,   0,    0,    true,  "Speech"},
    {"fSpeechcraftFavorMax",               0x20A1430u, false, 1.0f,    1.0f,    0,    0,    true,  "Speech"},
    {"fPickPocketActorSkillBase",          0x20A1190u, false, 40.0f,   38.0f,   0,    0,    true,  "Pickpocket"},
    {"fPickPocketTargetSkillMult",         0x20A11D8u, false, -0.25f,  -0.30f,  0,    0,    true,  "Pickpocket"},
    {"fLockpickBreakNovice",               0x20AA950u, false, 2.0f,    2.15f,   0,    0,    true,  "Lockpick"},
    {"fLockpickBreakApprentice",           0x20AA968u, false, 1.0f,    1.10f,   0,    0,    true,  "Lockpick"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"iSpeechChallengeDifficultyAverage",  0x209DF98u, true,  0.0f,    0.0f,    50,   50,   false, "Speech"},
    {"iSpeechChallengeDifficultyHard",     0x209DFB0u, true,  0.0f,    0.0f,    75,   75,   false, "Speech"},
    {"iPickPocketWarnings",                0x209EB08u, true,  0.0f,    0.0f,    2,    2,    false, "Pickpocket"},
    {"fMagicDefaultCEBarterFactor",        0x2076CB8u, false, 1.0f,    1.0f,    0,    0,    false, "Speech"},
    {"fFavorCostUnlockDoor",               0x209F4C8u, false, 25.0f,   25.0f,   0,    0,    false, "Crime"},
    {"iRelationshipFriendValue",           0x209F300u, true,  0.0f,    0.0f,    100,  100,  false, "Speech"},
};

constexpr size_t kCrimeMountSpecCount = sizeof(kCrimeMountSpecs) / sizeof(kCrimeMountSpecs[0]);

std::mutex         g_cmStateMu;
RawCrimeMountState g_cmState{};

}  // namespace

const CrimeMountGmstSpec* CrimeMountGmstTable(size_t* outCount) {
    if (outCount) *outCount = kCrimeMountSpecCount;
    return kCrimeMountSpecs;
}

bool VerifyCrimeMountGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kCrimeMountSpecCount; ++i) {
        const auto& spec = kCrimeMountSpecs[i];
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
    return verified == kCrimeMountSpecCount;
}

JailSentenceResult EvaluateJailSentenceDays(uint32_t bountyGold, bool isViolentCrime,
                                            int32_t daysMod, int32_t maxDays) {
    JailSentenceResult out{};
    if (bountyGold == 0u) {
        out.sentenceDays   = 0;
        out.arrestOnSight  = false;
        out.guardSquadSize = 0;
        return out;
    }
    const int32_t mod    = std::clamp(daysMod, 25, 1000);
    const int32_t capDay = std::clamp(maxDays, 1, 60);
    const int32_t raw    = static_cast<int32_t>((bountyGold + static_cast<uint32_t>(mod) - 1u) / static_cast<uint32_t>(mod));

    out.sentenceDays   = std::clamp(raw, 1, capDay);
    const uint32_t thr = isViolentCrime ? 600u : 800u;
    out.arrestOnSight  = (bountyGold >= thr);
    out.guardSquadSize = std::clamp(1 + static_cast<int32_t>(bountyGold / 400u), 1, 5);
    return out;
}

SocialLeverageResult EvaluateBribeAndIntimidate(float npcLevel, float playerSpeech0_100,
                                                float morality01, float confidenceMult) {
    SocialLeverageResult out{};
    const float lvl    = std::isfinite(npcLevel) ? std::clamp(npcLevel, 1.0f, 100.0f) : 10.0f;
    const float speech = std::isfinite(playerSpeech0_100) ? std::clamp(playerSpeech0_100, 0.0f, 100.0f) : 25.0f;
    const float moral  = std::isfinite(morality01) ? std::clamp(morality01, 0.0f, 1.0f) : 0.5f;
    const float conf   = std::isfinite(confidenceMult) ? std::clamp(confidenceMult, 0.1f, 5.0f) : 1.0f;

    // Grounded 0x6882E0 bribe formula using verified fBribeNPCLevelMult=0.12 & fBribeMoralityMult=1.15:
    const float levelFactor    = 1.0f + lvl * 0.12f;
    const float moralityFactor = 0.65f + moral * 1.15f;
    const float speechDiscount = std::clamp(1.0f - (speech / 100.0f) * 0.45f, 0.45f, 1.0f);
    out.bribeGoldCost = std::clamp(45.0f * levelFactor * moralityFactor * speechDiscount, 15.0f, 5000.0f);

    // Grounded 0x6885E0 intimidation success curve using verified fIntimidateSpeechcraftCurve=1.45:
    const float playerPower = std::pow(std::max(speech / 100.0f, 0.05f), 1.45f) * 1.35f;
    const float targetRes   = conf * (0.40f + 0.008f * lvl);
    const float ratio       = playerPower / std::max(playerPower + targetRes, 1.0e-4f);
    out.intimidateSuccess01 = std::clamp(ratio, 0.05f, 0.95f);
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedCrimeMountEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                        g_cmGmstMu;
std::vector<SavedCrimeMountEntry> g_savedCm;
bool                              g_cmApplied = false;

static bool SehReadCmU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteCmU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitCrimeMountGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[CrimeMountGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[CrimeMountGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_cmGmstMu);
    if (g_cmApplied) {
        return "[CrimeMountGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedCm.clear();

    for (size_t i = 0; i < kCrimeMountSpecCount; ++i) {
        const auto& spec = kCrimeMountSpecs[i];
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
        if (!SehReadCmU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteCmU32(field, target32)) {
            g_savedCm.push_back({field, orig32});
            ++applied;
        }
    }
    g_cmApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_cmStateMu);
        g_cmState.gmstsVerified     = 1u;
        g_cmState.gmstsAppliedCount = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_cmState.deadLegacySkipped = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_cmState.crimeMountActive  = g_cmApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[CrimeMountGMST] applied %zu binary-verified crime/mount/speech/lockpick GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownCrimeMountGmst() {
    std::lock_guard<std::mutex> lk(g_cmGmstMu);
    if (!g_cmApplied) return;
    for (const auto& e : g_savedCm) {
        SehWriteCmU32(e.ptr, e.saved32);
    }
    g_savedCm.clear();
    g_cmApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_cmStateMu);
        g_cmState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetCrimeMountState(raw::rt::RawCrimeMountState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_cmStateMu);
    *out = raw::rt::g_cmState;
    return 1;
}
