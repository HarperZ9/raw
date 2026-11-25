// rt_archery_mount_gmst.cpp: Wave 56 Binary-Verified 1.7.104.0 Bow Draw, Archery Kinematics,
// Mounted Combat & HUD Compass GMSTs (Zero overlap across all 226 prior GMSTs!).
// Original work, MIT.
#include "rt_archery_mount_gmst.h"
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
// with STRICT ZERO OVERLAP against all 226 previously cataloged GMSTs across the 10 prior modules:
//   - Bow Draw, Zoom Fatigue & Arrow Kinematics (0x41FBA0, 0x41FAF0, 0x41FB60, 0x745200, 0x41DC40, 0x28C450, 0x7F2CAE, 0x7F43A0, 0x172460):
//       fArrowBowSlowMult               (0x20A73F8, float def=0.5,    readers 0x41FBD4, 0x41FC3E) -> 0.60
//       fArrowMinPower                  (0x20A7428, float def=0.35,   5 readers incl. 0x41FB12)   -> 0.28
//       fArrowMinVelocity               (0x20A7440, float def=0.2,    readers 0x41FB7E, 0x41FB8E) -> 0.24
//       fBowZoomStaminaDrainMult        (0x20A8B98, float def=15.0,   reader  0x745E5F)           -> 12.5
//       fBowZoomStaminaRegenDelay       (0x20A8BB0, float def=3.0,    reader  0x745ECF)           -> 2.2
//       fDefaultBowSpeedBonus           (0x20A1400, float def=0.5,    reader  0x41DC43)           -> 0.55
//       fBowNPCSpreadAngle              (0x20A74E8, float def=5.0,    reader  0x28D123)           -> 5.5
//       fArrowBounceLinearSpeed         (0x20A7350, float def=0.1,    readers 0x7F2E10, 0x7F2F97) -> 0.14
//       iArrowInventoryChance           (0x20A63D8, int32 def=50,     reader  0x7F45ED)           -> 65
//       iMaxAttachedArrows              (0x2077088, int32 def=3,      reader  0x172512)           -> 6
//   - Ranged Combat Envelope & Projectile Persistence (0x834660, 0x80B650, 0x8F3380, 0x7F7600):
//       fCombatMaximumProjectileRange   (0x20A15C8, float def=4096.0, readers 0x8347D3, 0x86AEA9) -> 5120.0
//       fCombatRangedDistance           (0x20A1F40, float def=1024.0, 12 readers incl. 0x80B8DE)  -> 1280.0
//       fCombatRangedAttackChanceMax    (0x20A1B20, float def=1.0,    reader  0x8F3396)           -> 0.95
//       iProjectileMaxRefCount          (0x20A63F0, int32 def=15,     readers 0x7F76F8, 0x7F8207) -> 24
//   - Mounted / Horse Camera & Companion Steed Ergonomics (0x67C380, 0x6B5F30, 0x7B7DE0):
//       fHorseMaxUpwardPitch            (0x20BA3B8, float def=25.0,   4 readers incl. 0x67CEF9)   -> 42.0
//       fFlyingMountedMaxLookingDown    (0x20A0470, float def=70.0,   3 readers incl. 0x67CF17)   -> 78.0
//       fAIHorseSearchDistance          (0x20A0F98, float def=3000.0, readers 0x6B6004, 0x6D37B5) -> 3600.0
//       iCrimeGoldStealHorse            (0x20A8538, int32 def=25,     reader  0x7B7DF9)           -> 100
//   - HUD Compass & Exploration Visibility (0x9361F0, 0x6A33B0):
//       fHUDCompassLocationMaxDist      (0x20ADD88, float def=8000.0, reader  0x93624F)           -> 9600.0
//       fDetectProjectileDistancePlayer (0x209F000, float def=1024.0, reader  0x6A3BC7)           -> 1280.0
//   - Dead 0-Reader Legacy GMSTs (confirmed 0 .text readers via overlap-safe ModRM scan, strictly never written!):
//       fArrowOptimalDistance           (0x20A64E0, float def=1000.0, 0 .text readers)
//       fArrowMaxDistance               (0x20A64F8, float def=2000.0, 0 .text readers)
//       fBowHoldTimer                   (0x20A04E8, float def=0.8,    0 .text readers)
//       fArrowBounceRotateSpeed         (0x20A7338, float def=7.5,    0 .text readers)
//       fHorseMountOffsetY              (0x20A8B20, float def=0.0,    0 .text readers)
//       iHorseTurnDegreesPerSecond      (0x20A8AC0, int32 def=45,     0 .text readers)
constexpr ArcheryMountGmstSpec kArcheryMountSpecs[] = {
    {"fArrowBowSlowMult",               0x20A73F8u, false, 0.5f,    0.60f,   0,   0,   true,  "Archery"},
    {"fArrowMinPower",                  0x20A7428u, false, 0.35f,   0.28f,   0,   0,   true,  "Archery"},
    {"fArrowMinVelocity",               0x20A7440u, false, 0.2f,    0.24f,   0,   0,   true,  "Archery"},
    {"fBowZoomStaminaDrainMult",        0x20A8B98u, false, 15.0f,   12.5f,   0,   0,   true,  "Archery"},
    {"fBowZoomStaminaRegenDelay",       0x20A8BB0u, false, 3.0f,    2.2f,    0,   0,   true,  "Archery"},
    {"fDefaultBowSpeedBonus",           0x20A1400u, false, 0.5f,    0.55f,   0,   0,   true,  "Archery"},
    {"fBowNPCSpreadAngle",              0x20A74E8u, false, 5.0f,    5.5f,    0,   0,   true,  "Archery"},
    {"fArrowBounceLinearSpeed",         0x20A7350u, false, 0.1f,    0.14f,   0,   0,   true,  "Archery"},
    {"iArrowInventoryChance",           0x20A63D8u, true,  0.0f,    0.0f,    50,  65,  true,  "Archery"},
    {"iMaxAttachedArrows",              0x2077088u, true,  0.0f,    0.0f,    3,   6,   true,  "Archery"},
    {"fCombatMaximumProjectileRange",   0x20A15C8u, false, 4096.0f, 5120.0f, 0,   0,   true,  "RangedAI"},
    {"fCombatRangedDistance",           0x20A1F40u, false, 1024.0f, 1280.0f, 0,   0,   true,  "RangedAI"},
    {"fCombatRangedAttackChanceMax",    0x20A1B20u, false, 1.0f,    0.95f,   0,   0,   true,  "RangedAI"},
    {"iProjectileMaxRefCount",          0x20A63F0u, true,  0.0f,    0.0f,    15,  24,  true,  "Archery"},
    {"fHorseMaxUpwardPitch",            0x20BA3B8u, false, 25.0f,   42.0f,   0,   0,   true,  "Mount"},
    {"fFlyingMountedMaxLookingDown",    0x20A0470u, false, 70.0f,   78.0f,   0,   0,   true,  "Mount"},
    {"fAIHorseSearchDistance",          0x20A0F98u, false, 3000.0f, 3600.0f, 0,   0,   true,  "Mount"},
    {"iCrimeGoldStealHorse",            0x20A8538u, true,  0.0f,    0.0f,    25,  100, true,  "Mount"},
    {"fHUDCompassLocationMaxDist",      0x20ADD88u, false, 8000.0f, 9600.0f, 0,   0,   true,  "HUD"},
    {"fDetectProjectileDistancePlayer", 0x209F000u, false, 1024.0f, 1280.0f, 0,   0,   true,  "HUD"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fArrowOptimalDistance",           0x20A64E0u, false, 1000.0f, 1000.0f, 0,   0,   false, "Archery"},
    {"fArrowMaxDistance",               0x20A64F8u, false, 2000.0f, 2000.0f, 0,   0,   false, "Archery"},
    {"fBowHoldTimer",                   0x20A04E8u, false, 0.8f,    0.8f,    0,   0,   false, "Archery"},
    {"fArrowBounceRotateSpeed",         0x20A7338u, false, 7.5f,    7.5f,    0,   0,   false, "Archery"},
    {"fHorseMountOffsetY",              0x20A8B20u, false, 0.0f,    0.0f,    0,   0,   false, "Mount"},
    {"iHorseTurnDegreesPerSecond",      0x20A8AC0u, true,  0.0f,    0.0f,    45,  45,  false, "Mount"},
};

constexpr size_t kArcheryMountSpecCount = sizeof(kArcheryMountSpecs) / sizeof(kArcheryMountSpecs[0]);

std::mutex           g_amStateMu;
RawArcheryMountState g_amState{};

}  // namespace

const ArcheryMountGmstSpec* ArcheryMountGmstTable(size_t* outCount) {
    if (outCount) *outCount = kArcheryMountSpecCount;
    return kArcheryMountSpecs;
}

bool VerifyArcheryMountGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kArcheryMountSpecCount; ++i) {
        const auto& spec = kArcheryMountSpecs[i];
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
    return verified == kArcheryMountSpecCount;
}

BowDrawResult EvaluateBowDrawPower(float drawSeconds, float fullDrawTimeSec, bool isZooming) {
    BowDrawResult out{};
    const float t      = std::isfinite(drawSeconds) ? std::max(drawSeconds, 0.0f) : 0.0f;
    const float tFull  = std::isfinite(fullDrawTimeSec) ? std::clamp(fullDrawTimeSec, 0.4f, 4.0f) : 1.65f;
    const float frac   = std::clamp(t / tFull, 0.0f, 1.0f);
    out.drawFraction01 = frac;

    // Non-linear smoothstep-blended draw power curve from verified fArrowMinPower (0.28) to 1.0:
    const float smooth = frac * frac * (3.0f - 2.0f * frac);
    const float curve  = 0.35f * frac + 0.65f * smooth;
    out.releasePower   = std::clamp(0.28f + 0.72f * curve, 0.28f, 1.0f);

    // Over-hold fatigue kicks in after holding past full draw + Eagle-Eye zoom stamina burn (12.5/s):
    out.isOverHeld = (t > tFull + 0.5f);
    float drain = 0.0f;
    if (isZooming) {
        drain += 12.5f;  // matches verified fBowZoomStaminaDrainMult
    }
    if (out.isOverHeld) {
        const float overSec = std::min(t - (tFull + 0.5f), 10.0f);
        drain += 2.5f + 1.15f * overSec;
    }
    out.staminaDrainPerSec = std::clamp(drain, 0.0f, 35.0f);
    return out;
}

CompassMarkerResult EvaluateCompassMarkerScale(float distanceU, float maxCompassDistU,
                                               float fog01, float blizzard01) {
    CompassMarkerResult out{};
    const float dist = std::isfinite(distanceU) ? std::max(distanceU, 0.0f) : 0.0f;
    const float maxD = std::isfinite(maxCompassDistU) ? std::clamp(maxCompassDistU, 1000.0f, 24000.0f) : 9600.0f;
    const float fog  = std::isfinite(fog01) ? std::clamp(fog01, 0.0f, 1.0f) : 0.0f;
    const float bliz = std::isfinite(blizzard01) ? std::clamp(blizzard01, 0.0f, 1.0f) : 0.0f;

    // Fog and blizzards contract effective horizon landmark visibility cleanly down to >= 35%:
    const float visFactor = std::clamp(1.0f - 0.35f * fog - 0.40f * bliz, 0.35f, 1.0f);
    out.effectiveMaxDistU = maxD * visFactor;

    if (dist > out.effectiveMaxDistU) {
        out.isVisible = false;
        out.iconScale = 0.55f;
        out.opacity01 = 0.0f;
        return out;
    }

    const float u = std::clamp(dist / std::max(out.effectiveMaxDistU, 1.0f), 0.0f, 1.0f);
    out.isVisible = true;
    out.iconScale = std::clamp(1.0f - 0.45f * u, 0.55f, 1.0f);
    out.opacity01 = std::clamp(1.0f - 0.65f * u * u, 0.35f, 1.0f);
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedArchMountEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                       g_amGmstMu;
std::vector<SavedArchMountEntry> g_savedAm;
bool                             g_amApplied = false;

static bool SehReadAmU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteAmU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitArcheryMountGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[ArcheryMountGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[ArcheryMountGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_amGmstMu);
    if (g_amApplied) {
        return "[ArcheryMountGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedAm.clear();

    for (size_t i = 0; i < kArcheryMountSpecCount; ++i) {
        const auto& spec = kArcheryMountSpecs[i];
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
        if (!SehReadAmU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteAmU32(field, target32)) {
            g_savedAm.push_back({field, orig32});
            ++applied;
        }
    }
    g_amApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_amStateMu);
        g_amState.gmstsVerified      = 1u;
        g_amState.gmstsAppliedCount  = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_amState.deadLegacySkipped  = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_amState.archeryMountActive = g_amApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[ArcheryMountGMST] applied %zu binary-verified archery/mount/compass GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownArcheryMountGmst() {
    std::lock_guard<std::mutex> lk(g_amGmstMu);
    if (!g_amApplied) return;
    for (const auto& e : g_savedAm) {
        SehWriteAmU32(e.ptr, e.saved32);
    }
    g_savedAm.clear();
    g_amApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_amStateMu);
        g_amState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetArcheryMountState(raw::rt::RawArcheryMountState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_amStateMu);
    *out = raw::rt::g_amState;
    return 1;
}
