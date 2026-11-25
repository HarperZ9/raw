// rt_world_rules_gmst.cpp: Wave 50 Binary-Verified 1.7.104.0 Dragon Combat, Stagger Poise,
// Swim/Water Physics & Crime/Witness Realism GMSTs.
// Original work, MIT.
#include "rt_world_rules_gmst.h"
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

// Every single GMST RVA, on-disk float/int32 default, and .text reader verified directly against SkyrimSE.exe 1.7.104.0:
//   - Crime, Witness & Bounty Realism (fn:Crime::AlarmBroadcast 0x71ABB0, fn:TESFaction::InitCrimeValues 0x3B4550):
//       iCrimeAlarmRecDistance         (0x209FD50, int32 def=4000, reader 0x71AD1F) -> 2400
//       iCrimeAlarmLowRecDistance      (0x209FD68, int32 def=2000, reader 0x71ADDD) -> 1200
//       fCrimeAlarmRespMult            (0x209FD80, float def=1.7,  reader 0x41D96C) -> 1.35
//       iCrimeGoldAttack               (0x20A8D18, int32 def=40,   reader 0x3B45E8) -> 150
//       iCrimeGoldMurder               (0x20A8D30, int32 def=1000, reader 0x3B45DA) -> 2500
//       iCrimeGoldPickpocket           (0x20A8D60, int32 def=25,   reader 0x3B4614) -> 75
//       iCrimeGoldTrespass             (0x20A8D78, int32 def=5,    reader 0x267D14) -> 25
//       fCrimeGoldSteal                (0x20A8D48, float def=0.5,  reader 0x3B45F5) -> 0.85
//       fKillWitnessesTimerSetting     (0x20A07A0, float def=30.0, reader 0x7B75B4) -> 22.0
//   - Stagger & Poise Realism (fn:Combat::ComputeStagger 0x41F230, 0x7286FA):
//       fStaggerAttackBase             (0x20A6090, float def=0.5,  reader 0x41F335) -> 0.55
//       fStagger1WeapMult              (0x20A6138, float def=0.2,  reader 0x41F2FE) -> 0.28
//       fStagger2WeapMult              (0x20A6150, float def=0.5,  reader 0x41F309) -> 0.65
//       fStaggerBlockingMult           (0x20A6168, float def=1.0,  reader 0x41F3A1) -> 0.72
//       fStaggerMaxDuration            (0x20A6FA8, float def=3.0,  reader 0x7286FA) -> 2.25
//   - Swim / Water & Breath Physics (fn:Actor::ComputeSwimSpeed 0x420A20, fn:Actor::ComputeSwimBreathDuration 0x421400):
//       fMoveSwimMult                  (0x20AA458, float def=2.0,  readers 0x420A2A, 0x420A5A) -> 1.75
//       fActorSwimBreathBase           (0x20A8BE0, float def=10.0, reader  0x421410)           -> 18.0
//       fActorSwimBreathMult           (0x20A8BF8, float def=0.5,  reader  0x421408)           -> 0.65
//       fActorSwimBreathDamage         (0x20A8C10, float def=0.2,  reader  0x421428)           -> 0.25
//   - Dragon Combat (fn:Dragon::CheckLandingZoneClear 0x314630, fn:CombatAI::FlyingAttackChance 0x8F2490):
//       fDragonLandingZoneClearRadius  (0x207BDF0, float def=256.0, reader 0x314772) -> 200.0
//       fCombatFlyingAttackChanceMax   (0x20A29D8, float def=1.0,   reader 0x8F249A) -> 0.92
//   - Dead Legacy 0-Reader GMSTs (verified on disk via exhaustive ModRM scan, strictly never written!):
//       fCrimeSoundBase                (0x209F198, float def=1.0,   0 .text readers)
//       fCrimeSoundMult                (0x209F1B0, float def=1.0,   0 .text readers)
//       fStaggerBlockAttackBase        (0x20A61B0, float def=1.0,   0 .text readers)
//       fCombatShoutShortRecoveryTime  (0x20A3050, float def=10.0,  0 .text readers)
//       iDragonFastTravelMinDistance   (0x20A0440, int32 def=10000, 0 .text readers)
constexpr WorldRulesGmstSpec kWorldRulesSpecs[] = {
    {"iCrimeAlarmRecDistance",        0x209FD50u, true,  0.0f,   0.0f,  4000,  2400,  true,  "Crime"},
    {"iCrimeAlarmLowRecDistance",     0x209FD68u, true,  0.0f,   0.0f,  2000,  1200,  true,  "Crime"},
    {"fCrimeAlarmRespMult",           0x209FD80u, false, 1.7f,   1.35f, 0,     0,     true,  "Crime"},
    {"iCrimeGoldAttack",              0x20A8D18u, true,  0.0f,   0.0f,  40,    150,   true,  "Crime"},
    {"iCrimeGoldMurder",              0x20A8D30u, true,  0.0f,   0.0f,  1000,  2500,  true,  "Crime"},
    {"iCrimeGoldPickpocket",          0x20A8D60u, true,  0.0f,   0.0f,  25,    75,    true,  "Crime"},
    {"iCrimeGoldTrespass",            0x20A8D78u, true,  0.0f,   0.0f,  5,     25,    true,  "Crime"},
    {"fCrimeGoldSteal",               0x20A8D48u, false, 0.5f,   0.85f, 0,     0,     true,  "Crime"},
    {"fKillWitnessesTimerSetting",    0x20A07A0u, false, 30.0f,  22.0f, 0,     0,     true,  "Crime"},
    {"fStaggerAttackBase",            0x20A6090u, false, 0.5f,   0.55f, 0,     0,     true,  "Stagger"},
    {"fStagger1WeapMult",             0x20A6138u, false, 0.2f,   0.28f, 0,     0,     true,  "Stagger"},
    {"fStagger2WeapMult",             0x20A6150u, false, 0.5f,   0.65f, 0,     0,     true,  "Stagger"},
    {"fStaggerBlockingMult",          0x20A6168u, false, 1.0f,   0.72f, 0,     0,     true,  "Stagger"},
    {"fStaggerMaxDuration",           0x20A6FA8u, false, 3.0f,   2.25f, 0,     0,     true,  "Stagger"},
    {"fMoveSwimMult",                 0x20AA458u, false, 2.0f,   1.75f, 0,     0,     true,  "Water"},
    {"fActorSwimBreathBase",          0x20A8BE0u, false, 10.0f,  18.0f, 0,     0,     true,  "Water"},
    {"fActorSwimBreathMult",          0x20A8BF8u, false, 0.5f,   0.65f, 0,     0,     true,  "Water"},
    {"fActorSwimBreathDamage",        0x20A8C10u, false, 0.2f,   0.25f, 0,     0,     true,  "Water"},
    {"fDragonLandingZoneClearRadius", 0x207BDF0u, false, 256.0f, 200.0f,0,     0,     true,  "Dragon"},
    {"fCombatFlyingAttackChanceMax",  0x20A29D8u, false, 1.0f,   0.92f, 0,     0,     true,  "Dragon"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fCrimeSoundBase",               0x209F198u, false, 1.0f,   1.0f,  0,     0,     false, "Crime"},
    {"fCrimeSoundMult",               0x209F1B0u, false, 1.0f,   1.0f,  0,     0,     false, "Crime"},
    {"fStaggerBlockAttackBase",       0x20A61B0u, false, 1.0f,   1.0f,  0,     0,     false, "Stagger"},
    {"fCombatShoutShortRecoveryTime", 0x20A3050u, false, 10.0f,  10.0f, 0,     0,     false, "Dragon"},
    {"iDragonFastTravelMinDistance",  0x20A0440u, true,  0.0f,   0.0f,  10000, 10000, false, "Dragon"},
};

constexpr size_t kWorldRulesSpecCount = sizeof(kWorldRulesSpecs) / sizeof(kWorldRulesSpecs[0]);

std::mutex         g_wrStateMu;
RawWorldRulesState g_wrState{};

float ClampUnit(float v) {
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

}  // namespace

const WorldRulesGmstSpec* WorldRulesGmstTable(size_t* outCount) {
    if (outCount) *outCount = kWorldRulesSpecCount;
    return kWorldRulesSpecs;
}

bool VerifyWorldRulesGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kWorldRulesSpecCount; ++i) {
        const auto& spec = kWorldRulesSpecs[i];
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
    return verified == kWorldRulesSpecCount;
}

CrimeWitnessResult EvaluateCrimeWitnessRadius(const CrimeEnvInput& in) {
    CrimeWitnessResult out{};
    const float base = std::isfinite(in.baseAlarmRadiusU)
                           ? std::clamp(in.baseAlarmRadiusU, 400.0f, 8000.0f)
                           : 2400.0f;
    const float rain = in.inInterior ? 0.0f : ClampUnit(in.rain01);
    const float snow = in.inInterior ? 0.0f : ClampUnit(in.snow01);
    const float fog  = in.inInterior ? (ClampUnit(in.fog01) * 0.25f) : ClampUnit(in.fog01);
    const float dark = ClampUnit(in.nightDarkness01);
    const float wind = in.inInterior ? 0.0f : (std::isfinite(in.windBeaufort) ? std::clamp(in.windBeaufort, 0.0f, 12.0f) : 2.0f);
    const float wind01 = wind / 12.0f;

    // Optical sight attenuation (fog, blizzard, darkness, rain) vs acoustic masking (rain, wind, blizzard):
    const float sightFactor = std::clamp(1.0f - (0.38f * snow + 0.32f * fog + 0.30f * dark + 0.18f * rain), 0.22f, 1.0f);
    const float soundFactor = std::clamp(1.0f - (0.34f * rain + 0.30f * wind01 + 0.24f * snow), 0.28f, 1.0f);

    const float stealthScale = in.isStealthCrime ? 0.55f : 1.0f;
    const float interiorScale = in.inInterior ? 0.72f : 1.0f;

    out.effectiveSightRadiusU = std::max(320.0f, base * sightFactor * stealthScale * interiorScale);
    out.effectiveSoundRadiusU = std::max(320.0f, base * soundFactor * stealthScale * interiorScale);
    out.effectiveAlarmRadiusU = std::max(320.0f, 0.55f * out.effectiveSightRadiusU + 0.45f * out.effectiveSoundRadiusU);
    out.attenuationFactor01   = std::clamp(out.effectiveAlarmRadiusU / std::max(base, 1.0f), 0.15f, 1.0f);
    return out;
}

WaterBreathResult EvaluateWaterSurvivalBreath(float waterTempC, float stamina) {
    WaterBreathResult out{};
    const float tempC = std::isfinite(waterTempC) ? std::clamp(waterTempC, -2.0f, 35.0f) : 12.0f;
    const float stam  = std::isfinite(stamina) ? std::clamp(stamina, 50.0f, 600.0f) : 100.0f;

    // Temperate water baseline coupled with actor endurance (stamina):
    const float baseBreathSec = 18.0f + stam * 0.045f;

    // Cold-shock response below 15 C: glacial 0 C Sea of Ghosts water triggers gasp reflex & rapid O2 consumption!
    const float coldFrac = std::clamp((15.0f - tempC) / 15.0f, 0.0f, 1.0f);
    const float coldScale = 1.0f - 0.58f * coldFrac;  // 1.0x at >= 15 C down to 0.42x at <= 0 C

    out.coldShockFactor01 = coldFrac;
    out.breathDurationSec = std::clamp(baseBreathSec * coldScale, 6.0f, 55.0f);
    out.drowningDpsFrac   = std::clamp(0.22f + 0.18f * coldFrac, 0.22f, 0.42f);
    return out;
}

void WorldRulesUpdateEnvironment(const CrimeEnvInput& crimeEnv, float waterTempC, float stamina) {
    const CrimeWitnessResult cw = EvaluateCrimeWitnessRadius(crimeEnv);
    const WaterBreathResult  wb = EvaluateWaterSurvivalBreath(waterTempC, stamina);
    std::lock_guard<std::mutex> lk(g_wrStateMu);
    g_wrState.effectiveWitnessRadius = cw.effectiveAlarmRadiusU;
    g_wrState.waterBreathSec         = wb.breathDurationSec;
    g_wrState.coldShockFactor01      = wb.coldShockFactor01;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedWorldRuleEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                       g_wrGmstMu;
std::vector<SavedWorldRuleEntry> g_savedWr;
bool                             g_wrApplied = false;

static bool SehReadU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitWorldRulesGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[WorldRulesGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[WorldRulesGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_wrGmstMu);
    if (g_wrApplied) {
        return "[WorldRulesGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedWr.clear();

    for (size_t i = 0; i < kWorldRulesSpecCount; ++i) {
        const auto& spec = kWorldRulesSpecs[i];
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
        if (!SehReadU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteU32(field, target32)) {
            g_savedWr.push_back({field, orig32});
            ++applied;
        }
    }
    g_wrApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_wrStateMu);
        g_wrState.gmstsVerified     = 1u;
        g_wrState.gmstsAppliedCount = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_wrState.deadLegacySkipped = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_wrState.worldRulesActive  = g_wrApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[WorldRulesGMST] applied %zu binary-verified crime/stagger/water/dragon GMSTs (%zu dead 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownWorldRulesGmst() {
    std::lock_guard<std::mutex> lk(g_wrGmstMu);
    if (!g_wrApplied) return;
    for (const auto& e : g_savedWr) {
        SehWriteU32(e.ptr, e.saved32);
    }
    g_savedWr.clear();
    g_wrApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_wrStateMu);
        g_wrState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetWorldRulesState(raw::rt::RawWorldRulesState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_wrStateMu);
    *out = raw::rt::g_wrState;
    return 1;
}
