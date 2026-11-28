#pragma once
// rt_archery_mount_gmst.h: Wave 56 Binary-Verified 1.7.104.0 Bow Draw, Archery Kinematics,
// Mounted Combat & HUD Compass GMSTs (Crossing 252 Total Zero-Overlap Verified GMSTs / 200+ Live-Read!).
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

struct ArcheryMountGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "Archery" | "RangedAI" | "Mount" | "HUD"
};

const ArcheryMountGmstSpec* ArcheryMountGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyArcheryMountGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Bow Draw Kinematics & Weather-Attenuated HUD Compass Visibility Evaluators
// =================================================================================================
struct BowDrawResult {
    float drawFraction01       = 0.0f;  // [0..1] normalized draw completion
    float releasePower         = 0.28f; // [fArrowMinPower..1.0] non-linear kinetic release multiplier
    float staminaDrainPerSec   = 0.0f;  // > 0 during Eagle-Eye zoom or over-hold fatigue
    bool  isOverHeld           = false;
};

BowDrawResult EvaluateBowDrawPower(float drawSeconds, float fullDrawTimeSec = 1.65f, bool isZooming = false);

struct CompassMarkerResult {
    float effectiveMaxDistU = 9600.0f;
    float iconScale         = 1.0f;    // smoothly bounded in [0.55 .. 1.0]
    float opacity01         = 1.0f;    // smoothly bounded in [0.0 .. 1.0]
    bool  isVisible         = true;
};

CompassMarkerResult EvaluateCompassMarkerScale(float distanceU, float maxCompassDistU = 9600.0f,
                                               float fog01 = 0.0f, float blizzard01 = 0.0f);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetArcheryMountState)
// =================================================================================================
struct RawArcheryMountState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t archeryMountActive       = 1u;
    float   arrowMinPower            = 0.28f;
    float   bowZoomStaminaDrain      = 12.5f;
    float   horseMaxUpwardPitchDeg   = 42.0f;
    float   compassLocationMaxDistU  = 9600.0f;
    int32_t arrowInventoryChancePct  = 65;
    int32_t maxAttachedArrows        = 6;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitArcheryMountGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownArcheryMountGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetArcheryMountState(raw::rt::RawArcheryMountState* out);
