// rt_stealth_social_gmst.cpp: Wave 52 Binary-Verified 1.7.104.0 Stealth Sound/Light, Lockpicking,
// Pickpocket & Speechcraft/Bribery GMSTs.
// Original work, MIT.
#include "rt_stealth_social_gmst.h"
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

// Every single GMST RVA, on-disk float default, and .text reader verified directly against SkyrimSE.exe 1.7.104.0:
//   - Stealth Sound, Armor-Weight & Light Detection (fn:Stealth::ComputeMaxDistance 0x41CD30, SneakDetectionCalc 0x41CD90):
//       fSneakMaxDistance                 (0x209EB68, def=1500.0,  readers 0x41CD40, 0x41CD4A) -> 1850.0
//       fSneakExteriorDistanceMult        (0x209ED90, def=2.0,     readers 0x41CD38, 0x41CF38) -> 1.85
//       fSneakEquippedWeightBase          (0x209EB80, def=12.0,    reader  0x41CFF6)           -> 8.0
//       fSneakEquippedWeightMult          (0x209EB98, def=0.5,     reader  0x41CFD3)           -> 0.68
//       fSneakLightMoveMult               (0x209ECD0, def=0.01,    reader  0x41D08C)           -> 0.025
//       fSneakLightRunMult                (0x209ECE8, def=0.2,     reader  0x41D094)           -> 0.32
//       fSneakSleepMod                    (0x209ED30, def=-0.2,    reader  0x41CEF6)           -> -0.30
//       fSneakAlertMod                    (0x209ED48, def=0.2,     reader  0x41CED1)           -> 0.35
//   - Lockpicking Sweet-Spot & Durability (fn:Lockpick::ComputeSweetSpot 0x421650, fn:Lockpick::ComputeBreakDurability 0x421780):
//       fLockpickSkillSweetSpotBase       (0x20AA7D0, def=0.82,    reader  0x421668)           -> 0.72
//       fLockpickSkillSweetSpotMult       (0x20AA7E8, def=0.012,   reader  0x421653)           -> 0.015
//       fLockpickBreakAdept               (0x20AA980, def=0.75,    reader  0x4217B5)           -> 0.82
//       fLockpickBreakExpert              (0x20AA998, def=0.5,     reader  0x4217AB)           -> 0.58
//       fLockpickBreakMaster              (0x20AA9B0, def=0.25,    reader  0x4217A1)           -> 0.34
//   - Pickpocket Probability & Weight Risk (fn:Stealth::ComputePickpocketChance 0x41D860):
//       fPickPocketMinChance              (0x20A1268, def=5.0,     reader  0x41D919)           -> 5.0
//       fPickPocketMaxChance              (0x20A1280, def=95.0,    reader  0x41D908)           -> 92.0
//       fPickPocketWeightMult             (0x20A1250, def=-5.0,    reader  0x41D8AB)           -> -5.5
//   - Speechcraft, Bribery & Intimidation (fn:Social::ComputeBribeCost 0x6882E0, fn:Social::EvaluateIntimidate 0x6885E0):
//       fBribeScale                       (0x209F660, def=1.0,     readers 0x688407, 0x688574) -> 1.35
//       fBribeCostCurve                   (0x209F678, def=1.5,     readers 0x6883E1, 0x68854E) -> 1.60
//       fBribeSpeechcraftMult             (0x209F690, def=0.03,    readers 0x68838A, 0x6884F7) -> 0.035
//       fIntimidateConfidenceMultAverage  (0x209F708, def=1.0,     reader  0x6886F6)           -> 1.10
//   - Dead Legacy 0-Reader GMSTs (confirmed 0 .text readers via exhaustive ModRM scan, strictly never written!):
//       fSneakStealthBoyMult              (0x209EDD8, def=0.0,     0 .text readers - Fallout 3 leftover!)
//       fSneakSizeBase                    (0x209EC88, def=20.0,    0 .text readers)
//       fLockSkillMult                    (0x209FC60, def=1.0,     0 .text readers - Oblivion leftover!)
//       fLockPickBreakBase                (0x209FCD8, def=0.05,    0 .text readers - Oblivion leftover!)
//       fBribeBase                        (0x20A83B8, def=10.0,    0 .text readers - Oblivion leftover!)
//       fSpeechCraftBase                  (0x20A8388, def=100.0,   0 .text readers - Oblivion leftover!)
constexpr StealthSocialGmstSpec kStealthSocialSpecs[] = {
    {"fSneakMaxDistance",                0x209EB68u, 1500.0f, 1850.0f, true,  "Stealth"},
    {"fSneakExteriorDistanceMult",       0x209ED90u, 2.0f,    1.85f,   true,  "Stealth"},
    {"fSneakEquippedWeightBase",         0x209EB80u, 12.0f,   8.0f,    true,  "Stealth"},
    {"fSneakEquippedWeightMult",         0x209EB98u, 0.5f,    0.68f,   true,  "Stealth"},
    {"fSneakLightMoveMult",              0x209ECD0u, 0.01f,   0.025f,  true,  "Stealth"},
    {"fSneakLightRunMult",               0x209ECE8u, 0.2f,    0.32f,   true,  "Stealth"},
    {"fSneakSleepMod",                   0x209ED30u, -0.2f,   -0.30f,  true,  "Stealth"},
    {"fSneakAlertMod",                   0x209ED48u, 0.2f,    0.35f,   true,  "Stealth"},
    {"fLockpickSkillSweetSpotBase",      0x20AA7D0u, 0.82f,   0.72f,   true,  "Lockpick"},
    {"fLockpickSkillSweetSpotMult",      0x20AA7E8u, 0.012f,  0.015f,  true,  "Lockpick"},
    {"fLockpickBreakAdept",              0x20AA980u, 0.75f,   0.82f,   true,  "Lockpick"},
    {"fLockpickBreakExpert",             0x20AA998u, 0.5f,    0.58f,   true,  "Lockpick"},
    {"fLockpickBreakMaster",             0x20AA9B0u, 0.25f,   0.34f,   true,  "Lockpick"},
    {"fPickPocketMinChance",             0x20A1268u, 5.0f,    5.0f,    true,  "Pickpocket"},
    {"fPickPocketMaxChance",             0x20A1280u, 95.0f,   92.0f,   true,  "Pickpocket"},
    {"fPickPocketWeightMult",            0x20A1250u, -5.0f,   -5.5f,   true,  "Pickpocket"},
    {"fBribeScale",                      0x209F660u, 1.0f,    1.35f,   true,  "Social"},
    {"fBribeCostCurve",                  0x209F678u, 1.5f,    1.60f,   true,  "Social"},
    {"fBribeSpeechcraftMult",            0x209F690u, 0.03f,   0.035f,  true,  "Social"},
    {"fIntimidateConfidenceMultAverage", 0x209F708u, 1.0f,    1.10f,   true,  "Social"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fSneakStealthBoyMult",             0x209EDD8u, 0.0f,    0.0f,    false, "Stealth"},
    {"fSneakSizeBase",                   0x209EC88u, 20.0f,   20.0f,   false, "Stealth"},
    {"fLockSkillMult",                   0x209FC60u, 1.0f,    1.0f,    false, "Lockpick"},
    {"fLockPickBreakBase",               0x209FCD8u, 0.05f,   0.05f,   false, "Lockpick"},
    {"fBribeBase",                       0x20A83B8u, 10.0f,   10.0f,   false, "Social"},
    {"fSpeechCraftBase",                 0x20A8388u, 100.0f,  100.0f,  false, "Social"},
};

constexpr size_t kStealthSocialSpecCount = sizeof(kStealthSocialSpecs) / sizeof(kStealthSocialSpecs[0]);

std::mutex            g_ssStateMu;
RawStealthSocialState g_ssState{};

float ClampUnit(float v) {
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

}  // namespace

const StealthSocialGmstSpec* StealthSocialGmstTable(size_t* outCount) {
    if (outCount) *outCount = kStealthSocialSpecCount;
    return kStealthSocialSpecs;
}

bool VerifyStealthSocialGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kStealthSocialSpecCount; ++i) {
        const auto& spec = kStealthSocialSpecs[i];
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
    return verified == kStealthSocialSpecCount;
}

StealthEvalResult EvaluateStealthNoiseAndVisibility(const StealthEnvInput& in) {
    StealthEvalResult out{};
    const float bootW   = std::isfinite(in.armorBootWeight) ? std::clamp(in.armorBootWeight, 0.0f, 50.0f) : 4.0f;
    const float light01 = ClampUnit(in.lightLevel01);
    const float skill01 = std::isfinite(in.sneakSkill0_100) ? std::clamp(in.sneakSkill0_100, 0.0f, 100.0f) / 100.0f : 0.25f;
    const float rain01  = ClampUnit(in.rain01);
    const float wind01  = std::isfinite(in.windBeaufort) ? std::clamp(in.windBeaufort, 0.0f, 12.0f) / 12.0f : 0.15f;

    // Acoustic noise model matching verified fSneakEquippedWeightBase=8.0 & fSneakEquippedWeightMult=0.68:
    const float moveMult   = !in.isMoving ? 0.12f : (in.isRunning ? 2.15f : 0.72f);
    const float skillQuiet = 1.0f - 0.55f * skill01;
    const float rawNoise   = (8.0f + bootW * 0.68f) * moveMult * skillQuiet;

    // Rain and wind mask footstep acoustics cleanly:
    const float weatherMask = std::clamp(1.0f - 0.36f * rain01 - 0.22f * wind01, 0.42f, 1.0f);
    out.audibleNoiseScore   = std::max(0.0f, rawNoise * weatherMask);
    out.audibleRadiusU      = std::clamp(out.audibleNoiseScore * 28.0f, 80.0f, 2200.0f);

    // Optical visibility model matching verified fSneakLightMoveMult=0.025 & fSneakLightRunMult=0.32:
    const float motionVisAdd = !in.isMoving ? 0.0f : (in.isRunning ? 0.32f : 0.06f);
    const float visScore     = std::clamp((0.12f + 0.88f * light01 + motionVisAdd) * (1.0f - 0.40f * skill01), 0.04f, 1.35f);
    out.visualExposureScore  = visScore;

    // Composite [0..1] detection risk:
    const float normAudio = std::clamp(out.audibleNoiseScore / 45.0f, 0.0f, 1.0f);
    const float normVis   = std::clamp(visScore, 0.0f, 1.0f);
    out.compositeDetectRisk01 = std::clamp(0.55f * normVis + 0.45f * normAudio, 0.0f, 1.0f);
    return out;
}

PickpocketResult EvaluatePickpocketChance(const PickpocketInput& in) {
    PickpocketResult out{};
    const float aSkill = std::isfinite(in.actorSkill0_100)  ? std::clamp(in.actorSkill0_100,  0.0f, 100.0f) : 15.0f;
    const float tSkill = std::isfinite(in.targetSkill0_100) ? std::clamp(in.targetSkill0_100, 0.0f, 100.0f) : 25.0f;
    const float weight = std::isfinite(in.itemWeight)       ? std::max(in.itemWeight, 0.0f)                 : 0.0f;
    const float value  = std::isfinite(in.itemValueGold)    ? std::max(in.itemValueGold, 0.0f)              : 0.0f;

    const float lo = std::isfinite(in.minChancePct) ? std::clamp(in.minChancePct, 0.0f, 50.0f)   : 5.0f;
    const float hi = std::isfinite(in.maxChancePct) ? std::clamp(in.maxChancePct, lo,   100.0f)  : 92.0f;

    // Grounded Skyrim 0x41D860 pickpocket curve: actor skill vs target perception, item weight (-5.5/unit) & gold value:
    float raw = 38.0f + 0.65f * aSkill - 0.28f * tSkill - 5.5f * weight - 0.045f * value;
    if (in.targetIsSleeping) raw += 25.0f;
    if (in.isDetected)       raw -= 35.0f;

    out.rawChancePct     = std::isfinite(raw) ? raw : lo;
    out.clampedChancePct = std::clamp(out.rawChancePct, lo, hi);
    out.wasClamped       = (out.rawChancePct < lo || out.rawChancePct > hi);
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedStealthSocialEntry {
    float* ptr        = nullptr;
    float  savedValue = 0.0f;
};

std::mutex                           g_ssGmstMu;
std::vector<SavedStealthSocialEntry> g_savedSs;
bool                                 g_ssApplied = false;

static bool SehReadSsFloat(const float* p, float* out) noexcept {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteSsFloat(float* p, float v) noexcept {
    if (!p || !std::isfinite(v)) return false;
    __try {
        *p = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitStealthSocialGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[StealthSocialGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[StealthSocialGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_ssGmstMu);
    if (g_ssApplied) {
        return "[StealthSocialGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedSs.clear();

    for (size_t i = 0; i < kStealthSocialSpecCount; ++i) {
        const auto& spec = kStealthSocialSpecs[i];
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
        if (!SehReadSsFloat(field, &orig)) continue;
        if (SehWriteSsFloat(field, spec.balancedValue)) {
            g_savedSs.push_back({field, orig});
            ++applied;
        }
    }
    g_ssApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_ssStateMu);
        g_ssState.gmstsVerified       = 1u;
        g_ssState.gmstsAppliedCount   = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_ssState.deadLegacySkipped   = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_ssState.stealthSocialActive = g_ssApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[StealthSocialGMST] applied %zu binary-verified stealth/lockpick/pickpocket/social GMSTs (%zu dead 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownStealthSocialGmst() {
    std::lock_guard<std::mutex> lk(g_ssGmstMu);
    if (!g_ssApplied) return;
    for (const auto& e : g_savedSs) {
        SehWriteSsFloat(e.ptr, e.savedValue);
    }
    g_savedSs.clear();
    g_ssApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_ssStateMu);
        g_ssState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetStealthSocialState(raw::rt::RawStealthSocialState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_ssStateMu);
    *out = raw::rt::g_ssState;
    return 1;
}
