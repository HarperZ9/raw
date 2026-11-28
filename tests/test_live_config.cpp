// test_live_config.cpp: Wave 44/45 verification suite for Live Hot-Reloading Configuration (raw_engine.ini),
// F10 In-Game Runtime Diagnostics Dump, 100% live INI propagation across softbody/movement/stealth/survival,
// Directional Flanking & Low-HP Morale Combat AI, and BSPointerHandleManager 1M Ref-Cap Watchdog!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_LIVE_CONFIG_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_combat_ai.h"
#include "../src/engine_rt/rt_live_config.h"
#include "../src/engine_rt/rt_memory_guards.h"
#include "../src/engine_rt/rt_movement_combat.h"
#include "../src/engine_rt/rt_softbody.h"
#include "../src/engine_rt/rt_stealth_survival.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>

namespace raw::rt {

struct FlankingMoraleResult {
    float dotFacing  = 1.0f;
    float attackMult = 1.0f;
    float blockAdd   = 0.0f;
    bool  isFlanking = false;
    bool  lowMorale  = false;
};
FlankingMoraleResult EvaluateFlankingAndMorale(const float attackerPosXY[2],
                                               const float targetPosXY[2],
                                               float targetYawRad,
                                               float attackerHpFrac);

int RunLiveConfigTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s live_config::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path();
    const std::filesystem::path iniPath = tmpDir / "raw_w44_test_engine.ini";
    const std::filesystem::path dumpPath = tmpDir / "raw_w44_live_status_dump.txt";
    std::error_code ec;
    std::filesystem::remove(iniPath, ec);
    std::filesystem::remove(dumpPath, ec);

    // 1. Default INI auto-generation & faithful round-trip parsing (preserving user edits when present):
    {
        const bool created = EnsureDefaultLiveConfigFile(iniPath.string());
        const bool exists  = std::filesystem::exists(iniPath, ec);
        LiveEngineConfig loaded{};
        const bool parsed  = LoadLiveConfigFromFile(iniPath.string(), loaded);
        const LiveEngineConfig def = DefaultLiveEngineConfig();

        const bool defaultsMatch =
            std::fabs(loaded.parryWindowMs - def.parryWindowMs) < 1e-3f &&
            std::fabs(loaded.dodgeIFrameMs - def.dodgeIFrameMs) < 1e-3f &&
            std::fabs(loaded.dodgeStaminaCost - def.dodgeStaminaCost) < 1e-3f &&
            loaded.hitstopEnabled == def.hitstopEnabled &&
            std::fabs(loaded.hitstopScale - def.hitstopScale) < 1e-3f &&
            loaded.nonSpongyDifficulty == def.nonSpongyDifficulty &&
            loaded.tacticalSquadAi == def.tacticalSquadAi &&
            loaded.statusDumpHotkeyVK == 121u &&
            loaded.hotReloadIntervalFrames == 60u;

        {
            std::ofstream app(iniPath, std::ios::binary | std::ios::app);
            app << "; user custom marker\n";
        }
        EnsureDefaultLiveConfigFile(iniPath.string());
        std::ifstream in(iniPath, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const bool preserved = (ss.str().find("; user custom marker") != std::string::npos);

        check(created && exists && parsed && defaultsMatch && preserved,
              "default_ini_autogeneration_and_preservation");
    }

    // 2. Parse overrides & strict safe clamping on out-of-range / NaN / corrupt values:
    {
        const std::string wildIni =
            "[Combat]\n"
            "ParryWindowMs = 99999 ; out of range high -> clamp 450\n"
            "DodgeIFrameMs = -50   ; negative -> clamp 80\n"
            "DodgeStaminaCost = NaN ; corrupt -> fallback default 18\n"
            "DodgeIFramesEnabled = false\n"
            "HitstopEnabled = 0\n"
            "HitstopScale = 7.5    ; clamp 2.0\n"
            "NonSpongyDifficulty = off\n"
            "TacticalSquadAI = no\n"
            "[Physics]\n"
            "SkeletonXpbdEnabled = 1\n"
            "XpbdWindScale = 2.25\n"
            "FovBreathEnabled = 0\n"
            "FovBreathScale = -4.0 ; clamp 0.0\n"
            "[World]\n"
            "TfcLodVoidGuard = 1\n"
            "LivingEconomyEnabled = 0\n"
            "WeatherStealthEnabled = true\n"
            "ThermalSurvivalEnabled = bogus_value ; keep default true\n"
            "[Diagnostics]\n"
            "StatusDumpHotkeyVK = 999 ; clamp 254 (0xFE)\n"
            "HotReloadIntervalFrames = 2 ; clamp 10\n";

        LiveEngineConfig c{};
        const bool parsed = ParseLiveConfigIni(wildIni, c);
        const bool ok = parsed &&
            std::fabs(c.parryWindowMs - 450.0f) < 1e-3f &&
            std::fabs(c.dodgeIFrameMs - 80.0f) < 1e-3f &&
            std::fabs(c.dodgeStaminaCost - 18.0f) < 1e-3f &&
            !c.dodgeIFramesEnabled &&
            !c.hitstopEnabled &&
            std::fabs(c.hitstopScale - 2.0f) < 1e-3f &&
            !c.nonSpongyDifficulty &&
            !c.tacticalSquadAi &&
            c.skeletonXpbdEnabled &&
            std::fabs(c.xpbdWindScale - 2.25f) < 1e-3f &&
            !c.fovBreathEnabled &&
            std::fabs(c.fovBreathScale - 0.0f) < 1e-3f &&
            c.tfcLodVoidGuard &&
            !c.livingEconomyEnabled &&
            c.weatherStealthEnabled &&
            c.thermalSurvivalEnabled &&
            c.statusDumpHotkeyVK == 0xFEu &&
            c.hotReloadIntervalFrames == 10u;
        check(ok, "parse_overrides_and_corrupt_value_safe_clamping");
    }

    // 3. Hot-reload mtime/size change detection (sub-microsecond unchanged path + reload on edit):
    {
        std::filesystem::remove(iniPath, ec);
        LiveConfigHotReloader hr;
        hr.Init(iniPath.string(), true);

        const bool unchanged1 = !hr.PollReload(true);
        const uint32_t count0 = hr.ReloadCount();

        {
            std::ofstream out(iniPath, std::ios::binary | std::ios::trunc);
            out << "[Combat]\nParryWindowMs=310\nHitstopScale=1.4\n[Diagnostics]\nHotReloadIntervalFrames=15\n";
        }
        HANDLE hFile = CreateFileA(iniPath.string().c_str(), FILE_WRITE_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            FILETIME ft{};
            GetSystemTimeAsFileTime(&ft);
            ULARGE_INTEGER uli{};
            uli.LowPart  = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;
            uli.QuadPart += 30000000ull;
            ft.dwLowDateTime  = uli.LowPart;
            ft.dwHighDateTime = uli.HighPart;
            SetFileTime(hFile, nullptr, nullptr, &ft);
            CloseHandle(hFile);
        }

        const bool reloaded = hr.PollReload(true);
        const bool unchangedAfter = !hr.PollReload(true);
        const bool ok = unchanged1 && count0 == 0u && reloaded && hr.ReloadCount() == 1u && unchangedAfter &&
                        std::fabs(hr.Config().parryWindowMs - 310.0f) < 1e-3f &&
                        std::fabs(hr.Config().hitstopScale - 1.4f) < 1e-3f &&
                        hr.Config().hotReloadIntervalFrames == 15u;
        check(ok, "hot_reload_mtime_change_detection");
    }

    // 4. F10 Runtime Diagnostics Dump completeness verification:
    {
        LiveStatusDumpData d{};
        d.config              = DefaultLiveEngineConfig();
        d.config.parryWindowMs = 240.0f;
        d.configReloads       = 2u;
        d.iniPath             = iniPath.string();
        d.runtimeVersion      = "1.7.104.0";
        d.zydisPassed         = 41u;
        d.zydisChecked        = 41u;
        d.zydisOk             = true;
        d.skeletonHookActive  = true;
        d.skeletonActors      = 12u;
        d.skeletonIkBones     = 48u;
        d.skeletonXpbdBones   = 96u;
        d.gameplayHooksMask   = 0x1Fu;
        d.timedParries        = 7u;
        d.dodgesTriggered     = 9u;
        d.evadedHits          = 4u;
        d.hitstopVerified     = true;
        d.hitstopTriggers     = 11u;
        d.activeHoldName      = "Winterhold";
        d.economyBuyMult      = 1.42f;
        d.economySellMult     = 0.88f;
        d.sneakHookActive     = true;
        d.coreWarmth          = 64.5f;
        d.acousticRt60Sec     = 0.85f;
        d.calYear             = 201.0f;
        d.calMonth            = 11.0f;
        d.calDay              = 14.0f;
        d.calHour             = 22.5f;

        const bool written = WriteLiveStatusDumpToFile(dumpPath.string(), d);
        std::ifstream in(dumpPath, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string txt = ss.str();

        const bool hasAllSections =
            (txt.find("Zydis Binary Verification: 41/41 (OK - VERIFIED)") != std::string::npos) &&
            (txt.find("[Active Configuration]") != std::string::npos) &&
            (txt.find("ParryWindowMs=240") != std::string::npos) &&
            (txt.find("[Skeleton IK & 240Hz XPBD Soft-Body]") != std::string::npos) &&
            (txt.find("ActiveActors=12") != std::string::npos) &&
            (txt.find("[Combat, Parry, Dodge i-Frames, Hitstop & Camera FOV]") != std::string::npos) &&
            (txt.find("TimedParries=7") != std::string::npos) &&
            (txt.find("EvadedHits=4") != std::string::npos) &&
            (txt.find("[9-Hold Living Skyrim Economy & NPC Ecosystem]") != std::string::npos) &&
            (txt.find("ActiveHold=Winterhold") != std::string::npos) &&
            (txt.find("[Weather Stealth, Thermal Survival & 3D Acoustics]") != std::string::npos) &&
            (txt.find("SabineEyringRT60=0.85s") != std::string::npos) &&
            (txt.find("[Live Skyrim Calendar]") != std::string::npos) &&
            (txt.find("4E 201") != std::string::npos);

        check(written && hasAllSections, "f10_status_dump_completeness");
    }

    // 5. Wave 45: 100% Live INI Propagation across SoftBody, MovementCombat, Stealth & Thermal Survival (ZERO decorative keys!):
    {
        // A) SoftBody xpbdWindScale (0.0 vs 2.5):
        SkeletonPhysicsSetWind(120.0f, 0.0f, 0.0f, 0.8f);
        const TissueProfile cloakProf = DefaultTissueProfile(TissueKind::Cloak);
        const float parentPos[3] = {100.0f, 200.0f, 50.0f};

        LiveEngineConfig cfgZeroWind = DefaultLiveEngineConfig();
        cfgZeroWind.xpbdWindScale = 0.0f;
        SetLiveConfigSnapshot(cfgZeroWind);
        SoftBoneState sbZero{};
        SoftBoneOutput outZero{};
        for (int i = 0; i < 15; ++i) outZero = StepSoftBone(sbZero, parentPos, cloakProf, 1.0f / 60.0f);

        LiveEngineConfig cfgHighWind = DefaultLiveEngineConfig();
        cfgHighWind.xpbdWindScale = 2.5f;
        SetLiveConfigSnapshot(cfgHighWind);
        SoftBoneState sbHigh{};
        SoftBoneOutput outHigh{};
        for (int i = 0; i < 15; ++i) outHigh = StepSoftBone(sbHigh, parentPos, cloakProf, 1.0f / 60.0f);
        const bool windScaledOk = (std::fabs(outZero.localOffset[0]) < 1e-3f) && (outHigh.localOffset[0] > 0.5f);

        // B) MovementCombat: dodgeIFramesEnabled, dodgeIFrameMs=400, dodgeStaminaCost=30, fovBreathEnabled & fovBreathScale=1.5:
        LiveEngineConfig cfgNoDodge = DefaultLiveEngineConfig();
        cfgNoDodge.dodgeIFramesEnabled = false;
        cfgNoDodge.fovBreathEnabled    = false;
        SetLiveConfigSnapshot(cfgNoDodge);
        DodgeControllerState dcOff{};
        DodgeTriggerInput di{};
        di.nowMs = 1000ull; di.currentStamina = 100.0f; di.altEdge = true;
        const auto rOff = StepDodgeController(dcOff, di);

        FovSpringState fsOff{};
        FovStepInput fi{};
        fi.liveReadFovDeg = 80.0f; fi.sprinting = true; fi.dtSec = 1.0f / 60.0f;
        const auto foOff = StepFovBreathSpring(fsOff, fi);

        LiveEngineConfig cfgCustomDodge = DefaultLiveEngineConfig();
        cfgCustomDodge.dodgeIFrameMs    = 400.0f;
        cfgCustomDodge.dodgeStaminaCost = 30.0f;
        cfgCustomDodge.fovBreathScale   = 1.5f;
        SetLiveConfigSnapshot(cfgCustomDodge);
        DodgeControllerState dcCust{};
        di.currentStamina = 25.0f;
        const auto rCustLow = StepDodgeController(dcCust, di);
        di.currentStamina = 35.0f; di.nowMs = 1050ull;
        const auto rCustOk  = StepDodgeController(dcCust, di);
        const bool iFrame400 = IsDodgeEvadingAt(dcCust, 1440ull) && !IsDodgeEvadingAt(dcCust, 1455ull);

        FovSpringState fsScale{};
        float liveFov = 80.0f;
        FovStepOutput foScale{};
        for (int i = 0; i < 60; ++i) {
            fi.liveReadFovDeg = liveFov;
            foScale = StepFovBreathSpring(fsScale, fi);
            if (foScale.needWrite) liveFov = foScale.desiredWorldFovDeg;
        }
        const bool moveConfigOk =
            !rOff.triggered && (std::fabs(foOff.appliedOffsetDeg) < 1e-4f) &&
            !rCustLow.triggered && rCustLow.rejectedStamina &&
            rCustOk.triggered && std::fabs(rCustOk.staminaCost - 30.0f) < 1e-3f && iFrame400 &&
            std::fabs(foScale.appliedOffsetDeg - 6.3f) < 0.08f;

        // C) Stealth & Thermal Survival toggles:
        LiveEngineConfig cfgOffWorld = DefaultLiveEngineConfig();
        cfgOffWorld.weatherStealthEnabled  = false;
        cfgOffWorld.thermalSurvivalEnabled = false;
        SetLiveConfigSnapshot(cfgOffWorld);

        StealthDetectInput sIn{};
        sIn.baseScore = 60; sIn.soundScore = 40.0f; sIn.visualScore = 30.0f;
        sIn.distanceU = 500.0f; sIn.isExterior = true; sIn.isMoving = true; sIn.hasLOS = true;
        StealthEnvSnapshot sEnv{};
        sEnv.rain01 = 0.95f; sEnv.storm01 = 0.95f; sEnv.fog01 = 0.80f;
        const auto srOff = EvaluateWeatherMaskedStealth(sIn, sEnv);

        ThermalSurvivalState tsPrev{};
        tsPrev.coreWarmth = 75.0f;
        ThermalSurvivalInput tIn{};
        tIn.ambientTempC = -25.0f; tIn.windSpeedMs = 20.0f; tIn.snow01 = 1.0f; tIn.dtSec = 1.0f;
        const auto tsOff = StepThermalSurvival(tsPrev, tIn);

        // Restore defaults cleanly so other suites in ce_runtime_check always see default config!
        SetLiveConfigSnapshot(DefaultLiveEngineConfig());
        const auto srOn = EvaluateWeatherMaskedStealth(sIn, sEnv);
        const auto tsOn = StepThermalSurvival(tsPrev, tIn);

        const bool stealthThermalOk =
            (srOff.adjustedScore == 60 && std::fabs(srOff.soundMaskMult - 1.0f) < 1e-4f) &&
            (srOn.adjustedScore < 60 && srOn.soundMaskMult < 0.85f) &&
            (std::fabs(tsOff.coreWarmth - 75.0f) < 1e-4f && std::fabs(tsOff.staminaRegenMult - 1.0f) < 1e-4f) &&
            (tsOn.coreWarmth < 75.0f);

        check(windScaledOk && moveConfigOk && stealthThermalOk,
              "live_ini_propagates_to_all_subsystems");
    }

    // 6. Wave 45: Directional Flanking Bearing (+22% flank/rear) & Low-HP Morale (+28% block, -20% attack):
    {
        const float targetPos[2] = {0.0f, 0.0f};
        const float yawNorth = 0.0f;  // facing +Y (0, +1)

        const float frontPos[2] = {0.0f, 150.0f};    // dot = +1.0 (in front)
        const float flankPos[2] = {150.0f, 0.0f};    // dot =  0.0 (90 deg right flank)
        const float rearPos[2]  = {0.0f, -150.0f};   // dot = -1.0 (directly behind)

        const auto fmFront   = EvaluateFlankingAndMorale(frontPos, targetPos, yawNorth, 0.90f);
        const auto fmFlank   = EvaluateFlankingAndMorale(flankPos, targetPos, yawNorth, 0.90f);
        const auto fmRear    = EvaluateFlankingAndMorale(rearPos,  targetPos, yawNorth, 0.90f);
        const auto fmWounded = EvaluateFlankingAndMorale(frontPos, targetPos, yawNorth, 0.22f);

        const bool ok =
            (!fmFront.isFlanking && !fmFront.lowMorale && std::fabs(fmFront.dotFacing - 1.0f) < 1e-3f &&
             std::fabs(fmFront.attackMult - 1.0f) < 1e-3f && std::fabs(fmFront.blockAdd) < 1e-3f) &&
            (fmFlank.isFlanking && std::fabs(fmFlank.dotFacing) < 1e-3f &&
             std::fabs(fmFlank.attackMult - 1.22f) < 1e-3f) &&
            (fmRear.isFlanking && std::fabs(fmRear.dotFacing - (-1.0f)) < 1e-3f &&
             std::fabs(fmRear.attackMult - 1.22f) < 1e-3f) &&
            (!fmWounded.isFlanking && fmWounded.lowMorale &&
             std::fabs(fmWounded.attackMult - 0.80f) < 1e-3f &&
             std::fabs(fmWounded.blockAdd - 0.28f) < 1e-3f);
        check(ok, "flanking_bearing_and_low_hp_morale_combat_ai");
    }

    // 7. Wave 45: BSPointerHandleManager 1M Ref-Cap Saturation Watchdog threshold verification:
    {
        constexpr uint32_t kCap = 4096u;
        std::vector<uint8_t> table(static_cast<size_t>(kCap) * kRefHandleEntryStride, 0u);
        auto fillRatio = [&](uint32_t activeCount) {
            std::fill(table.begin(), table.end(), uint8_t{0});
            for (uint32_t i = 0; i < activeCount && i < kCap; ++i) {
                uint32_t bits = kRefHandleInUseBit | ((i & 0x3Fu) << 20);
                std::memcpy(table.data() + static_cast<size_t>(i) * kRefHandleEntryStride, &bits, 4u);
            }
        };

        fillRatio(820u);   // ~20.0% -> Healthy
        const auto rHealthy = SampleRefHandleSaturation(table.data(), kCap, 4u);
        fillRatio(2376u);  // ~58.0% -> Elevated
        const auto rElev    = SampleRefHandleSaturation(table.data(), kCap, 4u);
        fillRatio(3072u);  //  75.0% -> Warning
        const auto rWarn    = SampleRefHandleSaturation(table.data(), kCap, 4u);
        fillRatio(3686u);  // ~90.0% -> Critical
        const auto rCrit    = SampleRefHandleSaturation(table.data(), kCap, 4u);
        const auto rNull    = SampleRefHandleSaturation(nullptr, kCap, 4u);

        const bool ok =
            (rHealthy.level == RefHandleHealthLevel::Healthy && !rHealthy.warningTriggered && rHealthy.saturationPct > 19.0f && rHealthy.saturationPct < 21.0f) &&
            (rElev.level == RefHandleHealthLevel::Elevated && !rElev.warningTriggered && rElev.saturationPct > 56.0f && rElev.saturationPct < 60.0f) &&
            (rWarn.level == RefHandleHealthLevel::Warning && rWarn.warningTriggered && std::fabs(rWarn.saturationPct - 75.0f) < 0.5f && rWarn.estimatedActive == 3072u) &&
            (rCrit.level == RefHandleHealthLevel::Critical && rCrit.warningTriggered && rCrit.saturationPct > 89.0f) &&
            (rNull.sampledSlots == 0u && !rNull.warningTriggered);
        check(ok, "ref_handle_cap_watchdog_saturation_thresholds");
    }

    std::filesystem::remove(iniPath, ec);
    std::filesystem::remove(dumpPath, ec);
    return fail;
}

}  // namespace raw::rt

#ifndef RAW_LIVE_CONFIG_NO_MAIN
int main() {
    const int fail = raw::rt::RunLiveConfigTests();
    std::printf("live_config: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
