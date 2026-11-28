#pragma once
// rt_crime_mount_gmst.h: Wave 58 Binary-Verified 1.7.104.0 Crime/Jail, Mounted Camera,
// Speech/Bribe/Intimidate & Lockpick/Pickpocket GMSTs (Crossing 312 Total Zero-Overlap Verified GMSTs!).
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

struct CrimeMountGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "Crime" | "Mount" | "Resist" | "Speech" | "Pickpocket" | "Lockpick"
};

const CrimeMountGmstSpec* CrimeMountGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyCrimeMountGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Jail Sentence & Bribe/Intimidation Leverage Evaluators
// =================================================================================================
struct JailSentenceResult {
    int32_t sentenceDays      = 0;     // bounded in [0 .. maxDays]
    bool    arrestOnSight     = false; // true when bounty crosses violent/non-violent arrest threshold
    int32_t guardSquadSize    = 1;     // responsive guard count [1 .. 5]
};

JailSentenceResult EvaluateJailSentenceDays(uint32_t bountyGold, bool isViolentCrime,
                                            int32_t daysMod = 150, int32_t maxDays = 14);

struct SocialLeverageResult {
    float bribeGoldCost        = 50.0f;  // rises with NPC level & morality, falls with player Speechcraft
    float intimidateSuccess01  = 0.50f;  // bounded [0.05 .. 0.95], rises with Speechcraft, falls with target confidence
};

SocialLeverageResult EvaluateBribeAndIntimidate(float npcLevel, float playerSpeech0_100,
                                                float morality01, float confidenceMult);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetCrimeMountState)
// =================================================================================================
struct RawCrimeMountState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t crimeMountActive         = 1u;
    int32_t guardResponseCount       = 5;
    int32_t arrestViolentThreshold   = 600;
    int32_t maxJailDays              = 14;
    float   playerMaxResistancePct   = 85.0f;
    float   bribeNpcLevelMult        = 0.12f;
    float   dragonRiderClearRadiusU  = 170.0f;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitCrimeMountGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownCrimeMountGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetCrimeMountState(raw::rt::RawCrimeMountState* out);
